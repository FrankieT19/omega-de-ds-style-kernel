#ifndef DS_STYLE_START_SELECTION_MOTION_H
#define DS_STYLE_START_SELECTION_MOTION_H

typedef struct
{
    LauncherStartCorners from, to, current;
    LauncherStartCornerSave saved;
    u32 frame, selected;
    u32 active;
} LauncherStartMotion;

/* Dedicated EWRAM state: not the CPU stack or shared artwork workspace. */
static LauncherStartMotion launcher_start_motion_storage EWRAM_BSS;
static LauncherStartMotion *launcher_start_motion;

static void Launcher_StartMotionBegin(u32 old_selected, u32 selected)
{
    LauncherStartMotion *motion = launcher_start_motion;
    if(motion->active)
    {
        Launcher_RestoreSavedStartCorners(&motion->saved);
        motion->from = motion->current;
    }
    else
    {
        Launcher_GetStartCornersForItem(old_selected, &motion->from);
        Launcher_RestoreStartCorners(old_selected);
    }
    Launcher_GetStartCornersForItem(selected, &motion->to);
    motion->current = motion->from;
    motion->frame = 0;
    motion->selected = selected;
    motion->active = 1;
    Launcher_SaveStartCornersUnder(&motion->current, &motion->saved);
    Launcher_DrawStartCornersAtPositions(&motion->current, gl_color_selectBG_sd);
}

static void Launcher_StartMotionTick(void)
{
    /* Fast initial travel, then a soft landing over ten display refreshes.
       Navigation changes the destination immediately, including mid-flight. */
    static const u16 progress[] = {69, 125, 169, 203, 226, 241, 250, 254, 255, 256};
    LauncherStartMotion *motion = launcher_start_motion;
    int corner, step;
    if(!motion || !motion->active) return;
    Launcher_RestoreSavedStartCorners(&motion->saved);
    step = progress[motion->frame++];
    for(corner = 0; corner < 4; corner++)
    {
        int dx = motion->to.x[corner] - motion->from.x[corner];
        int dy = motion->to.y[corner] - motion->from.y[corner];
        motion->current.x[corner] = motion->from.x[corner] + ((dx * step + 128) >> 8);
        motion->current.y[corner] = motion->from.y[corner] + ((dy * step + 128) >> 8);
        motion->current.sx[corner] = motion->to.sx[corner];
        motion->current.sy[corner] = motion->to.sy[corner];
    }
    motion->current.arm = motion->from.arm + (((motion->to.arm - motion->from.arm) * step + 128) >> 8);
    motion->current.thickness = motion->from.thickness + (((motion->to.thickness - motion->from.thickness) * step + 128) >> 8);
    Launcher_SaveStartCornersUnder(&motion->current, &motion->saved);
    Launcher_DrawStartCornersAtPositions(&motion->current, gl_color_selectBG_sd);
    if(motion->frame == sizeof(progress) / sizeof(progress[0]))
    {
        Launcher_RestoreSavedStartCorners(&motion->saved);
        motion->active = 0;
        Launcher_DrawStartCorners(motion->selected, motion->selected);
    }
}

static void Launcher_StartMotionSettle(void)
{
    LauncherStartMotion *motion = launcher_start_motion;
    if(!motion || !motion->active) return;
    Launcher_RestoreSavedStartCorners(&motion->saved);
    motion->current = motion->to;
    motion->active = 0;
    Launcher_DrawStartCorners(motion->selected, motion->selected);
}

#endif
