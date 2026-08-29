# HCC2D CLI C Streamer 0.9.0

Written by Marco Querini

Standalone distribution of the HCC2D single-file C command-line Streamer.

The program transfers a file from a desktop computer to a smartphone by
displaying a repeating stream of QR, HCC2D4, or HCC2D8 symbols in an SDL2
window. The HCC2D Decoder companion app captures the symbols and reconstructs
the original file.

## Official links

- HCC2D website: `https://hcc2d.com/en/`
- HCC2D specification PDF: `https://hcc2d.com/hcc2d_specification_v0.9.0.pdf`
- HCC2D API: `https://hcc2d.com/en/api`
- HCC2D Decoder:
  - Google Play: `https://play.google.com/store/apps/details?id=com.hcc2d.decoder`
  - Huawei AppGallery: `https://appgallery.cloud.huawei.com/marketshare/app/C117478101`
  - App Store for iPhone: `https://apps.apple.com/app/id6762202762`

## Contents

- `single_file_c_hcc2d_streamer_v0.9.0.c`
- `test_single_file_streamer.sh`
- `LICENSE`
- `CHANGELOG.md`
- `Makefile`
- `SHA256SUMS.txt`
- `hcc2d_streamer.1`
- `debian/` packaging for Debian and Ubuntu
- `apt-repo/` publication tooling for a signed APT repository

## Scope

This public distribution supports only the non-experimental symbol families:

- standard black-and-white QR Codes;
- HCC2D4 with four colours and 2 bits per data module;
- HCC2D8 with eight colours and 3 bits per data module;
- EC levels `L`, `M`, `Q`, and `H`;
- symbol versions `1..40`;
- dynamic Reed-Solomon erasure groups;
- configurable frame rate, display, quiet zone, and redundancy;
- optional complete custom RGB palettes for HCC2D4 and HCC2D8.

Experimental HCC2D16 and experimental symbol version 45 are intentionally not
included and cannot be selected through this CLI.

## Build from source

### Debian or Ubuntu dependencies

```bash
sudo apt update
sudo apt install build-essential pkg-config libsdl2-dev zlib1g-dev
```

### Build

```bash
make
```

Equivalent direct command:

```bash
cc -O2 -Wall $(pkg-config --cflags sdl2) \
  single_file_c_hcc2d_streamer_v0.9.0.c \
  $(pkg-config --libs sdl2) -lz -lm -o hcc2d_streamer
```

The implementation is contained in the single C source file. SDL2 and zlib
are linked as system libraries; no other project source files are required.

Run the regression checks:

```bash
make test
```

Install under `/usr/local`:

```bash
sudo make install
```

Install into a staging directory:

```bash
make install DESTDIR=/tmp/hcc2d-streamer-stage PREFIX=/usr
```

## Linux package

Build a Debian/Ubuntu binary package:

```bash
sudo apt install build-essential debhelper pkg-config libsdl2-dev zlib1g-dev
dpkg-buildpackage -us -uc -b
```

The `.deb` is written to the parent directory. Install it with:

```bash
sudo apt install ../hcc2d-streamer_0.9.0-1_amd64.deb
```

The package installs:

- `/usr/bin/hcc2d_streamer`
- `/usr/share/man/man1/hcc2d_streamer.1.gz`
- package documentation under `/usr/share/doc/hcc2d-streamer/`

The `apt-repo/` directory contains tooling for publishing the package in a
signed APT repository. It uses the same public HCC2D archive key as the HCC2D
Encoder package. Private signing keys are never stored in this repository.

## Usage

```bash
hcc2d_streamer [options] FILE
```

Press **Esc** or close the window to stop streaming.

### Examples

Use the default HCC2D8 profile:

```bash
./hcc2d_streamer document.pdf
```

Stream HCC2D8 version 40 at 15 symbols per second with EC level L:

```bash
./hcc2d_streamer --mode hcc2d8 --ec-level L --version 40 --fps 15 document.pdf
```

Stream HCC2D4:

```bash
./hcc2d_streamer --mode hcc2d4 --ec-level M --version 30 document.pdf
```

Stream standard QR Codes:

```bash
./hcc2d_streamer --mode qr --ec-level L --version 20 document.pdf
```

The equivalent palette-size syntax is `--colors 2`, `--colors 4`, or
`--colors 8`. Do not combine `--colors` and `--mode`.

## Main options

| Option | Values | Default | Meaning |
|---|---:|---:|---|
| `--mode` | `qr`, `hcc2d4`, `hcc2d8` | `hcc2d8` | Symbol family |
| `--colors` | `2`, `4`, `8` | `8` | Palette-size form of `--mode` |
| `--ec-level` | `L`, `M`, `Q`, `H` | `M` | Error correction inside each symbol |
| `--version` | `1..40` | `30` | Fixed symbol version |
| `--fps` | `10..20` | `10` | Displayed symbols per second |
| `--display` | non-negative index | `0` | SDL display used for window placement |
| `--quiet-zone` | `0..16` | `4` | Quiet-zone width in modules |
| `--no-titlebar` | flag | off | Hide window decorations |
| `--palette-rgb` | RGB list | built in | Complete HCC2D4/8 palette |

The shard payload size is derived automatically from the selected symbol
family, EC level, and version. It is the largest payload that fits exactly in
one symbol after the HCC2DST and HCC2DF headers.

## Reed-Solomon transfer redundancy

| Option | Range | Default | Meaning |
|---|---:|---:|---|
| `--k-max` | `1..254` | `150` | Maximum data shards in one erasure group |
| `--parity-num` | non-negative integer | `70` | Parity-ratio numerator |
| `--parity-den` | positive integer | `100` | Parity-ratio denominator |

For each group, the Streamer computes approximately
`round(k * parity-num / parity-den)` parity shards. The default parity is 70%
of the data-shard count. A full default group contains 150 data and 105 parity
shards, so as many as 105 of 255 transmitted symbols may be lost. The program
rejects settings that would exceed the limit `k + m <= 255`.

The Streamer writes no temporary symbol images to disk. Symbols are generated
in memory and uploaded directly to SDL textures.

## Security and privacy

The transfer is local and one-way through displayed symbols. The command-line
Streamer does not require an account, network connection, or credentials. It
does not upload the selected file.

Anyone able to see and decode enough displayed symbols may reconstruct the
file. Use the Streamer only where the screen is appropriately protected.

## License

Apache License 2.0. See [`LICENSE`](LICENSE).

This software is provided without warranty. See the license and the notice in
the source header for the applicable terms.
