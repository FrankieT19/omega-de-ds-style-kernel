#ifndef DS_STYLE_SAVE_POPUP_H
#define DS_STYLE_SAVE_POPUP_H

#include "save_popup_text.h"

/* All artwork I/O finishes before the save writer takes the shared cache.
   No chdir, NOR scan, save-format extension or new persistent cache is needed. */
static u32 Launcher_LoadSaveArtwork(const char *save_name)
{
    char stem[104], recent_stem[104], path[MAX_path_len], name[200], full[512];
    u8 *raw = pReadCache + 0x10000;
    u8 code[4];
    UINT got;
    u32 found = 0, read_size = Launcher_ThumbnailReadSize();
    u32 bitmap_height;
    const char *extension = strrchr(save_name, '.');
    Launcher_CustomThumbFileName(save_name, stem, sizeof(stem));
    if(!stem[0]) return 0;
    snprintf(full, sizeof(full), "%s/CUSTOM/%s.bmp", Launcher_ThumbnailFolder(), stem);
    if(f_open(&gfile, full, FA_READ) == FR_OK)
    {
        UIAudio_StopForSharedBufferUse();
        found = f_read(&gfile, raw, read_size, &got) == FR_OK && got == read_size;
        f_close(&gfile);
    }
    if(!found && extension && !strcasecmp(extension, ".sav") &&
       Read_last_played_entry(path, sizeof(path), name, sizeof(name)))
    {
        Launcher_CustomThumbFileName(name, recent_stem, sizeof(recent_stem));
        if(!strcasecmp(stem, recent_stem) && Launcher_IsGbaFilename(name))
        {
            Launcher_BuildFullPath(path, name, full, sizeof(full));
            memcpy(code, GAMECODE, 4);
            found = Launcher_LoadThumbnailFromRomHeader(full, raw);
            memcpy(GAMECODE, code, 4);
        }
    }
    if(!found) return 0;
    memcpy(&bitmap_height, raw + 22, 4);
    if(raw[0] != 'B' || raw[1] != 'M' || raw[10] != 54 ||
       raw[18] != Launcher_ThumbnailSourceWidth() || raw[19] || raw[20] || raw[21] ||
       (bitmap_height != 80 && bitmap_height != (u32)-80) || raw[28] != 16) return 0;
    Launcher_ScaleThumbToBox((const u16*)(raw + 54), Launcher_ThumbnailSourceWidth(),
        Launcher_ThumbnailSourceHeight(), (u16*)(pReadCache + 0x18000), 48, 32);
    return 1;
}

static void Launcher_SavePopupFit(const char *src, char *out, u32 size, u32 cells)
{
    u32 length = DrawText12VisibleLength((char*)src);
    DrawText12CopyVisible(out, size, (char*)src, length > cells ? cells - 3 : cells);
    if(length > cells) strncat(out, "...", size - strlen(out) - 1);
}

static void Launcher_SavePopupLine(const char *src, int x, int y, int width, u32 centre)
{
    char line[100];
    Launcher_SavePopupFit(src, line, sizeof(line), width / 6);
    if(centre) x += (width - DrawText12VisibleLength(line) * 6) / 2;
    DrawHZText12(line, 0, x, y, gl_color_text, 1);
}

static void Launcher_SavePopupName(const char *name, u32 artwork)
{
    char title[128], first[100], second[100], *space;
    u32 cells = artwork ? 15 : 25, used;
    int x = artwork ? 101 : 44, width = artwork ? 90 : 152;
    Launcher_CleanTitle(name, title, sizeof(title));
    DrawText12CopyVisible(first, sizeof(first), title, cells);
    used = DrawText12ByteOffsetForGlyphs(title, cells);
    if(title[used] && (space = strrchr(first, ' ')) && space - first > (int)cells / 2)
    {
        used = space - first;
        *space = 0;
    }
    while(title[used] == ' ') used++;
    Launcher_SavePopupFit(title + used, second, sizeof(second), cells);
    Launcher_SavePopupLine(first, x, second[0] ? 63 : 70, width, !artwork);
    if(second[0]) Launcher_SavePopupLine(second, x, 77, width, !artwork);
}

static void Launcher_SavePopupHeading(u32 writing)
{
    Launcher_SavePopupLine(Launcher_SavePopupText(writing ? 1 : 0), 44, 40, 152, 1);
}

/* The 80px artwork radius scales to a small rounded curve at 32px high.
   Art and border share this contour; skipped pixels retain the popup fill. */
static int Launcher_SaveArtInset(int row)
{
    if(!Launcher_RoundedCornersForCarousel()) return 0;
    if(row > 15) row = 31 - row;
    return row == 0 ? 3 : row < 3 ? 1 : 0;
}

static void Launcher_DrawSaveArtwork(const u16 *src, int x, int y, int w)
{
    int row, inset, start;
    u16 colour = Launcher_ArtBorderColourEx(1);
    for(row = -1; row <= 32; row++)
    {
        if(row == -1 || row == 32)
        {
            if(launcher_art_border_mode)
            {
                inset = Launcher_SaveArtInset(0);
                Clear(x + (inset ? inset : -1), y + row,
                    inset ? w - 2 * inset : w + 2, 1, colour, 1);
            }
            continue;
        }
        inset = Launcher_SaveArtInset(row);
        dmaCopy(src + row * 48 + inset, VideoBuffer + (y + row) * 240 + x + inset,
            (w - 2 * inset) * 2);
        if(launcher_art_border_mode)
        {
            /* Four-neighbour silhouette: bridge steps only beside art pixels. */
            start = inset - 1;
            if(row > 0 && Launcher_SaveArtInset(row - 1) < start) start = Launcher_SaveArtInset(row - 1);
            if(row < 31 && Launcher_SaveArtInset(row + 1) < start) start = Launcher_SaveArtInset(row + 1);
            Clear(x + start, y + row, inset - start, 1, colour, 1);
            Clear(x + w - inset, y + row, inset - start, 1, colour, 1);
        }
    }
}

static void Launcher_DrawSavePopup(const char *name, u32 artwork, u32 writing)
{
    u16 edge = RGB(15,15,15);
    Launcher_DrawPopupPanel(36, 25);
    Launcher_SavePopupHeading(writing);
    Clear(44, 56, 152, 1, edge, 1);
    if(artwork)
    {
        int inset = launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX ? 8 : 0;
        int width = inset ? 32 : 48;
        Launcher_DrawSaveArtwork((const u16*)(pReadCache + 0x18000) + inset,
            45 + inset, 62, width);
    }
    Launcher_SavePopupName(name, artwork);
    Launcher_SavePopupLine(Launcher_SavePopupText(2), 44, 97, 152, 1);
    Launcher_SavePopupLine(Launcher_SavePopupText(3), 44, 111, 152, 1);
}

static void Launcher_SavePopupBeginWrite(void)
{
    /* Only the heading changes; the name, art and safety text stay in place. */
    Launcher_RestoreLegacyMenuRow(39, 15);
    Launcher_SavePopupHeading(1);
}

static void Launcher_DrawSaveTopbar(void)
{
    u8 dt[3];
    char clock[16];
    Launcher_DrawTopbarName(SD_list);
    rtc_enable(); rtc_gettime(dt); rtc_disenable();
    Launcher_FormatClock(clock, sizeof(clock), UNBCD(dt[0] & 0x3F),
        UNBCD(dt[1] & 0x7F), UNBCD(dt[2] & 0x7F));
    DrawHZText12(clock, 0, 189, 3, gl_color_topbar_text, 1);
}

#endif
