/* Relocatable Thumb mapper/BIOS handoff. Both callers enter fully black. */
.align 2
.thumb
Slot_ResetCode:
    mov r0,#0x20
    lsl r3,r0,#22
    lsl r0,r0,#12
    add r4,r3,r0
    add r5,r4,r0
    lsl r1,r0,#8
    add r2,r3,r1
    lsr r1,r3,#4
    sub r6,r2,r1
    lsr r1,r1,#4
    add r6,r6,r1
    sub r2,r2,r0
    sub r7,r2,r0
    mov r0,#210
    lsl r0,r0,#8
    mov r1,#21
    lsl r1,r1,#8
    strh r0,[r2]
    strh r1,[r3]
    strh r0,[r4]
    strh r1,[r5]
    lsr r0,r3,#12
    add r0,#2
    strh r0,[r6]
    strh r1,[r7]
    ldr r1,Slot_ResetFlag
    mov r0,#0
    strb r0,[r1]
    /* BIOS RAM reset forces a white display. Reset only registers, during
       VBlank, then restore mode 3 black before the next visible scanline.
       The kernel paints its complete framebuffer; bulk VRAM/OAM/palette
       clears are unnecessary here. EWRAM retains the one-shot cookie. */
    ldr r2,Slot_ResetIO
    add r2,#6
2:  ldrh r0,[r2]
    cmp r0,#160
    bhs 2b
3:  ldrh r0,[r2]
    cmp r0,#160
    blo 3b
    mov r0,#0xe0
    swi 0x01
    ldr r1,Slot_ResetIO
    ldr r0,Slot_ResetMode
    strh r0,[r1]
    add r1,#0x50
    ldr r0,Slot_ResetBlend
    strh r0,[r1]
    mov r0,#16
    strh r0,[r1,#4]
    swi 0x00
.align 2
Slot_ResetFlag: .word 0x03007ffa
Slot_ResetIO: .word 0x04000000
Slot_ResetMode: .word 0x0403
Slot_ResetBlend: .word 0xff
Slot_ResetEnd:
