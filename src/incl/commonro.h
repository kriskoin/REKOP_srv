/**********************************************************************************
 CLASS CommonRules
 Date: 20180707 kriskoin :  "CommonRules" is an object defining common rule functions
  It must be used only by being inhereted by a specific rule object (see holdem.cpp)
***********************************************************************************/
#ifndef _COMMONRO_H_INCLUDED
#define _COMMONRO_H_INCLUDED

#if 1	// 2022 kriskoin
  #define INCLUDE_EXTRA_SEND_DATA_CALLS	0	// try to conserve bandwidth as much as possible
#else
  #define INCLUDE_EXTRA_SEND_DATA_CALLS	1	// hk had it this way.  don't know if it matters
#endif

#include "memory.h"
#include "pplib.h"
#include "deck.h"
#include "hand.h"
#include "poker.h"
#include "pot.h"


// game states moved to gamedata.h

// when we need to post something, it could be one of a few things
enum { POST_NEEDED_NONE, POST_NEEDED_INITIAL, POST_NEEDED_BB, POST_NEEDED_SB,
	   POST_NEEDED_BOTH, POST_NEEDED_ANTE, POST_NEEDED_UNKNOWN } ;

// extra delay to account for card animation
#define CARD_ANIMATION_DELAY	300 // milliseconds per card

// used for initializations in different places
#define INVALID_PLAYER_INDEX	(BYTE8)-1

// dealer chat queue
#define MAX_SZ_DEALER_TXT	160
struct DealerChatQueueEntry {
	int text_type;
	char message[MAX_SZ_DEALER_TXT];
};

struct BadBeatPrizes {
	WORD32 game_serial_number;
	int winners;			// # of winners
	int winner_share;		// amount for each winner
	int losers;				// # of losers
	int loser_share;		// amount for each loser
	int participants;		// # of other participants
	int participant_share;	// amount for each other participant
	int remnant_amount;		// amount left over (given to first loser)
	int total_prize;		// total prize pool to be awarded
	int player_prizes[MAX_PLAYERS_PER_GAME];		// how much each player gets
	int player_prize_status[MAX_PLAYERS_PER_GAME];	// 0=winner, 1=loser, 2=participant
};

// functions and storage common to all rule objects
class CommonRules {

public:
	// publicly accessible functions and access pointers to the structures
	GamePlayerData *PlayerData[MAX_PLAYERS_PER_GAME];	// player structures
	GamePlayerData *WatchingData;		// watch structure
	GamePlayerData *InternalData;		// only to be used my monitoring client
	GameCommonData *CommonData;			// pointer to the common data structure
	GamePlayerInputRequest *GPIRequest;	// external access pointer
	GamePlayerInputResult *GPIResult;	// external access pointer
	void AddPlayer(int seating_position, WORD32 player_id, char *name,
			char *city, int chips, BYTE8 gender, int sitting_out_flag,
			int post_needed, int *output_not_enough_money_flag,
			struct ClientStateInfo *client_state_info_ptr);	// called from Game

	WORD32 delay_before_next_eval;		// # of seconds before we should eval again.
	WORD32 next_eval_time;				// desired SecondCounter (if any) when we should eval again (calculated from delay_before_next_eval)
	int sit_out_if_timed_out;			// sit player out if they time out on current input request?
	WORD32 alternate_delay_before_next_game;	// set to non-zero if we think the next game can start sooner than normal

	int _hi_pot_winners;	// # of players participating in the high pot
	int _lo_pot_winners;	// # of players participating in the low pot (if applicable)

	struct BadBeatPrizes bad_beat_prizes;	// bad beat prize awards (if any)

protected:
	CommonRules(void);
	~CommonRules(void);
	// our internal data structures
	GamePlayerData _internal_data;	// the "master copy" from where we make our clones
	GamePlayerData _watch_data;		// internal structure of what's being watched
	GamePlayerData _player_data[MAX_PLAYERS_PER_GAME];	// internal player structures
	GamePlayerInputRequest _GPIRequest;	// our internal GPIRequest structure
	GamePlayerInputResult _GPIResult;	// our internal GPIResult structure
	struct ClientStateInfo *client_state_info_ptrs[MAX_PLAYERS_PER_GAME];	// ptrs to each player's ClientStateInfo structure.

	// pointers to objects potentially used
	Deck *_deck;							// the deck we'll be using
	Hand *_hand[MAX_PLAYERS_PER_GAME];		// hand storage for all players
	Hand *_best_hand[MAX_PLAYERS_PER_GAME];	// hand storage for player's best hand
	Hand *_low_hand[MAX_PLAYERS_PER_GAME];	// hand storage for player's best hand
	Hand *_flop;				// flop hand
	// kriskoin 
	Hand *_hi_hand_of_day;
	INT32 _game_num_of_day;
	char _name_of_day[MAX_PLAYER_USERID_LEN];
	// end kriskoin 
	Pot  *_pot;								// our betting pot
	Poker *_poker;							// our poker evaluation object
	void *_table_ptr;						// ptr to the Table object which created us.
	// internal data
	WORD16 _action_number;		// an internal number for knowing what we're responding to
	int _player_count;			// how many signed up to play?
	int _max_this_game_players;	// max allowable players for this type of game
	int _low_limit;				// structured game low limit
	int _high_limit;			// structured game high limit
	int _total_winners;			// how many winners in this game?
	int _best_hand_shown;		// player index of best hand "shown" so far at end of game
	int _low_hand_shown;		// player index of low hand "shown" so far at end of game
	int _last_hand_compared_hi;	// player index of last hand compared
	int _last_hand_compared_lo;	// player index of last hand compared
	int _max_raises_to_cap;		// how many bets per round allowed?
	char _szHandDescriptionHi[MAX_PLAYERS_PER_GAME][MAX_HAND_DESC_LEN]; // defined in gamedata.h
	char _szHandDescriptionLo[MAX_PLAYERS_PER_GAME][MAX_HAND_DESC_LEN]; // defined in gamedata.h
	CommonGameState	_game_state;// what state of the game are we at?
	ClientDisplayTabIndex client_display_tab_index;	// which tab is it displayed on for the client?
	GameRules _game_rules;		// set by specific rule object
	RakeType _rake_profile;		// which rake structure to use
	BYTE8 _next_player_to_act;	// who is the next player to do something?
	// member functions
	void CommonGameInit(WORD32 serial_number, int sb, int bb, struct GameCommonData *gcd);	// initialize
	BYTE8 GetNextPlayerInTurn(BYTE8 current_player, int active_flag);// get next player in turn
	BYTE8 GetNextPlayerInTurn(BYTE8 current_player);// get the next active player in turn
	// two version of this function -- one sets the flag to TRUE (most common usage)
	BYTE8 GetNextPlayerEligibleForCard(BYTE8 current_player);// similar to above
	BYTE8 GetNextPlayerEligibleForCard(BYTE8 current_player, int all_in_ok_flag);
	char *GenderPronoun(BYTE8 p_index);				// get 'his','her' etc for a player
	void RequestActionFromPlayer(BYTE8);			// prepare to ask for actions
	void RequestNextMoveFromPlayer(BYTE8 p_index);  // internal setup for call to RequestAction
	int  RequestOrShowHandFromPlayer(void);		// ask player if show/not show hand (figure out who too)
	void SetActionTimeOut(int time_out, int sit_out_if_timed_out_flag);	// override the default timeout for this action
	void AddToActionTimeOut(int milliseconds);		// add some extra time to the timeout call
	void SetValidAction(ActionType);	 // set a valid action into the bitmask
	void PostAction(void);				 // action mask is ready; post it
	void DealEveryoneOneCard(int button_exists); // deal around -- one card to all playing players
	void DealEveryoneOneCard(int button_exists, int can_be_all_in);	// see above
	void UpdateStructures(void);		 // make sure all structures are up to date
	void FillInternalHandStructure(void);// let it know all the cards out there
	void MaskCards(void);				 // mask all cards except his own
	void RemoveCards(int p_index);		 // remove this player's cards
	void EvaluateLiveHands(void);		 // evaluate all hands that might win something
	void UpdateDataBaseChipCounts(void); // keep logged chip counts up to date
	void LogPlayerStayedThisFar(CommonGameState cgs);	// player was in to see this

	void ShowHandForPlayer(BYTE8 p_index);			// show everyone player's hand
	void MuckHandForPlayer(BYTE8 p_index);	// muck this player's hand
	void BuildPotentialWinnersList(int pot_number);	// for this pot, who can win it?
	int AnyPlayerLeftToAskAboutShowingHand(void);	// T/F anyone left to ask?
	void DistributePot(void);	// distribute the pot among the winners
	// kriskoin 
	void SetFileLock(int fd, short type);
	time_t GetSavedHiHand(int flag, int flag_tie);
	void SaveHiHand(Hand *hand, INT32 game_serial_num, char *user_id, int flag, int flag_tie);
	void SaveAndLogHiHand(void);
	// end kriskoin 
	void AnnounceWinners(int pot_number, int first_pass);	// announce winner(s) of this pot
	void RefundChips(void);		// refund chips if game is cancelled
	void ClearChipsInFrontOfPlayers(void);	// needed for animation consistency
	void SendDataNow(void);		// force a send of table data to everyone (and update)
	void SendDataNow(int update_structures_flag);	// force a send of table data to everyone
	void CheckForBadBeat(void);	// test for (and pay) bad beats on the hand
	void LogEndGame(void);		// log end game stats to the logfile

	int SittingOut(BYTE8 p_index);		 // is this player sitting out?

	// Test if a particular player index is connected or not (returns TRUE/FALSE)
	int TestIfPlayerConnected(BYTE8 p_index);

	// Return our best guess about the quality of the connection to
	// a player given just their seat (player)index.
	// Returns CONNECTION_STATE_* (see player.h)
	int GetPlayerConnectionState(BYTE8 p_index);

	// dealer chat
	#define DEALER_QUEUE_SIZE	10	// queue up to 10 entries
	DealerChatQueueEntry DealerChatQueue[DEALER_QUEUE_SIZE];
	char _szDealerTxt[MAX_SZ_DEALER_TXT];
	void SendDealerMessage(int text_type, char *fmt, ...);	// send a dealer message to all players (calls Table)
	void AddToDealerMessageQueue(int text_type, char *msg);
	void PostDealerMessageQueue(void);
	int _dealer_queue_index;
	
	// internal flags
	int _waiting_for_user_action;
	int _initializing;		// true while we're still setting up, false during play
	int _b_end_game_handled;	// TRUE when we've dealt with the end of the game
	int _table_is_full;		// true if it's full
	int _dealt_river;			// game was dealt out fully
	int _posted_blind[MAX_PLAYERS_PER_GAME];	// posted something this game?

	enum { SHOW_HAND_UNKNOWN, SHOW_HAND_SHOWED, SHOW_HAND_MUCK, SHOW_HAND_TOSS, SHOW_HAND_ASK };
	int _show_hand[MAX_PLAYERS_PER_GAME];		// mark hand showable to everyone
	int _live_for_payoff[MAX_PLAYERS_PER_GAME];	// player marked live for pot being distributed
	int _winner_of_pot_hi[MAX_PLAYERS_PER_GAME];	// player won something this pot (T/F)
	int _winner_of_pot_lo[MAX_PLAYERS_PER_GAME];	// player won something this pot (T/F)
	int _chips_won_this_pot_hi[MAX_PLAYERS_PER_GAME];//chips won in this pot distribution
	int _chips_won_this_pot_lo[MAX_PLAYERS_PER_GAME];//chips won in this pot distribution

	int _pot_is_being_distributed;	// T/F if we're in the middle of distributing a pot
	int _game_was_cancelled;		// T/F if the game was cancelled (refunds)

	int _pot_number;
	int _pot_amount_playing_for;	// how much is currently being distributed?
	int _live_players_for_pot;		// how many had a shot at this pot in the first place?
	ChipType _chip_type;			// the kind of chips we're playing for

	int _cards_dealt;				// sometimes, we want to count how many cards we've dealt
	int _next_player_gets_delay;	// some players get extra time	
	int _split_pot_game;			// T/F this is a game with a split pot for hi/lo
	int	_show_hand_count;			// count of players this game that have showed their hand
	int _river_7cs_dealt_to_everyone;// T/F if river card has been dealt in 7cs

	int _tournament_game;			// T/F if this game is a tournament game
};
#endif // !_COMMONRO_H_INCLUDED

// This function actually resides with the table but it must be defined
// at a lower level so that the games can call it.  It simply takes a pointer
// to the relevant table and the message to be sent out to players at that table.
void Table_SendDealerMessage(void *table_ptr, char *message, int text_type);
void Table_SendDataNow(void *table_ptr);
