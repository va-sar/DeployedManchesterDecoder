# Lab stimulus generator

`generate_manchester_wav.py` builds the repeated Manchester WAV that the laptop plays into the microphone. It is the acoustic counterpart of `generate_synthetic_dataset.m` in [BasebandManchesterDecoder](https://github.com/va-sar/BasebandManchesterDecoder).

The defaults match the firmware grid:

| Parameter | Default | Why |
|---|---|---|
| Sample rate | 43750 Hz | I2S master rate on this clock tree |
| Bit rate | 43750/281 bit/s | `SAMPLES_PER_BIT` in `Core/Src/main.c` |
| Mode | ASK | Speaker and microphone are AC-coupled; a carrier survives that path |
| Carrier | 3000 Hz | Remembered working lab take; still well above the envelope smoother |
| Duration | 15 s | Same window the SD writer records |

```bash
pip install -r tools/requirements.txt
python tools/generate_manchester_wav.py
```

The script prints `Samples per bit`. That line must read `281` before the WAV is used with the current firmware. See [Acoustic lab](../docs/ACOUSTIC_LAB.md).
