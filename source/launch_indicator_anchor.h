#ifndef DS_STYLE_LAUNCH_INDICATOR_ANCHOR_H
#define DS_STYLE_LAUNCH_INDICATOR_ANCHOR_H

static void Launcher_GetLaunchIndicatorRect(int *x, int *y, int *w, int *h)
{
    u32 view = Launcher_ActiveViewMode();
    *x = 88; *y = 48; *w = 64; *h = 64;
    if(launcher_active_page == START_win)
    {
        *x = LAUNCHER_START_LAST_THUMB_X; *y = LAUNCHER_START_LAST_THUMB_Y;
        *w = LAUNCHER_START_THUMB_W; *h = LAUNCHER_START_THUMB_H;
        if(launcher_spinner.start_icon)
        {
            *x += (*w - 32) / 2; *y += (*h - 28) / 2;
            *w = 32; *h = 28;
        }
        else if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
        {
            *x += 9; *w = 37;
        }
    }
    else if(view == LAUNCHER_VIEW_HORIZONTAL || view == LAUNCHER_VIEW_VERTICAL)
    {
        LauncherMotionRect r = Launcher_MotionSlot(0, view);
        LauncherMotionImage image = Launcher_MotionImage(&launcher_cache_selected,
            launcher_cache_selected.absolute_index);
        r = Launcher_MotionImageRect(r, image.icon, 0);
        *x = r.x; *y = r.y; *w = r.w; *h = r.h;
    }
    else if(view == LAUNCHER_VIEW_LIST_ART && launcher_list_art_screen_has_art)
        Launcher_GetListArtRect(x, y, w, h);
}
#endif
