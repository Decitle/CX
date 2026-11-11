# Third-party notices

CX Engine uses SFML 3.1.0 for its graphical window, input, and rendering integration. SFML is distributed under the permissive zlib/libpng license. Its complete license text is included in each runtime archive inside the single root-level LICENSES.txt file. SFML permits commercial and proprietary use; the license requires that its origin not be misrepresented and that altered SFML source versions be clearly marked. An acknowledgement is appreciated but is not mandatory.

SFML 3.1.0 also uses components under separate licenses. When bundled by the target build, these include FreeType (FreeType Project License), HarfBuzz (Old MIT License, including its Microsoft USE component notice), and SheenBidi (Apache 2.0). SFML's vendored QOI and cpp-unicodelib components use MIT licenses; its glad-generated OpenGL loader uses CC0-1.0 and Apache 2.0; and stb_image/stb_image_write offer MIT or public-domain terms. The runtime packager includes applicable full notices for components present in the fetched SFML source tree in the same LICENSES.txt file. Dependencies provided by the operating system are not copied into the archive and are not represented as bundled runtime files.

Each runtime archive contains CX-Engine/runtime-manifest.json with file sizes and SHA-256 hashes. Any SFML upgrade or dependency-configuration change must be followed by a review of SFML's dependency list and applicable license notices before a release is tagged.

CX Engine itself is distributed under the BSD 3-Clause License in LICENSE.
