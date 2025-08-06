# Conan Integration for Baresip

This document describes how to use Conan for dependency management in Baresip, which provides a modern alternative to manually finding system packages.

## Overview

Baresip now supports both traditional dependency management (using custom Find modules) and modern Conan-based dependency management. The integration is designed to be:

- **Backward compatible**: Existing builds continue to work without Conan
- **Gradual adoption**: You can use Conan for some dependencies while falling back to system packages for others
- **Cross-platform**: Works consistently across Linux, macOS, and Windows
- **Version-controlled**: Dependencies are pinned to specific versions for reproducible builds

## Quick Start

### Prerequisites

1. Install Conan 2.x:
   ```bash
   pip install conan>=2.0
   ```

2. Create a default Conan profile:
   ```bash
   conan profile detect --force
   ```

### Basic Usage

1. **Generate dependency files**:
   ```bash
   mkdir build && cd build
   conan install .. --build=missing
   ```

2. **Configure and build**:
   ```bash
   cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
   make
   ```

## Advanced Configuration

### Custom Conan Options

You can customize which dependencies to use via Conan options:

```bash
# Install with specific options
conan install .. \
    --options=with_opus=True \
    --options=with_ffmpeg=True \
    --options=with_alsa=False \
    --build=missing
```

### Conan Profile

Create a custom profile in `~/.conan2/profiles/baresip`:

```ini
[settings]
os=Linux
arch=x86_64
compiler=gcc
compiler.version=11
compiler.libcxx=libstdc++11
build_type=Release

[options]
baresip/*:with_opus=True
baresip/*:with_ffmpeg=True
baresip/*:with_vpx=True
baresip/*:with_alsa=True
baresip/*:with_pulseaudio=True

[buildenv]
CC=gcc-11
CXX=g++-11
```

Use the profile:
```bash
conan install .. --profile=baresip --build=missing
```

## Available Options

The Conanfile supports the following options:

### Audio Codecs
- `with_opus`: Enable Opus codec support (default: True)
- `with_g722`: Enable G.722 codec support (default: True)
- `with_g711`: Enable G.711 codec support (default: True)
- `with_aac`: Enable AAC codec support (default: False, requires license)

### Video Codecs
- `with_vpx`: Enable VP8/VP9 codec support (default: True)
- `with_av1`: Enable AV1 codec support (default: False)
- `with_ffmpeg`: Enable FFmpeg support (default: True)

### Audio Systems
- `with_alsa`: Enable ALSA support (default: auto-detected)
- `with_pulseaudio`: Enable PulseAudio support (default: auto-detected)
- `with_jack`: Enable JACK support (default: False)
- `with_portaudio`: Enable PortAudio support (default: False)
- `with_pipewire`: Enable PipeWire support (default: False)

### Other Features
- `with_openssl`: Enable OpenSSL support (default: True)
- `with_gstreamer`: Enable GStreamer support (default: False)
- `with_gtk`: Enable GTK3 support (default: False)
- `with_sdl`: Enable SDL support (default: False)
- `with_mosquitto`: Enable MQTT support (default: False)
- `with_png`: Enable PNG support (default: True)
- `with_sndfile`: Enable libsndfile support (default: True)

## Integration Details

### How It Works

1. **Dual Mode Operation**: The build system checks for `conan_toolchain.cmake` to determine if Conan is being used
2. **Fallback Strategy**: If a Conan package isn't found, the system falls back to the traditional Find module
3. **Modern CMake Targets**: When using Conan, the system prefers modern CMake imported targets (e.g., `opus::opus`)
4. **Compatibility**: Traditional variables (`OPUS_LIBRARIES`, `OPUS_INCLUDE_DIRS`) are still supported for fallback

### Module Integration

Each module's `CMakeLists.txt` has been updated to support both modes:

```cmake
# Try Conan first, then fallback to manual find
if(BARESIP_USE_CONAN)
    find_package(opus QUIET)
    if(opus_FOUND)
        set(OPUS_FOUND TRUE)
    else()
        find_package(OPUS)
    endif()
else()
    find_package(OPUS)
endif()

# Link dependencies based on mode
if(BARESIP_USE_CONAN AND TARGET opus::opus)
    target_link_libraries(${PROJECT_NAME} PRIVATE opus::opus)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${OPUS_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OPUS_LIBRARIES})
endif()
```

## Troubleshooting

### Common Issues

1. **"Conan not detected"**: 
   - Make sure you ran `conan install` before `cmake`
   - Check that `conan_toolchain.cmake` exists in your build directory

2. **Missing packages**: 
   - Use `--build=missing` to build packages from source
   - Check available packages: `conan search <package_name> --remote=conancenter`

3. **Compiler mismatch**:
   - Ensure your Conan profile matches your system compiler
   - Update profile: `conan profile detect --force`

4. **Dependency conflicts**:
   - Clean build directory and regenerate: `rm -rf build && mkdir build`
   - Update Conan packages: `conan remove '*' --confirm`

### Building without Conan

To disable Conan and use traditional dependency management:

```bash
cmake .. -DUSE_CONAN=OFF
```

Or remove/rename the `conan_toolchain.cmake` file.

## Migration Guide

### For Existing Projects

1. **No immediate changes required**: Existing builds continue to work
2. **Gradual adoption**: Start by enabling a few dependencies via Conan
3. **Testing**: Build with both modes to ensure compatibility

### For New Projects

1. **Recommend starting with Conan**: More consistent and reproducible
2. **Use profiles**: Create project-specific Conan profiles
3. **Pin versions**: Use specific versions in `conanfile.py` for production

## Contributing

When adding new dependencies:

1. **Add to conanfile.py**: Include the new option and requirement
2. **Update module CMakeLists.txt**: Add Conan support with fallback
3. **Update documentation**: Add the new option to this document
4. **Test both modes**: Ensure the dependency works with and without Conan

## Examples

### Basic Build
```bash
# Traditional build
mkdir build-traditional && cd build-traditional
cmake ..
make

# Conan build  
mkdir build-conan && cd build-conan
conan install .. --build=missing
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
make
```

### Cross-compilation
```bash
# Create ARM profile
conan profile new arm --detect
conan profile update settings.arch=armv7 arm

# Install for ARM
conan install .. --profile=arm --build=missing

# Build for ARM
cmake .. -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake
make
```

### Development Build
```bash
# Debug build with specific dependencies
conan install .. \
    --settings=build_type=Debug \
    --options=with_opus=True \
    --options=with_ffmpeg=True \
    --build=missing

cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug

make
```