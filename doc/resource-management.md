# Resource Management in the C64U OBS Plugin

This document explains how the C64U plugin manages resources like images and data files, following OBS plugin best practices.

## Overview

The plugin uses OBS's standard **data directory mechanism** for managing resources. This is the idiomatic approach used by all official OBS plugins.

## Directory Structure

```
c64u-obs/
├── data/                           # Plugin data files
│   ├── images/                     # Image resources
│   │   └── c64u-logo.png          # Logo displayed when no signal
│   └── locale/                     # Localization files
│       └── en-US.ini              # English translations
├── src/                            # Source code
└── build_x86_64/                   # Build artifacts
```

## How It Works

### 1. Data Directory Convention

OBS plugins store non-code resources in a `data/` directory at the root of the project. This directory is automatically:
- Included in the build
- Installed alongside the plugin binary
- Accessible via the `obs_module_file()` macro

### 2. Accessing Data Files

The plugin uses the OBS-provided `obs_module_file()` macro to locate resources:

```c
// Get the full path to a data file
char *logo_path = obs_module_file("images/c64u-logo.png");

// Use the file
gs_texture_t *texture = gs_texture_create_from_file(logo_path);

// Free the path string
bfree(logo_path);
```

### 3. Installation Paths

OBS automatically installs data files to platform-specific locations:

**Linux:**
- Binary: `~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/c64u-plugin-for-obs.so`
- Data: `/usr/share/obs/obs-plugins/c64u-plugin-for-obs/images/c64u-logo.png`

**Windows:**
- Binary: `%ProgramFiles%\obs-studio\obs-plugins\64bit\c64u-plugin-for-obs.dll`
- Data: `%ProgramFiles%\obs-studio\data\obs-plugins\c64u-plugin-for-obs\images\c64u-logo.png`

**macOS:**
- Binary: `~/Library/Application Support/obs-studio/plugins/c64u-plugin-for-obs.plugin/Contents/MacOS/`
- Data: `~/Library/Application Support/obs-studio/plugins/c64u-plugin-for-obs.plugin/Contents/Resources/`

## Advantages of This Approach

### 1. **Cross-Platform Compatibility**
OBS handles all platform-specific path resolution. The same code works on Linux, Windows, and macOS.

### 2. **Standard OBS Pattern**
This is how all official OBS plugins work:
- `image-source` plugin: stores image files in `data/`
- `obs-filters` plugin: stores effect files in `data/effects/`
- `obs-text` plugin: stores font data in `data/`

### 3. **No Temporary Files**
Unlike binary embedding approaches, we load directly from the installed location. No need to:
- Write to `/tmp` (which may not be available)
- Handle temporary file cleanup
- Deal with permissions issues

### 4. **Easy Updates**
Users can replace resources without rebuilding:
```bash
# Update the logo
cp new-logo.png ~/.local/share/obs-studio/plugins/.../images/c64u-logo.png
```

### 5. **Efficient**
- Resources aren't duplicated in the binary
- No build-time code generation needed
- Faster builds and smaller binaries

### 6. **Debugging-Friendly**
You can easily inspect and modify resource files without recompiling during development.

## Logo Management

### Current Implementation

The C64U logo is displayed when no video signal is present. It's loaded once per source instance:

```c
static gs_texture_t *load_logo_texture(void)
{
    char *logo_path = obs_module_file("images/c64u-logo.png");
    if (!logo_path) {
        C64U_LOG_WARNING("Failed to locate logo file");
        return NULL;
    }

    gs_texture_t *logo_texture = gs_texture_create_from_file(logo_path);
    bfree(logo_path);

    return logo_texture;
}
```

### Logo Specifications

- **Location**: `data/images/c64u-logo.png`
- **Format**: PNG with optional transparency
- **Size**: Current logo is 500×333 pixels (87KB)
- **Aspect Ratio**: 3:2 recommended

### Changing the Logo

See [changing-logo.md](./changing-logo.md) for detailed instructions.

## Adding New Resources

To add new data files to the plugin:

1. **Add the file to `data/`:**
   ```bash
   mkdir -p data/effects
   cp my-shader.effect data/effects/
   ```

2. **Access it in code:**
   ```c
   char *effect_path = obs_module_file("effects/my-shader.effect");
   gs_effect_t *effect = gs_effect_create_from_file(effect_path, NULL);
   bfree(effect_path);
   ```

3. **Rebuild:**
   ```bash
   cmake --build build_x86_64
   ```

The CMake build system automatically includes all files in `data/` when installing the plugin.

## Localization

The plugin uses OBS's standard localization mechanism:

```
data/locale/
├── en-US.ini       # English
├── de-DE.ini       # German
└── ja-JP.ini       # Japanese
```

Access localized strings:
```c
obs_module_text("MyKey")  // Returns translated text
```

## Best Practices

1. **Keep Data Small**: Avoid large files in the data directory
2. **Use Appropriate Formats**: PNG for images, ini for text, JSON for config
3. **Check File Existence**: Always check if `obs_module_file()` returns NULL
4. **Free Path Strings**: Always call `bfree()` on paths from `obs_module_file()`
5. **Log Errors**: Use `C64U_LOG_WARNING()` if resources fail to load

## References

- [OBS Plugin Documentation](https://obsproject.com/docs/plugins.html)
- [OBS Image Source Plugin](https://github.com/obsproject/obs-studio/tree/master/plugins/image-source)
- [OBS Module API Reference](https://obsproject.com/docs/reference-modules.html)
