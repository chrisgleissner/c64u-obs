#!/bin/bash

# C64U OBS Plugin - Docker Build Script
# Replicates GitHub Actions workflow for local development

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

# Default values
PLATFORM="ubuntu"
INSTALL_PLUGIN=false
START_OBS=false
OBS_PLUGIN_DIR=""
DOCKER_IMAGE=""
BUILD_CONFIG="RelWithDebInfo"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

usage() {
    cat << EOF
Usage: $0 [PLATFORM] [OPTIONS]

PLATFORMS:
    ubuntu      Build for Ubuntu/Linux (default)
    macos       Build for macOS
    windows     Build for Windows

OPTIONS:
    --install           Copy built plugin to OBS plugin directory
    --start-obs         Start OBS after installation (implies --install)
    --obs-dir DIR       Custom OBS plugin directory
    --config CONFIG     Build configuration (Debug, RelWithDebInfo, Release, MinSizeRel)
    --help             Show this help message

EXAMPLES:
    $0                                    # Build Ubuntu version
    $0 ubuntu --install                  # Build and install to default OBS directory
    $0 windows --config Release          # Build Windows release version
    $0 macos --install --start-obs       # Build, install, and start OBS

NOTES:
    - The script replicates the GitHub Actions build workflow using Docker
    - Ubuntu builds use the same environment as CI (ubuntu-24.04)
    - Built plugins are placed in build_PLATFORM directories
    - --install requires OBS to be installed locally
EOF
}

detect_obs_plugin_dir() {
    case "$PLATFORM" in
        ubuntu|linux)
            # Try common Linux OBS plugin directories
            if [[ -d "$HOME/.config/obs-studio/plugins" ]]; then
                OBS_PLUGIN_DIR="$HOME/.config/obs-studio/plugins"
            elif [[ -d "/usr/share/obs/obs-plugins" ]]; then
                OBS_PLUGIN_DIR="/usr/share/obs/obs-plugins"
            else
                log_warning "Could not detect OBS plugin directory for Linux"
                log_info "Common locations:"
                log_info "  ~/.config/obs-studio/plugins/"
                log_info "  /usr/share/obs/obs-plugins/"
                return 1
            fi
            ;;
        macos)
            if [[ -d "$HOME/Library/Application Support/obs-studio/plugins" ]]; then
                OBS_PLUGIN_DIR="$HOME/Library/Application Support/obs-studio/plugins"
            else
                log_warning "Could not detect OBS plugin directory for macOS"
                log_info "Expected location: ~/Library/Application Support/obs-studio/plugins/"
                return 1
            fi
            ;;
        windows)
            # Note: This would need to be handled differently in a real Windows environment
            log_warning "Windows installation not supported in this Linux/Docker environment"
            log_info "Copy build_x64/RelWithDebInfo/c64u-plugin-for-obs.dll to:"
            log_info "  %ProgramFiles%\\obs-studio\\obs-plugins\\64bit\\"
            log_info "  or %APPDATA%\\obs-studio\\plugins\\c64u-plugin-for-obs\\bin\\64bit\\"
            return 1
            ;;
    esac
    return 0
}

setup_docker_image() {
    case "$PLATFORM" in
        ubuntu|linux)
            DOCKER_IMAGE="ubuntu:24.04"
            ;;
        macos)
            log_error "macOS builds require macOS host system - Docker cross-compilation not supported"
            log_info "Use native macOS build instead:"
            log_info "  cmake --preset macos && cmake --build build_macos"
            exit 1
            ;;
        windows)
            log_error "Windows builds require Windows host system - Docker cross-compilation not supported"
            log_info "Use native Windows build instead:"
            log_info "  cmake --preset windows-x64 && cmake --build build_x64 --config $BUILD_CONFIG"
            exit 1
            ;;
    esac
}

build_ubuntu_docker() {
    log_info "Building C64U plugin for Ubuntu using Docker..."
    log_info "Using image: $DOCKER_IMAGE"
    log_info "Build config: $BUILD_CONFIG"

    # Create build script for Docker
    cat > "$PROJECT_ROOT/docker-build.sh" << 'EOF'
#!/bin/bash
set -euo pipefail

# Update package lists
apt-get update

# Install build dependencies (matching GitHub Actions)
DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    libobs-dev \
    libobs0t64 \
    zsh \
    clang-format-19 \
    python3-pip \
    git

# Install gersemi for CMake formatting
pip3 install gersemi

# Build the plugin
cd /workspace
cmake --preset ubuntu-x86_64
cmake --build build_x86_64 --config RelWithDebInfo

# Run formatting checks
echo "Checking code formatting..."
clang-format-19 --dry-run --Werror src/plugin-main.c tests/*.c || {
    echo "Code formatting issues found. Run 'clang-format-19 -i src/plugin-main.c tests/*.c' to fix."
    exit 1
}

gersemi --check tests/CMakeLists.txt || {
    echo "CMake formatting issues found. Run 'gersemi -i tests/CMakeLists.txt' to fix."
    exit 1
}

echo "Build completed successfully!"
ls -la build_x86_64/
EOF

    chmod +x "$PROJECT_ROOT/docker-build.sh"

    # Run Docker build
    docker run --rm \
        -v "$PROJECT_ROOT:/workspace" \
        -w /workspace \
        "$DOCKER_IMAGE" \
        /workspace/docker-build.sh

    # Cleanup
    rm -f "$PROJECT_ROOT/docker-build.sh"

    log_success "Ubuntu build completed!"
    log_info "Plugin built: build_x86_64/c64u-plugin-for-obs.so"
}

install_plugin() {
    if [[ "$PLATFORM" != "ubuntu" ]]; then
        log_warning "Plugin installation only supported for Ubuntu builds in this script"
        return 1
    fi

    if [[ -z "$OBS_PLUGIN_DIR" ]] && ! detect_obs_plugin_dir; then
        log_error "Could not determine OBS plugin directory"
        log_info "Specify with --obs-dir /path/to/obs/plugins"
        return 1
    fi

    log_info "Installing plugin to: $OBS_PLUGIN_DIR"

    # Create plugin directory structure
    PLUGIN_INSTALL_DIR="$OBS_PLUGIN_DIR/c64u-plugin-for-obs"
    mkdir -p "$PLUGIN_INSTALL_DIR/bin/64bit"

    # Copy the built plugin
    if [[ -f "$PROJECT_ROOT/build_x86_64/c64u-plugin-for-obs.so" ]]; then
        cp "$PROJECT_ROOT/build_x86_64/c64u-plugin-for-obs.so" \
           "$PLUGIN_INSTALL_DIR/bin/64bit/"
        log_success "Plugin installed successfully!"
    else
        log_error "Built plugin not found: build_x86_64/c64u-plugin-for-obs.so"
        log_info "Run build first: $0 $PLATFORM"
        return 1
    fi
}

start_obs() {
    if ! command -v obs &> /dev/null; then
        log_error "OBS Studio not found in PATH"
        log_info "Install OBS Studio or add it to your PATH"
        return 1
    fi

    log_info "Starting OBS Studio..."
    log_info "Look for 'C64U' in the source list"

    # Start OBS in background and detach
    nohup obs > /dev/null 2>&1 &
    OBS_PID=$!

    log_success "OBS Studio started (PID: $OBS_PID)"
    log_info "Plugin should be available as 'C64U' source"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        ubuntu|linux|macos|windows)
            PLATFORM="$1"
            shift
            ;;
        --install)
            INSTALL_PLUGIN=true
            shift
            ;;
        --start-obs)
            INSTALL_PLUGIN=true
            START_OBS=true
            shift
            ;;
        --obs-dir)
            OBS_PLUGIN_DIR="$2"
            shift 2
            ;;
        --config)
            BUILD_CONFIG="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate build config
case "$BUILD_CONFIG" in
    Debug|RelWithDebInfo|Release|MinSizeRel)
        ;;
    *)
        log_error "Invalid build configuration: $BUILD_CONFIG"
        log_info "Valid options: Debug, RelWithDebInfo, Release, MinSizeRel"
        exit 1
        ;;
esac

log_info "C64U OBS Plugin - Docker Build Script"
log_info "Platform: $PLATFORM"
log_info "Build Config: $BUILD_CONFIG"
log_info "Install Plugin: $INSTALL_PLUGIN"
log_info "Start OBS: $START_OBS"

# Check Docker availability
if ! command -v docker &> /dev/null; then
    log_error "Docker not found. Please install Docker to use this script."
    log_info "Alternative: Use native build instructions in README.md"
    exit 1
fi

# Setup Docker image for platform
setup_docker_image

# Build the plugin
case "$PLATFORM" in
    ubuntu|linux)
        build_ubuntu_docker
        ;;
    *)
        log_error "Platform $PLATFORM not yet supported in Docker build"
        exit 1
        ;;
esac

# Install plugin if requested
if [[ "$INSTALL_PLUGIN" == true ]]; then
    install_plugin || exit 1
fi

# Start OBS if requested
if [[ "$START_OBS" == true ]]; then
    start_obs || log_warning "Could not start OBS automatically"
fi

log_success "Build script completed successfully!"
