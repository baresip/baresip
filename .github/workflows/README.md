# Baresip Conan CI/CD Workflows

This directory contains GitHub Actions workflows for automated testing and publishing of the Baresip Conan package.

## 🚀 Workflows Overview

### 1. `conan-ci.yml` - Main CI Pipeline
**Triggers**: Push to main/conan-integration branches, PRs

**Purpose**: Comprehensive multi-platform testing with full feature matrix

**Platforms**:
- ✅ macOS (arm64 & x86_64)
- ✅ Windows (x86_64) 
- ✅ Linux (x86_64)
- ✅ iOS (cross-compilation)

**Features Tested**:
- 🎵 Audio codecs (Opus, G.722, G.711)
- 🎬 Video codecs (FFmpeg, VP8/VP9, AV1)
- 🖥️ Display systems (SDL, GStreamer)
- 🔒 Security (OpenSSL)
- 📁 File formats (libsndfile, PNG)
- 🌐 IoT connectivity (MQTT)

### 2. `conan-configurations.yml` - Configuration Matrix
**Triggers**: Changes to conanfile.py, manual dispatch

**Purpose**: Test different feature combinations and build configurations

**Test Scenarios**:
- **Minimal**: Fastest build, basic features only
- **Audio-focused**: Optimized for audio applications
- **Video-focused**: Full video codec support
- **Full-featured**: All features enabled

### 3. `quick-test.yml` - Development Testing
**Triggers**: PRs to conan-integration branch

**Purpose**: Fast feedback for development iterations

**Features**:
- Quick build with minimal dependencies
- Basic package verification
- Cached dependencies for speed

### 4. `conan-publish.yml` - Package Publishing
**Triggers**: Releases, manual dispatch

**Purpose**: Build and publish production packages

**Outputs**:
- Multi-platform packages
- Package verification tests
- Upload to Conan repositories

## 🛠️ Configuration

### Required Secrets (for publishing)
Add these to your repository secrets for publishing workflows:

```bash
# For custom Conan repositories
CONAN_LOGIN_USERNAME=your-username
CONAN_PASSWORD=your-password-or-token
CONAN_REMOTE_URL=https://your-conan-repo.com/artifactory/api/conan/conan
```

### Workflow Customization

#### Enable All Configuration Testing
```yaml
# In conan-configurations.yml workflow_dispatch
test_all_configs: true
```

#### Custom Publishing Version
```yaml
# In conan-publish.yml workflow_dispatch
version: "4.0.0"
remote: "custom"
```

## 📊 Performance & Caching

### Conan Package Caching
All workflows use GitHub Actions cache to speed up builds:
- Cache key includes OS, configuration, and conanfile.py hash
- Automatic fallback to broader cache keys
- Typical cache hit saves 5-10 minutes per job

### Build Time Expectations
| Workflow | Platform | Typical Duration |
|----------|----------|------------------|
| Quick Test | Linux | ~3-5 minutes |
| Quick Test | macOS | ~4-6 minutes |
| Full CI | Linux | ~8-12 minutes |
| Full CI | macOS | ~10-15 minutes |
| Full CI | Windows | ~12-18 minutes |

## 🧪 Testing Strategy

### 1. Pull Request Validation
- Quick test on Linux & macOS
- Minimal configuration for speed
- Basic package verification

### 2. Branch Protection
- Main CI must pass before merge
- Multiple platform validation
- Feature matrix verification

### 3. Release Validation
- Full platform matrix
- All configuration combinations
- Production package builds

## 📋 Monitoring & Debugging

### Workflow Status
Check workflow status at: `https://github.com/SubGridLabs/baresip/actions`

### Common Issues & Solutions

#### Build Failures
1. **Dependency conflicts**: Check Conan version overrides in conanfile.py
2. **Missing system packages**: Update system dependency installation steps
3. **Cache corruption**: Clear cache by changing cache key

#### Platform-Specific Issues
- **macOS**: Ensure Xcode command line tools are available
- **Windows**: Check MSVC version compatibility
- **Linux**: Verify system package installation
- **iOS**: Cross-compilation profile configuration

### Debugging Locally
Reproduce CI issues locally:

```bash
# Install same Conan version as CI
pip install "conan>=2.0.0"

# Use same profile as CI
conan profile detect --force

# Run same commands as CI
conan create . -o with_ffmpeg=True --build=missing
```

## 🎯 Future Enhancements

### Planned Improvements
- [ ] Code coverage reporting
- [ ] Performance benchmarking
- [ ] Static analysis integration
- [ ] Dependency vulnerability scanning
- [ ] Automatic version bumping
- [ ] ConanCenter submission automation

### Contributing
When adding new workflows or modifying existing ones:

1. Test locally with `act` or similar tools
2. Use meaningful job and step names
3. Add appropriate caching strategies
4. Include summary generation for complex workflows
5. Update this README with any changes

## 📚 References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Conan CI/CD Best Practices](https://docs.conan.io/2/devops.html)
- [CMake with Conan](https://docs.conan.io/2/examples/tools/cmake.html)
- [Multi-platform Conan Packages](https://docs.conan.io/2/tutorial/consuming_packages/cross_platform_packages.html)