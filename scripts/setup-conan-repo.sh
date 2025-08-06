#!/bin/bash
# Setup script for SubGridLabs Conan repository

set -e

echo "🏗️ Setting up SubGridLabs Conan Repository"

# Configuration
REPO_NAME="SubGridLabs/conan-packages"
REMOTE_NAME="subgridlabs"
REMOTE_URL="https://raw.githubusercontent.com/${REPO_NAME}/main"

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check dependencies
if ! command_exists conan; then
    echo "❌ Conan not found. Please install Conan first:"
    echo "   pip install conan>=2.0.0"
    exit 1
fi

if ! command_exists git; then
    echo "❌ Git not found. Please install Git first."
    exit 1
fi

echo "✅ Dependencies found"

# Setup Conan remote
echo "📡 Setting up Conan remote..."
if conan remote list | grep -q "$REMOTE_NAME"; then
    echo "⚠️  Remote '$REMOTE_NAME' already exists, removing..."
    conan remote remove "$REMOTE_NAME"
fi

# For now, we'll use a simple approach with GitHub releases
# Later we can upgrade to a proper Conan server
conan remote add "$REMOTE_NAME" "$REMOTE_URL"
echo "✅ Added remote: $REMOTE_NAME -> $REMOTE_URL"

# List current remotes
echo ""
echo "📋 Current Conan remotes:"
conan remote list

echo ""
echo "🎯 Next steps:"
echo "1. Upload your libre/4.0.0 package:"
echo "   conan upload libre/4.0.0 --remote=$REMOTE_NAME --all"
echo ""
echo "2. For CI/CD, add these secrets to GitHub:"
echo "   CONAN_REMOTE_URL: $REMOTE_URL"
echo "   CONAN_USERNAME: your-github-username"
echo "   CONAN_PASSWORD: your-github-token"
echo ""
echo "3. Test the setup:"
echo "   conan search --remote=$REMOTE_NAME '*'"

echo ""
echo "🚀 Conan repository setup complete!"