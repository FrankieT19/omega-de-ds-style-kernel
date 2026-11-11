/* SRAM byte writes only: never touch game save banks 0x00/0x10. */
.macro SLOT_MARK_RETURN
    mov r0,#0x30
    bl SetRampage
    ldr r0,=0x0e00ff00
    ldrb r1,[r0]
    ldrb r2,[r0,#1]
    orr r1,r1,r2,lsl #8
    ldrb r2,[r0,#2]
    orr r1,r1,r2,lsl #16
    ldrb r2,[r0,#3]
    orr r1,r1,r2,lsl #24
    ldr r2,=0x52544c53
    cmp r1,r2
    bne .Lreturn_unarmed\@
    ldrb r4,[r0,#4]
    ldrb r2,[r0,#5]
    orr r4,r4,r2,lsl #8
    ldrb r2,[r0,#6]
    orr r4,r4,r2,lsl #16
    ldrb r2,[r0,#7]
    orr r4,r4,r2,lsl #24
    ldr r5,=0x0203fff0
    ldr r6,=0x53574a45
    str r6,[r5]
    mvn r6,r6
    str r6,[r5,#4]
    str r4,[r5,#8]
    mvn r4,r4
    str r4,[r5,#12]
    add r0,r0,#8
    adrl r1,Slot_ReturnRequestWords
    mov r2,#8
    bl WriteSram
.Lreturn_unarmed\@:
    mov r0,#0
    bl SetRampage
.endm
