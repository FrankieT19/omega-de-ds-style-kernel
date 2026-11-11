/* Physical GBA VBlank clock, independent of emulator fast-forward/slomo. */
#ifndef SLOT_EMULATOR_CONTROLS_H
#define SLOT_EMULATOR_CONTROLS_H
typedef struct {
    unsigned previous,holding,since,blocked;
} SlotEmulatorControls;
static unsigned Slot_EmulatorKeys(SlotEmulatorControls *s,unsigned held,unsigned now,unsigned hold_enabled)
{
    unsigned chord=(held&12)==12;
    if(s->blocked){
        if(!(held&12))s->blocked=0;
        s->previous=held;s->holding=0;return 0;
    }
    if(chord && (s->previous&12)!=12){
        s->previous=held;s->holding=0;s->blocked=1;return 1;
    }
    s->previous=held;
    if(!hold_enabled || (held&12)!=4){s->holding=0;return 0;}
    if(!s->holding){s->holding=1;s->since=now;}
    if((unsigned)(now-s->since)>=179){s->holding=0;s->blocked=1;return 2;}
    return 0;
}
#endif
