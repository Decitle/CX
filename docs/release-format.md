# Release format

The project intends to publish two source archives and native starter-runtime packages from each validated version. This page describes the planned structure; only the platform assets produced successfully by CI should be attached to a release.

## Source distributions

- `CXEngine-vX.Y.Z-source.zip`
- `CXEngine-vX.Y.Z-source.tar.gz`

Both contain the same source commit, public documentation, examples, tests, build configuration, tools, and license. They exclude build trees, compiled binaries, internal development records, handoff documents, logs, and complete showcase games.

## Native runtime packages

| Platform | Planned package |
|---|---|
| Windows x86_64 | `CXEngine-vX.Y.Z-windows-x86_64.zip` |
| Windows ARM64 | `CXEngine-vX.Y.Z-windows-arm64.zip` |
| Linux x86_64 | `CXEngine-vX.Y.Z-linux-x86_64.tar.gz` |
| Linux ARM64 | `CXEngine-vX.Y.Z-linux-arm64.tar.gz` |
| macOS x86_64 | `CXEngine-vX.Y.Z-macos-x86_64.zip` |
| macOS ARM64 | `CXEngine-vX.Y.Z-macos-arm64.zip` |

A runtime package should contain the graphical executable, the separate CLI, a basic `config.json`, a minimal `scripts/main.cx`, required redistributable third-party runtime libraries, the corresponding notices/licenses, a first-run README, and a build manifest/checksum. It must not contain a complete game.

## Dependency expectations

Windows packages should include SFML DLLs and identify any required Microsoft runtime components. Linux packages should include the allowed application libraries but document a minimum system-library baseline; they cannot bundle the kernel, graphics drivers, or every distribution's system ABI. macOS packages should use the correct architecture and application resource layout.

## One project, multiple runtimes

Scripts, room JSON, configuration, and game assets should remain the same across platforms. Players need the runtime matching their operating system and CPU architecture; native executables themselves are not interchangeable.

## Release validation

A package is publishable only after a native build, CTest, CLI check, clean extraction, dependency scan, starter launch, and checksum verification succeed for that exact target. An untested platform is omitted or clearly marked experimental; it is never presented as supported.
