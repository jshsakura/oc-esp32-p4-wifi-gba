#pragma once

// A one-shot probe for the single question Phase 2 of the roadmap is blocked on: can this
// chip fetch instructions from PSRAM, and if so, how much slower than from internal RAM?
//
// The answer decides where a dynarec's code cache can live. Yes means hot blocks in IRAM and
// cold blocks in PSRAM, which is the difference between a 64KB code cache and a 4MB one. No
// means the whole cache has to fit in an internal SRAM budget that GBA memory has already
// mostly spent (see docs/ROADMAP.md section 3).
//
// Runs only when /sd/retro-go/psram_exec_test exists, and deletes that file before it tries
// anything. Answering "no" here means a fetch fault, which is a panic -- and a probe that
// panics on every boot from then on is a device that no longer boots, not an experiment.
// Results go to /sd/retro-go/psram_exec_test.log as well as the console, because the run
// that crashes is the one whose console output nobody is watching.
//
// Delete this file and its caller once the answer is written into docs/ROADMAP.md. It is a
// question, not a feature.
void rg_psram_exec_test(void);
