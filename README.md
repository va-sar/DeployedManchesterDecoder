# Deployed Manchester Decoder

Streaming Manchester receiver on an STM32F405. It is the embedded deployment of the offline receiver in [va-sar/BasebandManchesterDecoder](https://github.com/va-sar/BasebandManchesterDecoder): complex I/Q generation, magnitude, timing-phase search, preamble-aware sync, and BER/FER curves.

The firmware acquires the same synthetic frame from a live sample stream, recovers bit timing, and prints the payload. The CubeIDE project is still named `ManchesterDecoder`. This repository name is the deployment, not a second algorithm.

A 13.56 MHz electromagnetic fixture was not available, so the lab check used a slowed carrier and bit rate. A Python generator wrote the frame as a WAV, a laptop played it, and an INMP441 captured it. That acoustic path is the bench method. The receiver under test is the streaming decoder.

```text
Python ASK WAV  →  speaker  →  I2S microphone  →  envelope
        →  half-bit integrate-and-dump  →  sync 0xB24D  →  UART payload
```

A successful lab take prints:

```text
>>> Decoder: Payload: 03 AB CD 90 35
```

This is an independent educational project. It is not affiliated with STMicroelectronics, Sony, NXP, or any other trademark holder. The bytes above are synthetic test constants shared with the MATLAB repository. They are not a commercial NFC frame, and the acoustic setup is not a radiated-field test.

## Operating point

| | |
|---|---|
| MCU | STM32F405RGT6, 168 MHz, custom board |
| Microphone | INMP441, L/R to ground, I2S2 master, 16-bit left slot, **43.750 kHz** |
| Bit grid | **281 samples/bit**, about **155.7 bit/s** |
| Lab carrier | about 3 kHz ASK, remembered from the working take |
| Frame | 48 zero bits, sync `B2 4D`, payload `03 AB CD 90 35` |
| Logs | USART1 at 115200, plus `decoded_bits.txt` and a 15 s WAV on the SD card |
| Cycle budget | about 3840 cycles per sample, which is why the port can stay in `float` |

The MATLAB deployment note shows why the original rate does not port unchanged: a 13.56 MHz sample stream leaves on the order of ten cycles per sample. The lab rate restores about 3840 cycles per sample, which is enough to run the acquisition in floating point and watch the payload on a UART.

## Documentation

| Document | What it answers |
|---|---|
| [docs/MATLAB_LINEAGE.md](docs/MATLAB_LINEAGE.md) | Which blocks moved, which were simplified, and why the rate changed |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Tasks, queues, files, and how to build |
| [docs/RECEIVER.md](docs/RECEIVER.md) | Envelope, phase search, bit rule, parser, and the limits still in the code |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Clocks, pins, DMA buffers, and the SD-card boot dependency |
| [docs/ACOUSTIC_LAB.md](docs/ACOUSTIC_LAB.md) | How to generate the WAV, play it, and read the UART |
| [tools/README.md](tools/README.md) | Generator defaults |

## Reproduce the lab take

1. Build and flash the CubeIDE project. The Debug folder is a local build output and is not in git.
2. Fit a FAT SD card. The firmware halts if the mount fails, even when the only result you want is the UART line.
3. Wire an INMP441: SCK to PB10, WS to PB12, SD to PC3, L/R to ground. The firmware decodes the left slot.
4. Connect PA9 to a USB-UART adapter and open 115200 8N1.
5. Generate a grid-matched file and play it before reset:

```bash
pip install -r tools/requirements.txt
python tools/generate_manchester_wav.py
```

The generator must print `Samples per bit : 281`. Carrier, gain, and the requirement that the phase search land inside the preamble are covered in [docs/ACOUSTIC_LAB.md](docs/ACOUSTIC_LAB.md).

## Repository layout

```text
Core/                         application and CubeMX user code
Drivers/ Middlewares/         vendored CubeFW F4, CMSIS, FreeRTOS, FatFs
docs/                         design and lab notes
tools/generate_manchester_wav.py
ManchesterDecoder.ioc         CubeMX project, firmware package V1.27.1
```

## Scope

The firmware demonstrates a live path: DMA capture, a causal envelope, a one-shot timing search, and a frame parser that reports the known payload while a WAV logger runs in parallel.

It does not yet include the MATLAB preamble ranker, a timing-tracking loop, CRC checking, or fixed-point equivalence against the Q15 corpus. Those gaps are listed in [docs/RECEIVER.md](docs/RECEIVER.md) so the port can be read against the reference without guessing.

## License

Application code, docs, and tools are MIT. See [LICENSE](LICENSE). HAL, CMSIS, FreeRTOS, and FatFs keep the licenses listed in [NOTICE.md](NOTICE.md).
