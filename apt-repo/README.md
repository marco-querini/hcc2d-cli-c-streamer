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
distribution and architecture matrix. One repository component is supported;
it is `main` by default.

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

- `gpg` and `gpgv`;
- `apt-ftparchive` from `apt-utils`;
- `dpkg-deb`, `gzip`, and `sha256sum`;
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

The publishing script validates every package, prepares package indexes and
signatures in a staging directory, and then installs the completed metadata.
The check script verifies the repository fields, compressed indexes, package
sizes and SHA-256 hashes, and both APT signatures when signed metadata is
present.

Generated pools, distribution metadata, and temporary files are ignored by
Git. They must be deployed separately to the actual HTTPS package host.

## Official repository and installation

The official HCC2D Streamer repository is hosted at:

```text
https://hcc2d.com/apt-streamer/
```

Debian/Ubuntu users can install it with:

```bash
curl -fsSL https://hcc2d.com/apt-streamer/hcc2d-archive-keyring.gpg \
  | sudo tee /usr/share/keyrings/hcc2d-archive-keyring.gpg >/dev/null

echo "deb [signed-by=/usr/share/keyrings/hcc2d-archive-keyring.gpg] https://hcc2d.com/apt-streamer noble main" \
  | sudo tee /etc/apt/sources.list.d/hcc2d-streamer.list >/dev/null

sudo apt update
sudo apt install hcc2d-streamer
```

Replace `noble` with the user distribution codename as needed.

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
