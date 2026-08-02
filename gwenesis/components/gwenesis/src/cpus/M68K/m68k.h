#ifndef M68K__HEADER
#define M68K__HEADER

/* ======================================================================== */
/* ========================= LICENSING & COPYRIGHT ======================== */
/* ======================================================================== */
/*
 *                                  MUSASHI
 *                                Version 3.32
 *
 * A portable Motorola M680x0 processor emulation engine.
 * Copyright Karl Stenerud.  All rights reserved.
 *
 * This code may be freely used for non-commercial purposes as long as this
 * copyright notice remains unaltered in the source code and any binary files
 * containing this code in compiled form.
 *
 * All other licensing terms must be negotiated with the author
 * (Karl Stenerud).
 *
 * The latest version of this code can be obtained at:
 * http://kstenerud.cjb.net
 */

 /* Modified by Eke-Eke for Genesis Plus GX:

    - removed unused stuff to reduce memory usage / optimize execution (multiple CPU types support, NMI support, ...)
    - moved stuff to compile statically in a single object file
    - implemented support for global cycle count (shared by 68k & Z80 CPU)
    - added support for interrupt latency (Sesame's Street Counting Cafe, Fatal Rewind)
    - added proper cycle use on reset
    - added cycle accurate timings for MUL/DIV instructions (thanks to Jorge Cwik !) 
    - fixed undocumented flags for DIV instructions (Blood Shot)
    - fixed undocumented behaviors for ABCD/SBCD/NBCD instructions (thanks to flamewing for his test ROM)
    - improved auto-vectored interrupts acknowledge cycle timing accuracy
    - added MAIN-CPU & SUB-CPU support for Mega CD emulation
    
  */

/* ======================================================================== */
/* ================================ INCLUDES ============================== */
/* ======================================================================== */

#include <setjmp.h>
#include "macros.h"
#ifdef HOOK_CPU
#include "cpuhook.h"
#endif

/* ======================================================================== */
/* ==================== ARCHITECTURE-DEPENDANT DEFINES ==================== */
/* ======================================================================== */

/* Check for > 32bit sizes */
#if UINT_MAX > 0xffffffff
  #define M68K_INT_GT_32_BIT  1
#else
  #define M68K_INT_GT_32_BIT  0
#endif

/* Data types used in this emulation core */
#undef sint8
#undef sint16
#undef sint32
#undef sint64
#undef uint8
#undef uint16
#undef uint32
#undef uint64
#undef sint
#undef uint

#define sint8  signed   char      /* ASG: changed from char to signed char */
#define sint16 signed   short
#define sint32 signed   int      /* AWJ: changed from long to int */
#define uint8  unsigned char
#define uint16 unsigned short
#define uint32 unsigned int      /* AWJ: changed from long to int */

/* signed and unsigned int must be at least 32 bits wide */
#define sint   signed   int
#define uint   unsigned int


#if M68K_USE_64_BIT
#define sint64 signed   long long
#define uint64 unsigned long long
#else
#define sint64 sint32
#define uint64 uint32
#endif /* M68K_USE_64_BIT */



/* Allow for architectures that don't have 8-bit sizes */
/*#if UCHAR_MAX == 0xff*/
  #define MAKE_INT_8(A) (sint8)(A)
/*#else
  #undef  sint8
  #define sint8  signed   int
  #undef  uint8
  #define uint8  unsigned int
  INLINE sint MAKE_INT_8(uint value)
  {
    return (value & 0x80) ? value | ~0xff : value & 0xff;
  }*/
/*#endif *//* UCHAR_MAX == 0xff */


/* Allow for architectures that don't have 16-bit sizes */
/*#if USHRT_MAX == 0xffff*/
  #define MAKE_INT_16(A) (sint16)(A)
/*#else
  #undef  sint16
  #define sint16 signed   int
  #undef  uint16
  #define uint16 unsigned int
  INLINE sint MAKE_INT_16(uint value)
  {
    return (value & 0x8000) ? value | ~0xffff : value & 0xffff;
  }*/
/*#endif *//* USHRT_MAX == 0xffff */


/* Allow for architectures that don't have 32-bit sizes */
/*#if UINT_MAX == 0xffffffff*/
  #define MAKE_INT_32(A) (sint32)(A)
/*#else
  #undef  sint32
  #define sint32  signed   int
  #undef  uint32
  #define uint32  unsigned int
  INLINE sint MAKE_INT_32(uint value)
  {
    return (value & 0x80000000) ? value | ~0xffffffff : value & 0xffffffff;
  }*/
/*#endif *//* UINT_MAX == 0xffffffff */



/* ======================================================================== */
/* ============================ GENERAL DEFINES =========================== */
/*** BZHXX ***/
#ifndef FALSE
#define FALSE 0
#define TRUE 1
#endif

#define ROM_SWAP
#define RAM_SWAP

// 16/32 bits acces to RAM/ROM

extern unsigned char *ROM_DATA;
extern unsigned char M68K_RAM[];
/* Next-power-of-two-minus-one bound on the loaded cartridge size, computed
 * once in load_cartridge() (gwenesis_bus.c). See its declaration there for
 * why: without it, FETCHnROM indexes ROM_DATA (a plain pointer to whatever
 * few-hundred-KB buffer this specific ROM's file read allocated) directly
 * by the full 0..MAX_ROM_SIZE (8MB) nominal ROM window, which reads far
 * past the actual allocation for any ROM smaller than 8MB. */
extern unsigned int gwenesis_rom_mask;

#define FETCH8ROM(A) ((ROM_DATA[(((A) & gwenesis_rom_mask) ^ 1)]))
#define FETCH16ROM(A) ((*(unsigned short *)&ROM_DATA[(A) & gwenesis_rom_mask]))
#define FETCH32ROM(A) ( (*(unsigned int *)&ROM_DATA[(A) & gwenesis_rom_mask] << 16) | (*(unsigned int *)&ROM_DATA[(A) & gwenesis_rom_mask] >> 16) )

#define FETCH8RAM(A) ((M68K_RAM[(A ^ 1) & 0xFFFF]))
#define FETCH16RAM(A) ((*(unsigned short *)&M68K_RAM[(A)&0XFFFF]))
#define FETCH32RAM(A) ( (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] << 16) | (*(unsigned int *)&M68K_RAM[(A&0XFFFF)] >> 16) )

#define WRITE8RAM(A, V) (M68K_RAM[(A ^ 1) & 0xFFFF] = (V))
#define WRITE16RAM(A, V) ((*(unsigned short *)&M68K_RAM[(A)&0XFFFF] = (V)))
#define WRITE32RAM(A, V) ((*(unsigned int *)&M68K_RAM[(A)&0XFFFF] =( ((V) << 16) | ((V) >> 16) ) ))

/* Read from anywhere */
unsigned int  m68k_read_memory_8(unsigned int address);
unsigned int  m68k_read_memory_16(unsigned int address);
unsigned int  m68k_read_memory_32(unsigned int address);

/* m68k_read_immediate_16/32 fetch the opcode/extension-word stream at
 * REG_PC -- called on every single instruction dispatch
 * (m68kcpu.c:307,313) and for PC-relative extension words. The versions
 * that used to be here tested only bit 23 (`& 0x800000`) to pick ROM vs
 * RAM and never masked the address to the 68000's real 24-bit bus
 * (0xFFFFFF -- the same CPU_ADDRESS_MASK the general-purpose
 * m68ki_read_8/16/32 in m68kcpu.h already apply via ADDRESS_68K(), just
 * not here). A guest PC that goes wild -- from any upstream bug -- was
 * therefore not wrapped into an in-range access the way real 68000
 * silicon (24 address pins) or m68ki_read_16/32 (data reads) already do;
 * it walked off ROM_DATA's host pointer by the raw, unmasked guest
 * address. Measured 2026-08-02: PC=0x21000002, ROM_DATA=0x480d247c, fault
 * address exactly ROM_DATA+PC=0x690d247c -- outside the 32MB PSRAM window
 * entirely. Masking to 24 bits turns that PC into 0x000002, a normal ROM
 * offset. This does not fix whatever put PC there; it stops an emulated
 * wild jump from becoming a host segfault, matching what an emulated bus
 * error should look like instead.
 * Routing mirrors m68ki_read_16/32: below 0x800000 is ROM, at/above
 * 0xFF0000 is RAM, everything between (I/O, VDP, banking registers, that
 * middle region) goes through the general memory-mapped dispatch instead
 * of being silently treated as ROM. */
static inline unsigned int m68k_read_immediate_16(unsigned int address)
{
  /* Diagnostic for the wild-jump question this masking fix papers over:
   * keep the last few opcode-fetch PCs in a ring buffer, and the first
   * time one lands somewhere a 68000 program can never legitimately be
   * executing from, dump that history once.
   *
   * Only ROM (< 0x800000) and work RAM (0xFF0000-0xFFFFFF) are real,
   * executable memory on a Mega Drive. The first version of this check
   * flagged any raw address that needed its top byte masked off, which
   * false-positived constantly: games routinely relocate hot code into
   * RAM and jump to it, and that target is commonly computed through a
   * sign-extended 16-bit value, so the *raw* register legitimately reads
   * like 0xFFxxxxxx. Measured 2026-08-02: PC=0xfffffc1a masks to
   * 0xfffc1a, squarely inside RAM -- not wild, just ordinary in-RAM
   * execution, and the old check reported it anyway. What's actually
   * nonsense is a *masked* PC landing in what's left (0x800000-0xFEFFFF):
   * VDP/sound/bank registers and expansion space, none of which is
   * executable on real hardware either -- that's the only range this
   * checks now. (Compare the incident this diagnostic was built for:
   * PC=0x21000002 masked to a plausible-looking ROM offset by
   * coincidence; the real bug there was upstream -- an unstripped copier
   * header, fixed separately in load_cartridge() -- not something this
   * narrower check would have needed to catch.) */
  static unsigned int history[8];
  static unsigned int idx = 0;
  unsigned int masked = address & 0xFFFFFF;
  if (masked >= 0x800000 && masked < 0xFF0000) {
    static int reported = 0;
    if (!reported) {
      extern int scan_line;
      reported = 1;
      printf("m68k_read_immediate_16: wild PC 0x%08x (masked 0x%06x, in VDP/IO/bank space) at scan_line %d, recent PCs:",
             address, masked, scan_line);
      for (unsigned int i = 0; i < 8; i++)
        printf(" 0x%08x", history[(idx + i) & 7]);
      printf("\n");
    }
  }
  history[idx & 7] = address;
  idx++;
  if (masked < 0x800000) return FETCH16ROM(masked);
  if (masked >= 0xFF0000) return FETCH16RAM(masked);
  return m68k_read_memory_16(masked);
}
static inline unsigned int m68k_read_immediate_32(unsigned int address)
{
  address &= 0xFFFFFF;
  if (address < 0x800000) return FETCH32ROM(address);
  if (address >= 0xFF0000) return FETCH32RAM(address);
  return m68k_read_memory_32(address);
}

static inline unsigned int m68k_read_pcrelative_8(unsigned int address)
{
  address &= 0xFFFFFF;
  if (address < 0x800000) return FETCH8ROM(address);
  if (address >= 0xFF0000) return FETCH8RAM(address);
  return m68k_read_memory_8(address);
}
static inline unsigned int m68k_read_pcrelative_16(unsigned int address)
{
  return m68k_read_immediate_16(address);
}
static inline unsigned int m68k_read_pcrelative_32(unsigned int address)
{
  return m68k_read_immediate_32(address);
}

/* Memory access for the disassembler */
unsigned int m68k_read_disassembler_8  (unsigned int address);
unsigned int m68k_read_disassembler_16 (unsigned int address);
unsigned int m68k_read_disassembler_32 (unsigned int address);

/* Write to anywhere */
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);

/*** BZHXX ***/
/* ======================================================================== */

/* There are 7 levels of interrupt to the 68K.
 * A transition from < 7 to 7 will cause a non-maskable interrupt (NMI).
 */
#define M68K_IRQ_NONE 0
#define M68K_IRQ_1    1
#define M68K_IRQ_2    2
#define M68K_IRQ_3    3
#define M68K_IRQ_4    4
#define M68K_IRQ_5    5
#define M68K_IRQ_6    6
#define M68K_IRQ_7    7


/* Special interrupt acknowledge values.
 * Use these as special returns from the interrupt acknowledge callback
 * (specified later in this header).
 */

/* Causes an interrupt autovector (0x18 + interrupt level) to be taken.
 * This happens in a real 68K if VPA or AVEC is asserted during an interrupt
 * acknowledge cycle instead of DTACK.
 */
#define M68K_INT_ACK_AUTOVECTOR    0xffffffff

/* Causes the spurious interrupt vector (0x18) to be taken
 * This happens in a real 68K if BERR is asserted during the interrupt
 * acknowledge cycle (i.e. no devices responded to the acknowledge).
 */
#define M68K_INT_ACK_SPURIOUS      0xfffffffe


/* Registers used by m68k_get_reg() and m68k_set_reg() */
typedef enum
{
  /* Real registers */
  M68K_REG_D0,    /* Data registers */
  M68K_REG_D1,
  M68K_REG_D2,
  M68K_REG_D3,
  M68K_REG_D4,
  M68K_REG_D5,
  M68K_REG_D6,
  M68K_REG_D7,
  M68K_REG_A0,    /* Address registers */
  M68K_REG_A1,
  M68K_REG_A2,
  M68K_REG_A3,
  M68K_REG_A4,
  M68K_REG_A5,
  M68K_REG_A6,
  M68K_REG_A7,
  M68K_REG_PC,    /* Program Counter */
  M68K_REG_SR,    /* Status Register */
  M68K_REG_SP,    /* The current Stack Pointer (located in A7) */
  M68K_REG_USP,   /* User Stack Pointer */
  M68K_REG_ISP,   /* Interrupt Stack Pointer */

#if M68K_EMULATE_PREFETCH
  /* Assumed registers */
  /* These are cheat registers which emulate the 1-longword prefetch
   * present in the 68000 and 68010.
   */
  M68K_REG_PREF_ADDR,  /* Last prefetch address */
  M68K_REG_PREF_DATA,  /* Last prefetch data */
#endif

  /* Convenience registers */
  M68K_REG_IR    /* Instruction register */
} m68k_register_t;


/* 68k memory map structure */
typedef struct 
{
  unsigned char *base;                             /* memory-based access (ROM, RAM) */
  unsigned int (*read8)(unsigned int address);               /* I/O byte read access */
  unsigned int (*read16)(unsigned int address);              /* I/O word read access */
  void (*write8)(unsigned int address, unsigned int data);  /* I/O byte write access */
  void (*write16)(unsigned int address, unsigned int data); /* I/O word write access */
} cpu_memory_map;

/* 68k idle loop detection */
typedef struct
{
  uint pc;
  uint cycle;
  uint detected;
} cpu_idle_t;

typedef struct
{
  cpu_memory_map memory_map[256]; /* memory mapping */

  cpu_idle_t poll;      /* polling detection */

  uint cycles;          /* current master cycle count */ 
  uint cycle_end;       /* aimed master cycle count for current execution frame */

  uint dar[16];         /* Data and Address Registers */
  uint pc;              /* Program Counter */
  uint sp[5];           /* User and Interrupt Stack Pointers */
  uint ir;              /* Instruction Register */
  uint t1_flag;         /* Trace 1 */
  uint s_flag;          /* Supervisor */
  uint x_flag;          /* Extend */
  uint n_flag;          /* Negative */
  uint not_z_flag;      /* Zero, inverted for speedups */
  uint v_flag;          /* Overflow */
  uint c_flag;          /* Carry */
  uint int_mask;        /* I0-I2 */
  uint int_level;       /* State of interrupt pins IPL0-IPL2 -- ASG: changed from ints_pending */
  uint stopped;         /* Stopped state */

  uint pref_addr;       /* Last prefetch address */
  uint pref_data;       /* Data in the prefetch queue */

  uint instr_mode;      /* Stores whether we are in instruction mode or group 0/1 exception mode */
  uint run_mode;        /* Stores whether we are processing a reset, bus error, address error, or something else */
  uint aerr_enabled;    /* Enables/deisables address error checks at runtime */
  jmp_buf aerr_trap;    /* Address error jump */
  uint aerr_address;    /* Address error location */
  uint aerr_write_mode; /* Address error write mode */
  uint aerr_fc;         /* Address error FC code */

  uint tracing;         /* Tracing enable flag */

  uint address_space;   /* Current FC code */

#ifdef M68K_OVERCLOCK_SHIFT
  int cycle_ratio;
#endif

  /* Callbacks to host */
  int  (*int_ack_callback)(int int_line);           /* Interrupt Acknowledge */
  void (*reset_instr_callback)(void);               /* Called when a RESET instruction is encountered */
  int  (*tas_instr_callback)(void);                 /* Called when a TAS instruction is encountered, allows / disallows writeback */
  void (*set_fc_callback)(unsigned int new_fc);     /* Called when the CPU function code changes */
} m68ki_cpu_core;

/* CPU cores */
extern m68ki_cpu_core m68k;

/* ======================================================================== */
/* ============================== CALLBACKS =============================== */
/* ======================================================================== */

/* These functions allow you to set callbacks to the host when specific events
 * occur.  Note that you must enable the corresponding value in m68kconf.h
 * in order for these to do anything useful.
 * Note: I have defined default callbacks which are used if you have enabled
 * the corresponding #define in m68kconf.h but either haven't assigned a
 * callback or have assigned a callback of NULL.
 */

#if M68K_EMULATE_INT_ACK == OPT_ON
/* Set the callback for an interrupt acknowledge.
 * You must enable M68K_EMULATE_INT_ACK in m68kconf.h.
 * The CPU will call the callback with the interrupt level being acknowledged.
 * The host program must return either a vector from 0x02-0xff, or one of the
 * special interrupt acknowledge values specified earlier in this header.
 * If this is not implemented, the CPU will always assume an autovectored
 * interrupt, and will automatically clear the interrupt request when it
 * services the interrupt.
 * Default behavior: return M68K_INT_ACK_AUTOVECTOR.
 */
void m68k_set_int_ack_callback(int  (*callback)(int int_level));
#endif

#if M68K_EMULATE_RESET == OPT_ON
/* Set the callback for the RESET instruction.
 * You must enable M68K_EMULATE_RESET in m68kconf.h.
 * The CPU calls this callback every time it encounters a RESET instruction.
 * Default behavior: do nothing.
 */
void m68k_set_reset_instr_callback(void  (*callback)(void));
#endif

#if M68K_TAS_HAS_CALLBACK == OPT_ON
/* Set the callback for the TAS instruction.
 * You must enable M68K_TAS_HAS_CALLBACK in m68kconf.h.
 * The CPU calls this callback every time it encounters a TAS instruction.
 * Default behavior: return 1, allow writeback.
 */
void m68k_set_tas_instr_callback(int  (*callback)(void));
#endif

#if M68K_EMULATE_FC == OPT_ON
/* Set the callback for CPU function code changes.
 * You must enable M68K_EMULATE_FC in m68kconf.h.
 * The CPU calls this callback with the function code before every memory
 * access to set the CPU's function code according to what kind of memory
 * access it is (supervisor/user, program/data and such).
 * Default behavior: do nothing.
 */
void m68k_set_fc_callback(void  (*callback)(unsigned int new_fc));
#endif


/* ======================================================================== */
/* ====================== FUNCTIONS TO ACCESS THE CPU ===================== */
/* ======================================================================== */

/* Do whatever initialisations the core requires.  Should be called
 * at least once at init time.
 */
extern void m68k_init(void);

/* Pulse the RESET pin on the CPU.
 * You *MUST* reset the CPU at least once to initialize the emulation
 */
extern void m68k_pulse_reset(void);

/* Run until given cycle count is reached */
extern void m68k_run(unsigned int cycles);

/* Get current instruction execution time */
extern int m68k_cycles(void);

/* Number of cycles run so far from start of frame */
extern int m68k_cycles_master(void);

/* Number of cycles run so far from run call */
extern int m68k_cycles_run(void);

/* Set the IPL0-IPL2 pins on the CPU (IRQ).
 * A transition from < 7 to 7 will cause a non-maskable interrupt (NMI).
 * Setting IRQ to 0 will clear an interrupt request.
 */
extern void m68k_set_irq(unsigned int int_level);
extern void m68k_set_irq_delay(unsigned int int_level);
extern void m68k_update_irq(unsigned int mask);

/* Halt the CPU as if you pulsed the HALT pin. */
extern void m68k_pulse_halt(void);
extern void m68k_clear_halt(void);

/* Peek at the internals of a CPU context.  This can either be a context
 * retrieved using m68k_get_context() or the currently running context.
 * If context is NULL, the currently running CPU context will be used.
 */
extern unsigned int m68k_get_reg(m68k_register_t reg);

/* Poke values into the internals of the currently running CPU context */
extern void m68k_set_reg(m68k_register_t reg, unsigned int value);

/* Load/Save state of CPU */
extern void gwenesis_m68k_save_state();
extern void gwenesis_m68k_load_state();

/* ======================================================================== */
/* ============================== END OF FILE ============================= */
/* ======================================================================== */

#endif /* M68K__HEADER */
