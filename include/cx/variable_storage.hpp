#pragma once

#include "cx/value.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace cx {

// Persistent globals are mirrored in a human-editable CSV and a compressed binary cache.
// A present CSV is the source of truth (including deliberate row deletions); malformed rows
// may be skipped with a warning rather than being silently imported from the binary cache.
// variables.meta identifies the last complete CSV/cache generation and gates cache recovery.
// Storage boundary for persistent global variables. The rest of Runtime works with
// typed Values; this class converts them to/from the on-disk CSV/cache formats.
// Saving serializes cooperating engine writers per project and flushes each atomic file
// replacement before returning. A versioned variables.meta record is committed after both
// data files; it verifies cache bytes before a missing CSV may be reconstructed. CSV and cache
// remain sequential commits, not one indivisible transaction. Saving never deletes an
// unexpected destination to force a replacement.
class VariableStorage {
public:
    using VariableMap = std::unordered_map<std::string, Value>;

    [[nodiscard]] static bool load(const std::filesystem::path& projectRoot,
                                   VariableMap& destination, std::string& warning);
    [[nodiscard]] static bool save(const std::filesystem::path& projectRoot,
                                   const VariableMap& variables, std::string& error);
};

} // namespace cx
