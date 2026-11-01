#pragma once
#include <filesystem>
#include <string_view>
namespace zodar {
// Write a newly created exclusive sibling file, then atomically replace the destination.
// Existing files/symlinks at predictable temporary names are never opened or removed.
// Does not provide fsync/power-loss durability or multi-file transactions.
void writeFileAtomically(const std::filesystem::path& destination, std::string_view bytes);
}
