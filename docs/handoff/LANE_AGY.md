# Antigravity lane: can the SNES APU HLE be ported, and what would it cost?

This is a research task. Produce a written answer, not a port.

## The question
SNES runs at 26-28 fps here (36 ms/frame against a 16.7 ms budget) and is the
worst system we have. The Game & Watch port reached 55.4 fps, and the lever that
did it was replacing the SPC700 + DSP with a native N-SPC player.

Measured on this device today: the SPC700 costs **6-33% of wall clock** depending
on scene (`RG_BENCH_PROFILE_APU=1`, timing the per-scanline drain loop in
`retro-core/components/snes9x/src/spc700.h`). That is a lower bound — the
per-opcode `APU_EXECUTE1()` sites are not timed.

## Why it is not a copy
Their SNES core is **not snes9x**. It is `external/sm` — a separate emulator with
its own `src/snes/apu.c` and `src/dsp.c`. The N-SPC wire
(`tools/nspc_audio_wire/nspc_wire.c`, ~1050 lines, pure C, no assembly) is bound
to that emulator's contract: `apu_run()`, `dsp_cycle`, its ARAM layout, its port
protocol.

Ours is snes9x: `retro-core/components/snes9x/src/` — `spc700.c`, `apu.c`,
`soundux.c`, with `IAPU`/`APUExecute()` and a different port model.

## What to produce
A document at `docs/SNES_APU_HLE_FEASIBILITY.md` answering:
1. Which parts of the N-SPC package are emulator-agnostic (the sequencer and the
   DSP mixing) versus glue that must be rewritten against snes9x.
2. What snes9x would have to expose for the swap: where ARAM lives, where the
   $2140-43 port traffic is handled, where a driver-detect hook could sit.
3. The honest boundary. Their own README is explicit that only the std/YI N-SPC
   variants work, that Konami's GD3 dialect hangs, and that SFX protocols are
   instant-acked rather than played. State what fraction of a library that
   leaves, using their `docs/SNES_COMPATIBILITY.md` numbers.
4. A recommendation, with the reasoning, on whether this is worth doing here.

## Read, do not modify
`/home/jshsakura/app/game-and-watch-retro-go-sd/` — theirs, read only. Start with
`tools/nspc_audio_wire/README.md` and `docs/OPTIMIZATION_LEDGER.md`.

## Do not touch
Any source file in this repo. This lane produces one document.
