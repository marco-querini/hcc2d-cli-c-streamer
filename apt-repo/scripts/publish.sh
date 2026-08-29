#!/bin/sh
set -eu

usage() {
    cat <<'EOF'
Usage:
  apt-repo/scripts/publish.sh --distribution CODENAME --deb PATH [--deb PATH ...] [--skip-signing]

Environment:
  APT_REPO_GPG_KEY  Signing-key fingerprint or long key ID.
  GNUPGHOME         GnuPG home containing the private signing key.

The script copies .deb files into the selected distribution, regenerates APT
metadata, and signs it unless --skip-signing is supplied. Private key material
is read only by GnuPG and is never copied into the repository.
EOF
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_ROOT/apt-repo/repo.conf"

distribution=
skip_signing=0
debs=

while [ "$#" -gt 0 ]; do
    case "$1" in
        --distribution)
            distribution=${2:-}
            shift 2
            ;;
        --deb)
            debs="${debs}
${2:-}"
            shift 2
            ;;
        --skip-signing)
            skip_signing=1
            shift
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
if [ -z "$debs" ]; then
    echo "Error: at least one --deb PATH is required." >&2
    exit 2
fi

is_supported=0
for candidate in $APT_REPO_DISTRIBUTIONS; do
    if [ "$candidate" = "$distribution" ]; then
        is_supported=1
        break
    fi
done
if [ "$is_supported" -ne 1 ]; then
    echo "Error: unsupported distribution '$distribution'." >&2
    exit 2
fi

output_dir="$REPO_ROOT/$APT_REPO_OUTPUT_DIR"
tmp_dir="$REPO_ROOT/$APT_REPO_TMP_DIR"
mkdir -p "$output_dir" "$tmp_dir"

first_deb=
for deb in $debs; do
    if [ ! -f "$deb" ]; then
        echo "Error: missing .deb file: $deb" >&2
        exit 2
    fi
    if [ -z "$first_deb" ]; then
        first_deb=$deb
    fi
done

package_name=$(dpkg-deb -f "$first_deb" Package)
if [ "$package_name" != "hcc2d-streamer" ]; then
    echo "Error: expected package hcc2d-streamer, got $package_name." >&2
    exit 2
fi

package_initial=$(printf '%s' "$package_name" | cut -c1)
component=main
pool_rel="$APT_REPO_POOL_PREFIX/$distribution/$component/$package_initial/$package_name"
pool_dir="$output_dir/$pool_rel"
mkdir -p "$pool_dir"

for deb in $debs; do
    cp -f "$deb" "$pool_dir/"
done

for arch in $APT_REPO_ARCHITECTURES; do
    binary_dir="$output_dir/dists/$distribution/$component/binary-$arch"
    mkdir -p "$binary_dir"
    packages_file="$binary_dir/Packages"
    (
        cd "$output_dir"
        apt-ftparchive packages "$pool_rel"
    ) > "$packages_file"
    gzip -9n < "$packages_file" > "$packages_file.gz"
done

release_dir="$output_dir/dists/$distribution"
mkdir -p "$release_dir"
release_tmp="$tmp_dir/Release.$distribution"

apt-ftparchive \
    -o "APT::FTPArchive::Release::Origin=$APT_REPO_ORIGIN" \
    -o "APT::FTPArchive::Release::Label=$APT_REPO_LABEL" \
    -o "APT::FTPArchive::Release::Suite=$distribution" \
    -o "APT::FTPArchive::Release::Codename=$distribution" \
    -o "APT::FTPArchive::Release::Architectures=$APT_REPO_ARCHITECTURES" \
    -o "APT::FTPArchive::Release::Components=$APT_REPO_COMPONENTS" \
    -o "APT::FTPArchive::Release::Description=$APT_REPO_DESCRIPTION" \
    release "$release_dir" > "$release_tmp"
mv "$release_tmp" "$release_dir/Release"

if [ "$skip_signing" -eq 1 ]; then
    rm -f "$release_dir/InRelease" "$release_dir/Release.gpg"
else
    if [ -z "${APT_REPO_GPG_KEY:-}" ]; then
        echo "Error: APT_REPO_GPG_KEY is required for signing." >&2
        exit 2
    fi
    gpg --batch --yes --local-user "$APT_REPO_GPG_KEY" \
        --armor --detach-sign --output "$release_dir/Release.gpg" \
        "$release_dir/Release"
    gpg --batch --yes --local-user "$APT_REPO_GPG_KEY" \
        --clearsign --output "$release_dir/InRelease" \
        "$release_dir/Release"
fi

echo "Published $package_name for $distribution in $output_dir"
