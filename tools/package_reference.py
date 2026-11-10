#!/usr/bin/env python3
"""Create a version-matched standalone CX Engine HTML reference for a release."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
from urllib.parse import quote


GUIDE_LINKS = {
    "index.html": None,
    "getting-started.html": "getting-started.md",
    "learn-cx-script.html": "learn-cx-script.md",
    "syntax.html": "syntax.md",
    "engine-settings.html": "engine-settings.md",
    "world-and-entities.html": "world-and-entities.md",
    "projects-and-rooms.html": "projects-and-rooms.md",
    "architecture.html": "architecture.md",
    "room-studio.html": "room-studio.md",
    "platform-support.html": "platform-support.md",
    "testing.html": "testing.md",
    "roadmap.html": "roadmap.md",
}


def build_reference(source: Path, output_dir: Path, version: str, tag: str, repository: str) -> Path:
    if not re.fullmatch(r"[0-9A-Za-z.+-]+", version):
        raise ValueError("Version contains unsupported filename characters.")
    if not re.fullmatch(r"v[0-9A-Za-z.+-]+", tag):
        raise ValueError("Tag must be a version tag beginning with v.")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+", repository):
        raise ValueError("Repository must use owner/name form.")
    if not source.is_file():
        raise FileNotFoundError(f"Reference source does not exist: {source}")

    html = source.read_text(encoding="utf-8")
    encoded_tag = quote(tag, safe=".+-")
    for page, guide in GUIDE_LINKS.items():
        if guide is None:
            url = f"https://github.com/{repository}/tree/{encoded_tag}/docs"
        else:
            url = f"https://github.com/{repository}/blob/{encoded_tag}/docs/{guide}"
        html = html.replace(f'href="{page}"', f'href="{url}"')

    unresolved = [page for page in GUIDE_LINKS if f'href="{page}"' in html]
    if unresolved:
        raise RuntimeError("Unconverted relative guide links: " + ", ".join(unresolved))
    if "Language & Runtime Reference" not in html:
        raise RuntimeError("Input is not the CX Engine HTML reference page.")
    if "<style>" not in html or "</style>" not in html or "const entries" not in html:
        raise RuntimeError("Reference is not self-contained or has no searchable catalogue.")

    if "reference-version-banner" not in html:
        marker = '<section class="hero">'
        banner = (
            '<div class="reference-version-banner" role="note">Version-matched reference · CX Engine '
            + tag + ' · Detailed guides are pinned to this exact release.</div>\n'
        )
        html = html.replace(marker, marker + "\n" + banner, 1)
        css = (
            ".reference-version-banner{display:inline-block;margin:8px 0 13px;padding:7px 11px;"
            "border:1px solid #475d78;border-radius:8px;background:#1b293a;color:#d4e2fa;"
            "font-size:12px;font-weight:700;letter-spacing:.02em}\n"
        )
        html = html.replace("</style>", css + "</style>", 1)

    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"CXEngine-v{version}-reference.html"
    output.write_text(html, encoding="utf-8", newline="\n")
    return output


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--tag", required=True)
    parser.add_argument("--repository", required=True)
    args = parser.parse_args()
    try:
        output = build_reference(args.source, args.output_dir, args.version, args.tag, args.repository)
    except (OSError, ValueError, RuntimeError) as error:
        parser.exit(1, f"ERROR: {error}\n")
    print(f"REFERENCE: {output}")
    print(f"SIZE_BYTES: {output.stat().st_size}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
