reset_now:
    mov r0,#REG_BASE
    mov r1,#0
    add r2,r0,#REG_IE
    strh r1,[r2,#8] @ IME

    @ Freeze the frame on a VBlank boundary. Raster DMA must stop with
    @ the game's IRQ handler so it cannot run off its scanline tables.
    bl Slot_ReturnVBlankStart
    strh r1,[r0,#REG_DM0CNT_H]
    strh r1,[r0,#REG_DM1CNT_H]
    strh r1,[r0,#REG_DM2CNT_H]
    strh r1,[r0,#REG_DM3CNT_H]
    strh r1,[r0,#REG_SOUNDCNT_X]

    @ Keep the game's layer masks and enable darkening in every window.
    mov r2,#0x2000
    orr r2,r2,#0x20
    ldrh r3,[r0,#REG_WININ]
    orr r3,r3,r2
    strh r3,[r0,#REG_WININ]
    ldrh r3,[r0,#REG_WINOUT]
    orr r3,r3,r2
    strh r3,[r0,#REG_WINOUT]
    bl DSStyle_ReturnDark
    cmp r0,#0
    moveq r3,#0xbf
    movne r3,#0xff
    mov r0,#REG_BASE
    mov r1,#0
    strh r3,[r0,#REG_BLDCNT]
1:  strh r1,[r0,#REG_BLDY]
    bl Slot_ReturnVBlank
    add r1,r1,#2
    cmp r1,#16
    bls 1b

    @ Leave only the fully darkened backdrop during the cartridge handoff.
    mov r1,#3
    strh r1,[r0,#REG_DISPCNT]
    bl Slot_MarkReturn
    adr r1,Slot_ResetCode
    adr r3,Slot_ResetEnd
    mov r2,#0x02000000
2:  ldr r0,[r1],#4
    str r0,[r2],#4
    cmp r1,r3
    blo 2b
    mov r0,#0x04000000
    ldrh r3,[r0,#0x50]
    str r3,[r2,#-4] @ Last word of the copied stub
    mov r0,#0x02000000
    add r0,r0,#1
    bx r0

Slot_ReturnVBlank:
3:  ldrh r2,[r0,#REG_VCOUNT]
    cmp r2,#160
    bhs 3b
Slot_ReturnVBlankStart:
    ldrh r2,[r0,#REG_VCOUNT]
    cmp r2,#160
    blo Slot_ReturnVBlankStart
    bx lr

    .include "slot_reset_stub.inc"
    .arm
