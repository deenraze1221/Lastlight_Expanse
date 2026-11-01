#include "Core/AtomicFile.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <system_error>
#include <vector>
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
void writeFileAtomically(const std::filesystem::path& destination, std::string_view bytes) {
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
        if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING))
            throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),"replace destination");
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
        if(::rename(name.data(),destination.c_str())<0) throw std::system_error(errno,std::generic_category(),"replace destination");
    } catch(...) {
        if(fd>=0) ::close(fd);
        ::unlink(name.data()); throw;
    }
#endif
}
}
