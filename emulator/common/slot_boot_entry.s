/* The standard header and secondary entry branch remain unchanged. The CRT's
   first instruction branches here, before BSS setup; replay it before return.
   This controls kernel-owned frames, not a cartridge firmware loader. */
.arm
.section .text
.global Slot_BlackEntry
Slot_BlackEntry:
    ldr r0, Slot_BlackIO
    mov r1, #0
    add r2,r0,#0x200
    strh r1, [r2, #8]
    ldr r2, Slot_EntryCookie
    ldmia r2, {r2,r3}
    ldr r1, Slot_EntryMagic
    cmp r2, r1
    mvneq r1, r1
    cmpeq r3, r1
    movne r1, #0xbf
    ldreqh r1, [r0, #0x50]
    andeq r1, r1, #0x40
    orreq r1, r1, #0xbf
    strh r1, [r0, #0x50]
    mov r1, #16
    strh r1, [r0, #0x54]
    /* Game affine transforms survive SoftReset; reset BG2 before mode 3. */
    mov r1, #0x100
    str r1, [r0, #0x20]
    mov r1, r1, lsl #16
    str r1, [r0, #0x24]
    mov r1, #0
    str r1, [r0, #0x28]
    str r1, [r0, #0x2c]
    ldr r1, Slot_BlackMode
    strh r1, [r0]
    mov r0, #0x04000000  /* Replay the CRT instruction displaced by our branch. */
    ldr pc, Slot_OriginalEntry
Slot_BlackIO: .word 0x04000000
Slot_BlackMode: .word 0x0403
Slot_OriginalEntry: .word 0x12345678
Slot_EntryCookie: .word 0x0203fff0
Slot_EntryMagic: .word 0x53574a45

/* devkitARM clears all EWRAM even on a soft return. Preserve only the
   16-byte powered-session cookie around that clear, before C startup.
   A power-on BIOS clear still erases it, excluding cold boots. */
.thumb
.thumb_func
Slot_ClearWorkRam:
    push {r4-r7,lr}
    ldr r2,Slot_WarmMailbox
    ldmia r2!,{r4-r7}
    ldr r3,Slot_OriginalClear
    bl Slot_CallClear
    ldr r2,Slot_WarmMailbox
    stmia r2!,{r4-r7}
    pop {r4-r7}
    pop {r3}
    bx r3
Slot_CallClear:
    bx r3
.align 2
Slot_WarmMailbox: .word 0x0203fff0
Slot_OriginalClear: .word 0x23456789
