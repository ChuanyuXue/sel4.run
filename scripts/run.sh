#!/bin/sh
#
# Boot the seL4 system image in TinyEmu.
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")

exec "$repo_root/tools/TinyEmu/temu" "$repo_root/build/images/tinyemu.cfg"
