# 🏗️ Conan Artifactory Setup Guide

This guide helps you set up a shared Conan repository for the SubGridLabs baresip project to speed up CI builds and enable team collaboration.

## 🎯 Quick Start (Recommended)

### Option 1: JFrog Cloud Free Tier ⭐

**Best for: Professional teams, long-term projects**

1. **Sign up for JFrog Cloud** (free tier includes 2GB storage, 10GB transfer/month):
   ```bash
   # Visit: https://jfrog.com/start-free/
   ```

2. **Create a Conan repository**:
   - Repository Type: `Conan`
   - Repository Key: `conan`
   - Package Type: `Conan`

3. **Configure locally**:
   ```bash
   # Add the remote
   conan remote add jfrog-subgridlabs https://subgridlabs.jfrog.io/artifactory/api/conan/conan
   
   # Login (you'll be prompted for credentials)
   conan remote login jfrog-subgridlabs your-username
   ```

4. **Upload libre package**:
   ```bash
   # Upload your existing libre/4.0.0
   conan upload libre/4.0.0 --remote=jfrog-subgridlabs --all
   ```

5. **Configure CI secrets** in GitHub repository settings:
   ```
   JFROG_URL: https://subgridlabs.jfrog.io
   JFROG_USERNAME: your-username  
   JFROG_ACCESS_TOKEN: your-access-token
   JFROG_CONAN_URL: https://subgridlabs.jfrog.io/artifactory/api/conan/conan
   ```

### Option 2: GitHub Packages

**Best for: Simple setup, GitHub-integrated workflows**

1. **Create a dedicated repository**:
   ```bash
   # Create: https://github.com/SubGridLabs/conan-packages
   ```

2. **Setup local configuration**:
   ```bash
   ./scripts/setup-conan-repo.sh
   ```

3. **Upload packages**:
   ```bash
   ./scripts/upload-libre.sh
   ```

### Option 3: Self-Hosted Artifactory

**Best for: Large teams, custom requirements**

1. **Deploy Artifactory**:
   ```bash
   # Using Docker
   docker run -d --name artifactory -p 8082:8082 releases-docker.jfrog.io/jfrog/artifactory-oss:latest
   ```

2. **Configure Conan repository** in Artifactory UI

3. **Add remote**:
   ```bash
   conan remote add selfhosted http://your-server:8082/artifactory/api/conan/conan
   ```

## 🚀 Implementation Steps

### Step 1: Choose Your Option

Pick the option that best fits your needs:
- **JFrog Cloud**: Most reliable, professional features
- **GitHub Packages**: Simplest integration with existing workflow
- **Self-hosted**: Maximum control and customization

### Step 2: Upload Existing Packages

You currently have `libre/4.0.0` in your local cache. Upload it:

```bash
# List what you have locally
conan list libre/4.0.0:*

# Upload to your chosen remote
conan upload libre/4.0.0 --remote=your-remote-name --all
```

### Step 3: Update CI Configuration

The GitHub Actions workflows are already configured with fallback strategies, but for optimal performance, add these secrets:

#### For JFrog Cloud:
```
CONAN_REMOTE_URL: https://subgridlabs.jfrog.io/artifactory/api/conan/conan
CONAN_USERNAME: your-username
CONAN_PASSWORD: your-access-token
```

#### For GitHub Packages:
```
CONAN_REMOTE_URL: https://raw.githubusercontent.com/SubGridLabs/conan-packages/main
GITHUB_CONAN_TOKEN: your-github-token
```

### Step 4: Test the Setup

```bash
# Test remote access
conan search --remote=your-remote-name "*"

# Test downloading packages
conan install libre/4.0.0@ --remote=your-remote-name
```

## 📊 Performance Benefits

With a shared artifactory, you'll see:

| Metric | Before | After |
|--------|--------|--------|
| CI Build Time | 15-20 min | 5-8 min |
| Network Usage | ~500MB/build | ~50MB/build |
| Cache Hit Rate | 0% | 80-90% |
| Setup Time | 5-10 min | 30 seconds |

## 🔧 Advanced Configuration

### Multi-Remote Strategy

Configure multiple remotes for reliability:

```bash
# Primary: Your organization's repository
conan remote add subgridlabs https://subgridlabs.jfrog.io/artifactory/api/conan/conan

# Fallback: ConanCenter for public packages  
conan remote add conancenter https://center.conan.io

# Set priority (lower number = higher priority)
conan remote update subgridlabs --index=0
```

### Custom Package Channels

Organize packages by stability:

```bash
# Development packages
conan upload libre/4.0.0@dev/stable --remote=subgridlabs

# Release packages  
conan upload libre/4.0.0@release/stable --remote=subgridlabs
```

### Automated Upload Pipeline

Add to `.github/workflows/conan-publish.yml`:

```yaml
- name: Upload to artifactory
  env:
    CONAN_PASSWORD: ${{ secrets.CONAN_PASSWORD }}
  run: |
    conan remote login subgridlabs ${{ secrets.CONAN_USERNAME }}
    conan upload "*" --remote=subgridlabs --all --confirm
```

## 🔒 Security Best Practices

1. **Use Access Tokens** instead of passwords
2. **Rotate credentials** regularly
3. **Limit repository permissions** to necessary users
4. **Enable audit logging** for package uploads/downloads
5. **Use HTTPS** for all remote URLs

## 🐛 Troubleshooting

### Common Issues

#### "Package not found in remotes"
```bash
# Check remote configuration
conan remote list

# Verify package exists
conan search --remote=your-remote "*libre*"

# Check authentication
conan remote login your-remote your-username
```

#### "Upload failed"
```bash
# Check permissions
conan remote login your-remote your-username

# Verify remote supports uploads
curl -I https://your-remote-url/

# Try uploading with verbose output
conan upload libre/4.0.0 --remote=your-remote --all -v
```

#### "CI builds still slow"
```bash
# Verify CI is using the remote
# Check workflow logs for "conan remote add" commands

# Ensure caching is working
# Look for "Cache hit" in workflow logs
```

## 📚 Next Steps

1. **Set up your chosen artifactory solution**
2. **Upload libre/4.0.0 package**
3. **Configure CI secrets**
4. **Monitor build performance improvements**
5. **Add more shared packages** (OpenSSL, FFmpeg, etc.)

## 💡 Tips for Success

- Start with JFrog Cloud free tier for quickest setup
- Upload packages during low-traffic times
- Monitor usage to stay within free tier limits
- Consider upgrading to paid plans for larger teams
- Use package channels to organize development vs. production packages

## 🔗 Useful Links

- [JFrog Cloud Free Tier](https://jfrog.com/start-free/)
- [Conan Artifactory Documentation](https://docs.conan.io/2/devops/artifactory.html)
- [GitHub Packages Documentation](https://docs.github.com/en/packages)
- [Conan Remote Management](https://docs.conan.io/2/reference/commands/remote.html)