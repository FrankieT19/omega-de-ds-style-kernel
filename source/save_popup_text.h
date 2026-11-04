#ifndef DS_STYLE_SAVE_POPUP_TEXT_H
#define DS_STYLE_SAVE_POPUP_TEXT_H
static const struct { u16 language; const char *line[4]; } launcher_save_popup_text[] = {
    {0xE1E1, {"Save data to SD card?", "Saving game data...", "Please do not turn off", "the power."}},
    {0xE101, {"Save data to SD card?", "Saving game data...", "Please do not turn off", "the power."}},
    {0xE102, {"\302\277Guardar la partida?", "Guardando la partida...", "No apagues la consola.", ""}},
    {0xE103, {"Enregistrer la partie ?", "Enregistrement...", "Ne mettez pas la console", "hors tension."}},
    {0xE104, {"Guardar dados do jogo?", "A guardar dados...", "N\303\243o desligues a consola.", ""}},
    {0xE105, {"Spieldaten speichern?", "Spieldaten speichern...", "Bitte nicht ausschalten.", ""}},
    {0xE106, {"Oyun kaydedilsin mi?", "Oyun kaydediliyor...", "L\303\274tfen g\303\274c\303\274 kapatmay\304\261n.", ""}},
    {0xE107, {"Salvare i dati di gioco?", "Salvataggio in corso...", "Non spegnere la console.", ""}},
    {0xE108, {"Spelgegevens opslaan?", "Spelgegevens opslaan...", "Zet het systeem niet uit.", ""}},
    {0xE109, {"Vill du spara speldata?", "Sparar speldata...", "St\303\244ng inte av konsolen.", ""}},
    {0xE10A, {"Tallennetaanko peli?", "Tallennetaan peli\303\244...", "\303\204l\303\244 katkaise virtaa.", ""}},
    {0xE2E2, {"\261\243\264\346\323\316\317\267\312\375\276\335\243\277", "\325\375\324\332\261\243\264\346\323\316\317\267\312\375\276\335...", "\307\353\316\360\271\330\261\325\265\347\324\264\241\243", ""}},
    {0x0E01, {"\340\270\232\340\270\261\340\270\231\340\270\227\340\270\266\340\270\201\340\270\202\340\271\211\340\270\255\340\270\241\340\270\271\340\270\245\340\271\200\340\270\201\340\270\241?", "\340\270\201\340\270\263\340\270\245\340\270\261\340\270\207\340\270\232\340\270\261\340\270\231\340\270\227\340\270\266\340\270\201\340\270\202\340\271\211\340\270\255\340\270\241\340\270\271\340\270\245\340\271\200\340\270\201\340\270\241...", "\340\271\202\340\270\233\340\270\243\340\270\224\340\270\255\340\270\242\340\271\210\340\270\262\340\270\233\340\270\264\340\270\224\340\271\200\340\270\204\340\270\243\340\270\267\340\271\210\340\270\255\340\270\207", ""}},
};
static const char *Launcher_SavePopupText(u32 key)
{
    u32 i;
    if(key >= 4) return "";
    for(i=0;i<sizeof(launcher_save_popup_text)/sizeof(launcher_save_popup_text[0]);i++)
        if(launcher_save_popup_text[i].language == gl_select_lang) return launcher_save_popup_text[i].line[key];
    return launcher_save_popup_text[0].line[key];
}
#endif
