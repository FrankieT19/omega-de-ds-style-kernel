/* Shared fade registers and the final, DMA-free black display. */
#ifndef SLOT_EMULATOR_FADE_H
#define SLOT_EMULATOR_FADE_H
#include <stdint.h>
static void Slot_EmulatorWaitBlank(void)
{
    volatile uint16_t *scanline=(volatile uint16_t*)0x04000006;
    while(*scanline>=160){}
    while(*scanline<160){}
}
extern unsigned Slot_JagoombaBlend;
static void Slot_EmulatorApplyFade(unsigned level)
{
    *(volatile uint16_t*)0x04000048|=0x2020;
    *(volatile uint16_t*)0x0400004a|=0x2020;
    *(volatile uint16_t*)0x04000050=Slot_JagoombaBlend;
    *(volatile uint16_t*)0x04000054=level;
}
static void Slot_EmulatorHoldBlack(void)
{
    *(volatile uint16_t*)0x04000208=0;
    *(volatile uint16_t*)0x040000ba=0;
    *(volatile uint16_t*)0x040000c6=0;
    *(volatile uint16_t*)0x040000d2=0;
    *(volatile uint16_t*)0x040000de=0;
    *(volatile uint16_t*)0x04000000=0x0403;
    Slot_EmulatorApplyFade(16);
}
/* Mode-3 fixture helper. Jagoomba uses its display IRQs to apply the same fade. */
static void Slot_EmulatorFadeBlack(void)
{
    unsigned level;
    for(level=0;level<=16;level+=2){
        Slot_EmulatorWaitBlank();
        Slot_EmulatorApplyFade(level);
    }
    Slot_EmulatorHoldBlack();
}
#endif
