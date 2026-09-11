# Arrow2 SoftAP stream + on-drone classify (experimental)

Keeps inference on GAP8. SoftAP JPEG for debug view; CRTP `a2 …` for gates.

Stable radio-only app: [`../arrow2-dory/`](../arrow2-dory/) (git snapshot).

## Status bar in the JPEG

Top ~10 rows painted after onboard classify:

| Tone | Meaning |
|------|---------|
| mid gray (~128) | `uncertain` |
| bright (~220) | `left` |
| dark (~40) | `right` |

## Build / flash

```bash
cd tinyml_demo
bash scripts/docker_build_arrow2_stream_demo.sh
bash scripts/deploy_arrow2_stream.sh   # JTAG HyperFlash
```

## Run

1. Power-cycle, join SoftAP **`AI Deck arrow2`**, ping `192.168.4.1`.
2. Viewer (JPEG only):

```bash
python3 scripts/arrow2_stream_viewer.py
```

3. Parallel radio diagnostics:

```bash
python3 scripts/arrow2_monitor.py radio://0/67/2M
```

Detection is on-drone; PC viewer does not run the CNN.
