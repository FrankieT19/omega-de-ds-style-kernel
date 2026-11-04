#ifndef DS_STYLE_LAUNCH_INDICATOR_H
#define DS_STYLE_LAUNCH_INDICATOR_H

/* Last 2 KiB of OBJ VRAM, beyond compositor code and patch scratch. */
#define LAUNCHER_SPINNER_TILES ((vu16*)0x06017800)
#define LAUNCHER_SPINNER_OAM ((vu16*)0x07000000)
#define LAUNCHER_SPINNER_PALETTE ((vu16*)0x050003E0)
static struct {
    u16 oam[512], tiles[1024], palette[16], display;
    struct IntTable interrupts[MAX_INTS];
    volatile u16 frame;
    u16 dark, start_icon, y;
} launcher_spinner EWRAM_BSS;
static void Launcher_GetLaunchIndicatorRect(int *x, int *y, int *w, int *h);

static void Launcher_LaunchVBlank(void)
{
    u32 i;
    if(!launcher_launch_screen_active) return;
    launcher_spinner.frame++;
    if(launcher_spinner.frame < 6) return;
    LAUNCHER_SPINNER_OAM[0] = launcher_spinner.y;
    for(i = 0; i < 6; i++)
    {
        u32 step = (launcher_spinner.frame + 30 - i * 5) % 30;
        u16 shade = step * 20 / 30;
        u16 grey = launcher_spinner.dark ? 29 - shade : 8 + shade;
        LAUNCHER_SPINNER_PALETTE[i + 1] = RGB(grey, grey, grey);
    }
}

/* Doubled coordinates preserve the artwork's integer/half-pixel centre.
   Six 50-degree arcs leave consistent ten-degree gaps, with no backing. */
static void Launcher_PrepareLaunchRing(int x, int y, int w, int h)
{
    static const s16 vx[6] = {0, 887, 887, 0, -887, -887};
    static const s16 vy[6] = {-1024, -512, 512, 1024, 512, -512};
    int d = (w < h ? w : h) * 3 / 5;
    int rw, rh, row, col, inner;
    u8 *tiles = pReadCache + 0x200;
    if(d < 12) d = 12;
    if(d > 48) d = 48;
    rw = d + ((d ^ w) & 1);
    rh = d + ((d ^ h) & 1);
    inner = d * 2 / 3;
    memset(tiles, 0, 2048);
    for(row = 0; row < rh; row++) for(col = 0; col < rw; col++)
    {
        int dx = 2 * col - rw + 1, dy = 2 * row - rh + 1;
        int radius = dx * dx + dy * dy;
        int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        int sector, dot, cross, offset;
        if(radius > d * d || radius < inner * inner) continue;
        sector = ax * 1024 <= ay * 591 ? (dy < 0 ? 0 : 3) :
            (dx > 0 ? (dy < 0 ? 1 : 2) : (dy < 0 ? 5 : 4));
        dot = dx * vx[sector] + dy * vy[sector];
        cross = dx * vy[sector] - dy * vx[sector];
        if(cross < 0) cross = -cross;
        if(cross * 1024 > dot * 478) continue;
        offset = ((row >> 3) * 8 + (col >> 3)) * 32 + (row & 7) * 4 + (col & 7) / 2;
        tiles[offset] |= (sector + 1) << ((col & 1) * 4);
    }
    launcher_spinner.y = (y + (h - rh) / 2) & 255;
    LAUNCHER_SPINNER_OAM[0] = launcher_spinner.y | 0x0200;
    LAUNCHER_SPINNER_OAM[1] = ((x + (w - rw) / 2) & 511) | 0xC000;
    LAUNCHER_SPINNER_OAM[2] = 960 | 0xF000;
}

static void Launcher_BeginLaunchScreen(void)
{
    u32 i;
    int x, y, w, h;
    u16 ime = REG_IME;
    if(launcher_launch_screen_active) return;
    Launcher_GetLaunchIndicatorRect(&x, &y, &w, &h);
    REG_IME = 0;
    launcher_spinner.display = REG_DISPCNT;
    launcher_spinner.frame = 0;
    launcher_spinner.dark = launcher_dark_mode || (launcher_custom_theme_mode && LAUNCHER_CUSTOM_THEME_DARK_STYLE);
    for(i = 0; i < 512; i++) launcher_spinner.oam[i] = LAUNCHER_SPINNER_OAM[i];
    for(i = 0; i < 1024; i++) launcher_spinner.tiles[i] = LAUNCHER_SPINNER_TILES[i];
    for(i = 0; i < 16; i++) launcher_spinner.palette[i] = LAUNCHER_SPINNER_PALETTE[i];
    for(i = 0; i < MAX_INTS; i++) launcher_spinner.interrupts[i] = IntrTable[i];
    for(i = 0; i < 128; i++) LAUNCHER_SPINNER_OAM[i * 4] = 0x0200;
    Launcher_PrepareLaunchRing(x, y, w, h);
    for(i = 0; i < 1024; i++) LAUNCHER_SPINNER_TILES[i] = ((u16*)(pReadCache + 0x200))[i];
    launcher_launch_screen_active = 1;
    irqSet(IRQ_VBLANK, Launcher_LaunchVBlank);
    REG_DISPCNT |= 0x1040;
    asm volatile("" ::: "memory");
    REG_IME = ime;
}

static void Launcher_EndLaunchIndicator(void)
{
    u32 i;
    u16 ime = REG_IME;
    if(!launcher_launch_screen_active) return;
    REG_IME = 0;
    launcher_launch_screen_active = 0;
    REG_DISPCNT = launcher_spinner.display;
    for(i = 0; i < MAX_INTS; i++) IntrTable[i] = launcher_spinner.interrupts[i];
    for(i = 0; i < 512; i++) LAUNCHER_SPINNER_OAM[i] = launcher_spinner.oam[i];
    for(i = 0; i < 1024; i++) LAUNCHER_SPINNER_TILES[i] = launcher_spinner.tiles[i];
    for(i = 0; i < 16; i++) LAUNCHER_SPINNER_PALETTE[i] = launcher_spinner.palette[i];
    asm volatile("" ::: "memory");
    REG_IME = ime;
}

u32 Launcher_DrawLaunchProgress(const char *status)
{
    (void)status;
    return launcher_launch_screen_active;
}

static void Launcher_FinishLaunch(void)
{
    Launcher_EndLaunchIndicator();
    Launcher_FadeOut();
}
#endif
