/*
 * Voice between the two players of an online match (15 Nov 2026, the user: "a simple mic button: my mic is heard by
 * the other and theirs by me").
 *
 * The default microphone is read through SDL at 8 kHz mono, 20 ms at a time, made 8-bit with the mu-law of telephones
 * (160 bytes a packet, 8 KB a second each way) and sent to the other player beside the match's own packets (T_VOICE in
 * net.c). What comes in is played on the default speaker at once; more than half a second waiting is dropped, so a
 * slow line never makes the voice late. Nothing of it touches the game: the fight is the same with or without it.
 * Ctrl+M or the option "Mic on" turns this player's microphone on and off (setting "mic", off by default).
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <string.h>
#include "net_voice.h"

extern int Port_Setting(const char *name, int def);   /* plat_settings.c */
extern void Port_SettingSave(const char *name, int value);
extern void Port_SettingsWrite(void);

#define RATE 8000
static SDL_AudioStream *sIn, *sOut;
static int sOn = -1;
static volatile int sWantClose;
static volatile uint64_t sLiveMs, sHeardMs;

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

int PortVoice_MicOn(void) {
    if (sOn < 0) {
        sOn = Port_Setting("mic", 0) != 0;
    }
    return sOn;
}

void PortVoice_MicSet(int on) {
    sOn = on != 0;
    Port_SettingSave("mic", sOn);
    Port_SettingsWrite();
}

int PortVoice_Shown(void) {
    return SDL_GetTicks() - sLiveMs < 1500;
}

int PortVoice_TheyTalk(void) {
    return SDL_GetTicks() - sHeardMs < 300;
}

void PortVoice_Live(void) {
    sLiveMs = SDL_GetTicks();
}

static void close_all(void) {
    if (sIn != NULL) {
        SDL_DestroyAudioStream(sIn);
        sIn = NULL;
    }
    if (sOut != NULL) {
        SDL_DestroyAudioStream(sOut);
        sOut = NULL;
    }
}

void PortVoice_Reset(void) {
    sWantClose = 1; /* (closed on the game thread, which owns the streams) */
}

int PortVoice_Outgoing(uint8_t *out) {
    static const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, RATE};
    int16_t pcm[VOICE_FRAME];
    int i;
    if (sWantClose) {
        sWantClose = 0;
        close_all();
    }
    if (!PortVoice_MicOn()) {
        if (sIn != NULL) {
            SDL_DestroyAudioStream(sIn);
            sIn = NULL;
        }
        return 0;
    }
    if (sIn == NULL) {
        sIn = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_RECORDING, &spec, NULL, NULL);
        if (sIn == NULL) {
            SDL_Log("bt3: voice: no microphone (%s)", SDL_GetError());
            sOn = 0;
            return 0;
        }
        SDL_ResumeAudioStreamDevice(sIn);
    }
    if (SDL_GetAudioStreamAvailable(sIn) < (int)sizeof(pcm) || SDL_GetAudioStreamData(sIn, pcm, sizeof(pcm)) != (int)sizeof(pcm)) {
        return 0;
    }
    for (i = 0; i < VOICE_FRAME; i++) {
        out[i] = lin2ulaw(pcm[i]);
    }
    return VOICE_FRAME;
}

void PortVoice_Heard(const uint8_t *in, int n) {
    static const SDL_AudioSpec spec = {SDL_AUDIO_S16, 1, RATE};
    int16_t pcm[VOICE_FRAME];
    int i;
    if (n <= 0) {
        return;
    }
    if (n > VOICE_FRAME) {
        n = VOICE_FRAME;
    }
    if (sOut == NULL) {
        sOut = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (sOut == NULL) {
            return;
        }
        SDL_ResumeAudioStreamDevice(sOut);
    }
    if (SDL_GetAudioStreamQueued(sOut) > RATE) { /* more than half a second behind: start again from now */
        SDL_ClearAudioStream(sOut);
    }
    for (i = 0; i < n; i++) {
        pcm[i] = ulaw2lin(in[i]);
    }
    SDL_PutAudioStreamData(sOut, pcm, n * 2);
    sHeardMs = SDL_GetTicks();
}
