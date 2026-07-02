#!/bin/sh
#
# Deploy the lab to Cloudflare Pages (project: sel4-run, domain: sel4.run).
#
# One-time setup:
#   npx wrangler login                       # authenticate with Cloudflare
#   npx wrangler pages project create sel4-run --production-branch main
#   ...then attach the sel4.run custom domain to the project in the
#   Cloudflare dashboard (Pages -> sel4-run -> Custom domains).
#
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repo_root=$(dirname "$(dirname "$script_dir")")
dist="$repo_root/build/web"

"$script_dir/build-web.sh"

if [ ! -f "$dist/toolchain.json" ] || grep -q '{}' "$dist/toolchain.json"; then
    echo "error: toolchain missing from $dist; build LLVM wasm first (see README)" >&2
    exit 1
fi

npx wrangler pages deploy "$dist" --project-name sel4-run --branch main
