/* Animation scratch aliases the inactive list cache and scaled-art workspace.
   Raw artwork remains in its established three cache slots throughout motion. */
#ifndef DS_STYLE_CAROUSEL_MOTION_H
#define DS_STYLE_CAROUSEL_MOTION_H
typedef struct { int x, y, w, h; } LauncherMotionRect;
typedef struct { const u16 *pixels; int w, h; u32 icon; } LauncherMotionImage;
#define LAUNCHER_MOTION_FRAME ((u16*)(pReadCache + 0x200))
#define LAUNCHER_MOTION_MAX_PIXELS ((0x10000 - 0x200) / 2)
typedef struct
{
    LauncherMotionImage images[4];
    LauncherMotionRect from[4], to[4], current[4];
    LauncherThumbCache incoming;
    u16 *background_rows[160];
} LauncherMotionWork;
#define LAUNCHER_MOTION_WORK ((LauncherMotionWork*)((u8*)&launcher_thumbnail_workspace + 0x4C40))
typedef char LauncherMotionIncomingFits[
    (sizeof(launcher_thumbnail_workspace) >= 0x4C40 + sizeof(LauncherMotionWork)) ? 1 : -1];

/* Bitmap mode leaves this sprite VRAM unused. Execute the compositor here:
   its 16-bit bus is substantially faster than EWRAM instruction fetches. All
   calls out of this copied section use absolute long-call targets. */
#define LAUNCHER_MOTION_CODE_ADDRESS 0x06014200
#define LAUNCHER_MOTION_FAST ARM_CODE __attribute__((section("motion_code"), aligned(4), noinline, optimize("O2")))
extern const u8 __start_motion_code[], __stop_motion_code[];
static u32 Launcher_MotionInstallCode(void)
{
    u32 size = __stop_motion_code - __start_motion_code;
    if(size > 0x18000 - LAUNCHER_MOTION_CODE_ADDRESS) return 0;
    dmaCopy(__start_motion_code, (void*)LAUNCHER_MOTION_CODE_ADDRESS, size);
    return 1;
}
static int __attribute__((noinline, long_call)) Launcher_MotionDivide(int a, int b) { return a / b; }
static u16 __attribute__((noinline, long_call)) Launcher_MotionBorderColour(u32 selected)
{
    return Launcher_ArtBorderColourEx(selected);
}

static inline __attribute__((always_inline)) int Launcher_MotionInset(int row, int height)
{
    static const u8 curve[5] = {5, 3, 2, 1, 1};
    if(!Launcher_RoundedCornersForCarousel()) return 0;
    if(row >= height / 2) row = height - 1 - row;
    return row < 5 ? curve[row] : 0;
}

static LauncherMotionRect Launcher_MotionSlot(int slot, u32 view)
{
    LauncherMotionRect r;
    if(view == LAUNCHER_VIEW_HORIZONTAL)
    {
        r.w = slot && !launcher_carousel_equal_sides ? LAUNCHER_HORZ_SIDE_W : LAUNCHER_HORZ_THUMB_W;
        r.h = slot && !launcher_carousel_equal_sides ? LAUNCHER_HORZ_SIDE_H : LAUNCHER_HORZ_THUMB_H;
        r.x = LAUNCHER_HORZ_THUMB_X;
        r.y = LAUNCHER_HORZ_THUMB_Y;
        if(slot)
        {
            r.x = slot < 0 ? LAUNCHER_HORZ_LEFT_X : LAUNCHER_HORZ_RIGHT_X;
            r.y = Launcher_HorizontalSideY(slot < 0 ? LAUNCHER_HORZ_LEFT_Y : LAUNCHER_HORZ_RIGHT_Y);
            if(launcher_carousel_equal_sides)
            {
                r.x = slot < 0 ? LAUNCHER_HORZ_THUMB_X - r.w - 5 : LAUNCHER_HORZ_THUMB_X + r.w + 5;
                r.y = LAUNCHER_HORZ_THUMB_Y;
            }
            if(slot < -1) r.x -= r.w + 5;
            if(slot > 1) r.x += r.w + 5;
        }
    }
    else
    {
        r.w = slot && !launcher_carousel_equal_sides ? LAUNCHER_VERT_PREV_W : LAUNCHER_VERT_THUMB_W;
        r.h = slot && !launcher_carousel_equal_sides ? LAUNCHER_VERT_PREV_H : LAUNCHER_VERT_THUMB_H;
        r.x = LAUNCHER_VERT_THUMB_X;
        r.y = LAUNCHER_VERT_THUMB_Y;
        if(slot)
        {
            r.x = Launcher_VerticalSideX(slot < 0 ? LAUNCHER_VERT_PREV_X : LAUNCHER_VERT_NEXT_X, r.w);
            r.y = slot < 0 ? LAUNCHER_VERT_PREV_Y : LAUNCHER_VERT_NEXT_Y;
            if(launcher_carousel_equal_sides)
            {
                r.x = LAUNCHER_VERT_THUMB_X;
                r.y = slot < 0 ? LAUNCHER_VERT_THUMB_Y - r.h - 6 : LAUNCHER_VERT_THUMB_Y + r.h + 6;
            }
            if(slot < -1) r.y -= r.h + 6;
            if(slot > 1) r.y += r.h + 6;
        }
    }
    if(launcher_thumbnail_style == LAUNCHER_THUMB_STYLE_BOX)
    {
        r.x += (r.w - r.h) / 2;
        r.w = r.h;
    }
    return r;
}

static LauncherMotionImage Launcher_MotionImage(const LauncherThumbCache *cache, u32 index)
{
    LauncherEntryInfo info;
    LauncherMotionImage image = {0, 0, 0, 0};
    if(cache && !cache->valid) return image;
    memset(&info, 0, sizeof(info));
    Launcher_GetEntryInfo(index, &info);
    if(!info.name) return image;
    if(cache && cache->valid && cache->has_thumbnail)
    {
        image.pixels = (const u16*)cache->thumb_data;
        image.w = Launcher_ThumbnailSourceWidth();
        image.h = Launcher_ThumbnailSourceHeight();
    }
    else if(Launcher_ShouldUsePreviewPanel(&info))
    {
        image.pixels = Launcher_NotFoundImage();
        image.w = Launcher_NotFoundWidth();
        image.h = Launcher_NotFoundHeight();
    }
    else
    {
        image.pixels = Launcher_IsNORPage() ? (const u16*)gImage_icon_nor :
            (info.is_folder ? (const u16*)gImage_icon_folder : Launcher_GetFileIcon(info.name));
        image.w = 16; image.h = 14; image.icon = 1;
    }
    return image;
}

static LauncherMotionRect Launcher_MotionImageRect(LauncherMotionRect r, u32 icon, int slot)
{
    if(icon)
    {
        int w = (!slot || launcher_carousel_equal_sides) ? 32 : 16;
        int h = (!slot || launcher_carousel_equal_sides) ? 28 : 14;
        r.x += (r.w - w) / 2; r.y += (r.h - h) / 2;
        r.w = w; r.h = h;
    }
    return r;
}

static int Launcher_MotionLerp(int a, int b, int p)
{
    return (a * (256 - p) + b * p + 128) >> 8;
}

static LauncherMotionRect Launcher_MotionBetween(LauncherMotionRect a, LauncherMotionRect b, int p)
{
    LauncherMotionRect r;
    r.x = Launcher_MotionLerp(a.x, b.x, p); r.y = Launcher_MotionLerp(a.y, b.y, p);
    r.w = Launcher_MotionLerp(a.w, b.w, p); r.h = Launcher_MotionLerp(a.h, b.h, p);
    return r;
}

static LauncherMotionRect Launcher_MotionRegion(u32 view)
{
    LauncherMotionRect region = {240, 160, 0, 0};
    int x1 = 0, y1 = 0, slot;
    for(slot = -1; slot <= 1; slot++)
    {
        LauncherMotionRect r = Launcher_MotionSlot(slot, view);
        if(r.x - 1 < region.x) region.x = r.x - 1;
        if(r.y - 1 < region.y) region.y = r.y - 1;
        if(r.x + r.w + 1 > x1) x1 = r.x + r.w + 1;
        if(r.y + r.h + 1 > y1) y1 = r.y + r.h + 1;
    }
    if(view == LAUNCHER_VIEW_HORIZONTAL) { region.x = 0; x1 = 240; }
    else { region.y = 19; y1 = 160; }
    if(region.x < 0) region.x = 0;
    if(region.y < 19) region.y = 19;
    if(x1 > 240) x1 = 240;
    if(y1 > 160) y1 = 160;
    region.w = x1 - region.x; region.h = y1 - region.y;
    return region;
}

static u32 Launcher_MotionRegionValid(LauncherMotionRect r, u32 view)
{
    int stride = r.w * 2;
    int background_start = (0x4C40 + sizeof(LauncherMotionWork) + 3) & ~3;
    if(r.w <= 0 || r.h <= 0 || r.w * r.h > LAUNCHER_MOTION_MAX_PIXELS) return 0;
    if((0x10000 - 0x200 - r.w * r.h * 2) / stride +
       ((int)sizeof(launcher_thumbnail_workspace) - background_start) / stride +
       (0x20000 - 0x1E400) / stride < r.h) return 0;
    /* Overlapping custom layouts retain their established renderer. */
    if(view == LAUNCHER_VIEW_HORIZONTAL && r.y + r.h > LAUNCHER_HORZ_TITLE_Y - 1) return 0;
    if(view == LAUNCHER_VIEW_VERTICAL && r.x + r.w > LAUNCHER_VERT_TITLE_X - 1) return 0;
    return 1;
}

static void Launcher_MotionPrepareBackground(LauncherMotionRect r)
{
    const u16 *bg = Launcher_GetBGImage();
    int row, stride = r.w * 2;
    int low_start = 0x200 + r.w * r.h * 2;
    int low_rows = (0x10000 - low_start) / stride;
    int middle_start = (0x4C40 + sizeof(LauncherMotionWork) + 3) & ~3;
    int middle_rows = ((int)sizeof(launcher_thumbnail_workspace) - middle_start) / stride;
    for(row = 0; row < r.h; row++)
    {
        u16 *dst;
        if(row < low_rows) dst = (u16*)(pReadCache + low_start) + row * r.w;
        else if(row < low_rows + middle_rows)
            dst = (u16*)((u8*)&launcher_thumbnail_workspace + middle_start) + (row - low_rows) * r.w;
        else dst = (u16*)(pReadCache + 0x1E400) + (row - low_rows - middle_rows) * r.w;
        LAUNCHER_MOTION_WORK->background_rows[row] = dst;
        dmaCopy((void*)(bg + (r.y + row) * 240 + r.x), dst, stride);
    }
}

static void LAUNCHER_MOTION_FAST Launcher_MotionBackgroundCode(
    LauncherMotionRect r, const LauncherMotionRect *rects, const LauncherMotionImage *images, int count)
{
    int row, i, j;
    for(row = 0; row < r.h; row++)
    {
        int starts[4], ends[4], spans = 0, at = 0;
        u16 *source = LAUNCHER_MOTION_WORK->background_rows[row];
        u16 *dest = LAUNCHER_MOTION_FRAME + row * r.w;
        /* Opaque artwork will overwrite these spans. Only restore exposed
           background, including the exact rounded corners and transparent icons. */
        for(i = 0; i < count; i++)
        {
            int y = r.y + row - rects[i].y, a, b, inset;
            if(!images[i].pixels || images[i].icon || y < 0 || y >= rects[i].h) continue;
            inset = Launcher_MotionInset(y, rects[i].h);
            a = rects[i].x + inset - r.x;
            b = rects[i].x + rects[i].w - inset - r.x;
            if(a < 0) a = 0;
            if(b > r.w) b = r.w;
            if(a >= b) continue;
            j = spans;
            while(j && starts[j - 1] > a)
            { starts[j] = starts[j - 1]; ends[j] = ends[j - 1]; j--; }
            starts[j] = a; ends[j] = b; spans++;
        }
        for(i = 0; i < spans; i++)
        {
            if(starts[i] > at) dmaCopy(source + at, dest + at, (starts[i] - at) * 2);
            if(ends[i] > at) at = ends[i];
        }
        if(at < r.w) dmaCopy(source + at, dest + at, (r.w - at) * 2);
    }
}

static inline __attribute__((always_inline)) void Launcher_MotionPixel(LauncherMotionRect clip, int x, int y, u16 colour)
{
    if(x >= clip.x && x < clip.x + clip.w && y >= clip.y && y < clip.y + clip.h)
        LAUNCHER_MOTION_FRAME[(y - clip.y) * clip.w + x - clip.x] = colour;
}

static void IWRAM_CODE ARM_CODE __attribute__((noinline, optimize("O3"))) Launcher_MotionCopyPixels(const u16 *source, u16 *dest,
    const u16 *map, int count, u32 transparent)
{
    const u8 *bytes = (const u8*)source;
    if(!transparent)
    {
        while(count >= 4)
        {
            dest[0] = *(const u16*)(bytes + map[0]);
            dest[1] = *(const u16*)(bytes + map[1]);
            dest[2] = *(const u16*)(bytes + map[2]);
            dest[3] = *(const u16*)(bytes + map[3]);
            dest += 4; map += 4; count -= 4;
        }
        while(count-- > 0) *dest++ = *(const u16*)(bytes + *map++);
        return;
    }
    while(count-- > 0)
    {
        u16 pixel = *(const u16*)(bytes + *map++);
        if(pixel) *dest = pixel;
        dest++;
    }
}

/* Sampling maps keep division and corner tests outside the pixel loop. */
static void LAUNCHER_MOTION_FAST Launcher_MotionBlitCode(LauncherMotionRect clip, LauncherMotionRect r,
    LauncherMotionImage image, u32 selected)
{
    /* Mode 3 reserves the upper 16 KiB of VRAM for sprites. DS Style uses no
       sprites, so a 480-byte sampling table here avoids slow EWRAM byte reads. */
    u16 *map = (u16*)0x06014000;
    int row, col, x0, x1, y0, y1, sx = 0, step_x, sy, step_y;
    if(!image.pixels || r.w <= 0 || r.h <= 0 || r.w > 240) return;
    if(r.x + r.w < clip.x || r.x > clip.x + clip.w ||
       r.y + r.h < clip.y || r.y > clip.y + clip.h) return;
    step_x = Launcher_MotionDivide(image.w << 16, r.w); step_y = Launcher_MotionDivide(image.h << 16, r.h);
    for(col = 0; col < r.w; col++, sx += step_x) map[col] = (sx >> 16) * 2;
    y0 = clip.y > r.y ? clip.y - r.y : 0;
    y1 = clip.y + clip.h - r.y;
    if(y1 > r.h) y1 = r.h;
    sy = y0 * step_y;
    for(row = y0; row < y1; row++, sy += step_y)
    {
        const u16 *source = image.pixels + (sy >> 16) * image.w;
        u16 *dest = LAUNCHER_MOTION_FRAME + (r.y + row - clip.y) * clip.w;
        int inset = image.icon ? 0 : Launcher_MotionInset(row, r.h);
        x0 = inset; x1 = r.w - inset;
        if(r.x + x0 < clip.x) x0 = clip.x - r.x;
        if(r.x + x1 > clip.x + clip.w) x1 = clip.x + clip.w - r.x;
        if(x1 > x0 && !image.icon && r.w == image.w && r.h == image.h)
            dmaCopy((void*)(source + x0), dest + r.x + x0 - clip.x, (x1 - x0) * 2);
        else if(x1 > x0)
            Launcher_MotionCopyPixels(source, dest + r.x + x0 - clip.x,
                map + x0, x1 - x0, image.icon);
    }
    if(!image.icon && launcher_art_border_mode)
    {
        u16 colour = Launcher_MotionBorderColour(selected);
        int inset;
        /* Four-connected dilation: outline pixels are adjacent to the exact
           visible rounded image, including every step of its corner curve. */
        for(row = -1; row <= r.h; row++)
        {
            if(row == -1 || row == r.h)
            {
                inset = Launcher_MotionInset(0, r.h);
                /* Square borders include the four outer corner pixels. */
                for(col = inset ? inset : -1; col < r.w - inset + (inset == 0); col++)
                    Launcher_MotionPixel(clip, r.x + col, r.y + row, colour);
            }
            else
            {
                int inner = Launcher_MotionInset(row, r.h), outer = inner - 1;
                if(row && Launcher_MotionInset(row - 1, r.h) < outer) outer = Launcher_MotionInset(row - 1, r.h);
                if(row + 1 < r.h && Launcher_MotionInset(row + 1, r.h) < outer) outer = Launcher_MotionInset(row + 1, r.h);
                for(col = outer; col < inner; col++)
                {
                    Launcher_MotionPixel(clip, r.x + col, r.y + row, colour);
                    Launcher_MotionPixel(clip, r.x + r.w - 1 - col, r.y + row, colour);
                }
            }
        }
    }
}

static void Launcher_MotionBackground(LauncherMotionRect r, const LauncherMotionRect *rects,
    const LauncherMotionImage *images, int count)
{
    void (*render)(LauncherMotionRect, const LauncherMotionRect*, const LauncherMotionImage*, int) =
        (void*)(LAUNCHER_MOTION_CODE_ADDRESS + (const u8*)Launcher_MotionBackgroundCode - __start_motion_code);
    render(r, rects, images, count);
}
static void Launcher_MotionBlit(LauncherMotionRect clip, LauncherMotionRect r, LauncherMotionImage image, u32 selected)
{
    void (*render)(LauncherMotionRect, LauncherMotionRect, LauncherMotionImage, u32) =
        (void*)(LAUNCHER_MOTION_CODE_ADDRESS + (const u8*)Launcher_MotionBlitCode - __start_motion_code);
    render(clip, r, image, selected);
}

static void __attribute__((noinline, long_call)) Launcher_MotionWaitVBlank(void)
{
    VBlankIntrWait();
}
static void IWRAM_CODE ARM_CODE __attribute__((noinline, optimize("O2"))) Launcher_MotionPresent(LauncherMotionRect r)
{
    int row;
    Launcher_MotionWaitVBlank();
    if(r.x == 0 && r.w == 240)
    {
        DMA3COPY(LAUNCHER_MOTION_FRAME, VideoBuffer + r.y * 240, DMA32 | (r.w * r.h / 2));
    }
    else
        for(row = 0; row < r.h; row++)
            dmaCopy(LAUNCHER_MOTION_FRAME + row * r.w, VideoBuffer + (r.y + row) * 240 + r.x, r.w * 2);
    UIAudio_Update();
}

static void Launcher_MotionPollInput(u16 *previous)
{
    u16 down, held, return_mask;
    scanKeys();
    held = keysHeld();
    return_mask = Launcher_ReturnInputMask(held);
    down = keysDown() & ~return_mask;
    launcher_motion_released |= keysUp() & ~return_mask;
    if(down && launcher_motion_input_count < 4) launcher_motion_input[launcher_motion_input_count++] = down;
    *previous = held & ~return_mask;
}

static u16 Launcher_MotionTakeInput(void)
{
    u16 result;
    u32 i;
    if(!launcher_motion_input_count) return 0;
    result = launcher_motion_input[0];
    for(i = 1; i < launcher_motion_input_count; i++) launcher_motion_input[i - 1] = launcher_motion_input[i];
    launcher_motion_input_count--;
    return result;
}

static u32 Launcher_DrawMotionRest(void)
{
    LauncherMotionRect clip;
    const LauncherThumbCache *cache[3] = {&launcher_cache_prev, &launcher_cache_selected, &launcher_cache_next};
    u32 view = Launcher_ActiveViewMode();
    int slot;
    if(!launcher_carousel_animations) return 0;
    if(launcher_motion_finished == launcher_cache_center_index)
    {
        launcher_motion_finished = 0xFFFFFFFF;
        return 1;
    }
    clip = Launcher_MotionRegion(view);
    if(!Launcher_MotionRegionValid(clip, view)) return 0;
    if(!Launcher_MotionInstallCode()) return 0;
    UIAudio_StopForSharedBufferUse();
    Launcher_MotionPrepareBackground(clip);
    for(slot = -1; slot <= 1; slot++)
    {
        LAUNCHER_MOTION_WORK->images[slot + 1] = Launcher_MotionImage(cache[slot + 1], cache[slot + 1]->absolute_index);
        LAUNCHER_MOTION_WORK->current[slot + 1] = Launcher_MotionImageRect(Launcher_MotionSlot(slot, view),
            LAUNCHER_MOTION_WORK->images[slot + 1].icon, slot);
    }
    Launcher_MotionBackground(clip, LAUNCHER_MOTION_WORK->current, LAUNCHER_MOTION_WORK->images, 3);
    for(slot = -1; slot <= 1; slot += 2)
    {
        LauncherMotionImage image = Launcher_MotionImage(cache[slot + 1], cache[slot + 1]->absolute_index);
        LauncherMotionRect r = Launcher_MotionImageRect(Launcher_MotionSlot(slot, view), image.icon, slot);
        Launcher_MotionBlit(clip, r, image, 0);
    }
    {
        LauncherMotionImage image = Launcher_MotionImage(cache[1], cache[1]->absolute_index);
        LauncherMotionRect r = Launcher_MotionImageRect(Launcher_MotionSlot(0, view), image.icon, 0);
        Launcher_MotionBlit(clip, r, image, 1);
    }
    Launcher_MotionPresent(clip);
    return 1;
}

static void Launcher_AnimateCarouselMove(int move, u32 new_index)
{
    /* An integer ease-out trajectory; no floating point or SD reads per frame. */
    static const u16 progress[] = {85, 158, 211, 243, 256};
    const LauncherThumbCache *cache[3] = {&launcher_cache_prev, &launcher_cache_selected, &launcher_cache_next};
    LauncherMotionImage *images = LAUNCHER_MOTION_WORK->images;
    LauncherMotionRect *from = LAUNCHER_MOTION_WORK->from, *to = LAUNCHER_MOTION_WORK->to, clip;
    LauncherThumbCache *incoming = &LAUNCHER_MOTION_WORK->incoming;
    u32 view = Launcher_ActiveViewMode(), total = Launcher_GetTotalEntries(), expected;
    int item, frame;
    u16 physical = (~REG_KEYINPUT) & 0x3FF;
    if(!launcher_carousel_animations || total <= 1 ||
        (view != LAUNCHER_VIEW_HORIZONTAL && view != LAUNCHER_VIEW_VERTICAL)) return;
    expected = move > 0 ? (launcher_cache_center_index + 1) % total :
        (launcher_cache_center_index + total - 1) % total;
    if(new_index != expected || !Launcher_ThumbCachePointersValid()) return;
    clip = Launcher_MotionRegion(view);
    if(!Launcher_MotionRegionValid(clip, view)) return;
    if(!Launcher_MotionInstallCode()) return;
    UIAudio_StopForSharedBufferUse();
    for(item = 0; item < 3; item++)
    {
        int slot = item - 1;
        images[item] = Launcher_MotionImage(cache[item], cache[item]->absolute_index);
        from[item] = Launcher_MotionImageRect(Launcher_MotionSlot(slot, view), images[item].icon, slot);
        to[item] = Launcher_MotionImageRect(Launcher_MotionSlot(slot - move, view), images[item].icon, slot - move);
    }
    memset(incoming, 0, sizeof(*incoming));
    incoming->absolute_index = 0xFFFFFFFF;
    incoming->thumb_data = (u8*)&launcher_thumbnail_workspace + LAUNCHER_THUMB_BMP_HEADER;
    if(move > 0 && (new_index + 1 < total || launcher_wrap_around)) incoming->absolute_index = (new_index + 1) % total;
    if(move < 0 && (new_index || launcher_wrap_around)) incoming->absolute_index = (new_index + total - 1) % total;
    if(incoming->absolute_index != 0xFFFFFFFF)
    {
        incoming->valid = 1;
        for(item = 0; item < 3; item++)
            if(cache[item]->valid && cache[item]->absolute_index == incoming->absolute_index) break;
        if(item < 3)
        {
            incoming->has_thumbnail = cache[item]->has_thumbnail;
            if(incoming->has_thumbnail)
                dmaCopy(cache[item]->thumb_data, incoming->thumb_data, Launcher_ThumbnailSourceWidth() * Launcher_ThumbnailSourceHeight() * 2);
        }
        else incoming->has_thumbnail = Launcher_LoadThumbDataForIndex(incoming->absolute_index, incoming->thumb_data);
    }
    Launcher_MotionPollInput(&physical);
    images[3] = Launcher_MotionImage(incoming, incoming->absolute_index);
    from[3] = Launcher_MotionImageRect(Launcher_MotionSlot(move * 2, view), images[3].icon, move * 2);
    to[3] = Launcher_MotionImageRect(Launcher_MotionSlot(move, view), images[3].icon, move);
    Launcher_MotionPrepareBackground(clip);
    launcher_carousel_art_draw = 1;
    for(frame = 0; frame < (int)(sizeof(progress) / sizeof(progress[0])); frame++)
    {
        for(item = 0; item < 4; item++)
            LAUNCHER_MOTION_WORK->current[item] = Launcher_MotionBetween(from[item], to[item], progress[frame]);
        Launcher_MotionBackground(clip, LAUNCHER_MOTION_WORK->current, images, 4);
        /* The item moving into focus is composited last. */
        for(item = 0; item < 4; item++)
        {
            int focus = move > 0 ? 2 : 0;
            int i = item == 3 ? focus : (item == focus ? 3 : item);
            LauncherMotionRect r = LAUNCHER_MOTION_WORK->current[i];
            u32 selected = progress[frame] < 128 ? i == 1 : i == focus;
            Launcher_MotionBlit(clip, r, images[i], selected);
        }
        Launcher_MotionPresent(clip);
        Launcher_MotionPollInput(&physical);
    }
    launcher_carousel_art_draw = 0;
    launcher_motion_finished = new_index;
    launcher_motion_incoming_ready = incoming->valid;
}

/* A short press-and-release pulse, using the same compositor and scratch RAM.
   Contracting instead of enlarging preserves the side-art gap and title area. */
static void Launcher_AnimateCarouselLaunch(void)
{
    static const u16 scale[] = {250, 240, 232, 238, 250, 256};
    const LauncherThumbCache *cache[3] = {&launcher_cache_prev, &launcher_cache_selected, &launcher_cache_next};
    LauncherMotionImage *images = LAUNCHER_MOTION_WORK->images;
    LauncherMotionRect *rects = LAUNCHER_MOTION_WORK->current, clip, selected;
    u32 view = Launcher_ActiveViewMode();
    int item, frame;
    if(!launcher_carousel_animations || !Launcher_MotionLaunchPage() ||
       (view != LAUNCHER_VIEW_HORIZONTAL && view != LAUNCHER_VIEW_VERTICAL) ||
       !launcher_cache_selected.valid || !Launcher_ThumbCachePointersValid()) return;
    clip = Launcher_MotionRegion(view);
    if(!Launcher_MotionRegionValid(clip, view) || !Launcher_MotionInstallCode()) return;
    UIAudio_StopForSharedBufferUse();
    Launcher_MotionPrepareBackground(clip);
    for(item = 0; item < 3; item++)
    {
        images[item] = Launcher_MotionImage(cache[item], cache[item]->absolute_index);
        rects[item] = Launcher_MotionImageRect(Launcher_MotionSlot(item - 1, view), images[item].icon, item - 1);
    }
    selected = rects[1];
    for(frame = 0; frame < (int)(sizeof(scale) / sizeof(scale[0])); frame++)
    {
        rects[1].w = (selected.w * scale[frame] + 128) >> 8;
        rects[1].h = (selected.h * scale[frame] + 128) >> 8;
        rects[1].x = selected.x + (selected.w - rects[1].w) / 2;
        rects[1].y = selected.y + (selected.h - rects[1].h) / 2;
        Launcher_MotionBackground(clip, rects, images, 3);
        for(item = 0; item < 3; item++)
        {
            int i = item == 2 ? 1 : item == 1 ? 2 : 0;
            Launcher_MotionBlit(clip, rects[i], images[i], i == 1);
        }
        Launcher_MotionPresent(clip);
    }
    /* Let the released selection settle without repainting or stretching
       the pause according to artwork-rendering cost. Six refreshes ~100 ms. */
    for(frame = 0; frame < 6; frame++) Launcher_MotionWaitVBlank();
}

static void Launcher_MotionLoadIncoming(LauncherThumbCache *cache, u32 index)
{
    const LauncherThumbCache *incoming = &LAUNCHER_MOTION_WORK->incoming;
    if(launcher_motion_incoming_ready && incoming->absolute_index == index)
    {
        cache->valid = 1;
        cache->absolute_index = index;
        cache->has_thumbnail = incoming->has_thumbnail;
        if(cache->has_thumbnail)
            dmaCopy(incoming->thumb_data, cache->thumb_data,
                Launcher_ThumbnailSourceWidth() * Launcher_ThumbnailSourceHeight() * 2);
        launcher_motion_incoming_ready = 0;
    }
    else Launcher_LoadThumbCacheForIndex(cache, index);
}
#endif
