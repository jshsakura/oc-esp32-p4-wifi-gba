//---------------------------------------------------------------------------
// NEOPOP : Emulator as in Dreamland
//
// Copyright (c) 2001-2002 by neopop_uk
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version. See also the license.txt file for
//	additional informations.
//---------------------------------------------------------------------------

/*
//---------------------------------------------------------------------------
//=========================================================================

	mem.h

//=========================================================================
//---------------------------------------------------------------------------

  History of changes:
  ===================

20 JUL 2002 - neopop_uk
=======================================
- Cleaned and tidied up for the source release

15 AUG 2002 - neopop_uk
=======================================
- Removed the legacy 'eeprom' variables.

18 AUG 2002 - neopop_uk
=======================================
- Moved RAM_START/RAM_END definition and ram[] declaration to NeoPop.h

01 AUG 2026 - oc-gba port
=======================================
- Turned translate_address_read/write and loadX()/storeX() into 'static
  inline' functions defined here instead of ordinary extern functions in mem.c.
  Measured: ESP32-P4 build was 42-44fps at 100% CPU busy (docs/BRINGUP.md
  A11) while every other small core on this port had headroom. This is the
  single hottest path in the core -- every opcode fetch and almost every
  operand access (TLCS900h_interpret_src.c/dst.c alone has 200+ call sites)
  went through loadX()/storeX() calling translate_address_X(), two ordinary
  cross-TU function calls per access. On the original Xtensa target that
  was nearly free: Xtensa's windowed-register ABI makes call/return cheap
  because the callee just gets a fresh register window instead of spilling
  to the stack. RISC-V (this chip) has no register windows -- every call
  pushes/pops through the stack per the standard calling convention -- so
  the same two-call chain is not free here, and it runs ~6.1M times/sec
  (the emulated TLCS900h clock) plus whatever the Z80 sound CPU needs.
  Making the whole address-translation switch inline lets the compiler
  fold it directly into each caller instead of paying for the calls.
  This is a pure refactor (identical logic), not a behaviour change.
  Needs a hardware measurement to confirm the win (see docs/BRINGUP.md).

//---------------------------------------------------------------------------
*/

#ifndef __MEM__
#define __MEM__
//=============================================================================

// Pulled in explicitly (rather than relying on include order in whichever
// .c file happens to include mem.h first) because the inline functions
// below now need _u8/_u32/ram/rom/bios/flash_write/timer_hint directly.
#include "neopop.h"
#include "bios.h"
#include "flash.h"
#include "interrupt.h"

#define ROM_START	0x200000
#define ROM_END		0x3FFFFF

#define HIROM_START	0x800000
#define HIROM_END	0x9FFFFF

#define BIOS_START	0xFF0000
#define BIOS_END	0xFFFFFF

void reset_memory(void);

void dump_memory(_u32 start, _u32 length);

extern BOOL debug_abort_memory;
extern BOOL debug_mask_memory_error_messages;

extern BOOL memory_unlock_flash_write;
extern BOOL memory_flash_error;
extern BOOL memory_flash_command;

extern BOOL eepromStatusEnable;
extern _u32 eepromStatus;

// Runs any side effects a write to 'address' has (sound chip latches, DAC,
// timer clears, Z80 NMI). Kept as a normal call in mem.c: it is only taken
// on writes, it is bulkier than the load/store fast path, and it needs
// sound.h/Z80_interface.h which would otherwise have to be dragged into
// this header for every translation unit that includes mem.h.
void post_write(_u32 address);

#ifdef NEOPOP_DEBUG
static inline void memory_error(_u32 address, BOOL read)
{
	debug_abort_memory = TRUE;

	if (filter_mem)
	{
		if (debug_mask_memory_error_messages)
			return;

		if (read)
			system_debug_message("Memory Exception: Read from %06X", address);
		else
			system_debug_message("Memory Exception: Write to %06X", address);
	}
}
#endif

//=============================================================================

static inline void* translate_address_read(_u32 address)
{
	address &= 0xFFFFFF;

#ifdef NEOPOP_DEBUG

	if (address == 0 && debug_mask_memory_error_messages == FALSE)
	{ memory_error(address, TRUE); return NULL; }

#endif

	// ===================================

	//RAS.H read (Simulated horizontal raster position)
	if (address == 0x8008)
		ram[0x8008] = (_u8)((abs(TIMER_HINT_RATE - (int)timer_hint)) >> 2);

	if (address <= RAM_END)
		return ram + address;

	// ===================================

	//Get EEPROM status?
	if (eepromStatusEnable)
	{
		eepromStatusEnable = FALSE;
		if (address == 0x220000 || address == 0x230000)
		{
			eepromStatus = 0xFFFFFFFF;
			return &eepromStatus;
		}
	}

	//ROM (LOW)
	if (rom.data && address >= ROM_START && address <= ROM_END)
	{
		if (address <= ROM_START + rom.length)
			return rom.data + (address - ROM_START);
		else
			return NULL;
	}

	//ROM (HIGH)
	if (rom.data && address >= HIROM_START && address <= HIROM_END)
	{
		if (address <= HIROM_START + (rom.length - 0x200000))
			return rom.data + 0x200000 + (address - HIROM_START);
		else
			return NULL;
	}

	// ===================================

	//BIOS Access?
	if ((address & 0xFF0000) == 0xFF0000)
		return bios + (address & 0xFFFF); // BIOS ROM

	// ===================================

	//Signal a flash memory error
	if (memory_unlock_flash_write)
		memory_flash_error = TRUE;

#ifdef NEOPOP_DEBUG
	memory_error(address, TRUE);
#endif
	return NULL;
}

//=============================================================================

static inline void* translate_address_write(_u32 address)
{
	address &= 0xFFFFFF;

#ifdef NEOPOP_DEBUG

	if (address == 0 && debug_mask_memory_error_messages == FALSE)
	{ memory_error(address, FALSE); return NULL; }

#endif

	// ===================================


	if (address <= RAM_END)
		return ram + address;

	// ===================================

	if (memory_unlock_flash_write)
	{
		//ROM (LOW)
		if (rom.data && address >= ROM_START && address <= ROM_END)
		{
			if (address <= ROM_START + rom.length)
				return rom.data + (address - ROM_START);
			else
				return NULL;
		}

		//ROM (HIGH)
		if (rom.data && address >= HIROM_START && address <= HIROM_END)
		{
			if (address <= HIROM_START + (rom.length - 0x200000))
				return rom.data + 0x200000 + (address - HIROM_START);
			else
				return NULL;
		}

		//Signal a flash memory error
		memory_flash_error = TRUE;
	}
	else
	{
		//ROM (LOW)
		if (rom.data && address >= ROM_START && address <= ROM_END)
		{
			//Ignore EEPROM commands
			if (address == 0x202AAA || address == 0x205555)
			{
	//			system_debug_message("%06X: Enable EEPROM command from %06X", pc, address);
				memory_flash_command = TRUE;
				return NULL;
			}

			//Set EEPROM status reading?
			if (address == 0x220000 || address == 0x230000)
			{
	//			system_debug_message("%06X: EEPROM status read from %06X", pc, address);
				eepromStatusEnable = TRUE;
				return NULL;
			}

			if (memory_flash_command)
			{
				//Write the 256byte block around the flash data
				flash_write(address & 0xFFFF00, 256);

				//Need to issue a new command before writing will work again.
				memory_flash_command = FALSE;

	//			system_debug_message("%06X: Direct EEPROM write to %06X", pc, address & 0xFFFF00);
	//			system_debug_stop();

				//Write to the rom itself.
				if (address <= ROM_START + rom.length)
					return rom.data + (address - ROM_START);
			}
		}
	}

	// ===================================

#ifdef NEOPOP_DEBUG
	memory_error(address, FALSE);
#endif
	return NULL;
}

//=============================================================================

static inline _u8 loadB(_u32 address)
{
	_u8* ptr = translate_address_read(address);
	if (ptr == NULL)
		return 0;
	else
		return *ptr;
}

static inline _u16 loadW(_u32 address)
{
	_u16* ptr = translate_address_read(address);
	if (ptr == NULL)
		return 0;
	else
		return le16toh(*ptr);
}

static inline _u32 loadL(_u32 address)
{
	_u32* ptr = translate_address_read(address);
	if (ptr == NULL)
		return 0;
	else
		return le32toh(*ptr);
}

//=============================================================================

static inline void storeB(_u32 address, _u8 data)
{
	_u8* ptr = translate_address_write(address);

	//Write
	if (ptr)
	{
		*ptr = data;
		post_write(address);
	}
}

static inline void storeW(_u32 address, _u16 data)
{
	_u16* ptr = translate_address_write(address);

	//Write
	if (ptr)
	{
		*ptr = htole16(data);
		post_write(address);
	}
}

static inline void storeL(_u32 address, _u32 data)
{
	_u32* ptr = translate_address_write(address);

	//Write
	if (ptr)
	{
		*ptr = htole32(data);
		post_write(address);
	}
}

//=============================================================================
#endif
