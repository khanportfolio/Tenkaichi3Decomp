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
 */
#define STORY_GROUPS 96
#define STORY_RANGES 8
#define STORY_PAIRS 1024
#define STORY_TEXT 240
typedef struct { char name[24]; int count, lo[STORY_RANGES], hi[STORY_RANGES]; } StoryGroup;
typedef struct { int a, b; char lineA[STORY_TEXT], lineB[STORY_TEXT]; } StoryPair;
static StoryGroup *sStoryGroups;
static StoryPair *sStoryPairs;
static int sStoryGroupCount, sStoryPairCount, sStoryLoaded;
static char sStoryNow[STORY_TEXT + 32];
static pthread_mutex_t sStoryLock = PTHREAD_MUTEX_INITIALIZER;

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

static void story_pair(char *line) {
    char *f[4];
    int i;
    StoryPair *p;

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
    if (sStoryGroups != NULL && sStoryPairs != NULL) {
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "group ", 6) == 0) {
                story_group(line + 6);
            } else if (line[0] != '#' && strchr(line, '|') != NULL) {
                story_pair(line);
            }
        }
    }
    fclose(fp);
    fprintf(stderr, "bt3: story talk: %d groups, %d pairs\n", sStoryGroupCount, sStoryPairCount);
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
        snprintf(sStoryNow, sizeof(sStoryNow), "%s", Port_CharaOmni(speaker) ? "Zeno: I am the Omni King. Bye bye." : "The Omni King?! Please... not me!");
        pthread_mutex_unlock(&sStoryLock);
        return;
    }
    if (a < 0 || b < 0) {
        return;
    }
    for (i = 0; i < sStoryPairCount && text == NULL; i++) {
        if (sStoryPairs[i].a == a && sStoryPairs[i].b == b) {
            text = sStoryPairs[i].lineA;
        } else if (sStoryPairs[i].a == b && sStoryPairs[i].b == a) {
            text = sStoryPairs[i].lineB;
        }
    }
    if (text != NULL) {
        pthread_mutex_lock(&sStoryLock);
        snprintf(sStoryNow, sizeof(sStoryNow), "%s: %s", sStoryGroups[a].name, text);
        pthread_mutex_unlock(&sStoryLock);
    }
}

int Port_StoryText(char *out, int size) {
    int on;

    pthread_mutex_lock(&sStoryLock);
    on = sStoryNow[0] != '\0';
    if (on) {
        snprintf(out, (size_t)size, "%s", sStoryNow);
    }
    pthread_mutex_unlock(&sStoryLock);
    return on;
}
