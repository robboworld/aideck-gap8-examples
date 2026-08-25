#!/usr/bin/env python3
"""Turn DORY hex blobs into a single flash-resident C array for stop1."""
from pathlib import Path

root = Path(__file__).resolve().parent
hex_dir = root / "generated" / "hex"
files = sorted(hex_dir.glob("BNReluConvolution*_weights.hex")) + sorted(
    hex_dir.glob("Convolution*_weights.hex")
)
if not files:
    raise SystemExit("missing hex blobs under generated/hex — package DORY first")
payload = b"".join(path.read_bytes() for path in files)
header = """#ifndef STOP1_EMBEDDED_WEIGHTS_H
#define STOP1_EMBEDDED_WEIGHTS_H
#include <stdint.h>
#include <stddef.h>
extern const uint8_t stop1_dory_weights[];
extern const size_t stop1_dory_weights_len;
#endif
"""
(root / "generated" / "inc").mkdir(parents=True, exist_ok=True)
(root / "generated" / "src").mkdir(parents=True, exist_ok=True)
(root / "generated" / "inc" / "embedded_weights.h").write_text(header)
rows = [
    ", ".join("0x{:02x}".format(value) for value in payload[offset : offset + 12])
    for offset in range(0, len(payload), 12)
]
source = (
    '#include "embedded_weights.h"\n\n'
    "const uint8_t stop1_dory_weights[] = {\n  "
    + ",\n  ".join(rows)
    + ",\n};\n"
    "const size_t stop1_dory_weights_len = sizeof(stop1_dory_weights);\n"
)
(root / "generated" / "src" / "embedded_weights.c").write_text(source)
print(len(payload))
