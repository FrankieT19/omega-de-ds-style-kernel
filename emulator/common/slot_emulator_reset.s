/* Same mapper and BIOS soft-return path as the native GBA addon. */
.text
.arm
.global Slot_EmulatorReset
.type Slot_EmulatorReset,%function
Slot_EmulatorReset:
    mov r0,#0x04000000
    mov r1,#0
    add r2,r0,#0x200
    strh r1,[r2,#8]
    strh r1,[r0,#0xba]
    strh r1,[r0,#0xc6]
    strh r1,[r0,#0xd2]
    strh r1,[r0,#0xde]
    ldr r1,=Slot_JagoombaBlend
    ldr r1,[r1]
    strh r1,[r0,#0x50]
    mov r1,#16
    strh r1,[r0,#0x54]
    bl Slot_JagoombaMarkReturn
    adr r1,Slot_ResetCode
    adr r3,Slot_ResetEnd
    mov r2,#0x02000000
1:  ldr r0,[r1],#4
    str r0,[r2],#4
    cmp r1,r3
    blt 1b
    mov r0,#0x04000000
    ldrh r3,[r0,#0x50]
    str r3,[r2,#-4] @ Last word of the copied stub
    mov r0,#0x02000000
    add r0,r0,#1
    bx r0
.include "slot_reset_stub.inc"
