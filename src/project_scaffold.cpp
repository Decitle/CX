#include "cx/project_scaffold.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>
#include <vector>

namespace cx {
namespace {

bool isCxFile(const std::filesystem::directory_entry& entry, std::error_code& ec) {
    if (!entry.is_regular_file(ec) || ec) return false;
    auto extension = entry.path().extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".cx";
}

bool hasCxScript(const std::filesystem::path& scriptsDirectory, std::error_code& ec) {
    ec.clear();
    const bool exists = std::filesystem::exists(scriptsDirectory, ec);
    if (ec || !exists) return false;
    if (!std::filesystem::is_directory(scriptsDirectory, ec) || ec) return false;
    for (std::filesystem::recursive_directory_iterator it(scriptsDirectory, ec), end;
         it != end && !ec; it.increment(ec)) {
        if (isCxFile(*it, ec)) return true;
        if (ec) return false;
    }
    return false;
}

bool writeIfMissing(const std::filesystem::path& path, const std::string& contents,
                    ProjectScaffoldResult& result, std::string& error) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) return !ec;
    if (ec) {
        error = "could not inspect " + path.string() + ": " + ec.message();
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::out);
    if (!output) {
        error = "could not create " + path.string();
        return false;
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    output.flush();
    if (!output) {
        error = "could not finish writing " + path.string();
        return false;
    }
    result.createdPaths.push_back(path);
    result.changed = true;
    return true;
}

bool ensureDirectory(const std::filesystem::path& path, ProjectScaffoldResult& result,
                     std::string& error) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        if (ec || !std::filesystem::is_directory(path, ec) || ec) {
            error = path.string() + " exists but is not a usable directory";
            return false;
        }
        return true;
    }
    if (!std::filesystem::create_directories(path, ec) && ec) {
        error = "could not create directory " + path.string() + ": " + ec.message();
        return false;
    }
    result.createdPaths.push_back(path);
    result.changed = true;
    return true;
}

} // namespace

ProjectScaffoldResult ensureProjectScaffold(const std::filesystem::path& projectRoot) {
    ProjectScaffoldResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec) return result;

    const auto scriptsDirectory = projectRoot / "scripts";
    bool hasScripts = hasCxScript(scriptsDirectory, ec);
    if (ec) {
        result.error = "could not scan the scripts directory: " + ec.message();
        return result;
    }

    std::vector<std::filesystem::path> rootScripts;
    bool rootIsEmpty = true;
    for (std::filesystem::directory_iterator it(projectRoot, ec), end; it != end && !ec; it.increment(ec)) {
        rootIsEmpty = false;
        if (isCxFile(*it, ec)) rootScripts.push_back(it->path());
        if (ec) break;
    }
    if (ec) {
        result.error = "could not inspect project directory: " + ec.message();
        return result;
    }

    ec.clear();
    const bool scriptsDirectoryExists = std::filesystem::is_directory(scriptsDirectory, ec) && !ec;
    const bool importOneRootScript = !hasScripts && rootScripts.size() == 1;
    const bool createFreshProject = rootIsEmpty;
    if (!hasScripts && !importOneRootScript && !createFreshProject) return result;

    result.recognizedProject = true;
    std::string error;
    const std::vector<std::filesystem::path> directories = {
        scriptsDirectory,
        projectRoot / "rooms",
        projectRoot / "assets" / "textures",
        projectRoot / "assets" / "audio",
        projectRoot / "assets" / "fonts",
        projectRoot / "tools"
    };
    for (const auto& directory : directories) {
        if (!ensureDirectory(directory, result, error)) {
            result.error = std::move(error);
            return result;
        }
    }

    if (importOneRootScript && !scriptsDirectoryExists) {
        const auto destination = scriptsDirectory / rootScripts.front().filename();
        if (std::filesystem::exists(destination, ec) || ec) {
            result.error = "cannot import the root-level script because its destination already exists: " + destination.string();
            return result;
        }
        std::filesystem::copy_file(rootScripts.front(), destination,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
            result.error = "could not copy the root-level script into scripts/: " + ec.message();
            return result;
        }
        result.createdPaths.push_back(destination);
        result.changed = true;
        hasScripts = true;
    } else if (importOneRootScript && scriptsDirectoryExists) {
        const auto destination = scriptsDirectory / rootScripts.front().filename();
        if (std::filesystem::exists(destination, ec) || ec) {
            result.error = "cannot import the root-level script because its destination already exists: " + destination.string();
            return result;
        }
        std::filesystem::copy_file(rootScripts.front(), destination,
                                   std::filesystem::copy_options::none, ec);
        if (ec) {
            result.error = "could not copy the root-level script into scripts/: " + ec.message();
            return result;
        }
        result.createdPaths.push_back(destination);
        result.changed = true;
        hasScripts = true;
    }

    if (createFreshProject) {
        const std::string starter =
            "# This is a convenient starting file, not a required entry point.\n"
            "# Every .cx file under scripts/ is loaded in stable path order.\n"
            "# Uncomment and edit this example when the project needs startup behavior:\n"
            "# on load:\n"
            "#     set {score} to 0\n";
        if (!writeIfMissing(scriptsDirectory / "main.cx", starter, result, error)) {
            result.error = std::move(error);
            return result;
        }
    }

    // Deliberately small: the engine's responsive window, tick-rate and rendering
    // defaults already work for a new game. Add only settings a new project may
    // reasonably want to discover and change immediately.
    const std::string config =
        "{\n"
        "  \"window_title\": \"My Game\",\n"
        "  \"console_enabled\": true\n"
        "}\n";
    if (!writeIfMissing(projectRoot / "config.json", config, result, error)) {
        result.error = std::move(error);
        return result;
    }

    const std::string readme =
        "# CX Game Project\n\n"
        "This folder is a CX game project.\n\n"
        "## Scripts\n\n"
        "Every `.cx` file under `scripts/` is loaded recursively in stable path order. "
        "`main.cx` is only a convenient starting file; it is not a required entry point. "
        "Use `on load:` for game startup behavior. Top-level settings run before load handlers.\n\n"
        "## Configuration\n\n"
        "`config.json` is optional. It sets baseline engine options; top-level `set engine::...` "
        "statements in scripts override those values. The built-in window defaults are responsive "
        "and do not need to be repeated in every game.\n\n"
        "## Run and validate\n\n"
        "Open this folder with the graphical runtime using `cx-engine <project-folder>`. "
        "To check scripts without opening a window or changing project files, run "
        "`cx-cli --check <project-folder>`.\n\n"
        "## Generated save files\n\n"
        "Persistent variable files are created only when the game first saves persistent values. "
        "Do not create empty `variables.csv`, `variables.cxv`, or `variables.meta` files by hand.\n\n"
        "## Folders\n\n"
        "- `rooms/` — room maps\n"
        "- `assets/textures/` — image assets\n"
        "- `assets/audio/` — audio assets\n"
        "- `assets/fonts/` — optional fonts\n"
        "- `tools/` — project-local authoring tools\n";
    if (!writeIfMissing(projectRoot / "README.md", readme, result, error)) {
        result.error = std::move(error);
        return result;
    }
    return result;
}

} // namespace cx
