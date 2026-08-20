#!/usr/bin/env python3
"""Turn the built configuration page into a C++ header the device serves from RAM.

The page is a single self-contained index.html produced by `npm run build` in
web/ (ADR-0014). The generated header is checked in, so neither CI nor the
aarch64 cross build needs Node — the same arrangement bdf_to_header.py uses for
the font.

The payload goes in as a raw string literal rather than a byte array: it keeps
the generated file the size of the asset instead of six times that, and it keeps
it readable, which matters for a file that is reviewed by diff. The cost is one
compile flag, -Wno-overlength-strings, on the one target that includes it.

Usage, from web/:
    npm run embed          # vite build, then this script

or by hand, from the repository root:
    tools/bundle_to_header.py web/dist/index.html src/net/web_assets.h
"""

import sys
from pathlib import Path

# Anything that cannot occur in HTML, CSS or JavaScript output.
DELIMITER = "MTXOS"

HEADER = """// Copyright (C) 2026 RobBotzz
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License, version 2, as published by the
// Free Software Foundation. See LICENSE for details.
//
// GENERATED FILE - do not edit.
//
// Produced by tools/bundle_to_header.py from web/dist/index.html. To change the
// page, edit web/src/ and run `npm run embed` in web/ (ADR-0014).

#pragma once

#include <string_view>

namespace matrixos::assets
{
"""

FOOTER = """
} // namespace matrixos::assets
"""


def main():
    source = Path(sys.argv[1] if len(sys.argv) > 1 else "dist/index.html")
    target = Path(sys.argv[2] if len(sys.argv) > 2 else "../src/net/web_assets.h")

    if not source.is_file():
        sys.exit(f"{source} does not exist — run `npm run build` in web/ first")

    text = source.read_text(encoding="utf-8")

    # A raw string literal ends at its delimiter and nothing escapes inside it,
    # so both of these have to be checked rather than assumed.
    if f'){DELIMITER}"' in text:
        sys.exit(f'the bundle contains the delimiter ){DELIMITER}" — pick another one')
    if "\0" in text:
        sys.exit("the bundle contains a NUL byte; it is no longer plain text")

    with target.open("w", encoding="utf-8") as out:
        out.write(HEADER)
        out.write(f"\n// {len(text.encode('utf-8'))} bytes, built from web/src/\n")
        out.write(f'inline constexpr std::string_view kConfigPage = R"{DELIMITER}(')
        out.write(text)
        out.write(f'){DELIMITER}";\n')
        out.write(FOOTER)

    print(f"{source} ({len(text)} bytes) -> {target}", file=sys.stderr)


if __name__ == "__main__":
    main()
