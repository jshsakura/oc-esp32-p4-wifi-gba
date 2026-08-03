/* This file is part of Snes9x. See LICENSE file. */

#ifndef USE_BLARGG_APU

#ifndef _SPC700_H_
#define _SPC700_H_

#define Carry       1
#define Zero        2
#define Interrupt   4
#define HalfCarry   8
#define BreakFlag  16
#define DirectPageFlag 32
#define Overflow   64
#define Negative  128

#define APUClearCarry() (IAPU._Carry = 0)
#define APUSetCarry() (IAPU._Carry = 1)
#define APUSetInterrupt() (IAPU.Registers.P |= Interrupt)
#define APUClearInterrupt() (IAPU.Registers.P &= ~Interrupt)
#define APUSetHalfCarry() (IAPU.Registers.P |= HalfCarry)
#define APUClearHalfCarry() (IAPU.Registers.P &= ~HalfCarry)
#define APUSetBreak() (IAPU.Registers.P |= BreakFlag)
#define APUClearBreak() (IAPU.Registers.P &= ~BreakFlag)
#define APUSetDirectPage() (IAPU.Registers.P |= DirectPageFlag)
#define APUClearDirectPage() (IAPU.Registers.P &= ~DirectPageFlag)
#define APUSetOverflow() (IAPU._Overflow = 1)
#define APUClearOverflow() (IAPU._Overflow = 0)

#define APUCheckZero() (IAPU._Zero == 0)
#define APUCheckCarry() (IAPU._Carry)
#define APUCheckInterrupt() (IAPU.Registers.P & Interrupt)
#define APUCheckHalfCarry() (IAPU.Registers.P & HalfCarry)
#define APUCheckBreak() (IAPU.Registers.P & BreakFlag)
#define APUCheckDirectPage() (IAPU.Registers.P & DirectPageFlag)
#define APUCheckOverflow() (IAPU._Overflow)
#define APUCheckNegative() (IAPU._Zero & 0x80)

typedef union
{
   struct
   {
#ifdef MSB_FIRST
      uint8_t Y, A;
#else
      uint8_t A, Y;
#endif
   } B;

   uint16_t W;
} YAndA;

typedef struct
{
   uint8_t   P;
   YAndA     YA;
   uint8_t   X;
   uint8_t   S;
   uint16_t  PC;
} SAPURegisters;

/* Needed by ILLUSION OF GAIA */
#define ONE_APU_CYCLE 21

void APUExecute(void);

#ifdef RG_BENCH_PROFILE_APU
/* Bench only: price the SPC700 without removing it.
 *
 * Removal was tried and cannot work: the SNES CPU handshakes with the APU through its
 * ports at boot, so with the SPC700 gone the guest spins forever and the board dies on the
 * watchdog. That is also exactly why the Game & Watch port replaced this with an HLE
 * rather than skipping it -- the guest must still be answered.
 *
 * Two other probes were tried first and measured nothing, both worth recording: the
 * front-end's "Audio enable" switch gates mixing and output, not emulation; and the
 * blargg S9xAPUExecute() path in cpuexec.c is dead code here, because USE_BLARGG_APU is
 * not defined in this build.
 *
 * This times the per-scanline drain loop, which is where the bulk runs. The APU_EXECUTE1()
 * sites in cpuops.c are per-opcode and are deliberately NOT timed -- a timer call there
 * would cost more than the work it measures -- so this is a lower bound, not a total. */
extern int64_t g_apu_us;
extern uint32_t g_apu_calls;
#include <esp_timer.h>

#define APU_EXECUTE1() \
APUExecute();

#define APU_EXECUTE() do { \
    int64_t _apu_t0 = esp_timer_get_time(); \
    if (IAPU.APUExecuting) \
        while (APU.Cycles <= CPU.Cycles) \
            APUExecute(); \
    g_apu_us += esp_timer_get_time() - _apu_t0; \
    g_apu_calls++; \
} while (0)
#else
#define APU_EXECUTE1() \
APUExecute();

#define APU_EXECUTE() \
if (IAPU.APUExecuting) \
    while (APU.Cycles <= CPU.Cycles) \
      APUExecute();
#endif

#endif
#endif
