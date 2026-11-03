#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
: "${VCPKG_ROOT:?Set VCPKG_ROOT to your bootstrapped vcpkg checkout}"
cmake --preset linux-release "$@"
cmake --build --preset linux-release --parallel
ctest --preset linux-release
cmake --install build-vcpkg --prefix dist/linux
