# From the MATLAB reference to this firmware

The algorithm family lives in [va-sar/BasebandManchesterDecoder](https://github.com/va-sar/BasebandManchesterDecoder). That repository generates synthetic complex I/Q, acquires Manchester timing, and reports payload BER and frame-error rate. Its [embedded deployment note](https://github.com/va-sar/BasebandManchesterDecoder/blob/main/docs/EMBEDDED_DEPLOYMENT.md) is a migration plan: DMA buffers, a causal envelope, half-bit accumulation, and a phase search sized for a Cortex-M4.

This repository is that migration, run under a constraint the plan calls out directly: a raw carrier near 13.56 MHz does not leave enough cycles per sample for the reference chain, and this lab has no electromagnetic fixture that can radiate that field into a receiver.

The substitute is acoustic. A laptop renders the same synthetic frame as sound. A digital microphone samples it. The STM32 runs the acquisition idea in floating point, because the audio rate restores a comfortable cycle budget.

## What transferred

| Stage | MATLAB reference | This firmware |
|---|---|---|
| Stimulus | Complex I/Q, random phase, Q15 files | Mono WAV, ASK or bipolar baseband |
| Frontend | \(\sqrt{I^2+Q^2}\) | \(\lvert x\rvert\) of the left I2S slot, then a one-pole smoother |
| Baseline | Running-mean removal | The smoother rejects the carrier; no separate DC tracker |
| Smoothing | 17-tap causal FIR, cutoff configured in `dsp_config.m` | Exponential moving average, \(\alpha = 0.98\) |
| Bit metric | Mean of second half minus mean of first half | Same sign rule. 281 samples split as 140 then 141, both divided by 140 |
| Timing | Search over one bit period, configurable step | Search every sample phase over the first 20 bit periods |
| Frame lock | Preamble errors, sync errors, signed sequence margin | Exact 16-bit match of `0xB24D` |
| Payload | Compared against the reference, BER/FER aggregated | Five bytes logged on UART and on the SD card |
| Numeric format | Offline float, with a Q15 deployment plan | `float` on the Cortex-M4F FPU |

The bit polarity is the same in both trees. A zero is high then low. A one is low then high. The hard decision is 1 when the second half-bit mean is at least the first.

## Why the rates changed

The MATLAB note estimates about nine cycles per sample for a 120 MHz core sampling at 13.56 MHz. That budget cannot hold a magnitude, a FIR, and a phase search.

This board clocks the core at 168 MHz and the microphone at 43.750 kHz:

\[
C_{sample} = \frac{168\times 10^6}{43750} \approx 3840 \text{ cycles/sample}
\]

Three thousand cycles is enough for a floating-point envelope and a one-time search. The search itself is not per sample. It runs once, over a 20-bit snapshot, after the acquisition buffer fills.

The bit rate follows the same choice. `SAMPLES_PER_BIT` is 281, so the integrate-and-dump grid is:

\[
R_b = \frac{43750}{281} \approx 155.7 \text{ bit/s}
\]

A 104-bit frame then lasts about 0.67 s. That is slow for a contactless air interface and comfortable for a speaker, a microphone, and a first streaming port. Lowering the rate is what made the acoustic channel usable. It does not, by itself, cancel a sample-clock ratio error: slip measured in bits depends on the clock ratio and the frame length in bits. The lab generator is therefore locked to 43750 Hz, the rate the I2S master actually produces. Details are in [Acoustic lab](ACOUSTIC_LAB.md).

## What this port leaves for a later revision

The MATLAB receiver ranks candidates with preamble errors and a signed sequence margin, then can refine timing. The firmware takes the strongest half-bit contrast in the opening snapshot and afterwards trusts a fixed phase. Sync is an exact shift-register compare, so a noise pattern of `B2 4D` opens the payload parser.

Those shortcuts are visible in `StartManchesterDecoder` and `find_initial_offset`. They are the right scope for a first hardware pass that had to prove streaming capture, envelope polarity, and frame printout. They are not the full reference acquisition. A later pass can put the preamble check and the signed margin back in front of the sync compare, then add a narrow early/late update so a long capture does not depend on one phase estimate.

Fixed-point equivalence against the MATLAB vectors is still open. At 43.750 kHz it is an accuracy task, not a throughput task.
