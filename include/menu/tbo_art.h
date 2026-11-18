/* Tenkaichi Budokai Online: its pictures, drawn by the renderer at their own resolution. Each is tbo/ui/<name>.png
   next to the program; the game asks for one by its number (src/menu/port_tbo.c), the renderer reads and keeps it
   (port/src/gs/gs_draw.c, tbo_texture). Plain C, no other headers: both sides include it. The pictures are made by
   port/build/ml/tbo_assets.py from the Flow art in online_design/elements. */
#ifndef TBO_ART_H
#define TBO_ART_H

#define TBO_ART_LIST(X) \
    X(TBO_BG, "bg")                         /* the tournament grounds, 16:9 */ \
    X(TBO_CREST, "crest")                   /* the tournament's crest */ \
    X(TBO_HOSTS_MIC_WAVE, "hosts_mic_wave") /* Goten and Trunks, one picture per pose */ \
    X(TBO_HOSTS_BINOCULARS, "hosts_binoculars") \
    X(TBO_HOSTS_ARMS_CROSSED, "hosts_arms_crossed") \
    X(TBO_HOSTS_PUZZLED, "hosts_puzzled") \
    X(TBO_HOSTS_CHEER, "hosts_cheer") \
    X(TBO_HOSTS_THUMBS_UP, "hosts_thumbs_up") \
    X(TBO_KIT_SEL, "kit_sel")               /* the interface pieces: the chosen bar, */ \
    X(TBO_KIT_BAR, "kit_bar")               /* a bar, */ \
    X(TBO_KIT_DIALOG, "kit_dialog")         /* the hosts' box, */ \
    X(TBO_KIT_WINDOW, "kit_window")         /* a window, */ \
    X(TBO_KIT_GOLD, "kit_gold")             /* the gold capsule, */ \
    X(TBO_KIT_STEEL, "kit_steel")           /* the steel capsule */ \
    X(TBO_BTN_CROSS, "btn_cross")           /* the pad's buttons, in the order of the prompts' kinds */ \
    X(TBO_BTN_SQUARE, "btn_square") \
    X(TBO_BTN_TRIANGLE, "btn_triangle") \
    X(TBO_BTN_LEFTRIGHT, "btn_leftright") \
    X(TBO_BTN_CIRCLE, "btn_circle") \
    X(TBO_BTN_START, "btn_start") \
    X(TBO_RADAR, "radar")                   /* the Dragon Radar of the search: its frames, 11 a row (src/menu/port_tbo.c Tb_Radar) */ \
    X(TBO_LB2_GLASS, "lb2_glass") /* the lobby's new look (18 Nov 2026, tools/lobby_art.py) */ \
    X(TBO_LB2_PLATE, "lb2_plate") \
    X(TBO_LB2_TOPBAR, "lb2_topbar") \
    X(TBO_LB2_BTN_DARK, "lb2_btn_dark") \
    X(TBO_LB2_BTN_BLUE, "lb2_btn_blue") \
    X(TBO_LB2_BADGE, "lb2_badge") \
    X(TBO_LB2_START, "lb2_start") \
    X(TBO_LB2_LOGO, "lb2_logo") \
    X(TBO_LB2_R_MIC, "lb2_r_mic") \
    X(TBO_LB2_R_MIC_OFF, "lb2_r_mic_off") \
    X(TBO_LB2_R_SPK, "lb2_r_spk") \
    X(TBO_LB2_R_SPK_OFF, "lb2_r_spk_off") \
    X(TBO_LB2_R_INV, "lb2_r_inv") \
    X(TBO_LB2_R_RANK, "lb2_r_rank") \
    X(TBO_LB2_R_CHAT, "lb2_r_chat") \
    X(TBO_LB2_F_DAILY, "lb2_f_daily") \
    X(TBO_LB2_F_TOUR, "lb2_f_tour") \
    X(TBO_LB2_F_HISTORY, "lb2_f_history") \
    X(TBO_LB2_F_COSTUME, "lb2_f_costume") \
    X(TBO_LB2_READY, "lb2_ready") \
    X(TBO_LB2_CROWN, "lb2_crown") \
    X(TBO_LB2_EMBLEM, "lb2_emblem") \
    X(TBO_LB2_CHANGE, "lb2_change") \
    X(TBO_LB2_COPY, "lb2_copy") \
    X(TBO_LB2_LEAVE, "lb2_leave") \
    X(TBO_LB2_CLOSE, "lb2_close") \
    X(TBO_LB2_TABS, "lb2_tabs") \
    X(TBO_LB2_CHATIN, "lb2_chatin") \
    X(TBO_LB2_I_EYE, "lb2_i_eye") \
    X(TBO_LB2_I_RULES, "lb2_i_rules") \
    X(TBO_LB2_I_FRIENDS, "lb2_i_friends") \
    X(TBO_LB2_I_PIN, "lb2_i_pin") \
    X(TBO_LB2_I_BATTLE, "lb2_i_battle") \
    X(TBO_LB2_I_TIME, "lb2_i_time") \
    X(TBO_LB2_I_SPEED, "lb2_i_speed") \
    X(TBO_LB2_I_ROLL, "lb2_i_roll") \
    X(TBO_LB2_I_DELAY, "lb2_i_delay") \
    X(TBO_LB2_I_GLOBE, "lb2_i_globe") \
    X(TBO_LB2_I_BOLT, "lb2_i_bolt") \
    X(MM_BG, "mm_bg")               /* the Mod Manager's pictures (src/menu/port_mmkit.c), in mm_sizes.h's order */ \
    X(MM_BAR, "mm_bar") \
    X(MM_BAR_SEL, "mm_bar_sel") \
    X(MM_TAB, "mm_tab") \
    X(MM_TAB_SEL, "mm_tab_sel") \
    X(MM_BAR_GREY, "mm_bar_grey") \
    X(MM_NAVBAR, "mm_navbar") \
    X(MM_FIELD, "mm_field") \
    X(MM_KEY, "mm_key") \
    X(MM_WINDOW_DARK, "mm_window_dark") \
    X(MM_CARD, "mm_card") \
    X(MM_WINDOW, "mm_window") \
    X(MM_EMBLEM, "mm_emblem") \
    X(MM_PLATFORM, "mm_platform") \
    X(MM_FOLDER, "mm_folder") \
    X(MM_FOLDER_MOD, "mm_folder_mod") \
    X(MM_FOLDER_BATCH, "mm_folder_batch") \
    X(MM_ZIP, "mm_zip") \
    X(MM_KEYCAP, "mm_keycap") \
    X(MM_KEYCAP_SEL, "mm_keycap_sel") \
    X(MM_CELL, "mm_cell") \
    X(MM_UP, "mm_up") \
    X(MM_DOWN, "mm_down") \
    X(MM_LEFT, "mm_left") \
    X(MM_RIGHT, "mm_right") \
    X(MM_BALL, "mm_ball") \
    X(MM_LOCK, "mm_lock") \
    X(MM_PIP, "mm_pip") \
    X(MM_PIP_OFF, "mm_pip_off") \
    X(MM_BG_DUSK, "mm_bg_dusk")     /* the character select's mod grid */

#define TBO_ART_ENUM(id, name) id,
enum { TBO_ART_LIST(TBO_ART_ENUM) TBO_ART_COUNT };
#undef TBO_ART_ENUM

/* The texture format that marks a draw as one of these pictures (a reserved PSM value the GS has no format for and
   the game never uses); the picture's number is in the palette address (CBP), the size given is 1024 x 1024 so
   that UVs 0..1024 cover the whole picture whatever its own size. */
#define TBO_PSM 0x05

/* The lines of letters (port/src/gs/tbo_text.c) are drawn the same way: numbers TBO_TEXT_BASE and on, one per line
   the screens write, the picture made from a TrueType font of the font list. */
#define TBO_TEXT_BASE 0x100
#define TBO_TEXT_SLOTS 256

/* Native artwork for the first-run language selector, decoded from the player's EU disc. */
/* The lobby's own pictures (17 Nov 2026, this modded copy): each fighter's portrait (tbo/ui/fighters/<id>.png, the
   character select's 256 x 512, made by Super-Characters/tools/lobby_fighters.py), each stage's view
   (tbo/ui/stages/<id>.png, 384 x 216) and the lobby's background (tbo/ui/lobby_bg.png). */
#define TBO_EXTRA_BASE 0x300
#define TBO_FIGHTER_BASE 0x300 /* + the fighter's id, 0..255 */
#define TBO_STAGE_BASE 0x400   /* + the stage's id, 0..63 */
#define TBO_LOBBY_BG 0x440
#define TBO_FACE_BASE 0x441  /* + the fighter's id: its face, the select's chip (tbo/ui/faces/<id>.png) */
#define TBO_EXTRA_COUNT 0x241
#define TBO_LANG_BG 0x200
#define TBO_LANG_ARROW_LEFT 0x201
#define TBO_LANG_ARROW_RIGHT 0x202

#endif
