#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace zodar {
// SplitMix64, one independent stream per name. No global state or distribution-library
// dependence. Access on the simulation thread; serialize seed/state for save continuation.
class Random {
public:
    explicit Random(uint64_t seed = 0) : m_seed(seed) {}
    uint64_t next(std::string_view name) {
        uint64_t hash=14695981039346656037ull;
        for(unsigned char c:name) { hash^=c; hash*=1099511628211ull; }
        auto [it,inserted]=m_states.try_emplace(std::string(name),m_seed^hash);
        (void)inserted;
        uint64_t z=(it->second+=0x9e3779b97f4a7c15ull);
        z=(z^(z>>30))*0xbf58476d1ce4e5b9ull;
        z=(z^(z>>27))*0x94d049bb133111ebull;
        return z^(z>>31);
    }
    double unit(std::string_view name) { return static_cast<double>(next(name)>>11)*0x1.0p-53; }
    uint64_t seed() const { return m_seed; }
    const std::map<std::string,uint64_t>& states() const { return m_states; }
    void restore(uint64_t seed, std::map<std::string,uint64_t> states) { m_seed=seed; m_states=std::move(states); }
private:
    uint64_t m_seed;
    std::map<std::string,uint64_t> m_states;
};
} // namespace zodar
