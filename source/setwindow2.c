#include <gba_systemcalls.h>
#include <stdio.h>
#include <stdlib.h>
#include <gba_base.h>
#include <gba_input.h>
#include <string.h>
#include <gba_dma.h>

#include "ez_define.h"
#include "lang.h"
#include "ezkernel.h"
#include "RTC.h"
#include "draw.h"
#include "Ezcard_OP.h"

u16 auto_save_sel;
u16 resume_last_on;
u16 ModeB_INIT;
u16 boot_mode_pref;
u16 led_open_sel;

u16 Breathing_R;
u16 Breathing_G;
u16 Breathing_B;

u16 toggle_reset;
u16 toggle_backup;

u16 SD_R;
u16 SD_G;
u16 SD_B;

extern u16 gl_auto_save_sel;
extern u16 gl_resume_last_on;
extern u16 gl_ModeB_init;
extern u16 gl_boot_mode_pref;

extern u16 gl_led_open_sel;
extern u16 gl_Breathing_R;
extern u16 gl_Breathing_G;
extern u16 gl_Breathing_B;

extern u16 gl_toggle_backup;
extern u16 gl_toggle_reset;

extern u16 gl_SD_R;
extern u16 gl_SD_G;
extern u16 gl_SD_B;
extern void UIAudio_HandleKeys(u16 keysdown, u16 keysrepeat);

void save_set2_info(void);
extern void Draw_select_icon(u32 X,u32 Y,u32 mode);
extern void IWRAM_CODE Set_LED_control(u16  status);

extern void CheckSwitch(void);

static void Draw_set2_button(u32 offsety, u32 highlighted, u32 is_ok)
{
	u16 clean_color = highlighted ? gl_color_btn_clean : gl_color_MENU_btn;
	char msg[32];

	Clear(202,offsety-2,30,14,clean_color,1);
	if(is_ok)
		sprintf(msg,"%s",gl_ok_btn);
	else
		sprintf(msg,"%s",gl_set_btn);
	DrawHZText12(msg,0,205,offsety,gl_color_text,1);
}

u32 Setting_window2(void)
{
	u16 keys;
	u32 line;
	u32 select;
	u32 Set_OK=0;
	u32 Set_OK_line=0;
	u32 currstate=0;
	char msg[128];
	u16 clean_color;
	u32 re_show=1;
	u32 full_redraw = 1;
	u16 prev_led_open_sel = 0xFFFF;
	u32 main_row_y[7];

	u8 line_total;
	u8 auto_save_pos = 1;
	u8 resume_last_pos = 1;
	u8 boot_mode_pos = 3;
	u8 led_pos = 1;
	u8 reset_pos = 1;
	u8 backup_pos = 1;
	u8 ModeB_pos = 3;

	select = 0;
	u32 y_offset = 24;
	u32 set_offset = 1;
	u32 x_offset = set_offset+9*6+3;
	u32 line_x = 17;

	CheckSwitch();

	auto_save_sel = gl_auto_save_sel;
	resume_last_on = gl_resume_last_on;
	ModeB_INIT = gl_ModeB_init;
	boot_mode_pref = gl_boot_mode_pref;
	led_open_sel = gl_led_open_sel;
	Breathing_R = gl_Breathing_R;
	Breathing_G = gl_Breathing_G;
	Breathing_B = gl_Breathing_B;
	SD_R = gl_SD_R;
	SD_G = gl_SD_G;
	SD_B = gl_SD_B;
	toggle_reset = gl_toggle_reset;
	toggle_backup = gl_toggle_backup;

	while(1)
	{
		VBlankIntrWait();

		if(re_show)
		{
			u32 led_sub1_y = y_offset + line_x*5;
			u32 led_sub2_y = y_offset + line_x*6;
			u32 reset_y;
			u32 backup_y;

			reset_y = y_offset + line_x*(led_open_sel ? 7 : 5);
			backup_y = y_offset + line_x*(led_open_sel ? 8 : 6);

			main_row_y[0] = y_offset + line_x*0;
			main_row_y[1] = y_offset + line_x*1;
			main_row_y[2] = y_offset + line_x*2;
			main_row_y[3] = y_offset + line_x*3;
			main_row_y[4] = y_offset + line_x*4;
			main_row_y[5] = reset_y;
			main_row_y[6] = backup_y;

			if(full_redraw || (prev_led_open_sel != led_open_sel))
			{
				VBlankIntrWait();
				ClearWithBG((u16*)gImage_START,0,y_offset,240,160-y_offset,1);
				full_redraw = 0;
			}
			else
			{
				for(line=0; line<5; line++)
				{
					ClearWithBG((u16*)gImage_START,x_offset,main_row_y[line]-2,240-x_offset,14,1);
				}
				if(led_open_sel == 0x1)
				{
					ClearWithBG((u16*)gImage_START,x_offset,led_sub1_y-2,240-x_offset,14,1);
					ClearWithBG((u16*)gImage_START,x_offset,led_sub2_y-2,240-x_offset,14,1);
				}
				ClearWithBG((u16*)gImage_START,x_offset,main_row_y[5]-2,240-x_offset,14,1);
				ClearWithBG((u16*)gImage_START,x_offset,main_row_y[6]-2,240-x_offset,14,1);
			}

			prev_led_open_sel = led_open_sel;

			line_total = 7;

			sprintf(msg,"%s",gl_save);
			DrawHZText12(msg,0,set_offset,main_row_y[0],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[0],(auto_save_sel == 0x1));
			sprintf(msg,"%s",gl_auto_save);
			DrawHZText12(msg,0,x_offset+15,main_row_y[0],(auto_save_pos==0)?gl_color_selected:gl_color_text,1);

			sprintf(msg,"%s"," Remember");
			DrawHZText12(msg,0,set_offset,main_row_y[1],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[1],(resume_last_on == 0x1));
			if(resume_last_on)
				sprintf(msg,"%s",gl_enabled);
			else
				sprintf(msg,"%s",gl_disabled);
			DrawHZText12(msg,0,x_offset+15,main_row_y[1],(resume_last_pos==0)?gl_color_selected:gl_color_text,1);

			sprintf(msg,"%s","Boot Mode");
			DrawHZText12(msg,0,set_offset,main_row_y[2],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[2],(boot_mode_pref == 0x0));
			sprintf(msg,"%s","Menu");
			DrawHZText12(msg,0,x_offset+15,main_row_y[2],(boot_mode_pos==0)?gl_color_selected:gl_color_text,1);
			Draw_select_icon(x_offset+8*6,main_row_y[2],(boot_mode_pref == 0x1));
			sprintf(msg,"%s","Clean");
			DrawHZText12(msg,0,x_offset+8*6+15,main_row_y[2],(boot_mode_pos==1)?gl_color_selected:gl_color_text,1);
			Draw_select_icon(x_offset+16*6,main_row_y[2],(boot_mode_pref == 0x2));
			sprintf(msg,"%s","Addon");
			DrawHZText12(msg,0,x_offset+16*6+15,main_row_y[2],(boot_mode_pos==2)?gl_color_selected:gl_color_text,1);

			sprintf(msg,"%s",gl_modeB_INITstr);
			DrawHZText12(msg,0,set_offset,main_row_y[3],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[3],(ModeB_INIT == 0x0));
			sprintf(msg,"%s",gl_modeB_RUMBLE);
			DrawHZText12(msg,0,x_offset+15,main_row_y[3],(ModeB_pos==0)?gl_color_selected:gl_color_text,1);
			Draw_select_icon(x_offset+9*6,main_row_y[3],(ModeB_INIT == 0x1));
			sprintf(msg,"%s",gl_modeB_RAM);
			DrawHZText12(msg,0,x_offset+9*6+15,main_row_y[3],(ModeB_pos==1)?gl_color_selected:gl_color_text,1);
			Draw_select_icon(x_offset+17*6,main_row_y[3],(ModeB_INIT == 0x2));
			sprintf(msg,"%s",gl_modeB_LINK);
			DrawHZText12(msg,0,x_offset+17*6+15,main_row_y[3],(ModeB_pos==2)?gl_color_selected:gl_color_text,1);

			sprintf(msg,"%s",gl_led);
			DrawHZText12(msg,0,set_offset,main_row_y[4],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[4],(led_open_sel == 0x1));
			sprintf(msg,"%s",gl_led_open);
			DrawHZText12(msg,0,x_offset+15,main_row_y[4],(led_pos==0)?gl_color_selected:gl_color_text,1);

			if(led_open_sel == 0x1){
				sprintf(msg,"%s",gl_Breathing_light);
				DrawHZText12(msg,0,set_offset,led_sub1_y,gl_color_selected,1);
				Draw_select_icon(x_offset,led_sub1_y,(Breathing_R == 0x1));
				sprintf(msg,"%s","R");
				DrawHZText12(msg,0,x_offset+15,led_sub1_y,(led_pos==2)?gl_color_selected:gl_color_text,1);
				Draw_select_icon(x_offset+5*6+15,led_sub1_y,(Breathing_G == 0x1));
				sprintf(msg,"%s","G");
				DrawHZText12(msg,0,x_offset+5*6+15+15,led_sub1_y,(led_pos==3)?gl_color_selected:gl_color_text,1);
				Draw_select_icon(x_offset+5*6+5*6+15+15,led_sub1_y,(Breathing_B == 0x1));
				sprintf(msg,"%s","B");
				DrawHZText12(msg,0,x_offset+5*6+5*6+15+15+15,led_sub1_y,(led_pos==4)?gl_color_selected:gl_color_text,1);

				sprintf(msg,"%s",gl_SD_working);
				DrawHZText12(msg,0,set_offset,led_sub2_y,gl_color_selected,1);
				Draw_select_icon(x_offset,led_sub2_y,(SD_R == 0x1));
				sprintf(msg,"%s","R");
				DrawHZText12(msg,0,x_offset+15,led_sub2_y,(led_pos==5)?gl_color_selected:gl_color_text,1);
				Draw_select_icon(x_offset+5*6+15,led_sub2_y,(SD_G == 0x1));
				sprintf(msg,"%s","G");
				DrawHZText12(msg,0,x_offset+5*6+15+15,led_sub2_y,(led_pos==6)?gl_color_selected:gl_color_text,1);
				Draw_select_icon(x_offset+5*6+5*6+15+15,led_sub2_y,(SD_B == 0x1));
				sprintf(msg,"%s","B");
				DrawHZText12(msg,0,x_offset+5*6+5*6+15+15+15,led_sub2_y,(led_pos==7)?gl_color_selected:gl_color_text,1);
			}

			sprintf(msg,"%s",gl_lang_toggle_reset);
			DrawHZText12(msg,0,set_offset,main_row_y[5],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[5],(toggle_reset == 0x1));
			if(toggle_reset)
				sprintf(msg,"%s",gl_enabled);
			else
				sprintf(msg,"%s",gl_disabled);
			DrawHZText12(msg,0,x_offset+15,main_row_y[5],(reset_pos==0)?gl_color_selected:gl_color_text,1);

			sprintf(msg,"%s",gl_lang_toggle_backup);
			DrawHZText12(msg,0,set_offset,main_row_y[6],gl_color_selected,1);
			Draw_select_icon(x_offset,main_row_y[6],(toggle_backup == 0x1));
			if(toggle_backup)
				sprintf(msg,"%s",gl_enabled);
			else
				sprintf(msg,"%s",gl_disabled);
			DrawHZText12(msg,0,x_offset+15,main_row_y[6],(backup_pos==0)?gl_color_selected:gl_color_text,1);

			for(line=0;line<line_total;line++)
			{
				u32 offsety = main_row_y[line];
				u32 highlighted;

				if(Set_OK==1)
				{
					if((line==0) && (select==0) && (auto_save_pos==1))
						highlighted = 1;
					else if((line==1) && (select==1) && (resume_last_pos==1))
						highlighted = 1;
					else if((line==2) && (select==2) && (boot_mode_pos==3))
						highlighted = 1;
					else if((line==3) && (select==3) && (ModeB_pos==3))
						highlighted = 1;
					else if((line==4) && (select==4) && (led_pos==1))
						highlighted = 1;
					else if((line==5) && (select==5) && (reset_pos==1))
						highlighted = 1;
					else if((line==6) && (select==6) && (backup_pos==1))
						highlighted = 1;
					else
						highlighted = 0;
				}
				else
				{
					highlighted = (line==select);
				}

				Draw_set2_button(offsety, highlighted, Set_OK && (line == Set_OK_line));
			}
			VBlankIntrWait();
		}

		currstate=Set_OK;
		switch(currstate) {
			case 0:
				re_show = 0;
				scanKeys();
				keys = keysDown();
				{
					u32 old_select = select;
					UIAudio_HandleKeys(keys & ~(KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_START | KEY_SELECT), 0);
				if (keys & KEY_A) {
					Set_OK_line = select;
					Set_OK = 1;
					re_show=1;
					if(select == 1)
						resume_last_pos = 0;
					else if(select == 2)
						boot_mode_pos = 0;
					else if(select == 3)
						ModeB_pos = 0;
					else if(select == 4)
						led_pos = 0;
				}
				else if (keys  & KEY_DOWN){
					if(select < line_total-1){
						u32 old_select = select;
						select++;
						Draw_set2_button(main_row_y[old_select], 0, 0);
						Draw_set2_button(main_row_y[select], 1, 0);
					}
				}
				else if(keys & KEY_UP){
					if(select){
						u32 old_select = select;
						select--;
						Draw_set2_button(main_row_y[old_select], 0, 0);
						Draw_set2_button(main_row_y[select], 1, 0);
					}
				}
				else if(keys & KEY_L) {
					return 0;
				}
				else if(keys & KEY_R) {
					return 1;
				}

					if((keys & (KEY_UP | KEY_DOWN)) && (select != old_select))
					{
						UIAudio_HandleKeys(0, KEY_DOWN);
					}
				}
				break;

			case 1:
				VBlankIntrWait();
				re_show = 0;
				scanKeys();
				keys = keysDown();
				{
					u8 old_auto_save_pos = auto_save_pos;
					u8 old_resume_last_pos = resume_last_pos;
					u8 old_boot_mode_pos = boot_mode_pos;
					u8 old_led_pos = led_pos;
					u8 old_reset_pos = reset_pos;
					u8 old_backup_pos = backup_pos;
					u8 old_ModeB_pos = ModeB_pos;
					int nav_input = 0;
					u32 nav_changed = 0;
					u32 play_accept_sfx = 0;
					UIAudio_HandleKeys(keys & ~(KEY_A | KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT | KEY_START | KEY_SELECT), 0);
					{
						u16 keys_released = keysUp();
					if(keys_released & KEY_UP) {
						if(led_pos>4)
							led_pos -= 3;
						else if(led_pos>1)
							led_pos = 0;
						re_show = 1;
					}
					else if(keys_released & KEY_DOWN) {
						if((select ==4) && (led_open_sel == 0x1))
						{
							if(led_pos<2)
								led_pos = 2;
							else if(led_pos<5)
								led_pos += 3;
							re_show = 1;
						}
					}
					else if(keys & KEY_RIGHT) {
						if(select == 0)
							auto_save_pos = 1;
						else if(select == 1)
							resume_last_pos = 1;
						else if(select == 2)
						{
							if(boot_mode_pos<3)
								boot_mode_pos++;
						}
						else if(select == 3)
						{
							if(ModeB_pos<3)
								ModeB_pos++;
						}
						else if(select == 4)
						{
							if((led_pos==0) || (led_pos==4 ) || (led_pos==7))
								led_pos = 1;
							else if((led_pos<4) && (led_pos>1))
								led_pos++;
							else if((led_pos<7) && (led_pos>1))
								led_pos++;
						}
						else if(select == 5)
							reset_pos = 1;
						else if(select == 6)
							backup_pos = 1;
						re_show = 1;
					}
					else if(keys & KEY_LEFT) {
						if(select == 0)
							auto_save_pos = 0;
						else if(select == 1)
							resume_last_pos = 0;
						else if(select == 2)
						{
							if(boot_mode_pos>0)
								boot_mode_pos--;
						}
						else if(select == 3)
						{
							if(ModeB_pos>0)
								ModeB_pos--;
						}
						else if(select == 4)
						{
							if(led_pos==1)
								led_pos = 0;
							else if(led_pos>5)
								led_pos--;
							else if(led_pos>2)
								led_pos--;
						}
						else if(select == 5)
							reset_pos = 0;
						else if(select == 6)
							backup_pos = 0;
						re_show = 1;
					}
					else if(keys & KEY_A) {
						if(select == 0)
						{
							switch(auto_save_pos)
							{
								case 0:auto_save_sel = !auto_save_sel; play_accept_sfx = 1; break;
								case 1:
									save_set2_info();
									Set_OK = 0;
									gl_auto_save_sel = Read_SET_info(assress_auto_save_sel) & 0x00FF;
									if((gl_auto_save_sel != 0x0) && (gl_auto_save_sel != 0x1))
										gl_auto_save_sel = 0x1;
									play_accept_sfx = 1;
									break;
							}
						}
						else if(select == 1)
						{
							switch(resume_last_pos)
							{
								case 0:resume_last_on = !resume_last_on; play_accept_sfx = 1; break;
								case 1:
									save_set2_info();
									Set_OK = 0;
									{
										u16 autosave_raw = Read_SET_info(assress_auto_save_sel);
										gl_resume_last_on = (autosave_raw >> 8) & 0x00FF;
										if((gl_resume_last_on != 0x0) && (gl_resume_last_on != 0x1))
											gl_resume_last_on = 0x0;
									}
									play_accept_sfx = 1;
									break;
							}
						}
						else if(select == 2)
						{
							switch(boot_mode_pos)
							{
								case 0:boot_mode_pref = 0; play_accept_sfx = 1; break;
								case 1:boot_mode_pref = 1; play_accept_sfx = 1; break;
								case 2:boot_mode_pref = 2; play_accept_sfx = 1; break;
								case 3:
									save_set2_info();
									Set_OK = 0;
									gl_boot_mode_pref = (Read_SET_info(assress_ModeB_INIT) >> 8) & 0x00FF;
									if((gl_boot_mode_pref != 0x0) && (gl_boot_mode_pref != 0x1) && (gl_boot_mode_pref != 0x2))
										gl_boot_mode_pref = 0x0;
									play_accept_sfx = 1;
									break;
							}
						}
						else if(select == 3)
						{
							switch(ModeB_pos)
							{
								case 0:ModeB_INIT = 0; play_accept_sfx = 1; break;
								case 1:ModeB_INIT = 1; play_accept_sfx = 1; break;
								case 2:ModeB_INIT = 2; play_accept_sfx = 1; break;
								case 3:
									save_set2_info();
									Set_OK = 0;
									play_accept_sfx = 1;
									break;
							}
						}
						else if(select == 4)
						{
							switch(led_pos)
							{
								case 0:led_open_sel = !led_open_sel; play_accept_sfx = 1; break;
								case 1:
									save_set2_info();
									Set_OK = 0;
									play_accept_sfx = 1;
									break;
								case 2:Breathing_R = !Breathing_R; play_accept_sfx = 1; break;
								case 3:Breathing_G = !Breathing_G; play_accept_sfx = 1; break;
								case 4:Breathing_B = !Breathing_B; play_accept_sfx = 1; break;
								case 5:SD_R = !SD_R; play_accept_sfx = 1; break;
								case 6:SD_G = !SD_G; play_accept_sfx = 1; break;
								case 7:SD_B = !SD_B; play_accept_sfx = 1; break;
							}
						}
						else if(select == 5)
						{
							switch(reset_pos)
							{
								case 0:toggle_reset = !toggle_reset; play_accept_sfx = 1; break;
								case 1:
									save_set2_info();
									Set_OK = 0;
									gl_toggle_reset = Read_SET_info(assress_toggle_reset);
									if((gl_toggle_reset != 0x0) && (gl_toggle_reset != 0x1))
										gl_toggle_reset = 0x1;
									play_accept_sfx = 1;
									break;
							}
						}
						else if(select == 6)
						{
							switch(backup_pos)
							{
								case 0:toggle_backup = !toggle_backup; play_accept_sfx = 1; break;
								case 1:
									save_set2_info();
									Set_OK = 0;
									gl_toggle_backup = Read_SET_info(assress_toggle_backup);
									if((gl_toggle_backup != 0x0) && (gl_toggle_backup != 0x1))
										gl_toggle_backup = 0x1;
									play_accept_sfx = 1;
									break;
							}
						}
						re_show = 1;
						if(play_accept_sfx)
						{
							UIAudio_HandleKeys(KEY_A, 0);
						}
					}

						nav_input = ((keys_released & (KEY_UP | KEY_DOWN)) || (keys & (KEY_LEFT | KEY_RIGHT)));
						nav_changed = (old_auto_save_pos != auto_save_pos) || (old_resume_last_pos != resume_last_pos) || (old_boot_mode_pos != boot_mode_pos) || (old_led_pos != led_pos) || (old_reset_pos != reset_pos) || (old_backup_pos != backup_pos) || (old_ModeB_pos != ModeB_pos);
						if(nav_input && nav_changed)
						{
							UIAudio_HandleKeys(0, KEY_DOWN);
						}
					}
				}
				break;
		}
	}
}

extern u16 SET_info_buffer [0x200]EWRAM_BSS;
void save_set2_info(void)
{
	u32 address;
	for(address=0;address < assress_max;address++)
	{
		SET_info_buffer[address] = Read_SET_info(address);
	}
	SET_info_buffer[assress_auto_save_sel] = (auto_save_sel & 0x00FF) | ((resume_last_on & 0x00FF) << 8);
	SET_info_buffer[assress_ModeB_INIT] = (ModeB_INIT & 0x00FF) | ((boot_mode_pref & 0x00FF) << 8);
	SET_info_buffer[assress_led_open_sel] = led_open_sel;
	SET_info_buffer[assress_Breathing_R] = Breathing_R;
	SET_info_buffer[assress_Breathing_G] = Breathing_G;
	SET_info_buffer[assress_Breathing_B] = Breathing_B;
	SET_info_buffer[assress_SD_R] = SD_R;
	SET_info_buffer[assress_SD_G] = SD_G;
	SET_info_buffer[assress_SD_B] = SD_B;
	SET_info_buffer[assress_toggle_backup] = toggle_backup;
	SET_info_buffer[assress_toggle_reset] = toggle_reset;

	Save_SET_info(SET_info_buffer,0x200);
	{
		u16 led_status = (led_open_sel<<7) | (Breathing_R<<5) | (Breathing_G<<4) | (Breathing_B<<3) | (SD_R<<2) | (SD_G<<1) | (SD_B);
		Set_LED_control(led_status);
	}
}
