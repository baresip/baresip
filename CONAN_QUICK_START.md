# Conan Quick Start for Baresip

A minimal guide to get started with Conan for Baresip dependency management.

## Install Conan

```bash
pip install conan>=2.0
conan profile detect --force
```

**Note on macOS**: Some dependencies (like GTK) may require system packages:
```bash
# If using GTK on macOS
brew install gtk+3

# If using other system dependencies  
brew install pkg-config
```

## Simple Build

```bash
# Traditional build (still works)
mkdir build && cd build
cmake .. && make

# Modern Conan build  
./build-conan.sh
```

## Custom Build

```bash
# Debug build with specific features
./build-conan.sh --debug --with-gtk --with-mosquitto

# Minimal build
./build-conan.sh --without-ffmpeg --without-alsa
```

## Advanced Usage

```bash
# Use custom profile
cp conanprofile.txt ~/.conan2/profiles/baresip
./build-conan.sh --profile baresip

# Manual control
mkdir build-conan && cd build-conan
conan install .. --build=missing --options=with_opus=True
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
make -j$(nproc)
```

## Benefits

- **Reproducible builds**: Same dependencies across all environments
- **Cross-platform**: Works on Linux, macOS, Windows
- **Version control**: Pin specific dependency versions
- **Easy setup**: No need to manually install development packages
- **Backward compatible**: Existing builds still work

## Troubleshooting

- **Missing conan**: `pip install conan>=2.0`
- **Build fails**: Try `./build-conan.sh --clean --build-all`
- **Compiler issues**: Run `conan profile detect --force`
- **Package errors**: See `KNOWN_ISSUES.md` for Conan Center package bugs

## Status

✅ **Integration Working**: Conan dependency resolution and CMake integration functional  
⚠️ **Some Packages Have Issues**: Due to bugs in Conan Center (not our integration)  
✅ **Fallback System Works**: Automatically uses system packages when Conan fails  

For detailed documentation, see `docs/CONAN_INTEGRATION.md`.