English | [简体中文](CONTRIBUTING.zh-CN.md)

# Contributing

Reproducible bug reports, documentation improvements, and focused code changes are welcome. For new features or substantial architectural changes, first open an Issue describing the use case, input parameters, expected behavior, and validation approach.

## Getting started

1. Follow the [build instructions](docs/BUILDING.md) to install the Windows C++ toolchain and build `YUVRaw.sln`.
2. Read the [architecture](ARCHITECTURE.md) to identify each datum's owning document and accessing thread.
3. Make the necessary changes and run relevant regressions. Describe the concrete problem, final behavior, and actual validation in the PR.

Use an `F` prefix for C++ classes, `E` for enums, PascalCase for functions/members, and `b` for boolean members. Use `#pragma once` in headers. Name constants with domain meaning; comments on concurrency, boundaries, and compatibility should explain why. Use the existing logging entry point, avoid continuous per-frame logging, and never log credentials or complete sensitive data.

Register new source files in `.vcxproj` / `.filters` and source-package inputs. Append format enums, and review both CPU and GLSL implementations for color changes. Commit messages should briefly explain the change and technical approach. Keep English and Chinese documentation synchronized; add UI text to the shared bilingual resources.

## Pull requests

- Address one clear problem per PR. Document final behavior and remove obsolete approaches.
- Report test outcomes and reasons for `PASS` / `FAIL` / `SKIP`. Missing GPUs, codecs, or real fixtures must not be reported as passing.
- Dependency updates must synchronize manifests, source locks, recipes/patches, licenses, SBOM, and corresponding-source delivery; rerun DNG, color, and export regressions.
- Keep the branch up to date and resolve review discussions before merging. Ordinary branch pushes and PRs do not trigger CI; pushing a version tag runs the full Debug/Release checks, packaging, and publication workflow.

By contributing, you confirm that you may publish the content and agree to distribute project-owned contributions under `GPL-3.0-only`. Preserve the origins and licenses of third-party code and check compatibility before adding it. Report security issues according to [SECURITY.md](SECURITY.md).
