# Changelog

## 0.9.0

- published the standalone single-file C HCC2D Streamer;
- support standard QR, HCC2D4, and HCC2D8 symbol streams;
- support HCC2D symbol versions 1--40 and EC levels L, M, Q, and H;
- derive the maximum shard payload from symbol capacity;
- split transfers into dynamic Cauchy Reed-Solomon erasure groups;
- use parity equal to 70% of the data-shard count by default;
- include integrity checks for streamed data;
- stream symbols from memory through SDL2 without temporary image files;
- add Makefile, regression checks, manual page, and Debian/Ubuntu packaging;
- intentionally exclude experimental HCC2D16 and experimental version 45.
