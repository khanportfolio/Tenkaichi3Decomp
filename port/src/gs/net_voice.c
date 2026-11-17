/*
 * Voice between the two players of an online match (15 Nov 2026, the user: "a simple mic button: my mic is heard by
 * the other and theirs by me"), and among a lobby's members (17 Nov 2026: "mic and speaker in the lobby").
 *
 * The default microphone is read through SDL at 8 kHz mono, 20 ms at a time, made 8-bit with the mu-law of telephones
 * (160 bytes a packet, 8 KB a second each way) and sent beside the match's own packets (T_VOICE in net.c) or to the
 * lobby's owner, who passes it on to every other member (T_L_VOICE in net_lobby.c). Only while the player speaks: a
 * quiet microphone sends nothing (a short hang keeps the ends of words). What comes in is played on the default
 * speaker at once, each speaker on its own stream (SDL mixes them, so several can talk together); more than half a
 * second waiting is dropped, so a slow line never makes the voice late. Nothing of it touches the game: the fight is
 * the same with or without it.
 * The mic (setting "mic"): Off, On, or Push to talk (while V is held); Ctrl+M switches On and Off. The speaker
 * (setting "speaker", on by default): off, nobody's voice is played.
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "net_voice.h"

extern int Port_Setting(const char *name, int def);   /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

#define RATE 8000
#define SPEAKERS 8
#define GATE 260        /* a 20 ms frame quieter than this (mean of the samples' size) is not sent, */
#define HANG_MS 400     /* ... unless the player spoke this short a time before */
static SDL_AudioStream *sIn;
static struct {
    uint32_t who;
    SDL_AudioStream *s;
    uint64_t heardMs;
} sOut[SPEAKERS];
static int sMode = -1, sSpeaker = -1;
static uint64_t sNoSpeakerMs; /* (when opening the speaker last failed: tried again 10 s later) */
static volatile int sWantClose;
static volatile uint64_t sLiveMs, sSpokeMs;

static uint8_t lin2ulaw(int16_t s) {
    int sign = (s >> 8) & 0x80, exp, mant, v = s;
    if (sign) v = -v;
    if (v > 32635) v = 32635;
    v += 0x84;
    for (exp = 7; exp > 0 && !(v & (0x4000 >> (7 - exp))); exp--) {
    }
    mant = (v >> (exp + 3)) & 0x0F;
    return (uint8_t)~(sign | (exp << 4) | mant);
}

static int16_t ulaw2lin(uint8_t u) {
    int t;
    u = (uint8_t)~u;
    t = ((u & 0x0F) << 3) + 0x84;
    t <<= (u & 0x70) >> 4;
    return (int16_t)((u & 0x80) ? (0x84 - t) : (t - 0x84));
}

int PortVoice_Mode(void) {
    if (sMode < 0) {
        int v = Port_Setting("mic", 0);
        sMode = v == VOICE_ON || v == VOICE_PTT ? v : VOICE_OFF;
    }
    return sMode;
}

void PortVoice_ModeSet(int mode) {
    sMode = mode == VOICE_ON || mode == VOICE_PTT ? mode : VOICE_OFF;
    Port_SettingSave("mic", sMode);
    Port_SettingsWrite();
}

static int ptt_held(void) {
    const bool *keys = SDL_GetKeyboardState(NULL);
    return keys != NULL && keys[SDL_SCANCODE_V];
}

int PortVoice_MicOn(void) {
    int m = PortVoice_Mode();
    return m == VOICE_ON || (m == VOICE_PTT && ptt_held());
}

void PortVoice_MicSet(int on) {
    PortVoice_ModeSet(on ? VOICE_ON : VOICE_OFF);
}

int PortVoice_SpeakerOn(void) {
    if (sSpeaker < 0) {
        sSpeaker = Port_Setting("speaker", 1) != 0;
    }
    return sSpeaker;
}

void PortVoice_SpeakerSet(int on) {
    sSpeaker = on != 0;
    Port_SettingSave("speaker", sSpeaker);
    Port_SettingsWrite();
}

int PortVoice_Shown(void) {
    return SDL_GetTicks() - sLiveMs < 1500;
}

int PortVoice_Talking(uint32_t who) {
    int i;
    if (who == 0) {
        return PortVoice_MicOn() && SDL_GetTicks() - sSpokeMs < 300;
    }
    for (i = 0; i < SPEAKERS; i++) {
        if (sOut[i].s != NULL && sOut[i].who == who) {
            return SDL_GetTicks() - sOut[i].heardMs < 300;
        }
    }
    return 0;
}

int PortVoice_TheyTalk(void) {
    return PortVoice_Talking(VOICE_MATCH);
}

void PortVoice_Live(void) {
    sLiveMs = SDL_GetTicks();
}

static void close_all(void) {
    int i;
    if (sIn != NULL) {
        SDL_DestroyAudioStream(sIn);
        sIn = NULL;
    }
    for (i = 0; i < SPEAKERS; i++) {
        if (sOut[i].s != NULL) {
            SDL_DestroyAudioStream(sOut[i].s);
            sOut[i].s = NULL;
        }
    }
}

void PortVoice_Reset(void) {
    sWantClose = 1; /* (closed on the game thread, which owns the streams) */
}

int PortVoice_Outgoing(uint8_t *out) {
    static const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, RATE};
    int16_t pcm[VOICE_FRAME];
    int i, level = 0;
    if (sWantClose) {
        sWantClose = 0;
        close_all();
    }
    if (getenv("BT3_VOICE_TEST") != NULL) { /* (tests: a tone instead of the microphone, 20 ms every 20 ms) */
        static uint64_t next;
        static unsigned phase;
        uint64_t now = SDL_GetTicks();
        if (now < next) {
            return 0;
        }
        next = (next == 0 || now - next > 100 ? now : next) + 20;
        for (i = 0; i < VOICE_FRAME; i++, phase++) {
            out[i] = lin2ulaw((int16_t)((phase / 9) % 2 ? 6000 : -6000)); /* (about 440 Hz) */
        }
        sSpokeMs = now;
        return VOICE_FRAME;
    }
    if (PortVoice_Mode() == VOICE_OFF) {
        if (sIn != NULL) {
            SDL_DestroyAudioStream(sIn);
            sIn = NULL;
        }
        return 0;
    }
    if (sIn == NULL) { /* (push to talk keeps the microphone open: no wait for it when V is pressed) */
        sIn = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
        if (sIn == NULL) {
            SDL_Log("bt3: voice: no microphone (%s)", SDL_GetError());
            sMode = VOICE_OFF;
            return 0;
        }
        SDL_ResumeAudioStreamDevice(sIn);
    }
    if (SDL_GetAudioStreamAvailable(sIn) < (int)sizeof(pcm) || SDL_GetAudioStreamData(sIn, pcm, sizeof(pcm)) != (int)sizeof(pcm)) {
        return 0;
    }
    if (!PortVoice_MicOn()) {
        return 0; /* (push to talk, V not held: read and dropped) */
    }
    for (i = 0; i < VOICE_FRAME; i++) {
        level += abs(pcm[i]);
    }
    if (level / VOICE_FRAME >= GATE) {
        sSpokeMs = SDL_GetTicks();
    } else if (SDL_GetTicks() - sSpokeMs > HANG_MS) {
        return 0; /* (quiet: nothing sent) */
    }
    for (i = 0; i < VOICE_FRAME; i++) {
        out[i] = lin2ulaw(pcm[i]);
    }
    return VOICE_FRAME;
}

void PortVoice_HeardFrom(uint32_t who, const uint8_t *in, int n) {
    static const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, RATE};
    int16_t pcm[VOICE_FRAME];
    int i, k = -1, old = 0;
    if (n <= 0 || who == 0 || !PortVoice_SpeakerOn()) {
        return;
    }
    if (n > VOICE_FRAME) {
        n = VOICE_FRAME;
    }
    for (i = 0; i < SPEAKERS; i++) {
        if (sOut[i].s != NULL && sOut[i].who == who) {
            k = i;
            break;
        }
        if (sOut[i].s == NULL && k < 0) {
            k = -2 - i; /* (a free stream, if this one has none) */
        }
        if (sOut[i].heardMs < sOut[old].heardMs) {
            old = i;
        }
    }
    if (k < 0 && sNoSpeakerMs != 0 && SDL_GetTicks() - sNoSpeakerMs < 10000) {
        return;
    }
    if (k < 0) {
        k = k == -1 ? old : -2 - k; /* (none free: the one heard longest ago gives its stream) */
        if (sOut[k].s != NULL) {
            SDL_DestroyAudioStream(sOut[k].s);
        }
        sOut[k].s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        sOut[k].who = who;
        if (sOut[k].s == NULL) {
            if (sNoSpeakerMs == 0) {
                SDL_Log("bt3: voice: hearing %08x, but no speaker (%s)", (unsigned)who, SDL_GetError());
            }
            sNoSpeakerMs = SDL_GetTicks();
            return;
        }
        sNoSpeakerMs = 0;
        SDL_Log("bt3: voice: hearing %08x", (unsigned)who);
        SDL_ResumeAudioStreamDevice(sOut[k].s);
    }
    if (SDL_GetAudioStreamQueued(sOut[k].s) > RATE) { /* more than half a second behind: start again from now */
        SDL_ClearAudioStream(sOut[k].s);
    }
    for (i = 0; i < n; i++) {
        pcm[i] = ulaw2lin(in[i]);
    }
    SDL_PutAudioStreamData(sOut[k].s, pcm, n * 2);
    sOut[k].heardMs = SDL_GetTicks();
}

void PortVoice_Heard(const uint8_t *in, int n) {
    PortVoice_HeardFrom(VOICE_MATCH, in, n);
}
