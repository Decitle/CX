#include "cx/project_scaffold.hpp"
#include "cx/runtime.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void write(const std::filesystem::path& path, const std::string& value) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << value;
    if (!out) throw std::runtime_error("could not write test fixture: " + path.string());
}

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read test fixture: " + path.string());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void testEmptyFolderCreatesMinimalProject() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-empty-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto result = cx::ensureProjectScaffold(root);
    expect(result.recognizedProject && result.changed && result.error.empty(),
           "an empty directory should become a new project");
    expect(std::filesystem::is_regular_file(root / "scripts" / "main.cx"),
           "a new project should get a convenient, empty main.cx starter");
    const auto starter = read(root / "scripts" / "main.cx");
    expect(starter.find("not a required entry point") != std::string::npos,
           "the generated starter should explicitly say main.cx is not required");
    expect(starter.find("# on load:") != std::string::npos &&
           starter.find("#     set {score} to 0") != std::string::npos,
           "the starter should include a safe commented example without adding game behavior");
    const auto readme = read(root / "README.md");
    expect(readme.find("cx-engine <project-folder>") != std::string::npos &&
           readme.find("cx-cli --check <project-folder>") != std::string::npos,
           "the generated README should explain how to run and validate the project");
    expect(std::filesystem::is_directory(root / "rooms") &&
           std::filesystem::is_directory(root / "assets" / "textures") &&
           std::filesystem::is_directory(root / "assets" / "audio") &&
           std::filesystem::is_directory(root / "assets" / "fonts") &&
           std::filesystem::is_directory(root / "tools"),
           "a new project should have the standard authoring directories");
    expect(!std::filesystem::exists(root / "variables.csv") &&
           !std::filesystem::exists(root / "variables.cxv") &&
           !std::filesystem::exists(root / "variables.meta"),
           "empty variable storage files must not be created before persistent values exist");
    expect(read(root / "config.json").find("window_width") == std::string::npos,
           "the minimal config must not duplicate built-in window defaults");

    const auto second = cx::ensureProjectScaffold(root);
    expect(second.recognizedProject && !second.changed && second.error.empty(),
           "project preparation should be idempotent");
    std::filesystem::remove_all(root);
}

void testSingleRootScriptIsImportedWithoutChangingOriginal() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-root-script";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const std::string contents = "# a user script\non load:\n    set {score} to 1\n";
    write(root / "game.cx", contents);

    const auto result = cx::ensureProjectScaffold(root);
    expect(result.recognizedProject && result.changed && result.error.empty(),
           "a single root-level CX script should be enough to recognize a project");
    expect(read(root / "game.cx") == contents,
           "the original root-level script must remain untouched");
    expect(read(root / "scripts" / "game.cx") == contents,
           "the root-level script should be copied into the standard scripts directory");
    std::filesystem::remove_all(root);
}

void testSingleRootScriptIsImportedIntoExistingScriptsDirectory() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-root-script-existing-scripts";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    const std::string contents = "# keep the user filename\non load:\n    set {ready} to true\n";
    write(root / "gameplay.cx", contents);

    const auto result = cx::ensureProjectScaffold(root);
    expect(result.recognizedProject && result.changed && result.error.empty(),
           "a single root script should be imported even when an empty scripts folder exists");
    expect(read(root / "gameplay.cx") == contents &&
           read(root / "scripts" / "gameplay.cx") == contents,
           "the source should be preserved and copied using its original filename");
    expect(!std::filesystem::exists(root / "scripts" / "main.cx"),
           "importing a root script must not silently rename it to main.cx");
    std::filesystem::remove_all(root);
}

void testExistingScriptProjectGetsOnlyMissingFiles() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-existing-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "scripts");
    write(root / "scripts" / "empty.cx", "");
    write(root / "config.json", "{\n  \"window_title\": \"Owner Settings\"\n}\n");
    write(root / "README.md", "Owner notes stay here.\n");

    const auto result = cx::ensureProjectScaffold(root);
    expect(result.recognizedProject && result.changed && result.error.empty(),
           "an empty but existing CX script should recognize the project");
    expect(read(root / "config.json").find("Owner Settings") != std::string::npos,
           "an existing config must never be overwritten");
    expect(read(root / "README.md") == "Owner notes stay here.\n",
           "an existing README must never be overwritten");
    expect(std::filesystem::is_regular_file(root / "scripts" / "empty.cx"),
           "the empty user script must remain present");
    expect(!std::filesystem::exists(root / "scripts" / "main.cx"),
           "existing script projects should not get a redundant main.cx");
    expect(std::filesystem::is_directory(root / "rooms") &&
           std::filesystem::is_directory(root / "assets" / "textures") &&
           std::filesystem::is_directory(root / "assets" / "audio") &&
           std::filesystem::is_directory(root / "assets" / "fonts") &&
           std::filesystem::is_directory(root / "tools"),
           "a partially populated project should receive only its missing authoring folders");
    const auto second = cx::ensureProjectScaffold(root);
    expect(second.recognizedProject && !second.changed && second.error.empty(),
           "preparing a partially populated project a second time should be idempotent");
    std::filesystem::remove_all(root);
}

void testUnrelatedNonEmptyFolderIsNotModified() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-unrelated-folder";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write(root / "notes.txt", "not a project\n");

    const auto result = cx::ensureProjectScaffold(root);
    expect(!result.recognizedProject && !result.changed && result.error.empty(),
           "a non-empty unrelated directory should not be auto-converted");
    expect(std::filesystem::is_regular_file(root / "notes.txt") &&
           !std::filesystem::exists(root / "scripts") &&
           !std::filesystem::exists(root / "config.json"),
           "unrelated directory contents must remain untouched");
    std::filesystem::remove_all(root);
}

void testRuntimeLoadsAnAutoScaffoldedEmptyProject() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-runtime-empty-project";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    cx::Runtime runtime(root);
    expect(runtime.loadScripts(true), "runtime should load the comment-only generated starter");
    expect(runtime.program().filesLoaded == 1,
           "runtime should see the generated starter as one ordinary script file");
    expect(std::filesystem::is_regular_file(root / "config.json") &&
           std::filesystem::is_regular_file(root / "scripts" / "main.cx"),
           "runtime startup should create the expected starter files");
    expect(!std::filesystem::exists(root / "variables.csv") &&
           !std::filesystem::exists(root / "variables.cxv") &&
           !std::filesystem::exists(root / "variables.meta"),
           "an empty project should not receive empty persistent-variable databases");
    std::filesystem::remove_all(root);
}

void testMultipleRootScriptsAreNotChosenArbitrarily() {
    const auto root = std::filesystem::temp_directory_path() / "cx-scaffold-ambiguous-root";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write(root / "first.cx", "");
    write(root / "second.cx", "");

    const auto result = cx::ensureProjectScaffold(root);
    expect(!result.recognizedProject && !result.changed,
           "multiple root-level scripts without a scripts folder must not be guessed");
    expect(!std::filesystem::exists(root / "scripts"),
           "ambiguous root scripts must not be copied or rearranged automatically");
    std::filesystem::remove_all(root);
}

} // namespace

int main() {
    try {
        testEmptyFolderCreatesMinimalProject();
        testSingleRootScriptIsImportedWithoutChangingOriginal();
        testSingleRootScriptIsImportedIntoExistingScriptsDirectory();
        testExistingScriptProjectGetsOnlyMissingFiles();
        testUnrelatedNonEmptyFolderIsNotModified();
        testRuntimeLoadsAnAutoScaffoldedEmptyProject();
        testMultipleRootScriptsAreNotChosenArbitrarily();
        std::cout << "All project scaffold tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Project scaffold test failure: " << error.what() << '\n';
        return 1;
    }
}
