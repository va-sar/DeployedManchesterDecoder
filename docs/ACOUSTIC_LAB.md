# Acoustic lab

The MATLAB model assumes a complex baseband sample stream. This lab did not have a fixture that could illuminate the board with a 13.56 MHz field, so the carrier and the bit rate were moved into the audio band and the "channel" became a speaker and a microphone.

```text
synthetic frame
    → Python ASK waveform
    → laptop headphone or speaker output
    → INMP441 (L/R grounded, left slot) on the STM32
    → envelope, phase search, sync, payload
    → USART1 text at 115200
    → SD card WAV and decoded_bits.txt
```

What is under test is the streaming receiver: DMA, envelope polarity, half-bit integration, and frame parsing. The air interface, the antenna, and the 13.56 MHz analog front end are outside this setup. Results here do not transfer to a radiated link by a scale factor.

## Generate the stimulus

From the repository root, with Python 3 and NumPy:

```bash
pip install -r tools/requirements.txt
python tools/generate_manchester_wav.py
```

Defaults:

| Knob | Value |
|---|---|
| Format | Mono PCM 16-bit |
| Sample rate | 43750 Hz |
| Samples per bit | 281 |
| Bit rate | 155.694 bit/s |
| Mode | ASK, low 0.2, high 1.0 |
| Carrier | 3000 Hz |
| Duration | 15 s |
| Gap between frames | 0 |
| Fade | 5 ms |

The script prints `Samples per bit`. It has to be 281. The filename rounds the bit rate, so the default file is named `br156` even though the rate is 155.694 bit/s. Trust the printed sample count, not the rounded name.

Earlier lab files generated at 44117 Hz (`br100`, `br156`, `br199`, and nearby carriers) use a different grid. A one-sample-per-bit error is most of a bit of slip over a 104-bit frame.

The working take is remembered at about 3 kHz. Other files in the lab folder used carriers from a few hundred hertz up to 6 kHz. The firmware smoother has a 3 dB bandwidth near 140 Hz, so 3 kHz is high enough for the one-pole stage to act as an envelope detector. A few hundred hertz is not. Keep the laptop volume high enough for a clean tone and low enough that the INMP441 does not clip. The decoder has no AGC.

Play the file in a loop from the laptop. Start playback before resetting the board, or within the preamble of a frame. The phase search consumes the first 20 bit periods after DMA starts, about 127 ms. Those samples need to be preamble, not silence. The 48-bit preamble is 308 ms, so a capture that begins during the preamble still has preamble left after the search.

Leave `--gap-ms` at 0. A silent gap between frames does not reset the bit grid. The parser can re-find sync, but the sample phase stays at the original offset.

## What the terminal should show

Open a serial terminal on USART1, 115200 8N1. Lines from the three tasks interleave. A healthy take looks like this:

```text
>>> SD Writer: Task started.
>>> Decoder: Ready. Waiting for audio...
>>> Audio: Waiting...
>>> SD Writer: Attempting to mount SD card...
>>> SD Writer: SD Card mounted successfully.
>>> SD Writer: Opening WAV file...
>>> SD Writer: Writing WAV header...
>>> SD Writer: Releasing StartCapture semaphore.
>>> Audio: Starting DMA...
>>> Decoder: Phase locked! Offset = 17
>>> Decoder: SYNC FOUND at sample 18432!
>>> Decoder: Payload: 03 AB CD 90 35
```

`Offset` depends on when DMA started relative to the bit edge. The payload line is the pass criterion used in the lab: `03 AB CD 90 35`. Sync itself is not printed again; it was the gate. Further payload lines appear for each following frame until the 15 s recording ends and the writer stops I2S.

If the card is absent or unformatted the writer calls `Error_Handler` and the UART goes quiet after the mount message. The card has to be FAT and writable. The firmware opens `Recording.WAV`, `reference.txt`, and `decoded_bits.txt` in the root.

## How to read a miss

| Symptom | Likely cause |
|---|---|
| No `Phase locked` line | DMA never started, or the decoder task did not get buffers |
| `Phase locked`, never `SYNC FOUND` | Grid is 281 but the WAV was built with another length, or the snapshot was silence, or the carrier is too low for the smoother |
| Sync found, payload bytes differ | Phase is slipping, the microphone is clipping, or a false `B2 4D` was accepted in noise |
| One good frame, then garbage | Fixed phase and a sample-rate mismatch between the WAV header rate and 43.750 kHz |
| Mount failed, then silence | SD card, wiring, or filesystem; the CPU is halted in `Error_Handler` |

`decoded_bits.txt` keeps every accepted payload with the sample index of the last bit of that frame. `reference.txt` is the expected field, written once at start.

## What this lab is allowed to claim

The board can acquire a known Manchester ASK frame from a microphone and print the synthetic payload on a UART while a WAV logger runs beside it. That is the hardware step the MATLAB repository left open.

It is not a BER curve, not a radiated NFC test, and not a fixed-point equivalence run against the Q15 corpus. Those remain the validation method in the MATLAB tree. The acoustic take is a functional test of the streaming port at one high signal-to-noise operating point.
