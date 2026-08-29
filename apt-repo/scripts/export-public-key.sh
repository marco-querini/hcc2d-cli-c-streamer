#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
. "$REPO_ROOT/apt-repo/repo.conf"

if [ -z "${APT_REPO_GPG_KEY:-}" ]; then
    echo "Error: set APT_REPO_GPG_KEY to the signing-key fingerprint." >&2
    exit 2
fi

mkdir -p "$(dirname "$REPO_ROOT/$APT_REPO_KEYRING_OUTPUT")"
gpg --batch --yes --export "$APT_REPO_GPG_KEY" \
    > "$REPO_ROOT/$APT_REPO_KEYRING_OUTPUT"

if [ ! -s "$REPO_ROOT/$APT_REPO_KEYRING_OUTPUT" ]; then
    echo "Error: GnuPG did not export a public key." >&2
    exit 1
fi

echo "Wrote $APT_REPO_KEYRING_OUTPUT"
