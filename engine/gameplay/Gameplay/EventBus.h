#pragma once
#include <map>
#include <memory>
#include <span>
#include <typeindex>
#include <vector>
#include <utility>

namespace zodar {
// Main-thread typed values; no callbacks or cross-type delivery order. Read spans remain
// valid until advance(). Emission never invalidates the currently readable batch.
class EventBus {
    struct Base { virtual ~Base() = default; virtual void advance() = 0; };
    template<class T> struct Channel final : Base {
        std::vector<T> current, pending;
        void advance() override { current.clear(); current.swap(pending); }
    };
public:
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
};
} // namespace zodar
