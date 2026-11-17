/* Kame House Arena (this modded copy, 17 Nov 2026): friends, invitations, recent players, the leaderboard and ranked
   results through the user's own matchmaking service (its /v1/f/ calls, Super-Characters/online-service/friends.js).
   net_friends.c. C and C++. */
#ifndef NET_FRIENDS_H
#define NET_FRIENDS_H
#ifdef __cplusplus
extern "C" {
#endif

#define FR_MAX 100
#define FR_NAME 20
#define FR_CODE 12

typedef struct {
    char code[FR_CODE], name[FR_NAME], state[12], lobby[8];
    int fighter;   /* the character id + 1, 0 none */
    int online;
    int ago;       /* seconds since (recent players, invitations) */
    int rating, wins, losses, place;
} PortFriend;

typedef struct {
    int known;                 /* the service has answered at least once */
    char myCode[FR_CODE];
    int rating, wins, losses, place;
    int friendCount, askCount, sentCount, inviteCount, recentCount, blockCount, boardCount;
    PortFriend friends[FR_MAX], asks[20], sent[20], invites[5], recent[20], blocked[50], board[FR_MAX];
    char note[96];             /* what the last request said ("Friend request sent") */
    unsigned noteSeq;
    int boardFriends;          /* the board shown: 0 top 100, 1 friends */
} PortFriendsView;

void PortFriends_Tick(void);                  /* every frame (render thread): presence now and then, the queue */
void PortFriends_View(PortFriendsView *out);  /* a copy of what is known */
void PortFriends_Add(const char *code);
void PortFriends_Answer(const char *code, int yes);
void PortFriends_Remove(const char *code);
void PortFriends_Block(const char *code, int block);
void PortFriends_Invite(const char *code);    /* to this player's lobby (its room code) */
void PortFriends_Board(int friendsOnly);
void PortFriends_Result(const char *match, const char *otherCode, int won); /* a ranked match's result */
void PortFriends_Refresh(void);               /* presence at once */
/* The window (ui.cpp): shown from the Online menu's Friends / Leaderboard (tab 0 friends, 1 recent, 2 requests,
   3 leaderboard). */
void PortFriends_Show(int tab);
int PortFriends_Shown(int *tab);              /* 1 if it is to be open; *tab the tab asked for, -1 none (taken) */
void PortFriends_Hide(void);
/* Joining a friend's lobby or an invitation's: the code is kept until the Online menu takes it (port_tbo.c). */
void PortFriends_JoinSet(const char *lobbyCode);
int PortFriends_JoinTake(char *out);          /* 1 and the code (7 bytes) if one waits */

#ifdef __cplusplus
}
#endif
#endif
