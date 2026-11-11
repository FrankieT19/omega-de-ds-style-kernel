#ifndef DS_STYLE_RETURN_INPUT_H
#define DS_STYLE_RETURN_INPUT_H

static u8 launcher_return_select_held;

static u16 Launcher_ReturnInputMask(u16 held)
{
    if(!launcher_return_select_held) return 0;
    /* The release is part of the exit gesture, not a launcher tap. */
    if(!(held & KEY_SELECT)) launcher_return_select_held = 0;
    return KEY_SELECT;
}

#endif
