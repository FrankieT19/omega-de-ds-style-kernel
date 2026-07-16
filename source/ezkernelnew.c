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
#include "thai620.h"

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
	else if(gl_select_lang == THAI_CP_FIRST)
		LoadThai();
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
static u32 launcher_boot_target = 0;
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
#define LAUNCHER_VIEW_LIST 0
#define LAUNCHER_VIEW_HORIZONTAL 1
#define LAUNCHER_VIEW_VERTICAL 2
#define LAUNCHER_VIEW_LIST_ART 3
#define LAUNCHER_UPDATE_CAROUSEL_SELECTION 6
#define LAUNCHER_LIST_ART_CACHE_COUNT 3
#define LAUNCHER_LIST_ART_IDLE_LOAD_FRAMES 0
#define LAUNCHER_LIST_ART_INPUT_QUEUE_SIZE 4
#define LAUNCHER_LIST_ART_BUFFERABLE_KEYS (KEY_A | KEY_B)
#define LAUNCHER_THUMB_BMP_HEADER 0x36
#define LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX 256
#define LAUNCHER_SYSTEM_NAME_DISPLAY_MAX 11
#define LAUNCHER_THEME_MODE_LIGHT 0
#define LAUNCHER_THEME_MODE_DARK 1
#define LAUNCHER_THEME_MODE_CUSTOM 2
#define LAUNCHER_BOOT_TO_START 0
#define LAUNCHER_BOOT_TO_SD 1
#define LAUNCHER_BOOT_TO_NOR 2
#define LAUNCHER_BOOT_TO_LAST_GAME 3
#define LAUNCHER_BOOT_TO_RECENTS 4
#define LAUNCHER_BOOT_TO_FAVOURITES 5
#define LAUNCHER_BOOT_TO_TOTAL 6
#define LAUNCHER_LIST_ART_MAX_SPANS 8
#define LAUNCHER_LIST_ROW_COUNT 10
#define LAUNCHER_LIST_ROW_HEIGHT 14
#define LAUNCHER_LIST_ROW_PIXELS (240 * LAUNCHER_LIST_ROW_HEIGHT)
#define LAUNCHER_LIST_ROW_BYTES (LAUNCHER_LIST_ROW_PIXELS * 2)
/* Clear() uses the first 480 bytes of pReadCache as a fill line.  Nine cached
   rows live after that scratch line; the tenth fits in the unused tail after
   the three maximum-size 120x80 thumbnail slots. */
#define LAUNCHER_LIST_ROW_CACHE_BASE 0x0200
#define LAUNCHER_LIST_ROW_CACHE_TAIL 0x1E400
#define LAUNCHER_LIST_SELECTED_ROW_SCRATCH 0x14C00
typedef char LauncherListRowsFitLowerScratch[
	(LAUNCHER_LIST_ROW_CACHE_BASE + 9 * LAUNCHER_LIST_ROW_BYTES <= 0x10000) ? 1 : -1];
typedef char LauncherListSelectedRowFitsScratch[
	(LAUNCHER_LIST_SELECTED_ROW_SCRATCH + LAUNCHER_LIST_ROW_BYTES <= LAUNCHER_LIST_ROW_CACHE_TAIL) ? 1 : -1];
typedef char LauncherListThumbStageEndsBeforeSelectedRow[
	(0x10000 + LAUNCHER_THUMB_BMP_HEADER + 120 * 80 * 2 <= LAUNCHER_LIST_SELECTED_ROW_SCRATCH) ? 1 : -1];
typedef char LauncherListRowFitsTailScratch[
	(LAUNCHER_LIST_ROW_CACHE_TAIL + LAUNCHER_LIST_ROW_BYTES <= MAX_pReadCache_size) ? 1 : -1];
typedef char LauncherTitleThumbsEndBeforeTail[
	(0x19800 + LAUNCHER_THUMB_BMP_HEADER + 120 * 80 * 2 <= LAUNCHER_LIST_ROW_CACHE_TAIL) ? 1 : -1];

static u16 launcher_start_preview_cache[LAUNCHER_START_PREVIEW_CACHE_COUNT][LAUNCHER_START_THUMB_W * LAUNCHER_START_THUMB_H]EWRAM_BSS;
static char launcher_start_preview_path[LAUNCHER_START_PREVIEW_CACHE_COUNT][LAUNCHER_FAVOURITE_PATH_LEN];
static u8 launcher_start_preview_valid[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u8 launcher_start_preview_mode[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u32 launcher_start_preview_index[LAUNCHER_START_PREVIEW_CACHE_COUNT];
static u8 launcher_custom_thumb_manifest_loaded[2];
static u8 launcher_custom_thumb_manifest_present[2];
static u16 launcher_custom_thumb_manifest_count[2];
static u32 launcher_custom_thumb_manifest_hash[2][LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX]EWRAM_BSS;
/* FatFs directory objects are too large for the launcher's small IWRAM stack.
   The manifest scan is single-threaded, so one shared EWRAM workspace is enough. */
static char launcher_custom_thumb_scan_path[32]EWRAM_BSS;
static char launcher_custom_thumb_scan_name[112]EWRAM_BSS;
static DIR launcher_custom_thumb_scan_dir EWRAM_BSS;
static FILINFO launcher_custom_thumb_scan_info EWRAM_BSS;

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
static const char *Launcher_BootToText(void);
static void Launcher_CycleStartSource(void);
static void Launcher_CycleStartEnabled(void);
static void Launcher_CycleBootTo(int dir);
const char *Launcher_OnOffText(u16 value);
static void Launcher_LoadFavourites(void);
static u32 Read_last_played_entry(TCHAR *out_path, u32 out_path_size, TCHAR *out_name, u32 out_name_size);
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
static void Launcher_DrawIconCenteredClip3x(const u16 *icon, int box_x, int box_y, int box_w, int box_h);
static void Launcher_GetListArtRect(int *x, int *y, int *w, int *h);
static s32 Launcher_GetListArtCachedState(u32 absolute_index, int *x, int *y, int *w, int *h, u32 preserve_current);
static u32 Launcher_RoundedThumbPixelVisible(int dst_x, int dst_y, int w, int h);
static u16 Launcher_ArtBorderColourEx(u32 selected);
static u32 Launcher_GetTotalEntries(void);
static u32 Launcher_IsListArtMode(void);
static u32 Launcher_IsListLikeMode(void);
static void Launcher_BuildThumbCache(u32 center_index);
static void Launcher_ViewModeCycle(int dir);
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
#ifndef LAUNCHER_VERT_SIDE_CUSTOM_ENABLED
#define LAUNCHER_VERT_SIDE_CUSTOM_ENABLED 0
#endif
#ifndef LAUNCHER_HORZ_SIDE_CUSTOM_ENABLED
#define LAUNCHER_HORZ_SIDE_CUSTOM_ENABLED 0
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

#define LAUNCHER_SIDE_ALIGN_CENTER 0
#define LAUNCHER_SIDE_ALIGN_LEFT 1
#define LAUNCHER_SIDE_ALIGN_RIGHT 2
#define LAUNCHER_SIDE_ALIGN_TOP 1
#define LAUNCHER_SIDE_ALIGN_BOTTOM 2
#define LAUNCHER_SIDE_ALIGN_CUSTOM 3

static u32 Launcher_RoundedCornersForCarousel(void);
static u32 Launcher_RoundedCornersForStart(void);
static int Launcher_VerticalSideX(int custom_x, int side_w);
static int Launcher_HorizontalSideY(int custom_y);
static void Launcher_DrawThumbBorderEx(int x, int y, int w, int h, u32 selected);

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
#define LAUNCHER_HORZ_LEFT_X -5
#define LAUNCHER_HORZ_LEFT_Y LAUNCHER_HORZ_SIDE_Y
#define LAUNCHER_HORZ_RIGHT_X 185
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
#define LAUNCHER_VERT_TITLE_X 93
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
static u8 launcher_virtual_gamecode[10][4]EWRAM_BSS;
static u8 launcher_virtual_gamecode_valid[10]EWRAM_BSS;
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
static u16 launcher_hide_system_files = 1;
static u16 launcher_list_folders = 0;
static u16 launcher_clean_list = 0;
static u16 launcher_clock_24_hour = 1;
static u16 launcher_art_border_mode = 0;
static u16 launcher_art_rounded_corners = 0;
static u16 launcher_vertical_side_align = 0;
static u16 launcher_horizontal_side_align = 0;
static u16 launcher_list_art_position = LAUNCHER_SIDE_ALIGN_BOTTOM;
static u16 launcher_carousel_art_draw = 0;
static u16 launcher_list_art_selected_has_art = 0;
static u16 launcher_effective_show_thumbnail = 0;

static u32 launcher_sd_launchable_file_count = 0;
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
extern u32 gl_cheat_selected_count;
extern u32 CheatSelectionAppliesTo(TCHAR *gamefilename, u32 havecht);
extern void CheatSelectionForget(void);
u16 SAV_info_buffer [0x200]EWRAM_BSS;
/* Only one thumbnail renderer is active at a time. Sharing its prepared-pixel
   workspace gives list + art a deeper cache without increasing permanent RAM. */
typedef union
{
	struct
	{
		u16 left[60 * 40];
		u16 right[60 * 40];
	} horizontal;
	struct
	{
		u16 prev[48 * 32];
		u16 selected[84 * 56];
		u16 next[48 * 32];
	} vertical;
	u16 list_art[LAUNCHER_LIST_ART_CACHE_COUNT][90 * 60];
} LauncherThumbnailRenderWorkspace;

static LauncherThumbnailRenderWorkspace launcher_thumbnail_workspace EWRAM_BSS;
static u8 launcher_thumbnail_workspace_mode = 0xFF;
#define launcher_side_preview_left launcher_thumbnail_workspace.horizontal.left
#define launcher_side_preview_right launcher_thumbnail_workspace.horizontal.right
#define launcher_vert_prev_scaled launcher_thumbnail_workspace.vertical.prev
#define launcher_vert_selected_scaled launcher_thumbnail_workspace.vertical.selected
#define launcher_vert_next_scaled launcher_thumbnail_workspace.vertical.next
static u8 launcher_scale84_x[84]EWRAM_BSS;
static u8 launcher_scale56_y[56]EWRAM_BSS;
static u8 launcher_scale48_x[48]EWRAM_BSS;
static u8 launcher_scale32_y[32]EWRAM_BSS;
static u8 launcher_scale80_56[56]EWRAM_BSS;
static u8 launcher_scale80_37[37]EWRAM_BSS;
static u8 launcher_scale80_32[32]EWRAM_BSS;
static u8 launcher_scale_maps_ready = 0;
static u8 launcher_list_art_span_count[160]EWRAM_BSS;
static u8 launcher_list_art_span_start[160][LAUNCHER_LIST_ART_MAX_SPANS]EWRAM_BSS;
static u8 launcher_list_art_span_end[160][LAUNCHER_LIST_ART_MAX_SPANS]EWRAM_BSS;
static u8 launcher_list_art_span_cache_valid = 0;
static u8 launcher_list_art_span_sig_x = 0;
static u8 launcher_list_art_span_sig_y = 0;
static u8 launcher_list_art_span_sig_w = 0;
static u8 launcher_list_art_span_sig_h = 0;
static u8 launcher_list_art_span_sig_border = 0;
static u8 launcher_list_art_span_sig_rounded = 0;
static u32 launcher_list_art_scaled_index[LAUNCHER_LIST_ART_CACHE_COUNT] = {
	0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF
};
static u8 launcher_list_art_scaled_valid[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u8 launcher_list_art_scaled_has_art[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u8 launcher_list_art_scaled_sig_w[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u8 launcher_list_art_scaled_sig_h[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u8 launcher_list_art_scaled_sig_style[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u32 launcher_list_art_scaled_age[LAUNCHER_LIST_ART_CACHE_COUNT] = {0, 0, 0};
static u32 launcher_list_art_cache_clock = 0;
static u8 launcher_list_art_scaled_selected_slot = 0xFF;
static u32 launcher_list_art_pending_index = 0xFFFFFFFF;
static u8 launcher_list_art_pending = 0;
static u8 launcher_list_art_idle_frames = 0;
static u16 launcher_list_art_input_queue[LAUNCHER_LIST_ART_INPUT_QUEUE_SIZE];
static u8 launcher_list_art_input_queue_head = 0;
static u8 launcher_list_art_input_queue_count = 0;
static u8 launcher_list_art_input_capture = 0;
static u16 launcher_list_art_input_previous = 0;
static u8 launcher_list_row_slot_for_line[LAUNCHER_LIST_ROW_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
static u8 launcher_list_row_cache_valid = 0;
static u8 launcher_list_row_cache_nor = 0;
static u8 launcher_list_art_screen_has_art = 0;
static u32 launcher_list_row_cache_show_offset = 0;
static u32 launcher_list_row_cache_file_select = 0;

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

static void Launcher_FormatClock(char *out, u32 out_size, u8 HH, u8 MM, u8 SS)
{
	if(!out || out_size == 0)
		return;
	if(launcher_clock_24_hour)
		snprintf(out, out_size, "%02u:%02u:%02u", HH, MM, SS);
	else
	{
		u8 hour = HH % 12;
		if(hour == 0)
			hour = 12;
		snprintf(out, out_size, "%2u:%02u %s", hour, MM, (HH >= 12) ? "PM" : "AM");
	}
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

static const u16 *launcher_current_topbar_bg = 0;
static char launcher_counter_last_msg[20] = "";
static u16 launcher_counter_last_x = 185;
static u32 launcher_counter_last_list = 0xFFFFFFFF;
static u32 launcher_counter_valid = 0;
static char launcher_cheat_title[96] = "";
static u32 launcher_cheat_title_frame = 0;
static u32 launcher_cheat_title_offset = 0;
static u16 launcher_cheat_counter_x = 219;
static u16 launcher_cheat_counter_last_x = 219;
static u32 launcher_cheat_counter_valid = 0;
static u32 launcher_reopen_sd_menu_after_redraw = 0;
static u32 launcher_reopen_from_cheat_screen = 0;
static u32 launcher_restore_popup_region = 0;
static u32 launcher_start_window_preserved = 0;
static u32 launcher_popup_restore_redraw = 0;

static void Launcher_ClearWithThemeBG(const u16 *base, u16 x, u16 y, u16 w, u16 h);
static const u16 *Launcher_GetBGImage(void);

static u32 Launcher_CheatTitleMaxChars(void)
{
	if(launcher_cheat_counter_x <= 9)
		return 30;
	return (launcher_cheat_counter_x - 4) / 6;
}

static void Launcher_RestorePopupRegionForPage(u32 page_num, u32 top_art_has_thumbnail)
{
	const u16 *bg;
	u16 old_carousel_art_draw;
	launcher_popup_restore_redraw = ((page_num == SD_list) || (page_num == NOR_list)) &&
	!Launcher_IsListLikeMode();

	if(page_num == START_win)
		bg = (const u16*)gImage_START;
	else
		bg = Launcher_GetBGImage();

	Launcher_ClearWithThemeBG(bg, 36, 25, 168, 110);
	if(launcher_popup_restore_redraw && launcher_effective_show_thumbnail == LAUNCHER_VIEW_VERTICAL &&
	launcher_art_border_mode && top_art_has_thumbnail)
	{
		int border_x = Launcher_VerticalSideX(LAUNCHER_VERT_PREV_X, LAUNCHER_VERT_PREV_W);
		int border_w = LAUNCHER_VERT_PREV_W;
		if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
		{
			border_x += 8;
			border_w = 32;
		}
		old_carousel_art_draw = launcher_carousel_art_draw;
		launcher_carousel_art_draw = 1;
		Launcher_DrawThumbBorderEx(border_x, LAUNCHER_VERT_PREV_Y, border_w,
		LAUNCHER_VERT_PREV_H, 0);
		launcher_carousel_art_draw = old_carousel_art_draw;
	}
}

static void Launcher_DrawThemeBGFull(const u16 *base)
{
	const u16 *top = Launcher_GetThemeTopForBase(base);

	if(top && (top == launcher_current_topbar_bg))
	{
		ClearWithBG((u16*)base, 0, LAUNCHER_TOP_BAR_HEIGHT, 240, 160 - LAUNCHER_TOP_BAR_HEIGHT, 1);
		ClearWithBG((u16*)top, 70, 2, 170, 15, 1);
		return;
	}

	DrawPic((u16*)base, 0, 0, 240, 160, 0, 0, 1);
	if(top)
		DrawPic((u16*)top, 0, 0, 240, LAUNCHER_TOP_BAR_HEIGHT, 0, 0, 1);
	launcher_current_topbar_bg = top;
	launcher_counter_valid = 0;
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
#define UI_AUDIO_MAX_SAMPLES 65520

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

static volatile u8 g_ui_audio_initialised = 0;
static volatile u8 g_ui_audio_active = 0;
static volatile u8 g_ui_audio_uses_shared_buffer = 0;

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
        copy_len = len;
        if(copy_len > UI_AUDIO_MAX_SAMPLES)
            copy_len = UI_AUDIO_MAX_SAMPLES;
        if(out_buffer)
            *out_buffer = (s8*)data;
        return copy_len;
    }

    buffer = UIAudio_GetBuffer();
    buffer_size = UI_AUDIO_BUFFER_SIZE;

    copy_len = len;
    if(copy_len > buffer_size)
        copy_len = buffer_size;
    if(copy_len > UI_AUDIO_MAX_SAMPLES)
        copy_len = UI_AUDIO_MAX_SAMPLES;

    padded_len = (copy_len + 15) & ~15;
    if(padded_len > buffer_size)
        padded_len = buffer_size;

    /* Clear the active transfer, including its final partial FIFO block. */
    memset(buffer, 0, padded_len);
    memcpy(buffer, data, copy_len);

    if(out_buffer)
        *out_buffer = buffer;

    return padded_len;
}


static void UIAudio_StopHardware(void)
{
    REG_DMA1CNT_UI = 0;
    REG_TM0CNT_H_UI = 0;
    REG_TM1CNT_H_UI = 0;
    REG_DMA1SAD_UI = 0;
    REG_DMA1DAD_UI = 0;
    REG_SOUNDCNT_H_UI |= UI_SOUNDCNT_A_RESET;
    REG_IF_UI = IRQ_TIMER1;
    g_ui_audio_active = 0;
    g_ui_audio_uses_shared_buffer = 0;
}

static void UIAudio_Stop(void)
{
    u16 old_ime = REG_IME;
    REG_IME = 0;
    UIAudio_StopHardware();
    REG_IME = old_ime;
    delay(8);
}

static void UIAudio_StopFromTimer(void)
{
    UIAudio_StopHardware();
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
    u16 old_ime = REG_IME;
    REG_IME = 0;
    REG_DMA1CNT_UI = 0;
    REG_TM0CNT_H_UI = 0;
    REG_TM1CNT_H_UI = 0;
    REG_IF_UI = IRQ_TIMER1;
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

    irqSet(IRQ_TIMER1, UIAudio_Timer1IRQ);
    irqEnable(IRQ_TIMER1);

    g_ui_audio_initialised = 1;
    REG_IME = old_ime;
}

static void UIAudio_PlayRaw(const signed char *data, u32 len, u32 sample_rate, u32 allow_shared_long_clip)
{
    u32 sample_count;
    u32 copy_len;
    u32 padded_len;
    s8 *play_buffer;
    u16 old_ime;

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

    if(allow_shared_long_clip && (play_buffer == (s8*)data))
        padded_len = copy_len;
    else
        padded_len = (copy_len + 15) & ~15;
    sample_count = padded_len;
    if(sample_count == 0)
        return;
    if(sample_count > UI_AUDIO_MAX_SAMPLES)
        sample_count = UI_AUDIO_MAX_SAMPLES;

    old_ime = REG_IME;
    REG_IME = 0;
    UIAudio_StopHardware();
    REG_SOUNDBIAS_UI = 0x0200;
    REG_DMA1SAD_UI = (u32)play_buffer;
    REG_DMA1DAD_UI = (u32)&REG_FIFO_A_UI;
    REG_DMA1CNT_UI = UI_DMA_COUNT_4 | UI_DMA_ENABLE | UI_DMA_TIMING_FIFO | UI_DMA_32BIT | UI_DMA_REPEAT | UI_DMA_DST_FIXED | UI_DMA_SRC_INC;

    REG_TM0CNT_L_UI = UIAudio_TimerReload(sample_rate);
    REG_TM1CNT_H_UI = 0;
    REG_TM1CNT_L_UI = (u16)(0x10000 - sample_count);
    REG_IF_UI = IRQ_TIMER1;
    g_ui_audio_active = 1;
    g_ui_audio_uses_shared_buffer = allow_shared_long_clip && (play_buffer == UIAudio_GetSharedLongClipBuffer());
    /* Timer 1 counts Timer 0 sample ticks, so playback stops on an exact sample
       boundary rather than an independently approximated wall-clock duration. */
    REG_TM1CNT_H_UI = UI_TIMER_ENABLE | UI_TIMER_COUNT_UP | UI_TIMER_IRQ;
    REG_TM0CNT_H_UI = UI_TIMER_ENABLE | UI_TIMER_FREQ_1;
    REG_IME = old_ime;
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

#define LAUNCHER_SPLASH_VISUAL_FRAMES 0

static u32 UIAudio_GetStartupSplashFrames(void)
{
    return LAUNCHER_SPLASH_VISUAL_FRAMES;
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
    return !strcmp(path, "/THEMES") || !strcmp(path, "/SYSTEM/KERNELS");
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

static u32 Launcher_GetVirtualFileInfo(const TCHAR *path, const TCHAR *name, u32 *out_size, u8 gamecode[4])
{
	FIL file;
	FRESULT res;
	UINT read_count = 0;
	u32 name_len = name ? strlen(name) : 0;
	u32 is_gba = (name_len >= 3) &&
	(!strcasecmp(&(name[name_len - 3]), "gba") ||
	!strcasecmp(&(name[name_len - 3]), "agb"));

	res = f_open(&file, path, FA_READ);
	if(res != FR_OK)
		return 0;
	if(out_size)
		*out_size = f_size(&file);
	if(gamecode)
		memset(gamecode, 0, 4);
	if(gamecode && is_gba && (f_size(&file) >= 0xB0))
	{
		f_lseek(&file, 0xAC);
		res = f_read(&file, gamecode, 4, &read_count);
	}
	f_close(&file);
	return !gamecode || !is_gba || ((res == FR_OK) && (read_count == 4));
}

u32 Stage_kernel_update(const TCHAR *src_name)
{
    TCHAR src_path[MAX_path_len + 200];
    const TCHAR *tmp_path = "/ezkernelnew.tmp";
    const TCHAR *dst_path = "/ezkernelnew.bin";
    u32 src_size = 0;
    u32 tmp_size = 0;

    memset(src_path, 0, sizeof(src_path));

    if (src_name[0] == '/')
        snprintf(src_path, sizeof(src_path), "%s", src_name);
    else if (!strcmp(currentpath, "/"))
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
static const u16 *Launcher_GetBGImage(void);
static u32 Launcher_IsNORPage(void);
static void Launcher_ClearTextBodyBackground(void);
static void Launcher_ClearTextBodyBackgroundRegion(int x, int y, int w, int h);
static void Launcher_DrawSDListRow(u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail);
static void Launcher_DrawSDListRowClip(u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail, u32 copy_x, u32 copy_w);
static void Launcher_BuildSDListRowBuffer(u16 *row_buffer, u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail);
static void Launcher_BuildSDListRowBufferState(u16 *row_buffer, u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail, u32 selected);
static void Launcher_CopySDListRowBuffer(u16 *row_buffer, u32 showy, u32 line, u32 haveThumbnail, u32 copy_x, u32 copy_w);
static s32 Launcher_GetListArtCachedState(u32 absolute_index, int *x, int *y, int *w, int *h, u32 preserve_current);
static void Launcher_PreScaleListArtCache(void);
static void Launcher_DrawListArtImageOnly(u32 show_offset, u32 file_select);
static void Launcher_DrawCurrentListArtImageOnly(void);
static void Launcher_InvalidateListArtScaledCache(void);
static void Launcher_DrawScrolledSDListBody(u32 show_offset, u32 file_select);
static void Launcher_GetDisplayTitleBounded(const TCHAR *src, char *dst, u32 dst_size);
static u32 Launcher_IsFavouritePathName(const TCHAR *path, const TCHAR *name);


static void Launcher_GetListDisplayName(const TCHAR *src, char *dst, u32 dst_size)
{
	if(!dst || dst_size == 0)
		return;
	dst[0] = '\0';
	if(!src)
		return;

	if(launcher_clean_list)
	{
		Launcher_GetDisplayTitleBounded(src, dst, dst_size);
		return;
	}

	strncpy(dst, src, dst_size - 1);
	dst[dst_size - 1] = '\0';
}

static void Launcher_GetSDListDisplayNameWithFavourite(u32 file_index, char *out, u32 out_size)
{
	char clean_name[256];
	TCHAR *src;

	if(!out || out_size == 0)
		return;
	out[0] = '\0';
	if(file_index >= game_total_SD)
		return;

	src = pFilename_buffer[file_index].filename;
	Launcher_GetListDisplayName(src, clean_name, sizeof(clean_name));
	if(Launcher_IsFavouritePathName(currentpath, src))
		snprintf(out, out_size, "%s <3", clean_name);
	else
		snprintf(out, out_size, "%s", clean_name);
}

static u32 Launcher_ListArtRowIntersects(u32 line)
{
	int x, y, w, h;
	int row_y = 20 + line * 14;

	Launcher_GetListArtRect(&x, &y, &w, &h);
	return row_y < (y + h + 1) && (row_y + 14) > (y - 1);
}

typedef struct
{
	int start;
	int end;
} LauncherListArtSpan;

static void Launcher_ListArtAddSpan(LauncherListArtSpan *spans, u32 *count, u32 max_count, int start, int end)
{
	u32 i;
	u32 insert;

	if(start < 0)
		start = 0;
	if(end > 240)
		end = 240;
	if(start >= end || *count >= max_count)
		return;

	insert = *count;
	for(i = 0; i < *count; i++)
	{
		if(start < spans[i].start)
		{
			insert = i;
			break;
		}
	}
	for(i = *count; i > insert; i--)
		spans[i] = spans[i - 1];
	spans[insert].start = start;
	spans[insert].end = end;
	(*count)++;

	for(i = 1; i < *count; )
	{
		if(spans[i].start <= spans[i - 1].end)
		{
			if(spans[i].end > spans[i - 1].end)
				spans[i - 1].end = spans[i].end;
			for(insert = i; insert + 1 < *count; insert++)
				spans[insert] = spans[insert + 1];
			(*count)--;
		}
		else
			i++;
	}
}

static u32 Launcher_ListArtProtectedSpansAtYRaw(int screen_y, LauncherListArtSpan *spans, u32 max_spans)
{
	int x, y, w, h;
	int ly;
	u32 count = 0;

	Launcher_GetListArtRect(&x, &y, &w, &h);
	ly = screen_y - y;

	if(ly >= 0 && ly < h)
	{
		int start = 0;
		int end = w;
		while(start < end && !Launcher_RoundedThumbPixelVisible(start, ly, w, h))
			start++;
		while(end > start && !Launcher_RoundedThumbPixelVisible(end - 1, ly, w, h))
			end--;
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + start, x + end);
	}

	if(!launcher_art_border_mode)
		return count;

	if(!Launcher_RoundedCornersForCarousel())
	{
		if(screen_y == y - 1 || screen_y == y + h)
			Launcher_ListArtAddSpan(spans, &count, max_spans, x - 1, x + w + 1);
		if(screen_y >= y - 1 && screen_y <= y + h)
		{
			Launcher_ListArtAddSpan(spans, &count, max_spans, x - 1, x);
			Launcher_ListArtAddSpan(spans, &count, max_spans, x + w, x + w + 1);
		}
		return count;
	}

	if(screen_y == y - 1 || screen_y == y + h)
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + 5, x + w - 5);
	if(screen_y >= y + 5 && screen_y < y + h - 5)
	{
		Launcher_ListArtAddSpan(spans, &count, max_spans, x - 1, x);
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + w, x + w + 1);
	}

	if(screen_y == y || screen_y == y + h - 1)
	{
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + 3, x + 5);
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + w - 5, x + w - 3);
	}
	else if(screen_y == y + 1 || screen_y == y + h - 2)
	{
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + 2, x + 3);
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + w - 3, x + w - 2);
	}
	else if(screen_y == y + 2 || screen_y == y + h - 3)
	{
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + 1, x + 2);
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + w - 2, x + w - 1);
	}
	else if(screen_y == y + 3 || screen_y == y + 4 ||
	screen_y == y + h - 5 || screen_y == y + h - 4)
	{
		Launcher_ListArtAddSpan(spans, &count, max_spans, x, x + 1);
		Launcher_ListArtAddSpan(spans, &count, max_spans, x + w - 1, x + w);
	}

	return count;
}

static void Launcher_ListArtPrepareSpanCache(void)
{
	int x, y, w, h;
	u32 rounded;
	u32 row;

	Launcher_GetListArtRect(&x, &y, &w, &h);
	rounded = Launcher_RoundedCornersForCarousel();
	if(launcher_list_art_span_cache_valid &&
	launcher_list_art_span_sig_x == (u8)x &&
	launcher_list_art_span_sig_y == (u8)y &&
	launcher_list_art_span_sig_w == (u8)w &&
	launcher_list_art_span_sig_h == (u8)h &&
	launcher_list_art_span_sig_border == (u8)launcher_art_border_mode &&
	launcher_list_art_span_sig_rounded == (u8)rounded)
		return;

	for(row = 0; row < 160; row++)
	{
		LauncherListArtSpan spans[LAUNCHER_LIST_ART_MAX_SPANS];
		u32 count = Launcher_ListArtProtectedSpansAtYRaw((int)row, spans, LAUNCHER_LIST_ART_MAX_SPANS);
		u32 i;

		launcher_list_art_span_count[row] = (u8)count;
		for(i = 0; i < count; i++)
		{
			launcher_list_art_span_start[row][i] = (u8)spans[i].start;
			launcher_list_art_span_end[row][i] = (u8)spans[i].end;
		}
	}

	launcher_list_art_span_sig_x = (u8)x;
	launcher_list_art_span_sig_y = (u8)y;
	launcher_list_art_span_sig_w = (u8)w;
	launcher_list_art_span_sig_h = (u8)h;
	launcher_list_art_span_sig_border = (u8)launcher_art_border_mode;
	launcher_list_art_span_sig_rounded = (u8)rounded;
	launcher_list_art_span_cache_valid = 1;
}

static u32 Launcher_ShowListMetaForRow(u32 haveThumbnail, u32 line, u32 selected)
{
	if(launcher_clean_list)
		return 0;
	if(haveThumbnail)
	{
		if(!Launcher_IsListArtMode() && line > 3)
			return 0;
		if(selected && Launcher_IsListArtMode() && launcher_list_art_selected_has_art &&
		Launcher_ListArtRowIntersects(line))
			return 0;
	}
	return 1;
}

static u32 Launcher_ListTextCharsForRow(u32 haveThumbnail, u32 line, u32 selected)
{
	int x, y, w, h;
	(void)selected;

	if(haveThumbnail)
	{
		if(!Launcher_IsListArtMode() && line > 3)
			return 17;
		if(selected && Launcher_IsListArtMode() && launcher_list_art_selected_has_art &&
		Launcher_ListArtRowIntersects(line))
		{
			Launcher_GetListArtRect(&x, &y, &w, &h);
			(void)y;
			(void)w;
			(void)h;
			x -= 2;
			if(x < 67)
				return 8;
			return (u32)((x - 19) / 6);
		}
	}
	return launcher_clean_list ? 36 : 32;
}

static u32 Launcher_ListSelectWidth(u32 haveThumbnail, u32 line, u32 char_num)
{
	u32 width = (char_num == 17) ? (17 * 6 + 1) : (240 - 17);
	(void)haveThumbnail;
	(void)line;
	return width;
}

static void Launcher_DrawListSelectBGScreen(u32 line, u32 width)
{
	u32 row_y = 20 + line * 14;
	u32 end_x = 17 + width;

	if(end_x > 240)
		end_x = 240;

	Clear(17, row_y, end_x - 17, 13, gl_color_selectBG_sd, 1);
}

static void Launcher_DrawListSelectBGScreenClipped(u32 line, u32 width)
{
	int x, y, w, h;
	u32 row_y = 20 + line * 14;
	u32 end_x = 17 + width;
	u32 py;

	if(end_x > 240)
		end_x = 240;

	if(!Launcher_IsListArtMode() || !launcher_list_art_selected_has_art ||
	!Launcher_ListArtRowIntersects(line))
	{
		Launcher_DrawListSelectBGScreen(line, width);
		return;
	}

	Launcher_GetListArtRect(&x, &y, &w, &h);
	if(x < 17)
		x = 17;
	for(py = 0; py < 13; py++)
	{
		u32 sy = row_y + py;
		if((sy >= (u32)y) && (sy < (u32)(y + h)))
		{
			if((u32)x > 17)
				Clear(17, sy, (u32)x - 17, 1, gl_color_selectBG_sd, 1);
			if(((u32)(x + w) < end_x) && ((x + w) < 240))
				Clear((u32)(x + w), sy, end_x - (u32)(x + w), 1, gl_color_selectBG_sd, 1);
		}
		else
			Clear(17, sy, end_x - 17, 1, gl_color_selectBG_sd, 1);
	}
}

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
		char_num = Launcher_ListTextCharsForRow(haveThumbnail, line, line == file_select);

		if(line== file_select)
		{
			Launcher_DrawListSelectBGScreen(line, Launcher_ListSelectWidth(haveThumbnail, line, char_num));
		}

		DrawPic((u16*)(gImage_icon_folder/*gImage_icons+0*16*14*2*/),
			0,
			y_offset + line*14,
			16,
			14,
			1,
			0x0000,
			1);

		{
			char list_name[256];
			Launcher_GetListDisplayName(pFolder[show_offset+line].filename, list_name, sizeof(list_name));
			DrawHZText12(list_name, char_num, 1+16, y_offset + line*14, row_color,1);
		}

		if(!Launcher_ShowListMetaForRow(haveThumbnail, line, line == file_select))
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
		char_num = Launcher_ListTextCharsForRow(haveThumbnail, line, line == file_select);

		if(line== file_select)
		{
			Launcher_DrawListSelectBGScreen(line, Launcher_ListSelectWidth(haveThumbnail, line, char_num));
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
			Launcher_GetSDListDisplayNameWithFavourite(offset+line-need_show_folder, fav_name, sizeof(fav_name));
			DrawHZText12(fav_name, char_num, 1+16, showy, row_color,1);
		}
		if(recents_view_active)
		{}
		else if(!Launcher_ShowListMetaForRow(haveThumbnail, line, line == file_select))
		{}
		else
		{
			char msg[20];
			Get_file_size(offset+line-need_show_folder,msg);
			DrawHZText12(msg,0,208,showy, row_color,1);
		}
	}
}

static void Launcher_DrawSDListRow(u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail)
{
	Launcher_DrawSDListRowClip(show_offset, line, file_select, haveThumbnail, 0, 240);
}

static void Launcher_CopySDListRowBuffer(u16 *row_buffer, u32 showy, u32 line, u32 haveThumbnail, u32 copy_x, u32 copy_w)
{
	u32 y;

	if(copy_w > 240)
		copy_w = 240;
	if(copy_x > 240)
		copy_x = 240;
	if(copy_x + copy_w > 240)
		copy_w = 240 - copy_x;
	if(copy_w == 0)
		return;

	for(y = 0; y < 14; y++)
	{
		u32 screen_y = showy + y;

		if(haveThumbnail && Launcher_IsListArtMode() && launcher_list_art_selected_has_art)
		{
			u32 span_count;
			u8 *span_start;
			u8 *span_end;
			u32 span_index = 0;
			u32 x = copy_x;
			u32 end = copy_x + copy_w;

			Launcher_ListArtPrepareSpanCache();
			if(screen_y >= 160)
				continue;
			span_count = launcher_list_art_span_count[screen_y];
			span_start = launcher_list_art_span_start[screen_y];
			span_end = launcher_list_art_span_end[screen_y];
			if(span_count == 0)
			{
				dmaCopy(row_buffer + y * 240 + copy_x,
				VideoBuffer + (showy + y) * 240 + copy_x,
				copy_w * 2);
				continue;
			}

			while(x < end)
			{
				u32 run_start;
				while(span_index < span_count && span_end[span_index] <= x)
					span_index++;
				if(span_index < span_count && span_start[span_index] <= x)
				{
					x = (span_end[span_index] < end) ? span_end[span_index] : end;
					continue;
				}
				run_start = x;
				while(x < end)
				{
					if(span_index < span_count && span_start[span_index] <= x)
						break;
					x++;
				}
				if(x > run_start)
				{
					dmaCopy(row_buffer + y * 240 + run_start,
					VideoBuffer + screen_y * 240 + run_start,
					(x - run_start) * 2);
				}
			}
			continue;
		}

		dmaCopy(row_buffer + y * 240 + copy_x, VideoBuffer + (showy + y) * 240 + copy_x, copy_w * 2);
	}
}

static void Launcher_DrawListRowIcon(u16 *row_buffer, const u16 *icon)
{
	u32 x;
	u32 y;

	if(!row_buffer || !icon)
		return;
	for(y = 0; y < LAUNCHER_LIST_ROW_HEIGHT; y++)
	{
		for(x = 0; x < 16; x++)
		{
			u16 pixel = icon[y * 16 + x];
			if(pixel)
				row_buffer[y * 240 + x] = pixel;
		}
	}
}

static void Launcher_FillListRowSelection(u16 *row_buffer, u32 width, u16 colour)
{
	u16 *fill_line = (u16*)pReadCache;
	u32 x;
	u32 y;

	if(width > 223)
		width = 223;
	if(row_buffer == fill_line)
	{
		for(y = 0; y < 13; y++)
			for(x = 17; x < 17 + width; x++)
				row_buffer[y * 240 + x] = colour;
		return;
	}
	for(x = 0; x < width; x++)
		fill_line[x] = colour;
	for(y = 0; y < 13; y++)
		dmaCopy(fill_line, row_buffer + y * 240 + 17, width * 2);
}

static inline void Launcher_DmaCopyAligned32(const void *source, void *dest, u32 size)
{
	if((((u32)source | (u32)dest | size) & 3) != 0)
		dmaCopy(source, dest, size);
	else
		DMA_Copy(3, source, dest, DMA32 | (size >> 2));
}

static void Launcher_BuildSDListRowBufferState(u16 *row_buffer, u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail, u32 selected)
{
	u32 absolute_index = show_offset + line;
	u32 char_num = Launcher_ListTextCharsForRow(haveThumbnail, line, selected);
	u32 showy = 20 + line * 14;
	u16 row_color = selected ? LAUNCHER_SELECTED_TEXT : gl_color_text;
	const u16 *bg = Launcher_GetBGImage();
	u16 select_bg = Launcher_IsNORPage() ? gl_color_selectBG_nor : gl_color_selectBG_sd;
	u32 y;
	char msg[20];

	for(y = 0; y < 14; y++)
		Launcher_DmaCopyAligned32(bg + (showy + y) * 240, row_buffer + y * 240, 240 * 2);

	if(selected)
	{
		u32 width = Launcher_ListSelectWidth(haveThumbnail, line, char_num);
		Launcher_FillListRowSelection(row_buffer, width, select_bg);
	}

	if(Launcher_IsNORPage())
	{
		if(absolute_index >= game_total_NOR)
			return;

		Launcher_DrawListRowIcon(row_buffer, (const u16*)gImage_icon_nor);
		{
			char list_name[256];
			Launcher_GetListDisplayName(pNorFS[absolute_index].filename, list_name, sizeof(list_name));
			DrawHZText12ToBuffer(list_name, char_num, 17, 0, row_color, row_buffer);
		}
		if(Launcher_ShowListMetaForRow(haveThumbnail, line, selected))
		{
			sprintf(msg, "%4luM", pNorFS[absolute_index].filesize >> 20);
			DrawHZText12ToBuffer(msg, 0, 208, 0, row_color, row_buffer);
		}
		return;
	}

	if(absolute_index >= (folder_total + game_total_SD))
		return;

	if(absolute_index < folder_total)
	{
		Launcher_DrawListRowIcon(row_buffer, (const u16*)gImage_icon_folder);
		{
			char list_name[256];
			Launcher_GetListDisplayName(pFolder[absolute_index].filename, list_name, sizeof(list_name));
			DrawHZText12ToBuffer(list_name, char_num, 17, 0, row_color, row_buffer);
		}
		if(Launcher_ShowListMetaForRow(haveThumbnail, line, selected))
			DrawHZText12ToBuffer("DIR", 0, 221, 0, row_color, row_buffer);
	}
	else
	{
		u32 file_index = absolute_index - folder_total;
		TCHAR *pfilename = pFilename_buffer[file_index].filename;
		char fav_name[256];

		Launcher_DrawListRowIcon(row_buffer, (const u16*)Launcher_GetFileIcon(pfilename));
		Launcher_GetSDListDisplayNameWithFavourite(file_index, fav_name, sizeof(fav_name));
		DrawHZText12ToBuffer(fav_name, char_num, 17, 0, row_color, row_buffer);
		if(!recents_view_active && Launcher_ShowListMetaForRow(haveThumbnail, line, selected))
		{
			Get_file_size(file_index, msg);
			DrawHZText12ToBuffer(msg, 0, 208, 0, row_color, row_buffer);
		}
	}
}

static void Launcher_BuildSDListRowBuffer(u16 *row_buffer, u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail)
{
	Launcher_BuildSDListRowBufferState(row_buffer, show_offset, line, file_select,
	haveThumbnail, line == file_select);
}

static u16 *Launcher_ListRowCacheSlot(u32 slot)
{
	if(slot >= (LAUNCHER_LIST_ROW_COUNT - 1))
		return (u16*)(pReadCache + LAUNCHER_LIST_ROW_CACHE_TAIL);
	return (u16*)(pReadCache + LAUNCHER_LIST_ROW_CACHE_BASE + slot * LAUNCHER_LIST_ROW_BYTES);
}

static u16 *Launcher_ListRowCacheLine(u32 line)
{
	if(line >= LAUNCHER_LIST_ROW_COUNT)
		return 0;
	return Launcher_ListRowCacheSlot(launcher_list_row_slot_for_line[line]);
}

static u16 *Launcher_ListSelectedRowScratch(void)
{
	return (u16*)(pReadCache + LAUNCHER_LIST_SELECTED_ROW_SCRATCH);
}

static void Launcher_ResetListRowCacheMap(void)
{
	u32 line;
	for(line = 0; line < LAUNCHER_LIST_ROW_COUNT; line++)
		launcher_list_row_slot_for_line[line] = (u8)line;
}

static void Launcher_InvalidateListRowCache(void)
{
	launcher_list_row_cache_valid = 0;
	launcher_list_art_screen_has_art = 0;
}

static u32 Launcher_ListRowCacheMatches(u32 show_offset, u32 file_select)
{
	(void)file_select;
	return launcher_list_row_cache_valid &&
	launcher_list_row_cache_show_offset == show_offset &&
	launcher_list_row_cache_nor == (u8)Launcher_IsNORPage();
}

static void Launcher_BuildCachedListRow(u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail)
{
	u16 *row_buffer = Launcher_ListRowCacheLine(line);
	if(row_buffer)
		Launcher_BuildSDListRowBufferState(row_buffer, show_offset, line, file_select,
		haveThumbnail, 0);
}

static void Launcher_BuildSelectedListRowScratch(u32 show_offset, u32 file_select, u32 haveThumbnail)
{
	Launcher_BuildSDListRowBufferState(Launcher_ListSelectedRowScratch(), show_offset,
	file_select, file_select, haveThumbnail, 1);
}

static void Launcher_CopySelectedListRowScratch(u32 file_select, u32 haveThumbnail)
{
	Launcher_CopySDListRowBuffer(Launcher_ListSelectedRowScratch(),
	20 + file_select * LAUNCHER_LIST_ROW_HEIGHT,
	file_select, haveThumbnail, 0, 240);
}

static void Launcher_BuildAllCachedListRows(u32 show_offset, u32 file_select, u32 haveThumbnail)
{
	u32 line;

	Launcher_ResetListRowCacheMap();
	for(line = 0; line < LAUNCHER_LIST_ROW_COUNT; line++)
		Launcher_BuildCachedListRow(show_offset, line, file_select, haveThumbnail);
	launcher_list_row_cache_show_offset = show_offset;
	launcher_list_row_cache_file_select = file_select;
	launcher_list_row_cache_nor = (u8)Launcher_IsNORPage();
	launcher_list_row_cache_valid = 1;
}

static void Launcher_CopyCachedListRow(u32 line)
{
	u16 *row_buffer = Launcher_ListRowCacheLine(line);
	if(row_buffer && line < LAUNCHER_LIST_ROW_COUNT)
		Launcher_DmaCopyAligned32(row_buffer, VideoBuffer + (20 + line * LAUNCHER_LIST_ROW_HEIGHT) * 240, LAUNCHER_LIST_ROW_BYTES);
}

static void Launcher_CopyCachedListRowAroundArt(u32 line, u32 protect_art)
{
	u16 *row_buffer = Launcher_ListRowCacheLine(line);

	if(!row_buffer || line >= LAUNCHER_LIST_ROW_COUNT)
		return;
	if(protect_art)
	{
		Launcher_CopySDListRowBuffer(row_buffer,
		20 + line * LAUNCHER_LIST_ROW_HEIGHT,
		line, 1, 0, 240);
		return;
	}
	Launcher_CopyCachedListRow(line);
}

/* Scroll one scanline while treating the artwork as a fixed overlay. Pixels
   covered by the new artwork are left alone; pixels sourced from beneath the
   old artwork come from the logical row cache instead of stale VRAM. */
static void Launcher_CopyListArtScrollLine(int dest_y, int source_y,
	u32 old_has_art, u32 new_has_art)
{
	u32 dest_count = 0;
	u32 source_count = 0;
	u32 dest_span = 0;
	u32 source_span = 0;
	u32 x = 0;
	u16 *cached_line;
	u32 row_line;
	u32 row_y;

	if(dest_y < 20 || dest_y >= 160 || source_y < 20 || source_y >= 160)
		return;

	row_line = (u32)(dest_y - 20) / LAUNCHER_LIST_ROW_HEIGHT;
	row_y = (u32)(dest_y - 20) % LAUNCHER_LIST_ROW_HEIGHT;
	cached_line = Launcher_ListRowCacheLine(row_line);
	if(!cached_line)
		return;
	cached_line += row_y * 240;

	if(new_has_art)
		dest_count = launcher_list_art_span_count[dest_y];
	if(old_has_art)
		source_count = launcher_list_art_span_count[source_y];
	if(dest_count == 0 && source_count == 0)
	{
		Launcher_DmaCopyAligned32(VideoBuffer + source_y * 240,
		VideoBuffer + dest_y * 240, 240 * 2);
		return;
	}

	while(x < 240)
	{
		u32 dest_active;
		u32 source_active;
		u32 next = 240;

		while(dest_span < dest_count &&
		launcher_list_art_span_end[dest_y][dest_span] <= x)
			dest_span++;
		while(source_span < source_count &&
		launcher_list_art_span_end[source_y][source_span] <= x)
			source_span++;

		dest_active = dest_span < dest_count &&
		launcher_list_art_span_start[dest_y][dest_span] <= x;
		source_active = source_span < source_count &&
		launcher_list_art_span_start[source_y][source_span] <= x;

		if(dest_span < dest_count)
		{
			u32 boundary = dest_active ?
			launcher_list_art_span_end[dest_y][dest_span] :
			launcher_list_art_span_start[dest_y][dest_span];
			if(boundary < next)
				next = boundary;
		}
		if(source_span < source_count)
		{
			u32 boundary = source_active ?
			launcher_list_art_span_end[source_y][source_span] :
			launcher_list_art_span_start[source_y][source_span];
			if(boundary < next)
				next = boundary;
		}

		if(next <= x)
		{
			x++;
			continue;
		}
		if(!dest_active)
		{
			if(source_active)
				Launcher_DmaCopyAligned32(cached_line + x,
				VideoBuffer + dest_y * 240 + x,
				(next - x) * 2);
			else
				Launcher_DmaCopyAligned32(VideoBuffer + source_y * 240 + x,
				VideoBuffer + dest_y * 240 + x,
				(next - x) * 2);
		}
		x = next;
	}
}

static void __attribute__((unused)) Launcher_ScrollListArtBodySegmented(int direction,
	u32 old_has_art,
	u32 new_has_art)
{
	int line;

	Launcher_ListArtPrepareSpanCache();
	if(direction > 0)
	{
		for(line = 0; line < 8; line++)
		{
			int dest_y = 20 + line * LAUNCHER_LIST_ROW_HEIGHT;
			int source_y = dest_y + LAUNCHER_LIST_ROW_HEIGHT;
			int y;
			u32 needs_segments = 0;

			for(y = 0; y < LAUNCHER_LIST_ROW_HEIGHT; y++)
			{
				if((new_has_art && launcher_list_art_span_count[dest_y + y]) ||
				(old_has_art && launcher_list_art_span_count[source_y + y]))
				{
					needs_segments = 1;
					break;
				}
			}
			if(!needs_segments)
			{
				Launcher_DmaCopyAligned32(VideoBuffer + source_y * 240,
				VideoBuffer + dest_y * 240,
				LAUNCHER_LIST_ROW_BYTES);
				continue;
			}
			for(y = 0; y < LAUNCHER_LIST_ROW_HEIGHT; y++)
				Launcher_CopyListArtScrollLine(dest_y + y, source_y + y,
				old_has_art, new_has_art);
		}
	}
	else
	{
		for(line = 9; line >= 2; line--)
		{
			int dest_y = 20 + line * LAUNCHER_LIST_ROW_HEIGHT;
			int source_y = dest_y - LAUNCHER_LIST_ROW_HEIGHT;
			int y;
			u32 needs_segments = 0;

			for(y = 0; y < LAUNCHER_LIST_ROW_HEIGHT; y++)
			{
				if((new_has_art && launcher_list_art_span_count[dest_y + y]) ||
				(old_has_art && launcher_list_art_span_count[source_y + y]))
				{
					needs_segments = 1;
					break;
				}
			}
			if(!needs_segments)
			{
				Launcher_DmaCopyAligned32(VideoBuffer + source_y * 240,
				VideoBuffer + dest_y * 240,
				LAUNCHER_LIST_ROW_BYTES);
				continue;
			}
			for(y = LAUNCHER_LIST_ROW_HEIGHT - 1; y >= 0; y--)
				Launcher_CopyListArtScrollLine(dest_y + y, source_y + y,
				old_has_art, new_has_art);
		}
	}
}

static void Launcher_CopyAllCachedListRows(u32 preserve_art, u32 skip_line)
{
	u32 line;
	for(line = 0; line < LAUNCHER_LIST_ROW_COUNT; line++)
	{
		if(line != skip_line)
			Launcher_CopyCachedListRowAroundArt(line, preserve_art);
	}
}

static void Launcher_CopyCachedRowsBehindArt(void)
{
	u32 line;
	for(line = 0; line < LAUNCHER_LIST_ROW_COUNT; line++)
	{
		if(Launcher_ListArtRowIntersects(line))
			Launcher_CopyCachedListRow(line);
	}
}

static void __attribute__((unused)) Launcher_RotateCachedListRows(int direction)
{
	u32 line;
	u8 recycled;

	if(direction > 0)
	{
		recycled = launcher_list_row_slot_for_line[0];
		for(line = 0; line + 1 < LAUNCHER_LIST_ROW_COUNT; line++)
			launcher_list_row_slot_for_line[line] = launcher_list_row_slot_for_line[line + 1];
		launcher_list_row_slot_for_line[LAUNCHER_LIST_ROW_COUNT - 1] = recycled;
	}
	else
	{
		recycled = launcher_list_row_slot_for_line[LAUNCHER_LIST_ROW_COUNT - 1];
		for(line = LAUNCHER_LIST_ROW_COUNT - 1; line > 0; line--)
			launcher_list_row_slot_for_line[line] = launcher_list_row_slot_for_line[line - 1];
		launcher_list_row_slot_for_line[0] = recycled;
	}
}

static void Launcher_DrawListBodyFromCache(u32 show_offset, u32 file_select, u32 haveThumbnail)
{
	u32 selected_has_art = 0;

	if(haveThumbnail && Launcher_IsListArtMode())
	{
		int x, y, w, h;
		s32 art_state = Launcher_GetListArtCachedState(show_offset + file_select, &x, &y, &w, &h, 0);
		selected_has_art = art_state > 0;
		Launcher_ListArtPrepareSpanCache();
		haveThumbnail = selected_has_art;
	}
	Launcher_BuildAllCachedListRows(show_offset, file_select, haveThumbnail);
	Launcher_BuildSelectedListRowScratch(show_offset, file_select, haveThumbnail);
	Launcher_CopyAllCachedListRows(0, file_select);
	Launcher_CopySelectedListRowScratch(file_select, haveThumbnail);
	if(haveThumbnail && Launcher_IsListArtMode())
		Launcher_DrawListArtImageOnly(show_offset, file_select);
	launcher_list_art_screen_has_art = (u8)selected_has_art;
}

static void Launcher_RestoreListPopupContent(u32 show_offset, u32 file_select)
{
	const u32 popup_x = 36;
	const u32 popup_w = 168;
	const u32 first_line = 0;
	const u32 last_line = 8;
	u32 line;

	if(Launcher_IsListArtMode())
	{
		u32 selected_has_art = launcher_list_art_screen_has_art ? 1 : 0;

		launcher_list_art_selected_has_art = (u16)selected_has_art;
		if(selected_has_art)
		{
			Launcher_ListArtPrepareSpanCache();
			/* Repaint the artwork immediately after the popup background is
			restored, before rebuilding the shared logical-row cache. */
			Launcher_DrawCurrentListArtImageOnly();
		}

		/* The game menu may use pReadCache, which also holds the logical list
		rows. Rebuild that cache without copying the full body to VRAM, then
		restore only the rows covered by the popup. */
		Launcher_BuildAllCachedListRows(show_offset, file_select, selected_has_art);
		Launcher_BuildSelectedListRowScratch(show_offset, file_select, selected_has_art);
		for(line = first_line; line <= last_line; line++)
		{
			u16 *row_buffer = (line == file_select) ?
			Launcher_ListSelectedRowScratch() :
			Launcher_ListRowCacheLine(line);
			if(row_buffer)
				Launcher_CopySDListRowBuffer(row_buffer, 20 + line * LAUNCHER_LIST_ROW_HEIGHT,
				line, selected_has_art, popup_x, popup_w);
		}
		launcher_list_art_screen_has_art = (u8)selected_has_art;
		return;
	}

	for(line = first_line; line <= last_line; line++)
		Launcher_DrawSDListRowClip(show_offset, line, file_select, 0, popup_x, popup_w);
}

static void Launcher_DrawListArtSelectionChange(u32 show_offset, u32 file_select, u32 old_line)
{
	u32 selected_has_art = 0;
	u32 old_screen_has_art = launcher_list_art_screen_has_art;
	u32 art_ready = 1;
	u32 cached_old_line = launcher_list_row_cache_file_select;

	if(Launcher_IsListArtMode())
	{
		int x, y, w, h;
		s32 art_state = Launcher_GetListArtCachedState(show_offset + file_select, &x, &y, &w, &h, 1);
		art_ready = art_state >= 0;
		selected_has_art = art_ready ? (u32)art_state : old_screen_has_art;
		launcher_list_art_selected_has_art = (u16)selected_has_art;
		Launcher_ListArtPrepareSpanCache();
	}

	if(!launcher_list_row_cache_valid ||
	launcher_list_row_cache_show_offset != show_offset ||
	launcher_list_row_cache_nor != (u8)Launcher_IsNORPage())
	{
		Launcher_DrawListBodyFromCache(show_offset, file_select, selected_has_art);
		return;
	}

	if(old_screen_has_art && !selected_has_art)
	{
		Launcher_CopyCachedRowsBehindArt();
		/* Restoring the old artwork footprint is not enough when the previous
		   selection row sits elsewhere on the screen.  Restore that row too so
		   its highlight cannot survive an artwork-to-no-art transition. */
		if((cached_old_line < LAUNCHER_LIST_ROW_COUNT) && (cached_old_line != file_select))
			Launcher_CopyCachedListRow(cached_old_line);
		if((old_line < LAUNCHER_LIST_ROW_COUNT) && (old_line != file_select) &&
		(old_line != cached_old_line))
			Launcher_CopyCachedListRow(old_line);
	}
	else
	{
		if((cached_old_line < LAUNCHER_LIST_ROW_COUNT) && (cached_old_line != file_select))
		{
			if(old_screen_has_art && selected_has_art)
				Launcher_CopyCachedListRowAroundArt(cached_old_line, 1);
			else
				Launcher_CopyCachedListRow(cached_old_line);
		}
		if((old_line < LAUNCHER_LIST_ROW_COUNT) && (old_line != file_select) &&
		(old_line != cached_old_line))
		{
			if(old_screen_has_art && selected_has_art)
				Launcher_CopyCachedListRowAroundArt(old_line, 1);
			else
				Launcher_CopyCachedListRow(old_line);
		}
	}

	launcher_list_row_cache_file_select = file_select;

	Launcher_BuildSelectedListRowScratch(show_offset, file_select, selected_has_art);
	Launcher_CopySelectedListRowScratch(file_select, selected_has_art);

	if(selected_has_art && art_ready)
		Launcher_DrawListArtImageOnly(show_offset, file_select);
	launcher_list_art_screen_has_art = (u8)selected_has_art;
}

static void Launcher_DrawListArtScrolledBody(u32 show_offset, u32 file_select, int direction)
{
	u32 selected_has_art = 0;
	u32 old_screen_has_art = launcher_list_art_screen_has_art;
	u32 art_ready = 1;
	u32 preserve_art;

	(void)direction;

	if(Launcher_IsListArtMode())
	{
		int art_x, art_y, art_w, art_h;
		s32 art_state = Launcher_GetListArtCachedState(show_offset + file_select, &art_x, &art_y, &art_w, &art_h, 1);
		art_ready = art_state >= 0;
		selected_has_art = art_ready ? (u32)art_state : old_screen_has_art;
		launcher_list_art_selected_has_art = (u16)selected_has_art;
		Launcher_ListArtPrepareSpanCache();
	}

	/* Background artwork belongs to fixed screen coordinates. Recompose every
	   visible row from its absolute background slice instead of moving pixels
	   already drawn in VRAM. Keep the old artwork overlay only while a pending
	   thumbnail is resolved, then replace it as the final drawing operation. */
	preserve_art = old_screen_has_art && selected_has_art;
	Launcher_BuildAllCachedListRows(show_offset, file_select, selected_has_art);
	Launcher_BuildSelectedListRowScratch(show_offset, file_select, selected_has_art);
	Launcher_CopyAllCachedListRows(preserve_art, file_select);
	Launcher_CopySelectedListRowScratch(file_select, preserve_art);
	if(selected_has_art && art_ready)
		Launcher_DrawCurrentListArtImageOnly();
	launcher_list_art_screen_has_art = (u8)selected_has_art;
}

static void Launcher_UpdateSelectedListArtScrollRow(u32 show_offset, u32 file_select, const char *msg, u32 char_count)
{
	u32 width = char_count * 6 + 1;
	u16 *row_buffer = Launcher_ListSelectedRowScratch();

	Launcher_BuildSDListRowBufferState(row_buffer, show_offset, file_select, file_select, 1, 1);
	if(width > 223)
		width = 223;
	Launcher_FillListRowSelection(row_buffer, width, gl_color_selectBG_sd);
	DrawHZText12ToBuffer((char*)msg, char_count, 17, 0, LAUNCHER_SELECTED_TEXT, row_buffer);
	Launcher_CopySDListRowBuffer(row_buffer, 20 + file_select * 14, file_select, 1, 0, 240);
}

static void Launcher_DrawSDListRowClip(u32 show_offset, u32 line, u32 file_select, u32 haveThumbnail, u32 copy_x, u32 copy_w)
{
	u32 showy = 20 + line * 14;
	u16 *row_buffer = Vcache;

	if(copy_w > 240)
		copy_w = 240;
	if(copy_x > 240)
		copy_x = 240;
	if(copy_x + copy_w > 240)
		copy_w = 240 - copy_x;

	Launcher_BuildSDListRowBuffer(row_buffer, show_offset, line, file_select, haveThumbnail);
	Launcher_CopySDListRowBuffer(row_buffer, showy, line, haveThumbnail, copy_x, copy_w);
}

static void Launcher_DrawScrolledSDListBody(u32 show_offset, u32 file_select)
{
	/* Build every visible row against its absolute background coordinates
	   before updating VRAM. Moving composited rows is only correct for a
	   vertically repeating background and corrupts custom list artwork. */
	Launcher_DrawListBodyFromCache(show_offset, file_select, 0);
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

	if(launcher_clean_list)
	{
		if(char_num1 == 32)
			char_num1 = 36;
		if(char_num2 == 32)
			char_num2 = 36;
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
		{ char list_name1[256]; Launcher_GetListDisplayName(pFolder[show_offset+xx1].filename, list_name1, sizeof(list_name1)); DrawHZText12(list_name1, char_num1, 1+16, showy1, name_color1,1); }
		{ char fav_name2[256]; Launcher_GetSDListDisplayNameWithFavourite(0, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }

		if(!launcher_clean_list && char_num1==32){
			sprintf(msg,"%s","DIR");
			DrawHZText12(msg,0,221,showy1, name_color1,1);
		}
		if(!launcher_clean_list && char_num2==32){
			Get_file_size(0,msg);
			DrawHZText12(msg,0,208,showy2, name_color2,1);
		}
	}
	else if(file_select < need_show_folder)
	{
		{ char list_name1[256]; Launcher_GetListDisplayName(pFolder[show_offset+xx1].filename, list_name1, sizeof(list_name1)); DrawHZText12(list_name1, char_num1, 1+16, showy1, name_color1,1); }
		{ char list_name2[256]; Launcher_GetListDisplayName(pFolder[show_offset+xx2].filename, list_name2, sizeof(list_name2)); DrawHZText12(list_name2, char_num2, 1+16, showy2, name_color2,1); }

		sprintf(msg,"%s","DIR");
		if(!launcher_clean_list && char_num1==32){
			DrawHZText12(msg,0,221,showy1, name_color1,1);
		}
		if(!launcher_clean_list && char_num2==32){
			DrawHZText12(msg,0,221,showy2, name_color2,1);
		}
	}
	else if((file_select == need_show_folder)&& (updown ==2))
	{
		{ char list_name1[256]; Launcher_GetListDisplayName(pFolder[show_offset+xx1].filename, list_name1, sizeof(list_name1)); DrawHZText12(list_name1, char_num1, 1+16, showy1, name_color1,1); }
		{ char fav_name2[256]; Launcher_GetSDListDisplayNameWithFavourite(0, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }

		if(!launcher_clean_list && char_num1==32){
			sprintf(msg,"%s","DIR");
			DrawHZText12(msg,0,221,showy1, name_color1,1);
		}
		if(!recents_view_active && !launcher_clean_list && char_num2==32){
			Get_file_size(0,msg);
			DrawHZText12(msg,0,208,showy2, name_color2,1);
		}
	}
	else
	{
		{ char fav_name1[256]; Launcher_GetSDListDisplayNameWithFavourite(offset+xx1-need_show_folder, fav_name1, sizeof(fav_name1)); DrawHZText12(fav_name1, char_num1, 1+16, showy1, name_color1,1); }
		{ char fav_name2[256]; Launcher_GetSDListDisplayNameWithFavourite(offset+xx2-need_show_folder, fav_name2, sizeof(fav_name2)); DrawHZText12(fav_name2, char_num2, 1+16, showy2, name_color2,1); }

		if(!recents_view_active && !launcher_clean_list && char_num1==32){
			Get_file_size(offset+xx1-need_show_folder,msg);
			DrawHZText12(msg,0,208,showy1, name_color1,1);
		}
		if(!recents_view_active && !launcher_clean_list && char_num2==32){
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
	u32 y_offset= 20;
	u32 char_num = launcher_clean_list ? 37 : 32;

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

		{
			char list_name[256];
			Launcher_GetListDisplayName(pNorFS[show_offset+line].filename, list_name, sizeof(list_name));
			DrawHZText12(list_name, char_num, 1+16, y_offset + line*14, row_color,1);
		}
		if(!launcher_clean_list)
		{
			sprintf(msg,"%4luM",pNorFS[show_offset+line].filesize >>20 );
			DrawHZText12(msg,0,208,y_offset + line*14, row_color,1);
		}

	}
}
//---------------------------------------------------------------------------------
void Refresh_filename_NOR(u32 show_offset,u32 file_select,u32 updown)
{
	char msg[20];
	u16 name_color1;
	u16 name_color2;
	u32 xx1;
	u32 xx2;
	u32 showy1;
	u32 showy2;
	u32 y_offset= 20;
	u32 char_num;
	u32 clean_len;

	char_num = launcher_clean_list ? 37 : 32;
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

	{ char list_name1[256]; Launcher_GetListDisplayName(pNorFS[show_offset+xx1].filename, list_name1, sizeof(list_name1)); DrawHZText12(list_name1, char_num, 1+16, showy1, name_color1,1); }
	{ char list_name2[256]; Launcher_GetListDisplayName(pNorFS[show_offset+xx2].filename, list_name2, sizeof(list_name2)); DrawHZText12(list_name2, char_num, 1+16, showy2, name_color2,1); }

	if(!launcher_clean_list)
	{
		sprintf(msg,"%4luM",(pNorFS[show_offset+xx1].filesize) >>20 );
		DrawHZText12(msg,0,208,showy1, name_color1,1);
		sprintf(msg,"%4luM",(pNorFS[show_offset+xx2].filesize) >>20 );
		DrawHZText12(msg,0,208,showy2, name_color2,1);
	}

}
//---------------------------------------------------------------------------------
void Show_game_num(u32 count,u32 list,u32 force)
{
	char msg[20];
	const u16 *bg = Launcher_GetBGImage();
	u32 total;
	u32 len;
	u16 x;

	if(list==0){
		if(game_total_SD+folder_total==0)
			count = 0;
		total = game_total_SD + folder_total;
	}
	else{
		if(game_total_NOR==0)
			count = 0;
		total = game_total_NOR;
	}

	sprintf(msg,"%lu/%lu",count,total);
	len = strlen(msg);
	x = (len < 9) ? (u16)(235 - (len * 6)) : 184;

	if(force || !launcher_counter_valid || (launcher_counter_last_list != list))
	{
		Launcher_ClearWithThemeBG(bg, 184, 3, 55, 13);
		DrawHZText12(msg, 0, x, 3, gl_color_topbar_text, 1);
		strncpy(launcher_counter_last_msg, msg, sizeof(launcher_counter_last_msg) - 1);
		launcher_counter_last_msg[sizeof(launcher_counter_last_msg) - 1] = '\0';
		launcher_counter_last_x = x;
		launcher_counter_last_list = list;
		launcher_counter_valid = 1;
		return;
	}

	if(strcmp(launcher_counter_last_msg, msg) || launcher_counter_last_x != x)
	{
		Launcher_ClearWithThemeBG(bg, 184, 3, 55, 13);
		DrawHZText12(msg, 0, x, 3, gl_color_topbar_text, 1);
	}


	strncpy(launcher_counter_last_msg, msg, sizeof(launcher_counter_last_msg) - 1);
	launcher_counter_last_msg[sizeof(launcher_counter_last_msg) - 1] = '\0';
	launcher_counter_last_x = x;
	launcher_counter_last_list = list;
	launcher_counter_valid = 1;
}
//---------------------------------------------------------------------------------
void Filename_loop(u32 shift,u32 show_offset,u32 file_select,u32 haveThumbnail)
{
	if(haveThumbnail && !Launcher_IsListArtMode())
		return;

	u32 need_show_folder;
	//u32 line;
	u32 char_num;
	u32 y_offset= 20;
	int namelen;
	static u32 orgtt = 123455;
	static u32 last_show_offset = 0xffffffff;
	static u32 last_file_select = 0xffffffff;
	static u32 last_have_thumbnail = 0xffffffff;
	static u32 last_is_nor = 0xffffffff;
	u32 is_nor = Launcher_IsNORPage();
	u32 timeout = 20;
	//u8 dwName=0;
	char msg[128];
	char temp_filename[100];
	char scroll_text[224];

	if(shift > timeout)
	{
		if((last_show_offset != show_offset) ||
		(last_file_select != file_select) ||
		(last_have_thumbnail != haveThumbnail) ||
		(last_is_nor != is_nor))
		{
			orgtt = 123455;
			dwName = 0;
			last_show_offset = show_offset;
			last_file_select = file_select;
			last_have_thumbnail = haveThumbnail;
			last_is_nor = is_nor;
		}

		if(is_nor)
		{
			need_show_folder = 0;
		}
		else if(show_offset >= folder_total)
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
			char_num = Launcher_ListTextCharsForRow(haveThumbnail, file_select, 1) + 1;
		}
		else{
			char_num = launcher_clean_list ? 37 : 33;
		}


		if(is_nor)
		{
			u32 absolute_index = show_offset + file_select;
			if(absolute_index >= game_total_NOR)
				return;
			Launcher_GetListDisplayName(pNorFS[absolute_index].filename, temp_filename, sizeof(temp_filename));
		}
		else
		{
			u32 offset=0;
			if(show_offset >= folder_total)
				offset = show_offset - folder_total;

			if(file_select < need_show_folder)
				Launcher_GetListDisplayName(pFolder[show_offset+file_select].filename, temp_filename, sizeof(temp_filename));
			else
				Launcher_GetSDListDisplayNameWithFavourite(offset+file_select-need_show_folder, temp_filename, sizeof(temp_filename));
		}

		namelen = strlen(temp_filename);
		if(namelen >(char_num-1) )
		{
			u32 cycle_len;
			snprintf(scroll_text, sizeof(scroll_text), "%s   %s", temp_filename, temp_filename);
			cycle_len = namelen + 3;
			u32  tt = ((shift-timeout)/8)% cycle_len;
			if(orgtt!= tt )
			{
				orgtt = tt ;
				strncpy(msg, scroll_text + tt, sizeof(msg) - 1);
				msg[sizeof(msg) - 1] = '\0';
				if((tt < (u32)namelen) && (temp_filename[tt] > 0x80))
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

				if(Launcher_IsListArtMode() && haveThumbnail)
				{
					Launcher_UpdateSelectedListArtScrollRow(show_offset, file_select, msg, char_num - 1);
				}
				else
				{
					Launcher_DrawListSelectBGScreenClipped(file_select, (char_num-1)*6+1);
					DrawHZText12(msg, char_num-1, 1+16, y_offset + file_select*14, LAUNCHER_SELECTED_TEXT,1);
				}
			}
		}
	}
}
//---------------------------------------------------------------------------------
void Show_MENU_btn()
{
	char *msg = gl_menu_btn;

	if(gl_select_lang == 0xE2E2)
		msg = (char*)" (B)\310\241\317\373    (A)\310\267\266\250";

	Clear(60,118-1,55,14,gl_color_selectBG_sd,1);
	Clear(125,118-1,55,14,gl_color_selectBG_sd,1);
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
static char *Launcher_RomMenuText(u32 line)
{
	switch(line)
	{
	case 0: return (char*)DSTEXT_ROM_MENU_CLEAN;
	case 1: return (char*)DSTEXT_ROM_MENU_ADDON;
	case 2: return (char*)DSTEXT_ROM_MENU_WRITE_NOR;
	case 3: return (char*)DSTEXT_ROM_MENU_WRITE_NOR_ADDON;
	case 4: return (char*)DSTEXT_ROM_MENU_SAVE_TYPE;
	case 5: return (char*)DSTEXT_ROM_MENU_CHEAT;
	default: return (char*)"";
	}
}

static char *Launcher_NorMenuText(u32 line)
{
	switch(line)
	{
	case 0: return (char*)DSTEXT_NOR_MENU_DIRECT;
	case 1: return (char*)DSTEXT_NOR_MENU_DELETE;
	case 2: return (char*)DSTEXT_NOR_MENU_FORMAT;
	case 3: return (char*)DSTEXT_NOR_MENU_LOAD_SAVE;
	case 4: return (char*)DSTEXT_NOR_MENU_SAVE_SAVE;
	default: return (char*)"";
	}
}
//---------------------------------------------------------------------------------
static void Show_MENU_Row(u32 line, u32 menu_select, PAGE_NUM page, u32 havecht, u32 Save_num)
{
	u32 y_offset= 30;
	u16 name_color;
	char msg[64];

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
			name_color = RGB(15, 15, 15);
		}
		else{
			name_color = gl_color_text;
		}
	}
	else{
		name_color = gl_color_text;
	}

	if(page==NOR_list)
		DrawHZText12(Launcher_NorMenuText(line), 32, 47, row_y, name_color,1);
	else
	{
		if(line == 5)//cheat
		{
			const char *cheat_label = Launcher_RomMenuText(line);
			if(!strcmp(cheat_label, "Cheat"))
				cheat_label = "Cheats";
			if(gl_cheat_selected_count)
				snprintf(msg,sizeof(msg),"%s (%ld)",cheat_label,gl_cheat_selected_count);
			else
				snprintf(msg,sizeof(msg),"%s",cheat_label);
			DrawHZText12(msg, 32, 47, row_y, name_color,1);
		}
		else{
			DrawHZText12(Launcher_RomMenuText(line), 32, 47, row_y, name_color,1);

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
		linemax = firstgame ? 5 : 3;
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

static void Launcher_ReadBootToSetting(void)
{
	char buf[32];

	launcher_boot_target = LAUNCHER_BOOT_TO_START;
	gl_resume_last_on = 0;
	memset(buf, 0, sizeof(buf));
	if(!Launcher_SettingsReadValue("Boot to", buf, sizeof(buf)))
	{
		if(Launcher_SettingsReadValue("Resume last", buf, sizeof(buf)) &&
		((buf[0] == '1') || !strcasecmp(buf, "On") || !strcasecmp(buf, DSTEXT_ON)))
		{
			launcher_boot_target = LAUNCHER_BOOT_TO_LAST_GAME;
			gl_resume_last_on = 1;
			launcher_settings_migration_pending = 1;
		}
		else
			launcher_settings_migration_pending = 1;
		return;
	}

	if((buf[0] == '5') || !strcasecmp(buf, "Favourites") || !strcasecmp(buf, "Favorites") ||
	!strcasecmp(buf, DSTEXT_BOOT_TO_FAVOURITES) || !strcasecmp(buf, DSTEXT_FAVOURITES))
		launcher_boot_target = LAUNCHER_BOOT_TO_FAVOURITES;
	else if((buf[0] == '4') || !strcasecmp(buf, "Recents") || !strcasecmp(buf, "Recently played") ||
	!strcasecmp(buf, DSTEXT_BOOT_TO_RECENTS) || !strcasecmp(buf, DSTEXT_RECENTLY_PLAYED))
		launcher_boot_target = LAUNCHER_BOOT_TO_RECENTS;
	else if((buf[0] == '3') || !strcasecmp(buf, "Last game") || !strcasecmp(buf, "Last played") ||
	!strcasecmp(buf, DSTEXT_BOOT_TO_LAST_GAME) || !strcasecmp(buf, DSTEXT_LAST_PLAYED))
		launcher_boot_target = LAUNCHER_BOOT_TO_LAST_GAME;
	else if((buf[0] == '2') || !strcasecmp(buf, "NOR") || !strcasecmp(buf, DSTEXT_BOOT_TO_NOR))
		launcher_boot_target = LAUNCHER_BOOT_TO_NOR;
	else if((buf[0] == '1') || !strcasecmp(buf, "SD") || !strcasecmp(buf, "SD Card") ||
	!strcasecmp(buf, DSTEXT_BOOT_TO_SD) || !strcasecmp(buf, DSTEXT_SD_CARD))
		launcher_boot_target = LAUNCHER_BOOT_TO_SD;
	else
		launcher_boot_target = LAUNCHER_BOOT_TO_START;

	gl_resume_last_on = (launcher_boot_target == LAUNCHER_BOOT_TO_LAST_GAME);

	if(strcasecmp(buf, "Start") && strcasecmp(buf, "SD") && strcasecmp(buf, "NOR") &&
	strcasecmp(buf, "Last game") && strcasecmp(buf, "Recents") && strcasecmp(buf, "Favourites"))
		launcher_settings_migration_pending = 1;
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

static const char *Launcher_BootToText(void)
{
	if(launcher_boot_target == LAUNCHER_BOOT_TO_FAVOURITES)
		return DSTEXT_BOOT_TO_FAVOURITES;
	if(launcher_boot_target == LAUNCHER_BOOT_TO_RECENTS)
		return DSTEXT_BOOT_TO_RECENTS;
	if(launcher_boot_target == LAUNCHER_BOOT_TO_LAST_GAME)
		return DSTEXT_BOOT_TO_LAST_GAME;
	if(launcher_boot_target == LAUNCHER_BOOT_TO_NOR)
		return DSTEXT_BOOT_TO_NOR;
	if(launcher_boot_target == LAUNCHER_BOOT_TO_SD)
		return DSTEXT_BOOT_TO_SD;
	return DSTEXT_BOOT_TO_START;
}

static const char *Launcher_BootToSettingName(void)
{
	if(launcher_boot_target == LAUNCHER_BOOT_TO_FAVOURITES)
		return "Favourites";
	if(launcher_boot_target == LAUNCHER_BOOT_TO_RECENTS)
		return "Recents";
	if(launcher_boot_target == LAUNCHER_BOOT_TO_LAST_GAME)
		return "Last game";
	if(launcher_boot_target == LAUNCHER_BOOT_TO_NOR)
		return "NOR";
	if(launcher_boot_target == LAUNCHER_BOOT_TO_SD)
		return "SD";
	return "Start";
}

static void Launcher_CycleBootTo(int dir)
{
	if(dir < 0)
		launcher_boot_target = (launcher_boot_target == 0) ? (LAUNCHER_BOOT_TO_TOTAL - 1) : (launcher_boot_target - 1);
	else
		launcher_boot_target = (launcher_boot_target + 1) % LAUNCHER_BOOT_TO_TOTAL;
	gl_resume_last_on = (launcher_boot_target == LAUNCHER_BOOT_TO_LAST_GAME);
	Launcher_SaveUnifiedSettings();
}

void Launcher_MarkTopbarNameDirty(void)
{
	launcher_system_name_dirty = 1;
	launcher_current_topbar_bg = 0;
}

void Launcher_UpdateCheatTitle(void)
{
	u32 len;
	u32 max_chars = Launcher_CheatTitleMaxChars();
	u32 x;
	char shown[40];
	u32 i;
	u32 cycle;
	const u16 *bg = (const u16*)gImage_SD_LIST;

	if(!launcher_cheat_title[0])
		return;

	len = DrawText12VisibleLength(launcher_cheat_title);
	if(len <= max_chars)
		return;

	launcher_cheat_title_frame++;
	if(launcher_cheat_title_frame < 40)
		return;
	if(((launcher_cheat_title_frame - 40) % 8) != 0)
		return;

	launcher_cheat_title_offset++;
	cycle = len + 4;
	for(i = 0; i < max_chars; i++)
	{
		u32 pos = (launcher_cheat_title_offset + i) % cycle;
		shown[i] = (pos < len) ? launcher_cheat_title[pos] : ' ';
	}
	shown[max_chars] = '\0';

	Launcher_ClearWithThemeBG(bg, 0, 3, (launcher_cheat_counter_x > 3) ? (launcher_cheat_counter_x - 3) : 181, 13);
	x = 3;
	DrawHZText12(shown, 0, x, 3, gl_color_topbar_text, 1);
}

void Launcher_DrawCheatBackground(const char *title)
{
	u32 len;
	u32 draw_chars;
	u32 max_chars = Launcher_CheatTitleMaxChars();
	u32 x = 3;

	Launcher_DrawThemeBGFull((const u16*)gImage_SD_LIST);
	Launcher_ClearWithThemeBG((const u16*)gImage_SD_LIST, 0, 0, 185, LAUNCHER_TOP_BAR_HEIGHT);

	memset(launcher_cheat_title, 0, sizeof(launcher_cheat_title));
	strncpy(launcher_cheat_title, (title && title[0]) ? title : DSTEXT_ROM_MENU_CHEAT, sizeof(launcher_cheat_title) - 1);
	launcher_cheat_title_frame = 0;
	launcher_cheat_title_offset = 0;
	launcher_cheat_counter_valid = 0;

	len = DrawText12VisibleLength(launcher_cheat_title);
	draw_chars = (len > max_chars) ? max_chars : 0;
	DrawHZText12(launcher_cheat_title, draw_chars, x, 3, gl_color_topbar_text, 1);
}

void Launcher_ClearCheatRegion(u16 x, u16 y, u16 w, u16 h)
{
	Launcher_ClearWithThemeBG((const u16*)gImage_SD_LIST, x, y, w, h);
}

void Launcher_DrawCheatCounter(u32 totalcount, u32 select)
{
	char msg[20];
	const u16 *bg = (const u16*)gImage_SD_LIST;
	u16 clear_x;
	u32 len;

	sprintf(msg, "%lu/%lu", select, totalcount);
	len = strlen(msg);
	launcher_cheat_counter_x = 184 + ((len < 9) ? (51 - len * 6) : 0);
	clear_x = launcher_cheat_counter_x;
	if(launcher_cheat_counter_valid && (launcher_cheat_counter_last_x < clear_x))
		clear_x = launcher_cheat_counter_last_x;
	Launcher_ClearWithThemeBG(bg, clear_x, 3, 240 - clear_x, 13);
	DrawHZText12(msg, 0, launcher_cheat_counter_x, 3, gl_color_topbar_text, 1);
	launcher_cheat_counter_last_x = launcher_cheat_counter_x;
	launcher_cheat_counter_valid = 1;
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
	memset(launcher_virtual_gamecode, 0, sizeof(launcher_virtual_gamecode));
	memset(launcher_virtual_gamecode_valid, 0, sizeof(launcher_virtual_gamecode_valid));
	for(i = 0; (i < launcher_favourite_count) && (count < MAX_files); i++)
	{
		if(!Launcher_SplitFullPath(Launcher_FavouritesBuffer()[i], path_part, sizeof(path_part), name, sizeof(name)))
			continue;
		strncpy(p_recently_play[count], Launcher_FavouritesBuffer()[i], sizeof(p_recently_play[count]) - 1);
		p_recently_play[count][sizeof(p_recently_play[count]) - 1] = '\0';
		strncpy(pFilename_buffer[count].filename, name, sizeof(pFilename_buffer[count].filename) - 1);
		pFilename_buffer[count].filename[sizeof(pFilename_buffer[count].filename) - 1] = '\0';
		if(Launcher_GetVirtualFileInfo(Launcher_FavouritesBuffer()[i], name, &size, launcher_virtual_gamecode[count]))
		{
			pFilename_buffer[count].filesize = size;
			launcher_virtual_gamecode_valid[count] =
				(strlen(name) >= 3) &&
				(!strcasecmp(&(name[strlen(name) - 3]), "gba") ||
				!strcasecmp(&(name[strlen(name) - 3]), "agb"));
		}
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
	memset(launcher_virtual_gamecode, 0, sizeof(launcher_virtual_gamecode));
	memset(launcher_virtual_gamecode_valid, 0, sizeof(launcher_virtual_gamecode_valid));
	for(i = 0; i < count; i++)
	{
		if(Recent_GetPathAt(i, full_path, sizeof(full_path), name, sizeof(name)))
		{
			strncpy(pFilename_buffer[i].filename, name, sizeof(pFilename_buffer[i].filename) - 1);
			pFilename_buffer[i].filename[sizeof(pFilename_buffer[i].filename) - 1] = '\0';
			if(Launcher_GetVirtualFileInfo(full_path, name, &size, launcher_virtual_gamecode[i]))
			{
				pFilename_buffer[i].filesize = size;
				launcher_virtual_gamecode_valid[i] =
					(strlen(name) >= 3) &&
					(!strcasecmp(&(name[strlen(name) - 3]), "gba") ||
					!strcasecmp(&(name[strlen(name) - 3]), "agb"));
			}
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
	if( (gl_show_Thumbnail != LAUNCHER_VIEW_LIST) &&
		(gl_show_Thumbnail != LAUNCHER_VIEW_HORIZONTAL) &&
		(gl_show_Thumbnail != LAUNCHER_VIEW_VERTICAL) &&
		(gl_show_Thumbnail != LAUNCHER_VIEW_LIST_ART))
	{
		gl_show_Thumbnail = LAUNCHER_VIEW_LIST;
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
	{
		u16 led_status = (gl_led_open_sel<<7) | (gl_Breathing_R<<5) | (gl_Breathing_G<<4) | (gl_Breathing_B<<3) | (gl_SD_R<<2) | (gl_SD_G<<1) | (gl_SD_B) ;
		Set_LED_control(led_status);
	}

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
    /* Drain both held keys and the edge-event latches. Merely scanning until
       release leaves keysUp() pending, so Start/Select can otherwise surface
       as a shortcut on a later screen. */
    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        (void)keysDown();
        (void)keysDownRepeat();
        (void)keysUp();
        if(keysHeld() == 0)
            break;
    }
    VBlankIntrWait();
    UIAudio_Update();
    scanKeys();
    (void)keysDown();
    (void)keysDownRepeat();
    (void)keysUp();
    VBlankIntrWait();
    UIAudio_Update();
    scanKeys();
    (void)keysDown();
    (void)keysDownRepeat();
    (void)keysUp();
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
	static u32 last_recent_favourites = 0xFFFFFFFF;
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

	show_recent_title = (page_num == SD_list) && recents_view_active;
	show_folder_title = ((page_num == SD_list) || (page_num == NOR_list)) && !show_recent_title;
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

	need_redraw = gl_clock_dirty;
	if(show_recent_title && gl_clock_dirty && (page_num == last_page_num) &&
	(page_mode == last_page_mode) && !launcher_system_name_dirty &&
	(last_recent_favourites == recents_view_favourites))
		need_redraw = 0;
	if(!show_recent_title && !show_folder_title &&
	((HH != last_hh) || (MM != last_mm) || (SS != last_ss)))
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
			Launcher_ClearWithThemeBG(Launcher_GetBGImage(),70, 3, 115, 13);
		else if (page_num==NOR_list)
			Launcher_ClearWithThemeBG(Launcher_GetBGImage(),70, 3, 115, 13);

		if(launcher_system_name_dirty || (page_num != last_page_num))
		{
			Launcher_DrawTopbarName(page_num);
			launcher_system_name_dirty = 0;
		}

		if(show_recent_title)
		{
			/* Restore the complete title/name band so a shorter translation or
			a newly centred title cannot leave pixels behind. */
			Launcher_ClearWithThemeBG(Launcher_GetTopbarBG(page_num), 0, 3, 185, 13);
			Launcher_DrawTopbarName(page_num);
			Launcher_DrawTopbarTitle(page_num, recent_title);
		}
		else if(show_folder_title)
		{
			u32 len;
			memset(folder_title, 0, sizeof(folder_title));
			Launcher_CleanTitle(Launcher_GetCurrentFolderLabel(), folder_title, sizeof(folder_title));
			Launcher_MakeEllipsisText(folder_title, folder_shown, sizeof(folder_shown), 17);
			len = DrawText12VisibleLength(folder_shown);
			DrawHZText12(folder_shown, 0, (240 - (len * 6)) / 2, 3, gl_color_topbar_text, 1);
		}
		else
		{
			Launcher_FormatClock(msgtime, sizeof(msgtime), HH, MM, SS);
			DrawHZText12(msgtime,0,120,3,gl_color_topbar_text,1);
		}
		gl_clock_dirty = 0;
	}

	last_hh = HH;
	last_mm = MM;
	last_ss = SS;
	last_page_num = page_num;
	last_page_mode = page_mode;
	if(show_recent_title)
		last_recent_favourites = recents_view_favourites;
	else
		last_recent_favourites = 0xFFFFFFFF;
}
//---------------------------------------------------------------
void IWRAM_CODE make_pogoshell_arguments(TCHAR *cmdname, TCHAR *filename, u32 cmdsize, u32 filesize, u32 Address, u32 offset)
{
	u32 *p, addr;
	char *ptr, *cmdptr, *fileptr;

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

static void __attribute__((noinline)) Launcher_LoadCustomThumbManifest(u32 style)
{
	u32 hash;

	if(style > LAUNCHER_THUMB_STYLE_BOX)
		style = LAUNCHER_THUMB_STYLE_TITLE;
	if(launcher_custom_thumb_manifest_loaded[style])
		return;

	launcher_custom_thumb_manifest_loaded[style] = 1;
	launcher_custom_thumb_manifest_present[style] = 0;
	launcher_custom_thumb_manifest_count[style] = 0;
	memset(&launcher_custom_thumb_scan_dir, 0, sizeof(launcher_custom_thumb_scan_dir));
	memset(&launcher_custom_thumb_scan_info, 0, sizeof(launcher_custom_thumb_scan_info));
	sprintf(launcher_custom_thumb_scan_path, "%s/CUSTOM", Launcher_ThumbnailFolder());
	if(f_opendir(&launcher_custom_thumb_scan_dir, launcher_custom_thumb_scan_path) != FR_OK)
		return;

	launcher_custom_thumb_manifest_present[style] = 1;
	while(launcher_custom_thumb_manifest_count[style] < LAUNCHER_CUSTOM_THUMB_MANIFEST_MAX)
	{
		if((f_readdir(&launcher_custom_thumb_scan_dir, &launcher_custom_thumb_scan_info) != FR_OK) ||
		!launcher_custom_thumb_scan_info.fname[0])
			break;
		if(launcher_custom_thumb_scan_info.fattrib & AM_DIR)
			continue;
		memset(launcher_custom_thumb_scan_name, 0, sizeof(launcher_custom_thumb_scan_name));
		strncpy(launcher_custom_thumb_scan_name, launcher_custom_thumb_scan_info.fname,
		sizeof(launcher_custom_thumb_scan_name) - 1);
		Launcher_CustomThumbStripLine(launcher_custom_thumb_scan_name);
		if(!launcher_custom_thumb_scan_name[0])
			continue;
		hash = Launcher_CustomThumbHash(launcher_custom_thumb_scan_name);
		if(hash)
			launcher_custom_thumb_manifest_hash[style][launcher_custom_thumb_manifest_count[style]++] = hash;
	}
	f_closedir(&launcher_custom_thumb_scan_dir);
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

static u16 Launcher_ListArtRawInput(void)
{
	return (u16)((~REG_KEYINPUT) & LAUNCHER_LIST_ART_BUFFERABLE_KEYS);
}

static void Launcher_ListArtQueueInput(u16 key)
{
	u32 tail;

	if(!key || launcher_list_art_input_queue_count >= LAUNCHER_LIST_ART_INPUT_QUEUE_SIZE)
		return;
	tail = (launcher_list_art_input_queue_head + launcher_list_art_input_queue_count) %
	LAUNCHER_LIST_ART_INPUT_QUEUE_SIZE;
	launcher_list_art_input_queue[tail] = key;
	launcher_list_art_input_queue_count++;
}

static void Launcher_ListArtPollInput(void)
{
	u16 current;
	u16 pressed;

	if(!launcher_list_art_input_capture)
		return;
	current = Launcher_ListArtRawInput();
	pressed = current & (u16)~launcher_list_art_input_previous;
	launcher_list_art_input_previous = current;
	if(pressed & KEY_A) Launcher_ListArtQueueInput(KEY_A);
	if(pressed & KEY_B) Launcher_ListArtQueueInput(KEY_B);
}

static void Launcher_ListArtBeginInputCapture(void)
{
	launcher_list_art_input_previous = Launcher_ListArtRawInput();
	launcher_list_art_input_capture = 1;
}

static void Launcher_ListArtEndInputCapture(void)
{
	Launcher_ListArtPollInput();
	launcher_list_art_input_capture = 0;
}

static u16 Launcher_ListArtTakeInput(void)
{
	u16 key;

	if(!launcher_list_art_input_queue_count)
		return 0;
	key = launcher_list_art_input_queue[launcher_list_art_input_queue_head];
	launcher_list_art_input_queue_head = (launcher_list_art_input_queue_head + 1) %
	LAUNCHER_LIST_ART_INPUT_QUEUE_SIZE;
	launcher_list_art_input_queue_count--;
	return key;
}

static u32 Launcher_ReadThumbnailBytes(FIL *file, u8 *dst, u32 read_size, UINT *read_out)
{
	u32 res = FR_OK;
	u32 total = 0;

	if(!launcher_list_art_input_capture)
		return f_read(file, dst, read_size, read_out);

	while(total < read_size)
	{
		UINT got = 0;
		u32 chunk = read_size - total;
		if(chunk > 2048)
			chunk = 2048;
		Launcher_ListArtPollInput();
		res = f_read(file, dst + total, chunk, &got);
		total += got;
		Launcher_ListArtPollInput();
		if((res != FR_OK) || (got != chunk))
			break;
	}
	if(read_out)
		*read_out = (UINT)total;
	return res;
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
	res = Launcher_ReadThumbnailBytes(&gfile, dst, read_size, (UINT*)&rett);
	f_close(&gfile);
	return (res == FR_OK) && (rett == read_size);
}


static void Launcher_CustomThumbFileName(const char *filename, char *name, u32 name_size);

static u32 Launcher_IsValidGameCodeBytes(const u8 gamecode[4])
{
	u32 i;
	if(!gamecode)
		return 0;
	for(i = 0; i < 4; i++)
	{
		u8 c = gamecode[i];
		if(!(((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9'))))
			return 0;
	}
	return 1;
}

static u32 Launcher_LoadThumbnailByGameCode(const u8 gamecode[4], u8 *dst)
{
	u32 rett;
	u32 res;
	TCHAR picpath[160];
	u32 read_size = Launcher_ThumbnailReadSize();

	if(!gamecode || !dst)
		return 0;
	snprintf(picpath, sizeof(picpath), "%s/%c/%c/%c%c%c%c.bmp",
	Launcher_ThumbnailFolder(), gamecode[0], gamecode[1],
	gamecode[0], gamecode[1], gamecode[2], gamecode[3]);
	res = f_open(&gfile, picpath, FA_READ);
	if(res != FR_OK)
		return 0;
	UIAudio_StopForSharedBufferUse();
	res = Launcher_ReadThumbnailBytes(&gfile, dst, read_size, (UINT*)&rett);
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

static u32 Launcher_LoadThumbnailFromRomHeader(TCHAR *filename, u8 *dst)
{
	u32 rett;
	u32 res;

	if(!filename || !dst)
		return 0;

	res = f_open(&gfile, filename, FA_READ);
	if(res != FR_OK)
		return 0;
	f_lseek(&gfile, 0xAC);
	res = Launcher_ReadThumbnailBytes(&gfile, GAMECODE, 4, (UINT *)&rett);
	f_close(&gfile);
	if((res != FR_OK) || (rett != 4))
		return 0;

	return Launcher_LoadThumbnailByGameCode(GAMECODE, dst);
}

u32 Load_ThumbnailEx(TCHAR *pfilename_pic, u8 *dst)
{
	TCHAR custom_name[104];

	Launcher_CustomThumbFileName(pfilename_pic, custom_name, sizeof(custom_name));

	if(Launcher_LoadCustomThumbnailByName(custom_name, dst))
		return 1;

	return Launcher_LoadThumbnailFromRomHeader(pfilename_pic, dst);
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

static void Launcher_SetThumbCachePointers(void)
{
	launcher_cache_prev.thumb_data = pReadCache + 0x14C36;
	launcher_cache_selected.thumb_data = pReadCache + 0x10036;
	launcher_cache_next.thumb_data = pReadCache + 0x19836;
}

static u32 Launcher_ThumbCachePointersValid(void)
{
	u8 *prev = launcher_cache_prev.thumb_data;
	u8 *selected = launcher_cache_selected.thumb_data;
	u8 *next = launcher_cache_next.thumb_data;
	u32 prev_valid = prev == (pReadCache + 0x10036) || prev == (pReadCache + 0x14C36) || prev == (pReadCache + 0x19836);
	u32 selected_valid = selected == (pReadCache + 0x10036) || selected == (pReadCache + 0x14C36) || selected == (pReadCache + 0x19836);
	u32 next_valid = next == (pReadCache + 0x10036) || next == (pReadCache + 0x14C36) || next == (pReadCache + 0x19836);

	if(!Launcher_IsListArtMode())
		return prev == (pReadCache + 0x14C36) &&
		selected == (pReadCache + 0x10036) &&
		next == (pReadCache + 0x19836);
	return prev_valid && selected_valid && next_valid &&
	prev != selected && prev != next && selected != next;
}

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

static void __attribute__((unused)) Launcher_ClearTitleFillClip(int x, int y, int w, int h, u16 base_fill)
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

static void __attribute__((unused)) Launcher_DrawPicClip(const u16 *src, int x, int y, int w, int h)
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

static u32 Launcher_RoundedThumbPixelVisible(int dst_x, int dst_y, int w, int h)
{
	if(!Launcher_RoundedCornersForCarousel())
		return 1;
	if(dst_y == 0 && (dst_x < 5 || dst_x >= (w - 5)))
		return 0;
	if(dst_y == 1 && (dst_x < 3 || dst_x >= (w - 3)))
		return 0;
	if(dst_y == 2 && (dst_x < 2 || dst_x >= (w - 2)))
		return 0;
	if((dst_y == 3 || dst_y == 4) && (dst_x < 1 || dst_x >= (w - 1)))
		return 0;
	if(dst_y == (h - 1) && (dst_x < 5 || dst_x >= (w - 5)))
		return 0;
	if(dst_y == (h - 2) && (dst_x < 3 || dst_x >= (w - 3)))
		return 0;
	if(dst_y == (h - 3) && (dst_x < 2 || dst_x >= (w - 2)))
		return 0;
	if((dst_y == (h - 4) || dst_y == (h - 5)) && (dst_x < 1 || dst_x >= (w - 1)))
		return 0;
	return 1;
}

static void Launcher_DrawPreparedThumbMasked(const u16 *src, int src_stride, int x, int y, int w, int h)
{
	int row;
	vu16 *dst_base = (vu16*)VRAM;

	if(!src || src_stride <= 0 || w <= 0 || h <= 0)
		return;

	for(row = 0; row < h; row++)
	{
		int screen_y = y + row;
		int start_x = 0;
		int end_x = w;

		if(screen_y < 0 || screen_y >= 160)
			continue;

		while(start_x < end_x && !Launcher_RoundedThumbPixelVisible(start_x, row, w, h))
			start_x++;
		while(end_x > start_x && !Launcher_RoundedThumbPixelVisible(end_x - 1, row, w, h))
			end_x--;
		if(start_x >= end_x)
			continue;

		if((x + start_x) < 0)
			start_x = -x;
		if((x + end_x) > 240)
			end_x = 240 - x;
		if(start_x >= end_x)
			continue;

		dmaCopy((void*)(src + row * src_stride + start_x),
		(void*)(dst_base + screen_y * 240 + x + start_x),
		(end_x - start_x) * 2);
	}
}

static u16 Launcher_ArtBorderColourEx(u32 selected)
{
	switch(launcher_art_border_mode)
	{
		case 1: return selected ? gl_color_selectBG_sd : RGB(16, 16, 16);
		case 2: return RGB(0, 0, 0);
		case 3: return RGB(16, 16, 16);
		case 4: return RGB(31, 31, 31);
		default: return RGB(0, 0, 0);
	}
}

static void Launcher_RestoreThumbCornerMaskRaw(const u16 *bg, int x, int y, int w, int h)
{
	Launcher_RestoreBGClip(bg, x, y, 5, 1);
	Launcher_RestoreBGClip(bg, x, y + 1, 3, 1);
	Launcher_RestoreBGClip(bg, x, y + 2, 2, 1);
	Launcher_RestoreBGClip(bg, x, y + 3, 1, 2);
	Launcher_RestoreBGClip(bg, x + w - 5, y, 5, 1);
	Launcher_RestoreBGClip(bg, x + w - 3, y + 1, 3, 1);
	Launcher_RestoreBGClip(bg, x + w - 2, y + 2, 2, 1);
	Launcher_RestoreBGClip(bg, x + w - 1, y + 3, 1, 2);
	Launcher_RestoreBGClip(bg, x, y + h - 1, 5, 1);
	Launcher_RestoreBGClip(bg, x, y + h - 2, 3, 1);
	Launcher_RestoreBGClip(bg, x, y + h - 3, 2, 1);
	Launcher_RestoreBGClip(bg, x, y + h - 5, 1, 2);
	Launcher_RestoreBGClip(bg, x + w - 5, y + h - 1, 5, 1);
	Launcher_RestoreBGClip(bg, x + w - 3, y + h - 2, 3, 1);
	Launcher_RestoreBGClip(bg, x + w - 2, y + h - 3, 2, 1);
	Launcher_RestoreBGClip(bg, x + w - 1, y + h - 5, 1, 2);
}

static void Launcher_RestoreThumbCornerMask(int x, int y, int w, int h)
{
	if(!launcher_carousel_art_draw || !Launcher_RoundedCornersForCarousel())
		return;
	Launcher_RestoreThumbCornerMaskRaw((const u16*)Launcher_GetBGImage(), x, y, w, h);
}

static void Launcher_DrawThumbBorderEx(int x, int y, int w, int h, u32 selected)
{
	u16 colour;
	if(!launcher_carousel_art_draw || !launcher_art_border_mode)
		return;
	colour = Launcher_ArtBorderColourEx(selected);
	if(Launcher_RoundedCornersForCarousel())
	{
		Launcher_ClearClip(x + 5, y - 1, w - 10, 1, colour);
		Launcher_ClearClip(x + 5, y + h, w - 10, 1, colour);
		Launcher_ClearClip(x - 1, y + 5, 1, h - 10, colour);
		Launcher_ClearClip(x + w, y + 5, 1, h - 10, colour);
		Launcher_ClearClip(x + 3, y, 2, 1, colour);
		Launcher_ClearClip(x + 2, y + 1, 1, 1, colour);
		Launcher_ClearClip(x + 1, y + 2, 1, 1, colour);
		Launcher_ClearClip(x, y + 3, 1, 2, colour);
		Launcher_ClearClip(x + w - 5, y, 2, 1, colour);
		Launcher_ClearClip(x + w - 3, y + 1, 1, 1, colour);
		Launcher_ClearClip(x + w - 2, y + 2, 1, 1, colour);
		Launcher_ClearClip(x + w - 1, y + 3, 1, 2, colour);
		Launcher_ClearClip(x + 3, y + h - 1, 2, 1, colour);
		Launcher_ClearClip(x + 2, y + h - 2, 1, 1, colour);
		Launcher_ClearClip(x + 1, y + h - 3, 1, 1, colour);
		Launcher_ClearClip(x, y + h - 5, 1, 2, colour);
		Launcher_ClearClip(x + w - 5, y + h - 1, 2, 1, colour);
		Launcher_ClearClip(x + w - 3, y + h - 2, 1, 1, colour);
		Launcher_ClearClip(x + w - 2, y + h - 3, 1, 1, colour);
		Launcher_ClearClip(x + w - 1, y + h - 5, 1, 2, colour);
		return;
	}
	Launcher_ClearClip(x - 1, y - 1, w + 2, 1, colour);
	Launcher_ClearClip(x - 1, y + h, w + 2, 1, colour);
	Launcher_ClearClip(x - 1, y - 1, 1, h + 2, colour);
	Launcher_ClearClip(x + w, y - 1, 1, h + 2, colour);
}

static void Launcher_DrawThumbBorder(int x, int y, int w, int h)
{
	Launcher_DrawThumbBorderEx(x, y, w, h, 1);
}

static void Launcher_FinishCarouselArtworkEx(int x, int y, int w, int h, u32 selected)
{
	Launcher_RestoreThumbCornerMask(x, y, w, h);
	Launcher_DrawThumbBorderEx(x, y, w, h, selected);
}

static void Launcher_FinishCarouselArtwork(int x, int y, int w, int h)
{
	Launcher_FinishCarouselArtworkEx(x, y, w, h, 1);
}

static void Launcher_RestoreStartThumbCorners(int x, int y, int w, int h)
{
	if(!Launcher_RoundedCornersForStart())
		return;
	Launcher_RestoreThumbCornerMaskRaw((const u16*)gImage_START, x, y, w, h);
}

static void __attribute__((unused)) Launcher_DrawThumbInBox(const u16 *src, int src_w, int src_h, int box_x, int box_y, int box_w, int box_h)
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
	Launcher_FinishCarouselArtwork(draw_x, draw_y, draw_w, draw_h);
}

static void __attribute__((unused)) Launcher_DrawThumbPanel(const u16 *src, int src_w, int src_h, u16 *dst, int box_x, int box_y, int box_w, int box_h)
{
	Launcher_ScaleThumbToBox(src, src_w, src_h, dst, box_w, box_h);
	Launcher_DrawPicClipStride(dst, box_w, box_x, box_y, box_w, box_h);
	Launcher_FinishCarouselArtwork(box_x, box_y, box_w, box_h);
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

static void Launcher_DrawPreparedCarouselSideArtwork(const u16 *src, int stride,
		int x, int y, int w, int h)
{
	if(launcher_popup_restore_redraw)
	{
		/* Rebuild the border before the image on popup close. The masked copy
		then leaves both straight and rounded border pixels untouched. */
		Launcher_RestoreThumbCornerMask(x, y, w, h);
		Launcher_DrawThumbBorderEx(x, y, w, h, 0);
		Launcher_DrawPreparedThumbMasked(src, stride, x, y, w, h);
		return;
	}
	Launcher_DrawPicClipStride(src, stride, x, y, w, h);
	Launcher_FinishCarouselArtworkEx(x, y, w, h, 0);
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

	if(!launcher_popup_restore_redraw)
		Launcher_RestoreHorizontalThumbBox(x, y, w, h, outline, fill);
	Launcher_DrawPreparedCarouselSideArtwork(src, stride, draw_x, draw_y, draw_w, draw_h);
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
	Launcher_FinishCarouselArtwork(draw_x, draw_y, draw_w, draw_h);
}

static void __attribute__((unused)) Launcher_DrawIconCenteredClip(const u16 *icon, int box_x, int box_y, int box_w, int box_h)
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

static void __attribute__((unused)) Launcher_DrawIconCenteredClip3x(const u16 *icon, int box_x, int box_y, int box_w, int box_h)
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

static u32 Launcher_ActiveViewMode(void)
{
	if((launcher_active_page == SD_list) || (launcher_active_page == NOR_list))
		return launcher_effective_show_thumbnail;
	return gl_show_Thumbnail;
}

static void Launcher_UpdateEffectiveViewMode(void)
{
	u16 old_view = launcher_effective_show_thumbnail;
	launcher_effective_show_thumbnail = gl_show_Thumbnail;
	if((launcher_active_page == SD_list) && launcher_list_folders && !recents_view_active &&
	gl_show_Thumbnail && (gl_show_Thumbnail != LAUNCHER_VIEW_LIST_ART) &&
	(launcher_sd_launchable_file_count == 0))
		launcher_effective_show_thumbnail = LAUNCHER_VIEW_LIST;
	if(old_view != launcher_effective_show_thumbnail)
		launcher_force_full_redraw = 1;
}

static const u16 *Launcher_GetBGImage(void)
{
	u32 view_mode = Launcher_ActiveViewMode();
	if(view_mode == LAUNCHER_VIEW_HORIZONTAL)
		return (const u16*)gImage_SD_HORIZONTAL;
	if(view_mode == LAUNCHER_VIEW_VERTICAL)
		return (const u16*)gImage_SD_VERTICAL;
	return (const u16*)gImage_SD_LIST;
}

static u32 Launcher_IsListArtMode(void)
{
	return Launcher_ActiveViewMode() == LAUNCHER_VIEW_LIST_ART;
}

static u32 Launcher_IsListLikeMode(void)
{
	u32 mode = Launcher_ActiveViewMode();
	return (mode == LAUNCHER_VIEW_LIST) || (mode == LAUNCHER_VIEW_LIST_ART);
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
	u32 visible_len;
	u32 copy_chars;
	u32 used;

	if(!dst || dst_size == 0)
		return;

	dst[0] = '\0';
	if(!src)
		return;

	visible_len = DrawText12VisibleLength((char*)src);
	if(visible_len <= max_chars)
	{
		strncpy(dst, src, dst_size - 1);
		dst[dst_size - 1] = '\0';
		return;
	}

	if(max_chars <= 3)
	{
		dst[max_chars] = '\0';
		return;
	}

	copy_chars = max_chars - 3;
	DrawText12CopyVisible(dst, dst_size, (char*)src, copy_chars);
	used = strlen(dst);
	if(used + 3 < dst_size)
	{
		dst[used++] = '.';
		dst[used++] = '.';
		dst[used++] = '.';
		dst[used] = '\0';
	}
}

static void __attribute__((unused)) Launcher_GetVerticalFolderLabelInfo(char *cleaned, int cleaned_size, int *outer_left, int *outer_top, int *outer_w, int *outer_h)
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

static int __attribute__((unused)) Launcher_ShouldPreserveVerticalFolderLabel(int *left, int *top, int *w, int *h)
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

static int __attribute__((unused)) Launcher_SplitTitleNarrow(const char *title, char lines[3][32])
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
	Launcher_SetThumbCachePointers();
	launcher_cache_center_index = 0xFFFFFFFF;
	launcher_cache_prev.valid = 0;
	launcher_cache_selected.valid = 0;
	launcher_cache_next.valid = 0;
	launcher_cache_prev.absolute_index = 0xFFFFFFFF;
	launcher_cache_selected.absolute_index = 0xFFFFFFFF;
	launcher_cache_next.absolute_index = 0xFFFFFFFF;
	launcher_cache_prev.has_thumbnail = 0;
	launcher_cache_selected.has_thumbnail = 0;
	launcher_cache_next.has_thumbnail = 0;
	Launcher_InitScaleMaps();
	memset(&launcher_thumbnail_workspace, 0, sizeof(launcher_thumbnail_workspace));
	Launcher_InvalidateListArtScaledCache();
	launcher_thumbnail_workspace_mode = 0xFF;
}

static void Launcher_ActivateThumbnailWorkspace(u32 mode)
{
	if(launcher_thumbnail_workspace_mode == (u8)mode)
		return;
	memset(&launcher_thumbnail_workspace, 0, sizeof(launcher_thumbnail_workspace));
	if(launcher_thumbnail_workspace_mode == LAUNCHER_VIEW_LIST_ART ||
	mode == LAUNCHER_VIEW_LIST_ART)
		Launcher_InvalidateListArtScaledCache();
	launcher_thumbnail_workspace_mode = (u8)mode;
}

static u32 Launcher_LoadThumbDataForIndex(u32 absolute_index, u8 *thumb_data)
{
	TCHAR *name = 0;
	TCHAR custom_name[104];
	u32 virtual_index = 0;
	u32 total_entries = Launcher_GetTotalEntries();
	u32 has_thumbnail = 0;

	if(!thumb_data || absolute_index >= total_entries)
		return 0;

	if(Launcher_IsNORPage())
	{
		name = pNorFS[absolute_index].filename;
		if(name)
		{
			Launcher_CustomThumbFileName(name, custom_name, sizeof(custom_name));
			has_thumbnail = Launcher_LoadCustomThumbnailByName(custom_name, thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		}
		if(!has_thumbnail && Launcher_IsValidGameCodeBytes((const u8*)&pNorFS[absolute_index].gamename[12]))
			has_thumbnail = Launcher_LoadThumbnailByGameCode((const u8*)&pNorFS[absolute_index].gamename[12],
			thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		return has_thumbnail;
	}

	if(absolute_index >= (folder_total + game_total_SD))
		return 0;

	if(absolute_index < folder_total)
	{
		name = pFolder[absolute_index].filename;
		if(name)
			has_thumbnail = Launcher_LoadCustomThumbnailByName(name, thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		return has_thumbnail;
	}

	if(recents_view_active)
	{
		virtual_index = absolute_index - folder_total;
		name = p_recently_play[absolute_index - folder_total];
	}
	else
		name = pFilename_buffer[absolute_index - folder_total].filename;
	if(name)
	{
		u32 len = strlen(name);
		Launcher_CustomThumbFileName(name, custom_name, sizeof(custom_name));
		has_thumbnail = Launcher_LoadCustomThumbnailByName(custom_name, thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		if(!has_thumbnail && recents_view_active && (virtual_index < 10) &&
		launcher_virtual_gamecode_valid[virtual_index])
			has_thumbnail = Launcher_LoadThumbnailByGameCode(launcher_virtual_gamecode[virtual_index],
			thumb_data - LAUNCHER_THUMB_BMP_HEADER);
		else if(!has_thumbnail &&
		(((len >= 3) && !strcasecmp(&(name[len - 3]), "gba")) ||
		((len >= 3) && !strcasecmp(&(name[len - 3]), "agb"))))
			has_thumbnail = Launcher_LoadThumbnailFromRomHeader(name, thumb_data - LAUNCHER_THUMB_BMP_HEADER);
	}
	return has_thumbnail;
}

static void Launcher_LoadThumbCacheForIndex(LauncherThumbCache *cache, u32 absolute_index)
{
	if(!cache || !Launcher_ThumbCachePointersValid() || absolute_index >= Launcher_GetTotalEntries())
		return;

	cache->valid = 1;
	cache->absolute_index = absolute_index;
	cache->has_thumbnail = Launcher_LoadThumbDataForIndex(absolute_index, cache->thumb_data);
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
	if(!info || !info->name)
		return 0;
	if(info->has_thumbnail)
		return 1;
	if(Launcher_IsNORPage())
		return 0;
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

static void Launcher_GetListArtRect(int *x, int *y, int *w, int *h)
{
	int draw_w = (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? 60 : 90;
	int draw_h = 60;
	int draw_x = 240 - 8 - draw_w;
	int draw_y;

	switch(launcher_list_art_position)
	{
		case LAUNCHER_SIDE_ALIGN_TOP:
			draw_y = 27;
			break;
		case LAUNCHER_SIDE_ALIGN_CENTER:
			draw_y = 20 + ((140 - draw_h) / 2);
			break;
		case LAUNCHER_SIDE_ALIGN_BOTTOM:
		default:
			draw_y = 160 - 8 - draw_h;
			break;
	}

	if(x) *x = draw_x;
	if(y) *y = draw_y;
	if(w) *w = draw_w;
	if(h) *h = draw_h;
}

static u16 *Launcher_ListArtScaledBuffer(u32 slot)
{
	if(slot >= LAUNCHER_LIST_ART_CACHE_COUNT)
		slot = 0;
	return launcher_thumbnail_workspace.list_art[slot];
}

static void Launcher_InvalidateListArtScaledCache(void)
{
	u32 i;
	for(i = 0; i < LAUNCHER_LIST_ART_CACHE_COUNT; i++)
	{
		launcher_list_art_scaled_index[i] = 0xFFFFFFFF;
		launcher_list_art_scaled_valid[i] = 0;
		launcher_list_art_scaled_has_art[i] = 0;
		launcher_list_art_scaled_sig_w[i] = 0;
		launcher_list_art_scaled_sig_h[i] = 0;
		launcher_list_art_scaled_sig_style[i] = 0;
		launcher_list_art_scaled_age[i] = 0;
	}
	launcher_list_art_cache_clock = 0;
	launcher_list_art_scaled_selected_slot = 0xFF;
	launcher_list_art_selected_has_art = 0;
	launcher_list_art_pending_index = 0xFFFFFFFF;
	launcher_list_art_pending = 0;
	launcher_list_art_idle_frames = 0;
	launcher_list_art_input_queue_head = 0;
	launcher_list_art_input_queue_count = 0;
	launcher_list_art_input_capture = 0;
	launcher_list_art_input_previous = 0;
	launcher_list_art_span_cache_valid = 0;
	Launcher_InvalidateListRowCache();
}

static int Launcher_FindListArtScaledSlot(u32 absolute_index, int w, int h)
{
	u32 i;
	for(i = 0; i < LAUNCHER_LIST_ART_CACHE_COUNT; i++)
	{
		if(launcher_list_art_scaled_valid[i] &&
		launcher_list_art_scaled_index[i] == absolute_index &&
		launcher_list_art_scaled_sig_w[i] == (u8)w &&
		launcher_list_art_scaled_sig_h[i] == (u8)h &&
		launcher_list_art_scaled_sig_style[i] == (u8)launcher_thumbnail_style)
			return (int)i;
	}
	return -1;
}

static void Launcher_TouchListArtScaledSlot(u32 slot)
{
	if(slot >= LAUNCHER_LIST_ART_CACHE_COUNT)
		return;
	launcher_list_art_cache_clock++;
	if(launcher_list_art_cache_clock == 0)
		launcher_list_art_cache_clock = 1;
	launcher_list_art_scaled_age[slot] = launcher_list_art_cache_clock;
}

static int Launcher_ChooseListArtScaledSlot(u32 center_index)
{
	u32 i;
	int slot = -1;
	u32 farthest = 0;
	u32 oldest = 0xFFFFFFFF;

	for(i = 0; i < LAUNCHER_LIST_ART_CACHE_COUNT; i++)
	{
		if(!launcher_list_art_scaled_valid[i])
			return (int)i;
	}

	for(i = 0; i < LAUNCHER_LIST_ART_CACHE_COUNT; i++)
	{
		u32 index;
		u32 distance;

		if(i == launcher_list_art_scaled_selected_slot)
			continue;
		index = launcher_list_art_scaled_index[i];
		distance = (index > center_index) ? (index - center_index) : (center_index - index);
		if(slot < 0 || distance > farthest ||
		(distance == farthest && launcher_list_art_scaled_age[i] < oldest))
		{
			slot = (int)i;
			farthest = distance;
			oldest = launcher_list_art_scaled_age[i];
		}
	}

	if(slot < 0)
		slot = 0;
	return slot;
}

static void Launcher_QueueListArtLoad(u32 absolute_index)
{
	if(!launcher_list_art_pending || launcher_list_art_pending_index != absolute_index)
	{
		launcher_list_art_pending = 1;
		launcher_list_art_pending_index = absolute_index;
		launcher_list_art_idle_frames = 0;
	}
}

static int Launcher_LoadListArtScaledSlot(u32 absolute_index, int w, int h)
{
	int slot;
	u8 *thumb_data = pReadCache + 0x10036;
	u32 has_art;

	if(absolute_index >= Launcher_GetTotalEntries())
		return -1;

	slot = Launcher_FindListArtScaledSlot(absolute_index, w, h);
	if(slot >= 0)
	{
		Launcher_TouchListArtScaledSlot((u32)slot);
		return slot;
	}

	slot = Launcher_ChooseListArtScaledSlot(launcher_cache_center_index);
	has_art = Launcher_LoadThumbDataForIndex(absolute_index, thumb_data);
	if(has_art)
	{
		Launcher_ScaleThumbToBox((const u16*)thumb_data,
		Launcher_ThumbnailSourceWidth(),
		Launcher_ThumbnailSourceHeight(),
		Launcher_ListArtScaledBuffer((u32)slot), w, h);
	}
	launcher_list_art_scaled_index[slot] = absolute_index;
	launcher_list_art_scaled_valid[slot] = 1;
	launcher_list_art_scaled_has_art[slot] = (u8)has_art;
	launcher_list_art_scaled_sig_w[slot] = (u8)w;
	launcher_list_art_scaled_sig_h[slot] = (u8)h;
	launcher_list_art_scaled_sig_style[slot] = (u8)launcher_thumbnail_style;
	Launcher_TouchListArtScaledSlot((u32)slot);
	return slot;
}

/* Selection and scrolling use cache state only. An unresolved image is queued
   so SD access can never stall the list's input path. */
static s32 Launcher_GetListArtCachedState(u32 absolute_index, int *x, int *y, int *w, int *h, u32 preserve_current)
{
	int draw_w;
	int draw_h;
	int slot;
	u32 has_art;

	if(!Launcher_IsListArtMode() || absolute_index >= Launcher_GetTotalEntries())
		return 0;

	Launcher_ActivateThumbnailWorkspace(LAUNCHER_VIEW_LIST_ART);
	Launcher_GetListArtRect(x, y, &draw_w, &draw_h);
	if(w) *w = draw_w;
	if(h) *h = draw_h;
	launcher_cache_center_index = absolute_index;
	slot = Launcher_FindListArtScaledSlot(absolute_index, draw_w, draw_h);
	if(slot < 0)
	{
		launcher_cache_selected.valid = 1;
		launcher_cache_selected.absolute_index = absolute_index;
		launcher_cache_selected.has_thumbnail = 0;
		Launcher_QueueListArtLoad(absolute_index);
		if(!preserve_current || !launcher_list_art_screen_has_art)
		{
			launcher_list_art_scaled_selected_slot = 0xFF;
			launcher_list_art_selected_has_art = 0;
		}
		return -1;
	}
	has_art = slot >= 0 && launcher_list_art_scaled_valid[slot] &&
	launcher_list_art_scaled_has_art[slot];
	launcher_cache_selected.valid = 1;
	launcher_cache_selected.absolute_index = absolute_index;
	launcher_cache_selected.has_thumbnail = has_art;
	launcher_list_art_scaled_selected_slot = (slot >= 0) ? (u8)slot : 0xFF;
	launcher_list_art_selected_has_art = (u16)has_art;
	Launcher_TouchListArtScaledSlot((u32)slot);
	if(launcher_list_art_pending)
	{
		launcher_list_art_pending = 0;
		launcher_list_art_pending_index = 0xFFFFFFFF;
		launcher_list_art_idle_frames = 0;
	}
	return has_art ? 1 : 0;
}

static void Launcher_DrawCurrentListArtImageOnly(void)
{
	int x, y, w, h;
	u32 slot = launcher_list_art_scaled_selected_slot;

	if(!launcher_list_art_selected_has_art ||
	slot >= LAUNCHER_LIST_ART_CACHE_COUNT ||
	!launcher_list_art_scaled_valid[slot] ||
	!launcher_list_art_scaled_has_art[slot] ||
	!launcher_cache_selected.valid ||
	!launcher_cache_selected.has_thumbnail ||
	launcher_cache_selected.absolute_index != launcher_cache_center_index ||
	launcher_list_art_scaled_index[slot] != launcher_cache_selected.absolute_index ||
	launcher_list_art_scaled_sig_style[slot] != (u8)launcher_thumbnail_style)
		return;

	Launcher_GetListArtRect(&x, &y, &w, &h);
	launcher_carousel_art_draw = 1;
	Launcher_DrawPreparedThumbMasked(Launcher_ListArtScaledBuffer(slot), w, x, y, w, h);
	Launcher_DrawThumbBorder(x, y, w, h);
	launcher_carousel_art_draw = 0;
}

static void Launcher_DrawListArtImageOnly(u32 show_offset, u32 file_select)
{
	(void)show_offset;
	(void)file_select;
	Launcher_DrawCurrentListArtImageOnly();
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

static void Launcher_PreScaleListArtCache(void)
{
	if(launcher_cache_center_index < Launcher_GetTotalEntries())
		Launcher_GetListArtCachedState(launcher_cache_center_index, 0, 0, 0, 0, 1);
	else
	{
		launcher_list_art_scaled_selected_slot = 0xFF;
		launcher_list_art_selected_has_art = 0;
	}
	Launcher_ListArtPrepareSpanCache();
}

/* Load only the latest selection after navigation becomes idle, then replace
   the fixed artwork overlay without rebuilding the surrounding list. */
static u32 Launcher_ServicePendingListArt(u32 show_offset, u32 file_select)
{
	u32 absolute_index = show_offset + file_select;
	u32 old_screen_has_art;
	u32 selected_has_art;
	int x, y, w, h;
	int slot;
	s32 art_state;

	if(!launcher_list_art_pending || !Launcher_IsListArtMode() ||
	absolute_index >= Launcher_GetTotalEntries())
		return 0;

	if(launcher_list_art_pending_index != absolute_index)
	{
		Launcher_QueueListArtLoad(absolute_index);
		return 0;
	}

	Launcher_GetListArtRect(&x, &y, &w, &h);
	(void)x;
	(void)y;
	launcher_cache_center_index = absolute_index;
	slot = Launcher_FindListArtScaledSlot(absolute_index, w, h);
	if(slot < 0)
	{
		Launcher_ListArtBeginInputCapture();
		slot = Launcher_LoadListArtScaledSlot(absolute_index, w, h);
		Launcher_ListArtEndInputCapture();
	}
	if(slot < 0)
		return 0;

	old_screen_has_art = launcher_list_art_screen_has_art;
	art_state = Launcher_GetListArtCachedState(absolute_index, 0, 0, 0, 0, 0);
	selected_has_art = art_state > 0;

	if(!Launcher_ListRowCacheMatches(show_offset, file_select))
	{
		Launcher_DrawListBodyFromCache(show_offset, file_select, 1);
		return 1;
	}

	if(old_screen_has_art && !selected_has_art)
		Launcher_CopyCachedRowsBehindArt();
	Launcher_BuildSelectedListRowScratch(show_offset, file_select, selected_has_art);
	Launcher_CopySelectedListRowScratch(file_select, selected_has_art);
	if(selected_has_art)
		Launcher_DrawCurrentListArtImageOnly();
	launcher_list_art_screen_has_art = (u8)selected_has_art;
	return 1;
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

static void __attribute__((unused)) Launcher_PreScaleVertPrev(void)
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

static void __attribute__((unused)) Launcher_PreScaleVertNext(void)
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
	Launcher_SetThumbCachePointers();
	memset(pReadCache + 0x10000, 0, 0xE400);
	Launcher_ResetThumbCache();
}

static void Launcher_BuildThumbCache(u32 center_index)
{
	u32 old_center_index = launcher_cache_center_index;

	if(center_index >= Launcher_GetTotalEntries())
	{
		Launcher_ResetThumbCache();
		return;
	}

	Launcher_SetThumbCachePointers();
	Launcher_ActivateThumbnailWorkspace(Launcher_ActiveViewMode());
	if(Launcher_IsListArtMode())
	{
		(void)old_center_index;
		launcher_cache_center_index = center_index;
		launcher_cache_prev.valid = 0;
		launcher_cache_next.valid = 0;
		launcher_cache_selected.valid = 1;
		launcher_cache_selected.absolute_index = center_index;
		launcher_cache_selected.has_thumbnail = 0;
		Launcher_PreScaleListArtCache();
		return;
	}

	launcher_cache_center_index = center_index;
	Launcher_LoadThumbCacheForIndex(&launcher_cache_selected, center_index);

	launcher_cache_prev.valid = 0;
	launcher_cache_next.valid = 0;
	launcher_cache_prev.absolute_index = 0xFFFFFFFF;
	launcher_cache_next.absolute_index = 0xFFFFFFFF;
	launcher_cache_prev.has_thumbnail = 0;
	launcher_cache_next.has_thumbnail = 0;

	if(center_index > 0)
		Launcher_LoadThumbCacheForIndex(&launcher_cache_prev, center_index - 1);
	if((center_index + 1) < Launcher_GetTotalEntries())
		Launcher_LoadThumbCacheForIndex(&launcher_cache_next, center_index + 1);

	if(Launcher_ActiveViewMode() == 2)
		Launcher_PreScaleVertCache();
	else if(Launcher_ActiveViewMode() == 1)
		Launcher_PreScaleHorzCache();
	else if(Launcher_ActiveViewMode() == LAUNCHER_VIEW_LIST_ART)
		Launcher_PreScaleListArtCache();
}

static void Launcher_ShiftThumbCache(int move, u32 new_center_index)
{
	u32 total_entries = Launcher_GetTotalEntries();
	LauncherThumbCache old_prev = launcher_cache_prev;
	LauncherThumbCache old_selected = launcher_cache_selected;
	LauncherThumbCache old_next = launcher_cache_next;

	if(new_center_index >= total_entries)
	{
		Launcher_ResetThumbCache();
		return;
	}
	if(Launcher_IsListArtMode())
	{
		(void)move;
		Launcher_GetListArtCachedState(new_center_index, 0, 0, 0, 0, 1);
		return;
	}

	if(!Launcher_ThumbCachePointersValid() ||
	launcher_cache_center_index == 0xFFFFFFFF ||
	launcher_cache_center_index >= total_entries)
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

		if(Launcher_ActiveViewMode() == 2)
		{
			/* Rebuild all vertical scaled caches to avoid size-mismatched copies
			between the 48x32 side buffers and the 84x56 selected buffer. */
			Launcher_PreScaleVertCache();
		}
		else if(Launcher_ActiveViewMode() == 1)
		{
			Launcher_PreScaleHorzCache();
		}
		else if(Launcher_ActiveViewMode() == LAUNCHER_VIEW_LIST_ART)
		{
			Launcher_PreScaleListArtCache();
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

		if(Launcher_ActiveViewMode() == 2)
		{
			/* Rebuild all vertical scaled caches to avoid size-mismatched copies
			between the 48x32 side buffers and the 84x56 selected buffer. */
			Launcher_PreScaleVertCache();
		}
		else if(Launcher_ActiveViewMode() == 1)
		{
			Launcher_PreScaleHorzCache();
		}
		else if(Launcher_ActiveViewMode() == LAUNCHER_VIEW_LIST_ART)
		{
			Launcher_PreScaleListArtCache();
		}
		return;
	}

	Launcher_BuildThumbCache(new_center_index);
}

static u32 Launcher_ThumbNavRepeatDelay(void)
{
	u32 total;
	u32 incomplete_triplet;

	if(Launcher_ActiveViewMode() != 2)
	{
		if(Launcher_ActiveViewMode() != 1)
			return 1;

		total = Launcher_GetTotalEntries();
		incomplete_triplet =
			!launcher_cache_selected.valid || !launcher_cache_selected.has_thumbnail ||
			(launcher_cache_center_index == 0) || !launcher_cache_prev.valid || !launcher_cache_prev.has_thumbnail ||
			((launcher_cache_center_index + 1) >= total) || !launcher_cache_next.valid || !launcher_cache_next.has_thumbnail;
		if(recents_view_active && incomplete_triplet)
			return 3;
		return 2;
	}

	total = Launcher_GetTotalEntries();
	incomplete_triplet =
		!launcher_cache_selected.valid || !launcher_cache_selected.has_thumbnail ||
		(launcher_cache_center_index == 0) || !launcher_cache_prev.valid || !launcher_cache_prev.has_thumbnail ||
		((launcher_cache_center_index + 1) >= total) || !launcher_cache_next.valid || !launcher_cache_next.has_thumbnail;
	if(incomplete_triplet)
		return recents_view_active ? 3 : 2;

	return recents_view_active ? 2 : 1;
}

static u32 Launcher_ListNavRepeatDelay(void)
{
	return 1;
}

static void __attribute__((unused)) Draw_ModernLauncher_SD_State(u32 show_offset, u32 file_select, int x_shift)
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
	int left_y = Launcher_HorizontalSideY(LAUNCHER_HORZ_LEFT_Y);
	int right_y = Launcher_HorizontalSideY(LAUNCHER_HORZ_RIGHT_Y);
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

	launcher_carousel_art_draw = 1;
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

	launcher_carousel_art_draw = 0;
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
	int left_y = Launcher_HorizontalSideY(LAUNCHER_HORZ_LEFT_Y);
	int right_y = Launcher_HorizontalSideY(LAUNCHER_HORZ_RIGHT_Y);
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

	launcher_carousel_art_draw = 1;
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
	launcher_carousel_art_draw = 0;
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
	int prev_x = Launcher_VerticalSideX(LAUNCHER_VERT_PREV_X, prev_w);
	int next_x = Launcher_VerticalSideX(LAUNCHER_VERT_NEXT_X, next_w);
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

	launcher_carousel_art_draw = 1;
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
				if(!launcher_popup_restore_redraw)
					Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), prev_x - 1, prev_y - 1, prev_w + 2, prev_h + 2);
				Launcher_DrawPreparedCarouselSideArtwork(launcher_vert_prev_scaled + 8, 48,
				prev_x + 8, prev_y, 32, 32);
			}
			else
			{
				if(!launcher_popup_restore_redraw)
					Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), prev_x - 1, prev_y - 1, prev_w + 2, prev_h + 2);
				Launcher_DrawPreparedCarouselSideArtwork(launcher_vert_prev_scaled, 48,
				prev_x, prev_y, prev_w, prev_h);
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
				if(!launcher_popup_restore_redraw)
					Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), next_x - 1, next_y - 1, next_w + 2, next_h + 2);
				Launcher_DrawPreparedCarouselSideArtwork(launcher_vert_next_scaled + 8, 48,
				next_x + 8, next_y, 32, 32);
			}
			else
			{
				if(!launcher_popup_restore_redraw)
					Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), next_x - 1, next_y - 1, next_w + 2, next_h + 2);
				Launcher_DrawPreparedCarouselSideArtwork(launcher_vert_next_scaled, 48,
				next_x, next_y, next_w, next_h);
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
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, 1);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y + thumb_h, thumb_w + 2, 1);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, 1, thumb_h + 2);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x + thumb_w, thumb_y - 1, 1, thumb_h + 2);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x, thumb_y, 14, thumb_h);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x + 70, thumb_y, 14, thumb_h);
			Launcher_DrawPicClipStride(launcher_vert_selected_scaled + 14, 84, thumb_x + 14, thumb_y, 56, 56);
			Launcher_FinishCarouselArtwork(thumb_x + 14, thumb_y, 56, 56);
		}
		else
		{
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, thumb_w + 2, 1);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y + thumb_h, thumb_w + 2, 1);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x - 1, thumb_y - 1, 1, thumb_h + 2);
			Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), thumb_x + thumb_w, thumb_y - 1, 1, thumb_h + 2);
			Launcher_DrawPicClipStride(launcher_vert_selected_scaled, 84, thumb_x, thumb_y, thumb_w, thumb_h);
			Launcher_FinishCarouselArtwork(thumb_x, thumb_y, thumb_w, thumb_h);
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
	if(selected_use_preview_panel)
	{
		if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
			Launcher_DrawThumbBorder(thumb_x + 14, thumb_y, 56, 56);
		else
			Launcher_DrawThumbBorder(thumb_x, thumb_y, thumb_w, thumb_h);
	}

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
	launcher_carousel_art_draw = 0;
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
	Launcher_ViewModeCycle(1);
	Launcher_SaveUnifiedSettings();
	launcher_active_page = page_num;
	Launcher_UpdateEffectiveViewMode();
	if(page_num == SD_list)
	{
		Launcher_DrawThemeBGFull(Launcher_GetBGImage());
		launcher_vertical_folder_label_dirty = 1;
		launcher_system_name_dirty = 1;
		if(Launcher_ActiveViewMode())
			Launcher_BuildThumbCache(show_offset + file_select);
	}
	else if(page_num == NOR_list)
	{
		Launcher_DrawThemeBGFull(Launcher_GetBGImage());
		launcher_vertical_folder_label_dirty = 1;
		launcher_system_name_dirty = 1;
		if(Launcher_ActiveViewMode())
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

		memset(SAV_info_buffer,0x00,sizeof(SAV_info_buffer));
		SAV_info_buffer[0] = 0x11;
		SAV_info_buffer[1] = savefilesize>>9;
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

const char *Launcher_OnOffText(u16 value)
{
    return value ? DSTEXT_ON : DSTEXT_OFF;
}

static const char *Launcher_EngineText(void)
{
    return gl_engine_sel ? DSTEXT_ENGINE_MANUAL : DSTEXT_ENGINE_FAST;
}

static const char *Launcher_ThumbnailText(void)
{
    if(gl_show_Thumbnail == LAUNCHER_VIEW_HORIZONTAL)
        return DSTEXT_VIEW_HORIZONTAL;
    if(gl_show_Thumbnail == LAUNCHER_VIEW_VERTICAL)
        return DSTEXT_VIEW_VERTICAL;
    if(gl_show_Thumbnail == LAUNCHER_VIEW_LIST_ART)
        return DSTEXT_VIEW_LIST_ART;
    return DSTEXT_VIEW_LIST;
}

static const char *Launcher_ThumbnailSettingName(void)
{
    if(gl_show_Thumbnail == LAUNCHER_VIEW_HORIZONTAL)
        return "Horizontal";
    if(gl_show_Thumbnail == LAUNCHER_VIEW_VERTICAL)
        return "Vertical";
    if(gl_show_Thumbnail == LAUNCHER_VIEW_LIST_ART)
        return "List + art";
    return "List";
}

static const char *Launcher_ThumbnailStyleText(void)
{
    return (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? DSTEXT_THUMB_BOX : DSTEXT_THUMB_TITLE;
}

static const char *Launcher_ClockFormatText(void)
{
    return launcher_clock_24_hour ? DSTEXT_CLOCK_24H : DSTEXT_CLOCK_12H;
}

static const char *Launcher_ArtBorderText(void)
{
    switch(launcher_art_border_mode)
    {
        case 1: return DSTEXT_BORDER_ACCENT;
        case 2: return DSTEXT_BORDER_BLACK;
        case 3: return DSTEXT_BORDER_GREY;
        case 4: return DSTEXT_BORDER_WHITE;
        default: return DSTEXT_OFF;
    }
}

static const char *Launcher_ArtBorderSettingName(void)
{
    switch(launcher_art_border_mode)
    {
        case 1: return "Accent";
        case 2: return "Black";
        case 3: return "Grey";
        case 4: return "White";
        default: return "Off";
    }
}

static const char *Launcher_RoundedCornersText(void)
{
    switch(launcher_art_rounded_corners)
    {
        case 1: return DSTEXT_ROUNDED_FULL;
        case 2: return DSTEXT_ROUNDED_NO_START;
        default: return DSTEXT_OFF;
    }
}

static const char *Launcher_RoundedCornersSettingName(void)
{
    switch(launcher_art_rounded_corners)
    {
        case 1: return "Full";
        case 2: return "No Start";
        default: return "Off";
    }
}

static u32 Launcher_RoundedCornersForCarousel(void)
{
    return launcher_art_rounded_corners != 0;
}

static u32 Launcher_RoundedCornersForStart(void)
{
    return launcher_art_rounded_corners == 1;
}

static u16 Launcher_VerticalSideOptionCount(void)
{
#if LAUNCHER_VERT_SIDE_CUSTOM_ENABLED
    return 4;
#else
    return 3;
#endif
}

static u16 Launcher_HorizontalSideOptionCount(void)
{
#if LAUNCHER_HORZ_SIDE_CUSTOM_ENABLED
    return 4;
#else
    return 3;
#endif
}

static const char *Launcher_VerticalSideText(void)
{
    switch(launcher_vertical_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_LEFT: return DSTEXT_ALIGN_LEFT;
        case LAUNCHER_SIDE_ALIGN_RIGHT: return DSTEXT_ALIGN_RIGHT;
        case LAUNCHER_SIDE_ALIGN_CUSTOM: return DSTEXT_ALIGN_CUSTOM;
        default: return DSTEXT_ALIGN_CENTER;
    }
}

static const char *Launcher_VerticalSideSettingName(void)
{
    switch(launcher_vertical_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_LEFT: return "Left";
        case LAUNCHER_SIDE_ALIGN_RIGHT: return "Right";
        case LAUNCHER_SIDE_ALIGN_CUSTOM: return "Custom";
        default: return "Center";
    }
}

static const char *Launcher_HorizontalSideText(void)
{
    switch(launcher_horizontal_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_TOP: return DSTEXT_ALIGN_TOP;
        case LAUNCHER_SIDE_ALIGN_BOTTOM: return DSTEXT_ALIGN_BOTTOM;
        case LAUNCHER_SIDE_ALIGN_CUSTOM: return DSTEXT_ALIGN_CUSTOM;
        default: return DSTEXT_ALIGN_CENTER;
    }
}

static const char *Launcher_HorizontalSideSettingName(void)
{
    switch(launcher_horizontal_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_TOP: return "Top";
        case LAUNCHER_SIDE_ALIGN_BOTTOM: return "Bottom";
        case LAUNCHER_SIDE_ALIGN_CUSTOM: return "Custom";
        default: return "Center";
    }
}

static const char *Launcher_ListArtPositionText(void)
{
    switch(launcher_list_art_position)
    {
        case LAUNCHER_SIDE_ALIGN_TOP: return DSTEXT_ALIGN_TOP;
        case LAUNCHER_SIDE_ALIGN_BOTTOM: return DSTEXT_ALIGN_BOTTOM;
        default: return DSTEXT_ALIGN_CENTER;
    }
}

static const char *Launcher_ListArtPositionSettingName(void)
{
    switch(launcher_list_art_position)
    {
        case LAUNCHER_SIDE_ALIGN_TOP: return "Top";
        case LAUNCHER_SIDE_ALIGN_BOTTOM: return "Bottom";
        default: return "Center";
    }
}

static int Launcher_VerticalSideX(int custom_x, int side_w)
{
    int visible_w = side_w;
    int visible_offset = 0;
    int main_x = LAUNCHER_VERT_THUMB_X;
    int main_w = LAUNCHER_VERT_THUMB_W;

    if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
    {
        visible_w = 32;
        visible_offset = 8;
        main_x = LAUNCHER_VERT_THUMB_X + 14;
        main_w = 56;
    }

    switch(launcher_vertical_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_LEFT:
            return main_x - visible_offset;
        case LAUNCHER_SIDE_ALIGN_RIGHT:
            return main_x + main_w - visible_w - visible_offset;
        case LAUNCHER_SIDE_ALIGN_CUSTOM:
#if LAUNCHER_VERT_SIDE_CUSTOM_ENABLED
            return custom_x;
#else
            break;
#endif
        default:
            break;
    }

    return main_x + ((main_w - visible_w) / 2) - visible_offset;
}

static int Launcher_HorizontalSideY(int custom_y)
{
    int visible_h = LAUNCHER_HORZ_SIDE_H;
    int main_y = LAUNCHER_HORZ_THUMB_Y;
    int main_h = LAUNCHER_HORZ_THUMB_H;

    switch(launcher_horizontal_side_align)
    {
        case LAUNCHER_SIDE_ALIGN_TOP:
            return main_y;
        case LAUNCHER_SIDE_ALIGN_BOTTOM:
            return main_y + main_h - visible_h;
        case LAUNCHER_SIDE_ALIGN_CUSTOM:
#if LAUNCHER_HORZ_SIDE_CUSTOM_ENABLED
            return custom_y;
#else
            break;
#endif
        default:
            break;
    }

    return main_y + ((main_h - visible_h) / 2);
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

static void Launcher_ReadHideSystemFilesSetting(void)
{
    char buf[32];

    launcher_hide_system_files = 1;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Hide system files", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Off") || !strcasecmp(buf, "No") ||
           !strcasecmp(buf, DSTEXT_OFF) || (buf[0] == '0'))
            launcher_hide_system_files = 0;
        if(strcasecmp(buf, "On") && strcasecmp(buf, "Off"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadListFoldersSetting(void)
{
    char buf[32];

    launcher_list_folders = 0;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("List folders", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "On") || !strcasecmp(buf, "Yes") ||
           !strcasecmp(buf, DSTEXT_ON) || (buf[0] == '1'))
            launcher_list_folders = 1;
        if(strcasecmp(buf, "On") && strcasecmp(buf, "Off"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadCleanListSetting(void)
{
    char buf[32];

    launcher_clean_list = 0;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Clean list", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "On") || !strcasecmp(buf, "Yes") ||
           !strcasecmp(buf, DSTEXT_ON) || (buf[0] == '1'))
            launcher_clean_list = 1;
        if(strcasecmp(buf, "On") && strcasecmp(buf, "Off"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadClockFormatSetting(void)
{
    char buf[32];

    launcher_clock_24_hour = 1;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Clock format", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "12 hour") || !strcasecmp(buf, "12-hour") ||
           !strcasecmp(buf, "12H") || !strcasecmp(buf, DSTEXT_CLOCK_12H) || (buf[0] == '1'))
            launcher_clock_24_hour = 0;
        else
            launcher_clock_24_hour = 1;
        if(strcasecmp(buf, "12 hour") && strcasecmp(buf, "24 hour"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadViewModeSetting(void)
{
    char buf[32];

    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("View mode", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Horizontal") || !strcasecmp(buf, DSTEXT_VIEW_HORIZONTAL))
            gl_show_Thumbnail = LAUNCHER_VIEW_HORIZONTAL;
        else if(!strcasecmp(buf, "Vertical") || !strcasecmp(buf, DSTEXT_VIEW_VERTICAL))
            gl_show_Thumbnail = LAUNCHER_VIEW_VERTICAL;
        else if(!strcasecmp(buf, "List + art") || !strcasecmp(buf, "List art") ||
                !strcasecmp(buf, DSTEXT_VIEW_LIST_ART))
            gl_show_Thumbnail = LAUNCHER_VIEW_LIST_ART;
        else
            gl_show_Thumbnail = LAUNCHER_VIEW_LIST;

        if(strcasecmp(buf, "List") && strcasecmp(buf, "Horizontal") &&
           strcasecmp(buf, "Vertical") && strcasecmp(buf, "List + art"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadListArtPositionSetting(void)
{
    char buf[32];

    launcher_list_art_position = LAUNCHER_SIDE_ALIGN_BOTTOM;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("List art", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Top") || !strcasecmp(buf, DSTEXT_ALIGN_TOP))
            launcher_list_art_position = LAUNCHER_SIDE_ALIGN_TOP;
        else if(!strcasecmp(buf, "Center") || !strcasecmp(buf, "Centre") ||
                !strcasecmp(buf, DSTEXT_ALIGN_CENTER))
            launcher_list_art_position = LAUNCHER_SIDE_ALIGN_CENTER;
        else
            launcher_list_art_position = LAUNCHER_SIDE_ALIGN_BOTTOM;

        if(strcasecmp(buf, "Center") && strcasecmp(buf, "Centre") &&
           strcasecmp(buf, "Top") && strcasecmp(buf, "Bottom"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadArtBorderSetting(void)
{
    char buf[32];

    launcher_art_border_mode = 0;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Art border", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Accent") || !strcasecmp(buf, DSTEXT_BORDER_ACCENT))
            launcher_art_border_mode = 1;
        else if(!strcasecmp(buf, "Black") || !strcasecmp(buf, DSTEXT_BORDER_BLACK))
            launcher_art_border_mode = 2;
        else if(!strcasecmp(buf, "Grey") || !strcasecmp(buf, "Gray") || !strcasecmp(buf, DSTEXT_BORDER_GREY))
            launcher_art_border_mode = 3;
        else if(!strcasecmp(buf, "White") || !strcasecmp(buf, DSTEXT_BORDER_WHITE))
            launcher_art_border_mode = 4;
        else
            launcher_art_border_mode = 0;
    }
}

static void Launcher_ReadRoundedCornersSetting(void)
{
    char buf[32];

    launcher_art_rounded_corners = 0;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Rounded corners", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Full") || !strcasecmp(buf, "On") || !strcasecmp(buf, "Yes") ||
           !strcasecmp(buf, DSTEXT_ROUNDED_FULL) || !strcasecmp(buf, DSTEXT_ON) || (buf[0] == '1'))
            launcher_art_rounded_corners = 1;
        else if(!strcasecmp(buf, "No Start") || !strcasecmp(buf, "No start screen") ||
                !strcasecmp(buf, DSTEXT_ROUNDED_NO_START) || (buf[0] == '2'))
            launcher_art_rounded_corners = 2;
        else
            launcher_art_rounded_corners = 0;

        if(strcasecmp(buf, "Full") && strcasecmp(buf, "No Start") && strcasecmp(buf, "Off"))
            launcher_settings_migration_pending = 1;
    }
}

static void Launcher_ReadVerticalSideSetting(void)
{
    char buf[32];

    launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_CENTER;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Vertical side", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Left") || !strcasecmp(buf, DSTEXT_ALIGN_LEFT))
            launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_LEFT;
        else if(!strcasecmp(buf, "Right") || !strcasecmp(buf, DSTEXT_ALIGN_RIGHT))
            launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_RIGHT;
        else if(!strcasecmp(buf, "Custom") || !strcasecmp(buf, DSTEXT_ALIGN_CUSTOM))
            launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_CUSTOM;
        else
            launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_CENTER;

        if(strcasecmp(buf, "Center") && strcasecmp(buf, "Left") && strcasecmp(buf, "Right") && strcasecmp(buf, "Custom"))
            launcher_settings_migration_pending = 1;
    }

    if(launcher_vertical_side_align >= Launcher_VerticalSideOptionCount())
        launcher_vertical_side_align = LAUNCHER_SIDE_ALIGN_CENTER;
}

static void Launcher_ReadHorizontalSideSetting(void)
{
    char buf[32];

    launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_CENTER;
    memset(buf, 0, sizeof(buf));
    if(Launcher_SettingsReadValue("Horizontal side", buf, sizeof(buf)))
    {
        if(!strcasecmp(buf, "Top") || !strcasecmp(buf, DSTEXT_ALIGN_TOP))
            launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_TOP;
        else if(!strcasecmp(buf, "Bottom") || !strcasecmp(buf, DSTEXT_ALIGN_BOTTOM))
            launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_BOTTOM;
        else if(!strcasecmp(buf, "Custom") || !strcasecmp(buf, DSTEXT_ALIGN_CUSTOM))
            launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_CUSTOM;
        else
            launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_CENTER;

        if(strcasecmp(buf, "Center") && strcasecmp(buf, "Top") && strcasecmp(buf, "Bottom") && strcasecmp(buf, "Custom"))
            launcher_settings_migration_pending = 1;
    }

    if(launcher_horizontal_side_align >= Launcher_HorizontalSideOptionCount())
        launcher_horizontal_side_align = LAUNCHER_SIDE_ALIGN_CENTER;
}

static u32 Launcher_ShouldHideSystemEntry(const TCHAR *path, const TCHAR *name, u8 attrib)
{
    u32 is_dir = ((attrib == AM_DIR) || (attrib == 0x30));

    if(!launcher_hide_system_files || !path || !name || strcmp(path, "/"))
        return 0;

    if(name[0] == '.')
        return 1;

    if(is_dir)
    {
        if(!strcasecmp(name, "SYSTEM") || !strcasecmp(name, "SAVER") ||
           !strcasecmp(name, "RTS") || !strcasecmp(name, "CHEAT") ||
           !strcasecmp(name, "PATCH") || !strcasecmp(name, "BACKUP") ||
           !strcasecmp(name, "IMGS") || !strcasecmp(name, "IMGS2") ||
           !strcasecmp(name, "THEMES") || !strcasecmp(name, "KERNELS") ||
           !strcasecmp(name, "System Volume Information") ||
           !strcasecmp(name, "$RECYCLE.BIN") || !strncasecmp(name, ".Trash", 6) ||
           !strcasecmp(name, ".Trashes") || !strcasecmp(name, ".Spotlight-V100"))
            return 1;
    }
    else
    {
        if(!strcasecmp(name, "ezkernel.bin") || !strcasecmp(name, "ezkernelnew.bin") ||
           !strcasecmp(name, "ezkernel.tmp") || !strcasecmp(name, "ezkernelnew.tmp") ||
           !strcasecmp(name, ".DS_Store") || !strcasecmp(name, "desktop.ini") ||
           !strcasecmp(name, "Thumbs.db") || !strcasecmp(name, "IndexerVolumeGuid") ||
           !strcasecmp(name, "WPSettings.dat") || !strncasecmp(name, "._", 2))
            return 1;
    }

    return 0;
}

static void Launcher_ReadLanguageSetting(void)
{
    char buf[32];
    u32 i;
    u16 old_lang = gl_select_lang;

    memset(buf, 0, sizeof(buf));
    if(!Launcher_SettingsReadValue("Language", buf, sizeof(buf)))
        return;

    for(i = 0; i < LAUNCHER_LANGUAGE_COUNT; i++)
    {
        if(!strcasecmp(buf, launcher_language_packs[i].name))
        {
            Launcher_ApplyLanguageIndex(i);
            if(gl_select_lang != old_lang)
                Launcher_SaveSettingsInfo();
            return;
        }
    }
    if(!strcasecmp(buf, "English UK") || !strcasecmp(buf, "English (UK)"))
    {
        Launcher_ApplyLanguageIndex(0);
        if(gl_select_lang != old_lang)
            Launcher_SaveSettingsInfo();
    }
    else if(!strcasecmp(buf, "Chinese"))
    {
        Launcher_ApplyLanguageIndex(Launcher_LanguageIndexFromStored(0xE2E2));
        if(gl_select_lang != old_lang)
            Launcher_SaveSettingsInfo();
    }
    else
    {
        Launcher_ApplyLanguageIndex(0);
        launcher_settings_migration_pending = 1;
        if(gl_select_lang != old_lang)
            Launcher_SaveSettingsInfo();
    }
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
    SETTINGS_CLOCK_FORMAT,
    SETTINGS_VIEW_MODE,
    SETTINGS_LIST_ART_POSITION,
    SETTINGS_THUMBNAILS,
    SETTINGS_ART_BORDER,
    SETTINGS_ROUNDED_CORNERS,
    SETTINGS_VERTICAL_SIDE,
    SETTINGS_HORIZONTAL_SIDE,
    SETTINGS_SOUNDS,
    SETTINGS_LANGUAGE,
    SETTINGS_THEME_MODE,
    SETTINGS_THEME,
    SETTINGS_LOAD_STYLE,
    SETTINGS_HIDE_SYSTEM,
    SETTINGS_LIST_FOLDERS,
    SETTINGS_CLEAN_LIST,
    SETTINGS_BOOT_ENGINE,
    SETTINGS_AUTO_SAVE,
    SETTINGS_RESUME_LAST,
    SETTINGS_START_ENABLED,
    SETTINGS_BOOT_TO,
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
    char label_short[96];
    u32 used;
    u32 spaces;

    switch(item)
    {
        case SETTINGS_TIME_SETTINGS: label = DSTEXT_SETTINGS_TIME; value = ">"; break;
        case SETTINGS_CLOCK_FORMAT: label = DSTEXT_SETTINGS_CLOCK_FORMAT; value = Launcher_ClockFormatText(); break;
        case SETTINGS_VIEW_MODE: label = DSTEXT_SETTINGS_VIEW_MODE; value = Launcher_ThumbnailText(); break;
        case SETTINGS_LIST_ART_POSITION: label = DSTEXT_SETTINGS_LIST_ART; value = Launcher_ListArtPositionText(); break;
        case SETTINGS_THUMBNAILS: label = DSTEXT_SETTINGS_THUMBNAILS; value = Launcher_ThumbnailStyleText(); break;
        case SETTINGS_ART_BORDER: label = DSTEXT_SETTINGS_ART_BORDER; value = Launcher_ArtBorderText(); break;
        case SETTINGS_ROUNDED_CORNERS: label = DSTEXT_SETTINGS_ROUNDED_CORNERS; value = Launcher_RoundedCornersText(); break;
        case SETTINGS_VERTICAL_SIDE: label = DSTEXT_SETTINGS_VERTICAL_SIDE; value = Launcher_VerticalSideText(); break;
        case SETTINGS_HORIZONTAL_SIDE: label = DSTEXT_SETTINGS_HORIZONTAL_SIDE; value = Launcher_HorizontalSideText(); break;
        case SETTINGS_SOUNDS: label = DSTEXT_SETTINGS_SOUNDS; value = Launcher_OnOffText(launcher_sounds_enabled); break;
        case SETTINGS_LANGUAGE: label = DSTEXT_SETTINGS_LANGUAGE; value = Launcher_LanguageName(); break;
        case SETTINGS_THEME_MODE: label = DSTEXT_SETTINGS_THEME; value = Launcher_ThemeModeName(); break;
        case SETTINGS_THEME: label = DSTEXT_SETTINGS_COLOUR; value = Launcher_ThemeName(); break;
        case SETTINGS_LOAD_STYLE: label = DSTEXT_SETTINGS_LOAD_STYLE; value = ">"; break;
        case SETTINGS_HIDE_SYSTEM: label = DSTEXT_SETTINGS_HIDE_SYSTEM; value = Launcher_OnOffText(launcher_hide_system_files); break;
        case SETTINGS_LIST_FOLDERS: label = DSTEXT_SETTINGS_LIST_FOLDERS; value = Launcher_OnOffText(launcher_list_folders); break;
        case SETTINGS_CLEAN_LIST: label = DSTEXT_SETTINGS_CLEAN_LIST; value = Launcher_OnOffText(launcher_clean_list); break;
        case SETTINGS_BOOT_ENGINE: label = DSTEXT_SETTINGS_BOOT_ENGINE; value = Launcher_EngineText(); break;
        case SETTINGS_AUTO_SAVE: label = DSTEXT_SETTINGS_AUTO_SAVE; value = Launcher_OnOffText(gl_auto_save_sel); break;
        case SETTINGS_START_ENABLED: label = DSTEXT_SETTINGS_START_SCREEN; value = Launcher_StartEnabledText(); break;
        case SETTINGS_BOOT_TO: label = DSTEXT_SETTINGS_BOOT_TO; value = Launcher_BootToText(); break;
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
    char msg[128];
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
    if(gl_select_lang == THAI_CP_FIRST)
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, 17, y - 1, 208, 14);
    else
        Launcher_ClearWithThemeBG((const u16*)gImage_SET, 17, y, 208, 13);
    get_line(item, msg, sizeof(msg));
    if(gl_select_lang == THAI_CP_FIRST)
    {
        char label_part[128];
        const char *value_part = msg;
        u16 value_offset;

        memset(label_part, 0, sizeof(label_part));
        value_offset = DrawText12ByteOffsetForGlyphs(msg, 16);
        if(value_offset >= sizeof(label_part))
            value_offset = sizeof(label_part) - 1;
        memcpy(label_part, msg, value_offset);
        label_part[value_offset] = 0;
        if(strlen(msg) > value_offset)
            value_part = msg + value_offset;

        if(item == selected)
            Clear(112, y - 1, 112, 14, gl_color_selectBG_sd, 1);
        DrawHZText12(label_part, 32, 23, y, gl_color_text, 1);
        DrawHZText12((TCHAR*)value_part, 32, 119, y, (item == selected) ? LAUNCHER_SELECTED_TEXT : gl_color_text, 1);
        return;
    }
    if(item == selected)
    {
        char label_part[128];
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
    char msg[128];
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
    if(gl_select_lang == THAI_CP_FIRST)
    {
        Launcher_SettingsDrawRow(item, selected, top, get_line);
        return;
    }

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

static void __attribute__((unused)) Launcher_SettingsDrawPopup(const char *title, u32 total, u32 selected, u32 top, void (*get_line)(u32,char*,u32))
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

static void Launcher_SettingsMovePopupSelection(const char *title, u32 total, u32 old_selected, u32 old_top, u32 selected, u32 top, void (*get_line)(u32,char*,u32), u32 row_y0)
{
    if(old_top != top)
    {
        Launcher_SettingsDrawPopupEx(title, total, selected, top, get_line, row_y0);
        return;
    }

    if(old_selected != selected)
    {
        Launcher_SettingsDrawPopupRowEx(old_selected, selected, top, get_line, row_y0);
        Launcher_SettingsDrawPopupRowEx(selected, selected, top, get_line, row_y0);
    }
}

static u32 Launcher_LoadStyleList(void)
{
    FRESULT res;
    u32 count = 0;

    f_mkdir("/SYSTEM");
    f_mkdir("/SYSTEM/KERNELS");

    res = f_opendir(&dir, "/SYSTEM/KERNELS");
    if(res != FR_OK)
        return 0;

    while(count < MAX_files)
    {
        res = f_readdir(&dir, &fileinfo);
        if(res != FR_OK || fileinfo.fname[0] == 0)
            break;
        if((fileinfo.fattrib == AM_DIR) || (fileinfo.fattrib == 0x30))
            continue;
        if(!Is_bin_file(fileinfo.fname))
            continue;
        memcpy(pFilename_buffer[count].filename, fileinfo.fname, 100);
        pFilename_buffer[count].filename[99] = 0;
        count++;
    }
    f_closedir(&dir);
    return count;
}

static void Launcher_StyleDisplayName(const char *filename, char *out, u32 out_size)
{
    char name[100];
    char *dot;
    u32 len;
    const u32 max_chars = 23;

    if(out_size == 0)
        return;
    out[0] = 0;
    if(!filename)
        return;

    memset(name, 0, sizeof(name));
    strncpy(name, filename, sizeof(name) - 1);
    dot = strrchr(name, '.');
    if(dot && !strcasecmp(dot, ".bin"))
        *dot = 0;

    len = strlen(name);
    if(len <= max_chars || out_size <= max_chars)
    {
        strncpy(out, name, out_size - 1);
        out[out_size - 1] = 0;
        return;
    }

    strncpy(out, name, max_chars - 3);
    out[max_chars - 3] = 0;
    strncat(out, "...", out_size - strlen(out) - 1);
}

static void Launcher_StyleGetLine(u32 item, char *out, u32 out_size)
{
    if(out_size == 0)
        return;
    out[0] = 0;
    if(item >= MAX_files)
        return;

    Launcher_StyleDisplayName(pFilename_buffer[item].filename, out, out_size);
}

static void Launcher_WaitForPopupButton(void)
{
    u16 keys;

    do
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
    } while(keysHeld() & (KEY_A | KEY_B));

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        keys = keysDown();
        if(keys & (KEY_A | KEY_B))
            break;
    }
}

static void Launcher_ShowStyleMessage(const char *line1, const char *line2, const char *line3)
{
    DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);
    DrawHZText12((TCHAR*)line1, 0, 47, 45, gl_color_text, 1);
    if(line2)
        DrawHZText12((TCHAR*)line2, 0, 47, 63, gl_color_text, 1);
    if(line3)
        DrawHZText12((TCHAR*)line3, 0, 47, 81, gl_color_text, 1);
}

static u32 Launcher_PrepareStyleKernel(const char *name)
{
    char style_path[MAX_path_len + 100];
    char display_name[32];
    u16 keysdown;

    Launcher_StyleDisplayName(name, display_name, sizeof(display_name));
    Launcher_ShowStyleMessage(DSTEXT_PREPARE_THEME_QUESTION, display_name, DSTEXT_A_OK_B_CANCEL);

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        keysdown = keysDown();

        if(keysdown & KEY_A)
        {
            UIAudio_PlayAccept();
            snprintf(style_path, sizeof(style_path), "/SYSTEM/KERNELS/%s", name);
            Launcher_ShowStyleMessage(DSTEXT_PREPARING_THEME, DSTEXT_PLEASE_WAIT, NULL);
            if(Stage_kernel_update(style_path))
                Launcher_ShowStyleMessage(DSTEXT_THEME_READY, DSTEXT_REBOOT_HOLD_R, DSTEXT_TO_INSTALL_IT);
            else
                Launcher_ShowStyleMessage(DSTEXT_PREPARATION_FAILED, DSTEXT_A_OK_B_CANCEL, NULL);
            Launcher_WaitForPopupButton();
            launcher_force_full_redraw = 1;
            return 1;
        }
        else if(keysdown & KEY_B)
        {
            UIAudio_PlayBack();
            return 0;
        }
    }
}

static int launcher_settings_tab_return = -1;

static void Launcher_RequestSettingsTab(int tab_return)
{
    launcher_settings_tab_return = tab_return;
    UIAudio_PlaySfx(UI_SFX_TAB);
}

static void Launcher_LoadStylePopup(void)
{
    u32 total = Launcher_LoadStyleList();
    u32 selected = 0;
    u32 top = 0;
    const u32 visible = 7;
    u16 keysdown;

    if(total == 0)
    {
        Launcher_ShowStyleMessage(DSTEXT_PLACE_STYLES_IN, "/SYSTEM/KERNELS", DSTEXT_THEN_LOAD_STYLE);
        Launcher_WaitForPopupButton();
        launcher_force_full_redraw = 1;
        return;
    }

    Launcher_SettingsDrawPopupEx(DSTEXT_SETTINGS_LOAD_STYLE, total, selected, top, Launcher_StyleGetLine, 45);

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        keysdown = keysDown();

        if(keysdown & KEY_DOWN)
        {
            if(selected + 1 < total)
            {
                u32 old_selected = selected;
                u32 old_top = top;
                selected++;
                if(selected >= top + visible)
                    top = selected - visible + 1;
                UIAudio_PlaySfx(UI_SFX_MOVE);
                Launcher_SettingsMovePopupSelection(DSTEXT_SETTINGS_LOAD_STYLE, total, old_selected, old_top, selected, top, Launcher_StyleGetLine, 45);
            }
        }
        else if(keysdown & KEY_UP)
        {
            if(selected > 0)
            {
                u32 old_selected = selected;
                u32 old_top = top;
                selected--;
                if(selected < top)
                    top = selected;
                UIAudio_PlaySfx(UI_SFX_MOVE);
                Launcher_SettingsMovePopupSelection(DSTEXT_SETTINGS_LOAD_STYLE, total, old_selected, old_top, selected, top, Launcher_StyleGetLine, 45);
            }
        }
        else if(keysdown & KEY_A)
        {
            UIAudio_PlayAccept();
            if(Launcher_PrepareStyleKernel(pFilename_buffer[selected].filename))
                return;
            Launcher_SettingsDrawPopupEx(DSTEXT_SETTINGS_LOAD_STYLE, total, selected, top, Launcher_StyleGetLine, 45);
        }
        else if(keysdown & KEY_B)
        {
            UIAudio_PlayBack();
            launcher_force_full_redraw = 1;
            return;
        }
        else if(keysdown & KEY_L)
        {
            launcher_force_full_redraw = 1;
            Launcher_RequestSettingsTab(2);
            return;
        }
        else if(keysdown & KEY_R)
        {
            launcher_force_full_redraw = 1;
            Launcher_RequestSettingsTab(0);
            return;
        }
    }
}

static void Launcher_ViewModeCycle(int dir)
{
	static const u16 order[] = {
		LAUNCHER_VIEW_LIST,
		LAUNCHER_VIEW_LIST_ART,
		LAUNCHER_VIEW_HORIZONTAL,
		LAUNCHER_VIEW_VERTICAL
	};
	u32 i;
	u32 current = 0;

	for(i = 0; i < (sizeof(order) / sizeof(order[0])); i++)
	{
		if(gl_show_Thumbnail == order[i])
		{
			current = i;
			break;
		}
	}

	if(dir < 0)
		current = (current == 0) ? ((sizeof(order) / sizeof(order[0])) - 1) : (current - 1);
	else
		current = (current + 1) % (sizeof(order) / sizeof(order[0]));
	gl_show_Thumbnail = order[current];
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
            if(keysdown & KEY_L)
            {
                Launcher_RequestSettingsTab(2);
                return;
            }
            if(keysdown & KEY_R)
            {
                Launcher_RequestSettingsTab(0);
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
        f_printf(&f, "\n# Load style reads .bin files from /SYSTEM/KERNELS.\n");
        f_printf(&f, "# Select one from the settings menu to prepare it for installation.\n");
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "# Hide system files keeps kernel and metadata files out of the root file browser.\n");
        f_printf(&f, "Hide system files = %s\n", launcher_hide_system_files ? "On" : "Off");
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "# List folders keeps non-game folders in list view.\n");
        f_printf(&f, "List folders = %s\n", launcher_list_folders ? "On" : "Off");
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "# Clean list removes extensions/brackets and hides right-side file details in list view.\n");
        f_printf(&f, "Clean list = %s\n", launcher_clean_list ? "On" : "Off");
        f_printf(&f, "\n# Options: 24 hour, 12 hour\n");
        f_printf(&f, "# Clock format controls the top bar clock display.\n");
        f_printf(&f, "Clock format = %s\n", launcher_clock_24_hour ? "24 hour" : "12 hour");
        f_printf(&f, "\n# Options: On, Off\n");
        f_printf(&f, "Start screen = %s\n", Launcher_StartEnabledSettingName());
        f_printf(&f, "\n# Options: Start, SD, NOR, Last game, Recents, Favourites\n");
        f_printf(&f, "# Boot to chooses the first screen shown after startup.\n");
        f_printf(&f, "Boot to = %s\n", Launcher_BootToSettingName());
        f_printf(&f, "\n# Options: Last played, Favourites\n");
        f_printf(&f, "Start screen source = %s\n", Launcher_StartSourceSettingName());
        f_printf(&f, "\n# Options: List, List + art, Horizontal, Vertical\n");
        f_printf(&f, "# View mode chooses how files are displayed.\n");
        f_printf(&f, "View mode = %s\n", Launcher_ThumbnailSettingName());
        f_printf(&f, "\n# Options: Top, Center, Bottom\n");
        f_printf(&f, "# List art chooses where artwork appears in List + art view.\n");
        f_printf(&f, "List art = %s\n", Launcher_ListArtPositionSettingName());
        f_printf(&f, "\n# Options: Title, Box\n");
        f_printf(&f, "# Title uses /SYSTEM/IMGS thumbnails. Box uses /SYSTEM/IMGS2 thumbnails.\n");
        f_printf(&f, "Thumbnails = %s\n",
                 (launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX) ? "Box" : "Title");
        f_printf(&f, "\n# Options: Off, Accent, Black, Grey, White\n");
        f_printf(&f, "# Art border draws a one-pixel outline around the selected carousel artwork.\n");
        f_printf(&f, "Art border = %s\n", Launcher_ArtBorderSettingName());
        f_printf(&f, "\n# Options: Full, No Start, Off\n");
        f_printf(&f, "# Rounded corners rounds artwork. No Start keeps the Start screen artwork square.\n");
        f_printf(&f, "Rounded corners = %s\n", Launcher_RoundedCornersSettingName());
#if LAUNCHER_VERT_SIDE_CUSTOM_ENABLED
        f_printf(&f, "\n# Options: Center, Left, Right, Custom\n");
#else
        f_printf(&f, "\n# Options: Center, Left, Right\n");
#endif
        f_printf(&f, "# Vertical side controls the small previews in vertical carousel view.\n");
        f_printf(&f, "Vertical side = %s\n", Launcher_VerticalSideSettingName());
#if LAUNCHER_HORZ_SIDE_CUSTOM_ENABLED
        f_printf(&f, "\n# Options: Center, Top, Bottom, Custom\n");
#else
        f_printf(&f, "\n# Options: Center, Top, Bottom\n");
#endif
        f_printf(&f, "# Horizontal side controls the small previews in horizontal carousel view.\n");
        f_printf(&f, "Horizontal side = %s\n", Launcher_HorizontalSideSettingName());
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
            if(keysdown & KEY_L)
            {
                if(sleep_hotkey)
                    memcpy(sleep_keys, launcher_key_popup_keys, 3);
                else
                    memcpy(addon_keys, launcher_key_popup_keys, 3);
                Launcher_SaveHotkeys(sleep_keys, addon_keys);
                Launcher_RequestSettingsTab(2);
                return;
            }
            if(keysdown & KEY_R)
            {
                if(sleep_hotkey)
                    memcpy(sleep_keys, launcher_key_popup_keys, 3);
                else
                    memcpy(addon_keys, launcher_key_popup_keys, 3);
                Launcher_SaveHotkeys(sleep_keys, addon_keys);
                Launcher_RequestSettingsTab(0);
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
        default: if(out_size) out[0] = '\0'; break;
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
            if(keysdown & KEY_L)
            {
                rtc_enable();
                rtc_set(launcher_time_dt);
                rtc_disenable();
                delay(0x200);
                Launcher_RequestSettingsTab(2);
                return;
            }
            if(keysdown & KEY_R)
            {
                rtc_enable();
                rtc_set(launcher_time_dt);
                rtc_disenable();
                delay(0x200);
                Launcher_RequestSettingsTab(0);
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
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_LIST_ART_POSITION:
            if(dir < 0)
                launcher_list_art_position = (launcher_list_art_position == LAUNCHER_SIDE_ALIGN_CENTER) ? LAUNCHER_SIDE_ALIGN_BOTTOM : (launcher_list_art_position - 1);
            else
            {
                launcher_list_art_position++;
                if(launcher_list_art_position > LAUNCHER_SIDE_ALIGN_BOTTOM)
                    launcher_list_art_position = LAUNCHER_SIDE_ALIGN_CENTER;
            }
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_THUMBNAILS:
            Launcher_CycleThumbnailStyle(dir);
            break;
        case SETTINGS_ART_BORDER:
            if(dir < 0)
                launcher_art_border_mode = (launcher_art_border_mode == 0) ? 4 : (launcher_art_border_mode - 1);
            else
                launcher_art_border_mode = (launcher_art_border_mode + 1) % 5;
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_ROUNDED_CORNERS:
            if(dir < 0)
                launcher_art_rounded_corners = (launcher_art_rounded_corners == 0) ? 2 : (launcher_art_rounded_corners - 1);
            else
                launcher_art_rounded_corners = (launcher_art_rounded_corners + 1) % 3;
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_VERTICAL_SIDE:
            if(dir < 0)
                launcher_vertical_side_align = (launcher_vertical_side_align == 0) ? (Launcher_VerticalSideOptionCount() - 1) : (launcher_vertical_side_align - 1);
            else
                launcher_vertical_side_align = (launcher_vertical_side_align + 1) % Launcher_VerticalSideOptionCount();
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_HORIZONTAL_SIDE:
            if(dir < 0)
                launcher_horizontal_side_align = (launcher_horizontal_side_align == 0) ? (Launcher_HorizontalSideOptionCount() - 1) : (launcher_horizontal_side_align - 1);
            else
                launcher_horizontal_side_align = (launcher_horizontal_side_align + 1) % Launcher_HorizontalSideOptionCount();
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_CLOCK_FORMAT:
            launcher_clock_24_hour ^= 1;
            gl_clock_dirty = 1;
            Launcher_SaveUnifiedSettings();
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
        case SETTINGS_LOAD_STYLE:
            Launcher_LoadStylePopup();
            break;
        case SETTINGS_HIDE_SYSTEM:
            launcher_hide_system_files ^= 1;
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_LIST_FOLDERS:
            launcher_list_folders ^= 1;
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_CLEAN_LIST:
            launcher_clean_list ^= 1;
            Launcher_SaveUnifiedSettings();
            break;
        case SETTINGS_BOOT_ENGINE:
            gl_engine_sel ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_AUTO_SAVE:
            gl_auto_save_sel ^= 1;
            Launcher_SaveSettingsInfo();
            break;
        case SETTINGS_START_ENABLED:
            Launcher_CycleStartEnabled();
            break;
        case SETTINGS_BOOT_TO:
            Launcher_CycleBootTo(dir);
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

static u32 Launcher_SettingsItemNeedsFullRedraw(u32 item)
{
    switch(item)
    {
        case SETTINGS_TIME_SETTINGS:
        case SETTINGS_LANGUAGE:
        case SETTINGS_THEME_MODE:
        case SETTINGS_THEME:
        case SETTINGS_LOAD_STYLE:
        case SETTINGS_ADDON_SETTINGS:
        case SETTINGS_SLEEP_HOTKEY:
        case SETTINGS_ADDON_HOTKEY:
        case SETTINGS_LED_SETTINGS:
            return 1;
        default:
            return 0;
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
    char msgtime[12];
    const int x = 240 - 3 - (8 * 6);
    const int y = 3;

    rtc_enable();
    rtc_gettime(datetime);
    rtc_disenable();
    delay(5);

    HH = UNBCD(datetime[0]&0x3F);
    MM = UNBCD(datetime[1]&0x7F);
    SS = UNBCD(datetime[2]&0x7F);
    if(HH > 23) HH = 0;
    if(MM > 59) MM = 0;
    if(SS > 59) SS = 0;

    if(force || HH != last_hh || MM != last_mm || SS != last_ss)
    {
        Launcher_FormatClock(msgtime, sizeof(msgtime), HH, MM, SS);
        Launcher_ClearWithThemeBG(Launcher_GetTopbarBG(HELP), x, y, 8 * 6, 13);
        DrawHZText12(msgtime, 0, x, y, gl_color_topbar_text, 1);
        last_hh = HH;
        last_mm = MM;
        last_ss = SS;
    }
}

static void Launcher_DrawHelpPageNumber(u32 page, u32 total)
{
    char msg[12];
    snprintf(msg, sizeof(msg), "%lu/%lu", page + 1, total);
    Launcher_ClearWithThemeBG(Launcher_GetTopbarBG(HELP), 184, 2, 54, 15);
    DrawHZText12(msg, 0, 218 - DrawText12VisibleLength(msg) * 6, 3, gl_color_topbar_text, 1);
}

static int Launcher_HelpLineShouldCenter(const char *line)
{
    if(!line || !line[0])
        return 0;
    if(strncmp(line, "DS Style ", 9) == 0)
        return 1;
    return (strcmp(line, "Interface") == 0) ||
           (strcmp(line, "Artwork display") == 0) ||
           (strcmp(line, "Start screen") == 0) ||
           (strcmp(line, "Games") == 0) ||
           (strcmp(line, "In-game options") == 0) ||
           (strcmp(line, "Hardware") == 0) ||
           (strcmp(line, "Artwork folders") == 0) ||
           (strcmp(line, "Custom artwork") == 0) ||
           (strcmp(line, "Credits") == 0);
}

static void Launcher_DrawHelpTextPage(const char *title, const char *const *lines, u32 line_count, u32 page)
{
    const u32 lines_per_page = 9;
    u32 start = page * lines_per_page;
    u32 i;
    u32 y = 24;

    Launcher_DrawThemeBGFull((const u16*)gImage_SD_LIST);
    Launcher_ClearListBodyBackground();
    Launcher_DrawTopbarName(HELP);
    Launcher_DrawTopbarTitle(HELP, title);
    Launcher_DrawHelpPageNumber(page, (line_count + lines_per_page - 1) / lines_per_page);

    for(i = 0; i < lines_per_page && (start + i) < line_count; i++)
    {
        const char *line = lines[start + i];
        int x = 14;
        if(Launcher_HelpLineShouldCenter(line))
        {
            int w = DrawText12VisibleLength((char*)line) * 6;
            x = (240 - w) / 2;
            if(x < 0)
                x = 0;
        }
        DrawHZText12((TCHAR*)line, 0, x, y, gl_color_text, 1);
        y += 14;
    }
}

#define LAUNCHER_SNAKE_COLS 20
#define LAUNCHER_SNAKE_ROWS 14
#define LAUNCHER_SNAKE_CELL 8
#define LAUNCHER_SNAKE_X 40
#define LAUNCHER_SNAKE_Y 33
#define LAUNCHER_SNAKE_MAX_LENGTH 120

typedef struct
{
    u8 x[LAUNCHER_SNAKE_MAX_LENGTH];
    u8 y[LAUNCHER_SNAKE_MAX_LENGTH];
    u32 length;
    u32 food_x;
    u32 food_y;
    u32 rng;
} LauncherSnakeState;

static LauncherSnakeState *Launcher_GetSnakeState(void)
{
    return (LauncherSnakeState*)(void*)launcher_thumbnail_workspace.list_art[0];
}

#define launcher_snake_x (Launcher_GetSnakeState()->x)
#define launcher_snake_y (Launcher_GetSnakeState()->y)
#define launcher_snake_length (Launcher_GetSnakeState()->length)
#define launcher_snake_food_x (Launcher_GetSnakeState()->food_x)
#define launcher_snake_food_y (Launcher_GetSnakeState()->food_y)
#define launcher_snake_rng (Launcher_GetSnakeState()->rng)

static u32 Launcher_SnakeOccupies(u32 x, u32 y)
{
    u32 i;
    for(i = 0; i < launcher_snake_length; i++)
    {
        if(launcher_snake_x[i] == x && launcher_snake_y[i] == y)
            return 1;
    }
    return 0;
}

static void Launcher_SnakePlaceFood(void)
{
    u32 attempts;
    for(attempts = 0; attempts < 512; attempts++)
    {
        launcher_snake_rng = launcher_snake_rng * 1664525u + 1013904223u;
        launcher_snake_food_x = (launcher_snake_rng >> 16) % LAUNCHER_SNAKE_COLS;
        launcher_snake_food_y = (launcher_snake_rng >> 24) % LAUNCHER_SNAKE_ROWS;
        if(!Launcher_SnakeOccupies(launcher_snake_food_x, launcher_snake_food_y))
            return;
    }
    launcher_snake_food_x = 0;
    launcher_snake_food_y = 0;
}

static void Launcher_SnakeReset(void)
{
    u32 i;
    launcher_snake_length = 4;
    for(i = 0; i < launcher_snake_length; i++)
    {
        launcher_snake_x[i] = (LAUNCHER_SNAKE_COLS / 2) - i;
        launcher_snake_y[i] = LAUNCHER_SNAKE_ROWS / 2;
    }
    launcher_snake_rng = 0x13579BDFu ^ ((u32)REG_VCOUNT << 16) ^ REG_KEYINPUT;
    Launcher_SnakePlaceFood();
}

static u16 Launcher_SnakeBoardColour(void)
{
    return launcher_dark_mode ? RGB(2, 2, 2) : RGB(29, 29, 29);
}

static u16 Launcher_SnakeGridColour(void)
{
    return launcher_dark_mode ? RGB(7, 7, 7) : RGB(24, 24, 24);
}

static void Launcher_DrawSnakeFood(void)
{
    Clear(LAUNCHER_SNAKE_X + launcher_snake_food_x * LAUNCHER_SNAKE_CELL + 2,
          LAUNCHER_SNAKE_Y + launcher_snake_food_y * LAUNCHER_SNAKE_CELL + 2,
          5, 5, RGB(31, 0, 0), 1);
}

static void Launcher_DrawSnakeSegment(u32 x, u32 y)
{
    Clear(LAUNCHER_SNAKE_X + x * LAUNCHER_SNAKE_CELL + 1,
          LAUNCHER_SNAKE_Y + y * LAUNCHER_SNAKE_CELL + 1,
          7, 7, gl_color_selectBG_sd, 1);
}

static void Launcher_ClearSnakeSegment(u32 x, u32 y)
{
    Clear(LAUNCHER_SNAKE_X + x * LAUNCHER_SNAKE_CELL + 1,
          LAUNCHER_SNAKE_Y + y * LAUNCHER_SNAKE_CELL + 1,
          7, 7, Launcher_SnakeBoardColour(), 1);
}

static void Launcher_DrawSnakeScore(void)
{
    char score[12];
    int x;
    snprintf(score, sizeof(score), "%lu", launcher_snake_length - 4);
    x = 237 - DrawText12VisibleLength(score) * 6;
    Launcher_ClearWithThemeBG(Launcher_GetTopbarBG(HELP), 190, 2, 48, 15);
    DrawHZText12(score, 0, x, 3, gl_color_topbar_text, 1);
}

static void Launcher_DrawSnakeBoard(void)
{
    const int board_w = LAUNCHER_SNAKE_COLS * LAUNCHER_SNAKE_CELL;
    const int board_h = LAUNCHER_SNAKE_ROWS * LAUNCHER_SNAKE_CELL;
    u16 board = Launcher_SnakeBoardColour();
    u16 grid = Launcher_SnakeGridColour();
    u32 i;

    Clear(LAUNCHER_SNAKE_X, LAUNCHER_SNAKE_Y, board_w, board_h, board, 1);
    for(i = 0; i <= LAUNCHER_SNAKE_COLS; i++)
        Clear(LAUNCHER_SNAKE_X + i * LAUNCHER_SNAKE_CELL, LAUNCHER_SNAKE_Y, 1, board_h + 1, grid, 1);
    for(i = 0; i <= LAUNCHER_SNAKE_ROWS; i++)
        Clear(LAUNCHER_SNAKE_X, LAUNCHER_SNAKE_Y + i * LAUNCHER_SNAKE_CELL, board_w + 1, 1, grid, 1);

    Launcher_DrawSnakeFood();
    for(i = launcher_snake_length; i > 0; i--)
    {
        u32 part = i - 1;
        Launcher_DrawSnakeSegment(launcher_snake_x[part], launcher_snake_y[part]);
    }
    Launcher_DrawSnakeScore();
}

static u32 Launcher_SnakeStep(int dx, int dy, u32 *tail_x, u32 *tail_y, u32 *grew)
{
    int new_x = (int)launcher_snake_x[0] + dx;
    int new_y = (int)launcher_snake_y[0] + dy;
    u32 ate;
    u32 i;

    if(new_x < 0 || new_x >= LAUNCHER_SNAKE_COLS || new_y < 0 || new_y >= LAUNCHER_SNAKE_ROWS)
        return 0;
    if(Launcher_SnakeOccupies((u32)new_x, (u32)new_y))
        return 0;

    *tail_x = launcher_snake_x[launcher_snake_length - 1];
    *tail_y = launcher_snake_y[launcher_snake_length - 1];
    ate = ((u32)new_x == launcher_snake_food_x && (u32)new_y == launcher_snake_food_y);
    *grew = ate && launcher_snake_length < LAUNCHER_SNAKE_MAX_LENGTH;
    if(*grew)
        launcher_snake_length++;
    for(i = launcher_snake_length - 1; i > 0; i--)
    {
        launcher_snake_x[i] = launcher_snake_x[i - 1];
        launcher_snake_y[i] = launcher_snake_y[i - 1];
    }
    launcher_snake_x[0] = (u8)new_x;
    launcher_snake_y[0] = (u8)new_y;
    if(ate)
        Launcher_SnakePlaceFood();
    return 1;
}

static void Launcher_DrawSnakeGameOver(void)
{
    const int x = 51;
    const int y = 65;
    const int w = 138;
    const int h = 38;
    Clear(x, y, w, h, gl_color_selectBG_sd, 1);
    DrawHZText12("Game over", 0, x + (w - 9 * 6) / 2, y + 6, LAUNCHER_SELECTED_TEXT, 1);
    DrawHZText12("A: Again  B: Back", 0, x + (w - 17 * 6) / 2, y + 21, LAUNCHER_SELECTED_TEXT, 1);
}

static void Launcher_PlaySnake(void)
{
    int dx = 1;
    int dy = 0;
    u32 move_frames = 0;
    u32 game_over = 0;

    Launcher_InvalidateListArtScaledCache();
    Launcher_DrawThemeBGFull((const u16*)gImage_SD_LIST);
    Launcher_DrawTopbarName(HELP);
    Launcher_DrawTopbarTitle(HELP, "Snake");
    Launcher_SnakeReset();
    Launcher_DrawSnakeBoard();

    while(1)
    {
        u16 keysdown;
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        keysdown = keysDown();

        if(keysdown & KEY_B)
        {
            UIAudio_PlayBack();
            Launcher_InvalidateListArtScaledCache();
            return;
        }
        if(game_over)
        {
            if(keysdown & (KEY_A | KEY_START))
            {
                UIAudio_PlayAccept();
                dx = 1;
                dy = 0;
                move_frames = 0;
                game_over = 0;
                Launcher_SnakeReset();
                Launcher_DrawSnakeBoard();
            }
            continue;
        }

        if((keysdown & KEY_UP) && dy == 0) { dx = 0; dy = -1; }
        else if((keysdown & KEY_DOWN) && dy == 0) { dx = 0; dy = 1; }
        else if((keysdown & KEY_LEFT) && dx == 0) { dx = -1; dy = 0; }
        else if((keysdown & KEY_RIGHT) && dx == 0) { dx = 1; dy = 0; }

        move_frames++;
        if(move_frames >= 8)
        {
            u32 old_tail_x;
            u32 old_tail_y;
            u32 grew;

            move_frames = 0;
            if(!Launcher_SnakeStep(dx, dy, &old_tail_x, &old_tail_y, &grew))
            {
                game_over = 1;
                Launcher_DrawSnakeGameOver();
            }
            else
            {
                if(!grew)
                    Launcher_ClearSnakeSegment(old_tail_x, old_tail_y);
                Launcher_DrawSnakeSegment(launcher_snake_x[0], launcher_snake_y[0]);
                if(grew)
                {
                    Launcher_DrawSnakeFood();
                    Launcher_DrawSnakeScore();
                }
            }
        }
    }
}

static void Launcher_ShowHelpTextPagesEx(const char *title, const char *const *lines, u32 line_count, u32 allow_snake)
{
    const u32 lines_per_page = 9;
    u32 page = 0;
    u32 page_count = (line_count + lines_per_page - 1) / lines_per_page;
    if(page_count == 0)
        page_count = 1;

    Launcher_DrawHelpTextPage(title, lines, line_count, page);

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        scanKeys();
        {
            u16 keysdown = keysDown();
            if(keysdown & KEY_B)
            {
                UIAudio_PlayBack();
                return;
            }
            if(allow_snake && (keysdown & KEY_START))
            {
                UIAudio_PlaySfx(UI_SFX_MENU);
                Launcher_PlaySnake();
                Launcher_DrawHelpTextPage(title, lines, line_count, page);
            }
            else if((keysdown & (KEY_A | KEY_R | KEY_RIGHT)) && page + 1 < page_count)
            {
                page++;
                UIAudio_PlaySfx(UI_SFX_MOVE);
                Launcher_DrawHelpTextPage(title, lines, line_count, page);
            }
            else if((keysdown & (KEY_L | KEY_LEFT)) && page > 0)
            {
                page--;
                UIAudio_PlaySfx(UI_SFX_MOVE);
                Launcher_DrawHelpTextPage(title, lines, line_count, page);
            }
        }
    }
}

static void Launcher_ShowHelpTextPages(const char *title, const char *const *lines, u32 line_count)
{
    Launcher_ShowHelpTextPagesEx(title, lines, line_count, 0);
}

static void Launcher_DrawHelpOnline(void)
{
    Launcher_DrawThemeBGFull((const u16*)gImage_HELP);
    Launcher_DrawTopbarName(HELP);
    Launcher_DrawTopbarTitle(HELP, DSTEXT_HELP_TITLE);
    Launcher_DrawHelpClock(1);
}

static void Launcher_ShowHelpOnline(void)
{
    Launcher_DrawHelpOnline();
    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawHelpClock(0);
        scanKeys();
        if(keysDown() & KEY_B)
        {
            UIAudio_PlayBack();
            return;
        }
    }
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
        DSTEXT_CONTROL_QUICK_HOTKEY,
        DSTEXT_CONTROL_QUICK_ACTION
    };
    Launcher_ShowHelpTextPages(DSTEXT_CONTROLS_TITLE, lines, sizeof(lines) / sizeof(lines[0]));
}

typedef enum
{
    HELP_TOPIC_ONLINE = 0,
    HELP_TOPIC_CONTROLS,
    HELP_TOPIC_OPERATION,
    HELP_TOPIC_ARTWORK,
    HELP_TOPIC_ABOUT,
    HELP_TOPIC_TOTAL
} LauncherHelpTopic;

static void Launcher_HelpTopicGetLine(u32 item, char *out, u32 out_size)
{
    const char *label = "";
    char label_short[48];
    u32 used;
    u32 spaces;

    switch(item)
    {
        case HELP_TOPIC_ONLINE: label = DSTEXT_HELP_ONLINE; break;
        case HELP_TOPIC_CONTROLS: label = DSTEXT_CONTROLS_TITLE; break;
        case HELP_TOPIC_OPERATION: label = DSTEXT_HELP_OPERATION; break;
        case HELP_TOPIC_ARTWORK: label = DSTEXT_HELP_ARTWORK; break;
        case HELP_TOPIC_ABOUT: label = DSTEXT_HELP_ABOUT; break;
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
        snprintf(out + used, out_size - used, ">");
}

static void Launcher_ShowHelpTopic(u32 topic)
{
    static const char *const operation_lines[] =
    {
        "Interface",
        "- Time sets the cartridge",
        "  clock shown in the top bar.",
        "- Clock format chooses a",
        "  12-hour or 24-hour clock.",
        "- Language changes the menu",
        "  text used by DS Style.",
        "- Sounds turns menu audio",
        "  effects on or off.",

        "- Hide system keeps kernel,",
        "  SYSTEM and computer files",
        "  out of the SD browser.",
        "- List folders keeps non-game",
        "  folders in list view.",
        "- Clean list removes file",
        "  extensions and details.",
        "- Theme changes Light, Dark",
        "  or a custom theme.",

        "- Colour changes accent",
        "  colours across DS Style.",
        "- Load style prepares another",
        "  kernel from SYSTEM/KERNELS.",
        "- View mode chooses List,",
        "  List + Art, Horizontal",
        "  or Vertical.",
        "- Thumbnails chooses Title",
        "  or Box artwork.",

        "Artwork display",
        "- Art border draws a fine",
        "  outline around selected art.",
        "- Rounded corners softens",
        "  artwork corners.",
        "- Vertical side aligns the",
        "  small artwork above and",
        "  below the selected item.",
        "",

        "- Horizontal side aligns the",
        "  small artwork left and",
        "  right of the selected item.",
        "- List art places the preview",
        "  at the top, centre or bottom",
        "  in List + Art view.",
        "- Boot to chooses Start, SD,",
        "  NOR, Last game, Recents",
        "  or Favourites at startup.",

        "Start screen",
        "- Start screen turns the",
        "  opening screen on or off.",
        "- The second Start screen",
        "  option chooses Last Played",
        "  or Favourites there.",
        "- Quick start chooses the",
        "  boot hotkey for instantly",
        "  launching the last game.",

        "Games",
        "- Boot engine changes launch",
        "  handling for games.",
        "- Auto save controls save",
        "  prompts when DS Style opens.",
        "- Addon settings choose Reset,",
        "  RTS, Sleep and Cheats.",
        "- Boot mode chooses Clean,",
        "  Addon or Menu by default.",

        "In-game options",
        "- Sleep hotkey chooses the",
        "  in-game sleep command.",
        "- Addon hotkey chooses the",
        "  in-game addon menu command.",
        "- Enable BIOS shows the GBA",
        "  intro before a game starts.",
        "- Backup saves keeps copies",
        "  of save files in BACKUP.",

        "Hardware",
        "- Mode B chooses Rumble, RAM",
        "  or Link behaviour with the",
        "  rear switch.",
        "- In-game RTC supports clocks",
        "  in compatible games.",
        "- LED settings changes the",
        "  cartridge light behaviour."
    };
    static const char *const artwork_lines[] =
    {
        "Artwork folders",
        "IMGS holds wide artwork.",
        "IMGS2 holds square artwork.",
        "CUSTOM is for homebrew,",
        "ROM hacks, folders, and",
        "other file types.",
        "",
        "",
        "",
        "Custom artwork",
        "Use the exact file or folder",
        "name, ending in .bmp.",
        "Works with any file type.",
        "Each CUSTOM folder supports",
        "up to 256 images.",
        "",
        "Use the Thumbnail Scraper",
        "to build artwork packs on PC."
    };
    static const char *const about_lines[] =
    {
        "DS Style " LAUNCHER_VERSION_TEXT,
        "Nintendo DS inspired kernel",
        "for EZ-Flash Omega and",
        "Omega Definitive Edition.",
        "",
        "Created by FrankieT19.",
        "Free and open source.",
        "Credit requested when shared.",
        "",
        "Credits",
        "Original kernel source by",
        "EZ-FLASH.",
        "Thai language support by",
        "aidiadayo.",
        "PogoShell integration",
        "inherited from Simple by",
        "Sterophonick."
    };


    switch(topic)
    {
        case HELP_TOPIC_ONLINE:
            Launcher_ShowHelpOnline();
            break;
        case HELP_TOPIC_CONTROLS:
            Launcher_DrawHelpControls();
            break;
        case HELP_TOPIC_OPERATION:
            Launcher_ShowHelpTextPages(DSTEXT_HELP_OPERATION, operation_lines, sizeof(operation_lines) / sizeof(operation_lines[0]));
            break;
        case HELP_TOPIC_ARTWORK:
            Launcher_ShowHelpTextPages(DSTEXT_HELP_ARTWORK, artwork_lines, sizeof(artwork_lines) / sizeof(artwork_lines[0]));
            break;
        case HELP_TOPIC_ABOUT:
            Launcher_ShowHelpTextPagesEx(DSTEXT_HELP_ABOUT, about_lines, sizeof(about_lines) / sizeof(about_lines[0]), 1);
            break;
        default:
            break;
    }
}

static void Launcher_ShowHelpBOnly(void)
{
    const u32 total = HELP_TOPIC_TOTAL;
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
            Launcher_SettingsDrawList(DSTEXT_SETTINGS_HELP, total, selected, top, Launcher_HelpTopicGetLine);
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
            if(keysdown & KEY_L)
            {
                Launcher_RequestSettingsTab(2);
                return;
            }
            if(keysdown & KEY_R)
            {
                Launcher_RequestSettingsTab(0);
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_HelpTopicGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_HelpTopicGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_HelpTopicGetLine);
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_HelpTopicGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_HelpTopicGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_HelpTopicGetLine);
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT))
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_ShowHelpTopic(selected);
                dirty = 1;
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
        {
            strncpy(out, recent_name, out_size - 1);
            out[out_size - 1] = '\0';
        }
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
		Launcher_ActivateThumbnailWorkspace(0xFE);
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
		Launcher_ActivateThumbnailWorkspace(0xFE);
        Launcher_ScaleThumbToBox(Launcher_NotFoundImage(), Launcher_NotFoundWidth(), Launcher_NotFoundHeight(), launcher_side_preview_left, thumb_w, thumb_h);
        if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
            Launcher_DrawPicClipStride(launcher_side_preview_left + 9, thumb_w, x + 9, y, 37, thumb_h);
        else
            Launcher_DrawPicClipStride(launcher_side_preview_left, thumb_w, x, y, thumb_w, thumb_h);
    }

    if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
        Launcher_RestoreStartThumbCorners(x + 9, y, 37, thumb_h);
    else
        Launcher_RestoreStartThumbCorners(x, y, thumb_w, thumb_h);
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
        Launcher_FormatClock(msgtime, sizeof(msgtime), HH, MM, SS);
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
        Launcher_FormatClock(msgtime, sizeof(msgtime), HH, MM, SS);
        DrawHZText12(msgtime, 0, x, y, gl_color_topbar_text, 1);
        last_hh = HH;
        last_mm = MM;
        last_ss = SS;
    }
}

static void Launcher_DrawEmptyListMessage(const char *message)
{
    int w;
    int x;

    if(!message || !message[0])
        return;

    w = DrawText12VisibleLength((char*)message) * 6;
    x = (240 - w) / 2;
    if(x < 0)
        x = 0;
    DrawHZText12((TCHAR*)message, 0, x, 84, gl_color_text, 1);
}

static void Launcher_DrawEmptyCarouselMessage(const char *message)
{
    char lines[3][32];
    int line_count;
    int i;
    int btn_x;
    int btn_y;
    int btn_w;
    int btn_h;
    int line_h = 12;
    int text_y;

    if(!message || !message[0])
        return;

    if(Launcher_ActiveViewMode() == 2)
    {
        btn_x = LAUNCHER_VERT_TITLE_X;
        btn_y = LAUNCHER_VERT_TITLE_Y;
        btn_w = LAUNCHER_VERT_TITLE_W;
        btn_h = LAUNCHER_VERT_TITLE_H;
    }
    else
    {
        btn_x = LAUNCHER_HORZ_TITLE_X;
        btn_y = LAUNCHER_HORZ_TITLE_Y;
        btn_w = LAUNCHER_HORZ_TITLE_W;
        btn_h = LAUNCHER_HORZ_TITLE_H;
    }

    Launcher_RestoreBGClip((u16*)Launcher_GetBGImage(), btn_x - 1, btn_y - 1, btn_w + 2, btn_h + 2);
    line_count = Launcher_SplitTitle(message, lines);
    if(line_count > 3)
        line_count = 3;
    text_y = btn_y + ((btn_h - (line_count * line_h)) / 2);
    if(text_y < btn_y + 2)
        text_y = btn_y + 2;

    for(i = 0; i < line_count; i++)
    {
        int len = strlen(lines[i]);
        int text_x = btn_x + ((btn_w - (len * 6)) / 2);
        if(text_x < btn_x + 4)
            text_x = btn_x + 4;
        DrawHZText12(lines[i], len, text_x, text_y + (i * line_h), gl_color_text, 1);
    }
}

static const char *Launcher_EmptyBrowserMessage(void)
{
    if(recents_view_active && recents_view_favourites)
        return DSTEXT_NO_FAVOURITES;
    return DSTEXT_EMPTY_FOLDER;
}

static void Launcher_DrawStartWindow(u32 selected)
{
    if(launcher_start_window_preserved)
    {
        Launcher_ClearWithThemeBG((const u16*)gImage_START, 0, LAUNCHER_TOP_BAR_HEIGHT, 240, 160 - LAUNCHER_TOP_BAR_HEIGHT);
        launcher_start_window_preserved = 0;
    }
    else
    {
        Launcher_DrawThemeBGFull((const u16*)gImage_START);
        Launcher_DrawTopbarName(START_win);
        Launcher_DrawStartClock(1);
    }

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

typedef enum
{
    SETTINGS_CATEGORY_INTERFACE = 0,
    SETTINGS_CATEGORY_GAMES,
    SETTINGS_CATEGORY_HARDWARE,
    SETTINGS_CATEGORY_HELP,
    SETTINGS_CATEGORY_TOTAL
} LauncherSettingsCategory;

static const u32 launcher_settings_interface_items[] = { SETTINGS_TIME_SETTINGS, SETTINGS_CLOCK_FORMAT, SETTINGS_LANGUAGE, SETTINGS_SOUNDS, SETTINGS_HIDE_SYSTEM, SETTINGS_LIST_FOLDERS, SETTINGS_CLEAN_LIST, SETTINGS_THEME_MODE, SETTINGS_THEME, SETTINGS_LOAD_STYLE, SETTINGS_VIEW_MODE, SETTINGS_THUMBNAILS, SETTINGS_ART_BORDER,
    SETTINGS_ROUNDED_CORNERS, SETTINGS_VERTICAL_SIDE, SETTINGS_HORIZONTAL_SIDE, SETTINGS_LIST_ART_POSITION, SETTINGS_START_ENABLED, SETTINGS_BOOT_TO, SETTINGS_START_SOURCE };
static const u32 launcher_settings_games_items[] = { SETTINGS_BOOT_ENGINE, SETTINGS_AUTO_SAVE, SETTINGS_AUTO_START, SETTINGS_ADDON_SETTINGS, SETTINGS_BOOT_MODE, SETTINGS_SLEEP_HOTKEY, SETTINGS_ADDON_HOTKEY, SETTINGS_FULL_INTRO, SETTINGS_BACKUP_SAVES };
static const u32 launcher_settings_hardware_items[] = { SETTINGS_MODE_B, SETTINGS_INGAME_RTC, SETTINGS_LED_SETTINGS };

static const u32 *launcher_settings_active_items = 0;
static u32 launcher_settings_active_total = 0;
static u32 launcher_settings_main_selected = 0;
static u32 launcher_settings_main_top = 0;
static int launcher_settings_resume_category = -1;
static u32 launcher_settings_category_selected[SETTINGS_CATEGORY_TOTAL];
static u32 launcher_settings_category_top[SETTINGS_CATEGORY_TOTAL];

static const char *Launcher_SettingsCategoryTitle(u32 category)
{
    switch(category)
    {
        case SETTINGS_CATEGORY_INTERFACE: return DSTEXT_CATEGORY_INTERFACE;
        case SETTINGS_CATEGORY_GAMES: return DSTEXT_CATEGORY_GAMES;
        case SETTINGS_CATEGORY_HARDWARE: return DSTEXT_CATEGORY_HARDWARE;
        case SETTINGS_CATEGORY_HELP: return DSTEXT_SETTINGS_HELP;
        default: return DSTEXT_SETTINGS_TITLE;
    }
}

static void Launcher_SettingsCategoryGetLine(u32 item, char *out, u32 out_size)
{
    const char *label = Launcher_SettingsCategoryTitle(item);
    char label_short[48];
    u32 used;
    u32 spaces;

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
        snprintf(out + used, out_size - used, ">");
}

static void Launcher_SettingsActiveGetLine(u32 item, char *out, u32 out_size)
{
    if(!launcher_settings_active_items || item >= launcher_settings_active_total)
    {
        if(out_size) out[0] = 0;
        return;
    }
    Launcher_SettingsGetLine(launcher_settings_active_items[item], out, out_size);
}

static void Launcher_SettingsSelectCategory(u32 category)
{
    launcher_settings_active_items = 0;
    launcher_settings_active_total = 0;
    switch(category)
    {
        case SETTINGS_CATEGORY_INTERFACE:
            launcher_settings_active_items = launcher_settings_interface_items;
            launcher_settings_active_total = sizeof(launcher_settings_interface_items) / sizeof(launcher_settings_interface_items[0]);
            break;
        case SETTINGS_CATEGORY_GAMES:
            launcher_settings_active_items = launcher_settings_games_items;
            launcher_settings_active_total = sizeof(launcher_settings_games_items) / sizeof(launcher_settings_games_items[0]);
            break;
        case SETTINGS_CATEGORY_HARDWARE:
            launcher_settings_active_items = launcher_settings_hardware_items;
            launcher_settings_active_total = sizeof(launcher_settings_hardware_items) / sizeof(launcher_settings_hardware_items[0]);
            break;
        default:
            break;
    }
}

static void Launcher_SettingsCategoryWindow(u32 category)
{
    const u32 visible = 9;
    u32 selected;
    u32 top;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    Launcher_SettingsSelectCategory(category);
    if(!launcher_settings_active_total)
        return;

    selected = launcher_settings_category_selected[category];
    top = launcher_settings_category_top[category];
    if(selected >= launcher_settings_active_total)
        selected = 0;
    if(top > selected)
        top = selected;
    if(selected >= top + visible)
        top = (selected >= visible) ? (selected - visible + 1) : 0;

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawSettingsClock(0);
        if(scroll_delay > 0)
            scroll_delay--;

        if(dirty)
        {
            Launcher_SettingsDrawList(Launcher_SettingsCategoryTitle(category), launcher_settings_active_total, selected, top, Launcher_SettingsActiveGetLine);
            dirty = 0;
        }

        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();
            if(keysdown & KEY_B)
            {
                launcher_settings_category_selected[category] = selected;
                launcher_settings_category_top[category] = top;
                UIAudio_PlayBack();
                return;
            }
            if(keysdown & KEY_L)
            {
                launcher_settings_category_selected[category] = selected;
                launcher_settings_category_top[category] = top;
                launcher_settings_resume_category = category;
                Launcher_RequestSettingsTab(2);
                return;
            }
            if(keysdown & KEY_R)
            {
                launcher_settings_category_selected[category] = selected;
                launcher_settings_category_top[category] = top;
                launcher_settings_resume_category = category;
                Launcher_RequestSettingsTab(0);
                return;
            }
            if((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && scroll_delay == 0))
            {
                if(selected + 1 < launcher_settings_active_total)
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsActiveGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsActiveGetLine);
                        Launcher_SettingsDrawArrows(launcher_settings_active_total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(launcher_settings_active_total, selected, top, Launcher_SettingsActiveGetLine);
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsActiveGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsActiveGetLine);
                        Launcher_SettingsDrawArrows(launcher_settings_active_total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(launcher_settings_active_total, selected, top, Launcher_SettingsActiveGetLine);
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT))
            {
                u32 changed_item = launcher_settings_active_items[selected];
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsToggle(changed_item, 1);
                if(launcher_settings_tab_return != -1)
                {
                    launcher_settings_category_selected[category] = selected;
                    launcher_settings_category_top[category] = top;
                    launcher_settings_resume_category = category;
                    return;
                }
                if(Launcher_SettingsItemNeedsFullRedraw(changed_item))
                    dirty = 1;
                else
                    Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsActiveGetLine);
            }
            else if(keysdown & KEY_LEFT)
            {
                u32 changed_item = launcher_settings_active_items[selected];
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                Launcher_SettingsToggle(changed_item, -1);
                if(launcher_settings_tab_return != -1)
                {
                    launcher_settings_category_selected[category] = selected;
                    launcher_settings_category_top[category] = top;
                    launcher_settings_resume_category = category;
                    return;
                }
                if(Launcher_SettingsItemNeedsFullRedraw(changed_item))
                    dirty = 1;
                else
                    Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsActiveGetLine);
            }
        }
    }
}

static u32 Launcher_SettingsWindow(void)
{
    const u32 total = SETTINGS_CATEGORY_TOTAL;
    const u32 visible = 9;
    u32 selected = launcher_settings_main_selected;
    u32 top = launcher_settings_main_top;
    u32 dirty = 1;
    u32 scroll_delay = 0;

    if(selected >= total)
        selected = 0;
    if(top > selected)
        top = selected;
    if(selected >= top + visible)
        top = (selected >= visible) ? (selected - visible + 1) : 0;

    if(launcher_settings_resume_category >= 0 && launcher_settings_resume_category < (int)SETTINGS_CATEGORY_TOTAL)
    {
        u32 resume_category = (u32)launcher_settings_resume_category;
        launcher_settings_resume_category = -1;
        selected = resume_category;
        launcher_settings_main_selected = selected;
        Launcher_SettingsCategoryWindow(resume_category);
        if(launcher_settings_tab_return != -1)
        {
            int tab_return = launcher_settings_tab_return;
            launcher_settings_tab_return = -1;
            return (u32)tab_return;
        }
        dirty = 1;
    }

    while(1)
    {
        VBlankIntrWait();
        UIAudio_Update();
        Launcher_DrawSettingsClock(0);
        if(scroll_delay > 0)
            scroll_delay--;

        if(dirty)
        {
            Launcher_SettingsDrawList(DSTEXT_SETTINGS_TITLE, total, selected, top, Launcher_SettingsCategoryGetLine);
            dirty = 0;
        }

        scanKeys();
        {
            u16 keysdown = keysDown();
            u16 keysrepeat = keysDownRepeat();

            if(keysdown & KEY_B)
            {
                launcher_settings_main_selected = selected;
                launcher_settings_main_top = top;
                launcher_settings_resume_category = -1;
                UIAudio_PlayBack();
                return 3;
            }
            if(keysdown & KEY_L)
            {
                launcher_settings_main_selected = selected;
                launcher_settings_main_top = top;
                UIAudio_PlaySfx(UI_SFX_TAB);
                return 2;
            }
            if(keysdown & KEY_R)
            {
                launcher_settings_main_selected = selected;
                launcher_settings_main_top = top;
                UIAudio_PlaySfx(UI_SFX_TAB);
                return 0;
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsCategoryGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsCategoryGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_SettingsCategoryGetLine);
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
                        Launcher_SettingsDrawRowValueOnly(old_selected, selected, top, Launcher_SettingsCategoryGetLine);
                        Launcher_SettingsDrawRowValueOnly(selected, selected, top, Launcher_SettingsCategoryGetLine);
                        Launcher_SettingsDrawArrows(total, top);
                    }
                    else
                        Launcher_SettingsDrawRowsOnly(total, selected, top, Launcher_SettingsCategoryGetLine);
                }
            }
            else if(keysdown & (KEY_A | KEY_RIGHT))
            {
                UIAudio_PlaySfx(UI_SFX_ACCEPT);
                if(selected == SETTINGS_CATEGORY_HELP)
                    Launcher_ShowHelpBOnly();
                else
                    Launcher_SettingsCategoryWindow(selected);
                if(launcher_settings_tab_return != -1)
                {
                    int tab_return = launcher_settings_tab_return;
                    launcher_settings_tab_return = -1;
                    launcher_settings_main_selected = selected;
                    launcher_settings_main_top = top;
                    return (u32)tab_return;
                }
                dirty = 1;
            }
        }
    }
}

static u32 __attribute__((unused)) Launcher_Setting_window2(void)
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
	Check_FW_update();


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
	Launcher_ReadHideSystemFilesSetting();
	Launcher_ReadListFoldersSetting();
	Launcher_ReadCleanListSetting();
	Launcher_ReadClockFormatSetting();
	Launcher_ReadViewModeSetting();
	Launcher_ReadListArtPositionSetting();
	Launcher_ReadArtBorderSetting();
	Launcher_ReadRoundedCornersSetting();
	Launcher_ReadVerticalSideSetting();
	Launcher_ReadHorizontalSideSetting();
	Launcher_ReadAutoStartKey();
	Launcher_ReadStartSource();
	Launcher_ReadBootToSetting();
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
	if((page_num == SD_list) || (page_num == NOR_list))
		launcher_force_full_redraw = 1;


	if(page_num== SD_list)
	{
		Launcher_RestoreSDState();
		folder_total = 0;
		game_total_SD = 0;
		launcher_sd_launchable_file_count = 0;

		if(recents_view_active)
		{
			game_total_SD = recents_view_favourites ? Build_favourites_virtual_list() : Build_recent_virtual_list();
			launcher_sd_launchable_file_count = game_total_SD;
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
					if(Launcher_ShouldHideSystemEntry(currentpath, fileinfo.fname, fileinfo.fattrib))
						continue;

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
						if(launcher_list_folders && (launcher_sd_launchable_file_count == 0) &&
						Launcher_IsLaunchableFilename(pFilename_buffer[game_total_SD].filename))
							launcher_sd_launchable_file_count = 1;
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
		if(launcher_boot_target == LAUNCHER_BOOT_TO_NOR)
		{
			recents_view_active = 0;
			page_num = NOR_list;
			launcher_start_selected = 2;
			launcher_force_full_redraw = 1;
		}
		else if((launcher_boot_target == LAUNCHER_BOOT_TO_RECENTS) ||
		(launcher_boot_target == LAUNCHER_BOOT_TO_FAVOURITES))
		{
			recents_view_active = 1;
			strncpy(recents_return_path, "/", sizeof(recents_return_path) - 1);
			recents_return_path[sizeof(recents_return_path) - 1] = '\0';
			recents_return_show_offset = 0;
			recents_return_file_select = 0;
			recents_return_folder_select = 1;
			Launcher_SetRecentVirtualMode(launcher_boot_target == LAUNCHER_BOOT_TO_FAVOURITES);
			page_num = SD_list;
			launcher_start_selected = 0;
			launcher_force_full_redraw = 1;
			start_screen_pending = 0;
			goto refind_file;
		}
		else if(launcher_start_screen_off || resume_last_found || launcher_boot_target == LAUNCHER_BOOT_TO_SD)
		{
			page_num = SD_list;
			launcher_start_selected = 0;
			launcher_force_full_redraw = 1;
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
	Launcher_UpdateEffectiveViewMode();
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
			if(shift==0){
				dwName =0;
			}
			shift++;

			haveThumbnail = 0;
			is_GBA = 0;
			launcher_active_page = page_num;

			if(updata && launcher_effective_show_thumbnail && ((page_num == SD_list) || (page_num == NOR_list)))
			{
				u32 absolute_index = show_offset + file_select;
				LauncherEntryInfo selected_info;

				memset(&selected_info, 0, sizeof(selected_info));
				selected_info.thumb_data = pReadCache + 0x10036;
				if(launcher_cache_center_index != absolute_index)
					Launcher_BuildThumbCache(absolute_index);
				Launcher_GetEntryInfo(absolute_index, &selected_info);

				if(selected_info.name && !selected_info.is_folder)
				{
					if(Launcher_IsNORPage())
					{
						is_GBA = 1;
						haveThumbnail = launcher_cache_selected.has_thumbnail;
					}
					else
					{
						u32 strlengba = strlen(selected_info.name);
						if((strlengba >= 3) && !strcasecmp(&(selected_info.name[strlengba-3]), "gba"))
						{
							is_GBA = 1;
							haveThumbnail = launcher_cache_selected.has_thumbnail;
						}
					}
				}
				else if(is_GBA_old==1)
				{
					updata = 1;
				}

				is_GBA_old = is_GBA;
			}
	if(updata==1){//reshow all
		u32 popup_list_restored = 0;
		if(page_num==SD_list)
		{
			if(launcher_restore_popup_region)
			{
				Launcher_RestorePopupRegionForPage(page_num,
				launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail);
				if(Launcher_IsListLikeMode())
				{
					Launcher_RestoreListPopupContent(show_offset, file_select);
					popup_list_restored = 1;
				}
				launcher_restore_popup_region = 0;
				launcher_counter_valid = 0;
			}
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
				if(Launcher_IsListLikeMode())
					Launcher_ClearWithThemeBG(Launcher_GetBGImage(),185, 0, 6*9+1, 18);
			}
			gl_clock_dirty = 1;
			if(Launcher_IsListLikeMode() && !popup_list_restored)
			{
				Launcher_ClearTextBodyBackground();
			}
			else if(game_folder_total == 0)
			{
				/* In the modern thumbnail launchers, an empty folder would otherwise
				skip the draw path entirely and leave the previous folder contents
				visible. Clear the launcher body so the new empty state is obvious. */
				Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 20, 240, 160-20);
				Launcher_DrawEmptyCarouselMessage(Launcher_EmptyBrowserMessage());
			}
			if(Launcher_IsListLikeMode() && !popup_list_restored)
			{
				if(Launcher_IsListArtMode())
					Launcher_DrawListBodyFromCache(show_offset, file_select, 1);
				else
					Show_ICON_filename_SD(show_offset,file_select,0);
				if(game_folder_total == 0)
					Launcher_DrawEmptyListMessage(Launcher_EmptyBrowserMessage());
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
							while(1)
							{
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

								if(!launcher_reopen_sd_menu_after_redraw)
									break;

								launcher_reopen_sd_menu_after_redraw = 0;
								if(launcher_reopen_from_cheat_screen)
								{
									launcher_reopen_from_cheat_screen = 0;
									launcher_restore_popup_region = 0;
									launcher_start_window_preserved = 0;
									Launcher_DrawStartWindow(0);
								}
								else
								{
									launcher_restore_popup_region = 0;
									launcher_start_window_preserved = 1;
								}
								if(!Launcher_PrepareLastPlayedForMenu())
									break;
							}

							launcher_start_selected = 0;
							page_num = START_win;
							if(launcher_restore_popup_region)
							{
								launcher_restore_popup_region = 0;
								launcher_start_window_preserved = 1;
								launcher_force_full_redraw = 0;
							}
							else
							{
								launcher_force_full_redraw = 1;
							}
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
						page_num = NOR_list;
						launcher_force_full_redraw = 1;
					}
					else if(res == 1){
						Launcher_DrawThemeBGFull((const u16*)gImage_SET);
						page_num = SET_win;
					}
					else{
						launcher_sd_restore_pending = 1;
						page_num = SD_list;
						launcher_force_full_redraw = 1;
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
					Launcher_FlushInputForModal();
					if(res == 0)
					{
						launcher_sd_restore_pending = 1;
						page_num = SD_list;
						launcher_start_selected = 1;
						launcher_force_full_redraw = 1;
						launcher_vertical_folder_label_dirty = 1;
						goto refind_file;
					}
					else if(res == 2)
					{
						recents_view_active = 0;
						page_num = NOR_list;
						launcher_start_selected = 2;
						launcher_force_full_redraw = 1;
					}
					else
					{
						if(launcher_start_screen_off)
						{
							launcher_sd_restore_pending = 1;
							page_num = SD_list;
							launcher_start_selected = 1;
							launcher_force_full_redraw = 1;
							goto refind_file;
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
			if(launcher_restore_popup_region)
			{
				Launcher_RestorePopupRegionForPage(page_num,
				launcher_cache_prev.valid && launcher_cache_prev.has_thumbnail);
				if(Launcher_IsListLikeMode())
				{
					Launcher_RestoreListPopupContent(show_offset, file_select);
					popup_list_restored = 1;
				}
				launcher_restore_popup_region = 0;
				launcher_counter_valid = 0;
			}
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
					if(Launcher_IsListLikeMode())
						Launcher_ClearWithThemeBG(Launcher_GetBGImage(),185, 0, 6*9+1, 18);
				}
			gl_clock_dirty = 1;
				if(Launcher_IsListLikeMode() && !popup_list_restored)
				{
					Launcher_ClearTextBodyBackground();
				}
				else if(game_total_NOR == 0)
				{
					Launcher_ClearWithThemeBG(Launcher_GetBGImage(),0, 20, 240, 160-20);
				}
				if(Launcher_IsListLikeMode() && !popup_list_restored)
				{
					if(Launcher_IsListArtMode())
						Launcher_DrawListBodyFromCache(show_offset, file_select, game_total_NOR != 0);
					else
						Show_ICON_filename_NOR(show_offset,file_select);
				}
		}
		Show_game_num(file_select+show_offset+1,page_num,1);
		}
		else if((updata == 4) || (updata == 5))
		{
			if(Launcher_IsListArtMode())
			{
				Launcher_DrawListArtScrolledBody(show_offset, file_select, (updata == 4) ? 1 : -1);
			}
			else
			{
				Launcher_DrawScrolledSDListBody(show_offset, file_select);
			}
			Show_game_num(file_select+show_offset+1,page_num,0);
		}
		else if(updata >1){
		if(page_num==NOR_list)
		{
				if(Launcher_IsListLikeMode())
				{
					if(Launcher_IsListArtMode())
					{
						u32 old_line = (updata == 2) ? (file_select - 1) : (file_select + 1);
						Launcher_DrawListArtSelectionChange(show_offset, file_select, old_line);
					}
					else
					{
						Refresh_filename_NOR(show_offset,file_select,updata);
					}
				}
		}
		else
		{
			if(Launcher_IsListLikeMode())
			{
				u32 old_line = (updata == 2) ? (file_select - 1) : (file_select + 1);

				if(Launcher_IsListArtMode())
				{
					Launcher_DrawListArtSelectionChange(show_offset, file_select, old_line);
				}
				else
				{
					Launcher_DrawSDListRow(show_offset, old_line, file_select, 0);
					Launcher_DrawSDListRow(show_offset, file_select, file_select, 0);
				}
			}
		}
		Show_game_num(file_select+show_offset+1,page_num,0);
		}

		if( updata && !Launcher_IsListLikeMode() && launcher_effective_show_thumbnail && ((page_num==SD_list) || (page_num==NOR_list)) && ((page_num==NOR_list) ? game_total_NOR : game_folder_total) )
		{
		if(launcher_effective_show_thumbnail == 1)
				Draw_ModernLauncher_SD(show_offset, file_select, haveThumbnail);
			else
			{
				Draw_ModernLauncher_SD_Vertical_State(show_offset, file_select);
			}
		}
		launcher_popup_restore_redraw = 0;

			if(updata)
			{
				ShowTime(page_num,page_mode);
			}

			if(launcher_reopen_sd_menu_after_redraw && (page_num == SD_list))
			{
				launcher_reopen_sd_menu_after_redraw = 0;
				launcher_reopen_from_cheat_screen = 0;
				res = SD_list_MENU(show_offset, file_select, recents_view_active ? (show_offset + file_select) : play_re);
				if(res)
				{
					if(res == 2)
					{
						recents_view_active = 0;
						page_num = NOR_list;
					}
					goto refind_file;
				}
				goto re_showfile;
			}

			if(continue_MENU) break;
			if(((page_num == SD_list) && game_folder_total) ||
			((page_num == NOR_list) && game_total_NOR))
			{
				if(Launcher_IsListLikeMode())
					Filename_loop(shift,show_offset,file_select,Launcher_IsListArtMode());
			}
	updata=0;
			scanKeys();
			u16 keysdown  = keysDown();
			u16 keys_released = keysUp();
			u16 keysheld = keysHeld();
			u16 keysrepeat = keysDownRepeat();
			u16 buffered_list_art_input = 0;
			u16 launcher_nav_repeat_mask = 0;
			u16 launcher_horz_step_forward = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_DOWN : KEY_RIGHT;
			u16 launcher_horz_step_back = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_UP : KEY_LEFT;
			u16 launcher_horz_jump_forward = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_RIGHT : KEY_DOWN;
			u16 launcher_horz_jump_back = (LAUNCHER_HORZ_NAV_MODE == 1) ? KEY_LEFT : KEY_UP;
			u16 launcher_vert_step_forward = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_RIGHT : KEY_DOWN;
			u16 launcher_vert_step_back = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_LEFT : KEY_UP;
			u16 launcher_vert_jump_forward = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_DOWN : KEY_RIGHT;
			u16 launcher_vert_jump_back = (LAUNCHER_VERT_NAV_MODE == 1) ? KEY_UP : KEY_LEFT;

			if(Launcher_IsListArtMode())
			{
				buffered_list_art_input = Launcher_ListArtTakeInput();
				if(buffered_list_art_input)
				{
					keysdown |= buffered_list_art_input;
					keysrepeat &= ~(KEY_UP | KEY_DOWN);
				}
			}

			if((page_num == SD_list) || (page_num == NOR_list))
			{
				/* Keep held navigation from running at the maximum key-repeat rate in
				fast folders, but do not add the heavy delay that made thumbnail
				traversal feel sluggish.  One main-loop beat is enough to stop the
				almost-instant runaway scroll while preserving responsiveness. */
				if(launcher_effective_show_thumbnail == 1)
					launcher_nav_repeat_mask = launcher_horz_step_forward | launcher_horz_step_back;
				else if(launcher_effective_show_thumbnail == 2)
					launcher_nav_repeat_mask = launcher_vert_step_forward | launcher_vert_step_back;
				else
					launcher_nav_repeat_mask = KEY_UP | KEY_DOWN;

				if(!(keysheld & launcher_nav_repeat_mask))
					launcher_nav_repeat_delay = 0;
				else if(launcher_nav_repeat_delay)
				{
					keysrepeat &= ~launcher_nav_repeat_mask;
					launcher_nav_repeat_delay--;
				}
			}

			u16 audio_keysdown = keysdown;
			if(launcher_select_release_cooldown)
				launcher_select_release_cooldown--;

			if((page_num == SD_list) || (page_num == NOR_list))
			{
				if(launcher_effective_show_thumbnail)
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
			if((launcher_effective_show_thumbnail == 2) && ((page_num==SD_list) || (page_num==NOR_list)))
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
					}
					shift = 0;
					thumbnail_nav_handled = 1;
				}
			}
			if((launcher_effective_show_thumbnail == 1) && ((page_num==SD_list) || (page_num==NOR_list)))
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
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
						updata = LAUNCHER_UPDATE_CAROUSEL_SELECTION;
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
	updata=((page_num == SD_list) && Launcher_IsListLikeMode()) ? 4 : 1;
	}
	}else{
	file_select++;
	updata=2;
	}
					UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
					shift = 0;
					if(Launcher_IsListArtMode())
						Launcher_ShiftThumbCache(1, show_offset + file_select);
				}
			}
			else if(!thumbnail_nav_handled && (keysrepeat & KEY_UP))
			{
				if (file_select ) {
					file_select--;
					updata=3;
					UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
				}else{
					if (show_offset){
						show_offset--;
						updata=((page_num == SD_list) && Launcher_IsListLikeMode()) ? 5 : 1;
						UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
					}
				}
				shift = 0;
				if(Launcher_IsListArtMode() && ((updata == 3) || (updata == 5)))
					Launcher_ShiftThumbCache(-1, show_offset + file_select);
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
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
		}
		else{
			if(file_select){
				file_select=0;
				updata=1;
				UIAudio_PlaySfx(UI_SFX_MOVE);
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
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
						launcher_nav_repeat_delay = Launcher_ListNavRepeatDelay();
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
						TCHAR nextpath[MAX_path_len + 200];
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
			res = SD_list_MENU(show_offset,file_select, recents_view_active ? (show_offset + file_select) : play_re);
						if(res){
							launcher_force_full_redraw = 1;
							if(res==2){
								recents_view_active = 0;
								page_num = NOR_list;
							}
							goto refind_file ;
						}
						else{
							if(launcher_reopen_sd_menu_after_redraw)
								goto re_showfile;
							launcher_force_full_redraw = 0;
							updata = 1;
							continue;
						}
						//break;
					}
				}
				else{   //NOR gba file
					if(game_total_NOR){
						res = NOR_list_MENU(show_offset,file_select);
						if(res){
							launcher_force_full_redraw = 1;
							goto refind_file ;
						}
						else{
							launcher_force_full_redraw = 0;
							updata = 1;
							continue;
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

			if(((page_num == SD_list) || (page_num == NOR_list)) &&
			Launcher_IsListArtMode() && Launcher_GetTotalEntries() &&
			launcher_list_art_pending)
			{
				if(updata || select_tap_pending || keysdown || keysheld || keysrepeat || keys_released)
					launcher_list_art_idle_frames = 0;
				else if(launcher_list_art_idle_frames < LAUNCHER_LIST_ART_IDLE_LOAD_FRAMES)
					launcher_list_art_idle_frames++;
				else
					Launcher_ServicePendingListArt(show_offset, file_select);
			}
			else
			{
				launcher_list_art_idle_frames = 0;
			}

			ShowTime(page_num,page_mode);
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
	MENU_max = (show_offset + file_select == 0) ? 4 : 2;
	DrawPic((u16*)gImage_MENU, 36, 25, 168, 110, 1, 0, 1);//show menu pic
	Show_MENU_btn();
	while(1)//3
	{
		if(re_menu)
		{
			Show_MENU(MENU_line,NOR_list, 0,0,0,(show_offset+file_select==0));
		}
		VBlankIntrWait();

    re_menu=0;
		UIAudio_Update();
		if(menu_scroll_delay > 0)
			menu_scroll_delay--;
		scanKeys();
		keysdown  = keysDown();
		keysup  = keysUp();
		keysrepeat = keysDownRepeat();
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
				u32 first_press = (keysdown & KEY_DOWN) != 0;
				u32 old_MENU_line = MENU_line;
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
				u32 first_press = (keysdown & KEY_UP) != 0;
				u32 old_MENU_line = MENU_line;
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
			launcher_restore_popup_region = 1;
			launcher_force_full_redraw = 0;
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
			else if(MENU_line==3){ //load save data to FRAM
				error_num = FRAM_save_op(1);
				if(error_num != 0)
					Show_error_num(error_num);
				return 0;
			}
			else{ //save FRAM data
				error_num = FRAM_save_op(2);
				if(error_num != 0)
					Show_error_num(error_num);
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
		if(!CheatSelectionAppliesTo(pfilename, havecht))
			gl_cheat_selected_count = 0;
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

    re_menu=0;
		UIAudio_Update();
		if(menu_scroll_delay > 0)
			menu_scroll_delay--;
		scanKeys();
		keysdown  = keysDown();
		keysup  = keysUp();
		keysrepeat = keysDownRepeat();
		UIAudio_HandleKeysEx(keysdown, 0, 0, 0);
		keys_released = keysUp();
		if(ignore_b_frames)
			ignore_b_frames--;

		if ((keysdown & KEY_DOWN) || ((keysrepeat & KEY_DOWN) && menu_scroll_delay == 0)) {
			if (MENU_line < MENU_max) {
        u32 first_press = (keysdown & KEY_DOWN) != 0;
        u32 old_MENU_line = MENU_line;
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
				u32 first_press = (keysdown & KEY_UP) != 0;
				u32 old_MENU_line = MENU_line;
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
			CheatSelectionForget();
			if(play_re!=0xBB){
				strncpy(currentpath, currentpath_temp, 256);//
			}
			f_chdir(currentpath);//return to old folder
			launcher_restore_popup_region = 1;
			launcher_force_full_redraw = 0;
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
				f_chdir(currentpath);
				launcher_reopen_sd_menu_after_redraw = 1;
				launcher_reopen_from_cheat_screen = 1;
				launcher_force_full_redraw = 1;
				return 0;
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
					launcher_current_topbar_bg = 0;
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
					launcher_current_topbar_bg = 0;
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
