# Changelog

All notable changes to this project will be documented in this file.
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [1.0.1] — 2026-23-06

### Added
- CMake build system (replaces raw Makefile as primary build)
- Legacy Makefile retained with auto-detected hidapi backend, `install` and `uninstall` targets
- GitHub Actions CI — Linux (gcc & clang), macOS, Windows (MSVC/vcpkg), release asset upload
- CodeQL security analysis workflow (scheduled weekly + on push/PR)
- `udev/99-xbit-flasher.rules` — allow non-root USB access on Linux
- `.clang-format` style config (LLVM-based, tab indented)
- `.editorconfig` for consistent whitespace across editors
- `.gitignore`
- `CONTRIBUTING.md`
- `CHANGELOG.md`
- Optional `BUILD_HOOKDLL` CMake flag to build the Windows hookDll
- `ENABLE_ASAN` / `ENABLE_UBSAN` CMake options for sanitizer builds
- Comprehensive README with badges, layout table, usage examples, troubleshooting guide

### Changed
- Makefile now auto-detects hidapi pkg-config backend (hidraw on Linux, hidapi on macOS)
- Makefile uses `$(CXX)` instead of hard-coded `g++`
- README rewritten with structured sections

---

## [1.0.0] — 2018-11-06

### Initial release
- Cross-platform HID flasher for X-BIT modchip
- Supports read, write, verify, format operations
- All 6 bank layouts
- hookDll source for reverse engineering original Windows tool
