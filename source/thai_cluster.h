/*
 * thai_cluster.h - Thai UTF-8 cluster renderer
 * Created by aidia_dayo on 20/6/2569 BE.
 *
 * Include thai620.h first, then define these 6 macros before including this file:
 *
 *   THAI_R_DRAW(idx, x, yoff) - draw THAI_DATA[idx] at pixel x, row offset yoff
 *   THAI_R_WIDTH(idx)         - pixel advance width for glyph idx (scaled)
 *   THAI_R_CP                 - current Thai codepoint (unsigned int, U+0E01..U+0E5B)
 *   THAI_R_PP                 - const unsigned char**, points just after base bytes,
 *                               will be advanced past any consumed combining marks
 *   THAI_R_X                  - int lvalue, current x pixel cursor
 *   THAI_R_SP                 - inter-cluster spacing in pixels (scaled)
 *
 * Expands to a single { } block. All macros are undef'd on exit.
 */
#ifndef THAI_R_DRAW
#error "Define THAI_R_DRAW (and other THAI_R_* macros) before including thai_cluster.h"
#endif
{
    unsigned int _tc_idx = THAI_R_CP - THAI_CP_FIRST;
    if(THAI_R_WIDTH(_tc_idx) == 0)
    {
        if(THAI_R_CP == 0x0E33u)
        {
            /* standalone ำ: head circle bitmap is at cols 4-6; shift left 4px
               so circle appears at THAI_R_X, then draw า body starting at THAI_R_X */
            THAI_R_DRAW(_tc_idx, THAI_R_X - 4 * THAI_R_SP, 0);
            THAI_R_DRAW(0x0E32u - THAI_CP_FIRST, THAI_R_X, 0);
            THAI_R_X += THAI_CLUSTER_SPACING + THAI_R_WIDTH(0x0E32u - THAI_CP_FIRST);
        }
        else
        {
            /* orphan combining mark: draw at current x, no advance */
            THAI_R_DRAW(_tc_idx, THAI_R_X, 0);
        }
    }
    else
    {
        /* consume following combining marks */
        const unsigned char *_tc_ms = *THAI_R_PP;
        while(**THAI_R_PP)
        {
            const unsigned char *_tc_pk = *THAI_R_PP;
            unsigned char _tc_b = _tc_pk[0];
            if((_tc_b & 0xF0) != 0xE0 ||
               (_tc_pk[1] & 0xC0) != 0x80 ||
               (_tc_pk[2] & 0xC0) != 0x80) break;
            unsigned int _tc_c2 = (unsigned int)(_tc_b    & 0x0F) << 12
                                | (unsigned int)(_tc_pk[1] & 0x3F) << 6
                                | (unsigned int)(_tc_pk[2] & 0x3F);
            if(_tc_c2 < THAI_CP_FIRST || _tc_c2 > THAI_CP_LAST ||
               THAI_R_WIDTH(_tc_c2 - THAI_CP_FIRST) > 0) break;
            *THAI_R_PP = _tc_pk + 3;
        }
        /* pre-scan: detect above-vowels (U+0E31 อั, U+0E33–U+0E37 ำอิอีอึอื).
           If present, tonal marks shift right+up to sit above the sara. */
        int _tc_has_av = 0;
        {
            const unsigned char *_tc_sv = _tc_ms;
            while(_tc_sv < *THAI_R_PP)
            {
                unsigned char _tc_bv = _tc_sv[0];
                if((_tc_bv & 0xF0) != 0xE0 ||
                   (_tc_sv[1] & 0xC0) != 0x80 ||
                   (_tc_sv[2] & 0xC0) != 0x80) { _tc_sv++; continue; }
                unsigned int _tc_cv = (unsigned int)(_tc_bv & 0x0F) << 12
                                    | (unsigned int)(_tc_sv[1] & 0x3F) << 6
                                    | (unsigned int)(_tc_sv[2] & 0x3F);
                _tc_sv += 3;
                if(_tc_cv == 0x0E31u ||
                   (_tc_cv >= 0x0E33u && _tc_cv <= 0x0E37u)) { _tc_has_av = 1; break; }
            }
        }
        /* draw base glyph */
        THAI_R_DRAW(_tc_idx, THAI_R_X, 0);
        /* overlay combining marks */
        int _tc_sam = 0;
        const unsigned char *_tc_mp = _tc_ms;
        while(_tc_mp < *THAI_R_PP)
        {
            unsigned char _tc_b = _tc_mp[0];
            if((_tc_b & 0xF0) != 0xE0 ||
               (_tc_mp[1] & 0xC0) != 0x80 ||
               (_tc_mp[2] & 0xC0) != 0x80)
                { _tc_mp++; continue; }
            unsigned int _tc_mc = (unsigned int)(_tc_b    & 0x0F) << 12
                                | (unsigned int)(_tc_mp[1] & 0x3F) << 6
                                | (unsigned int)(_tc_mp[2] & 0x3F);
            _tc_mp += 3;
            if(_tc_mc < THAI_CP_FIRST || _tc_mc > THAI_CP_LAST) continue;
            /* tonal marks (U+0E48–U+0E4B): when above-vowel present shift
               right 4px and up 1 row so tone sits above the sara, not in it */
            int _tc_xadj = 0, _tc_yadj = 0;
            if(_tc_has_av && _tc_mc >= 0x0E48u && _tc_mc <= 0x0E4Bu)
                { _tc_xadj = 4 * THAI_R_SP; _tc_yadj = -1; }
            THAI_R_DRAW(_tc_mc - THAI_CP_FIRST, THAI_R_X + _tc_xadj, _tc_yadj);
            if(_tc_mc == 0x0E33u) _tc_sam = 1;
        }
        /* advance past base cluster */
        THAI_R_X += THAI_R_WIDTH(_tc_idx) + THAI_R_SP;
        /* sara am tail: า drawn after cluster advance */
        if(_tc_sam)
        {
            THAI_R_DRAW(0x0E32u - THAI_CP_FIRST, THAI_R_X, 0);
            THAI_R_X += THAI_CLUSTER_SPACING + THAI_R_WIDTH(0x0E32u - THAI_CP_FIRST);
        }
    }
}
#undef THAI_R_DRAW
#undef THAI_R_WIDTH
#undef THAI_R_CP
#undef THAI_R_PP
#undef THAI_R_X
#undef THAI_R_SP
