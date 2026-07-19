#include <gba_base.h>


#define MAX_BUF_LEN 6000
#define MAX_KEY_LEN 50
#define MAX_VAL_LEN 6000

#define MAX_sectionVAL_LEN 300

/* Unified Original/Enhanced .cht specification revision. */
#define ENHANCED_CHT_FORMAT_REVISION 6

/* The injected runtime table is .space 0x400: 128 address/value records. */
#define MAX_RUNTIME_CHEAT_RECORDS 128
/* DS-Style compatibility name used by launcher-side code. */
#define MAX_CHEAT_ENTRIES MAX_RUNTIME_CHEAT_RECORDS

/*
 * Maximum memory writes performed per runtime pass. Keeping this comfortably
 * below a full VBlank's budget prevents compact operations from delaying the
 * game's own interrupt handler for several milliseconds.
 */
#define MAX_RUNTIME_WRITE_WORK    512

/*
 * Enhanced .cht ROM support. ROM records are kept separate from the injected
 * 128-record runtime table so arbitrary byte-list ROM patches do not reduce
 * normal runtime-cheat capacity. Revision 6 supports independent grouped
 * options, explicit multi-select groups, and standalone name=command rows.
 */
#define MAX_ROM_CHEAT_GROUPS       32
#define MAX_ROM_CONDITION_BYTES   128
#define MAX_ROM_PATCH_BYTES       256

/*
 * Internal runtime bytecode markers. Valid GBA runtime addresses are in the
 * 0x02/0x03/0x04 ranges after expansion, so 0xF1-0xFF are reserved safely.
 * Each table slot remains the existing eight-byte address/value pair.
 */
#define CHT_RECORD_CONDITION_FLAG 0x80000000UL
#define CHT_RUNTIME_OPCODE_MASK   0xFF000000UL
#define CHT_OP_IF_EQ              0xF1000000UL
#define CHT_OP_IF_NE              0xF2000000UL
#define CHT_OP_IF_LT              0xF3000000UL
#define CHT_OP_IF_GT              0xF4000000UL
#define CHT_OP_IF_LE              0xF5000000UL
#define CHT_OP_IF_GE              0xF6000000UL
#define CHT_OP_ELSE               0xF7000000UL
#define CHT_OP_ENDIF              0xF8000000UL
#define CHT_OP_ADD                0xF9000000UL
#define CHT_OP_SUB                0xFA000000UL
#define CHT_OP_PTR                0xFB000000UL
#define CHT_OP_DATA               0xFC000000UL
#define CHT_OP_WRITE16            0xFD000000UL
#define CHT_OP_WRITE32            0xFE000000UL
#define CHT_OP_COMPACT            0xFF000000UL

/* Width and compact-operation fields stored inside the opcode word. */
#define CHT_WIDTH_SHIFT           20
#define CHT_WIDTH_MASK            0x00300000UL
#define CHT_WIDTH_8               0x00000000UL
#define CHT_WIDTH_16              0x00100000UL
#define CHT_WIDTH_32              0x00200000UL
#define CHT_COMPACT_ADDRESS_MASK  0x000FFFFFUL
#define CHT_CONDITION_MASK_FLAG   0x00800000UL
#define CHT_COMPACT_SLIDE_FLAG    0x00800000UL

#define CHT_RECORD_ENDIF          CHT_OP_ENDIF
#define CHT_MENU_NORMAL           0
#define CHT_GROUP_ONE             0
#define CHT_GROUP_MULTI           1
#define CHT_NO_SECTION            0xFFFFU
#define CHT_GROUP_COLLAPSED        0
#define CHT_GROUP_EXPANDED         1

typedef struct CHT_LINE{
	char LINEname[MAX_KEY_LEN];
	//char KEY_val[256];
	u8 is_section ;
	u8 section_val_count ;
	u8 len;
	u8 select ;
	u16 parent_section;
} FM_CHT_LINE;

typedef struct ST_entry_{	
	u32  address;		
	u32  VAL;	
} ST_entry;

/* DS Style uses a compact ROM table layout to limit added EWRAM pressure. */
typedef struct CHT_ROM_GROUP_
{
	u16 condition_start;
	u16 condition_count;
	u16 patch_start;
	u16 patch_count;
	u8 runtime_start;
	u8 runtime_count;
	u8 active;
	u8 reserved;
} CHT_ROM_GROUP;

typedef u32 (*CHT_ROM_READ_BYTE)(u32 offset, u8 *value, void *context);
typedef u32 (*CHT_ROM_WRITE_BYTE)(u32 offset, u8 value, void *context);

extern u32 gl_cheat_count;
extern u32 gl_runtime_write_work;
extern u32 gl_rom_group_count;
extern u32 gl_rom_condition_count;
extern u32 gl_rom_patch_count;

void ResetCompiledCheats(void);
void EvaluateEnhancedCheatGroups(CHT_ROM_READ_BYTE reader, void *context,
	u32 rom_size);
void ApplyActiveRomPatches(CHT_ROM_WRITE_BYTE writer, void *context,
	u32 rom_size);
u32 IsRuntimeCheatRecordActive(u32 record_index);
u32 ResolveRuntimeCheatRecordAddress(u32 record_address, u32 *full_address);
void ToggleCheatMenuEntry(u32 entry_index, u32 total_entries);
u32 GetVisibleCheatMenuCount(u32 total_entries);
u32 GetVisibleCheatMenuEntryIndex(u32 total_entries, u32 visible_index);
u32 SetCheatGroupExpanded(u32 entry_index, u32 total_entries, u32 expanded);

//int Get_KEY_val(FIL* file,char*KEY_section,char*KEY_secval,char getbuff[]);
int Show_all_KEY_val(FIL* file);
u32 Check_cht_file(TCHAR *gamefilename);
void Open_cht_file(TCHAR *gamefilename,u32 havecht);
void Trim(char s[]);
