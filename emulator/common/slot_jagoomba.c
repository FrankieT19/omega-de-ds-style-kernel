/* slot. advance integration for the pinned Jagoomba Color v0.5 source. */
#include "includes.h"
#include "slot_return.h"
#include "slot_emulator_controls.h"
#include "slot_emulator_fade.h"
extern int main_ui_selection;
EWRAM_BSS unsigned Slot_JagoombaBlend;
EWRAM_BSS volatile unsigned Slot_JagoombaVBlanks;
EWRAM_BSS volatile unsigned Slot_JagoombaFadeRequested;
static EWRAM_BSS volatile unsigned slot_fade_clock,slot_fade_level;
static EWRAM_BSS SlotEmulatorControls slot_keys;
static EWRAM_BSS unsigned slot_hold_enabled,slot_exit_requested;
#ifdef SLOT_EMULATOR_TEST
EWRAM_BSS SlotReturnRecord Slot_JagoombaTestRecord;
EWRAM_BSS volatile unsigned Slot_JagoombaTestPage,Slot_JagoombaTestExit,Slot_JagoombaTestMenus;
EWRAM_BSS volatile unsigned Slot_JagoombaTestFadeLine,Slot_JagoombaTestFadeDMA,Slot_JagoombaTestFadeStarted;
#endif
static __attribute__((section(".ewram"),long_call,noinline)) void slot_sram_page(unsigned page)
{
#ifdef SLOT_EMULATOR_TEST
    Slot_JagoombaTestPage=page;
#else
    *(vu16*)0x09fe0000=0xd200;*(vu16*)0x08000000=0x1500;
    *(vu16*)0x08020000=0xd200;*(vu16*)0x08040000=0x1500;
    *(vu16*)0x09c00000=page;*(vu16*)0x09fc0000=0x1500;
#endif
}
static volatile unsigned char *slot_record_address(void)
{
#ifdef SLOT_EMULATOR_TEST
    return (volatile unsigned char*)&Slot_JagoombaTestRecord;
#else
    return (volatile unsigned char*)SLOT_RETURN_ADDRESS;
#endif
}
static unsigned slot_read_context(SlotReturnRecord *record)
{
    unsigned i;volatile unsigned char *source;
    slot_sram_page(SLOT_RETURN_PAGE);source=slot_record_address();
    for(i=0;i<sizeof(*record);i++)((unsigned char*)record)[i]=source[i];
    slot_sram_page(0);
    /* Validate the armed record with a local request; don't request an exit yet. */
    record->requested=SLOT_RETURN_REQUEST;record->requested_inverse=~SLOT_RETURN_REQUEST;
    return record->platform>0 && Slot_ReturnValid(record);
}
void Slot_JagoombaInit(void)
{
    SlotReturnRecord record;
    unsigned armed=slot_read_context(&record);
    slot_hold_enabled=1;
    Slot_JagoombaBlend=armed && !(record.options & DS_RETURN_DARK) ? 0xbf : 0xff;
    memset(&slot_keys,0,sizeof(slot_keys));slot_exit_requested=0;
    Slot_JagoombaVBlanks=0;main_ui_selection=-1;
    Slot_JagoombaFadeRequested=0;slot_fade_clock=0;slot_fade_level=0;
}
unsigned Slot_JagoombaInput(unsigned held)
{
    unsigned action=Slot_EmulatorKeys(&slot_keys,held,Slot_JagoombaVBlanks,slot_hold_enabled);
    if(action==2)slot_exit_requested=1;
    return action;
}
unsigned Slot_JagoombaTakeExit(void)
{
    unsigned requested=slot_exit_requested;slot_exit_requested=0;
#ifdef SLOT_EMULATOR_TEST
    Slot_JagoombaTestMenus++;
#endif
    return requested;
}
void Slot_JagoombaResume(void)
{
    memset(&slot_keys,0,sizeof(slot_keys));slot_keys.blocked=!!((~REG_P1)&12);
}
void Slot_JagoombaFadeBlack(void)
{
    unsigned level;
#ifdef SLOT_EMULATOR_TEST
    if(Slot_JagoombaTestFadeLine){
        while(*(volatile uint16_t*)0x04000006!=Slot_JagoombaTestFadeLine){}
    }
    Slot_JagoombaTestFadeDMA=REG_DM1CNT_H;
    Slot_JagoombaTestFadeStarted=1;
#endif
    /* The GB CPU is already paused by ui(). Keep the GBA display interrupts
       running: they stop/rewind HBlank DMA at the ends of frames. */
    slot_fade_clock=Slot_JagoombaVBlanks;slot_fade_level=0;
    for(level=0;level<=16;level+=2){
        unsigned previous=slot_fade_clock;
        Slot_JagoombaFadeRequested=level+1;
        Slot_EmulatorWaitBlank();
        while(slot_fade_clock==previous || slot_fade_level!=level){}
    }
    Slot_EmulatorHoldBlack();Slot_JagoombaFadeRequested=0;
}
/* Called after display IRQs, which also change blending and window registers.
   Latch a new level only on a new VBlank, so VCount IRQs cannot step it mid-row. */
void Slot_JagoombaApplyFade(void)
{
    unsigned requested=Slot_JagoombaFadeRequested,clock=Slot_JagoombaVBlanks;
    if(!requested)return;
    if(clock!=slot_fade_clock){slot_fade_level=requested-1;slot_fade_clock=clock;}
    Slot_EmulatorApplyFade(slot_fade_level);
}
void Slot_JagoombaMarkReturn(void)
{
    uint32_t magic=0,checksum=0,platform=0;unsigned i;
    volatile uint32_t *cookie=(volatile uint32_t*)SLOT_RETURN_COOKIE_ADDRESS;
    volatile unsigned char *destination;
    slot_sram_page(SLOT_RETURN_PAGE);destination=slot_record_address();
    for(i=0;i<4;i++){
        magic|=(uint32_t)destination[i]<<(i*8);
        checksum|=(uint32_t)destination[4+i]<<(i*8);
        platform|=(uint32_t)destination[16+i]<<(i*8);
    }
    if(magic!=SLOT_RETURN_MAGIC || platform<1 || platform>2){slot_sram_page(0);return;}
    /* All emulator save work is finished before this reuses the RAM tail. */
    cookie[0]=SLOT_RETURN_COOKIE_MAGIC;cookie[1]=~SLOT_RETURN_COOKIE_MAGIC;
    cookie[2]=checksum;cookie[3]=~checksum;
    for(i=0;i<4;i++){
        destination[8+i]=(unsigned char)(SLOT_RETURN_REQUEST>>(i*8));
        destination[12+i]=(unsigned char)((~SLOT_RETURN_REQUEST)>>(i*8));
    }
    slot_sram_page(0);
}
#ifdef SLOT_EMULATOR_TEST
void Slot_JagoombaTestReset(void)
{
    Slot_JagoombaMarkReturn();Slot_JagoombaTestExit++;
    /* BIOS IntrWait would re-enable game IRQs and repaint the menu. */
    for(;;)Slot_EmulatorWaitBlank();
}
#endif
