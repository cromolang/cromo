# Third-party notices

Cromo is distributed under the MIT license or the Apache License 2.0, at your option ([LICENSE-MIT](LICENSE-MIT), [LICENSE-APACHE](LICENSE-APACHE)). It contains the following software of others.

## LLVM

`cromo` contains LLVM and LLD, linked statically: <https://llvm.org>.

Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions (SPDX: `Apache-2.0 WITH LLVM-exception`). The text of the Apache License 2.0 is in `LICENSE-APACHE`; the exceptions follow.

```
---- LLVM Exceptions to the Apache 2.0 License ----

As an exception, if, as a result of your compiling your source code, portions
of this Software are embedded into an Object form of such source code, you
may redistribute such embedded portions in such Object form without complying
with the conditions of Sections 4(a), 4(b) and 4(d) of the License.

In addition, if you combine or link compiled forms of this Software with
software that is licensed under the GPLv2 ("Combined Software") and if a
court of competent jurisdiction determines that the patent provision (Section
3), the indemnity provision (Section 9) or other Section of the License
conflicts with the conditions of the GPLv2, you may retroactively and
prospectively choose to deem waived or otherwise exclude such Section(s) of
the License, but only in their entirety and only with respect to the Combined
Software.
```

## Mbed TLS

The runtime of Cromo, linked into the programs that use TLS or `std.crypto`, contains Mbed TLS 3.6: <https://github.com/Mbed-TLS/mbedtls>.

Licensed under the Apache License 2.0 or the GNU General Public License 2.0 or later; Cromo uses it under the Apache License 2.0. Its license is in `licenses/mbedtls.txt` (`third_party/mbedtls/LICENSE` in the repository).

## Monocypher

The runtime contains Monocypher 4.0, for Ed25519 and X25519: <https://monocypher.org>.

Dual-licensed under CC0 1.0 or the 2-clause BSD license. Its license is in `licenses/monocypher.md` (`third_party/monocypher/LICENCE.md` in the repository).

## SQLite

The runtime contains SQLite 3.54, for `std.db`: <https://sqlite.org>.

SQLite is in the public domain: its authors have dedicated it to the public domain, as the top of `third_party/sqlite/sqlite3.c` says, and it has no license to follow.

## IANA Time Zone Database

The library contains the time zones of the IANA Time Zone Database 2025b, for `std.time`, in the programs that ask for a zone by its name: <https://www.iana.org/time-zones>.

The database is in the public domain.

## SDL

The programs that use the windows of the library, and the apps for Android, contain SDL 3.4: <https://libsdl.org>.

Licensed under the zlib license. Its license is in `licenses/sdl.txt` (`third_party/SDL/LICENSE.txt` in the repository).

## stb_truetype

The programs that use the windows of the library contain stb_truetype 1.26, by Sean Barrett: <https://github.com/nothings/stb>. In the public domain, or under the MIT license, at your option; its license is at the end of `third_party/stb/stb_truetype.h` in the repository.

## stb_image

The programs that show images with the library contain stb_image 2.30, by Sean Barrett: <https://github.com/nothings/stb>. In the public domain, or under the MIT license, at your option; its license is at the end of `third_party/stb/stb_image.h` in the repository.

## Inter

The programs that use the windows of the library contain the font Inter 4.1, by The Inter Project Authors: <https://rsms.me/inter>. Licensed under the SIL Open Font License 1.1. Its license is in `licenses/inter.txt` (`third_party/inter/LICENSE.txt` in the repository).

## JetBrains Mono

The programs that use the windows of the library contain the font JetBrains Mono 2.304, without its hinting, by The JetBrains Mono Project Authors: <https://github.com/JetBrains/JetBrainsMono>. Licensed under the SIL Open Font License 1.1. Its license is in `licenses/jetbrains-mono.txt` (`third_party/jetbrains-mono/OFL.txt` in the repository).

## Lucide

The programs that use the windows of the library contain a part of the icons of Lucide 1.68.0, by Lucide Icons and Contributors: <https://lucide.dev>. Licensed under the ISC license, and the icons derived from Feather under the MIT license. Its license is in `licenses/lucide.txt` (`third_party/lucide/LICENSE` in the repository).

## zlib and zstd

`cromo` links statically zlib (<https://zlib.net>, zlib license) and zstd (<https://github.com/facebook/zstd>, BSD license), which LLVM uses.
