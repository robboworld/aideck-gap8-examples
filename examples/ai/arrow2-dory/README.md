# Arrow2 DORY — left/right/empty on AI-deck

Local TinyShapeCNN (`left` / `right` / `empty`), 32×32 grayscale, DORY L2 arena
32 KiB. No SoftAP/JPEG at runtime — CRTP class lines over Crazyradio.

Console prints one of: `left`, `right`, `empty`, `uncertain`.

`uncertain` when margin/logit gates fail (see `ARROW2_MARGIN_MIN` /
`ARROW2_LOGIT_MIN` in `main.c`).

## Motor twitch (optional)

On a debounced `left`/`right`, GAP8 sends a CPX APP packet to the STM32.
Pair with the Crazyflie app in
[`crazyflie-apps/arrow2_motor_twitch/`](../../../../crazyflie-apps/arrow2_motor_twitch/)
(props OFF). See that README for flash + desk test. CRTP also prints
`twitch L` / `twitch R` when a packet is queued.

## Rebuild

```bash
cd tinyml_demo
bash scripts/docker_generate_arrow2_dory.sh
bash scripts/docker_run_arrow2_gvsoc.sh
bash scripts/docker_build_arrow2_demo.sh   # → BUILD/.../target.board.devices.flash.img
```

Always `source configs/ai_deck.sh` (Himax I2C0).

## Flash

Prefer JTAG (OTA often hangs ~99%):

```bash
bash scripts/deploy_arrow2.sh
PYTHONPATH=. python3 scripts/arrow2_monitor.py radio://0/67/2M
```
