/*
    Copyright 2014 StapleButter

    This file is part of blargSnes.

    blargSnes is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    blargSnes is distributed in the hope that it will be useful, but WITHOUT ANY 
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS 
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along 
    with blargSnes. If not, see http://www.gnu.org/licenses/.
*/

#include "snes.h"


//extern u8* TopFB;


u32 Mem_WRAMAddr = 0;
u8 SPC_IOPorts[8];


u16 PPU_VCount = 0;

u16 PPU_ColorTable[0x10000];

u16 PPU_Planar2BPP[0x10000];
u32 PPU_Planar4BPP[0x10000];
u32 PPU_Planar8BPP[0x20000];

// 16+256+16: we leave 16 extra pixels on both sides so we don't have to handle tiles that are partially offscreen
//
// rendering order: sub screen is rendered above main screen.
// sub screen alpha: 128 if color divide is enabled, 255 otherwise
// main screen alpha: 
//  0: no color math
//  128: color math, color divide
//  255: color math
//
// sub screen blending: 1*mainscreen +/- (mainscreen alpha)*subscreen
// src: dstalpha, dst: one
u16 PPU_ColorBuffer[(16+256+16)*2];
u16 PPU_OBJBuffer[256];

u8 PPU_MainPrio[256];
u8 PPU_SubPrio[256];

u16 PPU_CGRAMAddr = 0;
u8 PPU_CGRAMVal = 0;
u16 PPU_CGRAM[256];

u16 PPU_VRAMAddr = 0;
u16 PPU_VRAMPref = 0;
u8 PPU_VRAMInc = 0;
u8 PPU_VRAMStep = 0;
u8 PPU_VRAM[0x10000];

u16 PPU_OAMAddr = 0;
u8 PPU_OAMVal = 0;
u8 PPU_OAMPrio = 0;
u8 PPU_FirstOBJ = 0;
u16 PPU_OAMReload = 0;
u8 PPU_OAM[0x220];

u8 PPU_OBJWidths[16] = 
{
	8, 16,
	8, 32,
	8, 64,
	16, 32,
	16, 64,
	32, 64,
	16, 32,
	16, 32
};
u8 PPU_OBJHeights[16] = 
{
	8, 16,
	8, 32,
	8, 64,
	16, 32,
	16, 64,
	32, 64,
	32, 64,
	32, 32
};

u8* PPU_OBJWidth;
u8* PPU_OBJHeight;


u8 PPU_Mode = 0;

u8 PPU_MainScreen = 0;
u8 PPU_SubScreen = 0;


u16 PPU_SubBackdrop = 0;

u8 PPU_BGOld = 0;
u8 PPU_M7Old = 0;

s16 PPU_MulA = 0;
s8 PPU_MulB = 0;
s32 PPU_MulResult = 0;

s16 PPU_M7A = 0;
s16 PPU_M7B = 0;
s16 PPU_M7C = 0;
s16 PPU_M7D = 0;
s16 PPU_M7RefX = 0;
s16 PPU_M7RefY = 0;
s16 PPU_M7XScroll = 0;
s16 PPU_M7YScroll = 0;


typedef struct
{
	u16* Tileset;
	u16* Tilemap;
	u8 Size;
	
	u16 XScroll, YScroll;

} PPU_Background;
PPU_Background PPU_BG[4];

u16* PPU_OBJTileset;
u32 PPU_OBJGap;


/*Handle fbCopyThread = NULL;
u8 fbCopyThreadStack[0x400]; // that thread isn't going to need a huge stack
void PPU_FBCopyThread(u32 arg);
int fbCopy = 0;*/



void PPU_Init()
{
	int i;
	
	// map SNES 15bit color to 3DS 15bit
	for (i = 0; i < 0x10000; i++)
	{
		int r = i & 0x001F;
		int g = i & 0x03E0;
		int b = i & 0x7C00;
		PPU_ColorTable[i] = 0x0001 | (r << 11) | (g << 1) | (b >> 9);
		//u32 val = (r << 19) | (g << 6) | (b >> 7);
		//PPU_ColorTable[i] = val | ((val >> 5) & 0x00070707);
	}
	
	for (i = 0; i < 0x10000; i++)
	{
		u16 val = 0;
		if (i & 0x0001) val |= 0x0001;
		if (i & 0x0100) val |= 0x0002;
		if (i & 0x0002) val |= 0x0004;
		if (i & 0x0200) val |= 0x0008;
		if (i & 0x0004) val |= 0x0010;
		if (i & 0x0400) val |= 0x0020;
		if (i & 0x0008) val |= 0x0040;
		if (i & 0x0800) val |= 0x0080;
		if (i & 0x0010) val |= 0x0100;
		if (i & 0x1000) val |= 0x0200;
		if (i & 0x0020) val |= 0x0400;
		if (i & 0x2000) val |= 0x0800;
		if (i & 0x0040) val |= 0x1000;
		if (i & 0x4000) val |= 0x2000;
		if (i & 0x0080) val |= 0x4000;
		if (i & 0x8000) val |= 0x8000;
		
		PPU_Planar2BPP[i] = val;
	}
	
	for (i = 0; i < 0x10000; i++)
	{
		u32 val = 0;
		if (i & 0x0001) val |= 0x00000001;
		if (i & 0x0100) val |= 0x00000002;
		if (i & 0x0002) val |= 0x00000010;
		if (i & 0x0200) val |= 0x00000020;
		if (i & 0x0004) val |= 0x00000100;
		if (i & 0x0400) val |= 0x00000200;
		if (i & 0x0008) val |= 0x00001000;
		if (i & 0x0800) val |= 0x00002000;
		if (i & 0x0010) val |= 0x00010000;
		if (i & 0x1000) val |= 0x00020000;
		if (i & 0x0020) val |= 0x00100000;
		if (i & 0x2000) val |= 0x00200000;
		if (i & 0x0040) val |= 0x01000000;
		if (i & 0x4000) val |= 0x02000000;
		if (i & 0x0080) val |= 0x10000000;
		if (i & 0x8000) val |= 0x20000000;
		
		PPU_Planar4BPP[i] = val;
	}
}

void PPU_Reset()
{
	int i;
	
	//PPU_MainBuffer = (u16*)MemAlloc(256 * 2);
	
	memset(PPU_VRAM, 0, 0x10000);
	memset(PPU_CGRAM, 0, 0x200);
	memset(PPU_OAM, 0, 0x220);
	
	memset(&PPU_BG[0], 0, sizeof(PPU_Background)*4);
	
	for (i = 0; i < 4; i++)
	{
		PPU_BG[i].Tileset = (u16*)PPU_VRAM;
		PPU_BG[i].Tilemap = (u16*)PPU_VRAM;
	}
	
	PPU_OBJTileset = (u16*)PPU_VRAM;
	
	PPU_OBJWidth = &PPU_OBJWidths[0];
	PPU_OBJHeight = &PPU_OBJHeights[0];
	
	/*fbCopy = 0;
	Result res;
	if (!fbCopyThread)
		res = svcCreateThread(&fbCopyThread, PPU_FBCopyThread, 0, (u32*)&fbCopyThreadStack[0x400], 0x3F, 1);
	bprintf("copythread=%08X\n", res);*/
}


inline void PPU_SetXScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU_M7XScroll = (s16)((val << 8) | PPU_M7Old);
		PPU_M7Old = val;
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->XScroll = (val << 8) | (PPU_BGOld & 0xFFF8) | ((bg->XScroll >> 8) & 0x7);
	PPU_BGOld = val;
}

inline void PPU_SetYScroll(int nbg, u8 val)
{
	if (nbg == 0)
	{
		PPU_M7YScroll = (s16)((val << 8) | PPU_M7Old);
		PPU_M7Old = val;
	}
	
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->YScroll = (val << 8) | PPU_BGOld;
	PPU_BGOld = val;
}

inline void PPU_SetBGSCR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->Size = (val & 0x03);
	bg->Tilemap = (u16*)&PPU_VRAM[(val & 0xFC) << 9];
}

inline void PPU_SetBGCHR(int nbg, u8 val)
{
	PPU_Background* bg = &PPU_BG[nbg];
	
	bg->Tileset = (u16*)&PPU_VRAM[val << 13];
}

extern u32 debugpc;
u8 PPU_Read8(u32 addr)
{
	u8 ret = 0;
	switch (addr)
	{
		case 0x34: ret = PPU_MulResult & 0xFF; break;
		case 0x35: ret = (PPU_MulResult >> 8) & 0xFF; break;
		case 0x36: ret = (PPU_MulResult >> 16) & 0xFF; break;
		
		/*case 0x37:
			PPU_LatchHVCounters();
			break;*/
			
		case 0x38:
			if (PPU_OAMAddr >= 0x200)
				ret = PPU_OAM[PPU_OAMAddr & 0x21F];
			else
				ret = PPU_OAM[PPU_OAMAddr];
			PPU_OAMAddr++;
			PPU_OAMAddr &= ~0x400;
			break;
		
		case 0x39:
			{
				ret = PPU_VRAMPref & 0xFF;
				if (!(PPU_VRAMInc & 0x80))
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
		case 0x3A:
			{
				ret = PPU_VRAMPref >> 8;
				if (PPU_VRAMInc & 0x80)
				{
					PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
					PPU_VRAMAddr += PPU_VRAMStep;
				}
			}
			break;
			
		/*case 0x3C:
			if (PPU_OPHFlag)
			{
				PPU_OPHFlag = 0;
				ret = PPU_OPHCT >> 8;
			}
			else
			{
				PPU_OPHFlag = 1;
				ret = PPU_OPHCT & 0xFF;
			}
			break;
		case 0x3D:
			if (PPU_OPVFlag)
			{
				PPU_OPVFlag = 0;
				ret = PPU_OPVCT >> 8;
			}
			else
			{
				PPU_OPVFlag = 1;
				ret = PPU_OPVCT & 0xFF;
			}
			break;*/
			
		case 0x3E: ret = 0x01; break;
		case 0x3F: 
			ret = 0x01 | (ROM_Region ? 0x10 : 0x00);// | PPU_OPLatch;
			/*PPU_OPLatch = 0;
			PPU_OPHFlag = 0;
			PPU_OPVFlag = 0;*/
			break;
		
		case 0x40: ret = SPC_IOPorts[4]; break;
		case 0x41: ret = SPC_IOPorts[5]; break;
		case 0x42: ret = SPC_IOPorts[6]; break;
		case 0x43: ret = SPC_IOPorts[7]; break;
		
		case 0x80: ret = SNES_SysRAM[Mem_WRAMAddr++]; Mem_WRAMAddr &= ~0x20000; break;
	}

	return ret;
}

u16 PPU_Read16(u32 addr)
{
	u16 ret = 0;
	switch (addr)
	{
		// not in the right place, but well
		// our I/O functions are mapped to the whole $21xx range
		
		case 0x40: ret = *(u16*)&SPC_IOPorts[4]; break;
		case 0x42: ret = *(u16*)&SPC_IOPorts[6]; break;
		
		default:
			ret = PPU_Read8(addr);
			ret |= (PPU_Read8(addr+1) << 8);
			break;
	}

	return ret;
}

void PPU_Write8(u32 addr, u8 val)
{
	switch (addr)
	{
		/*case 0x00: // force blank/master brightness
			{
				u16 mb;
				
				if (val & 0x80) val = 0;
				else val &= 0x0F;
				if (val == 0x0F)
					mb = 0x0000;
				else if (val == 0x00)
					mb = 0x8010;
				else
					mb = 0x8000 | (15 - (val & 0x0F));
					
				PPU_ScheduleLineChange(PPU_SetMasterBright, mb);
			}
			break;*/
			
		case 0x01:
			{
				PPU_OBJWidth = &PPU_OBJWidths[(val & 0xE0) >> 4];
				PPU_OBJHeight = &PPU_OBJHeights[(val & 0xE0) >> 4];
				
				PPU_OBJTileset = (u16*)&PPU_VRAM[(val & 0x03) << 14];
				PPU_OBJGap = (val & 0x1C) << 9;
			}
			break;
			
		case 0x02:
			PPU_OAMAddr = (PPU_OAMAddr & 0x200) | (val << 1);
			PPU_OAMReload = PPU_OAMAddr;
			PPU_FirstOBJ = PPU_OAMPrio ? ((PPU_OAMAddr >> 1) & 0x7F) : 0;
			break;
		case 0x03:
			PPU_OAMAddr = (PPU_OAMAddr & 0x1FE) | ((val & 0x01) << 9);
			PPU_OAMPrio = val & 0x80;
			PPU_OAMReload = PPU_OAMAddr;
			PPU_FirstOBJ = PPU_OAMPrio ? ((PPU_OAMAddr >> 1) & 0x7F) : 0;
			break;
			
		case 0x04:
			if (PPU_OAMAddr >= 0x200)
			{
				PPU_OAM[PPU_OAMAddr & 0x21F] = val;
			}
			else if (PPU_OAMAddr & 0x1)
			{
				*(u16*)&PPU_OAM[PPU_OAMAddr - 1] = PPU_OAMVal | (val << 8);
			}
			else
			{
				PPU_OAMVal = val;
			}
			PPU_OAMAddr++;
			PPU_OAMAddr &= ~0x400;
			break;
			
		case 0x05:
			PPU_Mode = val;
			break;
			
		/*case 0x06: // mosaic
			{
				if (PPU_ModeNow == 7)
				{
					*(u16*)0x0400000C = 0xC082 | (2 << 2) | (24 << 8) | (val & 0x01) << 6;
				}
				else
				{
					int i;
					for (i = 0; i < 4; i++)
					{
						PPU_Background* bg = &PPU_BG[i];
						if (val & (1 << i)) bg->BGCnt |= 0x0040;
						else bg->BGCnt &= 0xFFFFFFBF;
						
						*bg->BGCNT = bg->BGCnt;
					}
				}
			
				*(vu16*)0x0400004C = (val & 0xF0) | (val >> 4);
			}
			break;*/
			
		case 0x07: PPU_SetBGSCR(0, val); break;
		case 0x08: PPU_SetBGSCR(1, val); break;
		case 0x09: PPU_SetBGSCR(2, val); break;
		case 0x0A: PPU_SetBGSCR(3, val); break;
			
			
		case 0x0B:
			PPU_SetBGCHR(0, val & 0x0F);
			PPU_SetBGCHR(1, val >> 4);
			break;
		case 0x0C:
			PPU_SetBGCHR(2, val & 0x0F);
			PPU_SetBGCHR(3, val >> 4);
			break;
			
		
		case 0x0D: bprintf("BG1X %04X %06X\n", PPU_BG[0].XScroll, debugpc); PPU_SetXScroll(0, val); break;
		case 0x0E: PPU_SetYScroll(0, val); break;
		case 0x0F: PPU_SetXScroll(1, val); break;
		case 0x10: PPU_SetYScroll(1, val); break;
		case 0x11: PPU_SetXScroll(2, val); break;
		case 0x12: PPU_SetYScroll(2, val); break;
		case 0x13: PPU_SetXScroll(3, val); break;
		case 0x14: PPU_SetYScroll(3, val); break;
			
		
		case 0x15:
			if ((val & 0x7C) != 0x00) bprintf("UNSUPPORTED VRAM MODE %02X\n", val);
			PPU_VRAMInc = val;
			switch (val & 0x03)
			{
				case 0x00: PPU_VRAMStep = 2; break;
				case 0x01: PPU_VRAMStep = 64; break;
				case 0x02:
				case 0x03: PPU_VRAMStep = 256; break;
			}
			break;
			
		case 0x16:
			PPU_VRAMAddr &= 0xFE00;
			PPU_VRAMAddr |= (val << 1);
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
		case 0x17:
			PPU_VRAMAddr &= 0x01FE;
			PPU_VRAMAddr |= ((val & 0x7F) << 9);
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
		
		case 0x18: // VRAM shit
			{
				PPU_VRAM[PPU_VRAMAddr] = val;
				if (!(PPU_VRAMInc & 0x80))
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
		case 0x19:
			{
				PPU_VRAM[PPU_VRAMAddr+1] = val;
				if (PPU_VRAMInc & 0x80)
					PPU_VRAMAddr += PPU_VRAMStep;
			}
			break;
			
		case 0x1B: // multiply/mode7 shiz
			{
				u16 fval = (u16)(PPU_M7Old | (val << 8));
				PPU_MulA = (s16)fval;
				PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
				PPU_M7A = (s16)fval;
				PPU_M7Old = val;
			}
			break;
		case 0x1C:
			PPU_M7B = (s16)((val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			PPU_MulB = (s8)val;
			PPU_MulResult = (s32)PPU_MulA * (s32)PPU_MulB;
			break;
		case 0x1D:
			PPU_M7C = (s16)((val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x1E:
			PPU_M7D = (s16)((val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
			
		case 0x1F: // mode7 center
			PPU_M7RefX = (s16)((val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
		case 0x20:
			PPU_M7RefY = (s16)((val << 8) | PPU_M7Old);
			PPU_M7Old = val;
			break;
			
		case 0x21:
			PPU_CGRAMAddr = val << 1;
			break;
		
		case 0x22:
			if (!(PPU_CGRAMAddr & 0x1))
				PPU_CGRAMVal = val;
			else
				PPU_CGRAM[PPU_CGRAMAddr >> 1] = (val << 8) | PPU_CGRAMVal;

			PPU_CGRAMAddr++;
			PPU_CGRAMAddr &= ~0x200; // prevent overflow
			break;
			
		case 0x2C:
			PPU_MainScreen = val;
			break;
		case 0x2D:
			PPU_SubScreen = val;
			break;
		
		/*case 0x31:
			PPU_ScheduleLineChange(PPU_SetColorMath, val);
			break;*/
			
		case 0x32:
			{
				u8 intensity = val & 0x1F;
				if (val & 0x20) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x001F) | intensity;
				if (val & 0x40) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x03E0) | (intensity << 5);
				if (val & 0x80) PPU_SubBackdrop = (PPU_SubBackdrop & ~0x7C00) | (intensity << 10);
			}
			break;
			
		/*case 0x33: // SETINI
			if (val & 0x80) iprintf("!! PPU EXT SYNC\n");
			if (val & 0x40) iprintf("!! MODE7 EXTBG\n");
			if (val & 0x08) iprintf("!! PSEUDO HIRES\n");
			if (val & 0x02) iprintf("!! SMALL SPRITES\n");
			break;*/
			
		case 0x40: SPC_IOPorts[0] = val; break;
		case 0x41: SPC_IOPorts[1] = val; break;
		case 0x42: SPC_IOPorts[2] = val; break;
		case 0x43: SPC_IOPorts[3] = val; break;
		
		case 0x80: SNES_SysRAM[Mem_WRAMAddr++] = val; Mem_WRAMAddr &= ~0x20000; break;
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0001FF00) | val; break;
		case 0x82: Mem_WRAMAddr = (Mem_WRAMAddr & 0x000100FF) | (val << 8); break;
		case 0x83: Mem_WRAMAddr = (Mem_WRAMAddr & 0x0000FFFF) | ((val & 0x01) << 16); break;
				
		default:
			//iprintf("PPU_Write8(%08X, %08X)\n", addr, val);
			break;
	}
}

void PPU_Write16(u32 addr, u16 val)
{
	switch (addr)
	{
		// optimized route
		
		case 0x16:
			PPU_VRAMAddr = (val << 1) & 0xFFFEFFFF;
			PPU_VRAMPref = *(u16*)&PPU_VRAM[PPU_VRAMAddr];
			break;
			
		case 0x18:
			*(u16*)&PPU_VRAM[PPU_VRAMAddr] = val;
			PPU_VRAMAddr += PPU_VRAMStep;
			break;
			
		case 0x40: *(u16*)&SPC_IOPorts[0] = val; break;
		case 0x41: *(u16*)&SPC_IOPorts[1] = val; break;
		case 0x42: *(u16*)&SPC_IOPorts[2] = val; break;
		
		case 0x43: bprintf("!! write $21%02X %04X\n", addr, val); break;
		
		case 0x81: Mem_WRAMAddr = (Mem_WRAMAddr & 0x00010000) | val; break;
		
		// otherwise, just do two 8bit writes
		default:
			PPU_Write8(addr, val & 0x00FF);
			PPU_Write8(addr+1, val >> 8);
			break;
	}
}



#define PRIO_HIGH 0x2000
#define PRIO_LOW 0

void PPU_RenderBG_2bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 prio)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	u32 colorval;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = -(xoff & 0x7);
	
	for (; i < 256;)
	{
		idx = (xoff & 0xF8) >> 3;
		if (xoff & 0x100)
		{
			if (bg->Size & 0x1)
				idx += 1024;
		}
		curtile = tilemap[idx];
		
		// skip tiles that don't have the wanted prio
		if ((curtile ^ prio) & 0x2000)
		{
			i += 8;
			xoff += 8;
			continue;
		}
		
		idx = (curtile & 0x3FF) << 3;
		if (curtile & 0x8000) 	idx += (7 - tiley);
		else					idx += tiley;
		
		tilepixels = tileset[idx];
		if (!tilepixels)	// skip empty tiles
		{
			i += 8;
			xoff += 8;
			continue;
		}
		tilepixels = PPU_Planar2BPP[tilepixels];
		
		// render all the possible pixels
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (curtile & 0x4000) // hflip
			{
				colorval = (tilepixels & 0x3);
				tilepixels >>= 2;
			}
			else
			{
				colorval = (tilepixels & 0xC000) >> 14;
				tilepixels <<= 2;
			}
			
			if (colorval)
			{
				colorval |= (curtile & 0x1C00) >> 8;
				buffer[i<<1] = pal[colorval];
			}
			i++;
		}
		xoff += 8;
	}
}

void PPU_RenderBG_4bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 prio)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	u32 colorval;
	s32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	i = -(xoff & 0x7);
	
	for (; i < 256;)
	{
		idx = (xoff & 0xF8) >> 3;
		if (xoff & 0x100)
		{
			if (bg->Size & 0x1)
				idx += 1024;
		}
		curtile = tilemap[idx];
		
		// skip tiles that don't have the wanted prio
		if ((curtile ^ prio) & 0x2000)
		{
			i += 8;
			xoff += 8;
			continue;
		}
		
		idx = (curtile & 0x3FF) << 4;
		if (curtile & 0x8000) 	idx += (7 - tiley);
		else					idx += tiley;
		
		tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		if (!tilepixels)	// skip empty tiles
		{
			i += 8;
			xoff += 8;
			continue;
		}
		tilepixels = PPU_Planar4BPP[tilepixels&0xFFFF] | (PPU_Planar4BPP[tilepixels>>16] << 2);
		
		// render all the possible pixels
		for (idx = 0; idx < 8; idx++)
		{
			colorval = 0;
			if (curtile & 0x4000) // hflip
			{
				colorval = (tilepixels & 0xF);
				tilepixels >>= 4;
			}
			else
			{
				colorval = (tilepixels & 0xF0000000) >> 28;
				tilepixels <<= 4;
			}
			
			if (colorval)
			{
				colorval |= (curtile & 0x1C00) >> 6;
				buffer[i<<1] = pal[colorval];
			}
			i++;
		}
		xoff += 8;
	}
}

// TODO make this one work! we all want Super Bomberman
void PPU_RenderBG_4bpp_16x16(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 prio)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels;
	u32 colorval;
	u32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0x1F0) << 1;
	if (line & 0x200)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	
	for (i = 0; i < 256;)
	{
		// determine the next tile
		for (;;)
		{
			idx = (xoff & 0x1F0) >> 4;
			if (xoff & 0x200)
			{
				if (bg->Size & 0x1)
					idx += 1024;
			}
			curtile = tilemap[idx];
			
			// skip tiles that don't have the wanted prio
			if ((curtile ^ prio) & 0x2000)
			{
				u32 npix = 16 - (xoff & 0xF);
				i += npix;
				if (i >= 256) return;
				xoff += npix;
				continue;
			}
			
			idx = (curtile & 0x3FF) << 4;
			if (curtile & 0x8000) 
			{
				idx += (7 - tiley);
				if (!(line & 0x08)) idx += 256;
			}
			else
			{
				idx += tiley;
				if (line & 0x08) idx += 256;
			}
			
			if ((curtile & 0x4000) ^ ((xoff & 0x8) << 11)) idx += 16;
			tilepixels = tileset[idx] | (tileset[idx+8] << 16);
			/*if (!tilepixels)	// skip empty tiles
			{
				u32 npix = 8 - (xoff & 0x7);
				i += npix;
				if (i >= 256) return;
				xoff += npix;
				continue;
			}*/
			
			if (curtile & 0x4000)	tilepixels >>= (xoff & 0x7);
			else					tilepixels <<= (xoff & 0x7);
			
			break;
		}
		
		// render all the possible pixels
		do
		{
			colorval = 0;
			if (curtile & 0x4000) // hflip
			{
				if (tilepixels & 0x00000001) colorval |= 0x01;
				if (tilepixels & 0x00000100) colorval |= 0x02;
				if (tilepixels & 0x00010000) colorval |= 0x04;
				if (tilepixels & 0x01000000) colorval |= 0x08;
				tilepixels >>= 1;
			}
			else
			{
				if (tilepixels & 0x00000080) colorval |= 0x01;
				if (tilepixels & 0x00008000) colorval |= 0x02;
				if (tilepixels & 0x00800000) colorval |= 0x04;
				if (tilepixels & 0x80000000) colorval |= 0x08;
				tilepixels <<= 1;
			}
			
			if (colorval)
			{
				colorval |= (curtile & 0x1C00) >> 6;
				buffer[i] = pal[colorval];
			}
			i++;
			if (i >= 256) return;
			
			xoff++;
		}
		while (xoff & 0x7);
		
		// load next tile
		if (curtile & 0x4000) idx -= 16;
		else idx += 16;
		tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		
		if (curtile & 0x4000)	tilepixels >>= (xoff & 0x7);
		else					tilepixels <<= (xoff & 0x7);
		
		do
		{
			colorval = 0;
			if (curtile & 0x4000) // hflip
			{
				if (tilepixels & 0x00000001) colorval |= 0x01;
				if (tilepixels & 0x00000100) colorval |= 0x02;
				if (tilepixels & 0x00010000) colorval |= 0x04;
				if (tilepixels & 0x01000000) colorval |= 0x08;
				tilepixels >>= 1;
			}
			else
			{
				if (tilepixels & 0x00000080) colorval |= 0x01;
				if (tilepixels & 0x00008000) colorval |= 0x02;
				if (tilepixels & 0x00800000) colorval |= 0x04;
				if (tilepixels & 0x80000000) colorval |= 0x08;
				tilepixels <<= 1;
			}
			
			if (colorval)
			{
				colorval |= (curtile & 0x1C00) >> 6;
				buffer[i] = pal[colorval];
			}
			i++;
			if (i >= 256) return;
			
			xoff++;
		}
		while (xoff & 0x7);
	}
}

void PPU_RenderBG_8bpp_8x8(PPU_Background* bg, u16* buffer, u32 line, u16* pal, u32 prio)
{
	u16* tileset = bg->Tileset;
	u16* tilemap = bg->Tilemap;
	u32 xoff;
	u32 tiley;
	u16 curtile;
	u32 tilepixels1, tilepixels2;
	u32 colorval;
	u32 i;
	u32 idx;
	
	line += bg->YScroll;
	tiley = (line & 0x07);
	tilemap += (line & 0xF8) << 2;
	if (line & 0x100)
	{
		if (bg->Size & 0x2)
			tilemap += (bg->Size & 0x1) ? 2048 : 1024;
	}
	
	xoff = bg->XScroll;
	
	for (i = 0; i < 256;)
	{
		// determine the next tile
		for (;;)
		{
			idx = (xoff & 0xF8) >> 3;
			if (xoff & 0x100)
			{
				if (bg->Size & 0x1)
					idx += 1024;
			}
			curtile = tilemap[idx];
			
			// skip tiles that don't have the wanted prio
			if ((curtile ^ prio) & 0x2000)
			{
				u32 npix = 8 - (xoff & 0x7);
				i += npix;
				if (i >= 256) return;
				xoff += npix;
				continue;
			}
			
			idx = (curtile & 0x3FF) << 5;
			if (curtile & 0x8000) 	idx += (7 - tiley);
			else					idx += tiley;
			
			tilepixels1 = tileset[idx] | (tileset[idx+8] << 16);
			tilepixels2 = tileset[idx+16] | (tileset[idx+24] << 16);
			if (!(tilepixels1|tilepixels2))	// skip empty tiles
			{
				u32 npix = 8 - (xoff & 0x7);
				i += npix;
				if (i >= 256) return;
				xoff += npix;
				continue;
			}
			
			if (curtile & 0x4000)
			{
				tilepixels1 >>= (xoff & 0x7);
				tilepixels2 >>= (xoff & 0x7);
			}
			else
			{
				tilepixels1 <<= (xoff & 0x7);
				tilepixels2 <<= (xoff & 0x7);
			}
			
			break;
		}
		
		// render all the possible pixels
		do
		{
			colorval = 0;
			if (curtile & 0x4000) // hflip
			{
				if (tilepixels1 & 0x00000001) colorval |= 0x01;
				if (tilepixels1 & 0x00000100) colorval |= 0x02;
				if (tilepixels1 & 0x00010000) colorval |= 0x04;
				if (tilepixels1 & 0x01000000) colorval |= 0x08;
				if (tilepixels2 & 0x00000001) colorval |= 0x10;
				if (tilepixels2 & 0x00000100) colorval |= 0x20;
				if (tilepixels2 & 0x00010000) colorval |= 0x40;
				if (tilepixels2 & 0x01000000) colorval |= 0x80;
				tilepixels1 >>= 1;
				tilepixels2 >>= 1;
			}
			else
			{
				if (tilepixels1 & 0x00000080) colorval |= 0x01;
				if (tilepixels1 & 0x00008000) colorval |= 0x02;
				if (tilepixels1 & 0x00800000) colorval |= 0x04;
				if (tilepixels1 & 0x80000000) colorval |= 0x08;
				if (tilepixels2 & 0x00000080) colorval |= 0x10;
				if (tilepixels2 & 0x00008000) colorval |= 0x20;
				if (tilepixels2 & 0x00800000) colorval |= 0x40;
				if (tilepixels2 & 0x80000000) colorval |= 0x80;
				tilepixels1 <<= 1;
				tilepixels2 <<= 1;
			}
			
			if (colorval)
			{
				buffer[i<<1] = pal[colorval];
			}
			i++;
			if (i >= 256) return;
			
			xoff++;
		}
		while (xoff & 0x7);
	}
}


void PPU_RenderBG_Mode7(u16* buffer, u32 line, u16* pal)
{
	s32 x = (PPU_M7A * (PPU_M7XScroll-PPU_M7RefX)) + (PPU_M7B * (line+PPU_M7YScroll-PPU_M7RefY)) + (PPU_M7RefX << 8);
	s32 y = (PPU_M7C * (PPU_M7XScroll-PPU_M7RefX)) + (PPU_M7D * (line+PPU_M7YScroll-PPU_M7RefY)) + (PPU_M7RefY << 8);
	int i;
	u32 tileidx;
	u8 tilepixels;
	
	for (i = 0; i < 256; i++)
	{
		tileidx = ((x & 0x3F800) >> 10) + ((y & 0x3F800) >> 3);
		
		tileidx = PPU_VRAM[tileidx] << 7;
		tileidx += ((x & 0x700) >> 7) + ((y & 0x700) >> 4) + 1;
		tilepixels = PPU_VRAM[tileidx];
		
		if (tilepixels)
			buffer[i] = pal[tilepixels];
		
		x += PPU_M7A;
		y += PPU_M7C;
	}
}


void PPU_RenderOBJ(u8* oam, u32 oamextra, u32 ymask, u16* buffer, u32 line)
{
	u16* tileset = PPU_OBJTileset;
	s32 xoff;
	u16 attrib;
	u32 tilepixels;
	u32 colorval;
	u32 idx;
	u32 i;
	u8 width = PPU_OBJWidth[(oamextra & 0x2) >> 1];
	u32 paloffset;
	
	attrib = *(u16*)&oam[2];
	
	idx = (attrib & 0x01FF) << 4;
	
	if (attrib & 0x8000) line = ymask - line;
	idx += (line & 0x07) | ((line & 0x38) << 5);
	
	if (attrib & 0x4000)
		idx += ((width-1) & 0x38) << 1;
	
	xoff = oam[0];
	if (oamextra & 0x1) // xpos bit8, sign bit
	{
		xoff = 0x100 - xoff;
		if (xoff >= width) return;
		i = 0;
		
		if (attrib & 0x4000) 	idx -= ((xoff & 0x38) << 1);
		else 					idx += ((xoff & 0x38) << 1);
	}
	else
	{
		i = xoff;
		xoff = 0;
	}
	
	tilepixels = tileset[idx] | (tileset[idx+8] << 16);
	if (attrib & 0x4000)	tilepixels >>= (xoff & 0x7);
	else					tilepixels <<= (xoff & 0x7);
	
	paloffset = (oam[3] & 0x0E) << 3;
	
	while (xoff < width)
	{
		colorval = 0;
		if (attrib & 0x4000) // hflip
		{
			if (tilepixels & 0x00000001) colorval |= 0x01;
			if (tilepixels & 0x00000100) colorval |= 0x02;
			if (tilepixels & 0x00010000) colorval |= 0x04;
			if (tilepixels & 0x01000000) colorval |= 0x08;
			tilepixels >>= 1;
		}
		else
		{
			if (tilepixels & 0x00000080) colorval |= 0x01;
			if (tilepixels & 0x00008000) colorval |= 0x02;
			if (tilepixels & 0x00800000) colorval |= 0x04;
			if (tilepixels & 0x80000000) colorval |= 0x08;
			tilepixels <<= 1;
		}
		
		if (colorval)
		{
			buffer[i] = colorval | paloffset | ((oam[3] & 0x30) << 8);
		}
		
		i++;
		if (i >= 256) return;
		
		xoff++;
		if (!(xoff & 0x7)) // reload tile if needed
		{
			if (attrib & 0x4000) 	idx -= 16;
			else 					idx += 16;
			
			tilepixels = tileset[idx] | (tileset[idx+8] << 16);
		}
	}
}

inline void PPU_PrerenderOBJs(u16* buf, u32 line)
{
	int i = PPU_FirstOBJ;
	i--;
	if (i < 0) i = 127;
	int last = i;
	
	do
	{
		u8* oam = &PPU_OAM[i << 2];
		u8 oamextra = PPU_OAM[0x200 + (i >> 2)] >> ((i & 0x03) << 1);
		int oy = oam[1] + 1;
		u8 oh = PPU_OBJHeight[(oamextra & 0x2) >> 1];
		
		if (line >= oy && line < (oy+oh))
			PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
		/*else if (oy >= 192) // should fix cases of sprites with Y<0 disappearing, but causes garbage to be rendered across the screen
		{
			oy -= 0x100;
			if (line < (oy+oh))
				PPU_RenderOBJ(oam, oamextra, oh-1, buf, line-oy);
		}*/
		
		i--;
		if (i < 0) i = 127;
	}
	while (i != last);
}

inline void PPU_RenderOBJs(u16* buf, u32 line, u32 prio)
{
	int i;
	u32* srcbuf = (u32*)PPU_OBJBuffer;
	u16* pal = &PPU_CGRAM[128];
	
	for (i = 0; i < 256;)
	{
		u32 val = srcbuf[i >> 1];
		
		if ((val & 0xFF00) == prio)
			buf[i<<1] = pal[val & 0xFF];
		
		i++;
		val >>= 16;
		
		if ((val & 0xFF00) == prio)
			buf[i<<1] = pal[val & 0xFF];
		
		i++;
	}
}


void PPU_RenderMode0(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x08) PPU_RenderBG_2bpp_8x8(&PPU_BG[3], buf, line, &PPU_CGRAM[96], PRIO_LOW);
	if (screen & 0x04) PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[64], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x08) PPU_RenderBG_2bpp_8x8(&PPU_BG[3], buf, line, &PPU_CGRAM[96], PRIO_HIGH);
	if (screen & 0x04) PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[64], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000);
	
	if (screen & 0x02) PPU_RenderBG_2bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[32], PRIO_LOW);
	if (screen & 0x01) PPU_RenderBG_2bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000);
	
	if (screen & 0x02) PPU_RenderBG_2bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[32], PRIO_HIGH);
	if (screen & 0x01) PPU_RenderBG_2bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000);
}

void PPU_RenderMode1(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x04) PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x04) 
	{
		if (!(PPU_Mode & 0x08))
			PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	}
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000);
	
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x01) PPU_RenderBG_4bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000);
	
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x01) PPU_RenderBG_4bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000);
	
	if (screen & 0x04)
	{
		if (PPU_Mode & 0x08)
			PPU_RenderBG_2bpp_8x8(&PPU_BG[2], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	}
}

// TODO: offset per tile, someday
void PPU_RenderMode2(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x01) PPU_RenderBG_4bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000);
	
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000);
	
	if (screen & 0x01) PPU_RenderBG_4bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000);
}

void PPU_RenderMode3(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x01) PPU_RenderBG_8bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000);
	
	if (screen & 0x02) PPU_RenderBG_4bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000);
	
	if (screen & 0x01) PPU_RenderBG_8bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000);
}

// TODO: offset per tile, someday
void PPU_RenderMode4(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x02) PPU_RenderBG_2bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x01) PPU_RenderBG_8bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_LOW);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x1000);
	
	if (screen & 0x02) PPU_RenderBG_2bpp_8x8(&PPU_BG[1], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x2000);
	
	if (screen & 0x01) PPU_RenderBG_8bpp_8x8(&PPU_BG[0], buf, line, &PPU_CGRAM[0], PRIO_HIGH);
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x3000);
}

void PPU_RenderMode7(u16* buf, u32 line, u8 screen)
{
	if (screen & 0x10) PPU_RenderOBJs(buf, line, 0x0000);
	
	if (screen & 0x01) PPU_RenderBG_Mode7(buf, line, &PPU_CGRAM[0]);
	
	if (screen & 0x10)
	{
		PPU_RenderOBJs(buf, line, 0x1000);
		PPU_RenderOBJs(buf, line, 0x2000);
		PPU_RenderOBJs(buf, line, 0x3000);
	}
}


int blarg=0;
void PPU_RenderScanline(u32 line)
{
	// test frameskip code
	if (line==0) blarg=!blarg;
	if (blarg)
	{
		return;
	}
	
	int i;
	u16* buf = &PPU_ColorBuffer[0];
	
	// TODO more accurate backdrop (will be needed whenever we do color math)
	u32 backdrop = PPU_SubBackdrop;
	if (!backdrop) backdrop = PPU_CGRAM[0];
	backdrop |= (backdrop << 16);
	for (i = 0; i < (16+256+16)*2; i += 2)
		*(u32*)&buf[i] = backdrop;
		
	for (i = 0; i < 256; i += 2)
		*(u32*)&PPU_OBJBuffer[i] = 0xFFFFFFFF;
		
	if ((PPU_MainScreen|PPU_SubScreen) & 0x10)
		PPU_PrerenderOBJs(PPU_OBJBuffer, line);
		
	buf += 16<<1;
		
	// subscreen calls need to switch to &buf[1] whenever we do color math
	// (unless we do it in hardware)
		
	switch (PPU_Mode & 0x07)
	{
		case 0:
			PPU_RenderMode0(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode0(&buf[0], line, PPU_MainScreen);
			break;
			
		case 1:
			PPU_RenderMode1(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode1(&buf[0], line, PPU_MainScreen);
			break;
		
		case 2:
			PPU_RenderMode2(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode2(&buf[0], line, PPU_MainScreen);
			break;
			
		case 3:
			PPU_RenderMode3(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode3(&buf[0], line, PPU_MainScreen);
			break;
			
		case 4:
			PPU_RenderMode4(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode4(&buf[0], line, PPU_MainScreen);
			break;
			
		// TODO: mode 5/6 (hires)
		
		case 7:
			PPU_RenderMode7(&buf[0], line, PPU_SubScreen);
			PPU_RenderMode7(&buf[0], line, PPU_MainScreen);
			break;
	}
	
	// copy to final framebuffer
	/*u16* finalbuf = &((u16*)TopFB)[17512 - line];
	for (i = 0; i < 256; i++)
	{
		/*u16 maincol = buf[i];
		u16 subcol = buf[i+1];
		
		// color addition
		u16 xor = maincol ^ subcol;
		u16 finalcol = maincol + subcol;
		/*xor = (xor ^ finalcol) & 0x8420;
		if (xor & 0x0020) finalcol |= 0x001F;
		if (xor & 0x0400) finalcol |= 0x03E0;
		if (xor & 0x8000) finalcol |= 0x7C00;
		finalcol ^= xor;*-/
		finalcol >>= 1;
		
		*finalbuf = PPU_ColorTable[finalcol];*-/
		*finalbuf = PPU_ColorTable[buf[i<<1]];
		finalbuf += 240;
	}*/
	
	//svcSignalEvent(fbCopyEvent);
	//fbCopy = 1;
}

/*void PPU_FBCopyThread(u32 arg)
{
	int i;
	u16* buf = &PPU_ColorBuffer[0];
	u16* finalbuf;
	
	for (;;)
	{
		// TODO better wait loop
		while (!fbCopy);
		
		finalbuf = &((u16*)TopFB)[17512 - PPU_VCount];
		for (i = 0; i < 256; i++)
		{
			*finalbuf = PPU_ColorTable[buf[i]];
			finalbuf += 240;
		}
		
		//svcClearEvent(fbCopyEvent);
		fbCopy = 0;
	}
}*/

void PPU_VBlank()
{
	PPU_OAMAddr = PPU_OAMReload;
}
