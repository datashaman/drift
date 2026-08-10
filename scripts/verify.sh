#!/bin/sh

set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

npm ci --prefix ui
npm run test --prefix ui
npm run build --prefix ui

cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Print the deterministic timing report in CI even when the CTest run passes.
"$project_dir/build/debug/DriftStressTests"
