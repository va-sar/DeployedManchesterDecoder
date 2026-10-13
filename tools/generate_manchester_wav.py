#!/usr/bin/env python3
"""
Acoustic Manchester stimulus for the STM32 lab receiver.

Builds a mono 16-bit WAV of repeated synthetic frames:

    48 preamble zero bits
    B2 4D 03 AB CD 90 35

Two waveforms are available.

    baseband  bipolar Manchester (high/low mapped to +1/-1)
    ask       the same envelope applied to a sine carrier

ASK is the waveform the firmware envelope detector expects. Defaults follow
the STM32 I2S clock (43.750 kHz) and SAMPLES_PER_BIT (281):

    python tools/generate_manchester_wav.py

The frame fields are synthetic test constants shared with
https://github.com/va-sar/BasebandManchesterDecoder
They are not a commercial NFC frame.
"""

from __future__ import annotations

import argparse
import math
import wave
from pathlib import Path

import numpy as np

# Must stay equal to SAMPLES_PER_BIT in Core/Src/main.c.
FIRMWARE_SAMPLES_PER_BIT = 281
# CubeMX RealAudioFreq for PLLI2SN=168, PLLI2SR=4, MCLK off, 16-bit Philips.
FIRMWARE_SAMPLE_RATE_HZ = 43750


def bytes_to_bits_msb(data):
    """Convert bytes to bits, MSB first."""
    data = np.asarray(data, dtype=np.uint8)
    return np.unpackbits(data, bitorder="big")


def manchester_envelope(bits, samples_per_bit, low_level, high_level):
    """
    Manchester envelope.

    bit == 0: first half high, second half low
    bit == 1: first half low, second half high
    """
    bits = np.asarray(bits, dtype=np.uint8)

    if samples_per_bit < 2:
        raise ValueError("samples_per_bit must be at least 2.")

    # Same split as the firmware: half_bit = SAMPLES_PER_BIT / 2.
    # For 281 that is 140 samples, then 141. Both sums are later scaled by 140.
    half = samples_per_bit // 2
    out = np.empty(bits.size * samples_per_bit, dtype=np.float64)

    for i, bit in enumerate(bits):
        start = i * samples_per_bit
        mid = start + half
        end = start + samples_per_bit

        if bit == 0:
            out[start:mid] = high_level
            out[mid:end] = low_level
        else:
            out[start:mid] = low_level
            out[mid:end] = high_level

    return out


def apply_fade(x, sample_rate, fade_ms):
    """Short fade in/out so the speaker does not click."""
    if fade_ms <= 0:
        return x

    fade_samples = int(fade_ms * sample_rate / 1000.0)
    if fade_samples <= 0 or x.size < 2 * fade_samples:
        return x

    x = x.copy()
    x[:fade_samples] *= np.linspace(0.0, 1.0, fade_samples)
    x[-fade_samples:] *= np.linspace(1.0, 0.0, fade_samples)
    return x


def write_wav_mono_i16(path: Path, audio: np.ndarray, sample_rate: int):
    """Write mono 16-bit PCM WAV. `audio` is in the range [-1, 1]."""
    audio = np.asarray(audio, dtype=np.float64)
    pcm_float = np.clip(audio * 32767.0, -32768.0, 32767.0)
    pcm = np.round(pcm_float).astype(np.int16)

    with wave.open(str(path), "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())


def generate_manchester_wav(
    out_dir: Path,
    duration_sec: float,
    sample_rate: int,
    bit_rate: float,
    mode: str,
    amplitude: float,
    carrier_hz: float,
    ask_low: float,
    ask_high: float,
    gap_ms: float,
    fade_ms: float,
):
    out_dir = Path(out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    samples_per_bit = int(round(sample_rate / bit_rate))
    if samples_per_bit < 4:
        raise ValueError(
            "samples_per_bit is too small. Choose a lower bit rate or a higher sample rate."
        )

    actual_bit_rate = sample_rate / samples_per_bit

    print(f"Requested bit rate    : {bit_rate:.4f} bps")
    print(f"Samples per bit       : {samples_per_bit}")
    print(f"Actual bit rate       : {actual_bit_rate:.4f} bps")
    if samples_per_bit == FIRMWARE_SAMPLES_PER_BIT:
        print(f"Firmware grid         : matches SAMPLES_PER_BIT {FIRMWARE_SAMPLES_PER_BIT}")
    else:
        print(
            f"Firmware grid         : {samples_per_bit} samples/bit, "
            f"firmware is compiled for {FIRMWARE_SAMPLES_PER_BIT}"
        )

    sync_bytes = np.array([0xB2, 0x4D], dtype=np.uint8)
    length_byte = np.array([0x03], dtype=np.uint8)
    payload_bytes = np.array([0xAB, 0xCD], dtype=np.uint8)
    crc_bytes = np.array([0x90, 0x35], dtype=np.uint8)
    frame_bytes = np.concatenate([sync_bytes, length_byte, payload_bytes, crc_bytes])
    frame_hex = " ".join(f"{b:02X}" for b in frame_bytes)

    preamble_bits = np.zeros(48, dtype=np.uint8)
    frame_bits = np.concatenate([preamble_bits, bytes_to_bits_msb(frame_bytes)])

    print(f"Frame bytes           : {frame_hex}")
    print(f"Frame bits            : {frame_bits.size}")

    if mode == "baseband":
        # Bipolar levels survive an AC-coupled speaker better than a unipolar 0.2/1.0 envelope.
        frame_wave = manchester_envelope(
            frame_bits, samples_per_bit, low_level=-1.0, high_level=+1.0
        )
    elif mode == "ask":
        if carrier_hz <= 0:
            raise ValueError("carrier_hz must be positive in ask mode.")
        if carrier_hz >= sample_rate / 2:
            raise ValueError("carrier_hz must be below the Nyquist frequency.")

        envelope = manchester_envelope(
            frame_bits, samples_per_bit, low_level=ask_low, high_level=ask_high
        )
        t = np.arange(envelope.size, dtype=np.float64) / sample_rate
        carrier = np.sin(2.0 * math.pi * carrier_hz * t)
        frame_wave = envelope * carrier
    else:
        raise ValueError(f"Unknown mode: {mode}")

    frame_duration_sec = frame_wave.size / sample_rate
    print(f"One frame duration    : {frame_duration_sec * 1000.0:.2f} ms")

    gap_samples = int(gap_ms * sample_rate / 1000.0)
    if gap_samples > 0:
        block_wave = np.concatenate([frame_wave, np.zeros(gap_samples, dtype=np.float64)])
    else:
        block_wave = frame_wave

    target_samples = int(duration_sec * sample_rate)
    if block_wave.size <= 0:
        raise ValueError("Generated block is empty.")

    repeats = math.ceil(target_samples / block_wave.size)
    audio = np.tile(block_wave, repeats)[:target_samples]
    print(f"Approx frame repeats  : {target_samples / block_wave.size:.1f}")

    audio = apply_fade(audio, sample_rate, fade_ms)
    peak = float(np.max(np.abs(audio))) if audio.size else 0.0
    if peak > 0.0:
        audio = audio / peak
    audio = audio * amplitude

    if mode == "baseband":
        filename = (
            f"manchester_baseband_fs{sample_rate}_br{actual_bit_rate:.0f}_dur{duration_sec:.1f}s.wav"
        )
    else:
        filename = (
            f"manchester_ask_fs{sample_rate}_br{actual_bit_rate:.0f}_"
            f"carrier{carrier_hz:.0f}_dur{duration_sec:.1f}s.wav"
        )

    out_path = out_dir / filename
    write_wav_mono_i16(out_path, audio, sample_rate)
    print(f"Saved: {out_path}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate a repeated Manchester WAV for the STM32 acoustic lab."
    )
    parser.add_argument("--out", type=Path, default=Path("wav_generated"))
    parser.add_argument(
        "--duration",
        type=float,
        default=15.0,
        help="File duration in seconds. Default: 15 (matches the firmware recording window).",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=FIRMWARE_SAMPLE_RATE_HZ,
        help="WAV sample rate in Hz. Default: 43750 (I2S master rate).",
    )
    parser.add_argument(
        "--bit-rate",
        type=float,
        default=FIRMWARE_SAMPLE_RATE_HZ / FIRMWARE_SAMPLES_PER_BIT,
        help="Requested bit rate. Default yields 281 samples/bit at 43750 Hz.",
    )
    parser.add_argument("--mode", choices=["baseband", "ask"], default="ask")
    parser.add_argument("--amplitude", type=float, default=0.8, help="Peak scale, 0.0 to 1.0.")
    parser.add_argument(
        "--carrier",
        type=float,
        default=3000.0,
        help="ASK carrier in Hz. Default 3000 matches the remembered working lab take.",
    )
    parser.add_argument("--ask-low", type=float, default=0.2)
    parser.add_argument("--ask-high", type=float, default=1.0)
    parser.add_argument("--gap-ms", type=float, default=0.0)
    parser.add_argument("--fade-ms", type=float, default=5.0)
    args = parser.parse_args()

    generate_manchester_wav(
        out_dir=args.out,
        duration_sec=args.duration,
        sample_rate=args.sample_rate,
        bit_rate=args.bit_rate,
        mode=args.mode,
        amplitude=args.amplitude,
        carrier_hz=args.carrier,
        ask_low=args.ask_low,
        ask_high=args.ask_high,
        gap_ms=args.gap_ms,
        fade_ms=args.fade_ms,
    )


if __name__ == "__main__":
    main()
