# Third-party libraries

Libraries in C compiled into `cromo` with the clang of LLVM, without the C
library, and linked into the programs that use them (D233, D236, D429). Only
their sources, headers and licenses are kept here.

| Library | Version | Source | SHA-256 of the archive | License |
|---|---|---|---|---|
| mbedTLS | 3.6.7 (LTS) | https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2 | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` | Apache 2.0 or GPL 2.0 or later (`mbedtls/LICENSE`) |
| Monocypher | 4.0.3 | https://github.com/LoupVaillant/Monocypher/releases/download/4.0.3/monocypher-4.0.3.tar.gz | `8cc9bc341a66249016db9bd70e9142d8d0aef9945973744b1ac05dbc55d8ee66` | CC0 1.0 or 2-clause BSD (`monocypher/LICENCE.md`) |
| SQLite | 3.54.1 | https://sqlite.org/2026/sqlite-amalgamation-3540100.zip | `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9` (SHA3-256 `92281c86e2957f9b9305890f9ac04204caa7b677cc868dc326d942689df28e30`, as sqlite.org gives it) | public domain (the top of `sqlite/sqlite3.c`) |
| SDL | 3.4.18 | https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` | zlib (`SDL/LICENSE.txt`) |
| stb_truetype | 1.26 | https://raw.githubusercontent.com/nothings/stb/6e9f34d5429cf16790ec43c9bac3f1ee4ad1f760/stb_truetype.h | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` (the file) | public domain or MIT (at the end of `stb/stb_truetype.h`) |
| stb_image | 2.30 | https://raw.githubusercontent.com/nothings/stb/013ac3beddff3dbffafd5177e7972067cd2b5083/stb_image.h | `594c2fe35d49488b4382dbfaec8f98366defca819d916ac95becf3e75f4200b3` (the file) | public domain or MIT (at the end of `stb/stb_image.h`) |
| Inter | 4.1 | https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip | `9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e` | SIL Open Font License 1.1 (`inter/LICENSE.txt`) |
| nanosvg | 239e102 (master) | https://raw.githubusercontent.com/memononen/nanosvg/239e102ec2c691f2902e20ace2ed36ee4a35cfe6/src/nanosvg.h and `nanosvgrast.h` | `e34fd5d084be106cea972d19ce5d27fd96d17ba89f8d06bdceee058420c8b2b0` and `79a9c5f4db19debf9f3a648a1589e96d92854f245a5cb4f3d823f263785234d8` (the files) | zlib (`nanosvg/LICENSE.txt`) |
| JetBrains Mono | 2.304 | https://github.com/JetBrains/JetBrainsMono/releases/download/v2.304/JetBrainsMono-2.304.zip | `6f6376c6ed2960ea8a963cd7387ec9d76e3f629125bc33d1fdcd7eb7012f7bbf` | SIL Open Font License 1.1 (`jetbrains-mono/OFL.txt`) |
| Lucide | 1.68.0 | https://registry.npmjs.org/lucide-static/-/lucide-static-1.68.0.tgz | `62e0cdd6158bbb81877a95b757d739c60339dbb286153c074664f2b1ec8b2e94` | ISC, and MIT for the icons of Feather (`lucide/LICENSE`) |

From SQLite: `sqlite3.c` and `sqlite3.h` of the amalgamation, compiled for a system
without the C library with `runtime/db/sqlite_config.h` in front (D529). From mbedTLS: `include/mbedtls`, `include/psa` and the `.c` and `.h` files of
`library`. From Monocypher: `src/monocypher.c`, `src/monocypher.h` and Ed25519, in
`src/optional/monocypher-ed25519.c` and `.h`. From SDL: `CMakeLists.txt`, `cmake`,
`include`, `src`, `wayland-protocols`, `LICENSE.txt` and `REVISION.txt`, and the classes
of Java of its activity for Android, `android-project/app/src/main/java` (D452), without
the tests, the examples, the documents and the projects of the IDEs; SDL is built with
its CMake, not with the clang of the runtime (`runtime/sdl`). From nanosvg: `src/nanosvg.h`, `src/nanosvgrast.h` and `LICENSE.txt`, compiled with the images of
`std.gui` (D484). From Inter:
`extras/ttf/Inter-Regular.ttf`, `extras/ttf/Inter-SemiBold.ttf` and `LICENSE.txt`. From
JetBrains Mono: `OFL.txt`, and `JetBrainsMonoNL-Regular.ttf` and `JetBrainsMonoNL-SemiBold.ttf`,
written by `tools/mono.py` from those of `fonts/ttf` without what stb_truetype does not read,
the hinting, the substitutions and the names of the glyphs (D492). From
Lucide: `LICENSE`, and `lucide-icons.ttf`, the icons of `std.gui` taken from
`font/lucide.ttf` by `tools/lucide.py`, which also writes the enum `gui.Icon`.

The time zones of `std.time` are not here: `library/std/zoneinfo.dat` holds the compiled
files of the IANA Time Zone Database (https://www.iana.org/time-zones, public domain),
version 2025b, as the package `tzdata` 2025.2 of Python carries them, packed by
`tools/zoneinfo.py` (D533). A new version of the database needs only that tool run again.
