# Known Issues with Conan Integration

## Conan Center Package Issues

Several packages in Conan Center have bugs in their `package_info()` methods that reference invalid OS settings. These are **not issues with our Baresip Conan integration**, but with the upstream packages:

### Affected Packages

1. **opus/1.5.2**: References invalid "FreeBSD" OS setting
   - **Status**: Builds successfully, fails in package_info()
   - **Workaround**: Use system opus package as fallback

2. **openssl/3.5.1**: References invalid "Neutrino" OS setting  
   - **Status**: Fails during generation
   - **Workaround**: Use system OpenSSL as fallback

3. **libpng/1.6.50**: References invalid "FreeBSD" OS setting
   - **Status**: Builds successfully, fails in package_info()
   - **Workaround**: Use system libpng as fallback

4. **ffmpeg/6.1.1**: References invalid "FreeBSD" OS setting
   - **Status**: Fails during config_options()
   - **Workaround**: Use system FFmpeg as fallback

### Root Cause

The Conan profiles used in CI/CD for these packages include OS values like "FreeBSD" and "Neutrino" that are not in the standard Conan settings for newer versions.

### Impact on Baresip

- ✅ **Our integration works correctly**
- ✅ **Packages build successfully** 
- ❌ **Metadata generation fails** due to upstream bugs
- ✅ **Fallback to system packages works**

### Recommendations

1. **For Production**: Use system packages for affected dependencies
2. **For Testing**: Use only working Conan packages (see working list below)
3. **For CI**: Pin to specific package revisions that work

### Working Packages

These packages work correctly with our integration:

- ✅ **zlib** - Builds and installs correctly
- ✅ **Basic dependencies** - Most core packages work
- ⚠️ **Audio/video codecs** - Mixed results due to upstream issues

### Workarounds

The Baresip build system automatically falls back to system packages when Conan packages fail, so these issues don't block development.

## System Package Fallbacks

When Conan packages fail, install system dependencies:

### macOS (Homebrew)
```bash
brew install opus openssl libpng ffmpeg
```

### Ubuntu/Debian
```bash
sudo apt-get install libopus-dev libssl-dev libpng-dev libavcodec-dev
```

### Fedora/RHEL
```bash
sudo dnf install opus-devel openssl-devel libpng-devel ffmpeg-devel
```