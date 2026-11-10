# Release format

The release workflow is implemented in `.github/workflows/release.yml`. Pushing a version tag beginning with `v` starts source and native runtime builds. The source job creates curated source archives; the runtime matrix builds and packages one native runtime for each configured target. The final publish job depends on the source job and **all six runtime jobs**, so a failed job prevents a partial release from being published. Tags containing a hyphen are marked as pre-releases automatically.

The first planned preview tag is `v0.10.14-preview.5`. The actual tagged release workflow must complete successfully before all of its assets are available. See [GitHub Releases](https://github.com/Decitle/CX/releases) for current publication state.

## Curated source distributions

The workflow attaches:

- `CXEngine-vX.Y.Z-source.zip`
- `CXEngine-vX.Y.Z-source.tar.gz`

Both contain the same tagged source tree: build configuration, C++ source and public headers, tests, focused examples, tools, public documentation, third-party notices, and licenses. A versioned package manifest is included with SHA-256 and file-size records.

The source packager uses an explicit repository allowlist and rejects compiled binaries/libraries, nested archives, build output, caches, generated save data, private/internal directories, handoff files, journals, prompt logs, and validation dumps. It does not put the native runtime packages inside the source archive. The two formats carry the same source content.

GitHub also displays its standard automatically generated `Source code (zip)` and `Source code (tar.gz)` downloads for the tag. Those are separate from the two curated source assets produced by the release workflow.

## Native runtime packages

The release matrix is configured for these six 64-bit/ARM64 targets:

| Platform | Package |
|---|---|
| Windows x86_64 | `CXEngine-vX.Y.Z-windows-x86_64.zip` |
| Windows ARM64 | `CXEngine-vX.Y.Z-windows-arm64.zip` |
| Linux x86_64 | `CXEngine-vX.Y.Z-linux-x86_64.tar.gz` |
| Linux ARM64 | `CXEngine-vX.Y.Z-linux-arm64.tar.gz` |
| macOS x86_64 | `CXEngine-vX.Y.Z-macos-x86_64.zip` |
| macOS ARM64 | `CXEngine-vX.Y.Z-macos-arm64.zip` |

No 32-bit Windows or Linux packages are configured or claimed. Adding legacy 32-bit builds later would require separate toolchain/dependency setup and native or equivalent runtime verification; it should be driven by a real compatibility need, not added as an untested promise.

A runtime package contains the graphical executable, separate CLI, a basic `config.json`, a minimal `scripts/main.cx`, required application runtime libraries, license notices, a first-run README, and a file-size/SHA-256 manifest. It does not contain a full game or development SDK.

The Windows runtime filter excludes operating-system DLLs and development-only headers, static libraries, CMake metadata, and SDK documentation. The Linux package bundles allowed application libraries but documents system-library expectations; it cannot bundle the kernel, GPU drivers, or every distribution's system ABI. macOS packages are architecture-specific.

## One project, multiple runtimes

Scripts, room JSON, configuration, and game assets remain the same across platforms. Players download the runtime matching their operating system and CPU architecture; native executables themselves are not interchangeable.

## Release validation

The tag-triggered release workflow performs a native build and CTest run for each target, validates the starter project, installs runtime dependencies, packages the starter project, and attaches generated manifests/checksums. The regular CI workflow additionally performs graphical startup smoke tests on Linux and Windows. macOS graphical startup is not currently covered by an automated smoke step.

Use the [runtime package guide](runtime-package.md) after extracting a runtime package. The latest release appears at [GitHub Releases](https://github.com/Decitle/CX/releases).
