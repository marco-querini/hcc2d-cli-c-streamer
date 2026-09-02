# HCC2D CLI C Streamer 0.9.0

Written by Marco Querini

Standalone distribution of the HCC2D single-file C command-line Streamer.

The program transfers a file from a desktop computer to a smartphone by
displaying a repeating stream of QR, HCC2D4, or HCC2D8 symbols in an SDL2
window, or by exporting the complete sequence as a lossless animated GIF. The
HCC2D Decoder companion app captures the symbols and reconstructs the original
file.

HCC2DST v2 streams require HCC2D Decoder version 1.2.4 or later.

## Official links

- [HCC2D website](https://hcc2d.com/en/)
- [HCC2D specification PDF](https://hcc2d.com/hcc2d_specification_v0.9.0.pdf)
- [HCC2D API](https://hcc2d.com/en/api)
- HCC2D Decoder:
  - [Google Play](https://play.google.com/store/apps/details?id=com.hcc2d.decoder)
  - [Huawei AppGallery](https://appgallery.cloud.huawei.com/marketshare/app/C117478101)
  - [App Store for iPhone](https://apps.apple.com/app/id6762202762)

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

This public distribution supports the following symbol families and profiles:

- standard black-and-white QR Codes;
- HCC2D4 with four colors and 2 bits per data module;
- HCC2D8 with eight colors and 3 bits per data module;
- EC levels `L`, `M`, `Q`, and `H`;
- symbol versions `1..40`;
- input files up to 2 MiB (2,097,152 bytes), matching the current Decoder limit;
- HCC2DST v2 output;
- dynamic Reed-Solomon erasure groups;
- configurable frame rate, display, quiet zone, and redundancy;
- lossless, infinitely looping GIF89a export with integer module scaling;
- optional complete custom RGB palettes for HCC2D4 and HCC2D8.

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
cc -std=c11 -O2 -Wall -Wextra -Wpedantic $(pkg-config --cflags sdl2) \
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

Theoretical symbol-layer rate: **~0.47 Mbps**.

Stream HCC2D8 version 40 at 15 symbols per second with EC level L:

```bash
./hcc2d_streamer --mode hcc2d8 --ec-level L --version 40 --fps 15 document.pdf
```

Theoretical symbol-layer rate: **~1.06 Mbps**.

Usable throughput also depends on the display, receiving phone, framing,
lighting, and other capture conditions. In controlled optical-link testing, the
same HCC2D8 version 40, EC L, 15 fps profile with a Google Pixel 7 achieved a
mean file-transfer goodput of 1.036 Mbps. See the
[30-second 1 Mbps video demonstration](https://www.youtube.com/watch?v=z9uHewx-wNo).
Results on other hardware and in other environments may vary.

Stream HCC2D4:

```bash
./hcc2d_streamer --mode hcc2d4 --ec-level M --version 30 document.pdf
```

Theoretical symbol-layer rate: **~0.26 Mbps**.

Stream standard QR Codes:

```bash
./hcc2d_streamer --mode qr --ec-level L --version 20 document.pdf
```

Theoretical symbol-layer rate: **~0.08 Mbps**.

These are theoretical symbol-layer rates: the data-codeword capacity after
accounting for the internal QR/HCC2D Reed-Solomon overhead, multiplied by the
selected display rate. Application framing and external erasure-code parity are
not part of this symbol-layer calculation: parity supports recovery, and the
receiver can complete reconstruction as soon as the minimum required shards
are available.

## Lossless animated GIF export

Use `--export-gif` to write one complete interleaved symbol sequence and exit
without opening an SDL window:

```bash
./hcc2d_streamer --fps 10 --export-gif document-stream.gif document.pdf
```

The GIF loops indefinitely and includes every generated data and parity frame.
It uses the exact QR/HCC2D palette and GIF LZW compression; no lossy image or
video conversion is involved. Output is first completed in a temporary file
beside the destination and then installed atomically, so a failed export does
not replace an existing GIF with a partial file.

The canvas defaults to 1080 by 1080 pixels. `--gif-side` selects another square
canvas size:

```bash
./hcc2d_streamer --fps 12 --gif-side 640 \
  --export-gif document-stream.gif document.pdf
```

The canvas has the requested dimensions, while the symbol is centred inside it
and enlarged only by the largest integer factor that fits. Remaining pixels are
black margins outside the symbol's own quiet zone. Modules therefore remain
square, sharp, and equal-sized without interpolation or deformation.

GIF frame delays use centiseconds. Rates of 10 and 20 fps are exact per frame;
12 and 15 fps use alternating delays that approximate the requested rate as
closely as the format's centisecond timing permits.
> **Warning:** Open the exported GIF full-screen, ideally at 100% or an integer
> zoom, and disable smooth image scaling. A viewer that resizes the animation
> with interpolation can blur module edges and reduce decoding reliability.

Playback timing ultimately depends on the GIF viewer; 10 fps is the conservative
choice. A slower viewer only extends transfer time, while dropped frames can
normally be recovered on a later loop through the Reed-Solomon parity.

## Main options

| Option | Values | Default | Meaning |
|---|---:|---:|---|
| `--mode` | `qr`, `hcc2d4`, `hcc2d8` | `hcc2d8` | Symbol family |
| `--ec-level` | `L`, `M`, `Q`, `H` | `M` | Error correction inside each symbol |
| `--version` | `1..40` | `33` | Fixed symbol version |
| `--fps` | `10`, `12`, `15`, `20` | `12` | Displayed or exported symbols per second |
| `--export-gif` | output path | off | Export one complete looping GIF and exit |
| `--gif-side` | `1..8192` | `1080` | Square GIF canvas side; requires `--export-gif` |
| `--display` | non-negative integer | `0` | SDL display used for window placement |
| `--quiet-zone` | `0..16` | `4` | Quiet-zone width in modules |
| `--no-titlebar` | flag | off | Hide window decorations |
| `--palette-rgb` | RGB list | built in | Complete HCC2D4/8 palette |

The available display rates divide a commonly used 60 Hz refresh rate into
an integer number of refresh cycles per symbol: 6, 5, 4, or 3 cycles at 10,
12, 15, or 20 symbols per second, respectively.

The shard payload size is derived automatically from the selected symbol
family, EC level, and version. It is the largest payload that fits exactly in
one symbol after the HCC2DST and HCC2DF headers.

## Reed-Solomon transfer redundancy

| Option | Range | Default | Meaning |
|---|---:|---:|---|
| `--max-data-shards` | `1..255` | `150` | Maximum data shards in one erasure group |
| `--parity-ratio` | `0..1` | `0.70` | Parity shards relative to data shards |

For each group, the Streamer computes `round(k * parity-ratio)` parity shards.
The ratio accepts up to six decimal places. The default value of `0.70` adds
approximately 70 parity shards per 100 data shards. A full default group
contains 150 data and 105 parity shards, so as many as 105 of 255 transmitted
symbols may be lost. If the requested maximum data-shard count would make a
group exceed `k + m <= 255`, the Streamer automatically lowers the effective
group limit. Consequently, every `--parity-ratio` value from 0 to 1 remains
usable.

Symbols are generated in memory and rendered through a reusable SDL texture,
or written directly into the requested GIF without temporary frame images.

## Security and privacy

The transfer is local and one-way through displayed symbols. The command-line
Streamer does not require an account, network connection, or credentials. It
does not upload the selected file.

Anyone able to see and decode enough displayed symbols may reconstruct the
file.

## License

Apache License 2.0. See [`LICENSE`](LICENSE).

This software is provided without warranty. See the license and the notice in
the source header for the applicable terms.

## Notice

The views expressed in connection with the HCC2D Code Specification, HCC2D applications distributed through app stores, and HCC2D source code released as open source are solely those of the author and do not necessarily reflect the official position of any institution, organization, or employer with which the author is or has been affiliated.
