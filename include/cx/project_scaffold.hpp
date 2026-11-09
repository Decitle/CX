#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace cx {

struct ProjectScaffoldResult {
    bool recognizedProject{false};
    bool changed{false};
    std::string error;
    std::vector<std::filesystem::path> createdPaths;
};

// Creates only missing, non-destructive project basics. An empty directory is
// treated as a request for a new project; an existing project is recognized by
// at least one script under scripts/ or one unambiguous root-level .cx file.
// Existing user files are never overwritten.
ProjectScaffoldResult ensureProjectScaffold(const std::filesystem::path& projectRoot);

} // namespace cx
