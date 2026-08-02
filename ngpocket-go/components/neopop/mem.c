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

	mem.c

//=========================================================================
//---------------------------------------------------------------------------

  History of changes:
  ===================

20 JUL 2002 - neopop_uk
=======================================
- Cleaned and tidied up for the source release

22 JUL 2002 - neopop_uk
=======================================
- Added default state of interrupt control register, both enabled.
- Added auto detection of rom's colour mode.

25 JUL 2002 - neopop_uk
=======================================
- Added default hardware window values.

27 JUL 2002 - neopop_uk
=======================================
- Bios calls are only decoded in non-hle mode now. The HLE code provides
a more efficient way of decoding calls. 

01 AUG 2002 - neopop_uk
=======================================
- Added a default background colour. Improves "Mezase! Kanji Ou"

01 AUG 2002 - neopop_uk
=======================================
- Added default settings for memory error and mask memory error.
  
10 AUG 2002 - neopop_uk
=======================================
- Moved all of the bios memory setup from bios.c
- Added more detailed setup of memory, to fool clever games.
- Added raster horizontal position emulation, fixes "NeoGeo Cup '98 Plus Color"

15 AUG 2002 - neopop_uk
=======================================
- Made EEPROM status more secure if something goes wrong.

16 AUG 2002 - neopop_uk
=======================================
- Ignores direct EEPROM access to a rom that's too short.

05 SEP 2002 - neopop_uk
=======================================
- In debug mode, NULL memory read/writes only create an error if the
	error message mask is disabled.

07 SEP 2002 - neopop_uk
=======================================
- Made the direct EEPROM access more secure by requiring an EEPROM
command to be issued before every write. This isn't perfect, but it
should stop "Gals Fighters" from writing all over itself.

//---------------------------------------------------------------------------
*/

#include "neopop.h"
#include "TLCS900h_registers.h"
#include "Z80_interface.h"
#include "bios.h"
#include "gfx.h"
#include "mem.h"
#include "interrupt.h"
#include "sound.h"
#include "flash.h"

//=============================================================================

//Hack way of returning good EEPROM status.
BOOL eepromStatusEnable = FALSE;
// Not 'static' any more: translate_address_read() that uses this as scratch
// storage for the EEPROM-status read case moved to mem.h as a static inline
// function (see mem.h for why), so this needs real extern linkage now.
_u32 eepromStatus;

_u8 ram[1 + RAM_END - RAM_START];

BOOL debug_abort_memory = FALSE;
BOOL debug_mask_memory_error_messages = FALSE;

BOOL memory_unlock_flash_write = FALSE;
BOOL memory_flash_error = FALSE;
BOOL memory_flash_command = FALSE;

//=============================================================================

// memory_error(), translate_address_read()/write() and loadX()/storeX() all
// moved to mem.h as 'static inline' -- see the "01 AUG 2026" note at the top
// of that file for why (RISC-V call overhead vs. Xtensa register windows).

//=============================================================================

void post_write(_u32 address)
{
	address &= 0xFFFFFF;

	//Direct Access to Sound Chips
	if ((*(_u16*)(ram + 0xb8)) == htole16(0xAA55))
	{
		if (address == 0xA1)	Write_SoundChipTone(ram[0xA1]);
		if (address == 0xA0)	Write_SoundChipNoise(ram[0xA0]);
	}

	//DAC Write
	if (address == 0xA2)	dac_write();

	//Clear counters?
	if (address == 0x20)
	{
		_u8 TRUN = ram[0x20];

		if ((TRUN & 0x01) == 0)		timer[0] = 0;
		if ((TRUN & 0x02) == 0)		timer[1] = 0;
		if ((TRUN & 0x04) == 0)		timer[2] = 0;
		if ((TRUN & 0x08) == 0)		timer[3] = 0;
	}

	//z80 - NMI
	if (address == 0xBA)
		Z80_nmi();
}

//=============================================================================

static _u8 systemMemory[] = 
{
	// 0x00												// 0x08
	0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,		0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x08, 0xFF, 0xFF,
	// 0x10												// 0x18
	0x34, 0x3C, 0xFF, 0xFF, 0xFF, 0x3F, 0x00, 0x00,		0x3F, 0xFF, 0x2D, 0x01, 0xFF, 0xFF, 0x03, 0xB2,
	// 0x20												// 0x28
	0x80, 0x00, 0x01, 0x90, 0x03, 0xB0, 0x90, 0x62,		0x05, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x4C, 0x4C,
	// 0x30												// 0x38
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x30, 0x00, 0x00, 0x00, 0x20, 0xFF, 0x80, 0x7F,
	// 0x40												// 0x48
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0x50												// 0x58
	0x00, 0x20, 0x69, 0x15, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
	// 0x60												// 0x68
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x17, 0x17, 0x03, 0x03, 0x02, 0x00, 0x00, 0x4E,
	// 0x70												// 0x78
	0x02, 0x32, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0x80												// 0x88
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0x90												// 0x98
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xA0												// 0xA8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xB0												// 0xB8
	0x00, 0x00, 0x00, 0x00, 0x0A, 0x00, 0x00, 0x00,		0xAA, 0xAA, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xC0												// 0xC8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xD0												// 0xD8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xE0												// 0xE8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	// 0xF0												// 0xF8
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

//=============================================================================

void reset_memory(void)
{
	int i;

	eepromStatusEnable = FALSE;
	memory_flash_command = FALSE;

	memset(ram, 0, sizeof(ram));	//Clear ram

//=============================================================================
//000000 -> 000100	CPU Internal RAM (Timers/DMA/Z80)
//=====================================================

	for (i = 0; i < sizeof(systemMemory); i++)
		ram[i] = systemMemory[i];

//=============================================================================
//006C00 -> 006FFF	BIOS Workspace
//==================================

	if (rom.data)
	{
		*(_u32*)(ram + 0x6C00) = rom_header->startPC;		//Start
		*(_u16*)(ram + 0x6E82) = 
			*(_u16*)(ram + 0x6C04) = rom_header->catalog;	//Catalog
		*( _u8*)(ram + 0x6E84) = 
			*( _u8*)(ram + 0x6C06) = rom_header->subCatalog;	//Sub-Cat
		memcpy(ram + 0x6C08, rom.data + 0x24, 12);			//name

		*(_u8*)(ram + 0x6C58) = 0x01;			//LO-EEPROM present

		//32MBit cart?
		if (rom.length > 0x200000)
			*(_u8*)(ram + 0x6C59) = 0x01;			//HI-EEPROM present
		else
			*(_u8*)(ram + 0x6C59) = 0x00;			//HI-EEPROM not present

		ram[0x6C55] = 1;	//Commercial game
	}
	else
	{
		*(_u32*)(ram + 0x6C00) = htole32(0x00FF970A);	//Start
		*(_u16*)(ram + 0x6C04) = htole16(0xFFFF);	//Catalog
		*( _u8*)(ram + 0x6C06) = 0x00;			//Sub-Cat
		sprintf(ram + 0x6C08, "NEOGEOPocket");	//bios rom 'Name'

		*(_u8*)(ram + 0x6C58) = 0x00;			//LO-EEPROM not present
		*(_u8*)(ram + 0x6C59) = 0x00;			//HI-EEPROM not present

		ram[0x6C55] = 0;	//Bios menu
	}
	

	ram[0x6F80] = 0xFF;	//Lots of battery power!
	ram[0x6F81] = 0x03;

	ram[0x6F84] = 0x40;	// "Power On" startup
	ram[0x6F85] = 0x00;	// No shutdown request
	ram[0x6F86] = 0x00;	// No user answer (?)

	//Language: 0 = Japanese, 1 = English
	ram[0x6F87] = (_u8)language_english;	

	//Color Mode Selection: 0x00 = B&W, 0x10 = Colour
	if (system_colour == COLOURMODE_GREYSCALE)
		ram[0x6F91] = ram[0x6F95] = 0x00; //Force Greyscale
	if (system_colour == COLOURMODE_COLOUR)	
		ram[0x6F91] = ram[0x6F95] = 0x10; //Force Colour
	if (system_colour == COLOURMODE_AUTO) 
	{
		if (rom.data) 
			ram[0x6F91] = ram[0x6F95] = rom_header->mode;	//Auto-detect
		else 
			ram[0x6F91] = ram[0x6F95] = 0x10; // Default = Colour
	}

	//Interrupt table
	for (i = 0; i < 0x12; i++)
		*(_u32*)(ram + 0x6FB8 + (i * 4)) = htole32(0x00FF23DF);


//=============================================================================
//008000 -> 00BFFF	Video RAM
//=============================

	ram[0x8000] = 0xC0;	// Both interrupts allowed

	//Hardware window
	ram[0x8002] = 0x00;
	ram[0x8003] = 0x00;
	ram[0x8004] = 0xFF;
	ram[0x8005] = 0xFF;

	ram[0x8006] = 0xc6;	// Frame Rate Register

	ram[0x8012] = 0x00;	// NEG / OOWC setting.

	ram[0x8118] = 0x80;	// BGC on!

	ram[0x83E0] = 0xFF;	// Default background colour
	ram[0x83E1] = 0x0F;

	ram[0x83F0] = 0xFF;	// Default window colour
	ram[0x83F1] = 0x0F;

	ram[0x8400] = 0xFF;	// LED on
	ram[0x8402] = 0x80;	// Flash cycle = 1.3s
}

//=============================================================================


