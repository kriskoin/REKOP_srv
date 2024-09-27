/**********************************************************************************
 Member functions for game object
 Date: 20180707 kriskoin : **********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include "game.h"
#include "holdem.h"
#include "omaha.h"
#include "stud7.h"

/**********************************************************************************
 Function Game::Game(...)
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Game::Game(	ClientDisplayTabIndex input_client_display_tab_index,
			GameRules input_game_rules, ChipType chip_type,
			WORD32 serial_number, int max_number_of_players,
			int small_blind, int big_blind, RakeType rake_profile,
			int previous_big_blind_seat,
			void *table_ptr, struct GameCommonData *gcd)
{
	// HK990426 for the moment, it's been decided that there's no fancy posting for
	// players who benefit from players who drop out of blinds, etc.  if we decide to
	// change that in the future, the idea would be, using whoever was the big blind
	// in the last hand, to figure out who owes what this hand with respect to posting.
	// perhaps there will be two players posting big blinds, or something like that
	previous_big_blind_seat = 0;	// UNUSED -- just here to remove the compiler warning
	sit_out_if_timed_out_ptr = NULL;
	alternate_delay_before_next_game = NULL;
	ZeroSomeStackSpace();	// make stack crawls more relevant
	int index;
	game_rules = input_game_rules;
	switch (game_rules) {
	case GAME_RULES_HOLDEM :
		our_game = new Holdem;
		if (!our_game) {
			Error(ERR_INTERNAL_ERROR,"Failed to create Holdem game (params %d %d %d %d)",
				serial_number, max_number_of_players, small_blind, big_blind);
		}
		((Holdem *)our_game)->InitializeGame(serial_number, max_number_of_players,
				  small_blind, big_blind, rake_profile, table_ptr, gcd, chip_type);
		((Holdem *)our_game)->SetGameRules(game_rules);
		gcd->client_display_tab_index = (BYTE8)input_client_display_tab_index;
		// set "transparent access" pointers
		for (index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
			PlayerData[index] = ((Holdem *)our_game)->PlayerData[index];
		}
		CommonData   = ((Holdem *)our_game)->CommonData;
		WatchingData = ((Holdem *)our_game)->WatchingData;
		GPIRequest   = ((Holdem *)our_game)->GPIRequest;
		GPIResult    = ((Holdem *)our_game)->GPIResult;
		sit_out_if_timed_out_ptr = &((Holdem *)our_game)->sit_out_if_timed_out;
		alternate_delay_before_next_game = &((Holdem *)our_game)->alternate_delay_before_next_game;
		pLoPotWinners = &((Holdem *)our_game)->_lo_pot_winners;
		pHiPotWinners = &((Holdem *)our_game)->_hi_pot_winners;
		bad_beat_prizes = &((Holdem *)our_game)->bad_beat_prizes;
		break;

	case GAME_RULES_OMAHA_HI :
	case GAME_RULES_OMAHA_HI_LO :
		our_game = new Omaha;
		if (!our_game) {
			Error(ERR_INTERNAL_ERROR,"Failed to create Omaha game (params %d %d %d %d)",
				serial_number, max_number_of_players, small_blind, big_blind);
		}
		((Omaha *)our_game)->InitializeGame(serial_number, max_number_of_players,
				  small_blind, big_blind, rake_profile, table_ptr, gcd, chip_type);
		((Omaha *)our_game)->SetGameRules(game_rules);
		gcd->client_display_tab_index = (BYTE8)input_client_display_tab_index;
		// set "transparent access" pointers
		for (index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
			PlayerData[index] = ((Omaha *)our_game)->PlayerData[index];
		}
		CommonData   = ((Omaha *)our_game)->CommonData;
		WatchingData = ((Omaha *)our_game)->WatchingData;
		GPIRequest   = ((Omaha *)our_game)->GPIRequest;
		GPIResult    = ((Omaha *)our_game)->GPIResult;
		sit_out_if_timed_out_ptr = &((Omaha *)our_game)->sit_out_if_timed_out;
		alternate_delay_before_next_game = &((Omaha *)our_game)->alternate_delay_before_next_game;
		pLoPotWinners = &((Omaha *)our_game)->_lo_pot_winners;
		pHiPotWinners = &((Omaha *)our_game)->_hi_pot_winners;
		bad_beat_prizes = &((Omaha *)our_game)->bad_beat_prizes;
		break;

	case GAME_RULES_STUD7 :
	case GAME_RULES_STUD7_HI_LO :
		our_game = new Stud7;
		if (!our_game) {
			Error(ERR_INTERNAL_ERROR,"Failed to create Stud7 game (params %d %d %d %d)",
				serial_number, max_number_of_players, small_blind, big_blind);
		}
		((Stud7 *)our_game)->InitializeGame(serial_number, max_number_of_players,
				  small_blind, big_blind, rake_profile, table_ptr, gcd, chip_type);
		((Stud7 *)our_game)->SetGameRules(game_rules);
		gcd->client_display_tab_index = (BYTE8)input_client_display_tab_index;
		// set "transparent access" pointers
		for (index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
			PlayerData[index] = ((Stud7 *)our_game)->PlayerData[index];
		}
		CommonData   = ((Stud7 *)our_game)->CommonData;
		WatchingData = ((Stud7 *)our_game)->WatchingData;
		GPIRequest   = ((Stud7 *)our_game)->GPIRequest;
		GPIResult    = ((Stud7 *)our_game)->GPIResult;
		sit_out_if_timed_out_ptr = &((Stud7 *)our_game)->sit_out_if_timed_out;
		alternate_delay_before_next_game = &((Stud7 *)our_game)->alternate_delay_before_next_game;
		pLoPotWinners = &((Stud7 *)our_game)->_lo_pot_winners;
		pHiPotWinners = &((Stud7 *)our_game)->_hi_pot_winners;
		bad_beat_prizes = &((Stud7 *)our_game)->bad_beat_prizes;
		break;

	default :
		Error(ERR_INTERNAL_ERROR,"%s(%d) unsupported game rules (%d)", _FL, game_rules);
		DIE("unsupported game type in trying to create below Game object");
		break;
	}
}

/**********************************************************************************
 Function Game::~Game()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Game::~Game(void)
{
	if (our_game) {
		switch (game_rules) {
		case GAME_RULES_HOLDEM:
			delete (Holdem *)our_game;	// deallocate any game object
			break;

		case GAME_RULES_OMAHA_HI:
		case GAME_RULES_OMAHA_HI_LO:
			delete (Omaha *)our_game;	// deallocate any game object
			break;

		case GAME_RULES_STUD7:
		case GAME_RULES_STUD7_HI_LO:
			delete (Stud7 *)our_game;	// deallocate any game object
			break;

		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) unsupported game rules (%d)", _FL, game_rules);
			DIE("unsupported game type in ~Game()\n");
			break;
		}
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Add a player to the game.  seating_position can be used as an index
// into the various other player related data structures for this game.
// post_needed = POST_NEEDED_NONE, POST_NEEDED_BB, POST_NEEDED_SB, POST_NEEDED_BOTH
//
ErrorType Game::AddPlayer(int seating_position, WORD32 player_id, char *name, char *city,
			int chips, BYTE8 gender, int sitting_out_flag, int post_needed,
			int *output_not_enough_money_flag,
			struct ClientStateInfo *client_state_info_ptr)
{
  #if 0	// 2022 kriskoin
	if (client_state_info_ptr) {
		kp(("%s(%d) Just added seat %d, sitting_out = %d, csi->sitting_out(post/fold) = %d\n",
				_FL, seating_position, sitting_out_flag, client_state_info_ptr ? client_state_info_ptr->sitting_out_flag : 0));
	}
  #endif

	switch (game_rules) {
	case GAME_RULES_HOLDEM:
		((Holdem *)our_game)->AddPlayer(seating_position, player_id, name, city,
			chips, gender, sitting_out_flag, post_needed, output_not_enough_money_flag,
			client_state_info_ptr);
		break;

	case GAME_RULES_OMAHA_HI:
	case GAME_RULES_OMAHA_HI_LO:
		((Omaha *)our_game)->AddPlayer(seating_position, player_id, name, city,
			chips, gender, sitting_out_flag, post_needed, output_not_enough_money_flag,
			client_state_info_ptr);
		break;

	case GAME_RULES_STUD7:
	case GAME_RULES_STUD7_HI_LO:
		((Stud7 *)our_game)->AddPlayer(seating_position, player_id, name, city,
			chips, gender, sitting_out_flag, post_needed, output_not_enough_money_flag,
			client_state_info_ptr);
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) unsupported game rules (%d)", _FL, game_rules);
		DIE("unsupported game type in AddPlayer");
		break;
	}
	return ERR_NONE;
}

/**********************************************************************************
 Function Game::StartGame(void)
 date: kriskoin 2019/01/01 Purpose: set the button and start the game
***********************************************************************************/
void Game::StartGame(int player_with_button)
{
	switch (game_rules) {
	case GAME_RULES_HOLDEM:
		((Holdem *)our_game)->StartGame((BYTE8)player_with_button);
		break;

	case GAME_RULES_OMAHA_HI:
	case GAME_RULES_OMAHA_HI_LO:
		((Omaha *)our_game)->StartGame((BYTE8)player_with_button);
		break;

	case GAME_RULES_STUD7:
	case GAME_RULES_STUD7_HI_LO:
		((Stud7 *)our_game)->StartGame();	// no button in 7-card stud
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) unsupported game rules (%d)", _FL, game_rules);
		DIE("unsupported game type in StartGame");
		break;
	}
}

/**********************************************************************************
 Function ::UpdateGame(int *work_was_done_flag)
 Date: 20180707 kriskoin :  Purpose: tells the game to proceed to the next step
***********************************************************************************/
void Game::UpdateGame(int *work_was_done_flag)
{
	switch (game_rules) {
	case GAME_RULES_HOLDEM:
		((Holdem *)our_game)->PlayOn(work_was_done_flag);
		break;

	case GAME_RULES_OMAHA_HI:
	case GAME_RULES_OMAHA_HI_LO:
		((Omaha *)our_game)->PlayOn(work_was_done_flag);
		break;

	case GAME_RULES_STUD7:
	case GAME_RULES_STUD7_HI_LO:
		((Stud7 *)our_game)->PlayOn(work_was_done_flag);
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) unsupported game rules (%d)", _FL, game_rules);
		DIE("unsupported game type in UpdateGame");
		break;
	}
}
