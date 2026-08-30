#!/bin/sh
set -eu

usage() {
    echo "Usage: apt-repo/scripts/check.sh --distribution CODENAME"
}

fail() {
    echo "Error: $1" >&2
    exit 1
}

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_ROOT/apt-repo/repo.conf"

distribution=

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

output_dir="$REPO_ROOT/$APT_REPO_OUTPUT_DIR"
release_dir="$output_dir/dists/$distribution"
release_file="$release_dir/Release"

[ -s "$release_file" ] || fail "missing or empty Release file for $distribution."
grep -Fqx "Codename: $distribution" "$release_file" ||
    fail "Release has an unexpected Codename field."
grep -Fqx "Components: $APT_REPO_COMPONENTS" "$release_file" ||
    fail "Release has an unexpected Components field."
grep -Fqx "Architectures: $APT_REPO_ARCHITECTURES" "$release_file" ||
    fail "Release has an unexpected Architectures field."

check_release_sha256() {
    relative_path=$1
    full_path="$release_dir/$relative_path"
    expected_hash=$(sha256sum "$full_path" | awk '{print $1}')
    expected_size=$(wc -c < "$full_path" | tr -d '[:space:]')
    awk -v hash="$expected_hash" -v size="$expected_size" \
        -v path="$relative_path" \
        '$1 == hash && $2 == size && $3 == path { found = 1 }
         END { exit found ? 0 : 1 }' "$release_file" ||
        fail "Release SHA256 entry does not match $relative_path."
}

for arch in $APT_REPO_ARCHITECTURES; do
    case "$arch" in
        ''|*[!A-Za-z0-9-]*)
            echo "Error: unsafe repository architecture '$arch'." >&2
            exit 2
            ;;
    esac
    packages_file="$release_dir/$component/binary-$arch/Packages"
    packages_gz="$packages_file.gz"
    [ -s "$packages_file" ] || fail "missing or empty Packages file for $arch."
    [ -s "$packages_gz" ] || fail "missing or empty Packages.gz file for $arch."
    grep -Fqx 'Package: hcc2d-streamer' "$packages_file" ||
        fail "Packages for $arch does not contain hcc2d-streamer."
    gzip -t "$packages_gz" || fail "Packages.gz for $arch is not valid gzip data."
    gzip -cd "$packages_gz" | cmp -s - "$packages_file" ||
        fail "Packages.gz for $arch differs from Packages."

    relative_packages="$component/binary-$arch/Packages"
    check_release_sha256 "$relative_packages"
    check_release_sha256 "$relative_packages.gz"

    awk '
        /^Filename: / { filename = substr($0, 11) }
        /^Size: /     { size = $2 }
        /^SHA256: /   { hash = $2 }
        /^$/ {
            if (filename != "") print hash "\t" size "\t" filename
            filename = size = hash = ""
        }
        END {
            if (filename != "") print hash "\t" size "\t" filename
        }
    ' "$packages_file" |
    while IFS="$(printf '\t')" read -r package_hash package_size package_path; do
        [ -n "$package_hash" ] && [ -n "$package_size" ] && [ -n "$package_path" ] ||
            fail "Packages for $arch contains an incomplete package record."
        case "$package_path" in
            ''|.|..|/*|../*|*/../*|*/..)
                fail "Packages for $arch contains an unsafe Filename field."
                ;;
        esac
        package_file="$output_dir/$package_path"
        [ -f "$package_file" ] || fail "missing package referenced by Packages: $package_path."
        actual_size=$(wc -c < "$package_file" | tr -d '[:space:]')
        [ "$actual_size" = "$package_size" ] ||
            fail "package size does not match Packages: $package_path."
        actual_hash=$(sha256sum "$package_file" | awk '{print $1}')
        [ "$actual_hash" = "$package_hash" ] ||
            fail "package SHA256 does not match Packages: $package_path."
    done
done

inrelease_file="$release_dir/InRelease"
release_signature="$release_dir/Release.gpg"
signature_status=unsigned
if [ -e "$inrelease_file" ] || [ -e "$release_signature" ]; then
    [ -s "$inrelease_file" ] && [ -s "$release_signature" ] ||
        fail "signed metadata is incomplete."
    keyring="$REPO_ROOT/$APT_REPO_KEYRING_OUTPUT"
    [ -s "$keyring" ] || fail "public signing keyring is missing."
    gpgv --keyring "$keyring" "$release_signature" "$release_file" >/dev/null 2>&1 ||
        fail "Release.gpg signature verification failed."
    gpgv --keyring "$keyring" --output - "$inrelease_file" 2>/dev/null |
        cmp -s - "$release_file" || fail "InRelease does not match Release."
    signature_status=signed
fi

echo "Repository metadata for $distribution is valid ($signature_status)."
