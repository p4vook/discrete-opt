#!/usr/bin/env bash

set -euo pipefail

repo_dir=$(cd "$(dirname "$0")" && pwd)
build_dir=${BUILD_DIR:-"$repo_dir/build"}
build_type=${BUILD_TYPE:-Release}
jobs=${JOBS:-4}

if [[ ${1:-} == "--help" ]]; then
  cat <<'EOF'
Usage: ./build.sh [TARGET ...]

Configures a Ninja build and builds every target, or only the named targets.
Environment variables: BUILD_DIR, BUILD_TYPE, JOBS.
EOF
  exit 0
fi

for target in "$@"; do
  if [[ $target == -* ]]; then
    printf 'unknown option: %s\n' "$target" >&2
    exit 1
  fi
done

if [[ ! $jobs =~ ^[1-9][0-9]*$ ]]; then
  printf 'JOBS must be a positive integer: %s\n' "$jobs" >&2
  exit 1
fi

cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE="$build_type"

build_command=(cmake --build "$build_dir" --parallel "$jobs")
if (($#)); then
  build_command+=(--target "$@")
fi
"${build_command[@]}"
