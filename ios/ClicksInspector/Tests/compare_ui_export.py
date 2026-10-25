#!/usr/bin/env python3
"""Compare saved Simulator exports with xcresult UI-capture attachments."""

import hashlib
import json
from pathlib import Path
import sys


def digest(data):
    return hashlib.sha256(data).hexdigest()


def common_prefix(left, right):
    return next((i for i, (a, b) in enumerate(zip(left, right)) if a != b), min(len(left), len(right)))


def main():
    source_directory, attachment_directory = map(Path, sys.argv[1:])
    sources = {path.name: path.read_bytes() for path in source_directory.glob("stock-*-*.json")}
    captures = {}
    # xcresult uses generated attachment filenames. Select the captured document
    # by its export fields, including malformed JSON from the original failure.
    for path in attachment_directory.rglob("*"):
        if path.is_file() and path.name != "manifest.json":
            data = path.read_bytes()
            if data.startswith(b"{") and b'"coverage"' in data:
                captures[str(path.relative_to(attachment_directory))] = data
    failed = len(sources) != 3 or len(captures) != 3
    print(f"Expected three source exports and three UI captures: sources={len(sources)} captures={len(captures)}")
    if not sources or not captures:
        raise SystemExit("Missing source/UI evidence")
    unmatched_sources = set(sources)
    for name, data in sources.items():
        print(f"SOURCE {name}: bytes={len(data)} sha256={digest(data)}")
        try:
            json.loads(data)
            print("Source JSON valid")
        except (ValueError, UnicodeDecodeError) as error:
            failed = True
            print(f"Source JSON error: {error}")
    for name, data in captures.items():
        source_name, source = max(sources.items(), key=lambda item: common_prefix(data, item[1]))
        offset = common_prefix(source, data)
        print(f"CAPTURE {name}: bytes={len(data)} sha256={digest(data)} source={source_name} exact={source == data}")
        try:
            json.loads(data)
        except (ValueError, UnicodeDecodeError) as error:
            failed = True
            print(f"UI JSON error: {error}")
        if source != data:
            failed = True
            print(f"First differing byte (zero-based): {offset}; source={source[offset:offset + 80]!r}; UI={data[offset:offset + 80]!r}")
            # This export's current test input is ASCII. Swift pages use Characters,
            # so never present byte-page alignment as evidence for Unicode exports.
            if source.isascii() and data.isascii():
                pages = [source[i:i + 16384] for i in range(0, len(source), 16384)]
                for i in range(0, len(data), 16384):
                    page = data[i:i + 16384]
                    matches = [index + 1 for index, expected in enumerate(pages) if page == expected]
                    print(f"UI page {i // 16384 + 1}: source_pages={matches}")
        elif source_name not in unmatched_sources:
            failed = True
            print(f"Duplicate UI capture for source {source_name}")
        else:
            unmatched_sources.remove(source_name)
    if unmatched_sources:
        failed = True
        print(f"Source exports without an exact UI capture: {sorted(unmatched_sources)}")
    return int(failed)


if __name__ == "__main__":
    sys.exit(main())
