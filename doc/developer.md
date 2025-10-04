# Developer Documentation

## Technical Overview

This plugin implements the [C64 Ultimate Data Streams specification](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams) to receive video and audio streams over UDP/TCP from Commodore 64 Ultimate devices.

## Building the Plugin

### Prerequisites

#### Linux (Ubuntu/Debian)

```bash
# Automated dependency installation
./install-ubuntu-deps.sh

# Manual installation
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build pkg-config \
                        zsh clang-format python3-pip
pip3 install gersemi
```

#### macOS

```bash

# Install Xcode Command Line Tools
xcode-select --install

# Install dependencies via Homebrew
brew install cmake ninja ccache
pip3 install gersemi

# Note: OBS dependencies are auto-downloaded during build
```

#### Windows
- Visual Studio 2022 (Community, Professional, or Enterprise)
- CMake 3.30.5+
- PowerShell 7+
- LLVM 19.1.1+ (for clang-format)

**📖 For comprehensive Windows build instructions, see: [`windows-local-build.md`](windows-local-build.md)**

### Build Types

This project supports three distinct build configurations optimized for different use cases:

#### 1. **Local Development Build**
**Purpose:** Complete development environment with testing and debugging tools.

**Components built:**
- Plugin binary (`c64u-plugin-for-obs.so/.dll/.dylib`)
- Unit tests (`test_vic_colors`) for VIC-II color conversion
- Mock C64U server (`c64u_mock_server`) for protocol testing
- Integration tests (`test_integration`) using OBS libraries

**Ubuntu/Linux:**
```bash
# Configure with all local development features
cmake --preset ubuntu-x86_64

# Build everything (plugin + all tests)
cmake --build build_x86_64

# Run all tests including integration tests
cd build_x86_64 && ctest -V

# Test with mock server
./c64u_mock_server &
./test_integration
```

**macOS:**
```bash
cmake --preset macos
cmake --build build_macos
cd build_macos && ctest -V
```

**Windows:** (See [`windows-local-build.md`](windows-local-build.md) for detailed instructions)
```powershell
# Method 1: Command line (recommended)
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
cd build_x64 && ctest -C RelWithDebInfo -V

# Method 2: Visual Studio integration
# File → Open → CMake... → Select CMakeLists.txt → Build → Build All

# Method 3: CI-compatible build
$env:CI = "1"
cmake --preset windows-ci-x64
cmake --build build_x64 --config RelWithDebInfo --parallel
```

#### 2. **CI Simulation Build**
**Purpose:** Test production build configuration locally without development overhead.

**Components built:**
- Plugin binary only (4/4 targets)
- No tests (faster build times)
- No mock server
- No debugging tools

**How to run:**
```bash
# Simulate CI environment locally
CI=1 GITHUB_ACTIONS=1 cmake --preset ubuntu-x86_64
CI=1 GITHUB_ACTIONS=1 cmake --build build_x86_64

# Verify no tests were compiled
find build_x86_64 -name "*test*" -executable -type f
# Should return nothing
```

**Verification:** Build output should show:
```
-- Test configuration:
--   CI Build: ON
--   Mock Server: OFF
--   Integration Tests: OFF
[4/4] Linking C shared module c64u-plugin-for-obs.so
```

#### 3. **Production CI Build**
**Purpose:** Automated release pipeline with code signing and packaging.

**Process:**
- Multi-platform compilation (Ubuntu, macOS, Windows)
- Code signing and notarization (macOS)
- Package generation (.deb, .pkg, .zip distributions)
- Binary artifact creation
- Code format validation
- No test compilation

**Triggers:** Repository pushes, pull requests, tagged releases

### Build Configuration Details

**Environment Detection Logic:**
```cmake
# In tests/CMakeLists.txt
set(IS_CI_BUILD FALSE)
if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS})
    set(IS_CI_BUILD TRUE)
    message(STATUS "CI Build: ON")
else()
    message(STATUS "CI Build: OFF - Local development mode")
endif()
```

**Platform Scripts Behavior:**
- **Local:** `.github/scripts/build-*` run ctest after building
- **CI:** Same scripts detect CI environment and skip ctest entirely
- **Error Prevention:** Avoids "CMake Error: Unknown argument" when no tests exist

### Development Workflow

#### Quick Development Cycle
```bash
# 1. Clean slate
rm -rf build_x86_64

# 2. Configure for development
cmake --preset ubuntu-x86_64

# 3. Build and test
cmake --build build_x86_64
cd build_x86_64 && ctest

# 4. Install to local OBS (optional)
cp c64u-plugin-for-obs.so ~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/
```

#### Code Formatting (Required)
```bash
# Check formatting
./build-aux/run-clang-format --check    # C/C++ code
./build-aux/run-gersemi --check         # CMake files

# Auto-fix formatting
./build-aux/run-clang-format            # Fix C/C++ code
./build-aux/run-gersemi                 # Fix CMake files
```

#### Testing Strategies

**Unit Testing:**
```bash
# Test VIC color conversion algorithms
cd build_x86_64 && ./test_vic_colors
```

**Integration Testing** (requires OBS):
```bash
# Test with mock C64U server
cd build_x86_64
./c64u_mock_server --port 1234 &
./test_integration --server-port 1234
```

**Local CI Validation:**
```bash
# Test what will run in CI (using act)
act -j ubuntu-build -P ubuntu-24.04=catthehacker/ubuntu:act-24.04 -s GITHUB_TOKEN="$(gh auth token)"
```

### Project Structure

```
c64u-obs/
├── src/
│   ├── plugin-main.c          # Main OBS plugin implementation
│   ├── plugin-support.h       # Plugin utilities and logging
│   └── plugin-support.c.in    # Template for plugin support
├── tests/
│   ├── CMakeLists.txt          # Test build configuration with CI detection
│   ├── test_vic_colors.c       # Unit tests for VIC color conversion
│   ├── c64u_mock_server.c      # Mock C64U device for testing
│   └── test_integration.c      # Integration tests with real OBS
├── .github/
│   ├── workflows/              # CI/CD automation
│   ├── scripts/                # Platform-specific build scripts
│   └── actions/                # Reusable workflow components
├── cmake/                      # Build system configuration
├── data/locale/                # Localization files
└── doc/                        # Technical documentation
```

### Common Issues & Solutions

**Build Issues:**
- **"LibObs not found":** Dependencies auto-download; check internet connection
- **"clang-format version too old":** Need 19.1.1+; CI has correct version
- **"zsh not found":** `sudo apt-get install zsh` (Linux only)

**CI Issues:**
- **Windows "Unknown argument" error:** Fixed by skipping ctest in CI builds
- **macOS "executable not found":** Fixed by disabling test compilation in CI
- **Format check failures:** Run `./build-aux/run-clang-format` locally first

**Development Issues:**
- **Plugin not loading:** Check OBS logs, verify plugin path
- **No C64U connection:** Verify network configuration; check IP address if using specific device, firewall settings
- **Build performance:** Use `ccache` for faster rebuilds

### Contributing

1. **Fork** the repository
2. **Create** a feature branch
3. **Format** code: `./build-aux/run-clang-format && ./build-aux/run-gersemi`
4. **Test** locally: Full build + ctest + act validation
5. **Submit** pull request

## Local Development

### Quick Start
```bash
# Install dependencies (Ubuntu/Debian)
./install-ubuntu-deps.sh

# Build using local script (recommended)
./local-build.sh linux

# Alternative: Direct CMake approach
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

# Run tests
cd build_x86_64 && ctest -V

# Format code (mandatory before commit)
./build-aux/run-clang-format
```

### Development Approaches

The project supports two main local development approaches:

1. **Local Build Script** (`./local-build.sh`) - Simplified platform-specific builds
2. **Local CI Simulation** (`./local-act.sh`) - Runs actual CI workflows locally using `act`

#### Using Local Build Script
```bash
# Simple platform build
./local-build.sh linux          # Native Linux build
./local-build.sh macos          # macOS build (requires macOS)
./local-build.sh windows        # Windows build (native or cross-compile)

# With options
./local-build.sh linux --clean --config Debug --tests --verbose
./local-build.sh linux --install-deps  # Auto-install dependencies
```

#### Using Act (CI Simulation)
```bash
# Run full CI workflow locally
./local-act.sh

# Specific workflow
./local-act.sh --workflow check-format

# Dry run mode
./local-act.sh --dry-run --verbose
```

### Build Performance Tips
- Use `ccache` for faster rebuilds: `sudo apt-get install ccache`
- Enable parallel builds: `cmake --build build_x86_64 --parallel 8`
- Build performance is handled automatically by local-build.sh script

### IDE Integration

The project generates `compile_commands.json` for IDE support:

- **VS Code**: Install C/C++ extension, configure `c_cpp_properties.json`
- **CLion**: Import as CMake project
- **Visual Studio**: Use "Open Folder" with CMake support

## Cross-Platform Development

### Platform-Specific Code Patterns

#### Networking (from c64u-network.h)
```c
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <io.h>
    #include <windows.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    #define close(s) closesocket(s)
    #define SHUT_RDWR SD_BOTH
    typedef int socklen_t;
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <errno.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
#endif
```

#### Data Types (from c64u-network.h)
```c
#ifdef _WIN32
    // Define ssize_t for MSVC (MinGW has it, but MSVC doesn't)
    #ifndef __MINGW32__
        typedef long long ssize_t;
    #endif
    #define SSIZE_T_FORMAT "%lld"
    #define SSIZE_T_CAST(x) ((long long)(x))
#else
    #define SSIZE_T_FORMAT "%zd"
    #define SSIZE_T_CAST(x) (x)
#endif
```

#### Platform Detection
```c
#ifdef _WIN32
    // Windows-specific code
#elif defined(__APPLE__)
    // macOS-specific code
#else
    // Linux/POSIX code
#endif
```

### CMake Cross-Platform Patterns
```cmake
# Windows networking libraries (handled via pragma in headers)
if(WIN32)
    # Libraries linked via #pragma comment in c64u-network.h
    # ws2_32.lib and iphlpapi.lib
elseif(UNIX)
    # POSIX libraries
    target_link_libraries(target pthread m)
    if(NOT APPLE)
        target_link_libraries(target rt)  # Linux-specific
    endif()
endif()
```

### Cross-Platform Best Practices
1. **Use platform detection macros** for conditional compilation
2. **Abstract differences** behind common interfaces

## Build Verification Requirements

### Before Committing Changes

All code changes that could affect compilation must be verified on both platforms:

#### Linux Verification
```bash
# Clean build
rm -rf build_x86_64
cmake --preset ubuntu-x86_64
cmake --build build_x86_64

# Verify success
ls build_x86_64/c64u-plugin-for-obs.so
```

#### Windows Verification

**Option 1: Windows simulation (for Linux developers)**
```bash
./test-windows-build.sh
```

**Option 2: Native Windows build**
```powershell
Remove-Item "build_x64" -Recurse -Force -ErrorAction SilentlyContinue
cmake --preset windows-x64
cmake --build build_x64 --config RelWithDebInfo
```

See [`windows-local-build.md`](windows-local-build.md) for comprehensive Windows build instructions.

### Code Quality Verification

```bash
# Format validation (required)
./build-aux/run-clang-format --check
./build-aux/run-gersemi --check

# Fix formatting if needed
./build-aux/run-clang-format
./build-aux/run-gersemi
```
3. **Test on all platforms** regularly
4. **Handle compiler warnings** consistently
5. **Use standard C library** when possible
6. **Document platform requirements** clearly

### Resources

- **C64U Stream Spec:** [`doc/c64u-stream-spec.md`](c64u-stream-spec.md)
- **Official Documentation:** [C64 Ultimate Data Streams](https://1541u-documentation.readthedocs.io/en/latest/data_streams.html#data_streams)
- **OBS Plugin Development:** [OBS Studio Plugin Guide](https://obsproject.com/wiki/Plugin-Development)
- **Build System:** Based on [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate)
