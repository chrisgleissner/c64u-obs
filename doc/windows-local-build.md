# Windows Local Build Guide

## Overview

This guide provides comprehensive instructions for building the C64U OBS Plugin locally on Windows. It covers all build scenarios from quick development builds to full CI-compatible builds that match the GitHub Actions environment.

## Prerequisites

### Required Software

1. **Visual Studio 2022** (Community, Professional, or Enterprise)
   - Download from: https://visualstudio.microsoft.com/downloads/
   - Required workloads: "Desktop development with C++"
   - Required components: MSVC v143, Windows 11 SDK (10.0.26100.0 or later)

2. **CMake 3.30.5 or later**
   - Download from: https://cmake.org/download/
   - Add to PATH during installation
   - Verify: `cmake --version`

3. **PowerShell 7+** (recommended)
   - Download from: https://github.com/PowerShell/PowerShell/releases
   - Verify: `$PSVersionTable.PSVersion`

4. **Git for Windows**
   - Download from: https://git-scm.com/download/win
   - Required for cloning and version control

### Optional but Recommended

1. **LLVM 19.1.1+** (for code formatting)
   - Download from: https://llvm.org/builds/
   - Provides `clang-format.exe` for code style validation
   - Add LLVM bin directory to PATH

2. **Python 3.8+** (for CMake formatting)
   - Install gersemi: `pip install gersemi`

## Build Methods

### Method 1: Visual Studio CMake Integration (Recommended for Development)

1. **Open Visual Studio 2022**
2. **File → Open → CMake...**
3. **Select `CMakeLists.txt`** in the project root
4. **Wait for CMake configuration** (may take several minutes for dependency download)
5. **Select configuration**: `x64-RelWithDebInfo` (recommended for debugging)
6. **Build → Build All** or press `Ctrl+Shift+B`

**Benefits:**
- Integrated debugging support
- IntelliSense code completion
- Integrated testing and breakpoints
- Visual Studio's advanced debugging tools

### Method 2: Command Line Build (Matches CI Environment)

#### Quick Development Build

Open **PowerShell** in the project directory:

```powershell
# Configure the build (downloads OBS dependencies automatically)
cmake --preset windows-x64

# Build the plugin
cmake --build build_x64 --config RelWithDebInfo

# Verify build succeeded
if (Test-Path "build_x64\RelWithDebInfo\c64u-plugin-for-obs.dll") {
    Write-Host "✅ Build successful!" -ForegroundColor Green
} else {
    Write-Host "❌ Build failed!" -ForegroundColor Red
}
```

#### CI-Compatible Build (Full Verification)

This method exactly matches the GitHub Actions CI environment:

```powershell
# Set CI environment (disables tests, enables warnings-as-errors)
$env:CI = "1"

# Configure with CI settings (uses windows-ci-x64 preset)
cmake --preset windows-ci-x64

# Build exactly as CI does
cmake --build build_x64 --config RelWithDebInfo --parallel -- /consoleLoggerParameters:Summary /noLogo

# Install to release directory (same as CI)
cmake --install build_x64 --prefix release/RelWithDebInfo --config RelWithDebInfo

# Verify plugin exists
if (Test-Path "release\RelWithDebInfo\obs-plugins\64bit\c64u-plugin-for-obs.dll") {
    Write-Host "✅ CI-compatible build successful!" -ForegroundColor Green
} else {
    Write-Host "❌ CI-compatible build failed!" -ForegroundColor Red
}
```

### Method 3: Using Build Scripts (Advanced)

For developers who want to use the exact same build process as CI:

```powershell
# Run the official CI build script
.github\scripts\Build-Windows.ps1 -Target x64 -Configuration RelWithDebInfo
```

**Note:** This requires `CI=1` environment variable to be set.

## Build Configurations

### Available Configurations

- **Debug**: Full debug symbols, no optimization, assertions enabled
- **RelWithDebInfo**: Optimized code with debug symbols (recommended for development)
- **Release**: Full optimization, no debug symbols (smallest binaries)
- **MinSizeRel**: Optimized for size, no debug symbols

### Configuration Examples

```powershell
# Debug build (largest, slowest, best for debugging)
cmake --build build_x64 --config Debug

# Release build (smallest, fastest, for production)
cmake --build build_x64 --config Release

# RelWithDebInfo (balanced, recommended)
cmake --build build_x64 --config RelWithDebInfo
```

## Testing Your Build

### Method 1: Manual OBS Installation

```powershell
# Create OBS plugin directory (adjust path to your OBS installation)
$ObsPluginDir = "$env:APPDATA\obs-studio\plugins\c64u-plugin-for-obs"
New-Item -ItemType Directory -Path "$ObsPluginDir\bin\64bit" -Force
New-Item -ItemType Directory -Path "$ObsPluginDir\data" -Force

# Copy plugin binary
Copy-Item "build_x64\RelWithDebInfo\c64u-plugin-for-obs.dll" "$ObsPluginDir\bin\64bit\"

# Copy plugin data
Copy-Item "data\*" "$ObsPluginDir\data\" -Recurse -Force

Write-Host "✅ Plugin installed to OBS!" -ForegroundColor Green
Write-Host "Restart OBS Studio to load the plugin." -ForegroundColor Yellow
```

### Method 2: Test in Development Environment

```powershell
# Run OBS with plugin path (for testing without installation)
$ObsPath = "C:\Program Files\obs-studio\bin\64bit\obs64.exe"  # Adjust path
$PluginPath = (Get-Location).Path + "\build_x64\RelWithDebInfo"

& $ObsPath --plugin-dir $PluginPath
```

## Code Formatting and Validation

### Format Code (Required Before Commits)

```powershell
# Format all C/C++ code
& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/*.c src/*.h

# Verify no formatting issues remain
& "C:\Program Files\LLVM\bin\clang-format.exe" --dry-run --Werror src/*.c src/*.h

# Format CMake files (if gersemi is installed)
gersemi --in-place CMakeLists.txt cmake/
```

### Validate Build Quality

```powershell
# Check for common Windows build issues
if (Get-ChildItem "build_x64" -Filter "*.pdb" -Recurse) {
    Write-Host "✅ Debug symbols generated" -ForegroundColor Green
} else {
    Write-Host "⚠️ No debug symbols found" -ForegroundColor Yellow
}

# Verify plugin dependencies
dumpbin /dependents "build_x64\RelWithDebInfo\c64u-plugin-for-obs.dll"
```

## Troubleshooting

### Common Issues and Solutions

#### "CMake not found"
```powershell
# Add CMake to PATH or use full path
& "C:\Program Files\CMake\bin\cmake.exe" --preset windows-x64
```

#### "MSVC not found"
- Ensure Visual Studio 2022 with C++ workload is installed
- Run from "Developer PowerShell for VS 2022" or "Developer Command Prompt"

#### "clang-format not found"
```powershell
# Install LLVM and use full path
& "C:\Program Files\LLVM\bin\clang-format.exe" -i src/*.c
```

#### "Access denied" during build
- Run PowerShell as Administrator if needed
- Check antivirus software isn't blocking file access

#### Dependency download failures
```powershell
# Clear dependency cache and retry
Remove-Item ".deps" -Recurse -Force -ErrorAction SilentlyContinue
cmake --preset windows-x64
```

#### "Unknown argument" error from ctest
This is expected in CI builds where tests are disabled. For local development:
```powershell
# Use local preset instead of CI preset
cmake --preset windows-x64  # Not windows-ci-x64
```

### Build Performance Tips

1. **Use Ninja generator** (faster than MSBuild):
   ```powershell
   cmake -G Ninja -B build_ninja --preset windows-x64
   cmake --build build_ninja
   ```

2. **Enable parallel compilation**:
   ```powershell
   cmake --build build_x64 --parallel
   ```

3. **Use ccache** (if available):
   ```powershell
   $env:CCACHE_DIR = ".ccache"
   cmake --preset windows-x64 -DENABLE_CCACHE=ON
   ```

## Integration with Development Tools

### Visual Studio Code

1. Install the **C/C++** and **CMake Tools** extensions
2. Open the project folder
3. Select the `windows-x64` CMake preset
4. Use `Ctrl+Shift+P` → "CMake: Build" to build

### CLion

1. Open the project directory
2. CLion will auto-detect CMakeLists.txt
3. Select the desired build configuration
4. Build using `Ctrl+F9`

### Qt Creator

1. Open CMakeLists.txt as a project
2. Configure with the desired preset
3. Build using `Ctrl+B`

## Validation Checklist

Before submitting changes, verify your build meets all requirements:

- [ ] Build completes without errors
- [ ] Build completes without warnings (CI uses `-Werror`)
- [ ] Code passes clang-format validation
- [ ] Plugin loads successfully in OBS Studio
- [ ] Plugin shows up in Sources list
- [ ] All atomic operations compile correctly
- [ ] No Windows-specific compatibility issues

## Advanced Topics

### Cross-Compilation for Windows

For Linux developers who want to test Windows compatibility:

```bash
# Using our Windows simulation script
./test-windows-build.sh

# Or manual cross-compilation with mingw-w64
sudo apt-get install mingw-w64
cmake -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-toolchain.cmake -B build_mingw
cmake --build build_mingw
```

### Performance Profiling

```powershell
# Build with profiling enabled
cmake --preset windows-x64 -DENABLE_PROFILING=ON
cmake --build build_x64 --config RelWithDebInfo

# Use Visual Studio Performance Profiler or Intel VTune
```

### Debugging Tips

1. **Attach to OBS process**:
   - Build in RelWithDebInfo mode
   - Start OBS normally
   - Attach Visual Studio debugger to obs64.exe

2. **Debug plugin loading**:
   - Set breakpoints in `obs_module_load()`
   - Check OBS log files: `%APPDATA%\obs-studio\logs\`

3. **Network debugging**:
   - Use Wireshark to capture UDP packets
   - Check Windows Firewall settings
   - Verify port accessibility

This comprehensive guide ensures you can build and test the plugin exactly as the CI environment does, providing confidence that your changes will work in production.
