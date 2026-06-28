#!/bin/sh
#
# Use this script to build and run the shell locally.
#
# Usage: ./run.sh [arguments...]

set -e # Exit early if any commands fail

# Build the project
(
  cd "$(dirname "$0")" # Ensure compile steps are run within the repository directory
  cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake
  cmake --build ./build
)

# Run the shell
exec "$(dirname "$0")/build/shell" "$@"
