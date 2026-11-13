#ifndef DS_STYLE_RETURN_INPUT_H
#define DS_STYLE_RETURN_INPUT_H

static u16 launcher_return_held;

static u16 Launcher_ReturnInputMask(u16 held)
{
    u16 mask = launcher_return_held;
    launcher_return_held &= held;
    return mask;
}

#endif
