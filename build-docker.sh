#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
output="${1:-$repo_root/dist}"
mkdir -p "$output"
output="$(cd -- "$output" && pwd)"
docker build --platform linux/amd64 -t wwhd-deadzone-builder "$repo_root"
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    --mount "type=bind,source=$repo_root,target=/project,readonly" \
    --mount "type=bind,source=$output,target=/output" wwhd-deadzone-builder
