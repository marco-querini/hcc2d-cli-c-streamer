#!/bin/sh
set -eu

usage() {
    echo "Usage: apt-repo/scripts/check.sh --distribution CODENAME"
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_ROOT/apt-repo/repo.conf"

distribution=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --distribution)
            distribution=${2:-}
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ -z "$distribution" ]; then
    echo "Error: --distribution is required." >&2
    exit 2
fi

release_dir="$REPO_ROOT/$APT_REPO_OUTPUT_DIR/dists/$distribution"
release_file="$release_dir/Release"

test -s "$release_file"
grep -q "^Codename: $distribution\$" "$release_file"
grep -q "^Components: $APT_REPO_COMPONENTS\$" "$release_file"

for arch in $APT_REPO_ARCHITECTURES; do
    packages_file="$release_dir/main/binary-$arch/Packages"
    test -s "$packages_file"
    grep -q '^Package: hcc2d-streamer$' "$packages_file"
done

echo "Repository metadata for $distribution looks structurally valid."
