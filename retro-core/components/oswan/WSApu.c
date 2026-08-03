#include <stdio.h>
#include <string.h>

#ifndef GNW_WSWAN
#include <SDL/SDL.h>
#endif

#include "WSHard.h"
#include "WSApu.h"
#include "sound.h"

SOUND Ch[4];
SWEEP Swp;
NOISE Noise; 

int16_t sndbuffer[2][SND_RNGSIZE]; /* Sound Ring Buffer */
int32_t rBuf, wBuf;

int8_t VoiceOn;
int16_t Sound[7] = {1, 1, 1, 1, 1, 1, 1};

static uint32_t convert_multiplier = MULT;

static uint8_t PData[4][32];
static uint8_t PDataN[8][BUFSIZEN];
static uint16_t RandData[BUFSIZEN];

/* Runtime phase/position accumulators. These were function-local statics in
 * apuShiftReg/apuVoice/apuWaveSet; promoted to file scope so a savestate can
 * capture them. The savestate's WriteIO(0x80-0x90) replay rebuilds the channel
 * config (Ch/Swp.on/Noise/VoiceOn/PData) but NOT these accumulators — they are
 * not IO-mapped. Leaving them at reset on a cold load makes a game that polls
 * sound-DMA completion (One Piece's voice DMA: IO[SDMACTL] bit7, gated by the
 * apuVoice DMA position) or reads the noise register (NCSR = RandData[apu_nPos])
 * see the wrong value and hang. apuSaveState/apuLoadState (below) serialise them.
 * PDataN/RandData are apuInit-filled constant tables (identical every boot), so
 * they are not part of the snapshot. */
uint32_t apu_nPos = 0;               /* apuShiftReg noise read position (-> NCSR) */
int32_t  apu_voice_index = 0;        /* apuVoice sound-DMA sample position */
int32_t  apu_voice_b = 0;            /* apuVoice sound-DMA bank offset */
uint16_t apu_point[4]    = {0,0,0,0};/* apuWaveSet per-channel wave phase */
uint16_t apu_preindex[4] = {0,0,0,0};/* apuWaveSet per-channel previous index */

extern uint8_t *Page[16];
extern uint8_t IO[0x100];

int32_t apuBufLen(void)
{
	if (wBuf >= rBuf) return wBuf - rBuf;
	return SND_RNGSIZE + (wBuf - rBuf);
}

void apuWaveCreate(void)
{
    /*memset(sndbuffer,0x00, SND_RNGSIZE);*/
    memset(sndbuffer, 0x00, sizeof(*sndbuffer));
}

void apuWaveRelease(void)
{
	Pause_Sound();
}

void apuInit(void)
{
    uint32_t i, j;
    
    convert_multiplier = MULT;

    for (i = 0; i < 4; i++)
    {
        for (j = 0; j < 32; j++)
        {
            PData[i][j] = 8;
        }
    }
    for (i = 0; i < 8; i++)
    {
        for (j = 0; j < BUFSIZEN; j++)
        {
            PDataN[i][j] = ((apuMrand(15 - i) & 1) ? 15 : 0);
        }
    }

    for (i = 0; i < BUFSIZEN; i++)
    {
        RandData[i] = apuMrand(15);
    }
    apuWaveCreate();
}

/* Savestate hooks for the runtime accumulators the WriteIO replay cannot rebuild
 * (noise read position, sound-DMA position, wave phase, sweep countdown). Fixed
 * 32-byte layout; if it changes, bump the container's WS_STATE_VERSION so old
 * files are rejected rather than misread. Ch/Swp.on/Noise/VoiceOn/PData are NOT
 * here — the 0x80-0x90 WriteIO replay reconstructs them from restored IO+IRAM. */
uint32_t apuStateSize(void) { return 32; }

void apuSaveState(uint8_t *p)
{
    memcpy(p, &apu_nPos, 4);        p += 4;
    memcpy(p, &apu_voice_index, 4); p += 4;
    memcpy(p, &apu_voice_b, 4);     p += 4;
    memcpy(p, apu_point, 8);        p += 8;
    memcpy(p, apu_preindex, 8);     p += 8;
    memcpy(p, &Swp.cnt, 4);
}

void apuLoadState(const uint8_t *p)
{
    memcpy(&apu_nPos, p, 4);        p += 4;
    memcpy(&apu_voice_index, p, 4); p += 4;
    memcpy(&apu_voice_b, p, 4);     p += 4;
    memcpy(apu_point, p, 8);        p += 8;
    memcpy(apu_preindex, p, 8);     p += 8;
    memcpy(&Swp.cnt, p, 4);
}

void apuEnd(void)
{
    apuWaveRelease();
	Sound_APUClose();
}

uint16_t apuMrand(uint32_t Degree)
{
	#define BIT(n) (1<<n)
    typedef struct
    {
        uint32_t N;
        uint16_t InputBit;
        int32_t Mask;
    } POLYNOMIAL;

    static POLYNOMIAL TblMask[]=
    {
        { 2,BIT(2) ,BIT(0)|BIT(1)},
        { 3,BIT(3) ,BIT(0)|BIT(1)},
        { 4,BIT(4) ,BIT(0)|BIT(1)},
        { 5,BIT(5) ,BIT(0)|BIT(2)},
        { 6,BIT(6) ,BIT(0)|BIT(1)},
        { 7,BIT(7) ,BIT(0)|BIT(1)},
        { 8,BIT(8) ,BIT(0)|BIT(2)|BIT(3)|BIT(4)},
        { 9,BIT(9) ,BIT(0)|BIT(4)},
        {10,BIT(10),BIT(0)|BIT(3)},
        {11,BIT(11),BIT(0)|BIT(2)},
        {12,BIT(12),BIT(0)|BIT(1)|BIT(4)|BIT(6)},
        {13,BIT(13),BIT(0)|BIT(1)|BIT(3)|BIT(4)},
        {14,BIT(14),BIT(0)|BIT(1)|BIT(4)|BIT(5)},
        {15,BIT(15),BIT(0)|BIT(1)},
        { 0,      0,      0},
    };
    static POLYNOMIAL *pTbl = TblMask;
    static uint16_t ShiftReg = BIT(2)-1;
    int32_t XorReg = 0;
    int32_t Masked;

    if(pTbl->N != Degree)
    {
        pTbl = TblMask;
        while(pTbl->N) {
            if(pTbl->N == Degree)
            {
                break;
            }
            pTbl++;
        }
        if(!pTbl->N)
        {
            pTbl--;
        }
        ShiftReg &= pTbl->InputBit-1;
        if(!ShiftReg)
        {
            ShiftReg = pTbl->InputBit-1;
        }
    }
    Masked = ShiftReg & pTbl->Mask;
    while(Masked)
    {
        XorReg ^= Masked & 0x01;
        Masked >>= 1;
    }
    if(XorReg)
    {
        ShiftReg |= pTbl->InputBit;
    }
    else
    {
        ShiftReg &= ~pTbl->InputBit;
    }
    ShiftReg >>= 1;
    return ShiftReg;
}

void apuSetPData(int32_t addr, uint8_t val)
{
    int32_t i, j;

    i = (addr & 0x30) >> 4;
    j = (addr & 0x0F) << 1;
    PData[i][j]=(uint8_t)(val & 0x0F);
    PData[i][j + 1]=(uint8_t)((val & 0xF0)>>4);
}

uint8_t apuVoice(void)
{
    /* apu_voice_index / apu_voice_b: file-scope so savestates capture the
     * sound-DMA position (was function-local static index/b). */
    uint8_t v;

    if ((IO[SDMACTL] & 0x98) == 0x98) /* Hyper voice */
    {
        v = Page[IO[SDMASH] + apu_voice_b][*(uint16_t*)(IO + SDMASL) + apu_voice_index++];
        if ((*(uint16_t*)(IO + SDMASL) + apu_voice_index) == 0)
        {
            apu_voice_b++;
        }
        if (v < 0x80)
        {
            v += 0x80;
        }
        else
        {
            v -= 0x80;
        }
        if (*(uint16_t*)(IO+SDMACNT) <= apu_voice_index)
        {
            apu_voice_index = 0;
            apu_voice_b = 0;
        }
        return v;
    }
    else if ((IO[SDMACTL] & 0x88) == 0x80) /* DMA start */
    {
        IO[SND2VOL] = Page[IO[SDMASH] + apu_voice_b][*(uint16_t*)(IO + SDMASL) + apu_voice_index++];
        if ((*(uint16_t*)(IO + SDMASL) + apu_voice_index) == 0)
        {
            apu_voice_b++;
        }
        if (*(uint16_t*)(IO + SDMACNT) <= apu_voice_index)
        {
            IO[SDMACTL] &= 0x7F; /* DMA end */
            *(uint16_t*)(IO + SDMACNT) = 0;
            apu_voice_index = 0;
            apu_voice_b = 0;
        }
    }
    return ((VoiceOn && Sound[4]) ? IO[SND2VOL] : 0x80);
}

void apuSweep(void)
{
    if ((Swp.step) && Swp.on) /* Sweep on */
    {
        if (Swp.cnt < 0)
        {
            Swp.cnt = Swp.time;
            Ch[2].freq += Swp.step;
            Ch[2].freq &= 0x7ff;
        }
        Swp.cnt--;
    }
}

uint16_t apuShiftReg(void)
{
    /* Noise counter (apu_nPos: file-scope so savestates capture it) */
    if (++apu_nPos >= BUFSIZEN)
    {
        apu_nPos = 0;
    }
    return RandData[apu_nPos];
}

void apuWaveSet(void)
{
	/* Do you like them, The Wonders of uninitialized variables ?
	* Especially when the compiler gives you no insight on that ?
	* If lVol and rVol are not initiliased, then it will sound wrong on
	* games like Klonoa with voices. After initializing them,
	* it would work and it would still sound fine if we're using a 16-bits
	* size for them. This should hopefully make things faster on
	* some platforms. No FPU needed !
	*/
	/* point[]/preindex[] promoted to file-scope apu_point[]/apu_preindex[] so
	 * savestates capture the per-channel wave phase (was function-local static). */
    uint16_t value = 0, lVol[4] = {0, 0, 0, 0}, rVol[4] = {0, 0, 0, 0};
    int16_t LL, RR, vVol;
    uint16_t index;
    uint32_t channel;

    Sound_APU_Start();

    apuSweep();

    for (channel = 0; channel < 4; channel++)
    {
        if (Ch[channel].on)
        {
            if (channel == 1 && VoiceOn && Sound[4])
            {
                continue;
            }
            else if (channel == 2 && Swp.on && !Sound[5])
            {
                continue;
            }
            else if (channel == 3 && Noise.on && Sound[6])
            {
                index = (3072000 / BPSWAV) * apu_point[3] / (2048 - Ch[3].freq);
                if ((index %= BUFSIZEN) == 0 && apu_preindex[3])
                {
                    apu_point[3] = 0;
                }

				value = PDataN[Noise.pattern][index] - 8;
            }
            else if (Sound[channel] == 0)
            {
                continue;
            }
            else
            {
                index = (3072000 / BPSWAV) * apu_point[channel] / (2048 - Ch[channel].freq);
                if ((index %= 32) == 0 && apu_preindex[channel])
                {
                    apu_point[channel] = 0;
                }
                value = PData[channel][index] - 8;
            }
            apu_preindex[channel] = index;
            apu_point[channel]++;
            lVol[channel] = value * Ch[channel].volL; /* -8*15=-120, 7*15=105 */
            rVol[channel] = value * Ch[channel].volR;
        }
		else
		{
			lVol[channel] = 0;
			rVol[channel] = 0;	
		}
    }
    
    vVol = (apuVoice() - 0x80);
    /* mix 16bits wave -32768 ～ +32767 32768/120 = 273 */
    LL = (lVol[0] + lVol[1] + lVol[2] + lVol[3] + vVol) * WAV_VOLUME;
    RR = (rVol[0] + rVol[1] + rVol[2] + rVol[3] + vVol) * WAV_VOLUME;

	#ifdef NATIVE_AUDIO
	sndbuffer[0][wBuf] = LL;
	sndbuffer[1][wBuf] = RR;
	if (++wBuf >= SND_RNGSIZE)
	{
		wBuf = 0;
	}
	#else
	if (convert_multiplier == MULT) 
	{
		convert_multiplier = (MULT+1);
	}
	else
	{
		convert_multiplier = MULT;
	}

	for (uint32_t i=0;i<convert_multiplier;i++)	/* 48000/12000 */
	{ 
		sndbuffer[0][wBuf] = LL;
		sndbuffer[1][wBuf] = RR;
		if (++wBuf >= SND_RNGSIZE)
		{
			wBuf = 0;
		}
	}
	#endif
	
	Sound_APU_End();
}

