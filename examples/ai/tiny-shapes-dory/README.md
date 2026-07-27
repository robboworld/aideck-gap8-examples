# Tiny shapes with DORY

This AI-deck example classifies a bright `circle`, `square`, or `triangle` on
a dark background with a NEMO-quantized DORY network. It supports:

- `INPUT_MODE=static`: one committed golden input for deterministic GVSOC
  checksum verification.
- `INPUT_MODE=camera`: continuous Himax capture, preprocessing, inference,
  CPX console output, and a binary `CPX_F_APP` result packet.

## Model contract

The input is one unsigned 8-bit grayscale tensor with shape `1x1x32x32`.
Camera mode center-crops the 324x244 Himax frame to 244x244 and applies a
deterministic nearest-neighbor resize. Values remain in `[0, 255]`; there is
no mean subtraction or floating-point normalization.

The three signed 32-bit outputs are integer logits in this order:
`circle`, `square`, `triangle`. The reported `margin` is the integer
difference between the largest and second-largest logit. It is not a
calibrated probability or confidence.

The DORY directional allocator uses a verified 32 KiB L2 arena. The original
generated 380 KB example allocation was a generic template value and left
insufficient L2 memory for the 79 KB camera frame and FreeRTOS/CPX firmware.

## Reproduce the generated runtime

From the repository's `tinyml_demo` directory:

```bash
./scripts/package_aideck_runtime.sh
```

The command checks IntegerDeployable accuracy, copies only DORY runtime
sources, headers, and ReadFS payloads into `generated/`, installs the
FreeRTOS-compatible PMSIS cluster DMA adapter, fixes the generated async
argument bound, and writes SHA-256 metadata.

Do not edit `generated/` manually. Regenerate it after changing the model,
quantization, or DORY configuration.

## Verify without hardware

Run the static application and all layer checksums in GVSOC:

```bash
cd ../../../../tinyml_demo
./scripts/docker_run_aideck_gvsoc.sh
```

Expected final output includes:

```text
Checking final output: Checksum OK
class=square id=1 logits=[-34617,23583,-20334] margin=43917
```

Cross-compile the camera/CPX firmware without attempting to connect to a
physical deck:

```bash
./scripts/docker_build_aideck_camera.sh
```

## Build and flash with an AI-deck

After sourcing a compatible GAP SDK:

```bash
make clean all CORE=8 platform=board INPUT_MODE=camera
make flash CORE=8 platform=board INPUT_MODE=camera
```

The CPX console prints the class, all logits, the integer margin, and cluster
inference cycles. `CPX_F_APP` also sends a packed, little-endian 24-byte
version-1 packet:

- byte 0: protocol version (`1`)
- byte 1: class ID
- bytes 2-3: reserved (`0`)
- bytes 4-15: three signed 32-bit logits
- bytes 16-19: unsigned 32-bit top-1/top-2 margin
- bytes 20-23: unsigned 32-bit inference cycles

## Limitations and troubleshooting

The training set is synthetic. Live-camera accuracy, robustness to lighting,
and probability calibration have not been measured; collect real AI-deck
frames before making those claims. A bright centered shape on a dark
background is the intended starting condition.

- Missing `generated/`: run `tinyml_demo/scripts/package_aideck_runtime.sh`.
- ReadFS cannot open a weight: rebuild the flash image and flash both the
  firmware and filesystem.
- Camera build tries to access JTAG: use the provided cross-build script,
  which builds with `platform=gvsoc` but does not run the camera application.
- Static checksum mismatch: regenerate NEMO golden vectors, DORY code, and
  the packaged runtime from the same model checkpoint.
