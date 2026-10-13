# Receiver

All of the algorithm is in `Core/Src/main.c`. Three tasks share one I2S stream. The decoder task is the port of the MATLAB core.

## Frame the parser expects

```text
48 zero bits          preamble, used as a timing field
B2 4D                 sync, exact 16-bit match
03 AB CD 90 35        five bytes copied out as the payload
```

Bits are MSB first inside each byte. The five payload bytes are the length, two data bytes, and two trailing check bytes from the synthetic generator. The firmware does not recompute a CRC. A successful line means those five bytes were shifted in after sync.

Total length is 104 bits. At 281 samples/bit and 43.750 kHz that is 668 ms.

## Sample path

Each DMA half copies one pool slot and posts the same pointer to two queues:

```text
I2S DMA circular buffer
        │
        ├─ AudioLogQueueHandle      → SD writer (AGC, WAV)
        └─ DecoderAudioQueueHandle  → Manchester decoder
```

For each left-slot sample \(x\):

\[
e[n] = 0.98\,e[n-1] + 0.02\,\lvert x[n]\rvert
\]

The time constant is about 1.1 ms. The 3 dB bandwidth is about 140 Hz, which sits next to the 156 bit/s Manchester fundamental. The working lab take used an ASK carrier of about 3 kHz. That tone is still well above the smoother, so the one-pole stage behaves as an envelope detector. A carrier of a few hundred hertz does not.

## Phase search

The first `20 * 281` envelope samples fill `acq_buf`. `find_initial_offset` then scores every offset \(\tau \in [0, 281)\).

For each of 16 bits starting at \(\tau\):

\[
m = \left\lvert \mathrm{mean}(H_1) - \mathrm{mean}(H_2) \right\rvert
\]

The offset with the largest average \(m\) becomes the bit grid. Manchester energy is the half-bit difference, so a correct phase scores high and a phase near the middle of a plateau scores low. The search uses 16 bits of the 20-bit snapshot to keep the inner loops inside the buffer.

`SAMPLES_PER_BIT` is 281, so integer division gives a first half of 140 samples and a second half of 141. Both sums are divided by 140. The generator uses that same split.

After the choice is printed (`Phase locked! Offset = …`), the grid is fixed. Samples that arrived inside the snapshot are not decoded. Counting resumes on the live stream, with the accumulator pre-aligned by:

```text
samples_in_current_bit = (ACQ_SAMPLES - locked_offset) % SAMPLES_PER_BIT
```

## Bit decision and parser

Each finished bit period computes:

\[
m = \mathrm{mean}(H_2) - \mathrm{mean}(H_1)
\]

\[
\hat b = 1 \text{ when } m \ge 0, \text{ else } 0
\]

Bits shift into a 32-bit register. In `STATE_SEARCH_SYNC`, the low 16 bits are compared with `0xB24D`. On a match the task switches to `STATE_READ_PAYLOAD`, packs 40 bits into five bytes, appends a line to `decoded_bits.txt`, prints the bytes, and returns to sync search. Back-to-back frames in the WAV are decoded as separate hits for as long as DMA keeps running.

## Task order

```text
SD writer mounts the card
    → releases SDMountedSem
    → decoder opens reference.txt and decoded_bits.txt
SD writer creates Recording.WAV and writes a placeholder header
    → releases StartCaptureSem
    → microphone task starts HAL_I2S_Receive_DMA
At 15 s of mono samples the writer rewrites the WAV header, closes the file,
and calls HAL_I2S_DMAStop
```

Stopping I2S stops the decoder too. Fifteen seconds is the length of one lab take.

## Limits that are still in the code

1. **One phase estimate.** There is no early/late loop. A sample-clock error walks the grid by a fraction of a bit over each 104-bit frame.
2. **The snapshot is the start of DMA, not the start of modulation.** If the first 127 ms are silence or room noise, the winning offset is a noise maximum and every later bit uses it.
3. **Sync does not consult the preamble.** Any 16-bit pattern equal to `B2 4D` is accepted. The MATLAB ranker required the zero preamble and a positive signed margin before it trusted sync.
4. **No error tolerance.** A single flipped sync bit misses the frame. A flipped payload bit is still printed, with no checksum test.
5. **Shared pool, no backpressure.** `osMessageQueuePut(..., 0)` drops the message if a queue is full, and the pool slot can be reused after 30 callbacks.
6. **Recording AGC and decoder gain are different signals.** `Recording.WAV` is a diagnostic take, not a bit-exact capture of \(e[n]\).
7. **The comment above `SAMPLES_PER_BIT` is stale.** It still mentions 100 bit/s and 441 samples. The compiled value is 281.

`configENABLE_FPU` in `FreeRTOSConfig.h` is 0, which is the CubeMX default flag. The linked port is `portable/GCC/ARM_CM4F`, and `SystemInit` enables CP10/CP11. Both the writer and the decoder use `float`. The CM4F port turns the FPU on and relies on hardware lazy stacking across switches.
