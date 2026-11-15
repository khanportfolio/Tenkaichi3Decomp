/* Voice between the two players of an online match (net_voice.c, 15 Nov 2026): the microphone, 8 kHz mu-law, 20 ms
   to a packet (T_VOICE in net.c), played as it comes. C and C++. */
#ifndef NET_VOICE_H
#define NET_VOICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_FRAME 160 /* samples (and bytes) of one packet: 20 ms at 8 kHz */

/* The overlay and the options (render thread). */
int PortVoice_MicOn(void);       /* setting "mic" (off by default): this player's voice goes to the other */
void PortVoice_MicSet(int on);
int PortVoice_Shown(void);       /* 1 in an online match (the mic badge is drawn) */
int PortVoice_TheyTalk(void);    /* 1 while the other player's voice is heard */

/* The connection (game thread, net.c). */
int PortVoice_Outgoing(uint8_t *out); /* a 20 ms packet of this player's voice: its length, 0 = none */
void PortVoice_Heard(const uint8_t *in, int n);
void PortVoice_Live(void);       /* called while connected */
void PortVoice_Reset(void);      /* closes the microphone and the speaker (a session ended) */

#ifdef __cplusplus
}
#endif
#endif
