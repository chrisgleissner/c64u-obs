# Copilot Instructions for c64u-obs

## Project Overview

This repository provides an OBS Studio source plugin for streaming video and audio from a Commodore 64 Ultimate device to OBS Studio. The plugin implements streaming capabilities according to the C64 Ultimate data streams specification. See `doc/c64u-stream-spec.md` for a concise technical specification, or the [official documentation](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams) for full details.

### Debug Build Tips:
- Use `RelWithDebInfo` configuration for debugging with symbols
- Enable verbose CMake output: `cmake --preset <n> -- -DCMAKE_VERBOSE_MAKEFILE=ON`
- Check ccache statistics after builds for performance insights

## Code Review Guidelines

### Core Principles
1. **Keep It Simple (KISS)**
   - Prefer simple, clear solutions over complex ones
   - Code should be easy to understand at first glance
   - Avoid overengineering and premature optimization

2. **Don't Repeat Yourself (DRY)**
   - Eliminate code duplication through appropriate abstraction
   - Reuse existing functionality where possible
   - Keep configuration and constants in one place

3. **Consistency**
   - Follow established patterns in the codebase
   - Maintain consistent naming and structure
   - Use similar solutions for similar problems

4. **Modularity and Maintainability**
   - Keep functions and modules focused on single responsibilities
   - Make code easy to modify without breaking other parts
   - Design clear interfaces between components

5. **Code Formatting (MANDATORY)**
   - **ALWAYS run `./build-aux/run-clang-format` after ANY code changes**
   - All C/C++ code must pass clang-format 19.1.1+ validation
   - All files must end with a single newline character
   - Use 4 spaces for indentation, 120 character line limit
   - VS Code is configured to auto-format on save
   - **Never commit code that fails clang-format checks**

### Review Focus Areas
- Architecture and design issues
- Performance implications
- Security concerns
- Error handling
- Edge cases
- API design and usability

### What to Avoid
- Trivial comments that don't add value
- Overly complex solutions
- Speculative features
- Premature optimization
- Breaking existing patterns without good reason

## Documentation Organization

### Markdown File Structure (MANDATORY)
- **All Markdown files MUST live in the `doc/` folder** and use kebab-case naming (e.g., `network-jitter-analysis.md`, `udp-packet-ordering.md`)
- **The ONLY exception is `README.md`** which lives at the top-level as the main project entry point
- **No Markdown files in project root** except README.md (move any others to `doc/` folder)
- **Consolidate related documentation** to avoid duplication and maintain single source of truth
- **Use descriptive kebab-case names** that clearly indicate the document's purpose
- **Update all cross-references** when moving or renaming documentation files

## Technologies and Tools

**Key Facts:**
- Language: C (primary), CMake for build system
- Project Type: Cross-platform OBS Studio plugin (Windows, macOS, Linux)
- Size: Small codebase (~10 source files, extensive build infrastructure)
- Dependencies: OBS Studio SDK, platform-specific build tools
- Build System: CMake 3.28+ with preset configurations
- CI/CD: GitHub Actions with comprehensive workflows

## Build Prerequisites and Dependencies

### Always Required Before Building:
1. **CMake 3.28.3+ (Ubuntu) or 3.30.5+ (Windows/macOS)**
2. **Platform-specific toolchain:**
   - Windows: Visual Studio 17 2022
   - macOS: Xcode 16.0+
   - Linux: `build-essential`, `ninja-build`, `pkg-config`
3. **Shell environment:**
   - Linux/macOS: `zsh` (required for build scripts)
   - Windows: PowerShell 5.1+
4. **Formatting tools:**
   - `clang-format` version 19.1.1+ (critical requirement)
   - `gersemi` for CMake formatting (install via: `pip install gersemi`)

### Dependency Management:
The build system automatically downloads OBS Studio dependencies defined in `buildspec.json`:
- OBS Studio SDK (version 31.1.1)
- Pre-built dependencies (obs-deps)
- Qt6 (if ENABLE_QT=ON)

Dependencies are cached in `.deps/` directory and validated by version/hash.

## Build Instructions

### CMake Configuration Options:
- `ENABLE_FRONTEND_API=ON` - Enables OBS frontend API for UI integration
- `ENABLE_QT=ON` - Enables Qt6 support for custom UI elements
- `CMAKE_BUILD_TYPE` - RelWithDebInfo (default), Release, Debug, MinSizeRel
- `CMAKE_INSTALL_PREFIX` - Installation destination (platform-specific defaults)

### Quick Local Development Build:

**Linux/macOS:**
```bash
# Simple development build (without full CI infrastructure)
cmake --preset ubuntu-x86_64  # or macos
cmake --build build_x86_64    # or build_macos
```

**Windows (PowerShell):**
```powershell
# Configure and build using full paths (if cmake not in PATH)
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64
& "C:\Program Files\CMake\bin\cmake.exe" --build build_x64

# Alternative if cmake is in PATH
cmake --preset windows-x64
cmake --build build_x64
```

### Configure and Build (Any Platform):

**Step 1: Configure with preset**
```bash
# Linux
cmake --preset ubuntu-x86_64

# macOS
cmake --preset macos

# Windows
cmake --preset windows-x64
```

**Step 2: Build**
```bash
cmake --build --preset <preset-name>
```

### Using Build Scripts (Recommended for CI-like builds):

**Linux:**
```bash
# Requires zsh - install if missing: sudo apt-get install zsh
# Requires CI environment variable set
export CI=1
.github/scripts/build-ubuntu --target ubuntu-x86_64 --config RelWithDebInfo
```

**macOS:**
```bash
# Note: macOS build script requires CI environment
export CI=1
.github/scripts/build-macos --config RelWithDebInfo --codesign
```

**Windows:**
```powershell
.github/scripts/Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

### Common Build Issues and Solutions:

**Issue: "LibObs not found"**
- Solution: Dependencies not downloaded. The build system should auto-download them, but if it fails, check internet connectivity and buildspec.json integrity.

**Issue: "clang-format version too old"**
- Solution: Format checking requires clang-format 19.1.1+. Either upgrade or skip format checking for development builds. The CI system has the correct version.

**Issue: "zsh not found"**
- Solution: Install zsh (`sudo apt-get install zsh` on Ubuntu)

**Issue: "script execution error" from build scripts**
- Solution: Build scripts require CI=1 environment variable to be set for local execution.

**Issue: "./build-aux/run-clang-format" fails on Windows**
- Solution: The script requires zsh which may not be available on Windows. Use direct clang-format command instead: `& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/*.c src/*.h`

## Validation and Testing

### Code Formatting (Always run before committing):

**Linux/macOS (requires zsh):**
```bash
# Check C/C++ formatting
./build-aux/run-clang-format --check

# Check CMake formatting
./build-aux/run-gersemi --check

# Fix formatting automatically
./build-aux/run-clang-format
./build-aux/run-gersemi
```

**Windows (PowerShell):**
```powershell
# Check C/C++ formatting (if clang-format is in PATH)
clang-format --dry-run --Werror src/*.c src/*.h

# Fix C/C++ formatting with LLVM installation
& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/*.c src/*.h

# Alternative: Format specific file
& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/c64u-source.c

# Check CMake formatting (requires gersemi via pip)
gersemi --check CMakeLists.txt cmake/

# Fix CMake formatting
gersemi --in-place CMakeLists.txt cmake/
```

**Windows Notes:**
- The `./build-aux/run-clang-format` script requires zsh, which may not be available on Windows
- Install LLVM from https://llvm.org/builds/ to get clang-format
- Install gersemi: `pip install gersemi`
- Use PowerShell call operator `&` when paths contain spaces

### Windows Development Environment

**Complete Windows Development Workflow (PowerShell):**

**Step 1: Configure build with preset**
```powershell
# If cmake is in PATH
cmake --preset windows-x64

# If cmake not in PATH (common on Windows)
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64
```

**Step 2: Build the plugin**
```powershell
# If cmake is in PATH
cmake --build build_x64

# If cmake not in PATH (full path required)
& "C:\Program Files\CMake\bin\cmake.exe" --build build_x64
```

**Step 3: Validate formatting (mandatory before commit)**
```powershell
# Check all source files for formatting issues
& "C:\Program Files\LLVM\bin\clang-format.exe" --dry-run --Werror src/*.c src/*.h

# Fix all formatting issues automatically
& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/*.c src/*.h

# Alternative: Fix specific files one by one
Get-ChildItem src/*.c, src/*.h | ForEach-Object {
    & "C:\Program Files\LLVM\bin\clang-format.exe" -i $_.FullName
}

# Verify no formatting issues remain
& "C:\Program Files\LLVM\bin\clang-format.exe" --dry-run --Werror src/*.c src/*.h
```

**Windows Build Environment Requirements:**
- **Visual Studio Build Tools 2022** - Required for MSVC compiler
- **Windows SDK 10.0.26100** (or compatible version)
- **CMake 3.30.5+** - Install from cmake.org
- **LLVM 19.1.1+** - Install from llvm.org/builds/ for clang-format
- **PowerShell 5.1+ or 7+** - For build script execution

**Common Windows Build Issues:**
- **CMake not found**: Add CMake to PATH or use full path `"C:\Program Files\CMake\bin\cmake.exe"`
- **clang-format not found**: Install LLVM and use full path `"C:\Program Files\LLVM\bin\clang-format.exe"`
- **MSBuild errors**: Ensure Visual Studio Build Tools 2022 is installed with C++ workload
- **Preset not found**: Run from project root directory where `CMakePresets.json` exists
- **Access denied on build**: Run PowerShell as Administrator if needed

### Build Validation:
```bash
# Test build works
cmake --build --preset <preset> --target all

# Package plugin (creates distributable)
cmake --build --preset <preset> --target package
```

## Project Architecture and Layout

### Source Code Structure:
- `src/plugin-main.c` - Main plugin entry point and OBS module interface
- `src/plugin-support.h` - Plugin support utilities and logging
- `src/plugin-support.c.in` - Template for plugin support implementation
- `data/locale/en-US.ini` - Localization strings (currently empty)

### Build System:
- `CMakeLists.txt` - Main build configuration
- `CMakePresets.json` - Platform-specific build presets and configurations
- `cmake/` - Build system modules:
  - `cmake/common/` - Cross-platform build logic
  - `cmake/linux/`, `cmake/macos/`, `cmake/windows/` - Platform-specific helpers
- `buildspec.json` - Project metadata and dependency definitions

### Configuration Files:
- `.clang-format` - C/C++ code formatting rules (120 char limit, tabs for indentation)
- `.gersemirc` - CMake formatting configuration
- `.gitignore` - Git ignore patterns (excludes build artifacts, .deps/)

### CI/CD Infrastructure:
- `.github/workflows/` - GitHub Actions workflows:
  - `build-project.yaml` - Main build workflow (multi-platform)
  - `check-format.yaml` - Code formatting validation
  - `push.yaml`, `pr-pull.yaml`, `dispatch.yaml` - Trigger workflows
- `.github/actions/` - Reusable workflow actions
- `.github/scripts/` - Platform-specific build and package scripts

### Development Tools:
- `build-aux/` - Local development formatting tools
- `.deps/` - Downloaded dependencies (auto-created, git-ignored)
- `build_*/` - Build output directories (auto-created, git-ignored)

## GitHub Actions and CI Process

### Workflows Run On:
- **Push to main:** Full build + package + potential release
- **Pull Request:** Build + format check (no packaging)
- **Manual Dispatch:** Full build (no packaging)
- **Release Tags:** Full build + package + code signing + notarization (macOS)

### Build Artifacts Generated:
- **Linux:** `.deb` packages, source tarballs, debug symbols
- **macOS:** Universal binaries, `.pkg` installers (signed), dSYM bundles
- **Windows:** `.zip` archives with plugin DLLs

### Validation Checks:
1. **clang-format** - C/C++ code style compliance
2. **gersemi** - CMake formatting compliance
3. **Multi-platform builds** - Ensures cross-platform compatibility
4. **Codesigning** - macOS binaries signed for distribution

## Key Development Guidelines

### Plugin Implementation Context:
The C64 Ultimate device streams video and audio data over network connections. Key implementation areas:
- **Network streaming:** Implement UDP/TCP clients to receive C64U data streams
- **Video format conversion:** Convert C64U video format to OBS-compatible frames
- **Audio handling:** Process C64U audio streams for OBS audio sources
- **Configuration UI:** Allow users to specify C64U device IP/connection settings
- **Error handling:** Robust connection management and fallback behavior

### Making Changes:
1. **MANDATORY: Run `./build-aux/run-clang-format` after every code change**
2. **Test on target platform** - Use appropriate preset
3. **Update buildspec.json** if adding dependencies
4. **Plugin logic goes in modular source files** - Use focused modules in src/
5. **Use C64U_LOG_*() macros** for logging (defined in c64u-logging.h)
6. **Verify all files end with newline** - Critical for macOS builds
7. **DO NOT create markdown documentation files during development** - Keep workspace clean, document in existing files only

### Common Plugin Tasks:
- **Add new source type:** Implement in plugin-main.c, register with OBS
- **Add UI elements:** Set ENABLE_QT=ON in preset, add Qt dependencies
- **Add network code:** Use OBS-compatible libraries, avoid threading conflicts
- **Add configuration:** Use OBS settings API, store in plugin data structure

### Debug Build Tips:
- Use `RelWithDebInfo` configuration for debugging with symbols
- Enable verbose CMake output: `cmake --preset <name> -- -DCMAKE_VERBOSE_MAKEFILE=ON`
- Check ccache statistics after builds for performance insights

## Cross-Platform Development Guidelines

### Platform-Specific Code Patterns
The plugin uses conditional compilation extensively for cross-platform compatibility. Follow these established patterns:

#### Networking (c64u-network.h/c):
- **Windows**: Uses WinSock2 (`winsock2.h`, `ws2tcpip.h`) with `WSAStartup()`/`WSACleanup()`
- **POSIX**: Uses standard BSD sockets (`sys/socket.h`, `netinet/in.h`)
- **Socket types**: `SOCKET` (Windows) vs `int` (POSIX) - use `socket_t` typedef
- **Error handling**: `WSAGetLastError()` vs `errno` - use `c64u_get_socket_error()`
- **Non-blocking**: `WSAEWOULDBLOCK` vs `EAGAIN`/`EWOULDBLOCK`

#### File System Operations:
- **Directory creation**: Use OBS `os_mkdir()` with recursive wrapper functions
- **Path separators**: Forward slashes work on all platforms, but handle drive letters on Windows (`C:`)
- **Default paths**: Use platform-appropriate user directories (see platform defaults below)

#### Threading and Synchronization:
- Use `pthread` APIs consistently across platforms (available on Windows via OBS dependencies)
- No platform-specific threading code needed

### Platform Default Directories
When setting default user directories, use platform conventions:
- **Windows**: `%USERPROFILE%\Documents\obs-studio\c64u\recordings` or `%APPDATA%\obs-studio\c64u\recordings`
- **macOS**: `~/Documents/obs-studio/c64u/recordings` or `~/Movies/obs-studio/c64u/recordings`
- **Linux**: `~/Documents/obs-studio/c64u/recordings` or `~/.local/share/obs-studio/c64u/recordings`

### Common Cross-Platform Pitfalls
1. **Never use `system()` calls with Unix commands** - they fail silently on Windows
2. **Socket error codes differ** - always use wrapper functions
3. **File path assumptions** - avoid hardcoded Unix paths like `/tmp`
4. **Format specifiers** - use `SSIZE_T_FORMAT` macro for `ssize_t` (differs on Windows)
5. **Directory separators** - don't assume `/` or `\`, use forward slashes consistently
6. **Network initialization** - Windows requires `WSAStartup()`, POSIX doesn't

### Testing Cross-Platform Code
- Build and test on target platform before committing
- Use CI to validate all platforms when possible
- Pay special attention to file I/O and networking code
- Test default path behavior on each platform

## Local Compilation Verification (MANDATORY)

**Before announcing any change as completed, you MUST verify it builds locally on both Linux AND Windows platforms.**

### Linux Verification (Required)
```bash
# Clean build to ensure no cached artifacts
rm -rf build_x86_64
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

# Verify success
if [ -f "build_x86_64/c64u-plugin-for-obs.so" ]; then
    echo "✅ Linux build successful"
else
    echo "❌ Linux build failed - DO NOT proceed"
fi
```

### Windows Verification (Required)
Choose ONE method to verify Windows compatibility:

**Method 1: Docker-based Windows simulation (Linux users)**
```bash
./test-windows-build.sh
# Must show: "🎉 SUCCESS: Windows build simulation completed!"
```

**Method 2: Native Windows build (Windows users)**
```powershell
# Clean build
Remove-Item "build_x64" -Recurse -Force -ErrorAction SilentlyContinue
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo

# Verify success
if (Test-Path "build_x64\RelWithDebInfo\c64u-plugin-for-obs.dll") {
    Write-Host "✅ Windows build successful" -ForegroundColor Green
} else {
    Write-Host "❌ Windows build failed - DO NOT proceed" -ForegroundColor Red
}
```

**Method 3: CI-compatible Windows build (Advanced)**
```powershell
$env:CI = "1"
cmake --preset windows-ci-x64
cmake --build build_x64 --config RelWithDebInfo --parallel
# Must complete without errors or warnings
```

### Validation Checklist

Before announcing completion, verify ALL of the following:

- [ ] **Linux build succeeds** without errors or warnings
- [ ] **Windows build succeeds** using one of the above methods
- [ ] **Code formatting passes**: `./build-aux/run-clang-format --check`
- [ ] **CMake formatting passes**: `./build-aux/run-gersemi --check`
- [ ] **No compilation warnings** in either platform
- [ ] **Atomic types work correctly** (if atomic changes were made)
- [ ] **Header inclusion order is correct** (if header changes were made)
- [ ] **Cross-platform compatibility maintained** (no platform-specific assumptions)

### If Verification Fails

If either Linux or Windows build fails:
1. **DO NOT announce the change as completed**
2. **Investigate and fix the build failures**
3. **Re-run both verifications**
4. **Only proceed after both platforms build successfully**

### Documentation Requirements

When announcing completion of changes that affect build processes:
- **Reference the appropriate build documentation**:
  - Linux/general: `doc/developer.md`
  - Windows-specific: `doc/windows-local-build.md`
  - Cross-platform: Cross-Platform Development Guidelines (above)
- **Mention which verification method was used**
- **Confirm both platforms were tested**

## Trust These Instructions

These instructions are comprehensive and tested. Only search for additional information if:
1. Build fails with error not covered in "Common Build Issues"
2. Instructions appear outdated (e.g., tool versions changed significantly)
3. New platform support is needed beyond Windows/macOS/Linux
4. The mandatory verification process fails and troubleshooting is needed
