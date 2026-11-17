/* Voice between the two players of an online match (net_voice.c, 15 Nov 2026) and among a lobby's members (17 Nov
   2026): the microphone, 8 kHz mu-law, 20 ms to a packet (T_VOICE in net.c, T_L_VOICE in net_lobby.c), played as it
   comes, each speaker on its own stream. C and C++. */
#ifndef NET_VOICE_H
#define NET_VOICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_FRAME 160 /* samples (and bytes) of one packet: 20 ms at 8 kHz */
#define VOICE_MATCH 0xFFFFFFFFu /* the speaker "who" of a 1v1 match's other player */
enum { VOICE_OFF = 0, VOICE_ON = 1, VOICE_PTT = 2 /* push to talk: while V is held */ };

/* The overlay and the options (render thread). */
int PortVoice_MicOn(void);       /* the microphone is sending now (setting "mic": 0 off, 1 on, 2 push to talk) */
int PortVoice_Mode(void);        /* VOICE_OFF / VOICE_ON / VOICE_PTT */
void PortVoice_ModeSet(int mode);
void PortVoice_MicSet(int on);   /* (on: VOICE_ON, off: VOICE_OFF) */
int PortVoice_SpeakerOn(void);   /* setting "speaker" (on by default): the others' voices are played */
void PortVoice_SpeakerSet(int on);
int PortVoice_Shown(void);       /* 1 in an online match or a lobby (the mic badge is drawn) */
int PortVoice_TheyTalk(void);    /* 1 while the other player's voice is heard (a match) */
int PortVoice_Talking(uint32_t who); /* 1 while that one's voice is heard; who 0: this player's own microphone */

/* The connection (game thread, net.c and net_lobby.c). */
int PortVoice_Outgoing(uint8_t *out); /* a 20 ms packet of this player's voice: its length, 0 = none */
void PortVoice_Heard(const uint8_t *in, int n);                  /* (a match's other player) */
void PortVoice_HeardFrom(uint32_t who, const uint8_t *in, int n); /* (a lobby's member, by its id) */
void PortVoice_Live(void);       /* called while connected */
void PortVoice_Reset(void);      /* closes the microphone and the speakers (a session or a lobby ended) */

#ifdef __cplusplus
}
#endif
#endif
