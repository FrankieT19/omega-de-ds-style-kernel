@;--------------------------------------------------------------------
	@ The kernel only copies this blob into game ROM. Keep its template in ROM.
	.section .text.slot_gba_addon,"ax",%progbits

/* Palette is resolved once when the menu opens, including for NOR games. */
.macro DS_MENU_COLOUR out, pair, scratch
    adrl \scratch,spend_0x80
    ldr \scratch,[\scratch]
    ldrh \scratch,[\scratch,#0x3e]
    adrl \out,\pair
    add \out,\out,\scratch,lsl #1
    ldrh \out,[\out]
.endm
	.include "slot_hold_reset.inc"
	.include "slot_return_mark.inc"
	.global Slot_RTS_HoldEnabled
	.global Slot_RTS_Background,Slot_RTS_Panel,Slot_RTS_Ink
	.global Slot_MenuFont,Slot_MarkReturn
	.global ingamemenu_now
	.global  RTS_ReplaceIRQ_start
	.global  RTS_ReplaceIRQ_end
	.global  RTS_Return_address_L
	.global  RTS_Sleep_key
	.global  RTS_Reset_key
	@;.global  RTS_Wakeup_key
	.global	 RTS_switch
	.global	 Cheat_count
	.global	 CHEAT
	.global  no_CHEAT_end
	@;.global  ASCII

REG_BASE		= 0x4000000
REG_DISPCNT		= 0x00
REG_DISPSTAT	= 0x04
REG_VCOUNT		= 0x06
REG_BG0CNT		= 0x08
REG_BG1CNT		= 0x0A
REG_BG2CNT		= 0x0C
REG_BG3CNT		= 0x0E
REG_BG0HOFS		= 0x10
REG_BG0VOFS		= 0x12
REG_BG1HOFS		= 0x14
REG_BG1VOFS		= 0x16
REG_BG2HOFS		= 0x18
REG_BG2VOFS		= 0x1A
REG_BG3HOFS		= 0x1C
REG_BG3VOFS		= 0x1E
REG_WIN0H		= 0x40
REG_WIN1H		= 0x42
REG_WIN0V		= 0x44
REG_WIN1V		= 0x46
REG_WININ		= 0x48
REG_WINOUT		= 0x4A
REG_BLDCNT		= 0x50
REG_BLDALPHA	= 0x52
REG_BLDY		= 0x54
REG_SOUND1CNT_L	= 0x60
REG_SOUND1CNT_H	= 0x62
REG_SOUND1CNT_X	= 0x64
REG_SOUND2CNT_L	= 0x68
REG_SOUND2CNT_H	= 0x6C
REG_SOUND3CNT_L	= 0x70
REG_SOUND3CNT_H	= 0x72
REG_SOUND3CNT_X	= 0x74
REG_SOUND4CNT_L	= 0x78
REG_SOUND4CNT_H	= 0x7c
REG_SOUNDCNT_L		= 0x80
REG_SOUND2CNT_H		= 0x82
REG_SOUNDCNT_X		= 0x84
REG_SOUNDBIAS		= 0x88
REG_WAVE_RAM0_L		= 0x90
REG_FIFO_A_L	= 0xA0
REG_FIFO_A_H	= 0xA2
REG_FIFO_B_L	= 0xA4
REG_FIFO_B_H	= 0xA6
REG_DM0SAD		= 0xB0
REG_DM0DAD		= 0xB4
REG_DM0CNT_L	= 0xB8
REG_DM0CNT_H	= 0xBA
REG_DM1SAD		= 0xBC
REG_DM1DAD		= 0xC0
REG_DM1CNT_L	= 0xC4
REG_DM1CNT_H	= 0xC6
REG_DM2SAD		= 0xC8
REG_DM2DAD		= 0xCC
REG_DM2CNT_L	= 0xD0
REG_DM2CNT_H	= 0xD2
REG_DM3SAD		= 0xD4
REG_DM3DAD		= 0xD8
REG_DM3CNT_L	= 0xDC
REG_DM3CNT_H	= 0xDE
REG_TM0D		= 0x100
REG_TM0CNT		= 0x102
REG_IE			= 0x200
REG_IF			= 0x202
REG_P1			= 0x130
REG_P1CNT		= 0x132
REG_WAITCNT		= 0x204

@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.arm
RTS_ReplaceIRQ_start:
	MOV             R0, #0x4000000
	ADR             R1, RTS_irq
	STR             R1, [R0,#-4] @; 0x3FFFFFC = RTS_irq;
	adr r1,spend_0x80
	ldr r1,[r1]
	mov r2,#0
	str r2,[r1,#0x7c]
	LDR             R0, =0x12345678
	BX              R0
	.align
RTS_Return_address_L:
	.ltorg 			@;return address need modify
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
spend_0x80:
	.word 0x0203FE00   @; default
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
RTS_irq:
	LDR			R1, [R0,#0x200]
	ldr	r2,[r0,#REG_P1]
	bic 		r2,r2,#0xFF000000
	bic 		r2,r2,#0x00FF0000

	SLOT_HOLD_TICK spend_0x80, Slot_RTS_HoldEnabled, reset_now
	@;check ingamemenu
	adr 		r3,RTS_Reset_key
	ldr 		r3,[r3]
	cmp 		r2,r3
	bne			check_sleep
	@;----------------------
	adrl		r12, spend_0x80
	ldr			r12,[r12]
	stmia		r12!,{r4-r11,sp,lr} @;0x0
	mrs	 		r2,SPSR
	stmia		r12!,{r2}       @;0x4

	stmfd   SP!, {LR}  			@;0x4000000,
	bl 			ingamemenu_now
	ldmfd   SP!, {LR}
	@;----------------------
	@;check sleep
check_sleep:
	TST			R1, #0x10000
	TSTEQ		R1, #0x10000000
	LDREQ		PC, [R0,#-0xC]			@;old_interrupt_handler

	adr 		r3,RTS_Sleep_key
	ldr 		r3,[r3]
	cmp 		r2,r3
	beq 		sleep_now

	@;check cheat
	adrl 		r2,CheatONOFF
	ldr 		r2,[r2]
	ldr 		r2,[r2] @
	cmp 		r2,#1
	bne 		nocheat
	stmfd   SP!, {r4-r6}
	adrl 		r5,CHEAT
	adrl		r6,Cheat_count
	ldr			r2,[r6]
cheat_loop:
	cmp			r2,#0
	beq			cheat_loop_end
	ldmia   r5!,{r3-r4}
	strb		r4,[r3]
	sub 		r2,#0x01
	b				cheat_loop
cheat_loop_end:
	ldmfd   SP!, {r4-r6}
nocheat:
	ldr 		pc,[r0,#-(0x04000000-0x03FFFFF4)] @;to normal IRQ routine
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.include "slot_gba_return.inc"
Slot_RTS_HoldEnabled:
	.word 1
RTS_Sleep_key:
	.word 0xFB @;L+R+select
RTS_Reset_key:
	.word 0xF7 @;L+R+Start
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
DSStyle_ReturnDark:
    stmfd sp!,{r4,lr}
    mov r0,#0x30
    bl SetRampage
    ldr r1,=0x0e00ff00
    ldrb r2,[r1]
    ldrb r3,[r1,#1]
    orr r2,r2,r3,lsl #8
    ldrb r3,[r1,#2]
    orr r2,r2,r3,lsl #16
    ldrb r3,[r1,#3]
    orr r2,r2,r3,lsl #24
    ldr r3,=0x52544c53
    cmp r2,r3
    ldreqb r4,[r1,#120]
    movne r4,#0
    mov r0,#0
    bl SetRampage
    and r0,r4,#2
    mov r0,r0,lsr #1
    ldmfd sp!,{r4,pc}

DSStyle_LoadMenuColours:
    stmfd sp!,{r4,lr}
    mov r0,#0x30
    bl SetRampage
    ldr r1,=0x0e00fef0
    ldrb r2,[r1]
    ldrb r3,[r1,#1]
    orr r2,r2,r3,lsl #8
    ldr r3,=0x4453
    cmp r2,r3
    ldreqb r2,[r1,#2]
    ldreqb r3,[r1,#3]
    orreq r2,r2,r3,lsl #8
    movne r2,#0x4500
    orrne r2,r2,#0xca
    ldreqb r3,[r1,#4]
    ldreqb r4,[r1,#5]
    orreq r3,r3,r4,lsl #8
    ldrne r3,=0x7fff
    adrl r4,spend_0x80
    ldr r4,[r4]
    strh r2,[r4,#0x38]
    strh r3,[r4,#0x3a]
    mov r0,#0
    bl SetRampage
    ldmfd sp!,{r4,pc}
    .ltorg
Slot_MarkReturn:
	stmfd sp!,{r4-r6,lr}
	SLOT_MARK_RETURN
	ldmfd sp!,{r4-r6,pc}
	.align
Slot_ReturnRequestWords:
	.word 0x5443454a,0xabbcbab5
	.ltorg
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.arm
sleep_now:
	stmfd sp!,{r4-r11,lr}
	add r1,r0,#REG_SOUND1CNT_L
	@;copy and push 32 bytes
	ldmia r1!,{r2-r9}
	stmfd sp!,{r2-r9}
	@;copy and push 32 bytes
	ldmia r1!,{r2-r9}
	stmfd sp!,{r2-r9}
	@;r3 = contents of REG_SOUND3CNT_X

	@;save old io values
	add r1,r0,#REG_IE
	ldrh r4,[r1]
	ldr r5,[r0,#REG_P1]
	ldrh r6,[r0,#REG_DISPCNT]

	@;enable ints on Keypad, Game Pak
	ldr r1,=0xFFFF1000
	str r1,[r0,#REG_IE]
	mov r1,#0xC0000000		@;interrupt on start+sel
	@;orr r1,r1,#0x000C0000
	adr r2,RTS_Wakeup_key
	ldr r2,[r2]
	MVN R2,R2
	lsl	r2,r2,#0x10
	orr r1,r1,r2
	str r1,[r0,#REG_P1]
	strh r0,[r0,#REG_SOUNDCNT_X]	@;sound off
	orr r1,r6,#0x80
	strh r1,[r0,#REG_DISPCNT]			@;LCD off

	swi 0x030000

	@;Loop to wait for letting go of Sel+start
loop:
	mov r0,#REG_BASE
	ldr r1,[r0,#REG_P1]
	adr r7,RTS_Wakeup_key
	ldr r7,[r7]
	and r1,r1,r7
	@;cmp r1,#0x000C
	cmp r1,r7
	bne loop

	@;spin until VCOUNT==159
spin2:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#159
	bne spin2
	@;spin until VCOUNT==160
spin4:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#160
	bne spin4
	@;spin until VCOUNT==159
spin5:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#159
	bne spin5
	@;spin until VCOUNT==160
spin6:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#160
	bne spin6
	@;spin until VCOUNT==159
spin7:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#159
	bne spin7

	@;restore interrupts
	add r1,r0,#REG_IE
	strh r4,[r1]
	@;restore joystick interrupt
	str r5,[r0,#REG_P1]
	mov r4,#0x1000 @;clear the damn joystick interrupt
	strh r4,[r1,#2]

	@;restore screen
	strh r6,[r0,#REG_DISPCNT]
	ldmfd sp!,{r2-r9}
	@;restore sound state
	str r3,[r0,#REG_SOUNDCNT_X]
	add r1,r0,#0x80
	stmia r1!,{r2-r9}
	add r1,r0,#0x60
	ldmfd sp!,{r2-r9}
	stmia r1!,{r2-r9}
	ldmfd sp!,{r4-r11,lr}
	@;spin until VCOUNT==160, triggers next vblank
spin3:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#160
	bne spin3  @<insert ytmnd cliche here>
	@;all done!
	ldr pc,[r0,#-(0x04000000-0x03FFFFF4)] @to IRQ routine
	@b b_pressed_quit
	.align
RTS_Wakeup_key:
	.word 0x3F3 @;start and select
	.ltorg
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.arm
@;------------------------------------------------------
@;------------------------------------------------------
SetRampage:
	ldr 	r1,=0xD200
	ldr 	r2,=0x1500
	ldr 	r3,=0x9fe0000
	strh 	r1,[r3]
	mov 	r3,#0x8000000
	strh  r2,[r3]
	ldr 	r3,=0x8020000
	strh 	r1,[r3]
	ldr 	r3,=0x8040000
	strh  r2,[r3]
	ldr 	r3,=0x9C00000
	strh  r0,[r3]
	ldr 	r3,=0x9FC0000
	strh  r2,[r3]
	bx		lr
@;------------------------------------------------------
@;------------------------------------------------------
@;WriteSram_val:        @; sram address, val
@;	STRB    R1, [R0],#1
@;	LSR     R1, R1, #0x8
@;	STRB    R1, [R0],#1
@;	LSR     R1, R1, #0x8
@;	STRB    R1, [R0],#1
@;	LSR     R1, R1, #0x8
@;	STRB    R1, [R0]
@;	BX      LR
@;------------------------------------------------------
WriteSram: @;(u32 address, u8 *data, u32 size)
	ADD 		R2,R2,R0
	SUB 		R1,R1,R0
wSram_loop1:
	CMP     R0, R2
	BNE     wSram_loop
	BX      LR
wSram_loop:
	LDR     R3, [R1,R0]
	STRB    R3, [R0],#1
	LSR     R3, R3, #0x8
	STRB    R3, [R0],#1
	LSR     R3, R3, #0x8
	STRB    R3, [R0],#1
	LSR     R3, R3, #0x8
	STRB    R3, [R0],#1
	B       wSram_loop1
@;------------------------------------------------------
ReadSram: @;(u32 address, u8 *data, u32 size)
	ADDS    R2, R2, R0
	SUBS    R1, R1, R0
rSram_loop1:
	CMP     R0, R2
	BNE     rSram_loop
	BX      LR
rSram_loop:
	LDRB    R3, [R0]
	LSL 		R4,R3,#0
	LDRB    R3, [R0,#1]
	LSL 		R5,R3,#8
	ORR			R4,R5
	LDRB    R3, [R0,#2]
	LSL 		R5,R3,#16
	ORR			R4,R5
	LDRB    R3, [R0,#3]
	LSL 		R5,R3,#24
	ORR			R4,R5
	STR     R4, [R1,R0]
	ADDS    R0, #4
	B       rSram_loop1
@;------------------------------------------------------
backup_LCD:
	stmfd	sp!,{r0-r7,lr}
	mov 	r0,#0x20
	bl 		SetRampage
	mov 	r0,#0x0E000000
	mov 	r1,#0x6000000
	mov 	r2,#0x10000		@;0x12C00
	bl 		WriteSram   	@;(u32 address, u8 *data, u32 size)
	mov 	r0,#0x30
	bl 		SetRampage
	mov 	r0,#0x0E000000
	ldr 	r1,=0x6010000
	ldr 	r2,=0x2C00
	bl 		WriteSram   	@;(u32 address, u8 *data, u32 size)

	ldmfd	sp!,{r0-r7,PC}

@;------------------------------------------------------
restore_LCD:
	stmfd	sp!,{r0-r7,lr}
	mov 	r0,#0x20
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x6000000
	mov r2,#0x10000		@;0x12C00
	bl  ReadSram    	@;(u32 address, u8 *data, u32 size)
	mov 	r0,#0x30
	bl 		SetRampage
	mov r0,#0x0E000000
	ldr r1,=0x6010000
	ldr r2,=0x2C00
	bl 	ReadSram   	 @;(u32 address, u8 *data, u32 size)


	ldmfd	sp!,{r0-r7,PC}
@;------------------------------------------------------
restore2_IO:        @; IOaddress, offset
	LDRB    R3, [R1]
	LSL 		R4,R3,#0
	LDRB    R3, [R1,#1]
	LSL 		R5,R3,#8
	ORR			R4,R5
	STRH    R4, [R0]
	bx lr
@;restore4_IO:        @; IOaddress, offset
@;	LDRB    R3, [R1]
@;	LSL 		R4,R3,#0
@;	LDRB    R3, [R1,#1]
@;	LSL 		R5,R3,#8
@;	ORR			R4,R5
@;	LDRB    R3, [R1,#2]
@;	LSL 		R5,R3,#16
@;	ORR			R4,R5
@;	LDRB    R3, [R1,#3]
@;	LSL 		R5,R3,#24
@;	ORR			R4,R5
@;	STR     R4, [R0]
@;	bx lr
@;------------------------------------------------------
@;------------------------------------------------------
ingamemenu_now:
	stmfd	sp!,{r0-r12,lr}

    mov r7,#0x04000000
    add r1,r7,#0x200
    ldrh r2,[r1,#8]
    adrl r3,spend_0x80
    ldr r3,[r3]
    strh r2,[r3,#0x3c]
    mov r2,#0
    strh r2,[r1,#8]

	mov 	r7,#0x4000000
	add 	r1,r7,#REG_SOUND1CNT_L
	adrl	r11, spend_0x80
	ldr		r11,[r11]
	add		r11,#0x40     @;0x3007EC0

	add   r3,r11,#0x32 	@; IO 0x60-0x90 offset 0x100-130
loopbak:
	ldrh  r2,[r1],#2
	strh  r2,[r11],#2
	cmp   r11,r3
	bne   loopbak

	ldrh r2,[r7,#0xBA]@;DMA0CNT_H
	strh  r2,[r11],#2 @;offset 0x72
	ldrh r2,[r7,#0xC6]@;DMA1CNT_H
	strh  r2,[r11],#2 @;offset 0x74
	ldrh r2,[r7,#0xD2]@;DMA2CNT_H
	strh  r2,[r11],#2 @;offset 0x76
	ldrh r2,[r7,#0xDE]@;DMA3CNT_H
	strh  r2,[r11],#2 @;offset 0x78

	mov		r7,#0x4000000
	ldrh 	r3,[r7,#REG_SOUNDCNT_L]  @;san guo
	stmfd	sp!,{r3}
	ldrh 	r3,[r7,#REG_SOUNDCNT_X]  @;bak
	stmfd	sp!,{r3}

	ldrh r2,[r7,#0x50]
	strh r2,[r11]
	mov r2,#0
	strh r2,[r7,#0x50]
	ldrh  r6,[r7]     @;Displaly Control
	stmfd	sp!,{r6}

	mov 	r3,#0x0100
	strh 	r3,[r7,#0x20]	@;Rotation/Scaling BG2P
	strh 	r3,[r7,#0x26]
	mov 	r3,#0x0
	strh 	r3,[r7,#0x22]
	strh 	r3,[r7,#0x24]
	str 	r3,[r7,#0x28]	@;BG2X/Y
	str 	r3,[r7,#0x2c]

	strh 	r3,[r7,#0x54]	@;Bldy Brightness

	strh 	r3,[r7,#0xBA]	@;DMA0CNT_H	0086game
	strh 	r3,[r7,#0xC6]	@;DMA1CNT_H
	strh 	r3,[r7,#0xD2]	@;DMA2CNT_H
	strh 	r3,[r7,#0xDE]	@;DMA3CNT_H
	ldr  	r6,=0x403 		@;MODE_3 | BG2_ENABLE
	strh 	r6,[r7]
	@;mov 	r2,#0
	@ IME was disabled before touching DMA/video state.

	strh 	r3,[r7,#REG_SOUNDCNT_X] @;sound off

	bl 		backup_LCD
    bl DSStyle_ReturnDark
    adrl r1,spend_0x80
    ldr r1,[r1]
    strh r0,[r1,#0x3e]
    bl DSStyle_LoadMenuColours


    mov r11,#0
    bl Slot_MenuPanel
    adrl r0,spend_0x80
    ldr r0,[r0]
    mvn r1,#0
    str r1,[r0,#0x2c] @ Previous selection; unused gap before saved system SP.
begin_show:
    bl Slot_MenuVBlank
    adrl r7,Cheat_count
    ldr r7,[r7]
    cmp r7,#0
    moveq r6,#4
    movne r6,#5
    cmp r11,#0
    sublt r11,r6,#1
    cmp r11,r6
    movge r11,#0
    mov r12,#0
    cmp r6,#4
    moveq r9,#58
    movne r9,#51
menu_row:
    adrl r0,spend_0x80
    ldr r0,[r0]
    ldr r0,[r0,#0x2c]
    cmn r0,#1
    beq menu_draw_row
    cmp r12,r0
    beq menu_draw_row
    cmp r12,r11
    bne menu_next_row
menu_draw_row:
    mov r0,#42
    mov r1,r9
    mov r2,#156
    mov r3,#13
    DS_MENU_COLOUR r4, Slot_RTS_Panel, r5
    cmp r12,r11
    DS_MENU_COLOUR r8, DS_PanelLine, r5
    cmp r12,r11
    bne menu_row_fill
    adrl r5,spend_0x80
    ldr r5,[r5]
    ldrh r4,[r5,#0x38]
    mov r8,r4
menu_row_fill:
    bl Slot_MenuStripedRect
    adrl r8,Slot_MenuLabels
    ldr r0,[r8,r12,lsl #2]
    add r8,r8,r0
    cmp r12,#3
    bne menu_text_start
    adrl r7,Cheat_count
    ldr r7,[r7]
    cmp r7,#0
    beq menu_return_label
    adrl r7,CheatONOFF
    ldr r7,[r7]
    ldr r7,[r7]
    cmp r7,#1
    bne menu_text_start
    adrl r8,s_cheatON
    b menu_text_start
menu_return_label:
    adrl r8,s_reset
menu_text_start:
    mov r10,#47
menu_character:
    ldrb r0,[r8],#1
    cmp r0,#0
    beq menu_next_row
    mov r1,r10
    mov r2,r9
    DS_MENU_COLOUR r3, Slot_RTS_Ink, r5
    cmp r12,r11
    bne menu_ink_ready
    adrl r5,spend_0x80
    ldr r5,[r5]
    ldrh r3,[r5,#0x3a]
menu_ink_ready:
    cmp r12,#1
    beq menu_state_ink
    cmp r12,#2
    bne menu_print
menu_state_ink:
    adrl r7,RTS_switch
    ldr r7,[r7]
    cmp r7,#0
    ldreq r3,=0x4210
menu_print:
    bl printchar
    add r10,r10,#6
    b menu_character
menu_next_row:
    add r9,r9,#14
    add r12,r12,#1
    adrl r7,Cheat_count
    ldr r7,[r7]
    cmp r7,#0
    moveq r7,#4
    movne r7,#5
    cmp r12,r7
    blt menu_row
    adrl r0,spend_0x80
    ldr r0,[r0]
    str r11,[r0,#0x2c]
waitSeletc:
    ldr r0,=0x3ff
    ldr r8,=0x4000130
    ldrh r9,[r8]
    cmp r9,r0
    beq waitSeletc
pressdown:
    mov r12,r9
    ldrh r9,[r8]
    cmp r9,r0
    bne pressdown
    ldr r0,=0x3bf
    cmp r12,r0
    subeq r11,r11,#1
    beq begin_show
    ldr r0,=0x37f
    cmp r12,r0
    addeq r11,r11,#1
    beq begin_show
    ldr r0,=0x3fd
    cmp r12,r0
    beq b_pressed_quit
    ldr r0,=0x3fe
    cmp r12,r0
    bne waitSeletc
A_pressed:
    cmp r11,#0
    beq b_pressed_quit
    cmp r11,#1
    beq menu_save
    cmp r11,#2
    beq menu_load
    adrl r7,Cheat_count
    ldr r7,[r7]
    cmp r7,#0
    beq reset_now
    cmp r11,#4
    beq reset_now
    adrl r2,CheatONOFF
    ldr r2,[r2]
    ldr r3,[r2]
    cmp r3,#1
    moveq r3,#0
    movne r3,#1
    str r3,[r2]
    b begin_show
menu_save:
    adrl r7,RTS_switch
    ldr r7,[r7]
    cmp r7,#1
    beq call_Save
    b begin_show
menu_load:
    adrl r7,RTS_switch
    ldr r7,[r7]
    cmp r7,#1
    beq call_Load
    b begin_show
    .ltorg
@;ingamemenu_exit:
b_pressed_quit:
	ldmfd	sp!,{r6}
	mov		r7,#0x4000000
	strh 	r6,[r7] @;re Displaly Control
	bl 		restore_LCD
save_exit:
	adrl r0,spend_0x80
	ldr r0,[r0]
	ldrh r2,[r0,#0x7a]
	mov r7,#0x4000000
	strh r2,[r7,#0x50]

	mov		r7,#0x4000000
	mov 	r2,#1
	@ Restore the original IME only after all state and banking are ready.

	ldmfd	sp!,{r3}
	strh 	r3,[r7,#REG_SOUNDCNT_X]

	ldmfd	sp!,{r3}
	strh 	r3,[r7,#REG_SOUNDCNT_L]  @;san guo

	adrl	r11, spend_0x80
	ldr		r11,[r11]
	add		r0,r11,#0x40 @;0x3007EC0

	ldrh  r3,[r0],#2
	strh 	r3,[r7,#0x60]	@;SOUND1CNT_L
	ldrh  r3,[r0],#6
	strh 	r3,[r7,#0x62]	@;SOUND1CNT_H
	ldrh  r3,[r0],#8
	strh 	r3,[r7,#0x68]	@;SOUND2CNT_L
	ldrh  r3,[r0],#8
	strh 	r3,[r7,#0x70]	@;SOUND3CNT_L
	ldrh  r3,[r0],#8
	strh 	r3,[r7,#0x78]	@;SOUND3CNT_L

	add		r11,#0x70   @;0x3007EC0+0x30
	add 	r11,#2
	ldrh  r3,[r11],#2
	strh 	r3,[r7,#0xBA]	@;DMA0CNT_H
	ldrh  r3,[r11],#2
	strh 	r3,[r7,#0xC6]	@;DMA1CNT_H
	ldrh  r3,[r11],#2
	strh 	r3,[r7,#0xD2]	@;DMA2CNT_H
	ldrh  r3,[r11],#2
	strh 	r3,[r7,#0xDE]	@;DMA3CNT_H

	mov 	r0,#0x00
	bl 		SetRampage


    adrl r0,spend_0x80
    ldr r0,[r0]
    ldrh r2,[r0,#0x3c]
    mov r1,#0x04000000
    add r1,r1,#0x200
    strh r2,[r1,#8]
	ldmfd	sp!,{r0-r12,PC}
	.ltorg
	@;===================================================
call_Save:
	adrl	r7,RTS_switch
	ldr		r7,[r7]
	cmp 	r7,#1
	bne 	errorRTS

	@;02000000-0203FFFF   WRAM - On-board Work RAM  (256 KBytes)
	mov		r8,#0x40   @; 0x40 0x50 0x60 0x70
	mov   r9,#0x2000000
wram_2000000:
	mov 	r0,r8
	bl 		SetRampage
	mov 	r0,#0x0E000000
	mov 	r1,r9
	mov		r2,#0x10000
	bl 		WriteSram
	add 	r8,#0x10
	add 	r9,#0x10000
	cmp 	r8,#0x80
	bne 	wram_2000000

	@;03000000-03007FFF   WRAM - On-chip Work RAM   (32 KBytes)
	mov 	r0,#0x80
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x3000000
	mov r2,#0x8000
	bl 	WriteSram

	@;05000000-050003FF   BG/OBJ Palette RAM        (1 Kbyte)
	ldr r0,=0x0E008000
	mov r1,#0x5000000
	mov r2,#0x400
	bl 	WriteSram

	@;06000000-06017FFF   VRAM - Video RAM          (96 KBytes)
	ldmfd	sp!,{r6}
	ldr		r7,=0x4000000
	strh 	r6,[r7] @;re
	bl 		restore_LCD

	mov 	r0,#0x90
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x6000000
	mov r2,#0x10000
	bl 	WriteSram

	mov 	r0,#0xA0
	bl 		SetRampage
	mov r0,#0x0E000000
	ldr r1,=0x6010000
	mov r2,#0x8000
	bl 	WriteSram

	@;07000000-070003FF   OAM - OBJ Attributes      (1 Kbyte)
	ldr r0,=0x0E008000
	mov r1,#0x7000000
	mov r2,#0x400
	bl 	WriteSram

	@;R4-R11
	mrs	  r0,CPSR    @;Back up
	adrl	r7, spend_0x80
	ldr		r7,[r7]
	add		r7,#0x30  @;{r4-r11,sp,lr} SPSR  0x28+4

	mov		r1, #0xDF		@; Switch to systme Mode
	msr		cpsr_cf, r1
	NOP
	mov		r6,sp
	stmia r7!,{r6,lr}

	msr 	cpsr_cf,r0	;@return IRQ mode
	NOP

	ldr 	r0,=0x0E008400
	adrl 	r1, spend_0x80
	ldr		r1,[r1]
	mov 	r2,#0x80
	bl 		WriteSram

	@;04000000-040003FE   I/O Registers
	ldr r0,=0x0E009000
	mov r1,#0x4000000
	mov r2,#0x60					@;0x0-0x60
	bl 	WriteSram

	ldr 	r0,=0x0E009060
	adrl 	r1, spend_0x80
	ldr		r1,[r1]
	add 	r1,#0x40
	@;ldr 	r1,=0x2010000
	mov 	r2,#0x30   				@;0x60-0x90
	bl 		WriteSram

	ldr r0,=0x0E009090
	mov r1,#0x4000000
	add r1,#0x90          @;0x90-0x3FE
	mov r2,#0x370
	bl 	WriteSram


    adrl r1,spend_0x80
    ldr r1,[r1]
    ldrh r2,[r1,#0x3c]
    ldr r0,=0x0e009208
    strb r2,[r0]
    mov r2,r2,lsr #8
    strb r2,[r0,#1]
    ldrh r2,[r1,#0x7a]
    ldr r0,=0x0e009050
    strb r2,[r0]
    mov r2,r2,lsr #8
    strb r2,[r0,#1]
	@;FLAG
	ldr r0,=0x0E00FFF0
	adrl r1,S_RTS_FLAG
	mov r2,#0x10
	bl 	WriteSram

	mov r7,#0x50000
delay_loop:
	cmp			r7,#0
	beq			save_exit
	nop
	sub 		r7,#0x01
	b				delay_loop

	b save_exit
	@;===================================================
	@;===================================================
call_Load:
	@;check 	;FLAG
	mov 	r0,#0xA0
	bl 		SetRampage
	ldr 	r0,=0x0E00FFF0
	adrl 	r1, spend_0x80  @;temp buff
	ldr 	r1,[r1]
	mov 	r2,#0x10
	bl 		ReadSram

	adrl 	r1,S_RTS_FLAG
	adrl 	r2, spend_0x80 @;temp buff
	ldr 	r2,[r2]
	mov 	r3,#0
loop_check:
	ldr 	r4,[r1],#4
	ldr 	r5,[r2],#4
	cmp 	r4,r5
	@;bne errorRTS
	add 	r3,#1
	cmp 	r3,#4
	bne 	loop_check

	@;02000000-0203FFFF   WRAM - On-board Work RAM  (256 KBytes)
	mov		r8,#0x40   @; 0x40 0x50 0x60 0x70
	mov   r9,#0x2000000
wram_2000000_Load:
	mov 	r0,r8
	bl 		SetRampage
	mov 	r0,#0x0E000000
	mov 	r1,r9
	mov		r2,#0x10000
	bl 		ReadSram
	add 	r8,#0x10
	add 	r9,#0x10000
	cmp 	r8,#0x80
	bne 	wram_2000000_Load

	@;03000000-03007FFF   WRAM - On-chip Work RAM   (32 KBytes)
	mov 	r0,#0x80
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x3000000
	mov r2,#0x8000
	bl 	ReadSram

	@;05000000-050003FF   BG/OBJ Palette RAM        (1 Kbyte)
	ldr r0,=0x0E008000
	mov r1,#0x5000000
	mov r2,#0x400
	bl 	ReadSram

	@;06000000-06017FFF   VRAM - Video RAM          (96 KBytes)
	mov 	r0,#0x90
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x6000000
	mov r2,#0x10000
	bl 	ReadSram
	mov 	r0,#0xA0
	bl 		SetRampage
	mov r0,#0x0E000000
	ldr r1,=0x6010000
	mov r2,#0x8000
	bl 	ReadSram

	@;07000000-070003FF   OAM - OBJ Attributes      (1 Kbyte)
	ldr r0,=0x0E008000
	mov r1,#0x7000000
	mov r2,#0x400
	bl 	ReadSram

	@;-------------------------------------
	mov r10,#0x4000000
	LDR r11,=0x0E009000

	adrl r9,register_list
register_list_loop:
	ldrh r2,[r9],#2
	cmp r2 ,#0xFF00
	beq register_list_end

	add r0,r10,r2   @;0x4000000  0x4000002 0x4000004
	add r1,r11,r2
	bl  restore2_IO
	b 	register_list_loop
register_list_end:

	@;mov r10,#0x4000000
	LDR r11,=0x0E008500
@;	add r0,r10,#0xBA  @;0x40000BA  DMA
@;	add r1,r11,#0x32
@;	bl  restore2_IO
	add r0,r10,#0xC6  @;0x40000C6
	add r1,r11,#0x34
	bl  restore2_IO
@;	add r0,r10,#0xD2  @;0x40000D2
@;	add r1,r11,#0x36
@;	bl  restore2_IO
@;	add r0,r10,#0xDE  @;0x40000DE
@;	add r1,r11,#0x38
@;	bl  restore2_IO

	@;mov   r4,#0x8F
	@;mov   r7,#0x4000000
	@;strh 	r4,[r7,#REG_SOUNDCNT_X]

	ldr r0,=0x4000202   @;0x4000202
	mov r1,#0
	strh r1,[r0]


	ldr 	r0,=0x0E008400
	adrl 	r1, spend_0x80 @;temp buff
	ldr		r1,[r1]
	mov 	r2,#0x80
	bl 		ReadSram

	mrs	  r0,CPSR
	adrl	r7, spend_0x80
	ldr		r7,[r7]
	add		r7,#0x28      @;SPSR offset

	ldmia	r7!,{r2}			@;r7=0x2C
	msr		SPSR_cxsf,r2	@;restore SPSR_irq

	mov		r1, #0xDF		  @;Switch to systme Mode
	msr		cpsr_cf, r1
	NOP
	add		r7,#0x4					@;offset 0x30
	ldmia r7!,{r13-r14}

	msr 	cpsr_cf,r0	  @;restore IRQ
	NOP

	mov 	r0,#0x0
	bl 		SetRampage

	@;spin until VCOUNT==160, triggers next vblank
	mov 	r0,#0x4000000
spin3_load:
	ldrh 	r1,[r0,#REG_VCOUNT]
	cmp 	r1,#160
	bne 	spin3_load

	adrl	r12, spend_0x80
	ldr		r12,[r12]
	ldmia r12!,{r4-r11,sp,lr}

	ldr 	pc,[r0,#-(0x04000000-0x03FFFFF4)] @;to normal IRQ routine

	@;===================================================
errorRTS:
	adrl  r8,s_badRTS
	mov		r9,#145	   @;Y
	mov		r10,#64    @;X
showerror:
	ldrb	r0,[r8],#+1
	cmp		r0,#0x00
	beq		enderror
	mov		r1,r10
	add		r2,r9,#0
	mov	  r3,#0x1F  @;red
	bl		printchar    @; r0 char£¬ r1, X  £¬ r2, Y , r3 color
	add		r10,#8
	b			showerror
enderror:
waitB:
	ldr		r0,=0x3FF		@;No key press
	ldr		r8,=0x4000130
	ldrh	r9,[r8]
	cmp		r9,r0
	beq		waitB
pressB:
	mov		r12,r9
	ldrh	r9,[r8]
	cmp		r9,r0
	bne		pressB

	ldr		r0,=0x3FD		;@B
	cmp		r12,r0
	moveq	r11,#5
	beq		b_pressed_quit
	b		waitB



	@;===================================================
call_CheatON:
	mov r3,#1
set_Cheat:
	ADR r2,CheatONOFF
	ldr	r2,[r2]
	str r3,[r2]
	b b_pressed_quit
	@;===================================================
call_CheatOFF:
	mov r3,#0
	b   set_Cheat


	.ltorg
	@;--------------
CheatONOFF:
	.word	 0x03007FE0
	@;===================================================
draw_plot:
	MOV     R11, #0xF0
	MLA     R3, R1, R11, R0
	MOV     R3, R3,LSL#1
	ADD     R3, R3, #0x6000000
	STRH    R2, [R3]
	BX			LR
		@;===================================================

@ Native-font settings-style panel. No game IRQ stack allocations beyond LR.
Slot_MenuPanel:
    stmfd sp!,{lr}
    mov r0,#0
    mov r1,#0
    mov r2,#240
    mov r3,#160
    DS_MENU_COLOUR r4, Slot_RTS_Background, r5
    bl Slot_MenuRect
    mov r0,#36
    mov r1,#25
    mov r2,#168
    mov r3,#110
    DS_MENU_COLOUR r4, DS_PanelOutline, r5
    bl Slot_MenuRect
    mov r0,#37
    mov r1,#26
    mov r2,#166
    mov r3,#108
    DS_MENU_COLOUR r4, Slot_RTS_Panel, r5
    DS_MENU_COLOUR r8, DS_PanelLine, r5
    bl Slot_MenuStripedRect
    adrl r0,Slot_MenuTitle
    mov r1,#47
    mov r2,#35
    DS_MENU_COLOUR r3, Slot_RTS_Ink, r5
    bl Slot_MenuText
    ldmfd sp!,{pc}
@ Poll VCOUNT: game IRQs are disabled while the menu owns the display.
Slot_MenuVBlank:
    ldr r0,=0x04000006
Slot_MenuWaitVisible:
    ldrh r1,[r0]
    cmp r1,#160
    bhs Slot_MenuWaitVisible
Slot_MenuWaitBlank:
    ldrh r1,[r0]
    cmp r1,#160
    blo Slot_MenuWaitBlank
    bx lr
Slot_MenuRect:
    mov r8,r4
Slot_MenuStripedRect:
    mov r5,#240
    mla r5,r1,r5,r0
    mov r5,r5,lsl #1
    add r5,r5,#0x06000000
    rsb r7,r2,#240
    mov r7,r7,lsl #1
    sub r1,r1,#25 @ Stripe phase relative to the popup origin
Slot_MenuRectRow:
    mov r6,r2
    tst r1,#3
    moveq r0,r8
    movne r0,r4
    add r1,r1,#1
Slot_MenuRectPixel:
    strh r0,[r5],#2
    subs r6,r6,#1
    bne Slot_MenuRectPixel
    add r5,r5,r7
    subs r3,r3,#1
    bne Slot_MenuRectRow
    bx lr
Slot_MenuText:
    stmfd sp!,{r8-r11,lr}
    mov r8,r0
    mov r10,r1
    mov r11,r2
    mov r9,r3
Slot_MenuTextLoop:
    ldrb r0,[r8],#1
    cmp r0,#0
    beq Slot_MenuTextEnd
    mov r1,r10
    mov r2,r11
    mov r3,r9
    bl printchar
    add r10,r10,#6
    b Slot_MenuTextLoop
Slot_MenuTextEnd:
    ldmfd sp!,{r8-r11,pc}
    .ltorg

printchar:	@; r0 ´òÓ¡µÄ×Ö·û£¬ r1, X  £¬ r2, Y , r3 ÑÕÉ«
	STMFD   SP!, {R8-R11,LR}
	MOV     R8, R1
	MOV     R6, R2
	MOV     R9, R3
	MOV     R10, #0x80
	ADRL     R4, ASCII
	SUB R0,R0,#32
	ADD R0,R0,R0,LSL#1
	ADD R4,R4,R0,LSL#2
	ADD R7,R4,#12

loc_A0DD7C:
	MOV     R5, #0
loc_A0DD80:
	LDRB    R3, [R4]
	ANDS    R3, R3, R10,ASR R5
	MOVNE   R2, R9
	MOVNE   R1, R6
	ADDNE   R0, R5, R8
	BLNE    draw_plot
	ADD     R5, R5, #1
	CMP     R5, #6
	BNE     loc_A0DD80
	ADD     R4, R4, #1
	CMP     R4, R7
	ADD     R6, R6, #1
	BNE     loc_A0DD7C

	LDMFD   SP!, {R8-R11,PC}
	.align
register_list:
	.hword 0x0000,0x0002,0x0004,0x0008,0x000A,0x000C,0x000E,0x0048
	.hword 0x004A,0x0050,0x0052
	.hword 0x0084
	.hword 0x0060,0x0062,0x0068,0x0070,0x0072,0x0078
	.hword 0x0080,0x0082,0x0088                 @;0x008C,0x008E,
	.hword 0x0090,0x0092,0x0094,0x0096,0x0098,0x009A,0x009C,0x009E

	.hword 0x00B8,0x00C4,0x00D0,0x00DC  @;DMA
	.hword 0x0120,0x0122,0x0124,0x0126,0x0128,0x012A,0x012C,0x0132,0x0134
	.hword 0x0140,0x0150,0x0154,0x0200,0x0204,0x0208,0xFF00
	.align
@;------------------------------------------------
ASCII:
Slot_MenuFont:
	.include "slot_menu_font.inc"
	.align

Slot_MenuLabels:
    .word s_resume-Slot_MenuLabels,s_save-Slot_MenuLabels,s_load-Slot_MenuLabels
    .word s_cheatOFF-Slot_MenuLabels,s_reset-Slot_MenuLabels
Slot_MenuTitle: .asciz "DS Style"
s_resume: .asciz "Resume"
s_reset: .asciz "Exit game"
s_save: .asciz "Save state"
s_load: .asciz "Load state"
s_cheatON: .asciz "Cheats: On"
s_cheatOFF: .asciz "Cheats: Off"
    .align
Slot_RTS_Background: .hword 0x56b5,0x0000
Slot_RTS_Panel: .hword 0x7bde,0x1084
Slot_RTS_Ink: .hword 0x0000,0x7fff
DS_PanelOutline: .hword 0x0000,0x3def
DS_PanelLine: .hword 0x77bd,0x14a5

	.align
S_RTS_FLAG:
	.ascii "DSStyle81RTSFILE"
	.align
s_badRTS:
 	.byte  'R','T','S','F','I','L','E','D','A','M','A','G','E','D',0x00
	.align
RTS_switch:
	.word 0x00000000
Cheat_count:
	.word 0x00000000
no_CHEAT_end:
CHEAT:
	.space 0x400
	.align
RTS_ReplaceIRQ_end:
   .end
