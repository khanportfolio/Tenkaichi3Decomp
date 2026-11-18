/*
 * Kame House Arena (this modded copy, 17 Nov 2026, the user: "friends: friend codes, who's online, invites, recent
 * players", "like PUBG"): the game's side of the user's own service's /v1/f/ calls
 * (Super-Characters/online-service/friends.js).
 *
 * This player is a secret key made once (32 hex digits, kept in bt3_player.txt beside the settings) and the friend
 * code the service gives it (DBZ-XXXX). While the game runs it tells the service now and then that it is on (every
 * 60 s; every 30 s in a lobby or with the Friends window open) and in which lobby, and gets back in the same answer
 * its friends (online or not, and the lobby each is in), friend requests, invitations and recent players. One request
 * at a time, on a thread of its own; nothing here waits on the network. Setting friends_online 0: none of it.
 */
#ifdef _WIN32
#define _CRT_RAND_S
#endif
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "net_friends.h"
#include "net_lobby.h"

extern int Match_Post(const char *what, const char *json, char *out, size_t size); /* net_match.c */
extern const char *Port_NetNameGet(void);                                            /* net.c */
extern int Port_Setting(const char *name, int def);
extern int Port_LobbyFighter(void);                                                  /* net_lobby.c */

#define ANSWER 16384
static SDL_SpinLock sLock;
static PortFriendsView sV;
static char sPk[33];
static int sPkTried;
static SDL_AtomicInt sBusy;
static uint64_t sNextMs = 5000, sLastMs;
static struct { char what[16]; char extra[200]; } sQ[12];
static int sQn;
static char sSeenRoom[8];
static int sShow = 0, sTab = -1;
static char sJoin[8];

/* ---- this player's key ---- */
static void player_path(char *path, size_t size) {
    const char *set = getenv("BT3_SETTINGS"), *slash;
    size_t n;
    if (getenv("BT3_PLAYER") != NULL) { /* (a test's second copy: another player) */
        SDL_strlcpy(path, getenv("BT3_PLAYER"), size);
        return;
    }
    if (set == NULL || set[0] == '\0') {
        SDL_strlcpy(path, "bt3_player.txt", size);
        return;
    }
    slash = strrchr(set, '/');
#ifdef _WIN32
    if (strrchr(set, '\\') != NULL && (slash == NULL || strrchr(set, '\\') > slash)) {
        slash = strrchr(set, '\\');
    }
#endif
    n = slash != NULL ? (size_t)(slash - set + 1) : 0;
    if (n + 16 > size) {
        SDL_strlcpy(path, "bt3_player.txt", size);
        return;
    }
    memcpy(path, set, n);
    SDL_strlcpy(path + n, "bt3_player.txt", size - n);
}

static uint32_t random32(void) {
#ifdef _WIN32
    unsigned int v = 0;
    if (rand_s(&v) == 0) {
        return v;
    }
#else
    FILE *f = fopen("/dev/urandom", "rb");
    uint32_t v = 0;
    if (f != NULL) {
        size_t got = fread(&v, 1, sizeof(v), f);
        fclose(f);
        if (got == sizeof(v)) {
            return v;
        }
    }
#endif
    return (uint32_t)(SDL_GetPerformanceCounter() * 2654435761u) ^ (uint32_t)SDL_GetTicksNS();
}

static int key_load(void) {
    char path[600], line[128];
    FILE *f;
    int i;
    if (sPk[0] != '\0') {
        return 1;
    }
    if (sPkTried) {
        return 0;
    }
    sPkTried = 1;
    player_path(path, sizeof(path));
    f = fopen(path, "r");
    if (f != NULL) {
        while (fgets(line, sizeof(line), f) != NULL) {
            if (strncmp(line, "pk=", 3) == 0 && strspn(line + 3, "0123456789abcdef") == 32) {
                memcpy(sPk, line + 3, 32);
                sPk[32] = '\0';
            }
        }
        fclose(f);
    }
    if (sPk[0] == '\0') {
        for (i = 0; i < 4; i++) {
            SDL_snprintf(sPk + i * 8, 9, "%08x", (unsigned)random32());
        }
        f = fopen(path, "w");
        if (f == NULL) {
            sPk[0] = '\0';
            return 0;
        }
        fprintf(f, "# This game's player key for the online friends list (keep it: a new one is a new player).\npk=%s\n", sPk);
        fclose(f);
        SDL_Log("bt3: friends: a new player key (%s)", path);
    }
    return 1;
}

/* ---- the requests ---- */
static void json_text(char *out, size_t size, const char *s) { /* (a name: printable, no quote or backslash) */
    size_t n = 0;
    for (; *s != '\0' && n + 1 < size; s++) {
        out[n++] = (*s >= 0x20 && *s < 0x7f && *s != '"' && *s != '\\') ? *s : ' ';
    }
    out[n] = '\0';
}

typedef struct {
    char what[16];
    char body[512];
} Req;

static int fields(char *line, char *f[], int max) {
    int n = 0;
    f[n++] = line;
    for (; *line != '\0' && n < max; line++) {
        if (*line == '|') {
            *line = '\0';
            f[n++] = line + 1;
        }
    }
    return n;
}

static void put_friend(PortFriend *p, char *f[], int n, int kind) {
    memset(p, 0, sizeof(*p));
    SDL_strlcpy(p->code, f[0], sizeof(p->code));
    if (n > 1) SDL_strlcpy(p->name, f[1], sizeof(p->name));
    if (kind == 'f' && n >= 6) {
        p->fighter = atoi(f[2]);
        p->online = atoi(f[3]);
        SDL_strlcpy(p->state, f[4], sizeof(p->state));
        SDL_strlcpy(p->lobby, f[5], sizeof(p->lobby));
    } else if (kind == 'i' && n >= 4) {
        SDL_strlcpy(p->lobby, f[2], sizeof(p->lobby));
        p->ago = atoi(f[3]);
    } else if (kind == 'r' && n >= 5) {
        p->fighter = atoi(f[2]);
        p->online = atoi(f[3]);
        p->ago = atoi(f[4]);
    }
}

static void take_answer(const char *what, char *ans) {
    static PortFriendsView v; /* (the thread's own: copied in under the lock) */
    int presence = strcmp(what, "f/presence") == 0, board = strcmp(what, "f/board") == 0;
    char *line = ans, *next, *f[8];
    int n;
    SDL_LockSpinlock(&sLock);
    v = sV;
    SDL_UnlockSpinlock(&sLock);
    if (presence) {
        v.friendCount = v.askCount = v.sentCount = v.inviteCount = v.recentCount = v.blockCount = 0;
    }
    if (board) {
        v.boardCount = 0;
    }
    for (; line != NULL && *line != '\0'; line = next) {
        next = strchr(line, '\n');
        if (next != NULL) {
            *next++ = '\0';
        }
        if (line[0] == '\0' || line[1] == '\0') {
            continue;
        }
        if (strncmp(line, "me ", 3) == 0) {
            n = fields(line + 3, f, 8);
            SDL_strlcpy(v.myCode, f[0], sizeof(v.myCode));
            if (n >= 5) {
                v.rating = atoi(f[1]);
                v.wins = atoi(f[2]);
                v.losses = atoi(f[3]);
                v.place = atoi(f[4]);
            }
            v.known = 1;
        } else if (line[1] == ' ' && strchr("fqoirxb", line[0]) != NULL) {
            int k = line[0];
            n = fields(line + 2, f, 8);
            if (k == 'f' && v.friendCount < FR_MAX) put_friend(&v.friends[v.friendCount++], f, n, 'f');
            else if (k == 'q' && v.askCount < 20) put_friend(&v.asks[v.askCount++], f, n, 'q');
            else if (k == 'o' && v.sentCount < 20) put_friend(&v.sent[v.sentCount++], f, n, 'o');
            else if (k == 'i' && v.inviteCount < 5) put_friend(&v.invites[v.inviteCount++], f, n, 'i');
            else if (k == 'r' && v.recentCount < 20) put_friend(&v.recent[v.recentCount++], f, n, 'r');
            else if (k == 'x' && v.blockCount < 50) put_friend(&v.blocked[v.blockCount++], f, n, 'x');
            else if (k == 'b' && v.boardCount < FR_MAX && n >= 7) {
                PortFriend *p = &v.board[v.boardCount++];
                memset(p, 0, sizeof(*p));
                p->place = atoi(f[0]);
                SDL_strlcpy(p->code, f[1], sizeof(p->code));
                SDL_strlcpy(p->name, f[2], sizeof(p->name));
                p->rating = atoi(f[3]);
                p->wins = atoi(f[4]);
                p->losses = atoi(f[5]);
                p->fighter = atoi(f[6]);
            }
        } else if (strncmp(line, "ok ", 3) == 0 || strncmp(line, "error ", 6) == 0) {
            const char *w = line[0] == 'o' ? line + 3 : line + 6;
            const char *say = strcmp(w, "sent") == 0 ? "Friend request sent."
                            : strcmp(w, "friends") == 0 ? "You are friends now."
                            : strcmp(w, "declined") == 0 ? "Request declined."
                            : strcmp(w, "remove") == 0 ? "Friend removed."
                            : strcmp(w, "block") == 0 ? "Player blocked."
                            : strcmp(w, "unblock") == 0 ? "Player unblocked."
                            : strcmp(w, "invited") == 0 ? "Invitation sent."
                            : strcmp(w, "nocode") == 0 ? "No player has that friend code."
                            : strcmp(w, "self") == 0 ? "That is your own friend code."
                            : strcmp(w, "notfriend") == 0 ? "Only friends can be invited."
                            : strcmp(w, "busy") == 0 ? "The service is busy: try again in a while."
                            : NULL;
            if (say == NULL && (w[0] == '+' || w[0] == '-')) {
                SDL_snprintf(v.note, sizeof(v.note), "Ranked: rating %s", w);
                v.noteSeq++;
            } else if (say == NULL && strcmp(w, "waiting") == 0) {
                SDL_strlcpy(v.note, "Ranked: result sent (the rating moves when the other game's comes)", sizeof(v.note));
                v.noteSeq++;
            } else if (say == NULL && strcmp(w, "disagree") == 0) {
                SDL_strlcpy(v.note, "Ranked: the two games disagreed: not counted", sizeof(v.note));
                v.noteSeq++;
            }
            if (say != NULL) {
                SDL_strlcpy(v.note, say, sizeof(v.note));
                v.noteSeq++;
            }
        }
    }
    SDL_LockSpinlock(&sLock);
    sV = v;
    SDL_UnlockSpinlock(&sLock);
}

static int SDLCALL req_thread(void *arg) {
    Req *r = (Req *)arg;
    char *ans = (char *)SDL_malloc(ANSWER);
    if (ans != NULL) {
        ans[0] = '\0';
        if (Match_Post(r->what, r->body, ans, ANSWER) && ans[0] != '\0' && ans[0] != '{') {
            take_answer(r->what, ans);
        }
        SDL_free(ans);
    }
    SDL_free(r);
    SDL_SetAtomicInt(&sBusy, 0);
    return 0;
}

static void send_req(const char *what, const char *extra) {
    Req *r;
    SDL_Thread *t;
    char name[40];
    if (!key_load() || !SDL_CompareAndSwapAtomicInt(&sBusy, 0, 1)) {
        return;
    }
    r = (Req *)SDL_malloc(sizeof(Req));
    if (r == NULL) {
        SDL_SetAtomicInt(&sBusy, 0);
        return;
    }
    json_text(name, sizeof(name), Port_NetNameGet());
    SDL_snprintf(r->what, sizeof(r->what), "%s", what);
    SDL_snprintf(r->body, sizeof(r->body), "{\"pk\":\"%s\",\"name\":\"%s\",\"fighter\":%d%s%s}", sPk, name, Port_LobbyFighter(),
                 extra[0] != '\0' ? "," : "", extra);
    t = SDL_CreateThread(req_thread, "bt3-friends", r);
    if (t == NULL) {
        SDL_free(r);
        SDL_SetAtomicInt(&sBusy, 0);
    } else {
        SDL_DetachThread(t);
    }
}

static void queue(const char *what, const char *extra) {
    if (sQn < (int)(sizeof(sQ) / sizeof(sQ[0]))) {
        SDL_strlcpy(sQ[sQn].what, what, sizeof(sQ[sQn].what));
        SDL_strlcpy(sQ[sQn].extra, extra, sizeof(sQ[sQn].extra));
        sQn++;
    }
}

static void code_extra(char *out, size_t size, const char *key, const char *code) {
    char c[FR_CODE];
    json_text(c, sizeof(c), code);
    SDL_snprintf(out, size, "\"%s\":\"%s\"", key, c);
}

void PortFriends_Tick(void) {
    static int off = -1;
    uint64_t now = SDL_GetTicks();
    PortLobbyView lv;
    int inLobby;
    if (off < 0) {
        off = Port_Setting("friends_online", 1) == 0;
    }
    if (off || SDL_GetAtomicInt(&sBusy)) {
        return;
    }
    inLobby = Port_LobbyView(&lv) && lv.state == 2 && lv.code[0] != '\0';
    if (inLobby && strcmp(lv.code, sSeenRoom) != 0) { /* (a lobby entered: its other members are recent players) */
        char extra[40];
        SDL_strlcpy(sSeenRoom, lv.code, sizeof(sSeenRoom));
        SDL_snprintf(extra, sizeof(extra), "\"room\":\"%s\"", lv.code);
        queue("f/seen", extra);
        sNextMs = now; /* (and the friends told at once) */
    }
    if (sQn > 0) {
        char what[16], extra[200];
        SDL_strlcpy(what, sQ[0].what, sizeof(what));
        SDL_strlcpy(extra, sQ[0].extra, sizeof(extra));
        memmove(&sQ[0], &sQ[1], sizeof(sQ[0]) * (size_t)(sQn - 1));
        sQn--;
        send_req(what, extra);
        if (strcmp(what, "f/presence") != 0 && strcmp(what, "f/board") != 0) {
            sNextMs = now + 300; /* (what changed: the lists again soon after) */
        }
        return;
    }
    if (now >= sNextMs) {
        char extra[64];
        SDL_snprintf(extra, sizeof(extra), "\"state\":\"%s\",\"lobby\":\"%s\"", inLobby ? "lobby" : "online", inLobby ? lv.code : "");
        send_req("f/presence", extra);
        sLastMs = now;
        sNextMs = now + (sShow || inLobby ? 30000 : 60000);
    }
}

void PortFriends_View(PortFriendsView *out) {
    SDL_LockSpinlock(&sLock);
    *out = sV;
    SDL_UnlockSpinlock(&sLock);
}

int PortFriends_Known(void) {
    return PortFriends_MyCode() != NULL;
}

const char *PortFriends_MyCode(void) {
    static char code[FR_CODE];
    SDL_LockSpinlock(&sLock);
    SDL_strlcpy(code, sV.myCode, sizeof(code));
    SDL_UnlockSpinlock(&sLock);
    return code[0] != '\0' ? code : NULL;
}

void PortFriends_Refresh(void) {
    sNextMs = 0;
}

void PortFriends_Add(const char *code) {
    char extra[64];
    code_extra(extra, sizeof(extra), "code", code);
    queue("f/add", extra);
}

void PortFriends_Answer(const char *code, int yes) {
    char extra[80];
    code_extra(extra, sizeof(extra), "code", code);
    SDL_strlcat(extra, yes ? ",\"yes\":true" : ",\"yes\":false", sizeof(extra));
    queue("f/answer", extra);
}

void PortFriends_Remove(const char *code) {
    char extra[64];
    code_extra(extra, sizeof(extra), "code", code);
    queue("f/remove", extra);
}

void PortFriends_Block(const char *code, int block) {
    char extra[64];
    code_extra(extra, sizeof(extra), "code", code);
    queue(block ? "f/block" : "f/unblock", extra);
}

void PortFriends_Invite(const char *code) {
    PortLobbyView lv;
    char extra[100], room[24];
    if (!Port_LobbyView(&lv) || lv.state != 2 || lv.code[0] == '\0') {
        SDL_LockSpinlock(&sLock);
        SDL_strlcpy(sV.note, "Open a lobby first (Online, Lobby): then invite.", sizeof(sV.note));
        sV.noteSeq++;
        SDL_UnlockSpinlock(&sLock);
        return;
    }
    code_extra(extra, sizeof(extra), "code", code);
    SDL_snprintf(room, sizeof(room), ",\"lobby\":\"%s\"", lv.code);
    SDL_strlcat(extra, room, sizeof(extra));
    queue("f/invite", extra);
}

void PortFriends_Board(int friendsOnly) {
    SDL_LockSpinlock(&sLock);
    sV.boardFriends = friendsOnly != 0;
    SDL_UnlockSpinlock(&sLock);
    queue("f/board", friendsOnly ? "\"scope\":\"friends\"" : "\"scope\":\"top\"");
}

void PortFriends_Result(const char *match, const char *otherCode, int won) {
    char extra[160], m[48], c[FR_CODE];
    json_text(m, sizeof(m), match);
    json_text(c, sizeof(c), otherCode);
    SDL_snprintf(extra, sizeof(extra), "\"match\":\"%s\",\"other\":\"%s\",\"won\":%d", m, c, won ? 1 : 0);
    queue("f/result", extra);
}

void PortFriends_Show(int tab) {
    sShow = 1;
    sTab = tab;
    sNextMs = 0;
}

int PortFriends_Shown(int *tab) {
    *tab = sTab;
    sTab = -1;
    return sShow;
}

void PortFriends_Hide(void) {
    sShow = 0;
}

int PortFriends_IsShown(void) {
    return sShow;
}

void PortFriends_JoinSet(const char *lobbyCode) {
    SDL_strlcpy(sJoin, lobbyCode, sizeof(sJoin));
}

int PortFriends_JoinTake(char *out) {
    if (sJoin[0] == '\0') {
        return 0;
    }
    SDL_strlcpy(out, sJoin, 7);
    sJoin[0] = '\0';
    return 1;
}
