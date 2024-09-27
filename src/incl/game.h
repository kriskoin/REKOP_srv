/**********************************************************************************
 CLASS Game
 Date: 20180707 kriskoin :  "Game" is an interface-layer object between rule modules/games and the higher-level
  player/table/etc management.  All communication to/from rule modules must go
  through this object.
***********************************************************************************/
#ifndef _GAME_H_INCLUDED
#define _GAME_H_INCLUDED

#include "pplib.h"
#include "gamedata.h"	// Shared structures between client and server
#include "hand.h"

class Game {
public:
	Game(	ClientDisplayTabIndex input_client_display_tab_index,
			GameRules input_game_rules, ChipType chip_type,
			WORD32 serial_number, int max_number_of_players,
			int small_blind, int big_blind, RakeType rake_profile,
			int previous_big_blind_seat,
			void *table_ptr, struct GameCommonData *gcd);
	~Game(void);

	// Add a player to the game.  seating_position can be used as an index
	// into the various other player related data structures for this game.
	// post_needed = POST_NEEDED_NONE, POST_NEEDED_BB, POST_NEEDED_SB, POST_NEEDED_BOTH
	ErrorType AddPlayer(int seating_position, WORD32 player_id, char *name,
				char *city, int chips, BYTE8 gender, int sitting_out_flag,
				int post_needed, int *output_not_enough_money_flag,
				struct ClientStateInfo *client_state_info_ptr);

	// Tell us who the button is and get the game going
	void StartGame(int button);

	// Tell the game to proceed to the next step
	void UpdateGame(int *work_was_done_flag);

	// pointers into useful structures
	GamePlayerData *PlayerData[MAX_PLAYERS_PER_GAME];	// array of pointers to the player structures
	GamePlayerData *WatchingData;		// pointer to the watching data structure
	GameCommonData *CommonData;			// pointer to the data structure common to all players
	GamePlayerInputRequest *GPIRequest;	// external access pointer to input request structure
	GamePlayerInputResult *GPIResult;	// external access pointer to input result structure
	int *sit_out_if_timed_out_ptr;		// ptr to: sit player out if they time out on current input request?
	WORD32 *alternate_delay_before_next_game; // ptr to:// set to non-zero if we think the next game can start sooner than normal
	// Ask the game module to evaluate using CommonData, PlayerData[seating_position],
	// and GPIRequest to fill in GPIResult.  Computer logic should not have
	// access to any other variables.
	ErrorType EvalComputerizedPlayerInput(int seating_position);

	int *pHiPotWinners;	// output: # of winners of the high pot
	int *pLoPotWinners;	// output: # of winners of the low pot (if applicable)

	struct BadBeatPrizes *bad_beat_prizes;	// ptr to who won how much of the bad beat (if any)

private:
  #if 1	// 2022 kriskoin
	GameRules game_rules;	// the rules we're playing by
  #else
	PokerGame _our_game_type;	// what type of game are we running..?
  #endif
	void *our_game;	// internal pointer to the game we're controlling
};

#endif // !_GAME_H_INCLUDED
