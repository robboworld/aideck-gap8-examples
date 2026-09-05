#!/usr/bin/env python3
"""Embed DORY hex blobs as C arrays for radio OTA (no ReadFS)."""
from pathlib import Path

root = Path(__file__).resolve().parent
hex_dir = root / "generated" / "hex"
names = [
    "BNReluConvolution0_weights",
    "BNReluConvolution2_weights",
    "BNReluConvolution4_weights",
    "FullyConnected6_weights",
    "inputs",
]
blobs = []
for name in names:
    path = hex_dir / (name + ".hex")
    if not path.is_file():
        raise SystemExit("missing {}".format(path))
    blobs.append((name, path.read_bytes()))

header_lines = [
    "#ifndef ARROW2_EMBEDDED_WEIGHTS_H",
    "#define ARROW2_EMBEDDED_WEIGHTS_H",
    "#include <stdint.h>",
    "#include <stddef.h>",
]
for name, data in blobs:
    header_lines.append(
        "extern const uint8_t {}[{}];".format(name, len(data))
    )
    header_lines.append(
        "extern const size_t {}_len;".format(name)
    )
header_lines.extend(["#endif", ""])
(root / "generated" / "inc").mkdir(parents=True, exist_ok=True)
(root / "generated" / "src").mkdir(parents=True, exist_ok=True)
(root / "generated" / "inc" / "embedded_weights.h").write_text(
    "\n".join(header_lines), encoding="utf-8"
)

parts = ['#include "embedded_weights.h"\n']
for name, data in blobs:
    rows = [
        ", ".join("0x{:02x}".format(b) for b in data[i : i + 12])
        for i in range(0, len(data), 12)
    ]
    parts.append(
        "const uint8_t {}[{}] = {{\n  ".format(name, len(data))
        + ",\n  ".join(rows)
        + ",\n};\n"
        + "const size_t {0}_len = sizeof({0});\n".format(name)
    )
(root / "generated" / "src" / "embedded_weights.c").write_text(
    "\n".join(parts), encoding="utf-8"
)
print("embedded", {name: len(data) for name, data in blobs})
