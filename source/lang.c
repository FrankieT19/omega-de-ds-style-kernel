#include "lang.h"
#include "launcher_text.h"

#include "asc126_old.h"
#include "asc126_new.h"

char* gl_init_error;
char* gl_power_off;
char* gl_init_ok;
char* gl_Loading;
char* gl_file_overflow;
char* gl_theme_credit;
char* gl_theme_credit2;

char* gl_generating_emu;

char* gl_menu_btn;
char* gl_lastest_game;
int gl_font_above_extent = 0;

char* gl_writing;

char* gl_time;
char* gl_Mon;
char* gl_Tues;
char* gl_Wed;
char* gl_Thur;
char* gl_Fri;
char* gl_Sat;
char* gl_Sun;

char* gl_addon;
char* gl_reset;
char* gl_rts;
char* gl_sleep;
char* gl_cheat;

char* gl_hot_key;
char* gl_hot_key2;

char* gl_language;
char* gl_en_lang;
char* gl_zh_lang;
char* gl_set_btn;
char* gl_ok_btn;

char* gl_formatnor_info1;
char* gl_formatnor_info2;

char* temp;

char* gl_check_sav;
char* gl_make_sav;

char* gl_check_RTS;
char* gl_make_RTS;

char* gl_check_pat;
char* gl_make_pat;

char* gl_loading_game;

char* gl_engine;
char* gl_use_engine;

char*  gl_recently_play;

char* gl_START_help;
char* gl_SELECT_help;
char* gl_L_A_help;
char* gl_LSTART_help;
char* gl_online_manual;

char* gl_no_game_played;

char* gl_ingameRTC;
//char* gl_offRTC_powersave;
char* gl_ingameRTC_open;
char* gl_ingameRTC_close;

char* gl_lang_toggle_reset;
char* gl_lang_toggle_backup;

char* gl_error_0;
char* gl_error_1;
char* gl_error_2;
char* gl_error_3;
char* gl_error_4;
char* gl_error_5;
char* gl_error_6;

char* gl_save_sav;
char* gl_save_ing;

char* gl_save;
char* gl_auto_save;

char* gl_modeB_INITstr;
char* gl_modeB_RUMBLE;
char* gl_modeB_RAM;
char* gl_modeB_LINK;

char* gl_led;
char* gl_led_open;

char* gl_Breathing_light;
char* gl_SD_working;

char* gl_NOR_full;
char* gl_save_loaded;
char* gl_save_saved;
char* gl_file_exist;
char* gl_file_noexist;
//--
char**  gl_rom_menu;
char**  gl_nor_op;

char* gl_copying_data;

char* gl_enabled;
char* gl_disabled;

unsigned char* ASC_DATA;


//����
const char zh_init_error[]="TF����ʼ��ʧ��";
const char zh_power_off[]="�ػ�";
const char zh_init_ok[]="TF����ʼ���ɹ�";
const char zh_Loading[]="������...";
const char zh_file_overflow[]="�ļ�̫��,���ܼ���";

const char zh_menu_btn[]=" (B)ȡ��    (A)ȷ��";
const char zh_writing[]="����д��...";
const char zh_lastest_game[]="��ѡ�����һ����Ϸ";

const char zh_time[] ="     ʱ��";
const char zh_Mon[]="һ";
const char zh_Tues[]="��";
const char zh_Wed[]="��";
const char zh_Thur[]="��";
const char zh_Fri[]="��";
const char zh_Sat[]="��";
const char zh_Sun[]="��";

const char zh_addon[]="     ����";
const char zh_reset[]="����λ";
const char zh_rts[]="��ʱ�浵";
const char zh_sleep[]="˯��";
const char zh_cheat[]="����ָ";

const char zh_hot_key[]=" ˯���ȼ�";
const char zh_hot_key2[]=" �˵��ȼ�";

const char zh_language[]=" LANGUAGE";
const char zh_lang[]=" ����";

const char zh_set_btn[]="����";
const char zh_ok_btn[]="����";
const char zh_formatnor_info[]="ȷ��?��Լ4����";

const char zh_theme_credit[]="DS Style v6";
const char zh_theme_credit2[]="by FrankieT19";

const char zh_check_sav[]="���SAV�ļ�";
const char zh_make_sav[]="����SAV�ļ�";

const char zh_check_RTS[]="���RTS�ļ�";
const char zh_make_RTS[]="����RTS�ļ�";

const char zh_check_pat[]="���PAT�ļ�";
const char zh_make_pat[]="����PAT�ļ�";

const char zh_please_wait[]="��ȴ�...";

const char zh_loading_game[]="������Ϸ";

const char zh_no_roms[]="�Ҳ���.gba�ļ�!";

const char zh_engine[]="     ����";
const char zh_use_engine[]="���ٲ�������";

const char zh_recently_play[]="�����Ϸ�б�";

const char zh_START_help[]="�������Ϸ�б�";
const char zh_SELECT_help[]="�л���Ϸ����ͼ";
const char zh_L_A_help[]="��ѡ������ѡ��";
const char zh_LSTART_help[]="ɾ���ļ�";
const char zh_LSELECT_help[]="ɾ���浵";
const char zh_online_manual[]="  ����˵����";

const char zh_no_game_played[]="�����û�����Ϸ";

const char zh_ingameRTC[]=" ��Ϸʱ��";
//const char zh_offRTC_powersave[]=" ";
const char zh_ingameRTC_open[]="��";
const char zh_ingameRTC_close[]="�ر�";//TURNOFF TO POWER SAVE

const char zh_lang_toggle_reset[]="Ӳ����";
const char zh_lang_toggle_backup[]="���汸��";

const char zh_error_0[]="�ļ��д���";
const char zh_error_1[]="�ļ�����";
const char zh_error_2[]="SAVER����";
const char zh_error_3[]="�浵����";
const char zh_error_4[]="��ȡ�浵����";
const char zh_error_5[]="�����浵����";
const char zh_error_6[]="RTS�ļ�����";

const char zh_save_sav[]="����浵?";
const char zh_save_ing[]="����...";
const char zh_save[]="     �浵";
const char zh_auto_save[]="�����Զ�����";

const char zh_modeB_INITstr[]="ģʽB״̬";
const char zh_modeB_RUMBLE[]="��";
const char zh_modeB_RAM[]="�ڴ�";
const char zh_modeB_LINK[]="����";

const char zh_led[]="   ָʾ��";
const char zh_led_open[]="��LED";
const char zh_Breathing_light[]="   ������";
const char zh_SD_working[]=" SD������";

const char zh_NOR_full[]="NOR�ռ䲻��";
const char zh_save_loaded[]="�浵�Ѽ��ص�FRAM";
const char zh_save_saved[]="�浵�ѱ��浽SD";
const char zh_file_exist[]="�ļ�����,������?";
const char zh_file_noexist[]="�Ҳ����浵�ļ�";

const char zh_copying_data[]="����ROM...";
const char zh_generating_emu[]="����ģ����...";

const char zh_enabled[]="������";
const char zh_disabled[]="�ѽ���";

const char *zh_rom_menu[]={
	"ֱ������",
	"����������",
	"��¼��NOR",
	"��¼��NOR������",
	"�浵����",
	"����ָ",
};
const char *zh_nor_op[5]={
	"ֱ������",
	"ɾ��",
	"ȫ����ʽ��",
	"���ش浵��FRAM",
	"����FRAM�浵",
};



//English
const char en_init_error[]=DSTEXT_STATUS_INIT_ERROR;
const char en_power_off[]=DSTEXT_STATUS_POWER_OFF;
const char en_init_ok[]=DSTEXT_STATUS_INIT_OK;
const char en_Loading[]=DSTEXT_STATUS_LOADING;
const char en_file_overflow[]=DSTEXT_STATUS_FILE_TOO_BIG;

const char en_menu_btn[]=" (B) No     (A) OK";
const char en_writing[]=DSTEXT_STATUS_WRITING;
const char en_lastest_game[]=DSTEXT_STATUS_SELECT_LATEST;

const char en_time[]="     Time";
const char en_Mon[]="Mon";
const char en_Tues[]="Tue";
const char en_Wed[]="Wed";
const char en_Thur[]="Thu";
const char en_Fri[]="Fri";
const char en_Sat[]="Sat";
const char en_Sun[]="Sun";

const char en_addon[]="    Addon";
const char en_reset[]="Reset";
const char en_rts[]="Savestate";
const char en_sleep[]="Sleep";
const char en_cheat[]="Cheat";

const char en_hot_key[] ="Sleep key";
const char en_hot_key2[]=" Menu key";

const char en_language[]=" Language";
const char en_lang[]="English";
const char en_set_btn[]="Set";
const char en_ok_btn[]=" OK";
const char en_formatnor_info1[]="Are you sure?";
const char en_formatnor_info2[]="This will take a while.";

const char en_theme_credit[]="DS Style v6";
const char en_theme_credit2[]="by FrankieT19";

const char en_check_sav[]=DSTEXT_STATUS_CHECK_SAVE;
const char en_make_sav[]=DSTEXT_STATUS_MAKE_SAVE;

const char en_check_RTS[]=DSTEXT_STATUS_CHECK_RTS;
const char en_make_RTS[]=DSTEXT_STATUS_MAKE_RTS;

const char en_check_pat[]=DSTEXT_STATUS_CHECK_PATCH;
const char en_make_pat[]=DSTEXT_STATUS_MAKE_PATCH;

const char en_please_wait[]="Please Wait...";

const char en_loading_game[]=DSTEXT_STATUS_LOADING_ROM;

const char en_no_roms[]=DSTEXT_STATUS_NO_ROMS;

const char en_engine[]="   Engine";
const char en_use_engine[]="Fast Patch Engine";

const char en_recently_play[]="Recently Played";

const char en_START_help[]="Open recently played list";
const char en_SELECT_help[]="Toggle game thumbnails";
const char en_L_A_help[]="Invert cold start option";
const char en_LSTART_help[]="Delete file";
const char en_LSELECT_help[]="Delete save file";
const char en_online_manual[]="Online manual";

const char en_no_game_played[]="No recently played games yet...";

const char en_ingameRTC[]=" Game RTC";
const char en_ingameRTC_open[]="Open";
const char en_ingameRTC_close[]="Close";//TURNOFF TO POWER SAVE

const char en_lang_toggle_reset[]="Full Boot";
const char en_lang_toggle_backup[]="   Backup";

const char en_error_0[]=DSTEXT_ERROR_FOLDER;
const char en_error_1[]=DSTEXT_ERROR_FILE;
const char en_error_2[]=DSTEXT_ERROR_SAVER;
const char en_error_3[]=DSTEXT_ERROR_SAVE;
const char en_error_4[]=DSTEXT_ERROR_READ_SAVE;
const char en_error_5[]=DSTEXT_ERROR_MAKE_SAVE;
const char en_error_6[]=DSTEXT_ERROR_RTS;


const char en_save_sav[]=DSTEXT_STATUS_COPY_SAVE;
const char en_save_ing[]=DSTEXT_STATUS_SAVING;
const char en_save[]="     Save";
const char en_auto_save[]="Auto save";

const char en_modeB_INITstr[]="   Mode B";
const char en_modeB_RUMBLE[]="Rumble";
const char en_modeB_RAM[]="RAM";
const char en_modeB_LINK[]="Cart";

const char en_led[]="      LED";
const char en_led_open[]="Enable LED";
const char en_Breathing_light[]="Breathing";
const char en_SD_working[]="   SD LED";

const char en_NOR_full[]=DSTEXT_STATUS_NOR_FULL;
const char en_save_loaded[]=DSTEXT_STATUS_SAVE_LOADED;
const char en_save_saved[]=DSTEXT_STATUS_SAVE_SAVED;
const char en_file_exist[]=DSTEXT_STATUS_FILE_EXISTS;
const char en_file_noexist[]=DSTEXT_STATUS_SAVE_NOT_FOUND;

const char en_copying_data[]=DSTEXT_STATUS_COPYING_ROM;
const char en_generating_emu[]=DSTEXT_STATUS_GENERATING_EMU;

const char en_enabled[]="Enabled";
const char en_disabled[]="Disabled";

const char *en_rom_menu[] = {
	DSTEXT_ROM_MENU_CLEAN,
	DSTEXT_ROM_MENU_ADDON,
	DSTEXT_ROM_MENU_WRITE_NOR,
	DSTEXT_ROM_MENU_WRITE_NOR_ADDON,
	DSTEXT_ROM_MENU_SAVE_TYPE,
	DSTEXT_ROM_MENU_CHEAT,
};
const char *en_nor_op[5]={
	DSTEXT_NOR_MENU_DIRECT,
	DSTEXT_NOR_MENU_DELETE,
	DSTEXT_NOR_MENU_FORMAT,
	DSTEXT_NOR_MENU_LOAD_SAVE,
	DSTEXT_NOR_MENU_SAVE_SAVE,
};

//---------------------------------------------------------------------------------

// Thai
const char th_init_error[]   = "ไม่สามารถเริ่มต้น SD";
const char th_power_off[]    = "ปิดเครื่อง";
const char th_init_ok[]      = "เริ่มต้น SD สำเร็จ";
const char th_Loading[]      = "กำลังโหลด...";
const char th_file_overflow[]= "ไฟล์ใหญ่เกินไป";

const char th_menu_btn[]     = "(B)ยกเลิก   (A)ตกลง";
const char th_writing[]      = "กำลังเขียน...";
const char th_lastest_game[] = "เลือกเกมล่าสุด";

const char th_time[]         = "     เวลา";
const char th_Mon[]          = "จัน";
const char th_Tues[]         = "อัง";
const char th_Wed[]          = "พุธ";
const char th_Thur[]         = "พฤหัส";
const char th_Fri[]          = "ศุกร์";
const char th_Sat[]          = "เสาร์";
const char th_Sun[]          = "อาทิตย์";

const char th_addon[]        = " ส่วนเสริม";
const char th_reset[]        = "รีเซ็ต";
const char th_rts[]          = "เซฟสเตท";
const char th_sleep[]        = "สลีป";
const char th_cheat[]        = "สูตรโกง";

const char th_hot_key[]      = "ปุ่มลัดสลีป";
const char th_hot_key2[]     = "ปุ่มลัดเมนู";

const char th_language[]     = " ภาษา";
const char th_lang[]         = "ไทย";
const char th_zh_lang[]      = "จีน";
const char th_set_btn[]      = "ตั้งค่า";
const char th_ok_btn[]       = "ตกลง";
const char th_formatnor_info1[] = "ยืนยัน?";
const char th_formatnor_info2[] = "ใช้เวลาสักครู่";

const char th_theme_credit[] = "DS Style v6";
const char th_theme_credit2[]= "by FrankieT19";

const char th_check_sav[]    = "ตรวจสอบ SAV...";
const char th_make_sav[]     = "สร้าง SAV...";

const char th_check_RTS[]    = "ตรวจสอบ RTS...";
const char th_make_RTS[]     = "สร้าง RTS...";

const char th_check_pat[]    = "ตรวจสอบแพทช์...";
const char th_make_pat[]     = "สร้างแพทช์...";

const char th_loading_game[] = "กำลังโหลดเกม";

const char th_engine[]       = "   เอ็นจิน";
const char th_use_engine[]   = "เอ็นจินเร็ว";

const char th_recently_play[]= "เพิ่งเล่น";

const char th_START_help[]   = "เล่นล่าสุด/โปรด";
const char th_SELECT_help[]  = "สลับมุมมอง";
const char th_L_A_help[]     = "สลับตัวเลือก";
const char th_LSTART_help[]  = "ลบไฟล์";
const char th_online_manual[]= "คู่มือออนไลน์";

const char th_no_game_played[]= "ยังไม่มีเกม";

const char th_ingameRTC[]    = "  นาฬิกา";
const char th_ingameRTC_open[]= "เปิด";
const char th_ingameRTC_close[]= "ปิด";

const char th_lang_toggle_reset[]= "บูตเต็ม";
const char th_lang_toggle_backup[]= "สำรอง";

const char th_error_0[]      = "ข้อผิดพลาดของโฟลเดอร์";
const char th_error_1[]      = "ข้อผิดพลาดไฟล์";
const char th_error_2[]      = "ข้อผิดพลาด SAVER";
const char th_error_3[]      = "ข้อผิดพลาดการบันทึก";
const char th_error_4[]      = "ข้อผิดพลาดการอ่านเซฟ";
const char th_error_5[]      = "ข้อผิดพลาดการสร้างเซฟ";
const char th_error_6[]      = "ข้อผิดพลาด RTS";

const char th_save_sav[]     = "บันทึก?";
const char th_save_ing[]     = "กำลังบันทึก...";
const char th_save[]         = "    บันทึก";
const char th_auto_save[]    = "บันทึกอัตโนมัติ";

const char th_modeB_INITstr[]= "  โหมด B";
const char th_modeB_RUMBLE[] = "สั่น";
const char th_modeB_RAM[]    = "RAM";
const char th_modeB_LINK[]   = "ลิงค์";

const char th_led[]          = "   LED";
const char th_led_open[]     = "เปิด LED";
const char th_Breathing_light[]= "Breathing";
const char th_SD_working[]   = "   SD LED";

const char th_NOR_full[]     = "NOR เต็มแล้ว!";
const char th_save_loaded[]  = "โหลดเซฟแล้ว";
const char th_save_saved[]   = "บันทึกเซฟแล้ว";
const char th_file_exist[]   = "เขียนทับไฟล์?";
const char th_file_noexist[] = "ไม่พบไฟล์เซฟ";

const char th_copying_data[] = "กำลังคัดลอก ROM...";
const char th_generating_emu[]= "สร้างโปรแกรมจำลอง...";

const char th_enabled[]      = "เปิดใช้งาน";
const char th_disabled[]     = "ปิดใช้งาน";

const char *th_rom_menu[] = {
	"โหลดตรง",
	"โหลดพร้อมส่วนเสริม",
	"บันทึก NOR",
	"บันทึก NOR+ส่วนเสริม",
	"ประเภทเซฟ",
	"สูตรโกง",
};
const char *th_nor_op[] = {
	"โหลดตรง",
	"ลบ",
	"ฟอร์แมตทั้งหมด",
	"โหลดเซฟ FRAM",
	"บันทึกเซฟ FRAM",
};

//---------------------------------------------------------------------------------

void LoadChinese(void)
{
	gl_init_error = (char*)zh_init_error;
	gl_power_off = (char*)zh_power_off;
	gl_init_ok = (char*)zh_init_ok;
	gl_Loading = (char*)zh_Loading;
	gl_file_overflow = (char*)zh_file_overflow;
	gl_theme_credit = (char*)zh_theme_credit;
	gl_theme_credit2 = (char*)zh_theme_credit2;

	gl_menu_btn = (char*)zh_menu_btn;
	gl_writing = (char*)zh_writing;
	gl_lastest_game = (char*)zh_lastest_game;


	gl_time = (char*)zh_time;
	gl_Mon = (char*)zh_Mon;
	gl_Tues = (char*)zh_Tues;
	gl_Wed = (char*)zh_Wed;
	gl_Thur = (char*)zh_Thur;
	gl_Fri = (char*)zh_Fri;
	gl_Sat = (char*)zh_Sat;
	gl_Sun = (char*)zh_Sun;

	gl_addon = (char*)zh_addon;
	gl_reset = (char*)zh_reset;
	gl_rts = (char*)zh_rts;
	gl_sleep = (char*)zh_sleep;
	gl_cheat = (char*)zh_cheat;

	gl_hot_key = (char*)zh_hot_key;
	gl_hot_key2 = (char*)zh_hot_key2;

	gl_language =  (char*)zh_language;
	gl_en_lang = (char*)en_lang;
	gl_zh_lang = (char*)zh_lang;;
	gl_set_btn = (char*)zh_set_btn;
	gl_ok_btn = (char*)zh_ok_btn;
	gl_formatnor_info1 = (char*)zh_formatnor_info;
	gl_formatnor_info2= " ";

	temp = " ";


	gl_check_sav = (char*)zh_check_sav;
	gl_make_sav = (char*)zh_make_sav;

	gl_check_RTS = (char*)zh_check_RTS;
	gl_make_RTS = (char*)zh_make_RTS;

	gl_check_pat = (char*)zh_check_pat;
	gl_make_pat = (char*)zh_make_pat;

	gl_loading_game = (char*)zh_loading_game;
	gl_engine = (char*)zh_engine;
	gl_use_engine = (char*)zh_use_engine;

	gl_recently_play = (char*)zh_recently_play;

	gl_START_help = (char*)zh_START_help;
	gl_SELECT_help = (char*)zh_SELECT_help;
	gl_L_A_help = (char*)zh_L_A_help;
	gl_LSTART_help = (char*)zh_LSTART_help;
	gl_online_manual = (char*)zh_online_manual;

	gl_no_game_played = (char*)zh_no_game_played;

	gl_ingameRTC = (char*)zh_ingameRTC;
	//gl_offRTC_powersave = (char*)zh_offRTC_powersave;
	gl_ingameRTC_open = (char*)zh_ingameRTC_open;
	gl_ingameRTC_close = (char*)zh_ingameRTC_close;

	gl_lang_toggle_reset = (char*)zh_lang_toggle_reset;
	gl_lang_toggle_backup = (char*)zh_lang_toggle_backup;

	gl_error_0 = (char*)zh_error_0;
	gl_error_1 = (char*)zh_error_1;
	gl_error_2 = (char*)zh_error_2;
	gl_error_3 = (char*)zh_error_3;
	gl_error_4 = (char*)zh_error_4;
	gl_error_5 = (char*)zh_error_5;
	gl_error_6 = (char*)zh_error_6;

	gl_save_sav = (char*)zh_save_sav;
	gl_save_ing = (char*)zh_save_ing;
	gl_save = (char*)zh_save;
	gl_auto_save = (char*)zh_auto_save;

	gl_modeB_INITstr = (char*)zh_modeB_INITstr;
	gl_modeB_RUMBLE = (char*)zh_modeB_RUMBLE;
	gl_modeB_RAM= (char*)zh_modeB_RAM;
	gl_modeB_LINK= (char*)zh_modeB_LINK;

	gl_led = (char*)zh_led;
	gl_led_open = (char*)zh_led_open;

	gl_Breathing_light = (char*)zh_Breathing_light;
	gl_SD_working = (char*)zh_SD_working;

	gl_NOR_full = (char*)zh_NOR_full;
	gl_save_loaded = (char*)zh_save_loaded;
	gl_save_saved = (char*)zh_save_saved;
	gl_file_exist = (char*)zh_file_exist;
	gl_file_noexist = (char*)zh_file_noexist;
	//
	gl_rom_menu = (char**)zh_rom_menu;
	gl_nor_op = (char**)zh_nor_op;

	gl_copying_data = (char*)zh_copying_data;

	gl_generating_emu = (char*)zh_generating_emu;

	gl_enabled = (char*)zh_enabled;
	gl_disabled = (char*)zh_disabled;

	// For Chinese, Use old font
	gl_font_above_extent = 0;
	ASC_DATA = (unsigned char*)ASC_DATA_NEW;

}
//---------------------------------------------------------------------------------
void LoadEnglish(void)
{
	gl_init_error = (char*)en_init_error;
	gl_power_off = (char*)en_power_off;
	gl_init_ok = (char*)en_init_ok;
	gl_Loading = (char*)en_Loading;
	gl_file_overflow = (char*)en_file_overflow;
	gl_theme_credit = (char*)en_theme_credit;
	gl_theme_credit2 = (char*)en_theme_credit2;

	gl_menu_btn = (char*)en_menu_btn;
	gl_writing = (char*)en_writing;
	gl_lastest_game = (char*)en_lastest_game;

	gl_time = (char*)en_time;
	gl_Mon = (char*)en_Mon;
	gl_Tues = (char*)en_Tues;
	gl_Wed = (char*)en_Wed;
	gl_Thur = (char*)en_Thur;
	gl_Fri = (char*)en_Fri;
	gl_Sat = (char*)en_Sat;
	gl_Sun = (char*)en_Sun;
	gl_addon = (char*)en_addon;
	gl_reset = (char*)en_reset;
	gl_rts = (char*)en_rts;
	gl_sleep = (char*)en_sleep;
	gl_cheat = (char*)en_cheat;

	gl_hot_key = (char*)en_hot_key;
	gl_hot_key2 = (char*)en_hot_key2;

	gl_language =  (char*)en_language;
	gl_en_lang = (char*)en_lang;
	gl_zh_lang = (char*)zh_lang;;
	gl_set_btn = (char*)en_set_btn;
	gl_ok_btn = (char*)en_ok_btn;
	gl_formatnor_info1 = (char*)en_formatnor_info1;
	gl_formatnor_info2= (char*)en_formatnor_info2;

	temp = "Sure? about 4 mins";

	gl_check_sav = (char*)en_check_sav;
	gl_make_sav = (char*)en_make_sav;

	gl_check_RTS = (char*)en_check_RTS;
	gl_make_RTS = (char*)en_make_RTS;

	gl_check_pat = (char*)en_check_pat;
	gl_make_pat = (char*)en_make_pat;

	gl_loading_game = (char*)en_loading_game;

	gl_engine = (char*)en_engine;
	gl_use_engine = (char*)en_use_engine;

	gl_recently_play = (char*)en_recently_play;

	gl_START_help = (char*)en_START_help;
	gl_SELECT_help = (char*)en_SELECT_help;
	gl_L_A_help = (char*)en_L_A_help;
	gl_LSTART_help = (char*)en_LSTART_help;
	gl_online_manual = (char*)en_online_manual;

	gl_no_game_played = (char*)en_no_game_played;

	gl_ingameRTC = (char*)en_ingameRTC;
	//gl_offRTC_powersave = (char*)en_offRTC_powersave;
	gl_ingameRTC_open = (char*)en_ingameRTC_open;
	gl_ingameRTC_close = (char*)en_ingameRTC_close;

	gl_lang_toggle_reset = (char*)en_lang_toggle_reset;
	gl_lang_toggle_backup = (char*)en_lang_toggle_backup;

	gl_error_0 = (char*)en_error_0;
	gl_error_1 = (char*)en_error_1;
	gl_error_2 = (char*)en_error_2;
	gl_error_3 = (char*)en_error_3;
	gl_error_4 = (char*)en_error_4;
	gl_error_5 = (char*)en_error_5;
	gl_error_6 = (char*)en_error_6;

	gl_save_sav = (char*)en_save_sav;
	gl_save_ing = (char*)en_save_ing;
	gl_save = (char*)en_save;
	gl_auto_save = (char*)en_auto_save;

	gl_modeB_INITstr = (char*)en_modeB_INITstr;
	gl_modeB_RUMBLE = (char*)en_modeB_RUMBLE;
	gl_modeB_RAM = (char*)en_modeB_RAM;
	gl_modeB_LINK= (char*)en_modeB_LINK;

	gl_led = (char*)en_led;
	gl_led_open = (char*)en_led_open;
	gl_Breathing_light = (char*)en_Breathing_light;
	gl_SD_working = (char*)en_SD_working;

	gl_NOR_full = (char*)en_NOR_full;
	gl_save_loaded = (char*)en_save_loaded;
	gl_save_saved = (char*)en_save_saved;
	gl_file_exist = (char*)en_file_exist;
	gl_file_noexist = (char*)en_file_noexist;
	//
	gl_rom_menu = (char**)en_rom_menu;
	gl_nor_op = (char**)en_nor_op;

	gl_copying_data = (char*)en_copying_data;

	gl_generating_emu = (char*)en_generating_emu;

	gl_enabled = (char*)en_enabled;
	gl_disabled = (char*)en_disabled;

	// For English, Use new font
	gl_font_above_extent = 0;
	ASC_DATA = (unsigned char*)ASC_DATA_NEW;
}
//---------------------------------------------------------------------------------
void LoadThai(void)
{
	gl_init_error          = (char*)th_init_error;
	gl_power_off           = (char*)th_power_off;
	gl_init_ok             = (char*)th_init_ok;
	gl_Loading             = (char*)th_Loading;
	gl_file_overflow       = (char*)th_file_overflow;
	gl_theme_credit        = (char*)th_theme_credit;
	gl_theme_credit2       = (char*)th_theme_credit2;

	gl_menu_btn            = (char*)th_menu_btn;
	gl_writing             = (char*)th_writing;
	gl_lastest_game        = (char*)th_lastest_game;

	gl_time                = (char*)th_time;
	gl_Mon                 = (char*)th_Mon;
	gl_Tues                = (char*)th_Tues;
	gl_Wed                 = (char*)th_Wed;
	gl_Thur                = (char*)th_Thur;
	gl_Fri                 = (char*)th_Fri;
	gl_Sat                 = (char*)th_Sat;
	gl_Sun                 = (char*)th_Sun;
	gl_addon               = (char*)th_addon;
	gl_reset               = (char*)th_reset;
	gl_rts                 = (char*)th_rts;
	gl_sleep               = (char*)th_sleep;
	gl_cheat               = (char*)th_cheat;

	gl_hot_key             = (char*)th_hot_key;
	gl_hot_key2            = (char*)th_hot_key2;
	
	gl_language            = (char*)th_language;
	gl_en_lang             = (char*)en_lang;
	gl_zh_lang             = (char*)th_zh_lang;
	gl_set_btn             = (char*)th_set_btn;
	gl_ok_btn              = (char*)th_ok_btn;
	gl_formatnor_info1     = (char*)th_formatnor_info1;
	gl_formatnor_info2     = (char*)th_formatnor_info2;

	temp                   = " ";
	
	gl_check_sav           = (char*)th_check_sav;
	gl_make_sav            = (char*)th_make_sav;

	gl_check_RTS           = (char*)th_check_RTS;
	gl_make_RTS            = (char*)th_make_RTS;
	
	gl_check_pat           = (char*)th_check_pat;
	gl_make_pat            = (char*)th_make_pat;
	
	gl_loading_game        = (char*)th_loading_game;
	
	gl_engine              = (char*)th_engine;
	gl_use_engine          = (char*)th_use_engine;
	
	gl_recently_play       = (char*)th_recently_play;
	
	gl_START_help          = (char*)th_START_help;
	gl_SELECT_help         = (char*)th_SELECT_help;
	gl_L_A_help            = (char*)th_L_A_help;
	gl_LSTART_help         = (char*)th_LSTART_help;
	gl_online_manual       = (char*)th_online_manual;
	
	gl_no_game_played      = (char*)th_no_game_played;
	
	gl_ingameRTC           = (char*)th_ingameRTC;
	gl_ingameRTC_open      = (char*)th_ingameRTC_open;
	gl_ingameRTC_close     = (char*)th_ingameRTC_close;
	
	gl_lang_toggle_reset   = (char*)th_lang_toggle_reset;
	gl_lang_toggle_backup  = (char*)th_lang_toggle_backup;
	
	gl_error_0             = (char*)th_error_0;
	gl_error_1             = (char*)th_error_1;
	gl_error_2             = (char*)th_error_2;
	gl_error_3             = (char*)th_error_3;
	gl_error_4             = (char*)th_error_4;
	gl_error_5             = (char*)th_error_5;
	gl_error_6             = (char*)th_error_6;
	
	gl_save_sav            = (char*)th_save_sav;
	gl_save_ing            = (char*)th_save_ing;
	gl_save                = (char*)th_save;
	gl_auto_save           = (char*)th_auto_save;
	
	gl_modeB_INITstr       = (char*)th_modeB_INITstr;
	gl_modeB_RUMBLE        = (char*)th_modeB_RUMBLE;
	gl_modeB_RAM           = (char*)th_modeB_RAM;
	gl_modeB_LINK          = (char*)th_modeB_LINK;
	
	gl_led                 = (char*)th_led;
	gl_led_open            = (char*)th_led_open;
	gl_Breathing_light     = (char*)th_Breathing_light;
	gl_SD_working          = (char*)th_SD_working;
	
	gl_NOR_full            = (char*)th_NOR_full;
	gl_save_loaded         = (char*)th_save_loaded;
	gl_save_saved          = (char*)th_save_saved;
	gl_file_exist          = (char*)th_file_exist;
	gl_file_noexist        = (char*)th_file_noexist;
	
	gl_rom_menu            = (char**)th_rom_menu;
	gl_nor_op              = (char**)th_nor_op;
	
	gl_copying_data        = (char*)th_copying_data;
	
	gl_generating_emu      = (char*)th_generating_emu;
	
	gl_enabled             = (char*)th_enabled;
	gl_disabled            = (char*)th_disabled;
	
	gl_font_above_extent   = 1;
	ASC_DATA               = (unsigned char*)ASC_DATA_NEW;
}
