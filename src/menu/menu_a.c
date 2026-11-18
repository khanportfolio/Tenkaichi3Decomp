#include "common.h"
#include "menu/menu_a.h"
#ifdef PORT
extern void Port_NetMenuOpen(void);
extern int Port_NetMenuListed(void);
extern int Port_NetSession(void);
extern void Port_NetLeave(void);
#endif
#include "sys/pad.h"
#include "sys/save.h"
#ifdef PORT
extern void Port_MenuPage(int screen); /* port/src/gs_marker.c: widescreen menus */
extern void Port_MenuFill(int on);
#endif

/* The screen's work (overlay .data, 0x3B0E80). */
MainMenu *gMainMenu = NULL;

/*
 * MainMenu: the main menu of the game (progress mode 4), 0x334C00..0x336A90. First object of the menu
 * overlay DBZP.BIN. A ring of up to 11 items, six plates visible, the cursor on rows 0..3; four guide
 * characters (Gohan, Videl, Trunks, Goten) of which one talks; the seven dragon balls of the save.
 */

#define MAINMENU_VOICE_BASE 0x86F9

/* The item under the cursor. */
#define MAINMENU_CUR(m) ((m)->items[((m)->top + (m)->cursor + 1) % (m)->itemCount])

#ifdef PORT
/*
 * PC only: "Tenkaichi Budokai Online" (item 11), listed after Dragon Net Battle (after Duel when that one is
 * hidden). Its label is written into the free fourth row of the second label sheet of each plate state (label 11,
 * the item's own number), put together from the letters of the other labels of the player's disc
 * (port/src/gs/menu_text.c, Port_MainItemLabel); without them (another edition of the game) from port_tbo_label.h. Its
 * icon is Dragon Net Battle's. Choosing it opens the port's screen mode (menu/port_screen.c) with the screens of
 * menu/port_tbo.c. It has no voiced line of the guides.
 */
#include "port_tbo_label.h"
/* "Mod Manager" (item 12), listed after Options. The label sheets have no free row left, so each plate state gets a
   third sheet (PortMainMenu_AddSheet: a copy of the second with pixels of its own) whose first row is its label
   (label 16), drawn with the font list's line font in the main menu's own colours (port/src/gs/menu_text.c,
   Port_MainItemText); its icon is the Data Center's. Choosing it opens the Duel mode with gPortDuelRole 2, where
   Duel_Main runs the manager's screen (menu/port_modmgr.c) instead of its own. */

#define MAINMENU_TBO 11
#define MAINMENU_MOD_MANAGER 12
s32 gPortDuelRole; /* the Duel screens (progress mode 38) are open for a screen of the PC's: 2 the Mod Manager */
#define MM_LABEL(item) ((item) == MAINMENU_MOD_MANAGER ? 16 : (item)) /* whose label row an item shows */
#define MM_ICON(item) ((item) == MAINMENU_MOD_MANAGER ? 7 : (item) == MAINMENU_TBO ? 4 : (item)) /* whose icon an item shows */

/* A third label sheet for a plate state (slot 8: not chosen, 12: chosen): the movie takes a label's sheet as the
   n-th entry behind the one in gMainMenu->tex[slot], so the two entries of the game are copied into a block with
   room for a third, which is the second again with pixels of its own (the same palette: the PC's labels are made
   for it). Returns the new sheet's entry. */
static u8 sPortSheetEnt[2][3 * 0x40];
static u8 *sPortSheetPix[2];

static u8 *PortMainMenu_AddSheet(s32 slot) {
    s32 n = slot == 8 ? 0 : 1;
    u8 *ent = sPortSheetEnt[n];
    u8 *third = ent + 0x80;
    u32 size;

    memcpy(ent, gMainMenu->tex[slot], 0x80);
    memcpy(third, gMainMenu->tex[slot] + 0x40, 0x40);
    size = *(u32 *)(third + 8);
    sPortSheetPix[n] = Heap_Alloc(size, 0x40, 0, 2);
    memcpy(sPortSheetPix[n], *(u8 **)(third + 0x38), size);
    *(u8 **)(third + 0x38) = sPortSheetPix[n];
    gMainMenu->tex[slot] = ent;
    return third;
}

/* A label sheet's pixels are stored the way the GS keeps 8-bit textures (in blocks): where pixel (x, y) is. */
static s32 PortMainMenu_PixelAt(s32 x, s32 y) {
    s32 block = (y & ~0xF) * 512 + (x & ~0xF) * 2;
    s32 swap = (((y + 2) >> 2) & 1) * 4;
    s32 line = (((y & ~3) >> 1) + (y & 1)) & 7;

    return block + line * 512 * 2 + ((x + swap) & 7) * 4 + ((y >> 1) & 1) + ((x >> 2) & 2);
}

/* Writes runs of palette indices into label row `row` of a sheet. */
static void PortMainMenu_WriteRuns(u8 *tex, s32 row, const PortLabelRun *run, s32 count) {
    u8 *px = *(u8 **)(tex + 0x38) + 0x60;
    s32 i;
    s32 k;

    for (i = 0; i < count; i++) {
        for (k = 0; k < run[i].len; k++) {
            px[PortMainMenu_PixelAt(run[i].x + k, row * 0x20 + run[i].y)] = run[i].v;
        }
    }
}

/* The label in the disc's lettering into row `row` of a sheet; 0 when it could not be made. */
static s32 PortMainMenu_WriteDisc(u8 *tex, s32 row, s32 lit) {
    extern s32 Port_MainItemLabel(s32 entry, s32 lit, s32 out);
    static u8 index[512 * 0x20];
    u8 *px = *(u8 **)(tex + 0x38) + 0x60;
    s32 x;
    s32 y;

    if (!Port_MainItemLabel((s32)(u32)tex, lit, (s32)(u32)index)) {
        return 0;
    }
    for (y = 0; y < 0x20; y++) {
        for (x = 0; x < 512; x++) {
            px[PortMainMenu_PixelAt(x, row * 0x20 + y)] = index[y * 512 + x];
        }
    }
    return 1;
}
/* A label of the PC's drawn with a TrueType font into row `row` of a sheet; the row keeps what it has without a font. */
static void PortMainMenu_WriteText(u8 *tex, s32 row, s32 lit, char *text) {
    extern s32 Port_MainItemText(s32 entry, s32 lit, s32 out, s32 text);
    u8 *index = Heap_Alloc(512 * 32, 0x40, 1, 2);
    u8 *px = *(u8 **)(tex + 0x38) + 0x60;
    s32 x, y;

    if (index == NULL) {
        return;
    }
    if (Port_MainItemText((s32)(u32)tex, lit, (s32)(u32)index, (s32)(u32)text)) {
        for (y = 0; y < 32; y++) {
            for (x = 0; x < 512; x++) {
                px[PortMainMenu_PixelAt(x, row * 0x20 + y)] = index[y * 512 + x];
            }
        }
    }
    Heap_Free(index);
}
#define MAINMENU_VOICED(item) ((item) < MAINMENU_TBO)
#else
#define MM_LABEL(item) (item)
#define MM_ICON(item) (item)
#define MAINMENU_VOICED(item) 1
#endif

/* Starts the idle loop of the seven dragon balls (all seven collected). */
void MainMenu_StartBallLoops(void) {
    MFlashRef ref;
    char name[64];
    s32 i;
    MFlash *flash = &gMainMenu->flash[0];

    for (i = 0; i < 7; i++) {
        sprintf(name, "mc_ch_a_db_%d", i + 1);
        Flash_FindLabel(flash, NULL, name, &ref);
        Flash_ClipGotoLabel(flash, &ref, "fl_loop");
    }
}

#define MM_RES(n) \
    res = (MTexRes *)MPACK_AT(gMainMenu->res, n); \
    Res_RelocateOffsets(&res, res, res)

/* Loads and unpacks the screen (section `section` of archive 1), builds the item list and the windows. */
void MainMenu_Init(s32 section) {
    MTexRes *res = NULL;
    s32 i;

    if (gMenuArc1 == NULL) {
        gMenuArc1 = File_LoadSync(gProgress->baseFile + 1, NULL, 0);
    }
#ifdef PORT
    gMainMenu = Heap_Alloc(sizeof(MainMenu), 0x20, 0, 2); /* (its item list is longer on the PC: MAINMENU_ITEM_MAX) */
    memset(gMainMenu, 0, sizeof(MainMenu));
#else
    gMainMenu = Heap_Alloc(0x188, 0x20, 0, 2);
    memset(gMainMenu, 0, 0x188);
#endif
    gMainMenu->pack = (u32 *)MPACK_AT(gMenuArc1, section);
    gMainMenu->res = Sprite_Unpack(gMainMenu->pack, NULL, NULL);
    MM_RES(1);
    gMainMenu->bg = res;
    MM_RES(2);
    gMainMenu->tex[0] = MTEX(res, 0);
    gMainMenu->tex[1] = MTEX(res, 1);
    MM_RES(3);
    gMainMenu->tex[5] = MTEX(res, 0);
    gMainMenu->tex[4] = MTEX(res, 1);
    MM_RES(4);
    gMainMenu->tex[2] = MTEX(res, 0);
    gMainMenu->tex[3] = MTEX(res, 1);
    MM_RES(5);
    gMainMenu->tex[8] = MTEX(res, 0);
    MM_RES(6);
    gMainMenu->tex[12] = MTEX(res, 0);
    MM_RES(7);
    gMainMenu->tex[7] = MTEX(res, 0);
    gMainMenu->tex[11] = MTEX(res, 1);
    MM_RES(8);
    gMainMenu->tex[6] = MTEX(res, 0);
    gMainMenu->tex[10] = MTEX(res, 1);
    gMainMenu->tex[9] = MTEX(res, 2);
    MM_RES(9);
    gMainMenu->tex[41] = MTEX(res, 0);
    MM_RES(10);
    gMainMenu->tex[43] = MTEX(res, 0);
    MM_RES(11);
    gMainMenu->tex[42] = MTEX(res, 0);
    MM_RES(12);
    gMainMenu->tex[44] = MTEX(res, 0);
    gMainMenu->tex[45] = MTEX(res, 1);
    gMainMenu->tex[46] = MTEX(res, 2);
    gMainMenu->tex[47] = MTEX(res, 3);
    gMainMenu->tex[48] = MTEX(res, 4);
    gMainMenu->tex[49] = MTEX(res, 5);
    MM_RES(13);
    gMainMenu->tex[22] = MTEX(res, 0);
    MM_RES(26);
    gMainMenu->tex[27] = MTEX(res, 0);
    gMainMenu->tex[28] = MTEX(res, 1);
    gMainMenu->tex[29] = MTEX(res, 2);
    MM_RES(15);
    gMainMenu->tex[30] = MTEX(res, 0);
    gMainMenu->tex[13] = MTEX(res, 1);
    gMainMenu->tex[14] = MTEX(res, 2);
    gMainMenu->tex[15] = MTEX(res, 3);
    MM_RES(14);
    gMainMenu->tex[31] = MTEX(res, 0);
    gMainMenu->tex[32] = MTEX(res, 1);
    gMainMenu->tex[33] = MTEX(res, 2);
    MM_RES(24);
    gMainMenu->tex[38] = MTEX(res, 0);
    gMainMenu->tex[39] = MTEX(res, 1);
    gMainMenu->tex[40] = MTEX(res, 2);
    MM_RES(25);
    gMainMenu->tex[19] = MTEX(res, 0);
    gMainMenu->tex[20] = MTEX(res, 1);
    gMainMenu->tex[21] = MTEX(res, 2);
    gMainMenu->tex[16] = MTEX(res, 3);
    gMainMenu->tex[17] = MTEX(res, 4);
    gMainMenu->tex[18] = MTEX(res, 5);
    MM_RES(16);
    gMainMenu->tex[23] = MTEX(res, 0);
    gMainMenu->tex[24] = MTEX(res, 2);
    MM_RES(17);
    gMainMenu->tex[25] = MTEX(res, 0);
    gMainMenu->tex[26] = MTEX(res, 2);
    MM_RES(18);
    gMainMenu->tex[35] = MTEX(res, 0);
    gMainMenu->tex[34] = MTEX(res, 2);
    MM_RES(19);
    gMainMenu->tex[37] = MTEX(res, 0);
    gMainMenu->tex[36] = MTEX(res, 2);
    Flash_Create(&gMainMenu->flash[0], MPACK_AT(gMainMenu->res, 20), gMainMenu->tex);
    Flash_Play(&gMainMenu->flash[0], 1);
    MM_RES(23);
    IconWin_Init(MPACK_AT(gMainMenu->res, 22), res);
    IconWin_Open();
    gMainMenu->msgText = MPACK_AT(gMainMenu->res, 28);
    gMainMenu->subtitles = MPACK_AT(gMainMenu->res, 27);
    MsgWin_Init(MPACK_AT(gMainMenu->res, 21), gMainMenu->msgText, 1, 0);
    MsgWin_Open();

    for (i = 0; i < MAINMENU_ITEM_MAX; i++) {
#ifdef PORT
        /* Item 4, "Dragon Net Battle", is the one entry the PS2 game never lists: it has its plate, label, icon and
           the guides' voiced lines on the disc, and no mode behind it (the Wii version's online mode). The port
           lists it only when the player asks for it (F1 > Online > Advanced: Port_NetMenuListed, port/src/headless.c):
           Tenkaichi Budokai Online, listed after Duel then, is the way into online play. BT3_MENU_ITEM4=1 lists it
           whatever the setting (tests: the pad recordings made while it was always listed count on that menu). */
        if (i == 4 && !Port_NetMenuListed()) {
            continue;
        }
        if (i == MAINMENU_TBO || i == MAINMENU_MOD_MANAGER) { /* (listed after the game's they follow, below) */
            continue;
        }
#else
        if (i == 4) {
            continue;
        }
#endif
        if (i == 10 && (gSaveData->unlockFlags & 0x7F) != 0x7F) {
            continue;
        }
        gMainMenu->items[gMainMenu->itemCount++] = i;
#ifdef PORT
        if (i == (Port_NetMenuListed() ? 4 : 3)) {
            gMainMenu->items[gMainMenu->itemCount++] = MAINMENU_TBO;
        }
        if (i == 9) {
            gMainMenu->items[gMainMenu->itemCount++] = MAINMENU_MOD_MANAGER;
        }
#endif
    }
#ifdef PORT
    /* the second sheet of each state (slot 8 not chosen, 12 chosen) is the entry behind the first */
    if (!PortMainMenu_WriteDisc(gMainMenu->tex[8] + 0x40, 3, 0) || !PortMainMenu_WriteDisc(gMainMenu->tex[12] + 0x40, 3, 1)) {
        PortMainMenu_WriteRuns(gMainMenu->tex[8] + 0x40, 3, sPortTboOff, sizeof(sPortTboOff) / sizeof(sPortTboOff[0]));
        PortMainMenu_WriteRuns(gMainMenu->tex[12] + 0x40, 3, sPortTboOn, sizeof(sPortTboOn) / sizeof(sPortTboOn[0]));
    }
    /* the Mod Manager: the first row of the third sheets */
    PortMainMenu_WriteText(PortMainMenu_AddSheet(8), 0, 0, "Mod Manager");
    PortMainMenu_WriteText(PortMainMenu_AddSheet(12), 0, 1, "Mod Manager");
#endif
    gMainMenu->top = gMainMenu->itemCount - 1;
    for (i = 0; i < gMainMenu->itemCount; i++) {
        if (gMainMenu->items[i] == gProgress->mainMenuItem) {
            gMainMenu->top = i - 1;
            if (gMainMenu->top < 0) {
                gMainMenu->top += gMainMenu->itemCount;
            }
            break;
        }
    }
    gMainMenu->voiceLine = -1;
    for (i = 0; i < 4; i++) {
        gMainMenu->blink[i] = Rand_Range(0x20);
    }
}

/* Frees the screen.
   The movies of a screen are an array (MainMenu_PlateGoto takes the index) of ONE, and Term / Update / Draw
   walk it with a loop. The loop compiles away, but it is what makes the original load the work pointer for the
   Flash call separately (`lw a0,%lo(gMainMenu)(s0)`) instead of sharing the address register with the code
   after it: without the loop the function does not match. */
void MainMenu_Term(void) {
    s32 i;

    MsgWin_Term();
    IconWin_Term();
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
        Flash_Destroy(&gMainMenu->flash[i]);
    }
    if (gMainMenu->res != NULL) {
        Heap_Free(gMainMenu->res);
        gMainMenu->res = NULL;
    }
#ifdef PORT
    for (i = 0; i < 2; i++) {
        if (sPortSheetPix[i] != NULL) {
            Heap_Free(sPortSheetPix[i]);
            sPortSheetPix[i] = NULL;
        }
    }
#endif
    if (gMainMenu != NULL) {
        Heap_Free(gMainMenu);
        gMainMenu = NULL;
    }
}

#ifdef PORT
/*
 * PC only (19 Nov 2026, the user's design: Super-Characters/menu-design-19Nov): the main menu's own look. The movie's
 * plates, arrows, dragon balls and the title window are hidden and the list is drawn with the port's pictures
 * (port/tbo/ui/mm2_*.png, include/menu/tbo_art.h) over the backdrop: a "MAIN MENU" title with Shenron, a row per
 * item with its icon and English name (the chosen one gold and larger), arrows, the save's seven balls in a gold
 * frame, and the keys at the foot. The message window (the item's line) stays the game's. BT3_OLD_MENU=1: the
 * game's own look.
 */
#include <stdio.h>
#include <stdlib.h>
#include "menu/port_screen.h"
#include "menu/tbo_art.h"

static const char *const kMmName[MAINMENU_ITEM_MAX] = {
    "Dragon History", "Ultimate Battle", "Dragon World Tour", "Duel", "Dragon Net Battle", "Evolution Z",
    "Ultimate Training", "Data Center", "Character Reference", "Options", "Shenron", "Tenkaichi Budokai Online",
    "Mod Manager",
};
static const s32 kMmIcon[MAINMENU_ITEM_MAX] = {
    MM2_I_BOOK, MM2_I_SWORDS, MM2_I_GLOBE, MM2_I_VS, MM2_I_WIFI, MM2_I_GEAR, MM2_I_SWORDS, MM2_I_DATA, MM2_I_BOOK,
    MM2_I_GEAR, MM2_I_SHENRON, MM2_I_WIFI, MM2_I_GEAR,
};
static s32 sMmFrames; /* frames since the screen opened (the pieces fade in) */
static void *sMmFor;  /* the screen they count for */

static s32 MmNew_On(void) {
    static s32 on = -1;

    if (on < 0) {
        char *e = getenv("BT3_OLD_MENU");
        on = !(e != NULL && e[0] == '1');
    }
    return on;
}

/* The design's place (1280 x 720 over the whole window) in the page's units. */
static f32 MmNew_X(f32 px) {
    f32 l = Ps_Left();

    return l + px * (512.0f - 2.0f * l) / 1280.0f;
}

static f32 MmNew_Y(f32 py) {
    return py * 448.0f / 720.0f;
}

/* A picture centred on the design's (cx, cy), h of the design's pixels tall. */
static void MmNew_Pic(s32 id, f32 cx, f32 cy, f32 h, s32 a) {
    Ps_Icon(id, MmNew_X(cx), MmNew_Y(cy), MmNew_Y(h), a);
}

/* The rows: the design's top of row i (0..5) and its height; the chosen row is taller. */
#define MMNEW_TOP 160.0f
#define MMNEW_ROW_H 46.0f
#define MMNEW_SEL_H 62.0f
#define MMNEW_GAP 6.0f

static void MmNew_Hide(MFlash *flash) {
    MFlashRef ref;
    char name[64];
    s32 i;

    for (i = 0; i < 7; i++) {
        sprintf(name, "mc_menu_plate_%d", i + 1);
        Flash_FindLabel(flash, NULL, name, &ref);
        Flash_ClipSetFlags(flash, &ref, 2, 0);
        sprintf(name, "mc_ch_a_db_%d", i + 1);
        Flash_FindLabel(flash, NULL, name, &ref);
        Flash_ClipSetFlags(flash, &ref, 2, 0);
    }
    Flash_FindLabel(flash, NULL, "mc_menu_yajirusi", &ref);
    Flash_ClipSetFlags(flash, &ref, 2, 0);
}

static void MmNew_DrawList(void) {
    s32 a;
    s32 i;
    s32 item;
    s32 sel = gMainMenu->cursor + 1;
    f32 y = MMNEW_TOP;
    f32 h;
    f32 x0;
    f32 x1;
    f32 scale;
    u32 top;
    u32 bot;
    u32 line;

    if (sMmFor != (void *)gMainMenu) {
        sMmFor = gMainMenu;
        sMmFrames = 0;
    }
    sMmFrames++;
    a = sMmFrames * 8 > 0x80 ? 0x80 : sMmFrames * 8;
    Ps_PageSet();
    Ps_SetShift(0.0f);

    Ps_Stand(MM2_CLOUD, MmNew_X(1190.0f), MmNew_Y(112.0f), MmNew_Y(120.0f), a);
    MmNew_DrawButton(a);
    Ps_Stand(MM2_TITLE, MmNew_X(262.0f), MmNew_Y(146.0f), MmNew_Y(146.0f), a);
    MmNew_Pic(MM2_UP, 277.0f, MMNEW_TOP - 12.0f, 24.0f, a);

    for (i = 0; i < 6; i++) {
        item = gMainMenu->items[(gMainMenu->top + i) % gMainMenu->itemCount];
        h = i == sel ? MMNEW_SEL_H : MMNEW_ROW_H;
        x0 = i == sel ? 40.0f : 58.0f;
        x1 = i == sel ? 492.0f : 440.0f;
        Ps_Pill(i == sel ? MM2_BAR_SEL : MM2_BAR, MmNew_X(x0), MmNew_Y(y), MmNew_X(x1), MmNew_Y(y + h), a);
        MmNew_Pic(kMmIcon[item], x0 + 42.0f, y + h * 0.5f, h * 1.18f, a);
        y += h + MMNEW_GAP;
    }
    MmNew_Pic(MM2_DOWN, 270.0f, y + 10.0f, 24.0f, a);

    y += 26.0f; /* the balls' frame */
    Ps_Pill(MM2_FRAME, MmNew_X(34.0f), MmNew_Y(y), MmNew_X(456.0f), MmNew_Y(y + 58.0f), a);
    for (i = 0; i < 7; i++) {
        MmNew_Pic((gSaveData->unlockFlags & (s32)(1U << i)) ? MM2_BALL_LIT_1 + i : MM2_BALL_DIM_1 + i,
                  92.0f + i * 51.0f, y + 29.0f, 42.0f, a);
    }

    /* the names, over every picture */
    y = MMNEW_TOP;
    for (i = 0; i < 6; i++) {
        item = gMainMenu->items[(gMainMenu->top + i) % gMainMenu->itemCount];
        h = i == sel ? MMNEW_SEL_H : MMNEW_ROW_H;
        x0 = i == sel ? 40.0f : 58.0f;
        x1 = i == sel ? 492.0f : 440.0f;
        scale = i == sel ? 1.0f : 0.74f;
        if (i == sel) {
            top = Ps_Rgba(0xFF, 0xFF, 0xFF, a);
            bot = Ps_Rgba(0xFF, 0xF0, 0xC8, a);
            line = Ps_Rgba(0x46, 0x1C, 0x00, a);
        } else {
            top = Ps_Rgba(0xFF, 0xFF, 0xFF, a);
            bot = Ps_Rgba(0xD4, 0xE2, 0xFF, a);
            line = Ps_Rgba(0x06, 0x10, 0x26, a);
        }
        Ps_TextEx(MmNew_X(x0 + 90.0f), Ps_TextTop(MmNew_Y(y + h * 0.5f), scale), (char *)kMmName[item], scale,
                  MmNew_X(x1 - (i == sel ? 70.0f : 30.0f)) - MmNew_X(x0 + 90.0f), top, bot, line);
        y += h + MMNEW_GAP;
    }
}

/*
 * The credits (19 Nov 2026, the user: "credit to the game owner, where I took things from the BT4 game, and mod by
 * A. Khan; small, a button top right"): a small CREDITS button under the gold cloud, pressed by a click, the C key
 * or SELECT; it rolls the names up over the menu like a film's end, "Mod by A. Khan" stays in the middle at the end;
 * any button, a click or Esc closes it.
 */
#define MMNEW_KEY_C 6 /* SDL_SCANCODE_C */
/* each line's kind: 0 a heading, 1 a name, 2 a gap, 3 the last big line, 4 a small line */
static const s32 kMmCreditKind[] = {0, 2, 0, 1, 4, 4, 2, 0, 1, 4, 2, 0, 1, 4, 2, 0, 1, 4, 2, 0, 1, 1, 1, 2, 0, 4, 2, 2, 4, 2, 2, 3};
static const char *const kMmCreditText[] = {
    "CREDITS",
    "",
    "Original Game",
    "Dragon Ball Z: Budokai Tenkaichi 3",
    "Developed by Spike",
    "Published by Namco Bandai Games and Atari (2007)",
    "",
    "Dragon Ball",
    "Created by Akira Toriyama",
    "Bird Studio / Shueisha, Toei Animation",
    "",
    "PC Version",
    "Tenkaichi3Decomp",
    "The decompilation and PC port project and its contributors",
    "",
    "Extra Fighters and Moves",
    "Dragon Ball Z: Budokai Tenkaichi 4 (fan mod)",
    "Team BT4: fighters, moves and pictures",
    "",
    "3D Models",
    "RaulV2 (CC-BY 4.0)",
    "free122 (CC-BY 4.0)",
    "Kinnikuchu",
    "",
    "Menus, Maps and Videos",
    "Designs made with AI tools (ChatGPT, Google Flow)",
    "",
    "",
    "Fan-made modification for personal use. Not affiliated with the owners above.",
    "",
    "",
    "Mod by A. Khan",
};
#define MMNEW_CREDIT_N ((s32)(sizeof(kMmCreditKind) / sizeof(kMmCreditKind[0])))
static s32 sMmCredits;    /* frames the credits are open, 0 closed */
static s32 sMmBtnHover;   /* the pointer is on the button */
static s32 sMmKeyC;       /* C was held last frame */

/* The button's box, the design's pixels. */
#define MMNEW_BTN_X0 1104.0f
#define MMNEW_BTN_Y0 118.0f
#define MMNEW_BTN_X1 1256.0f
#define MMNEW_BTN_Y1 154.0f

static f32 MmNew_LineH(s32 kind) {
    return kind == 0 ? 50.0f : kind == 1 ? 38.0f : kind == 3 ? 80.0f : kind == 4 ? 30.0f : 30.0f;
}

/* The pointer over the button (the pointer's game pixels to the page's units, as the lobby's). */
static s32 MmNew_OnButton(void) {
    extern int Port_MenuPointerX(void);
    extern int Port_MenuPointerY(void);
    s32 px = Port_MenuPointerX();
    s32 py = Port_MenuPointerY();
    f32 ax;
    f32 bx;
    f32 ay;
    f32 by;
    f32 x;
    f32 y;

    Ps_PageSet();
    ax = Ps_X(0.0f);
    bx = Ps_X(512.0f);
    ay = Ps_Y(0.0f);
    by = Ps_Y(448.0f);
    if (px < 0 || bx - ax < 1.0f || by - ay < 1.0f) {
        return 0;
    }
    x = ((f32)px - ax) * 512.0f / (bx - ax);
    y = ((f32)py - ay) * 448.0f / (by - ay);
    return x >= MmNew_X(MMNEW_BTN_X0) && x <= MmNew_X(MMNEW_BTN_X1) && y >= MmNew_Y(MMNEW_BTN_Y0) &&
           y <= MmNew_Y(MMNEW_BTN_Y1);
}

/* Per frame, before the menu's own buttons: non-zero when the credits took this frame's input. */
static s32 MmNew_CreditsInput(void) {
    extern int Port_MenuPointerPoll(void);
    extern int Port_KeyScanHeld(int sc);
    s32 ev = Port_MenuPointerPoll();
    s32 c = Port_KeyScanHeld(MMNEW_KEY_C);
    s32 cDown = c && !sMmKeyC;

    sMmKeyC = c;
    if (sMmCredits > 0) {
        if (sMmCredits > 10 && ((gPad[0].gamePressed & (PADG_CROSS | PADG_CIRCLE | PADG_TRIANGLE | PADG_START | PADG_SELECT)) ||
                                (ev & 6) || cDown)) {
            sMmCredits = 0;
            Snd_PlaySe(1, 2);
        }
        return 1;
    }
    sMmBtnHover = MmNew_OnButton();
    if (((ev & 2) && sMmBtnHover) || cDown || (gPad[0].gamePressed & PADG_SELECT)) {
        sMmCredits = 1;
        Snd_PlaySe(1, 1);
        return 1;
    }
    return 0;
}

static void MmNew_DrawButton(s32 a) {
    u32 top = sMmBtnHover ? Ps_Rgba(0xFF, 0xFF, 0xFF, a) : Ps_Rgba(0xFF, 0xE6, 0x80, a);
    u32 bot = sMmBtnHover ? Ps_Rgba(0xFF, 0xE6, 0x80, a) : Ps_Rgba(0xFF, 0x9C, 0x1E, a);

    Ps_Pill(MM2_FRAME, MmNew_X(MMNEW_BTN_X0), MmNew_Y(MMNEW_BTN_Y0), MmNew_X(MMNEW_BTN_X1), MmNew_Y(MMNEW_BTN_Y1), a);
    Ps_Align(1);
    Ps_TextEx(MmNew_X((MMNEW_BTN_X0 + MMNEW_BTN_X1) * 0.5f), Ps_TextTop(MmNew_Y((MMNEW_BTN_Y0 + MMNEW_BTN_Y1) * 0.5f), 0.5f),
              "CREDITS", 0.5f, MmNew_X(MMNEW_BTN_X1 - 30.0f) - MmNew_X(MMNEW_BTN_X0 + 30.0f), top, bot,
              Ps_Rgba(0x3A, 0x16, 0x00, a));
    Ps_Align(0);
}

/* The rolling names over a dark veil. */
static void MmNew_DrawCredits(void) {
    f32 total = 0.0f;
    f32 y;
    f32 cy;
    f32 fade;
    f32 roll;
    s32 i;
    s32 k;
    s32 a;
    f32 scale;

    if (sMmCredits <= 0) {
        return;
    }
    sMmCredits++;
    for (i = 0; i < MMNEW_CREDIT_N; i++) {
        total += MmNew_LineH(kMmCreditKind[i]);
    }
    fade = sMmCredits * 6 > 0x70 ? (f32)0x70 : (f32)(sMmCredits * 6);
    Ps_Rect(Ps_Left(), 0.0f, 512.0f - Ps_Left(), 448.0f, Ps_Rgba(0, 0, 0, (s32)fade), Ps_Rgba(0, 0, 0, (s32)fade));
    /* the list starts under the window's foot and rolls up until its last line is in the middle */
    roll = sMmCredits * 1.2f;
    if (roll > 720.0f + total - 360.0f - MmNew_LineH(3) * 0.5f) {
        roll = 720.0f + total - 360.0f - MmNew_LineH(3) * 0.5f;
    }
    y = 720.0f - roll;
    Ps_Align(1);
    for (i = 0; i < MMNEW_CREDIT_N; i++) {
        k = kMmCreditKind[i];
        cy = y + MmNew_LineH(k) * 0.5f;
        y += MmNew_LineH(k);
        if (k == 2 || cy < -40.0f || cy > 760.0f) {
            continue;
        }
        /* fading in at the foot and out at the top */
        a = 0x80;
        if (cy < 110.0f) {
            a = cy <= 30.0f ? 0 : (s32)((cy - 30.0f) / 80.0f * 128.0f);
        } else if (cy > 640.0f) {
            a = cy >= 710.0f ? 0 : (s32)((710.0f - cy) / 70.0f * 128.0f);
        }
        if (a <= 0) {
            continue;
        }
        scale = k == 0 ? 0.78f : k == 1 ? 0.62f : k == 3 ? 1.35f : 0.46f;
        if (k == 0 || k == 3) {
            Ps_TextExOuter(MmNew_X(640.0f), Ps_TextTop(MmNew_Y(cy), scale), (char *)kMmCreditText[i], scale,
                           MmNew_X(1200.0f) - MmNew_X(80.0f), Ps_Rgba(0xFF, 0xF0, 0x90, a), Ps_Rgba(0xFF, 0x8C, 0x10, a),
                           Ps_Rgba(0x3A, 0x16, 0x00, a), Ps_Rgba(0x00, 0x00, 0x00, a / 2), 2.0f);
        } else {
            Ps_TextEx(MmNew_X(640.0f), Ps_TextTop(MmNew_Y(cy), scale), (char *)kMmCreditText[i], scale,
                      MmNew_X(1200.0f) - MmNew_X(80.0f), Ps_Rgba(0xFF, 0xFF, 0xFF, a),
                      k == 4 ? Ps_Rgba(0xC8, 0xD4, 0xEC, a) : Ps_Rgba(0xDC, 0xE6, 0xFF, a), Ps_Rgba(0x06, 0x10, 0x26, a));
        }
    }
    Ps_Align(0);
    if (roll >= 720.0f + total - 360.0f - MmNew_LineH(3) * 0.5f) { /* at the end: how to leave */
        Ps_Align(1);
        Ps_TextEx(MmNew_X(640.0f), Ps_TextTop(MmNew_Y(660.0f), 0.42f), "Press any button", 0.42f, 0.0f,
                  Ps_Rgba(0xFF, 0xFF, 0xFF, 0x60), Ps_Rgba(0xC8, 0xD4, 0xEC, 0x60), Ps_Rgba(0x06, 0x10, 0x26, 0x60));
        Ps_Align(0);
    }
}

/* The keys at the foot, right of the message window's line: Enter / Cross choose, Esc / Circle go back. */
static void MmNew_DrawKeys(void) {
    s32 a = sMmFrames * 8 > 0x80 ? 0x80 : sMmFrames * 8;
    f32 cy = 690.0f;
    f32 x = 1252.0f;
    f32 w;
    u32 top = Ps_Rgba(0xFF, 0xFF, 0xFF, a);
    u32 bot = Ps_Rgba(0xE6, 0xEE, 0xFF, a);
    u32 line = Ps_Rgba(0x06, 0x10, 0x26, a);

    /* right to left: O, X, BACK, Esc, |, SELECT, Enter */
    MmNew_Pic(MM2_K_O, x - 16.0f, cy, 32.0f, a);
    x -= 38.0f;
    MmNew_Pic(MM2_K_X, x - 16.0f, cy, 32.0f, a);
    x -= 46.0f;
    w = Ps_TextWidth("BACK", 0.62f);
    Ps_TextEx(MmNew_X(x) - w, Ps_TextTop(MmNew_Y(cy), 0.62f), "BACK", 0.62f, 0.0f, top, bot, line);
    x -= (w / (MmNew_X(1.0f) - MmNew_X(0.0f))) + 8.0f;
    MmNew_Pic(MM2_K_ESC, x - 24.0f, cy, 30.0f, a);
    x -= 66.0f;
    w = Ps_TextWidth("SELECT", 0.62f);
    Ps_TextEx(MmNew_X(x) - w, Ps_TextTop(MmNew_Y(cy), 0.62f), "SELECT", 0.62f, 0.0f, top, bot, line);
    x -= (w / (MmNew_X(1.0f) - MmNew_X(0.0f))) + 8.0f;
    MmNew_Pic(MM2_K_ENTER, x - 24.0f, cy, 30.0f, a);
}
#endif

/* Draws the background, sets up every clip of the movie for this frame and draws it and the two windows. */
void MainMenu_Draw(void) {
    MFlashRef ref;
    MFlashUv uv;
    char name[64];
    MFlashRef eye;
    MFlashRef mouth;
    s32 i;
    s32 item;
    MFlash *flash;

#ifdef PORT
    Port_MenuPage(2); /* widescreen menus: this screen's page (PORT_MENU_SCREEN in port/src/gs_marker.c), its backdrop filling the width */
    Port_MenuFill(1);
    Sprite_DrawPicture(gMainMenu->bg, 0, 0, 0x80);
    Port_MenuFill(0);
#else
    Sprite_DrawPicture(gMainMenu->bg, 0, 0, 0x80);
#endif
    flash = &gMainMenu->flash[0];
#ifdef PORT
    {
        extern int Port_FlashNamesWanted(void);
        extern void Flash_PortDumpClips(void *movie);
        static s32 said;

        if (!said && Port_FlashNamesWanted()) { /* (BT3_FLASH_NAMES: the movie's pieces, once) */
            said = 1;
            Flash_PortDumpClips(flash);
        }
    }
#endif
    for (i = 0; i < 4; i++) {
        switch (i) {
        case 0:
            Flash_FindLabel(flash, NULL, "mc_guide_gohan_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_gohan_mouth", &mouth);
            break;
        case 1:
            Flash_FindLabel(flash, NULL, "mc_guide_bidel_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_bidel_mouth", &mouth);
            break;
        case 3:
            Flash_FindLabel(flash, NULL, "mc_guide_torankusu_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_torankusu_mouth", &mouth);
            break;
        case 2:
            Flash_FindLabel(flash, NULL, "mc_guide_goten_eye", &eye);
            Flash_FindLabel(flash, NULL, "mc_guide_goten_mouth", &mouth);
            break;
        }
        FlashAnim_Blink(flash, &eye, &gMainMenu->blink[i], 0);
        if (gMainMenu->guide == i) {
            FlashAnim_Talk(flash, &mouth, &gMainMenu->talk[i], 0);
        } else {
            FlashAnim_ShowNext2(flash, &mouth, 0);
        }
    }

    {
        /* columns of each item's animated icon sheet */
        s32 cols[MAINMENU_ITEM_MAX] = { 4, 4, 4, 5, 5, 5, 5, 4, 5, 5, 4 };

        for (i = 0; i < 6; i++) {
            item = gMainMenu->items[(gMainMenu->top + i) % gMainMenu->itemCount];
            uv.x0 = 0;
            uv.x1 = 0x200;
            uv.y0 = (MM_LABEL(item) % 8) * 0x20;
            uv.y1 = uv.y0 + 0x20;
            sprintf(name, "mc_menu_plate_%d", i + 1);
            Flash_FindLabel(flash, name, "mc_menu_text_off", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, MM_LABEL(item) / 8);
            Flash_FindLabel(flash, name, "mc_menu_text_on", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, MM_LABEL(item) / 8);
            uv.x0 = (MM_ICON(item) % 4) * 0x40;
            uv.y0 = (MM_ICON(item) / 4) * 0x40;
            uv.x1 = uv.x0 + 0x40;
            uv.y1 = uv.y0 + 0x40;
            Flash_FindLabel(flash, name, "mc_icon_off", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            uv.x0 = 0;
            uv.y0 = (MM_ICON(item) % 10) * 0x30;
            uv.x1 = 0x30;
            uv.y1 = uv.y0 + 0x30;
            Flash_FindLabel(flash, name, "mc_icon_play", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
            Flash_ClipSetTex(flash, &ref, MM_ICON(item) / 10);
            if (item == MAINMENU_CUR(gMainMenu)) {
                FlashAnim_Sheet(flash, &ref, &gMainMenu->iconTimer, &gMainMenu->iconFrame, &uv,
                                cols[MM_ICON(MAINMENU_CUR(gMainMenu))], 1, MM_ICON(MAINMENU_CUR(gMainMenu)) == 3 ? 3 : 6);
            }
        }
    }

    {
        item = gMainMenu->items[gMainMenu->extra];
        uv.x1 = 0x200;
        uv.x0 = 0;
        uv.y0 = (MM_LABEL(item) % 8) * 0x20;
        uv.y1 = uv.y0 + 0x20;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_menu_text_off", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, MM_LABEL(item) / 8);
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_menu_text_on", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, MM_LABEL(item) / 8);
        uv.x0 = (MM_ICON(item) % 4) * 0x40;
        uv.y0 = (MM_ICON(item) / 4) * 0x40;
        uv.x1 = uv.x0 + 0x40;
        uv.y1 = uv.y0 + 0x40;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_icon_off", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        uv.x0 = 0;
        uv.y0 = (MM_ICON(item) % 10) * 0x30;
        uv.x1 = 0x30;
        uv.y1 = uv.y0 + 0x30;
        Flash_FindLabel(flash, "mc_menu_plate_7", "mc_icon_play", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
        Flash_ClipSetTex(flash, &ref, MM_ICON(item) / 10);
    }

    for (i = 0; i < 2; i++) {
        uv.x0 = i * 0x20;
        uv.y0 = 0x20;
        uv.x1 = uv.x0 + 0x20;
        uv.y1 = 0x40;
        Flash_FindLabel(flash, NULL, i ? "mc_menu_yajirusi_down" : "mc_menu_yajirusi_up", &ref);
        Flash_ClipSetUv(flash, &ref, &uv);
    }

    for (i = 0; i < 7; i++) {
        sprintf(name, "mc_ch_a_db_%d", i + 1);
        if (gSaveData->unlockFlags & (s32)(1U << i)) {
            Flash_FindLabel(flash, NULL, name, &ref);
            Flash_ClipSetFlags(flash, &ref, 2, 1);
            uv.x0 = (i % 4) * 0x40;
            uv.y0 = (i / 4) * 0x40;
            uv.x1 = uv.x0 + 0x40;
            uv.y1 = uv.y0 + 0x40;
            Flash_FindLabel(flash, name, "mc_dragonball", &ref);
            Flash_ClipSetUv(flash, &ref, &uv);
        } else {
            Flash_FindLabel(flash, NULL, name, &ref);
            Flash_ClipSetFlags(flash, &ref, 2, 0);
        }
    }

    for (i = 0; i < 2; i++) {
        uv.y0 = 0;
        uv.y1 = 0x100;
        uv.x0 = 0;
        uv.x1 = 0x200;
        Flash_FindLabel(flash, NULL, i ? "mc_bg_cloud" : "mc_bg_cloud_a", &ref);
        FlashAnim_Scroll(flash, &ref, &uv, &gMainMenu->cloud[i], NULL, i ? -0.18962963f : -0.35555556f, 0.0f);
    }

#ifdef PORT
    if (MmNew_On()) {
        MmNew_Hide(flash);
    }
#endif
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
        Flash_Draw(&gMainMenu->flash[i]);
    }
#ifdef PORT
    if (!MmNew_On()) {
        IconWin_Draw();
    }
    MsgWin_Draw(0, 0, gMainMenu->voiceLine);
    Port_MenuPage(0);
    if (MmNew_On()) { /* (after the page: the menu page moves what it draws outside its movie's clips) */
        MmNew_DrawList();
        MmNew_DrawKeys();
        MmNew_DrawCredits();
    }
#else
    IconWin_Draw();
    MsgWin_Draw(0, 0, gMainMenu->voiceLine);
#endif
}

/* Sends the plate under the cursor of movie `idx` to a label. */
void MainMenu_PlateGoto(s32 idx, char *label) {
    MFlashRef ref;
    char name[64];
    MFlash *flash = &gMainMenu->flash[idx];

    sprintf(name, "mc_menu_plate_%d", gMainMenu->cursor + 2);
    Flash_FindLabel(flash, NULL, name, &ref);
    Flash_ClipGotoLabel(flash, &ref, label);
}

/* Per frame: when the guide's greeting has ended, lets a random guide read out the current item; steps the movie. */
void MainMenu_Update(void) {
    s32 i;

    if (gMainMenu->flags & MAINMENU_GREETED) {
        if (gMainMenu->voiceLine == gMainMenu->guide + 0xC || gMainMenu->voiceLine == gMainMenu->guide + 0x3C) {
            if (Voice_GetStat(0) == 5) {
                gMainMenu->guide = Rand_Range(4);
                if (MAINMENU_VOICED(MAINMENU_CUR(gMainMenu))) {
                    gMainMenu->voiceLine = MAINMENU_CUR(gMainMenu) * 4 + gMainMenu->guide + 0x10;
                    Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
                } else {
                    gMainMenu->voiceLine = -1;
                }
            }
        }
    }
    for (i = 0; i < MAINMENU_FLASH_NUM; i++) {
        Flash_Advance(&gMainMenu->flash[i]);
    }
}

/* What a choice does: marks the screen as leaving, sets the next mode and plays the movie out. */
#define MAINMENU_CHOOSE(next) \
    gMainMenu->flags |= MAINMENU_CHOSEN; \
    gMainMenu->flags |= MAINMENU_LEAVING; \
    gMainMenu->timer = 15; \
    gProgress->mode = (next); \
    Flash_GotoLabel(&gMainMenu->flash[0], "fl_out", 1)

/* Counts the choices made on the main menu in the save, 0..23. */
#define MAINMENU_COUNT() \
    gSaveData->unkA0C++; \
    if (gSaveData->unkA0C >= 24) { \
        gSaveData->unkA0C = 0; \
    }

/* Pad 0: up / down move the cursor (the ring scrolls at the ends), confirm enters a mode, cancel leaves. */
void MainMenu_Input(s32 *result) {
    if (!(gMainMenu->flash[0].flags & MFLASH_PAD)) {
        return;
    }
#ifdef PORT
    {   /* In a lobby (joined from the Dragon Net Battle window, or back here after its match): its screen, in
           Tenkaichi Budokai Online (menu/port_tbo.c; port/src/gs/net_lobby.c) */
        extern int Port_LobbyWantsScreen(void);

        if (!(gMainMenu->flags & MAINMENU_LEAVING) && Port_LobbyWantsScreen()) {
            gPortScreen = PORT_SCREEN_TBO;
            MAINMENU_CHOOSE(MPROG_MODE_PORT_SCREEN);
            return;
        }
    }
#endif
    if (!(gMainMenu->flags & MAINMENU_STARTED)) {
        MainMenu_PlateGoto(0, "fl_on_start");
        if ((gSaveData->unlockFlags & 0x7F) == 0x7F) {
            MainMenu_StartBallLoops();
        }
        gMainMenu->flags |= MAINMENU_STARTED;
    }
#ifdef PORT
    if (MmNew_On() && MmNew_CreditsInput()) { /* (the credits: their button, C, SELECT; open, they take the buttons) */
        return;
    }
#endif
    if (gPad[0].gameRepeat & 8) {
        MainMenu_PlateGoto(0, "fl_off_start");
        gMainMenu->iconTimer = 0;
        gMainMenu->iconFrame = 0;
        gMainMenu->cursor--;
        if (gMainMenu->cursor < 0) {
            gMainMenu->cursor = 0;
            Flash_GotoLabel(&gMainMenu->flash[0], "fl_menu_down", 1);
            gMainMenu->top--;
            if (gMainMenu->top < 0) {
                gMainMenu->top += gMainMenu->itemCount;
            }
            gMainMenu->extra = (gMainMenu->top + 6) % gMainMenu->itemCount;
        }
        goto moved;
    } else if (gPad[0].gameRepeat & 4) {
        MainMenu_PlateGoto(0, "fl_off_start");
        gMainMenu->iconTimer = 0;
        gMainMenu->iconFrame = 0;
        gMainMenu->cursor++;
        if (gMainMenu->cursor >= MAINMENU_ROWS) {
            gMainMenu->cursor = MAINMENU_ROWS - 1;
            Flash_GotoLabel(&gMainMenu->flash[0], "fl_menu_up", 1);
            gMainMenu->extra = gMainMenu->top;
            gMainMenu->top++;
            if (gMainMenu->top >= gMainMenu->itemCount) {
                gMainMenu->top -= gMainMenu->itemCount;
            }
        }
    moved:
        MainMenu_PlateGoto(0, "fl_on_start");
        gMainMenu->guide = Rand_Range(4);
        if (MAINMENU_VOICED(MAINMENU_CUR(gMainMenu))) {
            gMainMenu->voiceLine = MAINMENU_CUR(gMainMenu) * 4 + gMainMenu->guide + 0x10;
            Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
        } else {
            gMainMenu->voiceLine = -1;
        }
        Snd_PlaySe(1, 0);
    } else if (gPad[0].gamePressed & 0x200) {
        switch (MAINMENU_CUR(gMainMenu)) {
        case 0:
            MAINMENU_CHOOSE(6);
            MAINMENU_COUNT();
            break;
        case 1:
            MAINMENU_CHOOSE(13);
            MAINMENU_COUNT();
            break;
        case 2:
            MAINMENU_CHOOSE(33);
            MAINMENU_COUNT();
            break;
        case 3:
            MAINMENU_CHOOSE(38);
            MAINMENU_COUNT();
            break;
        case 4:
#ifdef PORT
            Port_NetMenuOpen(); /* the port's own screen for online play (port/src/gs/ui.cpp); the menu stays where it is */
#endif
            break;
        case 5:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(48);
            break;
        case 6:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(44);
            break;
        case 7:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(53);
            break;
        case 8:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(60);
            break;
        case 9:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(62);
            break;
        case 10:
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(70);
            break;
#ifdef PORT
        case MAINMENU_TBO: /* "Tenkaichi Budokai Online": the port's screen mode runs its screens (menu/port_tbo.c) */
            gPortScreen = PORT_SCREEN_TBO;
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(MPROG_MODE_PORT_SCREEN);
            break;
        case MAINMENU_MOD_MANAGER: /* the Duel mode runs the manager's screen (menu_g_b.c) */
            gPortDuelRole = 2;
            MAINMENU_COUNT();
            MAINMENU_CHOOSE(38);
            break;
#endif
        }
        MainMenu_PlateGoto(0, "fl_ok");
        Snd_PlaySe(1, 1);
    } else if (gPad[0].gamePressed & 0x400) {
        Flash_GotoLabel(&gMainMenu->flash[0], "fl_cancel", 1);
        IconWin_Close();
        MsgWin_Close();
        ColorFade_StartOut(0, 0, 0, 0x14);
        *result = 0;
        Snd_PlaySe(1, 2);
    }
}

/* The main menu's own frame loop. Returns 1 when a mode was chosen (gProgress->mode is set), 0 on cancel. */
s32 MainMenu_Run(s32 section) {
    s32 result = 1;

#ifdef PORT
    if (Port_NetSession()) {
        Port_NetLeave(); /* an online session lives in the versus mode: back at the main menu it is over (no return) */
    }
#endif
    MainMenu_Init(section);
    ColorFade_StartIn(0, 0, 0, 0x14);
    while (1) {
        Gfx_BeginFrame();
        Pad_Update();
        Snd_Update();
        ColorFade_Update();
        if (!(gProgress->flags & MPROG_FREEZE)) {
            MainMenu_Update();
        }
        MainMenu_Draw();
        ColorFade_Draw();
        Gfx_EndFrame(1);
        Dma_Flush();
        File_Stub264D90();
        if (gProgress->flags & MPROG_FREEZE) {
            continue;
        }
        if (ColorFade_IsInDone()) {
            if (!(gMainMenu->flags & MAINMENU_GREETED) && (gMainMenu->flash[0].flags & MFLASH_PAD)) {
                gMainMenu->flags |= MAINMENU_GREETED;
                gMainMenu->guide = Rand_Range(4);
                if ((gSaveData->unlockFlags & 0x7F) == 0x7F) {
                    gMainMenu->voiceLine = gMainMenu->guide + 0x3C;
                } else {
                    gMainMenu->voiceLine = gMainMenu->guide + 0xC;
                }
                Voice_PlayWithSubtitle(gMainMenu->subtitles, MAINMENU_VOICE_BASE, gMainMenu->voiceLine);
            }
        }
        if (ColorFade_IsFadingOut()) {
            Voice_FadeOutStep(0);
            Bgm_FadeOutStep();
            continue;
        }
        if (ColorFade_IsOutDone()) {
            break;
        }
        if (gMainMenu->flags & MAINMENU_LEAVING) {
            if (--gMainMenu->timer == -1) {
                ColorFade_StartOut(0, 0, 0, 0x14);
                gProgress->mainMenuItem = MAINMENU_CUR(gMainMenu);
            }
        } else {
            MainMenu_Input(&result);
        }
    }
    sceGsSyncPath(0, 0);
    MainMenu_Term();
    Dma_ResetBuffers();
    return result;
}
