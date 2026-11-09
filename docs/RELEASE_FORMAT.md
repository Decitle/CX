# Release format and package layout

The goal is to make CX simple to try without requiring every player to install a compiler, CMake, or SFML development tools.

No multi-platform runtime release is published yet. The matrix and names below are the intended release design, not currently downloadable files.

## Release assets

Each tagged release should include two equivalent source distributions:

- `CXEngine-vX.Y.Z-source.zip`
- `CXEngine-vX.Y.Z-source.tar.gz`

The source distributions should contain the same source revision, build files, tests, documentation, and license notices. They should not embed a sample game, temporary build directory, compiler output, or machine-specific cache.

The planned native runtime packages are:

| Target | Planned asset |
|---|---|
| Windows x86_64 | `CXEngine-vX.Y.Z-windows-x86_64.zip` |
| Windows ARM64 | `CXEngine-vX.Y.Z-windows-arm64.zip` |
| Linux x86_64 | `CXEngine-vX.Y.Z-linux-x86_64.tar.gz` |
| Linux ARM64 | `CXEngine-vX.Y.Z-linux-arm64.tar.gz` |
| macOS x86_64 | `CXEngine-vX.Y.Z-macos-x86_64.zip` |
| macOS ARM64 | `CXEngine-vX.Y.Z-macos-arm64.zip` |

Only targets that pass their native build and runtime checks should be attached to a release. Missing targets should be listed as unavailable rather than attached as experimental downloads without explanation.

## What a runtime package should contain

Each runtime should include the engine's compiled graphical executable and the minimum files needed to start creating a project:

- The graphical runtime executable (`cx-engine.exe` on Windows, `cx-engine` on Unix-like systems).
- The separate command-line utility (`cx-cli.exe` / `cx-cli`) for project checks and diagnostics.
- A starter `config.json`.
- A minimal `scripts/main.cx` with a small, clearly commented starting point.
- Only any genuinely required default assets, such as a font or runtime metadata.
- The SFML runtime libraries or DLLs required by the build, with their license notices.
- A first-run README explaining how to launch the engine, open the starter project, and run CLI checks.
- A version/build manifest and SHA-256 checksum.

The package should not include the Cave-to-Ancient-Arena sample game or another full game. Showcase projects should be separate downloads so a first-time user gets a clean workspace.

## Self-contained does not mean operating-system independent

The package should carry the application-level libraries that can legally and practically be bundled. It cannot bundle a user's graphics driver or replace operating-system components.

- Windows packages should include the required SFML DLLs and identify any required Microsoft runtime components.
- Linux packages should include permitted non-system shared libraries and state the minimum supported system-library baseline. A tarball cannot make every Linux distribution ABI-compatible.
- macOS packages should include the appropriate frameworks/dylibs and follow normal app-bundle/resource conventions.

All redistributed third-party libraries need their license texts and notices included. Release packaging must verify dependency resolution on a clean machine or clean runner, not merely copy whatever happens to exist on the build host.

## One game project, several native runtimes

The same project folder should be reusable across the target matrix. The project data—scripts, room files, textures, and configuration—should remain the same. The executable and bundled native libraries must match the player's operating system and CPU architecture.

For example, a Windows x86_64 player uses the Windows x86_64 runtime with the shared project directory; a Linux ARM64 player uses the Linux ARM64 runtime with that same project directory. A Windows executable is not expected to run on Linux by itself.

The project format must avoid platform-specific absolute paths, filename-case assumptions, and undocumented encoding behavior. A cross-platform compatibility suite should open, validate, and run the same starter and sample projects on every release target.

## Release automation

Once the complete engine source is present in this repository, the release pipeline should:

1. Build and test on native runners for each target.
2. Package source ZIP and TAR.GZ from one clean source revision.
3. Build the graphical runtime and CLI for each platform.
4. Assemble the minimal starter project and permitted dependencies.
5. Run package-content, binary-format, dependency, and checksum validation.
6. Launch the starter project in a graphical smoke test where the runner supports it.
7. Attach only successful target assets to a GitHub Release and publish a checksums file and build matrix.

The source-import and CI work must be completed before a release workflow can honestly be advertised as working.
