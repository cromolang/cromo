# Third-party libraries

Libraries in C compiled into `cromo` with the clang of LLVM, without the C
library, and linked into the programs that use them (D233, D236). Only their
sources, headers and licenses are kept here.

| Library | Version | Source | SHA-256 of the archive | License |
|---|---|---|---|---|
| mbedTLS | 3.6.7 (LTS) | https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` | Apache 2.0 or GPL 2.0 or later (`mbedtls/LICENSE`) |
| Monocypher | 4.0.3 | https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz | `8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66` | CC0 1.0 or 2-clause BSD (`monocypher/LICENCE.md`) |

From mbedTLS: `include/mbedtls`, `include/psa` and the `.c` and `.h` files of
`library`. From Monocypher: `src/monocypher.c`, `src/monocypher.h` and Ed25519, in
`src/optional/monocypher-ed25519.c` and `.h`.
