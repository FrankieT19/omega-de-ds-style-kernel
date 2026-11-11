/* One-shot game return context. SRAM page 0x30 is the existing LCD scratch
   bank; its LCD backup ends at 0x2c00, well below this 128-byte record. */
#ifndef SLOT_RETURN_H
#define SLOT_RETURN_H
#include <stdint.h>
#define SLOT_RETURN_PAGE 0x30
#define SLOT_RETURN_ADDRESS 0x0e00ff00
#define SLOT_RETURN_MAGIC 0x52544c53u
#define SLOT_RETURN_REQUEST 0x5443454au
#define SLOT_RETURN_COOKIE_ADDRESS 0x0203fff0
#define SLOT_RETURN_COOKIE_MAGIC 0x53574a45u
#define SLOT_RETURN_HOLD_ENABLED 1u
#define DS_RETURN_DARK 2u
typedef struct {
    uint32_t magic,checksum,requested,requested_inverse,platform;
    char filename[100];
    uint32_t options,reserved;
} SlotReturnRecord;
_Static_assert(sizeof(SlotReturnRecord)==128,"Return record ABI");
static uint32_t Slot_ReturnChecksum(const SlotReturnRecord *record)
{
    uint32_t hash=2166136261u;unsigned i;
    hash=(hash^record->platform)*16777619u;
    hash=(hash^record->options)*16777619u;
    for(i=0;i<sizeof(record->filename);i++)hash=(hash^(unsigned char)record->filename[i])*16777619u;
    return hash;
}
static unsigned Slot_ReturnValid(const SlotReturnRecord *record)
{
    unsigned i;
    if(record->magic!=SLOT_RETURN_MAGIC || record->platform>=3 ||
       (record->options&~(SLOT_RETURN_HOLD_ENABLED | DS_RETURN_DARK)) || record->reserved ||
       record->requested!=SLOT_RETURN_REQUEST || record->requested_inverse!=~SLOT_RETURN_REQUEST ||
       !record->filename[0] || record->filename[99] || record->checksum!=Slot_ReturnChecksum(record))return 0;
    for(i=0;record->filename[i];i++)if(record->filename[i]=='/' || record->filename[i]=='\\' ||
       (unsigned char)record->filename[i]<32)return 0;
    return 1;
}
static unsigned Slot_ReturnWarmValid(const SlotReturnRecord *record,const uint32_t cookie[4])
{
    return Slot_ReturnValid(record) && cookie[0]==SLOT_RETURN_COOKIE_MAGIC &&
           cookie[1]==~SLOT_RETURN_COOKIE_MAGIC && cookie[2]==record->checksum &&
           cookie[3]==~record->checksum;
}
#endif
