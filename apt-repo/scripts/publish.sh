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

work_base=${TMPDIR:-/tmp}
work_dir=$(mktemp -d "${work_base%/}/hcc2d-streamer-publish.XXXXXX")
deb_list="$work_dir/debs"
: > "$deb_list"

cleanup() {
    case "$work_dir" in
        "${work_base%/}"/hcc2d-streamer-publish.*)
            rm -rf -- "$work_dir"
            ;;
        *)
            echo "Error: refusing to remove unexpected path: $work_dir" >&2
            ;;
    esac
}
trap cleanup EXIT HUP INT TERM

distribution=
skip_signing=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --distribution)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "Error: --distribution requires a value." >&2
                exit 2
            fi
            distribution=$2
            shift 2
            ;;
        --deb)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "Error: --deb requires a path." >&2
                exit 2
            fi
            case "$2" in
                *'
'*) echo "Error: .deb paths cannot contain a newline." >&2; exit 2 ;;
            esac
            printf '%s\n' "$2" >> "$deb_list"
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
if [ ! -s "$deb_list" ]; then
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
case "$distribution" in
    ''|.*|*..*|*[!a-z0-9._-]*)
        echo "Error: unsafe distribution name '$distribution'." >&2
        exit 2
        ;;
esac

set -- $APT_REPO_COMPONENTS
if [ "$#" -ne 1 ]; then
    echo "Error: exactly one APT_REPO_COMPONENTS value is supported." >&2
    exit 2
fi
component=$1
case "$component" in
    ''|.*|*..*|*[!a-z0-9._-]*)
        echo "Error: unsafe repository component '$component'." >&2
        exit 2
        ;;
esac

case "$APT_REPO_OUTPUT_DIR" in
    ''|.|..|/*|../*|*/../*|*/..)
        echo "Error: APT_REPO_OUTPUT_DIR must be a safe repository-relative path." >&2
        exit 2
        ;;
esac
case "$APT_REPO_POOL_PREFIX" in
    ''|.|..|/*|../*|*/../*|*/..)
        echo "Error: APT_REPO_POOL_PREFIX must be a safe relative path." >&2
        exit 2
        ;;
esac

if [ "$skip_signing" -ne 1 ] && [ -z "${APT_REPO_GPG_KEY:-}" ]; then
    echo "Error: APT_REPO_GPG_KEY is required for signing." >&2
    exit 2
fi

while IFS= read -r deb; do
    if [ ! -f "$deb" ]; then
        echo "Error: missing .deb file: $deb" >&2
        exit 2
    fi

    package_name=$(dpkg-deb -f -- "$deb" Package)
    if [ "$package_name" != "hcc2d-streamer" ]; then
        echo "Error: expected package hcc2d-streamer, got $package_name in $deb." >&2
        exit 2
    fi

    package_version=$(dpkg-deb -f -- "$deb" Version)
    case "$package_version" in
        ''|*[!A-Za-z0-9.+:~_-]*)
            echo "Error: invalid package version '$package_version' in $deb." >&2
            exit 2
            ;;
    esac

    package_arch=$(dpkg-deb -f -- "$deb" Architecture)
    case "$package_arch" in
        ''|*[!A-Za-z0-9-]*)
            echo "Error: invalid package architecture '$package_arch' in $deb." >&2
            exit 2
            ;;
    esac
    arch_supported=0
    if [ "$package_arch" = "all" ]; then
        arch_supported=1
    else
        for candidate in $APT_REPO_ARCHITECTURES; do
            if [ "$candidate" = "$package_arch" ]; then
                arch_supported=1
                break
            fi
        done
    fi
    if [ "$arch_supported" -ne 1 ]; then
        echo "Error: package architecture '$package_arch' is not configured." >&2
        exit 2
    fi
done < "$deb_list"

package_name=hcc2d-streamer
package_initial=$(printf '%s' "$package_name" | cut -c1)
output_dir="$REPO_ROOT/$APT_REPO_OUTPUT_DIR"
pool_rel="$APT_REPO_POOL_PREFIX/$distribution/$component/$package_initial/$package_name"
pool_dir="$output_dir/$pool_rel"
stage_output="$work_dir/output"
stage_pool_dir="$stage_output/$pool_rel"
publish_names="$work_dir/publish-names"
mkdir -p "$stage_pool_dir"
: > "$publish_names"

if [ -d "$pool_dir" ]; then
    cp -a -- "$pool_dir/." "$stage_pool_dir/"
fi

while IFS= read -r deb; do
    package_version=$(dpkg-deb -f -- "$deb" Version)
    package_version=${package_version#*:}
    package_arch=$(dpkg-deb -f -- "$deb" Architecture)
    canonical_name="${package_name}_${package_version}_${package_arch}.deb"
    staged_deb="$stage_pool_dir/$canonical_name"

    if [ -e "$staged_deb" ] && ! cmp -s -- "$deb" "$staged_deb"; then
        echo "Error: refusing to replace existing package $canonical_name with different content." >&2
        exit 2
    fi
    cp -f -- "$deb" "$staged_deb"
    printf '%s\n' "$canonical_name" >> "$publish_names"
done < "$deb_list"
sort -u "$publish_names" -o "$publish_names"

stage_release_dir="$work_dir/dists/$distribution"
for arch in $APT_REPO_ARCHITECTURES; do
    case "$arch" in
        ''|*[!A-Za-z0-9-]*)
            echo "Error: unsafe repository architecture '$arch'." >&2
            exit 2
            ;;
    esac
    binary_dir="$stage_release_dir/$component/binary-$arch"
    mkdir -p "$binary_dir"
    packages_file="$binary_dir/Packages"
    (
        cd "$stage_output"
        apt-ftparchive -o "APT::FTPArchive::Architecture=$arch" \
            packages "$pool_rel"
    ) > "$packages_file"
    gzip -9n < "$packages_file" > "$packages_file.gz"
done

release_tmp="$work_dir/Release"

apt-ftparchive \
    -o "APT::FTPArchive::Release::Origin=$APT_REPO_ORIGIN" \
    -o "APT::FTPArchive::Release::Label=$APT_REPO_LABEL" \
    -o "APT::FTPArchive::Release::Suite=$distribution" \
    -o "APT::FTPArchive::Release::Codename=$distribution" \
    -o "APT::FTPArchive::Release::Architectures=$APT_REPO_ARCHITECTURES" \
    -o "APT::FTPArchive::Release::Components=$APT_REPO_COMPONENTS" \
    -o "APT::FTPArchive::Release::Description=$APT_REPO_DESCRIPTION" \
    release "$stage_release_dir" > "$release_tmp"

if [ "$skip_signing" -eq 1 ]; then
    :
else
    release_gpg_tmp="$work_dir/Release.gpg"
    inrelease_tmp="$work_dir/InRelease"
    gpg --batch --yes --local-user "$APT_REPO_GPG_KEY" \
        --armor --detach-sign --output "$release_gpg_tmp" \
        "$release_tmp"
    gpg --batch --yes --local-user "$APT_REPO_GPG_KEY" \
        --clearsign --output "$inrelease_tmp" \
        "$release_tmp"
fi

mkdir -p "$pool_dir"
while IFS= read -r canonical_name; do
    cp -f -- "$stage_pool_dir/$canonical_name" "$pool_dir/$canonical_name"
done < "$publish_names"

release_dir="$output_dir/dists/$distribution"
for arch in $APT_REPO_ARCHITECTURES; do
    binary_dir="$release_dir/$component/binary-$arch"
    mkdir -p "$binary_dir"
    mv -f -- "$stage_release_dir/$component/binary-$arch/Packages" \
        "$binary_dir/Packages"
    mv -f -- "$stage_release_dir/$component/binary-$arch/Packages.gz" \
        "$binary_dir/Packages.gz"
done

mkdir -p "$release_dir"
mv -f -- "$release_tmp" "$release_dir/Release"
if [ "$skip_signing" -eq 1 ]; then
    rm -f -- "$release_dir/InRelease" "$release_dir/Release.gpg"
else
    mv -f -- "$release_gpg_tmp" "$release_dir/Release.gpg"
    mv -f -- "$inrelease_tmp" "$release_dir/InRelease"
fi

echo "Published $package_name for $distribution in $output_dir"
