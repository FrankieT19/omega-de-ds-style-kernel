#ifndef DS_STYLE_MENU_FADE_H
#define DS_STYLE_MENU_FADE_H

static void Launcher_FadeTo(u32 white)
{
    static const u8 brightness[] = {1, 2, 3, 5, 7, 9, 11, 13, 14, 15, 16};
    u16 previous = REG_BLDCNT;
    u32 frame;
    REG_BLDY = 0;
    REG_BLDCNT = (white ? 0x0080 : 0x00C0) | 0x003F;
    for(frame = 0; frame < sizeof(brightness); frame++)
    {
        VBlankIntrWait();
        UIAudio_Update();
        REG_BLDY = brightness[frame];
    }
    VBlankIntrWait();
    /* Commit the terminal colour before restoring effects. No blend register
       may leak into the loading screen, an error dialog, or the launched game. */
    {
        volatile u32 fill = white ? 0x7FFF7FFF : 0;
        DMA3COPY(&fill, (void*)VRAM, DMA_SRC_FIXED | DMA32 | (240 * 160 / 2));
    }
    REG_BLDY = 0;
    REG_BLDCNT = previous;
    launcher_current_topbar_bg = 0;
    launcher_current_theme_bg = 0;
    launcher_counter_valid = 0;
    launcher_system_name_dirty = 1;
    gl_clock_dirty = 1;
}

static void Launcher_FadeOut(void)
{
    Launcher_FadeTo(!(launcher_dark_mode || (launcher_custom_theme_mode && LAUNCHER_CUSTOM_THEME_DARK_STYLE)));
}

/* The complete image is already in VRAM, hidden by forced blank. */
static void Launcher_RevealSplash(void)
{
    static const u8 levels[] = {12, 8, 4, 1, 0};
    u32 i;
    REG_BLDCNT = 0x00BF;
    REG_BLDY = 16;
    VBlankIntrWait();
    REG_DISPCNT &= (u16)~0x0080;
    for(i = 0; i < sizeof(levels); i++)
    {
        VBlankIntrWait();
        UIAudio_Update();
        REG_BLDY = levels[i];
    }
    REG_BLDCNT = 0;
    REG_BLDY = 0;
}

/* Consumed before the first OS screen, including a pending-save dialog. */
static u32 launcher_splash_fade_pending = 1;
static void Launcher_EndSplash(void)
{
    if(!launcher_splash_fade_pending) return;
    launcher_splash_fade_pending = 0;
    Launcher_FadeTo(1);
}

#endif
