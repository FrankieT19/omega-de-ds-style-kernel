#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>

#include "ez_define.h"
#include "HZK12.h"
#include "thai620.h"
#include "ezkernel.h"
#include "draw.h"
#include "lang.h"
#include "launcher_font_extended.h"

int current_y = 1;
extern u8 pReadCache [MAX_pReadCache_size]EWRAM_BSS;
//******************************************************************************
void IWRAM_CODE Clear(u16 x, u16 y, u16 w, u16 h, u16 c, u8 isDrawDirect)
{
	u16 *p;
	u16 yi,ww,hh;
    
	if(isDrawDirect)
		p = VideoBuffer;
	else
		p = Vcache;

    hh = (y+h>160)?160:(y+h);
    ww  = (x+w>240)?(240-x):w;

	//u16 tmp[240];
	for(u32 i=0;i<240;i++)
		((u16*)pReadCache)[i] = c;

	for(yi=y; yi < hh; yi++)
		dmaCopy(pReadCache,p+yi*240+x,ww*2);         
}
//******************************************************************************
void IWRAM_CODE ClearWithBG(u16* pbg,u16 x, u16 y, u16 w, u16 h, u8 isDrawDirect)
{
	u16 *p;
	u16 yi,ww,hh;
    
	if(isDrawDirect)
		p = VideoBuffer;
	else
		p = Vcache;

    hh = (y+h>160)?160:(y+h);
    ww  = (x+w>240)?(240-x):w;

	for(yi=y; yi < hh; yi++)
		dmaCopy(pbg+yi*240+x,p+yi*240+x,ww*2);       
}
//******************************************************************************
void IWRAM_CODE DrawPic(u16 *GFX, u16 x, u16 y, u16 w, u16 h, u8 isTrans, u16 tcolor, u8 isDrawDirect)
{
	u16 *p,c;
	u16 xi,yi,ww,hh;

	if(isDrawDirect)
		p = VideoBuffer;
	else
		p = Vcache;
		
  hh = (y+h>160)?160:(y+h);
  ww  = (x+w>240)?(240-x):w;	
	
	if(isTrans)
	{
		for(yi=y; yi < hh; yi++)
			for(xi=x;xi<x+ww;xi++)
			{
				c = GFX[(yi-y)*w+(xi-x)];
				if(c!=tcolor)
					p[yi*240+xi] = c;
			}
	}
	else
	{
		for(yi=y; yi < hh; yi++)
			dmaCopy(GFX+(yi-y)*w,p+yi*240+x,w*2); 
	}
}
//---------------------------------------------------------------------------------
static void DrawAsciiGlyph12(u16 *v, u16 x, u16 y, u16 c, u8 ch)
{
	u8 cc;
	u32 i;
	u32 location = ch * 12;
	u16 yy;

	yy = 240 * y;
	for(i = 0; i < 12; i++)
	{
		cc = ASC_DATA[location + i];
		if(cc & 0x01)
			v[x + 7 + yy] = c;
		if(cc & 0x02)
			v[x + 6 + yy] = c;
		if(cc & 0x04)
			v[x + 5 + yy] = c;
		if(cc & 0x08)
			v[x + 4 + yy] = c;
		if(cc & 0x10)
			v[x + 3 + yy] = c;
		if(cc & 0x20)
			v[x + 2 + yy] = c;
		if(cc & 0x40)
			v[x + 1 + yy] = c;
		if(cc & 0x80)
			v[x + yy] = c;
		yy += 240;
	}
}

static void DrawCustomGlyph12(u16 *v, u16 x, u16 y, u16 c, const u8 *rows)
{
	u32 i;
	u8 cc;
	u16 yy = 240 * y;

	for(i = 0; i < 12; i++)
	{
		cc = rows[i];
		if(cc & 0x80)
			v[x + yy] = c;
		if(cc & 0x40)
			v[x + 1 + yy] = c;
		if(cc & 0x20)
			v[x + 2 + yy] = c;
		if(cc & 0x10)
			v[x + 3 + yy] = c;
		if(cc & 0x08)
			v[x + 4 + yy] = c;
		if(cc & 0x04)
			v[x + 5 + yy] = c;
		yy += 240;
	}
}

static void DrawThaiRows12(u16 *v, u16 x, u16 y, u16 c, const u8 *rows)
{
	u32 i;
	u8 cc;
	u16 *p = v + 240 * y + x;
	for(i = 0; i < 12; i++, p += 240)
	{
		cc = rows[i];
		if(!cc) continue;
		if(cc & 0x80) p[0] = c;
		if(cc & 0x40) p[1] = c;
		if(cc & 0x20) p[2] = c;
		if(cc & 0x10) p[3] = c;
		if(cc & 0x08) p[4] = c;
		if(cc & 0x04) p[5] = c;
		if(cc & 0x02) p[6] = c;
		if(cc & 0x01) p[7] = c;
	}
}

static u32 DecodeUtf8Text12(char *str, u32 l, u32 *hi, u8 c1, u32 *codepoint)
{
	u8 c2;
	u8 c3;

	if((c1 >= 0xC2) && (c1 <= 0xDF) && (*hi < l))
	{
		c2 = str[*hi];
		if((c2 & 0xC0) == 0x80)
		{
			(*hi)++;
			*codepoint = ((u32)(c1 & 0x1F) << 6) | (u32)(c2 & 0x3F);
			return 1;
		}
	}
	else if((c1 >= 0xE0) && (c1 <= 0xEF) && ((*hi + 1) < l))
	{
		c2 = str[*hi];
		c3 = str[*hi + 1];
		if(((c2 & 0xC0) == 0x80) && ((c3 & 0xC0) == 0x80))
		{
			*hi += 2;
			*codepoint = ((u32)(c1 & 0x0F) << 12) | ((u32)(c2 & 0x3F) << 6) | (u32)(c3 & 0x3F);
			return 1;
		}
	}

	return 0;
}

static u32 DecodeCp936LatinText12(char *str, u32 l, u32 *hi, u8 c1, u32 *codepoint)
{
	u8 c2;

	if((c1 != 0xA8) || (*hi >= l))
		return 0;

	c2 = str[*hi];
	switch(c2)
	{
		case 0xA2: *codepoint = 0x00E1; break; /* a acute */
		case 0xA4: *codepoint = 0x00E0; break; /* a grave */
		case 0xA6: *codepoint = 0x00E9; break; /* e acute */
		case 0xA8: *codepoint = 0x00E8; break; /* e grave */
		case 0xAA: *codepoint = 0x00ED; break; /* i acute */
		case 0xAC: *codepoint = 0x00EC; break; /* i grave */
		case 0xAE: *codepoint = 0x00F3; break; /* o acute */
		case 0xB0: *codepoint = 0x00F2; break; /* o grave */
		case 0xB2: *codepoint = 0x00FA; break; /* u acute */
		case 0xB4: *codepoint = 0x00F9; break; /* u grave */
		case 0xB9: *codepoint = 0x00FC; break; /* u diaeresis */
		case 0xBA: *codepoint = 0x00EA; break; /* e circumflex */
		default: return 0;
	}

	(*hi)++;
	return 1;
}

static u32 ThaiIsLeadingVowel(u32 codepoint)
{
	return (codepoint >= 0x0E40u) && (codepoint <= 0x0E44u);
}

static u32 ThaiReadCodepointAt(char *str, u32 l, u32 *hi, u32 *codepoint)
{
	u8 c1;

	if(*hi >= l)
		return 0;
	c1 = (u8)str[*hi];
	if(c1 < 0x80)
		return 0;
	(*hi)++;
	return DecodeUtf8Text12(str, l, hi, c1, codepoint);
}

static void ThaiConsumeMarks(char *str, u32 l, u32 *hi)
{
	while(*hi < l)
	{
		u32 cp2;
		u32 hi2 = *hi;
		if(!ThaiReadCodepointAt(str, l, &hi2, &cp2))
			break;
		if(cp2 < THAI_CP_FIRST || cp2 > THAI_CP_LAST ||
		   THAI_WIDTH[cp2 - THAI_CP_FIRST] > 0)
			break;
		*hi = hi2;
	}
}

static u32 ThaiConsumeVisibleCluster(char *str, u32 l, u32 *hi, u32 codepoint)
{
	if(ThaiIsLeadingVowel(codepoint))
	{
		u32 cp2;
		u32 hi2 = *hi;
		if(ThaiReadCodepointAt(str, l, &hi2, &cp2) &&
		   cp2 >= THAI_CP_FIRST && cp2 <= THAI_CP_LAST &&
		   THAI_WIDTH[cp2 - THAI_CP_FIRST] > 0)
		{
			*hi = hi2;
			ThaiConsumeMarks(str, l, hi);
			return 1;
		}
	}

	if(THAI_WIDTH[codepoint - THAI_CP_FIRST] > 0 || codepoint == 0x0E33u)
	{
		ThaiConsumeMarks(str, l, hi);
		return 1;
	}

	return 0;
}

static int MapLatinGlyph12(u32 cp)
{
	switch(cp)
	{
		case 0x00C0: return 0;
		case 0x00C1: return 1;
		case 0x00C2: return 2;
		case 0x00C3: return 3;
		case 0x00C4: return 4;
		case 0x00C5: return 5;
		case 0x00C7: return 6;
		case 0x00C8: return 7;
		case 0x00C9: return 8;
		case 0x00CA: return 9;
		case 0x00CB: return 10;
		case 0x00CC: return 11;
		case 0x00CD: return 12;
		case 0x00CE: return 13;
		case 0x00CF: return 14;
		case 0x00D1: return 15;
		case 0x00D2: return 16;
		case 0x00D3: return 17;
		case 0x00D4: return 18;
		case 0x00D5: return 19;
		case 0x00D6: return 20;
		case 0x00D9: return 21;
		case 0x00DA: return 22;
		case 0x00DB: return 23;
		case 0x00DC: return 24;
		case 0x00DD: return 25;
		case 0x00DF: return 26;
		case 0x00E0: return 27;
		case 0x00E1: return 28;
		case 0x00E2: return 29;
		case 0x00E3: return 30;
		case 0x00E4: return 31;
		case 0x00E5: return 32;
		case 0x00E7: return 33;
		case 0x00E8: return 34;
		case 0x00E9: return 35;
		case 0x00EA: return 36;
		case 0x00EB: return 37;
		case 0x00EC: return 38;
		case 0x00ED: return 39;
		case 0x00EE: return 40;
		case 0x00EF: return 41;
		case 0x00F1: return 42;
		case 0x00F2: return 43;
		case 0x00F3: return 44;
		case 0x00F4: return 45;
		case 0x00F5: return 46;
		case 0x00F6: return 47;
		case 0x00F9: return 48;
		case 0x00FA: return 49;
		case 0x00FB: return 50;
		case 0x00FC: return 51;
		case 0x00FD: return 52;
		case 0x00FF: return 53;
		case 0x011E: return 54;
		case 0x011F: return 55;
		case 0x0130: return 56;
		case 0x0131: return 57;
		case 0x015E: return 58;
		case 0x015F: return 59;
		default: break;
	}

	return -1;
}

u16 DrawText12VisibleLength(char *str)
{
	u32 l = strlen(str);
	u32 hi = 0;
	u16 shown = 0;

	while(hi < l)
	{
		u8 c1 = str[hi++];
		if(c1 < 0x80)
		{
			shown++;
		}
		else
		{
			u32 codepoint;
			u32 hi_save = hi;
			if(gl_select_lang == THAI_CP_FIRST &&
			   DecodeUtf8Text12(str, l, &hi, c1, &codepoint) &&
			   codepoint >= THAI_CP_FIRST && codepoint <= THAI_CP_LAST)
			{
				if(ThaiConsumeVisibleCluster(str, l, &hi, codepoint))
				{
					shown++;
					continue;
				}
			}
			else
			{
				hi = hi_save;
				if((gl_select_lang != 0xE2E2) &&
				   (DecodeUtf8Text12(str, l, &hi, c1, &codepoint) ||
				    DecodeCp936LatinText12(str, l, &hi, c1, &codepoint)))
					shown++;
				else if(hi < l)
				{
					hi++;
					shown += 2;
				}
				else
					shown++;
			}
		}
	}

	return shown;
}

u16 DrawText12ByteOffsetForGlyphs(char *str, u16 glyphs)
{
	u32 l = strlen(str);
	u32 hi = 0;
	u16 shown = 0;

	while((hi < l) && (shown < glyphs))
	{
		u8 c1 = str[hi++];
		if(c1 < 0x80)
		{
			shown++;
		}
		else
		{
			u32 codepoint;
			u32 hi_save = hi;
			if(gl_select_lang == THAI_CP_FIRST &&
			   DecodeUtf8Text12(str, l, &hi, c1, &codepoint) &&
			   codepoint >= THAI_CP_FIRST && codepoint <= THAI_CP_LAST)
			{
				if(ThaiConsumeVisibleCluster(str, l, &hi, codepoint))
				{
					shown++;
					continue;
				}
			}
			else
			{
				hi = hi_save;
				if((gl_select_lang != 0xE2E2) &&
				   (DecodeUtf8Text12(str, l, &hi, c1, &codepoint) ||
				    DecodeCp936LatinText12(str, l, &hi, c1, &codepoint)))
					shown++;
				else if(hi < l)
				{
					hi++;
					shown += 2;
				}
				else
					shown++;
			}
		}
	}

	return hi;
}

void DrawText12CopyVisible(char *dst, u16 dst_size, char *src, u16 glyphs)
{
	u16 offset;

	if(dst_size == 0)
		return;

	offset = DrawText12ByteOffsetForGlyphs(src, glyphs);
	if(offset >= dst_size)
		offset = dst_size - 1;
	memcpy(dst, src, offset);
	dst[offset] = 0;
}

static void DrawHZText12Surface(char *str, u16 len, u16 x, u16 y, u16 c, u16 *v)
{
  u32 i,l,hi=0,shown=0;
  u32 location;
	u8 cc,c1,c2;
	u16 yy;

	l=strlen(str);

	if((u16)(len*6)>(u16)(240-x))
		len=(240-x)/6;
    while((hi<l) && ((len == 0) || (shown < len)))
    {
		c1 = str[hi];
    	hi++;
    	if(c1<0x80)  //ASCII
    	{
			DrawAsciiGlyph12(v, x, y, c, c1);
    		x+=6;
			shown++;
    		continue;
    	}
		else	//Double-byte / multi-byte
		{
			u32 codepoint;
			int latin_index;
			u32 hi_save;

			hi_save = hi;
			if(gl_select_lang == THAI_CP_FIRST &&
			   DecodeUtf8Text12(str, l, &hi, c1, &codepoint) &&
			   codepoint >= THAI_CP_FIRST && codepoint <= THAI_CP_LAST)
			{
				const u8 *_tc_pp = (const u8 *)str + hi; /* just after base codepoint */
				if(ThaiIsLeadingVowel(codepoint))
				{
					u32 cp2;
					u32 hi2 = hi;
					if(ThaiReadCodepointAt(str, l, &hi2, &cp2) &&
					   cp2 >= THAI_CP_FIRST && cp2 <= THAI_CP_LAST &&
					   THAI_WIDTH[cp2 - THAI_CP_FIRST] > 0)
					{
						DrawThaiRows12(v, x, y, c, THAI_DATA[codepoint - THAI_CP_FIRST]);
						x += THAI_WIDTH[codepoint - THAI_CP_FIRST];
						codepoint = cp2;
						_tc_pp = (const u8 *)str + hi2;
					}
				}
				#define THAI_R_DRAW(idx, xpos, yoff) DrawThaiRows12(v, (u16)(xpos), (u16)((int)y + (yoff)), c, THAI_DATA[idx])
				#define THAI_R_WIDTH(idx)      ((int)THAI_WIDTH[idx])
				#define THAI_R_CP              codepoint
				#define THAI_R_PP              (&_tc_pp)
				#define THAI_R_X               x
				#define THAI_R_SP              ((int)THAI_CLUSTER_SPACING)
				#include "thai_cluster.h"
				hi = (u32)(_tc_pp - (const u8 *)str);
				shown++;
				continue;
			}
			hi = hi_save;

			if((gl_select_lang != 0xE2E2) &&
			   (DecodeUtf8Text12(str, l, &hi, c1, &codepoint) ||
			    DecodeCp936LatinText12(str, l, &hi, c1, &codepoint)))
			{
				latin_index = MapLatinGlyph12(codepoint);
				if(latin_index >= 0)
					DrawCustomGlyph12(v, x, y, c, LAUNCHER_EXTENDED_LATIN[latin_index]);
				else
					DrawAsciiGlyph12(v, x, y, c, '?');
				x += 6;
				shown++;
				continue;
			}

			if(gl_select_lang != 0xE2E2)
			{
				DrawAsciiGlyph12(v, x, y, c, '?');
				x += 6;
				shown++;
				continue;
			}

			if(hi >= l)
			{
				DrawAsciiGlyph12(v, x, y, c, '?');
				x += 6;
				shown++;
				continue;
			}

    		c2 = str[hi];
    		hi++;
    		if(c1<0xb0){   		
    			location = ((c1-0xa1)*94+(c2-0xa1))*24;
    		}
    		else{
    			location = (9*94+(c1-0xb0)*94+(c2-0xa1))*24;
    		}

			yy = 240*y;
			for(i=0;i<12;i++)
			{				
				cc = acHZK12[location+i*2];
				if(cc & 0x01)
					v[x+7+yy]=c;
				if(cc & 0x02)
					v[x+6+yy]=c;
				if(cc & 0x04)
					v[x+5+yy]=c;
				if(cc & 0x08)
					v[x+4+yy]=c;
				if(cc & 0x10)
					v[x+3+yy]=c;
				if(cc & 0x20)
					v[x+2+yy]=c;
				if(cc & 0x40)
					v[x+1+yy]=c;
				if(cc & 0x80)
					v[x+yy]=c;
								
				cc = acHZK12[location+i*2+1];
				if(cc & 0x01)
					v[x+15+yy]=c;
				if(cc & 0x02)
					v[x+14+yy]=c;
				if(cc & 0x04)
					v[x+13+yy]=c;
				if(cc & 0x08)
					v[x+12+yy]=c;
				if(cc & 0x10)
					v[x+11+yy]=c;
				if(cc & 0x20)
					v[x+10+yy]=c;
				if(cc & 0x40)
					v[x+9+yy]=c;
				if(cc & 0x80)
					v[x+8+yy]=c;
				yy+=240;
			}
			x+=12;
			shown += 2;
		}
	}
}

void DrawHZText12(char *str, u16 len, u16 x, u16 y, u16 c, u8 isDrawDirect)
{
	DrawHZText12Surface(str, len, x, y, c, isDrawDirect ? VideoBuffer : Vcache);
}

void DrawHZText12ToBuffer(char *str, u16 len, u16 x, u16 y, u16 c, u16 *buffer)
{
	if(buffer)
		DrawHZText12Surface(str, len, x, y, c, buffer);
}
//---------------------------------------------------------------------------------
void DEBUG_printf(const char *format, ...)
{
    char str[128];
    va_list va;
    va_start(va, format);
    //vasprintf(str, format, va);
    vsprintf(str, format, va);
    va_end(va);

		if(current_y==1)
			{
				
				Clear(0, 0, 240, 160, 0x0000, 1);
			}

    DrawHZText12(str,0,0,current_y, RGB(31,31,31),1);
    
    //free(str);

    current_y += 12;
    if(current_y>150) 
    {
    	wait_btn();
    	current_y=1;
    }
}
//---------------------------------------------------------------------------------
void ShowbootProgress(char *str)
{
    u16 str_glyphs = DrawText12VisibleLength(str);
    Clear(0,160-15,240,15,gl_color_cheat_black,1);
	DrawHZText12(gl_loading_game,0,(240-DrawText12VisibleLength(gl_loading_game)*6)/2,72,0x7FFF,1);
    DrawHZText12(str,0,(240-str_glyphs*6)/2,160-15,0x7FFF,1);
}
