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

/* 18 Nov 2026 (the user: "not too loud, not too quiet, no background noise: the best setting"). Each 20 ms of the
   microphone is cleaned before it is sent (tried first on test voices and rooms, a simulation of these same steps:
   quiet, normal and loud voices all come out at -19..-21 dBFS; with nobody talking nothing is sent in a quiet room
   and 2-4 % of the time with a fan, a hum or typing, mostly the first second while the room is learnt):
   - a high-pass filter of two stages (about 100 Hz): rumble, mains hum and knocks on the desk go;
   - the room's noise level is followed all the time: down at once, up quickly while nobody speaks (a fan or a hum
     is learnt in about a second), hardly while someone does;
   - the gate opens on 60 ms clearly above that level (x2.8, and above an absolute minimum): a key's click or a
     knock is shorter; the 40 ms before the opening are sent too, so no word loses its start; while it is open,
     40 ms are enough to keep it so, and it closes 0.4 s after the last;
   - an automatic level brings speech to about -20 dBFS: a quiet microphone up to x8, a loud one down to x0.35,
     raised slowly and lowered fast; a soft limiter above that, so shouting never cracks;
   - in the hang between words the sound is a third as loud: the room is hardly heard.
   BT3_MIC_LOG: every 2 s the level heard, the room's level and the gain, in the log. */
#define MIC_TARGET 3300.0f
#define MIC_MIN 180.0f
static float sHp[4], sFloor = 120.0f, sGain = 2.0f;
static int sLoudRun;
static int16_t sPrev[2][VOICE_FRAME]; /* the last two frames (filtered), sent first when the gate opens */
static uint8_t sPend[3][VOICE_FRAME];
static int sPendN, sPendAt;

static void mic_encode(const int16_t *pcm, float g, uint8_t *out) {
    int i;
    for (i = 0; i < VOICE_FRAME; i++) {
        float y = (float)pcm[i] * g, a = y < 0.0f ? -y : y;
        if (a > 20000.0f) {
            a = 20000.0f + (a - 20000.0f) * 0.25f;
        }
        if (a > 32000.0f) {
            a = 32000.0f;
        }
        out[i] = lin2ulaw((int16_t)(y < 0.0f ? -a : a));
    }
}

/* 1: `out` holds the frame to send; 0: nothing (quiet). When the gate opens, the frames before come first. */
static int mic_process(int16_t *pcm, uint8_t *out) {
    float sum = 0.0f, rms;
    uint64_t now = SDL_GetTicks();
    int i, loud, voice, open = sSpokeMs != 0 && now - sSpokeMs <= HANG_MS;
    static uint64_t logAt;
    static int log = -1;
    for (i = 0; i < VOICE_FRAME; i++) {
        float x = (float)pcm[i], y1 = 0.927f * (sHp[1] + x - sHp[0]), y = 0.927f * (sHp[3] + y1 - sHp[2]);
        sHp[0] = x;
        sHp[1] = y1;
        sHp[2] = y1;
        sHp[3] = y;
        pcm[i] = (int16_t)(y > 32767.0f ? 32767.0f : y < -32768.0f ? -32768.0f : y);
        sum += y * y;
    }
    rms = SDL_sqrtf(sum / (float)VOICE_FRAME);
    loud = rms > sFloor * 2.8f && rms > MIC_MIN;
    sFloor += (rms - sFloor) * (rms < sFloor ? 0.20f : loud ? 0.0005f : 0.03f);
    if (sFloor < 20.0f) {
        sFloor = 20.0f;
    }
    sLoudRun = loud ? sLoudRun + 1 : 0;
    voice = loud && (sLoudRun >= 3 || (sLoudRun >= 2 && open));
    if (log < 0) {
        log = getenv("BT3_MIC_LOG") != NULL;
    }
    if (log && now >= logAt) {
        logAt = now + 2000;
        SDL_Log("bt3: mic: level %.0f, room %.0f, gain x%.2f, %s", (double)rms, (double)sFloor, (double)sGain, voice ? "speaking" : "quiet");
    }
    if (voice) {
        float want = MIC_TARGET / rms;
        want = want > 8.0f ? 8.0f : want < 0.35f ? 0.35f : want;
        sGain += (want - sGain) * (want < sGain ? 0.30f : 0.04f);
        sSpokeMs = now;
        if (!open) { /* (just opened: the 40 ms before it first, then this one) */
            mic_encode(sPrev[0], sGain, sPend[0]);
            mic_encode(sPrev[1], sGain, sPend[1]);
            mic_encode(pcm, sGain, sPend[2]);
            sPendN = 3;
            sPendAt = 0;
        }
    }
    memcpy(sPrev[0], sPrev[1], sizeof(sPrev[0]));
    memcpy(sPrev[1], pcm, sizeof(sPrev[1]));
    if (sPendN > 0) {
        memcpy(out, sPend[sPendAt++], VOICE_FRAME);
        sPendN--;
        return 1;
    }
    if (!voice && !open) {
        return 0;
    }
    mic_encode(pcm, voice ? sGain : sGain * 0.33f, out);
    return 1;
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
    if (sPendN > 0) { /* (the frames from before the gate opened, one a call) */
        memcpy(out, sPend[sPendAt++], VOICE_FRAME);
        sPendN--;
        return VOICE_FRAME;
    }
    if (SDL_GetAudioStreamAvailable(sIn) < (int)sizeof(pcm) || SDL_GetAudioStreamData(sIn, pcm, sizeof(pcm)) != (int)sizeof(pcm)) {
        return 0;
    }
    if (!PortVoice_MicOn()) {
        return 0; /* (push to talk, V not held: read and dropped) */
    }
    (void)level;
    return mic_process(pcm, out) ? VOICE_FRAME : 0; /* (quiet: nothing sent) */
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
