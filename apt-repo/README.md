# HCC2D Streamer APT Repository

This directory contains the infrastructure for publishing the
`hcc2d-streamer` Debian package in a signed APT repository for Debian and
Ubuntu users.

It is separate from the package sources:

- `debian/` builds the `.deb` package;
- `apt-repo/` publishes packages for `apt update`, `apt install`, `apt
  upgrade`, and `apt remove`.

## Supported distributions

The default configuration includes:

- Debian `bookworm`;
- Debian `trixie`;
- Ubuntu `jammy`;
- Ubuntu `noble`.

Edit `apt-repo/repo.conf` or override its environment variables to change the
matrix.

## Signing key

The Streamer uses the same public HCC2D archive key as the HCC2D Encoder:

```text
4DCDC5549D1955166EE3E83A567381E2B0354085
```

Only the public key `apt-repo/public/hcc2d-archive-keyring.gpg` is versioned.
Private GnuPG material must remain outside the repository and is covered by
the top-level `.gitignore` if a local `.secrets/` directory is used.

When the private key is stored in a non-default keyring, provide its location
only for the publishing command:

```bash
GNUPGHOME=/path/to/private/apt-gpg \
  apt-repo/scripts/publish.sh --distribution noble \
  --deb ../hcc2d-streamer_0.9.0-1_amd64.deb
```

The publishing script passes the selected key to GnuPG; it never copies
private-key files into this repository.

## Prerequisites

- `gpg`;
- `apt-ftparchive` from `apt-utils`;
- a `.deb` built with `dpkg-buildpackage`.

## Build the package

From the repository root:

```bash
dpkg-buildpackage -us -uc -b
```

## Publish metadata

Publish for Ubuntu 24.04:

```bash
apt-repo/scripts/publish.sh \
  --distribution noble \
  --deb ../hcc2d-streamer_0.9.0-1_amd64.deb
```

Generate unsigned metadata for a dry run:

```bash
apt-repo/scripts/publish.sh \
  --distribution noble \
  --deb ../hcc2d-streamer_0.9.0-1_amd64.deb \
  --skip-signing
```

Validate the generated repository:

```bash
apt-repo/scripts/check.sh --distribution noble
```

Generated pools, distribution metadata, and temporary files are ignored by
Git. They must be deployed separately to the actual HTTPS package host.

## Export the public key

If the public key needs to be regenerated from the signing keyring:

```bash
GNUPGHOME=/path/to/private/apt-gpg \
  apt-repo/scripts/export-public-key.sh
```

Verify the exported fingerprint before committing it:

```bash
gpg --show-keys --with-fingerprint \
  apt-repo/public/hcc2d-archive-keyring.gpg
```

Never add a private key, `private-keys-v1.d`, revocation certificate, or a
complete private GnuPG home to this public repository.
