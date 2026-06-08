#include <gba_video.h>
#include <gba_interrupt.h>
#include <gba_systemcalls.h>
#include <gba_input.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <gba_dma.h>
#include <string.h>
#include <stdarg.h>
#include <gba_timers.h>

#include "ez_define.h"
#include "ff.h"
#include "draw.h"
#include "ezkernel.h"
#include "Ezcard_OP.h"
#include "saveMODE.h"
#include "RTC.h"
#include "NORflash_OP.h"
#include "lang.h"
#include "GBApatch.h"
#include "showcht.h"
#include "helpwindow.h"
#include "launcher_version.h"
#include "launcher_text.h"

static void Launcher_SaveUnifiedSettings(void);
static void Launcher_SaveSettingsInfo(void);
extern u16 gl_select_lang;

static u32 launcher_language_index = 0;
#include "launcher_runtime_text.h"

static const char *Launcher_Text(LauncherTextId id)
{
	if(id >= LTXT_TOTAL)
		return "";
	if(launcher_language_index >= LAUNCHER_LANGUAGE_COUNT)
		launcher_language_index = 0;
	return launcher_language_packs[launcher_language_index].text[id];
}

static u32 Launcher_LanguageIndexFromStored(u16 stored)
{
	u32 i;
	for(i = 0; i < LAUNCHER_LANGUAGE_COUNT; i++)
	{
		if(launcher_language_packs[i].stored == stored)
			return i;
	}
	return 0;
}

static void Launcher_ApplyLanguageIndex(u32 index)
{
	if(index >= LAUNCHER_LANGUAGE_COUNT)
		index = 0;
	launcher_language_index = index;
	gl_select_lang = launcher_language_packs[launcher_language_index].stored;
	if(gl_select_lang == 0xE2E2)
		LoadChinese();
	else
		LoadEnglish();
}

static const char *Launcher_LanguageName(void)
{
	if(launcher_language_index >= LAUNCHER_LANGUAGE_COUNT)
		launcher_language_index = 0;
	return launcher_language_packs[launcher_language_index].name;
}

static void Launcher_CycleLanguage(int dir)
{
	u32 index = launcher_language_index;
	if(dir < 0)
		index = (index == 0) ? (LAUNCHER_LANGUAGE_COUNT - 1) : (index - 1);
	else
		index = (index + 1) % LAUNCHER_LANGUAGE_COUNT;
	Launcher_ApplyLanguageIndex(index);
	Launcher_SaveUnifiedSettings();
	Launcher_SaveSettingsInfo();
}

static char launcher_system_name[32] = "";
static u32 launcher_system_name_dirty = 1;
static u32 launcher_start_selected = 0;
static TCHAR launcher_sd_saved_path[MAX_path_len];
static u32 launcher_sd_saved_folder_select = 1;
static u32 launcher_sd_restore_pending = 0;
static u32 launcher_start_release_suppressed = 0;
#define LAUNCHER_MAX_FAVOURITES 10
#define LAUNCHER_FAVOURITE_PATH_LEN 256
static u32 launcher_favourite_count = 0;
static u32 launcher_favourite_index = 0;
static u32 launcher_favourites_cache_valid = 0;
static char launcher_favourites_cache[LAUNCHER_MAX_FAVOURITES][LAUNCHER_FAVOURITE_PATH_LEN];
static u32 launcher_start_uses_favourites = 0;
static u32 launcher_start_screen_off = 0;
static u32 launcher_select_release_cooldown = 0;
static u32 launcher_suppress_next_select_cycle = 0;
static u32 launcher_start_title_scroll_offset = 0;
static u32 launcher_start_title_scroll_frame = 0;

#define LAUNCHER_TOP_BAR_HEIGHT 19
#define LAUNCHER_SELECTED_TEXT gl_color_selected
#define LAUNCHER_START_THUMB_W 56
#define LAUNCHER_START_THUMB_H 37
#define LAUNCHER_START_PREVIEW_CACHE_COUNT 1
#define LAUNCHER_THUMB_STYLE_TITLE 0
#define LAUNCHER_THUMB_STYLE_BOX 1
#define LAUNCHER_THUMB_BMP_HEADER 0x36
#define LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX 256
#define LAUNCHER_SYSTEM_NAME_DISPLAY_MAX 11
#define LAUNCHER_THEME_MODE_LIGHT 0
#define LAUNCHER_THEME_MODE_DARK 1
#define LAUNCHER_THEME_MODE_CUSTOM 2

static u16 launcher_start_preview_cache[LAUNCHER_START_PREVIEW_CACHE_COUNT][LAUNCHER_START_THUMB_W * LAUNCHER_START_THUMB_H]EWRAM_BSS;
static char launcher_start_preview_path[LAUNCHER_START_PREVIEW_CACHE_COUNT][LAUNCHER_FAVOURITE_PATH_LEN];
static u8 launcher_start_preview_valid[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u8 launcher_start_preview_mode[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u32 launcher_start_preview_index[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u8 launcher_custom_thumb_manifest_loaded[2];
static u8 launcher_custom_thumb_manifest_present[2];
static u16 launcher_custom_thumb_manifest_count[2];
static u32 launcher_custom_thumb_manifest_hash[2][LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX]EWRAM_BSS;

static void Launcher_SaveSDState(void);
static void Launcher_RestoreSDState(void);
static void Launcher_DrawTopbarName(u32 page_num);
static void Launcher_DrawTopbarTitle(u32 page_num, const char *title);
static const char* Launcher_GetCurrentFolderLabel(void);
static void Launcher_MakeEllipsisText(const char *src, char *dst, u32 dst_size, u32 max_chars);
const unsigned char *Launcher_ImageSDList(void);
static const char *Launcher_AutoStartText(void);
static void Launcher_CycleAutoStartKey(int dir);
static const char *Launcher_StartSourceText(void);
static void Launcher_CycleStartSource(void);
static void Launcher_CycleStartEnabled(void);
static const char *Launcher_OnOffText(u16 value);
static void Launcher_LoadFavourites(void);
static u32 Read_last_played_entry(TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size);
static void Launcher_GetSDDisplayNameWithFavourite(u32 file_index, char *out, u32 out_size);
static u32 Launcher_IsFavouriteSDIndex(u32 absolute_index);
static void Launcher_DrawFavouriteHeart(int x, int y, u16 colour);
static char (*Launcher_FavouritesBuffer(void))[LAUNCHER_FAVOURITE_PATH_LEN];
static s32 Launcher_FindFavouriteFullPath(const char *fullpath);
static void Launcher_StartPreviewCacheInvalidate(void);
static u32 Launcher_IsGbaFilename(const TCHAR *pfilename);
static u32 Launcher_ThumbnailSourceWidth(void);
static u32 Launcher_ThumbnailSourceHeight(void);
static u32 Launcher_ThumbnailReadSize(void);
static const char *Launcher_ThumbnailStyleText(void);
static void Launcher_ReadThumbnailStyle(void);
static void Launcher_DrawThumbInBox(const u16 *src, int src_w, int src_h, int box_x, int box_y, int box_w, int box_h);
static void Launcher_ScaleThumbToBox(const u16 *src, int src_w, int src_h, u16 *dst, int box_w, int box_h);
static void Launcher_ScaleThumb80x80_To40x40(const u16 *src, u16 *dst);
static void Launcher_DrawStartLastTitle(u32 selected);
static void Launcher_DrawPicClipStride(const u16 *src, int src_stride, int x, int y, int w, int h);
static void Launcher_InitScaleMaps(void);
static const u16 *Launcher_NotFoundImage(void);
static int Launcher_NotFoundWidth(void);
static int Launcher_NotFoundHeight(void);
u32 Load_ThumbnailEx(TCHAR *pfilename_pic, u8 *dst);
u32 Check_file_type(TCHAR *pfilename);

#define LAUNCHER_THEME_ASSET_DEFINITIONS
#undef gImage_HELP
#undef gImage_MENU
#undef gImage_SD_LIST
#undef gImage_SD_HORIZONTAL
#undef gImage_SD_VERTICAL
#undef gImage_SET
#undef gImage_START
#undef gImage_icon_gba
#undef gImage_icon_folder
#undef gImage_icon_chip
#include "launcher_theme_assets.h"
#undef LAUNCHER_THEME_ASSET_DEFINITIONS

#ifndef LAUNCHER_CUSTOM_THEME_ENABLED
#define LAUNCHER_CUSTOM_THEME_ENABLED 0
#endif

#define gImage_HELP (Launcher_ImageHELP())
#define gImage_MENU (Launcher_ImageMENU())
#define gImage_SD_LIST (Launcher_ImageSDList())
#define gImage_SD_HORIZONTAL (Launcher_ImageSDHorizontal())
#define gImage_SD_VERTICAL (Launcher_ImageSDVertical())
#define gImage_SET (Launcher_ImageSET())
#define gImage_START (Launcher_ImageSTART())
#define gImage_icon_gba (Launcher_ImageIconGBA())
#define gImage_icon_folder (Launcher_ImageIconFolder())
#define gImage_icon_chip (Launcher_ImageIconChip())


#include "icon_CV.h"
#include "icon_MSX.h"
#include "icon_GG.h"
#include "icon_SMS.h"
#include "icon_SV.h"
#include "icon_a26.h"
#include "icon_GBC.h"
#include "icon_WS.h"
#include "icon_FC.h"
#include "icon_GB.h"
#include "icon_SG.h"
#include "icon_NG.h"
#include "icon_IMG.h"
#include "icon_TXT.h"
#include "icon_PCE.h"
#include "icon_ZX.h"
#include "icon_o2.h"
#include "icon_pokem.h"
#include "icon_vmu.h"
#include "icon_wav.h"
#include "icon_arc.h"
#include "icon_sc3000.h"
#include "icon_EXE.h"
#include "icon_mod.h"
#include "icon_other.h"
#include "Chinese_manual.h"
#include "English_manual.h"

#include "nor_icon.h"
#include "NOTFOUND.h"
#include "NOTFOUNDsquare.h"
#include "SPLASH.h"


#include "goomba.h"

#include "accept_raw.h"
#include "back_raw.h"
#include "menu_raw.h"
#include "move_raw.h"
#include "startup_raw.h"
#include "tab_raw.h"
#include "launcher_customiser_config.h"

#ifndef LAUNCHER_BOOT_SOUND_ENABLED
#define LAUNCHER_BOOT_SOUND_ENABLED 1
#endif

#ifndef LAUNCHER_CUSTOM_THEME_DARK_STYLE
#define LAUNCHER_CUSTOM_THEME_DARK_STYLE 0
#endif

#ifndef LAUNCHER_THUMB_BORDER_ENABLED
#define LAUNCHER_THUMB_BORDER_ENABLED 0
#endif

#ifndef LAUNCHER_START_SELECTION_MODE
#define LAUNCHER_START_SELECTION_MODE 0
#endif
#ifndef LAUNCHER_START_SELECTION_SHAPE
#define LAUNCHER_START_SELECTION_SHAPE 0
#endif
#ifndef LAUNCHER_START_SELECTION_ANIMATE
#define LAUNCHER_START_SELECTION_ANIMATE 1
#endif
#define LAUNCHER_START_SELECTION_OFF 3
#ifndef LAUNCHER_START_NAV_MODE
#define LAUNCHER_START_NAV_MODE 0
#endif
#ifndef LAUNCHER_HORZ_NAV_MODE
#define LAUNCHER_HORZ_NAV_MODE 0
#endif
#ifndef LAUNCHER_VERT_NAV_MODE
#define LAUNCHER_VERT_NAV_MODE 0
#endif
#ifndef LAUNCHER_START_LAST_X
#define LAUNCHER_START_LAST_X 25
#define LAUNCHER_START_LAST_Y 43
#define LAUNCHER_START_LAST_W 190
#define LAUNCHER_START_LAST_H 47
#define LAUNCHER_START_LAST_THUMB_X 30
#define LAUNCHER_START_LAST_THUMB_Y 48
#define LAUNCHER_START_LAST_TEXT_X 93
#define LAUNCHER_START_LAST_TEXT_Y 49
#define LAUNCHER_START_LAST_TEXT_W 114
#define LAUNCHER_START_LAST_TEXT_H 42
#define LAUNCHER_START_LAST_TEXT_CY 66
#define LAUNCHER_START_LAST_TEXT_LINES 3
#define LAUNCHER_START_LAST_TEXT_ALIGN 1
#define LAUNCHER_START_SD_X 25
#define LAUNCHER_START_SD_Y 92
#define LAUNCHER_START_SD_W 95
#define LAUNCHER_START_SD_H 45
#define LAUNCHER_START_SD_TEXT_X 42
#define LAUNCHER_START_SD_TEXT_Y 108
#define LAUNCHER_START_SD_TEXT_W 60
#define LAUNCHER_START_SD_TEXT_ALIGN 1
#define LAUNCHER_START_NOR_X 120
#define LAUNCHER_START_NOR_Y 92
#define LAUNCHER_START_NOR_W 95
#define LAUNCHER_START_NOR_H 45
#define LAUNCHER_START_NOR_TEXT_X 137
#define LAUNCHER_START_NOR_TEXT_Y 108
#define LAUNCHER_START_NOR_TEXT_W 60
#define LAUNCHER_START_NOR_TEXT_ALIGN 1
#define LAUNCHER_START_SETTINGS_X 111
#define LAUNCHER_START_SETTINGS_Y 145
#define LAUNCHER_START_SETTINGS_W 18
#define LAUNCHER_START_SETTINGS_H 11
#define LAUNCHER_START_SETTINGS_TEXT_ENABLED 0
#define LAUNCHER_START_SETTINGS_TEXT_X 92
#define LAUNCHER_START_SETTINGS_TEXT_Y 143
#define LAUNCHER_START_SETTINGS_TEXT_W 56
#define LAUNCHER_START_SETTINGS_TEXT_ALIGN 1
#endif

#ifndef LAUNCHER_START_LAST_TEXT_ALIGN
#define LAUNCHER_START_LAST_TEXT_ALIGN 1
#endif
#ifndef LAUNCHER_START_LAST_TEXT_LINES
#define LAUNCHER_START_LAST_TEXT_LINES 3
#endif

#ifndef LAUNCHER_HORZ_THUMB_X
#define LAUNCHER_HORZ_THUMB_X 60
#define LAUNCHER_HORZ_THUMB_Y 27
#define LAUNCHER_HORZ_THUMB_W 120
#define LAUNCHER_HORZ_THUMB_H 80
#define LAUNCHER_HORZ_SIDE_W 60
#define LAUNCHER_HORZ_SIDE_H 40
#define LAUNCHER_HORZ_SIDE_Y 47
#define LAUNCHER_HORZ_LEFT_X -24
#define LAUNCHER_HORZ_LEFT_Y LAUNCHER_HORZ_SIDE_Y
#define LAUNCHER_HORZ_RIGHT_X 204
#define LAUNCHER_HORZ_RIGHT_Y LAUNCHER_HORZ_SIDE_Y
#define LAUNCHER_HORZ_TITLE_X 39
#define LAUNCHER_HORZ_TITLE_Y 115
#define LAUNCHER_HORZ_TITLE_W 162
#define LAUNCHER_HORZ_TITLE_H 39
#define LAUNCHER_HORZ_HEART_X 45
#define LAUNCHER_HORZ_HEART_Y 118
#define LAUNCHER_VERT_THUMB_X 7
#define LAUNCHER_VERT_THUMB_Y 62
#define LAUNCHER_VERT_THUMB_W 84
#define LAUNCHER_VERT_THUMB_H 56
#define LAUNCHER_VERT_PREV_X 25
#define LAUNCHER_VERT_PREV_Y 24
#define LAUNCHER_VERT_PREV_W 48
#define LAUNCHER_VERT_PREV_H 32
#define LAUNCHER_VERT_NEXT_X 25
#define LAUNCHER_VERT_NEXT_Y 124
#define LAUNCHER_VERT_NEXT_W 48
#define LAUNCHER_VERT_NEXT_H 32
#define LAUNCHER_VERT_TITLE_X 92
#define LAUNCHER_VERT_TITLE_Y 62
#define LAUNCHER_VERT_TITLE_W 141
#define LAUNCHER_VERT_TITLE_H 56
#define LAUNCHER_VERT_HEART_X 97
#define LAUNCHER_VERT_HEART_Y 64
#endif

u32 list_game_total;


FM_FILE_FS pFilename_buffer[MAX_files]EWRAM_BSS;
FM_NOR_FS pNorFS[MAX_NOR]EWRAM_BSS;
FM_Folder_FS pFolder[MAX_folder]EWRAM_BSS;

FM_FILE_FS pFilename_temp;

u32 FAT_table_buffer[FAT_table_size/4]EWRAM_BSS;
u8 pReadCache [MAX_pReadCache_size]EWRAM_BSS;
static char (*Launcher_FavouritesBuffer(void))[LAUNCHER_FAVOURITE_PATH_LEN]
{
	return launcher_favourites_cache;
}
/* Keep the persistent UI PCM bounce buffer small so we do not push NOR metadata
   and other launcher state out of stable EWRAM layout. Longer clips (startup)
   fall back to the shared cache only when no launcher thumbnails are active yet. */
static s8 g_ui_audio_buffer[0x2000]EWRAM_BSS __attribute__((aligned(4)));

char p_recently_play[10][512]EWRAM_BSS;
TCHAR currentpath[MAX_path_len];//
TCHAR currentpath_temp[MAX_path_len];
TCHAR current_filename[200];

static u32 recents_view_active = 0;
static u32 recents_view_favourites = 0;
static TCHAR recents_return_path[MAX_path_len];
static u32 recents_return_show_offset = 0;
static u32 recents_return_file_select = 0;
static u32 recents_return_folder_select = 1;
static u32 recents_saved_show_offset = 0;
static u32 recents_saved_file_select = 0;
static const char recents_virtual_path[] = "/Recently Played";
static const char favourites_virtual_path[] = "/Favourites";

TCHAR plugin[100]; //pogoshell plugin

u32 p_folder_select_show_offset[100]EWRAM_BSS;
u32 p_folder_select_file_select[100]EWRAM_BSS;
u32 folder_select;
u32 gl_nor_show_offset_saved;
u32 gl_nor_file_select_saved;

u8 key_L = 0;
u8 gl_clock_dirty = 1;

u32 game_total_SD;
u32 game_total_NOR;
u32 folder_total;

u32 gl_currentpage;
u32 gl_norOffset;
u16 gl_select_lang;
u16 gl_engine_sel;


u16 gl_show_Thumbnail;
u16 gl_ingame_RTC_open_status;


u8 __attribute__((aligned(4)))GAMECODE[4];

FATFS EZcardFs;
FILINFO fileinfo;
DIR dir;
FIL gfile;
u8 dwName;

u16 gl_reset_on;
u16 gl_rts_on;
u16 gl_sleep_on;
u16 gl_cheat_on;


u16 gl_auto_save_sel;
u16 gl_ModeB_init;
u16 gl_boot_mode_pref;
u16 gl_resume_last_on;

u16 gl_led_open_sel;
u16 gl_Breathing_R;
u16 gl_Breathing_G;
u16 gl_Breathing_B;

u16 gl_toggle_reset;
u16 gl_toggle_backup;

u16 gl_SD_R;
u16 gl_SD_G;
u16 gl_SD_B;


#define LAUNCHER_COLOUR_AUTO 0xFFFF

//----------------------------------------
typedef struct
{
	const char *name;
	const unsigned char *set;
	const unsigned char *start;
	const unsigned char *help;
	const unsigned char *sd_list;
	const unsigned char *sd_horizontal;
	const unsigned char *sd_vertical;
	const unsigned char *sd_top;
	const unsigned char *set_top;
	const unsigned char *start_top;
	const unsigned char *help_top;
	const unsigned char *menu;
	const unsigned short *icon_gba;
	const unsigned short *icon_folder;
	const unsigned short *icon_chip;
	u16 selected;
	u16 text;
	u16 select_sd;
	u16 select_nor;
	u16 menu_btn;
	u16 btn_clean;
	u16 topbar_text;
	u16 heart;
	u16 title_fill;
	u16 title_stripe;
	u16 body_fill;
	u16 body_stripe;
	u16 dark_title_fill;
	u16 dark_title_stripe;
	u16 dark_body_fill;
	u16 dark_body_stripe;
} LauncherTheme;

static const LauncherTheme launcher_themes[LAUNCHER_THEME_COUNT] =
{
	LAUNCHER_THEME_TABLE_ENTRIES
};

static u16 launcher_theme_index = 0;
static u16 launcher_dark_mode = 0;
static u16 launcher_custom_theme_mode = 0;
static u16 launcher_thumbnail_style = LAUNCHER_THUMB_STYLE_TITLE;
static u16 launcher_sounds_enabled = 1;
static u32 launcher_settings_migration_pending = 0;

u16 gl_color_selected = RGB(31, 31, 31);
u16 gl_color_text = RGB(00, 00, 00);
u16 gl_color_selectBG_sd = RGB(10, 14, 17);
u16 gl_color_selectBG_nor = RGB(10, 14, 17);
u16 gl_color_MENU_btn = RGB(23, 23, 23);
u16 gl_color_topbar_text = RGB(31, 31, 31);
u16 gl_color_heart = RGB(00, 00, 00);
static u16 gl_color_title_fill = RGB(31, 31, 31);
static u16 gl_color_title_stripe = RGB(29, 29, 29);
static u16 gl_color_body_fill = RGB(31, 31, 31);
static u16 gl_color_body_stripe = RGB(28, 28, 28);
u16 gl_color_cheat_count = RGB(00, 31, 00);
u16 gl_color_cheat_black = RGB(00, 00, 00);
u16 gl_color_NORFULL = RGB(31, 00, 00);
u16 gl_color_btn_clean = RGB(10, 14, 17);
u16 SAV_info_buffer [0x200]EWRAM_BSS;
u16 launcher_side_preview_left[60*40]EWRAM_BSS;
u16 launcher_side_preview_right[60*40]EWRAM_BSS;
u16 launcher_vert_prev_scaled[48*32]EWRAM_BSS;
u16 launcher_vert_selected_scaled[84*56]EWRAM_BSS;
u16 launcher_vert_next_scaled[48*32]EWRAM_BSS;
static u8 launcher_scale84_x[84]EWRAM_BSS;
static u8 launcher_scale56_y[56]EWRAM_BSS;
static u8 launcher_scale48_x[48]EWRAM_BSS;
static u8 launcher_scale32_y[32]EWRAM_BSS;
static u8 launcher_scale80_56[56]EWRAM_BSS;
static u8 launcher_scale80_37[37]EWRAM_BSS;
static u8 launcher_scale80_32[32]EWRAM_BSS;
static u8 launcher_scale_maps_ready = 0;

#define SETTINGS_FILE "/SYSTEM/SETTINGS.TXT"
#define THEME_FILE "/SYSTEM/THEME.TXT"

static void Launcher_SaveUnifiedSettings(void);
static void Launcher_SaveMigratedSettingsIfNeeded(void);

static char *Launcher_SettingsTrim(char *text)
{
	char *end;
	if(!text)
		return text;
	while((*text == ' ') || (*text == '\t') || (*text == '\r') || (*text == '\n'))
		text++;
	end = text + strlen(text);
	while((end > text) && ((end[-1] == ' ') || (end[-1] == '\t') || (end[-1] == '\r') || (end[-1] == '\n')))
		end--;
	*end = '\0';
	return text;
}

static u32 Launcher_SettingsReadValue(const char *key, char *out, u32 out_size)
{
	FIL f;
	char line[96];
	char *equals;
	char *comment;
	char *name;
	char *value;

	if(!key || !out || (out_size == 0))
		return 0;

	out[0] = '\0';
	if(f_open(&f, SETTINGS_FILE, FA_READ) != FR_OK)
		return 0;

	while(f_gets(line, sizeof(line), &f) != NULL)
	{
		name = Launcher_SettingsTrim(line);
		if((name[0] == '\0') || (name[0] == '#') || (name[0] == ';'))
			continue;

		equals = strchr(name, '=');
		if(!equals)
			continue;

		*equals = '\0';
		value = Launcher_SettingsTrim(equals + 1);
		comment = strchr(value, '#');
		if(!comment)
			comment = strchr(value, ';');
		if(comment)
			*comment = '\0';
		value = Launcher_SettingsTrim(value);
		name = Launcher_SettingsTrim(name);
		if(!strcasecmp(name, key))
		{
			strncpy(out, value, out_size - 1);
			out[out_size - 1] = '\0';
			f_close(&f);
			return 1;
		}
	}

	f_close(&f);
	return 0;
}

static u16 Launcher_AutoThemeTextColour(u16 dark_style)
{
	return dark_style ? RGB(31, 31, 31) : RGB(0, 0, 0);
}

static const LauncherTheme *Launcher_ActiveTheme(void)
{
	if(launcher_theme_index >= LAUNCHER_THEME_COUNT)
		launcher_theme_index = 0;
	return &launcher_themes[launcher_theme_index];
}

static void Launcher_ApplyThemeColours(void)
{
	const LauncherTheme *theme = Launcher_ActiveTheme();
	u16 dark_style = launcher_dark_mode || (launcher_custom_theme_mode && LAUNCHER_CUSTOM_THEME_DARK_STYLE);
	u16 default_text = (theme->text == RGB(0, 0, 0));
	gl_color_selected = theme->selected;
	gl_color_text = (dark_style && default_text) ? RGB(31, 31, 31) : theme->text;
	gl_color_topbar_text = (theme->topbar_text == LAUNCHER_COLOUR_AUTO) ? RGB(31, 31, 31) : theme->topbar_text;
	gl_color_heart = (theme->heart == LAUNCHER_COLOUR_AUTO) ? Launcher_AutoThemeTextColour(dark_style) : theme->heart;
	gl_color_selectBG_sd = theme->select_sd;
	gl_color_selectBG_nor = theme->select_nor;
	gl_color_MENU_btn = theme->select_sd;
	gl_color_btn_clean = theme->btn_clean;
	gl_color_title_fill = dark_style ? theme->dark_title_fill : theme->title_fill;
	gl_color_title_stripe = dark_style ? theme->dark_title_stripe : theme->title_stripe;
	gl_color_body_fill = dark_style ? theme->dark_body_fill : theme->body_fill;
	gl_color_body_stripe = dark_style ? theme->dark_body_stripe : theme->body_stripe;
}

static void Launcher_SetThemeIndex(u16 index)
{
	if(index >= LAUNCHER_THEME_COUNT)
		index = 0;
	launcher_theme_index = index;
	Launcher_ApplyThemeColours();
}

static u16 Launcher_FindThemeByName(const char *name)
{
	u16 i;
	if(!name || !name[0])
		return 0;
	for(i = 0; i < LAUNCHER_THEME_COUNT; i++)
	{
		if(!strcasecmp(name, launcher_themes[i].name))
			return i;
	}
	return 0;
}

static u32 Launcher_ThumbnailSourceWidth(void)
{
	return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? 80 : 120;
}

static u32 Launcher_ThumbnailSourceHeight(void)
{
	return 80;
}

static u32 Launcher_ThumbnailReadSize(void)
{
	return LAUNCHER_THUMB_BMP_HEADER + (Launcher_ThumbnailSourceWidth() * Launcher_ThumbnailSourceHeight() * 2);
}

static const char *Launcher_ThumbnailFolder(void)
{
	return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? "/SYSTEM/IMGS2" : "/SYSTEM/IMGS";
}

static const char *Launcher_ThemeModeName(void)
{
	if(launcher_custom_theme_mode)
		return "Custom";
	return launcher_dark_mode ? "Dark" : "Light";
}

static u32 Launcher_IsThemeModeName(const char *name)
{
	if(!name)
		return 0;
	if(!strcasecmp(name, "Light") || !strcasecmp(name, "Dark"))
		return 1;
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(!strcasecmp(name, "Custom"))
		return 1;
#endif
	return 0;
}

static void Launcher_SetThemeModeName(const char *name)
{
	launcher_dark_mode = 0;
	launcher_custom_theme_mode = 0;
	if(!name)
		return;
	if(!strcasecmp(name, "Dark"))
		launcher_dark_mode = 1;
#if LAUNCHER_CUSTOM_THEME_ENABLED
	else if(!strcasecmp(name, "Custom"))
		launcher_custom_theme_mode = 1;
#endif
}

static void Launcher_LoadTheme(void)
{
	FIL f;
	char buf[32];

	launcher_theme_index = 0;
	launcher_dark_mode = 0;
	launcher_custom_theme_mode = 0;
	memset(buf, 0, sizeof(buf));
	if(Launcher_SettingsReadValue("Colour", buf, sizeof(buf)))
	{
		launcher_theme_index = Launcher_FindThemeByName(buf);
	}
	else if(Launcher_SettingsReadValue("Theme", buf, sizeof(buf)))
	{
		if(Launcher_IsThemeModeName(buf))
			Launcher_SetThemeModeName(buf);
		else
		{
			launcher_theme_index = Launcher_FindThemeByName(buf);
			launcher_settings_migration_pending = 1;
		}
	}
	else if(f_open(&f, THEME_FILE, FA_READ) == FR_OK)
	{
		if(f_gets(buf, sizeof(buf), &f) != NULL)
		{
			Trim(buf);
			if(Launcher_IsThemeModeName(buf))
				Launcher_SetThemeModeName(buf);
			else
				launcher_theme_index = Launcher_FindThemeByName(buf);
			launcher_settings_migration_pending = 1;
		}
		f_close(&f);
	}
	if(Launcher_SettingsReadValue("Theme", buf, sizeof(buf)) && Launcher_IsThemeModeName(buf))
		Launcher_SetThemeModeName(buf);
	Launcher_ApplyThemeColours();
}

static void Launcher_SaveTheme(void)
{
	f_mkdir("/SYSTEM");
	Launcher_SaveUnifiedSettings();
}

static void Launcher_CycleTheme(int dir)
{
	u16 index = launcher_theme_index;
	if(dir < 0)
		index = (index == 0) ? (LAUNCHER_THEME_COUNT - 1) : (index - 1);
	else
	{
		index++;
		if(index >= LAUNCHER_THEME_COUNT)
			index = 0;
	}
	Launcher_SetThemeIndex(index);
	Launcher_SaveTheme();
}

static void Launcher_CycleThemeMode(int dir)
{
	u16 mode = launcher_custom_theme_mode ? LAUNCHER_THEME_MODE_CUSTOM : (launcher_dark_mode ? LAUNCHER_THEME_MODE_DARK : LAUNCHER_THEME_MODE_LIGHT);
	u16 count = LAUNCHER_CUSTOM_THEME_ENABLED ? 3 : 2;
	if(dir < 0)
		mode = (mode == 0) ? (count - 1) : (mode - 1);
	else
	{
		mode++;
		if(mode >= count)
			mode = 0;
	}
	launcher_dark_mode = (mode == LAUNCHER_THEME_MODE_DARK);
	launcher_custom_theme_mode = (mode == LAUNCHER_THEME_MODE_CUSTOM);
	Launcher_ApplyThemeColours();
	Launcher_SaveTheme();
}

static const char *Launcher_ThemeName(void)
{
	return Launcher_ActiveTheme()->name;
}

const unsigned char *Launcher_ImageHELP(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_HELP_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_HELP_DARK : Launcher_ActiveTheme()->help;
}
const unsigned char *Launcher_ImageMENU(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_MENU_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_MENU_DARK : Launcher_ActiveTheme()->menu;
}
const unsigned char *Launcher_ImageSDList(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_SD_LIST_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_SD_LIST_DARK : Launcher_ActiveTheme()->sd_list;
}
const unsigned char *Launcher_ImageSDHorizontal(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_SD_HORIZONTAL_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_SD_HORIZONTAL_DARK : Launcher_ActiveTheme()->sd_horizontal;
}
const unsigned char *Launcher_ImageSDVertical(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_SD_VERTICAL_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_SD_VERTICAL_DARK : Launcher_ActiveTheme()->sd_vertical;
}
const unsigned char *Launcher_ImageSET(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_SET_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_SET_DARK : Launcher_ActiveTheme()->set;
}
const unsigned char *Launcher_ImageSTART(void) {
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
		return gImage_START_CUSTOM_THEME;
#endif
	return launcher_dark_mode ? gImage_START_DARK : Launcher_ActiveTheme()->start;
}
const unsigned short *Launcher_ImageIconGBA(void) { return Launcher_ActiveTheme()->icon_gba; }
const unsigned short *Launcher_ImageIconFolder(void) { return Launcher_ActiveTheme()->icon_folder; }
const unsigned short *Launcher_ImageIconChip(void) { return launcher_dark_mode ? (const unsigned short*)gImage_icon_chip_DARK : Launcher_ActiveTheme()->icon_chip; }

static const u16 *Launcher_GetThemeTopForBase(const u16 *base)
{
	const LauncherTheme *theme = Launcher_ActiveTheme();

	if(!base)
		return 0;
#if LAUNCHER_CUSTOM_THEME_ENABLED
	if(launcher_custom_theme_mode)
	{
		if(base == (const u16*)gImage_SD_LIST_CUSTOM_THEME)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SD_HORIZONTAL_CUSTOM_THEME)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SD_VERTICAL_CUSTOM_THEME)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SET_CUSTOM_THEME)
			return (const u16*)theme->set_top;
		if(base == (const u16*)gImage_START_CUSTOM_THEME)
			return (const u16*)theme->start_top;
		if(base == (const u16*)gImage_HELP_CUSTOM_THEME)
			return (const u16*)theme->help_top;
	}
#endif
	if(launcher_dark_mode)
	{
		if(base == (const u16*)gImage_SD_LIST_DARK)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SD_HORIZONTAL_DARK)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SD_VERTICAL_DARK)
			return (const u16*)theme->sd_top;
		if(base == (const u16*)gImage_SET_DARK)
			return (const u16*)theme->set_top;
		if(base == (const u16*)gImage_START_DARK)
			return (const u16*)theme->start_top;
		if(base == (const u16*)gImage_HELP_DARK)
			return (const u16*)theme->help_top;
	}
	if((base == (const u16*)theme->sd_list) || (base == (const u16*)theme->sd_horizontal) ||
	   (base == (const u16*)theme->sd_vertical))
		return (const u16*)theme->sd_top;
	if(base == (const u16*)theme->set)
		return (const u16*)theme->set_top;
	if(base == (const u16*)theme->start)
		return (const u16*)theme->start_top;
	if(base == (const u16*)theme->help)
		return (const u16*)theme->help_top;
	return 0;
}

static void Launcher_DrawThemeBGFull(const u16 *base)
{
	const u16 *top = Launcher_GetThemeTopForBase(base);

	DrawPic((u16*)base, 0, 0, 240, 160, 0, 0, 1);
	if(top)
		DrawPic((u16*)top, 0, 0, 240, LAUNCHER_TOP_BAR_HEIGHT, 0, 0, 1);
}

static void Launcher_ClearWithThemeBG(const u16 *base, u16 x, u16 y, u16 w, u16 h)
{
	const u16 *top = Launcher_GetThemeTopForBase(base);
	u16 y_end = y + h;

	/* The top bar can be a separate themed overlay. Do not first restore the
	   base background and then the overlay in the same scanline area; on real
	   hardware that two-pass clear can tear/flicker during heavier thumbnail
	   redraws. Split the rectangle and restore each pixel from its final source
	   exactly once. */
	if(top && (y < LAUNCHER_TOP_BAR_HEIGHT))
	{
		u16 top_h = h;

		if(y_end > LAUNCHER_TOP_BAR_HEIGHT)
			top_h = LAUNCHER_TOP_BAR_HEIGHT - y;

		if(top_h)
			ClearWithBG((u16*)top, x, y, w, top_h, 1);

		if(y_end > LAUNCHER_TOP_BAR_HEIGHT)
		{
			u16 body_y = LAUNCHER_TOP_BAR_HEIGHT;
			u16 body_h = y_end - LAUNCHER_TOP_BAR_HEIGHT;
			ClearWithBG((u16*)base, x, body_y, w, body_h, 1);
		}

		return;
	}

	ClearWithBG((u16*)base, x, y, w, h, 1);
}
//******************************************************************************
#define REG_SOUNDCNT_L_UI   (*(volatile u16*)0x04000080)
#define REG_SOUNDCNT_H_UI   (*(volatile u16*)0x04000082)
#define REG_SOUNDCNT_X_UI   (*(volatile u16*)0x04000084)
#define REG_TM0CNT_L_UI     (*(volatile u16*)0x04000100)
#define REG_TM0CNT_H_UI     (*(volatile u16*)0x04000102)
#define REG_TM1CNT_L_UI     (*(volatile u16*)0x04000104)
#define REG_TM1CNT_H_UI     (*(volatile u16*)0x04000106)
#define REG_DMA1SAD_UI      (*(volatile u32*)0x040000BC)
#define REG_DMA1DAD_UI      (*(volatile u32*)0x040000C0)
#define REG_DMA1CNT_UI      (*(volatile u32*)0x040000C4)
#define REG_FIFO_A_UI       (*(volatile u32*)0x040000A0)
#define REG_SOUNDBIAS_UI    (*(volatile u16*)0x04000088)
#define REG_IF_UI           (*(volatile u16*)0x04000202)

#define UI_DMA_ENABLE       0x80000000
#define UI_DMA_TIMING_FIFO  0x30000000
#define UI_DMA_32BIT        0x04000000
#define UI_DMA_REPEAT       0x02000000
#define UI_DMA_DST_FIXED    0x00200000
#define UI_DMA_SRC_INC      0x00000000
#define UI_DMA_COUNT_4      0x00000004

#define UI_TIMER_ENABLE     0x0080
#define UI_TIMER_FREQ_1     0x0000
#define UI_TIMER_FREQ_1024  0x0003
#define UI_TIMER_COUNT_UP   0x0004
#define UI_TIMER_IRQ        0x0040

#define UI_SOUNDCNT_A_RIGHT 0x0100
#define UI_SOUNDCNT_A_LEFT  0x0200
#define UI_SOUNDCNT_A_TIMER0 0x0000
#define UI_SOUNDCNT_A_RESET 0x0800
#define UI_MASTER_ENABLE    0x0080
#define UI_AUDIO_BUFFER_SIZE 0x2000

typedef enum
{
    UI_SFX_NONE = 0,
    UI_SFX_ACCEPT,
    UI_SFX_BACK,
    UI_SFX_MENU,
    UI_SFX_MOVE,
    UI_SFX_STARTUP,
    UI_SFX_TAB
} UI_SFX_ID;

static u16 UIAudio_TimerReload(u32 sample_rate)
{
    if(sample_rate == 0) sample_rate = 22050;
    return (u16)(65536 - (16777216 / sample_rate));
}

static u8 g_ui_audio_initialised = 0;
static u8 g_ui_audio_active = 0;
static u8 g_ui_audio_uses_shared_buffer = 0;
static u16 g_ui_audio_elapsed_start = 0;
static u16 g_ui_audio_elapsed_ticks = 0;

static void UIAudio_Stop(void);
static void UIAudio_Timer1IRQ(void);

static void UIAudio_StopForSharedBufferUse(void)
{
    if(g_ui_audio_active && g_ui_audio_uses_shared_buffer)
        UIAudio_Stop();
}

static void UIAudio_Update(void);

static void UIAudio_WaitForCurrentClip(u32 max_frames)
{
    u32 frames = 0;

    while(g_ui_audio_active && (frames < max_frames))
    {
        VBlankIntrWait();
        UIAudio_Update();
        frames++;
    }
}

static s8 *UIAudio_GetBuffer(void)
{
    return g_ui_audio_buffer;
}

static s8 *UIAudio_GetSharedLongClipBuffer(void)
{
    return (s8*)pReadCache;
}

static u32 UIAudio_PrepareBuffer(const signed char *data, u32 len, u32 allow_shared_long_clip, s8 **out_buffer)
{
    u32 copy_len;
    u32 padded_len;
    u32 buffer_size;
    s8 *buffer;

    if(out_buffer)
        *out_buffer = 0;

    if(!data || !len)
        return 0;

    if(allow_shared_long_clip && (len > UI_AUDIO_BUFFER_SIZE))
    {
        buffer = UIAudio_GetSharedLongClipBuffer();
        buffer_size = len;
    }
    else
    {
        buffer = UIAudio_GetBuffer();
        buffer_size = UI_AUDIO_BUFFER_SIZE;
    }

    copy_len = len;
    if(copy_len > buffer_size)
        copy_len = buffer_size;

    padded_len = (copy_len + 15) & ~15;
    if(padded_len > buffer_size)
        padded_len = buffer_size;

    /* Clear the full active bounce buffer because FIFO DMA repeats until we stop it.
       Leaving old sample bytes after the new clip can cause a burst of stale audio
       or alter the perceived pitch/texture at the tail of short UI sounds. */
    memset(buffer, 0, buffer_size);
    memcpy(buffer, data, copy_len);

    if(out_buffer)
        *out_buffer = buffer;

    return padded_len;
}


static void UIAudio_Stop(void)
{
    REG_DMA1CNT_UI = 0;
    REG_TM0CNT_H_UI = 0;
    REG_TM1CNT_H_UI = 0;
    REG_DMA1SAD_UI = 0;
    REG_DMA1DAD_UI = 0;
    REG_SOUNDCNT_H_UI |= UI_SOUNDCNT_A_RESET;
    delay(8);
    g_ui_audio_active = 0;
    g_ui_audio_uses_shared_buffer = 0;
    g_ui_audio_elapsed_start = 0;
    g_ui_audio_elapsed_ticks = 0;
}

static void UIAudio_StopFromTimer(void)
{
    REG_DMA1CNT_UI = 0;
    REG_TM0CNT_H_UI = 0;
    REG_TM1CNT_H_UI = 0;
    REG_DMA1SAD_UI = 0;
    REG_DMA1DAD_UI = 0;
    REG_SOUNDCNT_H_UI |= UI_SOUNDCNT_A_RESET;
    g_ui_audio_active = 0;
    g_ui_audio_uses_shared_buffer = 0;
    g_ui_audio_elapsed_start = 0;
    g_ui_audio_elapsed_ticks = 0;
}

static void UIAudio_Timer1IRQ(void)
{
    UIAudio_StopFromTimer();
}

static void UIAudio_Update(void)
{
    (void)g_ui_audio_active;
}

void UIAudio_Init(void)
{
    REG_DMA1CNT_UI = 0;
    REG_TM0CNT_H_UI = 0;
    REG_TM1CNT_H_UI = 0;
    REG_SOUNDCNT_X_UI = 0;
    REG_SOUNDCNT_L_UI = 0;
    REG_SOUNDCNT_H_UI = 0;
    REG_SOUNDBIAS_UI = 0x0200;
    delay(256);
    REG_SOUNDCNT_X_UI = UI_MASTER_ENABLE;
    delay(64);
    REG_SOUNDCNT_H_UI =
        UI_SOUNDCNT_A_RIGHT |
        UI_SOUNDCNT_A_LEFT  |
        UI_SOUNDCNT_A_TIMER0 |
        UI_SOUNDCNT_A_RESET |
        (1 << 2);  // 100% volume

    g_ui_audio_active = 0;
    g_ui_audio_uses_shared_buffer = 0;
    g_ui_audio_elapsed_start = 0;
    g_ui_audio_elapsed_ticks = 0;

    irqSet(IRQ_TIMER1, UIAudio_Timer1IRQ);
    irqEnable(IRQ_TIMER1);

    g_ui_audio_initialised = 1;
}

static void UIAudio_PlayRaw(const signed char *data, u32 len, u32 sample_rate, u32 allow_shared_long_clip)
{
    u32 sample_count;
    u32 copy_len;
    u32 padded_len;
    s8 *play_buffer;

    if(!g_ui_audio_initialised || !data || !len)
        return;

    /* Stop the previous FIFO stream before touching the bounce buffer.
       Otherwise DMA can read the buffer while it is being memset/memcpy'd,
       which is the classic source of random clipped UI sounds. */
    UIAudio_Update();
    if(g_ui_audio_active)
        UIAudio_Stop();

    copy_len = UIAudio_PrepareBuffer(data, len, allow_shared_long_clip, &play_buffer);
    if(copy_len == 0 || !play_buffer)
        return;

    padded_len = (copy_len + 15) & ~15;
    sample_count = padded_len;
    if(sample_count == 0)
        return;
    if(sample_count > 65535)
        sample_count = 65535;

    REG_SOUNDBIAS_UI = 0x0200;
    delay(16);

    REG_DMA1SAD_UI = (u32)play_buffer;
    REG_DMA1DAD_UI = (u32)&REG_FIFO_A_UI;
    REG_DMA1CNT_UI = UI_DMA_COUNT_4 | UI_DMA_ENABLE | UI_DMA_TIMING_FIFO | UI_DMA_32BIT | UI_DMA_REPEAT | UI_DMA_DST_FIXED | UI_DMA_SRC_INC;

    g_ui_audio_elapsed_ticks = (u16)((((unsigned long long)sample_count) * 16384ULL + (sample_rate - 1)) / sample_rate + 1);
    if(g_ui_audio_elapsed_ticks == 0)
        g_ui_audio_elapsed_ticks = 1;
    REG_TM0CNT_L_UI = UIAudio_TimerReload(sample_rate);
    REG_TM1CNT_H_UI = 0;
    REG_TM1CNT_L_UI = (u16)(0x10000 - g_ui_audio_elapsed_ticks);
    REG_IF_UI = IRQ_TIMER1;
    REG_TM1CNT_H_UI = UI_TIMER_ENABLE | UI_TIMER_FREQ_1024 | UI_TIMER_IRQ;
    REG_TM0CNT_H_UI = UI_TIMER_ENABLE | UI_TIMER_FREQ_1;
    g_ui_audio_elapsed_start = REG_TM1CNT_L_UI;
    g_ui_audio_active = 1;
    g_ui_audio_uses_shared_buffer = allow_shared_long_clip && (play_buffer == UIAudio_GetSharedLongClipBuffer());
}

static void UIAudio_PlaySfx(UI_SFX_ID id)
{
    if((id != UI_SFX_STARTUP) && !launcher_sounds_enabled)
    {
        UIAudio_StopForSharedBufferUse();
        return;
    }

    switch(id)
    {
        case UI_SFX_ACCEPT: UIAudio_PlayRaw((const signed char*)accept_raw, accept_raw_len, 22050, 0); break;
        case UI_SFX_BACK: UIAudio_PlayRaw((const signed char*)back_raw, back_raw_len, 22050, 0); break;
        case UI_SFX_MENU: UIAudio_PlayRaw((const signed char*)menu_raw, menu_raw_len, 22050, 0); break;
        case UI_SFX_MOVE: UIAudio_PlayRaw((const signed char*)move_raw, move_raw_len, 22050, 0); break;
        case UI_SFX_STARTUP: UIAudio_PlayRaw((const signed char*)startup_raw, startup_raw_len, 22050, 1); break;
        case UI_SFX_TAB: UIAudio_PlayRaw((const signed char*)tab_raw, tab_raw_len, 22050, 0); break;
        default: break;
    }
}

void UIAudio_PlayStartup(void)
{
#if LAUNCHER_BOOT_SOUND_ENABLED
    UIAudio_PlaySfx(UI_SFX_STARTUP);
#endif
}

static u32 UIAudio_GetStartupSplashFrames(void)
{
    return 60;
}

static void UIAudio_HandleKeysEx(u16 keysdown, u16 keysrepeat, u32 allow_tab, u32 allow_move)
{
    UIAudio_Update();
    if(keysdown & KEY_A)
        UIAudio_PlaySfx(UI_SFX_ACCEPT);
    else if(keysdown & (KEY_SELECT | KEY_START))
        UIAudio_PlaySfx(UI_SFX_MENU);
    else if(allow_tab && (keysdown & (KEY_L | KEY_R)))
        UIAudio_PlaySfx(UI_SFX_TAB);
    else if(allow_move && ((keysdown | keysrepeat) & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)))
        UIAudio_PlaySfx(UI_SFX_MOVE);
}

void UIAudio_HandleKeys(u16 keysdown, u16 keysrepeat)
{
    UIAudio_HandleKeysEx(keysdown, keysrepeat, 1, 1);
}

static void UIAudio_PlayAccept(void)
{
    UIAudio_PlaySfx(UI_SFX_ACCEPT);
}

static void UIAudio_PlayBack(void)
{
    UIAudio_PlaySfx(UI_SFX_BACK);
}

void UIAudio_PlayMove(void)
{
    UIAudio_PlaySfx(UI_SFX_MOVE);
}

void UIAudio_PlayAcceptExport(void)
{
    UIAudio_PlayAccept();
}

void UIAudio_PlayBackExport(void)
{
    UIAudio_PlayBack();
}

void UIAudio_UpdateExport(void)
{
    UIAudio_Update();
}

void UIAudio_WaitForCurrentClipExport(u32 max_frames)
{
    UIAudio_WaitForCurrentClip(max_frames);
}

void UIAudio_CutOffTrailingClipExport(void)
{
    UIAudio_Update();
    UIAudio_Stop();
}

static void UIAudio_CutOffTrailingClip(void)
{
    UIAudio_CutOffTrailingClipExport();
}

void delay(u32 R0)
{
  int volatile i;

  for ( i = R0; i; --i );
  return;
}
//---------------------------------------------------------------------------------
void wait_btn()
{
	while(1)
	{
		VBlankIntrWait();
		UIAudio_Update();
		scanKeys();
		u16 keys = keysUp();
		if (keys & KEY_B) {
			UIAudio_CutOffTrailingClip();
			break;
		}
	}
	//while(*(vu16*)0x04000130 == 0x3FF );
	//while(*(vu16*)0x04000130 != 0x3FF );
}
//---------------------------------------------------------------------------------
static void Launcher_PrepareSettingsFlashWrite(void)
{
	UIAudio_WaitForCurrentClip(20);
	UIAudio_CutOffTrailingClip();
}
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
u32 Copy_file(const char* src, const char* dst)
{
	UIAudio_StopForSharedBufferUse();
	UINT read_ret;
	UINT write_ret;
	u32 filesize;
	u32 res;
	u32 blocknum;
	u32 total_read = 0;
	u32 total_written = 0;
	FIL dst_file;

	res = f_open(&gfile, src, FA_READ);
	if (res == FR_OK)
	{
		res = f_open(&dst_file, dst, FA_WRITE | FA_CREATE_ALWAYS);
		if (res == FR_OK)
		{
			filesize = f_size(&gfile);
			res = f_lseek(&gfile, 0x0000);

			for (blocknum = 0x0000; (res == FR_OK) && (blocknum < filesize); blocknum += 0x20000)
			{
				u32 chunk = filesize - blocknum;
				if (chunk > 0x20000)
					chunk = 0x20000;

				res = f_read(&gfile, pReadCache, chunk, &read_ret);
				if ((res != FR_OK) || (read_ret != chunk))
					break;

				total_read += read_ret;
				res = f_write(&dst_file, pReadCache, read_ret, &write_ret);
				if (write_ret != read_ret)
					break;

				total_written += write_ret;
			}
			if (res == FR_OK)
				res = f_sync(&dst_file);
			f_close(&dst_file);

			if ((res == FR_OK) && (total_read == filesize) && (total_written == filesize))
			{
				f_close(&gfile);
				return 1;
			}

			f_unlink(dst);
		}
		f_close(&gfile);
	}

	return 0;
}
u32 Is_bin_file(const TCHAR *name)
{
    const TCHAR *ext = strrchr(name, '.');
    if (!ext) return 0;
    return !strcasecmp(ext, ".bin");
}

u32 Is_themes_folder(const TCHAR *path)
{
    return !strcmp(path, "/THEMES");
}

u32 Get_file_size_path(const TCHAR *path, u32 *out_size)
{
    FIL file;
    FRESULT res = f_open(&file, path, FA_READ);
    if (res != FR_OK) return 0;
    *out_size = f_size(&file);
    f_close(&file);
    return 1;
}

u32 Stage_kernel_update(const TCHAR *src_name)
{
    TCHAR src_path[MAX_path_len];
    const TCHAR *tmp_path = "/ezkernelnew.tmp";
    const TCHAR *dst_path = "/ezkernelnew.bin";
    u32 src_size = 0;
    u32 tmp_size = 0;

    memset(src_path, 0, sizeof(src_path));

    if (!strcmp(currentpath, "/"))
        snprintf(src_path, sizeof(src_path), "/%s", src_name);
    else
        snprintf(src_path, sizeof(src_path), "%s/%s", currentpath, src_name);

    if (!Get_file_size_path(src_path, &src_size))
        return 0;

    f_unlink(tmp_path);

    if (!Copy_file(src_path, tmp_path))
    {
        f_unlink(tmp_path);
        return 0;
    }

    if (!Get_file_size_path(tmp_path, &tmp_size))
    {
        f_unlink(tmp_path);
        return 0;
    }

    if (src_size != tmp_size)
    {
        f_unlink(tmp_path);
        return 0;
    }

    f_unlink(dst_path);

    if (f_rename(tmp_path, dst_path) != FR_OK)
    {
        f_unlink(tmp_path);
        return 0;
    }

    return 1;
}
//---------------------------------------------------------------------------------
static const u16 *Launcher_GetFileIcon(const TCHAR *pfilename);
static void Launcher_ClearTextBodyBackground(void);
static void Launcher_ClearTextBodyBackgroundRegion(int x, int y, int w, int h);

void Get_file_size(u32 num,char*str)
{
		u32 filesize;

		filesize = (pFilename_buffer[num].filesize) >>20 ;//M
		sprintf(str,"%4luM",filesize);
		if(filesize ==0)
		{
			filesize = (pFilename_buffer[num].filesize) /1024 ;//K
			sprintf(str,"%4luK",filesize);
		}
		if(filesize ==0)
		{
			filesize = pFilename_buffer[num].filesize  ;
			sprintf(str,"%4luB",filesize);
		}
}
//---------------------------------------------------------------------------------
void Show_ICON_filename_SD(u32 show_offset,u32 file_select,u32 haveThumbnail)
{
	u32 need_show_game;
	u32 need_show_folder;
	u32 line;
	u32 char_num;

	if(show_offset >= folder_total)
	{
		need_show_folder = 0;
	}
	else
	{
		need_show_folder = folder_total-show_offset;
		if(need_show_folder > 10)
			need_show_folder = 10;
	}
	need_show_game = 10-need_show_folder;
	if(need_show_game > game_total_SD)
		need_show_game = game_total_SD;


	u32 y_offset= 20;

	for(line=0;line<need_show_folder;line++)
	{
		u16 row_color = (line == file_select) ? LAUNCHER_SELECTED_TEXT : gl_color_text;
		if(haveThumbnail)
		{
			if(line>3){
				char_num = 17;
			}
			else{
				char_num = 32;
			}
		}
		else{
			char_num = 32;
		}
		
		if(line== file_select)
		{			
			Clear(17,20 + file_select*14,(char_num == 17)?(17*6+1):(240-17),13,gl_color_selectBG_sd,1);
		}

		DrawPic((u16*)(gImage_icon_folder/*gImage_icons+0*16*14*2*/),
			0,
			y_offset + line*14,
			16,
			14,
			1,
			0x0000,
			1);

		DrawHZText12(pFolder[show_offset+line].filename, char_num, 1+16, y_offset + line*14, row_color,1);
					
		if((haveThumbnail==1)&&(line>3))
		{}
		else
		{
			char msg[20];
			sprintf(msg,"%s","DIR");
			DrawHZText12(msg,0,221,y_offset + line*14, row_color,1);
		}
	}


	u32 offset=0;
	TCHAR *pfilename;
	if(show_offset >= folder_total)
		offset = show_offset - folder_total;
									
	for(line=need_show_folder;line < need_show_folder+need_show_game;line++)
	{
		u16 row_color = (line == file_select) ? LAUNCHER_SELECTED_TEXT : gl_color_text;
		if(haveThumbnail)
		{
			if(line>3){
				char_num = 17;
			}
			else{
				char_num = 32;
			}
		}
		else{
			char_num = 32;
		}
		
		if(line== file_select)
		{
			Clear(17,20 + file_select*14,(char_num == 17)?(17*6+1):(240-17),13,gl_color_selectBG_sd,1);
		}

		u32 showy = y_offset +(line)*14;
		pfilename = pFilename_buffer[offset+line-need_show_folder].filename;
		u16* icon = (u16*)Launcher_GetFileIcon(pfilename);
		DrawPic(icon,
			0,
			showy,
			16,
			14,
			1,
			0x0000,
			1);

			{
			char fav_name[256];
			Launcher_GetSDDisplayNameWithFavourite(offset+line-need_show_folder, fav_name, sizeof(fav_name));
			DrawHZText12(fav_name, char_num, 1+16, showy, row_color,1);
		}
		if(recents_view_active)
		{}
		else if((haveThumbnail==1)&&(line>3))
		{}
		else
		{		
			char msg[20];
			Get_file_size(offset+line-need_show_folder,msg);
			DrawHZText12(msg,0,208,showy, row_color,1);
		}
	}
}
//---------------------------------------------------------------------------------
void Backup_savefile(const char* filename)
{
	const char* backup_dir = "/SYSTEM/BACKUP/SAVER";
	TCHAR temp_filename[MAX_path_len] = { 0 };
	TCHAR temp_filename_dst[MAX_path_len] = { 0 };
	u32 temp_filename_length;
	int written;

	written = snprintf(temp_filename, sizeof(temp_filename), "%s/%s", backup_dir, filename);
	if ((written < 0) || (written >= (int)sizeof(temp_filename) - 1))
		return;
	temp_filename_length = strlen(temp_filename);
	if (temp_filename_length + 1 >= sizeof(temp_filename))
		return;

	f_mkdir("/SYSTEM/BACKUP");
	f_mkdir("/SYSTEM/BACKUP/SAVER");
	strncpy(temp_filename_dst, temp_filename, sizeof(temp_filename_dst));
	temp_filename_dst[sizeof(temp_filename_dst) - 1] = 0;

	for (s8 i = 3; i >= 0; --i)
	{
		temp_filename[temp_filename_length] = '0' + i;
		temp_filename[temp_filename_length + 1] = 0;
		temp_filename_dst[temp_filename_length] = '0' + i + 1;
		temp_filename_dst[temp_filename_length + 1] = 0;

		f_unlink(temp_filename_dst);
		f_rename(temp_filename, temp_filename_dst);
	}

	temp_filename[temp_filename_length] = '0';
	temp_filename[temp_filename_length + 1] = 0;
	if (!Copy_file(filename, temp_filename))
		f_unlink(temp_filename);
}
//---------------------------------------------------------------------------------
static u32 Launcher_IsNORPage(void);
static const u16 *Launcher_GetBGImage(void);

void IWRAM_CODE Refresh_filename(u32 show_offset,u32 file_select,u32 updown,u32 haveThumbnail)
{
	u32 need_show_game;
	u32 need_show_folder;
	char msg[20];
	u32 y_offset= 20;	
		
	u32 char_num1;
	u32 char_num2;
	u32 clean_len1;
	u32 clean_len2;
	
	if(show_offset >= folder_total)
	{
		need_show_folder = 0;
	}
	else
	{
		need_show_folder = folder_total-show_offset;
		if(need_show_folder > 10)
			need_show_folder = 10;
	}
	need_show_game = 10-need_show_folder;
	if(need_show_game > game_total_SD)
		need_show_game = game_total_SD;

	u32 offset=0;
	if(show_offset >= folder_total)
		offset = show_offset - folder_total;

	u16 name_color1;
	u16 name_color2;
	//u16 name_color2;
	u32 xx1;
	u32 xx2;
	u32 showy1;
	u32 showy2;

	if(haveThumbnail)
	{
		switch(file_select)
		{
			case 0:
			case 1:
			case 2:
				char_num1 = 32;
				char_num2 = 32;
				clean_len1 = 240-17;
				clean_len2 = 240-17;
				break;
			case 3:
				if(updown ==3){
					char_num1 = 32;
					char_num2 = 17;
					clean_len1 = 240-17;
					clean_len2 = 17*6+1;
				}
				else{
					char_num1 = 32;
					char_num2 = 32;
					clean_len1 = 240-17;
					clean_len2 = 240-17;
				}
				break;	
			case 4:
				if(updown ==2){
					char_num1 = 32;
					char_num2 = 17;
					clean_len1 = 240-17;
					clean_len2 = 17*6+1;
				}
				else{
					char_num1 = 17;
					char_num2 = 17;
					clean_len1 = 17*6+1;
					clean_len2 = 17*6+1;
				}	
				break;
			case 5:
				if(updown ==2){
					char_num1 = 17;
					char_num2 = 17;
					clean_len1 = 240-17;
					clean_len2 = 17*6+1;
				}
				else{
					char_num1 = 17;
					char_num2 = 17;
					clean_len1 = 17*6+1;
					clean_len2 = 17*6+1;
				}	
				break;
			default:
				char_num1 = 17;
				char_num2 = 17;
				clean_len1 = 17*6+1;
				clean_len2 = 17*6+1;	
				break;
		}
	}
	else{
		char_num1 = 32;
		char_num2 = 32;
		clean_len1 = 240-17;
		clean_len2 = 240-17;
	}
		

	name_color1 = gl_color_text;
	name_color2 = gl_color_text;
	if(updown ==2) //down
	{
		xx1 = file_select-1;
		xx2 = file_select;
		showy1 = y_offset +(file_select-1)*14;
		showy2 = y_offset +(file_select)*14;
		Launcher_ClearTextBodyBackgroundRegion(17, 20 + xx1*14, clean_len1, 13);
		Clear(17,20 + xx2*14,clean_len2,13,gl_color_selectBG_sd,1);
		name_color2 = LAUNCHER_SELECTED_TEXT;
	}
	else// if(updown ==3)//up
	{
		xx1 = file_select;
		xx2 = file_select+1;
		showy1 = y_offset +(file_select)*14;
		showy2 = y_offset +(file_select+1)*14;	
		Clear(17,20 + xx1*14,clean_len1,13,gl_color_selectBG_sd,1);	
		Launcher_ClearTextBodyBackgroundRegion(17, 20 + xx2*14, clean_len2, 13);	
		name_color1 = LAUNCHER_SELECTED_TEXT;
	}

	if((file_select == (need_show_folder-1)) && (updown ==3))
	{
		DrawHZText12(pFolder[show_offset+xx1].filename, char_num1, 1+16, showy1, name_color1,1);
		{ char fav_name2[256]; Launcher_GetSDDisplayNameWithFavourite(0, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }
		
		if(char_num1==32){
			sprintf(msg,"%s","DIR");
			DrawHZText12(msg,0,221,showy1, name_color1,1);
		}
		if(char_num2==32){
			Get_file_size(0,msg);
			DrawHZText12(msg,0,208,showy2, name_color2,1);			
		}
	}
	else if(file_select < need_show_folder)
	{
		DrawHZText12(pFolder[show_offset+xx1].filename, char_num1, 1+16, showy1, name_color1,1);
		DrawHZText12(pFolder[show_offset+xx2].filename, char_num2, 1+16, showy2, name_color2,1);

		sprintf(msg,"%s","DIR");
		if(char_num1==32){
			DrawHZText12(msg,0,221,showy1, name_color1,1);			
		}
		if(char_num2==32){
			DrawHZText12(msg,0,221,showy2, name_color2,1);
		}
	}
	else if((file_select == need_show_folder)&& (updown ==2))
	{
		DrawHZText12(pFolder[show_offset+xx1].filename,char_num1, 1+16, showy1, name_color1,1);
		{ char fav_name2[256]; Launcher_GetSDDisplayNameWithFavourite(0, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }
		
		if(char_num1==32){
			sprintf(msg,"%s","DIR");			
			DrawHZText12(msg,0,221,showy1, name_color1,1);			
		}
		if(!recents_view_active && char_num2==32){
			Get_file_size(0,msg);
			DrawHZText12(msg,0,208,showy2, name_color2,1);
		}
	}
	else
	{
		{ char fav_name1[256]; Launcher_GetSDDisplayNameWithFavourite(offset+xx1-need_show_folder, fav_name1, sizeof(fav_name1)); DrawHZText12(fav_name1, char_num1, 1+16, showy1, name_color1,1); }
		{ char fav_name2[256]; Launcher_GetSDDisplayNameWithFavourite(offset+xx2-need_show_folder, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }

		if(!recents_view_active && char_num1==32){
			Get_file_size(offset+xx1-need_show_folder,msg);
			DrawHZText12(msg,0,208,showy1, name_color1,1);		
		}
		if(!recents_view_active && char_num2==32){
			Get_file_size(offset+xx2-need_show_folder,msg);
			DrawHZText12(msg,0,208,showy2, name_color2,1);
		}
	}
}
static void Launcher_CleanTitle(const TCHAR *src, char *dst, u32 dst_size);
static void Launcher_GetDisplayTitleBounded(const TCHAR *src, char *dst, u32 dst_size)
{
	if(dst_size == 0)
		return;
	dst[0] = '\0';
	if(!src)
		return;

	Launcher_CleanTitle(src, dst, dst_size);
	if(dst[0] == '\0')
	{
		strncpy(dst, src, dst_size - 1);
		dst[dst_size - 1] = '\0';
	}
}

//---------------------------------------------------------------------------------
void Show_ICON_filename_NOR(u32 show_offset,u32 file_select)
{
	int need_show;
	int line;
	char msg[20];
	char title[128];
	u32 y_offset= 20;	
	u32 char_num=32;
	
	if(game_total_NOR<10)
		need_show = game_total_NOR;
	else
		need_show = 10;

	for(line=0;line<need_show;line++)
	{
		u16 row_color = (line == file_select) ? LAUNCHER_SELECTED_TEXT : gl_color_text;
		if(line== file_select){
			Clear(17,20 + file_select*14,240-17,13,gl_color_selectBG_nor,1);
		}		

		DrawPic((u16*)gImage_icon_nor/*(gImage_icons+2*16*14*2)*/,
			0,
			y_offset + line*14,
			16,
			14,
			1,
			0x0000,
			1);

		DrawHZText12(pNorFS[show_offset+line].filename, char_num, 1+16, y_offset + line*14, row_color,1);	
		sprintf(msg,"%4luM",pNorFS[show_offset+line].filesize >>20 );
		DrawHZText12(msg,0,208,y_offset + line*14, row_color,1);

	}
}
//---------------------------------------------------------------------------------
void Refresh_filename_NOR(u32 show_offset,u32 file_select,u32 updown)
{
	char msg[20];
	char title1[128];
	char title2[128];
	u16 name_color1;
	u16 name_color2;
	u32 xx1;
	u32 xx2;
	u32 showy1;
	u32 showy2;
	u32 y_offset= 20;
	u32 char_num;
	u32 clean_len;
	
	char_num = 32;
	clean_len = 240-17;
	
	name_color1 = gl_color_text;
	name_color2 = gl_color_text;

	if(updown ==2) //down
	{
		xx1 = file_select-1;
		xx2 = file_select;
		showy1 = y_offset +(file_select-1)*14;
		showy2 = y_offset +(file_select)*14;
		Launcher_ClearTextBodyBackgroundRegion(17, 20 + xx1*14, clean_len, 13);
		Clear(17,20 + xx2*14,clean_len,13,gl_color_selectBG_nor,1);
		name_color2 = LAUNCHER_SELECTED_TEXT;
	}
	else //if(updown ==3)//up
	{
		xx1 = file_select;
		xx2 = file_select+1;
		showy1 = y_offset +(file_select)*14;
		showy2 = y_offset +(file_select+1)*14;
		Clear(17,20 + xx1*14,clean_len,13,gl_color_selectBG_nor,1);
		Launcher_ClearTextBodyBackgroundRegion(17, 20 + xx2*14, clean_len, 13);
		name_color1 = LAUNCHER_SELECTED_TEXT;
	}

	DrawHZText12(pNorFS[show_offset+xx1].filename, char_num, 1+16, showy1, name_color1,1);
	DrawHZText12(pNorFS[show_offset+xx2].filename, char_num, 1+16, showy2, name_color2,1);

	sprintf(msg,"%4luM",(pNorFS[show_offset+xx1].filesize) >>20 );
	DrawHZText12(msg,0,208,showy1, name_color1,1);
	sprintf(msg,"%4luM",(pNorFS[show_offset+xx2].filesize) >>20 );
	DrawHZText12(msg,0,208,showy2, name_color2,1);

}
//---------------------------------------------------------------------------------
void Show_game_num(u32 count,u32 list)
{
	char msg[20];
	const u16 *bg = Launcher_GetBGImage();
	if(list==0){
		if(game_total_SD+folder_total==0)
			count = 0;
		sprintf(msg,"[%03lu/%03lu]",count,game_total_SD+folder_total);
	}
	else{
		if(game_total_NOR==0)
			count = 0;
		sprintf(msg,"[%03lu/%03lu]",count,game_total_NOR);
	}
	Launcher_ClearWithThemeBG(bg, 185, 3, 54, 13);
	DrawHZText12(msg,0,185,3, gl_color_topbar_text,1);
}
//---------------------------------------------------------------------------------
void Filename_loop(u32 shift,u32 show_offset,u32 file_select,u32 haveThumbnail)
{
	if(haveThumbnail)
		return;
	
	u32 need_show_folder;
	//u32 line;
	u32 char_num;	
	u32 y_offset= 20;	
	int namelen;
	static u32 orgtt = 123455;
	u32 timeout = 20;
	//u8 dwName=0;	
	char msg[128];
	char temp_filename[100];
	
	if(shift > timeout)
	{
		if(show_offset >= folder_total)
		{
			need_show_folder = 0;
		}
		else
		{
			need_show_folder = folder_total-show_offset;
			if(need_show_folder > 10)
				need_show_folder = 10;
		}

		if(haveThumbnail)
		{
			if(file_select>3){
				char_num = 17;
			}
			else{
				char_num = 33;
			}
		}
		else{
			char_num = 33;
		}

		
		u32 offset=0;
		if(show_offset >= folder_total)
			offset = show_offset - folder_total;

		if(file_select < need_show_folder)
		{
			strncpy(temp_filename,pFolder[show_offset+file_select].filename , 100 );
		
		}
		else
		{
			Launcher_GetSDDisplayNameWithFavourite(offset+file_select-need_show_folder, temp_filename, sizeof(temp_filename));
		}
		
		namelen = strlen(temp_filename);
		if(namelen >(char_num-1) ) 
		{
			u32  tt = ((shift-timeout)/8)% (namelen);
			if(orgtt!= tt )
			{	
				orgtt = tt ;
				sprintf(msg,"%s   ",temp_filename + tt);
				strncpy(msg+strlen(msg) ,temp_filename , 128 - strlen(msg) );
				if(temp_filename[tt] > 0x80)
				{						
					if(dwName)
					{
						msg[0] = 0x20;
						dwName = 0;
					}
					else
						dwName = 1;
				}
				else
					dwName = 0;
					
				Clear(17,20 + file_select*14,(char_num)*6,13,gl_color_selectBG_sd,1);	
				DrawHZText12(msg, char_num-1, 1+16, y_offset + file_select*14, LAUNCHER_SELECTED_TEXT,1);
			}	
		}
	}		
}
//---------------------------------------------------------------------------------
void Show_MENU_btn()
{
	char msg[30];
	Clear(60,118-1,55,14,gl_color_selectBG_sd,1);
	Clear(125,118-1,55,14,gl_color_selectBG_sd,1);
	sprintf(msg,"%s",gl_menu_btn);
	DrawHZText12(msg,0,60,118, LAUNCHER_SELECTED_TEXT,1);
}
//---------------------------------------------------------------------------------
static void Launcher_RestoreLegacyMenuRow(u32 y, u32 h)
{
	const u32 popup_x = 36;
	const u32 popup_y = 25;
	const u32 popup_w = 168;
	const u32 row_x = 42;
	const u32 row_w = 156;
	u32 src_x = row_x - popup_x;
	u32 src_y = y - popup_y;

	Launcher_DrawPicClipStride(((u16*)gImage_MENU) + src_y * popup_w + src_x, popup_w, row_x, y, row_w, h);
}
//---------------------------------------------------------------------------------
static void Show_MENU_Row(u32 line, u32 menu_select, PAGE_NUM page, u32 havecht, u32 Save_num)
{
	u32 y_offset= 30;
	u16 name_color;
	char msg[30];

	u32 row_y = y_offset + line*14;
	Launcher_RestoreLegacyMenuRow(row_y, 13);
	if(line== menu_select){
		Clear(42, row_y, 156, 13, gl_color_selectBG_sd, 1);
		name_color = LAUNCHER_SELECTED_TEXT;
	}
	else if(line == 1){
			if((gl_reset_on |  gl_rts_on| gl_sleep_on| gl_cheat_on) == 0)	{
				name_color = gl_color_MENU_btn;
			}	
			else {
				name_color = gl_color_text;	
			}
	}
	else if(line == 5){
		if(havecht==1 && gl_cheat_on==0)
		{
			name_color = gl_color_MENU_btn;
		}
		else if(gl_cheat_count)
		{
			name_color = gl_color_cheat_count;
		}
		else{
			name_color = gl_color_text;
		}			
	}				
	else{
		name_color = gl_color_text;
	}

	if(page==NOR_list)
		DrawHZText12(gl_nor_op[line], 32, 47, row_y, name_color,1);
	else
	{
		if(line == 5)//cheat
		{
			sprintf(msg,"%s(%ld)",gl_rom_menu[line],gl_cheat_count);
			DrawHZText12(msg, 32, 47, row_y, name_color,1);
		}
		else{
			DrawHZText12(gl_rom_menu[line], 32, 47, row_y, name_color,1);
						
			if(line == 4)//save tpye
			{				
				switch(Save_num)
				{
				case 1:
					sprintf(msg, "%s", ": SRAM 32kb");//0x11
					break;
				case 2:
					sprintf(msg, "%s", ": EEPROM 8kb");//0x22
					break;
				case 3:
					sprintf(msg, "%s", ": EEPROM 512b");//0x23
					break;
				case 4:
					sprintf(msg, "%s", ": Flash 64kb");//0x32
					break;
				case 5:
					sprintf(msg, "%s", ": Flash 128kb");//0x31
					break;
				case 0:
				default:
					sprintf(msg, "%s", ": Auto Detect");
					break;		
				}
				DrawHZText12(msg, 32, 60+54, row_y, name_color,1);					
			}
		}
	}
}
//---------------------------------------------------------------------------------
void Show_MENU(u32 menu_select,PAGE_NUM page,u32 havecht,u32 Save_num,u32 is_menu,u32 firstgame)
{
	int line;
	
	u32 linemax;// = (page==NOR_list)?3:(5+havecht);
	if(page==NOR_list){
		if(firstgame){
			linemax = 5;//load and save sav file
		}
		else {
			linemax = 3;
		}
	}
	else{
		linemax = 5 + havecht;
	}
		
	
	
	if(is_menu){
		linemax = 1;
	}
		
	for(line=0;line<linemax;line++)
	{		
		Show_MENU_Row(line, menu_select, page, havecht, Save_num);
	}
}
//------------------------------------------------------------------
static const u16 *Launcher_GetFileIcon(const TCHAR *pfilename);
static u32 Launcher_IsNORPage(void);
static const u16 *Launcher_GetBGImage(void);
static u32 Launcher_GetTotalEntries(void);
static void Recent_GetDisplayName(const char *fullpath, char *dst, u32 dst_size)
{
	const char *name = fullpath;
	char temp[256];
	u32 i;
	u32 j = 0;
	u32 paren = 0;
	u32 bracket = 0;
	char *dot;

	if(dst_size == 0)
		return;

	dst[0] = '\0';
	if(!fullpath || !fullpath[0])
		return;

	for(i = 0; fullpath[i] != '\0'; i++)
	{
		if((fullpath[i] == '/') || (fullpath[i] == '\\'))
			name = fullpath + i + 1;
	}

	memset(temp, 0, sizeof(temp));
	strncpy(temp, name, sizeof(temp) - 1);
	dot = strrchr(temp, '.');
	if(dot)
		*dot = '\0';

	for(i = 0; temp[i] != '\0' && j < dst_size - 1; i++)
	{
		if(temp[i] == '(')
		{
			paren = 1;
			continue;
		}
		if(temp[i] == ')')
		{
			paren = 0;
			continue;
		}
		if(temp[i] == '[')
		{
			bracket = 1;
			continue;
		}
		if(temp[i] == ']')
		{
			bracket = 0;
			continue;
		}

		if(!paren && !bracket)
			dst[j++] = temp[i];
	}
	dst[j] = '\0';

	while(j > 0 && (dst[j - 1] == ' ' || dst[j - 1] == '\t'))
		dst[--j] = '\0';

	while(dst[0] == ' ')
		memmove(dst, dst + 1, strlen(dst));

	if(dst[0] == '\0')
	{
		strncpy(dst, temp, dst_size - 1);
		dst[dst_size - 1] = '\0';
	}
}

void Show_game_name(u32 total,u32 Select)
{
	u32 need_show;
	u32 line;
	char msg[256];
	u32 X_offset=1;
	u32 Y_offset=20;
	u32 line_x = 14;
	const u16 *icon;

	if(total<10)
		need_show = total;
	else
		need_show = 10;

	for(line=0; line<need_show; line++)
	{
		u32 showy = Y_offset + line*line_x;

		Launcher_ClearTextBodyBackgroundRegion(0, showy, 240, 13);
		if(line == Select)
			Clear(17, showy, 240-17, 13, gl_color_selectBG_sd, 1);

		icon = Launcher_GetFileIcon(p_recently_play[line]);
		DrawPic((u16*)icon, 0, showy, 16, 14, 1, 0x0000, 1);

		Recent_GetDisplayName(p_recently_play[line], msg, sizeof(msg));
		DrawHZText12(msg, 32, X_offset+16, showy, (line == Select) ? LAUNCHER_SELECTED_TEXT : gl_color_text, 1);
	}
}
//---------------------------------------------------------------------------------
u32  get_count(void)
{
	u32 res;
	u32 count=0;
	char buf[512];	
	res = f_open(&gfile,"/SYSTEM/RECENT.TXT", FA_READ);	
	if(res == FR_OK)//have a play file
	{
		f_lseek(&gfile, 0x0);
		memset(buf,0x00,512);
		while(f_gets(buf, 512, &gfile) != NULL)
		{		
			//DrawHZText12(buf, 32, 1+16, showy, name_color,1);
			Trim(buf);
			if(buf[0] != '/') break;
			memset(p_recently_play[count],0x00,512);
			dmaCopy(buf,&(p_recently_play[count]), 512);		
			memset(buf,0x00,512);
			count++;
			if(count==10)break;
		}
	}
	f_close(&gfile);
	return count;
}
//---------------------------------------------------------------------------------
static u32 Recent_GetPathAt(u32 index, TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size)
{
	char *p;
	if(out_path_size == 0 || out_name_size == 0)
		return 0;
	out_path[0] = '\0';
	out_name[0] = '\0';
	if(recents_view_active && recents_view_favourites)
	{
		Launcher_LoadFavourites();
		if(index >= launcher_favourite_count)
			return 0;
	}
	else if(index >= get_count())
		return 0;
	p = strrchr(p_recently_play[index], '/');
	if(!p)
		return 0;
	if(p == p_recently_play[index])
	{
		strncpy(out_path, "/", out_path_size - 1);
		out_path[out_path_size - 1] = '\0';
	}
	else
	{
		u32 copy_len = (u32)(p - p_recently_play[index]);
		if(copy_len > out_path_size - 1) copy_len = out_path_size - 1;
		strncpy(out_path, p_recently_play[index], copy_len);
		out_path[copy_len] = '\0';
	}
	strncpy(out_name, p + 1, out_name_size - 1);
	out_name[out_name_size - 1] = '\0';
	return (out_name[0] != '\0');
}


#define FAVOURITES_FILE "/SYSTEM/FAVOURITES.TXT"
#define FAVOURITE_INDEX_FILE "/SYSTEM/FAVINDEX.TXT"
#define START_SOURCE_FILE "/SYSTEM/STARTSOURCE.TXT"

static void Launcher_BuildFullPath(const TCHAR *path, const TCHAR *name, char *out, u32 out_size)
{
	if(!out || out_size == 0)
		return;
	out[0] = '\0';
	if(!path || !name || !name[0])
		return;
	if(strcmp(path, "/") == 0)
		snprintf(out, out_size, "/%s", name);
	else
		snprintf(out, out_size, "%s/%s", path, name);
}

static u32 Launcher_SplitFullPath(const char *fullpath, TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size)
{
	char *p;
	if(!fullpath || !fullpath[0] || out_path_size == 0 || out_name_size == 0)
		return 0;
	out_path[0] = '\0';
	out_name[0] = '\0';
	p = strrchr(fullpath, '/');
	if(!p)
		return 0;
	if(p == fullpath)
	{
		strncpy(out_path, "/", out_path_size - 1);
		out_path[out_path_size - 1] = '\0';
	}
	else
	{
		u32 copy_len = (u32)(p - fullpath);
		if(copy_len > out_path_size - 1)
			copy_len = out_path_size - 1;
		strncpy(out_path, fullpath, copy_len);
		out_path[copy_len] = '\0';
	}
	strncpy(out_name, p + 1, out_name_size - 1);
	out_name[out_name_size - 1] = '\0';
	return out_name[0] != '\0';
}

static void Launcher_LoadFavouriteIndex(void)
{
	char buf[16];
	if(Launcher_SettingsReadValue("Favourite index", buf, sizeof(buf)))
	{
		launcher_favourite_index = atoi(buf);
	}
	else if(f_open(&gfile, FAVOURITE_INDEX_FILE, FA_READ) == FR_OK)
	{
		memset(buf, 0, sizeof(buf));
		if(f_gets(buf, sizeof(buf), &gfile) != NULL)
		{
			Trim(buf);
			launcher_favourite_index = atoi(buf);
			launcher_settings_migration_pending = 1;
		}
		f_close(&gfile);
	}
}

static void Launcher_SaveFavouriteIndex(void)
{
	Launcher_SaveUnifiedSettings();
}

static void Launcher_LoadFavourites(void)
{
	u32 res;
	char buf[512];
	if(launcher_favourites_cache_valid)
		return;

	launcher_favourite_count = 0;
	launcher_favourites_cache_valid = 0;
	Launcher_StartPreviewCacheInvalidate();
	memset(Launcher_FavouritesBuffer(), 0x00, LAUNCHER_MAX_FAVOURITES * LAUNCHER_FAVOURITE_PATH_LEN);
	res = f_open(&gfile, FAVOURITES_FILE, FA_READ);
	if(res == FR_OK)
	{
		f_lseek(&gfile, 0x0);
		while((launcher_favourite_count < LAUNCHER_MAX_FAVOURITES) && (f_gets(buf, sizeof(buf), &gfile) != NULL))
		{
			Trim(buf);
			if(buf[0] != '/')
				continue;
			strncpy(Launcher_FavouritesBuffer()[launcher_favourite_count], buf, LAUNCHER_FAVOURITE_PATH_LEN - 1);
			Launcher_FavouritesBuffer()[launcher_favourite_count][LAUNCHER_FAVOURITE_PATH_LEN - 1] = '\0';
			launcher_favourite_count++;
		}
		f_close(&gfile);
	}
	else
	{
		f_open(&gfile, FAVOURITES_FILE, FA_WRITE | FA_CREATE_ALWAYS);
		f_close(&gfile);
	}
	Launcher_LoadFavouriteIndex();
	if(launcher_favourite_count == 0)
		launcher_favourite_index = 0;
	else if(launcher_favourite_index >= launcher_favourite_count)
		launcher_favourite_index = 0;
	launcher_favourites_cache_valid = 1;
}

static void Launcher_SaveFavourites(void)
{
	u32 i;
	u32 res = f_open(&gfile, FAVOURITES_FILE, FA_WRITE | FA_CREATE_ALWAYS);
	if(res == FR_OK)
	{
		for(i = 0; i < launcher_favourite_count; i++)
			f_printf(&gfile, "%s\n", Launcher_FavouritesBuffer()[i]);
		f_close(&gfile);
	}
	launcher_favourites_cache_valid = 1;
	Launcher_StartPreviewCacheInvalidate();
	Launcher_SaveFavouriteIndex();
}

static u32 Launcher_AppendFavouriteFullPath(const char *fullpath)
{
	if(!fullpath || !fullpath[0])
		return 0;
	if(!launcher_favourites_cache_valid)
		Launcher_LoadFavourites();
	if(Launcher_FindFavouriteFullPath(fullpath) >= 0)
		return 1;
	if(launcher_favourite_count >= LAUNCHER_MAX_FAVOURITES)
		return 0;
	strncpy(Launcher_FavouritesBuffer()[launcher_favourite_count], fullpath, LAUNCHER_FAVOURITE_PATH_LEN - 1);
	Launcher_FavouritesBuffer()[launcher_favourite_count][LAUNCHER_FAVOURITE_PATH_LEN - 1] = '\0';
	launcher_favourite_index = launcher_favourite_count;
	launcher_favourite_count++;
	Launcher_SaveFavourites();
	return 1;
}

static s32 Launcher_FindFavouriteFullPath(const char *fullpath)
{
	u32 index;
	if(!fullpath || !fullpath[0])
		return -1;
	if(!launcher_favourites_cache_valid)
		Launcher_LoadFavourites();
	for(index = 0; index < launcher_favourite_count; index++)
	{
		if(strcmp(fullpath, Launcher_FavouritesBuffer()[index]) == 0)
			return (s32)index;
	}
	return -1;
}

static u32 Launcher_IsFavouritePathName(const TCHAR *path, const TCHAR *name)
{
	char full[512];
	Launcher_BuildFullPath(path, name, full, sizeof(full));
	return Launcher_FindFavouriteFullPath(full) >= 0;
}

static u32 Launcher_IsLaunchableFilename(const TCHAR *name)
{
	TCHAR temp[200];
	if(!name || !name[0])
		return 0;
	strncpy(temp, name, sizeof(temp) - 1);
	temp[sizeof(temp) - 1] = '\0';
	return Check_file_type(temp) != 0xff;
}

static u32 Launcher_GetSDFileFullPath(u32 absolute_index, char *out, u32 out_size)
{
	TCHAR *name;
	if(!out || out_size == 0)
		return 0;
	out[0] = '\0';
	if((absolute_index < folder_total) || (absolute_index >= folder_total + game_total_SD))
		return 0;
	name = pFilename_buffer[absolute_index - folder_total].filename;
	Launcher_BuildFullPath(currentpath, name, out, out_size);
	return out[0] != '\0';
}

static u32 Launcher_IsFavouriteSDIndex(u32 absolute_index)
{
	char full[512];
	if(!Launcher_GetSDFileFullPath(absolute_index, full, sizeof(full)))
		return 0;
	return Launcher_FindFavouriteFullPath(full) >= 0;
}

static void Launcher_GetSDDisplayNameWithFavourite(u32 file_index, char *out, u32 out_size)
{
	u32 total;
	if(!out || out_size == 0)
		return;
	out[0] = '\0';
	total = Launcher_IsNORPage() ? game_total_NOR : game_total_SD;
	if(file_index >= total)
		return;

	/* Favourites are an SD-only overlay.  Keep NOR list rendering completely
	 * independent so the NOR filename table is not filtered through SD state. */
	if(Launcher_IsNORPage())
	{
		snprintf(out, out_size, "%s", pNorFS[file_index].filename);
		return;
	}

	if(Launcher_IsFavouritePathName(currentpath, pFilename_buffer[file_index].filename))
		snprintf(out, out_size, "%s <3", pFilename_buffer[file_index].filename);
	else
		snprintf(out, out_size, "%s", pFilename_buffer[file_index].filename);
}

static void Launcher_ReadStartSource(void)
{
	char buf[32];
	launcher_start_uses_favourites = 0;
	launcher_start_screen_off = 0;

	memset(buf, 0, sizeof(buf));
	if(Launcher_SettingsReadValue("Start screen", buf, sizeof(buf)))
	{
		if((buf[0] == '0') || !strcasecmp(buf, "Off"))
			launcher_start_screen_off = 1;
	}

	memset(buf, 0, sizeof(buf));
	if(Launcher_SettingsReadValue("Start screen source", buf, sizeof(buf)))
	{
		if((buf[0] == '2') || !strcasecmp(buf, "Off"))
			launcher_start_screen_off = 1;
		else if((buf[0] == '1') || !strcasecmp(buf, "Favourites") || !strcasecmp(buf, "Favorites"))
			launcher_start_uses_favourites = 1;
	}
	else if(f_open(&gfile, START_SOURCE_FILE, FA_READ) == FR_OK)
	{
		if(f_gets(buf, sizeof(buf), &gfile) != NULL)
		{
			Trim(buf);
			if((buf[0] == '2') || !strcasecmp(buf, "Off"))
				launcher_start_screen_off = 1;
			else if((buf[0] == '1') || !strcasecmp(buf, "Favourites") || !strcasecmp(buf, "Favorites"))
				launcher_start_uses_favourites = 1;
			launcher_settings_migration_pending = 1;
		}
		f_close(&gfile);
	}
}

static void Launcher_SaveStartSource(void)
{
	Launcher_SaveUnifiedSettings();
}

static const char *Launcher_StartEnabledText(void)
{
	return Launcher_OnOffText(!launcher_start_screen_off);
}

static const char *Launcher_StartEnabledSettingName(void)
{
	return launcher_start_screen_off ? "Off" : "On";
}

static const char *Launcher_StartSourceText(void)
{
	return launcher_start_uses_favourites ? DSTEXT_FAVOURITES : DSTEXT_LAST_PLAYED;
}

static const char *Launcher_StartSourceSettingName(void)
{
	return launcher_start_uses_favourites ? "Favourites" : "Last played";
}

static void Launcher_CycleStartEnabled(void)
{
	launcher_start_screen_off ^= 1;
	Launcher_SaveStartSource();
}

static void Launcher_CycleStartSource(void)
{
	launcher_start_uses_favourites ^= 1;
	Launcher_SaveStartSource();
}

static u32 Launcher_GetStartGameEntry(TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size)
{
	Launcher_LoadFavourites();
	if(launcher_start_uses_favourites && launcher_favourite_count)
	{
		if(launcher_favourite_index >= launcher_favourite_count)
			launcher_favourite_index = 0;
		return Launcher_SplitFullPath(Launcher_FavouritesBuffer()[launcher_favourite_index], out_path, out_path_size, out_name, out_name_size);
	}
	return Read_last_played_entry(out_path, out_path_size, out_name, out_name_size);
}

static u32 Launcher_CanCycleStartFavourite(void)
{
	Launcher_LoadFavourites();
	return launcher_start_uses_favourites && launcher_favourite_count > 1;
}

static void Launcher_CycleStartFavourite(int dir)
{
	Launcher_LoadFavourites();
	if(!launcher_start_uses_favourites || launcher_favourite_count < 2)
		return;
	if(dir < 0)
		launcher_favourite_index = (launcher_favourite_index == 0) ? (launcher_favourite_count - 1) : (launcher_favourite_index - 1);
	else
		launcher_favourite_index = (launcher_favourite_index + 1) % launcher_favourite_count;
	Launcher_SaveFavouriteIndex();
}

static void Launcher_StartPreviewCacheInvalidate(void)
{
	memset(launcher_start_preview_valid, 0, sizeof(launcher_start_preview_valid));
	memset(launcher_start_preview_mode, 0, sizeof(launcher_start_preview_mode));
	memset(launcher_start_preview_path, 0, sizeof(launcher_start_preview_path));
}

static u32 Launcher_StartPreviewIndexPath(u32 index, TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size, char *out_full, u32 out_full_size)
{
	if(!launcher_start_uses_favourites || launcher_favourite_count == 0 || index >= launcher_favourite_count)
		return 0;
	if(!Launcher_SplitFullPath(Launcher_FavouritesBuffer()[index], out_path, out_path_size, out_name, out_name_size))
		return 0;
	if(out_full && out_full_size)
	{
		strncpy(out_full, Launcher_FavouritesBuffer()[index], out_full_size - 1);
		out_full[out_full_size - 1] = '\0';
	}
	return 1;
}

static u32 Launcher_StartPreviewEnsureCached(u32 index)
{
	TCHAR path[MAX_path_len];
	TCHAR name[200];
	TCHAR saved_path[MAX_path_len];
	char full[LAUNCHER_FAVOURITE_PATH_LEN];
	u32 slot;
	u32 have_thumb = 0;

	if(!launcher_start_uses_favourites || launcher_favourite_count == 0)
		return 0;
	if(index >= launcher_favourite_count)
		index = 0;

	memset(path, 0, sizeof(path));
	memset(name, 0, sizeof(name));
	memset(saved_path, 0, sizeof(saved_path));
	memset(full, 0, sizeof(full));
	if(!Launcher_StartPreviewIndexPath(index, path, sizeof(path), name, sizeof(name), full, sizeof(full)))
		return 0;
	if(!Launcher_IsGbaFilename(name))
		return 0;

	slot = index % LAUNCHER_START_PREVIEW_CACHE_COUNT;
	if(launcher_start_preview_valid[slot] &&
	   launcher_start_preview_index[slot] == index &&
	   strcmp(launcher_start_preview_path[slot], full) == 0)
		return 1;

	launcher_start_preview_valid[slot] = 0;
	launcher_start_preview_mode[slot] = 0;
	launcher_start_preview_index[slot] = index;
	strncpy(launcher_start_preview_path[slot], full, sizeof(launcher_start_preview_path[slot]) - 1);
	launcher_start_preview_path[slot][sizeof(launcher_start_preview_path[slot]) - 1] = '\0';

	f_getcwd(saved_path, sizeof(saved_path) / sizeof(*saved_path));
	if(f_chdir(path) == FR_OK)
		have_thumb = Load_ThumbnailEx(name, pReadCache + 0x10000);
	if(saved_path[0])
		f_chdir(saved_path);

	if(have_thumb)
	{
		Launcher_ScaleThumbToBox((u16*)(pReadCache + 0x10036),
		                         Launcher_ThumbnailSourceWidth(),
		                         Launcher_ThumbnailSourceHeight(),
		                         launcher_start_preview_cache[slot],
		                         LAUNCHER_START_THUMB_W,
		                         LAUNCHER_START_THUMB_H);
		launcher_start_preview_mode[slot] = 1;
	}
	else
	{
		Launcher_ScaleThumbToBox(Launcher_NotFoundImage(), Launcher_NotFoundWidth(), Launcher_NotFoundHeight(),
		                         launcher_start_preview_cache[slot],
		                         LAUNCHER_START_THUMB_W,
		                         LAUNCHER_START_THUMB_H);
		launcher_start_preview_mode[slot] = 2;
	}

	launcher_start_preview_valid[slot] = 1;
	return 1;
}

static void Launcher_StartPreviewWarmAdjacent(void)
{
	u32 prev;
	u32 next;

	if(LAUNCHER_START_PREVIEW_CACHE_COUNT < 2)
		return;
	if(!launcher_start_uses_favourites || launcher_favourite_count < 2)
		return;
	if(launcher_favourite_index >= launcher_favourite_count)
		launcher_favourite_index = 0;

	prev = (launcher_favourite_index == 0) ? (launcher_favourite_count - 1) : (launcher_favourite_index - 1);
	next = (launcher_favourite_index + 1) % launcher_favourite_count;
	Launcher_StartPreviewEnsureCached(prev);
	if(next != prev)
		Launcher_StartPreviewEnsureCached(next);
}

static const u16 *Launcher_StartPreviewCachedImage(u32 index)
{
	u32 slot;

	if(!Launcher_StartPreviewEnsureCached(index))
		return 0;
	slot = index % LAUNCHER_START_PREVIEW_CACHE_COUNT;
	if(!launcher_start_preview_valid[slot] || launcher_start_preview_mode[slot] == 0)
		return 0;
	return launcher_start_preview_cache[slot];
}

static void Launcher_ClearClip(int x, int y, int w, int h, u16 color);
static void Launcher_RestoreBGClip(const u16 *bg, int x, int y, int w, int h);

static void Launcher_DrawFavouriteHeart(int x, int y, u16 colour)
{
	/* Small 7x6 pixel heart, drawn with rectangles so it does not depend on
	   font glyphs and remains fixed in the title boxes while text scrolls. */
	Launcher_ClearClip(x + 1, y + 0, 2, 1, colour);
	Launcher_ClearClip(x + 4, y + 0, 2, 1, colour);
	Launcher_ClearClip(x + 0, y + 1, 7, 1, colour);
	Launcher_ClearClip(x + 0, y + 2, 7, 1, colour);
	Launcher_ClearClip(x + 1, y + 3, 5, 1, colour);
	Launcher_ClearClip(x + 2, y + 4, 3, 1, colour);
	Launcher_ClearClip(x + 3, y + 5, 1, 1, colour);
}

static u32 Build_favourites_virtual_list(void)
{
	u32 count = 0;
	u32 i;
	TCHAR path_part[MAX_path_len];
	TCHAR name[200];
	u32 size = 0;

	Launcher_LoadFavourites();
	memset(pFilename_buffer, 0x00, sizeof(FM_FILE_FS) * MAX_files);
	memset(p_recently_play, 0x00, sizeof(p_recently_play));
	for(i = 0; (i < launcher_favourite_count) && (count < MAX_files); i++)
	{
		if(!Launcher_SplitFullPath(Launcher_FavouritesBuffer()[i], path_part, sizeof(path_part), name, sizeof(name)))
			continue;
		strncpy(p_recently_play[count], Launcher_FavouritesBuffer()[i], sizeof(p_recently_play[count]) - 1);
		p_recently_play[count][sizeof(p_recently_play[count]) - 1] = '\0';
		strncpy(pFilename_buffer[count].filename, name, sizeof(pFilename_buffer[count].filename) - 1);
		pFilename_buffer[count].filename[sizeof(pFilename_buffer[count].filename) - 1] = '\0';
		if(Get_file_size_path(Launcher_FavouritesBuffer()[i], &size))
			pFilename_buffer[count].filesize = size;
		else
			pFilename_buffer[count].filesize = 0;
		count++;
	}
	return count;
}

static void Launcher_SetRecentVirtualMode(u32 favourites)
{
	recents_view_favourites = favourites ? 1 : 0;
	recents_saved_show_offset = 0;
	recents_saved_file_select = 0;
	strncpy(currentpath, recents_view_favourites ? favourites_virtual_path : recents_virtual_path, sizeof(currentpath) - 1);
	currentpath[sizeof(currentpath) - 1] = '\0';
	folder_select = 0;
}

static u32 Build_recent_virtual_list(void)
{
	u32 count = get_count();
	u32 i;
	TCHAR full_path[MAX_path_len];
	TCHAR name[200];
	u32 size = 0;

	if(count > MAX_files)
		count = MAX_files;

	memset(pFilename_buffer, 0x00, sizeof(FM_FILE_FS) * MAX_files);
	for(i = 0; i < count; i++)
	{
		if(Recent_GetPathAt(i, full_path, sizeof(full_path), name, sizeof(name)))
		{
			strncpy(pFilename_buffer[i].filename, name, sizeof(pFilename_buffer[i].filename) - 1);
			pFilename_buffer[i].filename[sizeof(pFilename_buffer[i].filename) - 1] = '\0';
			if(Get_file_size_path(full_path, &size))
				pFilename_buffer[i].filesize = size;
			else
				pFilename_buffer[i].filesize = 0;
		}
	}
	return count;
}

//---------------------------------------------------------------------------------
u32 show_recently_play(void)
{
	//u32 res;
	u32 all_count=0;
	u32 Select = 0;
	u32 re_show = 1;
	u32 return_val=0xBB;
	//u32 firsttime = 1;
	
	Launcher_DrawThemeBGFull((const u16*)gImage_SD_LIST);
	Launcher_DrawTopbarName(SD_list);
	Launcher_DrawTopbarTitle(SD_list, gl_recently_play);//TITLE
	
	all_count = get_count();	
	if(all_count)
	{				
		setRepeat(15,1);		
		while(1)
		{
			VBlankIntrWait();
			VBlankIntrWait();	
					
			if(re_show)
			{
				Show_game_name(all_count,Select);
				re_show = 0;
			}						
			scanKeys();
			u16 keysdown = keysDown();
			u16 keysrepeat = keysDownRepeat();	
			u16 keysup = keysUp();
			UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
			if (keysrepeat & KEY_DOWN) {
				if(Select < (all_count-1)){
					Select++;
					re_show=1;
					UIAudio_PlaySfx(UI_SFX_MOVE);
				}		
			}
			else if(keysrepeat & KEY_UP){					
				if(Select){
					Select--;
					re_show=1;
					UIAudio_PlaySfx(UI_SFX_MOVE);
				}
			}
			else if(keysup & KEY_B){	
				return_val = 0xBB;				
				break;
			}
			else if(keysup & KEY_A){					
	 			return_val = Select;	 				
	 			break;
			}				
		}
	}
	else{
		
		DrawHZText12(gl_no_game_played,0,1,20, gl_color_text,1);		
		while(1)
		{
			VBlankIntrWait();
			VBlankIntrWait();	
			scanKeys();
			u16 keysdown = keysDown();
			u16 keysup = keysUp();
			UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
			if(keysup & KEY_B){	
				UIAudio_PlayBack();
				return_val = 0xBB;				
				break;
			}
		}
		
	}
	return return_val;
}
//------------------------------------------------------------------
void Make_recently_play_file(TCHAR* path,TCHAR* gamefilename)
{
	u32 res;
	u32 i;
	u32  count;
	int get=1;
	char buf[512];	
	
	//res=f_chdir("/SYSTEM");
	//is in SAVER
	count = get_count();
		
	memset(buf,0x00,512);
	if(strcmp(path,"/") ==0){		
		snprintf(buf, sizeof(buf), "%s%s", path, gamefilename);	
	}
	else{
		snprintf(buf, sizeof(buf), "%s/%s", path, gamefilename);	
	}	

	for(i=0;i<count;i++)
	{
		get = strcmp(buf,p_recently_play[i]) ;
		if(get==0)
		{
			u32 j;
			for(j=i;j>0;j--){
				memset(p_recently_play[j],0x00,512);
				dmaCopy(&(p_recently_play[j-1]),&(p_recently_play[j]), 512);					
			}
			break;			
		}
	}	
	
	if(get != 0){
		if(count==10){
			for(i=9;i>0;i--){
				memset(p_recently_play[i],0x00,512);
				dmaCopy(&(p_recently_play[i-1]),&(p_recently_play[i]), 512);	
			}		
		}
		else if(count){
			for(i=count;i>0;i--){
				memset(p_recently_play[i],0x00,512);
				dmaCopy(&(p_recently_play[i-1]),&(p_recently_play[i]), 512);	
			}
		}	
	}
	dmaCopy(buf,&(p_recently_play[0]), 512);	//write first one
		
	res = f_open(&gfile,"/SYSTEM/RECENT.txt", FA_WRITE | FA_OPEN_ALWAYS);
	if(res == FR_OK)
	{	
		f_lseek(&gfile, 0x0000);
		for(i=0;i<count+1;i++){			
			res=f_printf(&gfile, "%s\n", p_recently_play[i]);
		}
		f_close(&gfile);
	}
}
//---------------------------------------------------------------------------------
void init_FAT_table(void)
{
	//memset(FAT_table_buffer,0,0x200);
	FAT_table_buffer[0] = 0x00000000;
	CpuFastSet( FAT_table_buffer, FAT_table_buffer, FILL | (FAT_table_size/4));
	FAT_table_buffer[2] = 0xFFFFFFFF;
}
//---------------------------------------------------------------------------------
u32 Check_game_RTS_FAT(TCHAR *filename,u32 game_save_rts)
{
	u32 res;
	//u32 ret;
	FIL file;
	u32 *FAT_table_P;
	u32 getcluster;
	u32 getcluster_old;
	u32 cluster_num = 0;
	u32 lastest_cluster;

	res = f_open(&file, filename, FA_READ);

	if(res != FR_OK)
			return 0xffffffff;


	#ifdef DEBUG
		//DEBUG_printf("first clust %x;  sec=%x ",(&file)->obj.sclust,	ClustToSect(&EZcardFs,(&file)->obj.sclust)	);
		//DEBUG_printf("fs->fs_type %x",(&EZcardFs)->fs_type);
	#endif
	if((&EZcardFs)->fs_type == FS_FAT16)
	{
		lastest_cluster = 0xFFFF;
	}
	else{
		lastest_cluster = 0xFFFFFF7;
	}
	getcluster =  (&file)->obj.sclust;

	if(game_save_rts == 1)
	{
		FAT_table_P = FAT_table_buffer;
	}
	/*else if	(game_save_rts == 2)
	{
		FAT_table_P = FAT_table_buffer+ FAT_table_SAV_offset/4;
	}*/
	else
	{
		FAT_table_P = FAT_table_buffer+ FAT_table_RTS_offset/4;
	}	


	*FAT_table_P = 0x00000000;
	FAT_table_P++;
	*FAT_table_P = (ClustToSect(&EZcardFs,getcluster));
	FAT_table_P++;

	getcluster_old = getcluster;
	do {
		getcluster =  Get_NextCluster(&(&file)->obj,getcluster);
		cluster_num++;
		if(getcluster != (getcluster_old+1)) {
			#ifdef DEBUG
				//DEBUG_printf("getcluster = %x",getcluster);
			#endif
			*FAT_table_P = (cluster_num * (&EZcardFs)->csize);//sector_per_cluster
			FAT_table_P++;
			*FAT_table_P = (ClustToSect(&EZcardFs,getcluster));//getcluster;
			FAT_table_P++;
		}
		getcluster_old = getcluster;
	} while(getcluster < lastest_cluster);
	*--FAT_table_P = 0x0;
	*--FAT_table_P = 0xffffffff;

	f_close(&file);
	return 0;
}
//---------------------------------------------------------------------------------
u32 IWRAM_CODE Loadsavefile(TCHAR *filename)
{
	UINT ret;
	UINT filesize;
	UINT left;
	FIL file;

	switch(f_open(&file, filename, FA_READ))
	{
		case FR_OK:
		{
			filesize = f_size(&file);
			if(filesize > 128*1024)
				filesize = 128*1024;

			SetRampage(0x0);
			
			if(filesize>64*1024)
			{
		      f_read(&file, pReadCache, 64*1024, (UINT *)&ret);
					WriteSram(SRAMSaver, pReadCache , 64*1024 );
					SetRampage(0x10);
					left = filesize - 64*1024 ;
		      f_read(&file, pReadCache, left, (UINT *)&ret);
					WriteSram(SRAMSaver, pReadCache , left );
			}
			else
			{
				f_read(&file, pReadCache, filesize, (UINT *)&ret);
				WriteSram(SRAMSaver,pReadCache,filesize);
			}
	    f_close(&file);
	    SetRampage(0x0);
	    return 1;
    }
    default:
			return false;
  }
}
//---------------------------------------------------------------------------------
u32 IWRAM_CODE Save_savefile(TCHAR *filename,u32 savesize)
{
	FIL file;
	if(savesize==0) return 0xff;
	u32 ret=f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
	switch(ret)
	{
		case FR_OK:
		{
			int i;
			unsigned int written;
			//memset(pReadCache,0xFF,0x200*4);
			SetRampage(0x0);
			
			if(savesize < 0x800)
			{
				ReadSram(SRAMSaver,pReadCache,savesize);
				for(i=0;i<(savesize+0x1FF)/0x200 ;i++)
				{
		      f_write(&file, pReadCache+0x200*i, 0x200, &written);
		      if(written != 0x200) break;
		    }	
			}
			else
			{
				if(savesize>64*1024)
				{
					ReadSram(SRAMSaver, pReadCache , 64*1024 );
					
					for(i=0;i<64*1024/0x800 ;i++)
					{
			      f_write(&file, pReadCache+0x800*i, 0x200*4, &written);
			      if(written != 0x200*4) break;
			    }
			    SetRampage(0x10);
					ReadSram(SRAMSaver, pReadCache , 64*1024 );
					
					for(i=0;i<64*1024/0x800 ;i++)
					{
			      f_write(&file, pReadCache+0x800*i, 0x200*4, &written);
			      if(written != 0x200*4) break;
			    }
				}
				else
				{
					ReadSram(SRAMSaver, pReadCache, savesize );
					for(i=0;i<savesize/0x800 ;i++)
					{
			      f_write(&file, pReadCache+0x800*i, 0x200*4, &written);
			      if(written != 0x200*4) break;
			    }
				}
		  }
	    
	    f_close(&file);

	     return 1;
    }
    break;
    default:
			return false;
  }
}
//---------------------------------------------------------------------------------
u32 IWRAM_CODE LoadRTSfile(TCHAR *filename)
{
	UINT ret;
	UINT filesize;
	FIL file;
	u32 page;

	switch(f_open(&file, filename, FA_READ))
	{
		case FR_OK:
		{
			filesize = f_size(&file);
			if(filesize > 0x70000)
				filesize = 0x70000;

			for(page = 0x40;page<0xB0;page += 0x10)
			{
				SetRampage(page);
				f_read(&file, pReadCache, 64*1024, (UINT *)&ret);
				WriteSram(SRAMSaver, pReadCache , 64*1024 );	
			}
	    f_close(&file);
	    SetRampage(0x0);
	    return 1;
    }
    default:
			return false;
  }
}
//---------------------------------------------------------------------------------
u32 SavefileWrite(TCHAR *filename,u32 savesize)
{
	FIL file;
	if(savesize==0) return 0xff;
	u32 ret=f_open(&file, filename, FA_WRITE | FA_CREATE_ALWAYS);
	switch(ret)
	{
		case FR_OK:
		{
			int i;
			unsigned int written;
			memset(pReadCache,0xFF,0x200*4);

			if(savesize < 0x800)
			{
				for(i=0;i<(savesize+0x1FF)/0x200 ;i++)
				{
		      f_write(&file, pReadCache, 0x200, &written);
		      if(written != 0x200) break;
		    }	
			}
			else
			{
				for(i=0;i<(savesize+0x1FF)/0x800 ;i++)
				{
		      f_write(&file, pReadCache, 0x200*4, &written);
		      if(written != 0x200*4) break;
		    }
		  }
	    
	    f_close(&file);

	     return 1;
    }
    break;
    default:
			return false;
  }
}
//---------------------------------------------------------------
u8 Check_saveMODE(u8 gamecode[])
{
	UIAudio_StopForSharedBufferUse();
	u32 i;
	BYTE savemode = 0x10;
	dmaCopy((void*)saveMODE_table, (void*)pReadCache, sizeof(saveMODE_table));
	for(i=0;i<3000;i++)
	{
		if( memcmp( ((SAVE_MODE_*)pReadCache)[i].gamecode,"FFFF",4) ==0)
		{			
			break;
		}	
		else if(  memcmp( ((SAVE_MODE_*)pReadCache)[i].gamecode,gamecode,4) ==0 )
		{
			savemode = ((SAVE_MODE_*)pReadCache)[i].savemode;
			break;
		}
	}
	return savemode;
}
//---------------------------------------------------------------
u8 Get_saveMODE(u8 Save_num,u32 gamefilesize)
{
	u8 saveMODE;
	if(Save_num==0)//auto
	{
		saveMODE = Check_saveMODE(GAMECODE);
	}
	else //Manual selection
	{
		switch(Save_num)
		{
			case 0x1:saveMODE=0x11;break;//SRAM
			case 0x2:
				if(gamefilesize> 0x1200000){//some eeprom modify rom 
					saveMODE=0x23;//32M //EEPROM8K
				}
				else{ 
					saveMODE=0x22;//EEPROM8K
				}
				break;
			case 0x3:saveMODE=0x21;break;//EEPROM512
			case 0x4:saveMODE=0x32;break;//FLASH64
			case 0x5:saveMODE=0x31;break;//FLASH128
			case 0xf:saveMODE=0x10;break;	
			default:saveMODE=0x00;break;					
		}
	}
	return saveMODE;
}
//---------------------------------------------------------------
u32 IWRAM_CODE Loadfile2PSRAM(TCHAR *filename)
{
	UIAudio_StopForSharedBufferUse();
	UINT  ret;
	u32 filesize;
	u32 res;
	u32 blocknum;
	char msg[20];
	
	u32 Address;
	vu16 page=0;
	SetPSRampage(page);
	
	res = f_open(&gfile, filename, FA_READ);
	if(res == FR_OK)
	{
		filesize = f_size(&gfile);		
		Clear(0, 160 - 15, 240, 15, gl_color_cheat_black, 1);
		ShowbootProgress(gl_copying_data);
		f_lseek(&gfile, 0x0000);
		for(blocknum=0x0000;blocknum<filesize;blocknum+=0x20000)
		{		
			sprintf(msg,"%luMb/%luMb",(blocknum)/0x20000,filesize/0x20000);
			Clear(78+54,160-15,110,15,gl_color_cheat_black,1);
			DrawHZText12(msg,0,78+54,160-15,gl_color_text,1);
			f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte
			
			if((gl_reset_on==1) || (gl_rts_on==1) || (gl_sleep_on==1) || (gl_cheat_on==1))		    
			{
				PatchInternal((u32*)pReadCache,0x20000,blocknum);
			}
						
			Address=blocknum;
			while(Address>=0x800000)
			{
				Address-=0x800000;
				page+=0x1000;
			}

			SetPSRampage(page);
			dmaCopy((void*)pReadCache,PSRAMBase_S98+Address, 0x20000);
			page = 0;
		}
		f_close(&gfile);
		SetPSRampage(0);
		return 0;
	}
	else
	{
		return 1;
	}	
	
}
//---------------------------------------------------------------------------------
void CheckLanguage(void)
{
	//read setting
	gl_select_lang =  Read_SET_info(assress_language);
	Launcher_ApplyLanguageIndex(Launcher_LanguageIndexFromStored(gl_select_lang));
}
//---------------------------------------------------------------------------------
void CheckSwitch(void)
{
	gl_reset_on = Read_SET_info(assress_v_reset);
	gl_rts_on = Read_SET_info(assress_v_rts);
	gl_sleep_on = Read_SET_info(assress_v_sleep);
	gl_cheat_on = Read_SET_info(assress_v_cheat);
	if( (gl_reset_on != 0x0) && (gl_reset_on != 0x1))
	{
		gl_reset_on = 0x0;
	}
	if( (gl_rts_on != 0x0) && (gl_rts_on != 0x1))
	{
		gl_rts_on = 0x0;
	}
	if( (gl_sleep_on != 0x0) && (gl_sleep_on != 0x1))
	{
		gl_sleep_on = 0x0;
	}
	if( (gl_cheat_on != 0x0) && (gl_cheat_on != 0x1))
	{
		gl_cheat_on = 0x0;
	}	
	
	gl_engine_sel = Read_SET_info(assress_engine_sel);
	if( (gl_engine_sel != 0x0) && (gl_engine_sel != 0x1))
	{
		gl_engine_sel = 0x1;
	}
	
	gl_show_Thumbnail = Read_SET_info(assress_show_Thumbnail);
	if( (gl_show_Thumbnail != 0x0) && (gl_show_Thumbnail != 0x1) && (gl_show_Thumbnail != 0x2))
	{
		gl_show_Thumbnail = 0x0;
	}
	
	gl_ingame_RTC_open_status = Read_SET_info(assress_ingame_RTC_open_status);
	if( (gl_ingame_RTC_open_status != 0x0) && (gl_ingame_RTC_open_status != 0x1))
	{
		gl_ingame_RTC_open_status = 0x1;
	}
	
	{
		u16 autosave_raw = Read_SET_info(assress_auto_save_sel);
		gl_auto_save_sel = autosave_raw & 0x00FF;
		gl_resume_last_on = (autosave_raw >> 8) & 0x00FF;
		if( (gl_auto_save_sel != 0x0) && (gl_auto_save_sel != 0x1))
		{
			gl_auto_save_sel = 0x0;
		}
		if( (gl_resume_last_on != 0x0) && (gl_resume_last_on != 0x1))
		{
			gl_resume_last_on = 0x0;
		}
	}
	
	{
		u16 modeb_raw = Read_SET_info(assress_ModeB_INIT);
		gl_ModeB_init = modeb_raw & 0x00FF;
		gl_boot_mode_pref = (modeb_raw >> 8) & 0x00FF;
		if( (gl_ModeB_init != 0x0) && (gl_ModeB_init != 0x1)  && (gl_ModeB_init != 0x2))
		{
			gl_ModeB_init = 0x2;
		}
		if( (gl_boot_mode_pref != 0x0) && (gl_boot_mode_pref != 0x1) && (gl_boot_mode_pref != 0x2))
		{
			gl_boot_mode_pref = 0x0;
		}
	}
		
	gl_led_open_sel = Read_SET_info(assress_led_open_sel);
	if( (gl_led_open_sel != 0x0) && (gl_led_open_sel != 0x1))
	{
		gl_led_open_sel = 0x1;
	}	
	gl_Breathing_R = Read_SET_info(assress_Breathing_R);
	if( (gl_Breathing_R != 0x0) && (gl_Breathing_R != 0x1))
	{
		gl_Breathing_R = 0x1;
	}		
	gl_Breathing_G = Read_SET_info(assress_Breathing_G);
	if( (gl_Breathing_G != 0x0) && (gl_Breathing_G != 0x1))
	{
		gl_Breathing_G = 0x1;
	}	
	gl_Breathing_B = Read_SET_info(assress_Breathing_B);
	if( (gl_Breathing_B != 0x0) && (gl_Breathing_B != 0x1))
	{
		gl_Breathing_B = 0x1;
	}	
	gl_SD_R = Read_SET_info(assress_SD_R);
	if( (gl_SD_R != 0x0) && (gl_SD_R != 0x1))
	{
		gl_SD_R = 0x0;
	}		
	gl_SD_G = Read_SET_info(assress_SD_G);
	if( (gl_SD_G != 0x0) && (gl_SD_G != 0x1))
	{
		gl_SD_G = 0x0;
	}	
	gl_SD_B = Read_SET_info(assress_SD_B);
	if( (gl_SD_B != 0x0) && (gl_SD_B != 0x1))
	{
		gl_SD_B = 0x0;
	}	
	gl_toggle_reset = Read_SET_info(assress_toggle_reset);
	if( (gl_toggle_reset != 0x0) && (gl_toggle_reset != 0x1))
	{
		gl_toggle_reset = 0x0;
	}	
	gl_toggle_backup = Read_SET_info(assress_toggle_backup);
	if( (gl_toggle_backup != 0x0) && (gl_toggle_backup != 0x1))
	{
		gl_toggle_backup = 0x0;
	}	

	u16 led_status = (gl_led_open_sel<<7) | (gl_Breathing_R<<5) | (gl_Breathing_G<<4) | (gl_Breathing_B<<3) | (gl_SD_R<<2) | (gl_SD_G<<1) | (gl_SD_B) ;
	Set_LED_control(led_status);
}

static void Launcher_StripNameLine(char *s)
{
    u32 i;
    if(!s) return;
    for(i = 0; s[i]; i++)
    {
        if((s[i] == '\r') || (s[i] == '\n'))
        {
            s[i] = '\0';
            break;
        }
    }
}

static void Launcher_ReadSystemName(void)
{
    FIL name_file;
    UINT br = 0;
    FRESULT fres;

    launcher_system_name[0] = '\0';

    fres = f_open(&name_file, "/SYSTEM/NAME.TXT", FA_READ);
    if(fres != FR_OK)
        fres = f_open(&name_file, "/SYSTEM/NAME", FA_READ);

    if(fres == FR_OK)
    {
        memset(launcher_system_name, 0, sizeof(launcher_system_name));
        f_read(&name_file, launcher_system_name, sizeof(launcher_system_name) - 1, &br);
        f_close(&name_file);
        launcher_system_name[sizeof(launcher_system_name) - 1] = '\0';
        Launcher_StripNameLine(launcher_system_name);
    }
    else
    {
        f_mkdir("/SYSTEM");
        if(f_open(&name_file, "/SYSTEM/NAME.TXT", FA_CREATE_NEW | FA_WRITE) == FR_OK)
        {
            f_printf(&name_file, "\r\n# The name shown on the top bar displays up to %u characters.\r\n", LAUNCHER_SYSTEM_NAME_DISPLAY_MAX);
            f_close(&name_file);
        }
    }

    launcher_system_name_dirty = 1;
}

static const u16 *Launcher_GetTopbarBG(u32 page_num)
{
    if(page_num == NOR_list)
        return (const u16*)Launcher_GetBGImage();
    if(page_num == SET_win)
        return (const u16*)gImage_SET;
    if(page_num == START_win)
        return (const u16*)gImage_START;
    if(page_num == HELP)
        return (const u16*)gImage_HELP;
    if((page_num == SD_list) && recents_view_active)
        return (const u16*)Launcher_GetBGImage();
    return (const u16*)Launcher_GetBGImage();
}

static void Launcher_DrawTopbarName(u32 page_num)
{
    char shown[16];
    u32 max_chars;

    (void)page_num;

    /* The full-screen/background draws already restore the top bar.
       Do not clear behind the name during refreshes: it causes visible
       flicker, especially while thumbnail views update. */
    if(!launcher_system_name[0])
        return;

    memset(shown, 0, sizeof(shown));
    max_chars = LAUNCHER_SYSTEM_NAME_DISPLAY_MAX;
    strncpy(shown, launcher_system_name, max_chars);
    shown[max_chars] = '\0';
    DrawHZText12(shown, 0, 3, 3, gl_color_topbar_text, 1);
}

static void Launcher_DrawTopbarTitle(u32 page_num, const char *title)
{
    u32 len;
    u32 x;

    (void)page_num;

    if(!title || !title[0])
        return;

    /* Page titles should be visually centred in the top bar.  The
       caller should have already drawn the top-bar background, so avoid
       clearing here as well. */
    len = DrawText12VisibleLength((char*)title);
    x = (240 - len * 6) / 2;
    DrawHZText12((TCHAR*)title, 0, x, 3, gl_color_topbar_text, 1);
}

static void Launcher_WaitForMenuKeyRelease(u16 mask)
{
    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        if((keysHeld() & mask) == 0)
            break;
    }
}

static void Launcher_FlushInputForModal(void)
{
    /* When a modal is opened directly from the start screen, stale key-up
       events from the previous screen can be seen by the boot menu on its
       first frame.  Drain held keys, then advance a couple of scans so the
       menu starts from a clean input state. */
    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        if(keysHeld() == 0)
            break;
    }
    VBlankIntrWait();
    UIAudio_Update();
    scanKeys();
    VBlankIntrWait();
    UIAudio_Update();
    scanKeys();
}

static void Launcher_SaveSDState(void)
{
    strncpy(launcher_sd_saved_path, currentpath, sizeof(launcher_sd_saved_path) - 1);
    launcher_sd_saved_path[sizeof(launcher_sd_saved_path) - 1] = '\0';
    launcher_sd_saved_folder_select = folder_select;
}

static void Launcher_RestoreSDState(void)
{
    if(!launcher_sd_restore_pending)
        return;

    launcher_sd_restore_pending = 0;
    if(launcher_sd_saved_path[0])
    {
        strncpy(currentpath, launcher_sd_saved_path, sizeof(currentpath) - 1);
        currentpath[sizeof(currentpath) - 1] = '\0';
        f_chdir(currentpath);
        folder_select = launcher_sd_saved_folder_select;
    }
}

//---------------------------------------------------------------------------------
void ShowTime(u32 page_num ,u32 page_mode)
{
    UIAudio_Update();
	static u8 last_hh = 0xFF;
	static u8 last_mm = 0xFF;
	static u8 last_ss = 0xFF;
	static u32 last_page_num = 0xFFFFFFFF;
	static u32 last_page_mode = 0xFFFFFFFF;
	u8 datetime[3];
	u8 HH;
	u8 MM;
	u8 SS;
	u32 need_redraw;
	u32 show_recent_title;
	u32 show_folder_title;
	char msgtime[50];
	char folder_title[64];
	char folder_shown[24];
	const char *recent_title = recents_view_favourites ? DSTEXT_FAVOURITES : DSTEXT_RECENTLY_PLAYED;
	//get time
	rtc_enable();
	rtc_gettime(datetime);
	rtc_disenable();
	delay(5);

	HH = UNBCD(datetime[0]&0x3F);
	MM = UNBCD(datetime[1]&0x7F);
	SS = UNBCD(datetime[2]&0x7F);
	if(HH >23)HH=0;
	if(MM >59)MM=0;
	if(SS >59)SS=0;

	show_recent_title = (page_num == SD_list) && recents_view_active && (gl_show_Thumbnail != 2);
	show_folder_title = ((page_num == SD_list) || (page_num == NOR_list)) && !show_recent_title;

	need_redraw = gl_clock_dirty;
	if(!show_recent_title && !show_folder_title && ((HH != last_hh) || (MM != last_mm) || (SS != last_ss)))
		need_redraw = 1;
	if(page_num != last_page_num)
	{
		need_redraw = 1;
		launcher_system_name_dirty = 1;
	}
	if(page_mode != last_page_mode)
		need_redraw = 1;

	if(need_redraw)
	{
		if(page_mode==0x1)
			Launcher_ClearWithThemeBG((const u16*)gImage_SD_LIST,80, 3, 105, 13);	
		else if(page_num==SD_list)
			Launcher_ClearWithThemeBG(Launcher_GetBGImage(),80, 3, 105, 13);
		else if (page_num==NOR_list)
			Launcher_ClearWithThemeBG(Launcher_GetBGImage(),80, 3, 105, 13);

		if(launcher_system_name_dirty || (page_num != last_page_num))
		{
			Launcher_DrawTopbarName(page_num);
			launcher_system_name_dirty = 0;
		}

		if(show_recent_title)
		{
			Launcher_ClearWithThemeBG(Launcher_GetTopbarBG(page_num), 66, 3, 118, 13);
			Launcher_DrawTopbarTitle(page_num, recent_title);
		}
		else if(show_folder_title)
		{
			u32 len;
			memset(folder_title, 0, sizeof(folder_title));
			Launcher_CleanTitle(Launcher_GetCurrentFolderLabel(), folder_title, sizeof(folder_title));
			Launcher_MakeEllipsisText(folder_title, folder_shown, sizeof(folder_shown), 17);
			len = DrawText12VisibleLength(folder_shown);
			DrawHZText12(folder_shown, 0, 184 - (len * 6), 3, gl_color_topbar_text, 1);
		}
		else
		{
			sprintf(msgtime,"%02u:%02u:%02u",HH,MM,SS);
			DrawHZText12(msgtime,0,120,3,gl_color_topbar_text,1);
		}
		gl_clock_dirty = 0;
	}

	last_hh = HH;
	last_mm = MM;
	last_ss = SS;
	last_page_num = page_num;
	last_page_mode = page_mode;
}
//---------------------------------------------------------------
void IWRAM_CODE make_pogoshell_arguments(TCHAR *cmdname, TCHAR *filename, u32 cmdsize, u32 filesize, u32 Address, u32 offset)
{
	u32 *p, addr;
	char *ptr, *cmdptr, *fileptr;
	int i = 0;

	addr = 0x08000000 + cmdsize;

	p = (u32 *)(0x02000000+255*1024);

	p[0] = 0xFAB0BABE; //magic value in IWRAM

	ptr = (char *)&p[2];
	*ptr++ = '/';
	cmdptr = ptr;

	if (strlen(cmdname) > 31) {
		TCHAR *ext = strrchr(cmdname, '.');
		if (!ext) {
			memcpy(ptr,cmdname,31);
			ptr[31]='\0';
		} else {
			if (strlen(ext) > 31) {
				memcpy(ptr,ext,31);
				ptr[31]='\0';
			} else {
				int extlen=strlen(ext);
				memcpy(ptr,cmdname,31-extlen);
				memcpy(ptr+31-extlen,ext,extlen+1);
			}
		}
	} else
		strcpy(ptr, cmdname);

	ptr += (strlen(ptr)+1);

	*ptr++ = '/';
	fileptr = ptr;

	if (strlen(filename) > 31) {
		TCHAR *ext = strrchr(filename, '.');
		if (!ext) {
			memcpy(ptr,filename,31);
			ptr[31]='\0';
		} else {
			if (strlen(ext) > 31) {
				memcpy(ptr,ext,31);
				ptr[31]='\0';
			} else {
				int extlen=strlen(ext);
				memcpy(ptr,filename,31-extlen);
				memcpy(ptr+31-extlen,ext,extlen+1);
			}
		}
	} else
		strcpy(ptr, filename);

	ptr += (strlen(ptr)+1);

	*ptr++ = '\0';

	p[1] = 2; // argc

	p[-1] = addr; //addr of file
	p[-2] = filesize;

	// Make fake Pogoshell filesize
	//
	// Passed in 32KB aligned
	offset = offset + 0x08000000 + 8;

	p = (u32*)pReadCache;

	// Magic value in ROM address space
	*p++ = 0xFAB0BABE;
	*p++ = (2*(32+4+4)) | 0x80000000;

	memcpy(p, cmdptr, 32);
	p+=32/4;
	*p++ = cmdsize;
	*p++ = 0x08000000 - offset;

	memcpy(p, fileptr, 32);

	p+=32/4;
	*p++ = filesize;
	*p++ = addr - offset;

	dmaCopy((void*)pReadCache,PSRAMBase_S98 + Address, 0x58);
}

u32 IWRAM_CODE LoadEMU2PSRAM(TCHAR *filename,u32 is_EMU)
{
	UIAudio_StopForSharedBufferUse();
	u8 str_len;
	UINT  ret;
	u32 filesize;
	u32 res;
    // u32 blocknum, blockoffset = gl_error_0; // why are you setting it to a string pointer? This is never brought up again outside of overwriting it.
	u32 blocknum, blockoffset = 0;
	char msg[20];
	
	u32 Address;
	vu16 page=0;
	SetPSRampage(page);
	
	u32 rom_start_address=0;
	switch(is_EMU)
	{
		case 1://gbc
		case 2://gb	
			dmaCopy((void*)goomba_gba,pReadCache, goomba_gba_size);
			dmaCopy((void*)pReadCache,PSRAMBase_S98, goomba_gba_size);
			rom_start_address = goomba_gba_size;
			break;
		default:
			res = f_open(&gfile, plugin, FA_READ);
			if(res != FR_OK)
				return 1;

			filesize = f_size(&gfile);

			f_lseek(&gfile, 0x0000);
			ShowbootProgress(gl_generating_emu);	
			for(blocknum=0x0000;blocknum<filesize;blocknum+=0x20000)
			{		
				sprintf(msg,"%luMb",(blocknum)/0x20000);
				str_len = strlen(msg);
				Clear(0, 130, 240, 15, gl_color_cheat_black, 1);
				DrawHZText12(msg, 0, (240 - str_len * 6) / 2, 160 - 30, 0x7fff, 1);
				//f_lseek(&gfile, blocknum);
				if (filesize-blocknum*0x20000 < 0x20000)
					memset(pReadCache, 0, 0x20000);
				f_read(&gfile, pReadCache, 0x20000, (UINT*)&ret);//pReadCache max 0x20000 Byte
				page = 0;
						
				Address=blocknum;
				while(Address>=0x400000)
				{
					Address-=0x400000;
					page+=0x800;
				}
				SetPSRampage(page);
				dmaCopy((void*)pReadCache,PSRAMBase_S98 + Address, 0x20000);
			
			}
			f_close(&gfile);
			SetPSRampage(0);
			blockoffset=blocknum;

			// Guarantee word alignment
			rom_start_address = (filesize+3)&~3;

			break;		
	}
	
	res = f_open(&gfile, filename, FA_READ);
	if(res == FR_OK)
	{
		filesize = f_size(&gfile);	
			
		Clear(60,160-15,120,15,gl_color_cheat_black,1);	
		DrawHZText12(gl_writing,0,78,160-15,0x7fff,1);	

		f_lseek(&gfile, 0x0000);
		ShowbootProgress(gl_generating_emu);
		for(blocknum=0x0000;blocknum<filesize;blocknum+=0x20000)
		{		
			sprintf(msg, "%luMb", (blocknum + blockoffset) / 0x20000);
			str_len = strlen(msg);
			Clear(0, 130, 240, 15, gl_color_cheat_black, 1);
			DrawHZText12(msg, 0, (240 - str_len * 6) / 2, 160 - 30, 0x7fff, 1);
			//f_lseek(&gfile, blocknum);
			if (filesize - blocknum * 0x20000 < 0x20000)
				memset(pReadCache, 0, 0x20000);
			f_read(&gfile, pReadCache, 0x20000, &ret);//pReadCache max 0x20000 Byte
			page = 0;	
			Address=blocknum;
			while(Address>=0x800000)
			{
				Address-=0x800000;
				page+=0x1000;
			}
			SetPSRampage(page);
			dmaCopy((void*)pReadCache,PSRAMBase_S98 + rom_start_address + Address, 0x20000);
			
			page = 0;
		}
		f_close(&gfile);

		if (is_EMU > 3) {
			Address = rom_start_address + filesize;
		      	Address = (Address + 0x7fff)&~0x7fff;
			u32 offset = Address;
			while(Address>=0x400000)
			{
				Address-=0x400000;
				page+=0x800;
			}
			SetPSRampage(page);
			make_pogoshell_arguments(plugin + 9, filename, rom_start_address, filesize, Address, offset);
		}

		SetPSRampage(0);
		return 0;
	}
	else
	{
		return 1;
	}	
	
	return 0;
}
//---------------------------------------------------------------------------------
extern u16 SET_info_buffer [0x200]EWRAM_BSS;
void save_set_info_SELECT(void)
{
	u32 address;
	for(address=0;address < assress_max;address++)
	{
		SET_info_buffer[address] = Read_SET_info(address);
	}
	SET_info_buffer[assress_show_Thumbnail] = gl_show_Thumbnail;
	/*for(address=13;address < 22;address++)
	{
		SET_info_buffer[address] = Read_SET_info(address);
	}	*/	
	
	//save to nor 
	Launcher_PrepareSettingsFlashWrite();
	Save_SET_info(SET_info_buffer,0x200);
}
//---------------------------------------------------------------------------------
//Sort folder
void Sort_folder(u32 folder_total)
{
	u32 ret;
	u32 i;
	int get;
	if(folder_total>1)
	{
		for(ret=0;ret<folder_total-1;ret++)
		{
			for(i=0;i<folder_total-ret-1;i++)
			{
				get = strcmp(pFolder[i].filename,pFolder[i+1].filename) ;
				if(get>0)
				{
					dmaCopy(&pFolder[i+1],&(pFilename_temp.filename),sizeof(FM_Folder_FS));
					dmaCopy(&pFolder[i],&pFolder[i+1],sizeof(FM_Folder_FS));
					dmaCopy(&(pFilename_temp.filename),&pFolder[i],sizeof(FM_Folder_FS));					
				}
			}
		}
	}
}
//---------------------------------------------------------------------------------
//Sort file 
void Sort_file(u32 game_total_SD)
{
	u32 ret;
	u32 i;
	int get;
	if(game_total_SD>1)
	{
		for(ret=0;ret<game_total_SD-1;ret++)
		{
			for(i=0;i<game_total_SD-ret-1;i++)
			{
				get = strcmp(pFilename_buffer[i].filename,pFilename_buffer[i+1].filename) ;
				if(get>0)
				{
					dmaCopy(&pFilename_buffer[i+1],&pFilename_temp,sizeof(FM_FILE_FS));
					dmaCopy(&pFilename_buffer[i],&pFilename_buffer[i+1],sizeof(FM_FILE_FS));
					dmaCopy(&pFilename_temp,&pFilename_buffer[i],sizeof(FM_FILE_FS));					
				}
			}
		}
	}	
}	
//---------------------------------------------------------------------------------
static u32 Launcher_CustomThumbHash(const char *name)
{
	u32 hash = 2166136261U;
	char ch;

	if(!name)
		return 0;
	while(*name)
	{
		ch = *name++;
		if((ch >= 'A') && (ch <= 'Z'))
			ch += ('a' - 'A');
		hash ^= (u8)ch;
		hash *= 16777619U;
	}
	return hash ? hash : 1;
}

static void Launcher_CustomThumbStripLine(char *line)
{
	char *src;
	char *dst;
	char *end;

	if(!line)
		return;
	src = line;
	while((*src == ' ') || (*src == '\t'))
		src++;
	if(src != line)
		memmove(line, src, strlen(src) + 1);
	end = line + strlen(line);
	while((end > line) && ((end[-1] == '\r') || (end[-1] == '\n') || (end[-1] == ' ') || (end[-1] == '\t')))
		*--end = '\0';
	end = strrchr(line, '.');
	if(end && !strcasecmp(end, ".bmp"))
		*end = '\0';
	dst = strchr(line, '/');
	if(!dst)
		dst = strchr(line, '\\');
	if(dst)
	{
		src = dst + 1;
		memmove(line, src, strlen(src) + 1);
	}
}

static void Launcher_LoadCustomThumbManifest(u32 style)
{
	char path[32];
	char name[112];
	DIR custom_dir;
	FILINFO custom_info;
	u32 hash;

	if(style > LAUNCHER_THUMB_STYLE_BOX)
		style = LAUNCHER_THUMB_STYLE_TITLE;
	if(launcher_custom_thumb_manifest_loaded[style])
		return;

	launcher_custom_thumb_manifest_loaded[style] = 1;
	launcher_custom_thumb_manifest_present[style] = 0;
	launcher_custom_thumb_manifest_count[style] = 0;
	sprintf(path, "%s/CUSTOM", Launcher_ThumbnailFolder());
	if(f_opendir(&custom_dir, path) != FR_OK)
		return;

	launcher_custom_thumb_manifest_present[style] = 1;
	while(launcher_custom_thumb_manifest_count[style] < LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX)
	{
		if((f_readdir(&custom_dir, &custom_info) != FR_OK) || !custom_info.fname[0])
			break;
		if(custom_info.fattrib & AM_DIR)
			continue;
		memset(name, 0, sizeof(name));
		strncpy(name, custom_info.fname, sizeof(name) - 1);
		Launcher_CustomThumbStripLine(name);
		if(!name[0])
			continue;
		hash = Launcher_CustomThumbHash(name);
		if(hash)
			launcher_custom_thumb_manifest_hash[style][launcher_custom_thumb_manifest_count[style]++] = hash;
	}
	f_closedir(&custom_dir);
}

static u32 Launcher_ShouldTryCustomThumbnail(const char *name)
{
	u32 style = launcher_thumbnail_style;
	u32 hash;
	u32 i;

	if(style > LAUNCHER_THUMB_STYLE_BOX)
		style = LAUNCHER_THUMB_STYLE_TITLE;
	if(!name || !name[0])
		return 0;
	Launcher_LoadCustomThumbManifest(style);
	if(!launcher_custom_thumb_manifest_present[style])
		return 0;
	hash = Launcher_CustomThumbHash(name);
	for(i = 0; i < launcher_custom_thumb_manifest_count[style]; i++)
	{
		if(launcher_custom_thumb_manifest_hash[style][i] == hash)
			return 1;
	}
	return 0;
}

static u32 Launcher_LoadCustomThumbnailByName(const char *name, u8 *dst)
{
	u32 rett;
	u32 res;
	TCHAR picpath[160];
	u32 read_size = Launcher_ThumbnailReadSize();

	if(!dst || !Launcher_ShouldTryCustomThumbnail(name))
		return 0;

	memset(picpath, 0, sizeof(picpath));
	sprintf(picpath, "%s/CUSTOM/%s.bmp", Launcher_ThumbnailFolder(), name);
	res = f_open(&gfile, picpath, FA_READ);
	if(res != FR_OK)
		return 0;

	UIAudio_StopForSharedBufferUse();
	res = f_read(&gfile, dst, read_size, (UINT*)&rett);
	f_close(&gfile);
	return (res == FR_OK) && (rett == read_size);
}

static void Launcher_CustomThumbFileName(const char *filename, char *name, u32 name_size)
{
	const char *base;
	const char *slash;
	const char *backslash;
	char *dot;

	if(!name || !name_size)
		return;
	name[0] = '\0';
	if(!filename)
		return;

	base = filename;
	slash = strrchr(filename, '/');
	backslash = strrchr(filename, '\\');
	if(slash && (!backslash || (slash > backslash)))
		base = slash + 1;
	else if(backslash)
		base = backslash + 1;

	strncpy(name, base, name_size - 1);
	name[name_size - 1] = '\0';
	dot = strrchr(name, '.');
	if(dot)
		*dot = '\0';
}

u32 Load_ThumbnailEx(TCHAR *pfilename_pic, u8 *dst)
{
  u32 rett;
  u32 res;
  TCHAR picpath[160];
  TCHAR custom_name[104];
  u32 read_size = Launcher_ThumbnailReadSize();

	Launcher_CustomThumbFileName(pfilename_pic, custom_name, sizeof(custom_name));

	if(Launcher_LoadCustomThumbnailByName(custom_name, dst))
		return 1;

	res = f_open(&gfile, pfilename_pic, FA_READ);
	if(res == FR_OK)
	{
		f_lseek(&gfile, 0xAC);
		res = f_read(&gfile, GAMECODE, 4, (UINT *)&rett);
		f_close(&gfile);
		if((res != FR_OK) || (rett != 4))
			return 0;
					
		memset(picpath,00,sizeof(picpath));
		sprintf(picpath,"%s/%c/%c/%c%c%c%c.bmp", Launcher_ThumbnailFolder(), GAMECODE[0],GAMECODE[1],GAMECODE[0],GAMECODE[1],GAMECODE[2],GAMECODE[3]);						
		res = f_open(&gfile,picpath, FA_READ);
		if(res == FR_OK)
		{
			UIAudio_StopForSharedBufferUse();
			res = f_read(&gfile, dst, read_size, (UINT*)&rett);
			f_close(&gfile);	
			return (res == FR_OK) && (rett == read_size);
		}		
				
	}			
	return 0;	
}

u32 Load_Thumbnail(TCHAR *pfilename_pic)
{
	return Load_ThumbnailEx(pfilename_pic, pReadCache+0x10000);
}

//---------------------------------------------------------------------------------
static void Launcher_CleanTitle(const TCHAR *src, char *dst, u32 dst_size)
{
	u32 i;
	u32 j = 0;
	u32 paren = 0;
	u32 bracket = 0;
	char temp[128];
	char *dot;

	if(dst_size == 0)
		return;

	memset(temp, 0, sizeof(temp));
	strncpy(temp, src, sizeof(temp) - 1);

	dot = strrchr(temp, '.');
	if(dot)
		*dot = '\0';

	for(i = 0; temp[i] != '\0' && j < dst_size - 1; i++)
	{
		if(temp[i] == '(')
		{
			paren = 1;
			continue;
		}
		if(temp[i] == ')')
		{
			paren = 0;
			continue;
		}
		if(temp[i] == '[')
		{
			bracket = 1;
			continue;
		}
		if(temp[i] == ']')
		{
			bracket = 0;
			continue;
		}

		if(!paren && !bracket)
			dst[j++] = temp[i];
	}
	dst[j] = '\0';

	while(j > 0 && (dst[j - 1] == ' ' || dst[j - 1] == '\t'))
	{
		dst[--j] = '\0';
	}

	if(dst[0] == '\0')
	{
		strncpy(dst, temp, dst_size - 1);
		dst[dst_size - 1] = '\0';
	}
}

static int Launcher_SplitTitle(const char *title, char lines[3][32])
{
	int title_len;
	int pos = 0;
	int line_count = 0;
	int i;

	memset(lines, 0, sizeof(char) * 3 * 32);
	title_len = strlen(title);

	while(pos < title_len && line_count < 3)
	{
		int remaining = title_len - pos;
		int max_take = (line_count < 2) ? 20 : 24;
		int take = (remaining > max_take) ? max_take : remaining;
		int split = pos + take;

		if(split < title_len)
		{
			for(i = split; i > pos + 8; i--)
			{
				if(title[i] == ' ')
				{
					split = i;
					break;
				}
			}
		}

		if(split <= pos)
			split = pos + take;

		strncpy(lines[line_count], title + pos, split - pos);
		lines[line_count][split - pos] = '\0';

		while(lines[line_count][0] == ' ')
			memmove(lines[line_count], lines[line_count] + 1, strlen(lines[line_count]));

		pos = split;
		while(title[pos] == ' ')
			pos++;

		line_count++;
	}

	if(pos < title_len && line_count > 0)
	{
		int last = line_count - 1;
		int len = strlen(lines[last]);
		if(len > 21)
			len = 21;
		while(len > 0 && lines[last][len - 1] == ' ')
			len--;
		lines[last][len] = '\0';
		strcat(lines[last], "...");
	}

	if(line_count == 0)
	{
		strcpy(lines[0], " ");
		line_count = 1;
	}

	return line_count;
}

typedef struct
{
	TCHAR *name;
	u32 is_folder;
	u32 has_thumbnail;
	u8 *thumb_data;
} LauncherEntryInfo;

typedef struct
{
	u32 valid;
	u32 absolute_index;
	u32 has_thumbnail;
	u8 *thumb_data;
} LauncherThumbCache;

static LauncherThumbCache launcher_cache_prev = {0, 0, 0, 0};
static LauncherThumbCache launcher_cache_selected = {0, 0, 0, 0};
static LauncherThumbCache launcher_cache_next = {0, 0, 0, 0};
static u32 launcher_cache_center_index = 0xFFFFFFFF;

static void Launcher_ClearClip(int x, int y, int w, int h, u16 color)
{
	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;

	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > 240) x1 = 240;
	if(y1 > 160) y1 = 160;

	if((x1 <= x0) || (y1 <= y0))
		return;

	Clear(x0, y0, x1 - x0, y1 - y0, color, 1);
}

static void Launcher_ClearClipStriped(int x, int y, int w, int h, u16 color_a, u16 color_b)
{
	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;
	int row;

	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > 240) x1 = 240;
	if(y1 > 160) y1 = 160;

	if((x1 <= x0) || (y1 <= y0))
		return;

	for(row = y0; row < y1; row++)
	{
		Clear(x0, row, x1 - x0, 1, ((row - y) & 1) ? color_b : color_a, 1);
	}
}

static void Launcher_ClearTitleFillClipPhase(int x, int y, int w, int h, u16 base_fill, int phase)
{
	u16 stripe_fill = gl_color_title_stripe;
	if(base_fill != gl_color_title_fill)
		stripe_fill = base_fill;

	if(phase & 1)
		Launcher_ClearClipStriped(x, y, w, h, stripe_fill, base_fill);
	else
		Launcher_ClearClipStriped(x, y, w, h, base_fill, stripe_fill);
}

static void Launcher_ClearTitleFillClip(int x, int y, int w, int h, u16 base_fill)
{
	Launcher_ClearTitleFillClipPhase(x, y, w, h, base_fill, 0);
}

static void Launcher_ClearTextBodyBackgroundRegion(int x, int y, int w, int h)
{
	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;

	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > 240) x1 = 240;
	if(y1 > 160) y1 = 160;

	if((x1 <= x0) || (y1 <= y0))
		return;

	Launcher_ClearWithThemeBG(Launcher_GetBGImage(), x0, y0, x1 - x0, y1 - y0);
}

static void Launcher_ClearTextBodyBackground(void)
{
	Launcher_ClearTextBodyBackgroundRegion(0, 19, 240, 160 - 19);
}

static void Launcher_ClearListBodyBackground(void)
{
	Launcher_ClearWithThemeBG((const u16*)gImage_SD_LIST, 0, 19, 240, 160 - 19);
}

static void Launcher_DrawPicClipStride(const u16 *src, int src_stride, int x, int y, int w, int h)
{
	int src_x = 0;
	int src_y = 0;
	int draw_w = w;
	int draw_h = h;
	int row;
	vu16 *dst_base = (vu16*)0x06000000;

	if(x < 0)
	{
		src_x = -x;
		draw_w -= src_x;
		x = 0;
	}
	if(y < 0)
	{
		src_y = -y;
		draw_h -= src_y;
		y = 0;
	}
	if((x + draw_w) > 240)
		draw_w = 240 - x;
	if((y + draw_h) > 160)
		draw_h = 160 - y;

	if((draw_w <= 0) || (draw_h <= 0))
		return;

	for(row = 0; row < draw_h; row++)
	{
		dmaCopy((void*)(src + ((src_y + row) * src_stride) + src_x),
		        (void*)(dst_base + ((y + row) * 240) + x),
		        draw_w * 2);
	}
}

static void Launcher_DrawPicClip(const u16 *src, int x, int y, int w, int h)
{
	Launcher_DrawPicClipStride(src, w, x, y, w, h);
}

static void Launcher_ThumbBoxSize(int box_w, int box_h, int *draw_w, int *draw_h)
{
	if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
	{
		int size = (box_w < box_h) ? box_w : box_h;
		*draw_w = size;
		*draw_h = size;
		return;
	}
	*draw_w = box_w;
	*draw_h = box_h;
}

static void Launcher_DrawScaledThumbClip(const u16 *src, int src_w, int src_h, int x, int y, int w, int h)
{
	int dst_y;
	vu16 *dst_base = (vu16*)VRAM;

	if(!src || src_w <= 0 || src_h <= 0 || w <= 0 || h <= 0)
		return;

	for(dst_y = 0; dst_y < h; dst_y++)
	{
		int screen_y = y + dst_y;
		int src_y = (dst_y * src_h) / h;
		int dst_x;
		if(screen_y < 0 || screen_y >= 160)
			continue;
		for(dst_x = 0; dst_x < w; dst_x++)
		{
			int screen_x = x + dst_x;
			int src_x = (dst_x * src_w) / w;
			if(screen_x < 0 || screen_x >= 240)
				continue;
			dst_base[screen_y * 240 + screen_x] = src[src_y * src_w + src_x];
		}
	}
}

static void Launcher_DrawThumbBorder(int x, int y, int w, int h)
{
	if(!LAUNCHER_THUMB_BORDER_ENABLED)
		return;
	Launcher_ClearClip(x - 1, y - 1, w + 2, 1, RGB(0,0,0));
	Launcher_ClearClip(x - 1, y + h, w + 2, 1, RGB(0,0,0));
	Launcher_ClearClip(x - 1, y - 1, 1, h + 2, RGB(0,0,0));
	Launcher_ClearClip(x + w, y - 1, 1, h + 2, RGB(0,0,0));
}

static void Launcher_DrawThumbInBox(const u16 *src, int src_w, int src_h, int box_x, int box_y, int box_w, int box_h)
{
	int draw_w;
	int draw_h;
	int draw_x;
	int draw_y;

	Launcher_ThumbBoxSize(box_w, box_h, &draw_w, &draw_h);
	draw_x = box_x + ((box_w - draw_w) / 2);
	draw_y = box_y + ((box_h - draw_h) / 2);

	if((src_w == draw_w) && (src_h == draw_h))
		Launcher_DrawPicClipStride(src, src_w, draw_x, draw_y, draw_w, draw_h);
	else
		Launcher_DrawScaledThumbClip(src, src_w, src_h, draw_x, draw_y, draw_w, draw_h);
	Launcher_DrawThumbBorder(draw_x, draw_y, draw_w, draw_h);
}

static void Launcher_DrawThumbPanel(const u16 *src, int src_w, int src_h, u16 *dst, int box_x, int box_y, int box_w, int box_h)
{
	Launcher_ScaleThumbToBox(src, src_w, src_h, dst, box_w, box_h);
	Launcher_DrawPicClipStride(dst, box_w, box_x, box_y, box_w, box_h);
	Launcher_DrawThumbBorder(box_x, box_y, box_w, box_h);
}

static const u16 *Launcher_NotFoundImage(void)
{
	return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? (const u16*)gImage_NOTFOUNDsquare : (const u16*)gImage_NOTFOUND;
}

static int Launcher_NotFoundWidth(void)
{
	return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? 80 : 120;
}

static int Launcher_NotFoundHeight(void)
{
	return 80;
}

static void Launcher_RestoreHorizontalThumbBox(int x, int y, int w, int h, u16 outline, u16 fill)
{
	(void)outline;
	(void)fill;
	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x - 1, y - 1, w + 2, h + 2);
}

static void Launcher_ScaleThumb120x80_To60x40(const u16 *src, u16 *dst)
{
	int y;
	int x;

	for(y = 0; y < 40; y++)
		for(x = 0; x < 60; x++)
			dst[y * 60 + x] = src[(y * 2) * 120 + (x * 2)];
}

static void Launcher_ScaleThumb80x80_To40x40(const u16 *src, u16 *dst)
{
	int y;
	int x;

	for(y = 0; y < 40; y++)
		for(x = 0; x < 40; x++)
			dst[y * 40 + x] = src[(y * 2) * 80 + (x * 2)];
}

static void Launcher_ScaleThumb80x80_To37x37(const u16 *src, u16 *dst, int dst_stride, int dst_x, int dst_y)
{
	int y;
	int x;

	Launcher_InitScaleMaps();
	for(y = 0; y < 37; y++)
	{
		const u16 *src_row = src + launcher_scale80_37[y] * 80;
		u16 *dst_row = dst + (dst_y + y) * dst_stride + dst_x;
		for(x = 0; x < 37; x++)
			dst_row[x] = src_row[launcher_scale80_37[x]];
	}
}

static void Launcher_ScaleThumb80x80_To56x56(const u16 *src, u16 *dst, int dst_stride, int dst_x, int dst_y)
{
	int y;
	int x;

	Launcher_InitScaleMaps();
	for(y = 0; y < 56; y++)
	{
		const u16 *src_row = src + launcher_scale80_56[y] * 80;
		u16 *dst_row = dst + (dst_y + y) * dst_stride + dst_x;
		for(x = 0; x < 56; x++)
			dst_row[x] = src_row[launcher_scale80_56[x]];
	}
}

static void Launcher_ScaleThumb80x80_To32x32(const u16 *src, u16 *dst, int dst_stride, int dst_x, int dst_y)
{
	int y;
	int x;

	Launcher_InitScaleMaps();
	for(y = 0; y < 32; y++)
	{
		const u16 *src_row = src + launcher_scale80_32[y] * 80;
		u16 *dst_row = dst + (dst_y + y) * dst_stride + dst_x;
		for(x = 0; x < 32; x++)
			dst_row[x] = src_row[launcher_scale80_32[x]];
	}
}

static void Launcher_InitScaleMaps(void)
{
	int i;
	for(i = 0; i < 84; i++)
		launcher_scale84_x[i] = (u8)((i * 119 + 41) / 83);
	for(i = 0; i < 56; i++)
		launcher_scale56_y[i] = (u8)((i * 79 + 27) / 55);
	for(i = 0; i < 48; i++)
		launcher_scale48_x[i] = (u8)((i * 119 + 23) / 47);
	for(i = 0; i < 32; i++)
		launcher_scale32_y[i] = (u8)((i * 79 + 15) / 31);
	for(i = 0; i < 56; i++)
		launcher_scale80_56[i] = (u8)((i * 79 + 27) / 55);
	for(i = 0; i < 37; i++)
		launcher_scale80_37[i] = (u8)((i * 79 + 18) / 36);
	for(i = 0; i < 32; i++)
		launcher_scale80_32[i] = (u8)((i * 79 + 15) / 31);
	launcher_scale_maps_ready = 1;
}

static void Launcher_ScaleThumb120x80_To84x56(const u16 *src, u16 *dst)
{
	int y;
	int x;

	Launcher_InitScaleMaps();
	for(y = 0; y < 56; y++)
	{
		const u16 *src_row = src + launcher_scale56_y[y] * 120;
		u16 *dst_row = dst + y * 84;
		for(x = 0; x < 84; x++)
			dst_row[x] = src_row[launcher_scale84_x[x]];
	}
}

static void Launcher_ScaleThumb120x80_To48x32(const u16 *src, u16 *dst)
{
	int y;
	int x;

	Launcher_InitScaleMaps();
	for(y = 0; y < 32; y++)
	{
		const u16 *src_row = src + launcher_scale32_y[y] * 120;
		u16 *dst_row = dst + y * 48;
		for(x = 0; x < 48; x++)
			dst_row[x] = src_row[launcher_scale48_x[x]];
	}
}

static void Launcher_ScaleThumbToBox(const u16 *src, int src_w, int src_h, u16 *dst, int box_w, int box_h)
{
	int draw_w;
	int draw_h;
	int draw_x;
	int draw_y;
	int x;
	int y;

	if(!src || !dst || box_w <= 0 || box_h <= 0)
		return;

	if(src_w == 120 && src_h == 80 && launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_TITLE)
	{
		if(box_w == 60 && box_h == 40)
		{
			Launcher_ScaleThumb120x80_To60x40(src, dst);
			return;
		}
		if(box_w == 84 && box_h == 56)
		{
			Launcher_ScaleThumb120x80_To84x56(src, dst);
			return;
		}
		if(box_w == 48 && box_h == 32)
		{
			Launcher_ScaleThumb120x80_To48x32(src, dst);
			return;
		}
	}

	for(y = 0; y < box_h; y++)
		for(x = 0; x < box_w; x++)
			dst[y * box_w + x] = gl_color_body_fill;

	if(src_w == 80 && src_h == 80 && launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
	{
		if(box_w == 56 && box_h == 37)
		{
			Launcher_ScaleThumb80x80_To37x37(src, dst, box_w, (box_w - 37) / 2, 0);
			return;
		}
		if(box_w == 84 && box_h == 56)
		{
			Launcher_ScaleThumb80x80_To56x56(src, dst, box_w, (box_w - 56) / 2, 0);
			return;
		}
		if(box_w == 48 && box_h == 32)
		{
			Launcher_ScaleThumb80x80_To32x32(src, dst, box_w, (box_w - 32) / 2, 0);
			return;
		}
	}

	Launcher_ThumbBoxSize(box_w, box_h, &draw_w, &draw_h);
	draw_x = (box_w - draw_w) / 2;
	draw_y = (box_h - draw_h) / 2;

	for(y = 0; y < draw_h; y++)
	{
		int sy = (y * src_h) / draw_h;
		for(x = 0; x < draw_w; x++)
		{
			int sx = (x * src_w) / draw_w;
			dst[(draw_y + y) * box_w + draw_x + x] = src[sy * src_w + sx];
		}
	}
}

static void Launcher_PreScaleHorizontalSidePreview(const u16 *src, int src_w, int src_h, u16 *dst)
{
	if(!src || !dst)
		return;
	if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
		Launcher_ScaleThumb80x80_To40x40(src, dst);
	else
		Launcher_ScaleThumbToBox(src, src_w, src_h, dst, LAUNCHER_HORZ_SIDE_W, LAUNCHER_HORZ_SIDE_H);
}

static void Launcher_DrawPreparedHorizontalSidePreview(u16 *src, int x, int y, int w, int h, u16 outline, u16 fill)
{
	int draw_x = x;
	int draw_y = y;
	int draw_w = w;
	int draw_h = h;
	int stride = w;

	if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
	{
		draw_x = x + 10;
		draw_w = 40;
		draw_h = 40;
		stride = 40;
	}

	Launcher_RestoreHorizontalThumbBox(x, y, w, h, outline, fill);
	Launcher_DrawPicClipStride(src, stride, draw_x, draw_y, draw_w, draw_h);
	Launcher_DrawThumbBorder(draw_x, draw_y, draw_w, draw_h);
}

static void Launcher_DrawHorizontalSelectedPreview(const u16 *src, int src_w, int src_h, int x, int y, int w, int h, u16 outline, u16 fill)
{
	int draw_x = x;
	int draw_y = y;
	int draw_w = w;
	int draw_h = h;

	(void)outline;
	(void)fill;
	if(!src)
		return;

	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x - 1, y - 1, w + 2, 1);
	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x - 1, y + h, w + 2, 1);
	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x - 1, y - 1, 1, h + 2);
	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x + w, y - 1, 1, h + 2);

	if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
	{
		draw_x = x + ((w - 80) / 2);
		draw_w = 80;
		draw_h = 80;
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), x, y, draw_x - x, h);
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), draw_x + draw_w, y, (x + w) - (draw_x + draw_w), h);
	}

	if((src_w == draw_w) && (src_h == draw_h))
		Launcher_DrawPicClipStride(src, src_w, draw_x, draw_y, draw_w, draw_h);
	else
		Launcher_DrawScaledThumbClip(src, src_w, src_h, draw_x, draw_y, draw_w, draw_h);
	Launcher_DrawThumbBorder(draw_x, draw_y, draw_w, draw_h);
}

static void Launcher_DrawIconCenteredClip(const u16 *icon, int box_x, int box_y, int box_w, int box_h)
{
	int icon_x = box_x + ((box_w - 16) / 2);
	int icon_y = box_y + ((box_h - 14) / 2);
	int x, y;
	vu16 *dst_base = (vu16*)VRAM;

	for(y = 0; y < 14; y++)
	{
		int dst_y = icon_y + y;
		if(dst_y < 0 || dst_y >= 160)
			continue;

		for(x = 0; x < 16; x++)
		{
			int dst_x = icon_x + x;
			u16 px;

			if(dst_x < 0 || dst_x >= 240)
				continue;

			px = icon[y * 16 + x];
			if(px != 0x0000)
				dst_base[dst_y * 240 + dst_x] = px;
		}
	}
}

static void Launcher_DrawIconCenteredClip2x(const u16 *icon, int box_x, int box_y, int box_w, int box_h)
{
	int icon_x = box_x + ((box_w - 32) / 2);
	int icon_y = box_y + ((box_h - 28) / 2);
	int x, y, dx, dy;
	vu16 *dst_base = (vu16*)VRAM;

	for(y = 0; y < 14; y++)
	{
		for(x = 0; x < 16; x++)
		{
			u16 px = icon[y * 16 + x];
			if(px == 0x0000)
				continue;

			for(dy = 0; dy < 2; dy++)
			{
				int dst_y = icon_y + y * 2 + dy;
				if(dst_y < 0 || dst_y >= 160)
					continue;
				for(dx = 0; dx < 2; dx++)
				{
					int dst_x = icon_x + x * 2 + dx;
					if(dst_x < 0 || dst_x >= 240)
						continue;
					dst_base[dst_y * 240 + dst_x] = px;
				}
			}
		}
	}
}

static void Launcher_DrawIconCenteredClip3x(const u16 *icon, int box_x, int box_y, int box_w, int box_h)
{
	int icon_x = box_x + ((box_w - 48) / 2);
	int icon_y = box_y + ((box_h - 42) / 2);
	int x, y, dx, dy;
	vu16 *dst_base = (vu16*)VRAM;

	for(y = 0; y < 14; y++)
	{
		for(x = 0; x < 16; x++)
		{
			u16 px = icon[y * 16 + x];
			if(px == 0x0000)
				continue;

			for(dy = 0; dy < 3; dy++)
			{
				int dst_y = icon_y + y * 3 + dy;
				if(dst_y < 0 || dst_y >= 160)
					continue;
				for(dx = 0; dx < 3; dx++)
				{
					int dst_x = icon_x + x * 3 + dx;
					if(dst_x < 0 || dst_x >= 240)
						continue;
					dst_base[dst_y * 240 + dst_x] = px;
				}
			}
		}
	}
}

static char launcher_vertical_folder_label_last[64] = {0};
static int launcher_vertical_folder_label_drawn = 0;
static int launcher_vertical_folder_label_dirty = 1;
static int launcher_vertical_folder_label_last_left = 0;
static int launcher_vertical_folder_label_last_top = 0;
static int launcher_vertical_folder_label_last_w = 0;
static int launcher_vertical_folder_label_last_h = 0;
static int launcher_force_full_redraw = 0;
static PAGE_NUM launcher_active_page = SD_list;

static u32 Launcher_IsNORPage(void)
{
	return (launcher_active_page == NOR_list);
}

static const u16 *Launcher_GetBGImage(void)
{
	if(gl_show_Thumbnail == 1)
		return (const u16*)gImage_SD_HORIZONTAL;
	if(gl_show_Thumbnail == 2)
		return (const u16*)gImage_SD_VERTICAL;
	return (const u16*)gImage_SD_LIST;
}

static u32 Launcher_GetTotalEntries(void)
{
	return Launcher_IsNORPage() ? game_total_NOR : (folder_total + game_total_SD);
}

static const char* Launcher_GetCurrentFolderLabel(void)
{
	const char *last_sep;

	if(Launcher_IsNORPage())
		return DSTEXT_NOR_FLASH;

	if((currentpath[0] == 0) || !strcmp(currentpath, "/"))
		return DSTEXT_SD_CARD;

	last_sep = strrchr(currentpath, '/');
	if(last_sep && last_sep[1])
		return last_sep + 1;

	return currentpath;
}

static void Launcher_MakeEllipsisText(const char *src, char *dst, u32 dst_size, u32 max_chars)
{
	u32 len;

	if(!dst || dst_size == 0)
		return;

	dst[0] = '\0';
	if(!src)
		return;

	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
	len = strlen(dst);
	if(len <= max_chars)
		return;

	if(max_chars <= 3)
	{
		dst[max_chars] = '\0';
		return;
	}

	dst[max_chars - 3] = '.';
	dst[max_chars - 2] = '.';
	dst[max_chars - 1] = '.';
	dst[max_chars] = '\0';
}

static void Launcher_GetVerticalFolderLabelInfo(char *cleaned, int cleaned_size, int *outer_left, int *outer_top, int *outer_w, int *outer_h)
{
	const char *label = Launcher_GetCurrentFolderLabel();
	int len;
	int text_w;
	int box_outer_right = 233;
	int box_y = 24;
	int total_w;

	memset(cleaned, 0, cleaned_size);
	Launcher_CleanTitle(label, cleaned, cleaned_size);

	if(cleaned[0] == 0)
	{
		*outer_left = box_outer_right;
		*outer_top = box_y - 1;
		*outer_w = 0;
		*outer_h = 16;
		return;
	}

	len = strlen(cleaned);
	text_w = len * 6;
	total_w = text_w + 10;
	*outer_left = box_outer_right - total_w + 1;
	if(*outer_left < 0)
		*outer_left = 0;
	*outer_top = box_y - 1;
	*outer_w = box_outer_right - *outer_left + 1;
	*outer_h = 16;
}

static int Launcher_ShouldPreserveVerticalFolderLabel(int *left, int *top, int *w, int *h)
{
	(void)left;
	(void)top;
	(void)w;
	(void)h;
	return 0;
}

static int Launcher_NeedsVerticalFolderLabelRedraw(void)
{
	return 0;
}

static void Launcher_GetLabelBoxColours(u16 *outline, u16 *fill, u16 *text_color)
{
	if(outline)
		*outline = RGB(7, 7, 7);
	if(fill)
		*fill = gl_color_title_fill;
	if(text_color)
		*text_color = gl_color_text;
}

static void Launcher_DrawVerticalFolderLabel(void)
{
	launcher_vertical_folder_label_last[0] = 0;
	launcher_vertical_folder_label_last_left = 0;
	launcher_vertical_folder_label_last_top = 0;
	launcher_vertical_folder_label_last_w = 0;
	launcher_vertical_folder_label_last_h = 0;
	launcher_vertical_folder_label_drawn = 0;
	launcher_vertical_folder_label_dirty = 0;
}

static void Launcher_DrawIconToPanel16(const u16 *icon, u16 *dst, int panel_w, int panel_h)
{
	int icon_x = (panel_w - 16) / 2;
	int icon_y = (panel_h - 14) / 2;
	int x, y;

	for(y = 0; y < 14; y++)
	{
		for(x = 0; x < 16; x++)
		{
			u16 px = icon[y * 16 + x];
			if(px != 0x0000)
				dst[(icon_y + y) * panel_w + (icon_x + x)] = px;
		}
	}
}

static void Launcher_PrepareBGPanel(const u16 *bg, u16 *dst, int dst_w, int dst_h, int screen_x, int screen_y)
{
	int x;
	int y;

	for(y = 0; y < dst_h; y++)
	{
		int sy = screen_y + y;
		for(x = 0; x < dst_w; x++)
		{
			int sx = screen_x + x;
			if((sx >= 0) && (sx < 240) && (sy >= 0) && (sy < 160))
				dst[y * dst_w + x] = bg[sy * 240 + sx];
			else
				dst[y * dst_w + x] = 0;
		}
	}
}

static void Launcher_PrepareSideIconPanel60x40(const u16 *icon, u16 *dst, const u16 *bg, int screen_x, int screen_y)
{
	Launcher_PrepareBGPanel(bg, dst, 60, 40, screen_x, screen_y);
	if(icon)
		Launcher_DrawIconToPanel16(icon, dst, 60, 40);
}

static void Launcher_PrepareSideIconPanel48x32(const u16 *icon, u16 *dst, const u16 *bg, int screen_x, int screen_y)
{
	Launcher_PrepareBGPanel(bg, dst, 48, 32, screen_x, screen_y);
	if(icon)
		Launcher_DrawIconToPanel16(icon, dst, 48, 32);
}

static int Launcher_SplitTitleNarrow(const char *title, char lines[3][32])
{
	int title_len;
	int pos = 0;
	int line_count = 0;
	int i;

	memset(lines, 0, sizeof(char) * 3 * 32);
	title_len = strlen(title);

	while(pos < title_len && line_count < 3)
	{
		int remaining = title_len - pos;
		int max_take = (line_count < 2) ? 14 : 16;
		int take = (remaining > max_take) ? max_take : remaining;
		int split = pos + take;

		if(split < title_len)
		{
			for(i = split; i > pos + 5; i--)
			{
				if(title[i] == ' ')
				{
					split = i;
					break;
				}
			}
		}

		if(split <= pos)
			split = pos + take;

		strncpy(lines[line_count], title + pos, split - pos);
		lines[line_count][split - pos] = '\0';

		while(lines[line_count][0] == ' ')
			memmove(lines[line_count], lines[line_count] + 1, strlen(lines[line_count]));

		pos = split;
		while(title[pos] == ' ')
			pos++;

		line_count++;
	}

	if(pos < title_len && line_count > 0)
	{
		int last = line_count - 1;
		int len = strlen(lines[last]);
		if(len > 16)
			len = 16;
		while(len > 0 && lines[last][len - 1] == ' ')
			len--;
		lines[last][len] = '\0';
		strcat(lines[last], "...");
	}

	if(line_count == 0)
	{
		strcpy(lines[0], " ");
		line_count = 1;
	}

	return line_count;
}

static void Launcher_RestoreBGClip(const u16 *bg, int x, int y, int w, int h)
{
	int x0 = x;
	int y0 = y;
	int x1 = x + w;
	int y1 = y + h;
	int row;
	vu16 *dst_base = (vu16*)0x06000000;

	if(x0 < 0) x0 = 0;
	if(y0 < 0) y0 = 0;
	if(x1 > 240) x1 = 240;
	if(y1 > 160) y1 = 160;

	if((x1 <= x0) || (y1 <= y0))
		return;

	for(row = y0; row < y1; row++)
	{
		dmaCopy((void*)(bg + (row * 240) + x0),
		        (void*)(dst_base + (row * 240) + x0),
		        (x1 - x0) * 2);
	}
}

static const u16 *Launcher_GetFileIcon(const TCHAR *pfilename)
{
	u32 strlen8;

	if(!pfilename)
		return (u16*)gImage_icon_other;

	strlen8 = strlen(pfilename);
	if(strlen8 < 2)
		return (u16*)gImage_icon_other;

	if((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8-3]), "gba"))
		return (u16*)gImage_icon_gba;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "agb"))
		return (u16*)gImage_icon_gba;
	else if((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8-3]), "gbc"))
		return (u16*)gImage_icon_GBC;
	else if((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8-2]), "gb"))
		return (u16*)gImage_icon_GB;
	else if((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8-3]), "nes"))
		return (u16*)gImage_icon_FC;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "bin"))
		return (u16*)gImage_icon_EXE;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "mb"))
		return (u16*)gImage_icon_EXE;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "mbz"))
		return (u16*)gImage_icon_EXE;
	else if ((strlen8 >= 4) && !strcasecmp(&(pfilename[strlen8 - 4]), "mbap"))
		return (u16*)gImage_icon_EXE;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "sms"))
		return (u16*)gImage_icon_SMS;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "gg"))
		return (u16*)gImage_icon_GG;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "sg"))
		return (u16*)gImage_icon_SG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "ngp"))
		return (u16*)gImage_icon_NG;
	else if ((strlen8 >= 4) && !strcasecmp(&(pfilename[strlen8 - 3]), "ngc"))
		return (u16*)gImage_icon_NG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "jpg"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 4) && !strcasecmp(&(pfilename[strlen8 - 4]), "jpeg"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "bmp"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "txt"))
		return (u16*)gImage_icon_TXT;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "esv"))
		return (u16*)gImage_icon_other;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "sv"))
		return (u16*)gImage_icon_SV;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "ws"))
		return (u16*)gImage_icon_WS;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "wsc"))
		return (u16*)gImage_icon_WS;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "col"))
		return (u16*)gImage_icon_CV;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "rom"))
		return (u16*)gImage_icon_MSX;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "pce"))
		return (u16*)gImage_icon_PCE;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "z80"))
		return (u16*)gImage_icon_ZX;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "o2"))
		return (u16*)gImage_icon_o2;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "c8"))
		return (u16*)gImage_icon_chip;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "ch8"))
		return (u16*)gImage_icon_chip;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "min"))
		return (u16*)gImage_icon_pokem;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "dci"))
		return (u16*)gImage_icon_vmu;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "vmi"))
		return (u16*)gImage_icon_vmu;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "mid"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "wav"))
		return (u16*)gImage_icon_wav;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "nsf"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "k3m"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "mod"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "pcx"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "vgm"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "cwz"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "sb"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "ap"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "lz"))
		return (u16*)gImage_icon_IMG;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "bgf"))
		return (u16*)gImage_icon_mod;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "arc"))
		return (u16*)gImage_icon_arc;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "a26"))
		return (u16*)gImage_icon_a26;
	else if ((strlen8 >= 2) && !strcasecmp(&(pfilename[strlen8 - 2]), "sc"))
		return (u16*)gImage_icon_SC3000;
	else if ((strlen8 >= 3) && !strcasecmp(&(pfilename[strlen8 - 3]), "mda"))
		return (u16*)gImage_icon_wav;

	return (u16*)gImage_icon_other;
}

static void Launcher_GetEntryInfo(u32 absolute_index, LauncherEntryInfo *info)
{
	u32 file_index;
	u8 *thumb_data = info->thumb_data;
	memset(info, 0, sizeof(LauncherEntryInfo));
	info->thumb_data = thumb_data;

	if(Launcher_IsNORPage())
	{
		if(absolute_index >= game_total_NOR)
			return;
		info->name = pNorFS[absolute_index].filename;
		return;
	}

	if(absolute_index >= (folder_total + game_total_SD))
		return;

	if(absolute_index < folder_total)
	{
		info->name = pFolder[absolute_index].filename;
		info->is_folder = 1;
		return;
	}

	file_index = absolute_index - folder_total;
	if(file_index >= game_total_SD)
		return;

	info->name = pFilename_buffer[file_index].filename;
}

static void Launcher_ResetThumbCache(void)
{
	launcher_cache_center_index = 0xFFFFFFFF;
	launcher_cache_prev.valid = 0;
	launcher_cache_selected.valid = 0;
	launcher_cache_next.valid = 0;
	launcher_cache_prev.has_thumbnail = 0;
	launcher_cache_selected.has_thumbnail = 0;
	launcher_cache_next.has_thumbnail = 0;
	Launcher_InitScaleMaps();
	memset(launcher_side_preview_left, 0, sizeof(launcher_side_preview_left));
	memset(launcher_side_preview_right, 0, sizeof(launcher_side_preview_right));
	memset(launcher_vert_prev_scaled, 0, sizeof(launcher_vert_prev_scaled));
	memset(launcher_vert_selected_scaled, 0, sizeof(launcher_vert_selected_scaled));
	memset(launcher_vert_next_scaled, 0, sizeof(launcher_vert_next_scaled));
}

static void Launcher_LoadThumbCacheForIndex(LauncherThumbCache *cache, u32 absolute_index)
{
	TCHAR *name = 0;
	TCHAR custom_name[104];

	if(!cache)
		return;

	cache->valid = 1;
	cache->absolute_index = absolute_index;
	cache->has_thumbnail = 0;

	if(Launcher_IsNORPage())
	{
		/* NOR entries do not reliably keep the original SD path/game code,
		   so leave thumbnails disabled here and use the normal NOR icon path. */
		return;
	}

	if(absolute_index >= (folder_total + game_total_SD))
		return;

	if(absolute_index < folder_total)
	{
		name = pFolder[absolute_index].filename;
		if(name)
			cache->has_thumbnail = Launcher_LoadCustomThumbnailByName(name, cache->thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		return;
	}

	if(recents_view_active)
		name = p_recently_play[absolute_index - folder_total];
	else
		name = pFilename_buffer[absolute_index - folder_total].filename;
	if(name)
	{
		u32 len = strlen(name);
		Launcher_CustomThumbFileName(name, custom_name, sizeof(custom_name));
		cache->has_thumbnail = Launcher_LoadCustomThumbnailByName(custom_name, cache->thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		if(!cache->has_thumbnail &&
		   (((len >= 3) && !strcasecmp(&(name[len - 3]), "gba")) ||
		    ((len >= 3) && !strcasecmp(&(name[len - 3]), "agb"))))
			cache->has_thumbnail = Load_ThumbnailEx(name, cache->thumb_data - LAUNCHER_THUMB_BMP_HEADER);
	}
}

static void Launcher_CopyThumbCacheImage(LauncherThumbCache *dst, const LauncherThumbCache *src)
{
	if(!dst || !src)
		return;

	if(dst->thumb_data && src->thumb_data)
		dmaCopy((void*)(src->thumb_data - LAUNCHER_THUMB_BMP_HEADER), (void*)(dst->thumb_data - LAUNCHER_THUMB_BMP_HEADER), Launcher_ThumbnailReadSize());
}

static u32 Launcher_IsGBAFile(const TCHAR *name)
{
	u32 len;
	if(!name)
		return 0;
	len = strlen(name);
	if(len < 3)
		return 0;
	return !strcasecmp(&(name[len - 3]), "gba") || ((len >= 3) && !strcasecmp(&(name[len - 3]), "agb"));
}

static u32 Launcher_ShouldUsePreviewPanel(const LauncherEntryInfo *info)
{
	if(!info || !info->name || Launcher_IsNORPage())
		return 0;
	if(info->has_thumbnail)
		return 1;
	if(info->is_folder)
		return 0;
	return Launcher_IsGBAFile(info->name);
}

static const u16 *Launcher_GetPreviewSourceForEntry(const LauncherEntryInfo *info)
{
	if(!info || !info->name)
		return 0;
	if(info->has_thumbnail)
		return (const u16*)info->thumb_data;
	if(!info->is_folder && Launcher_IsGBAFile(info->name))
		return Launcher_NotFoundImage();
	return 0;
}

static const u16 *Launcher_GetPreviewSourceForAbsoluteIndex(const LauncherThumbCache *cache, u32 absolute_index)
{
	LauncherEntryInfo info;

	memset(&info, 0, sizeof(info));
	info.thumb_data = cache ? cache->thumb_data : 0;
	Launcher_GetEntryInfo(absolute_index, &info);
	if(cache && cache->valid && cache->has_thumbnail)
		info.has_thumbnail = cache->has_thumbnail;
	return Launcher_GetPreviewSourceForEntry(&info);
}

static void Launcher_PreScaleVertCache(void)
{
	const u16 *src;

	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_prev, launcher_cache_prev.absolute_index);
	if(src)
		Launcher_ScaleThumbToBox(src,
		                         (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                         (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                         launcher_vert_prev_scaled, 48, 32);
	else
		memset(launcher_vert_prev_scaled, 0, sizeof(launcher_vert_prev_scaled));

	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_selected, launcher_cache_selected.absolute_index);
	if(src)
		Launcher_ScaleThumbToBox(src,
		                         (launcher_cache_selected.valid && launcher_cache_selected.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                         (launcher_cache_selected.valid && launcher_cache_selected.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                         launcher_vert_selected_scaled, 84, 56);
	else
		memset(launcher_vert_selected_scaled, 0, sizeof(launcher_vert_selected_scaled));

	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_next, launcher_cache_next.absolute_index);
	if(src)
		Launcher_ScaleThumbToBox(src,
		                         (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                         (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                         launcher_vert_next_scaled, 48, 32);
	else
		memset(launcher_vert_next_scaled, 0, sizeof(launcher_vert_next_scaled));
}

static void Launcher_PreScaleHorzCache(void)
{
	const u16 *src;

	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_prev, launcher_cache_prev.absolute_index);
	if(src)
		Launcher_PreScaleHorizontalSidePreview(src,
		                                      (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                                      (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                                      launcher_side_preview_left);
	else
		memset(launcher_side_preview_left, 0, sizeof(launcher_side_preview_left));

	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_next, launcher_cache_next.absolute_index);
	if(src)
		Launcher_PreScaleHorizontalSidePreview(src,
		                                      (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                                      (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                                      launcher_side_preview_right);
	else
		memset(launcher_side_preview_right, 0, sizeof(launcher_side_preview_right));
}

static void Launcher_PreScaleVertPrev(void)
{
	const u16 *src;
	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_prev, launcher_cache_prev.absolute_index);
	if(src)
		Launcher_ScaleThumbToBox(src,
		                         (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                         (launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                         launcher_vert_prev_scaled, 48, 32);
	else
		memset(launcher_vert_prev_scaled, 0, sizeof(launcher_vert_prev_scaled));
}

static void Launcher_PreScaleVertNext(void)
{
	const u16 *src;
	src = Launcher_GetPreviewSourceForAbsoluteIndex(&launcher_cache_next, launcher_cache_next.absolute_index);
	if(src)
		Launcher_ScaleThumbToBox(src,
		                         (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                         (launcher_cache_next.valid && launcher_cache_next.has_thumbnail) ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                         launcher_vert_next_scaled, 48, 32);
	else
		memset(launcher_vert_next_scaled, 0, sizeof(launcher_vert_next_scaled));
}


static void Launcher_InitThumbCache(void)
{
	launcher_cache_prev.thumb_data = pReadCache + 0x14C36;
	launcher_cache_selected.thumb_data = pReadCache + 0x10036;
	launcher_cache_next.thumb_data = pReadCache + 0x19836;
	Launcher_ResetThumbCache();
}

static void Launcher_BuildThumbCache(u32 center_index)
{
	launcher_cache_center_index = center_index;
	Launcher_LoadThumbCacheForIndex(&launcher_cache_selected, center_index);

	launcher_cache_prev.valid = 0;
	launcher_cache_next.valid = 0;

	if(center_index > 0)
		Launcher_LoadThumbCacheForIndex(&launcher_cache_prev, center_index - 1);
	if((center_index + 1) < Launcher_GetTotalEntries())
		Launcher_LoadThumbCacheForIndex(&launcher_cache_next, center_index + 1);

	if(gl_show_Thumbnail == 2)
		Launcher_PreScaleVertCache();
	else if(gl_show_Thumbnail == 1)
		Launcher_PreScaleHorzCache();
}

static void Launcher_ShiftThumbCache(int move, u32 new_center_index)
{
	LauncherThumbCache old_prev = launcher_cache_prev;
	LauncherThumbCache old_selected = launcher_cache_selected;
	LauncherThumbCache old_next = launcher_cache_next;

	if(launcher_cache_center_index == 0xFFFFFFFF)
	{
		Launcher_BuildThumbCache(new_center_index);
		return;
	}

	if(move > 0 && new_center_index == (launcher_cache_center_index + 1))
	{
		launcher_cache_prev.valid = old_selected.valid;
		launcher_cache_prev.absolute_index = old_selected.absolute_index;
		launcher_cache_prev.has_thumbnail = old_selected.has_thumbnail;
		if(old_selected.valid && old_selected.has_thumbnail)
			Launcher_CopyThumbCacheImage(&launcher_cache_prev, &old_selected);

		launcher_cache_selected.valid = old_next.valid;
		launcher_cache_selected.absolute_index = old_next.absolute_index;
		launcher_cache_selected.has_thumbnail = old_next.has_thumbnail;
		if(old_next.valid && old_next.has_thumbnail)
			Launcher_CopyThumbCacheImage(&launcher_cache_selected, &old_next);

		launcher_cache_center_index = new_center_index;
		launcher_cache_next.valid = 0;
		launcher_cache_next.has_thumbnail = 0;
		if((new_center_index + 1) < Launcher_GetTotalEntries())
			Launcher_LoadThumbCacheForIndex(&launcher_cache_next, new_center_index + 1);

		if(gl_show_Thumbnail == 2)
		{
			/* Rebuild all vertical scaled caches to avoid size-mismatched copies
			   between the 48x32 side buffers and the 84x56 selected buffer. */
			Launcher_PreScaleVertCache();
		}
		else if(gl_show_Thumbnail == 1)
		{
			Launcher_PreScaleHorzCache();
		}
		return;
	}

	if(move < 0 && launcher_cache_center_index > 0 && new_center_index == (launcher_cache_center_index - 1))
	{
		launcher_cache_next.valid = old_selected.valid;
		launcher_cache_next.absolute_index = old_selected.absolute_index;
		launcher_cache_next.has_thumbnail = old_selected.has_thumbnail;
		if(old_selected.valid && old_selected.has_thumbnail)
			Launcher_CopyThumbCacheImage(&launcher_cache_next, &old_selected);

		launcher_cache_selected.valid = old_prev.valid;
		launcher_cache_selected.absolute_index = old_prev.absolute_index;
		launcher_cache_selected.has_thumbnail = old_prev.has_thumbnail;
		if(old_prev.valid && old_prev.has_thumbnail)
			Launcher_CopyThumbCacheImage(&launcher_cache_selected, &old_prev);

		launcher_cache_center_index = new_center_index;
		launcher_cache_prev.valid = 0;
		launcher_cache_prev.has_thumbnail = 0;
		if(new_center_index > 0)
			Launcher_LoadThumbCacheForIndex(&launcher_cache_prev, new_center_index - 1);

		if(gl_show_Thumbnail == 2)
		{
			/* Rebuild all vertical scaled caches to avoid size-mismatched copies
			   between the 48x32 side buffers and the 84x56 selected buffer. */
			Launcher_PreScaleVertCache();
		}
		else if(gl_show_Thumbnail == 1)
		{
			Launcher_PreScaleHorzCache();
		}
		return;
	}

	Launcher_BuildThumbCache(new_center_index);
}

static u32 Launcher_ThumbNavRepeatDelay(void)
{
	u32 total;

	if(gl_show_Thumbnail == 1)
		return 2;
	if(gl_show_Thumbnail != 2)
		return 1;

	total = Launcher_GetTotalEntries();
	if(!launcher_cache_selected.valid || !launcher_cache_selected.has_thumbnail)
		return 2;
	if((launcher_cache_center_index == 0) || !launcher_cache_prev.valid || !launcher_cache_prev.has_thumbnail)
		return 2;
	if(((launcher_cache_center_index + 1) >= total) || !launcher_cache_next.valid || !launcher_cache_next.has_thumbnail)
		return 2;

	return 1;
}

static void Draw_ModernLauncher_SD_State(u32 show_offset, u32 file_select, int x_shift)
{
	LauncherEntryInfo selected;
	LauncherEntryInfo prev;
	LauncherEntryInfo next;
	const u16 *selected_icon;
	const u16 *prev_icon;
	const u16 *next_icon;
	char cleaned[128];
	char lines[3][32];
	int line_count;
	int i;
	int thumb_x = LAUNCHER_HORZ_THUMB_X + x_shift;
	int thumb_y = LAUNCHER_HORZ_THUMB_Y;
	int thumb_w = LAUNCHER_HORZ_THUMB_W;
	int thumb_h = LAUNCHER_HORZ_THUMB_H;
	int side_w = LAUNCHER_HORZ_SIDE_W;
	int side_h = LAUNCHER_HORZ_SIDE_H;
	int left_y = LAUNCHER_HORZ_LEFT_Y;
	int right_y = LAUNCHER_HORZ_RIGHT_Y;
	int left_x = LAUNCHER_HORZ_LEFT_X + x_shift;
	int right_x = LAUNCHER_HORZ_RIGHT_X + x_shift;
	int btn_x = LAUNCHER_HORZ_TITLE_X + x_shift;
	int btn_y = LAUNCHER_HORZ_TITLE_Y;
	int btn_w = LAUNCHER_HORZ_TITLE_W;
	int btn_h = LAUNCHER_HORZ_TITLE_H;
	int line_h = 12;
	int text_y;
	int text_x;
	u16 outline;
	u16 panel_fill = gl_color_body_fill;
	u16 title_text_color;

	Launcher_GetLabelBoxColours(&outline, 0, &title_text_color);
	u32 absolute_index = show_offset + file_select;
	u32 total_entries = Launcher_GetTotalEntries();
	u32 prev_use_preview_panel;
	u32 next_use_preview_panel;
	u32 selected_use_preview_panel;

	memset(&selected, 0, sizeof(selected));
	memset(&prev, 0, sizeof(prev));
	memset(&next, 0, sizeof(next));
	selected.thumb_data = pReadCache + 0x10036;
	prev.thumb_data = pReadCache + 0x14C36;
	next.thumb_data = pReadCache + 0x19836;

	if(launcher_cache_center_index != absolute_index)
		Launcher_BuildThumbCache(absolute_index);

	Launcher_GetEntryInfo(absolute_index, &selected);
	if(absolute_index > 0)
		Launcher_GetEntryInfo(absolute_index - 1, &prev);
	if((absolute_index + 1) < total_entries)
		Launcher_GetEntryInfo(absolute_index + 1, &next);

	if(launcher_cache_selected.valid && launcher_cache_selected.absolute_index == absolute_index)
		selected.has_thumbnail = launcher_cache_selected.has_thumbnail;
	if((absolute_index > 0) && launcher_cache_prev.valid && launcher_cache_prev.absolute_index == (absolute_index - 1))
		prev.has_thumbnail = launcher_cache_prev.has_thumbnail;
	if(((absolute_index + 1) < total_entries) && launcher_cache_next.valid && launcher_cache_next.absolute_index == (absolute_index + 1))
		next.has_thumbnail = launcher_cache_next.has_thumbnail;

	if(!selected.name)
		return;

	selected_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (selected.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(selected.name));
	prev_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (prev.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(prev.name));
	next_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (next.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(next.name));
	prev_use_preview_panel = Launcher_ShouldUsePreviewPanel(&prev);
	next_use_preview_panel = Launcher_ShouldUsePreviewPanel(&next);
	selected_use_preview_panel = Launcher_ShouldUsePreviewPanel(&selected);

	memset(cleaned, 0, sizeof(cleaned));
	Launcher_GetDisplayTitleBounded(selected.name, cleaned, sizeof(cleaned));
	line_count = Launcher_SplitTitle(cleaned, lines);

	if(prev.name)
	{
		if(prev_use_preview_panel)
		{
			Launcher_DrawPreparedHorizontalSidePreview(launcher_side_preview_left, left_x, left_y, side_w, side_h, outline, panel_fill);
		}
		else if(prev_icon)
		{
			Launcher_PrepareSideIconPanel60x40(prev_icon, launcher_side_preview_left, (u16*)Launcher_GetBGImage(), left_x, left_y);
			Launcher_DrawPicClipStride(launcher_side_preview_left, 60, left_x, left_y, side_w, side_h);
		}
	}

	if(next.name)
	{
		if(next_use_preview_panel)
		{
			Launcher_DrawPreparedHorizontalSidePreview(launcher_side_preview_right, right_x, right_y, side_w, side_h, outline, panel_fill);
		}
		else if(next_icon)
		{
			Launcher_PrepareSideIconPanel60x40(next_icon, launcher_side_preview_right, (u16*)Launcher_GetBGImage(), right_x, right_y);
			Launcher_DrawPicClipStride(launcher_side_preview_right, 60, right_x, right_y, side_w, side_h);
		}
	}

	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), btn_x - 1, btn_y - 1, btn_w + 2, btn_h + 2);

	if(selected_use_preview_panel)
	{
		const u16 *src = Launcher_GetPreviewSourceForEntry(&selected);
		Launcher_DrawHorizontalSelectedPreview(src,
		                                       selected.has_thumbnail ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                                       selected.has_thumbnail ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                                       thumb_x, thumb_y, thumb_w, thumb_h, outline, panel_fill);
	}
	else if(selected_icon)
	{
		Launcher_DrawIconCenteredClip2x(selected_icon, thumb_x, thumb_y, thumb_w, thumb_h);
	}

	text_y = btn_y + ((btn_h - (line_count * line_h)) / 2);
	if(text_y < btn_y + 2)
		text_y = btn_y + 2;

	for(i = 0; i < line_count; i++)
	{
		int max_chars = strlen(lines[i]);
		if(max_chars > 31)
			max_chars = 31;
		text_x = btn_x + ((btn_w - (strlen(lines[i]) * 6)) / 2);
		if(text_x < btn_x + 4)
			text_x = btn_x + 4;
		DrawHZText12(lines[i], max_chars, text_x, text_y + (i * line_h), title_text_color, 1);
	}

	if(!Launcher_IsNORPage() && !selected.is_folder && Launcher_IsFavouriteSDIndex(absolute_index))
		Launcher_DrawFavouriteHeart(LAUNCHER_HORZ_HEART_X + x_shift, LAUNCHER_HORZ_HEART_Y, gl_color_heart);

}

static void Draw_ModernLauncher_SD(u32 show_offset, u32 file_select, u32 haveThumbnail)
{
	LauncherEntryInfo selected;
	LauncherEntryInfo prev;
	LauncherEntryInfo next;
	const u16 *selected_icon;
	const u16 *prev_icon;
	const u16 *next_icon;
	char cleaned[128];
	char lines[3][32];
	int line_count;
	int i;
	int thumb_x = LAUNCHER_HORZ_THUMB_X;
	int thumb_y = LAUNCHER_HORZ_THUMB_Y;
	int thumb_w = LAUNCHER_HORZ_THUMB_W;
	int thumb_h = LAUNCHER_HORZ_THUMB_H;
	int side_w = LAUNCHER_HORZ_SIDE_W;
	int side_h = LAUNCHER_HORZ_SIDE_H;
	int left_y = LAUNCHER_HORZ_LEFT_Y;
	int right_y = LAUNCHER_HORZ_RIGHT_Y;
	int left_x = LAUNCHER_HORZ_LEFT_X;
	int right_x = LAUNCHER_HORZ_RIGHT_X;
	int btn_x = LAUNCHER_HORZ_TITLE_X;
	int btn_y = LAUNCHER_HORZ_TITLE_Y;
	int btn_w = LAUNCHER_HORZ_TITLE_W;
	int btn_h = LAUNCHER_HORZ_TITLE_H;
	int line_h = 12;
	int text_y;
	int text_x;
	u16 outline;
	u16 panel_fill = gl_color_body_fill;
	u16 title_text_color;

	Launcher_GetLabelBoxColours(&outline, 0, &title_text_color);
	u32 absolute_index = show_offset + file_select;
	u32 total_entries = Launcher_GetTotalEntries();
	u32 prev_use_preview_panel;
	u32 next_use_preview_panel;
	u32 selected_use_preview_panel;

	(void)haveThumbnail;
	memset(&selected, 0, sizeof(selected));
	memset(&prev, 0, sizeof(prev));
	memset(&next, 0, sizeof(next));
	selected.thumb_data = pReadCache + 0x10036;
	prev.thumb_data = pReadCache + 0x14C36;
	next.thumb_data = pReadCache + 0x19836;

	if(launcher_cache_center_index != absolute_index)
		Launcher_BuildThumbCache(absolute_index);

	Launcher_GetEntryInfo(absolute_index, &selected);
	if(absolute_index > 0)
		Launcher_GetEntryInfo(absolute_index - 1, &prev);
	if((absolute_index + 1) < total_entries)
		Launcher_GetEntryInfo(absolute_index + 1, &next);

	if(launcher_cache_selected.valid && launcher_cache_selected.absolute_index == absolute_index)
		selected.has_thumbnail = launcher_cache_selected.has_thumbnail;
	if((absolute_index > 0) && launcher_cache_prev.valid && launcher_cache_prev.absolute_index == (absolute_index - 1))
		prev.has_thumbnail = launcher_cache_prev.has_thumbnail;
	if(((absolute_index + 1) < total_entries) && launcher_cache_next.valid && launcher_cache_next.absolute_index == (absolute_index + 1))
		next.has_thumbnail = launcher_cache_next.has_thumbnail;

	if(!selected.name)
		return;

	selected_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (selected.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(selected.name));
	prev_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (prev.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(prev.name));
	next_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (next.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(next.name));
	prev_use_preview_panel = Launcher_ShouldUsePreviewPanel(&prev);
	next_use_preview_panel = Launcher_ShouldUsePreviewPanel(&next);
	selected_use_preview_panel = Launcher_ShouldUsePreviewPanel(&selected);

	memset(cleaned, 0, sizeof(cleaned));
	Launcher_GetDisplayTitleBounded(selected.name, cleaned, sizeof(cleaned));
	line_count = Launcher_SplitTitle(cleaned, lines);

	if(prev.name)
	{
		if(prev_use_preview_panel)
		{
			Launcher_DrawPreparedHorizontalSidePreview(launcher_side_preview_left, left_x, left_y, side_w, side_h, outline, panel_fill);
		}
		else if(prev_icon)
		{
			Launcher_PrepareSideIconPanel60x40(prev_icon, launcher_side_preview_left, (u16*)Launcher_GetBGImage(), left_x, left_y);
			Launcher_DrawPicClipStride(launcher_side_preview_left, 60, left_x, left_y, side_w, side_h);
		}
	}
	else
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), left_x - 1, left_y - 1, side_w + 2, side_h + 2);
	}

	if(next.name)
	{
		if(next_use_preview_panel)
		{
			Launcher_DrawPreparedHorizontalSidePreview(launcher_side_preview_right, right_x, right_y, side_w, side_h, outline, panel_fill);
		}
		else if(next_icon)
		{
			Launcher_PrepareSideIconPanel60x40(next_icon, launcher_side_preview_right, (u16*)Launcher_GetBGImage(), right_x, right_y);
			Launcher_DrawPicClipStride(launcher_side_preview_right, 60, right_x, right_y, side_w, side_h);
		}
	}
	else
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), right_x - 1, right_y - 1, side_w + 2, side_h + 2);
	}

	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), btn_x - 1, btn_y - 1, btn_w + 2, btn_h + 2);

	if(selected_use_preview_panel)
	{
		const u16 *src = Launcher_GetPreviewSourceForEntry(&selected);
		Launcher_DrawHorizontalSelectedPreview(src,
		                                       selected.has_thumbnail ? Launcher_ThumbnailSourceWidth() : Launcher_NotFoundWidth(),
		                                       selected.has_thumbnail ? Launcher_ThumbnailSourceHeight() : Launcher_NotFoundHeight(),
		                                       thumb_x, thumb_y, thumb_w, thumb_h, outline, panel_fill);
	}
	else if(selected_icon)
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, thumb_h + 2);
		Launcher_DrawIconCenteredClip2x(selected_icon, thumb_x, thumb_y, thumb_w, thumb_h);
	}

	text_y = btn_y + ((btn_h - (line_count * line_h)) / 2);
	if(text_y < btn_y + 2)
		text_y = btn_y + 2;

	for(i = 0; i < line_count; i++)
	{
		int max_chars = strlen(lines[i]);
		if(max_chars > 31)
			max_chars = 31;
		text_x = btn_x + ((btn_w - (strlen(lines[i]) * 6)) / 2);
		if(text_x < btn_x + 4)
			text_x = btn_x + 4;
		DrawHZText12(lines[i], max_chars, text_x, text_y + (i * line_h), title_text_color, 1);
	}

	if(!Launcher_IsNORPage() && !selected.is_folder && Launcher_IsFavouriteSDIndex(absolute_index))
		Launcher_DrawFavouriteHeart(LAUNCHER_HORZ_HEART_X, LAUNCHER_HORZ_HEART_Y, gl_color_heart);
}

static void Draw_ModernLauncher_SD_Vertical_State(u32 show_offset, u32 file_select)
{
	LauncherEntryInfo selected;
	LauncherEntryInfo prev;
	LauncherEntryInfo next;
	const u16 *selected_icon;
	const u16 *prev_icon;
	const u16 *next_icon;
	char cleaned[128];
	char lines[3][32];
	int line_count;
	int i;
	u32 absolute_index = show_offset + file_select;
	u32 total_entries = Launcher_GetTotalEntries();
	int thumb_w = LAUNCHER_VERT_THUMB_W;
	int thumb_h = LAUNCHER_VERT_THUMB_H;
	int thumb_x = LAUNCHER_VERT_THUMB_X;
	int thumb_y = LAUNCHER_VERT_THUMB_Y;
	int prev_w = LAUNCHER_VERT_PREV_W;
	int prev_h = LAUNCHER_VERT_PREV_H;
	int next_w = LAUNCHER_VERT_NEXT_W;
	int next_h = LAUNCHER_VERT_NEXT_H;
	int prev_x = LAUNCHER_VERT_PREV_X;
	int next_x = LAUNCHER_VERT_NEXT_X;
	int prev_y = LAUNCHER_VERT_PREV_Y;
	int next_y = LAUNCHER_VERT_NEXT_Y;
	int btn_x = LAUNCHER_VERT_TITLE_X;
	int btn_y = LAUNCHER_VERT_TITLE_Y;
	int btn_w = LAUNCHER_VERT_TITLE_W;
	int btn_h = LAUNCHER_VERT_TITLE_H;
	int line_h = 12;
	int text_y;
	int text_x;
	u16 title_text_color;

	Launcher_GetLabelBoxColours(0, 0, &title_text_color);
	u32 prev_use_preview_panel;
	u32 next_use_preview_panel;
	u32 selected_use_preview_panel;

	memset(&selected, 0, sizeof(selected));
	memset(&prev, 0, sizeof(prev));
	memset(&next, 0, sizeof(next));
	selected.thumb_data = pReadCache + 0x10036;
	prev.thumb_data = pReadCache + 0x14C36;
	next.thumb_data = pReadCache + 0x19836;

	if(launcher_cache_center_index != absolute_index)
		Launcher_BuildThumbCache(absolute_index);

	Launcher_GetEntryInfo(absolute_index, &selected);
	if(absolute_index > 0)
		Launcher_GetEntryInfo(absolute_index - 1, &prev);
	if((absolute_index + 1) < total_entries)
		Launcher_GetEntryInfo(absolute_index + 1, &next);

	if(launcher_cache_selected.valid && launcher_cache_selected.absolute_index == absolute_index)
		selected.has_thumbnail = launcher_cache_selected.has_thumbnail;
	if((absolute_index > 0) && launcher_cache_prev.valid && launcher_cache_prev.absolute_index == (absolute_index - 1))
		prev.has_thumbnail = launcher_cache_prev.has_thumbnail;
	if(((absolute_index + 1) < total_entries) && launcher_cache_next.valid && launcher_cache_next.absolute_index == (absolute_index + 1))
		next.has_thumbnail = launcher_cache_next.has_thumbnail;

	if(!selected.name)
		return;

	selected_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (selected.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(selected.name));
	prev_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (prev.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(prev.name));
	next_icon = Launcher_IsNORPage() ? (u16*)(gImage_icon_nor) : (next.is_folder ? (u16*)(gImage_icon_folder) : Launcher_GetFileIcon(next.name));
	prev_use_preview_panel = Launcher_ShouldUsePreviewPanel(&prev);
	next_use_preview_panel = Launcher_ShouldUsePreviewPanel(&next);
	selected_use_preview_panel = Launcher_ShouldUsePreviewPanel(&selected);

	/* Do not wipe the entire launcher body on ordinary moves.
	 * Redraw only the rectangles that actually change so the folder label,
	 * title box border and other static elements do not flash.
	 */

	if(prev.name)
	{
		if(prev_use_preview_panel)
		{
			if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
			{
				Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), prev_x - 1, prev_y - 1, prev_w + 2, prev_h + 2);
				Launcher_DrawPicClipStride(launcher_vert_prev_scaled + 8, 48, prev_x + 8, prev_y, 32, 32);
			}
			else
			{
				Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), prev_x - 1, prev_y - 1, prev_w + 2, prev_h + 2);
				Launcher_DrawPicClipStride(launcher_vert_prev_scaled, 48, prev_x, prev_y, prev_w, prev_h);
			}
		}
		else if(prev_icon)
		{
			Launcher_PrepareSideIconPanel48x32(prev_icon, launcher_vert_prev_scaled, (u16*)Launcher_GetBGImage(), prev_x, prev_y);
			Launcher_DrawPicClipStride(launcher_vert_prev_scaled, 48, prev_x, prev_y, prev_w, prev_h);
		}
	}
	else
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), prev_x - 1, prev_y - 1, prev_w + 2, prev_h + 2);
	}

	if(next.name)
	{
		if(next_use_preview_panel)
		{
			if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
			{
				Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), next_x - 1, next_y - 1, next_w + 2, next_h + 2);
				Launcher_DrawPicClipStride(launcher_vert_next_scaled + 8, 48, next_x + 8, next_y, 32, 32);
			}
			else
			{
				Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), next_x - 1, next_y - 1, next_w + 2, next_h + 2);
				Launcher_DrawPicClipStride(launcher_vert_next_scaled, 48, next_x, next_y, next_w, next_h);
			}
		}
		else if(next_icon)
		{
			Launcher_PrepareSideIconPanel48x32(next_icon, launcher_vert_next_scaled, (u16*)Launcher_GetBGImage(), next_x, next_y);
			Launcher_DrawPicClipStride(launcher_vert_next_scaled, 48, next_x, next_y, next_w, next_h);
		}
	}
	else
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), next_x - 1, next_y - 1, next_w + 2, next_h + 2);
	}

	if(selected_use_preview_panel)
	{
		if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
		{
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, thumb_h + 2);
			Launcher_DrawPicClipStride(launcher_vert_selected_scaled + 14, 84, thumb_x + 14, thumb_y, 56, 56);
		}
		else
		{
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, thumb_h + 2);
			Launcher_DrawPicClipStride(launcher_vert_selected_scaled, 84, thumb_x, thumb_y, thumb_w, thumb_h);
		}
	}
	else
	{
		Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, thumb_h + 2);
		if(selected_icon)
		{
			Launcher_DrawIconCenteredClip2x(selected_icon, thumb_x, thumb_y, thumb_w, thumb_h);
		}
	}

	Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), btn_x - 1, btn_y - 1, btn_w + 2, btn_h + 2);

	memset(cleaned, 0, sizeof(cleaned));
	Launcher_CleanTitle(selected.name, cleaned, sizeof(cleaned));
	line_count = Launcher_SplitTitle(cleaned, lines);

	text_y = btn_y + ((btn_h - (line_count * line_h)) / 2);
	if(text_y < btn_y + 2)
		text_y = btn_y + 2;

	for(i = 0; i < line_count; i++)
	{
		int max_chars = strlen(lines[i]);
		if(max_chars > 31)
			max_chars = 31;
		text_x = btn_x + ((btn_w - (strlen(lines[i]) * 6)) / 2);
		if(text_x < btn_x + 4)
			text_x = btn_x + 4;
		DrawHZText12(lines[i], max_chars, text_x, text_y + (i * line_h), title_text_color, 1);
	}

	if(!Launcher_IsNORPage() && !selected.is_folder && Launcher_IsFavouriteSDIndex(absolute_index))
		Launcher_DrawFavouriteHeart(LAUNCHER_VERT_HEART_X, LAUNCHER_VERT_HEART_Y, gl_color_heart);

	if(Launcher_NeedsVerticalFolderLabelRedraw())
		Launcher_DrawVerticalFolderLabel();
}

//---------------------------------------------------------------------------------
//Delete file
u32 SD_list_L_START(u32 show_offset,u32 file_select,u32 folder_total)
{
	//u32 res;	
	DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic		
	Show_MENU_btn();

	char *msg1 = "Delete this file?"; int x1 = (240 - strlen(msg1) * 6) / 2;
	DrawHZText12(msg1, 0, x1, 45, gl_color_text, 1);
	DrawHZText12( pFilename_buffer[show_offset + file_select - folder_total].filename, 20, 60, 60, gl_color_text, 1 );
	
	while(1){
		VBlankIntrWait();
		scanKeys();
		u16 keysdown  = keysDown();
		UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
		if (keysdown & KEY_A) {
			UIAudio_PlayAccept();
			TCHAR *pdelfilename;			
			pdelfilename = pFilename_buffer[show_offset+file_select-folder_total].filename;	
			/*res = */f_unlink(pdelfilename);	
			launcher_force_full_redraw = 1;
			return 1;
		}
		else if(keysdown & KEY_B){
			UIAudio_PlayBack();
			launcher_force_full_redraw = 1;
			return 0;
		}
	}	
}

static void Launcher_CycleViewModeAndRedraw(u32 page_num, u32 show_offset, u32 file_select, u32 *updata)
{
	UIAudio_PlaySfx(UI_SFX_MENU);
	gl_show_Thumbnail++;
	if(gl_show_Thumbnail > 2)
		gl_show_Thumbnail = 0;
	save_set_info_SELECT();
	if(page_num == SD_list)
	{
		Launcher_DrawThemeBGFull(Launcher_GetBGImage());
		launcher_vertical_folder_label_dirty = 1;
		launcher_system_name_dirty = 1;
		if(gl_show_Thumbnail)
			Launcher_BuildThumbCache(show_offset + file_select);
	}
	else if(page_num == NOR_list)
	{
		Launcher_DrawThemeBGFull(Launcher_GetBGImage());
		launcher_vertical_folder_label_dirty = 1;
		launcher_system_name_dirty = 1;
		if(gl_show_Thumbnail)
			Launcher_BuildThumbCache(show_offset + file_select);
	}
	if(updata)
		*updata = 1;
}

static void Launcher_FavouritePromptFullPath(const char *full)
{
	char msg2[80];
	s32 fav_index;

	if(!full || !full[0])
		return;

	Launcher_LoadFavourites();
	fav_index = Launcher_FindFavouriteFullPath(full);
	DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);
	Show_MENU_btn();
	if(fav_index >= 0)
	{
		char *msg1 = "Remove favourite?";
		int x1 = (240 - strlen(msg1) * 6) / 2;
		DrawHZText12(msg1, 0, x1, 45, gl_color_text, 1);
	}
	else
	{
		char *msg1 = "Set game as favourite?";
		int x1 = (240 - strlen(msg1) * 6) / 2;
		DrawHZText12(msg1, 0, x1, 45, gl_color_text, 1);
	}
	(void)msg2;

	while(1)
	{
		VBlankIntrWait();
		VBlankIntrWait();
		scanKeys();
		{
			u16 keysdown = keysDown();
			UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
			if(keysdown & KEY_A)
			{
				UIAudio_PlayAccept();
				if(fav_index >= 0)
				{
					u32 i;
					for(i = (u32)fav_index; i + 1 < launcher_favourite_count; i++)
					{
						memset(Launcher_FavouritesBuffer()[i], 0x00, LAUNCHER_FAVOURITE_PATH_LEN);
						dmaCopy(Launcher_FavouritesBuffer()[i + 1], Launcher_FavouritesBuffer()[i], LAUNCHER_FAVOURITE_PATH_LEN);
					}
					if(launcher_favourite_count)
						launcher_favourite_count--;
					memset(Launcher_FavouritesBuffer()[launcher_favourite_count], 0x00, LAUNCHER_FAVOURITE_PATH_LEN);
					if(launcher_favourite_index >= launcher_favourite_count)
						launcher_favourite_index = 0;
				}
				else
				{
					/* Append new favourites directly. Rewriting the whole favourites file
					   through the shared cache could drop existing entries on this kernel. */
					Launcher_AppendFavouriteFullPath(full);
				}
				if(fav_index >= 0)
					Launcher_SaveFavourites();
				launcher_force_full_redraw = 1;
				break;
			}
			else if(keysdown & KEY_B)
			{
				UIAudio_PlayBack();
				launcher_force_full_redraw = 1;
				break;
			}
		}
	}
}

static void Launcher_FavouritePrompt(u32 show_offset, u32 file_select)
{
	char full[512];
	u32 absolute_index = show_offset + file_select;

	if(!Launcher_GetSDFileFullPath(absolute_index, full, sizeof(full)))
		return;
	Launcher_FavouritePromptFullPath(full);
}

//---------------------------------------------------------------------------------
u32 Check_file_type(TCHAR *pfilename)
{
	u32 res;	
	TCHAR *ext = strrchr(pfilename, '.');
	TCHAR *p;
	

	if (!ext)
		return 0xff;

	ext++;

	snprintf(plugin, sizeof(plugin), "/SYSTEM/PLUG/%s.bin", ext);
	res = f_stat(plugin, NULL);
	if(res == FR_OK)
		return 4;
	snprintf(plugin, sizeof(plugin), "/SYSTEM/PLUG/%s.gba", ext);
	res = f_stat(plugin, NULL);
	if(res == FR_OK)
		return 5;
	snprintf(plugin, sizeof(plugin), "/SYSTEM/PLUG/%s.mb", ext);
	res = f_stat(plugin, NULL);
	if(res == FR_OK)
		return 6;
	snprintf(plugin, sizeof(plugin), "/SYSTEM/PLUG/%s.mbz", ext);
	res = f_stat(plugin, NULL);
	if(res == FR_OK)
		return 7;

	//u32 is_EMU;
	if(!strcasecmp(ext, "gba"))
	{
		return 0;
	}	
	else if(!strcasecmp(ext, "gbc"))
	{
		return 1;
	}
	else if(!strcasecmp(ext, "gb"))
	{
		return 2;
	}
	
	return 0xff;
}
//---------------------------------------------------------------------------------
void Show_error_num(u8 error_num)
{
	char msg[50];

	Launcher_ClearWithThemeBG(Launcher_GetBGImage(),90, 2, 90, 13);
	switch(error_num)
	{
		case 0x0:
			sprintf(msg,"%s",gl_error_0);
			break;
		case 0x1:
			sprintf(msg,"%s",gl_error_1);
			break;
		case 0x2:
			sprintf(msg,"%s",gl_error_2);
			break;
		case 0x3:
			sprintf(msg,"%s",gl_error_3);
			break;
		case 0x4:
			sprintf(msg,"%s",gl_error_4);
			break;
		case 0x5:
			sprintf(msg,"%s",gl_error_5);
			break;
		case 0x6:
			sprintf(msg,"%s",gl_error_6);
			break;
		default:
			sprintf(msg,"%s","error?");
			break;				
	}

	DrawHZText12(msg,0,90,2, RGB(31,00,00),1);
	wait_btn();
}
//---------------------------------------------------------------------------------
u32 Get_savefilesize(BYTE saveMODE)
{
	u32 savefilesize;
	switch(saveMODE)
	{
		case 0x00:savefilesize=0x0;break;//no save
		case 0x11:savefilesize=0x8000;break;//SRAM_TYPE 32k
		case 0x21:savefilesize=0x200;break;//EEPROM_TYPE 512
		case 0x22:savefilesize=0x2000;break;//EEPROM_TYPE 8k	
		case 0x23:savefilesize=0x2000;break;//EEPROM_TYPE v125 v126 must use 8k
		case 0x32:savefilesize=0x10000;break;//FLASH_TYPE 64k
		case 0x33:savefilesize=0x10000;break;//FLASH512_TYPE 64k	
		case 0x31:savefilesize=0x20000;break;//FLASH1M_TYPE 128k
		case 0xee:savefilesize=0x10000;break;//EMU 64k	
		default:	savefilesize=0x10000;break;//UNKNOW,FF  for homebrew SRAM_TYPE	//2018-4-23 some emu homebrew need 64kByte	
	}
	return 	savefilesize;
}
//---------------------------------------------------------------------------------
u8 Process_savefile(u32 is_EMU,TCHAR *pfilename,u32 gamefilesize,BYTE saveMODE)
{
	u32 res;
	u32 savefilesize=0;	
	TCHAR savfilename[100];
	u32 strlen8;
	
	res=f_chdir(SAVER_FOLDER);	
	if(res != FR_OK){
		return 2;
	}
	
	
	// BUG: not all file types are of equal length. sometimes they are two characters such as "gg" or "gb". we need to account for this
	// it was easy to hardcode upstream but since we have more flexibility with pogoshell an elegant solution is needed
	// we do it with strrchr so that we get the last instance of the period and then we append new characters to it.
	
	memcpy(savfilename,pfilename,100);
	savfilename[sizeof(savfilename) - 1] = '\0';
		
	char* last_period = strrchr(savfilename, '.');
	if(!last_period)
	{
		return 3;
	}
	
	if(is_EMU){
		
		strcpy(last_period + 1, "esv");
		
		// if(is_EMU ==2){//gb
		// 	(savfilename)[strlen8-2] = 'e';
		// 	(savfilename)[strlen8-1] = 's';
		// 	(savfilename)[strlen8-0] = 'v';		
		// 	(savfilename)[strlen8+1] = 0;	
		// }		
		// else{
		// 	(savfilename)[strlen8-3] = 'e';
		// 	(savfilename)[strlen8-2] = 's';
		// 	(savfilename)[strlen8-1] = 'v';
		// }	
	}
	else{//gba		
		strcpy(last_period + 1, "sav");
	}
	//#ifdef DEBUG
		//DEBUG_printf("sav %s",savfilename);
		//DEBUG_printf("saveMODE %x",saveMODE);	
		//wait_btn();			
	//#endif
		
	res = f_open(&gfile,savfilename, FA_OPEN_EXISTING);		
	if(res == FR_OK)//have a old save file
	{
		savefilesize = f_size(&gfile);		
		f_close(&gfile);
		if (gl_toggle_backup)
			Backup_savefile(savfilename);				
	}					
	else //make a new one
	{	
		
		ShowbootProgress(gl_make_sav);					
		savefilesize = Get_savefilesize(saveMODE);	
		res = SavefileWrite(savfilename, savefilesize);
		if(res == 0){
			u8 error_num = 5;
			return error_num;
		}
	}
	
	if(savefilesize)
	{			
		Bank_Switching(0);
		res = Loadsavefile(savfilename);	
		
		/*Save_savefile("dump.bin",0x8000);	
		DEBUG_printf("dump finish");
		wait_btn();	*/
		memset(SAV_info_buffer,0x00,sizeof(SAV_info_buffer));
		SAV_info_buffer[0] = 0x11;
		SAV_info_buffer[1] = savefilesize>>9;
		//memcpy(&SAV_info_buffer[2],savfilename,100);
		strcpy((char*)&SAV_info_buffer[2],savfilename);
		Save_sav_info(SAV_info_buffer,0x200);								
	}					

	FAT_table_buffer[0x1F0/4] = gamefilesize;//size
	FAT_table_buffer[0x1F4/4] = DMA_COPY_MODE;  //rom copy to psram
	FAT_table_buffer[0x1F8/4] = (&EZcardFs)->csize;//0x40;  //secort of cluster
	FAT_table_buffer[0x1FC/4] = (saveMODE<<24) | savefilesize;  //save mode and save file size
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0],FAT_table_buffer[1]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[2],FAT_table_buffer[3]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[4],FAT_table_buffer[5]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x200/4],FAT_table_buffer[0x204/4]);		
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x208/4],FAT_table_buffer[0x20C/4]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x1F0/4],FAT_table_buffer[0x1F4/4]);
	//DEBUG_printf(" %08X %08X ", FAT_table_buffer[0x1F8/4],FAT_table_buffer[0x1FC/4]);					

	return 0;
}
//---------------------------------------------------------------------------------
void Check_save_flag(void)
{
	//check save
	u16 readd;
	u32 savefilesize;
	readd = Read_sav_info(0);
	savefilesize = Read_sav_info(1)<<9;
	if(readd==0x11)
	{
			/* The splash/startup PCM may still be active when a NOR/FRAM save
			   recovery screen appears after reset.  Close FIFO DMA before drawing
			   or touching save buffers so it cannot buzz over the save prompt. */
			UIAudio_StopForSharedBufferUse();
			//u32 res;     
			register u32 loopwrite ;
			Launcher_DrawThemeBGFull(Launcher_GetBGImage());
			for(loopwrite=0;loopwrite<0x200/2;loopwrite++)
			{
				((u16*)SAV_info_buffer)[loopwrite] = Read_sav_info(loopwrite+2);
			}				
			DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic		
			

			DrawHZText12(gl_save_sav,0,47,28,gl_color_text,1);//use sure?gl_LSTART_help
			DrawHZText12((TCHAR *)SAV_info_buffer,   20,47,40,gl_color_text,1);//file name
			DrawHZText12((TCHAR *)SAV_info_buffer+20,20,47,52,gl_color_text,1);//file name
			DrawHZText12((TCHAR *)SAV_info_buffer+40,20,47,64,gl_color_text,1);//file name
			//DrawHZText12(gl_formatnor_info,5,60,90,gl_color_text,1);//use sure?
			
			if(gl_auto_save_sel){
					DrawHZText12(gl_save_ing,0,47,88,gl_color_text,1);//use sure?gl_LSTART_help
					f_mkdir(SAVER_FOLDER);//"/SAVER"
					f_chdir(SAVER_FOLDER); 
					Save_savefile((TCHAR *)SAV_info_buffer,savefilesize);			
			}
			else{
				Show_MENU_btn();
				while(1){
					VBlankIntrWait();
					scanKeys();
					u16 keysdown  = keysDown();
					UIAudio_HandleKeys(keysdown, 0);
					if (keysdown & KEY_A) {
						UIAudio_PlayAccept();
						DrawHZText12(gl_save_ing,0,60,88,gl_color_text,1);//use sure?gl_LSTART_help
						f_mkdir(SAVER_FOLDER);
						f_chdir(SAVER_FOLDER);
						Save_savefile((TCHAR *)SAV_info_buffer,savefilesize);
						/*TCHAR bmpfilename[100];	
						u32 bmpnum;
						for(bmpnum=0;bmpnum<1000;bmpnum++)
						{
							f_mkdir(SAVER_FOLDER);
							f_chdir(SAVER_FOLDER);
							sprintf(bmpfilename,"%4lu.sav",bmpnum);
							Save_savefile(bmpfilename,savefilesize);
						}*/
						break;
					}
					else if(keysdown & KEY_B){
						UIAudio_PlayBack();
						break;
					}
				}
			}
			memset(SAV_info_buffer,0x00,sizeof(SAV_info_buffer));//clean flag
			Save_sav_info(SAV_info_buffer,0x200);				

	}
}
//---------------------------------------------------------------------------------
void Set_saveMODE(BYTE saveMODE)
{
	u32 address;
	for(address=0;address < assress_max;address++)
	{
		SET_info_buffer[address] = Read_SET_info(address);
	}
	
	SET_info_buffer[assress_saveMODE] = saveMODE;


	
	//save to nor 
	Launcher_PrepareSettingsFlashWrite();
	Save_SET_info(SET_info_buffer,0x200);
}


#define LAST_LAUNCH_MODE_FILE "/SYSTEM/LASTMODE.TXT"
#define LAST_LAUNCH_MODE_CLEAN 0
#define LAST_LAUNCH_MODE_ADDON 1
#define AUTO_START_KEY_FILE "/SYSTEM/AUTOSTART.TXT"
#define AUTO_START_KEY_SELECT 2
#define AUTO_START_KEY_START  3
#define AUTO_START_KEY_R      8
#define AUTO_START_KEY_L      9

static u32 launcher_last_launch_mode = LAST_LAUNCH_MODE_CLEAN;
static u16 gl_auto_start_key = AUTO_START_KEY_START;

static u32 Read_last_launch_mode(void)
{
	u32 res;
	char buf[16];
	u32 mode = LAST_LAUNCH_MODE_CLEAN;

	memset(buf, 0x00, sizeof(buf));
	if(Launcher_SettingsReadValue("Last launch mode", buf, sizeof(buf)))
	{
		if((buf[0] == '1') || !strcasecmp(buf, "Addon"))
			mode = LAST_LAUNCH_MODE_ADDON;
	}
	else
	{
		res = f_open(&gfile, LAST_LAUNCH_MODE_FILE, FA_READ);
		if(res == FR_OK)
		{
			f_lseek(&gfile, 0x0);
			if(f_gets(buf, sizeof(buf), &gfile) != NULL)
			{
				Trim(buf);
				if(buf[0] == '1')
					mode = LAST_LAUNCH_MODE_ADDON;
				launcher_settings_migration_pending = 1;
			}
			f_close(&gfile);
		}
	}
	launcher_last_launch_mode = mode;
	return mode;
}

static void Save_last_launch_mode(u32 mode)
{
	launcher_last_launch_mode = (mode == LAST_LAUNCH_MODE_ADDON) ? LAST_LAUNCH_MODE_ADDON : LAST_LAUNCH_MODE_CLEAN;
	Launcher_SaveUnifiedSettings();
}

static u32 Read_last_played_entry(TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size)
{
	return Recent_GetPathAt(0, out_path, out_path_size, out_name, out_name_size);
}

static u32 Apply_last_played_selection(u32 *show_offset, u32 *file_select)
{
	u32 i;
	u32 absolute_index;
	if(current_filename[0] == '\0')
		return 0;
	for(i = 0; i < game_total_SD; i++)
	{
		if(strcmp(pFilename_buffer[i].filename, current_filename) == 0)
		{
			absolute_index = folder_total + i;
			if(absolute_index < 10)
			{
				*show_offset = 0;
				*file_select = absolute_index;
			}
			else
			{
				*show_offset = absolute_index - 9;
				*file_select = 9;
			}
			return 1;
		}
	}
	return 0;
}




static void Launcher_SaveSettingsInfo(void)
{
    u32 address;
    u16 led_status;

    for(address = 0; address < assress_max; address++)
        SET_info_buffer[address] = Read_SET_info(address);

    SET_info_buffer[assress_language] = gl_select_lang;
    SET_info_buffer[assress_v_reset] = gl_reset_on;
    SET_info_buffer[assress_v_rts] = gl_rts_on;
    SET_info_buffer[assress_v_sleep] = gl_sleep_on;
    SET_info_buffer[assress_v_cheat] = gl_cheat_on;
    SET_info_buffer[assress_engine_sel] = gl_engine_sel;
    SET_info_buffer[assress_show_Thumbnail] = gl_show_Thumbnail;
    SET_info_buffer[assress_ingame_RTC_open_status] = gl_ingame_RTC_open_status;
    SET_info_buffer[assress_auto_save_sel] = (u16)((gl_resume_last_on << 8) | gl_auto_save_sel);
    SET_info_buffer[assress_ModeB_INIT] = (u16)((gl_boot_mode_pref << 8) | gl_ModeB_init);
    SET_info_buffer[assress_led_open_sel] = gl_led_open_sel;
    SET_info_buffer[assress_Breathing_R] = gl_Breathing_R;
    SET_info_buffer[assress_Breathing_G] = gl_Breathing_G;
    SET_info_buffer[assress_Breathing_B] = gl_Breathing_B;
    SET_info_buffer[assress_SD_R] = gl_SD_R;
    SET_info_buffer[assress_SD_G] = gl_SD_G;
    SET_info_buffer[assress_SD_B] = gl_SD_B;
    SET_info_buffer[assress_toggle_reset] = gl_toggle_reset;
    SET_info_buffer[assress_toggle_backup] = gl_toggle_backup;

    Launcher_PrepareSettingsFlashWrite();
    Save_SET_info(SET_info_buffer, 0x200);

    Set_RTC_status(gl_ingame_RTC_open_status);
    led_status = (gl_led_open_sel << 7) | (gl_Breathing_R << 5) | (gl_Breathing_G << 4) |
                 (gl_Breathing_B << 3) | (gl_SD_R << 2) | (gl_SD_G << 1) | gl_SD_B;
    Set_LED_control(led_status);
}

static void Launcher_SaveHotkeys(const u8 *sleep_keys, const u8 *addon_keys)
{
    u32 address;

    for(address = 0; address < assress_max; address++)
        SET_info_buffer[address] = Read_SET_info(address);

    SET_info_buffer[assress_edit_sleephotkey_0] = sleep_keys[0];
    SET_info_buffer[assress_edit_sleephotkey_1] = sleep_keys[1];
    SET_info_buffer[assress_edit_sleephotkey_2] = sleep_keys[2];
    SET_info_buffer[assress_edit_rtshotkey_0] = addon_keys[0];
    SET_info_buffer[assress_edit_rtshotkey_1] = addon_keys[1];
    SET_info_buffer[assress_edit_rtshotkey_2] = addon_keys[2];

    Launcher_PrepareSettingsFlashWrite();
    Save_SET_info(SET_info_buffer, 0x200);
}

static const char *Launcher_OnOffText(u16 value)
{
    return value ? DSTEXT_ON : DSTEXT_OFF;
}

static const char *Launcher_EngineText(void)
{
    return gl_engine_sel ? DSTEXT_ENGINE_MANUAL : DSTEXT_ENGINE_FAST;
}

static const char *Launcher_ThumbnailText(void)
{
    if(gl_show_Thumbnail == 1)
        return DSTEXT_VIEW_HORIZONTAL;
    if(gl_show_Thumbnail == 2)
        return DSTEXT_VIEW_VERTICAL;
    return DSTEXT_VIEW_LIST;
}

static const char *Launcher_ThumbnailStyleText(void)
{
    return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? DSTEXT_THUMB_BOX : DSTEXT_THUMB_TITLE;
}

static void Launcher_ReadSoundsSetting(void)
{
    char buf[32];

    launcher_sounds_enabled = 1;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Sounds", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Off") || !strcasecmp(buf, "No") ||
           !strcasecmp(buf, DSTEXT_OFF) || (buf[0] == '0'))
            launcher_sounds_enabled = 0;
        if(strcasecmp(buf, "On") && strcasecmp(buf, "Off"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadLanguageSetting(void)
{
    char buf[32];
    u32 i;

    memset(buf, 0, sizeof(buf));
    if(!Launcher_SettingsReadValue("Language", buf, sizeof(buf)))
        return;

    for(i = 0; i < LAUNCHER_LANGUAGE_COUNT; i++)
    {
        if(!strcasecmp(buf, launcher_language_packs[i].name))
        {
            Launcher_ApplyLanguageIndex(i);
            return;
        }
    }
    if(!strcasecmp(buf, "English UK") || !strcasecmp(buf, "English (UK)"))
        Launcher_ApplyLanguageIndex(0);
    else if(!strcasecmp(buf, "Chinese"))
        Launcher_ApplyLanguageIndex(Launcher_LanguageIndexFromStored(0xE2E2));
}

static void Launcher_ReadThumbnailStyle(void)
{
    char buf[32];

    launcher_thumbnail_style = LAUNCHER_THUMB_STYLE_TITLE;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Thumbnails", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Box") || !strcasecmp(buf, DSTEXT_THUMB_BOX) || (buf[0] == '1'))
            launcher_thumbnail_style = LAUNCHER_THUMB_STYLE_BOX;
        else
            launcher_thumbnail_style = LAUNCHER_THUMB_STYLE_TITLE;
        if(strcasecmp(buf, "Title") && strcasecmp(buf, "Box"))
            launcher_settings_migration_pending = 1;
    }
    else
    {
        launcher_settings_migration_pending = 1;
    }
}

static const char *Launcher_BootModeText(void)
{
    if(gl_boot_mode_pref == 0x1)
        return DSTEXT_BOOT_CLEAN;
    if(gl_boot_mode_pref == 0x2)
        return DSTEXT_BOOT_ADDON;
    return DSTEXT_BOOT_MENU;
}

static const char *Launcher_ModeBText(void)
{
    if(gl_ModeB_init == 0x0)
        return DSTEXT_MODE_RUMBLE;
    if(gl_ModeB_init == 0x1)
        return DSTEXT_MODE_RAM;
    return DSTEXT_MODE_LINK;
}

typedef enum
{
    SETTINGS_TIME_SETTINGS = 0,
    SETTINGS_VIEW_MODE,
    SETTINGS_THUMBNAILS,
    SETTINGS_SOUNDS,
    SETTINGS_LANGUAGE,
    SETTINGS_THEME_MODE,
    SETTINGS_THEME,
    SETTINGS_BOOT_ENGINE,
    SETTINGS_AUTO_SAVE,
    SETTINGS_RESUME_LAST,
    SETTINGS_START_ENABLED,
    SETTINGS_START_SOURCE,
    SETTINGS_AUTO_START,
    SETTINGS_ADDON_SETTINGS,
    SETTINGS_BOOT_MODE,
    SETTINGS_MODE_B,
    SETTINGS_INGAME_RTC,
    SETTINGS_SLEEP_HOTKEY,
    SETTINGS_ADDON_HOTKEY,
    SETTINGS_FULL_INTRO,
    SETTINGS_BACKUP_SAVES,
    SETTINGS_LED_SETTINGS,
    SETTINGS_HELP,
    SETTINGS_TOTAL
} LauncherSettingsItem;

static void Launcher_SettingsGetLine(u32 item, char *out, u32 out_size)
{
    const char *label = "";
    const char *value = "";
    char label_short[48];
    u32 used;
    u32 spaces;

    switch(item)
    {
        case SETTINGS_TIME_SETTINGS: label = DSTEXT_SETTINGS_TIME; value = ">"; break;
        case SETTINGS_VIEW_MODE: label = DSTEXT_SETTINGS_VIEW_MODE; value = Launcher_ThumbnailText(); break;
        case SETTINGS_THUMBNAILS: label = DSTEXT_SETTINGS_THUMBNAILS; value = Launcher_ThumbnailStyleText(); break;
        case SETTINGS_SOUNDS: label = DSTEXT_SETTINGS_SOUNDS; value = Launcher_OnOffText(launcher_sounds_enabled); break;
        case SETTINGS_LANGUAGE: label = DSTEXT_SETTINGS_LANGUAGE; value = Launcher_LanguageName(); break;
        case SETTINGS_THEME_MODE: label = DSTEXT_SETTINGS_THEME; value = Launcher_ThemeModeName(); break;
        case SETTINGS_THEME: label = DSTEXT_SETTINGS_COLOUR; value = Launcher_ThemeName(); break;
        case SETTINGS_BOOT_ENGINE: label = DSTEXT_SETTINGS_BOOT_ENGINE; value = Launcher_EngineText(); break;
        case SETTINGS_AUTO_SAVE: label = DSTEXT_SETTINGS_AUTO_SAVE; value = Launcher_OnOffText(gl_auto_save_sel); break;
        case SETTINGS_RESUME_LAST: label = DSTEXT_SETTINGS_RESUME_LAST; value = Launcher_OnOffText(gl_resume_last_on); break;
        case SETTINGS_START_ENABLED: label = DSTEXT_SETTINGS_START_SCREEN; value = Launcher_StartEnabledText(); break;
        case SETTINGS_START_SOURCE: label = DSTEXT_SETTINGS_START_SCREEN; value = Launcher_StartSourceText(); break;
        case SETTINGS_AUTO_START: label = DSTEXT_SETTINGS_QUICK_START; value = Launcher_AutoStartText(); break;
        case SETTINGS_ADDON_SETTINGS: label = DSTEXT_SETTINGS_ADDON; value = ">"; break;
        case SETTINGS_BOOT_MODE: label = DSTEXT_SETTINGS_BOOT_MODE; value = Launcher_BootModeText(); break;
        case SETTINGS_MODE_B: label = DSTEXT_SETTINGS_MODE_B; value = Launcher_ModeBText(); break;
        case SETTINGS_INGAME_RTC: label = DSTEXT_SETTINGS_INGAME_RTC; value = Launcher_OnOffText(gl_ingame_RTC_open_status); break;
        case SETTINGS_SLEEP_HOTKEY: label = DSTEXT_SETTINGS_SLEEP_HOTKEY; value = ">"; break;
        case SETTINGS_ADDON_HOTKEY: label = DSTEXT_SETTINGS_ADDON_HOTKEY; value = ">"; break;
        case SETTINGS_FULL_INTRO: label = DSTEXT_SETTINGS_ENABLE_BIOS; value = Launcher_OnOffText(gl_toggle_reset); break;
        case SETTINGS_BACKUP_SAVES: label = DSTEXT_SETTINGS_BACKUP_SAVES; value = Launcher_OnOffText(gl_toggle_backup); break;
        case SETTINGS_LED_SETTINGS: label = DSTEXT_SETTINGS_LED; value = ">"; break;
        case SETTINGS_HELP: label = DSTEXT_SETTINGS_HELP; value = ">"; break;
        default: break;
    }

    if(out_size == 0)
        return;

    DrawText12CopyVisible(label_short, sizeof(label_short), (char*)label, 14);
    snprintf(out, out_size, "%s", label_short);
    used = strlen(out);
    spaces = DrawText12VisibleLength(label_short);
    spaces = (spaces < 16) ? (16 - spaces) : 1;
    while(spaces && (used + 1) < out_size)
    {
        out[used++] = ' ';
        spaces--;
    }
    out[used] = 0;
    if(used < out_size)
        snprintf(out + used, out_size - used, "%s", value);
}

static void Launcher_SettingsDrawRow(u32 item, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
{
    char msg[64];
    const u32 visible = 9;
    const u32 y0 = 24;
    const u32 line_h = 14;
    u32 row;
    u32 y;

    if(item < top || item >= top + visible)
        return;

    row = item - top;
    y = y0 + row * line_h;
    /* Keep the row restore clear away from the scroll-arrow column.
       The top arrow lives on the first row; if this clear reaches it,
       it visibly flickers during page scrolling.  The value highlight still
       ends at x=224, so restoring through x=224 is enough to remove trails. */
    Launcher_ClearWithThemeBG((const u16*)gImage_SET, 17, y, 208, 13);
    get_line(item, msg, sizeof(msg));
    if(item == selected)
    {
        char label_part[64];
        const char *value_part = msg;
        u16 value_offset;

        /* The SET background has a baked-in value box on the right.  Only
           highlight that value/change area, leaving the setting label on the
           left untouched. */
        Clear(112, y, 112, 13, gl_color_selectBG_sd, 1);

        memset(label_part, 0, sizeof(label_part));
        value_offset = DrawText12ByteOffsetForGlyphs(msg, 16);
        if(value_offset >= sizeof(label_part))
            value_offset = sizeof(label_part) - 1;
        memcpy(label_part, msg, value_offset);
        label_part[value_offset] = 0;
        if(strlen(msg) > value_offset)
            value_part = msg + value_offset;

        DrawHZText12(label_part, 32, 23, y, gl_color_text, 1);
        DrawHZText12((TCHAR*)value_part, 32, 119, y, LAUNCHER_SELECTED_TEXT, 1);
    }
    else
    {
        DrawHZText12(msg, 32, 23, y, gl_color_text, 1);
    }
}

static void Launcher_SettingsDrawRowValueOnly(u32 item, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
{
    char msg[64];
    const char *value_part = msg;
    const u32 visible = 9;
    const u32 y0 = 24;
    const u32 line_h = 14;
    u32 row;
    u32 y;
    u16 value_offset;

    if(item < top || item >= top + visible)
        return;

    row = item - top;
    y = y0 + row * line_h;
    get_line(item, msg, sizeof(msg));
    value_offset = DrawText12ByteOffsetForGlyphs(msg, 16);
    if(strlen(msg) > value_offset)
        value_part = msg + value_offset;

    if(item == selected)
    {
        Clear(112, y, 112, 13, gl_color_selectBG_sd, 1);
        DrawHZText12((TCHAR*)value_part, 32, 119, y, LAUNCHER_SELECTED_TEXT, 1);
    }
    else
    {
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, 112, y, 112, 13);
        DrawHZText12((TCHAR*)value_part, 32, 119, y, gl_color_text, 1);
    }
}

static void Launcher_DrawSettingsClock(u32 force);

static void Launcher_SettingsDrawArrows(u32 total, u32 top)
{
    const u32 visible = 9;
    const u32 arrow_x = 230;

    u32 show_top = (top > 0);
    u32 show_bottom = (top + visible < total);

    /* Do not clear an arrow that is still meant to be visible.  Clearing and
       then redrawing it every time the settings list scrolls causes the top
       arrow to flicker.  Only restore the baked background when an arrow is
       currently hidden, but still draw visible arrows after the rows so any
       row restore that touches the arrow area is repaired. */
    if(!show_top)
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, arrow_x - 4, 24, 14, 14);
    if(!show_bottom)
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, arrow_x - 4, 136, 14, 15);

    if(show_top)
        DrawHZText12("^", 0, arrow_x, 25, gl_color_text, 1);
    if(show_bottom)
        DrawHZText12("v", 0, arrow_x, 137, gl_color_text, 1);
}

static void Launcher_SettingsDrawRowsOnly(u32 total, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
{
    u32 i;
    const u32 visible = 9;

    for(i = 0; i < visible && (top + i) < total; i++)
        Launcher_SettingsDrawRow(top + i, selected, top, get_line);

    Launcher_SettingsDrawArrows(total, top);
}

static void Launcher_SettingsDrawList(const char *title, u32 total, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
{
    Launcher_DrawThemeBGFull((const u16*)gImage_SET);
    Launcher_DrawTopbarName(SET_win);
    Launcher_DrawTopbarTitle(SET_win, title);
    Launcher_DrawSettingsClock(1);

    Launcher_SettingsDrawRowsOnly(total, selected, top, get_line);
}

static void Launcher_SettingsDrawPopupEx(const char *title, u32 total, u32 selected, u32 top, void (*get_line)(u32,char*,u32), u32 row_y0)
{
    u32 i;
    char msg[40];
    const u32 visible = 7;
    const u32 x = 36;
    const u32 y = 25;
    const u32 w = 168;
    const u32 h = 110;
    const u32 line_h = 12;

    DrawPic((u16*)gImage_MENU, x, y, w, h, 1, 0, 1);
    DrawHZText12((TCHAR*)title, 0, x + (w - DrawText12VisibleLength((char*)title) * 6) / 2, y + 7, gl_color_text, 1);

    if(top > 0)
        DrawHZText12("^", 0, x + w - 17, y + 20, gl_color_text, 1);
    if(top + visible < total)
        DrawHZText12("v", 0, x + w - 17, y + h - 17, gl_color_text, 1);

    for(i = 0; i < visible && (top + i) < total; i++)
    {
        u32 item = top + i;
        u32 yy = row_y0 + i * line_h;
        get_line(item, msg, sizeof(msg));
        if(item == selected)
            Clear(x + 12, yy, w - 24, 11, gl_color_selectBG_sd, 1);
        DrawHZText12(msg, 32, x + 18, yy, (item == selected) ? LAUNCHER_SELECTED_TEXT : gl_color_text, 1);
    }
}

static void Launcher_SettingsDrawPopup(const char *title, u32 total, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
{
    Launcher_SettingsDrawPopupEx(title, total, selected, top, get_line, 50);
}


static void Launcher_SettingsRestorePopupArea(u32 x, u32 y, u32 w, u32 h)
{
    const u32 popup_x = 36;
    const u32 popup_y = 25;
    const u32 popup_w = 168;
    const u32 popup_h = 110;
    u32 src_x, src_y;

    if(x < popup_x)
    {
        u32 d = popup_x - x;
        if(d >= w) return;
        x += d;
        w -= d;
    }
    if(y < popup_y)
    {
        u32 d = popup_y - y;
        if(d >= h) return;
        y += d;
        h -= d;
    }
    if(x + w > popup_x + popup_w)
        w = popup_x + popup_w - x;
    if(y + h > popup_y + popup_h)
        h = popup_y + popup_h - y;
    if(w == 0 || h == 0)
        return;

    src_x = x - popup_x;
    src_y = y - popup_y;
    Launcher_DrawPicClipStride(((u16*)gImage_MENU) + src_y * popup_w + src_x, popup_w, x, y, w, h);
}

static void Launcher_SettingsDrawPopupRowEx(u32 item, u32 selected, u32 top, void (*get_line)(u32,char*,u32), u32 row_y0)
{
    char msg[40];
    const u32 visible = 7;
    const u32 x = 36;
    const u32 w = 168;
    const u32 line_h = 12;
    u32 i;
    u32 yy;

    if(item < top || item >= top + visible)
        return;

    i = item - top;
    yy = row_y0 + i * line_h;
    Launcher_SettingsRestorePopupArea(x + 12, yy, w - 24, 12);
    get_line(item, msg, sizeof(msg));
    if(item == selected)
        Clear(x + 12, yy, w - 24, 11, gl_color_selectBG_sd, 1);
    DrawHZText12(msg, 32, x + 18, yy, (item == selected) ? LAUNCHER_SELECTED_TEXT : gl_color_text, 1);
}

static void Launcher_ViewModeCycle(int dir)
{
    if(dir < 0)
        gl_show_Thumbnail = (gl_show_Thumbnail == 0) ? 2 : (gl_show_Thumbnail - 1);
    else
    {
        gl_show_Thumbnail++;
        if(gl_show_Thumbnail > 2)
            gl_show_Thumbnail = 0;
    }
}

static void Launcher_CycleThumbnailStyle(int dir)
{
    (void)dir;
    launcher_thumbnail_style = (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? LAUNCHER_THUMB_STYLE_TITLE : LAUNCHER_THUMB_STYLE_BOX;
    Launcher_ResetThumbCache();
    Launcher_StartPreviewCacheInvalidate();
    Launcher_SaveUnifiedSettings();
}

static void Launcher_BootModeCycle(int dir)
{
    if(dir < 0)
        gl_boot_mode_pref = (gl_boot_mode_pref == 0) ? 2 : (gl_boot_mode_pref - 1);
    else
    {
        gl_boot_mode_pref++;
        if(gl_boot_mode_pref > 2)
            gl_boot_mode_pref = 0;
    }
}

static void Launcher_ModeBCycle(int dir)
{
    if(dir < 0)
        gl_ModeB_init = (gl_ModeB_init == 0) ? 2 : (gl_ModeB_init - 1);
    else
    {
        gl_ModeB_init++;
        if(gl_ModeB_init > 2)
            gl_ModeB_init = 0;
    }
}

typedef enum
{
    ADDON_SETTING_RESET = 0,
    ADDON_SETTING_RTS,
    ADDON_SETTING_SLEEP,
    ADDON_SETTING_CHEAT,
    ADDON_SETTING_TOTAL
} LauncherAddonSettingItem;

static void Launcher_AddonSettingsGetLine(u32 item, char *out, u32 out_size)
{
    const char *label = "";
    const char *value = "";

    switch(item)
    {
        case ADDON_SETTING_RESET: label = DSTEXT_ADDON_RESET; value = Launcher_OnOffText(gl_reset_on); break;
        case ADDON_SETTING_RTS: label = DSTEXT_ADDON_RTS; value = Launcher_OnOffText(gl_rts_on); break;
        case ADDON_SETTING_SLEEP: label = DSTEXT_ADDON_SLEEP; value = Launcher_OnOffText(gl_sleep_on); break;
        case ADDON_SETTING_CHEAT: label = DSTEXT_ADDON_CHEAT; value = Launcher_OnOffText(gl_cheat_on); break;
        default: break;
    }

    snprintf(out, out_size, "%-11s %s", label, value);
}

static void Launcher_AddonSettingsToggle(u32 item)
{
    switch(item)
    {
        case ADDON_SETTING_RESET: gl_reset_on ^= 1; break;
        case ADDON_SETTING_RTS: gl_rts_on ^= 1; break;
        case ADDON_SETTING_SLEEP: gl_sleep_on ^= 1; break;
        case ADDON_SETTING_CHEAT: gl_cheat_on ^= 1; break;
        default: break;
    }
    Launcher_SaveSettingsInfo();
}

typedef enum
{
    LED_SETTING_MASTER = 0,
    LED_SETTING_BREATH_RED,
    LED_SETTING_BREATH_GREEN,
    LED_SETTING_BREATH_BLUE,
    LED_SETTING_SD_RED,
    LED_SETTING_SD_GREEN,
    LED_SETTING_SD_BLUE,
    LED_SETTING_TOTAL
} LauncherLedSettingItem;

static void Launcher_LedSettingsGetLine(u32 item, char *out, u32 out_size)
{
    const char *label = "";
    const char *value = "";

    switch(item)
    {
        case LED_SETTING_MASTER: label = DSTEXT_LED_MASTER; value = Launcher_OnOffText(gl_led_open_sel); break;
        case LED_SETTING_BREATH_RED: label = DSTEXT_LED_BREATH_RED; value = Launcher_OnOffText(gl_Breathing_R); break;
        case LED_SETTING_BREATH_GREEN: label = DSTEXT_LED_BREATH_GREEN; value = Launcher_OnOffText(gl_Breathing_G); break;
        case LED_SETTING_BREATH_BLUE: label = DSTEXT_LED_BREATH_BLUE; value = Launcher_OnOffText(gl_Breathing_B); break;
        case LED_SETTING_SD_RED: label = DSTEXT_LED_SD_RED; value = Launcher_OnOffText(gl_SD_R); break;
        case LED_SETTING_SD_GREEN: label = DSTEXT_LED_SD_GREEN; value = Launcher_OnOffText(gl_SD_G); break;
        case LED_SETTING_SD_BLUE: label = DSTEXT_LED_SD_BLUE; value = Launcher_OnOffText(gl_SD_B); break;
        default: break;
    }

    snprintf(out, out_size, "%-11s %s", label, value);
}

static void Launcher_LedSettingsToggle(u32 item)
{
    switch(item)
    {
        case LED_SETTING_MASTER: gl_led_open_sel ^= 1; break;
        case LED_SETTING_BREATH_RED: gl_Breathing_R ^= 1; break;
        case LED_SETTING_BREATH_GREEN: gl_Breathing_G ^= 1; break;
        case LED_SETTING_BREATH_BLUE: gl_Breathing_B ^= 1; break;
        case LED_SETTING_SD_RED: gl_SD_R ^= 1; break;
        case LED_SETTING_SD_GREEN: gl_SD_G ^= 1; break;
        case LED_SETTING_SD_BLUE: gl_SD_B ^= 1; break;
        default: break;
    }
    Launcher_SaveSettingsInfo();
}

static void Launcher_SettingsPopupToggleEx(const char *title, u32 total, void (*get_line)(u32,char*,u32), void (*toggle_item)(u32), u32 row_y0)
{
    const u32 visible = 7;
    u32 selected = 0;
    u32 top = 0;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        if(scroll_delay > 0)
            scroll_delay--;
        if(dirty)
        {
            Launcher_SettingsDrawPopupEx(title, total, selected, top, get_line, row_y0);
            dirty = 0;
        }
        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();

            if(keysdown & KEY_B)
            {
                UIAudio_PlayBack();
                return;
            }
            if((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && scroll_delay == 0))
            {
                if(selected + 1 < total)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
                    selected++;
                    if(selected >= top + visible)
                        top = selected - visible + 1;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, get_line, row_y0);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, get_line, row_y0);
                    }
                    else
                        dirty = 1;
                }
            }
            else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && scroll_delay == 0))
            {
                if(selected > 0)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
                    selected--;
                    if(selected < top)
                        top = selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, get_line, row_y0);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, get_line, row_y0);
                    }
                    else
                        dirty = 1;
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT | KEY_LEFT | KEY_START))
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                toggle_item(selected);
                Launcher_SettingsDrawPopupRowEx(selected, selected, top, get_line, row_y0);
            }
        }
    }
}

static void Launcher_SettingsPopupToggle(const char *title, u32 total, void (*get_line)(u32,char*,u32), void (*toggle_item)(u32))
{
    Launcher_SettingsPopupToggleEx(title, total, get_line, toggle_item, 50);
}

#define LAUNCHER_KEY_A      0
#define LAUNCHER_KEY_B      1
#define LAUNCHER_KEY_SELECT 2
#define LAUNCHER_KEY_START  3
#define LAUNCHER_KEY_RIGHT  4
#define LAUNCHER_KEY_LEFT   5
#define LAUNCHER_KEY_UP     6
#define LAUNCHER_KEY_DOWN   7
#define LAUNCHER_KEY_R      8
#define LAUNCHER_KEY_L      9

static const char *Launcher_KeyName(u8 key)
{
    switch(key)
    {
        case LAUNCHER_KEY_A: return "A";
        case LAUNCHER_KEY_B: return "B";
        case LAUNCHER_KEY_SELECT: return "Select";
        case LAUNCHER_KEY_START: return "Start";
        case LAUNCHER_KEY_RIGHT: return "Right";
        case LAUNCHER_KEY_LEFT: return "Left";
        case LAUNCHER_KEY_UP: return "Up";
        case LAUNCHER_KEY_DOWN: return "Down";
        case LAUNCHER_KEY_R: return "R";
        case LAUNCHER_KEY_L: return "L";
        default: return "L";
    }
}

static u16 Launcher_KeyFromNameOrNumber(const char *text)
{
    if(!text || !text[0])
        return LAUNCHER_KEY_START;
    if(!strcasecmp(text, "A"))
        return LAUNCHER_KEY_A;
    if(!strcasecmp(text, "B"))
        return LAUNCHER_KEY_B;
    if(!strcasecmp(text, "Select"))
        return LAUNCHER_KEY_SELECT;
    if(!strcasecmp(text, "Start"))
        return LAUNCHER_KEY_START;
    if(!strcasecmp(text, "L"))
        return LAUNCHER_KEY_L;
    return (u16)atoi(text);
}

static void Launcher_SaveUnifiedSettings(void)
{
    FIL f;

    f_mkdir("/SYSTEM");
    if(f_open(&f, SETTINGS_FILE, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
    {
        f_printf(&f, "# DS Style settings\n");
        f_printf(&f, "# Edit the value after '='. Unknown lines are ignored.\n\n");
        f_printf(&f, "# Options: Light, Dark");
#if LAUNCHER_CUSTOM_THEME_ENABLED
        f_printf(&f, ", Custom");
#endif
        f_printf(&f, "\n");
        f_printf(&f, "# Theme controls which full-screen background set the launcher uses.\n");
        f_printf(&f, "Theme = %s\n", Launcher_ThemeModeName());
        f_printf(&f, "\n# Options: Pale Blue, Light Blue, Blue, Dark Blue, Green, Pale Green, Bright Green, Lime, Yellow, Red, Orange, Brown, Pink, Pale Pink, Magenta, Purple");
#if LAUNCHER_THEME_COUNT > 16
        f_printf(&f, ", Custom");
#endif
        f_printf(&f, "\n");
        f_printf(&f, "# Colour controls the top bar, icons, and selection colour.\n");
        f_printf(&f, "Colour = %s\n", Launcher_ThemeName());
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "Start screen = %s\n", Launcher_StartEnabledSettingName());
        f_printf(&f, "\n# Options: Last played, Favourites\n");
        f_printf(&f, "Start screen source = %s\n", Launcher_StartSourceSettingName());
        f_printf(&f, "\n# Options: Title, Box\n");
        f_printf(&f, "# Title uses /SYSTEM/IMGS thumbnails. Box uses /SYSTEM/IMGS2 thumbnails.\n");
        f_printf(&f, "Thumbnails = %s\n",
                 (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? "Box" : "Title");
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "# Sounds controls launcher UI button sounds after startup.\n");
        f_printf(&f, "Sounds = %s\n", launcher_sounds_enabled ? "On" : "Off");
        f_printf(&f, "\n# Options: English (UK), English (US), Espa\303\261ol, Fran\303\247ais, Portugu\303\252s, Deutsch, T\303\274rk\303\247e, Italiano, Nederlands, Svenska, Suomi, Chinese\n");
        f_printf(&f, "Language = %s\n", Launcher_LanguageName());
        f_printf(&f, "\n# Options: Start, Select, L, A, B\n");
        f_printf(&f, "Quick start hotkey = %s\n", Launcher_KeyName((u8)gl_auto_start_key));
        f_printf(&f, "\n# Options: Clean, Addon\n");
        f_printf(&f, "Last launch mode = %s\n", (launcher_last_launch_mode == LAST_LAUNCH_MODE_ADDON) ? "Addon" : "Clean");
        f_printf(&f, "\n# Options: 0 or higher. 0 is the first favourite.\n");
        f_printf(&f, "Favourite index = %lu\n", launcher_favourite_index);
        f_close(&f);
        f_unlink(THEME_FILE);
        f_unlink(FAVOURITE_INDEX_FILE);
        f_unlink(START_SOURCE_FILE);
        f_unlink(LAST_LAUNCH_MODE_FILE);
        f_unlink(AUTO_START_KEY_FILE);
        launcher_settings_migration_pending = 0;
    }
}

static void Launcher_SaveMigratedSettingsIfNeeded(void)
{
    if(launcher_settings_migration_pending)
        Launcher_SaveUnifiedSettings();
}

static u32 Launcher_IsValidAutoStartKey(u16 key)
{
    return (key == LAUNCHER_KEY_START) || (key == LAUNCHER_KEY_SELECT) ||
           (key == LAUNCHER_KEY_L) || (key == LAUNCHER_KEY_A) ||
           (key == LAUNCHER_KEY_B);
}

static u16 Launcher_AutoStartKeyMask(void)
{
    switch(gl_auto_start_key)
    {
        case LAUNCHER_KEY_A: return KEY_A;
        case LAUNCHER_KEY_B: return KEY_B;
        case LAUNCHER_KEY_SELECT: return KEY_SELECT;
        case LAUNCHER_KEY_L: return KEY_L;
        case LAUNCHER_KEY_START:
        default: return KEY_START;
    }
}

static void Launcher_ReadAutoStartKey(void)
{
    FIL f;
    char buf[32];
    gl_auto_start_key = LAUNCHER_KEY_START;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Quick start hotkey", buf, sizeof(buf)))
    {
        gl_auto_start_key = Launcher_KeyFromNameOrNumber(buf);
    }
    else if(f_open(&f, AUTO_START_KEY_FILE, FA_READ) == FR_OK)
    {
        if(f_gets(buf, sizeof(buf), &f) != NULL)
        {
            Trim(buf);
            gl_auto_start_key = (u16)atoi(buf);
            launcher_settings_migration_pending = 1;
        }
        f_close(&f);
    }
    if(!Launcher_IsValidAutoStartKey(gl_auto_start_key))
        gl_auto_start_key = LAUNCHER_KEY_START;
}

static void Launcher_SaveAutoStartKey(void)
{
    f_mkdir("/SYSTEM");
    Launcher_SaveUnifiedSettings();
}

static const char *Launcher_AutoStartText(void)
{
    return Launcher_KeyName((u8)gl_auto_start_key);
}

static void Launcher_CycleAutoStartKey(int dir)
{
    static const u8 keys[] = { LAUNCHER_KEY_START, LAUNCHER_KEY_SELECT, LAUNCHER_KEY_L, LAUNCHER_KEY_A, LAUNCHER_KEY_B };
    u32 i;
    u32 index = 0;
    for(i = 0; i < sizeof(keys); i++)
    {
        if(keys[i] == gl_auto_start_key)
        {
            index = i;
            break;
        }
    }
    if(dir < 0)
        index = (index == 0) ? (sizeof(keys) - 1) : (index - 1);
    else
        index = (index + 1) % sizeof(keys);
    gl_auto_start_key = keys[index];
    Launcher_SaveAutoStartKey();
}

static u8 Launcher_ReadKeySetting(u32 address, u8 fallback)
{
    u16 value = Read_SET_info(address);
    if(value > LAUNCHER_KEY_L)
        return fallback;
    return (u8)value;
}

static void Launcher_KeyPopupLine(u32 item, char *out, u32 out_size);
static u8 launcher_key_popup_keys[3];

static void Launcher_KeyPopupLine(u32 item, char *out, u32 out_size)
{
    snprintf(out, out_size, "Button %u     %s", (unsigned)(item + 1), Launcher_KeyName(launcher_key_popup_keys[item]));
}

static void Launcher_NormaliseHotkey(u8 *keys)
{
    u32 i;
    u32 j;
    for(i = 0; i < 3; i++)
    {
        if(keys[i] > LAUNCHER_KEY_L)
            keys[i] = (i == 0) ? LAUNCHER_KEY_L : ((i == 1) ? LAUNCHER_KEY_R : LAUNCHER_KEY_SELECT);
        for(j = 0; j < i; j++)
        {
            if(keys[i] == keys[j])
                keys[i] = (u8)((keys[i] + 1) % 10);
        }
    }
}

static void Launcher_CycleHotkeyButton(u8 *keys, u32 index, int dir)
{
    u32 guard;
    u8 next = keys[index];

    for(guard = 0; guard < 10; guard++)
    {
        if(dir < 0)
            next = (next == 0) ? LAUNCHER_KEY_L : (u8)(next - 1);
        else
            next = (u8)((next + 1) % 10);

        if((index == 0 || next != keys[0]) && (index == 1 || next != keys[1]) && (index == 2 || next != keys[2]))
        {
            keys[index] = next;
            return;
        }
    }
}

static void Launcher_HotkeyPopup(const char *title, u32 sleep_hotkey)
{
    const u32 total = 3;
    const u32 visible = 7;
    u8 sleep_keys[3];
    u8 addon_keys[3];
    u32 selected = 0;
    u32 top = 0;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    sleep_keys[0] = Launcher_ReadKeySetting(assress_edit_sleephotkey_0, LAUNCHER_KEY_L);
    sleep_keys[1] = Launcher_ReadKeySetting(assress_edit_sleephotkey_1, LAUNCHER_KEY_R);
    sleep_keys[2] = Launcher_ReadKeySetting(assress_edit_sleephotkey_2, LAUNCHER_KEY_SELECT);
    addon_keys[0] = Launcher_ReadKeySetting(assress_edit_rtshotkey_0, LAUNCHER_KEY_L);
    addon_keys[1] = Launcher_ReadKeySetting(assress_edit_rtshotkey_1, LAUNCHER_KEY_R);
    addon_keys[2] = Launcher_ReadKeySetting(assress_edit_rtshotkey_2, LAUNCHER_KEY_START);
    Launcher_NormaliseHotkey(sleep_keys);
    Launcher_NormaliseHotkey(addon_keys);
    memcpy(launcher_key_popup_keys, sleep_hotkey ? sleep_keys : addon_keys, 3);

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        if(scroll_delay > 0)
            scroll_delay--;
        if(dirty)
        {
            Launcher_SettingsDrawPopupEx(title, total, selected, top, Launcher_KeyPopupLine, 50);
            dirty = 0;
        }
        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();

            if(keysdown & KEY_B)
            {
                if(sleep_hotkey)
                    memcpy(sleep_keys, launcher_key_popup_keys, 3);
                else
                    memcpy(addon_keys, launcher_key_popup_keys, 3);
                Launcher_SaveHotkeys(sleep_keys, addon_keys);
                UIAudio_PlayBack();
                return;
            }
            if((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && scroll_delay == 0))
            {
                if(selected + 1 < total)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
                    selected++;
                    if(selected >= top + visible)
                        top = selected - visible + 1;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, Launcher_KeyPopupLine, 50);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_KeyPopupLine, 50);
                    }
                    else
                        dirty = 1;
                }
            }
            else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && scroll_delay == 0))
            {
                if(selected > 0)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
                    selected--;
                    if(selected < top)
                        top = selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, Launcher_KeyPopupLine, 50);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_KeyPopupLine, 50);
                    }
                    else
                        dirty = 1;
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT | KEY_START))
            {
                Launcher_CycleHotkeyButton(launcher_key_popup_keys, selected, 1);
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_KeyPopupLine, 50);
            }
            else if(keysdown & KEY_LEFT)
            {
                Launcher_CycleHotkeyButton(launcher_key_popup_keys, selected, -1);
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_KeyPopupLine, 50);
            }
        }
    }
}

static const char *Launcher_WeekdayName(u8 weekday)
{
    switch(weekday)
    {
        case 0: return "Sun";
        case 1: return "Mon";
        case 2: return "Tue";
        case 3: return "Wed";
        case 4: return "Thu";
        case 5: return "Fri";
        case 6: return "Sat";
        default: return "Sun";
    }
}

static u8 Launcher_DaysInMonth(u8 year, u8 month)
{
    switch(month)
    {
        case 4:
        case 6:
        case 9:
        case 11:
            return 30;
        case 2:
            return (year % 4) ? 28 : 29;
        default:
            return 31;
    }
}

static void Launcher_NormaliseDateTime(u8 *dt)
{
    u8 max_day;
    if(dt[0] > 99) dt[0] = 0;
    if(dt[1] < 1 || dt[1] > 12) dt[1] = 1;
    max_day = Launcher_DaysInMonth(dt[0], dt[1]);
    if(dt[2] < 1 || dt[2] > max_day) dt[2] = 1;
    if(dt[3] > 6) dt[3] = 0;
    if(dt[4] > 23) dt[4] = 0;
    if(dt[5] > 59) dt[5] = 0;
    if(dt[6] > 59) dt[6] = 0;
}

static u8 Launcher_DecodeBCD(u8 value)
{
    return UNBCD(value);
}

static void Launcher_TimeGetLine(u32 item, char *out, u32 out_size);
static u8 launcher_time_dt[7];

static void Launcher_TimeGetLine(u32 item, char *out, u32 out_size)
{
    switch(item)
    {
        case 0: snprintf(out, out_size, "Year        20%02u", launcher_time_dt[0]); break;
        case 1: snprintf(out, out_size, "Month       %02u", launcher_time_dt[1]); break;
        case 2: snprintf(out, out_size, "Day         %02u", launcher_time_dt[2]); break;
        case 3: snprintf(out, out_size, "Weekday     %s", Launcher_WeekdayName(launcher_time_dt[3])); break;
        case 4: snprintf(out, out_size, "Hour        %02u", launcher_time_dt[4]); break;
        case 5: snprintf(out, out_size, "Minute      %02u", launcher_time_dt[5]); break;
        case 6: snprintf(out, out_size, "Second      %02u", launcher_time_dt[6]); break;
        default: snprintf(out, out_size, ""); break;
    }
}

static void Launcher_TimeAdjust(u32 item, int dir)
{
    u8 max_day;
    switch(item)
    {
        case 0:
            launcher_time_dt[0] = (dir < 0) ? ((launcher_time_dt[0] == 0) ? 99 : launcher_time_dt[0] - 1) : ((launcher_time_dt[0] == 99) ? 0 : launcher_time_dt[0] + 1);
            break;
        case 1:
            launcher_time_dt[1] = (dir < 0) ? ((launcher_time_dt[1] <= 1) ? 12 : launcher_time_dt[1] - 1) : ((launcher_time_dt[1] >= 12) ? 1 : launcher_time_dt[1] + 1);
            break;
        case 2:
            max_day = Launcher_DaysInMonth(launcher_time_dt[0], launcher_time_dt[1]);
            launcher_time_dt[2] = (dir < 0) ? ((launcher_time_dt[2] <= 1) ? max_day : launcher_time_dt[2] - 1) : ((launcher_time_dt[2] >= max_day) ? 1 : launcher_time_dt[2] + 1);
            break;
        case 3:
            launcher_time_dt[3] = (dir < 0) ? ((launcher_time_dt[3] == 0) ? 6 : launcher_time_dt[3] - 1) : ((launcher_time_dt[3] == 6) ? 0 : launcher_time_dt[3] + 1);
            break;
        case 4:
            launcher_time_dt[4] = (dir < 0) ? ((launcher_time_dt[4] == 0) ? 23 : launcher_time_dt[4] - 1) : ((launcher_time_dt[4] == 23) ? 0 : launcher_time_dt[4] + 1);
            break;
        case 5:
            launcher_time_dt[5] = (dir < 0) ? ((launcher_time_dt[5] == 0) ? 59 : launcher_time_dt[5] - 1) : ((launcher_time_dt[5] == 59) ? 0 : launcher_time_dt[5] + 1);
            break;
        case 6:
            launcher_time_dt[6] = (dir < 0) ? ((launcher_time_dt[6] == 0) ? 59 : launcher_time_dt[6] - 1) : ((launcher_time_dt[6] == 59) ? 0 : launcher_time_dt[6] + 1);
            break;
        default:
            break;
    }
    Launcher_NormaliseDateTime(launcher_time_dt);
}

static void Launcher_TimePopup(void)
{
    const u32 total = 7;
    const u32 visible = 7;
    u8 datetime[7];
    u32 selected = 0;
    u32 top = 0;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    rtc_enable();
    rtc_get(datetime);
    rtc_disenable();

    launcher_time_dt[0] = Launcher_DecodeBCD(datetime[0]);
    launcher_time_dt[1] = Launcher_DecodeBCD(datetime[1] & 0x1F);
    launcher_time_dt[2] = Launcher_DecodeBCD(datetime[2] & 0x3F);
    launcher_time_dt[3] = Launcher_DecodeBCD(datetime[3] & 0x07);
    launcher_time_dt[4] = Launcher_DecodeBCD(datetime[4] & 0x3F);
    launcher_time_dt[5] = Launcher_DecodeBCD(datetime[5] & 0x7F);
    launcher_time_dt[6] = Launcher_DecodeBCD(datetime[6] & 0x7F);
    Launcher_NormaliseDateTime(launcher_time_dt);

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        if(scroll_delay > 0)
            scroll_delay--;
        if(dirty)
        {
            Launcher_SettingsDrawPopupEx(DSTEXT_SETTINGS_TIME, total, selected, top, Launcher_TimeGetLine, 45);
            dirty = 0;
        }
        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();

            if(keysdown & KEY_B)
            {
                rtc_enable();
                rtc_set(launcher_time_dt);
                rtc_disenable();
                delay(0x200);
                UIAudio_PlayBack();
                return;
            }
            if((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && scroll_delay == 0))
            {
                if(selected + 1 < total)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
                    selected++;
                    if(selected >= top + visible)
                        top = selected - visible + 1;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, Launcher_TimeGetLine, 45);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_TimeGetLine, 45);
                    }
                    else
                        dirty = 1;
                }
            }
            else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && scroll_delay == 0))
            {
                if(selected > 0)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
                    selected--;
                    if(selected < top)
                        top = selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, Launcher_TimeGetLine, 45);
                        Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_TimeGetLine, 45);
                    }
                    else
                        dirty = 1;
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT | KEY_START))
            {
                Launcher_TimeAdjust(selected, 1);
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_TimeGetLine, 45);
            }
            else if(keysdown & KEY_LEFT)
            {
                Launcher_TimeAdjust(selected, -1);
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsDrawPopupRowEx(selected, selected, top, Launcher_TimeGetLine, 45);
            }
        }
    }
}

static void Launcher_SettingsToggle(u32 item, int dir)
{
    switch(item)
    {
        case SETTINGS_TIME_SETTINGS:
            Launcher_TimePopup();
            break;
        case SETTINGS_VIEW_MODE:
            Launcher_ViewModeCycle(dir);
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_THUMBNAILS:
            Launcher_CycleThumbnailStyle(dir);
            break;
        case SETTINGS_SOUNDS:
            launcher_sounds_enabled ^= 1;
            if(!launcher_sounds_enabled)
                UIAudio_StopForSharedBufferUse();
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_LANGUAGE:
            Launcher_CycleLanguage(dir);
            break;
        case SETTINGS_THEME_MODE:
            Launcher_CycleThemeMode(dir);
            break;
        case SETTINGS_THEME:
            Launcher_CycleTheme(dir);
            break;
        case SETTINGS_BOOT_ENGINE:
            gl_engine_sel ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_AUTO_SAVE:
            gl_auto_save_sel ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_RESUME_LAST:
            gl_resume_last_on ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_START_ENABLED:
            Launcher_CycleStartEnabled();
            break;
        case SETTINGS_START_SOURCE:
            Launcher_CycleStartSource();
            break;
        case SETTINGS_AUTO_START:
            Launcher_CycleAutoStartKey(dir);
            break;
        case SETTINGS_ADDON_SETTINGS:
            Launcher_SettingsPopupToggle(DSTEXT_SETTINGS_ADDON, ADDON_SETTING_TOTAL, Launcher_AddonSettingsGetLine, Launcher_AddonSettingsToggle);
            break;
        case SETTINGS_BOOT_MODE:
            Launcher_BootModeCycle(dir);
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_MODE_B:
            Launcher_ModeBCycle(dir);
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_INGAME_RTC:
            gl_ingame_RTC_open_status ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_SLEEP_HOTKEY:
            Launcher_HotkeyPopup(DSTEXT_SETTINGS_SLEEP_HOTKEY, 1);
            break;
        case SETTINGS_ADDON_HOTKEY:
            Launcher_HotkeyPopup(DSTEXT_SETTINGS_ADDON_HOTKEY, 0);
            break;
        case SETTINGS_FULL_INTRO:
            gl_toggle_reset ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_BACKUP_SAVES:
            gl_toggle_backup ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_LED_SETTINGS:
            Launcher_SettingsPopupToggleEx(DSTEXT_SETTINGS_LED, LED_SETTING_TOTAL, Launcher_LedSettingsGetLine, Launcher_LedSettingsToggle, 45);
            break;
        default:
            break;
    }
}

static void Launcher_DrawHelpClock(u32 force)
{
    static u8 last_hh = 0xFF;
    static u8 last_mm = 0xFF;
    static u8 last_ss = 0xFF;
    u8 datetime[3];
    u8 HH;
    u8 MM;
    u8 SS;
    char msgtime[16];
    const int x = 240 - 3 - (8 * 6);
    const int y = 3;

    rtc_enable();
    rtc_gettime(datetime);
    rtc_disenable();
    delay(5);

    HH = UNBCD(datetime[0] & 0x3F);
    MM = UNBCD(datetime[1] & 0x7F);
    SS = UNBCD(datetime[2] & 0x7F);
    if(HH > 23) HH = 0;
    if(MM > 59) MM = 0;
    if(SS > 59) SS = 0;

    if(force || HH != last_hh || MM != last_mm || SS != last_ss)
    {
        Launcher_ClearWithThemeBG((const u16*)gImage_HELP, x, y, 8 * 6, 13);
        sprintf(msgtime, "%02u:%02u:%02u", HH, MM, SS);
        DrawHZText12(msgtime, 0, x, y, gl_color_topbar_text, 1);
        last_hh = HH;
        last_mm = MM;
        last_ss = SS;
    }
}

static void Launcher_DrawHelpContent(void)
{
    Launcher_DrawThemeBGFull((const u16*)gImage_HELP);

    /* The Help body is now baked into HELP.bmp.
       Keep only the dynamic top-bar elements. */
    Launcher_DrawTopbarName(HELP);
    Launcher_DrawTopbarTitle(HELP, DSTEXT_HELP_TITLE);
    Launcher_DrawHelpClock(1);
    DrawHZText12(LAUNCHER_VERSION_TEXT, 0, 2, 147, gl_color_text, 1);
}

static void Launcher_DrawHelpControls(void)
{
    const char *lines[] =
    {
        DSTEXT_CONTROL_A,
        DSTEXT_CONTROL_B,
        DSTEXT_CONTROL_DPAD,
        DSTEXT_CONTROL_START,
        DSTEXT_CONTROL_SELECT,
        DSTEXT_CONTROL_HOLD_START,
        DSTEXT_CONTROL_DOUBLE_SELECT,
        DSTEXT_CONTROL_QUICK_HOTKEY
    };
    u32 i;
    u32 y = 27;
    const u32 x = 12;
    const u32 wrap_x = 18;

    Launcher_DrawThemeBGFull((const u16*)gImage_SD_LIST);
    Launcher_ClearListBodyBackground();
    Launcher_DrawTopbarName(HELP);
    Launcher_DrawTopbarTitle(HELP, DSTEXT_CONTROLS_TITLE);
    Launcher_DrawHelpClock(1);

    for(i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
    {
        DrawHZText12((TCHAR*)lines[i], 0, x, y, gl_color_text, 1);
        y += 13;
    }

    DrawHZText12(DSTEXT_CONTROL_QUICK_ACTION, 0, wrap_x, y, gl_color_text, 1);
}

static void Launcher_ShowHelpBOnly(void)
{
    u32 controls_page = 0;
    Launcher_DrawHelpContent();

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawHelpClock(0);
        scanKeys();
        {
            u16 keysdown = keysDown();
            if(keysdown & KEY_B)
            {
                if(controls_page)
                {
                    controls_page = 0;
                    UIAudio_PlayBack();
                    Launcher_DrawHelpContent();
                }
                else
                {
                    UIAudio_PlayBack();
                    return;
                }
            }
            if((keysdown & KEY_A) && !controls_page)
            {
                controls_page = 1;
                UIAudio_PlayAccept();
                Launcher_DrawHelpControls();
            }
        }
    }
}



static u32 Launcher_PrepareLastPlayedForMenu(void)
{
    TCHAR recent_path[MAX_path_len];
    TCHAR recent_name[200];

    memset(recent_path, 0, sizeof(recent_path));
    memset(recent_name, 0, sizeof(recent_name));

    if(!Launcher_GetStartGameEntry(recent_path, sizeof(recent_path), recent_name, sizeof(recent_name)))
        return 0;

    memset(p_recently_play[0], 0x00, 512);
    if(strcmp(recent_path, "/") == 0)
        snprintf(p_recently_play[0], 512, "/%s", recent_name);
    else
        snprintf(p_recently_play[0], 512, "%s/%s", recent_path, recent_name);

    return (p_recently_play[0][0] == '/') ? 1 : 0;
}

static void Launcher_StartGetLastTitle(char *out, u32 out_size)
{
    TCHAR recent_path[MAX_path_len];
    TCHAR recent_name[200];

    if(!out || out_size == 0)
        return;

    out[0] = '\0';
    memset(recent_path, 0, sizeof(recent_path));
    memset(recent_name, 0, sizeof(recent_name));

    if(Launcher_GetStartGameEntry(recent_path, sizeof(recent_path), recent_name, sizeof(recent_name)))
    {
        Launcher_CleanTitle(recent_name, out, out_size);
        if(out[0] == '\0')
            snprintf(out, out_size, "%s", recent_name);
    }

    if(out[0] == '\0')
        snprintf(out, out_size, "%s", DSTEXT_NO_RECENT_GAME);
}

static int Launcher_SplitStartTitle(const char *title, char lines[3][32])
{
    int title_len;
    int pos = 0;
    int line_count = 0;
    int i;
    const int max_take = 18;

    memset(lines, 0, sizeof(char) * 3 * 32);
    if(!title || !title[0])
    {
        snprintf(lines[0], 32, "%s", DSTEXT_NO_RECENT_GAME);
        return 1;
    }

    title_len = strlen(title);
    while(pos < title_len && line_count < 3)
    {
        int remaining = title_len - pos;
        int take = (remaining > max_take) ? max_take : remaining;
        int split = pos + take;

        if(split < title_len)
        {
            for(i = split; i > pos + 5; i--)
            {
                if(title[i] == ' ')
                {
                    split = i;
                    break;
                }
            }
        }

        if(split <= pos)
            split = pos + take;

        strncpy(lines[line_count], title + pos, split - pos);
        lines[line_count][split - pos] = '\0';

        while(lines[line_count][0] == ' ')
            memmove(lines[line_count], lines[line_count] + 1, strlen(lines[line_count]));

        pos = split;
        while(title[pos] == ' ')
            pos++;

        line_count++;
    }

    if(pos < title_len && line_count > 0)
    {
        int last = line_count - 1;
        int len = strlen(lines[last]);
        if(len > 15)
            len = 15;
        while(len > 0 && lines[last][len - 1] == ' ')
            len--;
        lines[last][len] = '\0';
        strcat(lines[last], "...");
    }

    if(line_count == 0)
    {
        strcpy(lines[0], " ");
        line_count = 1;
    }

    return line_count;
}

typedef struct
{
    int x;
    int y;
    int w;
    int h;
} LauncherStartBox;

typedef struct
{
    int x[4];
    int y[4];
    int sx[4];
    int sy[4];
} LauncherStartCorners;

typedef struct
{
    int x[4];
    int y[4];
    int w[4];
    int h[4];
    u16 pixels[4][144];
} LauncherStartCornerSave;

static LauncherStartBox Launcher_GetStartBox(u32 item)
{
    LauncherStartBox box;

    switch(item)
    {
        case 0: box.x = LAUNCHER_START_LAST_X; box.y = LAUNCHER_START_LAST_Y; box.w = LAUNCHER_START_LAST_W; box.h = LAUNCHER_START_LAST_H; break;
        case 1: box.x = LAUNCHER_START_SD_X; box.y = LAUNCHER_START_SD_Y; box.w = LAUNCHER_START_SD_W; box.h = LAUNCHER_START_SD_H; break;
        case 2: box.x = LAUNCHER_START_NOR_X; box.y = LAUNCHER_START_NOR_Y; box.w = LAUNCHER_START_NOR_W; box.h = LAUNCHER_START_NOR_H; break;
        case 3: box.x = LAUNCHER_START_SETTINGS_X; box.y = LAUNCHER_START_SETTINGS_Y; box.w = LAUNCHER_START_SETTINGS_W; box.h = LAUNCHER_START_SETTINGS_H; break;
        default: box.x = 0; box.y = 0; box.w = 0; box.h = 0; break;
    }

    return box;
}

static void Launcher_GetStartCornerPos(u32 item, int corner, int *x, int *y, int *sx, int *sy)
{
    LauncherStartBox box = Launcher_GetStartBox(item);

    switch(corner)
    {
        case 0: /* top-left */
            *x = box.x;
            *y = box.y;
            *sx = 1;
            *sy = 1;
            break;
        case 1: /* top-right */
            *x = box.x + box.w - 1;
            *y = box.y;
            *sx = -1;
            *sy = 1;
            break;
        case 2: /* bottom-left */
            *x = box.x;
            *y = box.y + box.h - 1;
            *sx = 1;
            *sy = -1;
            break;
        default: /* bottom-right */
            *x = box.x + box.w - 1;
            *y = box.y + box.h - 1;
            *sx = -1;
            *sy = -1;
            break;
    }

    /* Pixel nudges to match the baked-in boxes in START.bmp. */
    if(item == 0 && corner >= 2)
        (*y)--;
    if(item == 1)
    {
        if(corner < 2)
            (*y)--;
        if(corner == 1 || corner == 3)
            (*x)--;
    }
    if(item == 2)
    {
        if(corner < 2)
            (*y)--;
        if(corner == 0 || corner == 2)
            (*x)++;
    }
    if(item == 3)
    {
        if(corner == 0 || corner == 2)
            (*x)--;
        if(corner == 1 || corner == 3)
            (*x)++;
        if(corner < 2)
            (*y) -= 2;
    }
    else
    {
        if(corner == 1 || corner == 3)
            (*x)++;
        if(corner == 2 || corner == 3)
            (*y)++;
    }
}

static void Launcher_GetStartCornersForItem(u32 item, LauncherStartCorners *corners)
{
    int corner;

    if(!corners)
        return;

    for(corner = 0; corner < 4; corner++)
        Launcher_GetStartCornerPos(item, corner, &(corners->x[corner]), &(corners->y[corner]), &(corners->sx[corner]), &(corners->sy[corner]));
}

static void Launcher_DrawStartCornerAt(int x, int y, int sx, int sy, u16 colour)
{
    int hx = (sx < 0) ? (x - 8) : x;
    int hy = (sy < 0) ? (y - 2) : y;
    int vx = (sx < 0) ? (x - 2) : x;
    int vy = (sy < 0) ? (y - 8) : y;

    Launcher_ClearClip(hx, hy, 9, 3, colour);
    Launcher_ClearClip(vx, vy, 3, 9, colour);
}

static void Launcher_RestoreStartCornerAt(int x, int y, int sx, int sy)
{
    int hx = (sx < 0) ? (x - 8) : x;
    int hy = (sy < 0) ? (y - 2) : y;
    int vx = (sx < 0) ? (x - 2) : x;
    int vy = (sy < 0) ? (y - 8) : y;

    Launcher_RestoreBGClip((const u16*)gImage_START, hx, hy, 9, 3);
    Launcher_RestoreBGClip((const u16*)gImage_START, vx, vy, 3, 9);
}

static void Launcher_RestoreStartMarkerBox(LauncherStartBox box)
{
    Launcher_RestoreBGClip((const u16*)gImage_START, box.x - 2, box.y - 2, box.w + 4, box.h + 4);
}

static LauncherStartBox Launcher_GetStartSettingsIconMarkerBox(void)
{
    LauncherStartBox box = Launcher_GetStartBox(3);
    box.x -= 2;
    box.y -= 3;
    box.w += 5;
    box.h += 5;
    return box;
}

static void Launcher_DrawStartMarkerBox(LauncherStartBox box, u16 colour)
{
    if(LAUNCHER_START_SELECTION_SHAPE == 2)
    {
        int row;
        for(row = 0; row < box.h; row++)
        {
            int edge = row;
            int inset = 0;
            int width;

            if(box.h - 1 - row < edge)
                edge = box.h - 1 - row;
            if(edge == 0)
                inset = 6;
            else if(edge == 1)
                inset = 4;
            else if(edge == 2)
                inset = 3;
            else if(edge == 3)
                inset = 2;
            else if(edge == 4 || edge == 5)
                inset = 1;
            if(inset > 0)
            {
                if(inset > (box.w - 1) / 2)
                    inset = (box.w - 1) / 2;
            }

            width = box.w - (inset * 2);
            if(width > 0)
                Launcher_ClearClip(box.x + inset, box.y + row, width, 1, colour);
        }
    }
    else
    {
        Launcher_ClearClip(box.x, box.y, box.w, box.h, colour);
    }
}

static void Launcher_DrawStartMarkerOutline(LauncherStartBox box, u16 colour)
{
    Launcher_ClearClip(box.x, box.y, box.w, 3, colour);
    Launcher_ClearClip(box.x, box.y + box.h - 3, box.w, 3, colour);
    Launcher_ClearClip(box.x, box.y, 3, box.h, colour);
    Launcher_ClearClip(box.x + box.w - 3, box.y, 3, box.h, colour);
}

static void Launcher_DrawStartLastTitleEx(u32 selected, u32 redraw_marker);

static void Launcher_DrawStartCornersAtPositions(const LauncherStartCorners *corners, u16 colour)
{
    int corner;

    if(!corners)
        return;

    for(corner = 0; corner < 4; corner++)
        Launcher_DrawStartCornerAt(corners->x[corner], corners->y[corner], corners->sx[corner], corners->sy[corner], colour);
}

static void Launcher_StartCornerBounds(int x, int y, int sx, int sy, int *left, int *top, int *w, int *h)
{
    int x0 = ((sx < 0) ? (x - 8) : x) - 1;
    int x1 = ((sx < 0) ? (x + 1) : (x + 9)) + 1;
    int y0 = ((sy < 0) ? (y - 8) : y) - 1;
    int y1 = ((sy < 0) ? (y + 1) : (y + 9)) + 1;

    if(x0 < 0)
        x0 = 0;
    if(y0 < 0)
        y0 = 0;
    if(x1 > 240)
        x1 = 240;
    if(y1 > 160)
        y1 = 160;

    *left = x0;
    *top = y0;
    *w = x1 - x0;
    *h = y1 - y0;
}

static void Launcher_SaveStartCornersUnder(const LauncherStartCorners *corners, LauncherStartCornerSave *save)
{
    vu16 *src_base = (vu16*)VRAM;
    int corner;

    if(!corners || !save)
        return;

    for(corner = 0; corner < 4; corner++)
    {
        int row;

        Launcher_StartCornerBounds(corners->x[corner], corners->y[corner], corners->sx[corner], corners->sy[corner],
                                   &(save->x[corner]), &(save->y[corner]), &(save->w[corner]), &(save->h[corner]));

        for(row = 0; row < save->h[corner]; row++)
            dmaCopy((void*)(src_base + ((save->y[corner] + row) * 240) + save->x[corner]),
                    (void*)(save->pixels[corner] + (row * 12)),
                    save->w[corner] * 2);
    }
}

static void Launcher_RestoreSavedStartCorners(const LauncherStartCornerSave *save)
{
    vu16 *dst_base = (vu16*)VRAM;
    int corner;

    if(!save)
        return;

    for(corner = 0; corner < 4; corner++)
    {
        int row;

        for(row = 0; row < save->h[corner]; row++)
            dmaCopy((void*)(save->pixels[corner] + (row * 12)),
                    (void*)(dst_base + ((save->y[corner] + row) * 240) + save->x[corner]),
                    save->w[corner] * 2);
    }
}

static void Launcher_RestoreStartCorners(u32 item)
{
    LauncherStartBox box = Launcher_GetStartBox(item);
    int corner;

    if(box.w <= 0 || box.h <= 0)
        return;

    if(LAUNCHER_START_SELECTION_SHAPE == LAUNCHER_START_SELECTION_OFF)
        return;

    if((item == 3) && !LAUNCHER_START_SETTINGS_TEXT_ENABLED)
    {
        Launcher_RestoreStartMarkerBox(Launcher_GetStartSettingsIconMarkerBox());
        return;
    }

    if(LAUNCHER_START_SELECTION_SHAPE != 0)
    {
        Launcher_RestoreStartMarkerBox(box);
        return;
    }

    for(corner = 0; corner < 4; corner++)
    {
        int x, y, sx, sy;
        Launcher_GetStartCornerPos(item, corner, &x, &y, &sx, &sy);
        Launcher_RestoreStartCornerAt(x, y, sx, sy);
    }
}

static int Launcher_LerpStartCorner(int from, int to, int frame, int frames)
{
    int delta = to - from;

    if(delta >= 0)
        return from + ((delta * frame) + (frames / 2)) / frames;
    return from + ((delta * frame) - (frames / 2)) / frames;
}

static void Launcher_AnimateStartSelection(u32 old_selected, u32 selected)
{
    LauncherStartCorners from;
    LauncherStartCorners to;
    LauncherStartCorners current;
    LauncherStartCornerSave saved;
    const int frames = 7;
    int frame;
    int corner;
    int have_saved = 0;

    if(old_selected == selected)
        return;

    Launcher_GetStartCornersForItem(old_selected, &from);
    Launcher_GetStartCornersForItem(selected, &to);

    for(frame = 1; frame <= frames; frame++)
    {
        VBlankIntrWait();
        UIAudio_Update();

        if(have_saved)
            Launcher_RestoreSavedStartCorners(&saved);

        for(corner = 0; corner < 4; corner++)
        {
            current.x[corner] = Launcher_LerpStartCorner(from.x[corner], to.x[corner], frame, frames);
            current.y[corner] = Launcher_LerpStartCorner(from.y[corner], to.y[corner], frame, frames);
            current.sx[corner] = to.sx[corner];
            current.sy[corner] = to.sy[corner];
        }

        Launcher_SaveStartCornersUnder(&current, &saved);
        Launcher_DrawStartCornersAtPositions(&current, gl_color_selectBG_sd);
        have_saved = 1;
    }

    if(have_saved)
        Launcher_RestoreSavedStartCorners(&saved);
}

static void Launcher_DrawStartCorners(u32 item, u32 selected)
{
    LauncherStartBox box = Launcher_GetStartBox(item);
    u16 colour = gl_color_selectBG_sd;
    int corner;

    Launcher_RestoreStartCorners(item);
    if(selected != item || box.w <= 0 || box.h <= 0)
        return;

    if(LAUNCHER_START_SELECTION_SHAPE == LAUNCHER_START_SELECTION_OFF)
        return;

    if((item == 3) && !LAUNCHER_START_SETTINGS_TEXT_ENABLED)
    {
        Launcher_DrawStartMarkerOutline(Launcher_GetStartSettingsIconMarkerBox(), colour);
        return;
    }

    if(LAUNCHER_START_SELECTION_SHAPE != 0)
    {
        Launcher_DrawStartMarkerBox(box, colour);
        return;
    }

    for(corner = 0; corner < 4; corner++)
    {
        int x, y, sx, sy;
        Launcher_GetStartCornerPos(item, corner, &x, &y, &sx, &sy);
        Launcher_DrawStartCornerAt(x, y, sx, sy, colour);
    }
}


static u32 Launcher_IsGbaFilename(const TCHAR *pfilename)
{
    u32 len;
    if(!pfilename)
        return 0;
    len = strlen(pfilename);
    if(len >= 3 && !strcasecmp(&(pfilename[len - 3]), "gba"))
        return 1;
    if(len >= 3 && !strcasecmp(&(pfilename[len - 3]), "agb"))
        return 1;
    return 0;
}

static void Launcher_DrawStartFileIconInThumbBox(const TCHAR *filename, int x, int y, int thumb_w, int thumb_h)
{
    const u16 *icon = Launcher_GetFileIcon(filename);

    /* Non-GBA entries on the start screen use their normal icon rather than a
       cartridge thumbnail.  Draw it at 2x so it has more presence without
       overwhelming the thumbnail slot. */
    Launcher_DrawIconCenteredClip2x(icon, x, y, thumb_w, thumb_h);
}

static void Launcher_DrawStartLastThumb(int x, int y)
{
    TCHAR recent_path[MAX_path_len];
    TCHAR recent_name[200];
    TCHAR saved_path[MAX_path_len];
    const u16 *cached_preview = 0;
    u32 have_thumb = 0;
    u32 use_favourite_cache = 0;
    const int thumb_w = LAUNCHER_START_THUMB_W;
    const int thumb_h = LAUNCHER_START_THUMB_H;

    memset(recent_path, 0, sizeof(recent_path));
    memset(recent_name, 0, sizeof(recent_name));
    memset(saved_path, 0, sizeof(saved_path));

    if(Launcher_GetStartGameEntry(recent_path, sizeof(recent_path), recent_name, sizeof(recent_name)))
    {
        use_favourite_cache = launcher_start_uses_favourites && launcher_favourite_count && Launcher_IsGbaFilename(recent_name);
        if(use_favourite_cache)
            cached_preview = Launcher_StartPreviewCachedImage(launcher_favourite_index);
        else
        {
            f_getcwd(saved_path, sizeof(saved_path) / sizeof(*saved_path));
            if(f_chdir(recent_path) == FR_OK)
                have_thumb = Load_ThumbnailEx(recent_name, pReadCache + 0x10000);
            if(saved_path[0])
                f_chdir(saved_path);
        }
    }

    /* Restore the thumbnail area from the baked start-screen background only.
       Do not draw a black frame around it; the selected-box corner highlight
       supplies the only outline on this screen. */
    Launcher_RestoreBGClip((const u16*)gImage_START, x - 1, y - 1, thumb_w + 2, thumb_h + 2);
    if(cached_preview)
    {
        if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
            Launcher_DrawPicClipStride(cached_preview + 9, thumb_w, x + 9, y, 37, thumb_h);
        else
            Launcher_DrawPicClipStride(cached_preview, thumb_w, x, y, thumb_w, thumb_h);
    }
    else if(have_thumb)
    {
        Launcher_ScaleThumbToBox((u16*)(pReadCache + 0x10036),
                                 Launcher_ThumbnailSourceWidth(),
                                 Launcher_ThumbnailSourceHeight(),
                                 launcher_side_preview_left,
                                 thumb_w,
                                 thumb_h);
        if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
            Launcher_DrawPicClipStride(launcher_side_preview_left + 9, thumb_w, x + 9, y, 37, thumb_h);
        else
            Launcher_DrawPicClipStride(launcher_side_preview_left, thumb_w, x, y, thumb_w, thumb_h);
    }
    else if(recent_name[0] && !Launcher_IsGbaFilename(recent_name))
    {
        /* Non-GBA last-played entries do not have 120x80 thumbnails.  Show
           their normal file icon instead of the GBA missing-thumbnail plate. */
        Launcher_DrawStartFileIconInThumbBox(recent_name, x, y, thumb_w, thumb_h);
    }
    else
    {
        Launcher_ScaleThumbToBox(Launcher_NotFoundImage(), Launcher_NotFoundWidth(), Launcher_NotFoundHeight(), launcher_side_preview_left, thumb_w, thumb_h);
        if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
            Launcher_DrawPicClipStride(launcher_side_preview_left + 9, thumb_w, x + 9, y, 37, thumb_h);
        else
            Launcher_DrawPicClipStride(launcher_side_preview_left, thumb_w, x, y, thumb_w, thumb_h);
    }

    if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
        Launcher_DrawThumbBorder(x + 9, y, 37, thumb_h);
    else
        Launcher_DrawThumbBorder(x, y, thumb_w, thumb_h);
}

static int Launcher_StartAlignedTextX(const char *msg, int x, int w, int align)
{
    int text_w = DrawText12VisibleLength((char*)msg) * 6;
    int text_x = x;

    if(align == 2)
        text_x = x + w - text_w;
    else if(align == 1)
        text_x = x + ((w - text_w) / 2);

    if(text_x < x)
        text_x = x;
    if(text_x + text_w > x + w)
        text_x = x + w - text_w;
    if(text_x < 0)
        text_x = 0;
    return text_x;
}

static void Launcher_ResetStartTitleScroll(void)
{
    launcher_start_title_scroll_offset = 0;
    launcher_start_title_scroll_frame = 0;
}

static void Launcher_StartFitTextLine(const char *src, char *dst, u32 dst_size, int area_w)
{
    int max_chars;

    if(!dst || dst_size == 0)
        return;

    dst[0] = '\0';
    if(!src)
        return;

    max_chars = area_w / 6;
    if(max_chars < 1)
        max_chars = 1;

    if(DrawText12VisibleLength((char*)src) <= max_chars)
    {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return;
    }

    Launcher_MakeEllipsisText(src, dst, dst_size, max_chars);
}

static int Launcher_StartTitleAreaHeight(void)
{
    if(LAUNCHER_START_LAST_TEXT_LINES <= 1)
        return 16;
    if(LAUNCHER_START_LAST_TEXT_LINES == 2)
        return 27;
    return 42;
}

static void Launcher_StartScrollTextLine(const char *src, char *dst, u32 dst_size, int area_w)
{
    int max_chars;
    int len;
    int offset;
    int cycle;
    int i;
    const int gap = 4;

    if(!dst || dst_size == 0)
        return;

    dst[0] = '\0';
    if(!src)
        return;

    max_chars = area_w / 6;
    if(max_chars < 1)
        max_chars = 1;
    if(max_chars >= (int)dst_size)
        max_chars = dst_size - 1;

    len = strlen(src);
    if(len <= max_chars)
    {
        strncpy(dst, src, dst_size - 1);
        dst[dst_size - 1] = '\0';
        return;
    }

    cycle = len + gap;
    offset = launcher_start_title_scroll_offset % cycle;
    for(i = 0; i < max_chars; i++)
    {
        int pos = (offset + i) % cycle;
        dst[i] = (pos < len) ? src[pos] : ' ';
    }
    dst[max_chars] = '\0';
}

static u32 Launcher_StartTitleShouldScroll(void)
{
    char title[96];
    int max_chars = LAUNCHER_START_LAST_TEXT_W / 6;

    if(LAUNCHER_START_LAST_TEXT_LINES != 1)
        return 0;
    if(max_chars < 1)
        return 0;

    Launcher_StartGetLastTitle(title, sizeof(title));
    return (DrawText12VisibleLength(title) > max_chars);
}

static u32 Launcher_UpdateStartTitleScroll(u32 selected)
{
    if(selected != 0)
    {
        Launcher_ResetStartTitleScroll();
        return 0;
    }
    if(!Launcher_StartTitleShouldScroll())
        return 0;

    launcher_start_title_scroll_frame++;
    if(launcher_start_title_scroll_frame < 40)
        return 0;
    if(((launcher_start_title_scroll_frame - 40) % 8) != 0)
        return 0;

    launcher_start_title_scroll_offset++;
    Launcher_DrawStartLastTitleEx(selected, 0);
    return 1;
}

static void Launcher_DrawStartLastTitleEx(u32 selected, u32 redraw_marker)
{
    char title[96];
    char lines[3][32];
    int line_count;
    int i;
    int area_x = LAUNCHER_START_LAST_TEXT_X;
    int area_w = LAUNCHER_START_LAST_TEXT_W;
    int area_h = Launcher_StartTitleAreaHeight();
    int line_h = 11;
    int centre_y = LAUNCHER_START_LAST_TEXT_Y + ((LAUNCHER_START_LAST_TEXT_LINES <= 2) ? (area_h / 2) : (66 - 49));
    int text_y;
    u16 colour = (LAUNCHER_START_SELECTION_MODE && selected == 0) ? gl_color_selected : gl_color_text;

    Launcher_StartGetLastTitle(title, sizeof(title));
    if(LAUNCHER_START_LAST_TEXT_LINES == 1)
    {
        memset(lines, 0, sizeof(lines));
        strncpy(lines[0], title, sizeof(lines[0]) - 1);
        line_count = 1;
    }
    else
    {
        line_count = Launcher_SplitStartTitle(title, lines);
        if(line_count > LAUNCHER_START_LAST_TEXT_LINES)
            line_count = LAUNCHER_START_LAST_TEXT_LINES;
    }
    if(line_count == 3)
    {
        /* Three-line titles are centred by the drawn middle line itself,
           not by the top of that middle line.  DrawHZText12 is 12px high,
           so lift the block by half a glyph height. */
        text_y = centre_y - line_h - 6;
    }
    else
    {
        text_y = centre_y - ((line_count * line_h) / 2);
        if(line_count == 1)
            text_y--;
    }

    if(selected == 0 && redraw_marker && LAUNCHER_START_SELECTION_SHAPE != 0 && LAUNCHER_START_SELECTION_SHAPE != LAUNCHER_START_SELECTION_OFF)
        Launcher_DrawStartMarkerBox(Launcher_GetStartBox(0), gl_color_selectBG_sd);
    else if(selected == 0 && LAUNCHER_START_LAST_TEXT_LINES == 1 && Launcher_StartTitleShouldScroll())
        Launcher_ClearClip(area_x, text_y, area_w, 13, gl_color_selectBG_sd);
    else
        Launcher_RestoreBGClip((const u16*)gImage_START, area_x, LAUNCHER_START_LAST_TEXT_Y, area_w, area_h);
    for(i = 0; i < line_count; i++)
    {
        const char *line = (LAUNCHER_START_LAST_TEXT_LINES == 1) ? title : lines[i];
        char fitted[96];
        int text_x;
        if(LAUNCHER_START_LAST_TEXT_LINES == 1 && selected == 0)
            Launcher_StartScrollTextLine(line, fitted, sizeof(fitted), area_w);
        else
            Launcher_StartFitTextLine(line, fitted, sizeof(fitted), area_w);
        if(LAUNCHER_START_LAST_TEXT_LINES == 1 && selected == 0 && Launcher_StartTitleShouldScroll())
            text_x = area_x;
        else
            text_x = Launcher_StartAlignedTextX(fitted, area_x, area_w, LAUNCHER_START_LAST_TEXT_ALIGN);
        DrawHZText12(fitted, 0, text_x, text_y + (i * line_h), colour, 1);
    }
}

static void Launcher_DrawStartLastTitle(u32 selected)
{
    Launcher_DrawStartLastTitleEx(selected, 1);
}

static void Launcher_DrawStartOption(u32 item, u32 selected)
{
    char msg[24];
    int x = 0;
    int y = 0;
    int w = 96;
    int align = 1;
    int text_x;
    u32 filled_marker = (LAUNCHER_START_SELECTION_SHAPE != 0 && LAUNCHER_START_SELECTION_SHAPE != LAUNCHER_START_SELECTION_OFF && selected == item);
    u16 colour = (LAUNCHER_START_SELECTION_MODE && selected == item) ? gl_color_selected : gl_color_text;

    switch(item)
    {
        case 0:
            Launcher_DrawStartCorners(item, selected);
            Launcher_DrawStartLastTitle(selected);
            return;
        case 1:
            snprintf(msg, sizeof(msg), "%s", DSTEXT_SD_CARD);
            x = LAUNCHER_START_SD_TEXT_X; y = LAUNCHER_START_SD_TEXT_Y; w = LAUNCHER_START_SD_TEXT_W; align = LAUNCHER_START_SD_TEXT_ALIGN;
            break;
        case 2:
            snprintf(msg, sizeof(msg), "%s", DSTEXT_NOR_FLASH);
            x = LAUNCHER_START_NOR_TEXT_X; y = LAUNCHER_START_NOR_TEXT_Y; w = LAUNCHER_START_NOR_TEXT_W; align = LAUNCHER_START_NOR_TEXT_ALIGN;
            break;
        case 3:
            if(!LAUNCHER_START_SETTINGS_TEXT_ENABLED)
            {
                Launcher_DrawStartCorners(item, selected);
                return;
            }
            snprintf(msg, sizeof(msg), "%s", DSTEXT_START_SETTINGS);
            x = LAUNCHER_START_SETTINGS_TEXT_X; y = LAUNCHER_START_SETTINGS_TEXT_Y; w = LAUNCHER_START_SETTINGS_TEXT_W; align = LAUNCHER_START_SETTINGS_TEXT_ALIGN;
            break;
        default:
            return;
    }

    if(filled_marker)
        Launcher_DrawStartCorners(item, selected);
    else
    {
        Launcher_DrawStartCorners(item, selected);
        Launcher_RestoreBGClip((const u16*)gImage_START, x, y, w, 13);
    }
    text_x = Launcher_StartAlignedTextX(msg, x, w, align);
    DrawHZText12(msg, 0, text_x, y, colour, 1);
}


static void Launcher_DrawStartClock(u32 force)
{
    static u8 last_hh = 0xFF;
    static u8 last_mm = 0xFF;
    static u8 last_ss = 0xFF;
    u8 datetime[3];
    u8 HH;
    u8 MM;
    u8 SS;
    char msgtime[16];
    const int x = 240 - 3 - (8 * 6);
    const int y = 3;

    rtc_enable();
    rtc_gettime(datetime);
    rtc_disenable();
    delay(5);

    HH = UNBCD(datetime[0] & 0x3F);
    MM = UNBCD(datetime[1] & 0x7F);
    SS = UNBCD(datetime[2] & 0x7F);
    if(HH > 23) HH = 0;
    if(MM > 59) MM = 0;
    if(SS > 59) SS = 0;

    if(force || HH != last_hh || MM != last_mm || SS != last_ss)
    {
        Launcher_ClearWithThemeBG((const u16*)gImage_START, x, y, 8 * 6, 13);
        sprintf(msgtime, "%02u:%02u:%02u", HH, MM, SS);
        DrawHZText12(msgtime, 0, x, y, gl_color_topbar_text, 1);
        last_hh = HH;
        last_mm = MM;
        last_ss = SS;
    }
}


static void Launcher_DrawSettingsClock(u32 force)
{
    static u8 last_hh = 0xFF;
    static u8 last_mm = 0xFF;
    static u8 last_ss = 0xFF;
    u8 datetime[3];
    u8 HH;
    u8 MM;
    u8 SS;
    char msgtime[16];
    const int x = 240 - 3 - (8 * 6);
    const int y = 3;

    rtc_enable();
    rtc_gettime(datetime);
    rtc_disenable();
    delay(5);

    HH = UNBCD(datetime[0] & 0x3F);
    MM = UNBCD(datetime[1] & 0x7F);
    SS = UNBCD(datetime[2] & 0x7F);
    if(HH > 23) HH = 0;
    if(MM > 59) MM = 0;
    if(SS > 59) SS = 0;

    if(force || HH != last_hh || MM != last_mm || SS != last_ss)
    {
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, x, y, 8 * 6, 13);
        sprintf(msgtime, "%02u:%02u:%02u", HH, MM, SS);
        DrawHZText12(msgtime, 0, x, y, gl_color_topbar_text, 1);
        last_hh = HH;
        last_mm = MM;
        last_ss = SS;
    }
}

static void Launcher_DrawStartWindow(u32 selected)
{
    Launcher_DrawThemeBGFull((const u16*)gImage_START);
    Launcher_DrawTopbarName(START_win);
    Launcher_DrawStartClock(1);

    Launcher_DrawStartLastThumb(LAUNCHER_START_LAST_THUMB_X, LAUNCHER_START_LAST_THUMB_Y);

    Launcher_DrawStartOption(0, selected);
    Launcher_DrawStartOption(1, selected);
    Launcher_DrawStartOption(2, selected);
    Launcher_DrawStartOption(3, selected);
    Launcher_StartPreviewWarmAdjacent();
}

static void Launcher_UpdateStartSelection(u32 old_selected, u32 selected)
{
    if((old_selected == 0) || (selected == 0))
        Launcher_ResetStartTitleScroll();

    if(LAUNCHER_START_SELECTION_MODE)
    {
        Launcher_DrawStartOption(old_selected, selected);
        Launcher_DrawStartOption(selected, selected);
        return;
    }

    if(old_selected != selected)
    {
        Launcher_RestoreStartCorners(old_selected);
        if(LAUNCHER_START_SELECTION_ANIMATE && LAUNCHER_START_SELECTION_SHAPE == 0)
            Launcher_AnimateStartSelection(old_selected, selected);

        Launcher_DrawStartCorners(old_selected, selected);
        Launcher_DrawStartCorners(selected, selected);
        if(selected == 0 && LAUNCHER_START_SELECTION_SHAPE == 0)
            return;
        Launcher_DrawStartOption(selected, selected);
    }
    else
        Launcher_DrawStartOption(selected, selected);
}

static u32 Launcher_StartWindow(void)
{
    u32 selected = launcher_start_selected;
    u32 old_selected = selected;
    u32 new_selected;
    u32 dirty = 1;

    /* Keep p_recently_play[0] primed with the most recent game whenever
       the start screen is entered.  The Last Played boot menu path uses
       SD_list_MENU(..., play_re = 0), so this avoids relying on whatever
       the hidden SD/recent-list state happened to contain. */
    Launcher_PrepareLastPlayedForMenu();
    Launcher_ApplyThemeColours();

    if(selected > 3)
        selected = 1;
    old_selected = selected;

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawStartClock(0);

        if(dirty)
        {
            Launcher_ResetStartTitleScroll();
            Launcher_DrawStartWindow(selected);
            dirty = 0;
        }
        else
            Launcher_UpdateStartTitleScroll(selected);

        scanKeys();
        {
            u16 keysdown = keysDown();

            if(keysdown & KEY_DOWN)
            {
                new_selected = selected;
                if(LAUNCHER_START_NAV_MODE == 1)
                {
                    if(selected < 3)
                        new_selected = selected + 1;
                }
                else
                {
                    if(selected == 0)
                        new_selected = 1;
                    else if((selected == 1) || (selected == 2))
                        new_selected = 3;
                }

                if(new_selected != selected)
                {
                    selected = new_selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    Launcher_UpdateStartSelection(old_selected, selected);
                    old_selected = selected;
                }
            }
            else if(keysdown & KEY_UP)
            {
                new_selected = selected;
                if(LAUNCHER_START_NAV_MODE == 1)
                {
                    if(selected > 0)
                        new_selected = selected - 1;
                }
                else
                {
                    if(selected == 3)
                        new_selected = 1;
                    else if((selected == 1) || (selected == 2))
                        new_selected = 0;
                }

                if(new_selected != selected)
                {
                    selected = new_selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    Launcher_UpdateStartSelection(old_selected, selected);
                    old_selected = selected;
                }
            }
            else if(keysdown & (KEY_RIGHT | KEY_R))
            {
                if(selected == 0)
                {
                    if(Launcher_CanCycleStartFavourite())
                    {
                        Launcher_CycleStartFavourite(1);
                        Launcher_ResetStartTitleScroll();
                        UIAudio_PlaySfx(UI_SFX_MOVE);
                        Launcher_DrawStartLastThumb(LAUNCHER_START_LAST_THUMB_X, LAUNCHER_START_LAST_THUMB_Y);
                        Launcher_DrawStartOption(0, selected);
                        Launcher_StartPreviewWarmAdjacent();
                    }
                }
                else if(selected == 1)
                {
                    if(LAUNCHER_START_NAV_MODE == 0)
                    {
                        selected = 2;
                        UIAudio_PlaySfx(UI_SFX_MOVE);
                        Launcher_UpdateStartSelection(old_selected, selected);
                        old_selected = selected;
                    }
                }
            }
            else if(keysdown & (KEY_LEFT | KEY_L))
            {
                if(selected == 0)
                {
                    if(Launcher_CanCycleStartFavourite())
                    {
                        Launcher_CycleStartFavourite(-1);
                        Launcher_ResetStartTitleScroll();
                        UIAudio_PlaySfx(UI_SFX_MOVE);
                        Launcher_DrawStartLastThumb(LAUNCHER_START_LAST_THUMB_X, LAUNCHER_START_LAST_THUMB_Y);
                        Launcher_DrawStartOption(0, selected);
                        Launcher_StartPreviewWarmAdjacent();
                    }
                }
                else if(selected == 2)
                {
                    if(LAUNCHER_START_NAV_MODE == 0)
                    {
                        selected = 1;
                        UIAudio_PlaySfx(UI_SFX_MOVE);
                        Launcher_UpdateStartSelection(old_selected, selected);
                        old_selected = selected;
                    }
                }
            }
            else if(keysdown & KEY_SELECT)
            {
                Launcher_CycleStartSource();
                Launcher_ResetStartTitleScroll();
                Launcher_DrawStartLastThumb(LAUNCHER_START_LAST_THUMB_X, LAUNCHER_START_LAST_THUMB_Y);
                Launcher_DrawStartOption(0, selected);
                Launcher_StartPreviewWarmAdjacent();
                UIAudio_PlaySfx(UI_SFX_MENU);
                Launcher_WaitForMenuKeyRelease(KEY_SELECT);
                Launcher_FlushInputForModal();
                launcher_suppress_next_select_cycle = 1;
                launcher_select_release_cooldown = 60;
            }
            else if(keysdown & (KEY_A | KEY_START))
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_WaitForMenuKeyRelease(KEY_A | KEY_START);
                launcher_start_selected = selected;
                if(selected == 0)
                    return 4; /* Last Played */
                if(selected == 1)
                    return 0; /* SD */
                if(selected == 2)
                    return 2; /* NOR */
                return 1;     /* Settings */
            }
        }
    }
}

static u32 Launcher_SettingsWindow(void)
{
    const u32 total = SETTINGS_TOTAL;
    const u32 visible = 9;
    u32 selected = 0;
    u32 top = 0;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawSettingsClock(0);
        if(scroll_delay > 0)
            scroll_delay--;

        if(dirty)
        {
        Launcher_SettingsDrawList(DSTEXT_SETTINGS_TITLE, total, selected, top, Launcher_SettingsGetLine);
            dirty = 0;
        }

        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();

            if(keysdown & KEY_B)
            {
                UIAudio_PlayBack();
                return 3; /* Back to start screen. */
            }
            if(keysdown & KEY_L)
            {
                UIAudio_PlaySfx(UI_SFX_TAB);
                return 2; /* Settings -> NOR. */
            }
            if(keysdown & KEY_R)
            {
                UIAudio_PlaySfx(UI_SFX_TAB);
                return 0; /* Settings -> SD. */
            }
            if((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && scroll_delay == 0))
            {
                if(selected + 1 < total)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
                    selected++;
                    if(selected >= top + visible)
                        top = selected - visible + 1;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                    {
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_SettingsGetLine);
                    }
                }
            }
            else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && scroll_delay == 0))
            {
                if(selected > 0)
                {
                    u32 old_selected = selected;
                    u32 old_top = top;
                    u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
                    selected--;
                    if(selected < top)
                        top = selected;
                    UIAudio_PlaySfx(UI_SFX_MOVE);
                    scroll_delay = first_press ? 10 : 5;
                    if(top == old_top)
                    {
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                    {
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_SettingsGetLine);
                    }
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT))
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                if(selected == SETTINGS_HELP)
                {
                    UIAudio_StopForSharedBufferUse();
                    Launcher_ShowHelpBOnly();
                }
                else
                {
                    Launcher_SettingsToggle(selected, 1);
                }
                dirty = 1;
            }
            else if(keysdown & KEY_LEFT)
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                if(selected != SETTINGS_HELP)
                    Launcher_SettingsToggle(selected, -1);
                dirty = 1;
            }
        }
    }
}

static u32 Launcher_Setting_window2(void)
{
    return Launcher_SettingsWindow();
}

static u32 Get_path_depth(const TCHAR *path)
{
	u32 depth = 1;
	const TCHAR *p = path;
	if(!path || path[0] == '\0' || (path[0] == '/' && path[1] == '\0'))
		return 1;
	while(*p)
	{
		if(*p == '/')
			depth++;
		p++;
	}
	return depth;
}

//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
//---------------------------------------------------------------------------------
// Program entry point
//---------------------------------------------------------------------------------
int main(void) {

	irqInit();
	irqEnable(IRQ_VBLANK);

	REG_IME = 1;

	u32 res;
	u32 game_folder_total;
	u32 file_select;
	u32 show_offset;
	u32 updata;
	u32 continue_MENU;
	PAGE_NUM page_num=SD_list;
	u32 page_mode;
	u32 shift;
	u32 short_filename=0;
	u32 startup_resume_pending = 0;
	u32 startup_quicklaunch_pending = 0;
	u16 startup_keys_held = 0;
	u32 launcher_last_played_launch_pending = 0;
	u32 start_screen_pending = 1;
	u32 resume_last_found = 0;
	
	u8 error_num;

	//TCHAR savfilename[100];		
	//BYTE saveMODE;
		
	gl_currentpage = 0x8002 ;//kernel mode

	SetMode (MODE_3 | BG2_ENABLE );
	
	SD_Disable();	
	Set_RTC_status(1);

	//check FW
	scanKeys();
	u16 keys = keysDown();	
	
	Check_FW_update();
	/*else if(keys & KEY_L) {
		Check_FW_update(Current_FW_ver,Built_in_ver);
	}*/
		
		
	DrawPic((u16*)gImage_splash, 0, 0, 240, 160, 0, 0, 1);	
	UIAudio_Init();
	UIAudio_PlayStartup();
	{
		u32 splash_wait;
		for(splash_wait = 0; splash_wait < UIAudio_GetStartupSplashFrames(); splash_wait++)
		{
			VBlankIntrWait();
			UIAudio_Update();
		}
	}
	scanKeys();
	startup_keys_held = keysHeld();
	startup_quicklaunch_pending = 0;
	CheckLanguage();	
	CheckSwitch();
	Launcher_InitThumbCache();

	res = f_mount(&EZcardFs, "", 1);
	if( res != FR_OK)
	{
		DrawHZText12(gl_init_error,0,2,20, gl_color_cheat_black,1);
		DrawHZText12(gl_power_off,0,2,33, gl_color_cheat_black,1);
		while(1);
	}
	else
	{
	}
	VBlankIntrWait();	
	Launcher_LoadTheme();
	Launcher_ReadLanguageSetting();
	Launcher_ReadSystemName();
	Launcher_ReadThumbnailStyle();
	Launcher_ReadSoundsSetting();
	Launcher_ReadAutoStartKey();
	Launcher_ReadStartSource();
	Launcher_LoadFavourites();
	Read_last_launch_mode();
	Launcher_SaveMigratedSettingsIfNeeded();
	startup_quicklaunch_pending = (startup_keys_held & Launcher_AutoStartKeyMask()) ? 1 : 0;
	
	Check_save_flag();

	f_chdir("/");
	//TCHAR currentpath[MAX_path_len];
	memset(currentpath,00,MAX_path_len);
	memset(currentpath_temp,0x00,MAX_path_len);
	folder_select = 1;
	memset(p_folder_select_show_offset,0x00,sizeof(p_folder_select_show_offset));
	memset(p_folder_select_file_select,0x00,sizeof(p_folder_select_file_select));
	gl_nor_show_offset_saved = 0;
	gl_nor_file_select_saved = 0;
	
	res = f_getcwd(currentpath, sizeof currentpath / sizeof *currentpath);
	if((gl_resume_last_on || startup_quicklaunch_pending) && Read_last_played_entry(currentpath, sizeof currentpath, current_filename, sizeof current_filename))
	{
		folder_select = Get_path_depth(currentpath);
		f_chdir(currentpath);
		startup_resume_pending = gl_resume_last_on ? 1 : 0;
		resume_last_found = gl_resume_last_on ? 1 : 0;
	}
	else
	{
		memset(current_filename, 0x00, sizeof(current_filename));
	}
	Launcher_SaveSDState();

	Read_NOR_info();	
	gl_norOffset = 0x000000;
	game_total_NOR = GetFileListFromNor();//initialize to prevent direct writes to NOR without page turning
	if(game_total_NOR==0)
	{
		memset(pNorFS,00,sizeof(FM_NOR_FS)*MAX_NOR);
		Save_NOR_info((u16*)pNorFS,sizeof(FM_NOR_FS)*MAX_NOR);
	}

refind_file:
	Launcher_ResetThumbCache();
	

	if(page_num== SD_list)
	{
		Launcher_RestoreSDState();
		folder_total = 0;
		game_total_SD = 0; 

		if(recents_view_active)
		{
			game_total_SD = recents_view_favourites ? Build_favourites_virtual_list() : Build_recent_virtual_list();
			game_folder_total = game_total_SD;
		}
		else
		{
			res = f_opendir(&dir,currentpath);
			if (res == FR_OK)
			{
				while(1)
				{
					res = f_readdir(&dir, &fileinfo);                   //read next
					//DEBUG_printf("=%x %s %x %x",res, fileinfo.fname,fileinfo.fname[0],fileinfo.fattrib);
					//wait_btn();								
					if (res != FR_OK || fileinfo.fname[0] == 0) break;

					if(	(fileinfo.fattrib == AM_DIR) || (fileinfo.fattrib == 0x30))//DIR and exFAT dir
					{
						if ( folder_total >= MAX_folder )//cut
	      					break;
						memcpy(pFolder[folder_total].filename,fileinfo.fname,100);
						pFolder[folder_total++].filename[99] = 0;
					}
					else if(	(fileinfo.fattrib == AM_ARC) || (fileinfo.fattrib == 0x21) )
					{
						if ( game_total_SD >= MAX_files )//cut
	      					break;
						memcpy(pFilename_buffer[game_total_SD].filename,fileinfo.fname,100);
						pFilename_buffer[game_total_SD].filename[99] = 0;
						pFilename_buffer[game_total_SD++].filesize = fileinfo.fsize;
					}
				}
				f_closedir(&dir);
			}
		
			game_folder_total = folder_total + game_total_SD;
		
			Sort_folder(folder_total);//folder
			Sort_file(game_total_SD);//file
		}
  }  else
  {
		recents_view_active = 0;
  	Read_NOR_info();
 		gl_norOffset = 0x000000;
		game_total_NOR = GetFileListFromNor();
  }
  
  if(page_num==SD_list)
  {
		if(recents_view_active)
		{
			file_select = recents_saved_file_select;
			show_offset = recents_saved_show_offset;
		}
		else{
			/* Restore the saved SD selection for every folder level, including
			   the root.  Settings -> SD previously fell back to 0/0 at root,
			   unlike NOR -> SD, so the highlighted item was lost. */
			file_select = p_folder_select_file_select[folder_select];
			show_offset = p_folder_select_show_offset[folder_select];
		}
		if(startup_resume_pending || startup_quicklaunch_pending)
		{
			if(Apply_last_played_selection(&show_offset, &file_select))
			{
				p_folder_select_show_offset[folder_select] = show_offset;
				p_folder_select_file_select[folder_select] = file_select;
			}
			startup_resume_pending = 0;
		}

		/* Clamp restored SD selection after changing folders so the launcher
		   cannot point past the rebuilt directory contents and render blank. */
		if(show_offset + file_select >= game_folder_total)
		{
			if(game_folder_total == 0)
			{
				show_offset = 0;
				file_select = 0;
			}
			else if(game_folder_total <= 10)
			{
				show_offset = 0;
				file_select = game_folder_total - 1;
			}
			else
			{
				show_offset = game_folder_total - 10;
				file_select = 9;
			}
		}
  }
  else
  {
		show_offset = gl_nor_show_offset_saved;
		file_select = gl_nor_file_select_saved;
		if(file_select > 9) file_select = 9;
		if(show_offset + file_select >= game_total_NOR)
		{
			if(game_total_NOR == 0)
			{
				show_offset = 0;
				file_select = 0;
			}
			else if(game_total_NOR <= 10)
			{
				show_offset = 0;
				file_select = game_total_NOR - 1;
			}
			else
			{
				show_offset = game_total_NOR - 10;
				file_select = 9;
			}
		}
  }
	if(startup_quicklaunch_pending && (page_num == SD_list) && game_folder_total && (show_offset + file_select >= folder_total))
	{
		u16 old_boot_mode_pref = gl_boot_mode_pref;
		gl_boot_mode_pref = Read_last_launch_mode() ? 0x2 : 0x1;
		startup_quicklaunch_pending = 0;
		UIAudio_StopForSharedBufferUse();
		res = SD_list_MENU(show_offset, file_select, 0xBB);
		gl_boot_mode_pref = old_boot_mode_pref;
		if(res)
		{
			if(res == 2)
			{
				recents_view_active = 0;
				page_num = NOR_list;
			}
			goto refind_file;
		}
	}
	startup_quicklaunch_pending = 0;

	if(launcher_last_played_launch_pending && (page_num == SD_list) && game_folder_total && (show_offset + file_select >= folder_total))
	{
		launcher_last_played_launch_pending = 0;
		UIAudio_StopForSharedBufferUse();
		Launcher_FlushInputForModal();
		res = SD_list_MENU(show_offset, file_select, 0xBB);
		if(res)
		{
			if(res == 2)
			{
				recents_view_active = 0;
				page_num = NOR_list;
			}
			goto refind_file;
		}

		/* Last Played was launched from the start screen.  If the boot
		   menu is cancelled, return to the start screen instead of falling
		   through to the SD list.  The boot menu itself is drawn over the
		   existing start screen because the SD list has not been redrawn. */
		page_num = START_win;
		launcher_start_selected = 0;
		launcher_force_full_redraw = 1;
		goto re_showfile;
	}
	launcher_last_played_launch_pending = 0;

	if(start_screen_pending)
	{
		/* If Resume last is enabled and a recent entry was found, keep the
		   original behaviour of going straight to that SD folder/selection
		   instead of showing the new start screen. */
		if(launcher_start_screen_off || resume_last_found)
		{
			page_num = SD_list;
			launcher_start_selected = 0;
		}
		else
		{
			page_num = START_win;
		}
		start_screen_pending = 0;
	}

	continue_MENU = 0;
	
	u32 haveThumbnail;
	u32 is_GBA_old=0;
	u32 is_GBA;
	
	u32 play_re;
	play_re = 0xBB;
//NOR_list:
//SD_list:
re_showfile:
	launcher_active_page = page_num;
	Launcher_ResetThumbCache();
	
	shift =0;
	page_mode=0;
  updata=1;
	static u32 sd_topbar_initialised = 0;
	static u32 nor_topbar_initialised = 0;
	if(launcher_force_full_redraw)
	{
		if(page_num == SD_list)
		{
			Launcher_DrawThemeBGFull(Launcher_GetBGImage());
			sd_topbar_initialised = 1;
			launcher_system_name_dirty = 1;
		}
		else if(page_num == NOR_list)
		{
			Launcher_DrawThemeBGFull(Launcher_GetBGImage());
			nor_topbar_initialised = 1;
			launcher_system_name_dirty = 1;
		}
		launcher_force_full_redraw = 0;
	}
  u32 key_L=0;
	u32 select_tap_pending = 0;
	u32 select_tap_timer = 0;
	u32 select_double_handled = 0;
	if(launcher_suppress_next_select_cycle)
	{
		select_double_handled = 1;
		launcher_select_release_cooldown = 60;
		launcher_suppress_next_select_cycle = 0;
	}
	u32 start_hold_frames = 0;
	u32 start_long_delete_done = 0;
	u32 launcher_nav_repeat_delay = 0;
	setRepeat(5,1);
	
	/* Do not redraw the full SD background here. Folder enter/exit should
	   fall through to the targeted updata==1 redraw path below so the clock
	   and file counter bands can be preserved. On the very first SD load,
	   however, we still need to paint the whole top bar background once. */
	while(1)
	{
		while(1)//2
		{
			VBlankIntrWait();
			VBlankIntrWait();			
			if((shift==0) || (gl_show_Thumbnail==0)){
				short_filename = 0;				
			}
			if(shift==0){
				dwName =0;
			}			
			shift++;
			
			haveThumbnail = 0;
			is_GBA = 0;
			launcher_active_page = page_num;
			
			if(updata && gl_show_Thumbnail && ((page_num == SD_list) || (page_num == NOR_list)))
			{
				u32 absolute_index = show_offset + file_select;
				LauncherEntryInfo selected_info;

				memset(&selected_info, 0, sizeof(selected_info));
				selected_info.thumb_data = pReadCache + 0x10036;
				if(launcher_cache_center_index != absolute_index)
					Launcher_BuildThumbCache(absolute_index);
				Launcher_GetEntryInfo(absolute_index, &selected_info);

				if(selected_info.name && !selected_info.is_folder && !Launcher_IsNORPage())
				{
					u32 strlengba = strlen(selected_info.name);
					if((strlengba >= 3) && !strcasecmp(&(selected_info.name[strlengba-3]), "gba"))
					{
						is_GBA = 1;
						haveThumbnail = launcher_cache_selected.has_thumbnail;
						short_filename = 1;
					}
				}
				else if(is_GBA_old==1)
				{
					updata = 1;
				}

				is_GBA_old = is_GBA;
			}
	    if(updata==1){//reshow all
	    	if(page_num==SD_list)
	    	{
	    		if(!sd_topbar_initialised)
	    		{
	    			Launcher_DrawThemeBGFull(Launcher_GetBGImage());
	    			sd_topbar_initialised = 1;
	    		}
	    		else
	    		{
	    			/* Preserve the custom top-bar name.  Clearing this band on every
	    			   SD refresh made /SYSTEM/NAME.TXT disappear or flicker while
	    			   moving around the list/thumbnail views. */
	    			if(!launcher_system_name[0])
	    				Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 0, 90, 20);
	    			if(!gl_show_Thumbnail)
	    				Launcher_ClearWithThemeBG(Launcher_GetBGImage(),185, 0, 6*9+1, 18);
	    		}
	    		gl_clock_dirty = 1;
	    		if(!gl_show_Thumbnail)
	    		{
	    			Launcher_ClearTextBodyBackground();
	    		}
	    		else if(game_folder_total == 0)
	    		{
	    			/* In the modern thumbnail launchers, an empty folder would otherwise
	    			   skip the draw path entirely and leave the previous folder contents
	    			   visible. Clear the launcher body so the new empty state is obvious. */
	    			Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 20, 240, 160-20);
	    		}
	    		if(!gl_show_Thumbnail)
	    		{
	    			Show_ICON_filename_SD(show_offset,file_select,gl_show_Thumbnail&&is_GBA);
	    		}
	    	}
	    	else if(page_num==START_win)/* start screen */
	    	{
					res = Launcher_StartWindow();
					if(res == 4){
						TCHAR saved_path[MAX_path_len];
						TCHAR saved_filename[200];
						u32 saved_folder_select = folder_select;
						u32 saved_show_offset = show_offset;
						u32 saved_file_select = file_select;
						u8 menu_res;

						memset(saved_path, 0x00, sizeof(saved_path));
						memset(saved_filename, 0x00, sizeof(saved_filename));
						strncpy(saved_path, currentpath, sizeof(saved_path) - 1);
						strncpy(saved_filename, current_filename, sizeof(saved_filename) - 1);

						if(Launcher_PrepareLastPlayedForMenu())
						{
							/* Open/launch the recent game directly through the recent-entry path.
							   p_recently_play[0] is prepared as the latest game whenever the start
							   screen is entered, so this no longer depends on the hidden SD cursor
							   or a previously-built Recently Played screen. */
							UIAudio_StopForSharedBufferUse();
							Launcher_FlushInputForModal();
							menu_res = SD_list_MENU(0, 0, 0);

							/* SD_list_MENU temporarily switches currentpath/current_filename to the
							   recent game.  If it returns, restore the user's actual SD browsing
							   state so launching from the start screen never moves the SD view. */
							strncpy(currentpath, saved_path, sizeof(currentpath) - 1);
							currentpath[sizeof(currentpath) - 1] = '\0';
							strncpy(current_filename, saved_filename, sizeof(current_filename) - 1);
							current_filename[sizeof(current_filename) - 1] = '\0';
							folder_select = saved_folder_select;
							show_offset = saved_show_offset;
							file_select = saved_file_select;
							f_chdir(currentpath);

							if(menu_res)
							{
								if(menu_res == 2)
								{
									recents_view_active = 0;
									page_num = NOR_list;
								}
								goto refind_file;
							}

							launcher_start_selected = 0;
							page_num = START_win;
							launcher_force_full_redraw = 1;
							goto re_showfile;
						}
						else
						{
							launcher_start_selected = 0;
							page_num = START_win;
						}
					}
					else if(res == 2){
						recents_view_active = 0;
						Launcher_DrawThemeBGFull(Launcher_GetBGImage());
						page_num = NOR_list;
					}
					else if(res == 1){
						Launcher_DrawThemeBGFull((const u16*)gImage_SET);
						page_num = SET_win;
					}
					else{
						launcher_sd_restore_pending = 1;
						Launcher_DrawThemeBGFull(Launcher_GetBGImage());
						page_num = SD_list;
						launcher_vertical_folder_label_dirty = 1;
						goto refind_file;
					}
					launcher_vertical_folder_label_dirty = 1;
					goto re_showfile;
	    	}
	    	else if(page_num==SET_win)/* settings */
	    	{
					Launcher_DrawThemeBGFull((const u16*)gImage_SET);
					res = Launcher_SettingsWindow();
					if(res == 0)
					{
						launcher_sd_restore_pending = 1;
						Launcher_DrawThemeBGFull(Launcher_GetBGImage());
						page_num = SD_list;
						launcher_start_selected = 1;
						launcher_vertical_folder_label_dirty = 1;
						goto refind_file;
					}
					else if(res == 2)
					{
						recents_view_active = 0;
						Launcher_DrawThemeBGFull(Launcher_GetBGImage());
						page_num = NOR_list;
						launcher_start_selected = 2;
					}
					else
					{
						if(launcher_start_screen_off)
						{
							launcher_sd_restore_pending = 1;
							Launcher_DrawThemeBGFull(Launcher_GetBGImage());
							page_num = SD_list;
						}
						else
						{
							launcher_start_selected = 3;
							page_num = START_win;
						}
					}
					launcher_vertical_folder_label_dirty = 1;
					goto re_showfile;
	    	}
	    	else if(page_num==HELP)//legacy help window, no longer reachable by shoulder navigation
	    	{
					UIAudio_StopForSharedBufferUse();
					Launcher_ShowHelpBOnly();
					Launcher_DrawThemeBGFull((const u16*)gImage_SET);
					page_num = SET_win;//	
					goto re_showfile;
	    	}
	    	else
	    	{
				if(!nor_topbar_initialised)
				{
	      			Launcher_DrawThemeBGFull(Launcher_GetBGImage());
					nor_topbar_initialised = 1;
				}
				else
				{
					/* Preserve the custom top-bar name during NOR refreshes as well. */
					if(!launcher_system_name[0])
						Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 0, 90, 20);
					if(!gl_show_Thumbnail)
						Launcher_ClearWithThemeBG(Launcher_GetBGImage(),185, 0, 6*9+1, 18);
				}
	    		gl_clock_dirty = 1;
				if(!gl_show_Thumbnail)
				{
					Launcher_ClearTextBodyBackground();
				}
				else if(game_total_NOR == 0)
				{
					Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 20, 240, 160-20);
				}
				if(!gl_show_Thumbnail)
				{
					Show_ICON_filename_NOR(show_offset,file_select);
				}
	    	}
	    	Show_game_num(file_select+show_offset+1,page_num);
	  	}
	  	else if(updata >1){
	    	if(page_num==NOR_list)
	    	{
				if(!gl_show_Thumbnail)
					Refresh_filename_NOR(show_offset,file_select,updata);
	    	}
	    	else
	    	{
	    		if(!gl_show_Thumbnail)
	    			Refresh_filename(show_offset,file_select,updata,gl_show_Thumbnail&&is_GBA);
	    	}
	    	Show_game_num(file_select+show_offset+1,page_num );
	  	}
	  	
		if( updata && gl_show_Thumbnail && ((page_num==SD_list) || (page_num==NOR_list)) && ((page_num==NOR_list) ? game_total_NOR : game_folder_total) )
		{
			if(gl_show_Thumbnail == 1)
				Draw_ModernLauncher_SD(show_offset, file_select, haveThumbnail);
			else
			{
				Draw_ModernLauncher_SD_Vertical_State(show_offset, file_select);
			}
			Launcher_DrawTopbarName(page_num);
		}

			if(updata)
			{
				ShowTime(page_num,page_mode);
				Launcher_DrawTopbarName(page_num);
			}

			if(continue_MENU) break;
			if(page_num==SD_list){
				if(game_folder_total && !gl_show_Thumbnail)
					Filename_loop(shift,show_offset,file_select,short_filename);
			}
				
	    updata=0;
			scanKeys();
			u16 keysdown  = keysDown();
			u16 keys_released = keysUp();
			u16 keysheld = keysHeld();
			u16 keysrepeat = keysDownRepeat();
			u16 launcher_nav_repeat_mask = 0;
			u16 launcher_horz_step_forward = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_DOWN : KEY_RIGHT;
			u16 launcher_horz_step_back = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_UP : KEY_LEFT;
			u16 launcher_horz_jump_forward = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_RIGHT : KEY_DOWN;
			u16 launcher_horz_jump_back = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_LEFT : KEY_UP;
			u16 launcher_vert_step_forward = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_RIGHT : KEY_DOWN;
			u16 launcher_vert_step_back = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_LEFT : KEY_UP;
			u16 launcher_vert_jump_forward = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_DOWN : KEY_RIGHT;
			u16 launcher_vert_jump_back = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_UP : KEY_LEFT;

			if((page_num == SD_list) || (page_num == NOR_list))
			{
				/* Keep held navigation from running at the maximum key-repeat rate in
				   fast folders, but do not add the heavy delay that made thumbnail
				   traversal feel sluggish.  One main-loop beat is enough to stop the
				   almost-instant runaway scroll while preserving responsiveness. */
				if(gl_show_Thumbnail == 1)
					launcher_nav_repeat_mask = launcher_horz_step_forward | launcher_horz_step_back;
				else if(gl_show_Thumbnail == 2)
					launcher_nav_repeat_mask = launcher_vert_step_forward | launcher_vert_step_back;
				else
					launcher_nav_repeat_mask = KEY_UP | KEY_DOWN;

				if(!(keysheld & launcher_nav_repeat_mask))
					launcher_nav_repeat_delay = 0;
				else if((keysrepeat & launcher_nav_repeat_mask) && launcher_nav_repeat_delay)
				{
					keysrepeat &= ~launcher_nav_repeat_mask;
					launcher_nav_repeat_delay--;
				}
			}

			u16 audio_keysdown = keysdown;
			u16 audio_keysrepeat = 0;

			if(launcher_select_release_cooldown)
				launcher_select_release_cooldown--;

			if((page_num == SD_list) || (page_num == NOR_list))
			{
				if(!gl_show_Thumbnail)
				{
					audio_keysrepeat = keysrepeat & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
				}
				else
				{
					audio_keysdown &= ~(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT);
				}
				if((page_num == NOR_list) || (page_num == SD_list))
					audio_keysdown &= ~(KEY_START | KEY_SELECT);
			}

			UIAudio_HandleKeysEx(audio_keysdown, 0, 0, 0);

			if((keysdown & KEY_SELECT) && launcher_select_release_cooldown)
			{
				select_double_handled = 1;
				select_tap_pending = 0;
				select_tap_timer = 0;
			}
			else if(keysdown & KEY_SELECT)
			{
				select_double_handled = 0;
				if(select_tap_pending && select_tap_timer)
				{
					select_double_handled = 1;
					select_tap_pending = 0;
					select_tap_timer = 0;
					launcher_select_release_cooldown = 20;
					if((page_num == SD_list) && !recents_view_active && (show_offset + file_select >= folder_total) &&
					   Launcher_IsLaunchableFilename(pFilename_buffer[show_offset + file_select - folder_total].filename))
					{
						UIAudio_PlaySfx(UI_SFX_MENU);
						p_folder_select_show_offset[folder_select] = show_offset;
						p_folder_select_file_select[folder_select] = file_select;
						Launcher_SaveSDState();
						Launcher_FavouritePrompt(show_offset, file_select);
						Launcher_WaitForMenuKeyRelease(KEY_SELECT);
						select_tap_pending = 0;
						select_tap_timer = 0;
						select_double_handled = 1;
						launcher_select_release_cooldown = 40;
						launcher_vertical_folder_label_dirty = 1;
						goto refind_file;
					}
					else if((page_num == SD_list) && recents_view_active && recents_view_favourites && ((show_offset + file_select) < game_total_SD))
					{
						UIAudio_PlaySfx(UI_SFX_MENU);
						Launcher_FavouritePromptFullPath(p_recently_play[show_offset + file_select]);
						Launcher_WaitForMenuKeyRelease(KEY_SELECT);
						select_tap_pending = 0;
						select_tap_timer = 0;
						select_double_handled = 1;
						launcher_select_release_cooldown = 40;
						launcher_vertical_folder_label_dirty = 1;
						goto refind_file;
					}
				}
			}
			if(keysdown & KEY_START)
			{
				/* If the previous long-delete consumed its release inside the delete
				   prompt, do not let that stale suppression eat a later genuine
				   short START press. */
				launcher_start_release_suppressed = 0;
				start_hold_frames = 0;
				start_long_delete_done = 0;
			}

			if(page_num==NOR_list)
			{
				list_game_total = game_total_NOR;
			}
			else
			{
				list_game_total = game_folder_total;
			}

			if(select_tap_pending && select_tap_timer && launcher_select_release_cooldown)
			{
				select_tap_pending = 0;
				select_tap_timer = 0;
			}
			else if(select_tap_pending && select_tap_timer)
			{
				if(select_tap_timer > 2)
					select_tap_timer -= 2;
				else
				{
					select_tap_pending = 0;
					select_tap_timer = 0;
					Launcher_CycleViewModeAndRedraw(page_num, show_offset, file_select, &updata);
				}
			}


			u32 thumbnail_nav_handled = 0;
			if((gl_show_Thumbnail == 2) && ((page_num==SD_list) || (page_num==NOR_list)))
			{
				if ((keysrepeat & launcher_vert_step_forward) || (keysrepeat & launcher_vert_step_back))
				{
					int move = (keysrepeat & launcher_vert_step_forward) ? 1 : -1;
					u32 changed = 0;

					if((move > 0) && ((show_offset + file_select + 1) < list_game_total))
					{
						if(file_select < 9)
							file_select++;
						else
							show_offset++;
						changed = 1;
					}
					else if((move < 0) && ((show_offset + file_select) > 0))
					{
						if(file_select > 0)
							file_select--;
						else if(show_offset > 0)
							show_offset--;
						changed = 1;
					}

					if(changed)
					{
						Launcher_ShiftThumbCache(move, show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
						shift = 0;
					}
					thumbnail_nav_handled = 1;
				}
				else if(keysrepeat & launcher_vert_jump_forward)
				{
					u32 absolute_index = show_offset + file_select;
					u32 new_absolute_index = absolute_index;

					if(list_game_total)
					{
						new_absolute_index += 10;
						if(new_absolute_index >= list_game_total)
							new_absolute_index = list_game_total - 1;
					}

					if(new_absolute_index != absolute_index)
					{
						show_offset = (new_absolute_index / 10) * 10;
						file_select = new_absolute_index % 10;
						if((show_offset + file_select >= list_game_total) && list_game_total)
							file_select = (list_game_total - 1) - show_offset;

						Launcher_BuildThumbCache(show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
					}
					shift = 0;
					thumbnail_nav_handled = 1;
				}
				else if(keysrepeat & launcher_vert_jump_back)
				{
					u32 absolute_index = show_offset + file_select;
					u32 new_absolute_index = 0;

					if(absolute_index >= 10)
						new_absolute_index = absolute_index - 10;

					if(new_absolute_index != absolute_index)
					{
						show_offset = (new_absolute_index / 10) * 10;
						file_select = new_absolute_index % 10;

						Launcher_BuildThumbCache(show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
					}
					shift = 0;
					thumbnail_nav_handled = 1;
				}
			}
			if((gl_show_Thumbnail == 1) && ((page_num==SD_list) || (page_num==NOR_list)))
			{
				if ((keysrepeat & launcher_horz_step_forward) || (keysrepeat & launcher_horz_step_back))
				{
					int move = (keysrepeat & launcher_horz_step_forward) ? 1 : -1;
					u32 changed = 0;

					if((move > 0) && ((show_offset + file_select + 1) < list_game_total))
					{
						if(file_select < 9)
							file_select++;
						else
							show_offset++;
						changed = 1;
					}
					else if((move < 0) && ((show_offset + file_select) > 0))
					{
						if(file_select > 0)
							file_select--;
						else if(show_offset > 0)
							show_offset--;
						changed = 1;
					}

					if(changed)
					{
						Launcher_ShiftThumbCache(move, show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
						shift = 0;
					}
					thumbnail_nav_handled = 1;
				}
				else if(keysrepeat & launcher_horz_jump_forward)
				{
					u32 absolute_index = show_offset + file_select;
					u32 new_absolute_index = absolute_index;

					if(list_game_total)
					{
						new_absolute_index += 10;
						if(new_absolute_index >= list_game_total)
							new_absolute_index = list_game_total - 1;
					}

					if(new_absolute_index != absolute_index)
					{
						show_offset = (new_absolute_index / 10) * 10;
						file_select = new_absolute_index % 10;
						if((show_offset + file_select >= list_game_total) && list_game_total)
							file_select = (list_game_total - 1) - show_offset;

						Launcher_BuildThumbCache(show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
					}
					shift = 0;
					thumbnail_nav_handled = 1;
				}
				else if(keysrepeat & launcher_horz_jump_back)
				{
					u32 absolute_index = show_offset + file_select;
					u32 new_absolute_index = 0;

					if(absolute_index > 10)
						new_absolute_index = absolute_index - 10;

					if(new_absolute_index != absolute_index)
					{
						show_offset = (new_absolute_index / 10) * 10;
						file_select = new_absolute_index % 10;

						Launcher_BuildThumbCache(show_offset + file_select);
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ThumbNavRepeatDelay();
						updata = 1;
					}
					shift = 0;
					thumbnail_nav_handled = 1;
				}
			}
			if(!thumbnail_nav_handled && (keysrepeat  & KEY_DOWN)) {
				if (file_select + show_offset+1 < (list_game_total )) {
	        if ( file_select > 8 ){
	          if ( file_select == 9 ) {
	            show_offset++;
	            updata=1;
	          }
	        }else{
	          file_select++;
	          updata=2;
	        }
					UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
					shift = 0;
				}
			}
			else if(!thumbnail_nav_handled && (keysrepeat & KEY_UP))
			{
				if (file_select ) {
					file_select--;
					updata=3;
					UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
				}else{
					if (show_offset){
						show_offset--;
						updata=1;
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
					}
				}
				shift = 0;
			}
			else if(!thumbnail_nav_handled && (keysrepeat & KEY_LEFT))
			{
		    if ( show_offset )
		    {
		      if ( show_offset > 9 )
		        show_offset -= 10;
		      else
		        show_offset = 0;

		      updata=1;
		      UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
		    }
		    else{
		    	if(file_select){
		    		file_select=0;
		    		updata=1;
		    		UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
		   	 	}
		    }
		    shift = 0;
			}
			else if(!thumbnail_nav_handled && (keysrepeat & KEY_RIGHT))
			{
	      if ( show_offset + 10 < list_game_total )
	      {
	        if ( show_offset + 20 <= list_game_total )
	          show_offset += 10;
	        else
	          show_offset = list_game_total - 10;

					updata=1;
					UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = 1;
	      }
	      shift = 0;
			}
			else if((keysheld & KEY_START) && !start_long_delete_done)
			{
				if(start_hold_frames < 120)
					start_hold_frames += 2;
				if(start_hold_frames >= 120)
				{
					start_long_delete_done = 1;
					if((page_num == SD_list) && !recents_view_active && (show_offset + file_select >= folder_total))
					{
						UIAudio_PlaySfx(UI_SFX_MENU);
						u32 delete_confirmed;
						/* Preserve the current SD cursor before the delete popup.  The
						   rebuilt list restores from these per-folder slots, so without
						   this a cancel can jump back to the start of the folder. */
						p_folder_select_show_offset[folder_select] = show_offset;
						p_folder_select_file_select[folder_select] = file_select;
						Launcher_SaveSDState();
						delete_confirmed = SD_list_L_START(show_offset, file_select, folder_total);
						launcher_start_release_suppressed = 1;
						/* A long START hold is now delete, while a short START tap opens
						   Recently Played.  Consume the held START before returning to
						   the file list so the key-up cannot be interpreted as the
						   short-press Recently Played action after cancel/delete. */
						Launcher_WaitForMenuKeyRelease(KEY_START);
						/* Keep the long-press state latched until the following KEY_START
						   release is observed by the main loop. */
						start_hold_frames = 120;
						start_long_delete_done = 1;
						launcher_vertical_folder_label_dirty = 1;
						if(delete_confirmed)
							goto refind_file;
						goto re_showfile;
					}
				}
			}
			else if(keysdown & KEY_L)
			{
				if((page_num == SD_list) && recents_view_active)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					Launcher_SetRecentVirtualMode(!recents_view_favourites);
					goto refind_file;
				}
				else if(page_num == SD_list)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					p_folder_select_show_offset[folder_select] = show_offset;
					p_folder_select_file_select[folder_select] = file_select;
					Launcher_SaveSDState();
					launcher_start_selected = 3;
					page_num = SET_win;
					launcher_vertical_folder_label_dirty = 1;
					goto refind_file;
				}
				else if(page_num == NOR_list)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					gl_nor_show_offset_saved = show_offset;
					gl_nor_file_select_saved = file_select;
					launcher_start_selected = 1;
					launcher_sd_restore_pending = 1;
					page_num = SD_list;
					launcher_vertical_folder_label_dirty = 1;
					goto refind_file;
				}
			}
			else if(keys_released & KEY_L)
			{
				key_L = 0;
			}
			else if(keysdown & KEY_R)
			{
				if((page_num == SD_list) && recents_view_active)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					Launcher_SetRecentVirtualMode(!recents_view_favourites);
					goto refind_file;
				}
				else if(page_num == SD_list)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					p_folder_select_show_offset[folder_select] = show_offset;
					p_folder_select_file_select[folder_select] = file_select;
					Launcher_SaveSDState();
					launcher_start_selected = 2;
					recents_view_active = 0;
					page_num = NOR_list;
					launcher_vertical_folder_label_dirty = 1;
					goto refind_file;
				}
				else if(page_num == NOR_list)
				{
					UIAudio_PlaySfx(UI_SFX_TAB);
					gl_nor_show_offset_saved = show_offset;
					gl_nor_file_select_saved = file_select;
					launcher_start_selected = 3;
					page_num = SET_win;
					launcher_vertical_folder_label_dirty = 1;
					goto refind_file;
				}
			}   
			else if(keysdown & KEY_B)//return
			{
				if(page_num == NOR_list)
				{
					if(!launcher_start_screen_off)
					{
						UIAudio_PlayBack();
						gl_nor_show_offset_saved = show_offset;
						gl_nor_file_select_saved = file_select;
						launcher_start_selected = 2;
						page_num = START_win;
						updata = 1;
						shift = 0;
						goto refind_file;
					}
				}
				if(page_num == SD_list)
				{
					if(recents_view_active)
					{
						UIAudio_PlayBack();
						recents_view_active = 0;
						strncpy(currentpath, recents_return_path, sizeof(currentpath) - 1);
						currentpath[sizeof(currentpath) - 1] = '\0';
						folder_select = recents_return_folder_select;
						p_folder_select_show_offset[folder_select] = recents_return_show_offset;
						p_folder_select_file_select[folder_select] = recents_return_file_select;
						Launcher_SaveSDState();
						launcher_vertical_folder_label_dirty = 1;
						launcher_force_full_redraw = 1;
						goto refind_file;
					}
	   			//res = f_getcwd(currentpath, sizeof currentpath / sizeof *currentpath);
	   			if(strcmp(currentpath,"/") !=0 ){		
						UIAudio_PlayBack();
		    		dmaCopy(currentpath, currentpath_temp, MAX_path_len);
		    		TCHAR *p=strrchr(currentpath_temp, '/');
		    		memset(currentpath,0x00,MAX_path_len);
		    		strncpy(currentpath, currentpath_temp, p-currentpath_temp);
		    		if(currentpath[0]==0) currentpath[0]='/';
		    		
						res=f_chdir(currentpath);
						if(res != FR_OK){
							error_num = 10;
							Show_error_num(error_num);
							goto re_showfile;
						}						
						
						p_folder_select_show_offset[folder_select] = 0;//clean
						p_folder_select_file_select[folder_select] = 0;//clean
						if(folder_select){
							folder_select--;
						}
						launcher_vertical_folder_label_dirty = 1;												
				    goto refind_file;
			    }
			    else
			    {
						if(!launcher_start_screen_off)
						{
							UIAudio_PlayBack();
							p_folder_select_show_offset[folder_select] = show_offset;
							p_folder_select_file_select[folder_select] = file_select;
							Launcher_SaveSDState();
							launcher_start_selected = 1;
							page_num = START_win;
							updata = 1;
							shift = 0;
							goto refind_file;
						}
			    }
		  	}
			}
			else if(keys_released & KEY_SELECT)
			{
				if(select_double_handled || launcher_select_release_cooldown)
				{
					select_double_handled = 0;
					select_tap_pending = 0;
					select_tap_timer = 0;
				}
				else
				{
					select_tap_pending = 1;
					select_tap_timer = 24;
				}
			}
			else if(keysdown & KEY_A)
			{
				if(page_num==SD_list){
					//res = f_getcwd(currentpath, sizeof currentpath / sizeof *currentpath);		
		      if( show_offset+file_select <  folder_total)
		      {
						TCHAR nextpath[MAX_path_len];
						if(strcmp(currentpath,"/") !=0)
							snprintf(nextpath, sizeof(nextpath), "%s/%s", currentpath, pFolder[show_offset+file_select].filename);
						else
							snprintf(nextpath, sizeof(nextpath), "/%s", pFolder[show_offset+file_select].filename);
						strncpy(currentpath, nextpath, sizeof(currentpath) - 1);
						currentpath[sizeof(currentpath) - 1] = '\0';
						res=f_chdir(currentpath);
						if(res != FR_OK){
							error_num = 0;
							Show_error_num(error_num);
							goto re_showfile;
						}	
											
						p_folder_select_show_offset[folder_select] = show_offset;
						p_folder_select_file_select[folder_select] = file_select;
						folder_select++;
						launcher_vertical_folder_label_dirty = 1;

			      goto refind_file;
			    }
		      else{   //SD_list file
	      	launcher_force_full_redraw = 1;
	      		res = SD_list_MENU(show_offset,file_select, recents_view_active ? (show_offset + file_select) : play_re);
						if(res){
							if(res==2){
								recents_view_active = 0;
								page_num = NOR_list;
							}
							goto refind_file ;
						}
						else{
							goto re_showfile;
						} 
						//break;
					}
				}
				else{   //NOR gba file
					if(game_total_NOR){
						launcher_force_full_redraw = 1;
						res = NOR_list_MENU(show_offset,file_select);
						if(res){
							goto refind_file ;
						}
						else{
							goto re_showfile;
						} 
						//break;
					}
				} 
					
			}		
			else if((keys_released & KEY_START) && launcher_start_release_suppressed)
			{
				launcher_start_release_suppressed = 0;
				start_hold_frames = 0;
				start_long_delete_done = 0;
			}
			else if(keys_released & KEY_START)
			{
				if(!start_long_delete_done && (start_hold_frames < 120) && (page_num == SD_list))
				{
					UIAudio_PlaySfx(UI_SFX_MENU);
					if(recents_view_active)
					{
						Launcher_SetRecentVirtualMode(!recents_view_favourites);
					}
					else
					{
						recents_view_active = 1;
						strncpy(recents_return_path, currentpath, sizeof(recents_return_path) - 1);
						recents_return_path[sizeof(recents_return_path) - 1] = '\0';
						recents_return_show_offset = show_offset;
						recents_return_file_select = file_select;
						recents_return_folder_select = folder_select;
						Launcher_SaveSDState();
						Launcher_SetRecentVirtualMode(0);
					}
					goto refind_file;
				}
			}
			if(keys_released & KEY_START)
			{
				start_hold_frames = 0;
				start_long_delete_done = 0;
			}
				
			ShowTime(page_num,page_mode);
			Launcher_DrawTopbarName(page_num);
		}	//2
	}
}
//---------------------------------------------------------------
void Boot_NOR_game(u32 show_offset,	u32 file_select,u32 key_L)
{
	UIAudio_StopForSharedBufferUse();
	//TCHAR savfilename[100];	
	TCHAR *pfilename;
	u32 gamefilesize=0;
	BYTE SAVEMODE; 
	BYTE error_num;
	u32 res;
	
	Clear(0, 0, 240, 160, gl_color_cheat_black, 1);
	//DrawHZText12(gl_Loading,0,(240-strlen(gl_Loading)*6)/2,74, gl_color_text,1);

	init_FAT_table();		
	
	res = f_mkdir(SAVER_FOLDER);//"/SAVER"
	if((res != FR_OK) && (res != FR_EXIST)){
		error_num = 2;
		Show_error_num(error_num);
		return;
	}
		
	//boot nor game 
	pfilename = pNorFS[show_offset+file_select].filename;
	gamefilesize = pNorFS[show_offset+file_select].filesize;
	SAVEMODE = pNorFS[show_offset+file_select].savemode;	
	
	ShowbootProgress(gl_check_sav);				
	//memcpy(savfilename,pfilename,100);
	error_num = Process_savefile(0,pfilename,gamefilesize,SAVEMODE);
	if(error_num != 0){
		Show_error_num(error_num);
		return;
	}
	
	Set_64MROM_flag(pNorFS[show_offset+file_select].is_64MBrom);
	if(pNorFS[show_offset+file_select].have_patch && pNorFS[show_offset+file_select].have_RTS)
	{	
		ShowbootProgress(gl_check_RTS);		
		u32 size = Check_RTS(pfilename);
		if(size ==0)
		{
			error_num = 6;
			Show_error_num(error_num);
			return;
		}
	}
			
	FAT_table_buffer[0x1F4/4] = SET_PARAMETER_MODE;
	Send_FATbuffer(FAT_table_buffer,1); //only RTS FAT and some parameter
	//wait_btn();
	u8 reset_choice;
	if(key_L)
		reset_choice = !gl_toggle_reset;
	else
		reset_choice = gl_toggle_reset;
	SetRompageWithHardReset(pNorFS[show_offset+file_select].rompage,reset_choice);
	while(1);	
}
//---------------------------------------------------------------
u8 FRAM_save_op(u8 OP)
{
	u32 res;
	u32 savefilesize=0;	
	TCHAR savfilename[100];
	u32 strlen8;

	TCHAR *pfilename;
	BYTE SAVEMODE; 	

	res = f_mkdir(SAVER_FOLDER);//"/SAVER"
	if((res != FR_OK) && (res != FR_EXIST)){
		return 2;
	}		
	res=f_chdir(SAVER_FOLDER);	
	if(res != FR_OK){
		return 2;
	}
	
	pfilename = pNorFS[0].filename;
	SAVEMODE = pNorFS[0].savemode;	
	
	memcpy(savfilename,pfilename,100);
	strlen8 = strlen(savfilename);			
	(savfilename)[strlen8-3] = 's';
	(savfilename)[strlen8-2] = 'a';
	(savfilename)[strlen8-1] = 'v';
		
	res = f_open(&gfile,savfilename, FA_OPEN_EXISTING);	
	
	if(OP ==1){ //load to fram
		if(res == FR_OK)//have a old save file
		{
			f_close(&gfile);		
			Bank_Switching(0);
			res = Loadsavefile(savfilename);	
			DrawHZText12(gl_save_loaded,0,66,118-15,RGB(00,31,00),1);		
		}	
		else {
			DrawHZText12(gl_file_noexist,0,66,118-15,RGB(31,00,00),1);			
		}			
	}		
	else if(OP ==2){ //bak fram save
		if(res == FR_OK)//have a old save file	
		{					
			DrawHZText12(gl_file_exist,0,66,118-15,RGB(31,00,00),1);
			while(1){
				VBlankIntrWait();
				scanKeys();
				u16 keysdown  = keysDown();
				UIAudio_HandleKeys(keysdown, 0);
				if (keysdown & KEY_A) {
					UIAudio_PlayAccept();
					DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic
					Show_MENU_btn();															
					break;
				}
				else if(keysdown & KEY_B){
					UIAudio_PlayBack();
					return 0;
					//break;
				}
			}	
		}
		savefilesize =Get_savefilesize(SAVEMODE);
		Save_savefile(savfilename,savefilesize);	
		DrawHZText12(gl_save_saved,0,66,118-15,RGB(00,31,00),1);											
	}	
	wait_btn();		
	return 0;
	
}
//---------------------------------------------------------------
u8 NOR_list_MENU(u32 show_offset,	u32 file_select)
{
	//u32 res;
	u32 MENU_max;
	u32 MENU_line=0;
	u32 re_menu=1;
	u16 keysdown;
	u16 keysup;
	u16 keys_released;
	u16 keysrepeat;
	u32 menu_scroll_delay = 0;

	//TCHAR *pfilename;

	u32 key_L=0;
	u8 error_num;

			
	//pfilename = pNorFS[show_offset+file_select].filename;
	if(show_offset+file_select==0){
		MENU_max = 4;	//first game	
	}
	else{
		MENU_max = 2;		
	}
	DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic		
	Show_MENU_btn();			
	while(1)//3
	{
		if(re_menu)
		{				
			Show_MENU(MENU_line,NOR_list, 0,0,0,(show_offset+file_select==0));								
		}
		VBlankIntrWait();
		UIAudio_Update();
		if(menu_scroll_delay > 0)
			menu_scroll_delay--;
		
    re_menu=0;
		scanKeys();
		keysdown  = keysDown();
		keysrepeat = keysDownRepeat();
		keysup  = keysUp();
		{
			u16 audio_keysdown = keysdown;
			/* NOR erase/format operations touch hardware/shared buffers while the menu
			   accept sound may still be playing.  Defer the accept sound for those
			   rows so we can play it cleanly and close FIFO DMA before the operation. */
			if(((MENU_line == 1) || (MENU_line == 2)) && (keysdown & KEY_A))
				audio_keysdown &= (u16)~KEY_A;
			UIAudio_HandleKeysEx(audio_keysdown, 0, 0, 0);
		}
		keys_released = keysUp();

		if ((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && menu_scroll_delay == 0)) {
			if (MENU_line < MENU_max) {
				u32 old_MENU_line = MENU_line;
				u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
       	MENU_line++;
        Show_MENU_Row(old_MENU_line, MENU_line, NOR_list, 0, 0);
        Show_MENU_Row(MENU_line, MENU_line, NOR_list, 0, 0);
        re_menu=0;
				UIAudio_PlaySfx(UI_SFX_MOVE);
				menu_scroll_delay = first_press ? 10 : 5;
			}
		}
		else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && menu_scroll_delay == 0))
		{
			if (MENU_line ) {
				u32 old_MENU_line = MENU_line;
				u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
				MENU_line--;
				Show_MENU_Row(old_MENU_line, MENU_line, NOR_list, 0, 0);
				Show_MENU_Row(MENU_line, MENU_line, NOR_list, 0, 0);
				re_menu=0;
				UIAudio_PlaySfx(UI_SFX_MOVE);
				menu_scroll_delay = first_press ? 10 : 5;
			}
		}
		else if(keysup & KEY_B)
		{
			UIAudio_PlayBack();
			launcher_force_full_redraw = 1;
			return 0;
		}
		else if(keysdown & KEY_L)
		{
			key_L = 1;		
		}
		else if(keys_released & KEY_L)
		{
			key_L = 0;
		}
		else if(keysdown & KEY_A)
		{
    	if(MENU_line==0){//boot to NOR.page
				Boot_NOR_game(show_offset,file_select,key_L);
			}
			else if(MENU_line==1){
				//delete lastest geme
				UIAudio_PlayAccept();
				if(show_offset+file_select+1 == game_total_NOR){
					Block_Erase(gl_norOffset-pNorFS[show_offset+file_select].filesize);
				}
				else{
					DrawHZText12(gl_lastest_game,0,66,118-15,gl_color_text,1);	
					wait_btn();
				}
				return 1;	
			}
			else if(MENU_line==2){ //
				//format all
				UIAudio_PlayAccept();
							FormatNor();
				return 1;				
			}
			else if(MENU_line==3){ //load save data to FARM
				
				error_num = FRAM_save_op(1);
				if(error_num != 0){
					Show_error_num(error_num);
				}				
				return 0;
			}
			else{//save FRAM data
				error_num = FRAM_save_op(2);
				if(error_num != 0){
					Show_error_num(error_num);
				}				
				return 0;			
			}
		}
		ShowTime(NOR_list,0);
	}	//3
	
	return 0;
}
//---------------------------------------------------------------
u8 SD_list_MENU(u32 show_offset,	u32 file_select,u32 play_re )
{
	u32 res;
	u8 Save_num=0;//save tpye: auto
	u8 old_Save_num=0;		
	u32 havecht;	
	u32 MENU_line=0;
	u32 re_menu=1;
	u32 MENU_max;			
	u32 ignore_b_frames=0;
	u32 is_EMU;
	//u32 continue_MENU = 0;
	u16 keysdown;
	u16 keysup;
	u16 keys_released;
	u16 keysrepeat;
	u32 menu_scroll_delay = 0;
	u32 key_L=0;
	u8 error_num;
	//u32 page_mode;
	//TCHAR savfilename[100];	
	TCHAR *pfilename;
	BYTE SAVEMODE;
	
	//press A, show boot MENU;	
	if(play_re==0xBB){
		pfilename = pFilename_buffer[show_offset+file_select-folder_total].filename;
	}
	else{		
		char *p=strrchr(p_recently_play[play_re], '/');
		strncpy(currentpath_temp, currentpath, 256);//old
		memset(currentpath,00,256);
		strncpy(currentpath, p_recently_play[play_re], p-p_recently_play[play_re]);
		if(currentpath[0]==0){
			currentpath[0] = '/';
		}			
		memset(current_filename,00,200);
		strncpy(current_filename, p+1, 100);//remove directory path	
		pfilename = current_filename;						
	}		

is_EMU = Check_file_type(pfilename);

if (is_EMU == 0xff)
{
    if (Is_themes_folder(currentpath) && Is_bin_file(pfilename))
	{
		DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);
		Show_MENU_btn();
		DrawHZText12(DSTEXT_PREPARE_THEME_QUESTION, 0, 47, 45, gl_color_text, 1);
		DrawHZText12(pfilename, 20, 47, 60, gl_color_text, 1);

		while (1)
		{
			VBlankIntrWait();
			scanKeys();
			keysdown = keysDown();
			UIAudio_HandleKeys(keysdown, 0);

			if (keysdown & KEY_A)
			{
				UIAudio_PlayAccept();
				DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);
				DrawHZText12(DSTEXT_PREPARING_THEME, 0, 47, 50, gl_color_text, 1);
				DrawHZText12(DSTEXT_PLEASE_WAIT, 0, 47, 65, gl_color_text, 1);

				VBlankIntrWait();

				if (Stage_kernel_update(pfilename))
				{
					DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);

					DrawHZText12(DSTEXT_THEME_READY, 0, 47, 45, gl_color_text, 1);
					DrawHZText12(DSTEXT_REBOOT_HOLD_R, 0, 47, 60, gl_color_text, 1);
					DrawHZText12(DSTEXT_TO_INSTALL_IT, 0, 47, 75, gl_color_text, 1);

					while (keysHeld() != 0)
					{
						VBlankIntrWait();
						scanKeys();
					}

					while (1)
					{
						VBlankIntrWait();
						scanKeys();
						u16 kd = keysDown();
						if (kd & (KEY_A | KEY_B))
						{
							if(kd & KEY_A) UIAudio_PlayAccept(); else UIAudio_PlayBack();
							break;
						}
					}
				}
				else
				{
					DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);

					DrawHZText12(DSTEXT_PREPARATION_FAILED, 0, 47, 55, RGB(31,0,0), 1);

					while (keysHeld() != 0)
					{
						VBlankIntrWait();
						scanKeys();
					}

					while (1)
					{
						VBlankIntrWait();
						scanKeys();
						u16 kd = keysDown();
						if (kd & (KEY_A | KEY_B))
						{
							if(kd & KEY_A) UIAudio_PlayAccept(); else UIAudio_PlayBack();
							break;
						}
					}
				}

				launcher_force_full_redraw = 1;
				return 0;
			}
			else if (keysdown & KEY_B)
			{
				UIAudio_PlayBack();
				launcher_force_full_redraw = 1;
				launcher_force_full_redraw = 1;
				return 0;
			}
		}
	}

    launcher_force_full_redraw = 1;
    launcher_force_full_redraw = 1;
    return 0;
}					
	else if(is_EMU)
	{
		havecht = 0;
		Save_num = 0xF;
		MENU_max = 0;
		goto load_file;
	}
	else{ //gba file
		res=f_chdir(currentpath);//can open  re list game
		havecht = Check_cheat_file(pfilename);	
		old_Save_num = Check_mde_file(pfilename);	
		Save_num	= old_Save_num;
		MENU_max = 4+ ((gl_cheat_on==1)? ((havecht>0)?1:0):0) ;			
		if(gl_boot_mode_pref == 0x1)
		{
			MENU_line = 0;
			goto load_file;
		}
		else if(gl_boot_mode_pref == 0x2)
		{
			MENU_line = 1;
			goto load_file;
		}
	}
		
re_show_menu:
	DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic		
	Show_MENU_btn();			
	/* Ignore a few initial B-release frames so opening this menu from the
	   start screen cannot be cancelled by stale input from the previous
	   screen. */
	ignore_b_frames = 8;
	while(1)//3
	{
		if(re_menu)
		{				
			Show_MENU(MENU_line,SD_list, ((havecht>0)?1:0),Save_num,is_EMU,(show_offset+file_select==0));								
		}
		VBlankIntrWait();
		UIAudio_Update();
		if(menu_scroll_delay > 0)
			menu_scroll_delay--;
		
    re_menu=0;
		scanKeys();
		keysdown  = keysDown();
		keysrepeat = keysDownRepeat();
		keysup  = keysUp();
		UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
		keys_released = keysUp();
		if(ignore_b_frames)
			ignore_b_frames--;

		if ((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && menu_scroll_delay == 0)) {
			if (MENU_line < MENU_max) {
        u32 old_MENU_line = MENU_line;
        u32 first_press = (keysdown & KEY_DOWN) ? 1 : 0;
       	MENU_line++;
        Show_MENU_Row(old_MENU_line, MENU_line, SD_list, ((havecht>0)?1:0), Save_num);
        Show_MENU_Row(MENU_line, MENU_line, SD_list, ((havecht>0)?1:0), Save_num);
        re_menu=0;
				UIAudio_PlaySfx(UI_SFX_MOVE);
				menu_scroll_delay = first_press ? 10 : 5;
			}
		}
		else if((keysdown & KEY_UP) || ((keysrepeat & KEY_UP) && menu_scroll_delay == 0))
		{
			if (MENU_line ) {
				u32 old_MENU_line = MENU_line;
				u32 first_press = (keysdown & KEY_UP) ? 1 : 0;
				MENU_line--;
				Show_MENU_Row(old_MENU_line, MENU_line, SD_list, ((havecht>0)?1:0), Save_num);
				Show_MENU_Row(MENU_line, MENU_line, SD_list, ((havecht>0)?1:0), Save_num);
				re_menu=0;
				UIAudio_PlaySfx(UI_SFX_MOVE);
				menu_scroll_delay = first_press ? 10 : 5;
			}
		}
		else if((keysup & KEY_B) && !ignore_b_frames)
		{
			UIAudio_PlayBack();
			gl_cheat_count = 0;
			if(play_re!=0xBB){
				strncpy(currentpath, currentpath_temp, 256);//
			}
			f_chdir(currentpath);//return to old folder
			launcher_force_full_redraw = 1;
			return 0;
		}
		else if(keysdown & KEY_LEFT)
		{
			if(MENU_line==4){//save type
				if(Save_num){
					Save_num--;
					re_menu=1;
					UIAudio_PlaySfx(UI_SFX_MOVE);
					DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic
					Show_MENU_btn();
				}
			}
		}
		else if(keysdown & KEY_RIGHT)
		{
			if(MENU_line==4){//save type
				if(Save_num<5){
					Save_num++;	
					re_menu=1;
					UIAudio_PlaySfx(UI_SFX_MOVE);
					DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic
					Show_MENU_btn();
				}
			}
		}	
		else if(keysdown & KEY_L)
		{
			key_L = 1;		
		}
		else if(keys_released & KEY_L)
		{
			key_L = 0;
		}
		else if(keysdown & KEY_A)
		{
			if(MENU_line==1){//check switch
				if((gl_reset_on |  gl_rts_on| gl_sleep_on| gl_cheat_on) == 0)	{
				// do nothing
				}
				else break;
			}
			else if(MENU_line==5){
				UIAudio_PlayAccept();
				//open cht file
				Open_cht_file(pfilename,havecht);	
				re_menu=1;
				MENU_line=1;
				goto re_show_menu;	
			}
			else if(MENU_line==4){//save type
				// do nothing
			}
			else{ //boot game or load to NOR
				break;
			}
			
		}
		ShowTime(SD_list,0);
	}	//3
load_file:

	UIAudio_StopForSharedBufferUse();
	Clear(0, 0, 240, 160, gl_color_cheat_black, 1);
	//DrawHZText12(gl_Loading,0,(240-strlen(gl_Loading)*6)/2,74, gl_color_text,1);

	u32 gamefilesize=0;
	//u32 savefilesize=0;		
	u32 ret;
	u32 have_pat=0;
	
	init_FAT_table();		
			
	//Load to PSRAM or NOR 

	f_chdir(currentpath);//return to game folder	
	res = f_open(&gfile, pfilename, FA_READ);
	if(res == FR_OK)			{
		f_lseek(&gfile, 0xAC);
		f_read(&gfile, GAMECODE, 4, (UINT *)&ret);			
		gamefilesize = f_size(&gfile);
		f_close(&gfile);
	}
	else{
		memset(GAMECODE,'F',4);
	}	
											
	//check
	
	SAVEMODE = Get_saveMODE(Save_num,gamefilesize);			
	if(MENU_line<2){//work for psram
		if(gamefilesize > 0x2000000){
			ShowbootProgress(gl_file_overflow);	
			wait_btn();
			return 0;	
		}	
		
		ShowbootProgress(gl_check_sav);	
		res = f_mkdir(SAVER_FOLDER);//"/SAVER"
		if((res != FR_OK) && (res != FR_EXIST)){
			error_num = 2;
			Show_error_num(error_num);
			return 0;
		}		
		
		error_num = Process_savefile(is_EMU,pfilename,gamefilesize,SAVEMODE);	
		if(error_num != 0){
			Show_error_num(error_num);
			return 0;
		}
		Make_recently_play_file(currentpath,pfilename);	//make txt in /SAVER
		Save_last_launch_mode((MENU_line == 1) ? LAST_LAUNCH_MODE_ADDON : LAST_LAUNCH_MODE_CLEAN);
			
	}	
								
	if(is_EMU) //boot emu game
	{			
		ShowbootProgress(gl_loading_game);	
		f_chdir(currentpath);//return to game folder
		
	  FAT_table_buffer[0x1F4/4] = SET_PARAMETER_MODE;
		Send_FATbuffer(FAT_table_buffer,1);
							
		res=LoadEMU2PSRAM(pfilename,is_EMU);
			int bootmode = ((is_EMU > 3) && (is_EMU < 9)) ?
				((is_EMU == 6) ? 2
					: (is_EMU == 7) ? 4
					: ((is_EMU == 8) ? 5 : 3)) : gl_toggle_reset;
			SetRompageWithHardReset(0x200,bootmode);
		while(1);
	}		
	else {	//gba file
		if(old_Save_num != Save_num){
			error_num = Make_mde_file(pfilename,Save_num);
			if(error_num == 2){
				Show_error_num(error_num);
				return 0;
			}
		}
		
		f_chdir(currentpath);//return to game folder
		res = Check_game_RTS_FAT(pfilename,1);//game FAT
		if(res == 0xffffffff){
			error_num = 1;
			Show_error_num(error_num);
			return 0;
		}

	u8 reset_choice;
		
  	switch(MENU_line){
  		case 0://DirectPSRAM CLEAN BOOT

  			ShowbootProgress(gl_loading_game); 			

				Send_FATbuffer(FAT_table_buffer,0);
				GBApatch_Cleanrom(PSRAMBase_S98,gamefilesize);
				//wait_btn();
			if(key_L)
				reset_choice = !gl_toggle_reset;
			else
				reset_choice = gl_toggle_reset;
				    	SetRompageWithHardReset(0x200,reset_choice);
	  		break;
	    case 1://PSRAM BOOT WITH ADDON
	    	ShowbootProgress(gl_loading_game);
	    	gl_reset_on = Read_SET_info(assress_v_reset);
				gl_rts_on 	= Read_SET_info(assress_v_rts);
				gl_sleep_on = Read_SET_info(assress_v_sleep);
				gl_cheat_on = Read_SET_info(assress_v_cheat);
				if((gl_reset_on==1) || (gl_rts_on==1) || (gl_sleep_on==1) || (gl_cheat_on==1))
				{
					if(gl_rts_on==1)
					{		
						ShowbootProgress(gl_check_RTS);		
						u32 size = Check_RTS(pfilename);
						if(size ==0){
							error_num = 6;
							Show_error_num(error_num);
							return 0;
						}
					}	
					ShowbootProgress(gl_check_pat);						
					have_pat = Check_pat(pfilename);
					if(have_pat==1)	
					{			
			    	Send_FATbuffer(FAT_table_buffer,0);//Loading rom	
					}	
					else //(have_pat==0)
					{
						f_chdir(currentpath);//return to game folder	
						//get the location for the patch
						UIAudio_StopForSharedBufferUse();
						res = f_open(&gfile,pfilename, FA_READ);	
						f_lseek(&gfile, (gamefilesize-1)&0xFFFE0000);
						f_read(&gfile, pReadCache, 0x20000, (UINT*)&ret);
						f_close(&gfile);
						SetTrimSize(pReadCache,gamefilesize,0x20000,0x0,SAVEMODE);						
						
						if((gl_engine_sel==0) || (gl_select_lang == 0xE2E2))
						{				
							get_find:
			    		FAT_table_buffer[0x1F4/4] = SET_PARAMETER_MODE;
							Send_FATbuffer(FAT_table_buffer,1);												
			    		res=Loadfile2PSRAM(pfilename);
							ShowbootProgress(gl_make_pat);
							Make_pat_file(pfilename);		    		
						}
						else 
						{
							res=use_internal_engine(GAMECODE);
							if(res == 1)
							{
								Send_FATbuffer(FAT_table_buffer,0);//Loading rom
							}
							else
							{
								goto get_find;
							}
						}
					}	
	    		Patch_SpecialROM_sleepmode();//
	    		GBApatch_PSRAM(PSRAMBase_S98,gamefilesize);																																			
				}
				else{//no select switch ,CLEAN
					Send_FATbuffer(FAT_table_buffer,0);//Loading rom		
				}
				//wait_btn();	
			if(key_L)
				reset_choice = !gl_toggle_reset;
			else
				reset_choice = gl_toggle_reset;
	    	    	SetRompageWithHardReset(0x200,reset_choice);
	    	break;
	    case 2://WRITE TO NOR CLEAN    	
	    	UIAudio_StopForSharedBufferUse();
	    	f_chdir(currentpath);//return to game folder
				res = Loadfile2NOR(pfilename, gl_norOffset,0x0,SAVEMODE);
				if(res==0)//ok
				{
					if(gl_norOffset==0)//first game need set saveMODE
					{
						Set_saveMODE(SAVEMODE);
					}
					return 2;
				}
				else if(res==2)
				{
					Clear(0,160-15,200,15,gl_color_cheat_black,1);
					DrawHZText12(gl_NOR_full,0,0,160-15, gl_color_NORFULL,1);//"NOR FULL!"
					wait_btn();	
					return 1;
				}
	    	break;
	    case 3://WRITE TO NOR ADDON
	    	UIAudio_StopForSharedBufferUse();
	    	gl_reset_on = Read_SET_info(assress_v_reset);
				gl_rts_on = Read_SET_info(assress_v_rts);
				gl_sleep_on = Read_SET_info(assress_v_sleep);
				gl_cheat_on = Read_SET_info(assress_v_cheat);
														
				f_chdir(currentpath);//return to game folder	
				u32 needpatch = 0;
				if((gl_reset_on==1) || (gl_rts_on==1) || (gl_sleep_on==1) || (gl_cheat_on==1))		    	
	    	{
	    		Patch_SpecialROM_sleepmode();//
      				
					//get the location of the patch
					UIAudio_StopForSharedBufferUse();
					res = f_open(&gfile,pfilename, FA_READ);	
					if(res==FR_OK){
						f_lseek(&gfile, (gamefilesize-1)&0xFFFE0000);
						f_read(&gfile, pReadCache, 0x20000, (UINT*)&ret);
						f_close(&gfile);
						SetTrimSize(pReadCache,gamefilesize,0x20000,0x1,SAVEMODE);
					}
					needpatch = 1;
				}
				res = Loadfile2NOR(pfilename, gl_norOffset,needpatch,SAVEMODE);
				//wait_btn();	
				if(res==0)
				{
					if(gl_norOffset==0)//first game need set saveMODE
					{
						Set_saveMODE(SAVEMODE);
					}
					return 2;
				}	
				else if(res==2)
				{
			    Clear(0,160-15,200,15,gl_color_cheat_black,1);
					DrawHZText12(gl_NOR_full,0,0,160-15, gl_color_NORFULL,1);//"NOR FULL!"
					wait_btn();	
					return 0;
				}		
	    	break;
	    default:
	    	break;
	  }
	}	
	return 0;
}

