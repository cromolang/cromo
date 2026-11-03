# Third-party libraries

Libraries in C compiled into `cromo` with the clang of LLVM, without the C
library, and linked into the programs that use them (D233, D236, D429). Only
their sources, headers and licenses are kept here.

| Library | Version | Source | SHA-256 of the archive | License |
|---|---|---|---|---|
| mbedTLS | 3.6.7 (LTS) | https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` | Apache 2.0 or GPL 2.0 or later (`mbedtls/LICENSE`) |
| Monocypher | 4.0.3 | https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz | `8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66` | CC0 1.0 or 2-clause BSD (`monocypher/LICENCE.md`) |
| SDL | 3.4.18 | https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` | zlib (`SDL/LICENSE.txt`) |

From mbedTLS: `include/mbedtls`, `include/psa` and the `.c` and `.h` files of
`library`. From Monocypher: `src/monocypher.c`, `src/monocypher.h` and Ed25519, in
`src/optional/monocypher-ed25519.c` and `.h`. From SDL: `CMakeLists.txt`, `cmake`,
`include`, `src`, `wayland-protocols`, `LICENSE.txt` and `REVISION.txt`, without the
tests, the examples, the documents and the projects of the IDEs; SDL is built with
its CMake, not with the clang of the runtime (`runtime/sdl`).
