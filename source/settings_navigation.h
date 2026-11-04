#ifndef DS_STYLE_SETTINGS_NAVIGATION_H
#define DS_STYLE_SETTINGS_NAVIGATION_H

static u32 Launcher_MenuCanMove(u32 selected, u32 total, int direction)
{
    return total > 1 && (launcher_wrap_around ||
        (direction > 0 ? selected + 1 < total : selected > 0));
}

static u32 Launcher_MenuMove(u32 selected, u32 total, int direction)
{
    if(!Launcher_MenuCanMove(selected, total, direction)) return selected;
    if(direction > 0) return selected + 1 < total ? selected + 1 : 0;
    return selected ? selected - 1 : total - 1;
}

static u32 Launcher_MenuTop(u32 selected, u32 top, u32 visible)
{
    if(selected < top) return selected;
    return selected >= top + visible ? selected - visible + 1 : top;
}

/* Preserve stored Centre=0/Left-or-Top=1 values while presenting the natural
   Left/Top, Centre, Right/Bottom order. A custom alignment remains last. */
static u32 Launcher_MenuNextAlignment(u32 value, u32 count, int direction)
{
    u32 index = value < 2 ? 1 - value : value;
    index = direction > 0 ? (index + 1) % count : (index ? index - 1 : count - 1);
    return index < 2 ? 1 - index : index;
}
#endif
