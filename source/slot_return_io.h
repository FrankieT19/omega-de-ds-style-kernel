#ifndef SLOT_RETURN_IO_H
#define SLOT_RETURN_IO_H
#include "slot_return.h"
static unsigned slot_return_pending;
static unsigned slot_return_options;
static unsigned slot_return_request_seen;
static unsigned slot_return_save_failed;
static char slot_return_filename[100];
#ifndef SLOT_RETURN_MAILBOX
#define SLOT_RETURN_MAILBOX ((volatile uint32_t*)SLOT_RETURN_COOKIE_ADDRESS)
#endif
#if defined(__arm__) || defined(SLOT_RETURN_IO_TEST)
static void Slot_ReadReturn(void)
{
    SlotReturnRecord record;
    volatile uint32_t *mailbox=SLOT_RETURN_MAILBOX;
    uint32_t boot_cookie[4];unsigned i;
    unsigned char zero[16]={0};
    for(i=0;i<4;i++){boot_cookie[i]=mailbox[i];mailbox[i]=0;}
    SetRampage(SLOT_RETURN_PAGE);
    ReadSram(SLOT_RETURN_ADDRESS,(u8*)&record,sizeof(record));
    /* Consume even a torn/bad request, before save recovery or any SD work. */
    WriteSram(SLOT_RETURN_ADDRESS,zero,sizeof(zero));
    SetRampage(0);
    slot_return_request_seen=Slot_ReturnValid(&record);
    slot_return_pending=Slot_ReturnWarmValid(&record,boot_cookie);
    if(slot_return_pending){slot_return_options=record.options;memcpy(slot_return_filename,record.filename,sizeof(slot_return_filename));}
}
static void Slot_ArmReturn(const char *filename,unsigned platform,unsigned options)
{
    SlotReturnRecord record;
    memset(&record,0,sizeof(record));
    if(platform<3) {
        record.magic=SLOT_RETURN_MAGIC;
        record.platform=platform;record.options=options;
        snprintf(record.filename,sizeof(record.filename),"%s",filename);
        record.checksum=Slot_ReturnChecksum(&record);
    }
    SetRampage(SLOT_RETURN_PAGE);
    WriteSram(SLOT_RETURN_ADDRESS,(u8*)&record,sizeof(record));
    SetRampage(0);
}
#else
static void Slot_ReadReturn(void){slot_return_pending=0;}
static void Slot_ArmReturn(const char *filename,unsigned platform,unsigned options){(void)filename;(void)platform;(void)options;}
#endif
#endif
