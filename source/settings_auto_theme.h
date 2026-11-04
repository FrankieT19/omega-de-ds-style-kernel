#ifndef DS_STYLE_SETTINGS_AUTO_THEME_H
#define DS_STYLE_SETTINGS_AUTO_THEME_H

static void Launcher_QueueAutoTheme(u32 hour)
{
    u16 dark;
    if(!launcher_auto_theme || hour > 23) return;
    dark = hour >= 20 || hour < 8;
    launcher_auto_theme_pending = dark == launcher_dark_mode ? 0 : dark + 1;
}

/* Called only by the owner of a normal UI screen, never inside a loader or
   modal popup. This avoids mixing two themes while a popup is open. */
static u32 Launcher_ApplyPendingAutoTheme(void)
{
    if(!launcher_auto_theme || !launcher_auto_theme_pending) return 0;
    launcher_dark_mode = launcher_auto_theme_pending - 1;
    launcher_auto_theme_pending = 0;
    Launcher_ApplyThemeColours();
    return 1;
}
#endif
