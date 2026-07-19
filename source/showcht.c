#include <gba_systemcalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gba_base.h>
#include <gba_input.h>
#include <gba_dma.h>

#include "ezkernel.h"
#include "showcht.h"
#include "draw.h"


FM_CHT_LINE tmpCHTFS ;

#define CHT_BUFFER_OFFSET 0x2000
#define MAX_CHEAT_MENU_ENTRIES \
	((MAX_pReadCache_size - CHT_BUFFER_OFFSET) / sizeof(FM_CHT_LINE))

u8 *pCHTbuffer = (u8*)(pReadCache + CHT_BUFFER_OFFSET); //patchbuffer


/* Enhanced runtime table: exactly 128 address/value records. */
ST_entry pCHEAT[MAX_RUNTIME_CHEAT_RECORDS]EWRAM_BSS;
typedef char pCHEAT_capacity_must_match_runtime_limit[
    (sizeof(pCHEAT) / sizeof(pCHEAT[0]) == MAX_RUNTIME_CHEAT_RECORDS) ? 1 : -1];
u32 gl_cheat_count;
u32 gl_runtime_write_work;
u32 gl_cheat_selected_count;
static u32 gl_cheat_compile_errors;
static u32 gl_cheat_menu_truncated;
static u32 gl_cheat_value_truncated;

u32 pROMCONDITION_OFFSET[MAX_ROM_CONDITION_BYTES]EWRAM_BSS;
u8 pROMCONDITION_VALUE[MAX_ROM_CONDITION_BYTES]EWRAM_BSS;
u32 pROMPATCH_OFFSET[MAX_ROM_PATCH_BYTES]EWRAM_BSS;
u8 pROMPATCH_VALUE[MAX_ROM_PATCH_BYTES]EWRAM_BSS;
CHT_ROM_GROUP pROMGROUP[MAX_ROM_CHEAT_GROUPS]EWRAM_BSS;
u32 gl_rom_condition_count;
u32 gl_rom_patch_count;
u32 gl_rom_group_count;

#define CHEAT_SELECTION_CACHE_MAX 128
static char cheat_selection_key[128];
static char cheat_active_key[128];
static u16 cheat_selection_index[CHEAT_SELECTION_CACHE_MAX];
static u32 cheat_selection_cache_count;

extern u16 gl_select_lang;
unsigned long str2hex(char *str);

//u16 gl_color_chtBG    = RGB(4,8,0xC);
//------------------------------------------------------------------


extern FIL gfile;
char buf[MAX_BUF_LEN]EWRAM_BSS;
char _paramv[MAX_BUF_LEN] EWRAM_BSS;
extern void Draw_select_icon(u32 X,u32 Y,u32 mode);
extern void UIAudio_PlayMove(void);
extern void UIAudio_PlayAcceptExport(void);
extern void UIAudio_PlayBackExport(void);
extern void UIAudio_UpdateExport(void);
extern void UIAudio_WaitForCurrentClipExport(u32 max_frames);
extern void UIAudio_CutOffTrailingClipExport(void);
extern const char *Launcher_OnOffText(u16 value);
extern void Launcher_DrawCheatBackground(const char *title);
extern void Launcher_ClearCheatRegion(u16 x, u16 y, u16 w, u16 h);
extern void Launcher_DrawCheatCounter(u32 totalcount, u32 select);
extern void Launcher_MarkTopbarNameDirty(void);
extern void Launcher_UpdateCheatTitle(void);

static u32 cheat_use_chinese_folder = 0;

static void CheatSelectionMakeKey(char *dst, u32 dst_len, const TCHAR *gamefilename, u32 havecht)
{
	snprintf(dst, dst_len, "%08lx:%s", havecht, gamefilename ? gamefilename : "");
	dst[dst_len - 1] = '\0';
}

u32 CheatSelectionAppliesTo(TCHAR *gamefilename, u32 havecht)
{
	char key[128];
	CheatSelectionMakeKey(key, sizeof(key), gamefilename, havecht);
	return (cheat_selection_cache_count && !strcmp(cheat_selection_key, key)) ? 1 : 0;
}

void CheatSelectionForget(void)
{
	cheat_selection_key[0] = '\0';
	cheat_active_key[0] = '\0';
	cheat_selection_cache_count = 0;
	gl_cheat_selected_count = 0;
}

static void CheatSelectionRestore(u32 total)
{
	u32 i;
	if(strcmp(cheat_selection_key, cheat_active_key))
	{
		gl_cheat_selected_count = 0;
		return;
	}

	gl_cheat_selected_count = cheat_selection_cache_count;
	for(i=0;i<cheat_selection_cache_count;i++)
	{
		if(cheat_selection_index[i] < total &&
			((FM_CHT_LINE*)pCHTbuffer)[cheat_selection_index[i]].is_section == 0)
			((FM_CHT_LINE*)pCHTbuffer)[cheat_selection_index[i]].select = 1;
	}
}

static void CheatSelectionRemember(u32 total)
{
	u32 i;

	cheat_selection_cache_count = 0;
	strncpy(cheat_selection_key, cheat_active_key, sizeof(cheat_selection_key) - 1);
	cheat_selection_key[sizeof(cheat_selection_key) - 1] = '\0';

	for(i=0;i<total;i++)
	{
		if(((FM_CHT_LINE*)pCHTbuffer)[i].is_section == 0 &&
			((FM_CHT_LINE*)pCHTbuffer)[i].select == 1)
		{
			if(cheat_selection_cache_count < CHEAT_SELECTION_CACHE_MAX)
				cheat_selection_index[cheat_selection_cache_count++] = i;
		}
	}
}

static u32 CheatTextLooksGB2312(const char *str)
{
	u32 i;
	u32 l = strlen(str);

	for(i=0;i+1<l;i++)
	{
		u8 c1 = (u8)str[i];
		u8 c2 = (u8)str[i+1];

		if((c1 >= 0xC2) && (c1 <= 0xDF) && ((c2 & 0xC0) == 0x80))
		{
			i++;
			continue;
		}
		if((c1 >= 0xE0) && (c1 <= 0xEF) && (i+2<l) && ((c2 & 0xC0) == 0x80) && (((u8)str[i+2] & 0xC0) == 0x80))
		{
			i += 2;
			continue;
		}

		if((c1 >= 0xA1) && (c1 <= 0xF7) && (c2 >= 0xA1) && (c2 <= 0xFE))
			return 1;
	}
	return 0;
}

static u32 CheatTextVisibleColumns(const char *str)
{
	u32 i = 0;
	u32 shown = 0;
	u32 l = strlen(str);

	while(i<l)
	{
		u8 c1 = (u8)str[i++];
		if(c1 < 0x80)
			shown++;
		else if((i<l) && (c1 >= 0xA1) && (c1 <= 0xF7) && ((u8)str[i] >= 0xA1) && ((u8)str[i] <= 0xFE))
		{
			i++;
			shown += 2;
		}
		else
			shown++;
	}
	return shown;
}

static void DrawCheatText12(char *str, u16 len, u16 x, u16 y, u16 c, u8 isDrawDirect)
{
	u16 old_lang = gl_select_lang;
	if((old_lang != 0xE2E2) && CheatTextLooksGB2312(str))
		gl_select_lang = 0xE2E2;
	DrawHZText12(str, len, x, y, c, isDrawDirect);
	gl_select_lang = old_lang;
}
//------------------------------------------------------------------
//------------------------------------------------------------------
void Trim(char s[])
{
	int n;
	for(n = strlen(s) - 1; n >= 0; n--)
	{
		if(s[n]!=' ' && s[n]!='\t' && s[n]!='\n' && s[n]!='\r')
			break;
		s[n] = '\0';
	}
}
static u8 NormalizeSectionNameAndMode(char *section)
{
	int len;

	if (section == NULL)
		return CHT_GROUP_ONE;

	Trim(section);
	len = strlen(section);
	if (len >= 6 && strcasecmp(&section[len - 6], "|MULTI") == 0)
	{
		section[len - 6] = '\0';
		Trim(section);
		return CHT_GROUP_MULTI;
	}
	if (len >= 4 && strcasecmp(&section[len - 4], "|ONE") == 0)
	{
		section[len - 4] = '\0';
		Trim(section);
		return CHT_GROUP_ONE;
	}

	/* Plain sections keep the stock EZ-Flash zero-or-one behaviour. */
	return CHT_GROUP_ONE;
}

static void Clean_cheat_title(char s[])
{
	char *dash;
	char *p;
	char *dot;
	char temp[128];
	u32 r;
	u32 w = 0;
	u32 paren = 0;
	u32 bracket = 0;

	Trim(s);
	dot = strrchr(s, '.');
	if(dot)
		*dot = '\0';

	dash = strchr(s, '-');
	if(dash && (dash - s) <= 16)
	{
		u32 i;
		u32 prefix_ok = 0;
		for(i = 0; s[i] && (&s[i] < dash); i++)
		{
			if((s[i] >= '0') && (s[i] <= '9'))
				prefix_ok = 1;
			else if((s[i] != ' ') && (s[i] != '_') && (s[i] != '.'))
			{
				prefix_ok = 0;
				break;
			}
		}
		if(prefix_ok)
			memmove(s, dash + 1, strlen(dash + 1) + 1);
	}

	p = s;
	while((*p == ' ') || (*p == '-') || (*p == '_'))
		p++;
	if(p != s)
		memmove(s, p, strlen(p) + 1);

	memset(temp, 0, sizeof(temp));
	strncpy(temp, s, sizeof(temp) - 1);
	for(r = 0; temp[r] && w < sizeof(temp) - 1; r++)
	{
		if(temp[r] == '(')
		{
			paren = 1;
			continue;
		}
		if(temp[r] == ')')
		{
			paren = 0;
			continue;
		}
		if(temp[r] == '[')
		{
			bracket = 1;
			continue;
		}
		if(temp[r] == ']')
		{
			bracket = 0;
			continue;
		}
		if(!paren && !bracket)
			s[w++] = temp[r];
	}
	s[w] = '\0';
	Trim(s);
}
//------------------------------------------------------------------
void Get_KEY_val(FIL* file,char*KEY_section,char*KEY_secval,char getbuff[])
{
	int text_comment = 0;

	int in_section=0;
	//int keyval_count=0;

	char section[MAX_KEY_LEN] = {0};

	f_lseek(&gfile, 0x0);
	while(f_gets(buf, MAX_KEY_LEN, &gfile) != NULL)
	{
		Trim(buf);
    // to skip text comment with flags /* ...*/
    if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/'))
    {
			if (strstr(buf, "/*") != NULL)
			{
				text_comment = 1;
				continue;
			}
			else if (strstr(buf, "*/") != NULL)
			{
				text_comment = 0;
				continue;
			}
    }
    if (text_comment == 1)
	  {
			continue;
    }

		int buf_len = strlen(buf);
          
	    // ignore and skip the line with first chracter '#', '=' or '/'
    if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/')
    {
        continue;
    }

		char _paramk[MAX_KEY_LEN] = {0}; 
		char _paramv[/*MAX_VAL_LEN*/MAX_KEY_LEN] = {0};

    int _kv=0, _klen=0, _vlen=0;
    int i = 0;

    int is_section=0;
    int section_len=0;
    //int _val_len=0;

    for (i=0; i<buf_len; ++i)
    {

			if (buf[i] == '[')
			{
				is_section = 1;
				in_section = 0;
				
				memset(section,0,MAX_KEY_LEN);
				continue;
			}

			if(is_section == 1 && buf[i] != ']')
			{
				if (section_len < MAX_KEY_LEN - 1)
					section[section_len++] = buf[i];
				continue;
			}
      else if (buf[i] == ']')
      {
          is_section = 0;
          in_section = 1;
          NormalizeSectionNameAndMode(section);
          //_val_len = 0;
          break;
      }

			/*
			 * Parse both grouped rows and standalone root rows. A section remains
			 * active until the next [Section] header, matching normal INI rules.
			 */
			{
				// scan param key name
        if (_kv == 0 && buf[i] != '=')
        {
            if (_klen >= MAX_KEY_LEN - 1)
                break;
            _paramk[_klen++] = buf[i];
            continue;
        }
        else if (_kv == 0 && buf[i] == '=')
        {
            _kv = 1;
            continue;
        }
	      	
	      // scan param key value
	      if (_vlen >= MAX_KEY_LEN || buf[i] == '#')
					break;
	                
	      _paramv[_vlen++] = buf[i];
	    }
	          
	  }
	     //DEBUG_printf("KEY_section %s, section %s",KEY_section,section);	     
		if (((KEY_section[0] == '\0') && !in_section) ||
			((KEY_section[0] != '\0') && in_section &&
			 strcmp(KEY_section,section) == 0))
		{
			if( strcmp(KEY_secval,_paramk) == 0)
			{
				strcpy(getbuff,_paramv); 

				return ;	
			}				
		}
		if (strcmp(_paramk, "")==0 || strcmp(_paramv, "")==0)
			continue;

		memset(buf,0,MAX_KEY_LEN) ;
  }
}
//------------------------------------------------------------------
u32 Get_CHT_val(FIL* file,char*KEY_section,char*KEY_secval/*,char getbuff[]*/)
{
	int text_comment = 0;

	/* Avoid stale bytes from a previously selected cheat line. */
	memset(_paramv, 0x00, sizeof(_paramv));
	gl_cheat_value_truncated = 0;

	int in_section=0;
	//int keyval_count=0;

	char section[MAX_KEY_LEN] = {0};

	f_lseek(&gfile, 0x0);
	while(f_gets(buf, MAX_BUF_LEN, &gfile) != NULL)
	{
		/* Every key line gets a clean value buffer. */
		memset(_paramv, 0x00, sizeof(_paramv));
		Trim(buf);
    // to skip text comment with flags /* ...*/
    if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/'))
    {
			if (strstr(buf, "/*") != NULL)
			{
				text_comment = 1;
				continue;
			}
			else if (strstr(buf, "*/") != NULL)
			{
				text_comment = 0;
				continue;
			}
    }
    if (text_comment == 1)
	  {
			continue;
    }

		int buf_len = strlen(buf);
		//DEBUG_printf("cht buf_len %x",buf_len);	    
          
    // ignore and skip the line with first chracter '#', '=' or '/'
    if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/')
    {
        continue;
    }

		char _paramk[MAX_KEY_LEN] = {0}; 
		//char _paramv[/*MAX_VAL_LEN*/MAX_KEY_LEN] = {0};

    int _kv=0, _klen=0, _vlen=0;
    int i = 0;

    int is_section=0;
    int section_len=0;
    //int _val_len=0;

    for (i=0; i<buf_len; ++i)
    {

			if (buf[i] == '[')
			{
				is_section = 1;
				in_section = 0;
				
				memset(section,0,MAX_KEY_LEN);
				continue;
			}

			if(is_section == 1 && buf[i] != ']')
			{
				if (section_len < MAX_KEY_LEN - 1)
					section[section_len++] = buf[i];
				continue;
			}
      else if (buf[i] == ']')
      {
          is_section = 0;
          in_section = 1;
          NormalizeSectionNameAndMode(section);
          //_val_len = 0;
          break;
      }

			/* Parse both grouped rows and standalone root rows. */
			{
				// scan param key name
        if (_kv == 0 && buf[i] != '=')
        {
            if (_klen >= MAX_KEY_LEN - 1)
                break;
            _paramk[_klen++] = buf[i];
            continue;
        }
        else if (_kv == 0 && buf[i] == '=')
        {
            _kv = 1;
            continue;
        }	      	
	      // scan param key value
	      if (_vlen >= MAX_BUF_LEN || buf[i] == '#')
					break;
	                
	      _paramv[_vlen++] = buf[i];
	    }
	          
	  }
	    
     //DEBUG_printf("KEY_section %s, section %s",KEY_section,section);	     
		if (((KEY_section[0] == '\0') && !in_section) ||
			((KEY_section[0] != '\0') && in_section &&
			 strcmp(KEY_section,section) == 0))
		{
			if( strcmp(KEY_secval,_paramk) == 0)
			{
			  //0111 Multi-line
			  while(f_gets(buf, MAX_BUF_LEN, &gfile) != NULL)
				{
					Trim(buf);
			    // to skip text comment with flags /* ...*/
			    if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/'))
			    {
						if (strstr(buf, "/*") != NULL)
						{
							text_comment = 1;
							break;
						}
						else if (strstr(buf, "*/") != NULL)
						{
							text_comment = 0;
							break;
						}
			    }
			    if (text_comment == 1)
				  {
						break;
			    }

					int buf_len = strlen(buf);
					//DEBUG_printf("cht buf_len %x",buf_len);	    
			          
			    // stop at a blank/comment line or the next section header
			    if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' ||
			        buf[0] == '/' || buf[0] == '[')
			    {	    	
			        break;
			    }
			    for (i=0; i<buf_len; ++i)
		    	{
		     	 	if (buf[i] == '=')
		          goto endcht;
		      }
			    for (i=0; i<buf_len; ++i)
		    	{
						if (_vlen >= MAX_VAL_LEN - 1)
						{
							gl_cheat_value_truncated = 1;
							goto endcht;
						}
						_paramv[_vlen++] = buf[i];
		      }
		      //DEBUG_printf("%x %x %x %x %x %x %x %x", buf[0],buf[1], buf[2],buf[3], buf[4],buf[5], buf[6],buf[7]);	
		      //wait_btn();
			    
			  }			  			  
			  endcht:
				return _vlen;	
			}				
		}
		if (strcmp(_paramk, "")==0 || strcmp(_paramv, "")==0)
			continue;

		memset(buf,0,MAX_KEY_LEN) ;
  }
  return 0;
}
//------------------------------------------------------------------
u32 Get_all_Section_val(FIL* file)
{
	char buf[MAX_sectionVAL_LEN];
	int text_comment = 0;
	u32 Line = 0;
	u16 current_section = CHT_NO_SECTION;

	gl_cheat_menu_truncated = 0;
	f_lseek(&gfile, 0x0);
	while(f_gets(buf, MAX_sectionVAL_LEN, &gfile) != NULL)
	{
		char _paramk[MAX_KEY_LEN] = {0};
		int _klen = 0;
		int i;
		int buf_len;

		memset(&tmpCHTFS,0x00,sizeof(tmpCHTFS));
		tmpCHTFS.parent_section = CHT_NO_SECTION;
		Trim(buf);

		if(buf[0] == '-' && buf[1] == '-')
			break;
		if (buf[0] != '#' && (buf[0] != '/' || buf[1] != '/'))
		{
			if (strstr(buf, "/*") != NULL)
			{
				text_comment = 1;
				continue;
			}
			else if (strstr(buf, "*/") != NULL)
			{
				text_comment = 0;
				continue;
			}
		}
		if (text_comment == 1)
			continue;

		buf_len = strlen(buf);
		if (buf_len <= 1 || buf[0] == '#' || buf[0] == '=' || buf[0] == '/')
			continue;

		/* A [Section] row is a visual heading only. */
		if (buf[0] == '[')
		{
			int section_len = 0;

			/* A malformed/empty heading must not inherit the previous group. */
			current_section = CHT_NO_SECTION;
			for (i = 1; i < buf_len && buf[i] != ']'; i++)
			{
				if (section_len >= MAX_KEY_LEN - 1)
					break;
				tmpCHTFS.LINEname[section_len++] = buf[i];
			}
			if (i >= buf_len || buf[i] != ']' || section_len == 0)
				continue;

			tmpCHTFS.is_section = 1;
			tmpCHTFS.section_val_count =
				NormalizeSectionNameAndMode(tmpCHTFS.LINEname);
			tmpCHTFS.len = strlen(tmpCHTFS.LINEname);
			if (tmpCHTFS.len == 0)
				continue;
			if (Line >= MAX_CHEAT_MENU_ENTRIES)
			{
				gl_cheat_menu_truncated = 1;
				break;
			}
			/* Groups start expanded. The header select byte stores UI state only. */
			tmpCHTFS.select = CHT_GROUP_EXPANDED;
			tmpCHTFS.parent_section = CHT_NO_SECTION;
			current_section = (u16)Line;
			dmaCopy(&tmpCHTFS,
				&((FM_CHT_LINE*)pCHTbuffer)[Line], sizeof(FM_CHT_LINE));
			Line++;
			continue;
		}

		/* Any name=commands row is selectable, with or without a section. */
		for (i = 0; i < buf_len && buf[i] != '='; i++)
		{
			if (_klen >= MAX_KEY_LEN - 1)
				break;
			_paramk[_klen++] = buf[i];
		}
		if (i >= buf_len || buf[i] != '=' || _klen == 0)
			continue;

		memcpy(tmpCHTFS.LINEname, _paramk, _klen);
		tmpCHTFS.section_val_count = CHT_MENU_NORMAL;
		tmpCHTFS.is_section = 0;
		tmpCHTFS.select = 0;
		tmpCHTFS.parent_section = current_section;
		if (Line >= MAX_CHEAT_MENU_ENTRIES)
		{
			gl_cheat_menu_truncated = 1;
			break;
		}
		dmaCopy(&tmpCHTFS,
			&((FM_CHT_LINE*)pCHTbuffer)[Line], sizeof(FM_CHT_LINE));
		Line++;
	}
	return Line;
}
static u32 IsCheatMenuEntryVisible(u32 entry_index, u32 total_entries)
{
	FM_CHT_LINE *entries = (FM_CHT_LINE*)pCHTbuffer;
	u16 parent_section;

	if (entry_index >= total_entries)
		return 0;
	if (entries[entry_index].is_section == 1)
		return 1;

	parent_section = entries[entry_index].parent_section;
	if (parent_section == CHT_NO_SECTION)
		return 1;
	if (parent_section >= total_entries ||
		entries[parent_section].is_section != 1)
		return 1;

	return entries[parent_section].select == CHT_GROUP_EXPANDED;
}

u32 GetVisibleCheatMenuCount(u32 total_entries)
{
	u32 entry_index;
	u32 visible_count = 0;

	for (entry_index = 0; entry_index < total_entries; entry_index++)
	{
		if (IsCheatMenuEntryVisible(entry_index, total_entries))
			visible_count++;
	}
	return visible_count;
}

u32 GetVisibleCheatMenuEntryIndex(u32 total_entries, u32 visible_index)
{
	u32 entry_index;
	u32 current_visible = 0;

	for (entry_index = 0; entry_index < total_entries; entry_index++)
	{
		if (!IsCheatMenuEntryVisible(entry_index, total_entries))
			continue;
		if (current_visible == visible_index)
			return entry_index;
		current_visible++;
	}
	return total_entries;
}

u32 SetCheatGroupExpanded(u32 entry_index, u32 total_entries, u32 expanded)
{
	FM_CHT_LINE *entries = (FM_CHT_LINE*)pCHTbuffer;
	u8 new_state;

	if (entry_index >= total_entries || entries[entry_index].is_section != 1)
		return 0;
	new_state = expanded ? CHT_GROUP_EXPANDED : CHT_GROUP_COLLAPSED;
	if (entries[entry_index].select == new_state)
		return 0;
	entries[entry_index].select = new_state;
	return 1;
}

static u32 CountSelectedCheatMenuEntries(u32 total_entries)
{
	u32 index;
	u32 count = 0;
	FM_CHT_LINE *entries = (FM_CHT_LINE*)pCHTbuffer;

	for (index = 0; index < total_entries; index++)
	{
		if (entries[index].is_section == 0 && entries[index].select == 1)
			count++;
	}
	return count;
}

void ToggleCheatMenuEntry(u32 entry_index, u32 total_entries)
{
	FM_CHT_LINE *entries = (FM_CHT_LINE*)pCHTbuffer;
	u16 parent_section;

	if (entry_index >= total_entries || entries[entry_index].is_section == 1)
		return;

	parent_section = entries[entry_index].parent_section;
	if (parent_section != CHT_NO_SECTION &&
		parent_section < total_entries &&
		entries[parent_section].is_section == 1 &&
		entries[parent_section].section_val_count == CHT_GROUP_ONE)
	{
		u32 index;
		u32 sibling_selected = 0;

		/* A selected ONE entry may be pressed again to leave zero selected. */
		if (entries[entry_index].select == 1)
		{
			entries[entry_index].select = 0;
			return;
		}

		for (index = 0; index < total_entries; index++)
		{
			if (entries[index].is_section == 0 &&
				entries[index].parent_section == parent_section &&
				entries[index].select == 1)
			{
				sibling_selected = 1;
				break;
			}
		}
		if (!sibling_selected &&
			CountSelectedCheatMenuEntries(total_entries) >=
			MAX_RUNTIME_CHEAT_RECORDS)
			return;

		/* Selecting a new entry clears only siblings in the same ONE group. */
		for (index = 0; index < total_entries; index++)
		{
			if (entries[index].is_section == 0 &&
				entries[index].parent_section == parent_section)
				entries[index].select = 0;
		}
		entries[entry_index].select = 1;
		return;
	}

	/* Standalone rows and explicit [Group|MULTI] rows are independent. */
	if (entries[entry_index].select == 1)
		entries[entry_index].select = 0;
	else if (CountSelectedCheatMenuEntries(total_entries) <
		MAX_RUNTIME_CHEAT_RECORDS)
		entries[entry_index].select = 1;
}
//------------------------------------------------------------------
static void Show_KEY_line(u32 line, u32 Select, u32 showoffset, u32 total_entries)
{
	char msg[256];
	u32 X_offset=15;
	u32 Y_offset=20;
	u32 line_x = 14;
	u16 name_color;
	u32 row_y = Y_offset+line*line_x;
	u32 msg_len;
	u32 highlight_w;
	u32 visible_index = showoffset + line;
	u32 entry_index = GetVisibleCheatMenuEntryIndex(total_entries, visible_index);
	u8 select;
	FM_CHT_LINE *entry;

	Launcher_ClearCheatRegion(X_offset, row_y, 220, 13);
	if (entry_index >= total_entries)
		return;
	entry = &((FM_CHT_LINE*)pCHTbuffer)[entry_index];
	name_color = (line == Select) ? gl_color_selected : gl_color_text;
	select = entry->select;

	if(entry->is_section == 1)
	{
		sprintf(msg,"[%c] %s", select == CHT_GROUP_EXPANDED ? '-' : '+',
			entry->LINEname);
		if(line == Select)
		{
			msg_len = CheatTextVisibleColumns(msg);
			if(msg_len > 30) msg_len = 30;
			highlight_w = msg_len * 6 + 8;
			if(highlight_w < 30) highlight_w = 30;
			Clear(X_offset+1, row_y, highlight_w, 13, gl_color_selectBG_sd, 1);
		}
		DrawCheatText12(msg,30,X_offset+4,row_y,name_color,1);
	}
	else
	{
		Draw_select_icon(X_offset+13,row_y,select);
		sprintf(msg,"%s",entry->LINEname);
		if(!strcasecmp(msg, "ON"))
			sprintf(msg, "%s", Launcher_OnOffText(select));
		if(line == Select)
		{
			msg_len = CheatTextVisibleColumns(msg);
			if(msg_len > 30) msg_len = 30;
			highlight_w = 18 + msg_len * 6 + 8;
			if(highlight_w < 36) highlight_w = 36;
			if(highlight_w > 205) highlight_w = 205;
			Clear(X_offset+10, row_y, highlight_w, 13, gl_color_selectBG_sd, 1);
			Draw_select_icon(X_offset+13,row_y,select);
		}
		DrawCheatText12(msg,30,X_offset+28,row_y,name_color,1);
	}
}

void Show_KEY_val(u32 total_entries, u32 visible_total, u32 Select, u32 showoffset)
{
	u32 need_show = visible_total - showoffset;
	u32 line;

	if(need_show > 10)
		need_show = 10;
	for(line=0; line<need_show; line++)
		Show_KEY_line(line, Select, showoffset, total_entries);
}
static u32 IsHexDigitCHT(char c)
{
	return ((c >= '0' && c <= '9') ||
			(c >= 'a' && c <= 'f') ||
			(c >= 'A' && c <= 'F'));
}
//------------------------------------------------------------------
//------------------------------------------------------------------
static u32 IsRuntimeOpcode(u32 address)
{
	return (address & CHT_RUNTIME_OPCODE_MASK) >= CHT_OP_IF_EQ;
}
//------------------------------------------------------------------
/*
 * Convert one compiled pCHEAT address into the address copied to the
 * injected runtime table. Enhanced W8 and Original byte-list writes are
 * already expanded by the parser, while legacy records may still use the
 * compact 00000-47FFF form. Runtime opcodes/data slots are copied verbatim.
 */
u32 ResolveRuntimeCheatRecordAddress(u32 record_address, u32 *full_address)
{
	u32 compact_address;
	u32 is_condition;

	if (full_address == 0)
		return 0;

	if (IsRuntimeOpcode(record_address))
	{
		*full_address = record_address;
		return 1;
	}

	/*
	 * W8 and Original byte-list records are expanded before this stage.
	 * Preserve valid full EWRAM/IWRAM write addresses instead of treating
	 * them as compact values and discarding them. Direct I/O writes remain
	 * rejected.
	 */
	if ((record_address >= 0x02000000 && record_address <= 0x0203FFFF) ||
		(record_address >= 0x03000000 && record_address <= 0x03007FFF))
	{
		*full_address = record_address;
		return 1;
	}

	is_condition = record_address & CHT_RECORD_CONDITION_FLAG;
	compact_address = record_address & ~CHT_RECORD_CONDITION_FLAG;

	if (compact_address <= 0x3FFFF)
	{
		*full_address = 0x02000000 + compact_address;
		return 1;
	}
	if (compact_address >= 0x40000 && compact_address <= 0x47FFF)
	{
		*full_address = 0x03000000 + (compact_address - 0x40000);
		return 1;
	}
	if (is_condition && compact_address >= 0x80000 &&
		compact_address <= 0x803FF)
	{
		/* I/O is read-only from legacy IF-family conditions. */
		*full_address = 0x04000000 + (compact_address - 0x80000);
		return 1;
	}

	return 0;
}
//------------------------------------------------------------------
static u32 ReserveRuntimeWriteWork(u32 count)
{
	if (count == 0 || count > MAX_RUNTIME_WRITE_WORK - gl_runtime_write_work)
		return 0;
	gl_runtime_write_work += count;
	return 1;
}
//------------------------------------------------------------------
static u32 AddCheatRecord(u32 address, u32 value)
{
	if (gl_cheat_count >= MAX_RUNTIME_CHEAT_RECORDS)
		return 0;

	pCHEAT[gl_cheat_count].address = address;
	pCHEAT[gl_cheat_count].VAL = IsRuntimeOpcode(address) ? value : (value & 0xFF);
	gl_cheat_count++;
	return 1;
}
//------------------------------------------------------------------
//------------------------------------------------------------------
void ResetCompiledCheats(void)
{
	gl_cheat_count = 0;
	gl_runtime_write_work = 0;
	gl_rom_condition_count = 0;
	gl_rom_patch_count = 0;
	gl_rom_group_count = 0;
}
//------------------------------------------------------------------
/*
 * Accept either a ROM-relative byte offset (00000000-03FFFFFF) or a
 * Game Pak ROM address in any of the three 32 MiB wait-state mirrors.
 * Relative offsets above 01FFFFFF are useful for 64 MiB NOR images.
 */
static u32 NormalizeRomOffset(u32 address, u32 *offset)
{
	if (address <= 0x03FFFFFF)
	{
		*offset = address;
		return 1;
	}

	if (address >= 0x08000000 && address <= 0x0DFFFFFF)
	{
		*offset = (address - 0x08000000) & 0x01FFFFFF;
		return 1;
	}

	return 0;
}
//------------------------------------------------------------------
static u32 AddRomByteRecord(u32 *offsets, u8 *values, u32 *record_count,
	u32 max_records, u32 offset, u32 value)
{
	if (*record_count >= max_records)
		return 0;

	offsets[*record_count] = offset;
	values[*record_count] = (u8)(value & 0xFF);
	(*record_count)++;
	return 1;
}
//------------------------------------------------------------------
/*
 * Parse one arbitrary-length ROM: or ROMIF: byte run. The number of comma-
 * separated bytes is the payload length; it is not limited to 8/16/32 bits.
 */
static u32 ParseRomByteRun(const char *text, u32 text_len,
	u32 *offsets, u8 *values, u32 *record_count, u32 max_records)
{
	char address_buf[9] = {0};
	u32 address_len = 0;
	u32 address;
	u32 offset;
	u32 byte_offset = 0;
	u32 pos = 0;
	u32 have_value = 0;

	while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t'))
		pos++;

	while (pos < text_len && text[pos] != ',')
	{
		if (text[pos] != ' ' && text[pos] != '\t')
		{
			if (!IsHexDigitCHT(text[pos]) || address_len >= 8)
				return 0;
			address_buf[address_len++] = text[pos];
		}
		pos++;
	}

	if (pos >= text_len || text[pos] != ',' || address_len == 0)
		return 0;

	address = str2hex(address_buf);
	if (!NormalizeRomOffset(address, &offset))
		return 0;
	pos++;

	while (pos <= text_len)
	{
		char value_buf[3] = {0};
		u32 value_len = 0;

		while (pos < text_len && text[pos] != ',')
		{
			if (text[pos] != ' ' && text[pos] != '\t')
			{
				if (!IsHexDigitCHT(text[pos]) || value_len >= 2)
					return 0;
				value_buf[value_len++] = text[pos];
			}
			pos++;
		}

		if (value_len == 0 || offset + byte_offset > 0x03FFFFFF)
			return 0;

		if (!AddRomByteRecord(offsets, values, record_count, max_records,
			offset + byte_offset, str2hex(value_buf)))
			return 0;

		byte_offset++;
		have_value = 1;

		if (pos >= text_len)
			break;
		pos++;
	}

	return have_value;
}
//------------------------------------------------------------------
static u32 RomGroupHasInternalConflict(const CHT_ROM_GROUP *group)
{
	u32 left;

	for (left = 0; left < group->patch_count; left++)
	{
		u32 right;
		u32 a_index = group->patch_start + left;

		for (right = left + 1; right < group->patch_count; right++)
		{
			u32 b_index = group->patch_start + right;
			if (pROMPATCH_OFFSET[a_index] == pROMPATCH_OFFSET[b_index] &&
				pROMPATCH_VALUE[a_index] != pROMPATCH_VALUE[b_index])
				return 1;
		}
	}

	return 0;
}
//------------------------------------------------------------------
static u32 RomGroupsConflict(const CHT_ROM_GROUP *a_group,
	const CHT_ROM_GROUP *b_group)
{
	u32 a_index;

	for (a_index = 0; a_index < a_group->patch_count; a_index++)
	{
		u32 b_index;
		u32 a_record = a_group->patch_start + a_index;

		for (b_index = 0; b_index < b_group->patch_count; b_index++)
		{
			u32 b_record = b_group->patch_start + b_index;
			if (pROMPATCH_OFFSET[a_record] == pROMPATCH_OFFSET[b_record] &&
				pROMPATCH_VALUE[a_record] != pROMPATCH_VALUE[b_record])
				return 1;
		}
	}

	return 0;
}
//------------------------------------------------------------------
void EvaluateEnhancedCheatGroups(CHT_ROM_READ_BYTE reader, void *context,
	u32 rom_size)
{
	u32 group_index;

	for (group_index = 0; group_index < gl_rom_group_count; group_index++)
	{
		CHT_ROM_GROUP *group = &pROMGROUP[group_index];
		u32 condition_index;
		u32 patch_index;

		group->active = 1;

		for (patch_index = 0; patch_index < group->patch_count; patch_index++)
		{
			u32 record = group->patch_start + patch_index;
			if (pROMPATCH_OFFSET[record] >= rom_size)
			{
				group->active = 0;
				break;
			}
		}

		if (!group->active)
			continue;

		if (RomGroupHasInternalConflict(group))
		{
			group->active = 0;
			continue;
		}

		for (condition_index = 0;
			condition_index < group->condition_count; condition_index++)
		{
			u32 record = group->condition_start + condition_index;
			u8 actual = 0;

			if (pROMCONDITION_OFFSET[record] >= rom_size || reader == NULL ||
				!reader(pROMCONDITION_OFFSET[record], &actual, context) ||
				actual != pROMCONDITION_VALUE[record])
			{
				group->active = 0;
				break;
			}
		}
	}

	for (group_index = 0; group_index < gl_rom_group_count; group_index++)
	{
		u32 other_index;
		CHT_ROM_GROUP *group = &pROMGROUP[group_index];

		if (!group->active)
			continue;

		for (other_index = group_index + 1;
			other_index < gl_rom_group_count; other_index++)
		{
			CHT_ROM_GROUP *other = &pROMGROUP[other_index];
			if (other->active && RomGroupsConflict(group, other))
			{
				group->active = 0;
				other->active = 0;
			}
		}
	}
}
//------------------------------------------------------------------
void ApplyActiveRomPatches(CHT_ROM_WRITE_BYTE writer, void *context,
	u32 rom_size)
{
	u32 group_index;

	if (writer == NULL)
		return;

	for (group_index = 0; group_index < gl_rom_group_count; group_index++)
	{
		CHT_ROM_GROUP *group = &pROMGROUP[group_index];
		u32 patch_index;

		if (!group->active)
			continue;

		for (patch_index = 0; patch_index < group->patch_count; patch_index++)
		{
			u32 record = group->patch_start + patch_index;
			if (pROMPATCH_OFFSET[record] < rom_size)
				writer(pROMPATCH_OFFSET[record], pROMPATCH_VALUE[record], context);
		}
	}
}
//------------------------------------------------------------------
u32 IsRuntimeCheatRecordActive(u32 record_index)
{
	u32 group_index;

	for (group_index = 0; group_index < gl_rom_group_count; group_index++)
	{
		CHT_ROM_GROUP *group = &pROMGROUP[group_index];

		if (record_index >= group->runtime_start &&
			record_index < group->runtime_start + group->runtime_count)
			return group->active;
	}

	return 1;
}
//------------------------------------------------------------------
/*
 * Parse one or more arbitrary-length compact-address byte lists:
 *   40E48,4D,BE;40E50,01;
 *   10A78,01,02,03,04,05,06,07,08;
 *
 * Each payload byte becomes one stock-compatible 8-byte address/value record.
 * Consequently, Original and Enhanced writes may exceed 32 bits without a
 * width-specific command.
 * record_flags is zero for writes or CHT_RECORD_CONDITION_FLAG for reads.
 */
//------------------------------------------------------------------
/*
 * Enhanced revision 6 legacy-group/MULTI/standalone command grammar
 * -------------------------------------------
 * The database key is the visible menu option name.  Every value is a
 * semicolon-delimited command stream and every command uses ':':
 *
 *   moonjump=IFM:W16,80130,0001,0000;W8:1505C,80;ENDIF;
 *
 *   [Movement Codes|MULTI]
 *   Walk Through Walls=W8:25BC4,01;
 *   Fast Movement=ADD:W16,25BC6,0001;
 *
 *   [Conditional Choice]
 *   Hard=IFNE:W16,202,0001;W32:10A78,00000001;ELSE;
 *        W32:10A78,00000000;ENDIF;
 *
 * Compact W8/W16/W32 writes consume one runtime record each. Conditions,
 * ADD/SUB and pointer payloads are width-aware. FILL consumes two records and
 * SLIDE consumes four records regardless of repetition count.
 */
#define MAX_TEXT_CONDITION_DEPTH 16
#define MAX_COMPACT_REPEAT_COUNT 0x00010000UL

typedef struct CHT_TEXT_CONDITION_
{
	u32 true_action;
	u32 has_else;
	u32 false_action;
} CHT_TEXT_CONDITION;

static u32 KeyEquals(const char *key, const char *expected)
{
	return strcmp(key, expected) == 0;
}
//------------------------------------------------------------------
static u32 IsConditionKeyName(const char *key)
{
	return KeyEquals(key, "IF") || KeyEquals(key, "IFM") ||
		KeyEquals(key, "IFNE") || KeyEquals(key, "IFNEM") ||
		KeyEquals(key, "IFLT") || KeyEquals(key, "IFLTM") ||
		KeyEquals(key, "IFGT") || KeyEquals(key, "IFGTM") ||
		KeyEquals(key, "IFLE") || KeyEquals(key, "IFLEM") ||
		KeyEquals(key, "IFGE") || KeyEquals(key, "IFGEM");
}
//------------------------------------------------------------------
static u32 IsMaskedConditionKey(const char *key)
{
	return KeyEquals(key, "IFM") || KeyEquals(key, "IFNEM") ||
		KeyEquals(key, "IFLTM") || KeyEquals(key, "IFGTM") ||
		KeyEquals(key, "IFLEM") || KeyEquals(key, "IFGEM");
}
//------------------------------------------------------------------
static u32 ConditionOpcodeForKey(const char *key)
{
	if (KeyEquals(key, "IF") || KeyEquals(key, "IFM"))
		return CHT_OP_IF_EQ;
	if (KeyEquals(key, "IFNE") || KeyEquals(key, "IFNEM"))
		return CHT_OP_IF_NE;
	if (KeyEquals(key, "IFLT") || KeyEquals(key, "IFLTM"))
		return CHT_OP_IF_LT;
	if (KeyEquals(key, "IFGT") || KeyEquals(key, "IFGTM"))
		return CHT_OP_IF_GT;
	if (KeyEquals(key, "IFLE") || KeyEquals(key, "IFLEM"))
		return CHT_OP_IF_LE;
	if (KeyEquals(key, "IFGE") || KeyEquals(key, "IFGEM"))
		return CHT_OP_IF_GE;
	return 0;
}
//------------------------------------------------------------------
static u32 CompactToFullRuntimeAddress(u32 compact_address, u32 allow_io,
	u32 *full_address)
{
	if (compact_address <= 0x3FFFF)
	{
		*full_address = 0x02000000 + compact_address;
		return 1;
	}
	if (compact_address >= 0x40000 && compact_address <= 0x47FFF)
	{
		*full_address = 0x03000000 + (compact_address - 0x40000);
		return 1;
	}
	if (allow_io && compact_address >= 0x80000 && compact_address <= 0x803FF)
	{
		*full_address = 0x04000000 + (compact_address - 0x80000);
		return 1;
	}
	return 0;
}
//------------------------------------------------------------------
static u32 CompactRegion(u32 compact_address, u32 allow_io)
{
	if (compact_address <= 0x3FFFF)
		return 1;
	if (compact_address >= 0x40000 && compact_address <= 0x47FFF)
		return 2;
	if (allow_io && compact_address >= 0x80000 && compact_address <= 0x803FF)
		return 3;
	return 0;
}
//------------------------------------------------------------------
static u32 CompactRangeAllowed(u32 compact_address, u32 width, u32 allow_io)
{
	u32 end;
	u32 region;

	if (width != 1 && width != 2 && width != 4)
		return 0;
	if ((width == 2 && (compact_address & 1U) != 0) ||
		(width == 4 && (compact_address & 3U) != 0))
		return 0;
	if (compact_address > 0xFFFFFFFFUL - (width - 1))
		return 0;
	end = compact_address + width - 1;
	region = CompactRegion(compact_address, allow_io);
	return region != 0 && region == CompactRegion(end, allow_io);
}
//------------------------------------------------------------------
static u32 NextTextField(const char *text, u32 text_len, u32 *position,
	char *field, u32 field_capacity)
{
	u32 pos = *position;
	u32 length = 0;

	while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t'))
		pos++;
	while (pos < text_len && text[pos] != ',')
	{
		if (text[pos] != ' ' && text[pos] != '\t')
		{
			if (length + 1 >= field_capacity)
				return 0;
			field[length++] = text[pos];
		}
		pos++;
	}
	if (length == 0)
		return 0;
	field[length] = 0;
	if (pos < text_len && text[pos] == ',')
		pos++;
	*position = pos;
	return 1;
}
//------------------------------------------------------------------
static u32 NextHexField(const char *text, u32 text_len, u32 *position,
	u32 max_digits, u32 *value)
{
	char field[9] = {0};
	u32 field_len = 0;
	u32 pos = *position;

	while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t'))
		pos++;
	while (pos < text_len && text[pos] != ',')
	{
		if (text[pos] != ' ' && text[pos] != '\t')
		{
			if (!IsHexDigitCHT(text[pos]) || field_len >= max_digits ||
				field_len >= sizeof(field) - 1)
				return 0;
			field[field_len++] = text[pos];
		}
		pos++;
	}
	if (field_len == 0)
		return 0;
	*value = str2hex(field);
	if (pos < text_len && text[pos] == ',')
		pos++;
	*position = pos;
	return 1;
}
//------------------------------------------------------------------
/* Stock databases use both address,value and address:value separators. Keep
 * that compatibility local to the byte-list parser so Enhanced command
 * payloads remain strict. */
static u32 NextOriginalHexField(const char *text, u32 text_len, u32 *position,
	u32 max_digits, u32 *value)
{
	char field[9] = {0};
	u32 field_len = 0;
	u32 pos = *position;

	while (pos < text_len && (text[pos] == ' ' || text[pos] == '\t'))
		pos++;
	while (pos < text_len && text[pos] != ',' && text[pos] != ':')
	{
		if (text[pos] != ' ' && text[pos] != '\t')
		{
			if (!IsHexDigitCHT(text[pos]) || field_len >= max_digits ||
				field_len >= sizeof(field) - 1)
				return 0;
			field[field_len++] = text[pos];
		}
		pos++;
	}
	if (field_len == 0)
		return 0;
	*value = str2hex(field);
	if (pos < text_len && (text[pos] == ',' || text[pos] == ':'))
		pos++;
	*position = pos;
	return 1;
}
//------------------------------------------------------------------
static u32 ParseWidthField(const char *text, u32 text_len, u32 *position,
	u32 *width, u32 *width_bits)
{
	char field[4] = {0};

	if (!NextTextField(text, text_len, position, field, sizeof(field)))
		return 0;
	if (KeyEquals(field, "W8"))
	{
		*width = 1;
		*width_bits = CHT_WIDTH_8;
		return 1;
	}
	if (KeyEquals(field, "W16"))
	{
		*width = 2;
		*width_bits = CHT_WIDTH_16;
		return 1;
	}
	if (KeyEquals(field, "W32"))
	{
		*width = 4;
		*width_bits = CHT_WIDTH_32;
		return 1;
	}
	return 0;
}
//------------------------------------------------------------------
static u32 ValueFitsWidth(u32 value, u32 width)
{
	if (width == 1)
		return value <= 0xFF;
	if (width == 2)
		return value <= 0xFFFF;
	return width == 4;
}
//------------------------------------------------------------------
static u32 ParseWidthAddressValue(const char *text, u32 text_len,
	u32 allow_io, u32 *width, u32 *width_bits, u32 *compact, u32 *value)
{
	u32 position = 0;

	if (!ParseWidthField(text, text_len, &position, width, width_bits) ||
		!NextHexField(text, text_len, &position, 8, compact) ||
		!NextHexField(text, text_len, &position, 8, value) ||
		position != text_len || !ValueFitsWidth(*value, *width) ||
		!CompactRangeAllowed(*compact, *width, allow_io))
		return 0;
	return 1;
}
//------------------------------------------------------------------
//------------------------------------------------------------------
static u32 ParseDirectWritePayload(const char *key, const char *payload,
	u32 payload_len)
{
	u32 position = 0;
	u32 compact;
	u32 value;
	u32 width;
	u32 full_address;

	if (KeyEquals(key, "W8"))
	{
		width = 1;
	}
	else if (KeyEquals(key, "W16"))
	{
		width = 2;
	}
	else if (KeyEquals(key, "W32"))
	{
		width = 4;
	}
	else
		return 0;

	if (!NextHexField(payload, payload_len, &position, 8, &compact) ||
		!NextHexField(payload, payload_len, &position, 8, &value) ||
		position != payload_len || !ValueFitsWidth(value, width) ||
		!CompactRangeAllowed(compact, width, 0))
		return 0;

	if (!ReserveRuntimeWriteWork(1))
		return 0;
	if (width == 1)
	{
		if (!CompactToFullRuntimeAddress(compact, 0, &full_address))
			return 0;
		return AddCheatRecord(full_address, value);
	}
	return AddCheatRecord(
		(width == 2 ? CHT_OP_WRITE16 : CHT_OP_WRITE32) |
		(compact & CHT_COMPACT_ADDRESS_MASK), value);
}
//------------------------------------------------------------------
static u32 ParseConditionOperation(const char *key, const char *payload,
	u32 payload_len)
{
	u32 before = gl_cheat_count;
	u32 width;
	u32 width_bits;
	u32 compact;
	u32 mask;
	u32 value;
	u32 position = 0;
	u32 opcode = ConditionOpcodeForKey(key);
	u32 masked = IsMaskedConditionKey(key);

	if (opcode == 0)
		return 0;
	if (!masked)
	{
		if (!ParseWidthAddressValue(payload, payload_len, 1,
			&width, &width_bits, &compact, &value))
			return 0;
		return AddCheatRecord(opcode | width_bits |
			(compact & CHT_COMPACT_ADDRESS_MASK), value);
	}

	if (!ParseWidthField(payload, payload_len, &position, &width, &width_bits) ||
		!NextHexField(payload, payload_len, &position, 8, &compact) ||
		!NextHexField(payload, payload_len, &position, 8, &mask) ||
		!NextHexField(payload, payload_len, &position, 8, &value) ||
		position != payload_len || !ValueFitsWidth(mask, width) ||
		!ValueFitsWidth(value, width) ||
		!CompactRangeAllowed(compact, width, 1) ||
		!AddCheatRecord(opcode | CHT_CONDITION_MASK_FLAG | width_bits |
			(compact & CHT_COMPACT_ADDRESS_MASK), value) ||
		!AddCheatRecord(CHT_OP_DATA, mask))
	{
		gl_cheat_count = before;
		return 0;
	}
	return 1;
}
//------------------------------------------------------------------
static u32 ParseAddSubOperation(const char *key, const char *payload,
	u32 payload_len)
{
	u32 width;
	u32 width_bits;
	u32 compact;
	u32 value;
	u32 opcode = KeyEquals(key, "ADD") ? CHT_OP_ADD : CHT_OP_SUB;

	if (!ParseWidthAddressValue(payload, payload_len, 0,
		&width, &width_bits, &compact, &value) ||
		!ReserveRuntimeWriteWork(1))
		return 0;
	return AddCheatRecord(opcode | width_bits |
		(compact & CHT_COMPACT_ADDRESS_MASK), value);
}
//------------------------------------------------------------------
static u32 ParsePointerOperation(const char *payload, u32 payload_len)
{
	u32 before = gl_cheat_count;
	u32 position = 0;
	u32 width;
	u32 width_bits;
	u32 compact;
	u32 offset;
	u32 value;

	if (!ParseWidthField(payload, payload_len, &position, &width, &width_bits) ||
		!NextHexField(payload, payload_len, &position, 8, &compact) ||
		!NextHexField(payload, payload_len, &position, 8, &offset) ||
		!NextHexField(payload, payload_len, &position, 8, &value) ||
		position != payload_len || !ValueFitsWidth(value, width) ||
		!CompactRangeAllowed(compact, 4, 0) ||
		!ReserveRuntimeWriteWork(1) ||
		!AddCheatRecord(CHT_OP_PTR | width_bits |
			(compact & CHT_COMPACT_ADDRESS_MASK), offset) ||
		!AddCheatRecord(CHT_OP_DATA, value))
	{
		gl_cheat_count = before;
		return 0;
	}
	return 1;
}
//------------------------------------------------------------------
static u32 ValidateFillRange(u32 compact, u32 count, u32 width)
{
	unsigned long long last;
	if (count == 0 || count > MAX_COMPACT_REPEAT_COUNT)
		return 0;
	last = (unsigned long long)compact +
		(unsigned long long)(count - 1) * width;
	if (last > 0xFFFFFFFFULL)
		return 0;
	return CompactRangeAllowed(compact, width, 0) &&
		CompactRangeAllowed((u32)last, width, 0) &&
		CompactRegion(compact, 0) == CompactRegion((u32)last, 0);
}
//------------------------------------------------------------------
static u32 ParseFillOperation(const char *payload, u32 payload_len)
{
	u32 before = gl_cheat_count;
	u32 position = 0;
	u32 width;
	u32 width_bits;
	u32 compact;
	u32 count;
	u32 value;

	if (!ParseWidthField(payload, payload_len, &position, &width, &width_bits) ||
		!NextHexField(payload, payload_len, &position, 8, &compact) ||
		!NextHexField(payload, payload_len, &position, 8, &count) ||
		!NextHexField(payload, payload_len, &position, 8, &value) ||
		position != payload_len || !ValueFitsWidth(value, width) ||
		!ValidateFillRange(compact, count, width) ||
		!ReserveRuntimeWriteWork(count) ||
		!AddCheatRecord(CHT_OP_COMPACT | width_bits |
			(compact & CHT_COMPACT_ADDRESS_MASK), count) ||
		!AddCheatRecord(CHT_OP_DATA, value))
	{
		gl_cheat_count = before;
		return 0;
	}
	return 1;
}
//------------------------------------------------------------------
static u32 ValidateSlideRange(u32 compact, u32 count, s32 address_step,
	u32 width)
{
	u32 region = CompactRegion(compact, 0);
	long long last;

	if (count == 0 || count > MAX_COMPACT_REPEAT_COUNT || region == 0)
		return 0;
	if (width > 1 && (address_step % (s32)width) != 0)
		return 0;

	/* A linear slide is monotonic, so validating both endpoints is enough. */
	last = (long long)compact +
		(long long)address_step * (long long)(count - 1);
	if (last < 0 || last > 0xFFFFFFFFLL)
		return 0;
	return CompactRangeAllowed(compact, width, 0) &&
		CompactRangeAllowed((u32)last, width, 0) &&
		CompactRegion((u32)last, 0) == region;
}
//------------------------------------------------------------------
static u32 ParseSlideOperation(const char *payload, u32 payload_len)
{
	u32 before = gl_cheat_count;
	u32 position = 0;
	u32 width;
	u32 width_bits;
	u32 compact;
	u32 count;
	u32 raw_address_step;
	u32 raw_value_step;
	u32 value;

	if (!ParseWidthField(payload, payload_len, &position, &width, &width_bits) ||
		!NextHexField(payload, payload_len, &position, 8, &compact) ||
		!NextHexField(payload, payload_len, &position, 8, &count) ||
		!NextHexField(payload, payload_len, &position, 8, &raw_address_step) ||
		!NextHexField(payload, payload_len, &position, 8, &raw_value_step) ||
		!NextHexField(payload, payload_len, &position, 8, &value) ||
		position != payload_len || !ValueFitsWidth(value, width) ||
		!ValidateSlideRange(compact, count, (s32)raw_address_step, width) ||
		!ReserveRuntimeWriteWork(count) ||
		!AddCheatRecord(CHT_OP_COMPACT | CHT_COMPACT_SLIDE_FLAG |
			width_bits | (compact & CHT_COMPACT_ADDRESS_MASK), count) ||
		!AddCheatRecord(CHT_OP_DATA, raw_address_step) ||
		!AddCheatRecord(CHT_OP_DATA, raw_value_step) ||
		!AddCheatRecord(CHT_OP_DATA, value))
	{
		gl_cheat_count = before;
		return 0;
	}
	return 1;
}
//------------------------------------------------------------------
/* Stock EZ-Flash byte-list values remain valid. The database key is still
 * only the visible option name; the value has no Enhanced command token. */
static u32 ParseOriginalByteListValue(const char *text, u32 text_len)
{
	u32 pos = 0;
	u32 saw_write = 0;

	while (pos < text_len)
	{
		u32 start;
		u32 end;
		u32 field_pos = 0;
		u32 compact;
		u32 byte_offset = 0;

		while (pos < text_len &&
			(text[pos] == ' ' || text[pos] == '\t' || text[pos] == ';'))
			pos++;
		if (pos >= text_len)
			break;
		start = pos;
		while (pos < text_len && text[pos] != ';')
			pos++;
		end = pos;
		while (end > start &&
			(text[end - 1] == ' ' || text[end - 1] == '\t'))
			end--;
		if (end == start ||
			!NextOriginalHexField(text + start, end - start,
				&field_pos, 8, &compact) ||
			field_pos >= end - start)
			return 0;

		while (field_pos < end - start)
		{
			u32 value;
			u32 full_address;
			u32 target = compact + byte_offset;
			if (target < compact ||
				!NextOriginalHexField(text + start, end - start,
					&field_pos, 2, &value) ||
				!CompactToFullRuntimeAddress(target, 0, &full_address) ||
				!ReserveRuntimeWriteWork(1) ||
				!AddCheatRecord(full_address, value))
				return 0;
			byte_offset++;
			saw_write = 1;
		}
	}
	return saw_write;
}
//------------------------------------------------------------------
static u32 SplitRuntimeToken(const char *segment, u32 segment_len,
	char *key, u32 key_capacity, const char **payload, u32 *payload_len)
{
	u32 pos;

	for (pos = 0; pos < segment_len; pos++)
	{
		if (segment[pos] == '=')
			return 0;
		if (segment[pos] == ':')
		{
			u32 key_len = pos;
			while (key_len > 0 &&
				(segment[key_len - 1] == ' ' || segment[key_len - 1] == '\t'))
				key_len--;
			if (key_len == 0 || key_len >= key_capacity)
				return 0;
			memcpy(key, segment, key_len);
			key[key_len] = 0;
			*payload = segment + pos + 1;
			*payload_len = segment_len - pos - 1;
			return *payload_len != 0;
		}
	}
	return 0;
}
//------------------------------------------------------------------
static u32 IsEnhancedCommandKey(const char *key)
{
	return IsConditionKeyName(key) ||
		KeyEquals(key, "W8") || KeyEquals(key, "W16") ||
		KeyEquals(key, "W32") || KeyEquals(key, "ADD") ||
		KeyEquals(key, "SUB") || KeyEquals(key, "PTR") ||
		KeyEquals(key, "FILL") || KeyEquals(key, "SLIDE") ||
		KeyEquals(key, "ROMIF") || KeyEquals(key, "ROM");
}
//------------------------------------------------------------------
static u32 LooksLikeEnhancedOptionValue(const char *text, u32 text_len)
{
	u32 start = 0;
	u32 end;
	char key[16] = {0};
	const char *payload = NULL;
	u32 payload_len = 0;

	while (start < text_len &&
		(text[start] == ' ' || text[start] == '\t' || text[start] == ';'))
		start++;
	end = start;
	while (end < text_len && text[end] != ';')
		end++;
	if (end == start ||
		!SplitRuntimeToken(text + start, end - start,
			key, sizeof(key), &payload, &payload_len))
		return 0;

	return IsEnhancedCommandKey(key);
}
//------------------------------------------------------------------
static void MarkTextAction(CHT_TEXT_CONDITION *stack, u32 depth)
{
	if (depth == 0)
		return;
	if (stack[depth - 1].has_else)
		stack[depth - 1].false_action = 1;
	else
		stack[depth - 1].true_action = 1;
}
//------------------------------------------------------------------
static u32 ParseEnhancedOptionValue(const char *text, u32 text_len)
{
	CHT_TEXT_CONDITION stack[MAX_TEXT_CONDITION_DEPTH];
	u32 runtime_before = gl_cheat_count;
	u32 work_before = gl_runtime_write_work;
	u32 conditions_before = gl_rom_condition_count;
	u32 patches_before = gl_rom_patch_count;
	u32 groups_before = gl_rom_group_count;
	u32 pos = 0;
	u32 depth = 0;
	u32 saw_action = 0;
	u32 has_rom_data = 0;

	memset(stack, 0, sizeof(stack));
	if (!LooksLikeEnhancedOptionValue(text, text_len))
	{
		if (!ParseOriginalByteListValue(text, text_len))
			goto rollback;
		return 1;
	}
	while (pos < text_len)
	{
		u32 start;
		u32 end;
		u32 segment_len;
		char key[16] = {0};
		const char *payload = NULL;
		u32 payload_len = 0;

		while (pos < text_len &&
			(text[pos] == ' ' || text[pos] == '\t' || text[pos] == ';'))
			pos++;
		if (pos >= text_len)
			break;
		start = pos;
		while (pos < text_len && text[pos] != ';')
			pos++;
		end = pos;
		while (end > start &&
			(text[end - 1] == ' ' || text[end - 1] == '\t'))
			end--;
		segment_len = end - start;
		if (segment_len == 0)
			continue;

		if (segment_len == 4 && memcmp(text + start, "ELSE", 4) == 0)
		{
			if (depth == 0 || !stack[depth - 1].true_action ||
				stack[depth - 1].has_else ||
				!AddCheatRecord(CHT_OP_ELSE, 0))
				goto rollback;
			stack[depth - 1].has_else = 1;
			continue;
		}
		if (segment_len == 5 && memcmp(text + start, "ENDIF", 5) == 0)
		{
			if (depth == 0 || !stack[depth - 1].true_action ||
				(stack[depth - 1].has_else && !stack[depth - 1].false_action) ||
				!AddCheatRecord(CHT_OP_ENDIF, 0))
				goto rollback;
			depth--;
			MarkTextAction(stack, depth);
			continue;
		}

		if (!SplitRuntimeToken(text + start, segment_len,
			key, sizeof(key), &payload, &payload_len))
			goto rollback;

		if (IsConditionKeyName(key))
		{
			if (depth >= MAX_TEXT_CONDITION_DEPTH ||
				!ParseConditionOperation(key, payload, payload_len))
				goto rollback;
			memset(&stack[depth], 0, sizeof(stack[depth]));
			depth++;
			continue;
		}
		if (KeyEquals(key, "W8") || KeyEquals(key, "W16") ||
			KeyEquals(key, "W32"))
		{
			if (!ParseDirectWritePayload(key, payload, payload_len))
				goto rollback;
		}
		else if (KeyEquals(key, "ADD") || KeyEquals(key, "SUB"))
		{
			if (!ParseAddSubOperation(key, payload, payload_len))
				goto rollback;
		}
		else if (KeyEquals(key, "PTR"))
		{
			if (!ParsePointerOperation(payload, payload_len))
				goto rollback;
		}
		else if (KeyEquals(key, "FILL"))
		{
			if (!ParseFillOperation(payload, payload_len))
				goto rollback;
		}
		else if (KeyEquals(key, "SLIDE"))
		{
			if (!ParseSlideOperation(payload, payload_len))
				goto rollback;
		}
		else if (KeyEquals(key, "ROMIF"))
		{
			/* ROM guards are pre-launch and must precede runtime actions. */
			if (depth != 0 || gl_cheat_count != runtime_before ||
				gl_rom_patch_count != patches_before ||
				!ParseRomByteRun(payload, payload_len, pROMCONDITION_OFFSET,
				pROMCONDITION_VALUE, &gl_rom_condition_count,
				MAX_ROM_CONDITION_BYTES))
				goto rollback;
			has_rom_data = 1;
			continue;
		}
		else if (KeyEquals(key, "ROM"))
		{
			/* ROM patches are pre-launch and cannot live inside runtime IF. */
			if (depth != 0 ||
				!ParseRomByteRun(payload, payload_len, pROMPATCH_OFFSET,
				pROMPATCH_VALUE, &gl_rom_patch_count,
				MAX_ROM_PATCH_BYTES))
				goto rollback;
			has_rom_data = 1;
		}
		else
			goto rollback;

		MarkTextAction(stack, depth);
		saw_action = 1;
	}

	if (depth != 0 || !saw_action)
		goto rollback;

	if (has_rom_data)
	{
		CHT_ROM_GROUP *group;
		if (gl_rom_group_count >= MAX_ROM_CHEAT_GROUPS)
			goto rollback;
		group = &pROMGROUP[gl_rom_group_count];
		group->runtime_start = runtime_before;
		group->runtime_count = gl_cheat_count - runtime_before;
		group->condition_start = conditions_before;
		group->condition_count = gl_rom_condition_count - conditions_before;
		group->patch_start = patches_before;
		group->patch_count = gl_rom_patch_count - patches_before;
		group->active = group->condition_count == 0;
		group->reserved = 0;
		gl_rom_group_count++;
	}
	return 1;

rollback:
	gl_cheat_count = runtime_before;
	gl_runtime_write_work = work_before;
	gl_rom_condition_count = conditions_before;
	gl_rom_patch_count = patches_before;
	gl_rom_group_count = groups_before;
	return 0;
}
//------------------------------------------------------------------
unsigned long str2hex( char*str)
{
	unsigned long sum=0;
	unsigned long i;
	int len = strlen(str);

	if(len >8) return 0;
	for(i=0;i<len;i++)
	{
		if(str[i] >= '0' && str[i] <= '9')
			sum = sum*16 + str[i]-'0';
		else if(str[i] >= 'a' && str[i] <= 'f')
			sum = sum*16 + str[i]-0x57;
		else if(str[i] >= 'A' && str[i] <= 'F')
			sum = sum*16 + str[i]-0x37;
	}
	return sum;
}
//------------------------------------------------------------------
static u32 Analyze_KEYVAL(FIL* file,u32 total)
{
	u32 tol;

	ResetCompiledCheats();
	gl_cheat_selected_count = 0;
	gl_cheat_compile_errors = gl_cheat_menu_truncated ? 1 : 0;

	for(tol=0;tol<total;tol++)
	{
		if (((FM_CHT_LINE*)pCHTbuffer)[tol].is_section == 1)
			continue;
		if (((FM_CHT_LINE*)pCHTbuffer)[tol].select == 1)
		{
			u32 buflen;
			const char *option_name;
			const char *section_name = "";
			u16 parent_section;

			parent_section =
				((FM_CHT_LINE*)pCHTbuffer)[tol].parent_section;
			if (parent_section != CHT_NO_SECTION)
			{
				if (parent_section >= total ||
					((FM_CHT_LINE*)pCHTbuffer)[parent_section].is_section != 1)
				{
					((FM_CHT_LINE*)pCHTbuffer)[tol].select = 0;
					gl_cheat_compile_errors++;
					continue;
				}
				section_name =
					((FM_CHT_LINE*)pCHTbuffer)[parent_section].LINEname;
			}

			option_name = ((FM_CHT_LINE*)pCHTbuffer)[tol].LINEname;
			buflen = Get_CHT_val(&gfile, (char*)section_name,
				(char*)option_name);
			if (buflen != 0 && !gl_cheat_value_truncated &&
				ParseEnhancedOptionValue(_paramv, buflen))
				gl_cheat_selected_count++;
			else
			{
				((FM_CHT_LINE*)pCHTbuffer)[tol].select = 0;
				gl_cheat_compile_errors++;
			}
		}
	}
	CheatSelectionRemember(total);
	return gl_cheat_compile_errors;
}
//------------------------------------------------------------------
u32 Check_count(u32 all_count)
{
	u32 count=0;
	u32 Line;
	for(Line=0;Line<all_count;Line++)
	{
		if(((FM_CHT_LINE*)pCHTbuffer)[Line].is_section == 1 &&
			strcmp(((FM_CHT_LINE*)pCHTbuffer)[Line].LINEname,"GameInfo")==0)
			break;

		count++;
		//if(count>512)
			//break;
	}
	return count;
}
//------------------------------------------------------------------
unsigned char HexToChar(unsigned char bChar)
{
    if((bChar>=0x30)&&(bChar<=0x39))
    {
        bChar -= 0x30;
    }
    else if((bChar>=0x41)&&(bChar<=0x46)) // Capital
    {
        bChar -= 0x37;
    }
    else if((bChar>=0x61)&&(bChar<=0x66)) //littlecase
    {
        bChar -= 0x57;
    }
    else
    {
        bChar = 0xff;
    }
    return bChar;
}
//------------------------------------------------------------------
u32 Change2cht_folder(u32 chtname)
{
	TCHAR chtnamebuf[100];
	u32 res;
	TCHAR* folder_name;
	TCHAR currentpath[256];
	memset(currentpath,00,256);

	memset(chtnamebuf,0x00,100);
	sprintf(chtnamebuf,"%d%d%d%d",HexToChar(((u8*)&chtname)[0]),HexToChar(((u8*)&chtname)[1]),HexToChar(((u8*)&chtname)[2]),HexToChar(  ((u8*)&chtname)[3] )  );
	u32 num=atoi(chtnamebuf);
	//DEBUG_printf("num =%d", num);
	if(num < 200){
		folder_name = (TCHAR*)"0000";
	}
	else if(num < 400){
		folder_name = (TCHAR*)"0200";
	}
	else if(num < 600){
		folder_name = (TCHAR*)"0400";
	}
	else if(num < 800){
		folder_name = (TCHAR*)"0600";
	}
	else if(num < 1000){
		folder_name = (TCHAR*)"0800";
	}
	else if(num < 1200){
		folder_name = (TCHAR*)"1000";
	}
	else if(num < 1400){
		folder_name = (TCHAR*)"1200";
	}
	else if(num < 1600){
		folder_name = (TCHAR*)"1400";
	}
	else if(num < 1800){
		folder_name = (TCHAR*)"1600";
	}
	else if(num < 2000){
		folder_name = (TCHAR*)"1800";
	}
	else if(num < 2200){
		folder_name = (TCHAR*)"2000";
	}
	else if(num < 2400){
		folder_name = (TCHAR*)"2200";
	}
	else if(num < 2600){
		folder_name = (TCHAR*)"2400";
	}
	else if(num < 2800){
		folder_name = (TCHAR*)"2600";
	}
	else {
		folder_name = (TCHAR*)"2800";
	}


	if(!cheat_use_chinese_folder)
	{
		sprintf(currentpath,"/SYSTEM/CHEAT/Eng/%s",folder_name);
	}
	else{
		sprintf(currentpath,"/SYSTEM/CHEAT/Chn/%s",folder_name);
	}
	res=f_chdir(currentpath);
	return res;
}
//------------------------------------------------------------------
u32 Check_cheat_file(TCHAR *gamefilename)
{
	u32 res;
	UINT ret;
	TCHAR chtnamebuf[100];
	u32 filesize;
	u32 GAMEID=0;
	u32 i;
	cheat_use_chinese_folder = (gl_select_lang == 0xE2E2);

	res = f_open(&gfile, gamefilename, FA_READ);
	if(res == FR_OK)
	{
		f_lseek(&gfile, 0xAC);
		f_read(&gfile, &GAMEID, 4, (UINT *)&ret);
		f_close(&gfile);
		if(GAMEID==0) return 0;
	}

	memcpy(chtnamebuf,gamefilename,100);
	u32 len=strlen(chtnamebuf);
	chtnamebuf[len-3] = 'c';
	chtnamebuf[len-2] = 'h';
	chtnamebuf[len-1] = 't';

	res=f_chdir("/SYSTEM/CHEAT");
	if(res != FR_OK){
		return 0;
	}

	res = f_open(&gfile,chtnamebuf, FA_OPEN_EXISTING);
	//f_chdir(currentpath);
	if(res == FR_OK)//have a cht file
	{
		f_close(&gfile);
		return 0x0000FFFF;
	}
	else
	{
		res = f_open(&gfile,"GameID2cht.bin", FA_READ);
		u32* tempbuff = (u32*)(pReadCache);
		if(res == FR_OK)//have a file
		{
			filesize = f_size(&gfile);
			if(filesize > 0x10000) filesize=0x10000;
			f_lseek(&gfile, 0x0);
			f_read(&gfile, tempbuff, filesize, &ret);//pReadCache max 0x20000 Byte

			for(i=0;i<filesize/4;i+=2)
			{

				if(tempbuff[i]== GAMEID)
				{
					f_close(&gfile);

					u32 chtname= ((u32*)tempbuff)[i+1];

					cheat_use_chinese_folder = (gl_select_lang == 0xE2E2);
					res=Change2cht_folder(chtname);
					if(res!=0)
					{
						cheat_use_chinese_folder = !cheat_use_chinese_folder;
						res=Change2cht_folder(chtname);
					}
					if(res!=0)return 0;
					memset(chtnamebuf,0x00,100);
					sprintf(chtnamebuf,"%d%d%d%d.cht",HexToChar(((u8*)&chtname)[0]),HexToChar(((u8*)&chtname)[1]),HexToChar(((u8*)&chtname)[2]),HexToChar(  ((u8*)&chtname)[3] )  );
					res = f_open(&gfile,chtnamebuf, FA_OPEN_EXISTING);

					if(res == FR_OK)//have a cht file
					{
						f_close(&gfile);
						return chtname;
					}
					else{
						return 0;
					}
				}
			}
		}
		return 0;
	}
}
//---------------------------------------------------------------------------------
void Show_num(u32 totalcount,u32 select)
{
	u32 i;
	u32 total_sections = 0;
	u32 selected_section = 0;
	u32 line = select ? select - 1 : 0;

	if(totalcount == 0)
	{
		Launcher_DrawCheatCounter(0, 0);
		return;
	}
	if(line >= totalcount)
		line = totalcount - 1;
	for(i = 0; i < totalcount; i++)
	{
		if(((FM_CHT_LINE*)pCHTbuffer)[i].is_section == 1)
		{
			total_sections++;
			if(i <= line)
				selected_section = total_sections;
		}
	}
	if(selected_section == 0 && total_sections)
		selected_section = 1;
	Launcher_DrawCheatCounter(total_sections, selected_section);
}
//------------------------------------------------------------------
static void ShowCheatCompileWarning(u32 error_count)
{
	char msg[48];
	u16 pressed;

	Launcher_ClearCheatRegion(0, 19, 240, 160 - 19);
	sprintf(msg, "%lu cheat%s skipped", error_count,
		error_count == 1 ? "" : "s");
	DrawCheatText12(msg, 30, 44, 58, gl_color_text, 1);
	strcpy(msg, "Invalid format or limit reached");
	DrawCheatText12(msg, 30, 27, 79, gl_color_text, 1);
	strcpy(msg, "Press A or B to continue");
	DrawCheatText12(msg, 30, 42, 107, gl_color_text, 1);

	do
	{
		VBlankIntrWait();
		scanKeys();
	} while (keysHeld() != 0);
	do
	{
		VBlankIntrWait();
		scanKeys();
		pressed = keysDown();
	} while ((pressed & (KEY_A | KEY_B)) == 0);
}
//------------------------------------------------------------------
void Open_cht_file(TCHAR *gamefilename,u32 havecht)
{
	u32 res;
	char msg[128];
	TCHAR chtnamebuf[100];

	char buffer[127]={0};

	if(havecht == 0x0000FFFF)
	{
		res=f_chdir("/SYSTEM/CHEAT");
		if(res != FR_OK){
			return;
		}
		memcpy(chtnamebuf,gamefilename,100);
		u32 len=strlen(chtnamebuf);
		chtnamebuf[len-3] = 'c';
		chtnamebuf[len-2] = 'h';
		chtnamebuf[len-1] = 't';
	}
	else
	{
		Change2cht_folder(havecht);
		u8* chtmode;
		chtmode = (u8*)&havecht;
		sprintf(chtnamebuf,"%d%d%d%d.cht",HexToChar(chtmode[0]),HexToChar(chtmode[1]),HexToChar(chtmode[2]),HexToChar(chtmode[3]));
	}
	res = f_open(&gfile,chtnamebuf, FA_READ);

	if(res == FR_OK)//have a cht file
	{
		CheatSelectionMakeKey(cheat_active_key, sizeof(cheat_active_key), gamefilename, havecht);
		if(strcmp(cheat_selection_key, cheat_active_key))
			gl_cheat_selected_count = 0;

		strncpy(buffer, gamefilename, sizeof(buffer) - 1);
		buffer[sizeof(buffer) - 1] = '\0';
		Clean_cheat_title(buffer);
		sprintf(msg,"%s",buffer);

		Launcher_DrawCheatBackground(msg);

		u32 all_count = Get_all_Section_val(&gfile);
		u32 Select = 0;
		u32 showoffset = 0;
		u32 re_show = 2;
		u32 old_select = 1;
		u32 old_showoffset = 0;
		u32 cheat_scroll_delay = 0;
		u32 visible_count;

		CheatSelectionRestore(all_count);

		if(all_count)
		{
			all_count = Check_count(all_count);//cut from "GameInfo"
			visible_count = GetVisibleCheatMenuCount(all_count);
			if (visible_count == 0)
			{
				f_close(&gfile);
				Launcher_MarkTopbarNameDirty();
				return;
			}
			setRepeat(15,1);
			while(1)//3
			{
				VBlankIntrWait();
				VBlankIntrWait();
				UIAudio_UpdateExport();
				if(re_show)
				{
					if(re_show>1)
					{
						Launcher_ClearCheatRegion(0, 19, 240, 160-19);
						Show_KEY_val(all_count, visible_count, Select, showoffset);
					}
					else if(old_showoffset == showoffset)
					{
						Show_KEY_line(old_select, Select, showoffset, all_count);
						if(old_select != Select)
							Show_KEY_line(Select, Select, showoffset, all_count);
					}
					else
						Show_KEY_val(all_count, visible_count, Select, showoffset);
					{
					u32 selected_entry = GetVisibleCheatMenuEntryIndex(all_count, showoffset + Select);
					Show_num(all_count, selected_entry < all_count ? selected_entry + 1 : 1);
				}
					old_select = Select;
					old_showoffset = showoffset;
					re_show = 0;
				}
				else
				{
					Launcher_UpdateCheatTitle();
				}
				if(cheat_scroll_delay > 0)
					cheat_scroll_delay--;
				scanKeys();
				u16 keysdown  = keysDown();
				u16 keysup = keysUp();
				u16 keysrepeat = keysDownRepeat();

				if ((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && cheat_scroll_delay == 0))
				{
					u32 cursor = showoffset + Select;
					u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
					if (cursor + 1 < visible_count)
					{
						cursor++;
						if (cursor >= showoffset + 10)
						{
							showoffset = cursor - 9;
							Select = 9;
							re_show = 2;
						}
						else
						{
							Select++;
							re_show = 1;
						}
						UIAudio_PlayMove();
						cheat_scroll_delay = first_press ? 6 : 3;
					}
				}
				else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && cheat_scroll_delay == 0))
				{
					u32 cursor = showoffset + Select;
					u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
					if (cursor > 0)
					{
						cursor--;
						if (cursor < showoffset)
						{
							showoffset = cursor;
							Select = 0;
							re_show = 2;
						}
						else
						{
							Select--;
							re_show = 1;
						}
						UIAudio_PlayMove();
						cheat_scroll_delay = first_press ? 6 : 3;
					}
				}
				else if((keysdown & KEY_LEFT) || ((keysrepeat & KEY_LEFT) && cheat_scroll_delay == 0))
				{
					u32 entry_index = GetVisibleCheatMenuEntryIndex(all_count, showoffset + Select);
					u32 first_press = (keysdown & KEY_LEFT) ? 1 : 0;
					if (entry_index < all_count &&
						((FM_CHT_LINE*)pCHTbuffer)[entry_index].is_section == 1)
					{
						if (SetCheatGroupExpanded(entry_index, all_count, 0))
						{
							visible_count = GetVisibleCheatMenuCount(all_count);
							UIAudio_PlayAcceptExport();
							re_show = 2;
						}
					}
					else if (showoffset > 0)
					{
						u32 cursor = showoffset + Select;
						showoffset = showoffset > 10 ? showoffset - 10 : 0;
						if (cursor < showoffset) cursor = showoffset;
						Select = cursor - showoffset;
						if (Select > 9) Select = 9;
						UIAudio_PlayMove();
						re_show = 2;
						cheat_scroll_delay = first_press ? 6 : 3;
					}
				}
				else if((keysdown & KEY_RIGHT) || ((keysrepeat & KEY_RIGHT) && cheat_scroll_delay == 0))
				{
					u32 entry_index = GetVisibleCheatMenuEntryIndex(all_count, showoffset + Select);
					u32 first_press = (keysdown & KEY_RIGHT) ? 1 : 0;
					if (entry_index < all_count &&
						((FM_CHT_LINE*)pCHTbuffer)[entry_index].is_section == 1)
					{
						if (SetCheatGroupExpanded(entry_index, all_count, 1))
						{
							visible_count = GetVisibleCheatMenuCount(all_count);
							UIAudio_PlayAcceptExport();
							re_show = 2;
						}
					}
					else if (showoffset + 10 < visible_count)
					{
						u32 cursor = showoffset + Select;
						u32 max_offset = visible_count > 10 ? visible_count - 10 : 0;
						showoffset += 10;
						if (showoffset > max_offset) showoffset = max_offset;
						if (cursor < showoffset) cursor = showoffset;
						Select = cursor - showoffset;
						if (Select > 9) Select = 9;
						UIAudio_PlayMove();
						re_show = 2;
						cheat_scroll_delay = first_press ? 6 : 3;
					}
				}
				else if(keysdown & KEY_A)
				{
					u32 entry_index = GetVisibleCheatMenuEntryIndex(all_count, showoffset + Select);
					if (entry_index < all_count &&
						((FM_CHT_LINE*)pCHTbuffer)[entry_index].is_section == 1)
					{
						SetCheatGroupExpanded(entry_index, all_count,
							((FM_CHT_LINE*)pCHTbuffer)[entry_index].select == CHT_GROUP_COLLAPSED);
						visible_count = GetVisibleCheatMenuCount(all_count);
						re_show = 2;
					}
					else if (entry_index < all_count)
					{
						ToggleCheatMenuEntry(entry_index, all_count);
						re_show = 1;
					}
					UIAudio_PlayAcceptExport();
				}
				else if(keysup & KEY_B)
				{
					UIAudio_PlayBackExport();
					UIAudio_WaitForCurrentClipExport(60);
					UIAudio_CutOffTrailingClipExport();
					{
						u32 compile_errors = Analyze_KEYVAL(&gfile,all_count);
						if (compile_errors)
							ShowCheatCompileWarning(compile_errors);
					}
					break;
				}
			}
		}
	}
	f_close(&gfile);
	Launcher_MarkTopbarNameDirty();
}
