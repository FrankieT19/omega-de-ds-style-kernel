#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>

#include "ez_define.h"
#include "hzk12.h"
#include "ezkernel.h"
#include "draw.h"
#include "lang.h"

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
enum
{
	TEXT_ACCENT_NONE,
	TEXT_ACCENT_ACUTE,
	TEXT_ACCENT_GRAVE,
	TEXT_ACCENT_CIRC,
	TEXT_ACCENT_TILDE,
	TEXT_ACCENT_DIAERESIS,
	TEXT_ACCENT_RING,
	TEXT_ACCENT_BREVE,
	TEXT_ACCENT_CEDILLA,
	TEXT_ACCENT_DOT
};

#define TEXT_SPECIAL_NONE 0
#define TEXT_SPECIAL_DOTLESS_I 1
#define TEXT_SPECIAL_SHARP_S 2

static void DrawTextPixel12(u16 *v, u16 x, u16 y, u16 c, int px, int py)
{
	if((x + px) < 240 && (y + py) < 160)
		v[(y + py) * 240 + x + px] = c;
}

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

static void DrawLatinAccent12(u16 *v, u16 x, u16 y, u16 c, u8 accent, u8 base)
{
	u8 y_offset = ((accent == TEXT_ACCENT_DIAERESIS) &&
	               ((base == 'a') || (base == 'e') || (base == 'i') || (base == 'o') || (base == 'u') || (base == 'y'))) ? 2 : 0;
	if((accent == TEXT_ACCENT_BREVE) && (base == 'g'))
		y_offset = 1;

	switch(accent)
	{
		case TEXT_ACCENT_ACUTE:
			DrawTextPixel12(v, x, y, c, 4, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 3, 1 + y_offset);
			break;
		case TEXT_ACCENT_GRAVE:
			DrawTextPixel12(v, x, y, c, 2, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 3, 1 + y_offset);
			break;
		case TEXT_ACCENT_CIRC:
			DrawTextPixel12(v, x, y, c, 3, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 2, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 4, 1 + y_offset);
			break;
		case TEXT_ACCENT_TILDE:
			DrawTextPixel12(v, x, y, c, 2, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 4, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 1, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 3, 1 + y_offset);
			break;
		case TEXT_ACCENT_DIAERESIS:
			DrawTextPixel12(v, x, y, c, 2, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 4, 0 + y_offset);
			break;
		case TEXT_ACCENT_RING:
			DrawTextPixel12(v, x, y, c, 3, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 2, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 4, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 3, 2 + y_offset);
			break;
		case TEXT_ACCENT_BREVE:
			DrawTextPixel12(v, x, y, c, 1, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 5, 0 + y_offset);
			DrawTextPixel12(v, x, y, c, 2, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 3, 1 + y_offset);
			DrawTextPixel12(v, x, y, c, 4, 1 + y_offset);
			break;
		case TEXT_ACCENT_CEDILLA:
			DrawTextPixel12(v, x, y, c, 3, 10);
			DrawTextPixel12(v, x, y, c, 2, 11);
			DrawTextPixel12(v, x, y, c, 3, 11);
			break;
		case TEXT_ACCENT_DOT:
			DrawTextPixel12(v, x, y, c, 3, 0);
			break;
		default:
			break;
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

static u32 MapLatinGlyph12(u32 cp, u8 *base, u8 *accent, u8 *special)
{
	*accent = TEXT_ACCENT_NONE;
	*special = TEXT_SPECIAL_NONE;

	switch(cp)
	{
		case 0x00C0: *base = 'A'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00C1: *base = 'A'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00C2: *base = 'A'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00C3: *base = 'A'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00C4: *base = 'A'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00C5: *base = 'A'; *accent = TEXT_ACCENT_RING; return 1;
		case 0x00E0: *base = 'a'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00E1: *base = 'a'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00E2: *base = 'a'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00E3: *base = 'a'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00E4: *base = 'a'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00E5: *base = 'a'; *accent = TEXT_ACCENT_RING; return 1;
		case 0x00C7: *base = 'C'; *accent = TEXT_ACCENT_CEDILLA; return 1;
		case 0x00E7: *base = 'c'; *accent = TEXT_ACCENT_CEDILLA; return 1;
		case 0x00C8: *base = 'E'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00C9: *base = 'E'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00CA: *base = 'E'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00CB: *base = 'E'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00E8: *base = 'e'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00E9: *base = 'e'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00EA: *base = 'e'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00EB: *base = 'e'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00CC: *base = 'I'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00CD: *base = 'I'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00CE: *base = 'I'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00CF: *base = 'I'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00EC: *base = 'i'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00ED: *base = 'i'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00EE: *base = 'i'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00EF: *base = 'i'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00D1: *base = 'N'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00F1: *base = 'n'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00D2: *base = 'O'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00D3: *base = 'O'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00D4: *base = 'O'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00D5: *base = 'O'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00D6: *base = 'O'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00F2: *base = 'o'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00F3: *base = 'o'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00F4: *base = 'o'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00F5: *base = 'o'; *accent = TEXT_ACCENT_TILDE; return 1;
		case 0x00F6: *base = 'o'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00D9: *base = 'U'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00DA: *base = 'U'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00DB: *base = 'U'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00DC: *base = 'U'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00F9: *base = 'u'; *accent = TEXT_ACCENT_GRAVE; return 1;
		case 0x00FA: *base = 'u'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00FB: *base = 'u'; *accent = TEXT_ACCENT_CIRC; return 1;
		case 0x00FC: *base = 'u'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x00DD: *base = 'Y'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00FD: *base = 'y'; *accent = TEXT_ACCENT_ACUTE; return 1;
		case 0x00FF: *base = 'y'; *accent = TEXT_ACCENT_DIAERESIS; return 1;
		case 0x011E: *base = 'G'; *accent = TEXT_ACCENT_BREVE; return 1;
		case 0x011F: *base = 'g'; *accent = TEXT_ACCENT_BREVE; return 1;
		case 0x0130: *base = 'I'; *accent = TEXT_ACCENT_DOT; return 1;
		case 0x015E: *base = 'S'; *accent = TEXT_ACCENT_CEDILLA; return 1;
		case 0x015F: *base = 's'; *accent = TEXT_ACCENT_CEDILLA; return 1;
		case 0x00DF: *base = 0; *special = TEXT_SPECIAL_SHARP_S; return 1;
		case 0x0131: *base = 0; *special = TEXT_SPECIAL_DOTLESS_I; return 1;
		default: break;
	}

	return 0;
}

static void DrawMappedLatinGlyph12(u16 *v, u16 x, u16 y, u16 c, u8 base, u8 accent, u8 special)
{
	static const u8 dotless_i[12] = {0x00,0x00,0x00,0x00,0x10,0x10,0x10,0x10,0x10,0x10,0x00,0x00};
	static const u8 sharp_s[12] = {0x00,0x00,0x70,0x88,0x88,0x90,0xE0,0x90,0x88,0xF0,0x00,0x00};

	if(special == TEXT_SPECIAL_DOTLESS_I)
		DrawCustomGlyph12(v, x, y, c, dotless_i);
	else if(special == TEXT_SPECIAL_SHARP_S)
		DrawCustomGlyph12(v, x, y, c, sharp_s);
	else
	{
		DrawAsciiGlyph12(v, x, y, c, base);
		DrawLatinAccent12(v, x, y, c, accent, base);
	}
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
			if((gl_select_lang != 0xE2E2) && DecodeUtf8Text12(str, l, &hi, c1, &codepoint))
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
			if((gl_select_lang != 0xE2E2) && DecodeUtf8Text12(str, l, &hi, c1, &codepoint))
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

void DrawHZText12(char *str, u16 len, u16 x, u16 y, u16 c, u8 isDrawDirect)
{
  u32 i,l,hi=0,shown=0;
  u32 location;
	u8 cc,c1,c2;
	u16 *v;
	u16 *p1 = Vcache;
	u16 *p2 = VideoBuffer;
	u16 yy;
	

	if(isDrawDirect)
		v = p2;
	else
		v = p1;

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
		else	//Double-byte
		{
			u32 codepoint;
			u8 base;
			u8 accent;
			u8 special;
			if((gl_select_lang != 0xE2E2) && DecodeUtf8Text12(str, l, &hi, c1, &codepoint))
			{
				if(MapLatinGlyph12(codepoint, &base, &accent, &special))
					DrawMappedLatinGlyph12(v, x, y, c, base, accent, special);
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
    u8 str_len = strlen(str);
    Clear(0,160-15,240,15,gl_color_cheat_black,1);
	DrawHZText12(gl_loading_game,0,(240-strlen(gl_loading_game)*6)/2,72,0x7FFF,1);
    DrawHZText12(str,0,(240-str_len*6)/2,160-15,0x7FFF,1);
}
