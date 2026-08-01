#include "WSRender.h"
#include "WS.h"
#include "shared.h"
#include "cpu/necintrf.h"

#define MAP_TILE 0x01FF
#define MAP_PAL  0x1E00
#define MAP_BANK 0x2000
#define MAP_HREV 0x4000
#define MAP_VREV 0x8000
uint8_t *Scr1TMap;
uint8_t *Scr2TMap;

#define SPR_TILE 0x01FF
#define SPR_PAL  0x0E00
#define SPR_CLIP 0x1000
#define SPR_LAYR 0x2000
#define SPR_HREV 0x4000
#define SPR_VREV 0x8000
uint8_t *SprTTMap;
uint8_t *SprETMap;
uint8_t SprTMap[512];

uint16_t Palette[16][16];
uint16_t MonoColor[8];

#ifdef NOSDL_FB
uint16_t FrameBuffer[240*144];
#else
uint16_t* FrameBuffer;
#endif

uint8_t Layer[3] = {1, 1, 1};   /* BG/FG/sprite enable (non-const: rig profiling lever) */

uint8_t *pbTData;
uint32_t Index0[2];			// 32bit access to Index
uint8_t *Index = (uint8_t *)Index0;	// Index[8]
int32_t TMap;

void SetPalette(const uint32_t addr)
{
    uint16_t color, r, g, b;
    uint16_t pal;

    /* RGB444 format */
    color = *(uint16_t*)(IRAM + (addr & 0xFFFE));

	/* RGB565 */
	r = ((color & 0x0F00) << 4);
	g = ((color & 0x00F0) << 3);
	b = ((color & 0x000F) << 1);
	pal = (r) | (g) | (b);

	Palette[(addr & 0x1E0)>>5][(addr & 0x1E) >> 1] = pal;
}

/* Bit-spread tables for the planar (non-packed) tile formats. The stock code
 * de-interleaves each plane byte's 8 bits into 8 pixel slots with a column of
 * shift/mask expressions per pixel; that is the dominant BG/FG cost (rig: BG+FG
 * ~= 1.66M insn/frame in One Piece battle). Precompute the per-plane spread once
 * and OR the planes together at plane weight, cutting MakeIndex's arithmetic
 * roughly in half. Byte-for-byte identical to the expressions below (rig RUNHASH
 * gate proves it), so no visible change.
 *
 *   sprd_lo[b]  bytes 0..3 = bit7,bit6,bit5,bit4 of b   (pixels 0..3, normal)
 *   sprd_hi[b]  bytes 0..3 = bit3,bit2,bit1,bit0 of b   (pixels 4..7, normal)
 *   sprd_lo_r/hi_r: the SPR_HREV bit order (bit0,bit1,.. / bit4,bit5,..)
 */
static uint32_t sprd_lo[256], sprd_hi[256], sprd_lo_r[256], sprd_hi_r[256];
static int ws_render_tbl_ready = 0;
static void ws_render_init_tables(void)
{
	for (int b = 0; b < 256; b++) {
		uint32_t lo = 0, hi = 0, lor = 0, hir = 0;
		for (int i = 0; i < 4; i++) {
			lo  |= (uint32_t)((b >> (7 - i)) & 1) << (i * 8);
			hi  |= (uint32_t)((b >> (3 - i)) & 1) << (i * 8);
			lor |= (uint32_t)((b >> i)       & 1) << (i * 8);
			hir |= (uint32_t)((b >> (4 + i)) & 1) << (i * 8);
		}
		sprd_lo[b] = lo; sprd_hi[b] = hi; sprd_lo_r[b] = lor; sprd_hi_r[b] = hir;
	}
	ws_render_tbl_ready = 1;
}

static inline void MakeIndex(void)
{
	register uint_fast8_t pbTData0 = pbTData[0];
	register uint_fast8_t pbTData1 = pbTData[1];
	register uint_fast8_t pbTData2;
	register uint_fast8_t pbTData3;

	switch ( ((IO[COLCTL] & 0x60)>>5) | ((TMap & SPR_HREV)>>12) )
	{
	case 0:		// 4 Colors
		Index0[0] = sprd_lo[pbTData0] | (sprd_lo[pbTData1] << 1);
		Index0[1] = sprd_hi[pbTData0] | (sprd_hi[pbTData1] << 1);
		break;
	case 1:		// 4 Colors	Packed Mode
		Index0[0] =  (pbTData0>>6   )		|
			    ((pbTData0>>4)&3)<<8	|
			    ((pbTData0>>2)&3)<<16	|
			     (pbTData0    &3)<<24	;
		Index0[1] =  (pbTData1>>6   )		|
			    ((pbTData1>>4)&3)<<8	|
			    ((pbTData1>>2)&3)<<16	|
			     (pbTData1    &3)<<24	;
		break;
	case 2:		// 16 Colors
		pbTData2 = pbTData[2];
		pbTData3 = pbTData[3];
		Index0[0] = sprd_lo[pbTData0] | (sprd_lo[pbTData1] << 1)
			  | (sprd_lo[pbTData2] << 2) | (sprd_lo[pbTData3] << 3);
		Index0[1] = sprd_hi[pbTData0] | (sprd_hi[pbTData1] << 1)
			  | (sprd_hi[pbTData2] << 2) | (sprd_hi[pbTData3] << 3);
		break;
	case 3:		// 16 Colors	Packed Mode
		pbTData2 = pbTData[2];
		pbTData3 = pbTData[3];
		Index0[0] = (pbTData0>>4)		|
			    (pbTData0 & 0x0F)<<8	|
			    (pbTData1>>4)<<16		|
			    (pbTData1 & 0x0F)<<24	;
		Index0[1] = (pbTData2>>4)		|
			    (pbTData2 & 0x0F)<<8	|
			    (pbTData3>>4)<<16		|
			    (pbTData3 & 0x0F)<<24	;
		break;
	case 4:		// 4 Colors			SPR_HREV
		Index0[0] = sprd_lo_r[pbTData0] | (sprd_lo_r[pbTData1] << 1);
		Index0[1] = sprd_hi_r[pbTData0] | (sprd_hi_r[pbTData1] << 1);
		break;
	case 5:		// 4 Colors	Packed Mode	SPR_HREV
		Index0[0] = ( pbTData1    &3)		|
			    ((pbTData1>>2)&3)<<8	|
			    ((pbTData1>>4)&3)<<16	|
			    ( pbTData1>>6   )<<24	;
		Index0[1] = ( pbTData0    &3)		|
			    ((pbTData0>>2)&3)<<8	|
			    ((pbTData0>>4)&3)<<16	|
			    ( pbTData0>>6   )<<24	;
		break;
	case 6:		// 16 Colors 			SPR_HREV
		pbTData2 = pbTData[2];
		pbTData3 = pbTData[3];
		Index0[0] = sprd_lo_r[pbTData0] | (sprd_lo_r[pbTData1] << 1)
			  | (sprd_lo_r[pbTData2] << 2) | (sprd_lo_r[pbTData3] << 3);
		Index0[1] = sprd_hi_r[pbTData0] | (sprd_hi_r[pbTData1] << 1)
			  | (sprd_hi_r[pbTData2] << 2) | (sprd_hi_r[pbTData3] << 3);
		break;
	case 7:		// 16 Colors	Packed Mode	SPR_HREV
		pbTData2 = pbTData[2];
		pbTData3 = pbTData[3];
		Index0[0] = (pbTData3 & 0x0F)		|
			    (pbTData3>>4)<<8		|
			    (pbTData2 & 0x0F)<<16	|
			    (pbTData2>>4)<<24		;
		Index0[1] = (pbTData1 & 0x0F)		|
			    (pbTData1>>4)<<8		|
			    (pbTData0 & 0x0F)<<16	|
			    (pbTData0>>4)<<24		;
		break;
	}
}

/* When 0, the per-scanline pixel work is skipped (CPU still runs). The G&W
 * front-end clears this on frames it won't display so the emulator can keep
 * pace instead of rendering every frame. */
int ws_render_enabled = 1;

void RefreshLine(const uint16_t Line)
{
    uint16_t *pSBuf;		/* データ書き込みバッファ */
    if (!ws_render_enabled)
        return;
    if (!ws_render_tbl_ready)
        ws_render_init_tables();
    uint16_t *pSWrBuf;		/* ↑の書き込み位置用ポインタ*/
    uint8_t *pZ;		/* ↓のインクリメント用ポインタ*/
    uint8_t ZBuf[0x100];	/* FGレイヤーの非透明部を保存*/
    uint8_t *pW;		/*↓のインクリメント用ポインタ*/
    uint8_t WBuf[0x100];	/* FGレイヤーのウィンドーを保存*/
    int32_t OffsetX;
    int32_t OffsetY;
    uint8_t *pbTMap;
    int32_t TMapX;
    int32_t TMapXEnd;
    int32_t TMapTemp;
    int32_t PalIndex;
    int16_t i, j, k;
    uint16_t BaseCol; 
    pSBuf = FrameBuffer + Line * 240 + 8;	// +8 offset
    pSWrBuf = pSBuf;

    if(IO[LCDSLP] & 0x01)
    {
        if(IO[COLCTL] & 0xE0) BaseCol = Palette[(IO[BORDER] & 0xF0) >> 4][IO[BORDER] & 0x0F];
        else BaseCol = MonoColor[IO[BORDER] & 0x07];
    }
    else BaseCol = 0;

    for(i = 0; i < 224; i+=2) *(uint32_t*)(pSWrBuf+i) = BaseCol|(BaseCol<<16);

    if(!(IO[LCDSLP] & 0x01)) return;
/*********************************************************************/
    if((IO[DSPCTL] & 0x01) && Layer[0])                                 /* BG layer */
    {
        OffsetX = IO[SCR1X] & 0x07;
        pSWrBuf = pSBuf - OffsetX;
        i = Line + IO[SCR1Y];
        OffsetY = (i & 0x07);

        pbTMap = Scr1TMap + ((i & 0xF8) << 3);
        TMapX = (IO[SCR1X] & 0xF8) >> 2;
        TMapXEnd = ((IO[SCR1X] + 224 + 7) >> 2) & 0xFFE;

        for(; TMapX < TMapXEnd;)
        {
            TMap = *(pbTMap + (TMapX++ & 0x3F));
            TMap |= *(pbTMap + (TMapX++ & 0x3F)) << 8;

            if(IO[COLCTL] & 0x40) /* 16 colors */
            {
                if(TMap & MAP_BANK) pbTData = IRAM + 0x8000;
                else pbTData = IRAM + 0x4000;
                pbTData += (TMap & MAP_TILE) << 5;
                if(TMap & MAP_VREV) pbTData += (7 - OffsetY) << 2;
                else pbTData += OffsetY << 2;
            } else {
                if((IO[COLCTL] & 0x80) && (TMap & MAP_BANK)) /* 4 colors and bank 1 */
                     pbTData = IRAM + 0x4000;
                else pbTData = IRAM + 0x2000;
                pbTData += (TMap & MAP_TILE) << 4;
                if(TMap & MAP_VREV) pbTData += (7 - OffsetY) << 1;
                else pbTData += OffsetY << 1;
            }

	    MakeIndex();
            PalIndex = (TMap & MAP_PAL) >> 9;
	    TMapTemp = ((TMap & 0x0800) || (IO[COLCTL] & 0x40));

            /* Hoist the palette row and lift the per-tile TMapTemp branch out of
             * the 8 per-pixel writes. TMapTemp is constant for the whole tile:
             * when 0 every pixel is opaque (8 straight stores); when 1 index 0 is
             * transparent. Same stores as the per-pixel form above (rig-verified),
             * just without re-indexing Palette[PalIndex] and re-testing TMapTemp
             * eight times. */
            {
                const uint16_t *pal = Palette[PalIndex];
                if (TMapTemp) {
                    if (Index[0]) pSWrBuf[0] = pal[Index[0]];
                    if (Index[1]) pSWrBuf[1] = pal[Index[1]];
                    if (Index[2]) pSWrBuf[2] = pal[Index[2]];
                    if (Index[3]) pSWrBuf[3] = pal[Index[3]];
                    if (Index[4]) pSWrBuf[4] = pal[Index[4]];
                    if (Index[5]) pSWrBuf[5] = pal[Index[5]];
                    if (Index[6]) pSWrBuf[6] = pal[Index[6]];
                    if (Index[7]) pSWrBuf[7] = pal[Index[7]];
                } else {
                    pSWrBuf[0] = pal[Index[0]];
                    pSWrBuf[1] = pal[Index[1]];
                    pSWrBuf[2] = pal[Index[2]];
                    pSWrBuf[3] = pal[Index[3]];
                    pSWrBuf[4] = pal[Index[4]];
                    pSWrBuf[5] = pal[Index[5]];
                    pSWrBuf[6] = pal[Index[6]];
                    pSWrBuf[7] = pal[Index[7]];
                }
            }
	    pSWrBuf+=8;
        }
    }
/*********************************************************************/
    memset(ZBuf, 0, sizeof(ZBuf));
    if((IO[DSPCTL] & 0x02) && Layer[1])          /* FG layer表示 */
    {
        /* Fill ALL of WBuf, pads included: fine scroll makes the tile loop read
         * pW = WBuf+8-OffsetX, i.e. WBuf[0..7] (and past 231 on the right), and
         * upstream left those bytes as stack garbage — the garbage only gates
         * never-displayed margin pixels, but it makes the rendered buffer
         * nondeterministic, which poisons any whole-buffer comparison. */
        if((IO[DSPCTL] & 0x30) == 0x20) {	/* ウィンドウ内部のみに表示 */
            memset(WBuf, 1, sizeof(WBuf));
            if((Line >= IO[SCR2WT]) && (Line <= IO[SCR2WB]))
                { for(i = IO[SCR2WL], pW = WBuf + 8 + i; (i <= IO[SCR2WR]) && (i < 224); i++) *pW++ = 0; }
        } else if((IO[DSPCTL] & 0x30) == 0x30) {/* ウィンドウ外部のみに表示 */
            memset(WBuf, 0, sizeof(WBuf));
            if((Line >= IO[SCR2WT]) && (Line <= IO[SCR2WB]))
                { for(i = IO[SCR2WL], pW = WBuf + 8 + i; (i <= IO[SCR2WR]) && (i < 224); i++) *pW++ = 1; }
        } else {
	    memset(WBuf, 0, sizeof(WBuf));
	}

        OffsetX = IO[SCR2X] & 0x07;
        pSWrBuf = pSBuf - OffsetX;
        i = Line + IO[SCR2Y];
        OffsetY = (i & 0x07);

        pbTMap = Scr2TMap + ((i & 0xF8) << 3);
        TMapX = (IO[SCR2X] & 0xF8) >> 2;
        TMapXEnd = ((IO[SCR2X] + 224 + 7) >> 2) & 0xFFE;

        pW = WBuf + 8 - OffsetX;
        pZ = ZBuf + 8 - OffsetX;
        
        for(; TMapX < TMapXEnd;)
        {
            TMap = *(pbTMap + (TMapX++ & 0x3F));
            TMap |= *(pbTMap + (TMapX++ & 0x3F)) << 8;

            if(IO[COLCTL] & 0x40)
            {
                if(TMap & MAP_BANK) pbTData = IRAM + 0x8000;
                else pbTData = IRAM + 0x4000;
                pbTData += (TMap & MAP_TILE) << 5;
                if(TMap & MAP_VREV) pbTData += (7 - OffsetY) << 2;
                else pbTData += OffsetY << 2;
            }
            else
            {
                if((IO[COLCTL] & 0x80) && (TMap & MAP_BANK))	/* 4 colors and bank 1 */
                     pbTData = IRAM + 0x4000;
                else pbTData = IRAM + 0x2000;
                pbTData += (TMap & MAP_TILE) << 4;
                if(TMap & MAP_VREV) pbTData += (7 - OffsetY) << 1;
                else pbTData += OffsetY << 1;
            }

	    MakeIndex();
            PalIndex = (TMap & MAP_PAL) >> 9;
	    TMapTemp = ((TMap & 0x0800) || (IO[COLCTL] & 0x40));

            /* Same lift as the BG layer: hoist the palette row, split the tile's
             * constant TMapTemp out of the 8 writes. The per-pixel window mask
             * (pW) still gates each store, and pZ is still set on every write, so
             * the result is identical (rig-verified) — the redundant re-work per
             * pixel is what's removed. */
            {
                const uint16_t *pal = Palette[PalIndex];
                if (TMapTemp) {
                    if (Index[0] && !pW[0]) { pSWrBuf[0] = pal[Index[0]]; pZ[0] = 1; }
                    if (Index[1] && !pW[1]) { pSWrBuf[1] = pal[Index[1]]; pZ[1] = 1; }
                    if (Index[2] && !pW[2]) { pSWrBuf[2] = pal[Index[2]]; pZ[2] = 1; }
                    if (Index[3] && !pW[3]) { pSWrBuf[3] = pal[Index[3]]; pZ[3] = 1; }
                    if (Index[4] && !pW[4]) { pSWrBuf[4] = pal[Index[4]]; pZ[4] = 1; }
                    if (Index[5] && !pW[5]) { pSWrBuf[5] = pal[Index[5]]; pZ[5] = 1; }
                    if (Index[6] && !pW[6]) { pSWrBuf[6] = pal[Index[6]]; pZ[6] = 1; }
                    if (Index[7] && !pW[7]) { pSWrBuf[7] = pal[Index[7]]; pZ[7] = 1; }
                } else {
                    if (!pW[0]) { pSWrBuf[0] = pal[Index[0]]; pZ[0] = 1; }
                    if (!pW[1]) { pSWrBuf[1] = pal[Index[1]]; pZ[1] = 1; }
                    if (!pW[2]) { pSWrBuf[2] = pal[Index[2]]; pZ[2] = 1; }
                    if (!pW[3]) { pSWrBuf[3] = pal[Index[3]]; pZ[3] = 1; }
                    if (!pW[4]) { pSWrBuf[4] = pal[Index[4]]; pZ[4] = 1; }
                    if (!pW[5]) { pSWrBuf[5] = pal[Index[5]]; pZ[5] = 1; }
                    if (!pW[6]) { pSWrBuf[6] = pal[Index[6]]; pZ[6] = 1; }
                    if (!pW[7]) { pSWrBuf[7] = pal[Index[7]]; pZ[7] = 1; }
                }
            }
            pW+=8; pZ+=8; pSWrBuf+=8;
        }
    }
/*********************************************************************/
    if((IO[DSPCTL] & 0x04) && Layer[2])         /* Sprites */
    {
        if (IO[DSPCTL] & 0x08)     /* Sprite window */
        {
            memset(WBuf, 1, sizeof(WBuf));   /* pads too — see the FG note above */
            if ((Line >= IO[SPRWT]) && (Line <= IO[SPRWB]))
                { for (i = IO[SPRWL], pW = WBuf + 8 + i; (i <= IO[SPRWR]) && (i < 224); i++) *pW++ = 0; }
        }

        for (pbTMap = SprETMap; pbTMap >= SprTTMap; pbTMap -= 4)
        {
            if (pbTMap[2] > 0xF8) j = pbTMap[2] - 0x100;
            else j = pbTMap[2];
            if (pbTMap[3] > 0xF8) k = pbTMap[3] - 0x100;
            else k = pbTMap[3];

            if (Line < j) continue;
            if (Line >= j + 8) continue;
            if (224 <= k) continue;

            TMap = pbTMap[0] | (pbTMap[1] << 8);

            if (IO[COLCTL] & 0x40)
            {
                pbTData = IRAM + 0x4000 + ((TMap & SPR_TILE) << 5);
                if (TMap & SPR_VREV) pbTData += (7 - Line + j) << 2;
                else pbTData += (Line - j) << 2;
            }
            else
            {
                pbTData = IRAM + 0x2000 + ((TMap & SPR_TILE) << 4);
                if (TMap & SPR_VREV) pbTData += (7 - Line + j) << 1;
                else pbTData += (Line - j) << 1;
            }

            pSWrBuf = pSBuf + k;
            pW = WBuf + 8 + k;
            pZ = ZBuf + k + 8;
	    MakeIndex();
            PalIndex = ((TMap & SPR_PAL) >> 9) + 8;
	    TMapTemp = ((TMap & 0x0800) || (IO[COLCTL] & 0x40));

            for(i = 0; i < 8; i++)
            {
                if (IO[DSPCTL] & 0x08) {
                    if (TMap & SPR_CLIP) {
                        if (!*(pW+i)) continue;
                    } else {
                        if (*(pW+i)) continue;
                    }
                }
                if ( (!Index[i]) && (TMapTemp) )  continue;
                if ((*(pZ+i)) && (!(TMap & SPR_LAYR))) continue;
                *(pSWrBuf+i) = Palette[PalIndex][Index[i]];
            }
        }
    }
    
}
