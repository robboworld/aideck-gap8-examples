#!/usr/bin/env python3
"""Turn DORY hex blobs into a single flash-resident C array."""
from pathlib import Path

root = Path(__file__).resolve().parent
files = [
    "BNReluConvolution0_weights.hex", "BNReluConvolution1_weights.hex",
    "BNReluConvolution2_weights.hex", "BNReluConvolution3_weights.hex",
    "BNReluConvolution4_weights.hex", "Convolution5_weights.hex",
]
payload = b"".join((root / "generated" / "hex" / name).read_bytes() for name in files)
header = """#ifndef COCO20_EMBEDDED_WEIGHTS_H\n#define COCO20_EMBEDDED_WEIGHTS_H\n#include <stdint.h>\n#include <stddef.h>\nextern const uint8_t coco20_dory_weights[];\nextern const size_t coco20_dory_weights_len;\n#endif\n"""
(root / "generated" / "inc" / "embedded_weights.h").write_text(header)
rows = [", ".join("0x{:02x}".format(value) for value in payload[offset:offset + 12])
        for offset in range(0, len(payload), 12)]
source = "#include \"embedded_weights.h\"\n\nconst uint8_t coco20_dory_weights[] = {\n  " + ",\n  ".join(rows) + ",\n};\nconst size_t coco20_dory_weights_len = sizeof(coco20_dory_weights);\n"
(root / "generated" / "src" / "embedded_weights.c").write_text(source)
print(len(payload))
