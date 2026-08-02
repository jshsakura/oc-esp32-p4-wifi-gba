/*
Gwenesis : Genesis & megadrive Emulator.

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version.
This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
this program. If not, see <http://www.gnu.org/licenses/>.

__author__ = "bzhxx"
__contact__ = "https://github.com/bzhxx"
__license__ = "GPLv3"

*/
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>

#include "m68k.h"

#include "ym2612.h"
#include "z80inst.h"
#include "gwenesis_bus.h"
#include "gwenesis_io.h"
#include "gwenesis_vdp.h"
#include "gwenesis_sn76489.h"
#include "gwenesis_savestate.h"

#define BUS_DISABLE_LOGGING 1

#if !BUS_DISABLE_LOGGING
#include <stdarg.h>
void bus_log(const char *subs, const char *fmt, ...) {
  extern int frame_counter;
  extern int scan_line;

  va_list va;

  printf("%06d:%03d :[%s] vc:%04x hc:%04x hv:%04x ", frame_counter, scan_line, subs,gwenesis_vdp_vcounter(),gwenesis_vdp_hcounter(),gwenesis_vdp_hvcounter());

  va_start(va, fmt);
  vfprintf(stdout, fmt, va);
  va_end(va);
  printf("\n");
}
#else
	#define bus_log(...)  do {} while(0)
#endif

// Setup M68k memories ROM & RAM
unsigned char *ROM_DATA; // 68K Main Program (uncompressed)
// FETCH8ROM/16ROM/32ROM (m68k.h) index ROM_DATA directly by the guest 68000
// address with no bounds check against how big the actual cartridge is --
// only against MAX_ROM_SIZE (8MB), the nominal size of the whole ROM address
// window. ROM_DATA itself is a plain pointer to whatever main.c's file-read
// buffer was for *this* ROM (routinely a few hundred KB, e.g. 256KB), so any
// address between the real cartridge size and 8MB reads past the allocation.
// gwenesis_rom_mask bounds that to the next power of two at or above the
// loaded size (computed once in load_cartridge, applied as a single AND on
// every ROM fetch -- cheap, unlike a modulo, and this macro sits on the
// hottest possible path: every M68K instruction fetch goes through it).
// This does not perfectly reproduce real hardware ROM mirroring (which
// wraps within the cartridge's own size, not the next power of two), but it
// keeps every access inside memory that was actually allocated for the ROM
// buffer or its 64KB-rounded padding (rg_storage_read_file, RG_FILE_ALIGN_64KB),
// which is what matters for not walking off into unrelated PSRAM.
unsigned int gwenesis_rom_mask = 0xFFFFFFFFu;
unsigned char M68K_RAM[MAX_RAM_SIZE];    // 68K RAM

// Setup Z80 Memory
unsigned char ZRAM[MAX_Z80_RAM_SIZE]; // Z80 RAM
unsigned char TMSS[0x4];
extern unsigned short gwenesis_vdp_status;

// TMSS
int tmss_state = 0;
int tmss_count = 0;

/******************************************************************************
 *
 *   Load a Sega Genesis Cartridge into CPU Memory
 *
 ******************************************************************************/

static int gwenesis_is_pow2(size_t x)
{
    return x != 0 && (x & (x - 1)) == 0;
}

// Copier-header ("SMD") detection. Real cartridge dumps are a power of two
// (or at least don't carry copier metadata); files produced by cartridge
// copiers like the Super Magic Drive prepend a 512-byte header. That header
// pushes every fixed offset in the image -- the 68000 reset vector at 0x4,
// the "SEGA" signature at 0x100, the region bytes set_region() reads at
// 0x1F0 -- forward by 512 bytes, so the core loads and executes whatever
// garbage happens to sit at the *wrong* offset instead. That is not a
// hypothetical: measured 2026-08-02, a 262656-byte test ROM (262144 + 512)
// was fed in raw. The 68000's first-ever instruction fetch (scan_line 0,
// empty PC history -- i.e. state was never valid to begin with, nothing
// "corrupted" it) read PC=0x20e99020 from what should have been the reset
// vector, and gwenesis_bus_map_z80_address's assert caught the fallout a
// few lines later. The apparent "3-4 fps" and the older "15.6 fps" figures
// were both this: the core faithfully running noise, not a performance bug.
//
// True SMD files are also block-interleaved: after the header, ROM data is
// split into 16KB blocks, each stored as its even bytes (first 8KB) then
// its odd bytes (second 8KB) rather than in normal byte order, and the
// header itself flags this (byte[1]==0x03, byte[8..9]==0xAA,0xBB -- see
// https://github.com/franckverrot/EmulationResources/blob/master/consoles/megadrive/genesis_rom.txt).
// A header-sized file that does *not* carry that flag combination is just
// offset, not interleaved -- skipping the 512 bytes is correct and
// de-interleaving it would scramble a file that was otherwise fine.
static int gwenesis_detect_copier_header(unsigned char *buffer, size_t size)
{
    if (gwenesis_is_pow2(size))
        return 0; // Raw dump, no header.
    if (size > 512 && gwenesis_is_pow2(size - 512))
        return 1; // Header-sized remainder is a clean power of two.
    return 0; // Doesn't fit either shape; leave it alone rather than guess.
}

// Sanity check run *after* any header has been stripped/de-interleaved, so
// a file whose header this heuristic mis-detects (or that was never a Mega
// Drive ROM at all) is refused instead of silently executed. "SEGA" at
// 0x100 is present in effectively every official and homebrew header
// (SEGA GENESIS / SEGA MEGA DRIVE / SEGA 32X / SEGA PICO all start with
// it); the reset vector at 0x4 must be an even address inside the
// cartridge, since real 68000 silicon takes an address error on an odd
// PC and gwenesis doesn't emulate holding an unmapped one gracefully
// everywhere yet (see m68k_read_immediate_16's wild-PC guard in m68k.h,
// which is the safety net for anything that gets past this check wrong).
//
// Reads go through FETCH8ROM, not a raw ROM_DATA index, to match how
// set_region() (and every opcode/operand fetch) actually reads this
// buffer: ROM_SWAP unconditionally swaps every adjacent byte pair in
// ROM_DATA, and FETCH8ROM's `^1` undoes that per access to hand back the
// logical (pre-swap) byte. Indexing ROM_DATA directly here would check
// the signature and reset vector at the wrong byte order and reject
// every valid ROM.
static int gwenesis_rom_looks_valid(size_t data_size)
{
    if (data_size < 0x200)
        return 0;
    if (FETCH8ROM(0x100) != 'S' || FETCH8ROM(0x101) != 'E' ||
        FETCH8ROM(0x102) != 'G' || FETCH8ROM(0x103) != 'A')
        return 0;
    unsigned int reset_vector = ((unsigned int)FETCH8ROM(4) << 24) | ((unsigned int)FETCH8ROM(5) << 16) |
                                 ((unsigned int)FETCH8ROM(6) << 8) | (unsigned int)FETCH8ROM(7);
    if (reset_vector & 1)
        return 0;
    if (reset_vector >= data_size)
        return 0;
    return 1;
}

// Returns 1 on a ROM that looks like a real Mega Drive cartridge and is
// ready to run, 0 if it should be refused (caller must not proceed to
// power_on()/reset_emulation() in that case -- see main.c, which turns a 0
// here into rg_system_rom_load_failed() instead of booting).
int load_cartridge(unsigned char *buffer, size_t size)
{
    // Clear all volatile memory
    memset(M68K_RAM, 0, MAX_RAM_SIZE);
    memset(ZRAM, 0, MAX_Z80_RAM_SIZE);

    // Set Z80 Memory as ZRAM
    z80_set_memory(ZRAM);
    z80_pulse_reset();

    // Copy file contents to CPU ROM memory
    ROM_DATA = buffer;
    size_t data_size = size;

    if (gwenesis_detect_copier_header(buffer, size))
    {
      // https://github.com/franckverrot/EmulationResources/blob/master/consoles/megadrive/genesis_rom.txt
      if (buffer[1] == 0x03 && buffer[8] == 0xAA && buffer[9] == 0xBB)
      {
        printf("--Copier header detected, SMD-interleaved, de-interleaving--\n");
        memmove(ROM_DATA, ROM_DATA + 512, size - 512);
        data_size = size - 512;
        uint8 *temp = malloc(0x4000);
        for (size_t i = 0; i < data_size; i += 0x4000)
        {
          memcpy(temp, ROM_DATA + i, 0x4000);
          for (size_t j = 0; j < 0x2000; ++j)
          {
            ROM_DATA[i + (j * 2) + 0] = temp[0x2000 + j];
            ROM_DATA[i + (j * 2) + 1] = temp[0x0000 + j];
          }
        }
        free(temp);
      }
      else
      {
        printf("--Copier header detected (512 bytes), not interleaved, skipping--\n");
        memmove(ROM_DATA, ROM_DATA + 512, size - 512);
        data_size = size - 512;
      }
    }

    // Next power of two >= data_size, minus 1, e.g. a 256KB (0x40000) ROM
    // stays 0x3FFFF. Computed from data_size (post header-strip), not the
    // raw file size -- see gwenesis_rom_mask's declaration above.
    {
      unsigned int rounded = 1;
      while (rounded < data_size)
        rounded <<= 1;
      gwenesis_rom_mask = rounded - 1;
    }

    #ifdef ROM_SWAP
    bus_log(__FUNCTION__,"--ROM swap mode--");
    for (size_t i=0; i < data_size;i+=2 )
    {
        char z = ROM_DATA[i];
        ROM_DATA[i]=ROM_DATA[i+1];
        ROM_DATA[i+1]=z;
    }
    #endif

    // TEMP probe: what is actually in this file? Printed before the verdict so a refusal
    // still tells us the format rather than only that it was wrong.
    {
        printf("ROM probe: size=%u  head:", (unsigned)data_size);
        for (int i = 0; i < 16; ++i) printf(" %02x", ((unsigned char *)ROM_DATA)[i]);
        printf("\n  at 0x100:");
        for (int i = 0x100; i < 0x110; ++i) printf(" %02x", ((unsigned char *)ROM_DATA)[i]);
        printf("\n  ascii 0x100:");
        for (int i = 0x100; i < 0x120; ++i) {
            unsigned char c = ((unsigned char *)ROM_DATA)[i];
            printf("%c", (c >= 32 && c < 127) ? c : '.');
        }
        printf("\n");
    }

    if (!gwenesis_rom_looks_valid(data_size))
    {
      printf("load_cartridge: ROM does not look like a Mega Drive cartridge "
             "after header handling (no SEGA signature at 0x100, or reset "
             "vector out of range/odd) -- refusing to run it.\n");
      return 0;
    }

    set_region();
    return 1;
}

/******************************************************************************
 *
 *   Power ON the CPU
 *   Initialize 68K, Z80 and YM2612 Cores
 *
 ******************************************************************************/
void power_on() {
  // Set M68K CPU as original MOTOROLA 68000
  //m68k_set_cpu_type(M68K_CPU_TYPE_68000);
  // Initialize M68K CPU
  m68k_init();
  // Initialize Z80 CPU
  z80_start();
  // Initialize YM2612 chip
  YM2612Init();
  YM2612Config(9);
  // Initialize PSG SN76489 chip
  //CLOCK_NTSC      = 3579545,
  //CLOCK_PAL       = 3546895,
 // CLOCK_NTSC_SMS1 = 3579527

//  if (mode_pal) {
//     gwenesis_SN76489_Init(3546895, GWENESIS_AUDIO_BUFFER_LENGTH_PAL*50,AUDIO_FREQ_DIVISOR);
//   } else{
//     gwenesis_SN76489_Init(3579545, GWENESIS_AUDIO_BUFFER_LENGTH_NTSC*60,AUDIO_FREQ_DIVISOR);
//   }
  
  gwenesis_SN76489_Init(3579545, 888*60,AUDIO_FREQ_DIVISOR);

}

/******************************************************************************
 *
 *   Reset the CPU Emulation
 *   Send a pulse reset to 68K, Z80 and YM2612 Cores
 *
 ******************************************************************************/
void reset_emulation() {
  // Send a reset pulse to Z80 CPU
  z80_pulse_reset();
  // Send a reset pulse to Z80 M68K
  m68k_pulse_reset();
  // Send a reset pulse to YM2612 chip
  YM2612ResetChip();
  // Send a reset pulse to SEGA 315-5313 chip
  gwenesis_vdp_reset();
  gwenesis_SN76489_Reset();
}

/******************************************************************************
 *
 *   Set Region
 *   Look at ROM to set console compatible region
 *
 ******************************************************************************/
void set_region()
{    
  /*
    old style : JUE characters
    J : Domestic 60Hz (Asia)
    U : Oversea  60Hz (USA) 
    E : Oversea  50Hz (Europe) 

    new style : 1st character
    bit 0 : +1 Domestic 60Hz (Asia)
    bit 1 : +2 Domestc  50Hz (Asia)
    bit 2:  +4 Oversea  60Hz (USA) 
    bit 3:  +4 Oversea  50Hz (Europe) 
  */

   // extern int mode_pal;

    int country = 0;

    char rom_str[3];

    printf("ROM game  : ");
    for (int j=0; j < 48;j++) printf("%c",(char)FETCH8ROM(0x150+j));
    printf("\n");

    rom_str[0]=FETCH8ROM(0x1F0);
    rom_str[1]=FETCH8ROM(0x1F1);
    rom_str[2]=FETCH8ROM(0x1F2);

    printf("ROM region:%c%c%c (0x%02x 0x%02x 0x%02x)\n", rom_str[0],rom_str[1],rom_str[2],rom_str[0],rom_str[1],rom_str[2]);

    /* from Gens */
    if (!memcmp(rom_str, "eur", 3)) country |= 8;
    else if (!memcmp(rom_str, "EUR", 3)) country |= 8;
    else if (!memcmp(rom_str, "Europe", 3)) country |= 8;
    else if (!memcmp(rom_str, "jap", 3)) country |= 1;
    else if (!memcmp(rom_str, "JAP", 3)) country |= 1;
    else if (!memcmp(rom_str, "usa", 3)) country |= 4;
    else if (!memcmp(rom_str, "USA", 3)) country |= 4;
    else
    {
      int i;
      unsigned char c;

      /* look for each characters */
      for(i = 0; i < 3; i++)
      {
        c = rom_str[i];

        if (c == 'U') country |= 4;
        else if (c == 'E' || c == 'e' ) country |= 8;
        else if (c == 'J' || c == 'j' ) country |= 1;
        else if (c == 'K' || c == 'k' ) country |= 1;
        else if (c < 16) country |= c;
        else if ((c >= '0') && (c <= '9')) country |= c - '0';
        else if ((c >= 'A') && (c <= 'F')) country |= c - 'A' + 10;
      }
    }
    printf("country code=%01x : ",country);
      /* set default console region (USA > EUROPE > JAPAN) */
      /*
      IO REG0	:	MODE 	VMOD 	DISK 	RSV 	VER3 	VER2 	VER1 	VER0
      MODE (R) 	0: Domestic Model
  	            1: Overseas Model
      VMOD (R) 	0: NTSC CPU clock 7.67 MHz
  	            1: PAL CPU clock 7.60 MHz
      */

    /* USA 60Hz*/
    if (country & 4){
      printf("Oversea-NTSC USA 60Hz\n");
      gwenesis_io_set_reg(0, 0x81);
   //   gwenesis_vdp_status &= 0xFFFE;
     // mode_pal = 0;
      return;
    }
    /* EUROPE 50Hz */
    if (country & 8){
      printf("Oversea-PAL Europe 50Hz\n");
      gwenesis_io_set_reg(0, 0xC1);
    //  gwenesis_vdp_status |= 0x1;
      //mode_pal = 1;
      return;
    }
    /* set Asia 60HZ */
    if (country & 1){
      printf("Domestic-NTSC Asia 60Hz\n");
      gwenesis_io_set_reg(0, 0x1);
    //  gwenesis_vdp_status &= 0xFFFE;
      //mode_pal = 0;
      return;
    }
      printf("Oversea-NTSC USA 60Hz no detection>> default mode\n");
      gwenesis_io_set_reg(0, 0x81);
     // gwenesis_vdp_status &= 0xFFFE;
     // mode_pal = 0;

}
/******************************************************************************
 *
 *   Main memory address mapper
 *   Map all main memory region address for CPU program
 *   68K Access to Z80 Memory
 *
 ******************************************************************************/
static inline unsigned int gwenesis_bus_map_z80_address(unsigned int address) {

  unsigned int range = (address & 0xF000);
  switch (range) {
  case 0:
  case 0x1000:
    return Z80_RAM_ADDR;
  case 0x2000:
  case 0x3000:
    return Z80_RAM_ADDR1K;
  case 0x4000:
    return Z80_YM2612_ADDR;
  case 0x6000:
    return Z80_BANK_ADDR;
  case 0x7000:
    return Z80_SN76489_ADDR;
  default:
    bus_log(__FUNCTION__,"no map Z80 %x",address);
    assert(0);
    return NONE;
  }
}

/******************************************************************************
 *
 *   IO memory address mapper
 *   Map all input/output region address for CPU program
 *
 ******************************************************************************/
static inline unsigned int gwenesis_bus_map_io_address(unsigned int address)
{
  unsigned int range = (address & 0x1000) ;
  switch (range) {
  case 0:      return IO_CTRL;
  case 0x1000: return Z80_CTRL;
  default:
      // if (address >= 0xa14000 && address < 0xa11404)
      // return (tmss_state == 0) ? TMSS_CTRL : NONE;
      bus_log(__FUNCTION__,"no map io %x",address);

    return NONE;
  }
}

/******************************************************************************
 *
 *   Main memory address mapper
 *   Map all main memory region address for CPU program
 *
 ******************************************************************************/

static inline 
unsigned int gwenesis_bus_map_address(unsigned int address) {
  // Mask address page
  unsigned int range = (address & 0xFF0000) >> 16;

  // Check mask and select memory type
  if (range < 0x80) //        ROM ADDRESS 0x000000 - 0x3FFFFF
    return ROM_ADDR;

  else if (range == 0xA0) // Z80 ADDRESS 0xA00000 - 0xA0FFFF
    return gwenesis_bus_map_z80_address(address);


  else if (range == 0xA1) //                  IO ADDRESS  0xA10000 - 0xA1FFFF
    return gwenesis_bus_map_io_address(address);

  else if (range == 0xC0) // VDP ADDRESS 0xC00000 - 0xDFFFFFF
    return VDP_ADDR;
  else if (range == 0xFF) // RAM ADDRESS 0xE00000 - 0xFFFFFFF
    return RAM_ADDR;
  // If not a valid address return 0
  bus_log(__FUNCTION__,"M68K > ?? unnmap address %x", address);
  //assert(0);
  return NONE;
}
/******************************************************************************
 *
 *   Main read address routine
 *   Write an value to memory mapped on specified address
 *
 ******************************************************************************/
static inline unsigned int gwenesis_bus_read_memory_8(unsigned int address) {
 bus_log(__FUNCTION__,"read8  %x", address);

  switch (gwenesis_bus_map_address(address)) {
  
  case VDP_ADDR:
    return gwenesis_vdp_read_memory_8(address);

  case ROM_ADDR:
    return FETCH8ROM(address);

  case RAM_ADDR:
    return FETCH8RAM(address);

  case IO_CTRL:
    return gwenesis_io_read_ctrl(address & 0x1F);

  case Z80_CTRL:
    return z80_read_ctrl(address & 0xFFFF);

  case Z80_RAM_ADDR:
  case Z80_RAM_ADDR1K:
    return ZRAM[address & 0x1FFF];

  case Z80_YM2612_ADDR:
    return YM2612Read(m68k_cycles_master());

  case Z80_SN76489_ADDR:
    return 0xff;

  case Z80_BANK_ADDR:
    return 0xff;

  case TMSS_CTRL:
    bus_log(__FUNCTION__,"TMS");
    if (tmss_state == 0)
      return TMSS[address & 0x4];
    return 0xFF;

  default:
     bus_log(__FUNCTION__," default read 8 %x", address);
    return 0x00;
  }
  return 0x00;
}

static inline unsigned int gwenesis_bus_read_memory_16(unsigned int address) {
   bus_log(__FUNCTION__,"read16 %x", address);
   unsigned int ret_value;

  switch (gwenesis_bus_map_address(address)) {

  case VDP_ADDR:
    return gwenesis_vdp_read_memory_16(address);

  case RAM_ADDR:
    return FETCH16RAM(address);

  case ROM_ADDR:
    return FETCH16ROM(address);

  case IO_CTRL:
    return gwenesis_io_read_ctrl(address & 0x1F);

  case Z80_CTRL:
  //  ret_value = z80_read_ctrl(address & 0xFFFF); 
   // return ret_value | ret_value << 8;
    address &=0xFFFF;
        return (z80_read_ctrl(address) << 8) | z80_read_ctrl(address | 1);


  case Z80_RAM_ADDR:
  case Z80_RAM_ADDR1K:
    return ZRAM[address & 0X1FFF] | (ZRAM[address & 0X1FFF] << 8);

  case Z80_YM2612_ADDR:
    ret_value = YM2612Read(m68k_cycles_master());
    return ret_value | ret_value << 8;


  case Z80_SN76489_ADDR:
    return 0xff;

  case Z80_BANK_ADDR:
    return 0xff;

  default:
    bus_log(__FUNCTION__,"read mem 16 default %x", address);
    return (gwenesis_bus_read_memory_8(address) << 8) |
           gwenesis_bus_read_memory_8(address + 1);
  }
  return 0x00;
}

/******************************************************************************
 *
 *   Main write address routine
 *   Write an value to memory mapped on specified address
 *
 ******************************************************************************/
static inline void gwenesis_bus_write_memory_8(unsigned int address,
                                              unsigned int value) {
  bus_log(__FUNCTION__,"write8  @%x:%x", address,value);

  switch (gwenesis_bus_map_address(address)) {

  case VDP_ADDR:
    gwenesis_vdp_write_memory_16(address & ~1, (value << 8) | value);
    return;

  case RAM_ADDR:
    WRITE8RAM(address, value);
    return;

  case IO_CTRL:
    gwenesis_io_write_ctrl(address & 0x1F, value);
    return;

  case Z80_CTRL:
    z80_write_ctrl(address & 0x1FFF, value);
    return;

  case Z80_RAM_ADDR:
  case Z80_RAM_ADDR1K:
    ZRAM[address & 0x1FFF] = value;
    return;

  case Z80_YM2612_ADDR:
    bus_log(__FUNCTION__,"CPUZ80PSG8 ,m68kclk= %d", m68k_cycles_master());
    YM2612Write(address & 0x3, value & 0Xff,m68k_cycles_master());
    return;

  case Z80_SN76489_ADDR:
    bus_log(__FUNCTION__,"CPUZ80FM8  ,m68kclk= %d", m68k_cycles_master());
    gwenesis_SN76489_Write( value & 0Xff, m68k_cycles_master());
    return;

  case Z80_BANK_ADDR:
  //TODO
    return;

  case TMSS_CTRL:

    if (tmss_state == 0) {
      TMSS[address & 0x4] = value;
      tmss_count++;
      if (tmss_count == 4)
        tmss_state = 1;
    }
    return;



  default:
    //printf("write(%x, %x)\n", address, value);
    return;
  }
  return;
}

static inline void gwenesis_bus_write_memory_16(unsigned int address,
                                               unsigned int value) {
  bus_log(__FUNCTION__,"write16  @%x:%x", address,value);

  switch (gwenesis_bus_map_address(address)) {

  case VDP_ADDR:
    gwenesis_vdp_write_memory_16(address, value);
    return;

  case RAM_ADDR:
    WRITE16RAM(address, value);
    return;

  case Z80_RAM_ADDR:
  case Z80_RAM_ADDR1K:
    ZRAM[address & 0X1FFF]= value >> 8;
    return;

  case IO_CTRL:
    gwenesis_io_write_ctrl(address & 0x1F, value);
    return;

  case Z80_CTRL:
    z80_write_ctrl(address & 0xFFFF, value >> 8) ;
    return;

  case Z80_YM2612_ADDR:
    bus_log(__FUNCTION__,"CZYM16 ,mclk=%d",  m68k_cycles_master());
    YM2612Write(address & 0x3, value >> 8,m68k_cycles_master() );
    return;

  case Z80_SN76489_ADDR:
    bus_log(__FUNCTION__,"CZSN16 ,mclk=%d", m68k_cycles_master());
    gwenesis_SN76489_Write(value >> 8,m68k_cycles_master() );
    return;

  default:
    bus_log(__FUNCTION__,"write mem 16 default %x ", address);
    gwenesis_bus_write_memory_8(address, (value >> 8) & 0xff);
    gwenesis_bus_write_memory_8(address + 1, (value)&0xff);

    return;
  }
  return;
}

/******************************************************************************
 *
 *   68K CPU read address R8
 *   Read an address from memory mapped and return value as byte
 *
 ******************************************************************************/
unsigned int m68k_read_memory_8(unsigned int address)
{
      //  if ((address &  0xFF0000 ) == 0xFF0000) return FETCH8RAM(address);
    return gwenesis_bus_read_memory_8(address);
}

/******************************************************************************
 *
 *   68K CPU read address R16
 *   Read an address from memory mapped and return value as word
 *
 ******************************************************************************/
 unsigned int m68k_read_memory_16(unsigned int address)
{
     //   if ((address &  0xFF0000 ) == 0xFF0000) return FETCH16RAM(address);
    return gwenesis_bus_read_memory_16(address);
}

/******************************************************************************
 *
 *   68K CPU read address R32
 *   Read an address from memory mapped and return value as long
 *
 ******************************************************************************/
 unsigned int m68k_read_memory_32(unsigned int address)
{
  //  if ((address &  0xFF0000 ) == 0xFF0000) return FETCH32RAM(address);
    return (gwenesis_bus_read_memory_16(address) << 16) | gwenesis_bus_read_memory_16(address + 2);
}

/******************************************************************************
 *
 *   68K CPU write address W8
 *   Write an value as byte to memory mapped on specified address
 *
 ******************************************************************************/
void m68k_write_memory_8(unsigned int address, unsigned int value) {
  // if ((address & 0xFF0000) == 0xFF0000) {
  //   WRITE8RAM(address, value);
  //   return;
  // }
  gwenesis_bus_write_memory_8(address, value);
  return;
}

/******************************************************************************
 *
 *   68K CPU write address W16
 *   Write an value as word to memory mapped on specified address
 *
 ******************************************************************************/
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  // if ((address & 0xFF0000) == 0xFF0000) {
  //   WRITE16RAM(address, value);
  //   return;
  // }
  gwenesis_bus_write_memory_16(address, value);
  return;
}
/******************************************************************************
 *
 *   68K CPU write address W32
 *   Write an value as word to memory mapped on specified address
 *
 ******************************************************************************/
void m68k_write_memory_32(unsigned int address, unsigned int value) {

  // if ((address & 0xFF0000) == 0xFF0000) {
  //   WRITE32RAM(address, value);
  //   return;
  // }
  gwenesis_bus_write_memory_16(address, (value >> 16) & 0xffff);
  gwenesis_bus_write_memory_16(address + 2, (value)&0xffff);

  return;
}

unsigned int m68k_read_disassembler_16(unsigned int address)
{
    return m68k_read_memory_16(address);
}
unsigned int m68k_read_disassembler_32(unsigned int address)
{
    return m68k_read_memory_32(address);
}

void gwenesis_bus_save_state() {
  SaveState* state;
  state = saveGwenesisStateOpenForWrite("bus");
  saveGwenesisStateSetBuffer(state, "M68K_RAM", M68K_RAM, MAX_RAM_SIZE);
  saveGwenesisStateSetBuffer(state, "ZRAM", ZRAM, MAX_Z80_RAM_SIZE);
  saveGwenesisStateSetBuffer(state, "TMSS", TMSS, sizeof(TMSS));
  saveGwenesisStateSet(state, "tmss_state", tmss_state);
  saveGwenesisStateSet(state, "tmss_count", tmss_count);
}

void gwenesis_bus_load_state() {
    SaveState* state = saveGwenesisStateOpenForRead("bus");
    saveGwenesisStateGetBuffer(state, "M68K_RAM", M68K_RAM, MAX_RAM_SIZE);
    saveGwenesisStateGetBuffer(state, "ZRAM", ZRAM, MAX_Z80_RAM_SIZE);
    saveGwenesisStateGetBuffer(state, "TMSS", TMSS, sizeof(TMSS));
    tmss_state = saveGwenesisStateGet(state, "tmss_state");
    tmss_count = saveGwenesisStateGet(state, "tmss_count");
}