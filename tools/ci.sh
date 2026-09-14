#!/bin/bash
# CI-equivalent local workflow script for Linux/WSL2
# This script performs a clean build and runs tests

set -e  # Exit on error

# Get script directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=== CI Workflow for Matching Engine ==="
echo "Project root: $PROJECT_ROOT"

# Clean build directory
BUILD_DIR="$PROJECT_ROOT/build-ci"
echo "Cleaning build directory: $BUILD_DIR"
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"

# Configure with Debug/sanitizer flags
echo "Configuring CMake with Debug/sanitizer flags..."
cd "$BUILD_DIR"
cmake -DCMAKE_BUILD_TYPE=Debug "$PROJECT_ROOT"

# Build
echo "Building project..."
cmake --build . -- -j$(nproc)

# Run tests
echo "Running tests via CTest..."
ctest --verbose

echo "=== CI workflow completed successfully ==="
