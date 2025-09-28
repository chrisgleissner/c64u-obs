# Local Development Guide for C64U OBS Plugin

This guide provides comprehensive instructions for building and testing the C64U OBS Plugin locally across all three supported platforms (Linux, macOS, Windows) without relying on GitHub Actions.

## Overview

The C64U OBS Plugin project supports two main local development approaches:

1. **Direct Local Builds** (`local-build.sh`) - Native or cross-platform compilation
2. **Local GitHub Actions** (`local-act.sh`) - Runs actual CI workflows locally using `act`

## Prerequisites

### Common Requirements
- **CMake 3.28+** (3.30.5+ recommended for Windows/macOS)
- **Git** for source control
- **Docker** (for act-based workflows only)
- At least 4GB free disk space

### Platform-Specific Requirements

#### Linux (Ubuntu/Debian)
- GCC 7+ or Clang 10+
- `build-essential`, `ninja-build`, `pkg-config`
- `clang-format` (version 19.1.1+ for CI compatibility)
- `gersemi` (for CMake formatting)

#### macOS
- Xcode 16.0+ with command line tools
- Homebrew (recommended for dependencies)

#### Windows
- **Native**: Visual Studio 2022 or Build Tools
- **Cross-compilation**: MinGW-w64 on Linux

## Quick Start

### 1. Install Dependencies

**Ubuntu/Debian:**
```bash
./install-ubuntu-deps.sh
```

**macOS:**
```bash
brew install cmake ninja ccache
pip3 install gersemi
```

**Windows:**
```powershell
# Install Visual Studio 2022 Community
# Or install via winget:
winget install Microsoft.VisualStudio.2022.Community
```

### 2. Local Builds

**Build for your current platform:**
```bash
./local-build.sh
```

**Build for specific platform:**
```bash
./local-build.sh linux --config Release --tests
./local-build.sh macos --verbose
./local-build.sh windows --clean --install-deps
```

### 3. Local GitHub Actions Testing

**Test full CI pipeline:**
```bash
./local-act.sh
```

**Test specific platforms:**
```bash
./local-act.sh --platform ubuntu --verbose
./local-act.sh --workflow check-format --dry-run
```

## Detailed Usage

### Local Build Script (`local-build.sh`)

This script provides native and cross-platform builds with minimal dependencies on GitHub infrastructure.

#### Syntax
```bash
./local-build.sh <platform> [options]
```

#### Options
- `--config <CONFIG>` - Build configuration (Debug, Release, RelWithDebInfo, MinSizeRel)
- `--clean` - Clean build directory before building
- `--tests` - Run tests after building
- `--install-deps` - Install build dependencies automatically
- `--verbose` - Enable verbose build output

#### Examples
```bash
# Basic builds
./local-build.sh linux
./local-build.sh macos --config Release
./local-build.sh windows --install-deps

# Development workflow
./local-build.sh linux --clean --tests --verbose
./local-build.sh macos --config Debug --tests

# Cross-compilation (Windows on Linux)
./local-build.sh windows  # Uses MinGW if available
```

#### Output Locations
- **Linux**: `build_x86_64/c64u-plugin-for-obs.so`
- **macOS**: `build_macos/c64u-plugin-for-obs.dylib`  
- **Windows**: `build_x64/RelWithDebInfo/c64u-plugin-for-obs.dll`

### Local GitHub Actions (`local-act.sh`)

This script uses the `act` tool to run actual GitHub Actions workflows locally, providing the most accurate CI simulation.

#### Syntax
```bash
./local-act.sh [options]
```

#### Options
- `--workflow <WORKFLOW>` - Specific workflow (build-project, check-format, all)
- `--platform <PLATFORM>` - Platform filter (ubuntu, macos, windows)
- `--event <EVENT>` - Event type (push, pull_request, workflow_dispatch)
- `--job <JOB>` - Run specific job only
- `--dry-run` - Show execution plan without running
- `--no-pull` - Use cached Docker images
- `--reuse` - Reuse containers between runs
- `--verbose` - Enable verbose output

#### Examples
```bash
# Full CI simulation
./local-act.sh

# Platform-specific testing
./local-act.sh --platform ubuntu --verbose
./local-act.sh --platform windows --dry-run

# Workflow-specific testing
./local-act.sh --workflow check-format
./local-act.sh --workflow build-project --event pull_request

# Specific job testing
./local-act.sh --job ubuntu-build --verbose
```

## Troubleshooting

### Common Issues and Solutions

#### 1. Ubuntu: "Could NOT find SIMDe" Error
**Problem**: Missing SIMDe dependency for libobs
**Solution**: 
```bash
# Install SIMDe manually
./install-ubuntu-deps.sh
# Or let build system download OBS SDK automatically
```

#### 2. macOS: "Unable to find executable" for tests
**Problem**: Test executables built in wrong directory
**Solution**: Fixed in CMakeLists.txt with proper runtime output directories

#### 3. Windows: pthread/networking compilation errors
**Problem**: Cross-platform code compatibility
**Solution**: Fixed with Windows-specific threading and networking APIs

#### 4. Format Check Failures
**Problem**: Code doesn't meet style requirements
**Solution**:
```bash
# Check what needs fixing
./build-aux/run-clang-format --check
./build-aux/run-gersemi --check

# Auto-fix formatting
./build-aux/run-clang-format
./build-aux/run-gersemi
```

#### 5. Act: Docker Images Too Large
**Problem**: Initial Docker pulls are 2GB+
**Solution**:
```bash
# Use smaller images (may have limitations)
echo 'ubuntu-24.04=node:16-buster-slim' >> .github/act-platforms.json

# Clean up after testing
docker system prune -f
```

### Build System Architecture

The project uses a multi-layered build system:

1. **CMake Presets** (`CMakePresets.json`) - Platform-specific configurations
2. **Build Scripts** (`.github/scripts/`) - CI-compatible build logic
3. **Local Scripts** (`local-build.sh`, `local-act.sh`) - Development convenience
4. **Dependency Management** (`buildspec.json`) - Automatic OBS SDK download

### Platform-Specific Notes

#### Linux Development
- Uses system package manager for dependencies where possible
- Falls back to downloaded OBS SDK if system libobs incompatible
- Cross-compilation support for Windows targets

#### macOS Development  
- Requires Xcode 16.0+ for native builds
- Uses Homebrew for dependency management
- Universal binary support (Intel + Apple Silicon)

#### Windows Development
- **Native**: Requires Visual Studio 2022
- **Cross-compilation**: Uses MinGW-w64 on Linux
- PowerShell scripts for native Windows builds

### Testing Strategy

The project includes comprehensive testing at multiple levels:

1. **Unit Tests** (`test_vic_colors`) - Core algorithm validation
2. **Integration Tests** (`test_integration`) - Mock C64U device simulation  
3. **Format Tests** - Code style validation
4. **Build Tests** - Cross-platform compilation

### Continuous Integration Simulation

The `local-act.sh` script provides high-fidelity CI simulation:

- **Advantages**: Exact CI environment reproduction, Docker isolation
- **Limitations**: Large images, Linux-only containers, some GitHub features unavailable
- **Use Cases**: Pre-commit testing, CI debugging, workflow validation

## Advanced Usage

### Custom CMake Configuration

```bash
# Configure with custom options
cmake --preset ubuntu-x86_64 \
  -DENABLE_TESTS=ON \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_VERBOSE_MAKEFILE=ON

# Build with parallel jobs
cmake --build build_x86_64 --parallel 8
```

### Cross-Platform Development

```bash
# Linux -> Windows cross-compilation
sudo apt-get install gcc-mingw-w64-x86-64
./local-build.sh windows

# macOS universal binary
./local-build.sh macos --config Release
```

### Integration with IDEs

The project generates `compile_commands.json` for IDE integration:

**VS Code**: Install C/C++ extension, configure `c_cpp_properties.json`
**CLion**: Import as CMake project
**Visual Studio**: Use "Open Folder" with CMake support

## Performance Optimization

### Build Performance
- Use `ccache` for faster rebuilds
- Enable parallel builds: `cmake --build --parallel`
- Use Ninja generator for faster builds: `cmake --preset <preset> -G Ninja`

### Development Workflow
```bash
# Fast development iteration
./local-build.sh --clean --config Debug --tests --verbose

# Pre-commit validation
./local-act.sh --workflow check-format --dry-run
./build-aux/run-clang-format --check
```

## Contributing Guidelines

### Before Committing
1. Run format checks: `./build-aux/run-clang-format --check`
2. Test locally: `./local-build.sh <platform> --tests`
3. Validate CI: `./local-act.sh --dry-run`

### Code Style
- C17 standard compliance
- 120 character line limit
- Tab indentation for C/C++ 
- Follow existing patterns

### Pull Request Testing
```bash
# Simulate PR workflow
./local-act.sh --event pull_request --verbose

# Test specific changes
./local-build.sh linux --clean --tests
```

This comprehensive local development setup ensures you can build, test, and validate the C64U OBS Plugin across all platforms without depending on GitHub Actions, while still providing full CI simulation when needed.