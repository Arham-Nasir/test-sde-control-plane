#!/bin/bash
#
# Test runner script
#

set -e

BUILD_DIR="${BUILD_DIR:-builddir}"
VERBOSE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -v|--verbose)
            VERBOSE="-v"
            shift
            ;;
        --builddir)
            BUILD_DIR="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: $0 [-v|--verbose] [--builddir DIR]"
            exit 1
            ;;
    esac
done

if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found: $BUILD_DIR"
    echo "Run setup.sh first"
    exit 1
fi

echo "Running tests from $BUILD_DIR"
echo

# Run meson tests
meson test -C "$BUILD_DIR" $VERBOSE --print-errorlogs

echo
echo "All tests complete"
