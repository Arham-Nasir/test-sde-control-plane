#!/bin/bash
#
# Setup script for Control Plane Assessment
#

set -e

echo "Control Plane Assessment - Setup"
echo "================================="
echo

# Check prerequisites
echo "Checking prerequisites..."

if ! command -v meson &> /dev/null; then
    echo "ERROR: meson not found. Please install meson."
    echo "  Ubuntu/Debian: sudo apt install meson"
    echo "  macOS: brew install meson"
    exit 1
fi

if ! command -v ninja &> /dev/null; then
    echo "ERROR: ninja not found. Please install ninja."
    echo "  Ubuntu/Debian: sudo apt install ninja-build"
    echo "  macOS: brew install ninja"
    exit 1
fi

if ! command -v gcc &> /dev/null && ! command -v clang &> /dev/null; then
    echo "ERROR: C compiler not found. Please install gcc or clang."
    exit 1
fi

echo "Prerequisites OK"
echo

# Parse arguments
LEVEL="all"
BUILD_DIR="builddir"

while [[ $# -gt 0 ]]; do
    case $1 in
        --senior-engineer|--senior_engineer|--se)
            LEVEL="senior_engineer"
            shift
            ;;
        --principal-engineer|--principal_engineer|--principal)
            LEVEL="principal_engineer"
            shift
            ;;
        --builddir)
            BUILD_DIR="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [--senior-engineer|--principal-engineer] [--builddir DIR]"
            exit 1
            ;;
    esac
done

echo "Configuration:"
echo "  Assessment level: $LEVEL"
echo "  Build directory: $BUILD_DIR"
echo

# Configure build
echo "Configuring build..."
if [ -d "$BUILD_DIR" ]; then
    echo "Build directory exists, reconfiguring..."
    meson setup --reconfigure "$BUILD_DIR" \
        -Dassessment_level="$LEVEL" \
        -Db_sanitize=address,undefined
else
    meson setup "$BUILD_DIR" \
        -Dassessment_level="$LEVEL" \
        -Db_sanitize=address,undefined
fi

echo
echo "Building..."
ninja -C "$BUILD_DIR"

echo
echo "Running tests..."
ninja -C "$BUILD_DIR" test || true

echo
echo "Setup complete!"
echo
echo "To build:     ninja -C $BUILD_DIR"
echo "To test:      ninja -C $BUILD_DIR test"
echo "To clean:     ninja -C $BUILD_DIR clean"
