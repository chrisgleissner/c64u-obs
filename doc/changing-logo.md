# Changing the C64U Plugin Logo

The C64U plugin displays a logo when no video signal is present. This guide explains how to replace the logo with your own image.

## Quick Start

To change the logo, simply replace the PNG file in the plugin's data directory:

```bash
# Replace the logo image
cp your-new-logo.png data/images/c64u-logo.png

# Rebuild and reinstall the plugin
cmake --build build_x86_64
```

The new logo will be automatically included when you install the plugin.

## Logo Specifications

### Recommended Specifications
- **Format**: PNG with transparency (RGBA)
- **Aspect Ratio**: 3:2 (e.g., 600×400, 900×600, 1200×800)
- **Size**: Keep under 500KB for fast loading
- **Background**: Transparent or opaque, your choice

The current default logo is 500×333 pixels (87KB).

### Technical Details
- The logo is stored in `data/images/c64u-logo.png`
- OBS loads the image using `gs_texture_create_from_file()`
- The texture is centered on a black background when displayed
- The logo maintains its aspect ratio during scaling

## How It Works

The plugin uses OBS's standard data file mechanism:

1. **Storage**: Logo files are stored in the `data/images/` directory
2. **Access**: The plugin uses `obs_module_file("images/c64u-logo.png")` to locate the file
3. **Installation**: OBS automatically installs data files alongside the plugin:
   - Linux: `/usr/share/obs/obs-plugins/c64u-plugin-for-obs/images/c64u-logo.png`
   - Windows: `%ProgramFiles%\obs-studio\data\obs-plugins\c64u-plugin-for-obs\images\c64u-logo.png`
   - macOS: Inside the plugin bundle's Resources directory

## Installation Paths

After building and installing, your logo will be located at:

**Linux:**
```
~/.config/obs-studio/plugins/c64u-plugin-for-obs/bin/64bit/c64u-plugin-for-obs.so
/usr/share/obs/obs-plugins/c64u-plugin-for-obs/images/c64u-logo.png
```

**Windows:**
```
%ProgramFiles%\obs-studio\obs-plugins\64bit\c64u-plugin-for-obs.dll
%ProgramFiles%\obs-studio\data\obs-plugins\c64u-plugin-for-obs\images\c64u-logo.png
```

**macOS:**
```
~/Library/Application Support/obs-studio/plugins/c64u-plugin-for-obs.plugin/Contents/MacOS/c64u-plugin-for-obs
~/Library/Application Support/obs-studio/plugins/c64u-plugin-for-obs.plugin/Contents/Resources/images/c64u-logo.png
```

## Testing Your New Logo

1. Replace the logo file: `data/images/c64u-logo.png`
2. Rebuild: `cmake --build build_x86_64`
3. Reinstall: Copy the plugin to your OBS plugins directory
4. Launch OBS and add a C64U source
5. Without a C64 Ultimate connected, you should see your new logo

## Troubleshooting

### Logo Not Displaying
- Check the OBS log for "Failed to load logo texture" messages
- Verify the PNG file is valid (open it in an image viewer)
- Ensure the file is named exactly `c64u-logo.png`

### Logo Appears Distorted
- The plugin preserves aspect ratio, so distortion shouldn't occur
- Check your original image isn't already distorted

### File Size Concerns
- PNG files under 500KB load quickly
- For very large images, consider using image optimization tools
- The logo is loaded once when the source is created

## Advanced: Multiple Logos

To support multiple logo options:

1. Add additional PNG files to `data/images/`:
   ```
   data/images/c64u-logo.png          # Default
   data/images/c64u-logo-retro.png    # Retro variant
   data/images/c64u-logo-modern.png   # Modern variant
   ```

2. Modify `src/c64u-source.c` to add a properties option for logo selection

3. Update `load_logo_texture()` to use the selected logo file

## Why This Approach?

This method follows OBS plugin best practices:

- **Standard**: All OBS plugins use the `data/` directory for resources
- **Cross-platform**: Works identically on Linux, Windows, and macOS
- **Simple**: No build-time code generation or binary embedding
- **Maintainable**: Easy to update logos without recompiling
- **Efficient**: Direct file loading with no temporary files
