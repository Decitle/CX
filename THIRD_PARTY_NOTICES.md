# Third-party notices

CX Engine uses SFML 3.1.0 for its graphical window, input, and rendering integration. SFML is distributed under the zlib/png license. The license text is included in this repository at [`licenses/SFML-LICENSE.md`](licenses/SFML-LICENSE.md) and in runtime packages as `third-party-licenses/SFML-LICENSE.txt`.

The exact dependencies present in a particular runtime package depend on the native build. Each package's `runtime-manifest.json` records its files and SHA-256 hashes. System libraries supplied by the operating system are not copied into the package. Where an additional dependency is bundled, its license notice must be included beside the runtime package.

CX Engine itself is distributed under the BSD 3-Clause License in `LICENSE`.
