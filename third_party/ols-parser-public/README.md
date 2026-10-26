# Upstream reference parser

Contains `ols.c`, `ols.h`, and `ols_dump.c` from [JSD-SpeedUP/ols-parser-public](https://github.com/JSD-SpeedUP/ols-parser-public).

Upstream describes this as a zero-dependency C99 parser for WinOLS project files. It exposes header metadata, version/ROM segments, and map records including axis and cell-layout fields. It is included as an independent development-time reference, not as the GUI's primary renaming parser.

- License: BSD Zero Clause License (0BSD), retained in `LICENSE`.
- Upstream interpretations are reverse-engineered, not an official EVC specification.
- A successful parse does not prove every bit's semantics are known.
