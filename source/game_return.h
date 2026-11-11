#ifndef DS_STYLE_GAME_RETURN_H
#define DS_STYLE_GAME_RETURN_H

#include "slot_return_io.h"

/* This part of the LCD scratch bank is above the display backup and below
   the shared 128-byte mapper return record used by both game engines. */
#define DS_RETURN_CONTEXT 0x0e00f800
#define DS_RETURN_MENU_PALETTE 0x0e00fef0
typedef struct {
    u32 checksum, page, view, offset, row, start_item, featured;
    u32 virtual_mode, parent_offset, parent_row, favourite;
    u16 offsets[100], rows[100];
    char folder[MAX_path_len];
    char game[512];
} DSReturnContext;
_Static_assert(sizeof(DSReturnContext) <= DS_RETURN_MENU_PALETTE - DS_RETURN_CONTEXT, "Return context exceeds scratch bank");
static DSReturnContext ds_return EWRAM_BSS;
static u32 ds_return_pending;

static u32 DSStyle_ReturnChecksum(void)
{
    const u8 *data = (const u8 *)&ds_return;
    u32 hash = 2166136261u, i;
    for(i = sizeof(ds_return.checksum); i < sizeof(ds_return); i++)
        hash = (hash ^ data[i]) * 16777619u;
    return hash;
}

static void DSStyle_CaptureReturn(PAGE_NUM page, u32 offset, u32 row)
{
    unsigned i;
    memset(&ds_return, 0, sizeof(ds_return));
    ds_return.page = page;
    ds_return.view = gl_show_Thumbnail;
    ds_return.offset = offset;
    ds_return.row = row;
    ds_return.start_item = launcher_start_selected;
    ds_return.featured = launcher_start_uses_favourites;
    ds_return.favourite = launcher_favourite_index;
    ds_return.virtual_mode = page == SD_list && recents_view_active ? 1 + !!recents_view_favourites : 0;
    ds_return.parent_offset = recents_return_show_offset;
    ds_return.parent_row = recents_return_file_select;
    snprintf(ds_return.folder, sizeof(ds_return.folder), "%s",
             ds_return.virtual_mode ? recents_return_path :
             page == NOR_list ? launcher_sd_saved_path : currentpath);
    if(!ds_return.folder[0]) strcpy(ds_return.folder, "/");
    for(i = 0; i < 100; i++)
    {
        ds_return.offsets[i] = p_folder_select_show_offset[i];
        ds_return.rows[i] = p_folder_select_file_select[i];
    }
}

static void DSStyle_ArmReturn(unsigned platform, const char *filename)
{
    unsigned options = SLOT_RETURN_HOLD_ENABLED;
    u16 menu_palette[] = {0x4453, gl_color_selectBG_sd, gl_color_selected};
    if(launcher_dark_mode || (launcher_custom_theme_mode && LAUNCHER_CUSTOM_THEME_DARK_STYLE))
        options |= DS_RETURN_DARK;
    if(ds_return.virtual_mode)
        snprintf(ds_return.game, sizeof(ds_return.game), "%s%s%s", currentpath,
                 !strcmp(currentpath, "/") ? "" : "/", filename);
    ds_return.checksum = DSStyle_ReturnChecksum();
    SetRampage(SLOT_RETURN_PAGE);
    WriteSram(DS_RETURN_CONTEXT, (u8 *)&ds_return, sizeof(ds_return));
    WriteSram(DS_RETURN_MENU_PALETTE, (u8 *)menu_palette, sizeof(menu_palette));
    SetRampage(0);
    /* The filename in the common record is a sentinel. The full launcher
       path above is independent of the emulator's short filename field. */
    Slot_ArmReturn("DS Style", platform, options);
}

static void DSStyle_ReadReturn(void)
{
    ds_return_pending = 0;
    Slot_ReadReturn();
    if(!slot_return_pending || strcmp(slot_return_filename, "DS Style")) return;
    SetRampage(SLOT_RETURN_PAGE);
    ReadSram(DS_RETURN_CONTEXT, (u8 *)&ds_return, sizeof(ds_return));
    SetRampage(0);
    if(ds_return.checksum != DSStyle_ReturnChecksum() ||
       (ds_return.page != START_win && ds_return.page != SD_list && ds_return.page != NOR_list) ||
       ds_return.virtual_mode > 2 || ds_return.start_item > 3 || ds_return.row > 9 ||
       ds_return.view > LAUNCHER_VIEW_LIST_ART ||
       ds_return.folder[0] != '/' || ds_return.folder[sizeof(ds_return.folder)-1] ||
       ds_return.game[sizeof(ds_return.game)-1]) return;
    ds_return_pending = 1;
    launcher_return_blend = (slot_return_options & DS_RETURN_DARK) ? 0xff : 0xbf;
}

static PAGE_NUM DSStyle_RestoreReturn(void)
{
    unsigned i;
    if(f_chdir(ds_return.folder) != FR_OK)
    {
        f_chdir("/");
        strcpy(ds_return.folder, "/");
        ds_return.offset = ds_return.row = 0;
        ds_return.parent_offset = ds_return.parent_row = 0;
    }
    strcpy(currentpath, ds_return.folder);
    folder_select = Get_path_depth(currentpath);
    if(folder_select < 1 || folder_select >= 100) folder_select = 1;
    for(i = 0; i < 100; i++)
    {
        p_folder_select_show_offset[i] = ds_return.offsets[i];
        p_folder_select_file_select[i] = ds_return.rows[i] < 10 ? ds_return.rows[i] : 0;
    }
    Launcher_SaveSDState();
    gl_show_Thumbnail = ds_return.view;
    launcher_start_selected = ds_return.start_item;
    launcher_start_uses_favourites = !!ds_return.featured;
    launcher_favourite_index = ds_return.favourite;
    if(ds_return.page == NOR_list)
    {
        gl_nor_show_offset_saved = ds_return.offset;
        gl_nor_file_select_saved = ds_return.row;
    }
    else if(ds_return.virtual_mode)
    {
        recents_view_active = 1;
        strcpy(recents_return_path, currentpath);
        recents_return_folder_select = folder_select;
        recents_return_show_offset = ds_return.parent_offset;
        recents_return_file_select = ds_return.parent_row;
        Launcher_SetRecentVirtualMode(ds_return.virtual_mode == 2);
        recents_saved_show_offset = ds_return.offset;
        recents_saved_file_select = ds_return.row;
    }
    else if(ds_return.page == SD_list)
    {
        p_folder_select_show_offset[folder_select] = ds_return.offset;
        p_folder_select_file_select[folder_select] = ds_return.row;
    }
    return ds_return.page;
}
#endif
