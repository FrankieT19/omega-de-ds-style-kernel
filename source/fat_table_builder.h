#ifndef DS_STYLE_FAT_TABLE_BUILDER_H
#define DS_STYLE_FAT_TABLE_BUILDER_H

/* Bounds check adapted from Merkin's fragment-table fix (77ed01ca2751).
   Fragmentation issue reported by timdmackey. Existing table format is unchanged. */
#define LAUNCHER_FAT_GAME_TABLE_END 0x1F0
typedef char LauncherFatTableLayoutValid[
    (LAUNCHER_FAT_GAME_TABLE_END < FAT_table_RTS_offset &&
     FAT_table_RTS_offset < FAT_table_size &&
     !(LAUNCHER_FAT_GAME_TABLE_END & 7) && !(FAT_table_RTS_offset & 7) &&
     !(FAT_table_size & 7)) ? 1 : -1];

static inline u32 *Launcher_FatTableEnd(u32 *table, u32 mode)
{
    if(mode == 1)
        return table + LAUNCHER_FAT_GAME_TABLE_END / 4;
#ifdef FAT_table_SAV_offset
    /* Original Omega's save table must not run into its RTS table. */
    if(mode == 2)
        return table + FAT_table_RTS_offset / 4;
#endif
    return table + FAT_table_size / 4;
}

static inline u32 Launcher_FatTableAppendPair(u32 **cursor, const u32 *end,
                                           u32 first, u32 second)
{
    if(*cursor > end || end - *cursor < 2)
        return 0;
    (*cursor)[0] = first;
    (*cursor)[1] = second;
    *cursor += 2;
    return 1;
}

#endif
