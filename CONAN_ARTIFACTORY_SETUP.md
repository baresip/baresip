# Baresip Conan Artifactory Integration

This document explains how to set up and use the Conan Artifactory integration for the baresip project to efficiently manage and share multimedia dependencies.

## Overview

The baresip project has extensive multimedia dependencies including:
- **Audio Codecs**: Opus, G.722, G.711
- **Video Codecs**: AV1, VP9 (optional)
- **Audio Systems**: ALSA, PulseAudio, PipeWire, JACK, PortAudio
- **Multimedia Frameworks**: GStreamer, FFmpeg (optional)
- **Graphics**: SDL2, PNG support
- **Security**: OpenSSL
- **Networking**: MQTT (Mosquitto)
- **Core**: libre (SIP library)

The GitHub Actions workflows automatically build and upload all these dependencies to your Artifactory, making them available for reuse across builds and projects.

## Setup Instructions

### 1. GitHub Secrets Configuration

Add these secrets to your GitHub repository settings:

```
CONAN_REMOTE_URL=http://13.61.152.119:9300/artifactory/api/conan/conan
CONAN_USER=builder  
CONAN_PASSWORD=secure123
```

**How to add secrets:**
1. Go to your repository on GitHub
2. Click **Settings** → **Secrets and variables** → **Actions**
3. Click **New repository secret**
4. Add each of the above secrets

### 2. Workflow Files

Two main workflows are provided:

#### `conan-ci.yml` - Continuous Integration
- **Triggers**: Push/PR to main or conan-integration branches
- **Purpose**: 
  - Builds all baresip dependencies
  - Uploads missing packages to Artifactory
  - Tests multiple configurations (default, minimal, professional-audio, video-focused)
  - Runs on Linux, macOS, and Windows

#### `conan-publish.yml` - Manual Publishing
- **Triggers**: Manual workflow dispatch
- **Purpose**: 
  - Creates release packages for specific versions
  - Supports different configurations
  - Publishes to Artifactory or Conan Center

## Usage

### Automatic Dependency Management (CI)

Every push to main or conan-integration branches will:

1. **Install Dependencies**: Use existing packages from Artifactory when available
2. **Build Missing**: Only build dependencies not found in Artifactory
3. **Upload New Packages**: Automatically upload any newly built dependencies
4. **Test Configurations**: 
   - Default multimedia configuration
   - Minimal configuration (basic audio only)
   - Professional audio (Linux: all audio systems)
   - Video-focused configuration

### Manual Publishing (Releases)

To publish a specific version:

1. Go to **Actions** → **Baresip Conan Publish**
2. Click **Run workflow**
3. Configure:
   - **Version**: Leave empty to use branch name, or specify (e.g., "4.0.1")
   - **Remote**: Choose `artifactory` or `conancenter`
   - **Configuration**: Choose from:
     - `full`: All multimedia features
     - `minimal`: Basic audio only
     - `professional-audio`: All audio systems + professional features
     - `video-focused`: Video codecs + multimedia frameworks

## Configurations Explained

### Full Configuration
```bash
# Includes all features
-o baresip/*:with_opus=True
-o baresip/*:with_g722=True 
-o baresip/*:with_g711=True
-o baresip/*:with_av1=True
-o baresip/*:with_openssl=True
-o baresip/*:with_gstreamer=True
-o baresip/*:with_sdl=True
-o baresip/*:with_mosquitto=True
-o baresip/*:with_png=True
-o baresip/*:with_sndfile=True
```

### Professional Audio (Linux)
```bash
# Optimized for professional audio workflows
-o baresip/*:with_opus=True
-o baresip/*:with_alsa=True
-o baresip/*:with_pulseaudio=True
-o baresip/*:with_pipewire=True
-o baresip/*:with_jack=True
-o baresip/*:with_portaudio=True
-o baresip/*:with_gtk=True
-o baresip/*:with_sndfile=True
```

### Video-Focused
```bash
# Optimized for video applications
-o baresip/*:with_opus=True
-o baresip/*:with_av1=True
-o baresip/*:with_gstreamer=True
-o baresip/*:with_sdl=True
-o baresip/*:with_png=True
```

### Minimal
```bash
# Basic SIP functionality only
-o baresip/*:with_opus=True
-o baresip/*:with_g722=True
-o baresip/*:with_g711=True
-o baresip/*:with_openssl=True
```

## Platform-Specific Features

### Linux
- Full audio system support (ALSA, PulseAudio, PipeWire, JACK)
- GTK GUI support
- Professional audio configuration available

### macOS
- PortAudio for cross-platform audio
- Core Audio integration
- Metal/AVFoundation support

### Windows  
- Windows Audio API integration
- Limited to core multimedia features
- No GStreamer (platform limitations)

## Dependency Cache Strategy

### Artifactory First
1. Check Artifactory for existing packages
2. Download if available (fast)
3. Only build missing dependencies
4. Upload newly built packages

### Local Cache
- GitHub Actions cache for `~/.conan2`
- Reduces repeated downloads
- Speeds up consecutive builds

## Benefits

### For CI/CD
- **Faster Builds**: Reuse pre-built dependencies
- **Consistent Dependencies**: Same packages across all builds
- **Reduced Build Times**: From hours to minutes for clean builds
- **Cross-Project Sharing**: Other projects can reuse baresip's multimedia stack

### For Development
- **Quick Setup**: New developers get pre-built dependencies
- **Consistent Environment**: Same multimedia stack across team
- **Easy Testing**: Multiple configurations readily available

## Verifying Packages

### Check What's in Artifactory
```bash
# List all packages
conan search "*" -r artifactory

# List baresip packages specifically  
conan search "baresip/*" -r artifactory

# List multimedia dependencies
conan search "opus/*" -r artifactory
conan search "libaom-av1/*" -r artifactory
conan search "gstreamer/*" -r artifactory
```

### Check Package Details
```bash
# Get package info
conan inspect baresip/4.0.0@ -r artifactory

# List all variants/options
conan list baresip/4.0.0:* -r artifactory
```

## Troubleshooting

### Build Failures
1. Check if Artifactory is accessible
2. Verify GitHub secrets are correct
3. Look for missing system dependencies (Linux)
4. Check conanfile.py for option conflicts

### Upload Failures
1. Verify authentication with Artifactory
2. Check network connectivity from GitHub runners
3. Ensure package names match expectations

### Missing Dependencies
1. Check if system packages are installed (Linux: ALSA dev libs, etc.)
2. Verify Conan profile settings
3. Look for version conflicts in conanfile.py

## Local Development Setup

If you want to use the Artifactory locally:

```bash
# Add remote
conan remote add artifactory http://13.61.152.119:9300/artifactory/api/conan/conan

# Authenticate  
conan remote login artifactory builder

# Install baresip dependencies (uses Artifactory first)
conan install . --build=missing

# Build baresip
conan create .
```

## Advanced Usage

### Custom Configurations
Modify `conanfile.py` to add new option combinations, then update the workflows to test and publish them.

### Multi-Remote Strategy
```bash
# Priority order: artifactory → conancenter
conan remote add artifactory http://13.61.152.119:9300/artifactory/api/conan/conan
conan remote add conancenter https://center.conan.io
```

### Version Overrides
Use version overrides in `conanfile.py` to ensure multimedia libraries are compatible:
```python
self.requires("opus/1.4")  # Matches FFmpeg requirements
self.requires("libvpx/1.14.1", override=True)  # FFmpeg compatibility
```

This setup transforms baresip from a complex multimedia project requiring lengthy builds into a fast, dependency-managed system where builds complete in minutes instead of hours.