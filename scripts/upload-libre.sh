#!/bin/bash
# Script to upload libre/4.0.0 from local cache to shared repository

set -e

echo "📦 Uploading libre/4.0.0 to shared repository"

# Configuration
REMOTE_NAME="subgridlabs"
PACKAGE_REF="libre/4.0.0"

# Check if package exists locally
if ! conan list "$PACKAGE_REF:*" >/dev/null 2>&1; then
    echo "❌ Package $PACKAGE_REF not found in local cache"
    echo ""
    echo "🔧 To create libre/4.0.0, you can:"
    echo "1. Build from source:"
    echo "   git clone https://github.com/baresip/re.git"
    echo "   cd re && git checkout v4.0.0"
    echo "   conan create . --name=libre --version=4.0.0"
    echo ""
    echo "2. Or use an existing version and create alias:"
    echo "   conan alias libre/4.0.0 libre/3.x.x"
    exit 1
fi

echo "✅ Found package in local cache"

# Check if remote exists
if ! conan remote list | grep -q "$REMOTE_NAME"; then
    echo "❌ Remote '$REMOTE_NAME' not configured"
    echo "Run: ./scripts/setup-conan-repo.sh first"
    exit 1
fi

# Upload package
echo "🚀 Uploading package to remote '$REMOTE_NAME'..."
if conan upload "$PACKAGE_REF" --remote="$REMOTE_NAME" --all --confirm; then
    echo "✅ Successfully uploaded $PACKAGE_REF"
else
    echo "❌ Upload failed. This might be because:"
    echo "1. Remote is not properly configured for uploads"
    echo "2. Authentication is required"
    echo "3. The remote doesn't support uploads"
    echo ""
    echo "💡 For now, consider using JFrog Cloud free tier:"
    echo "   https://jfrog.com/start-free/"
fi

echo ""
echo "🔍 Verify upload:"
echo "conan search --remote=$REMOTE_NAME '*libre*'"