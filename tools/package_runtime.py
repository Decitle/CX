#!/usr/bin/env python3
"""Package an installed CX Engine runtime with a minimal editable project."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tarfile
import tempfile
import zipfile


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(block)
    return h.hexdigest()


def copy_tree(source: Path, destination: Path) -> None:
    if not source.is_dir():
        raise RuntimeError(f"Required project directory is missing: {source}")
    shutil.copytree(source, destination)



# These are Windows operating-system components, not application redistributables.
# CMake's dependency scanner can report them as resolved imports even though every
# supported Windows installation already provides them. Keep this list conservative:
# unknown DLLs (including a future shared SFML build) are retained for the application.
WINDOWS_SYSTEM_DLLS = frozenset({
    "advapi32.dll",
    "aepic.dll",
    "azureattestmanager.dll",
    "azureattestnormal.dll",
    "audioses.dll",
    "avrt.dll",
    "bcp47langs.dll",
    "bcrypt.dll",
    "bcryptprimitives.dll",
    "cabinet.dll",
    "cfgmgr32.dll",
    "cldapi.dll",
    "combase.dll",
    "comppkgsup.dll",
    "crypt32.dll",
    "cryptbase.dll",
    "cryptsp.dll",
    "dbghelp.dll",
    "devobj.dll",
    "devrtl.dll",
    "dhcpcsvc.dll",
    "dhcpcsvc6.dll",
    "dnsapi.dll",
    "dpapi.dll",
    "dsparse.dll",
    "faultrep.dll",
    "fltlib.dll",
    "gdi32.dll",
    "glu32.dll",
    "httpapi.dll",
    "hvsifiletrust.dll",
    "icu.dll",
    "imagehlp.dll",
    "iphlpapi.dll",
    "kernel32.dll",
    "kernelbase.dll",
    "logoncli.dll",
    "mmdevapi.dll",
    "mpr.dll",
    "msasn1.dll",
    "msimg32.dll",
    "msvcp_win.dll",
    "msvcrt.dll",
    "ncrypt.dll",
    "netapi32.dll",
    "netutils.dll",
    "nrtapi.dll",
    "nsi.dll",
    "ntasn1.dll",
    "ntdll.dll",
    "oleaut32.dll",
    "opengl32.dll",
    "profapi.dll",
    "pdmutilities.dll",
    "propsys.dll",
    "rmclient.dll",
    "rpcrt4.dll",
    "samlib.dll",
    "sechost.dll",
    "shcore.dll",
    "shell32.dll",
    "shlwapi.dll",
    "dbgeng.dll",
    "d2d1.dll",
    "uiautomationcore.dll",
    "esent.dll",
    "coreuicomponents.dll",
    "certenroll.dll",
    "setupapi.dll",
    "windows.storage.dll",
    "srvcli.dll",
    "sspicli.dll",
    "tbs.dll",
    "tokenbinding.dll",
    "umpdc.dll",
    "user32.dll",
    "userenv.dll",
    "webio.dll",
    "wpaxholder.dll",
    "websocket.dll",
    "wer.dll",
    "win32u.dll",
    "winhttp.dll",
    "winmm.dll",
    "winmmbase.dll",
    "winnsi.dll",
    "winsta.dll",
    "wintrust.dll",
    "wkscli.dll",
    "wldap32.dll",
    "ws2_32.dll",
    "xmllite.dll",
})


def copy_runtime_stage(stage: Path, destination: Path, target: str) -> None:
    """Copy runtime payloads beside the executables in the CX-Engine folder.

    CMake installs executables and Windows DLLs under bin/, shared Unix
    libraries under lib/, and macOS frameworks under Frameworks/. The public
    archive intentionally presents a single launch folder, so files from bin/
    and lib/ are placed together. CMake's install RPATH is configured to match.
    """
    if not stage.is_dir():
        raise RuntimeError(f"CMake install stage is missing: {stage}")

    windows = target.startswith("windows-")
    macos = target.startswith("macos-")
    copied = 0
    stage_root = stage.resolve()
    for source in sorted(stage.rglob("*")):
        if not source.is_file():
            continue
        if not source.resolve().is_relative_to(stage_root):
            raise RuntimeError(f"Runtime stage contains a symlink escaping its root: {source}")
        relative = source.relative_to(stage)
        parts = relative.parts
        top = parts[0] if parts else ""
        suffix = source.suffix.lower()
        name = source.name.lower()

        keep = False
        output_relative: Path | None = None
        if top == "bin":
            if windows:
                keep = suffix == ".exe" or (
                    suffix == ".dll"
                    and name not in WINDOWS_SYSTEM_DLLS
                    and not name.startswith(("api-ms-", "ext-ms-"))
                )
            else:
                keep = suffix in {"", ".so", ".dylib"} or ".so." in name or ".dylib." in name
            if keep:
                output_relative = Path(source.name)
        elif top == "lib":
            if windows:
                keep = suffix == ".dll" and name not in WINDOWS_SYSTEM_DLLS and not name.startswith(("api-ms-", "ext-ms-"))
            elif macos:
                keep = ".dylib" in name
            else:
                keep = ".so" in name
            if keep:
                output_relative = Path(source.name)
        elif top == "Frameworks" and macos:
            keep = True
            output_relative = relative

        if not keep or output_relative is None:
            continue

        output = destination / output_relative
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            raise RuntimeError(
                f"Runtime stage contains colliding runtime filenames: {output_relative}"
            )
        shutil.copy2(source, output)
        copied += 1

    required = ("cx-engine.exe", "cx-cli.exe") if windows else ("cx-engine", "cx-cli")
    for relative in required:
        if not (destination / relative).is_file():
            raise RuntimeError(f"Runtime install stage did not contain required runtime file: {relative}")
    if copied < len(required):
        raise RuntimeError("Runtime install stage did not contain the expected executable payload")

GENERATED_PROJECT_STATE_NAMES = frozenset({
    ".variables.lock",
    "variables.csv",
    "variables.csv.bak",
    "variables.cxv",
    "variables.cxv.bak",
    "variables.meta",
})


def validate_starter_project(project: Path) -> None:
    """Reject generated persistence state from starter-project templates."""
    if not project.is_dir():
        raise RuntimeError(f"Starter project directory is missing: {project}")
    for path in project.rglob("*"):
        if not path.is_file():
            continue
        name = path.name.lower()
        if name in GENERATED_PROJECT_STATE_NAMES or name.endswith(".tmp"):
            raise RuntimeError(
                f"Starter project contains generated persistent state: {path.relative_to(project)}"
            )


def collect_files(root: Path) -> list[Path]:
    files=[]
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise RuntimeError(f"Runtime package may not contain symlinks: {path.relative_to(root)}")
        if path.is_file():
            rel=PurePosixPath(path.relative_to(root).as_posix())
            if any(part in {"build", "CMakeFiles", "__pycache__", ".git"} for part in rel.parts):
                raise RuntimeError(f"Unexpected generated directory in runtime: {rel}")
            files.append(path)
    return files


def find_sfml_license(build_dir: Path, explicit: Path | None = None) -> Path:
    """Find SFML's top-level license without scanning outside its SDK.

    FetchContent places the SFML checkout under ``_deps/sfml-src``. Installed
    SDKs expose ``SFML_DIR`` (normally ``<sdk>/lib/cmake/SFML``) or a prefix
    through ``CMAKE_PREFIX_PATH``. Only a small set of known SDK roots is
    inspected; walking every ancestor recursively could descend into /proc.
    """
    if explicit is not None:
        candidate = explicit.expanduser().resolve()
        if not candidate.is_file():
            raise RuntimeError(f"The supplied SFML license file does not exist: {candidate}")
        return candidate

    roots: list[Path] = [build_dir / "_deps" / "sfml-src"]
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("SFML_DIR:") and "=" in line:
                sfml_dir = Path(line.split("=", 1)[1].strip()).expanduser()
                # SFML_DIR is typically <sdk>/lib/cmake/SFML. Find the first
                # ancestor that actually looks like an SFML SDK root.
                for ancestor in (sfml_dir, *sfml_dir.parents):
                    if (ancestor / "include" / "SFML").is_dir():
                        roots.append(ancestor)
                        break
            elif line.startswith("CMAKE_PREFIX_PATH:") and "=" in line:
                for entry in line.split("=", 1)[1].split(os.pathsep):
                    if entry:
                        prefix = Path(entry).expanduser()
                        if (prefix / "include" / "SFML").is_dir():
                            roots.append(prefix)

    license_names = ("license.md", "license", "license.txt", "license-mit", "copying")
    checked: set[Path] = set()
    for root in roots:
        root = root.expanduser().resolve()
        if root in checked or not root.is_dir():
            continue
        checked.add(root)
        for name in license_names:
            candidate = root / name
            if candidate.is_file():
                return candidate

    raise RuntimeError(
        "Could not locate SFML's top-level license. Use a FetchContent SFML build, "
        "set SFML_DIR to an installed SFML SDK, or pass --sfml-license."
    )


GLAD_CC0_LICENSE = "Creative Commons Legal Code\n\nCC0 1.0 Universal\n\n    CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE\n    LEGAL SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN\n    ATTORNEY-CLIENT RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS\n    INFORMATION ON AN \"AS-IS\" BASIS. CREATIVE COMMONS MAKES NO WARRANTIES\n    REGARDING THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS\n    PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES RESULTING FROM\n    THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED\n    HEREUNDER.\n\nStatement of Purpose\n\nThe laws of most jurisdictions throughout the world automatically confer\nexclusive Copyright and Related Rights (defined below) upon the creator\nand subsequent owner(s) (each and all, an \"owner\") of an original work of\nauthorship and/or a database (each, a \"Work\").\n\nCertain owners wish to permanently relinquish those rights to a Work for\nthe purpose of contributing to a commons of creative, cultural and\nscientific works (\"Commons\") that the public can reliably and without fear\nof later claims of infringement build upon, modify, incorporate in other\nworks, reuse and redistribute as freely as possible in any form whatsoever\nand for any purposes, including without limitation commercial purposes.\nThese owners may contribute to the Commons to promote the ideal of a free\nculture and the further production of creative, cultural and scientific\nworks, or to gain reputation or greater distribution for their Work in\npart through the use and efforts of others.\n\nFor these and/or other purposes and motivations, and without any\nexpectation of additional consideration or compensation, the person\nassociating CC0 with a Work (the \"Affirmer\"), to the extent that he or she\nis an owner of Copyright and Related Rights in the Work, voluntarily\nelects to apply CC0 to the Work and publicly distribute the Work under its\nterms, with knowledge of his or her Copyright and Related Rights in the\nWork and the meaning and intended legal effect of CC0 on those rights.\n\n1. Copyright and Related Rights. A Work made available under CC0 may be\nprotected by copyright and related or neighboring rights (\"Copyright and\nRelated Rights\"). Copyright and Related Rights include, but are not\nlimited to, the following:\n\n  i. the right to reproduce, adapt, distribute, perform, display,\n     communicate, and translate a Work;\n ii. moral rights retained by the original author(s) and/or performer(s);\niii. publicity and privacy rights pertaining to a person's image or\n     likeness depicted in a Work;\n iv. rights protecting against unfair competition in regards to a Work,\n     subject to the limitations in paragraph 4(a), below;\n  v. rights protecting the extraction, dissemination, use and reuse of data\n     in a Work;\n vi. database rights (such as those arising under Directive 96/9/EC of the\n     European Parliament and of the Council of 11 March 1996 on the legal\n     protection of databases, and under any national implementation\n     thereof, including any amended or successor version of such\n     directive); and\nvii. other similar, equivalent or corresponding rights throughout the\n     world based on applicable law or treaty, and any national\n     implementations thereof.\n\n2. Waiver. To the greatest extent permitted by, but not in contravention\nof, applicable law, Affirmer hereby overtly, fully, permanently,\nirrevocably and unconditionally waives, abandons, and surrenders all of\nAffirmer's Copyright and Related Rights and associated claims and causes\nof action, whether now known or unknown (including existing as well as\nfuture claims and causes of action), in the Work (i) in all territories\nworldwide, (ii) for the maximum duration provided by applicable law or\ntreaty (including future time extensions), (iii) in any current or future\nmedium and for any number of copies, and (iv) for any purpose whatsoever,\nincluding without limitation commercial, advertising or promotional\npurposes (the \"Waiver\"). Affirmer makes the Waiver for the benefit of each\nmember of the public at large and to the detriment of Affirmer's heirs and\nsuccessors, fully intending that such Waiver shall not be subject to\nrevocation, rescission, cancellation, termination, or any other legal or\nequitable action to disrupt the quiet enjoyment of the Work by the public\nas contemplated by Affirmer's express Statement of Purpose.\n\n3. Public License Fallback. Should any part of the Waiver for any reason\nbe judged legally invalid or ineffective under applicable law, then the\nWaiver shall be preserved to the maximum extent permitted taking into\naccount Affirmer's express Statement of Purpose. In addition, to the\nextent the Waiver is so judged Affirmer hereby grants to each affected\nperson a royalty-free, non transferable, non sublicensable, non exclusive,\nirrevocable and unconditional license to exercise Affirmer's Copyright and\nRelated Rights in the Work (i) in all territories worldwide, (ii) for the\nmaximum duration provided by applicable law or treaty (including future\ntime extensions), (iii) in any current or future medium and for any number\nof copies, and (iv) for any purpose whatsoever, including without\nlimitation commercial, advertising or promotional purposes (the\n\"License\"). The License shall be deemed effective as of the date CC0 was\napplied by Affirmer to the Work. Should any part of the License for any\nreason be judged legally invalid or ineffective under applicable law, such\npartial invalidity or ineffectiveness shall not invalidate the remainder\nof the License, and in such case Affirmer hereby affirms that he or she\nwill not (i) exercise any of his or her remaining Copyright and Related\nRights in the Work or (ii) assert any associated claims and causes of\naction with respect to the Work, in either case contrary to Affirmer's\nexpress Statement of Purpose.\n\n4. Limitations and Disclaimers.\n\n a. No trademark or patent rights held by Affirmer are waived, abandoned,\n    surrendered, licensed or otherwise affected by this document.\n b. Affirmer offers the Work as-is and makes no representations or\n    warranties of any kind concerning the Work, express, implied,\n    statutory or otherwise, including without limitation warranties of\n    title, merchantability, fitness for a particular purpose, non\n    infringement, or the absence of latent or other defects, accuracy, or\n    the present or absence of errors, whether or not discoverable, all to\n    the greatest extent permissible under applicable law.\n c. Affirmer disclaims responsibility for clearing rights of other persons\n    that may apply to the Work or any use thereof, including without\n    limitation any person's Copyright and Related Rights in the Work.\n    Further, Affirmer disclaims responsibility for obtaining any necessary\n    consents, permissions or other rights required for any use of the\n    Work.\n d. Affirmer understands and acknowledges that Creative Commons is not a\n    party to this document and has no duty or obligation with respect to\n    this CC0 or use of the Work.\n"
GLAD_APACHE_LICENSE = "Apache License\nVersion 2.0, January 2004\nhttp://www.apache.org/licenses/\n\nTERMS AND CONDITIONS FOR USE, REPRODUCTION, AND DISTRIBUTION\n\n1. Definitions.\n\n\"License\" shall mean the terms and conditions for use, reproduction, and distribution as defined by Sections 1 through 9 of this document.\n\n\"Licensor\" shall mean the copyright owner or entity authorized by the copyright owner that is granting the License.\n\n\"Legal Entity\" shall mean the union of the acting entity and all other entities that control, are controlled by, or are under common control with that entity. For the purposes of this definition, \"control\" means (i) the power, direct or indirect, to cause the direction or management of such entity, whether by contract or otherwise, or (ii) ownership of fifty percent (50%) or more of the outstanding shares, or (iii) beneficial ownership of such entity.\n\n\"You\" (or \"Your\") shall mean an individual or Legal Entity exercising permissions granted by this License.\n\n\"Source\" form shall mean the preferred form for making modifications, including but not limited to software source code, documentation source, and configuration files.\n\n\"Object\" form shall mean any form resulting from mechanical transformation or translation of a Source form, including but not limited to compiled object code, generated documentation, and conversions to other media types.\n\n\"Work\" shall mean the work of authorship, whether in Source or Object form, made available under the License, as indicated by a copyright notice that is included in or attached to the work (an example is provided in the Appendix below).\n\n\"Derivative Works\" shall mean any work, whether in Source or Object form, that is based on (or derived from) the Work and for which the editorial revisions, annotations, elaborations, or other modifications represent, as a whole, an original work of authorship. For the purposes of this License, Derivative Works shall not include works that remain separable from, or merely link (or bind by name) to the interfaces of, the Work and Derivative Works thereof.\n\n\"Contribution\" shall mean any work of authorship, including the original version of the Work and any modifications or additions to that Work or Derivative Works thereof, that is intentionally submitted to Licensor for inclusion in the Work by the copyright owner or by an individual or Legal Entity authorized to submit on behalf of the copyright owner. For the purposes of this definition, \"submitted\" means any form of electronic, verbal, or written communication sent to the Licensor or its representatives, including but not limited to communication on electronic mailing lists, source code control systems, and issue tracking systems that are managed by, or on behalf of, the Licensor for the purpose of discussing and improving the Work, but excluding communication that is conspicuously marked or otherwise designated in writing by the copyright owner as \"Not a Contribution.\"\n\n\"Contributor\" shall mean Licensor and any individual or Legal Entity on behalf of whom a Contribution has been received by Licensor and subsequently incorporated within the Work.\n\n2. Grant of Copyright License. Subject to the terms and conditions of this License, each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable copyright license to reproduce, prepare Derivative Works of, publicly display, publicly perform, sublicense, and distribute the Work and such Derivative Works in Source or Object form.\n\n3. Grant of Patent License. Subject to the terms and conditions of this License, each Contributor hereby grants to You a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable (except as stated in this section) patent license to make, have made, use, offer to sell, sell, import, and otherwise transfer the Work, where such license applies only to those patent claims licensable by such Contributor that are necessarily infringed by their Contribution(s) alone or by combination of their Contribution(s) with the Work to which such Contribution(s) was submitted. If You institute patent litigation against any entity (including a cross-claim or counterclaim in a lawsuit) alleging that the Work or a Contribution incorporated within the Work constitutes direct or contributory patent infringement, then any patent licenses granted to You under this License for that Work shall terminate as of the date such litigation is filed.\n\n4. Redistribution. You may reproduce and distribute copies of the Work or Derivative Works thereof in any medium, with or without modifications, and in Source or Object form, provided that You meet the following conditions:\n\n     (a) You must give any other recipients of the Work or Derivative Works a copy of this License; and\n\n     (b) You must cause any modified files to carry prominent notices stating that You changed the files; and\n\n     (c) You must retain, in the Source form of any Derivative Works that You distribute, all copyright, patent, trademark, and attribution notices from the Source form of the Work, excluding those notices that do not pertain to any part of the Derivative Works; and\n\n     (d) If the Work includes a \"NOTICE\" text file as part of its distribution, then any Derivative Works that You distribute must include a readable copy of the attribution notices contained within such NOTICE file, excluding those notices that do not pertain to any part of the Derivative Works, in at least one of the following places: within a NOTICE text file distributed as part of the Derivative Works; within the Source form or documentation, if provided along with the Derivative Works; or, within a display generated by the Derivative Works, if and wherever such third-party notices normally appear. The contents of the NOTICE file are for informational purposes only and do not modify the License. You may add Your own attribution notices within Derivative Works that You distribute, alongside or as an addendum to the NOTICE text from the Work, provided that such additional attribution notices cannot be construed as modifying the License.\n\n     You may add Your own copyright statement to Your modifications and may provide additional or different license terms and conditions for use, reproduction, or distribution of Your modifications, or for any such Derivative Works as a whole, provided Your use, reproduction, and distribution of the Work otherwise complies with the conditions stated in this License.\n\n5. Submission of Contributions. Unless You explicitly state otherwise, any Contribution intentionally submitted for inclusion in the Work by You to the Licensor shall be under the terms and conditions of this License, without any additional terms or conditions. Notwithstanding the above, nothing herein shall supersede or modify the terms of any separate license agreement you may have executed with Licensor regarding such Contributions.\n\n6. Trademarks. This License does not grant permission to use the trade names, trademarks, service marks, or product names of the Licensor, except as required for reasonable and customary use in describing the origin of the Work and reproducing the content of the NOTICE file.\n\n7. Disclaimer of Warranty. Unless required by applicable law or agreed to in writing, Licensor provides the Work (and each Contributor provides its Contributions) on an \"AS IS\" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied, including, without limitation, any warranties or conditions of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A PARTICULAR PURPOSE. You are solely responsible for determining the appropriateness of using or redistributing the Work and assume any risks associated with Your exercise of permissions under this License.\n\n8. Limitation of Liability. In no event and under no legal theory, whether in tort (including negligence), contract, or otherwise, unless required by applicable law (such as deliberate and grossly negligent acts) or agreed to in writing, shall any Contributor be liable to You for damages, including any direct, indirect, special, incidental, or consequential damages of any character arising as a result of this License or out of the use or inability to use the Work (including but not limited to damages for loss of goodwill, work stoppage, computer failure or malfunction, or any and all other commercial damages or losses), even if such Contributor has been advised of the possibility of such damages.\n\n9. Accepting Warranty or Additional Liability. While redistributing the Work or Derivative Works thereof, You may choose to offer, and charge a fee for, acceptance of support, warranty, indemnity, or other liability obligations and/or rights consistent with this License. However, in accepting such obligations, You may act only on Your own behalf and on Your sole responsibility, not on behalf of any other Contributor, and only if You agree to indemnify, defend, and hold each Contributor harmless for any liability incurred by, or claims asserted against, such Contributor by reason of your accepting any such warranty or additional liability.\n\nEND OF TERMS AND CONDITIONS\n\nAPPENDIX: How to apply the Apache License to your work.\n\nTo apply the Apache License to your work, attach the following boilerplate notice, with the fields enclosed by brackets \"[]\" replaced with your own identifying information. (Don't include the brackets!)  The text should be enclosed in the appropriate comment syntax for the file format. We also recommend that a file or class name and description of purpose be included on the same \"printed page\" as the copyright notice for easier identification within third-party archives.\n\nCopyright [yyyy] [name of copyright owner]\n\nLicensed under the Apache License, Version 2.0 (the \"License\");\nyou may not use this file except in compliance with the License.\nYou may obtain a copy of the License at\n\nhttp://www.apache.org/licenses/LICENSE-2.0\n\nUnless required by applicable law or agreed to in writing, software\ndistributed under the License is distributed on an \"AS IS\" BASIS,\nWITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.\nSee the License for the specific language governing permissions and\nlimitations under the License.\n"


def write_combined_licenses(
    source_root: Path,
    sfml_license: Path,
    build_dir: Path,
    destination: Path,
) -> None:
    """Create one runtime license file with notices for bundled dependencies."""
    cx_license = source_root / "LICENSE"
    if not cx_license.is_file():
        raise RuntimeError(f"CX Engine license file is missing: {cx_license}")
    if not sfml_license.is_file():
        raise RuntimeError(f"SFML license file is missing: {sfml_license}")

    sections: list[tuple[str, str]] = [
        ("CX ENGINE — BSD 3-CLAUSE LICENSE", cx_license.read_text(encoding="utf-8").strip()),
        ("SFML 3.1.0 — ZLIB/LIBPNG LICENSE", sfml_license.read_text(encoding="utf-8").strip()),
    ]

    def add_file(heading: str, path: Path) -> None:
        if not path.is_file():
            raise RuntimeError(f"Bundled dependency license is missing: {path}")
        sections.append((heading, path.read_text(encoding="utf-8").strip()))

    dependency_licenses = (
        ("FreeType — FREETYPE PROJECT LICENSE (FTL)",
         build_dir / "_deps" / "freetype-src" / "docs" / "FTL.TXT"),
        ("HarfBuzz — OLD MIT LICENSE",
         build_dir / "_deps" / "harfbuzz-src" / "COPYING"),
        ("HarfBuzz Microsoft USE component — MIT LICENSE",
         build_dir / "_deps" / "harfbuzz-src" / "src" / "ms-use" / "COPYING"),
        ("SheenBidi — APACHE LICENSE 2.0",
         build_dir / "_deps" / "sheenbidi-src" / "LICENSE"),
    )
    for heading, license_path in dependency_licenses:
        if license_path.is_file():
            add_file(heading, license_path)

    freetype_source = build_dir / "_deps" / "freetype-src"
    if freetype_source.is_dir():
        # The BDF and PCF drivers have their own notices, documented in these
        # source README files; retain those notices with the main FTL text.
        add_file("FreeType BDF driver — LICENSE NOTICE",
                 freetype_source / "src" / "bdf" / "README")
        add_file("FreeType PCF driver — LICENSE NOTICE",
                 freetype_source / "src" / "pcf" / "README")

        # FreeType's BDF hash helper is separately licensed by Computing
        # Research Labs / New Mexico State University. The .c and .h notices
        # should match; keep one full copy in the combined license file.
        hash_paths = (
            freetype_source / "src" / "base" / "fthash.c",
            freetype_source / "include" / "freetype" / "internal" / "fthash.h",
        )
        hash_notices: list[str] = []
        for hash_path in hash_paths:
            if not hash_path.is_file():
                raise RuntimeError(f"Bundled FreeType hash source is missing: {hash_path}")
            hash_source = hash_path.read_text(encoding="utf-8", errors="replace")
            marker = "/*" + chr(10) + " * Copyright 2000"
            begin = hash_source.find(marker)
            finish = hash_source.find("*/", begin)
            if begin < 0 or finish < 0 or "Copyright 2000" not in hash_source[begin:finish]:
                raise RuntimeError(f"Could not extract the FreeType hash notice from {hash_path}")
            hash_notices.append(hash_source[begin:finish + 2].strip())
        if hash_notices[0] != hash_notices[1]:
            raise RuntimeError("FreeType hash helper source/header license notices differ")
        sections.append(("FreeType BDF hash helper — MIT LICENSE", hash_notices[0]))

    sfml_source = build_dir / "_deps" / "sfml-src"
    if sfml_source.is_dir():
        extlibs = sfml_source / "extlibs" / "headers"

        qoi_header = extlibs / "qoi" / "qoi.h"
        if not qoi_header.is_file():
            raise RuntimeError("Fetched SFML source is missing its vendored QOI header")
        qoi_source = qoi_header.read_text(encoding="utf-8", errors="replace")
        if "SPDX-License-Identifier: MIT" not in qoi_source or "Dominic Szablewski" not in qoi_source:
            raise RuntimeError("SFML's QOI header no longer matches its expected MIT license")
        sections.append(("QOI — MIT LICENSE", """MIT License

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org

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
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE."""))

        unicode_header = extlibs / "cpp-unicodelib" / "unicodelib.h"
        if not unicode_header.is_file():
            raise RuntimeError("Fetched SFML source is missing cpp-unicodelib")
        unicode_source = unicode_header.read_text(encoding="utf-8", errors="replace")
        if "Copyright (c) 2025 Yuji Hirose" not in unicode_source or "MIT License" not in unicode_source:
            raise RuntimeError("SFML's cpp-unicodelib header no longer matches its expected MIT license")
        sections.append(("cpp-unicodelib — MIT LICENSE", """MIT License

Copyright (c) 2025 Yuji Hirose

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
SOFTWARE."""))

        glad_header = extlibs / "glad" / "include" / "glad" / "gl.h"
        if not glad_header.is_file():
            raise RuntimeError("Fetched SFML source is missing its glad OpenGL loader")
        glad_source = glad_header.read_text(encoding="utf-8", errors="replace")
        if "(WTFPL OR CC0-1.0) AND Apache-2.0" not in glad_source:
            raise RuntimeError("SFML glad SPDX identifier changed; review the license bundle")
        sections.append(("glad generated loader — CC0-1.0", GLAD_CC0_LICENSE))
        sections.append(("glad generated loader — APACHE LICENSE 2.0", GLAD_APACHE_LICENSE))

        stb_paths = (
            extlibs / "stb_image" / "stb_image.h",
            extlibs / "stb_image" / "stb_image_write.h",
        )
        stb_blocks: list[str] = []
        for stb_path in stb_paths:
            if not stb_path.is_file():
                raise RuntimeError(f"Fetched SFML source is missing a stb image header: {stb_path}")
            stb_source = stb_path.read_text(encoding="utf-8", errors="replace")
            start = stb_source.find("ALTERNATIVE A - MIT License")
            end = stb_source.find("ALTERNATIVE B - Public Domain", start)
            if start < 0 or end < 0:
                raise RuntimeError(f"Could not extract the MIT license from {stb_path}")
            stb_blocks.append(stb_source[start:end].strip())
        if stb_blocks[0] == stb_blocks[1]:
            sections.append(("stb_image and stb_image_write — MIT LICENSE", stb_blocks[0]))
        else:
            sections.append(("stb_image — MIT LICENSE", stb_blocks[0]))
            sections.append(("stb_image_write — MIT LICENSE", stb_blocks[1]))

    text_parts: list[str] = []
    for heading, body in sections:
        text_parts.extend((heading, "=" * len(heading), "", body.rstrip(), ""))
    destination.write_text("\n".join(text_parts).rstrip() + "\n", encoding="utf-8")


def make_manifest(root: Path, target: str, version: str) -> None:
    manifest_path = root / "CX-Engine" / "runtime-manifest.json"
    files = [path for path in collect_files(root) if path != manifest_path]
    manifest = {
        "product": "CX Engine",
        "version": version,
        "target": target,
        "package_type": "minimal-starter-runtime",
        "files": [
            {
                "path": path.relative_to(root).as_posix(),
                "size_bytes": path.stat().st_size,
                "sha256": sha256(path),
            }
            for path in files
        ],
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def verify_manifest(root: Path) -> int:
    path = root / "CX-Engine" / "runtime-manifest.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    listed = {entry["path"]: entry for entry in data["files"]}
    actual = {
        file.relative_to(root).as_posix(): file
        for file in collect_files(root)
        if file != path
    }
    if set(actual) != set(listed):
        raise RuntimeError(
            f"Runtime manifest path mismatch: missing={set(listed)-set(actual)}, "
            f"extra={set(actual)-set(listed)}"
        )
    for name, file in actual.items():
        item = listed[name]
        if file.stat().st_size != item["size_bytes"] or sha256(file) != item["sha256"]:
            raise RuntimeError(f"Runtime manifest hash/size mismatch: {name}")
    return len(actual)

def archive(root: Path, output: Path, fmt: str) -> None:
    output.parent.mkdir(parents=True,exist_ok=True)
    if fmt=="zip":
        with zipfile.ZipFile(output,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=9) as zf:
            for path in collect_files(root):
                rel=path.relative_to(root).as_posix()
                info=zipfile.ZipInfo(rel,date_time=(2026,11,8,0,0,0))
                info.compress_type=zipfile.ZIP_DEFLATED
                info.create_system=3
                info.external_attr=(stat.S_IFREG|stat.S_IMODE(path.stat().st_mode))<<16
                zf.writestr(info,path.read_bytes(),compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
        with zipfile.ZipFile(output) as zf:
            bad=zf.testzip()
            if bad: raise RuntimeError(f"Runtime ZIP integrity failure: {bad}")
    else:
        with tarfile.open(output,"w:gz",compresslevel=9) as tf:
            for path in collect_files(root):
                tf.add(path,arcname=path.relative_to(root).as_posix(),recursive=False)
        with tarfile.open(output,"r:gz") as tf:
            if any(not m.isfile() for m in tf.getmembers()):
                raise RuntimeError("Runtime TAR.GZ contains unexpected non-file entries")
    digest=sha256(output)
    output.with_suffix(output.suffix+".sha256").write_text(f"{digest}  {output.name}\n",encoding="utf-8")
    print(f"ARCHIVE: {output}\nSHA256: {digest}\nFILES: {verify_manifest(root)} + manifest")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True, help="CMake install prefix containing bin/ and optional lib/")
    parser.add_argument("--project", type=Path, required=True, help="Minimal starter project folder")
    parser.add_argument("--build-dir", type=Path, required=True, help="CMake build directory used to configure SFML")
    parser.add_argument("--sfml-license", type=Path, help="Explicit SFML license file when using a custom installed SDK")
    parser.add_argument("--source-root", type=Path, default=Path.cwd())
    parser.add_argument("--target", required=True, help="OS/architecture label, e.g. linux-x86_64")
    parser.add_argument("--version", default=os.environ.get("CX_VERSION", "0.10.14"))
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        windows = args.target.startswith("windows-")
        exe = "cx-engine.exe" if windows else "cx-engine"
        cli = "cx-cli.exe" if windows else "cx-cli"
        if not (args.stage / "bin" / exe).is_file():
            raise RuntimeError(f"Graphical runtime executable is missing from install stage: bin/{exe}")
        if not (args.stage / "bin" / cli).is_file():
            raise RuntimeError(f"CLI executable is missing from install stage: bin/{cli}")
        if not (args.project / "config.json").is_file() or not (args.project / "scripts" / "main.cx").is_file():
            raise RuntimeError("Starter project must contain config.json and scripts/main.cx")

        sfml_license = find_sfml_license(args.build_dir, args.sfml_license)
        validate_starter_project(args.project)
        with tempfile.TemporaryDirectory(prefix="cx-runtime-package-") as temporary:
            root = Path(temporary)
            runtime_dir = root / "CX-Engine"

            # CX-Engine is a directly runnable project folder: it holds the
            # executables, shared runtime libraries, and a copy of the starter.
            copy_tree(args.project, runtime_dir)
            copy_runtime_stage(args.stage, runtime_dir, args.target)

            # The sole starter project is directly inside CX-Engine/.
            # Never add a duplicate top-level project/ directory.
            shutil.copy2(args.source_root / "docs" / "runtime-package.md", root / "README.md")
            write_combined_licenses(
                args.source_root, sfml_license, args.build_dir, root / "LICENSES.txt"
            )

            make_manifest(root, args.target, args.version)
            verify_manifest(root)
            suffix = ".zip" if args.target.startswith(("windows-", "macos-")) else ".tar.gz"
            output = args.output_dir / f"CXEngine-v{args.version}-{args.target}{suffix}"
            archive(root, output, "zip" if suffix == ".zip" else "tar.gz")
    except (OSError, RuntimeError, ValueError, json.JSONDecodeError, tarfile.TarError, zipfile.BadZipFile) as exc:
        parser.exit(1, f"ERROR: {exc}\n")
    return 0

if __name__=="__main__":
    raise SystemExit(main())
