/*
 * Movies on PC: Sony's libmpeg (sceMpeg*) as the game's movie player uses it (src/sys/movie.c).
 *
 * On the PS2 the game reads the .PSS file itself, hands the bytes to the library to split off the video stream,
 * and asks for one decoded picture per frame. A picture comes back as 16 x 16 blocks of RGBA, columns first, which
 * the game uploads straight into the frame buffer. The sound is a separate ADX file (port/src/gs/snd_adx.c).
 *
 * Here the pictures come from the port's own decoder (plat_mpeg2.c: both movies are intra-only MPEG-2), reading
 * the movie file the game opened last (the path from plat_file.c): each sceMpegGetPicture decodes one picture and
 * rearranges it into the block order. The bytes the game feeds to sceMpegDemuxPss are ignored. Without a window a
 * movie ends at once, except when recorded input is played back (the movie has to take the same number of frames).
 */
extern int Port_NetSession(void); /* gs/net.c: no movies in an online session */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MOVIE_W 512
#define MOVIE_H 448

extern char gPortMoviePath[512];
extern int GsGpu_Enabled(void);

typedef struct Mpeg2 Mpeg2;
extern Mpeg2 *Mpeg2_Open(const char *path);
extern int Mpeg2_Decode(Mpeg2 *m);
extern void Mpeg2_Rgba(Mpeg2 *m, uint8_t *rgba);
extern int Mpeg2_Width(const Mpeg2 *m);
extern int Mpeg2_Height(const Mpeg2 *m);
extern void Mpeg2_Close(Mpeg2 *m);

static Mpeg2 *sMovie;
static int sStarted, sEnd;
static uint8_t sFrame[MOVIE_W * MOVIE_H * 4];

/* A movie of another size than the disc's (a player's own, mods/disc/DATA): its pictures do not fit the game's 512 x
   448 path, so the game is given black and the settings window's layer draws the picture over it at its own size
   (gs/ui.cpp). The newest picture is sBigShown; the next is made in sBigNext and the two change places. */
#define MOVIE_BIG_MAX_W 3840
#define MOVIE_BIG_MAX_H 2160
static uint8_t *sBigShown, *sBigNext;
static int sBigW, sBigH, sBigOn;
static unsigned sBigSeq;
static pthread_mutex_t sBigLock = PTHREAD_MUTEX_INITIALIZER;

/* The newest big picture, with the lock held (Port_MovieBigDone lets it go), or NULL when there is none. */
const uint8_t *Port_MovieBig(int *w, int *h, unsigned *seq) {
    pthread_mutex_lock(&sBigLock);
    if (!sBigOn || sBigShown == NULL) {
        pthread_mutex_unlock(&sBigLock);
        return NULL;
    }
    *w = sBigW;
    *h = sBigH;
    *seq = sBigSeq;
    return sBigShown;
}

void Port_MovieBigDone(void) {
    pthread_mutex_unlock(&sBigLock);
}

static int big_picture(void *rgb, int blocks) {
    int w = Mpeg2_Width(sMovie), h = Mpeg2_Height(sMovie);
    size_t bytes = (size_t)w * h * 4;
    uint8_t *swap;

    if (w <= 0 || h <= 0 || w > MOVIE_BIG_MAX_W || h > MOVIE_BIG_MAX_H) {
        sEnd = 1;
        return -1;
    }
    if (w != sBigW || h != sBigH || sBigNext == NULL || sBigShown == NULL) {
        pthread_mutex_lock(&sBigLock);
        free(sBigShown);
        free(sBigNext);
        sBigShown = malloc(bytes);
        sBigNext = malloc(bytes);
        sBigW = w;
        sBigH = h;
        sBigOn = 0;
        pthread_mutex_unlock(&sBigLock);
        if (sBigShown == NULL || sBigNext == NULL) {
            sEnd = 1;
            return -1;
        }
    }
    Mpeg2_Rgba(sMovie, sBigNext);
    pthread_mutex_lock(&sBigLock);
    swap = sBigShown;
    sBigShown = sBigNext;
    sBigNext = swap;
    sBigOn = 1;
    sBigSeq++;
    pthread_mutex_unlock(&sBigLock);
    memset(rgb, 0, (size_t)blocks * 0x400);
    return 0;
}

static void movie_close(void) {
    pthread_mutex_lock(&sBigLock);
    sBigOn = 0;
    pthread_mutex_unlock(&sBigLock);
    if (sMovie != NULL) {
        Mpeg2_Close(sMovie);
        sMovie = NULL;
    }
    sStarted = 0;
    sEnd = 0;
}

static void movie_start(void) {
    sStarted = 1;
    sEnd = 1;
    /* without a window too when recorded input is played back: the movie has to take the same number of frames */
    if ((!GsGpu_Enabled() && getenv("BT3_PAD_PLAY") == NULL) || gPortMoviePath[0] == '\0' || getenv("BT3_NOMOVIE") != NULL || Port_NetSession()) {
        return;
    }
    sMovie = Mpeg2_Open(gPortMoviePath);
    sEnd = sMovie == NULL;
}

int sceMpegInit(void) { return 0; }
int sceMpegCreate(void *mp, void *work, int size) { (void)mp; (void)work; (void)size; movie_close(); return 0; }
int sceMpegDelete(void *mp) { (void)mp; movie_close(); return 0; }
int sceMpegReset(void *mp) { (void)mp; movie_close(); return 0; }
void *sceMpegAddCallback(void *mp, int type, void *cb, void *data) { (void)mp; (void)type; (void)cb; (void)data; return NULL; }
void *sceMpegAddStrCallback(void *mp, int type, int ch, void *cb, void *data) { (void)mp; (void)type; (void)ch; (void)cb; (void)data; return NULL; }
int sceMpegDemuxPss(void *mp, void *data, int size) { (void)mp; (void)data; (void)size; return 0; }

int sceMpegIsEnd(void *mp) {
    (void)mp;
    if (!sStarted) {
        movie_start();
    }
    return sEnd;
}

/* One picture into `rgb`: `blocks` 16 x 16 RGBA blocks of 0x400 bytes, columns first (the order the game's upload
   chain expects); alpha 0x80. */
int sceMpegGetPicture(void *mp, void *rgb, int blocks) {
    uint8_t *out = rgb;
    int cols = MOVIE_W / 16, rows = MOVIE_H / 16, i, j, y, x;

    (void)mp;
    if (!sStarted) {
        movie_start();
    }
    if (sMovie == NULL || !Mpeg2_Decode(sMovie)) {
        sEnd = 1;
        return -1;
    }
    if (Mpeg2_Width(sMovie) != MOVIE_W || Mpeg2_Height(sMovie) != MOVIE_H) {
        return big_picture(rgb, blocks);
    }
    Mpeg2_Rgba(sMovie, sFrame);
    for (i = 0; i < cols; i++) {
        for (j = 0; j < rows && i * rows + j < blocks; j++) {
            uint8_t *b = out + (size_t)(i * rows + j) * 0x400;
            for (y = 0; y < 16; y++) {
                const uint8_t *s = &sFrame[((j * 16 + y) * MOVIE_W + i * 16) * 4];
                for (x = 0; x < 16; x++) {
                    b[(y * 16 + x) * 4 + 0] = s[x * 4 + 0];
                    b[(y * 16 + x) * 4 + 1] = s[x * 4 + 1];
                    b[(y * 16 + x) * 4 + 2] = s[x * 4 + 2];
                    b[(y * 16 + x) * 4 + 3] = 0x80;
                }
            }
        }
    }
    return 0;
}


/*
 * Story lines at the start of a fight, from the file story_talk.txt next to the game (the player's own text):
 *     group Name 0-11 198          a name for a set of character numbers
 *     Name|Other|Name's line|Other's line
 * The game's voice line plays as always; the text is shown over it (gs/ui.cpp). Not in an online session.
 * Several lines for the same two groups: one of them is chosen at random for each fight.
 *
 * Lines during the fight, shown for a few seconds:
 *     taunt:Name|Other|line         Name says it when Other falls to half health (Other * = anyone)
 *     half:Name|Other|line          Name says it when Name falls to half health
 *     low:Name|Other|line           Name says it when Name falls below a quarter
 *     finish:Name|Other|line        Name says it on the hit that ends Other
 * A line written for the pair is preferred to one for *; one of the matching lines is chosen at random.
 */
#include <time.h>
#define STORY_GROUPS 128
#define STORY_RANGES 12
#define STORY_PAIRS 2048
#define STORY_EVENTS 2048
#define STORY_TEXT 240
#define STORY_SHOW_MS 4500
typedef struct { char name[24]; int count, lo[STORY_RANGES], hi[STORY_RANGES]; } StoryGroup;
typedef struct { int a, b; char lineA[STORY_TEXT], lineB[STORY_TEXT]; } StoryPair;
enum { EV_TAUNT, EV_HALF, EV_LOW, EV_FINISH, EV_KINDS };
typedef struct { int kind, a, b; char line[STORY_TEXT]; } StoryEvent; /* b: -1 for anyone */
static StoryGroup *sStoryGroups;
static StoryPair *sStoryPairs;
static StoryEvent *sStoryEvents;
static int sStoryGroupCount, sStoryPairCount, sStoryEventCount, sStoryLoaded;
static char sStoryNow[STORY_TEXT + 32];
static uint64_t sStoryUntil; /* 0: shown until cleared (the entrance lines); else the time it goes, in ms */
static int sStoryPick = -1, sStoryPickA, sStoryPickB; /* the pair chosen for the first speaker, for the second */
static pthread_mutex_t sStoryLock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t story_now_ms(void);
/* The talk's own random numbers: the game's rand() drives the fights (and replays), so it is not touched here. */
static uint32_t sStorySeed;
static int story_rand(void) {
    if (sStorySeed == 0) {
        sStorySeed = (uint32_t)story_now_ms() | 1u;
    }
    sStorySeed ^= sStorySeed << 13;
    sStorySeed ^= sStorySeed >> 17;
    sStorySeed ^= sStorySeed << 5;
    return (int)(sStorySeed >> 1);
}

static uint64_t story_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static int story_find(const char *name) {
    int i;
    for (i = 0; i < sStoryGroupCount; i++) {
        if (strcmp(sStoryGroups[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static void story_group(char *line) {
    StoryGroup *g;
    char *tok = strtok(line, " \t\r\n");

    if (tok == NULL || sStoryGroupCount >= STORY_GROUPS) {
        return;
    }
    g = &sStoryGroups[sStoryGroupCount++];
    memset(g, 0, sizeof(*g));
    snprintf(g->name, sizeof(g->name), "%s", tok);
    while ((tok = strtok(NULL, " \t\r\n")) != NULL && g->count < STORY_RANGES) {
        char *dash = strchr(tok, '-');
        g->lo[g->count] = atoi(tok);
        g->hi[g->count] = dash != NULL ? atoi(dash + 1) : g->lo[g->count];
        g->count++;
    }
}

/* "kind:Name|Other|line" */
static void story_event(int kind, char *line) {
    char *f[3];
    int i;
    StoryEvent *e;

    f[0] = line;
    for (i = 1; i < 3; i++) {
        f[i] = strchr(f[i - 1], '|');
        if (f[i] == NULL) {
            return;
        }
        *f[i]++ = '\0';
    }
    f[2][strcspn(f[2], "\r\n")] = '\0';
    if (sStoryEventCount >= STORY_EVENTS || story_find(f[0]) < 0 || f[2][0] == '\0') {
        return;
    }
    e = &sStoryEvents[sStoryEventCount];
    e->kind = kind;
    e->a = story_find(f[0]);
    e->b = strcmp(f[1], "*") == 0 ? -1 : story_find(f[1]);
    if (e->b < 0 && strcmp(f[1], "*") != 0) {
        return; /* an unknown name */
    }
    snprintf(e->line, sizeof(e->line), "%s", f[2]);
    sStoryEventCount++;
}

static void story_pair(char *line) {
    char *f[4];
    int i;
    StoryPair *p;
    static const char *const kinds[EV_KINDS] = {"taunt:", "half:", "low:", "finish:"};

    for (i = 0; i < EV_KINDS; i++) {
        size_t n = strlen(kinds[i]);
        if (strncmp(line, kinds[i], n) == 0) {
            story_event(i, line + n);
            return;
        }
    }
    f[0] = line;
    for (i = 1; i < 4; i++) {
        f[i] = strchr(f[i - 1], '|');
        if (f[i] == NULL) {
            return;
        }
        *f[i]++ = '\0';
    }
    f[3][strcspn(f[3], "\r\n")] = '\0';
    if (sStoryPairCount >= STORY_PAIRS || story_find(f[0]) < 0 || story_find(f[1]) < 0) {
        return;
    }
    p = &sStoryPairs[sStoryPairCount++];
    p->a = story_find(f[0]);
    p->b = story_find(f[1]);
    snprintf(p->lineA, sizeof(p->lineA), "%s", f[2]);
    snprintf(p->lineB, sizeof(p->lineB), "%s", f[3]);
}

static void story_load(void) {
    char line[640];
    FILE *fp;

    sStoryLoaded = 1;
    fp = fopen("story_talk.txt", "r");
    if (fp == NULL) {
        return;
    }
    sStoryGroups = calloc(STORY_GROUPS, sizeof(StoryGroup));
    sStoryPairs = calloc(STORY_PAIRS, sizeof(StoryPair));
    sStoryEvents = calloc(STORY_EVENTS, sizeof(StoryEvent));
    if (sStoryGroups != NULL && sStoryPairs != NULL && sStoryEvents != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "group ", 6) == 0) {
                story_group(line + 6);
            } else if (line[0] != '#' && strchr(line, '|') != NULL) {
                story_pair(line);
            }
        }
    }
    fclose(fp);
    fprintf(stderr, "bt3: story talk: %d groups, %d pairs, %d lines during fights\n", sStoryGroupCount, sStoryPairCount,
            sStoryEventCount);
}

static int story_group_of(int chara) {
    int i, r;
    for (i = 0; i < sStoryGroupCount; i++) {
        for (r = 0; r < sStoryGroups[i].count; r++) {
            if (chara >= sStoryGroups[i].lo[r] && chara <= sStoryGroups[i].hi[r]) {
                return i;
            }
        }
    }
    return -1;
}

void Port_StoryTalkClear(void) {
    pthread_mutex_lock(&sStoryLock);
    sStoryNow[0] = '\0';
    sStoryUntil = 0;
    pthread_mutex_unlock(&sStoryLock);
}

extern int Port_CharaOmni(int chara); /* plat_charas.c */

/* `speaker` starts speaking to `other`: their pair's line, if the file has one, is shown until the next call. */
void Port_StoryTalkSay(int speaker, int other) {
    const char *text = NULL;
    int a, b, i;

    Port_StoryTalkClear();
    if (Port_NetSession()) {
        return;
    }
    if (!sStoryLoaded) {
        story_load();
    }
    a = story_group_of(speaker);
    b = story_group_of(other);
    if ((a < 0 || b < 0) && Port_CharaOmni(speaker) != Port_CharaOmni(other)) { /* no pair written: every fighter still knows him */
        pthread_mutex_lock(&sStoryLock);
        snprintf(sStoryNow, sizeof(sStoryNow), "%s", Port_CharaOmni(speaker) ? "" : "The Omni King?! Please... not me!");
        pthread_mutex_unlock(&sStoryLock);
        return;
    }
    if (a < 0 || b < 0) {
        return;
    }
    if (sStoryPick >= 0 && sStoryPickA == b && sStoryPickB == a) {
        i = sStoryPick; /* the second speaker answers with the same pair */
        text = sStoryPairs[i].a == a ? sStoryPairs[i].lineA : sStoryPairs[i].lineB;
        sStoryPick = -1;
    } else {
        int n = 0, chosen = -1;
        for (i = 0; i < sStoryPairCount; i++) {
            if ((sStoryPairs[i].a == a && sStoryPairs[i].b == b) || (sStoryPairs[i].a == b && sStoryPairs[i].b == a)) {
                if (story_rand() % ++n == 0) { /* each of the n matching pairs is kept with chance 1/n */
                    chosen = i;
                }
            }
        }
        sStoryPick = chosen;
        sStoryPickA = a;
        sStoryPickB = b;
        if (chosen >= 0) {
            text = sStoryPairs[chosen].a == a ? sStoryPairs[chosen].lineA : sStoryPairs[chosen].lineB;
        }
    }
    if (text != NULL && text[0] != '\0') { /* (an empty line: nothing shown, for a voice with no matching words) */
        pthread_mutex_lock(&sStoryLock);
        snprintf(sStoryNow, sizeof(sStoryNow), "%s: %s", sStoryGroups[a].name, text);
        sStoryUntil = 0;
        pthread_mutex_unlock(&sStoryLock);
    }
}

/* One line of the kind for the speaker against the other, if the file has one (a pair's own line first). */
static int story_say_event(int kind, int speaker, int other) {
    int a = story_group_of(speaker), b = story_group_of(other), i, n = 0, chosen = -1, pass;

    if (a < 0) {
        return 0;
    }
    for (pass = 0; pass < 2 && chosen < 0; pass++) {
        for (i = 0; i < sStoryEventCount; i++) {
            const StoryEvent *e = &sStoryEvents[i];
            if (e->kind == kind && e->a == a && (pass == 0 ? (b >= 0 && e->b == b) : e->b == -1)) {
                if (story_rand() % ++n == 0) {
                    chosen = i;
                }
            }
        }
    }
    if (chosen < 0) {
        return 0;
    }
    pthread_mutex_lock(&sStoryLock);
    snprintf(sStoryNow, sizeof(sStoryNow), "%s: %s", sStoryGroups[a].name, sStoryEvents[chosen].line);
    sStoryUntil = story_now_ms() + STORY_SHOW_MS;
    pthread_mutex_unlock(&sStoryLock);
    return 1;
}

/* A hit took `defender` from `before` to `after` health (of `max`): the lines during the fight. Each fires once per
   fighter until its health is full again (a new fight). */
void Port_StoryFightHit(int attacker, int defender, int before, int after, int max) {
    static int seen[1024];
    int *s;

    if (Port_NetSession() || max <= 0 || defender < 0 || defender >= 1024 || attacker < 0) {
        return;
    }
    if (!sStoryLoaded) {
        story_load();
    }
    if (sStoryEventCount == 0) {
        return;
    }
    s = &seen[defender];
    if (before >= max) {
        *s = 0;
    }
    if (after <= 0 && before > 0) {
        story_say_event(EV_FINISH, attacker, defender);
    } else if (!(*s & 2) && before * 4 > max && after * 4 <= max) {
        *s |= 2 | 1;
        story_say_event(EV_LOW, defender, attacker);
    } else if (!(*s & 1) && before * 2 > max && after * 2 <= max) {
        *s |= 1;
        if (story_rand() & 1) { /* the one who landed it gloats, or the one hit answers; the other if the first has nothing */
            if (!story_say_event(EV_TAUNT, attacker, defender)) {
                story_say_event(EV_HALF, defender, attacker);
            }
        } else if (!story_say_event(EV_HALF, defender, attacker)) {
            story_say_event(EV_TAUNT, attacker, defender);
        }
    }
}

int Port_StoryText(char *out, int size) {
    int on;

    pthread_mutex_lock(&sStoryLock);
    if (sStoryUntil != 0 && story_now_ms() >= sStoryUntil) {
        sStoryNow[0] = '\0';
        sStoryUntil = 0;
    }
    on = sStoryNow[0] != '\0';
    if (on) {
        snprintf(out, (size_t)size, "%s", sStoryNow);
    }
    pthread_mutex_unlock(&sStoryLock);
    return on;
}
