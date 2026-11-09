#include "cx/value.hpp"
#include "cx/variable_storage.hpp"

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

void testVariableStorageGenerationManifestAndVerifiedCacheRecovery() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-generation-manifest";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    const auto readBytes = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("could not read test file: " + path.string());
        return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    };

    cx::VariableStorage::VariableMap first;
    first.emplace("score", cx::Value(1));
    std::string error;
    expect(cx::VariableStorage::save(root, first, error), "first manifested save should succeed: " + error);
    const std::string manifestV1 = readBytes(root / "variables.meta");
    expect(manifestV1.starts_with("CXVM1\ngeneration=1\n"),
           "the first save should create a versioned generation-1 manifest");

    cx::VariableStorage::VariableMap second;
    second.emplace("score", cx::Value(2));
    expect(cx::VariableStorage::save(root, second, error), "second manifested save should succeed: " + error);
    const std::string manifestV2 = readBytes(root / "variables.meta");
    expect(manifestV2.starts_with("CXVM1\ngeneration=2\n"),
           "a later complete save should advance the manifest generation");
    const std::string committedCache = readBytes(root / "variables.cxv");

    // A corrupt cache must not prevent the authoritative CSV from loading.
    {
        std::ofstream cache(root / "variables.cxv", std::ios::binary | std::ios::app);
        cache.put('\0');
    }
    cx::VariableStorage::VariableMap intactCsv;
    std::string cacheWarning;
    expect(cx::VariableStorage::load(root, intactCsv, cacheWarning),
           "a present authoritative CSV should load even when its cache is damaged");
    expect(intactCsv.contains("score") && intactCsv.at("score").numberOr() == 2.0,
           "a damaged cache must not alter authoritative CSV values");
    expect(cacheWarning.find("does not match the committed variables.meta generation") != std::string::npos,
           "a damaged cache should be reported when the CSV still identifies the committed generation");
    {
        std::ofstream cache(root / "variables.cxv", std::ios::binary | std::ios::trunc);
        cache.write(committedCache.data(), static_cast<std::streamsize>(committedCache.size()));
    }

    // Human edits remain authoritative; the manifest only gates cache recovery.
    {
        std::ofstream csv(root / "variables.csv", std::ios::binary | std::ios::trunc);
        csv << "# CX Engine persistent variables. Edit or delete rows while the engine is closed.\n"
               "# name,type,value\nscore,number,3\n";
    }
    cx::VariableStorage::VariableMap edited;
    std::string warning;
    expect(cx::VariableStorage::load(root, edited, warning),
           "a hand-edited authoritative CSV should load even when the manifest is now stale");
    expect(edited.contains("score") && edited.at("score").numberOr() == 3.0,
           "the manifest must never override an existing editable CSV");

    // Once the CSV is missing, a cache that differs from the last fully committed
    // generation must not be used to recreate user data.
    std::filesystem::remove(root / "variables.csv");
    {
        std::ofstream cache(root / "variables.cxv", std::ios::binary | std::ios::app);
        cache.put('\0');
    }
    cx::VariableStorage::VariableMap rejected;
    warning.clear();
    expect(!cx::VariableStorage::load(root, rejected, warning),
           "a cache that does not match the committed manifest must be rejected");
    expect(rejected.empty(), "rejected cache data must not leak a partial variable map");
    expect(warning.find("does not match variables.meta") != std::string::npos,
           "cache-integrity failure should explain the manifest mismatch");
    expect(!std::filesystem::exists(root / "variables.csv"),
           "unverified cache data must not be written back as a reconstructed CSV");

    // Restore the known committed cache and prove that the verified generation is still
    // recoverable after the editable CSV is absent.
    {
        std::ofstream cache(root / "variables.cxv", std::ios::binary | std::ios::trunc);
        cache.write(committedCache.data(), static_cast<std::streamsize>(committedCache.size()));
    }
    cx::VariableStorage::VariableMap recovered;
    warning.clear();
    expect(cx::VariableStorage::load(root, recovered, warning),
           "a cache whose bytes match the committed manifest should remain recoverable: " + warning);
    expect(recovered.contains("score") && recovered.at("score").numberOr() == 2.0,
           "verified cache recovery should restore the last fully committed generation");
    expect(warning.empty(), "verified cache recovery should not produce a warning");
    expect(readBytes(root / "variables.meta") == manifestV2,
           "verified recovery should retain the original committed generation metadata");

    // Generation wraparound must fail before modifying the save set.
    const std::string csvBeforeOverflow = readBytes(root / "variables.csv");
    const std::string cacheBeforeOverflow = readBytes(root / "variables.cxv");
    const std::string exhaustedManifest =
        "CXVM1\ngeneration=18446744073709551615\ncsv_size=0\ncsv_hash=0000000000000000\n"
        "cache_size=0\ncache_hash=0000000000000000\n";
    {
        std::ofstream manifest(root / "variables.meta", std::ios::binary | std::ios::trunc);
        manifest << exhaustedManifest;
    }
    error.clear();
    expect(!cx::VariableStorage::save(root, second, error) && error.find("exhausted") != std::string::npos,
           "generation exhaustion should be reported clearly");
    expect(readBytes(root / "variables.csv") == csvBeforeOverflow &&
           readBytes(root / "variables.cxv") == cacheBeforeOverflow &&
           readBytes(root / "variables.meta") == exhaustedManifest,
           "overflow must leave CSV, cache, and metadata untouched");
    std::filesystem::remove_all(root);
}

void testVariableStorageRefusesCacheRecoveryWithMalformedManifest() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-malformed-generation-manifest";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    cx::VariableStorage::VariableMap values;
    values.emplace("score", cx::Value(4));
    std::string error;
    expect(cx::VariableStorage::save(root, values, error), "seed save for malformed manifest test should succeed");
    std::filesystem::remove(root / "variables.csv");
    {
        std::ofstream manifest(root / "variables.meta", std::ios::binary | std::ios::trunc);
        manifest << "CXVM1\ngeneration=not-a-number\n";
    }
    cx::VariableStorage::VariableMap recovered;
    std::string warning;
    expect(!cx::VariableStorage::load(root, recovered, warning),
           "a malformed generation manifest must block cache reconstruction");
    expect(recovered.empty(), "malformed manifest recovery must not expose cached values");
    expect(warning.find("variables.meta is invalid") != std::string::npos,
           "malformed manifest failure should identify the metadata problem");
    expect(!std::filesystem::exists(root / "variables.csv"),
           "malformed manifest recovery must not create a new CSV");
    std::filesystem::remove_all(root);
}

void testVariableStorageDoesNotTreatMissingManifestedSaveAsNewProject() {
    const auto root = std::filesystem::temp_directory_path() / "cx-engine-test-missing-manifested-save";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    cx::VariableStorage::VariableMap original;
    original.emplace("score", cx::Value(12));
    std::string error;
    expect(cx::VariableStorage::save(root, original, error), "seed save for missing-data test should succeed");
    std::filesystem::remove(root / "variables.csv");
    std::filesystem::remove(root / "variables.cxv");

    cx::VariableStorage::VariableMap loaded;
    std::string warning;
    expect(!cx::VariableStorage::load(root, loaded, warning),
           "a manifest with both save representations missing must not look like a fresh empty project");
    expect(loaded.empty(), "missing manifested save must not expose fabricated values");
    expect(warning.find("both variables.csv and variables.cxv are missing") != std::string::npos,
           "missing manifested save should explain that the data files are absent");

    // A malformed sidecar must be described as invalid, not as a valid generation.
    {
        std::ofstream manifest(root / "variables.meta", std::ios::binary | std::ios::trunc);
        manifest << "CXVM1\ngeneration=bad\n";
    }
    warning.clear();
    expect(!cx::VariableStorage::load(root, loaded, warning),
           "missing data with malformed metadata must remain a hard failure");
    expect(warning.find("Could not validate variables.meta") != std::string::npos &&
           warning.find("incomplete") != std::string::npos,
           "missing-save diagnostics must distinguish malformed metadata from a valid generation");
    std::filesystem::remove_all(root);
}


} // namespace

int main() {
    try {
        testVariableStorageGenerationManifestAndVerifiedCacheRecovery();
        testVariableStorageRefusesCacheRecoveryWithMalformedManifest();
        testVariableStorageDoesNotTreatMissingManifestedSaveAsNewProject();
    } catch (const std::exception& exception) {
        std::cerr << "TEST FAILED: " << exception.what() << '\n';
        return 1;
    }
    std::cout << "All CX Engine save-generation tests passed.\n";
    return 0;
}
