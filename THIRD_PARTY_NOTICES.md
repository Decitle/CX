# Third-party notices

CX Engine uses SFML 3.1.0 for its graphical window, input, and rendering integration. SFML is distributed under the zlib/libpng license.

The complete SFML license text is maintained in this source repository at [licenses/SFML-LICENSE.md](licenses/SFML-LICENSE.md). Curated runtime archives place the complete SFML notice and the CX Engine BSD 3-Clause license together in a single LICENSES.txt file, with each license clearly separated and reproduced in full.

The exact shared libraries present in a particular runtime package depend on the native build. The package's CX-Engine/runtime-manifest.json records all packaged files and their SHA-256 hashes. System libraries supplied by the operating system are not copied into the package. Any additional third-party dependency bundled by a future build must have its applicable license text added to the consolidated runtime license file before release.

CX Engine itself is distributed under the BSD 3-Clause License in LICENSE.
