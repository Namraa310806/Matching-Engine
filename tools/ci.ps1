# CI-equivalent local workflow script for Windows PowerShell
# This script performs a clean build and runs tests

$ErrorActionPreference = "Stop"

# Get script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Split-Path -Parent $ScriptDir

Write-Host "=== CI Workflow for Matching Engine ===" -ForegroundColor Cyan
Write-Host "Project root: $ProjectRoot"

# Clean build directory
$BuildDir = Join-Path $ProjectRoot "build-ci"
Write-Host "Cleaning build directory: $BuildDir"
if (Test-Path $BuildDir) {
    Remove-Item -Recurse -Force $BuildDir
}
New-Item -ItemType Directory -Path $BuildDir | Out-Null

# Configure with Debug/sanitizer flags
Write-Host "Configuring CMake with Debug/sanitizer flags..."
Set-Location $BuildDir
cmake -DCMAKE_BUILD_TYPE=Debug $ProjectRoot

# Build
Write-Host "Building project..."
cmake --build . --config Debug

# Run tests
Write-Host "Running tests via CTest..."
ctest -C Debug --verbose

Write-Host "=== CI workflow completed successfully ===" -ForegroundColor Green
