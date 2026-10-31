#pragma once

#include <cstdint>
#include <limits>

namespace zodar {

// Opaque entity handle (value type). The id packs an index and a version; a destroyed
// entity's handle stays invalid even after its index is reused. Default-constructed and
// Entity::null() handles never refer to a live entity. Handles are only meaningful for the
// World that created them.
class Entity {
public:
    using Id = std::uint32_t;

    constexpr Entity() noexcept = default;
    constexpr explicit Entity(Id id) noexcept : m_id(id) {}

    [[nodiscard]] static constexpr Entity null() noexcept { return Entity{}; }

    [[nodiscard]] constexpr Id id() const noexcept { return m_id; }
    [[nodiscard]] constexpr bool isNull() const noexcept { return m_id == kNullId; }
    constexpr explicit operator bool() const noexcept { return !isNull(); }

    friend constexpr bool operator==(Entity, Entity) noexcept = default;

    static constexpr Id kNullId = std::numeric_limits<Id>::max();

private:
    Id m_id = kNullId;
};

} // namespace zodar
