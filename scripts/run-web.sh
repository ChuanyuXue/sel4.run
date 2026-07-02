#!/bin/sh
#
# Serve the browser version of seL4-on-TinyEmu (build it first with
# scripts/build-web.sh), then open http://localhost:8000/
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")

port="${1:-8000}"
exec python3 -m http.server -d "$repo_root/build/web" "$port"
