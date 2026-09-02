# Changelog

## 0.9.0

- Published the standalone single-file C HCC2D Streamer.
- Added standard QR, HCC2D4, and HCC2D8 symbol streams.
- Added HCC2D symbol versions 1--40 and EC levels L, M, Q, and H.
- Set the default streaming profile to HCC2D8 version 33, EC level M, at
  12 symbols per second.
- Added HCC2DST v2 output with stream-integrity checks.
- Derived the maximum shard payload from the selected symbol capacity.
- Enforced the Decoder-compatible 2 MiB maximum input-file size.
- Split transfers into dynamic Cauchy Reed-Solomon erasure groups.
- Exposed transfer redundancy through a single `--parity-ratio` option.
- Set default parity to 70% of the data-shard count.
- Limited display rates to 10, 12, 15, and 20 symbols per second for regular
  cadence on commonly used 60 Hz displays.
- Streamed symbols from memory through SDL2 without temporary image files.
- Added lossless animated GIF89a export with LZW compression, complete
  infinitely looping sequences, requested frame timing, and integer-only module
  scaling inside a configurable square canvas.
- Added an explicit full-screen/no-smoothing playback warning and atomic GIF
  output replacement so failed exports do not overwrite an existing result.
- Added a Makefile, regression checks, a manual page, and Debian/Ubuntu
  packaging.
- Excluded experimental HCC2D16 and experimental version 45.
