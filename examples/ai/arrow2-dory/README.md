# Arrow2 DORY — left/right on AI-deck

Local TinyShapeCNN (`left` / `right`), 32×32 grayscale, DORY L2 arena 32 KiB.
No SoftAP/JPEG at runtime — only CPX console over Crazyradio.

```
arrow2 class=left|right|uncertain logits=[L,R] margin=N cycles=N
```

`uncertain` when `margin < ARROW2_MARGIN_MIN` (default 50).

## Rebuild

```bash
cd tinyml_demo
bash scripts/docker_generate_arrow2_dory.sh
bash scripts/docker_run_arrow2_gvsoc.sh
bash scripts/docker_build_arrow2_demo.sh   # → BUILD/.../target.board.devices.flash.img
```

Always `source configs/ai_deck.sh` (Himax I2C0).

## Flash later

```bash
bash scripts/deploy_arrow2.sh
PYTHONPATH=. python3 scripts/arrow2_monitor.py radio://0/67/2M
```
