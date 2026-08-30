#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_ROOT/apt-repo/repo.conf"

if [ -z "${APT_REPO_GPG_KEY:-}" ]; then
    echo "Error: set APT_REPO_GPG_KEY to the signing-key fingerprint." >&2
    exit 2
fi

case "$APT_REPO_KEYRING_OUTPUT" in
    ''|.|..|/*|../*|*/../*|*/..)
        echo "Error: APT_REPO_KEYRING_OUTPUT must be a safe repository-relative path." >&2
        exit 2
        ;;
esac

key_output="$REPO_ROOT/$APT_REPO_KEYRING_OUTPUT"
mkdir -p "$(dirname "$key_output")"
key_tmp=$(mktemp "${key_output}.tmp.XXXXXX")
trap 'rm -f -- "$key_tmp"' EXIT HUP INT TERM
gpg --batch --yes --export "$APT_REPO_GPG_KEY" > "$key_tmp"

if [ ! -s "$key_tmp" ]; then
    echo "Error: GnuPG did not export a public key." >&2
    exit 1
fi

public_key_count=$(gpg --batch --show-keys --with-colons "$key_tmp" |
    awk -F: '$1 == "pub" { count++ } END { print count + 0 }')
fingerprint=$(gpg --batch --show-keys --with-colons "$key_tmp" |
    awk -F: '$1 == "fpr" { print $10; exit }')
requested_key=$(printf '%s' "$APT_REPO_GPG_KEY" | tr '[:lower:]' '[:upper:]')
case "$fingerprint" in
    *"$requested_key") ;;
    *)
        echo "Error: exported-key fingerprint does not match APT_REPO_GPG_KEY." >&2
        exit 1
        ;;
esac
if [ "$public_key_count" -ne 1 ]; then
    echo "Error: expected one exported public key, got $public_key_count." >&2
    exit 1
fi

mv -f -- "$key_tmp" "$key_output"
trap - EXIT HUP INT TERM
echo "Wrote $APT_REPO_KEYRING_OUTPUT"
