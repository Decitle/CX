# Release format

The release workflow is implemented in `.github/workflows/release.yml`. Pushing a version tag beginning with `v` triggers source and native runtime builds. After the source package and every runtime job in the release matrix succeed, the workflow publishes a GitHub Release and attaches the verified assets. Tags containing a hyphen are marked as pre-releases.

No official versioned release has been published yet.

## Source distributions

Each release provides:

- `CXEngine-vX.Y.Z-source.zip`
- `CXEngine-vX.Y.Z-source.tar.gz`

Both contain the same tagged source commit, public documentation, examples, tests, build configuration, tools, and license. They exclude build trees, compiled binaries, internal development records, handoff documents, logs, and complete showcase games.

## Native runtime packages

The release matrix is configured for:

| Platform | Package |
|---|---|
| Windows x86_64 | `CXEngine-vX.Y.Z-windows-x86_64.zip` |
| Windows ARM64 | `CXEngine-vX.Y.Z-windows-arm64.zip` |
| Linux x86_64 | `CXEngine-vX.Y.Z-linux-x86_64.tar.gz` |
| Linux ARM64 | `CXEngine-vX.Y.Z-linux-arm64.tar.gz` |
| macOS x86_64 | `CXEngine-vX.Y.Z-macos-x86_64.zip` |
| macOS ARM64 | `CXEngine-vX.Y.Z-macos-arm64.zip` |

A runtime package contains the graphical executable, separate CLI, a basic `config.json`, a minimal `scripts/main.cx`, required application runtime libraries, license notices, a first-run README, and a file-size/SHA-256 manifest. It does not contain a full game or development SDK.

The Windows runtime filter excludes operating-system DLLs and development-only headers, static libraries, CMake metadata, and SDK documentation. The Linux package bundles allowed application libraries but documents system-library expectations; it cannot bundle the kernel, GPU drivers, or every distribution's system ABI. macOS packages are architecture-specific.

## One project, multiple runtimes

Scripts, room JSON, configuration, and game assets remain the same across platforms. Players download the runtime matching their operating system and CPU architecture; native executables themselves are not interchangeable.

## Release validation

The release workflow performs a native build and CTest run for each matrix target, validates the starter project, installs runtime dependencies, packages the starter project, and attaches the generated manifests and checksums. A package is published only if its release job succeeds. The CI workflow additionally performs graphical startup smoke tests on Linux and Windows. macOS graphical startup is not currently covered by an automated smoke step.

Follow the [getting-started guide](getting-started.md) after extracting a runtime package. The latest published version will appear at [GitHub Releases](https://github.com/Decitle/CX/releases).
