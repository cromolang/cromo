# Third-party libraries

Libraries in C compiled into `cromo` with the clang of LLVM, without the C
library, and linked into the programs that use them (D233, D236, D429). Only
their sources, headers and licenses are kept here.

| Library | Version | Source | SHA-256 of the archive | License |
|---|---|---|---|---|
| mbedTLS | 3.6.7 (LTS) | https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` | Apache 2.0 or GPL 2.0 or later (`mbedtls/LICENSE`) |
| Monocypher | 4.0.3 | https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz | `8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66` | CC0 1.0 or 2-clause BSD (`monocypher/LICENCE.md`) |
| SDL | 3.4.18 | https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` | zlib (`SDL/LICENSE.txt`) |
| stb_truetype | 1.26 | https://raw.githubusercontent.com/nothings/stb/6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760/stb_truetype.h | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` (the file) | public domain or MIT (at the end of `stb/stb_truetype.h`) |
| stb_image | 2.30 | https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` (the file) | public domain or MIT (at the end of `stb/stb_image.h`) |
| Inter | 4.1 | https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip | `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e` | SIL Open Font License 1.1 (`inter/LICENSE.txt`) |
| Lucide | 1.68.0 | https://registry.npmjs.org/lucide-static/-/lucide-static-1.68.0.tgz | `62e0cdd6158bbb81877a95b757d739c60339dbb286153c074664f2b1ec8b2e94` | ISC, and MIT for the icons of Feather (`lucide/LICENSE`) |

From mbedTLS: `include/mbedtls`, `include/psa` and the `.c` and `.h` files of
`library`. From Monocypher: `src/monocypher.c`, `src/monocypher.h` and Ed25519, in
`src/optional/monocypher-ed25519.c` and `.h`. From SDL: `CMakeLists.txt`, `cmake`,
`include`, `src`, `wayland-protocols`, `LICENSE.txt` and `REVISION.txt`, and the classes
of Java of its activity for Android, `android-project/app/src/main/java` (D452), without
the tests, the examples, the documents and the projects of the IDEs; SDL is built with
its CMake, not with the clang of the runtime (`runtime/sdl`). From Inter:
`extras/ttf/Inter-Regular.ttf`, `extras/ttf/Inter-SemiBold.ttf` and `LICENSE.txt`. From
Lucide: `LICENSE`, and `lucide-icons.ttf`, the icons of `std.gui` taken from
`font/lucide.ttf` by `tools/lucide.py`, which also writes the enum `gui.Icon`.
