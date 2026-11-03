#pragma once
#include <filesystem>
#include <string_view>
#include <span>
namespace zodar {
// Write a newly created exclusive sibling file, then atomically replace the destination.
// Existing files/symlinks at predictable temporary names are never opened or removed.
// Does not provide fsync/power-loss durability or multi-file transactions.
void writeFileAtomically(const std::filesystem::path& destination, std::string_view bytes);
struct AtomicFileWrite { std::filesystem::path destination; std::string_view bytes; };
// Prepare all replacement/original copies before publishing in order. On an ordinary
// failure, restore every published destination. Destinations must be distinct regular
// files (or absent); concurrent mutation, symlinks and crash durability are unsupported.
// If rollback itself fails, retain originals and report their recovery paths.
void writeFilesAtomically(std::span<const AtomicFileWrite> writes);
}
