#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
if [[ "$(uname -s)" != Darwin ]]; then
  echo "Please run this script on macOS." >&2
  exit 1
fi
if [[ -z "${VCPKG_ROOT:-}" && -f "$PWD/.vcpkg/scripts/buildsystems/vcpkg.cmake" ]]; then
  export VCPKG_ROOT="$PWD/.vcpkg"
fi
: "${VCPKG_ROOT:?Set VCPKG_ROOT to your bootstrapped vcpkg checkout}"
for tool in cmake ninja; do
  command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
cmake --preset macos-release "$@"
cmake --build --preset macos-release --parallel
ctest --preset macos-release
cmake --install build-vcpkg --prefix dist/macos
APP_EXECUTABLE="$PWD/dist/macos/MdEdit.app/Contents/MacOS/MdEdit"
SMOKE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/mdedit-smoke.XXXXXX")"
trap 'rm -rf "$SMOKE_DIR"' EXIT
(cd "$SMOKE_DIR" && "$APP_EXECUTABLE" --smoke-test)
hdiutil create -volname MdEdit -srcfolder dist/macos/MdEdit.app -ov -format UDZO dist/macos/MdEdit.dmg
echo "Built: $PWD/dist/macos/MdEdit.dmg"
