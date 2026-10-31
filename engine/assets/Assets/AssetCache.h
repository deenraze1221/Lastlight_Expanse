#pragma once

#include "Core/Error.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Generic reference-counted asset cache (Phase C). Renderer independent: the GPU AssetManager
// (Zodar::AssetManager) instantiates it for textures, models and materials; tests use plain
// types.
//
//   AssetCache<Mesh> cache(retire);                       // retire: deferred destruction
//   AssetHandle<Mesh> a = cache.getOrLoad("rock.glb", [] { return std::make_unique<Mesh>(...); });
//   AssetHandle<Mesh> b = cache.getOrLoad("rock.glb", ...); // same asset, not loaded again
//   a = {}; b = {};                                        // last handle: retire(value), entry gone
//
// Lifetime: a handle shares ownership of the asset. When the last handle is dropped, the
// entry leaves the cache and the value is passed to the retire function (the GPU manager
// queues it on the device deletion queue, so frames in flight can still read it). Failed
// loads are cached like successful ones while handles exist (state Failed + message), so a
// broken file is not retried every frame; dropping every handle allows a fresh attempt.
// Async loads (Phase C2): getOrCreatePending() returns a Loading entry that the loader later
// completes with resolve() or reject(); the handle is shared and cached meanwhile.
// Not thread safe: use from one thread (the main/render thread).
namespace zodar {

enum class AssetState : uint8_t { Loading, Ready, Failed };

template <class T>
struct AssetRecord {
    std::string key;
    AssetState state = AssetState::Failed;
    std::unique_ptr<T> value;
    std::string error; // Failed: why (exception message of the loader)
};

template <class T>
class AssetCache;

template <class T>
class AssetHandle {
public:
    AssetHandle() = default;

    bool valid() const { return m_record != nullptr; }
    bool ready() const { return m_record && m_record->state == AssetState::Ready; }
    bool failed() const { return m_record && m_record->state == AssetState::Failed; }
    bool loading() const { return m_record && m_record->state == AssetState::Loading; }
    AssetState state() const { return record().state; }
    explicit operator bool() const { return ready(); }

    const std::string& key() const { return record().key; }
    // Empty unless failed().
    const std::string& error() const { return record().error; }

    // The asset; throws zodar::Error if the handle is empty, still loading or failed.
    T& get() const {
        const AssetRecord<T>& r = record();
        if (r.state == AssetState::Loading) {
            fail("asset '" + r.key + "' is still loading");
        }
        if (r.state != AssetState::Ready) {
            fail("asset '" + r.key + "' failed to load: " + r.error);
        }
        return *r.value;
    }
    T* operator->() const { return &get(); }
    T& operator*() const { return get(); }

    long useCount() const { return m_record.use_count(); }
    bool operator==(const AssetHandle& other) const { return m_record == other.m_record; }

private:
    friend class AssetCache<T>;
    explicit AssetHandle(std::shared_ptr<AssetRecord<T>> record) : m_record(std::move(record)) {}

    const AssetRecord<T>& record() const {
        if (!m_record) {
            fail("empty asset handle");
        }
        return *m_record;
    }

    std::shared_ptr<AssetRecord<T>> m_record;
};

template <class T>
class AssetCache {
public:
    // Receives the value of an asset whose last handle was dropped. Empty: destroy at once.
    using Retire = std::function<void(std::unique_ptr<T>)>;

    struct Stats {
        uint64_t loads = 0;    // load functions run (successful or not)
        uint64_t hits = 0;     // getOrLoad calls answered from the cache
        uint64_t failures = 0; // loads that threw
        uint64_t released = 0; // entries whose last handle was dropped
    };

    explicit AssetCache(Retire retire = {}) : m_shared(std::make_shared<Shared>()) {
        m_shared->retire = std::move(retire);
    }

    AssetCache(const AssetCache&) = delete;
    AssetCache& operator=(const AssetCache&) = delete;

    // Handle for `key`, or an empty handle when it is not loaded.
    AssetHandle<T> find(const std::string& key) const {
        const auto it = m_shared->entries.find(key);
        if (it == m_shared->entries.end()) {
            return {};
        }
        return AssetHandle<T>(it->second.lock());
    }

    // Returns the cached asset for `key`, or runs `load` (returns std::unique_ptr<T>; may throw)
    // and caches the result. A throwing or null-returning loader yields a Failed handle.
    template <class Load>
    AssetHandle<T> getOrLoad(const std::string& key, Load&& load) {
        if (AssetHandle<T> cached = find(key); cached.valid()) {
            ++m_shared->stats.hits;
            return cached;
        }
        auto* record = new AssetRecord<T>{key, AssetState::Failed, nullptr, {}};
        std::shared_ptr<AssetRecord<T>> owned(record, Deleter{m_shared});
        ++m_shared->stats.loads;
        try {
            record->value = std::forward<Load>(load)();
            if (record->value) {
                record->state = AssetState::Ready;
            } else {
                record->error = "loader returned no asset";
            }
        } catch (const std::exception& e) {
            record->error = e.what();
        }
        if (record->state == AssetState::Failed) {
            ++m_shared->stats.failures;
        }
        m_shared->entries[key] = owned;
        return AssetHandle<T>(std::move(owned));
    }

    // Async: the cached entry for `key` (any state; `created` = false), or a new Loading entry
    // (`created` = true) that the caller must complete with resolve() or reject().
    AssetHandle<T> getOrCreatePending(const std::string& key, bool& created) {
        if (AssetHandle<T> cached = find(key); cached.valid()) {
            ++m_shared->stats.hits;
            created = false;
            return cached;
        }
        created = true;
        std::shared_ptr<AssetRecord<T>> owned(new AssetRecord<T>{key, AssetState::Loading, nullptr, {}},
                                              Deleter{m_shared});
        ++m_shared->stats.loads;
        m_shared->entries[key] = owned;
        return AssetHandle<T>(std::move(owned));
    }

    // Completes a Loading entry. A null value rejects it. No effect on other states.
    void resolve(const AssetHandle<T>& handle, std::unique_ptr<T> value) {
        if (!handle.loading()) {
            return;
        }
        if (!value) {
            reject(handle, "loader returned no asset");
            return;
        }
        handle.m_record->value = std::move(value);
        handle.m_record->state = AssetState::Ready;
    }
    void reject(const AssetHandle<T>& handle, std::string error) {
        if (!handle.loading()) {
            return;
        }
        handle.m_record->error = std::move(error);
        handle.m_record->state = AssetState::Failed;
        ++m_shared->stats.failures;
    }

    // Entries with at least one live handle.
    size_t size() const { return m_shared->entries.size(); }
    std::vector<std::string> keys() const {
        std::vector<std::string> result;
        result.reserve(m_shared->entries.size());
        for (const auto& [key, entry] : m_shared->entries) {
            result.push_back(key);
        }
        return result;
    }
    const Stats& stats() const { return m_shared->stats; }

    // Replaces the retire function (e.g. destroy at once after the device went idle).
    void setRetire(Retire retire) { m_shared->retire = std::move(retire); }

private:
    struct Shared {
        Retire retire;
        std::unordered_map<std::string, std::weak_ptr<AssetRecord<T>>> entries;
        Stats stats;
    };

    // Runs when the last handle goes away; holds the shared state, so it stays valid even if
    // the cache object itself was destroyed first.
    struct Deleter {
        std::shared_ptr<Shared> shared;
        void operator()(AssetRecord<T>* record) const {
            std::unique_ptr<AssetRecord<T>> owned(record);
            const auto it = shared->entries.find(owned->key);
            if (it != shared->entries.end() && it->second.expired()) {
                shared->entries.erase(it);
            }
            ++shared->stats.released;
            if (owned->value && shared->retire) {
                shared->retire(std::move(owned->value));
            }
        }
    };

    std::shared_ptr<Shared> m_shared;
};

} // namespace zodar
