#!/usr/bin/env python3
"""Turn DORY hex blobs into a single flash-resident C array."""
from pathlib import Path

root = Path(__file__).resolve().parent
files = [
    "BNReluConvolution0_weights.hex",
    "BNReluConvolution1_weights.hex",
    "BNReluConvolution2_weights.hex",
    "BNReluConvolution3_weights.hex",
    "Convolution4_weights.hex",
]
hex_dir = root / "generated" / "hex"
missing = [name for name in files if not (hex_dir / name).is_file()]
if missing:
    raise SystemExit("missing hex blobs: {}".format(", ".join(missing)))
payload = b"".join((hex_dir / name).read_bytes() for name in files)
header = """#ifndef LETTER20_EMBEDDED_WEIGHTS_H
#define LETTER20_EMBEDDED_WEIGHTS_H
#include <stdint.h>
#include <stddef.h>
extern const uint8_t letter20_dory_weights[];
extern const size_t letter20_dory_weights_len;
#endif
"""
(root / "generated" / "inc" / "embedded_weights.h").write_text(header)
rows = [
    ", ".join("0x{:02x}".format(value) for value in payload[offset : offset + 12])
    for offset in range(0, len(payload), 12)
]
source = (
    '#include "embedded_weights.h"\n\n'
    "const uint8_t letter20_dory_weights[] = {\n  "
    + ",\n  ".join(rows)
    + ",\n};\n"
    "const size_t letter20_dory_weights_len = sizeof(letter20_dory_weights);\n"
)
(root / "generated" / "src" / "embedded_weights.c").write_text(source)
print(len(payload))
