# Third-party notices

AniToolsPlatform itself is licensed under the Apache License 2.0 (`LICENSE`, `NOTICE`). This file
lists the third-party components that reach a user, together with the notices their licenses require.
It exists because those licenses ask for it, not because of section 4(d) of the Apache License: the
`NOTICE` file is deliberately kept to the project's own copyright line, since everything placed there
has to be reproduced by every downstream distribution forever.

| Component | Version | License | Reaches the user |
|---|---|---|---|
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | MIT | yes — compiled into `atp_app`, `atp_mcp`, `atp_studio` |
| [Qt 6](https://www.qt.io/) (Widgets, Svg, and the plugins deployed beside the executable) | 6.10.3 | LGPL-3.0-only | yes — shipped next to `atp_studio`, in packages that contain it |
| [GoogleTest](https://github.com/google/googletest) | 1.17.0 | BSD-3-Clause | no — test binaries only, not distributed |
| [aqtinstall](https://github.com/miurahr/aqtinstall) | as resolved by pip | MIT | no — a build-time downloader, see `cmake/AutoInstallQt.cmake` |

The last two are listed for completeness: they are used to build and test the project and are not part
of any package it produces. The SDK package (`--component sdk`: the headers under `include/atp` and the
CMake package files) contains no third-party code at all — `atp_runtime`, the target that uses
nlohmann/json, is deliberately not exported.

## nlohmann/json 3.12.0 — MIT

Obtained as a release archive by FetchContent (`src/runtime/CMakeLists.txt`) and used unmodified.

```
MIT License

Copyright (c) 2013-2025 Niels Lohmann

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Qt 6.10.3 — LGPL-3.0

`atp_studio` uses the Qt Widgets and Qt Svg modules of the Qt Toolkit under the **GNU Lesser General
Public License version 3**. Qt is a separate work by The Qt Company Ltd. and its contributors, and is
**not** covered by this project's Apache-2.0 license.

- Qt is used **unmodified**, as published by The Qt Company: version 6.10.3, the official prebuilt
  desktop binaries (this build obtains them with `aqtinstall`, see `cmake/AutoInstallQt.cmake`).
- Qt is linked **dynamically**. Its libraries and plugins are placed beside the executable rather than
  built into it, so a recipient may replace them with their own build of the same Qt version, modified
  or not, and run `atp_studio` against it. Nothing in this distribution restricts that.
- The complete corresponding source of Qt 6.10.3 is published by The Qt Company at
  <https://download.qt.io/archive/qt/6.10/6.10.3/single/> and in the tagged repositories at
  <https://code.qt.io/cgit/qt/qt5.git/> (`v6.10.3`).
- The text of the LGPL version 3 is at <https://www.gnu.org/licenses/lgpl-3.0.txt>, and the GPL
  version 3 it incorporates by reference at <https://www.gnu.org/licenses/gpl-3.0.txt>.
- Qt in turn contains third-party code of its own (FreeType, HarfBuzz, libpng, PCRE2, zlib and
  others). Their notices are documented by Qt itself under "Licenses Used in Qt"
  (<https://doc.qt.io/qt-6/licenses-used-in-qt.html>), and the kit ships machine-readable SPDX SBOM
  files listing them per module in its `sbom/` directory.

A build made with `-DATP_BUILD_STUDIO=OFF`, and any package that does not contain `atp_studio`, carries
no Qt code and none of the above applies to it.

## GoogleTest 1.17.0 — BSD-3-Clause

Used by `atp_tests` and `atp_ui_tests` only; no package produced by this project contains it.

```
Copyright 2008, Google Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:

    * Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following disclaimer
in the documentation and/or other materials provided with the
distribution.
    * Neither the name of Google Inc. nor the names of its
contributors may be used to endorse or promote products derived from
this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## aqtinstall — MIT

A Python tool, installed into a throwaway virtual environment under `external/` when
`ATP_AUTO_INSTALL_QT` is on, used to download the Qt kit. Neither it nor its dependencies are
redistributed by this project; its license text travels with its own distribution on PyPI.
