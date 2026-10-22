# letter20-cls-dory — 19 letters + empty on AI-deck

TinyShapeCNN (16/32/32 ch, ~24k params, int8 via NEMO → DORY) classifies the
whole Himax QVGA frame (324×244 → 32×32 nearest neighbour + 2–98 % contrast
stretch, bit-exact with `tinyml_demo.letter20_cls.preprocess_full_nn`).
No Wi-Fi at runtime: results go to the Crazyflie console over Crazyradio.

```
L20 <frame> <t_us> <top1> <logit1> <top2> <logit2> <top3> <logit3> <kcycles> <ms>
L20s <frame> <raw_mean> <raw_min> <raw_max>        every LETTER20_DUMP_EVERY frames
L20i <frame> <row> <32 hex nibbles>                32 rows, 4-bit model input
L20 boot / golden <cls> <logit> / camera orientation=3 / ready / error <why>
```

`golden` must equal the host value in `tinyml_demo/artifacts/letter20_cls_nemo/out_layer6.txt`
(argmax / max). Camera init copies `wifi-img-streamer` (orientation 3), which
captured the training set; without START→orient→STOP→AEG_INIT frames stay near-black.

`t_us` is the GAP8 timer value captured immediately before each frame capture.
Build the test image without an added delay using
`make LETTER20_PERIOD_MS=0` (default: 100 ms). With the default period, the
observed capture+preprocess+inference time is about 127 ms and throughput is
about 4.4 FPS. For the PМИ latency test, keep `LETTER20_DUMP_EVERY=20` so the
viewer receives `L20i` rows for each twentieth frame.

## Build / flash / view

```bash
cd tinyml_demo
bash scripts/letter20_cls_pipeline.sh models/letter20_cls_v3/best.pt   # NEMO→DORY→GVSOC→image→JTAG
python3 scripts/letter20_power_cycle.py                                 # boot new image over radio
python3 scripts/letter20_radio_viewer.py                                # terminal dashboard
```

Note: DORY's stock `network.c` copies all weights into one `L3_WEIGHTS_SIZE`
buffer; the packaging script sets it to 32 KiB (weights are ~25 KiB). The arrow2
value (8 KiB) silently corrupts the network.
