#include "cx/effects.hpp"
#include "cx/runtime.hpp"
#include "cx/script.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float epsilon = 0.001F) {
    return std::abs(a - b) <= epsilon;
}

class TemporaryProject {
public:
    explicit TemporaryProject(std::string suffix)
        : root_(std::filesystem::temp_directory_path() / ("cx-beam-effects-" + std::move(suffix))) {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
        std::filesystem::create_directories(root_ / "assets" / "textures");
        std::filesystem::create_directories(root_ / "scripts");
        std::ofstream(root_ / "assets" / "textures" / "beam.png", std::ios::binary) << "placeholder";
    }
    ~TemporaryProject() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    const std::filesystem::path& root() const noexcept { return root_; }
    void script(const std::string& source) const {
        std::ofstream file(root_ / "scripts" / "main.cx", std::ios::binary);
        if (!file) throw std::runtime_error("Could not write beam test script");
        file << source;
    }
private:
    std::filesystem::path root_;
};

void testBeamQuadGeometryAndTextureCoordinates() {
    cx::BeamEffect beam;
    beam.start = {10.0F, 20.0F};
    beam.end = {110.0F, 20.0F};
    beam.width = 20.0F;
    beam.textureRepeatLength = 50.0F;

    cx::BeamQuad quad;
    expect(cx::buildBeamQuad(beam, quad), "horizontal beam should produce a quad");
    expect(near(quad.length, 100.0F), "quad should retain segment length");
    expect(near(quad.vertices[0].position.x, 10.0F) && near(quad.vertices[0].position.y, 30.0F),
           "first corner should be offset by half-width perpendicular to the segment");
    expect(near(quad.vertices[2].position.x, 110.0F) && near(quad.vertices[2].position.y, 10.0F),
           "opposite end corner should be offset in the reverse perpendicular direction");
    expect(near(quad.vertices[1].u, 2.0F), "texture U should repeat according to world length/repeat length");
    expect(near(quad.vertices[2].v, 1.0F), "texture V should span the full beam width");

    beam.start = {5.0F, 10.0F};
    beam.end = {5.0F, 110.0F};
    expect(cx::buildBeamQuad(beam, quad), "vertical beam should produce a quad");
    expect(near(quad.vertices[0].position.x, -5.0F) && near(quad.vertices[0].position.y, 10.0F),
           "vertical beam should use a perpendicular along the X axis");

    const cx::BeamQuad previous = quad;
    beam.end = beam.start;
    expect(!cx::buildBeamQuad(beam, quad), "zero-length beam should be rejected");
    expect(near(quad.length, previous.length), "failed geometry build must leave the caller output unchanged");
    beam.end = {10.0F, 10.0F};
    beam.width = std::numeric_limits<float>::quiet_NaN();
    expect(!cx::buildBeamQuad(beam, quad), "non-finite beam width should be rejected");
}

void testScriptAuthoringAndBeamProperties() {
    TemporaryProject project("properties");
    project.script(R"CX(
create beam "sun shaft" from (320, -80) to (320, 240) using {$beam}
set beam "sun shaft" width to 48
set beam "sun shaft" opacity to 0.7
set beam "sun shaft" tint to (255, 230, 190)
set beam "sun shaft" layer to "world"
set beam "sun shaft" texture_repeat to 80
set beam "sun shaft" endpoint_light to true
set beam "sun shaft" endpoint_light_radius to 180
set beam "sun shaft" endpoint_light_intensity to 0.8
set beam "sun shaft" endpoint_light_color to (255, 200, 140)
set beam "sun shaft" end to (340, 260)
)CX");

    cx::Runtime runtime(project.root());
    expect(runtime.loadScripts(), "valid beam commands should load without errors");
    expect(runtime.beams().size() == 1, "one beam should be created");
    const auto& beam = runtime.beams().at("sun shaft");
    expect(beam.textureId == "beam", "beam should refer to its texture asset ID");
    expect(near(beam.width, 48.0F) && near(beam.opacity, 0.7F), "width and opacity properties should be applied");
    expect(beam.red == 255 && beam.green == 230 && beam.blue == 190, "tint should be applied");
    expect(beam.layer == cx::BeamDrawLayer::World, "world layer should parse as the depth-sorted layer");
    expect(near(beam.textureRepeatLength, 80.0F), "texture repeat length should be applied");
    expect(beam.endpointLightEnabled, "endpoint light should be independently enabled");
    expect(near(beam.endpointLightRadius, 180.0F) && near(beam.endpointLightIntensity, 0.8F),
           "endpoint light radius and intensity should be applied");
    expect(beam.endpointLightRed == 255 && beam.endpointLightGreen == 200 && beam.endpointLightBlue == 140,
           "endpoint light color should be applied independently from beam tint");
    expect(near(beam.end.x, 340.0F) && near(beam.end.y, 260.0F), "end point should be editable after creation");
    expect(!runtime.hasErrors(), "valid beam property changes should not add runtime errors");
}

void testBeamLifetimeRemovalAndPermanentDefault() {
    TemporaryProject project("lifetime");
    project.script(R"CX(
create beam "temporary" from (0, 0) to (0, 120) using {$beam}
set beam "temporary" lifetime to 0.5
create beam "permanent" from (16, 0) to (16, 120) using {$beam}
)CX");

    cx::Runtime runtime(project.root());
    expect(runtime.loadScripts(), "lifetime test project should load");
    expect(runtime.beams().size() == 2, "both temporary and permanent beams should be created");
    expect(near(runtime.beams().at("permanent").lifetimeSeconds, 0.0F), "zero lifetime should mean persistent");
    for (int i = 0; i < 29; ++i) runtime.tick();
    expect(runtime.beams().contains("temporary"), "temporary beam should remain before its lifetime elapses");
    runtime.tick();
    expect(!runtime.beams().contains("temporary"), "temporary beam should expire at its lifetime");
    expect(runtime.beams().contains("permanent"), "persistent beam should remain after many ticks");
    runtime.executeInline("remove beam \"permanent\"");
    expect(runtime.beams().empty(), "explicit remove command should remove the remaining beam");
    expect(!runtime.hasErrors(), "valid lifetime and removal operations should not produce errors");
}

void testInvalidBeamEditsAreTransactional() {
    TemporaryProject project("invalid-edit");
    project.script(R"CX(
create beam "beam" from (0, 0) to (0, 100) using {$beam}
)CX");
    cx::Runtime runtime(project.root());
    expect(runtime.loadScripts(), "baseline beam should load");
    runtime.executeInline("set beam \"beam\" end to (0, 0)");
    expect(runtime.hasErrors(), "setting an endpoint to the start should be rejected");
    const auto& beam = runtime.beams().at("beam");
    expect(near(beam.end.x, 0.0F) && near(beam.end.y, 100.0F),
           "invalid endpoint edits must leave the previous beam geometry intact");
}

void testUnknownBeamTextureIsRejected() {
    TemporaryProject project("unknown-texture");
    project.script(R"CX(
create beam "bad" from (0, 0) to (0, 100) using {$missing}
)CX");
    cx::Runtime runtime(project.root());
    expect(!runtime.loadScripts(), "unknown beam texture should fail transactional startup");
    expect(runtime.beams().empty(), "invalid startup should not commit a partially created beam");
}

} // namespace

int main() {
    try {
        testBeamQuadGeometryAndTextureCoordinates();
        testScriptAuthoringAndBeamProperties();
        testBeamLifetimeRemovalAndPermanentDefault();
        testInvalidBeamEditsAreTransactional();
        testUnknownBeamTextureIsRejected();
        std::cout << "All beam effect tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Beam effect test failure: " << error.what() << '\n';
        return 1;
    }
}
