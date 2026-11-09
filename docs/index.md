---
title: Documentation
description: Public-facing documentation for CX Engine
---

# CX Engine Documentation

CX is a graphical 2D game engine built around a C++20 runtime and a readable, Skript-inspired scripting language. The graphical desktop runtime is the primary application; command-line tooling is provided separately for checks and automation.

This documentation describes the project for developers, contributors, and people evaluating the engine. It distinguishes implemented work from planned work and records platform support only where there is build or test evidence.

## Start here

- [Project status](PROJECT_STATUS.md) — what exists today, what is currently in the GitHub repository, and what is not ready yet.
- [Platform support](PLATFORM_SUPPORT.md) — verified environments, unverified targets, and the planned compatibility matrix.
- [Release format](RELEASE_FORMAT.md) — planned source and runtime downloads, package contents, and portability rules.
- [Roadmap](ROADMAP.md) — the route from the current development checkpoint to a dependable public release.

## Current status

The broader engine has an internal v0.10.14 development checkpoint, but this repository is still a selective source import. The current branch does not yet contain the complete buildable engine or a published binary release. See [Project status](PROJECT_STATUS.md) before attempting to build or redistribute it.

## Documentation principles

- A feature is not described as complete unless it exists and has relevant tests or validation evidence.
- An operating system or CPU architecture is not labelled supported until it has a native build and runtime test.
- Release downloads must match the contents and version claimed by their filenames.
- Game projects and sample games are distributed separately from the engine's minimal starter runtime.
