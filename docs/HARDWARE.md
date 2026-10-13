# Hardware

Custom lab target. CubeMX records `board=custom`, part `STM32F405RGT6`, package LQFP64.

| Item | Value |
|---|---|
| Core | Cortex-M4F, single-precision FPU |
| System clock | 168 MHz from 8 MHz HSE, PLL M=8, N=336, P=2 |
| PLLQ | 48 MHz, used by SDIO |
| SRAM | 128 KB general-purpose plus 64 KB core-coupled |
| Toolchain | STM32CubeIDE, CubeFW F4 V1.27.1, CubeMX 6.6.1 |
| RTOS | FreeRTOS V10.3.1 through CMSIS-RTOS v2, heap 4, 50 KB |

## Clock tree that sets the sample rate

I2S uses a dedicated PLL configured in `HAL_I2S_MspInit`:

```text
I2SCLK = (HSE / PLLM) * PLLI2SN / PLLI2SR
       = (8 MHz / 8) * 168 / 4
       = 42 MHz
```

Master receive, Philips, 16-bit, MCLK disabled. The resulting audio clock is:

```text
fs = I2SCLK / (32 * (2 * I2SDIV + ODD))
   = 42 MHz / (32 * 30)
   = 43.750 kHz
```

CubeMX reports the same figure (`I2S2.RealAudioFreq=43.75 KHz`). The WAV header written to the SD card is tagged `44117` Hz. That tag is the design rate used while the generator was being tuned. Playback of `Recording.WAV` in a desktop editor is therefore about 0.8% fast relative to the samples the microphone actually produced. Decoder timing does not use the header. It uses `SAMPLES_PER_BIT`.

## Connectors

| Function | Pins | Notes |
|---|---|---|
| I2S2 CK | PB10 | Microphone bit clock, MCU is master |
| I2S2 WS | PB12 | Word select |
| I2S2 SD | PC3 | Serial data in |
| USART1 TX | PA9 | 115200 8N1, firmware printf |
| USART1 RX | PA10 | Wired, unused by the decoder |
| SDIO CK | PC12 | 1-bit bus |
| SDIO D0 | PC8 | Pull-up |
| SDIO CMD | PD2 | Pull-up |
| HSE | PH0, PH1 | 8 MHz |
| LSE | PC14, PC15 | 32.768 kHz crystal pads, not used by the decoder |

The microphone is an InvenSense INMP441. It is an I2S MEMS part and does not use MCLK, so that pin stays unconnected. L/R is tied to ground, which places the sample in the left Philips slot. The firmware keeps that slot (`src16[i * 2]`) and discards the right slot.

| INMP441 pin | STM32 pin | Signal |
|---|---|---|
| SCK | PB10 | I2S2 bit clock, MCU master |
| WS | PB12 | Word select |
| SD | PC3 | Serial data |
| L/R | GND | Left channel |
| VDD | 3.3 V | |
| GND | GND | |

The INMP441 shifts out a 24-bit sample, MSB first, one bit clock after WS changes. This firmware programs I2S for 16-bit slots, so each captured sample is the top 16 bits of that word.

SDIO clock division is 20, so the card clock is `48 MHz / 22 ≈ 2.2 MHz` on a 1-bit bus. That is only a logging path.

## DMA and memory

| Buffer | Size | Where it lives |
|---|---|---|
| `AudioDMA_Buffer` | 1024 × int16, circular | SRAM, DMA-accessible |
| `AudioSafePool` | 30 × 512 int16 | Snapshot pool so the callback can return |
| `acq_buf` | 20 × 281 floats (about 22 KB) | Static, decoder phase search |
| Decoder stack | 16 KB | Phase search is not on the stack |
| Writer and capture stacks | 4 KB each | |
| FreeRTOS heap | 50 KB | Queues, tasks, semaphores |

Half-transfer and transfer-complete each publish 512 int16 values, which is 256 stereo frames. At 43.750 kHz a callback arrives about every 5.9 ms. The pool holds about 170 ms of audio. Both consumers only read a published block. A stalled consumer can still be overrun if it falls behind by more than the pool depth, because the callback does not wait on queue space.

Core-coupled memory is not used. I2S and SDIO DMA cannot see CCM.

## Why an SD card is required at boot

`StartSDAudioWriter` mounts the filesystem before anyone is allowed to start DMA. If `f_mount` fails, the task calls `Error_Handler`, which disables interrupts and spins. The decoder waits forever on `SDMountedSem` in that case as well. A terminal-only experiment still needs a mounted card. The card receives:

| File | Contents |
|---|---|
| `Recording.WAV` | 15 s, mono 16-bit, left slot after a software AGC |
| `reference.txt` | `Expected Hex: B2 4D 03 AB CD 90 35` |
| `decoded_bits.txt` | One line per accepted payload |

The AGC (target peak 10000, attack 0.05, release 0.01) runs only on the recording path. The decoder sees the raw left-slot samples.
