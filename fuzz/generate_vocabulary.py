#!/usr/bin/env python3
"""Turn a strict libFuzzer/Centipede hex dictionary into FuzzTest byte tokens."""

import pathlib
import re
import sys


def generate(source: pathlib.Path, destination: pathlib.Path) -> None:
    tokens = []
    for number, line in enumerate(source.read_text().splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        match = re.fullmatch(r'[A-Za-z0-9_]+="((?:\\x[0-9a-fA-F]{2})+)"', line)
        if not match:
            raise ValueError(f"{source}:{number}: expected a named hex byte token")
        token = bytes.fromhex(match[1].replace("\\x", ""))
        if token in tokens:
            raise ValueError(f"{source}:{number}: duplicate byte token")
        tokens.append(token)
    if not tokens:
        raise ValueError(f"{source}: empty vocabulary")
    rows = ["{" + ",".join(str(byte) for byte in token) + "}" for token in tokens]
    destination.write_text(
        "// Generated from cbor.dict. Do not edit.\n#pragma once\n"
        "#include <cstdint>\n#include <vector>\n"
        "namespace cbor_fuzz {\n"
        "inline std::vector<std::vector<std::uint8_t>> wire_vocabulary() {\n"
        "return {" + ",\n".join(rows) + "};\n}\n}\n"
    )


if __name__ == "__main__":
    generate(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
