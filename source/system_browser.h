#ifndef DS_STYLE_SYSTEM_BROWSER_H
#define DS_STYLE_SYSTEM_BROWSER_H

/* The maintenance browser must not replace the user's normal SD location. */
static struct {
    u32 active, depth, saved_depth, restore, recent, favourites;
    u32 return_offset, return_selected, return_depth, recent_offset, recent_selected;
    TCHAR path[MAX_path_len], saved_path[MAX_path_len], return_path[MAX_path_len];
    u32 offsets[100], selections[100];
} launcher_system_browser EWRAM_BSS;

static FRESULT Launcher_OpenSystemFolder(u32 *offset, u32 *selected, u32 *depth)
{
    FRESULT result;
    launcher_open_system_requested = 0;
    result = f_chdir("/SYSTEM");
    if(result != FR_OK) return result;
    if(!launcher_system_browser.active)
    {
        memcpy(launcher_system_browser.path, currentpath, sizeof(currentpath));
        memcpy(launcher_system_browser.saved_path, launcher_sd_saved_path, sizeof(launcher_sd_saved_path));
        memcpy(launcher_system_browser.return_path, recents_return_path, sizeof(recents_return_path));
        memcpy(launcher_system_browser.offsets, p_folder_select_show_offset, sizeof(p_folder_select_show_offset));
        memcpy(launcher_system_browser.selections, p_folder_select_file_select, sizeof(p_folder_select_file_select));
        launcher_system_browser.depth = *depth;
        launcher_system_browser.saved_depth = launcher_sd_saved_folder_select;
        launcher_system_browser.restore = launcher_sd_restore_pending;
        launcher_system_browser.recent = recents_view_active;
        launcher_system_browser.favourites = recents_view_favourites;
        launcher_system_browser.return_offset = recents_return_show_offset;
        launcher_system_browser.return_selected = recents_return_file_select;
        launcher_system_browser.return_depth = recents_return_folder_select;
        launcher_system_browser.recent_offset = recents_saved_show_offset;
        launcher_system_browser.recent_selected = recents_saved_file_select;
        launcher_system_browser.active = 1;
    }
    strcpy(currentpath, "/SYSTEM");
    recents_view_active = 0;
    recents_view_favourites = 0;
    *offset = 0; *selected = 0; *depth = 2;
    p_folder_select_show_offset[2] = 0;
    p_folder_select_file_select[2] = 0;
    launcher_sd_restore_pending = 0;
    return FR_OK;
}

static void Launcher_CloseSystemBrowser(void)
{
    if(!launcher_system_browser.active) return;
    memcpy(currentpath, launcher_system_browser.path, sizeof(currentpath));
    memcpy(launcher_sd_saved_path, launcher_system_browser.saved_path, sizeof(launcher_sd_saved_path));
    memcpy(recents_return_path, launcher_system_browser.return_path, sizeof(recents_return_path));
    memcpy(p_folder_select_show_offset, launcher_system_browser.offsets, sizeof(p_folder_select_show_offset));
    memcpy(p_folder_select_file_select, launcher_system_browser.selections, sizeof(p_folder_select_file_select));
    folder_select = launcher_system_browser.depth;
    launcher_sd_saved_folder_select = launcher_system_browser.saved_depth;
    launcher_sd_restore_pending = launcher_system_browser.restore;
    recents_view_active = launcher_system_browser.recent;
    recents_view_favourites = launcher_system_browser.favourites;
    recents_return_show_offset = launcher_system_browser.return_offset;
    recents_return_file_select = launcher_system_browser.return_selected;
    recents_return_folder_select = launcher_system_browser.return_depth;
    recents_saved_show_offset = launcher_system_browser.recent_offset;
    recents_saved_file_select = launcher_system_browser.recent_selected;
    launcher_system_browser.active = 0;
    if(f_chdir(currentpath) != FR_OK)
    {
        strcpy(currentpath, "/"); strcpy(launcher_sd_saved_path, "/");
        folder_select = launcher_sd_saved_folder_select = 1;
        recents_view_active = 0;
        f_chdir("/");
    }
}

static u32 Launcher_SystemBrowserBack(PAGE_NUM *page)
{
    if(*page != SD_list || !launcher_system_browser.active || recents_view_active ||
        strcasecmp(currentpath, "/SYSTEM")) return 0;
    Launcher_CloseSystemBrowser();
    launcher_settings_main_selected = SETTINGS_CATEGORY_SYSTEM;
    launcher_settings_resume_category = -1;
    launcher_force_full_redraw = 1;
    launcher_system_name_dirty = 1;
    gl_clock_dirty = 1;
    *page = SET_win;
    return 1;
}
#endif
