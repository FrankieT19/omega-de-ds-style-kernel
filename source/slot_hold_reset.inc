/* Use the game's frame IRQ where available, or measure elapsed scanlines
   between its other IRQs. Never claim a timer or change the game's IRQ mask.
   The existing scratch word holds a tag, the last scanline, and elapsed lines. */
.macro SLOT_HOLD_TICK scratch, enabled, reset_target
    adrl r12,\scratch
    ldr r12,[r12]
    adrl r3,\enabled
    ldr r3,[r3]
    cmp r3,#0
    beq .Lhold_clear\@
    tst r2,r3
    bne .Lhold_clear\@
    adrl r3,RTS_Reset_key
    ldr r3,[r3]
    bic r3,r2,r3
    lsls r3,r3,#22
    beq .Lhold_clear\@
    stmfd sp!,{r1,r4-r5}
    ldr r4,[r12,#0x7c]
    mov r5,r4,lsr #24
    cmp r5,#0x53
    movne r4,#0x00ff0000
    mov r5,r4,lsl #16
    mov r5,r5,lsr #16

    ldrh r3,[r0,#4]
    tst r3,#8
    tstne r1,#1
    bne .Lhold_vblank\@
    tst r3,#32
    tstne r1,#4
    bne .Lhold_vcount\@

    ldrh r3,[r0,#6]
    and r4,r4,#0x00ff0000
    mov r4,r4,lsr #16
    cmp r4,#255
    beq .Lhold_store\@
    subs r4,r3,r4
    addmi r4,r4,#228
    add r5,r5,r4
    b .Lhold_store\@

.Lhold_vblank\@:
    tst r1,#0x10000
    b .Lhold_frame\@
.Lhold_vcount\@:
    tst r1,#0x40000
.Lhold_frame\@:
    addne r5,r5,#228
    mov r3,#255
.Lhold_store\@:
    /* 180 frames = 41040 scanlines (about 3 seconds). */
    mov r4,#0xa000
    add r4,r4,#80
    cmp r5,r4
    bhs .Lhold_reset\@
    orr r5,r5,r3,lsl #16
    orr r5,r5,#0x53000000
    str r5,[r12,#0x7c]
    ldmfd sp!,{r1,r4-r5}
    b .Lhold_exit\@
.Lhold_reset\@:
    mov r3,#0
    str r3,[r12,#0x7c]
    ldmfd sp!,{r1,r4-r5}
    b \reset_target
.Lhold_clear\@:
    mov r3,#0
    str r3,[r12,#0x7c]
.Lhold_exit\@:
.endm
