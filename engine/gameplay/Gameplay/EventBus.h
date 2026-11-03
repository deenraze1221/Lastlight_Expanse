#pragma once
#include <algorithm>
#include <map>
#include <functional>
#include <string>
#include <string_view>
#include <stdexcept>
#include <memory>
#include <span>
#include <typeindex>
#include <vector>
#include <utility>

namespace zodar {
// Main-thread typed values; no callbacks or cross-type delivery order. Read spans remain
// valid until advance(). Emission never invalidates the currently readable batch.
class EventBus {
    struct Base { virtual ~Base() = default; virtual void advance() = 0; virtual bool empty() const = 0; };
    template<class T> struct Channel final : Base {
        std::vector<T> current, pending;
        void advance() override { current.clear(); current.swap(pending); }
        bool empty() const override { return current.empty() && pending.empty(); }
    };
    struct Codec {
        std::type_index type;
        std::function<std::pair<std::vector<std::string>,std::vector<std::string>>(const Base&)> encode;
        std::function<std::unique_ptr<Base>(const std::vector<std::string>&,const std::vector<std::string>&)> decode;
    };
public:
    struct Batch { std::string name; std::vector<std::string> current, pending; };
    using Snapshot = std::vector<Batch>;
    template<class T> void registerType(std::string name, std::function<std::string(const T&)> encode,
                                        std::function<T(std::string_view)> decode) {
        if (name.empty() || !encode || !decode || m_codecs.contains(name)) throw std::invalid_argument("invalid event codec");
        for (const auto& [key, c] : m_codecs) { (void)key; if (c.type == typeid(T)) throw std::invalid_argument("duplicate event type"); }
        m_codecs.emplace(std::move(name), Codec{typeid(T),
            [encode](const Base& base) {
                const auto& c = static_cast<const Channel<T>&>(base);
                std::pair<std::vector<std::string>,std::vector<std::string>> out;
                for (const auto& v : c.current) out.first.push_back(encode(v));
                for (const auto& v : c.pending) out.second.push_back(encode(v));
                return out;
            },
            [decode](const auto& current, const auto& pending) -> std::unique_ptr<Base> {
                auto c = std::make_unique<Channel<T>>();
                for (const auto& v : current) c->current.push_back(decode(v));
                for (const auto& v : pending) c->pending.push_back(decode(v));
                return c;
            }});
    }
    Snapshot capture() const {
        Snapshot out;
        for (const auto& [type, channel] : m_channels) {
            bool found = false;
            for (const auto& [name, codec] : m_codecs) if (codec.type == type) {
                const auto values = codec.encode(*channel);
                out.push_back({name, values.first, values.second}); found = true; break;
            }
            if (!found && !channel->empty()) throw std::runtime_error("pending event type has no save codec");
        }
        std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
        return out;
    }
    EventBus stage(const Snapshot& snapshot) const {
        EventBus staged; staged.m_codecs = m_codecs;
        for (const auto& b : snapshot) {
            const auto& codec = m_codecs.at(b.name);
            if (staged.m_channels.contains(codec.type)) throw std::runtime_error("duplicate event channel");
            staged.m_channels.emplace(codec.type, codec.decode(b.current, b.pending));
        }
        return staged;
    }
    void commit(EventBus&& staged) noexcept { m_channels.swap(staged.m_channels); }
    template<class T> void emit(T value) {
        auto& channel = m_channels[std::type_index(typeid(T))];
        if (!channel) channel = std::make_unique<Channel<T>>();
        static_cast<Channel<T>&>(*channel).pending.push_back(std::move(value));
    }
    template<class T> std::span<const T> read() const {
        const auto it = m_channels.find(std::type_index(typeid(T)));
        if (it == m_channels.end()) return {};
        return static_cast<const Channel<T>&>(*it->second).current;
    }
    // Runtime calls once after timers at the end of each successful tick.
    void advance() { for (auto& [type, channel] : m_channels) { (void)type; channel->advance(); } }
private:
    std::map<std::type_index, std::unique_ptr<Base>> m_channels;
    std::map<std::string, Codec> m_codecs;
};
} // namespace zodar
