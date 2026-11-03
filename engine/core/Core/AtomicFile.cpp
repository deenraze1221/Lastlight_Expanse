#include "Core/AtomicFile.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <system_error>
#include <vector>
#include <fstream>
#include <functional>
#include <memory>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <random>
#else
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#endif

namespace zodar {
namespace {
std::filesystem::path prepareFile(const std::filesystem::path& destination, std::string_view bytes) {
#ifdef _WIN32
    std::filesystem::path temporary;
    HANDLE handle=INVALID_HANDLE_VALUE;
    std::random_device random;
    for(unsigned attempt=0;attempt<128;++attempt) {
        temporary=destination;
        temporary+=L".tmp."+std::to_wstring(random())+L"."+std::to_wstring(random());
        handle=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle!=INVALID_HANDLE_VALUE) break;
        const auto error=GetLastError();
        if(error!=ERROR_FILE_EXISTS && error!=ERROR_ALREADY_EXISTS)
            throw std::system_error(static_cast<int>(error),std::system_category(),"create exclusive temporary file");
    }
    if(handle==INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create exclusive temporary file");
    try {
        while(!bytes.empty()) {
            const auto count=static_cast<DWORD>(std::min<size_t>(bytes.size(),64*1024)); DWORD written=0;
            if(!WriteFile(handle,bytes.data(),count,&written,nullptr) || written==0)
                throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"write temporary file");
            bytes.remove_prefix(written);
        }
        const auto closing=handle; handle=INVALID_HANDLE_VALUE;
        if(!CloseHandle(closing)) throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"close temporary file");
        return temporary;
    } catch(...) {
        if(handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(temporary.c_str()); throw;
    }
#else
    auto pattern=destination.string()+".tmp.XXXXXX";
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back('\0');
    int fd=mkstemp(name.data()); // O_CREAT|O_EXCL, mode 0600; does not follow an existing symlink
    if(fd<0) throw std::system_error(errno,std::generic_category(),"create exclusive temporary file");
    try {
        if(fcntl(fd,F_SETFD,FD_CLOEXEC)<0) throw std::system_error(errno,std::generic_category(),"protect temporary descriptor");
        while(!bytes.empty()) {
            const auto count=::write(fd,bytes.data(),std::min<size_t>(bytes.size(),64*1024));
            if(count<0 && errno==EINTR) continue;
            if(count<=0) throw std::system_error(count<0 ? errno : EIO,std::generic_category(),"write temporary file");
            bytes.remove_prefix(static_cast<size_t>(count));
        }
        const int closing=fd; fd=-1;
        if(::close(closing)<0) throw std::system_error(errno,std::generic_category(),"close temporary file");
        return std::filesystem::path(name.data());
    } catch(...) {
        if(fd>=0) ::close(fd);
        ::unlink(name.data()); throw;
    }
#endif
}

void replaceFile(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING))
        throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"replace destination");
#else
    if (::rename(temporary.c_str(),destination.c_str())<0)
        throw std::system_error(errno,std::generic_category(),"replace destination");
#endif
}
struct Temporary {
    std::filesystem::path path;
    Temporary(const std::filesystem::path& destination, std::string_view bytes) : path(prepareFile(destination,bytes)) {}
    ~Temporary() { if (!path.empty()) { std::error_code ignored; std::filesystem::remove(path,ignored); } }
    void publish(const std::filesystem::path& destination) { replaceFile(path,destination); path.clear(); }
};
}
void writeFileAtomically(const std::filesystem::path& destination, std::string_view bytes) {
    Temporary temporary(destination,bytes);
    temporary.publish(destination);
}
namespace detail {
// Internal fault-injection seam: tests throw at a specific publication boundary. The
// public API has no hook; rollback always uses the real filesystem operation.
void writeFilesAtomically(std::span<const AtomicFileWrite> writes,
                          const std::function<void(size_t)>& beforeReplace) {
    struct Change {
        std::filesystem::path destination;
        std::unique_ptr<Temporary> replacement, original;
        std::filesystem::path recovery;
        bool rollbackFailed=false;
    };
    std::vector<Change> changes;
    changes.reserve(writes.size());
    std::set<std::filesystem::path> destinations;
    for (const auto& write:writes) {
        if (!destinations.insert(std::filesystem::weakly_canonical(write.destination)).second)
            throw std::invalid_argument("duplicate atomic transaction destination");
        Change change{write.destination,{},{},{},false};
        const auto status=std::filesystem::symlink_status(write.destination);
        if (std::filesystem::exists(status)) {
            if (!std::filesystem::is_regular_file(status))
                throw std::invalid_argument("save destination must be a regular file: "+write.destination.string());
            std::ifstream in(write.destination,std::ios::binary);
            in.exceptions(std::ios::badbit);
            if (!in) throw std::runtime_error("cannot read previous file: "+write.destination.string());
            const std::string previous(std::istreambuf_iterator<char>(in),{});
            change.original=std::make_unique<Temporary>(write.destination,previous);
        }
        change.replacement=std::make_unique<Temporary>(write.destination,write.bytes);
        changes.push_back(std::move(change));
    }
    size_t published=0;
    try {
        for (;published<changes.size();++published) {
            if (beforeReplace) beforeReplace(published);
            changes[published].replacement->publish(changes[published].destination);
        }
    } catch (...) {
        const auto failure=std::current_exception();
        bool rollbackFailed=false;
        while (published) {
            auto& change=changes[--published];
            try {
                if (change.original) change.original->publish(change.destination);
                else std::filesystem::remove(change.destination);
            } catch (...) {
                rollbackFailed=true; change.rollbackFailed=true;
                // Detach ownership without allocation before constructing diagnostics.
                // Even low-memory errors must not delete the only recovery copy.
                if (change.original) change.recovery.swap(change.original->path);
            }
        }
        if (rollbackFailed) {
            std::string recovery;
            for (const auto& change:changes) if (change.rollbackFailed) {
                recovery+=change.destination.string();
                if (!change.recovery.empty()) recovery+="; original retained at "+change.recovery.string();
                recovery+="\n";
            }
            throw std::runtime_error("atomic transaction rollback failed:\n"+recovery);
        }
        std::rethrow_exception(failure);
    }
}
}
void writeFilesAtomically(std::span<const AtomicFileWrite> writes) { detail::writeFilesAtomically(writes,{}); }
}
