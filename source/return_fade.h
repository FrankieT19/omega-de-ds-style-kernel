#ifndef DS_STYLE_RETURN_FADE_H
#define DS_STYLE_RETURN_FADE_H

static u16 launcher_return_blend;
static u8 launcher_return_fade_pending;

static void Launcher_HoldReturn(void)
{
    REG_BLDCNT = launcher_return_blend;
    REG_BLDY = 16;
    SetMode(MODE_3 | BG2_ENABLE);
    launcher_return_fade_pending = 1;
}

static void Launcher_RevealReturn(void)
{
    int level;
    if(!launcher_return_fade_pending) return;
    launcher_return_fade_pending = 0;
    /* The first complete launcher frame is already behind the blend. */
    for(level = 16; level >= 0; level -= 2)
    {
        VBlankIntrWait();
        REG_BLDY = level;
    }
    REG_BLDCNT = 0;
    REG_BLDY = 0;
}
#endif
