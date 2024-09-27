//*********************************************************
//	Table routines.
//	Server side only.
//
// 
//
//*********************************************************

#define DISP 0

#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>		// Needed for CritSec stuff
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include "commonro.h"
#include "pokersrv.h"
#include "cardroom.h"
#include "roboplay.h"
#include "sdb.h"
#include "logging.h"

// GENDER_UNDEFINED, GENDER_UNKNOWN, GENDER_MALE, GENDER_FEMALE
static char szGender1[4][6]		= {"its", "its", "his", "her" };
static char szGender2[4][6]		= {"it",  "it",  "he",  "she" };
static char szGender3[4][6]		= {"it",  "it",  "him", "her" };
static char szGenderCaps1[4][6] = {"Its", "Its", "His", "Her" };
static char szGenderCaps2[4][6] = {"It",  "It",  "He",  "She" };
static char possessive_suffix[11][3] = { "??", "st", "nd", "rd", "th", "th", "th", "th", "th", "th", "th" };

// payouts for tournaments
#if TESTING_TOURNAMENTS
  int PayoutPercentages[12] = { 0, 40, 25, 15, 8, 6, 3, 2, 1, 0, 0, 0 };
#else
  int PayoutPercentages[12] = { 0, 50, 30, 20, 0, 0, 0, 0, 0, 0, 0, 0 };
#endif

// Bad beat payout timing:
#define BADBEAT_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE	7	// wait n secs before showing the pot
#define BADBEAT_PAYOUT_DELAY_TO_SHOW_POT			10	// Show the pot for n secs
#define BADBEAT_PAYOUT_DELAY_TO_AWARD_PRIZES		20	// award prizes and wait n secs
// Special Hand payout timing (clone of above)
#define SPECIALHAND_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE	7	// wait n secs before showing the pot
#define SPECIALHAND_PAYOUT_DELAY_TO_SHOW_POT			12	// Show the pot for n secs
#define SPECIALHAND_PAYOUT_DELAY_TO_AWARD_PRIZES		25	// award prizes and wait n secs

//****************************************************************
// 
//
// Constructor/destructor for the Table object
//
Table::Table(void *input_cardroom_ptr, WORD32 input_table_serial_number, char *table_name)
{
	client_display_tab_index = DISPLAY_TAB_HOLDEM;
	game_rules = GAME_RULES_HOLDEM;
	table_tourn_state = TTS_WAITING;
	cardroom = input_cardroom_ptr;
	table_serial_number = input_table_serial_number;
	char str[100];
	sprintf(str, "Table #%d", table_serial_number);
	PPInitializeCriticalSection(&TableCritSec, CRITSECPRI_TABLE, str);
	EnterCriticalSection(&TableCritSec);
	game = NULL;
	last_input_request_serial_number = 0;
	last_input_request_game_serial_number = 0;
	last_input_request_ms = 0;
	zstruct(last_input_request_serial_numbers);
  #if 1	// 2022 kriskoin
	wait_list_called_player_count = 0;
	zstruct(wait_list_called_players);
  #else
	wait_list_player_id = 0;	// player_id we're waiting for a response from (only one at a time)
	wait_list_request_time = 0;	// SecondCounter when we first offered the seat to the user.
	wait_list_next_send_time = 0;
  #endif
	wait_list_potential_players = 0;

	resend_input_request_flag = 0;
	input_request_sent_time = 0;		// SecondCounter when input request last sent to player
	last_input_player_ping_time = 0;	// SecondCounter when we last sent a ping to the input player
	time_of_next_update = 0;			// cardroom related optimization timing
	time_of_next_action = 0;			// SecondCounter of next time table is allowed to do something (used to slow things down, such as promotional hands)
	time_of_last_player_database_scan = 0;// SecondCounter when we last scanned for locked out players

	worst_connection_state_for_input_player = 0;
	button_seat = 0;
	previous_big_blind_seat = -1;		// don't care who the BB was last hand
	rake_per_hour = 0;
	avg_response_time = 0.0;
	big_blind_amount = 0;
	small_blind_amount = 0;
	initial_big_blind_amount = 0;
	initial_small_blind_amount = 0;
  #if 1	// 2022 kriskoin
	if (iRunningLiveFlag) {
		// Give people time to get to the tables (when server starts)
		next_game_start_time = SecondCounter + TimeBetweenGames + 80;	// specify earliest time a new game can start.
	} else {
		next_game_start_time = SecondCounter + TimeBetweenGames + 14;	// specify earliest time a new game can start.
	}
  #else
	next_game_start_time = SecondCounter + TimeBetweenGames + 10;	// specify earliest time a new game can start.
  #endif
	game_start_time = SecondCounter;
	last_game_end_time = SecondCounter;	// keep track of when our games end.
	next_watching_scan = SecondCounter;
	game_type_was_disabled = FALSE;
	last_start_time_announce = 0;
	last_shotclock_announce_minutes = 0;
	new_players_must_post = FALSE;
	showed_not_enough_players_msg = FALSE;
	time_of_computerized_eval = 0;
	table_is_active = prev_table_is_active = TABLE_ACTIVE_FALSE;
	shut_down_announced = 0;
	chip_type = CT_NONE;
	rake_profile = RT_NONE;

	zstruct(treenode);
	treenode.object_ptr = (void *)this;
	treenode.sort_key = table_serial_number;

	zstruct(summary_info);
	summary_info.table_serial_number = table_serial_number;
	strnncpy(summary_info.table_name, table_name, MAX_TABLE_NAME_LEN);
	summary_info_changed = TRUE;
	summary_index = -1;	// initialize to something invalid (cardroom will fix)
	last_tableinfo_connection_update = 0;	// SecondCounter when connection flags were last updated in the summary_info structure

	zstruct(table_info);
	table_info.table_serial_number = table_serial_number;
	table_info_changed = TRUE;
	move_button_next_game = TRUE;

	// Make all seats vacant.
	memset(players, 0, sizeof(struct PlayerTableInfo) * MAX_PLAYERS_PER_GAME);
	memset(watching_players, 0, sizeof(struct PlayerTableInfo) * MAX_WATCHING_PLAYERS_PER_TABLE);
	memset(ttsi, 0, sizeof(struct TTournamentSummaryInfo) * MAX_PLAYERS_PER_GAME);
	memset(had_best_hand_last_game, 0, sizeof(int) * MAX_PLAYERS_PER_GAME);
	zstruct(table_gpd);

	max_number_of_players = 0;
	watching_player_count = 0;
	connected_watching_player_count = 0;
	zstruct(GameCommonData);
	GameCommonData.table_serial_number = table_serial_number;
	disconnected_flags = 0;
	
	prev_dealer_prompt_watching_count = 0;
	last_dealer_prompt_time = 0;
	
	bad_beat_game_number = 0;
	bad_beat_payout = 0;
	bad_beat_payout_stage = 0;			// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing
	next_bad_beat_payout_stage = 0;		// SecondCounter when we should switch to next stage.
	zstruct(bad_beat_prizes);
	special_hand_payout_stage = 0;		// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing
	next_special_hand_payout_stage = 0;	// SecondCounter when we should switch to next stage.

	time_of_next_tournament_stage = 0;
	t_last_level_msg_displayed = 0;
	tournament_button_override = -1;	// invalid until used
	tournament_table = FALSE;			// initialized in SetGameType
	tournament_prize_pool = 0;			// the prize pool to be paid out
	tournament_current_level = 0;		// current tournament level we're on
	tournament_current_game = 0;		// current tournament game we're on
	tournament_highest_payout = 0;		// keep track of the highest amount we've paid out in a tournament so far
	tournament_total_tourney_chips = 0;	// local tournament chip universe
	time_no_tourn_starting_msg = 0;		// SecondTimer of last time we announced this msg
	tournament_start_time = 0;			// WORD32 of tournament start time
	tournament_announced_start = FALSE;	// T/F if we've announced the start
	tournament_summary_prizes_paid = 0;	// how much have we paid out
	tournament_players_eliminated = 0;	// how many have been eliminated?
	tournament_hand_number = 0;			// current tournament hand number
	tournament_buyin = 0;				// buyin amount for the tournament

	high_card = MAKECARD(Two,Clubs);	// someone will do better than that
	zstruct(t_summary_filename);		// filename of temporary summary file

	LeaveCriticalSection(&TableCritSec);
}

Table::~Table(void)
{
	EnterCriticalSection(&TableCritSec);

	// If a game is still hanging around, destroy it.
	if (game) {
		delete game;
		game = NULL;
	}

	// Loop through all players and make them unjoin the game.
	int i;
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status) {
			RemovePlayerFromTable(i);
		}
	}
	while (watching_player_count) {
		RemoveWatchingPlayer(0);
	}

	LeaveCriticalSection(&TableCritSec);
	PPDeleteCriticalSection(&TableCritSec);
	zstruct(TableCritSec);
}

//****************************************************************
// https://github.com/kriskoin//
// Start a new regular game if necessary (not used for tournaments)
// 24/01/01 kriskoin:
// to prevent inlining optimizations that made stack traces harder.
//
ErrorType Table::StartNewGameIfNecessary(int *work_was_done_flag)
{
	char msg[80];
	zstruct(msg);
	int i;

	// HK990604 their sitting_out state may have been toggled from the client.
	// We should make sure it's current -- we "suddenly" may have enough players
	// to start a game
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].player && players[i].status) {
			if (players[i].player->CurrentConnectionState() >= CONNECTION_STATE_BAD) {
				// disconnected or bad... force to sitting out.
				players[i].sitting_out_flag = TRUE;
				pr(("%s(%d) Disconnected player %d is sitting out.\n",_FL,i));
			} else {
				// Currently connected...
				// Grab their sitting_out_flag from their ClientStateInfo structure.
				players[i].sitting_out_flag = TRUE;	// default to true
				int table_index = players[i].player->SerialNumToJoinedTableIndex(table_serial_number);
				if (table_index >= 0) {
					players[i].sitting_out_flag = players[i].player->JoinedTables[table_index].client_state_info.sitting_out_flag;
					//kp(("%s(%d) Connected player %d is %ssitting out.\n",_FL,i,players[i].sitting_out_flag ? "" : "NOT "));
				}
			}
		}
	  #if 0	// 2022 kriskoin
		// adate: if he timed out last hand but now he's not sitting out anymore, let him play
		if (!players[i].sitting_out_flag) {
			players[i].timed_out_this_hand = FALSE;
		}
	  #else
		// it's the start of a new game.  there's no way he could have timed out yet.
		players[i].timed_out_this_hand = FALSE;
	  #endif
	}
	// Count number of valid players ready to play
	int valid_players = 0;
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status && !players[i].sitting_out_flag) {	// player here?
			if (players[i].chips >= big_blind_amount*2) {	// got enough money?
				// If they're human, make sure they're connected
				// and ready to play
				if (players[i].status==2) {	// human?
					// A good or poor connection is good enough.  Bad or lost is not.
					if (players[i].player->CurrentConnectionState() <= CONNECTION_STATE_POOR) {
						valid_players++;
					}
				} else {	// computer player... never disconnected.
					valid_players++;
				}
			}
		}
	}

	// If this table is not active and now it's full, we can start right away.
	if (table_is_active==TABLE_ACTIVE_WAITING && valid_players >= max_number_of_players) {
		next_game_start_time = SecondCounter;
	}

	// HK990622 if the table is waiting, every so often, tell everyone how long it'll be 
	// before we fire up the game
	#define GAME_STARTING_ANNOUNCE_INTERVAL	15
	if (table_is_active == TABLE_ACTIVE_WAITING &&
			!iShutdownAfterGamesCompletedFlag &&
			!iShutdownPlayMoneyGamesFlag &&
			!(game_disable_bits & GameDisableBits))
	{
		if ( (next_game_start_time > SecondCounter) && 
				( (SecondCounter - last_start_time_announce) >= GAME_STARTING_ANNOUNCE_INTERVAL) ) {
			last_start_time_announce = SecondCounter;
			int seconds_to_start = next_game_start_time - SecondCounter;
			// don't bother if it's really soon
			if (seconds_to_start > 5) {
				// round down to the nearest 5
				seconds_to_start -= (seconds_to_start % 5);
				sprintf(msg, "Game will start in %d seconds", seconds_to_start);
				SendDealerMessage(msg, CHATTEXT_DEALER_NORMAL);
			}
		}
	}
	// do all the rest of the common game launch
	return SNG_LaunchIfNeccessary(work_was_done_flag, valid_players);
}

/**********************************************************************************
 Function Table::SNG_LaunchIfNeccessary(int *work_was_done_flag)
 date: 24/01/01 kriskoin Purpose: StartNewGame common to all new game starting functions...
 NOTE: this was adapted from StartNewGameIfNecessary()
***********************************************************************************/
// 24/01/01 kriskoin:
// to prevent inlining optimizations that made stack traces harder.
ErrorType Table::SNG_LaunchIfNeccessary(int *work_was_done_flag, int valid_players)
{
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	char str[120];
	zstruct(str);
	BYTE8 saved_bar_snacks[MAX_PLAYERS_PER_GAME];
	zstruct(saved_bar_snacks);
	struct GameChatMessage gcm;
	zstruct(gcm);

	// If a game is not defined, possibly create one
	if (!game && SecondCounter >= next_game_start_time) {
		int will_start = TRUE;
		if (iShutdownAfterGamesCompletedFlag ||
			(iShutdownPlayMoneyGamesFlag && chip_type == CT_PLAY)) {
			will_start = FALSE;
		}
		// 24/01/01 kriskoin:
		if (bad_beat_payout_stage || special_hand_payout_stage) {
			kp(("%s(%d) game not starting at table %d because a special payoff is taking place\n",
				_FL, table_serial_number));
			return ERR_NONE;
		}

		if (!will_start) {
			if (tournament_table) {
				SendDealerMessage("No new tournaments are starting; the server is being restarted soon.", CHATTEXT_ADMIN);
			} else {
				next_game_start_time = SecondCounter + 15;
				if (iShutdownIsBriefFlag) {
					SendDealerMessage("No new games are starting; the server is being restarted soon.", CHATTEXT_ADMIN);
					int tables_left = ((CardRoom *)cardroom)->active_tables;
					char str[100];
					zstruct(str);
					sprintf(str, "Please stay connected, we're just waiting for %d table%s to finish up.",
							tables_left, tables_left==1 ? "" : "s");
					SendDealerMessage(str, CHATTEXT_ADMIN);
				} else {
					SendDealerMessage("No new games are starting; the server is shutting down soon.", CHATTEXT_ADMIN);
				}
			}
			return ERR_NONE;	// nothing left to do this time around
		}

		if (chip_type == CT_REAL && !RealMoneyTablesOpen) {
			// Real money tables are not yet open... don't start yet
			return ERR_NONE;
		}
		
		next_game_start_time = SecondCounter + 5;	// worst case we try again in 5s.
		if (game_type_was_disabled) {
			next_game_start_time = SecondCounter + 1;	// try sooner if just opening up
		}
		// Make sure we have at least 2 players with enough money to play.
		if (valid_players >= 2) {	// If we've got enough players, start a game.
			// check if the table just now went active -- because, if it did, and there's
			// a waiting list, we're going to post a message and then wait a bit longer
			// for other players to come in from the waiting list
			// if the table is full, no need to wait...

			// tournaments don't need this because we never need to wait for players from a waiting list
			if (!tournament_table) {
				if ( (valid_players < max_number_of_players) &&
						!table_is_active &&
						(summary_info.flags & TSIF_WAIT_LIST_REQUIRED) &&
						!game_type_was_disabled )
				{
					table_is_active = TABLE_ACTIVE_WAITING;	// effectively active now, waiting for players to join
					next_game_start_time += 90;	// add 90 seconds to the next game start time
					return ERR_NONE;			// nothing left to do this time around
				}
			}

			//kriskoin: 			// PromotionalGameNumberMultiple are real money and have 4 or more
			// players.  If not, delay game start for a few more seconds.
			if (PromotionalGameNumberMultiple && !(((CardRoom *)cardroom)->GetNextGameSerialNumber() % PromotionalGameNumberMultiple)) {
				if (valid_players < PromotionalGameNumberMinPlayers || chip_type != CT_REAL) {
					kp(("%s Table %s: Game #%d is either not real or too few players (%d).\n",
							TimeStr(), summary_info.table_name,
							((CardRoom *)cardroom)->GetNextGameSerialNumber(), valid_players));
					next_game_start_time = SecondCounter + 2;	// try again in a second or two
					return ERR_NONE;			// nothing left to do this time around
				}

				char *game_names[MAX_GAME_RULES] = {
					"Hold'em",
					"Omaha Hi",
					"Omaha Hi/Lo 8",
					"7 Card Stud",
					"7 Card Stud Hi/Lo 8",
				};

				if (game_names[game_rules - GAME_RULES_START]) {
					SendAdminAlert(ALERT_3, "Game #%s is starting at table %s (%s/%s %s)",
							IntegerWithCommas(curr_str3, ((CardRoom *)cardroom)->GetNextGameSerialNumber()),
							summary_info.table_name,
							CurrencyString(curr_str1, big_blind_amount * GameStakesMultipliers[game_rules - GAME_RULES_START], chip_type),
							CurrencyString(curr_str2, big_blind_amount*2*GameStakesMultipliers[game_rules - GAME_RULES_START], chip_type),
							game_names[game_rules - GAME_RULES_START]);
				}
			}

			// irrelevant to tournament tables
			if (!tournament_table) {
				if (game_type_was_disabled) {
					// We've just opened a this game type.
					SendDealerMessage("Let the games begin!", CHATTEXT_ADMIN);
				} else if (table_is_active == TABLE_ACTIVE_WAITING && valid_players == max_number_of_players) {
					// 990625HK We were waiting, but the table filled up... let's just start now
					// message should be different on a 1-on-1 table
					if (max_number_of_players == 2) {
						sprintf(str, "Let's go");
					} else {
						sprintf(str, "This table is now full. Let us begin!");
					}
					SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
				} else if (!table_is_active &&
							SecondCounter > 30 &&		// .. but not upon startup
							!game_type_was_disabled )	// or if game type just opened
				{
					// adate: if the table just went active, wait at least 30 seconds anyway
					table_is_active = TABLE_ACTIVE_WAITING;	// effectively active now, waiting for players to join
					#define NEW_GAME_STARTUP_DELAY 15
					next_game_start_time += NEW_GAME_STARTUP_DELAY;	// add 30 seconds to the next game start time
					return ERR_NONE;			// nothing left to do this time around
				}
			}

			//MemTrackVMUsage(FALSE, "%s(%d) Table::StartNewGameIfNecessary() (top)",_FL);
			// Start the game
			// Move the button up to the next seat with a player in it.
			// We must skip anyone who must post anything.
			int original_button = button_seat;	// keep track of where we started.
			int suitable_button = FALSE;		// is this a suitable button position?
			int loop_count = 0;	// avoid infinite loop
			// 24/01/01 kriskoin:
			// stages of a tournament where it can be very unfair for someone to get the BB twice.  Specific cases:
			// Z-bb  Y-sb  X-(b)  (action moves right to left)
			// X goes all-in, we should move the button to Z
			// Y goes all-in, we should move the button to Z
			// Z goes all-in, do nothing, button will move properly
			// !! THIS CODE IS NOT YET DONE !!
			if (tournament_button_override >= 0) {  // button has been given to us
			   button_seat = tournament_button_override;
			   tournament_button_override = -1;        // clear it, won't be us
			} else {
				do {
					loop_count++;
					if (loop_count > 3 * MAX_PLAYERS_PER_GAME) {
						// avoiding infinite loop -- NOBODY is left to get the button??
						// Error(ERR_ERROR, "%s(%d) No players left to get button -- exiting game startup", _FL);
						return ERR_ERROR;
					}
					// if we're told not to move the button for this game, and the current button
					// is still valid (player hasn't left the table), use it
					if (!move_button_next_game && players[button_seat].status &&
								!players[button_seat].sitting_out_flag && players[button_seat].chips) {
						// button is set -- nothing more to do
						break;
					}
					button_seat = (button_seat + 1) % MAX_PLAYERS_PER_GAME;
					if (button_seat==original_button)  {
						// adate:  a new way to look at this situation -- let's forget that
						// anyone needs to post.  If everyone but 2 or less players owes a post,
						// it's a pretty short player list -- and if we're at a heads-up table,
						// one person with a lingering "post needed" will hang the game indefinitely,
						// let's clear all the posts needed in this situation.  this is to no
						// advantage to anyone, but it gets the game going again
						SetNobodyNeedsToPost();
						button_seat = (original_button + 1) % MAX_PLAYERS_PER_GAME;
					}
					suitable_button = (players[button_seat].status && 
							!players[button_seat].sitting_out_flag &&
							players[button_seat].chips &&	// added 19:::							players[button_seat].post_needed == POST_NEEDED_NONE);
				
				} while (!suitable_button);
			}
			pr(("%s(%d) Button has moved to seat %d\n",_FL,button_seat));

		  #ifdef POT_DEBUG
			kp(("-----------------------------------------\n"));
		  #endif
			// No game... time for a new one.
			//MemTrackVMUsage(FALSE, "%s(%d) Table::StartNewGameIfNecessary() (before new Game)",_FL);
			// adate: added real_money flag
			// 24/01/01 kriskoin:
			BYTE8 saved_gcd_flags = GameCommonData.flags;	// preserve across new Game() call.
			memcpy(saved_bar_snacks, GameCommonData.bar_snack, MAX_PLAYERS_PER_GAME);
			ZeroSomeStackSpace();	// make stack crawls more relevant
			game = new Game(client_display_tab_index, game_rules, chip_type,
						((CardRoom *)cardroom)->NextGameSerialNumberAndIncrement(),
						max_number_of_players, small_blind_amount, big_blind_amount, rake_profile,
						previous_big_blind_seat, (void *)this, &GameCommonData);

			ZeroSomeStackSpace();	// make stack crawls more relevant

			GameCommonData.flags = saved_gcd_flags;	// preserve across new Game() call.
			memcpy(GameCommonData.bar_snack, saved_bar_snacks, MAX_PLAYERS_PER_GAME);
			//MemTrackVMUsage(FALSE, "%s(%d) Table::StartNewGameIfNecessary() (after new Game)",_FL);
			if (!game) {
				Error(ERR_ERROR, "%s(%d) Error creating a new Game object.", _FL);
				return ERR_ERROR;
			}
			//kp(("%s(%d) ------------------- starting game %d ------------------------\n",_FL,GameCommonData.game_serial_number));

			// log game creation
			// 24/01/01 kriskoin:
			int game_rule_to_log = (int)game_rules;
			if (GameCommonData.flags & GCDF_ONE_ON_ONE) {
				game_rule_to_log += OLD_HEADSUP_RULE_LOGGING_OFFSET;
			}
			// for tournaments, announce hand details
			ZeroSomeStackSpace();	// make stack crawls more relevant
			if (tournament_table && tournament_hand_number) {
				// weird math due to the last level being 8, and hands counting up forever from there
				//#define T_MAX_LEVELS		8
				#define T_MAX_LEVELS		4
				#define T_GAMES_PER_LEVEL	5
				tournament_current_level = min(T_MAX_LEVELS,((tournament_hand_number-1)/T_GAMES_PER_LEVEL)+1);
				tournament_current_game = (tournament_hand_number - ((tournament_current_level-1) * T_GAMES_PER_LEVEL));
				// if we've looped, we want to see it only the first time
				if (tournament_hand_number != t_last_level_msg_displayed) {
					// we use suffixes to up to 8th
					t_last_level_msg_displayed = tournament_hand_number;
					sprintf(str, "Level %s (%d/%d) #%d",
						szRomanNumerals[tournament_current_level],
						// back to dollars from pennies
						big_blind_amount/100, 2*big_blind_amount/100,
						tournament_current_game);
					SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
					if (tournament_current_game == T_GAMES_PER_LEVEL && tournament_current_level < T_MAX_LEVELS) {
						sprintf(str, "Next game is at Level %s and the blinds increase",
							szRomanNumerals[tournament_current_level+1]);
						SendDealerMessage(str, CHATTEXT_DEALER_WINNER);
					}
				}
			}
			ZeroSomeStackSpace();	// make stack crawls more relevant
			int big_blind_to_log = big_blind_amount;
			int small_blind_to_log = small_blind_amount;
			if (chip_type == CT_TOURNAMENT) {
				// we're going to hide some information inside the small and big blinds
				// like tournament levels and game numbers
				#define SCALING_FACTOR	1000000	// see parallel code in logging.cpp
				big_blind_to_log += SCALING_FACTOR * tournament_current_level;
				small_blind_to_log += SCALING_FACTOR * tournament_current_game;
			}			
			PL->LogGameStart(GameCommonData.game_serial_number, table_serial_number, summary_info.table_name,
				game_rule_to_log, chip_type, big_blind_to_log, small_blind_to_log, button_seat, tournament_buyin);
			
			last_input_request_serial_number = 0;
			last_input_request_game_serial_number = 0;
			last_input_request_time = SecondCounter;	// always re-init at start of game
			game_start_time = SecondCounter;			// keep track of when this game started.
			game_type_was_disabled = FALSE;
			showed_not_enough_players_msg = FALSE;
			// Add all currently seated players to the game
			int i;
			int lgpe_count = 0;
			Log_GamePlayerEntry lgpe[MAX_PLAYERS_PER_GAME];
			zstruct(lgpe);
			for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
				players[i].force_to_sit_out_flag = FALSE;	// reset at the start of each game
				players[i].sent_timeout_msg = FALSE;		// set if the client has been sent a pop-up message talking about going all-in,folding,etc.
				players[i].sent_sitout_msg = FALSE;			// set if the client has been told to enter sit out mode after timing out
				players[i].timed_out_this_hand = FALSE;
				if (players[i].status) {
					int not_enough_money_flag;
					if (players[i].status == 1) {	// computer player
						// Just get their name and stuff out of this
						// table's table_info structure.
						game->AddPlayer(i, table_info.players[i].player_id,
								table_info.players[i].name,
								table_info.players[i].city,
								players[i].chips, players[i].gender,
								players[i].sitting_out_flag,
								players[i].post_needed, &not_enough_money_flag,
								NULL);
					} else {	// Human player
						ZeroSomeStackSpace();	// make stack crawls more relevant
						players[i].player->SendPing();
						// add the human player
						game->AddPlayer(i, players[i].player->player_id,
								players[i].player->user_id,
								players[i].player->City,
								players[i].chips,
								players[i].gender,
								players[i].sitting_out_flag,
								players[i].post_needed,
								&not_enough_money_flag,
								players[i].player->GetClientStateInfoPtr(table_serial_number));
						// log as one of the last 100 hands played for this player
						SDB->AddGameToPlayerHistory(table_info.players[i].player_id, GameCommonData.game_serial_number);
						// Tell them they didn't have enough money
						if (not_enough_money_flag) {
							if (!players[i].sent_out_of_money_msg) {
								OfferPlayerAbilityToBringMoreChips(i);
							}
						} else {
							players[i].sent_out_of_money_msg = FALSE;	// reset
						}
					}

					// log player for game
					lgpe[lgpe_count].player_id = table_info.players[i].player_id;
					strnncpy(lgpe[lgpe_count].user_id, table_info.players[i].name, MAX_PLAYER_USERID_LEN);
					lgpe[lgpe_count].seating_position = i;
					lgpe[lgpe_count].chips = players[i].chips;
					lgpe[lgpe_count].sitting_out_flag = players[i].sitting_out_flag;
					lgpe_count++;

					// send a sitting out message if this just changed
					if (players[i].old_sitting_out_flag != players[i].sitting_out_flag) {
						players[i].old_sitting_out_flag = players[i].sitting_out_flag;
						zstruct(str);
						if (players[i].sitting_out_flag) {
							sprintf(str,"%s is sitting out", players[i].player->user_id);
						} else {
							sprintf(str,"%s has returned", players[i].player->user_id);
						}
						// Now send it out to everyone...
						SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
					}
					players[i].worst_connection_state = 0;	// reset at start of each game
					players[i].timed_out_this_hand = FALSE;	// no timeout for this hand yet
				}
			}

			PL->LogGamePlayers(GameCommonData.game_serial_number, lgpe_count, lgpe);

			ZeroSomeStackSpace();	// make stack crawls more relevant
			UpdatePlayerCount();
			FlagResendGameCommonDataToEveryone();
			// Get the game started.
			//MemTrackVMUsage(FALSE, "%s(%d) Table::StartNewGameIfNecessary() (before StartGame())",_FL);
			bad_beat_payout_stage = 0;			// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing
			next_bad_beat_payout_stage = 0;		// SecondCounter when we should switch to next stage.
			special_hand_payout_stage = 0;		// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing
			next_special_hand_payout_stage = 0;	// SecondCounter when we should switch to next stage.
			table_is_active = TABLE_ACTIVE_TRUE;
			ZeroSomeStackSpace();	// make stack crawls more relevant
			game->StartGame(button_seat);
			ZeroSomeStackSpace();	// make stack crawls more relevant
	
			// for tournament tables, we may need to announce that it's starting...
			if (tournament_table && !tournament_announced_start) {
				tournament_announced_start = TRUE;
				tournament_start_time = SecondCounter;
				Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "\n");
				Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "Tournament started %s (CST) --  Game #%s\n\n", 
					TimeStrWithYear(),
					IntegerWithCommas(curr_str1, GameCommonData.game_serial_number));
			}
			
			// 24/01/01 kriskoin:
			if (GameCommonData.game_serial_number == SpecialHandNumber || SpecialHandNumber==(WORD32)-1) {
				AnnounceSpecialGame();		// announce to this table and everyone as well
				time_of_next_action = SecondCounter + 15;	// delay 15 seconds for everyone to read this
			}
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);

			//MemTrackVMUsage(FALSE, "%s(%d) Table::StartNewGameIfNecessary() (top)",_FL);
			//kp(("%s(%d) After starting a new game: GameCommonData.flags = $%02x\n", _FL, GameCommonData.flags));
		} else {
			// Not enough players to start a game, but we should at least
			// show which seats are empty and full.
			// irrelevant to tournament tables
			if (!tournament_table) {
				if (!showed_not_enough_players_msg) {
					showed_not_enough_players_msg = TRUE;
					if (max_number_of_players == 2) {
						sprintf(str, "We are unable to continue with only one player.");
					} else {
						sprintf(str, "We will wait for more players before starting the next game.");
					}
					SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
				}
				// perhaps someone needs the opportunity to buy more chips?
				for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
					if (players[i].player && players[i].chips < 2 * big_blind_amount) {
						if (!players[i].sent_out_of_money_msg) {
							OfferPlayerAbilityToBringMoreChips(i);
						}
					} else {
						players[i].sent_out_of_money_msg = FALSE;	// reset
					}
				}
			}
			table_is_active = TABLE_ACTIVE_FALSE;
			SetNobodyNeedsToPost();	// when it eventually fires up again, no need to post
			// Send out some blanked out GamePlayerData structures
			// to all connected players so that there are no chips
			// or cards in front of anyone.  Strangely, this should
			// be done before sending the GameCommonData to everyone
			// because it modifies the number of chips sitting in
			// front of a player.
			ClearChipsInFrontOfPlayers();
			UpdatePlayerCount();

			// Keep the hands/hour rate reasonable...  make it go down slowly
			// any time there are no games playing.
			// This gets executed about once every 5s, so don't go down too quickly.
			if (summary_info.hands_per_hour) {
				summary_info.hands_per_hour = (BYTE8)((float)summary_info.hands_per_hour * .98);
				//kp(("%s(%d) Reduced Hands/Hour rate for table %d. New value = %d\n", _FL, table_serial_number, summary_info.hands_per_hour));
				summary_info_changed = TRUE;
			}
			if (rake_per_hour) {
				rake_per_hour = (WORD16)((float)rake_per_hour * .98);
				//kp(("%s(%d) Reduced $/Hour rate for table %d. New value = %d\n", _FL, table_serial_number, summary_info.hands_per_hour));
			}
		}

	}
	return ERR_NONE;
}

//****************************************************************
// 
//
// Set the game parameters for this table.  These parameters can only be
// set once per table.  If a different game is to be played, a new table
// object should be used instead.
// Blind amounts are specified in 'chips', not dollars.
//
ErrorType Table::SetGameType(ClientDisplayTabIndex input_client_display_tab_index,
			GameRules input_game_rules,
			int input_max_number_of_players,
			int input_small_blind_amount, int input_big_blind_amount,
			ChipType passed_chip_type, int passed_game_disable_bits, RakeType passed_rake_profile)
{
	client_display_tab_index = input_client_display_tab_index;
	game_rules				 = input_game_rules;
	max_number_of_players	 = input_max_number_of_players;
	small_blind_amount		 = input_small_blind_amount;
	big_blind_amount		 = input_big_blind_amount;
	initial_small_blind_amount=input_small_blind_amount;
	initial_big_blind_amount = input_big_blind_amount;
	chip_type				 = passed_chip_type;
	game_disable_bits		 = passed_game_disable_bits;
	rake_profile			 = passed_rake_profile;

	pr(("%s(%d) SetGameType(tab_index=%d, rules=%d, chip_type=%d)\n",
			_FL,
			client_display_tab_index,
			game_rules,
			chip_type));
	summary_info.client_display_tab_index = (BYTE8)input_client_display_tab_index;
	summary_info.game_rules				  = (BYTE8)input_game_rules;
	summary_info.max_player_count	      = (BYTE8)max_number_of_players;
	summary_info.big_blind_amount		  = big_blind_amount;
	summary_info.small_blind_amount		  = small_blind_amount;
	if (chip_type==CT_TOURNAMENT) {
		summary_info.tournament_prize_pool = big_blind_amount * max_number_of_players;
	}
	summary_info_changed = TRUE;
	if (chip_type == CT_REAL) {
		summary_info.flags |= TSIF_REAL_MONEY;
	} else if (chip_type == CT_TOURNAMENT) {
		summary_info.flags |= TSIF_TOURNAMENT;
		tournament_table = TRUE;
	}

	GameCommonData.big_blind_amount   = big_blind_amount;
	GameCommonData.small_blind_amount = small_blind_amount;
	GameCommonData.client_display_tab_index = (BYTE8)input_client_display_tab_index;
	GameCommonData.game_rules = (BYTE8)input_game_rules;
	BYTE8 flags = GameCommonData.flags;
	flags &= ~(GCDF_REAL_MONEY|GCDF_TOURNAMENT|GCDF_ONE_ON_ONE);
	if (chip_type == CT_REAL) {
		flags |= GCDF_REAL_MONEY;
	} else if (chip_type == CT_TOURNAMENT) {
		flags |= GCDF_TOURNAMENT;
		flags |= GCDF_USE_REAL_MONEY_CHIPS;	// will be overriden when it needs to be
	}
	if (max_number_of_players==2) {
		flags |= GCDF_ONE_ON_ONE;
	}
	GameCommonData.flags = flags;

	FillBlankGPD(&table_gpd);
	FillBlankGPD(&pre_tourney_gpd);

	return ERR_NONE;
}

//****************************************************************
// 
//
// Count the number of players at this table.
//
int Table::UpdatePlayerCount(void)
{
	EnterCriticalSection(&TableCritSec);
	int player_count = 0;
	for (int i=0 ; i < MAX_PLAYERS_PER_GAME; i++) {
		if (players[i].status) {	// is someone presently seated in this seat?
			player_count++;
		}
	}
	summary_info.player_count = (BYTE8)player_count;
	summary_info.watching_count = (BYTE8)min(255,connected_watching_player_count);
	summary_info_changed = TRUE;

	//kriskoin: 	// no waiting list is required (somewhere else they'll be prevented from sitting)
	if (chip_type==CT_TOURNAMENT && table_tourn_state != TTS_WAITING) {
		// No waiting list should be allowed.
		summary_info.flags &= ~TSIF_WAIT_LIST_REQUIRED;
		if (GameCommonData.flags & GCDF_WAIT_LIST_REQUIRED) {
			GameCommonData.flags &= ~GCDF_WAIT_LIST_REQUIRED;
			FlagResendGameCommonDataToEveryone();	// re-send whenever waiting list status changes
		}
	} else {
		//kriskoin: 		// called to the table.  If we don't do this, someone might steal their
		// seat if they got called to a new table where the waiting list was not
		// previously required.
		int seat_count = player_count;
		seat_count += wait_list_called_player_count;

		if (seat_count >= max_number_of_players) {
			// Player must get on the waiting list to join this table.
			summary_info.flags |= TSIF_WAIT_LIST_REQUIRED;
			if (!(GameCommonData.flags & GCDF_WAIT_LIST_REQUIRED)) {
				GameCommonData.flags |= GCDF_WAIT_LIST_REQUIRED;
				//kp(("%s(%d) A waiting list is now required for table %s.\n", _FL, summary_info.table_name));
				FlagResendGameCommonDataToEveryone();	// re-send whenever waiting list status changes
			}
	  #if !FORCE_WAITING_LIST
		} else {
		  #if 0	//kriskoin: 			if (!tournament_table) {
				// If the table is half empty, no waiting list is required.
				if (max_number_of_players >= 8 && player_count <= max_number_of_players/2) {
					// Table is not even close to full... waiting list is not required.
					summary_info.flags &= ~TSIF_WAIT_LIST_REQUIRED;
					if (GameCommonData.flags & GCDF_WAIT_LIST_REQUIRED) {
						GameCommonData.flags &= ~GCDF_WAIT_LIST_REQUIRED;
						FlagResendGameCommonDataToEveryone();	// re-send whenever waiting list status changes
					}
				}
			}
		  #endif
	  #endif
		}
	}

	LeaveCriticalSection(&TableCritSec);
	return player_count;
}

//*********************************************************
// https://github.com/kriskoin//
// Set flags for all players and watching players so that
// the GameCommonData structure will be sent to them again.
//
void Table::FlagResendGameCommonDataToEveryone(void)
{
	EnterCriticalSection(&TableCritSec);
	int i;
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		players[i].game_common_data_sent = FALSE;
	}
	for (i=0 ; i<watching_player_count ; i++) {
		watching_players[i].game_common_data_sent = FALSE;
	}
	LeaveCriticalSection(&TableCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Check if a player is seated at this table
// returns: TRUE for seated, FALSE for not seated.
//
int Table::CheckIfPlayerSeated(Player *input_player_ptr)
{
	return CheckIfPlayerSeated(input_player_ptr->player_id);
}	
int Table::CheckIfPlayerSeated(WORD32 player_id)
{
	return CheckIfPlayerSeated(player_id, NULL);
}
int Table::CheckIfPlayerSeated(WORD32 player_id, int *output_seating_position)
{
	int found_flag = FALSE;
	if (output_seating_position) {
		*output_seating_position = 0;	// always initialize it.
	}
	EnterCriticalSection(&TableCritSec);
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		pr(("%s(%d) CheckIfPlayerSeated: table %d, player_id $%08lx, seat #%d has $%08lx\n",
					_FL, table_serial_number, player_id, i,
					players[i].player ? players[i].player->player_id : -1));
		if (players[i].player && players[i].player->player_id==player_id) {
			found_flag = TRUE;
			if (output_seating_position) {
				*output_seating_position = i;
			}
			break; 
		}
	}
	LeaveCriticalSection(&TableCritSec);
	return found_flag;
}

//*********************************************************
// https://github.com/kriskoin//
// Determine if a player is currently involved in a hand.
// If they are watching, sitting out, folded, or not at the table,
// then they are not involved.
// If they are playing or are All-In, then they ARE in involved
// in a hand.
// returns: TRUE if involved in a hand, FALSE otherwise.
//
int Table::IsPlayerInvolvedInHand(WORD32 player_id)
{
	EnterCriticalSection(&TableCritSec);

	if (!game) {
		LeaveCriticalSection(&TableCritSec);
		return FALSE;	// not playing (they can't be... there's no game)
	}

	int i = 0;
	if (!CheckIfPlayerSeated(player_id, &i)) {
		LeaveCriticalSection(&TableCritSec);
		return FALSE;	// not seated.
	}

	// They are seated in seating position 'i'
	if (game->PlayerData[0]->player_status[i] == PLAYER_STATUS_PLAYING || 
		game->PlayerData[0]->player_status[i] == PLAYER_STATUS_ALL_IN)
	{
		// Looks like they're still playing
		LeaveCriticalSection(&TableCritSec);
		return TRUE;	// currently playing
	}

	LeaveCriticalSection(&TableCritSec);
	return FALSE;	// not currently playing a hand
}

/**********************************************************************************
 Function Table::AddMoreChipsForPlayer(Player *input_player_ptr, int seating_position, WORD32 chips)
 date: kriskoin 2019/01/01 Purpose: for a player that's already seated, buy more chips
***********************************************************************************/
ErrorType Table::AddMoreChipsForPlayer(Player *input_player_ptr, int seating_position, WORD32 chips)
{
	EnterCriticalSection(&TableCritSec);
	if (!input_player_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to buy more chips for a NULL player", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	// Refuse if he's not logged in (implies hacked packet)
	if (ANONYMOUS_PLAYER(input_player_ptr->player_id)) {
		Error(ERR_ERROR, "%s(%d) Attempted to buy more chips for an anonymous player ($%08lx). Refusing.", _FL, input_player_ptr->player_id);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}

	// Verify that the player is not watching this table.
	int i;
	for (i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].player==input_player_ptr) {
			Error(ERR_ERROR, "%s(%d) Attempted to buy more chips for a player that's watching ($%08lx). Refusing.", _FL, input_player_ptr->player_id);
			LeaveCriticalSection(&TableCritSec);
			return ERR_ERROR;
		}
	}
	// verify he's not somehow buying more than he has in the bank
	int player_chips_in_bank = SDB->GetChipsInBankForPlayerID(input_player_ptr->player_id, chip_type);
	if ((int)chips > player_chips_in_bank) { 
		Error(ERR_ERROR, "%s(%d) AddMoreChipsForPlayer buy request was to bring more chips than he has!"
			"pID = $%08lx, req = %d, in bank = %d, chip_type = %d",
			_FL, input_player_ptr->player_id, chips, player_chips_in_bank, chip_type);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;	// do not process.
	} 
	// 24/01/01 kriskoin:
	// be taken advantage of by slowly bringing more when you're about to be all-in
	if (!players[seating_position].chips) {	// he's at zero, invalid to add a little more, must be enough
		WORD32 usual_required = 0, shortbuy_required = 0, minimum_required = 0;
		if (FillBuyMinimums(seating_position, &usual_required, &shortbuy_required, &minimum_required) != ERR_NONE) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Bad call to FillBuyMinimums()", _FL);
			LeaveCriticalSection(&TableCritSec);
			return ERR_ERROR;
		}
		if (chips < minimum_required) { // not enough, ignore it
			if (DebugFilterLevel <= 0) {
				Error(ERR_WARNING, "%s(%d) AddMoreChipsForPlayer (%d chips) not enough because he just went all-in", _FL, chips);
			}
			LeaveCriticalSection(&TableCritSec);
			return ERR_ERROR;	// do not process
		}
		// this might be his short buy
		if (chips < usual_required) {	// he's using his short buy
			players[seating_position].allowed_short_buy = FALSE;
		}
	}
	// all looks ok
	players[seating_position].chips += chips;
	GameCommonData.chips[seating_position] = players[seating_position].chips;

	// tell the player object to transfer chips from bank to playing
	// 24/01/01 kriskoin:
	// 'chips' variable and not the new incremented (full) amount.
	// Using players[seating_position].chips adds the new amount twice.
	players[seating_position].player->BuyingIntoTable(chips, chip_type);
	// 24/01/01 kriskoin:
	PL->LogFinancialTransfer(LOGTYPE_CHIPS_TO_TABLE, input_player_ptr->player_id, table_serial_number,
		chips, 0, 0, chip_type, "Chips to table");
	FlagResendGameCommonDataToEveryone();
  #if 0 // do we want this?
	char str[100];
	zstruct(str);
	sprintf(str, "%s has brought %d more chips to the table", input_player_ptr->Name, chips);
	SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
  #endif

	LeaveCriticalSection(&TableCritSec);
	return ERR_NONE;
}

/**********************************************************************************
 Function FillBuyMinimums(int *usual_required, int *shortbuy_required, int *minimum_required)
 date: 24/01/01 kriskoin Purpose: fill in these numbers we need when attempting to sit down or bring more chips
***********************************************************************************/
ErrorType Table::FillBuyMinimums(int p_index, WORD32 *usual_required, WORD32 *shortbuy_required, WORD32 *minimum_required)
{
	if (!usual_required || !shortbuy_required || !minimum_required) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) FillBuyMinimums called with a null", _FL);
		return ERR_ERROR;
	}
	*usual_required = big_blind_amount*USUAL_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN*GameStakesMultipliers[game_rules - GAME_RULES_START];
	*shortbuy_required = big_blind_amount*SHORTBUY_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN*GameStakesMultipliers[game_rules - GAME_RULES_START];
	if (players[p_index].allowed_short_buy) {
		*minimum_required = *shortbuy_required;
	} else {
		*minimum_required = *usual_required;
	}
	return ERR_NONE;
}

//****************************************************************
// 
//
// Add a player to a table.  They will be added to the seated and
// playing list.  If there is no room at the table or there is
// some other reason a player can't be added to the table, an
// error is returned.  Players can be added during a game but they
// won't get to play until the next game starts.
// Pass -1 for seating position if you don't care.
//
ErrorType Table::AddPlayer(Player *input_player_ptr, int seating_position, WORD32 buy_in_chips)
{
	EnterCriticalSection(&TableCritSec);
	if (!input_player_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to seat a NULL player", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	// Refuse seating requests for players who are not logged in.
	if (ANONYMOUS_PLAYER(input_player_ptr->player_id)) {
		Error(ERR_ERROR, "%s(%d) Attempted to seat an anonymous player ($%08lx). Refusing.", _FL, input_player_ptr->player_id);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}

	// make sure that the table isn't full
	if (summary_info.player_count >= max_number_of_players) {
	  #if 1	// 2022 kriskoin
		//kriskoin: 		// seat at the same time.  One of them will get the seat; the other won't.
		// They're already watching the table (we haven't removed them yet),
		// so there's no need to add them back in as watching.
	  #else
		Error(ERR_ERROR, "%s(%d) Attempted to add player to a full table.", _FL);
	  #endif
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	// range check the seating position
	if (seating_position < -1 || seating_position >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to add player to table in seat position %d. Table only holds %d", _FL, seating_position, max_number_of_players);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	if (seating_position == -1) {	// Caller doesn't care where they sit.
		// Pick the first empty seat.
		//!!! in the future, we should have a seating order array for tables which
		// accept 10 players.  We can then seat them scattered around the table.
		// This would be a special case for tables with exactly 10 seats.
		for (seating_position=0 ; seating_position < MAX_PLAYERS_PER_GAME; seating_position++) {
			if (!players[seating_position].status)
				break;
		}
	}

	// Verify that the player is not already watching this table.
	int i;
	for (i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].player==input_player_ptr) {
			// Remove him from the watching list but don't notify the client
			// that he's been removed (otherwise the window will close).
			RemoveWatchingPlayer(input_player_ptr, FALSE);
			break;
		}
	}

	// Verify that the player is not already sitting at this table.
	if (CheckIfPlayerSeated(input_player_ptr)) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to seat a player at a table he's already seated at", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	if (players[seating_position].status) {
		// this can validly happen if two players click on the same seat from different clients
		// one of them will get the seat; the other won't.
		// adate: send a packet letting us know we're back to just watching
		// we need to notify the client so it knows that the attempt to sit down failed
		// for a valid reason
		//kp(("%s(%d) Attempted to add player in an occupied seating position.\n", _FL));
		AddWatchingPlayer(input_player_ptr);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}

	// Make sure they have adequate privileges to sit down at this table
	BYTE8 priv = SDB->GetPrivLevel(input_player_ptr->player_id);
	BYTE8 req_priv = (BYTE8)(chip_type == CT_REAL ? ACCPRIV_REAL_MONEY : ACCPRIV_PLAY_MONEY);
	if (priv < req_priv) {
		// privilege level is less than required for this table type.
		AddWatchingPlayer(input_player_ptr);
		LeaveCriticalSection(&TableCritSec);
		input_player_ptr->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
				table_serial_number, 0, 0, 0, 0,
				"This account does not have the required privilege level\n"
				"to play at this table.");
		return ERR_ERROR;
	}

	// adate: it shouldn't be possible for the player to try to sit down with less than
	// the allowable number of chips (ie, 10x the big blind?).  if we got here and that's
	// the case, we won't allow it.  the implication is a hacked client -- because the client
	// is range checking this too
	// adate: We'll allow admin clients to buy in with less
	WORD32 usual_required = 0, shortbuy_required = 0, minimum_required = 0;
	if (FillBuyMinimums(seating_position, &usual_required, &shortbuy_required, &minimum_required) != ERR_NONE) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Bad call to FillBuyMinimums()", _FL);
		return ERR_ERROR;
	}

	// adate: if he's low on chips and it's our smallest Hold'em table, 
	// he gets to buy in with the small blind amount
	// SMALLEST_STAKES_BIG_BLIND is defined in pplib.h
	// allow him to sit if it's this or less
	if (big_blind_amount <= SMALLEST_STAKES_BIG_BLIND && client_display_tab_index == DISPLAY_TAB_HOLDEM) {
		int total_chips_left = 
			SDB->GetChipsInBankForPlayerID(input_player_ptr->player_id, chip_type) +
			SDB->GetChipsInPlayForPlayerID(input_player_ptr->player_id, chip_type);
		// let him play with what he has left
		minimum_required = min((int)minimum_required, (int)total_chips_left);
		// but not less than the big blind
		minimum_required = max((int)minimum_required, (int)big_blind_amount);
		// 20000120HK trim pennies so it's a valid round number
		minimum_required -= minimum_required % 100;
	}
	
	// tournament buy-in is small+big blinds
	if (tournament_table) {
		minimum_required = small_blind_amount + big_blind_amount;
	}
	
	if (input_player_ptr->priv < ACCPRIV_ADMINISTRATOR && buy_in_chips < (WORD32)minimum_required) {
		kp1(("%s(%d) ToDo: don't call AddWatchingPlayer is he's already watching\n", _FL));
		AddWatchingPlayer(input_player_ptr);
		LeaveCriticalSection(&TableCritSec);
		Error(ERR_ERROR, "%s(%d) Player [$%08lx] requested to sit at a %d/%d table[%d] with %d chips",
			_FL, input_player_ptr->player_id, small_blind_amount, big_blind_amount,
			table_serial_number, buy_in_chips);
		return ERR_ERROR;
	}
	
	// his chip amount has been validated at this point
	ErrorType err = input_player_ptr->JoinTable(&GameCommonData, FALSE, summary_info.table_name);
	if (err==ERR_NONE) {
		// Everything looks good... seat him.
		zstruct(players[seating_position]);
		players[seating_position].player = input_player_ptr;	// he's now seated.
		players[seating_position].chips = buy_in_chips;
		players[seating_position].status = 2;					// human
		players[seating_position].gender = input_player_ptr->Gender;
		if (new_players_must_post) {
			players[seating_position].post_needed = POST_NEEDED_INITIAL; // must post initial amount (usually BB)
		}
		players[seating_position].allowed_short_buy = 
			(buy_in_chips < usual_required ? FALSE : TRUE);

		// Update the table_info structure for sending summary info to
		// the clients.
		zstruct(table_info.players[seating_position]);
		table_info.players[seating_position].player_id = input_player_ptr->player_id;
		//cris Thu Feb 26 15:07:12 2004
		kp(("Player_id %d User_id %s Position %d \n",table_info.players[seating_position].player_id,input_player_ptr->user_id,seating_position));
        //end cris Thu Feb 26 15:07:12 2004
		table_info.players[seating_position].chips = players[seating_position].chips;
		strnncpy(table_info.players[seating_position].name, input_player_ptr->user_id, MAX_COMMON_STRING_LEN);
		strnncpy(table_info.players[seating_position].city, input_player_ptr->City, MAX_COMMON_STRING_LEN);
		table_info_changed = TRUE;

		// Update the GameCommonData structure as well (these fields can
		// be updated safely even DURING a game).
		strnncpy(GameCommonData.name[seating_position], input_player_ptr->user_id, MAX_COMMON_STRING_LEN);
		strnncpy(GameCommonData.city[seating_position], input_player_ptr->City, MAX_COMMON_STRING_LEN);
		GameCommonData.gender[seating_position] = players[seating_position].gender;
		GameCommonData.chips[seating_position] = players[seating_position].chips;
		GameCommonData.player_id[seating_position] = input_player_ptr->player_id;
		GameCommonData.bar_snack[seating_position] = BAR_SNACK_NONE;
		// specify earliest time a new game can start.
		next_game_start_time = max(next_game_start_time, SecondCounter + TimeBetweenGames);
		// tell the player object to transfer chips from bank to playing
		players[seating_position].player->BuyingIntoTable(players[seating_position].chips, chip_type);
	}
	LeaveCriticalSection(&TableCritSec);

	FlagResendGameCommonDataToEveryone();
	UpdatePlayerCount();

	char str[100];
	zstruct(str);
	sprintf(str, "%s has joined this table.", input_player_ptr->user_id);
	SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
	time_of_next_update = 0;	// update asap when new players join
	return err;
}
#if 0
//****************************************************************
// 
//
// Add a computer player to a table.  Similar to AddPlayer() but for computer players.
//
ErrorType Table::AddComputerPlayer(int seating_position)
{
	EnterCriticalSection(&TableCritSec);

  #if 1	// adate: we want to be able to sit wherever we want
	if (seating_position < -1 || seating_position >= MAX_PLAYERS_PER_GAME	) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to add player to table in seat position %d. Table only holds %d", _FL, seating_position, MAX_PLAYERS_PER_GAME);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
  #else
	if (seating_position < -1 || seating_position >= max_number_of_players) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to add player to table in seat position %d. Table only holds %d", _FL, seating_position, max_number_of_players);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
  #endif
	if (seating_position == -1) {	// Caller doesn't care where they sit.
		// Pick the first empty seat.
		//!!! in the future, we should have a seating order array for tables which
		// accept 10 players.  We can then seat them scattered around the table.
		// This would be a special case for tables with exactly 10 seats.
		for (seating_position=0 ; seating_position < max_number_of_players ; seating_position++) {
			if (!players[seating_position].status)
				break;
		}
	}

  #if 0	// :::	if (seating_position >= max_number_of_players) {
		Error(ERR_ERROR, "%s(%d) Attempted to add player to a full table.", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
  #endif

	if (players[seating_position].status) {
		Error(ERR_ERROR, "%s(%d) Attempted to add player in an occupied seating position.", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
/*
	char *computer_names[MAX_PLAYERS_PER_GAME] = {
			"Hal", "R2D2", "C3PO", "RobotRosie", "Eniac", "Deep Blue",
			"R0bocop", "Tin Man", "Data", "T-1000"
	};*/

        char *computer_names[30]= {
		    "Bubba","Cynthia", "Linda", "Miles", "Mark", "Jennifer",
			"Paula", "Denis", "Lency", "Fred","Froilan","Daniel",
			"Fabian","Andres","Chani","Jose",
			"Victor","Jonathan","Sofia","Ester","Marcela",
			"Estela","Angelo","Mauricio","Marco",
			"Ricardo69","Amalia69","Kris",
			"alex456","mar798"
			};   

     //char *computer_names[3]={"Cynthia","Linda","Miles"};


  #if 0	// 2022 kriskoin
	char *computer_cities[MAX_PLAYERS_PER_GAME] = {
			"Houston", "Alderaan", "Tatooine", "Mars", "Palo Alto", "New York",
			"San Francisco", "Oz", "Omicron Theta", "Detroit"
	};
  #endif
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	//cris 28-11-2003
	//make a random for player name
	int name_pos;
	int flag=0;
	int counter=0;
	//name_pos=1+(rand()%21);
	name_pos=1+(rand()%28);
	//srand(28);
	//loop still find a unique robot for this entry
	#if 0
	while(!flag){
		if(findPlayer4LoginName(computer_names[name_pos])==-1 || counter==25){
		//(1){
			flag=1;
			break;
		}else{
		//srand();
		srand(28);
		name_pos=1+(rand()%28);
			kp(("POS %d",name_pos));
		//sleep(20);
		};//if
		counter++;
	};//while
	if(counter==25){
		name_pos=5;
	};//while
	#endif
	Error(ERR_ERROR, "name pos FOR ROBOT (%d) NAME = %s \n",name_pos,computer_names[name_pos]);
	//cris 28-11-2003
	
	// search for this particular userid/password combo
	//cris 28-11-2003
	//int found_player = SDB->SearchDataBaseByUserID(computer_names[seating_position], &player_rec);
	//int found_player = SDB->SearchDataBaseByUserID(computer_names[name_pos], &player_rec);
	//cris 28-11-2003
  	int found_player = SDB->SearchDataBaseByUserID(computer_names[name_pos], &player_rec);
	if (found_player < 0) {	// didn't find an account for this robot
		Error(ERR_ERROR, "%s(%d) Didn't find user record for robot %s", _FL, computer_names[name_pos]);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
	//kp(("%s(%d) computer player '%s' has player_id $%08lx\n", _FL, computer_names[seating_position], player_rec.player_id));
	// no password validation; just fill things in
	int chips_in_bank;
	int chips_in_play;
	if (chip_type == CT_REAL) {
		chips_in_bank = player_rec.real_in_bank;
		chips_in_play = player_rec.real_in_play;
	} else if (chip_type == CT_TOURNAMENT) {	// use REAL for tournament buyin
		chips_in_bank = player_rec.real_in_bank;
		chips_in_play = player_rec.real_in_play;
	} else {	// play money
		chips_in_bank = player_rec.fake_in_bank;
		chips_in_play = player_rec.fake_in_play;
	}
	WORD32 chips_buying_in;
  #if 1	// 2022 kriskoin
	//kriskoin: 	ChipType chip_type_to_buy_in_with = (chip_type == CT_TOURNAMENT ? CT_REAL : chip_type);
	if (chip_type == CT_TOURNAMENT) {	// give the robot the right amount
		chips_buying_in = big_blind_amount + small_blind_amount;
	} else {	// anything else
		chips_buying_in = min(chips_in_bank, big_blind_amount*1000);
    	chips_buying_in -= (chips_buying_in % 100);
	}
  #else
	// he'll try to buy in with a quarter of what he has; if not, half the bankroll
	if (chips_in_bank > (WORD32)((chips_in_bank+chips_in_play)/6)) {
		chips_buying_in = (WORD32)((chips_in_bank+chips_in_play)/6);
    	chips_buying_in -= (chips_buying_in % 100);
	} else {
		chips_buying_in = (WORD32)(chips_in_bank/2);	// always a bit left over
    	chips_buying_in -= (chips_buying_in % 100);
	}
  #endif
	// adate: buy in with even dollar amounts only -- no .50, .25, etc...
	if (chip_type != CT_TOURNAMENT && chips_buying_in < (WORD32)big_blind_amount*2) {
		//kriskoin: 		kp(("%s(%d) computer player does not have enough to buy in.  Needs %d, only has %d (chips_in_bank=%d)\n", _FL, big_blind_amount*2, chips_buying_in, chips_in_bank));
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
	zstruct(players[seating_position]);
	players[seating_position].chips = chips_buying_in;
	players[seating_position].status = 1;		// computer.
	//players[seating_position].status = 2;		// HUMAN
	if (new_players_must_post) {
		players[seating_position].post_needed = POST_NEEDED_INITIAL;
	}
	players[seating_position].gender = player_rec.gender;
	GameCommonData.player_id[seating_position] = player_rec.player_id;
	GameCommonData.chips[seating_position] = chips_buying_in;
	strnncpy(GameCommonData.name[seating_position], player_rec.user_id, MAX_COMMON_STRING_LEN);
	strnncpy(GameCommonData.city[seating_position], player_rec.city, MAX_COMMON_STRING_LEN);

	FlagResendGameCommonDataToEveryone();
	// Update the table_info structure for sending summary info to the clients.
	zstruct(table_info.players[seating_position]);
	table_info.players[seating_position].player_id = player_rec.player_id;
	table_info.players[seating_position].chips = chips_buying_in;
	strnncpy(table_info.players[seating_position].name, player_rec.user_id, MAX_COMMON_STRING_LEN);
	strnncpy(table_info.players[seating_position].city, player_rec.city, MAX_COMMON_STRING_LEN);
	table_info_changed = TRUE;
	// spoof the player object's BuyingIntoTable()
	SDB->SetChipsInBankForPlayerID(player_rec.player_id, chips_in_bank-chips_buying_in, chip_type_to_buy_in_with);
	SDB->SetChipsInPlayForPlayerID(player_rec.player_id, chips_in_play+chips_buying_in, chip_type_to_buy_in_with);
	
	UpdatePlayerCount();
	LeaveCriticalSection(&TableCritSec);
	return ERR_NONE;
}		
	
#endif

//****************************************************************
// 
//
// Add a computer player to a table.  Similar to AddPlayer() but for computer players.
//
ErrorType Table::AddComputerPlayer(int seating_position)
{
	EnterCriticalSection(&TableCritSec);

  #if 1	// adate: we want to be able to sit wherever we want
	if (seating_position < -1 || seating_position >= MAX_PLAYERS_PER_GAME	) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to add player to table in seat position %d. Table only holds %d", _FL, seating_position, MAX_PLAYERS_PER_GAME);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
  #else
	if (seating_position < -1 || seating_position >= max_number_of_players) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to add player to table in seat position %d. Table only holds %d", _FL, seating_position, max_number_of_players);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
  #endif
	if (seating_position == -1) {	// Caller doesn't care where they sit.
		// Pick the first empty seat.
		//!!! in the future, we should have a seating order array for tables which
		// accept 10 players.  We can then seat them scattered around the table.
		// This would be a special case for tables with exactly 10 seats.
		for (seating_position=0 ; seating_position < max_number_of_players ; seating_position++) {
			if (!players[seating_position].status)
				break;
		}
	}

  #if 0	// :::	if (seating_position >= max_number_of_players) {
		Error(ERR_ERROR, "%s(%d) Attempted to add player to a full table.", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
  #endif

	if (players[seating_position].status) {
		Error(ERR_ERROR, "%s(%d) Attempted to add player in an occupied seating position.", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
/*
	char *computer_names[MAX_PLAYERS_PER_GAME] = {
			"Hal", "R2D2", "C3PO", "RobotRosie", "Eniac", "Deep Blue",
			"R0bocop", "Tin Man", "Data", "T-1000"
	};*/

/*        char *computer_names[23] = {
			"Bubba", "Cynthia", "Linda", "Miles", "Mark", "Jennifer",
			"Paula", "Denis", "Lency", "Fred","Froilan","Daniel",
			"Fabian","Andres","Chani","Jose",
			"Victor","Jonathan","Sofia","Ester","Marcela",
			"Estela","Angelo"
			};   */
//char *computer_names[10] = {"Robot1", "Robot2", "Robot3", "Robot4", "Robot5", "Robot6", "Robot7", "Robot8", "Robot9", "Robot10"};   
 char *computer_names[10] = {"Andres", "Cynthia", "Linda", "Miles", "Mark", "Jennifer","Paula", "Denis", "Lency", "Fred"};
			
      //char *computer_names="Bubba";


  #if 0	// 2022 kriskoin
	char *computer_cities[MAX_PLAYERS_PER_GAME] = {
			"Houston", "Alderaan", "Tatooine", "Mars", "Palo Alto", "New York",
			"San Francisco", "Oz", "Omicron Theta", "Detroit"
	};
  #endif
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	//cris 28-11-2003
	//make a random for player name
	int name_pos;
	name_pos=1+(rand()%1);
	//Error(ERR_ERROR, "name pos FOR ROBOT (%d) NAME = %s \n",name_pos,computer_names[name_pos]);
	//cris 28-11-2003
	name_pos = seating_position;
	// search for this particular userid/password combo
	//cris 28-11-2003
	//int found_player = SDB->SearchDataBaseByUserID(computer_names[seating_position], &player_rec);
	int found_player = SDB->SearchDataBaseByUserID(computer_names[name_pos], &player_rec);
	//cris 28-11-2003
  	//int found_player = SDB->SearchDataBaseByUserID(computer_names, &player_rec);
	if (found_player < 0) {	// didn't find an account for this robot
		Error(ERR_ERROR, "%s(%d) Didn't find user record for robot %s", _FL, computer_names[name_pos]);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
	//kp(("%s(%d) computer player '%s' has player_id $%08lx\n", _FL, computer_names[seating_position], player_rec.player_id));
	// no password validation; just fill things in
	int chips_in_bank;
	int chips_in_play;
	if (chip_type == CT_REAL) {
		chips_in_bank = player_rec.real_in_bank;
		chips_in_play = player_rec.real_in_play;
	} else if (chip_type == CT_TOURNAMENT) {	// use REAL for tournament buyin
		chips_in_bank = player_rec.real_in_bank;
		chips_in_play = player_rec.real_in_play;
	} else {	// play money
		chips_in_bank = player_rec.fake_in_bank;
		chips_in_play = player_rec.fake_in_play;
	}
	WORD32 chips_buying_in;
  #if 1	// 2022 kriskoin
	//kriskoin: 	ChipType chip_type_to_buy_in_with = (chip_type == CT_TOURNAMENT ? CT_REAL : chip_type);
	if (chip_type == CT_TOURNAMENT) {	// give the robot the right amount
		chips_buying_in = big_blind_amount + small_blind_amount;
	} else {	// anything else
		chips_buying_in = min(chips_in_bank, big_blind_amount*1000);
    	chips_buying_in -= (chips_buying_in % 100);
	}
  #else
	// he'll try to buy in with a quarter of what he has; if not, half the bankroll
	if (chips_in_bank > (WORD32)((chips_in_bank+chips_in_play)/6)) {
		chips_buying_in = (WORD32)((chips_in_bank+chips_in_play)/6);
    	chips_buying_in -= (chips_buying_in % 100);
	} else {
		chips_buying_in = (WORD32)(chips_in_bank/2);	// always a bit left over
    	chips_buying_in -= (chips_buying_in % 100);
	}
  #endif
	// adate: buy in with even dollar amounts only -- no .50, .25, etc...
	if (chip_type != CT_TOURNAMENT && chips_buying_in < (WORD32)big_blind_amount*2) {
		//kriskoin: 		kp(("%s(%d) computer player does not have enough to buy in.  Needs %d, only has %d (chips_in_bank=%d)\n", _FL, big_blind_amount*2, chips_buying_in, chips_in_bank));
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}
	zstruct(players[seating_position]);
	players[seating_position].chips = chips_buying_in;
	players[seating_position].status = 1;		// computer.
	if (new_players_must_post) {
		players[seating_position].post_needed = POST_NEEDED_INITIAL;
	}
	players[seating_position].gender = player_rec.gender;
	GameCommonData.player_id[seating_position] = player_rec.player_id;
	GameCommonData.chips[seating_position] = chips_buying_in;
	strnncpy(GameCommonData.name[seating_position], player_rec.user_id, MAX_COMMON_STRING_LEN);
	strnncpy(GameCommonData.city[seating_position], player_rec.city, MAX_COMMON_STRING_LEN);

	FlagResendGameCommonDataToEveryone();
	// Update the table_info structure for sending summary info to the clients.
	zstruct(table_info.players[seating_position]);
	table_info.players[seating_position].player_id = player_rec.player_id;
	table_info.players[seating_position].chips = chips_buying_in;
	strnncpy(table_info.players[seating_position].name, player_rec.user_id, MAX_COMMON_STRING_LEN);
	strnncpy(table_info.players[seating_position].city, player_rec.city, MAX_COMMON_STRING_LEN);
	table_info_changed = TRUE;
	// spoof the player object's BuyingIntoTable()
	SDB->SetChipsInBankForPlayerID(player_rec.player_id, chips_in_bank-chips_buying_in, chip_type_to_buy_in_with);
	SDB->SetChipsInPlayForPlayerID(player_rec.player_id, chips_in_play+chips_buying_in, chip_type_to_buy_in_with);
	
	UpdatePlayerCount();
	LeaveCriticalSection(&TableCritSec);
	return ERR_NONE;
}		
	
//****************************************************************
// January 4 -2004
// Cris
// Find a player for login name
// only for add robot  propuse
int Table::findPlayer4LoginName(char *loginName){
	int position=-1;
#if 0	
	int i=0;
	for(i=0;i<=10;i++){
	  if(table_info.players[i].name){
		 if(strcmp(table_info.players[i].name,loginName)){
			 position=i;
			  kp(("SI\n")); 
			 break;
		 };//if
	  };
	  kp(("NO\n")); 
    };//for
#endif
	return position;
};	

//****************************************************************
// 
//
// Add a player to a table's 'watching' list.  If there is no
// room at the table or there is some other reason a player
// can't be added to the table, an error is returned.
//
ErrorType Table::AddWatchingPlayer(Player *input_player_ptr)
{
	EnterCriticalSection(&TableCritSec);
	if (!input_player_ptr) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to seat a NULL player", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	if (watching_player_count >= MAX_WATCHING_PLAYERS_PER_TABLE) {
		Error(ERR_ERROR, "%s(%d) Attempted to add watching player but there was not room.", _FL);
		LeaveCriticalSection(&TableCritSec);
		return ERR_ERROR;
	}

	// Verify that the player is not already sitting at this table.
	int i;
	for (i=0 ; i<max_number_of_players ; i++) {
		if (players[i].player==input_player_ptr) {
		  #if 0	//kriskoin: 			Error(ERR_ERROR, "%s(%d) Attempted to seat a player at a table he's already seated at", _FL);
		  #endif
			LeaveCriticalSection(&TableCritSec);
			return ERR_ERROR;
		}
	}

	// Verify that the player is not already watching this table.
	for (i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].player==input_player_ptr) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Attempted to seat a player at a table he's already watching", _FL);
			LeaveCriticalSection(&TableCritSec);
			return ERR_INTERNAL_ERROR;
		}
	}

	ErrorType err = input_player_ptr->JoinTable(&GameCommonData, TRUE, summary_info.table_name);
	if (err==ERR_NONE) {
		// Looks good... add him to the list.
		i = watching_player_count;
		zstruct(watching_players[i]);
		watching_players[i].player = input_player_ptr;	// he's now seated.
		watching_players[i].status = 2;					// human
		watching_player_count++;
	}
	LeaveCriticalSection(&TableCritSec);
	summary_info.watching_count = (BYTE8)min(255,connected_watching_player_count);
	summary_info_changed = TRUE;

	FlagResendGameCommonDataToEveryone();	// send out new watching_count

	// Remind them about the rules of the table.
	struct GameChatMessage gcm;
	zstruct(gcm);
	gcm.text_type = (BYTE8)CHATTEXT_DEALER_NORMAL;
	gcm.table_serial_number = table_serial_number;
	strnncpy(gcm.name, "Dealer", MAX_COMMON_STRING_LEN);
	strnncpy(gcm.message, " Please chat in English and keep it clean.", MAX_CHAT_MSG_LEN);
	input_player_ptr->SendDataStructure(DATATYPE_CHAT_MSG, &gcm, sizeof(gcm));

	return err;
}

//****************************************************************
// https://github.com/kriskoin//
// Remove a player from a table.  This can be used to remove him
// for any number of reasons.  This function will make sure
// his chips total gets updated and will notify the player object
// that it no longer is joined to this table.  The player object
// in turn will take care of notifying the remote client.
//

// 24/01/01 kriskoin:
ErrorType Table::RemovePlayerFromTable(int seating_index)
{
	return RemovePlayerFromTable(seating_index, TRUE);
}

ErrorType Table::RemovePlayerFromTable(int seating_index, int notify_player)
{
	EnterCriticalSection(&TableCritSec);
	if (game) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) RemovePlayerFromTable(table %d, seat %d): Can't kick player off table mid-game!", _FL, table_serial_number, seating_index);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (seating_index < 0 || seating_index >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Illegal seating_index (%d) passed to RemovePlayerFromTable()", _FL, seating_index);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (!players[seating_index].status) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Empty seat (%d) passed to RemovePlayerFromTable()", _FL, seating_index);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

  #if 0	// 24/01/01 kriskoin:
	int tournament_in_progress = (tournament_table && table_tourn_state == TTS_PLAYING_GAME);
  #else // this will trap them at the table as soon as it's starting or has finished
	int tournament_in_progress = (tournament_table && 
								  table_tourn_state > TTS_WAITING &&
								  table_tourn_state < TTS_FINISHED);
  #endif
	// 24/01/01 kriskoin:
	// to leave when the tournament has already started
	if ( (tournament_in_progress && players[seating_index].chips) ||
		  table_tourn_state == TTS_CONVERT_CHIPS ||
		  table_tourn_state == TTS_MOVE_CHIPS_OUT) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Player (%d) tried to leave a tournament while it's in progress with %d chips", 
			_FL, seating_index, players[seating_index].chips);
		if (players[seating_index].player) {
			int player_table_index = players[seating_index].player->SerialNumToJoinedTableIndex(table_serial_number);
			if (player_table_index >= 0) {	// in case of wierdness...
				// this will allow him to return to his seat if he wishes
				players[seating_index].player->JoinedTables[player_table_index].client_state_info.leave_table = FALSE;
			}
		}
		LeaveCriticalSection(&TableCritSec);
		return ERR_NONE;
	}
  
	// There's someone there... remove them from the table
	pr(("%s(%d) Remove player %d from table %d\n", _FL, seating_index, table_serial_number));
	ErrorType err = ERR_NONE;
	if (players[seating_index].status==2) {	// human
		err = players[seating_index].player->LeaveTable(table_serial_number, notify_player);
		// tell the player object to transfer chips from playing to bank
		if (chip_type==CT_TOURNAMENT) {
			if (table_tourn_state==TTS_WAITING) {
				// This is a tournament table and it has not yet started.  The chips
				// in front of the player are actually real money chips (his buyin+fee).
				players[seating_index].player->LeavingTable(players[seating_index].chips, CT_REAL);
			} else {
				// This is a tournament table and it has been cancelled for some reason.
				// The player has already been paid out.
				players[seating_index].player->LeavingTable(0, CT_REAL);
			}
		} else {	// non-tournament table...
			players[seating_index].player->LeavingTable(players[seating_index].chips, chip_type);
		}
	}
	zstruct(players[seating_index]);	// make seat vacant
	zstruct(table_info.players[seating_index]);
	table_info_changed = TRUE;

	// Update the GameCommonData structure as well (these fields can
	// be updated safely even DURING a game).
	memset(GameCommonData.name[seating_index], 0, MAX_COMMON_STRING_LEN);
	memset(GameCommonData.city[seating_index], 0, MAX_COMMON_STRING_LEN);
	GameCommonData.chips[seating_index] = 0;
	GameCommonData.bar_snack[seating_index] = BAR_SNACK_NONE;
	GameCommonData.player_id[seating_index] = 0;
  #if 0	//kriskoin: 	GamePlayerData.chips_bet_total[seating_index] = 0;
	GamePlayerData.player_status[seating_index] = PLAYER_STATUS_EMPTY;
  #endif

	// Whenever a player leaves, tell the cardroom to update waiting
	// lists for this game type.
	((CardRoom *)cardroom)->update_waiting_list[client_display_tab_index] = TRUE;

	FlagResendGameCommonDataToEveryone();
	UpdatePlayerCount();
	LeaveCriticalSection(&TableCritSec);
	time_of_next_update = 0;	// update asap when new players join
	return err;
}

ErrorType Table::RemovePlayerFromTable(Player *player_ptr)
{
	// Scan for this player's seating index then call the
	// main RemovePlayerfromTable() function.
	EnterCriticalSection(&TableCritSec);
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status==2 && players[i].player==player_ptr) {
			ErrorType err = RemovePlayerFromTable(i);
			LeaveCriticalSection(&TableCritSec);
			return err;
		}
	}
	LeaveCriticalSection(&TableCritSec);
	return ERR_ERROR;	// player not found.
}

//****************************************************************
// https://github.com/kriskoin//
// Remove a player from the watching list
//
ErrorType Table::RemoveWatchingPlayer(int watching_player_index)
{
	return RemoveWatchingPlayer(watching_player_index, TRUE);
}	

ErrorType Table::RemoveWatchingPlayer(int watching_player_index, int notify_player_flag)
{
	EnterCriticalSection(&TableCritSec);
	if (watching_player_index < 0 || watching_player_index >= watching_player_count) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Illegal seating_index (%d) passed to RemoveWatchingPlayer()", _FL, watching_player_index);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (!watching_players[watching_player_index].player) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) No player ptr associated with a watching_player[%d]", _FL, watching_player_index);
		LeaveCriticalSection(&TableCritSec);
		return ERR_INTERNAL_ERROR;
	}

	// There's someone there... remove them from the table.
	pr(("%s(%d) Remove watching player %d from table %d\n", _FL, watching_player_index, table_serial_number));
	ErrorType err = watching_players[watching_player_index].player->LeaveTable(table_serial_number, notify_player_flag);
	zstruct(watching_players[watching_player_index]);	// make seat vacant
	// Scroll the rest of the list down.
	int move_count = watching_player_count - watching_player_index;
	watching_player_count--;	// one fewer in list.
	if (watching_player_count < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) watching_player_count for table %d went negative! (%d)",_FL,watching_player_count);
		err = ERR_INTERNAL_ERROR;
	}
	if (move_count > 0) {
		memmove(&watching_players[watching_player_index],
				&watching_players[watching_player_index+1],
				move_count * sizeof(watching_players[0]));
	}

	LeaveCriticalSection(&TableCritSec);
	summary_info.watching_count = (BYTE8)min(255,watching_player_count);
	summary_info_changed = TRUE;
	FlagResendGameCommonDataToEveryone();	// send out new watching_count
	return err;
}

ErrorType Table::RemoveWatchingPlayer(Player *input_player_ptr)
{
	return RemoveWatchingPlayer(input_player_ptr, TRUE);
}	

ErrorType Table::RemoveWatchingPlayer(Player *input_player_ptr, int notify_player_flag)
{
	// Scan for this player's watching index then call the
	// main RemoveWatchingPlayer() function.
	EnterCriticalSection(&TableCritSec);
	for (int i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].status==2 && watching_players[i].player==input_player_ptr) {
			ErrorType err = RemoveWatchingPlayer(i, notify_player_flag);
			LeaveCriticalSection(&TableCritSec);
			return err;
		}
	}
	LeaveCriticalSection(&TableCritSec);
	return ERR_ERROR;	// player not found.
}

//*********************************************************
// https://github.com/kriskoin//
// Add a player to the list of player's this table is currently
// calling to empty seats from the waiting list(s).
// Must be called from the cardroom thread.  Does not grab critsecs.
// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
//
ErrorType Table::AddCalledPlayer(struct WaitListCallInfo *wlci)
{
	if (wait_list_called_player_count >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) tried to call %d players to a table all at once.",
					_FL, wait_list_called_player_count+1);
		return ERR_ERROR;
	}
	wait_list_called_players[wait_list_called_player_count] = *wlci;
	wait_list_called_player_count++;
	UpdatePlayerCount();

	// Tell that particular player object that they've been called.
	Player *p = CardRoomPtr->FindPlayer(wlci->player_id);
	if (p) {
		if (p->saved_seat_avail.table_serial_number && p->saved_seat_avail.timeout) {
			kp(("%s %s(%d) Warning: player $%08lx already was being called to table %d when table %d tried to call him\n",
					TimeStr(), _FL, wlci->player_id, p->saved_seat_avail.table_serial_number, table_serial_number));
		} else {
			p->saved_seat_avail.table_serial_number = table_serial_number;
			p->saved_seat_avail.timeout = (WORD16)WaitListTotalTimeout;	// set to anything non-zero for now.
		}
	}

	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Add a player to the list of player's this table is currently
// calling to empty seats from the waiting list(s).
// Must be called from the cardroom thread.  Does not grab critsecs.
// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
//
ErrorType Table::RemoveCalledPlayer(WORD32 player_id)
{
	struct WaitListCallInfo *wlci = wait_list_called_players;
	for (int i=0 ; i<wait_list_called_player_count ; i++, wlci++) {
		if (wlci->player_id == player_id) {
			// Found them.  Scroll the remaining ones down to fill the gap.
			for ( ; i < wait_list_called_player_count-1 ; i++, wlci++) {
				*wlci = *(wlci+1);
			}
			zstruct(*wlci);
			wait_list_called_player_count--;
			UpdatePlayerCount();
			return ERR_NONE;
		}
	}
	Error(ERR_INTERNAL_ERROR, "%s(%d) Could not remove player $%08lx from called player list because he's not there",
					_FL, player_id);
	return ERR_ERROR;
}

//*********************************************************
// https://github.com/kriskoin//
// Check if a player is currently being called to this table
// from a waiting list.
// Must be called from the cardroom thread.  Does not grab critsecs.
// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
// Returns FALSE if not being called, TRUE if they are being called.
//
int Table::CheckIfCalledPlayer(WORD32 player_id)
{
	struct WaitListCallInfo *wlci = wait_list_called_players;
	for (int i=0 ; i<wait_list_called_player_count ; i++, wlci++) {
		if (wlci->player_id == player_id) {
			// Found them.
			return TRUE;
		}
	}
	return FALSE;
}

//*********************************************************
// https://github.com/kriskoin//
// If the game has asked for a particular action and we have
// that action in our action_mask, fill in the first match to the
// input request.
// This is similar to the client's comm.cpp function of the same name.
// Returns: FALSE for no match, TRUE for match found and result sent out.
//
int Table::CheckAutoRespondToInput(int action_mask)
{
	struct GamePlayerInputRequest *request = game->GPIRequest;
	int matched_bits = request->action_mask & action_mask;
	if (matched_bits) {
		// We have a match...  Find first bit.
		int action = 0;
		while (!(matched_bits & (1<<action)) && action < 32) {
			action++;
		}
		// We found the match... fill in the GPIResult structure.
		struct GamePlayerInputResult *result = game->GPIResult;
		result->game_serial_number			= request->game_serial_number;
		result->table_serial_number			= request->table_serial_number;
		result->input_request_serial_number	= request->input_request_serial_number;
		result->seating_position			= request->seating_position;
		result->action = (BYTE8)action;
		result->ready_to_process = TRUE;
		request->ready_to_process = FALSE;
		return TRUE;	// match found and processed.
	}
	return FALSE;	// no match found.
}

//*********************************************************
// https://github.com/kriskoin//
// Process an input request destined for a human player.  If
// they have set a matching in-turn action, use it as a result,
// otherwise send it out to the player object for passing directly
// to the remote player.
//
void Table::ProcessGPIRequestForHuman(void)
{
	input_request_sent_time = SecondCounter;	// keep track of when it was sent
	int seat = game->GPIRequest->seating_position;
  #if 1	//kriskoin:   		// being left in so we don't screw something up right before the wedding.
	game->GPIRequest->game_serial_number = GameCommonData.game_serial_number;
	game->GPIRequest->table_serial_number = table_serial_number;
  #endif

	// Check if the player we want an action from has set an in-turn action.
	// If so, make sure it's for the current game state and that it matches the
	// possible actions we're requesting.
	WORD16 previous_last_input_request_serial_number = last_input_request_serial_numbers[seat];
	last_input_request_serial_numbers[seat] = game->GPIRequest->input_request_serial_number;

  #if 0	//kriskoin: 	kp1(("%s(%d) WARNING: IN-TURN ACTIONS ARE NOT HANDLED ON THE SERVER\n",_FL));
  #else	// normal... in-turn action processing on the server:
	// The client sets the in_turn_action_last_input_request_serial_number field
	// to be equal to the last real input request we sent him, therefore if
	// the number in the most recent ClientStateInfo structure we have received
	// from the client is OLDER than the last input we got from him
	// (previous_last_input_request_serial_number), we know the ClientStateInfo
	// record we have for the player is too old and we should not use it.
	struct ClientStateInfo *csi = players[seat].player->GetClientStateInfoPtr(table_serial_number);
	pr(("%s(%d) client in_turn_action = %d, action_amount = %d\n", _FL, csi->in_turn_action, csi->in_turn_action_amount));

	// If this is a tournament game and they have selected 'post/fold' (sit out),
	// look for that right here.
	// If they timed out this hand and THAT's why they are in post&fold mode,
	// we should NOT act on it yet (they may need the use of an all-in).
	if (tournament_table && csi && csi->sitting_out_flag && !players[seat].timed_out_this_hand) {
		// firstly, posting is handled elsewhere.  A tournament post request should
		// never ever get to this stage.
		// That leaves us with check or fold to handle.
		// Check/muck/toss are all same priority (they never show up at the same time).
		if (!CheckAutoRespondToInput(
					(1<<ACT_CHECK) |
					(1<<ACT_MUCK_HAND) |
					(1<<ACT_TOSS_HAND)))
		{
			// That failed, try something else
			// fold/show are same priority (they never show up at the same time).
			CheckAutoRespondToInput((1<<ACT_FOLD)|(1<<ACT_SHOW_HAND));
		}
	}

	// If the client has selected 'Fold.' any (fold no matter what), spoof the
	// client's in_turn_action_game_state to match our current game state.
	if (csi &&
		csi->in_turn_action==ACT_FOLD && csi->in_turn_action_amount == -1)
	{
		csi->in_turn_action_game_state = game->PlayerData[0]->game_state;
	}
	if (!game->GPIResult->ready_to_process &&	// nothing filled in yet?
		csi && 
		csi->in_turn_action_game_serial_number == GameCommonData.game_serial_number &&
		csi->in_turn_action_game_state == game->PlayerData[0]->game_state &&
		csi->in_turn_action_last_input_request_serial_number >= previous_last_input_request_serial_number &&
		csi->in_turn_action &&
		!players[seat].timed_out_this_hand)	// no in-turn actions allowed if they timed out already
	{
		pr(("%s(%d) Attempting to act on csi->in_turn_action of %d (action_amount = %d)...\n", _FL, csi->in_turn_action, csi->in_turn_action_amount));
		// We've got an in turn action for this player at this point in
		// the game... if it's one of the ones we can act on,
		// This code is similar to the client end in comm.cpp

	  #if 1
		//19991020: If they are running an older client, it does not yet support
		// the in_turn_action_amount so we should fill in some defaults for them.
		// Use 0 for ACT_FOLD (check/fold), use -1 for everything else (call/raise any).
		// The 1.03 (build 4) client was the first to support these.
		if (players[seat].player->client_version_number.build < 0x01030004) {
			//kp1(("%s(%d) Old client detected... spoofing in_turn_action_amount\n",_FL));
			if (csi->in_turn_action==ACT_FOLD) {
				csi->in_turn_action_amount = 0;
			} else {
				csi->in_turn_action_amount = -1;
			}
		}
	  #endif

		switch (csi->in_turn_action) {
		case ACT_FOLD:
			// First, try to check (if allowed)... if that fails, fold.
			if (csi->in_turn_action_amount != -1) {	// is checking allowed?
				if (CheckAutoRespondToInput(1<<ACT_CHECK)) {
					break;	// no more processing to do.
				}
			}
			if (CheckAutoRespondToInput(1<<ACT_FOLD)) {
				break;	// no more processing to do.
			}
			break;
		case ACT_RAISE:
			if (csi->in_turn_action_amount != -1 &&
				csi->in_turn_action_amount != game->GPIRequest->raise_amount)
			{	// the raise amount changed and they did not select 'any'... disallow.
				break;
			}
			if (CheckAutoRespondToInput(
					(1<<ACT_BET) |
					(1<<ACT_RAISE) |
					(1<<ACT_BET_ALL_IN) |
					(1<<ACT_RAISE_ALL_IN))
			) {
				break;	// no more processing to do.
			}
			// That didn't work... try to check/call (if pot was capped)...
			// fall through to the ACT_CALL handler.
		case ACT_CALL:
			if (csi->in_turn_action_amount != -1 &&
				csi->in_turn_action_amount != game->GPIRequest->call_amount)
			{	// the call amount changed and they did not select 'any'... disallow.
				break;
			}
			if (CheckAutoRespondToInput(
					(1<<ACT_CHECK) |
					(1<<ACT_CALL) |
					(1<<ACT_CALL_ALL_IN))
			) {
				break;	// no more processing to do.
			}
			break;
		}
	}

	// 24/01/01 kriskoin:
	// no reason to bother sending it to the client as he has no choice in the matter, player
	// must post the blind even if he's not there
	// we always need to send a cancel unless we specifically are told not to, such as with blinds
	int need_to_send_cancel = TRUE;
	if (!game->GPIResult->ready_to_process && tournament_table) {
		if (CheckAutoRespondToInput( 1<<ACT_POST_BB|ACT_POST_SB|ACT_POST_BOTH|ACT_POST )) {
			pr(("%s(%d) Tournament auto-response for POSTing (player %d)\n",
				_FL, game->GPIRequest->seating_position));
			need_to_send_cancel = FALSE;
		}
	}
	// If there is now a matching input result, it's clear it got acted on.
	if (game->GPIResult->ready_to_process &&
		game->GPIRequest->input_request_serial_number == game->GPIResult->input_request_serial_number)
	{
		pr(("%s(%d) Auto-responding on server end to input mask of $%04x with action of %d\n",
				_FL, game->GPIRequest->action_mask, game->GPIResult->action));
		resend_input_request_flag = FALSE;
		// Send an input request cancel to the player so they know the
		// latest input request serial number and to stop displaying their
		// checkboxes.
		// 24/01/01 kriskoin:
		if (need_to_send_cancel) {
			struct GamePlayerInputRequestCancel gpirc;
			zstruct(gpirc);
			gpirc.game_serial_number			= game->GPIRequest->game_serial_number;
			gpirc.input_request_serial_number	= game->GPIRequest->input_request_serial_number;
			gpirc.table_serial_number			= game->GPIRequest->table_serial_number;
		  #if 0	// 2022 kriskoin
			if (DebugFilterLevel <= 11) {
				if (!gpirc.table_serial_number) {
					kp(("%s %s(%d) WARNING: table %s, player %s, gpirc.table_serial_number==0, game s/n=%d\n",
							TimeStr(), _FL, summary_info.table_name,
							table_info.players[seat].name,
							gpirc.game_serial_number));
				}
			}
		  #endif
			players[seat].player->SendPlayerInputRequestCancel(&gpirc);
		}
		csi->in_turn_action = 0;	// always clear our local copy so it doesn't get acted on again.
		time_of_next_update = 0;	// update ourselves again asap
		return;
	}
  #endif

	// We've determined there are no in-turn actions... send it off to
	// the player object to send to the remote player.

	// adate: don't send it out if he's already timed out this hand
	if (!players[seat].timed_out_this_hand && players[seat].player->Connected())
	{
		players[seat].player->SendPlayerInputRequest(game->GPIRequest);
		if (resend_input_request_flag) {
			pr(("%s(%d) Just re-sent input request for table %d (game %d, input request %d) to player $%08lx\n",
					_FL, table_serial_number,
					game->GPIRequest->game_serial_number,
					game->GPIRequest->input_request_serial_number,
					players[seat].player->player_id));
		}
	} else {
		pr(("%s(%d) Table %d is waiting for input from disconnected player $%08lx\n",
					_FL, table_serial_number, players[seat].player->player_id));
	}
	resend_input_request_flag = FALSE;
}

//*********************************************************
// https://github.com/kriskoin//
// Select a bar snack for a player at this table.
//
void Table::SelectBarSnack(WORD32 player_id, BAR_SNACK bs)
{
	//kp(("%s(%d) SelectBarSnack($%08lx, %d)\n", _FL, player_id, bs));
	EnterCriticalSection(&TableCritSec);
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (GameCommonData.player_id[i]==player_id) {
			if (GameCommonData.bar_snack[i] != (BYTE8)bs) {
				// it changed... save it and send to everyone.
				GameCommonData.bar_snack[i] = (BYTE8)bs;
				//kp(("%s(%d) SelectBarSnack($%08lx, %d) saved it at seat position %d\n", _FL, player_id, bs, i));
				FlagResendGameCommonDataToEveryone();
			}
			break;
		}
	}
	LeaveCriticalSection(&TableCritSec);
}	

//*********************************************************
// https://github.com/kriskoin//
// Send the GameCommonData (if necessary) and the GamePlayerData
// to a particular player (not for watching players).
//
void Table::SendDataToPlayer(int *work_was_done_flag,
		int force_send_player_data_flag, int plrnum)
{
	int sent_common_data = FALSE;
  #if 0	// 2022 kriskoin
	if (summary_info.player_count) {
		kp(("%s(%d) GameCommonData.chips[%d] = %d, GamePlayerData.chips_bet_total[%d] = %d\n",
				_FL, plrnum, GameCommonData.chips[plrnum], plrnum, game ? game->PlayerData[plrnum]->chips_in_front_of_player[plrnum] : -1));
	}
  #endif
	GameCommonData.table_tourn_state = (BYTE8)table_tourn_state;    // make a copy for the client.
	GameCommonData.tournament_hand_number = (BYTE8)min(255,tournament_hand_number);

	if (players[plrnum].status==2 && !players[plrnum].game_common_data_sent) {
		//kp(("%s(%d) Sending GameCommonData to player %d\n", _FL, plrnum));
		players[plrnum].game_common_data_sent = TRUE;

		// Personalize a few fields...
		GameCommonData.sit_out_request_serial_num = players[plrnum].force_to_sit_out_serial_num;
	  #if 0	//kriskoin: 		GameCommonData.flags &= ~GCDF_PLAYER_SITTING_OUT;
		// If he's sitting out or if he has timed out this hand,
		// there's no chance he'll get another opportunity to act during this hand.
		if (players[plrnum].sitting_out_flag || players[plrnum].timed_out_this_hand) {
			GameCommonData.flags |= GCDF_PLAYER_SITTING_OUT;
		}
	  #endif

		players[plrnum].player->SendGameCommonData(&GameCommonData);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
		sent_common_data = TRUE;
	}
	if (sent_common_data || force_send_player_data_flag) {
		// Send player data as well (if we're in a game)
		if (players[plrnum].status==2) {				// human?
			if (game) {	// real game in progress?
				// yes, send real structure.
				game->PlayerData[plrnum]->table_serial_number = table_serial_number;
				game->PlayerData[plrnum]->disconnected_flags = disconnected_flags;
				pr(("%s(%d) Sending GamePlayerData to player %d (chips = %3d/%3d/%3d/%3d/%3d/%3d/%3d/%3d/%3d/%3d)\n",
						_FL, plrnum,
						game->PlayerData[plrnum]->chips_in_front_of_player[0],
						game->PlayerData[plrnum]->chips_in_front_of_player[1],
						game->PlayerData[plrnum]->chips_in_front_of_player[2],
						game->PlayerData[plrnum]->chips_in_front_of_player[3],
						game->PlayerData[plrnum]->chips_in_front_of_player[4],
						game->PlayerData[plrnum]->chips_in_front_of_player[5],
						game->PlayerData[plrnum]->chips_in_front_of_player[6],
						game->PlayerData[plrnum]->chips_in_front_of_player[7],
						game->PlayerData[plrnum]->chips_in_front_of_player[8],
						game->PlayerData[plrnum]->chips_in_front_of_player[9]));

			  #if 0	//kriskoin: 				// Force their client into sit-out mode if they timed out.
				if (players[plrnum].force_to_sit_out_flag) {
					//kp(("%s(%d) Setting  GPDF_FORCED_TO_SIT_OUT for player %d\n", _FL, plrnum));
					game->PlayerData[plrnum]->flags |= GPDF_FORCED_TO_SIT_OUT;
				} else {
					//kp(("%s(%d) Clearing GPDF_FORCED_TO_SIT_OUT for player %d\n", _FL, plrnum));
					game->PlayerData[plrnum]->flags &= ~GPDF_FORCED_TO_SIT_OUT;
				}
			  #endif
				players[plrnum].player->SendGamePlayerData(game->PlayerData[plrnum]);
			} else {	// maybe need to send pretournament structure or between-games gpd?
				// this is going to go out to this particular player
				struct GamePlayerData *gpd_to_send;
				if (tournament_table && table_tourn_state < TTS_PLAYING_GAME) {
					Tourn_FillPreTourneyGPD(&pre_tourney_gpd);
					gpd_to_send = &pre_tourney_gpd;
				} else {
					// send the last known common gpd.
					gpd_to_send = &table_gpd;
				}
				players[plrnum].player->SendGamePlayerData(gpd_to_send);
			}
		}
	}
}

//****************************************************************
// 
//
// Send game common data, player data, and input requests out to
// any players who need them.
//
ErrorType Table::SendDataToPlayers(int *work_was_done_flag, int force_send_player_data_flag)
{
	ErrorType err = ERR_NONE;

	// First, send the game common data to all players and watching players.
	GameCommonData.table_serial_number = table_serial_number;
  #if 0	// 2022 kriskoin
	GameCommonData.watching_count = (BYTE8)min(255,connected_watching_player_count);
	GameCommonData.total_connections = (BYTE8)min(255,((CardRoom *)cardroom)->active_player_connections);
  #endif
	BYTE8 flags = GameCommonData.flags;
	flags &= ~(GCDF_REAL_MONEY|GCDF_TOURNAMENT|GCDF_ONE_ON_ONE);
	if (chip_type == CT_REAL) {
		flags |= GCDF_REAL_MONEY;
	} else if (chip_type == CT_TOURNAMENT) {
		flags |= GCDF_TOURNAMENT;
	}
	if (max_number_of_players==2) {
		flags |= GCDF_ONE_ON_ONE;
	}
	GameCommonData.flags = flags;

  #if 1	// 2022 kriskoin
	UpdateTableInfoConnectionStatus();	// disconnected_flags is global to the table
  #else
	SetDisconnectedFlags();	// disconnected flags is global to the table
  #endif

	// If we're waiting for input from someone, send their data first
	// so it doesn't get queued up behind a burst of other data...
	int input_plr_num = -1;	// default to 'nobody'
	// Check if we need to send out any input requests...
	if (game && game->GPIRequest->ready_to_process &&
	  ((game->GPIRequest->input_request_serial_number != last_input_request_serial_number) ||
	   (game->GPIRequest->game_serial_number          != last_input_request_game_serial_number) ||
	    resend_input_request_flag))
	{
		// New input request has been generated.  Send it to the appropriate player.
		//kp(("%s(%d) Sending input request.\n",_FL));
		time_of_computerized_eval = 0;	// default to not asking the computer.
		last_input_request_ms = GetTickCount();
		int seat = game->GPIRequest->seating_position;
		if (seat < 0 || seat > MAX_PLAYERS_PER_GAME) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Illegal seating position in GamePlayerInputRequest (%d). Ignoring.", _FL, seat);
			err = ERR_INTERNAL_ERROR;
		} else if (!players[seat].status) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Empty seating position specified in GamePlayerInputRequest (%d). Ignoring.", _FL, seat);
			err = ERR_INTERNAL_ERROR;
		} else {
			input_plr_num = seat;
			SendDataToPlayer(work_was_done_flag, force_send_player_data_flag, seat);

			if (players[seat].status==1) {	// computer player?
				// Computer player needs to respond with input... ask game
				// module to fill in game->GPIResult in a few seconds.
				// If it's a post request, there's no delay.
				if (game->GPIRequest->action_mask & (
							(1<<ACT_POST) |
							(1<<ACT_POST_SB) |
							(1<<ACT_POST_BB) |
							(1<<ACT_POST_BOTH) |
							(1<<ACT_POST_ANTE) |
							(1<<ACT_POST_ALL_IN)
				)) {
					//kp(("%s(%d) computer posting with no delay.\n",_FL));
					time_of_computerized_eval = GetTickCount();
					time_of_next_update = 0;	// update asap
				} else {
					time_of_computerized_eval = GetTickCount() + ComputerPlayerAnswerDelay * 1000 + random(500);
					time_of_next_update = SecondCounter + ComputerPlayerAnswerDelay + 1;	// update asap
					pr(("%s(%d) Set time_of_computerized_eval to %d\n",_FL,time_of_computerized_eval));
				}
			} else {
				// Human player needs to respond with input...
				ProcessGPIRequestForHuman();
			}
		}
		// If this is a new request, reset our counters...
		//kp(("%s(%d) last_input_request_time is currently %d\n",_FL,last_input_request_time));
		if ((last_input_request_serial_number != game->GPIRequest->input_request_serial_number) ||
			(last_input_request_game_serial_number != game->GPIRequest->game_serial_number))
		{
			last_input_request_serial_number = game->GPIRequest->input_request_serial_number;
			last_input_request_game_serial_number = game->GPIRequest->game_serial_number;
			last_input_request_time = SecondCounter;	// keep track of when we sent it.
			//kp(("%s(%d) New input request: re-setting last_input_request_time to %d\n",_FL,last_input_request_time));
			first_input_timeout_warning_sent  = FALSE;
			second_input_timeout_warning_sent = FALSE;
			input_player_disconnected_msg_sent = FALSE;
			worst_connection_state_for_input_player = 0;
		}
	  #if 0	// 2022 kriskoin
		kp(("%s(%d) last_input_request_time is now %d\n",_FL,last_input_request_time));
		kp(("%s(%d) GPIRequest: s/n = %d, game = %d\n",
				_FL, game->GPIRequest->input_request_serial_number,
					 game->GPIRequest->game_serial_number));
	  #endif
		resend_input_request_flag = FALSE;
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
	}

	int i;
	// Do everyone else who is playing...
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (i==input_plr_num) {	// already sent to this player?
			continue;	// don't send again
		}
		SendDataToPlayer(work_was_done_flag, force_send_player_data_flag, i);
	}

	// Remove any personalized info for playing players
	GameCommonData.sit_out_request_serial_num = 0;
  #if 0	//kriskoin: 	GameCommonData.flags &= ~GCDF_PLAYER_SITTING_OUT;
  #endif

	// Now send to the watching players...
	for (i=0 ; i<watching_player_count ; i++) {
		int sent_common_data = FALSE;
		if (watching_players[i].status==2 && !watching_players[i].game_common_data_sent) {
			watching_players[i].game_common_data_sent = TRUE;

			//kp(("%s(%d) Sending GameCommonData to watching player #%d\n", _FL, i));
			watching_players[i].player->SendGameCommonData(&GameCommonData);
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
			sent_common_data = TRUE;
		}
		if (sent_common_data || force_send_player_data_flag) {
			// Send player data as well (if we're in a game)
			if (game && watching_players[i].status==2) {	// human?
				game->WatchingData->table_serial_number = table_serial_number;
				game->WatchingData->disconnected_flags = disconnected_flags;
				pr(("%s(%d) Sending GamePlayerData to plr %d (cards = %02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x/%02x%02x)\n",
						_FL, i,
						game->WatchingData->cards[0][0],game->WatchingData->cards[0][1],
						game->WatchingData->cards[1][0],game->WatchingData->cards[1][1],
						game->WatchingData->cards[2][0],game->WatchingData->cards[2][1],
						game->WatchingData->cards[3][0],game->WatchingData->cards[3][1],
						game->WatchingData->cards[4][0],game->WatchingData->cards[4][1],
						game->WatchingData->cards[5][0],game->WatchingData->cards[5][1],
						game->WatchingData->cards[6][0],game->WatchingData->cards[6][1],
						game->WatchingData->cards[7][0],game->WatchingData->cards[7][1],
						game->WatchingData->cards[8][0],game->WatchingData->cards[8][1],
						game->WatchingData->cards[9][0],game->WatchingData->cards[9][1]));
				watching_players[i].player->SendGamePlayerData(game->WatchingData);
			} else {	// maybe need to send pretournament structure or between-games gpd?
				// this is going to go out to this watching player
				struct GamePlayerData *gpd_to_send;
				if (tournament_table && table_tourn_state < TTS_PLAYING_GAME) {
					Tourn_FillPreTourneyGPD(&pre_tourney_gpd);
					gpd_to_send = &pre_tourney_gpd;
				} else {
					// send the last known common gpd.
					gpd_to_send = &table_gpd;
				}
				watching_players[i].player->SendGamePlayerData(gpd_to_send);
			}
		}
	}

	//MemTrackVMUsage(FALSE, "%s(%d) Table::SendDataToPlayers()",_FL);
	return err;
}

//****************************************************************
// https://github.com/kriskoin//
// Send a chat message out to all players at this table.
//
ErrorType Table::SendChatMessage(char *source_name, char *message, int text_type)
{
	return SendChatMessage(source_name, message, text_type, FALSE);	
}
ErrorType Table::SendChatMessage(char *source_name, char *message, int text_type, int watching_players_only)
{
	// First, build up the GameChatMessage structure...
	struct GameChatMessage gcm;
	zstruct(gcm);
	gcm.text_type = (BYTE8)text_type;	// see definition in pplib.h or GameChatMessage def in gamedata.h
	gcm.table_serial_number = table_serial_number;
	gcm.game_serial_number = GameCommonData.game_serial_number;
	strnncpy(gcm.name, source_name, MAX_COMMON_STRING_LEN);
	strnncpy(gcm.message, message, MAX_CHAT_MSG_LEN);

	EnterCriticalSection(&TableCritSec);
	int i;
	if (!watching_players_only) {
		// Send it out to each seated real player...
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			if (players[i].status==2) {			// human?
				players[i].player->SendDataStructure(DATATYPE_CHAT_MSG, &gcm, sizeof(gcm));
			}
		}
	}

	// Now send it out to anyone watching...
	for (i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].status==2) {	// human?
			watching_players[i].player->SendDataStructure(DATATYPE_CHAT_MSG, &gcm, sizeof(gcm));
		}
	}
	LeaveCriticalSection(&TableCritSec);
	return ERR_NONE;
}

//****************************************************************
// https://github.com/kriskoin//
// Send a chat message out from the dealer to all players at this table.
//
ErrorType Table::SendDealerMessage(char *message, int text_type)
{
	return SendDealerMessage(message, text_type, FALSE);
}

ErrorType Table::SendDealerMessage(char *message, int text_type, int watching_players_only)
{
	char summary_name[30];
	zstruct(summary_name);
	char *dealer_name = "Dealer";
	if (text_type==CHATTEXT_DEALER_WINNER && GameCommonData.game_serial_number) {
		strcpy(summary_name, "#");
		IntegerWithCommas(summary_name+strlen(summary_name), GameCommonData.game_serial_number);
		dealer_name = summary_name;
	}
	return SendChatMessage(dealer_name, message, text_type, watching_players_only);
}

ErrorType Table::SendDealerMessageToWatchingPlayers(char *message, int text_type)
{
	return SendDealerMessage(message, text_type, TRUE);
}	

#if 0	//kriskoin: /**********************************************************************************
 Function Table::SetDisconnectedFlags(void);
 date: 24/01/01 kriskoin Purpose: set the disconnected status flags for all players
***********************************************************************************/
void Table::SetDisconnectedFlags(void)
{
	// disconnected flags is global to the table
	WORD16 new_flags = 0;
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status==2 && !players[i].player->Connected()) {
			new_flags |= (WORD16)(1<<i);
		}
	}
	disconnected_flags = new_flags;
}
#endif

//****************************************************************
// https://github.com/kriskoin//
// This function actually resides with the table but it must be defined
// at a lower level so that the games can call it.  It simply takes a pointer
// to the relevant table and the message to be sent out to players at that table.
//
void Table_SendDealerMessage(void *table_ptr, char *message, int text_type)
{
	((Table *)table_ptr)->SendDealerMessage(message, text_type);
}

/**********************************************************************************
 Function Table_SendDataNow(void *table_ptr)
 date: kriskoin 2019/01/01 Purpose: force a send of data to all players on table
***********************************************************************************/
void Table_SendDataNow(void *table_ptr)
{
	int dummy_flag = TRUE;
	((Table *)table_ptr)->SendDataToPlayers(&dummy_flag, TRUE);
}

//*********************************************************
// https://github.com/kriskoin//
// Send dealer chat to any watching players explaining how to
// start a new table, sit down, join waiting lists, etc.
//
void Table::MakeDealerPromptToWatchingPlayers(void)
{
	int time_to_say_something = FALSE;
	if (!connected_watching_player_count)
		return;	// nobody connected and watching... nothing to say.

	if (SecondCounter - last_dealer_prompt_time >= 10 &&
		connected_watching_player_count > prev_dealer_prompt_watching_count) {
		time_to_say_something = TRUE;
	}
	if (SecondCounter - last_dealer_prompt_time >= 120) {
		time_to_say_something = TRUE;
	}
	if (game_type_was_disabled) {
		time_to_say_something = FALSE;
	}
	if (time_to_say_something) {
		// It's time to say something.
		prev_dealer_prompt_watching_count = connected_watching_player_count;
		last_dealer_prompt_time = SecondCounter;
        //kp(("%s(%d) chip_type = %d, table_tourn_state = %d\n", _FL, chip_type, table_tourn_state));
		if (chip_type==CT_TOURNAMENT && table_tourn_state!=TTS_WAITING) {
            // A tournament is under way... you can't sit down any longer.
            if (table_tourn_state==TTS_FINISHED) {
    		    SendDealerMessageToWatchingPlayers(
    				    "This tournament is finished. Join a waiting list to start a new tournament.",
					    CHATTEXT_DEALER_NORMAL);
            } else {
    		    SendDealerMessageToWatchingPlayers(
    				    "This tournament has started. Join a waiting list to start a new tournament.",
					    CHATTEXT_DEALER_NORMAL);
            }
		} else {
		    // There are a few different things we want to say, depending on
		    // whether a game is going, seats are available, or the table
		    // is full.
		    // Break it down into those three scenarios:
		    if (table_is_active == TABLE_ACTIVE_FALSE) {
			    // Table is not active... tell them how to start up a table.
			    SendDealerMessageToWatchingPlayers(
					    "To start a new table, you should join a waiting list or sit down.",
					    CHATTEXT_DEALER_NORMAL);
		    } else if (!tournament_table && (summary_info.player_count >= max_number_of_players ||  
										     GameCommonData.flags & GCDF_WAIT_LIST_REQUIRED) ) {
			    SendDealerMessageToWatchingPlayers(
					    "Click on 'Waiting List' to be called to sit at this table or a new one.",
					    CHATTEXT_DEALER_NORMAL);
		    } else {
			    // Table is active and not full
			    SendDealerMessageToWatchingPlayers(
					    "Pick a chair marked 'Seat Empty' to start playing.",
					    CHATTEXT_DEALER_NORMAL);
		    }
		}
	}
}

/**********************************************************************************
 Function SuppressTournSummaryEmail()
 date: 24/01/01 kriskoin Purpose: Suppress a Tournament Summary Email for this player for this tournament
***********************************************************************************/
void Table::SuppressTournSummaryEmail(WORD32 player_id, WORD32 tourn_serial_number)
{
	if (tourn_serial_number != table_serial_number) {
		kp(("%s(%d) Got a CancelTournEmail for table %d, but we are table %d\n",
			_FL, tourn_serial_number, table_serial_number));
		return;
	}
	// find this player id in our ttsi structure and flag if found
	for (int i=0; i < max_number_of_players; i++) {
		if (ttsi[i].player_id == player_id) {	// found him
			ttsi[i].dont_send_email = TRUE;
			pr(("%s(%d) Flagged player %08lx to NOT get Got a CancelTournEmail for tournament %d\n",
				_FL, player_id, tourn_serial_number));
			break;
		}
	}
}

/**********************************************************************************
 ErrorType Table::StartNewTournamentGameIfNecessary
 Date: 20180707 kriskoin :  Purpose: start a new tournament game if necessary
***********************************************************************************/
ErrorType Table::StartNewTournamentGameIfNecessary(int *work_was_done_flag)
{
	int i;	
	char str[100];
	zstruct(str);

	int player_can_play[MAX_PLAYERS_PER_GAME];
	zstruct(player_can_play);	// T/F if he's allowed to play this time around

	//kriskoin: 	// often.  It only needs to be called ONCE when a game is about to start.
	kp1(("%s(%d) Optimization note: Counting table players occurs WAY too often at tournament tables.\n", _FL));
	// Count number of valid players with enough money to play
	int valid_players = 0;				// how many will be included in this game?
	int well_connected_players = 0;		// how many have good connections?
	int not_sitting_out_players = 0;	// how many are not in post and fold mode?	
	for (i=0; i < MAX_PLAYERS_PER_GAME ;i++) {
		t_was_playing_last_hand[i] = FALSE;
		t_chips_last_hand[i] = 0;
		players[i].sitting_out_flag = FALSE;	// never allow sitting out of a game during tournament play
		players[i].timed_out_this_hand = FALSE;
		if (players[i].status) {	// player here?
			if (players[i].chips > 0) {	// he can play with anything he has
				// if they've got any money, good enough
				valid_players++;
				t_was_playing_last_hand[i] = TRUE;
				t_chips_last_hand[i] = players[i].chips;

				if (players[i].player) {	// got a player object?
					int player_table_index = players[i].player->SerialNumToJoinedTableIndex(table_serial_number);
					if (player_table_index >= 0) {
						if (!players[i].player->JoinedTables[player_table_index].client_state_info.sitting_out_flag) {
							not_sitting_out_players++;	// he's not in post and fold mode.
						}
					}
					if (players[i].player->CurrentConnectionState()==CONNECTION_STATE_GOOD) {
						well_connected_players++;
					}
				} else {	// must be a robot -- he counts as connected for testing purposes
					well_connected_players++;
					not_sitting_out_players++;	// he's not in post and fold mode.
				}
			}
		}
	}
  #if 1	// 2022 kriskoin
	if (valid_players) {
		kp(("%s %s(%d) valid_players = %2d, well connected players = %2d, players not in post and fold = %2d\n",
				TimeStr(), _FL, valid_players, well_connected_players, not_sitting_out_players));
	}
  #endif

	#define TOURNAMENT_STARTING_ANNOUNCE_INTERVAL	30
	
  #if TESTING_TOURNAMENTS
	#pragma message("TESTING_TOURNAMENTS is enabled (see top of gamedata.h)")
	#define DELAY_BEFORE_TTS_ANNOUNCE_START		0
	#define DELAY_BEFORE_TTS_MOVE_CHIPS_IN		0
	#define DELAY_BEFORE_TTS_CONVERT_CHIPS		0
	#define DELAY_BEFORE_TTS_MOVE_CHIPS_OUT		0
	#define DELAY_BEFORE_TTS_DEAL_HIGH_CARD		0
	#define DELAY_BEFORE_TTS_SHOW_HIGH_CARD		0
	#define DELAY_BEFORE_TTS_START_GAME			0
	#define DELAY_BEFORE_TTS_PLAYING_GAME		0
	#define DELAY_BEFORE_TTS_FINISHED			0
	#define DELAY_BEFORE_CLOSING_TABLE			10
  #else
	#define DELAY_BEFORE_TTS_ANNOUNCE_START		5
	#define DELAY_BEFORE_TTS_MOVE_CHIPS_IN		5
	#define DELAY_BEFORE_TTS_CONVERT_CHIPS		5
	#define DELAY_BEFORE_TTS_MOVE_CHIPS_OUT		5
	#define DELAY_BEFORE_TTS_DEAL_HIGH_CARD		5
	#define DELAY_BEFORE_TTS_SHOW_HIGH_CARD		5
	#define DELAY_BEFORE_TTS_START_GAME			5
	#define DELAY_BEFORE_TTS_PLAYING_GAME		5
	#define DELAY_BEFORE_TTS_FINISHED			5
	#define DELAY_BEFORE_CLOSING_TABLE			(4*60)
  #endif

	switch (table_tourn_state) {
	// all of the pre-tournament stuff is here
	case TTS_WAITING:	// tournament hasn't started yet
		{
			
			int ready_to_play = TRUE;	// default to start
			summary_info.tournament_state = TOURN_STATE_WAITING;
			// never start the tournament if the server is known to be shutting down at some point
			if (ShotClockDate && iShotClockETA && (ShotClockFlags & SCUF_SHUTDOWN_WHEN_DONE)) {
				if (SecondCounter >= time_no_tourn_starting_msg) {
					time_no_tourn_starting_msg = SecondCounter + 20;// no more than every 20 seconds
					SendDealerMessage("Tournaments will not begin until the server has restarted", CHATTEXT_ADMIN);
					SendDealerMessage("This table will be closed soon", CHATTEXT_ADMIN);
				}
				return ERR_NONE;
			}
			int seconds_since_contact[MAX_PLAYERS_PER_GAME];
			zstruct(seconds_since_contact);
			for (i=0; i < MAX_PLAYERS_PER_GAME ;i++) {
				if (players[i].player && players[i].status) {
					if (players[i].player->CurrentConnectionState() >= CONNECTION_STATE_BAD) {
						ready_to_play = FALSE; // can't start yet
						// if he's been disconnected too long, we may boot him
						seconds_since_contact[i] = players[i].player->TimeSinceLastContact();
					}
				}
			}
			// loop and boot as needed -- allowed 1 minute if we're full, 5 minutes if there are empty seats
			int allowable_seconds_disconnected = (valid_players == max_number_of_players ? (1*60) : (5*60) );
			for (i=0; i < MAX_PLAYERS_PER_GAME ;i++) {
				if (seconds_since_contact[i] > allowable_seconds_disconnected) {	// boot him
					// send email
					SDBRecord player_rec;	// the result structure
					zstruct(player_rec);
					char email_address[MAX_EMAIL_ADDRESS_LEN];
					zstruct(email_address);
					if (players[i].player && SDB->SearchDataBaseByPlayerID(players[i].player->player_id, &player_rec) >= 0) {
						strnncpy(email_address, player_rec.email_address, MAX_EMAIL_ADDRESS_LEN);
						if (email_address[0]) {
							char curr_str1[MAX_CURRENCY_STRING_LEN];
							char curr_str2[MAX_CURRENCY_STRING_LEN];
							zstruct(curr_str1);
							zstruct(curr_str2);
						EmailStr(email_address,
								"Desert Poker",
								"support@kkrekop.io",
								"Tournament Refund",
								NULL,
								"You were entered in a tournament but were disconnected for too long\n"
								"before it started and were picked up from the table.\n"
								"\n"
								"Your tournament Buy-In of %s and tournament fee of %s have been\n"
								"refunded to your account.\n"
								"\n"
								"On your behalf, we removed you from the table before the tournament\n"
								"started as you were disconnected at the time.  This prevented risking\n"
								"you from having your hand folded every game automatically had you\n"
								"stayed at the table.\n"
								"\n"
								"----\n"
								"This email was computer generated and emailed to %s\n"
								"If you have any questions, please see the tournament web page at\n"
								"http://www.kkrekop.io/tournaments.html\n",
									CurrencyString(curr_str1, big_blind_amount, CT_REAL),
									CurrencyString(curr_str2, small_blind_amount, CT_REAL),
									players[i].player->user_id);
						}
					}
					// send a dealer message
					zstruct(str);
					sprintf(str, "%s has been picked up from the table for being disconnected too long",
						players[i].player->user_id);
					SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);

					// boot player
					RemovePlayerFromTable(i);
					// adjust for dealer messages
					valid_players = max(0, valid_players-1);	// avoid negative number in bizarre situation
				}
			}
			// we might be ready to start the tournament
			if (ready_to_play && valid_players == max_number_of_players) {	// ready to start the tournament
			  #if 0 // 224/01/01 kriskoin:
				sprintf(str, "We have  %d players... play will now commence!", valid_players);
			  #else
				sprintf(str, "Play will now commence!");
			  #endif
				SendDealerMessage(str, CHATTEXT_ADMIN);
				time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_ANNOUNCE_START;
				table_tourn_state = TTS_ANNOUNCE_START;
				if (CardRoomPtr) {
					CardRoomPtr->tournaments_played_today++;
				}
			} else {
				if (SecondCounter - last_start_time_announce >= TOURNAMENT_STARTING_ANNOUNCE_INTERVAL) {
					last_start_time_announce = SecondCounter;
					int players_missing = max_number_of_players - valid_players;
					// 24/01/01 kriskoin:
					if (valid_players == max_number_of_players) {	// there are disconnected players
						sprintf(str, "We've got %d players, however some are not connected.", valid_players);
						SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
						sprintf(str, "We will wait a little while longer for them to return");
						SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
					} else {	// not enough players yet
						sprintf(str, "We are still waiting for %d player%s",
							players_missing,
							players_missing == 1 ? "" : "s");
						SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
					}
				}
			}
		}
		return ERR_NONE;

	case TTS_ANNOUNCE_START:
		//kp(("TTS_ANNOUNCE_START\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
            GameCommonData.flags |= GCDF_TOURNAMENT_HAS_STARTED;
			GameCommonData.flags &= ~GCDF_WAIT_LIST_REQUIRED;
			summary_info.flags &= ~TSIF_WAIT_LIST_REQUIRED;
			summary_info.tournament_state = TOURN_STATE_PLAYING;
        	summary_info_changed = TRUE;    // indicate we changed the summary_info structure.
		  #if 0			
			SendDealerMessage("** Tournament Starting **", CHATTEXT_DEALER_NORMAL);
		  #endif
			// preserve all names and IDs
			for (i=0; i < max_number_of_players; i++) {
				strnncpy(ttsi[i].name, GameCommonData.name[i], MAX_COMMON_STRING_LEN);
				ttsi[i].player_id = GameCommonData.player_id[i];
			}
			// begin summary email
			if (!iRunningLiveFlag) {
				Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "*** THIS IS A TEST -- NOT A REAL TOURNAMENT RESULT ***\n\n");
			}
			Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "** ADMIN SUMMARY **\n");
			char *GameRuleNames[MAX_GAME_RULES] = {
				"Texas Hold'em",
				"Omaha Hi",
				"Omaha Hi/Lo 8 or better",
				"7-Card Stud",
				"7-Card Stud Hi/Lo 8 or better",
			};
			Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "%s Tournament Summary for table \"%s\"\n",
				GameRuleNames[game_rules - GAME_RULES_START],
				summary_info.table_name);
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_MOVE_CHIPS_IN;
			table_tourn_state = TTS_MOVE_CHIPS_IN;
		}
		return ERR_NONE;
		
	case TTS_MOVE_CHIPS_IN:
		//kp(("TTS_MOVE_CHIPS_IN\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			Tourn_FillPreTourneyGPD(&pre_tourney_gpd);
			pre_tourney_gpd.pot[0] = 0;
			// move everyone's chips into the middle pot
			for (i=0; i < max_number_of_players; i++) {
			  #if 0	// 2022 kriskoin
				if (players[i].chips != pre_tourney_gpd.chips_in_front_of_player[i]) {
					kp(("%s %s(%d) ERROR: table %s players[%d].chips != pre_tourney_gpd.chips_in_front_of_player[%d] (%d vs %d)\n",
							TimeStr(), _FL,
							summary_info.table_name,
							i, i,
							players[i].chips, pre_tourney_gpd.chips_in_front_of_player[i]));
				}
			  #endif
				int chips_being_added_to_pot =
						players[i].chips - small_blind_amount;	// take off fee
				ServerVars.gross_tournament_buyins += players[i].chips;

				if (chips_being_added_to_pot <= 0) {
					kp(("%s %s(%d) ERROR: table %s chips_being_added_to_pot for seat %d is %d (%d - %d)\n",
								TimeStr(), _FL,
								summary_info.table_name,
								i,
								chips_being_added_to_pot,
								players[i].chips,
								small_blind_amount));
					chips_being_added_to_pot = 0;
				}
				pre_tourney_gpd.pot[0] += chips_being_added_to_pot;
				tournament_prize_pool += chips_being_added_to_pot;
				pre_tourney_gpd.chips_in_front_of_player[i]	= 0;
				pr(("%s(%d) chips_being_added_to_pot from player %d = %d (%d - %d)\n",
							_FL,
							i,
							chips_being_added_to_pot,
							pre_tourney_gpd.chips_in_front_of_player[i],
							small_blind_amount));
			}
			char curr_str1[MAX_CURRENCY_STRING_LEN];
			char curr_str2[MAX_CURRENCY_STRING_LEN];
			char curr_str3[MAX_CURRENCY_STRING_LEN];
			zstruct(curr_str1);
			zstruct(curr_str2);
			zstruct(curr_str3);
			tournament_buyin = big_blind_amount;
			Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "%s Prize Pool, %s Buy-In, %s Fee, %d players\n\n",
				CurrencyString(curr_str1, tournament_prize_pool, CT_REAL),
				CurrencyString(curr_str2, big_blind_amount, CT_REAL),
				CurrencyString(curr_str3, small_blind_amount, CT_REAL),
				valid_players);			
			Tourn_SendPreTourneyGPD();
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_CONVERT_CHIPS;
			table_tourn_state = TTS_CONVERT_CHIPS;
		}
		return ERR_NONE;

	case TTS_CONVERT_CHIPS:
		//kp(("TTS_CONVERT_CHIPS\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			pre_tourney_gpd.pot[0] = 0;
			for (i=0; i < max_number_of_players; i++) {
				// for the summary emails, tell him current balance and all that.  note that the
				// buy in from the table has already been removed from his account
				char curr_str1[MAX_CURRENCY_STRING_LEN];
				char curr_str2[MAX_CURRENCY_STRING_LEN];
				char curr_str3[MAX_CURRENCY_STRING_LEN];
				char curr_str4[MAX_CURRENCY_STRING_LEN];
				zstruct(curr_str1);
				zstruct(curr_str2);
				zstruct(curr_str3);
				zstruct(curr_str4);
				ttsi[i].starting_account_balance = SDB->GetChipsInBankForPlayerID(table_info.players[i].player_id, CT_REAL);
				Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "%s: ", GameCommonData.name[i]);
				Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "Balance before: %s | "
					"Buy-In: %s + %s Fee | "
					"Balance after Buy-In: %s"
					"\n",
					CurrencyString(curr_str1, ttsi[i].starting_account_balance+small_blind_amount+big_blind_amount, CT_REAL),
					CurrencyString(curr_str2, big_blind_amount, CT_REAL),
					CurrencyString(curr_str3, small_blind_amount, CT_REAL),
					CurrencyString(curr_str4, ttsi[i].starting_account_balance, CT_REAL));
				Tourn_TextForSummaryEmail(i, "Available account balance before Buy-In: %s\n"
					"Tournament Buy-In: %s + %s Fee\n"
					"Available account balance after Buy-In: %s\n"
					"These balances do not include money in play at other tables (if applicable).\n"
					"\n",
					CurrencyString(curr_str1, ttsi[i].starting_account_balance+small_blind_amount+big_blind_amount, CT_REAL),
					CurrencyString(curr_str2, big_blind_amount, CT_REAL),
					CurrencyString(curr_str3, small_blind_amount, CT_REAL),
					CurrencyString(curr_str4, ttsi[i].starting_account_balance, CT_REAL));

				// if he's here, he's already got enough chips in play for the conversion
				if (players[i].status) {	// should be someone always there
					pre_tourney_gpd.pot[0] += STARTING_TOURNAMENT_CHIPS;
				}
				GameCommonData.chips[i] = 0;
				table_info.players[i].chips = 0;
			}
			// unmask the display flag so chips show up as T-chips now
			GameCommonData.big_blind_amount = STARTING_TOURNAMENT_CHIPS/2;
			GameCommonData.flags &= ~GCDF_USE_REAL_MONEY_CHIPS;
			FlagResendGameCommonDataToEveryone();
			Tourn_SendPreTourneyGPD();
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_MOVE_CHIPS_OUT;
			table_tourn_state = TTS_MOVE_CHIPS_OUT;
    		table_info_changed = TRUE;
		}
		return ERR_NONE;

	case TTS_MOVE_CHIPS_OUT:
		//kp(("TTS_MOVE_CHIPS_OUT\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			tournament_total_tourney_chips = pre_tourney_gpd.pot[0];
			pre_tourney_gpd.pot[0] = 0;
			pre_tourney_gpd.s_gameover = GAMEOVER_TRUE;	// force animation
			for (i=0; i < max_number_of_players; i++) {
				ErrorType rc = SDB->TournamentBuyIn(table_info.players[i].player_id, table_serial_number,
					big_blind_amount, small_blind_amount, 0, tournament_total_tourney_chips);
				if (rc != ERR_NONE) {
					Error(ERR_ERROR, "%s(%d) TournamentBuyIn() returned ERROR for Player 0x%08lx -- see src, this will have to be investigated",
						_FL, table_info.players[i].player_id);
				}
				// if he's here, he's already got enough chips in play for the conversion
				ttsi[i].buy_in = big_blind_amount;
				players[i].chips =
					SDB->GetChipsInPlayForPlayerID(table_info.players[i].player_id, CT_TOURNAMENT);
				GameCommonData.chips[i] = players[i].chips;
				pre_tourney_gpd.chips_won[i] = players[i].chips;
        		table_info.players[i].chips = players[i].chips;	// keep summary info up to date
			}
			FlagResendGameCommonDataToEveryone();
			Tourn_SendPreTourneyGPD();
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_DEAL_HIGH_CARD;
			table_tourn_state = TTS_DEAL_HIGH_CARD;
    		table_info_changed = TRUE;
		}
		return ERR_NONE;
		
	case TTS_DEAL_HIGH_CARD:
		//kp(("TTS_DEAL_HIGH_CARD\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			for (i=0; i < max_number_of_players; i++) {
				// clear chips
				GameCommonData.chips[i] = 0;
				pre_tourney_gpd.chips_won[i] = 0;
			}
			pre_tourney_gpd.s_gameover = GAMEOVER_FALSE;	// force animation to clear chips in front
			Tourn_SendPreTourneyGPD();
			SendDealerMessage("High card is the button", CHATTEXT_DEALER_WINNER);
			// animate single cards and select button			
			tournament_button_override = Tourn_SetInitialButton();
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_SHOW_HIGH_CARD;
			table_tourn_state = TTS_SHOW_HIGH_CARD;
		}
		return ERR_NONE;

	case TTS_SHOW_HIGH_CARD:
		//kp(("TTS_HIGH_CARD\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			zstruct(str);
			char szSuits[4][10] ={	"Clubs", "Diamonds", "Hearts", "Spades" } ;
			char szRanks[13][7] ={	"Two", "Three", "Four", "Five", "Six", "Seven", "Eight",
									"Nine", "Ten", "Jack", "Queen", "King", "Ace" } ;

			sprintf(str, "%s is the button with the %s of %s",
				GameCommonData.name[tournament_button_override],
				szRanks[RANK(high_card)],
				szSuits[SUIT(high_card)]);
			SendDealerMessage(str, CHATTEXT_DEALER_WINNER);
			for (i=0; i < max_number_of_players; i++) {
				// clear all but high card
				if (pre_tourney_gpd.cards[i][0] != high_card) {
					pre_tourney_gpd.cards[i][0] = CARD_HIDDEN;
				}
			}
			Tourn_SendPreTourneyGPD();
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_TTS_START_GAME;
			table_tourn_state = TTS_START_GAME;
		}
		return ERR_NONE;
	
	case TTS_START_GAME:
		//kp(("TTS_START_GAME\n"));
		if (SecondCounter >= time_of_next_tournament_stage) {
			// tournament is starting now
			tournament_hand_number = 1;	// this gets incremented in HandleGameOver()
			summary_info.tournament_hand_number = (BYTE8)min(255,tournament_hand_number);
			next_game_start_time = SecondCounter;
			table_tourn_state = TTS_PLAYING_GAME;
			SendDealerMessage(
					"The current Level and Game are displayed in the top right corner.",
					CHATTEXT_ADMIN);
		}
		return ERR_NONE;
	
	case TTS_PLAYING_GAME:	// we're between hands of a tournament
		//kp(("TTS_PLAYING_GAME\n"));
        // deal with emergency server shutdown
		if (iShutdownAfterGamesCompletedFlag ||
			(ShotClockFlags & SCUF_EMERGENCY_TOURNAMENT_SHUTDOWN))
		{
			Tourn_CancelTournamentInProgress();
			table_tourn_state = TTS_FINISHED;       // jump right to the end if we ever get back here
			return ERR_NONE;
		}
		// we're playing -- set the blinds
        int small_blind_chips, big_blind_chips;
		if (tournament_hand_number >= 160) {		// undocumented level 10
			small_blind_chips = 3000;
			big_blind_chips = 6000;
		} else if (tournament_hand_number >= 150) {	// undocumented level 9
			small_blind_chips = 1000;
			big_blind_chips = 2000;
		} else if (tournament_hand_number >= 71) {
			small_blind_chips = 500;
			big_blind_chips = 1000;
		} else if (tournament_hand_number >= 61) {
			small_blind_chips = 300;
			big_blind_chips = 600;
		} else if (tournament_hand_number >= 51) {
			small_blind_chips = 200;
			big_blind_chips = 400;
		} else if (tournament_hand_number >= 41) {
			small_blind_chips = 100;
			big_blind_chips = 200;
		} else if (tournament_hand_number >= 31) {
			small_blind_chips = 50;
			big_blind_chips = 100;
		} else if (tournament_hand_number >= 21) {
			small_blind_chips = 25;
			big_blind_chips = 50;
		} else if (tournament_hand_number >= 11) {
			small_blind_chips = 15;
			big_blind_chips = 25;
		} else {
			// first 10 hands
		  #if 0	//20000903MB (testing only)
		  	if (tournament_hand_number & 1) {
				small_blind_chips = 25;
				big_blind_chips = 50;
		  	} else {
				small_blind_chips = 300;
				big_blind_chips = 600;
		  	}
		  #else
			small_blind_chips = 10;
			big_blind_chips = 15;
		  #endif
		}
		// convert from chips to pennies
		small_blind_amount = small_blind_chips * 100;
		big_blind_amount = big_blind_chips * 100;
		// announce limits and blinds has been moved till after the game begins
		// fall through to starting the game
		break;
	
	case TTS_WRAP_UP:	// any final things to do
		//kp(("TTS_WRAP_UP\n"));
		SendDealerMessage("** The tournament is over **", CHATTEXT_DEALER_NORMAL);
		Tourn_SendEmails();	// send summary emails
		table_tourn_state = TTS_FINISHED;	// no more to do
		return ERR_NONE;

	case TTS_FINISHED:	// tournament has ended, wait for the table to go away
		//kp(("TTS_FINISHED\n"));
		//kp(("%s(%d) Processing TTS_FINISHED\n", _FL));
		if (summary_info.tournament_state != TOURN_STATE_FINISHED) {
			summary_info.tournament_state  = TOURN_STATE_FINISHED;
	       	summary_info_changed = TRUE;    // indicate we changed the summary_info structure.
	   		table_info_changed = TRUE;
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_CLOSING_TABLE;
		}
		if (SecondCounter >= time_of_next_tournament_stage) {
			// Time to close the table.
			// Boot everyone off.  Cardroom will handle the closing of the table.
			//kp(("%s %s(%d) Time to close this tournament table.\n", TimeStr(), _FL));
			for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
				if (players[i].status) {	// someone there?
					RemovePlayerFromTable(i);
				}
			}
			
			time_of_next_tournament_stage = SecondCounter + DELAY_BEFORE_CLOSING_TABLE;	// try again in n minutes.
		}
		return ERR_NONE;	// nothing left to do this time around

	default:
		Error(ERR_ERROR, "%s(%d) Unknown TTS_* state (%d)", _FL, table_tourn_state);
		return ERR_ERROR;
	}

  #if 1	// 2022 kriskoin
	// In tournaments, impose a minimum time between games when some of the players
	// have connection problems...
	if (!well_connected_players || !not_sitting_out_players) {
		// Either everyone has a bad connection or everyone is sitting out.  DO NOT
		// start this game.
		return ERR_NONE;	// nothing left to do this time around
	}
	int min_good_players = (valid_players + 1) / 2;
	if (well_connected_players <= min_good_players || not_sitting_out_players <= min_good_players) {
		// Impose a minimum time between games.
		next_game_start_time = max(next_game_start_time, last_game_end_time + 20);
	}
  #endif

	// do all the rest of the common game launch
	return SNG_LaunchIfNeccessary(work_was_done_flag, valid_players);
}

/**********************************************************************************
 Function Table::AnnounceSpecialGame(void)
 date: 24/01/01 kriskoin Purpose: on a special game number, announce to everyone (and particular table)
***********************************************************************************/
void Table::AnnounceSpecialGame(void)
{
	char msg[100];
	zstruct(msg);
	char str2[30];
	zstruct(str2);
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	char *game_names[MAX_GAME_RULES] = {
		"Hold'em",
		"Omaha Hi",
		"Omaha H/L8",
		"7CS",
		"7CS H/L8",
	};
	// send message to all tables that this game is starting (and where)
	sprintf(msg, "A game of #%s has just begun at table %s (%s/%s %s)",
		IntegerWithCommas(curr_str1, GameCommonData.game_serial_number),
		summary_info.table_name,
		CurrencyString(curr_str2, big_blind_amount * GameStakesMultipliers[game_rules - GAME_RULES_START], chip_type),
		CurrencyString(curr_str3, big_blind_amount*2*GameStakesMultipliers[game_rules - GAME_RULES_START], chip_type),
		game_names[game_rules - GAME_RULES_START]);
	CardRoomPtr->SendDealerMessageToAllTables(msg, CHATTEXT_ADMIN, 0);	// send it to everyone
	kp(("%s Dealer: %s\n", TimeStr(), msg));

	// send congratulations to this table
	sprintf(msg, "****CONGRATULATIONS! This is game #%s****",
		IntegerWithCommas(str2, SpecialHandNumber));
	SendDealerMessage(msg, CHATTEXT_ADMIN);
	kp(("%s Dealer: %s\n", TimeStr(), msg));

	// tell everyone here how much they've won (at least)
	sprintf(msg, "*** Everyone playing in this hand will win %s ***", CurrencyString(curr_str1, SpecialHandPrize, CT_REAL));
	SendDealerMessage(msg, CHATTEXT_ADMIN);
	kp(("%s Dealer: %s\n", TimeStr(), msg));
}

/**********************************************************************************
 int Table::TournamentTable(void);
 Date: 20180707 kriskoin :  Purpose: T/F if this is/isn't a tournament table
***********************************************************************************/
int Table::TournamentTable(void)
{
	return tournament_table;
}

//****************************************************************
// https://github.com/kriskoin//
// Handle the various game over issues, including deleting the game object.
//
ErrorType Table::HandleGameOver(void)
{
	// 24/01/01 kriskoin:
	button_seat = GameCommonData.p_button;
	//MemTrackVMUsage(FALSE, "%s(%d) Table::HandleGameOver() (top)",_FL);
	// Handle game over accounting...
	int chips_total_checksum = game->PlayerData[0]->rake_total;
	// the table will pay back ecash credits if it raked and is for real money
  #if 1 // 24/01/01 kriskoin:
	int credit_ecash = (chip_type == CT_REAL && game->PlayerData[0]->rake_total >= 100);	// must rake $1
  #else
	int credit_ecash = (chip_type == CT_REAL && game->PlayerData[0]->rake_total);	// any rake qualifies
  #endif

	int i;
	int filled_seats = 0;	// how many seats have someone in them?
	int played_this_hand[MAX_PLAYERS_PER_GAME];	// T/F if they played in this last hand
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status) {
			filled_seats++;	// another seat has someone in it.
		}

		WORD32 chips_net_change = game->PlayerData[i]->chips_won[i] - game->PlayerData[i]->chips_bet_total[i];
		players[i].chips += chips_net_change;

		// we want to know if this player has ever played a hand
		if (game->PlayerData[0]->player_status[i] == PLAYER_STATUS_PLAYING ||
			game->PlayerData[0]->player_status[i] == PLAYER_STATUS_FOLDED ||
			game->PlayerData[0]->player_status[i] == PLAYER_STATUS_ALL_IN)
		{
			players[i].played_a_hand = TRUE;
			played_this_hand[i] = TRUE;
		} else {
			played_this_hand[i] = FALSE;
		}
	  #if POT_DEBUG
		kp(("%s(%d) player %d had a net change of %d chips\n",
			_FL, i, chips_net_change));
	  #endif
		table_info.players[i].chips = players[i].chips;	// keep summary info up to date
		table_info_changed = TRUE;
		chips_total_checksum += chips_net_change;

		// as of most recent discussions, ecash fee crediting works like this:
		// we get one point per game participated.  when the points hit 100, we
		// move $5.00 from credit to cash for the player

		// credit back ecash if this player was dealt in and the game was eligible
		#define ECASH_CHUNK_CREDIT			500	// $5.00 at a time (per points needed)
		#define ECASH_CHUNK_CREDIT_POINTS	100	// points needed to credit a chunk
		if (credit_ecash) {
			if (played_this_hand[i]) {	// participated?
				WORD32 player_id = GameCommonData.player_id[i];
				SDB->AddToGoodRakedGames(player_id, 1);
				// we only credit if he's got a pending refund balance
				// !!! This is really inefficient database access !!!


                                // kriskoin : i don't use PendingCredit in this moment, i just use
                                //the CreditFeePoints to caculate the $1 rake games he played,
                                //in order to enbale his cashout function when he earned enough
                                //points
          #if 0 //marked by rgong
				if (SDB->GetPendingCreditForPlayerID(player_id) > 0) {	// he does
					SDB->AddToCreditFeePoints(player_id, 1);
					if (SDB->GetCreditFeePoints(player_id) >= ECASH_CHUNK_CREDIT_POINTS) {
						// move the credit
						SDB->MoveEcashCreditToCash(player_id, ECASH_CHUNK_CREDIT);
						// log it to the ATL file
						PL->LogFinancialTransfer(LOGTYPE_CC_FEE_REFUND, player_id, player_id,
							ECASH_CHUNK_CREDIT, 1, 0, CT_REAL, "CC fee refund");// 1 = pending fee refund field, 0 = cash avail
						// blank the points
						SDB->ClearCreditFeePoints(player_id);
					}
				}
			#endif
				// end kriskoin 

#if 0 //cris 11-02-2004				
                // kriskoin : deposit bonus and enable cashout control
                int total_epoints;

				
                total_epoints = SDB->GetCreditFeePoints(player_id);
				if(total_epoints > 1){
                       SDB->AddToCreditFeePoints(player_id, -1);
				} else {
					if(total_epoints>0){
                       SDB->AddToCreditFeePoints(player_id, -1);
                       EnableCashoutForPlayer(player_id);
					}
				}
#endif //cris 11-02-2004	
				
				
#if 0 //cris 11-02-2004		
				// kriskoin  03/15/2002, use good_raked_games for ecash_credit_points
				//since credit_fee_points has been used by promotion function
				int cur_pending_credit = SDB->GetPendingCreditForPlayerID(player_id);
						
				/*
				if (cur_pending_credit > 0){
                                    SDB->AddToGoodRakedGames(player_id, 1);
				    if (SDB->GetGoodRakedGames(player_id)>=ECASH_CHUNK_CREDIT_POINTS){
					//SDB->AddToGoodRakedGames(player_id, -ECASH_CHUNK_CREDIT_POINTS);
					//SDB->MoveEcashCreditToCash(player_id, ECASH_CHUNK_CREDIT);
						if (cur_pending_credit > ECASH_CHUNK_CREDIT){
						PL->LogFinancialTransfer(LOGTYPE_CC_FEE_REFUND, player_id, player_id,
							ECASH_CHUNK_CREDIT, 1, 0, CT_REAL, "CC fee refund");
						// 1 = pending fee refund field, 0 = cash avail
						} else {
						PL->LogFinancialTransfer(LOGTYPE_CC_FEE_REFUND, player_id, player_id,
							cur_pending_credit, 1, 0, CT_REAL, "CC fee refund");
						// 1 = pending fee refund field, 0 = cash avail
						}
 				        players[i].player->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
            					"Your Credit Pending Fee Has been Refunded.");
                                	kp(("%s(%d) player_id(%d), credit fee refund\n", \
						_FL, player_id));

				    }//f (SDB->GetGoodRakedGames(player_id)>=ECAS
				}	//if (cur_pending_credit > 0 */
				// end kriskoin  03/15/2002
                                total_epoints = SDB->GetCreditFeePoints(player_id);
                                //kp(("%s(%d) player_id(%d), games_to_play(%d)\n", \
					_FL, player_id, total_epoints));
                                // end kriskoin 
#endif //cris 11-02-2004								
			}
		}

		if (played_this_hand[i] && players[i].player) {
  	// we want to notify the player if he's there
			
			players[i].player->ChipBalancesHaveChanged();
		}
	}
	if (chips_total_checksum != 0) {
		Error(ERR_FATAL_ERROR, "%s(%d) Accounting error: chips_total_checksum = %d (should be zero)", _FL, chips_total_checksum);
	}

	// tell the database to count its total chips -- get an error if total chips has changed
	SDB->CountAllChips(GameCommonData.game_serial_number);

	WORD32 time_between_these_games = TimeBetweenGames;
	if (game_rules == GAME_RULES_OMAHA_HI_LO || game_rules==GAME_RULES_STUD7_HI_LO) {
		time_between_these_games = TimeBetweenHiLoGames;
		// If no low qualified and there was only a high winner,
		// use an alternate delay between games
		if (*game->pHiPotWinners==1 && *game->pLoPotWinners==0) {
			time_between_these_games = TimeBetweenHiLoGames_HiOnly;
		}
	}
	if (*game->alternate_delay_before_next_game) {
		time_between_these_games = min(time_between_these_games, *game->alternate_delay_before_next_game);
	}

	//kriskoin: 	// impose a minimum time between hands.
	if (dwLastWidespreadRoutingProblem &&
					(SecondCounter - dwLastWidespreadRoutingProblem) < 5*60)
	{
		time_between_these_games = max(time_between_these_games, 12);
	}

	//kp(("%s(%d) time_between_these_games = %d\n", _FL, time_between_these_games));
		
	next_game_start_time = SecondCounter + time_between_these_games;	// specify earliest time a new game can start.

	// we are told whether the next game should have the button move or not
	move_button_next_game = ( game->PlayerData[0]->s_gameover == GAMEOVER_MOVEBUTTON );

	// Keep track of who the big blind was so we can tell the next game
	// where to start.
	previous_big_blind_seat = game->PlayerData[0]->p_big_blind;

	// Update the post_needed values for any players who were in that game...
	// (so we have them available for the next game).
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		// we only set the post_needed flag is we know what it's supposed to be
		if (GameCommonData.post_needed[i] != POST_NEEDED_UNKNOWN) {
			players[i].post_needed = GameCommonData.post_needed[i];
		}
		// adate: if he's never played a hand, all we ever want from him is the
		// initial post (as per discussion with MM)
		if (!players[i].played_a_hand) {
			players[i].post_needed = POST_NEEDED_INITIAL;
		}

		// If the big blind has gone past this guy and he did not post,
		// increment his missed_blind_count.
		if (game_rules == GAME_RULES_STUD7 || game_rules==GAME_RULES_STUD7_HI_LO) {	// different for 7cs
			if (game->PlayerData[i]->player_status[i]==PLAYER_STATUS_SITTING_OUT ||
			  // 24/01/01 kriskoin:
			  game->PlayerData[i]->player_status[i]==PLAYER_STATUS_NOT_ENOUGH_MONEY) {
				// this guy has missed another blind.
				players[i].missed_blind_count++;
			} else {
				// he played, so clear them
				players[i].missed_blind_count=0;
			}//if (game->PlayerData[i]->player_status[i]==
		} else { // hold'em/omaha
			// if we don't need a post from this player, it implies he's played recently enough
			// that his missed_blind_count can be reset 19:::			if (GameCommonData.post_needed[i] == POST_NEEDED_NONE) {
				players[i].missed_blind_count = 0;
			}
			if (game->PlayerData[i]->player_status[i]==PLAYER_STATUS_DID_NOT_POST_BB) {
				// this guy has missed another blind.
				players[i].missed_blind_count++;
			}
		}//if (game_rules == GAME_RULES_STUD7 || game_rules
	}//for (i=0 ; i<MAX_PLAYERS_PER_GAM
	

	// Calculate the total pot size
	int total_pot_size = 0;
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		total_pot_size += game->PlayerData[0]->pot[i];
	}

	// If this is a real money table, update the gross bets total
	if (chip_type == CT_REAL) {
		ServerVars.gross_bets += total_pot_size + game->PlayerData[0]->rake_total;
	}

	// Update the summary info for this table.
	#define OLD_INFO_WEIGHTING	((float).91)
	// First, do the pot size.
	if (summary_info.avg_pot_size) {
		summary_info.avg_pot_size = (INT32)(
				(float)summary_info.avg_pot_size * OLD_INFO_WEIGHTING +
				(float)total_pot_size * (1.0-OLD_INFO_WEIGHTING));
	} else {
		// This is the first game... don't do any weighting.
		summary_info.avg_pot_size = total_pot_size;
	}

	// Calculate the hands per hour rate.
	WORD32 elapsed = SecondCounter - last_game_end_time;
	if (!elapsed) elapsed = 1;	// never allow divide by zero.
	float hands_per_hour = (float)3600.0 / (float)elapsed;
	if (summary_info.hands_per_hour) {
		hands_per_hour = (float)summary_info.hands_per_hour * OLD_INFO_WEIGHTING +
						hands_per_hour * (float)(1.0-OLD_INFO_WEIGHTING);
	}
	summary_info.hands_per_hour = (BYTE8)min(250, (int)hands_per_hour);

	// Calculate the rake per hour rate...
	float frake_per_hour = (float)3600.0 * (float)game->PlayerData[0]->rake_total /
						  (float)elapsed;
	if (rake_per_hour) {
		frake_per_hour = (float)rake_per_hour * OLD_INFO_WEIGHTING +
						frake_per_hour * (float)(1.0-OLD_INFO_WEIGHTING);
	}
	rake_per_hour = (WORD16)min(65535, (int)frake_per_hour);

	// Calculate percentage of players who played long enough to see the flop...
	float players_per_flop = (float)100.0 * (float)GameCommonData.players_saw_flop /
			(float)max(GameCommonData.players_saw_pocket,1);
	// now we have a float from 0.0 to 100.0.
	if (summary_info.players_per_flop) {
		players_per_flop = (float)summary_info.players_per_flop * OLD_INFO_WEIGHTING +
						players_per_flop * (float)(1.0-OLD_INFO_WEIGHTING);
	}
	summary_info.players_per_flop = (BYTE8)players_per_flop;
	if (tournament_start_time && SecondCounter >= tournament_start_time) {
		summary_info.tournament_elapsed = (WORD16)(SecondCounter - tournament_start_time);
	} else {
		summary_info.tournament_elapsed = 0;
	}

	// indicate we changed the summary_info structure.
	summary_info_changed = TRUE;

	// If there was a bad beat, keep track of the chip amounts so we can
	// animate the payout.
	if (bad_beat_payout) {
		// initiate the bad beat prize payout animation
		bad_beat_payout_stage = 1;
		pending_bad_beat_payout_amount = bad_beat_payout;	// save for animation purposes
		next_bad_beat_payout_stage = SecondCounter + BADBEAT_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE;
		next_game_start_time += BADBEAT_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE;
		bad_beat_prizes = *game->bad_beat_prizes;
		// Keep track of how much each person won...
		// Note that we make a copy in case they leave their seat (we need that cleared
		// so chips don't slide to a new player that might take his seat).
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			players[i].bad_beat_prize = bad_beat_prizes.player_prizes[i];
			pr(("%s(%d) *** BadBeat player %2d got %6d chips from game object with pot %d\n",
				_FL, i, players[i].bad_beat_prize, bad_beat_payout));
		}
    }

	// If we hit a special hand number, we'll animate that payoff
	if (GameCommonData.game_serial_number == SpecialHandNumber || SpecialHandNumber==(WORD32)-1) {
		special_hand_payout_stage = 1;
		next_special_hand_payout_stage = SecondCounter + SPECIALHAND_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE;
		next_game_start_time += SPECIALHAND_PAYOUT_DELAY_BEFORE_SHOWING_PRIZE;
		// we need to count the number of winners who'll be splitting the winner bonus
		int winner_count = 0;
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			if (had_best_hand_last_game[i]) {	// here's one
				winner_count++;
			}
		}
		// figure out how much each player will be getting
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			// he qualifies is he's played a hand here...
			if (played_this_hand[i]) {
				players[i].special_hand_prize = SpecialHandPrize;
				// we store the player id of the player sitting there to be sure the right guy gets it
				players[i].special_hand_playerid = GameCommonData.player_id[i];
				// if he had the best hand, he gets the winner bonus
				if (winner_count) {	// just in case
					if (had_best_hand_last_game[i]) {	// got something, add the bonus
						players[i].special_hand_prize += (SpecialHandWinnerBonus / winner_count);
					}
				}
			}
		}
	}
	
	// make a copy of the common game player data (the one watching players see)
	memcpy(&table_gpd, game->WatchingData, sizeof(struct GamePlayerData));
	
	// We're all done with that game object... delete it.
	delete game;
	game = NULL;

	// Do any tournament handling necessary
	if (tournament_table) {
		int players_left = Tourn_BetweenGamesProcessing();
		if (players_left == 1) {	// it's over
			table_tourn_state = TTS_WRAP_UP; // the tournament has finished -- do final things
		}
	}
		
	int empty_seats = max_number_of_players - filled_seats;
	//kp(("%s(%d) There are %d empty seats at table %s\n", _FL, empty_seats, summary_info.table_name));
	// Only kick players off for missing blinds if the table is filling up.
	if (empty_seats <= 2) {
		// Scan for any players who have been sitting out for too
		// long and remove them from the table.
		int stud_table = (game_rules == GAME_RULES_STUD7 || game_rules==GAME_RULES_STUD7_HI_LO);
		// 20000425 HK: changed from 10 : 2
		int max_misses = (stud_table ? 15 : 3);	// 15 antes / 3 blinds
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			if (players[i].status==2) {	// human?
				if (players[i].missed_blind_count >= max_misses) {
					char str[200];
					sprintf(str, "%s has been picked up from this table for missing %d %s.",
						players[i].player->user_id, max_misses, (stud_table ? "antes" : "big blinds") );
					SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
					// Save player ptr and missed blind count because the
					// players[i] entry gets nuked by RemovePlayerFromTable().
					Player *p = players[i].player;
					RemovePlayerFromTable(i);
					// Now add him back in as watching and notify him
					// with a personalize pop-up window message.
					AddWatchingPlayer(p);

					p->SendMiscClientMessage(MISC_MESSAGE_MISSED_BLINDS, table_serial_number, 0, 0, 0, 0,
							"You have been picked up from Table %s\n"
							"for missing %d %s.\n\n"
							"When the tables are full or nearly full,\n"
							"you must play or give up your seat.",
							summary_info.table_name, max_misses, (stud_table ? "antes" : "big blinds") );
				}
			}
		}
	}

	new_players_must_post = TRUE;
	last_game_end_time = SecondCounter;	// keep track of when our games end.

	//MemTrackVMUsage(FALSE, "%s(%d) Table::HandleGameOver() (bottom)",_FL);
	return ERR_NONE;
}

/* The two tables below are used for default action processing */
int action_table[] = {	// action responses in order of priority.
	ACT_TOSS_HAND,
	ACT_MUCK_HAND,
	ACT_SHOW_HAND,		// must be a default action because sometimes player has no choice
	ACT_SIT_OUT_ANTE,
	ACT_SIT_OUT_BOTH,
	ACT_SIT_OUT_POST,
	ACT_SIT_OUT_SB,
	ACT_SIT_OUT_BB,
	ACT_BRING_IN,		// sometimes, no choice but to do this
  #if 0	//kriskoin: 	ACT_CHECK,			//kriskoin:   #endif
	ACT_FOLD,
	ACT_FORCE_ALL_IN,	// we jump straight to this if needed
	0 };

int tournament_action_table[] = {	// action responses in order of priority.
	ACT_TOSS_HAND,
	ACT_MUCK_HAND,
	ACT_SHOW_HAND,		// must be a default action because sometimes player has no choice
	ACT_POST_ANTE,		// tournaments must force all posts to take place
	ACT_POST_BOTH,
	ACT_POST,
	ACT_POST_SB,
	ACT_POST_BB,
	ACT_BRING_IN,		// sometimes, no choice but to do this
  #if 0	//kriskoin: 	ACT_CHECK,			//kriskoin:   #endif
	ACT_FOLD,
	ACT_FORCE_ALL_IN,	// we jump straight to this if needed
	0 };
	
/**********************************************************************************
 Function Table::DealerTextForTimeout(int action_index, char *name, int gender)
 date: kriskoin 2019/01/01 Purpose: make the dealer tell everyone why someone timed out
***********************************************************************************/
void Table::DealerTextForTimeout(int action_taken, char *name, int gender, int on_purpose, Player *plr)
{
	char str[100];
	zstruct(str);
	int left_table = on_purpose;
	if (on_purpose) {
	  #if 0	// 990815HK : redundant
		sprintf(str,"%s has left this table.",name);
	  #endif
	} else {
		// If we've heard from them lately, indicate that they did not act
		// in time, otherwise assume we've lost contact with them.
		int connection_state = CONNECTION_STATE_LOST;
		if (plr) {
			connection_state = worst_connection_state_for_input_player;
		}
		if (connection_state <= CONNECTION_STATE_GOOD) {
			// good connection
			if (chip_type == CT_REAL) {
				sprintf(str, "%s did not act in time.", name);
			} else {
				sprintf(str, "%s is connected but timed-out.", name);
			}
		} else if (connection_state <= CONNECTION_STATE_POOR) {
			// poor connection
			sprintf(str, "%s didn't act early enough.", name);
		} else {
			// bad or lost...
			sprintf(str, "%s 's connection was lost (timed-out).", name);
		}
	}
	if (str[0]) {
		SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
	}
	zstruct(str);
	// possibly display something, depending on the default action taken
	switch (action_taken) {
	case ACT_TOSS_HAND:
		sprintf(str,"%s hand will not be shown.", szGenderCaps1[gender]);
		break;
	case ACT_MUCK_HAND:
		sprintf(str,"%s hand will be mucked.", szGenderCaps1[gender]);
		break;
	case ACT_SHOW_HAND:
		sprintf(str,"%s hand will be shown.", szGenderCaps1[gender]);
		break;
	case ACT_SIT_OUT_ANTE:
		if (left_table) {
			sprintf(str,"%s has left the table without posting an ante.", name);
		} else {
			sprintf(str,"%s did not post an ante.", szGenderCaps2[gender]);
		}
		break;
	case ACT_SIT_OUT_BOTH:	// fall through (no blind posted)
	case ACT_SIT_OUT_POST:	// fall through (no blind posted)
	case ACT_SIT_OUT_SB:	// fall through (no blind posted)
	case ACT_SIT_OUT_BB:
		if (left_table) {
			sprintf(str,"%s left the table without posting a blind.", name);
		} else {
			sprintf(str,"%s did not post a blind.", szGenderCaps2[gender]);
		}
		break;
	case ACT_BRING_IN:
		break;
	case ACT_CHECK:
		break;
	case ACT_FOLD:
		break;
	case ACT_FORCE_ALL_IN:
		break;
	case ACT_POST_ANTE:
		break;
	case ACT_POST_BOTH:
		break;
	case ACT_POST:
		break;
	case ACT_POST_SB:
		break;
	case ACT_POST_BB:
		break;
	}

	// display it if we generated a message
	if (str[0]) {
		SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Handle the timeout situation for an input request.
// Returns: index of action returned
//
int Table::HandleInputRequestTimeout(int player_leaving_table_flag, char *calling_file, int calling_line)
{
	int action_index = 0;	// unknown: needs documenting (this is also what gets returned)
	int action = 0;			// ACT_*

	int seating_position = game->GPIRequest->seating_position;

	Player *plr = players[seating_position].player;

	// update the worst connection state we've seen from this player while waiting for input.
	players[seating_position].worst_connection_state = max(
				players[seating_position].worst_connection_state,
				plr->CurrentConnectionState());
	players[seating_position].worst_connection_state = max(
				players[seating_position].worst_connection_state,
				worst_connection_state_for_input_player);
	worst_connection_state_for_input_player = players[seating_position].worst_connection_state;

  #if 0	// 2022 kriskoin
	kp(("%s(%d) HandleInputRequestTimeout: last_input_request_time is %d, now is %d, elapsed = %d\n",
			_FL, last_input_request_time, SecondCounter, SecondCounter - last_input_request_time));
	kp(("%s(%d) GPIRequest: s/n = %d, game = %d\n",
			_FL, game->GPIRequest->input_request_serial_number,
				 game->GPIRequest->game_serial_number));
  #endif

	// log to action log if it's something important enough to sit the
	// guy out if he were to time out.  Time outs on SHOW/MUCK etc.
	// should NOT be logged.
	if (!player_leaving_table_flag && *game->sit_out_if_timed_out_ptr) {
		// At present, there is no way to see these except examining .HAL file
		char connect_status[80];
		zstruct(connect_status);
		switch (worst_connection_state_for_input_player) {
		case CONNECTION_STATE_GOOD:
			sprintf(connect_status,"%s has a good connection", plr->user_id);
			break;
		case CONNECTION_STATE_POOR:
			sprintf(connect_status,"%s has a poor connection", plr->user_id);
			break;
		case CONNECTION_STATE_BAD:
			sprintf(connect_status,"%s has a bad connection", plr->user_id);
			break;
		case CONNECTION_STATE_LOST:
			sprintf(connect_status,"%s has lost their connection", plr->user_id);
			break;
		default:
			sprintf(connect_status,"%s has an unknown connection state", plr->user_id);
			break;
		}
		PL->LogComment(connect_status);	
		PL->LogGameAction(GameCommonData.game_serial_number,
				game->GPIRequest->input_request_serial_number,
				seating_position, ACT_TIMEOUT, 0,0);
	}

	//kriskoin: 	// We don't charge an all-in, we don't care about routing,
	// we don't care about connection state.
	//kriskoin: 	int last_betting_round = BETTING_ROUND_4;
	if (GameCommonData.game_rules==GAME_RULES_STUD7 ||
		GameCommonData.game_rules==GAME_RULES_STUD7_HI_LO)
	{
		last_betting_round = BETTING_ROUND_5;
	}
	pr(("%s(%d) This %s the last betting round.\n", _FL,
			game->PlayerData[0]->game_state==last_betting_round ? "IS" : "is NOT"));
	if (GET_ACTION(game->GPIRequest->action_mask, ACT_CHECK) &&
		game->PlayerData[0]->game_state==last_betting_round)
	{
		// Check IS an option.  Use it.
		action = ACT_CHECK;
	}

	// If he's not leaving the table and he has bet, check for an all-in...
	int chips_bet = game->PlayerData[seating_position]->chips_bet_total[seating_position];
	if (!action && !player_leaving_table_flag && chips_bet) {
		int allowed_all_in = TRUE;
		int charge_all_in = TRUE;	// default to charging the player an all-in
		if (chip_type == CT_PLAY) {
			// play money...
			if (worst_connection_state_for_input_player == CONNECTION_STATE_GOOD) {
				// good connections get folded for play money
				allowed_all_in = FALSE;
			}
		} else{
			// real money or tournament
			// 1. If there was widespread routing problem in the last n minutes
			//    this guy definitely gets an all-in, even he ran out.
			// 2. If his last all-in was within 120s, he gets an all-in
			//	  (in case he was playing two tables and only had one all-in)
			// 3. Just check his all-in count.

			#define ALL_IN_GRACE_PERIOD_AFTER_ROUTING_PROBLEM	(15*60)
			allowed_all_in = FALSE;
			if (dwLastWidespreadRoutingProblem &&
					(SecondCounter - dwLastWidespreadRoutingProblem) < ALL_IN_GRACE_PERIOD_AFTER_ROUTING_PROBLEM)
			{
				pr(("%s(%d) Widespread routing problem detected recently... granting unlimited all-ins\n",_FL));
				allowed_all_in = TRUE;
				charge_all_in = FALSE;	// don't charge him in this case
			} else {
				// Check if he had an all-in within the last 120s
				SDBRecord player_rec;	// the result structure
				zstruct(player_rec);
				if (SDB->SearchDataBaseByPlayerID(plr->player_id, &player_rec) >= 0) {
					time_t now = time(NULL);
					if (player_rec.all_in_times[0] &&
							now - player_rec.all_in_times[0] <= 120)
					{
						pr(("%s(%d) player %s needed another all-in after %ds.  Granting it.\n",
								_FL, plr->user_id, now - player_rec.all_in_times[0]));
						allowed_all_in = TRUE;
						charge_all_in = FALSE;	// don't charge him in this case
					}
				}
			}
			if (!allowed_all_in) {	// no other reason yet?
				// lastly, check their allowed all-in count.
				if (plr->AllowedAutoAllInCount()) {
					// he has at least one all-in left
					allowed_all_in = TRUE;
				}
			}
		}
		if (allowed_all_in) {
			// Player may not have had a good connection, treat them as all-in
			// If we're in a situation where being set all-in would be valid, one of the
			// following 3 choices must be a valid option... if none of these three are
			// present, it doesn't make sense to set him all-in
			// Real money players ALWAYS get called all-in, even when well connected.
			if (GET_ACTION(game->GPIRequest->action_mask, ACT_CHECK) ||
				GET_ACTION(game->GPIRequest->action_mask, ACT_CALL_ALL_IN) ||
				GET_ACTION(game->GPIRequest->action_mask, ACT_CALL))
			{
				action = ACT_FORCE_ALL_IN;
				action_index = ACT_FORCE_ALL_IN;
				if (chip_type == CT_REAL || chip_type == CT_TOURNAMENT) {
					if (charge_all_in) {
						// log it!
						SDB->SavePlayerAllInInfo(plr->player_id, time(NULL),
								worst_connection_state_for_input_player,
								GameCommonData.game_serial_number);
						// Tell the player object to re-send the all-in counts to the player
						plr->send_client_info = TRUE;
					}
					// Send them a pop-up message telling them what happened.
					int count = plr->AllowedAutoAllInCount();
					if (worst_connection_state_for_input_player==CONNECTION_STATE_GOOD) {
						// they had a good connection when they timed out.
						plr->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
								table_serial_number, 0, 0, 0, 0,
								"You have timed out and been forced ALL-IN.\n"
								"\n"
								"You now have %d ALL-IN%s left.\n"
								"\n"
								"You were connected when this timeout occured.\n"
								"\n"
								"In order to stop All In abuses and protect all our\n"
								"players, including yourself, we keep a record of all-ins.\n"
								"\n"
								"Players suspected of abusing the system will be warned\n"
								"and then banned from the games.",
								count, count==1 ? "" : "s");

						// Send a good all-in alert if requested.
						SDBRecord player_rec;	// the result structure
						zstruct(player_rec);
						if (SDB->SearchDataBaseByPlayerID(plr->player_id, &player_rec) >= 0) {
							if (player_rec.flags & SDBRECORD_FLAG_GOOD_ALLIN_ALERT) {
								// 24/01/01 kriskoin:
								SendAdminAlert(ALERT_7, "%-13s Good all-in alert for %s", summary_info.table_name, player_rec.user_id);
							}
						}
					} else {
						plr->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
								table_serial_number, 0, 0, 0, 0,
								"You have timed out and been forced ALL-IN.\n\n"
								"You now have %d ALL-IN%s left.",
								count, count==1 ? "" : "s");
					}
					players[seating_position].sent_timeout_msg = TRUE;	// we've told them
				}
			}
		} else {
			// Player not allowed all in... fold.
			if ( (chip_type == CT_REAL || chip_type == CT_TOURNAMENT) &&
					!players[seating_position].sent_timeout_msg)
			{
				if (*game->sit_out_if_timed_out_ptr) {	// something worth telling him about?
					players[seating_position].sent_timeout_msg = TRUE;	// we've told them
					plr->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
							table_serial_number, 0, 0, 0, 0,
							"You have timed out.\n\n"
							"You do not have any ALL-INs left\n"
							"so your hand has been folded.");
				}
			}
		}
	}

	// If no action already selected, pick the first bit allowed from our table.
	if (!action) {
		int *action_table_to_use = &action_table[0];
		if (tournament_table) {
			action_table_to_use = &tournament_action_table[0];
		}
		while (action_table_to_use[action_index]) {
			if (game->GPIRequest->action_mask & (1 << action_table_to_use[action_index])) {
				// found one.
				action = action_table_to_use[action_index];
				pr(("%s(%d) HandleInputRequestTimeout() is selecting action %d, ac_index %d.\n",
					_FL,action, action_index));
				break;
			}
			action_index++;
		}
	}

	if (!action) {
		action_index = 0;	// will "toss hand" if unknown (shouldn't happen)
		Error(ERR_INTERNAL_ERROR, "%s(%d) HandleInputRequestTimeout did not have an action to choose from (mask = %04x)!",_FL,game->GPIRequest->action_mask);
		// Pick the first action we find.
		action = 0;
		if (!game->GPIRequest->action_mask) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) HandleInputRequestTimeout: no possible actions!",_FL);
		} else {
			while ((1<<action) && !((1<<action) & game->GPIRequest->action_mask)) {
				// no match... try next action.
				action++;
			}
		}
		Error(ERR_INTERNAL_ERROR, "%s(%d) HandleInputRequestTimeout chose action %d",_FL, action);
	}

	// Now that we've picked an answer, fill in the input result.
	zstruct(*game->GPIResult);
	game->GPIResult->game_serial_number				= game->GPIRequest->game_serial_number;
	game->GPIResult->table_serial_number			= game->GPIRequest->table_serial_number;
	game->GPIResult->input_request_serial_number	= game->GPIRequest->input_request_serial_number;
	game->GPIResult->seating_position				= game->GPIRequest->seating_position;
	game->GPIResult->action = (BYTE8)action;
	game->GPIResult->ready_to_process = TRUE;
	// Tell the user we were waiting for to cancel the input request.
	if (players[seating_position].status==2) {	// human?
		struct GamePlayerInputRequestCancel gpirc;
		zstruct(gpirc);
		gpirc.game_serial_number			= game->GPIRequest->game_serial_number;
		gpirc.input_request_serial_number	= game->GPIRequest->input_request_serial_number;
		gpirc.table_serial_number			= game->GPIRequest->table_serial_number;
		// If necessary, tell the client to enter sit-out mode.
		pr(("%s(%d) game = $%08lx, *game->sit_out_if_timed_out_ptr = %d, sitting_out_flag = %d\n",
				_FL, game, *game->sit_out_if_timed_out_ptr, players[seating_position].sitting_out_flag));
		if (*game->sit_out_if_timed_out_ptr) {
			// Make sure we only ever tell them ONCE per game that they have been sat out.
			if (!players[seating_position].sent_sitout_msg) {
				players[seating_position].sent_sitout_msg = TRUE;	// flag that we've now told them
				players[seating_position].force_to_sit_out_flag = TRUE;
				players[seating_position].force_to_sit_out_serial_num++;	// change s/n for each new unique timeout
				if (!players[seating_position].force_to_sit_out_serial_num) {
					players[seating_position].force_to_sit_out_serial_num++;	// never allow zero.
				}
			  #if 0	// 2022 kriskoin
				if (DebugFilterLevel <= 11) {
					kp(("%s %s(%d) %sTable %s asking %s to enter sit-out mode due to timeout (game %d), called from %s(%d)\n",
							TimeStr(), _FL,
							tournament_table ? "Tournament " : "",
							summary_info.table_name,
							table_info.players[seating_position].name,
							gpirc.game_serial_number,
							calling_file, calling_line));
					kp(("%s %s(%d)      sitout s/n=%d, elapsed=%ds, leaving_table=%d, players[%d].timed_out_this_hand=%d\n",
							TimeStr(), _FL,
							players[seating_position].force_to_sit_out_serial_num,
							SecondCounter - game_start_time,
							player_leaving_table_flag,
							seating_position,
							players[seating_position].timed_out_this_hand));
				}
			  #endif
				gpirc.now_sitting_out = (BYTE8)*game->sit_out_if_timed_out_ptr;
				players[seating_position].game_common_data_sent = FALSE;	// force new sitout s/n to be sent to this player asap.

				// Put him into sitting out mode until he gets his act together.
				// (or put him into post and fold mode if a tournament table)
				if (players[seating_position].status==2) {	// human?
					int player_table_index = players[seating_position].player->SerialNumToJoinedTableIndex(table_serial_number);
					if (player_table_index >= 0) {
						players[seating_position].player->JoinedTables[player_table_index].client_state_info.sitting_out_flag = TRUE;
					}
				}
				//kriskoin: 				// game, but also they must be flagged as being in
				// post/fold mode (handled elsewhere), therefore
				// don't set the sitting_out_flag for tournaments.
				if (!tournament_table) {
					players[seating_position].sitting_out_flag = TRUE;
				}
			}
		}
	  #if 0	// 2022 kriskoin
		if (DebugFilterLevel <= 11) {
			if (!gpirc.table_serial_number) {
				kp(("%s %s(%d) WARNING: table %s, player %s, gpirc.table_serial_number==0, game s/n=%d\n",
						TimeStr(), _FL, summary_info.table_name,
						table_info.players[seating_position].name,
						gpirc.game_serial_number));
			}
		}
	  #endif
		plr->SendPlayerInputRequestCancel(&gpirc);
	}
	// we want to remember if this player has already timed out once this hand
	players[seating_position].timed_out_this_hand = TRUE;
	game->GPIRequest->ready_to_process = FALSE;
	NOTUSED(calling_file);
	NOTUSED(calling_line);
	return action_index;
}

//*********************************************************
// https://github.com/kriskoin//
// Update the end-of-game bad beat payout animation
//
void Table::UpdateBadBeatPayoutStage(void)
{
	if (!bad_beat_payout_stage) {
	    return;	// nothing to do.
	}
	if (SecondCounter < next_bad_beat_payout_stage) {
	    return; // nothing to do yet.
	}
	// Time for the next stage.
	bad_beat_payout_stage++;
	int delay_before_next_stage = 0;
	int i = 0;
	char cs[MAX_CURRENCY_STRING_LEN];
	zstruct(cs);
	char msg[200];
	zstruct(msg);
	FillBlankGPD(&table_gpd);

	switch (bad_beat_payout_stage) {
	case 2:     // time to show the prize pot
		delay_before_next_stage = BADBEAT_PAYOUT_DELAY_TO_SHOW_POT;
		SendSameGPDToEveryone(&table_gpd);	// clear chips in front of players before displaying pot.
		sprintf(msg, "Congratulations! Awarding %s Bad Beat Jackpot at this table",
				CurrencyString(cs, bad_beat_prizes.total_prize, chip_type));
		SendDealerMessage(msg, CHATTEXT_ADMIN);
		sprintf(msg, "%s to %s loser",
				CurrencyString(cs, bad_beat_prizes.loser_share, chip_type),
				bad_beat_prizes.losers==1 ? "the" : "each");
		sprintf(msg+strlen(msg), ", %s to %s winner",
				CurrencyString(cs, bad_beat_prizes.winner_share, chip_type),
				bad_beat_prizes.winners==1 ? "the" : "each");
		if (bad_beat_prizes.participants) {
			sprintf(msg+strlen(msg), ", %s to %s other player.",
					CurrencyString(cs, bad_beat_prizes.participant_share, chip_type),
					bad_beat_prizes.participants==1 ? "the" : "each");
		} else {
			strcat(msg, ".");
		}
		SendDealerMessage(msg, CHATTEXT_ADMIN);
		table_gpd.pot[0] = pending_bad_beat_payout_amount;
	    break;
	case 3:     // time to award the prizes
		delay_before_next_stage = BADBEAT_PAYOUT_DELAY_TO_AWARD_PRIZES;
		table_gpd.s_gameover = GAMEOVER_TRUE;	// allow animation of chips to the winners
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			kp(("%s(%d) *** BadBeat player %d paying %6d chips\n", _FL, i, players[i].bad_beat_prize));
			table_gpd.chips_in_front_of_player[i] = players[i].bad_beat_prize;
			table_gpd.chips_won[i] = players[i].bad_beat_prize;
			// 24/01/01 kriskoin:
			if (players[i].player) {
				players[i].player->send_client_info = TRUE;
				players[i].player->ChipBalancesHaveChanged();
			}
		}
		sprintf(msg, "The prize money has now been credited directly to everyone's account!");
		SendDealerMessage(msg, CHATTEXT_ADMIN);
		sprintf(msg, "Check your account history (Cashier screen) for details");
		SendDealerMessage(msg, CHATTEXT_ADMIN);
	    break;
	default:
		//SendDealerMessage("Bad beat payout is now completed. Switching back to normal.", CHATTEXT_ADMIN);
	    bad_beat_payout_stage = 0;
	    break;
	}
	if (bad_beat_payout_stage) {
		next_bad_beat_payout_stage = SecondCounter + delay_before_next_stage;
		next_game_start_time += delay_before_next_stage;
	} else {
		next_bad_beat_payout_stage = 0;
	}
	SendSameGPDToEveryone(&table_gpd);
}

/**********************************************************************************
 Function Table::UpdateSpecialHandPayoutStage(void)
 date: 24/01/01 kriskoin Purpose: pay out a prize if a special hand number was hit (similar to bad beats)
***********************************************************************************/
void Table::UpdateSpecialHandPayoutStage(void)
{
	pr(("%s(%d) UpdateSpecialHandPayoutStage called with special_hand_payout_stage = %d\n", _FL, special_hand_payout_stage));
	if (!special_hand_payout_stage) {
	    return;	// nothing to do.
	}
	if (bad_beat_payout_stage) {
	    return;	// don't trample this if it happens to be going on
	}
	if (SecondCounter < next_special_hand_payout_stage) {
	    return; // nothing to do yet.
	}
	ClientTransaction ct;
	// Time for the next stage.
	special_hand_payout_stage++;
	kp(("%s %s(%d) Processing special_hand_payout_stage %d...\n", TimeStr(), _FL, special_hand_payout_stage));
	int delay_before_next_stage = 0;
	int i = 0;
	char cs[MAX_CURRENCY_STRING_LEN];
	zstruct(cs);
	char msg[200];
	zstruct(msg);
	FillBlankGPD(&table_gpd);
	int prize_pot = 0;
	switch (special_hand_payout_stage) {
	case 2:     // time to show the prize pot
		delay_before_next_stage = SPECIALHAND_PAYOUT_DELAY_TO_SHOW_POT;
		SendSameGPDToEveryone(&table_gpd);	// clear chips in front of players before displaying pot.
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			prize_pot += players[i].special_hand_prize;
			if (players[i].special_hand_prize > SpecialHandPrize) {
				sprintf(msg, "%s wins an additional bonus of %s for best hand!",
					GameCommonData.name[i],
					CurrencyString(cs, players[i].special_hand_prize-SpecialHandPrize, CT_REAL));
				SendDealerMessage(msg, CHATTEXT_ADMIN);
				kp(("%s Dealer: %s\n", TimeStr(), msg));
			}
		}
		table_gpd.pot[0] = prize_pot;
		// log this amount to the Prizes account
		zstruct(ct);
		ct.timestamp = time(NULL);
		ct.transaction_amount = prize_pot;
		ct.transaction_type = CTT_PRIZE_AWARD;
		ct.ecash_id = SpecialHandNumber;
		SDB->LogPlayerTransaction(SDB->PrizesRec_ID, &ct);
		break;
	case 3:     // time to award the prizes
		zstruct(ct);
		ct.timestamp = time(NULL);
		ct.transaction_type = CTT_PRIZE_AWARD;
		ct.ecash_id = SpecialHandNumber;
		// now do each individual player's payout and logging
		for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
			if (players[i].special_hand_prize) {
				char curr_str[MAX_CURRENCY_STRING_LEN];
				zstruct(curr_str);
				kp(("%s %s(%d) *** SpecialHand player %d (%06lx) getting paid %s\n",
						TimeStr(), _FL, i, GameCommonData.player_id[i],
						CurrencyString(curr_str, players[i].special_hand_prize, CT_REAL)));
				ct.transaction_amount = players[i].special_hand_prize;
				// we use the player id we stored to be sure the right guy gets it
				SDB->TransferChips(SDB->PrizesRec_ID, players[i].special_hand_playerid, players[i].special_hand_prize);
				SDB->LogPlayerTransaction(players[i].special_hand_playerid, &ct);
				// we'll draw it for him only if he validly gets it
				if (GameCommonData.player_id[i] == players[i].special_hand_playerid) {
					table_gpd.chips_in_front_of_player[i] = players[i].special_hand_prize;
					table_gpd.chips_won[i] = players[i].special_hand_prize;
				}
				// make sure it makes it to their cashier
				if (players[i].player) {
					players[i].player->send_client_info = TRUE;
					players[i].player->ChipBalancesHaveChanged();
				}
				// send a summary email to each player
				SDBRecord player_rec;	// the result structure
				zstruct(player_rec);
				if (SDB->SearchDataBaseByPlayerID(players[i].special_hand_playerid, &player_rec) >= 0) {
					if (player_rec.email_address[0]) {	// something there to try
						char email_str[1500];
						zstruct(email_str);
						char curr_str1[MAX_CURRENCY_STRING_LEN];
						char curr_str2[MAX_CURRENCY_STRING_LEN];
						zstruct(curr_str1);
						zstruct(curr_str2);
						sprintf(email_str,
							//       10        20        30        40        50        60        70  v     80
							"%s"
							"Dear %s,\n"
							"\n"
							"Congratulations for playing in game #%s!\n"
							"\n"
							"As part of our 50,000,000th game promotion, your Desert Poker cash account\n"
							"has been credited %s.\n"
							"\n"
							"Details of our promotion are available at http://www.kkrekop.io/50million.html\n"
							"You can visit the Cashier screen and select History to see your prize credit.\n"
							"\n"
							"Thank you again for playing in Desert Poker!\n"
							"\n"
							"Best Wishes,\n"
							"\n"
							"Desert Poker\n"
							"The Ultimate Online Poker Experience (tm)\n"
							"\n",
							(iRunningLiveFlag ? "" : "*** THIS IS A TEST -- NOT A REAL EMAIL ***\n\n"),
							player_rec.full_name,
							IntegerWithCommas(curr_str1, SpecialHandNumber),
							CurrencyString(curr_str2, players[i].special_hand_prize, CT_REAL));
#if 0 //	10032001					EmailStr(	player_rec.email_address,
									"Desert Poker",
									"support@kkrekop.io",
									"Congratulations from Desert Poker!",
									iRunningLiveFlag ? "support@kkrekop.io" : NULL,// bcc
									"%s", email_str);
#endif
		char email_addr[MAX_EMAIL_ADDRESS_LEN];
		if(iRunningLiveFlag) strcpy(email_addr,"support@kkrekop.io"); else strcpy(email_addr, NULL);
						EmailStr(	player_rec.email_address,
									"Desert Poker",
									"support@kkrekop.io",
									"Congratulations from Desert Poker!",
									"%s","%s", email_addr,email_str);

					}
				}
			}

			// clear it now, we're done
			players[i].special_hand_prize = 0;
			players[i].special_hand_playerid = 0;
		}
		delay_before_next_stage = SPECIALHAND_PAYOUT_DELAY_TO_AWARD_PRIZES;
		table_gpd.s_gameover = GAMEOVER_TRUE;	// allow animation of chips to the winners
		sprintf(msg, "Your cash account balances have been credited");
		SendDealerMessage(msg, CHATTEXT_ADMIN);
		kp(("%s Dealer: %s\n", TimeStr(), msg));
		sprintf(msg, "Go to the Cashier screen [History] for details");
		SendDealerMessage(msg, CHATTEXT_ADMIN);
		kp(("%s Dealer: %s\n", TimeStr(), msg));
	    break;
	default:
		// we're finished
		special_hand_payout_stage = 0;
	    break;
	}
	if (special_hand_payout_stage) {
		next_special_hand_payout_stage = SecondCounter + delay_before_next_stage;
		next_game_start_time += delay_before_next_stage;
	} else {
		next_special_hand_payout_stage = 0;
	}
	SendSameGPDToEveryone(&table_gpd);
}

//****************************************************************
// 
//
// Do anything possible for this table.  Process incoming input
// packets, start new games, move current games along, etc.
// This function will not block.
// *work_was_done_flag is OR'd into if work was actually performed.
// If no work was performed (for instance if we're blocked),
// *work_was_done_flag will not be modified.
//
ErrorType Table::UpdateTable(int *work_was_done_flag)
{
	int our_work_was_done_flag = FALSE;
  #if WIN32 && 0	// make the delay large to look for things we failed to
  	// make sure time_of_next_update was minimized for
	kp1(("%s(%d) WARNING: Tables update very infrequently\n", _FL));
	time_of_next_update = SecondCounter+10;	//kriskoin:   #else
	time_of_next_update = SecondCounter+3;	//kriskoin:   #endif
	// Are we trying to slow the action for some reason?
	if (SecondCounter < time_of_next_action) {
		// Yes, we're not allowed to do anything until time_of_next_action
		// expires.
		return ERR_NONE;	// do nothing yet.
	}

	//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);
	int tournament_table = (chip_type == CT_TOURNAMENT);
	if (!game) {
	  #if 0	//kriskoin: 		// This code isn't yet done.  There are some issues related to whether the table
		// might close mid-tournament.  My current thinking is that this code may not
		// be necessary, so until I sort that out, I'm not gonna write it and test it.
		if (game_disable_bits & GameCloseBits) {
			// This type of game is closed from the .ini file... close the table
			// after giving the players some warning.
			if (told_players_table_is_closing) {
				// We've already told them it's closing.  Just close it.
				kp(("%s(%d) Booting players from table %s.\n", _FL, summary_info.table_name));
				// This table has been closed.
				for (int i=0; i < MAX_PLAYERS_PER_GAME ;i++) {
					if (players[i].status) {
						RemovePlayerFromTable(i);
					}
				}
				while (watching_player_count) {
					if (RemoveWatchingPlayer(0)!=ERR_NONE) {
						break;	// hmm... problem booting watching players.  don't hang
					}
				}
			}
			return ERR_NONE;
		}
	  #endif

		int games_are_disabled = (game_disable_bits & GameDisableBits);
		// If this is a tournament table that's already under way, let
		// the individual games keep going.  They shouldn't stop just
		// because new tournaments have been disabled.
		if (tournament_table && table_tourn_state > TTS_WAITING) {
			games_are_disabled = 0;	// no longer disabled.
		}

		if (games_are_disabled) {
			// This type of game is disabled from the .ini file...
			next_game_start_time = SecondCounter + 1;	// try again real soon
			if (SecondCounter - last_start_time_announce >= 60 && strlen(GameDisabledMessage)) {
				last_start_time_announce = SecondCounter;
				SendDealerMessage(GameDisabledMessage, CHATTEXT_DEALER_NORMAL);
			}
			game_type_was_disabled = TRUE;
		} else {
			// See about starting a new tournament or game...
			if (tournament_table) {
				//kp(("%s\n",_FL));
				StartNewTournamentGameIfNecessary(&our_work_was_done_flag);
			} else {
				StartNewGameIfNecessary(&our_work_was_done_flag);
			}
		}
	}

	// Count down the shot clock message if necessary
	if (iShotClockETA && (ShotClockFlags & SCUF_ANNOUNCE_AT_TABLES)) {
		int minutes = (iShotClockETA+59) / 60;
		// only announce on certain intervals...
		int interval = 1;
		if (minutes > 5) {
			interval = 5;
		}
		if (minutes > 30) {
			interval = 10;
		}
		if (minutes > 60) {
			interval = 30;
		}
		if (minutes > 120) {
			interval = 60;
		}
		if (!(minutes % interval) && minutes != last_shotclock_announce_minutes) {
			last_shotclock_announce_minutes = minutes;
			char str[SHOTCLOCK_MESSAGE_LEN+100];
			zstruct(str);
		  #if 1	// 2022 kriskoin
			sprintf(str, "%s %d minute%s", ShotClockMessage1, minutes, minutes==1 ? "" : "s");
		  #else
			sprintf(str, "%s in %d minute%s", ShotClockMessage1, minutes, minutes==1 ? "" : "s");
		  #endif
			SendDealerMessage(str, CHATTEXT_ADMIN);
		}
	}

	if (game) {
		if (iShutdownAfterGamesCompletedFlag && !shut_down_announced) {
			shut_down_announced = TRUE;
			if (iShutdownIsBriefFlag) {
				SendDealerMessage("This is the last game; the server is restarting soon.", CHATTEXT_ADMIN);
			} else {
				SendDealerMessage("This is the last game; the server is shutting down soon.", CHATTEXT_ADMIN);
			}
		}

		// Handle any input results that have come back...
		// If it's time for the computer to evaluate its move, do so now.
		if (game->GPIRequest->ready_to_process && time_of_computerized_eval &&
			   #if 1	// don't let simulated clients get too far behind
				 iPlrOutQueueLens[0] < 5000 &&
			   #endif
				 GetTickCount() >= time_of_computerized_eval)
		{
			time_of_computerized_eval = 0;	// no more eval to be done.
			pr(("%s(%d) Table calling game->EvalComputerizedPlayerInput(%d) to fill in GPIResult.\n",_FL, game->GPIRequest->seating_position));
			EvalComputerizedPlayerInput(&GameCommonData, game->PlayerData[game->GPIRequest->seating_position],
						game->GPIRequest, game->GPIResult);
			our_work_was_done_flag = TRUE;

			// Update the average response time for players at this table.
			double elapsed = (GetTickCount() - last_input_request_ms) / 1000.0;
			elapsed = min(elapsed, 10.0);	// anything over 10 seconds doesn't help us learn anything, so max out at 10.
			avg_response_time = avg_response_time * (1.0-AVG_RESPONSE_TIME_WEIGHTING) + (elapsed * AVG_RESPONSE_TIME_WEIGHTING);

		  #if 0	// 2022 kriskoin
			// Add some random chat text from the robots...
			#define ROBOT_CHATS_PER_MINUTE	200
			#define TICKS_BETWEEN_ROBOT_CHAT	(60*1000/ROBOT_CHATS_PER_MINUTE)
			static WORD32 dwNextRobotChatTicks;
			WORD32 ticks = GetTickCount();
			if (ticks >= dwNextRobotChatTicks) {
				int ticks_late = ticks - dwNextRobotChatTicks;
				if (ticks_late < 5000) {
					dwNextRobotChatTicks += TICKS_BETWEEN_ROBOT_CHAT;
				} else {
					dwNextRobotChatTicks = ticks + TICKS_BETWEEN_ROBOT_CHAT;
				}
				//kp(("%s(%d) robot chat: %dms late.\n", _FL, ticks_late));

				// You'd think this was easy, but's it's actually not.
				struct GameChatMessage gcm;
				zstruct(gcm);
				gcm.game_serial_number = GameCommonData.game_serial_number;
				gcm.table_serial_number = table_serial_number;
				gcm.text_type = CHATTEXT_PLAYER;
				strnncpy(gcm.name,
						GameCommonData.name[game->GPIRequest->seating_position],
						MAX_COMMON_STRING_LEN);
				strnncpy(gcm.message, "Random chat text from a robot player.", MAX_CHAT_MSG_LEN);
				PL->LogGameChatMsg(gcm.game_serial_number, gcm.name, gcm.message);

				{
					static time_t last_logged_chat_hour;
					// put a time stamp every hour or so
					time_t tt = time(NULL);
					struct tm tm;
					struct tm *t = localtime(&tt, &tm);
					if (t->tm_hour != last_logged_chat_hour) {
						last_logged_chat_hour = t->tm_hour;
						ChatLog->Write("*** %s\n", TimeStr());
					}

					ChatLog->Write("%-13s %02d:%02d %-12s %s\n",
							summary_info.table_name,
							t->tm_hour, t->tm_min,
							gcm.name,
							gcm.message);
				}	

				// Now send the mock robot chat text out to everyone...
				//EnterCriticalSection(&CardRoomPtr->TableListCritSec);
				Table *t = CardRoomPtr->TableSerialNumToTablePtr(gcm.table_serial_number);
				if (t) {
					t->SendChatMessage(gcm.name, gcm.message, gcm.text_type);
				}
				//LeaveCriticalSection(&CardRoomPtr->TableListCritSec);

				// send to all admin clients for monitoring.  2nd parameter is T/F for real money
				CardRoomPtr->SendAdminChatMonitor(&gcm, summary_info.table_name, chip_type);
			}
		  #endif
		}
		//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable() Just after SendDataToPlayers()",_FL);

		// Check for any input results that have come back from the player
		// we're waiting for results from.
		if (game->GPIRequest->ready_to_process) {
			game->GPIRequest->table_serial_number = table_serial_number;	// always update (commonro doesn't know about tables)
			game->GPIRequest->game_serial_number = GameCommonData.game_serial_number;

			int input_seat = game->GPIRequest->seating_position;
			int time_out = game->GPIRequest->time_out;
			if (time_out <= 0) {
				kp(("%s %s(%d) table %s game %d, elapsed = %ds, input seat %d %s, time_out = %ds!\n",
						TimeStr(), _FL,
						summary_info.table_name,
						GameCommonData.game_serial_number,
						SecondCounter - game_start_time,
						input_seat,
						table_info.players[input_seat].name,
						time_out));
				time_out = game->GPIRequest->time_out = 30;		// override with something reasonable.
			}

			//kp(("%s(%d) game->GPIRequest->time_out = %d, game->GPIRequest->ready_to_process = %d\n",_FL,game->GPIRequest->time_out, game->GPIRequest->ready_to_process));

			// Periodically ping the person we're waiting for input from.
			if (SecondCounter - last_input_player_ping_time >= 4 &&
				players[input_seat].status==2)
			{
				players[input_seat].player->SendPing();
				last_input_player_ping_time = SecondCounter;
			}

			// Update the worst connection state we've seen for this player
			// while waiting for him to act.
			if (players[input_seat].player) {
				worst_connection_state_for_input_player = max(
						worst_connection_state_for_input_player, players[input_seat].player->CurrentConnectionState());
			}

			//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);
			// If this is a disconnected human, there might be some additional
			// adjustments to the timeout length.
			if (players[input_seat].status==2 &&
				players[input_seat].player->CurrentConnectionState() >= CONNECTION_STATE_BAD &&
				*game->sit_out_if_timed_out_ptr)
			{
				// We're waiting for an important action from a disconnected human
				// (or at least a badly connected one).
				// If there was a recent internet routing problem, force the
				// timeout to be much longer.
				#define INTERNET_ROUTING_DELAY_TIMEOUT	(4*60)
				int time_to_end_of_routing_problem = dwLastWidespreadRoutingProblem + INTERNET_ROUTING_DELAY_TIMEOUT - SecondCounter;
				if (dwLastWidespreadRoutingProblem && time_to_end_of_routing_problem > 0) {
					// We've had a recent routing problem... give them extra time.
					last_input_request_time = SecondCounter;	// always reference it from here.
					time_out += time_to_end_of_routing_problem;
					// Periodically tell all players what's going on.
					if (!last_routing_problem_notice ||
							SecondCounter - last_routing_problem_notice >= 30) {
						last_routing_problem_notice = SecondCounter;
						SendDealerMessage("An internet connectivity problem has occurred and disconnected many players.", CHATTEXT_ADMIN);
						char str[200];
						sprintf(str, "We will wait up to %ds for these people to return.", time_out);
						SendDealerMessage(str, CHATTEXT_ADMIN);
						resend_input_request_flag = TRUE;
						input_player_disconnected_msg_sent = TRUE;
					}
				}
			}

			// determine if the player wants to leave the table after this hand.
			int player_leaving_table_flag = FALSE;
			if (players[input_seat].status==2) {	// human?
				int player_table_index = players[input_seat].player->SerialNumToJoinedTableIndex(table_serial_number);
				if (player_table_index >= 0 && players[input_seat].player->JoinedTables[player_table_index].client_state_info.leave_table) {
					player_leaving_table_flag = TRUE;	// they want to leave.
				}
			}
			
			if (players[input_seat].status==2) {	// human
				// adate: if he's already timed out once this hand, time him out without delay
				if (players[input_seat].timed_out_this_hand) {
					//kp(("%s(%d) players[%d].timed_out_this_hand = %d\n",_FL,input_seat,players[input_seat].timed_out_this_hand));
					HandleInputRequestTimeout(player_leaving_table_flag, _FL);
					our_work_was_done_flag = TRUE;
					goto done_input_check;
				}
				// Try to get the input result from the player.
				int output_ready_flag = FALSE;
				ErrorType err = players[input_seat].player->GetPlayerInputResult(table_serial_number,
						&output_ready_flag, game->GPIResult);
				if (err != ERR_NONE) {
					Error(err, "%s(%d) Error getting input result from Player class", _FL);
				}
				if (!output_ready_flag) {
					// Check for timeouts from players while waiting for input.

					// Step 1: if they've left the table, time them out
					// immediately.  There's no sense waiting at all.
					if (player_leaving_table_flag) {
						// Player wishes to leave the table.  Time them out now.
						//kp(("%s(%d) player_leaving_table_flag = %d\n",_FL,player_leaving_table_flag));
						int action_taken = HandleInputRequestTimeout(player_leaving_table_flag, _FL);
						DealerTextForTimeout(action_taken,
							players[input_seat].player->user_id,
							players[input_seat].player->Gender, TRUE,
							players[input_seat].player);
						our_work_was_done_flag = TRUE;
						goto done_input_check;
					}

					char str[200];
					zstruct(str);
					char *name = players[input_seat].player->user_id;
					int disconnected = players[input_seat].player->CurrentConnectionState() >= CONNECTION_STATE_BAD;
					//int disconnected_seconds = max(1,players[input_seat].player->TimeSinceLastContact());

					// Calculate seconds left before giving up.
					int seconds_left = time_out - (SecondCounter - last_input_request_time);

					// If he has reconnected, tell the other players.
					if (!disconnected && input_player_disconnected_msg_sent) {
						input_player_disconnected_msg_sent = FALSE;
						// adate: if he just reconnected, make sure he has at least 15
						// seconds left to respond
						last_input_request_time += 15;
						seconds_left = max(2,time_out - (SecondCounter - last_input_request_time));
						sprintf(str, "%s is now online again and has %d seconds remaining to act.", name, seconds_left);
						SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
						pr(("%s(%d) %s is now back online.  Flagging input request (game %d, input #%d) to be resent.\n",
								_FL, name, game->GPIRequest->game_serial_number, game->GPIRequest->input_request_serial_number));
						resend_input_request_flag = TRUE;
					}

					// First warning...
					if (!first_input_timeout_warning_sent && (SecondCounter - last_input_request_time >= FirstInputWarningSeconds)) {
						pr(("%s(%d) First timeout warning for %s. Flagging input request (table %d, game %d, input #%d) to be resent.\n",
								_FL, name, game->GPIRequest->table_serial_number, game->GPIRequest->game_serial_number, game->GPIRequest->input_request_serial_number));
						first_input_timeout_warning_sent = TRUE;
						resend_input_request_flag = TRUE;
						if (disconnected) {
							// Player is disconnected.
						  #if 0	// 2022 kriskoin
							sprintf(str, "%s has been disconnected for %d seconds.",
									name, disconnected_seconds);
							SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
							seconds_left = max(2,seconds_left);
							sprintf(str, "%s has %d more seconds to reconnect.",
								szGender3[players[input_seat].player->Gender], seconds_left);
							SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
						  #else
							sprintf(str, "Waiting %d seconds for %s.",
									seconds_left, name);
							SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
						  #endif
							input_player_disconnected_msg_sent = TRUE;
						} else {
							// Player is connected but not responding.
							sprintf(str, "%s, it is your turn. ", name);
							// adate: changed to BLAB from NORMAL
							SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
						}
						our_work_was_done_flag = TRUE;
					}

					// Second warning...
					if (!second_input_timeout_warning_sent && (SecondCounter - last_input_request_time >= SecondInputWarningSeconds)) {
						pr(("%s(%d) Second timeout warning for %s. Flagging input request (table %d, game %d, input #%d) to be resent.\n",
								_FL, name, game->GPIRequest->table_serial_number, game->GPIRequest->game_serial_number, game->GPIRequest->input_request_serial_number));
						second_input_timeout_warning_sent = TRUE;
						resend_input_request_flag = TRUE;
						seconds_left = max(2,seconds_left);
						if (disconnected) {
							// Player is disconnected.
						  #if 0	// 2022 kriskoin
							sprintf(str, "%s has been disconnected for %d seconds.",
									name, disconnected_seconds);
							SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
							sprintf(str, "We'll give %s %d more seconds to reconnect.",
								szGender3[players[input_seat].player->Gender], seconds_left);
							SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
						  #else
							sprintf(str, "We will wait %d seconds for %s.",
									seconds_left, name);
							SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
						  #endif
							input_player_disconnected_msg_sent = TRUE;
						} else {
							// Player is connected but not responding.
							sprintf(str, "%s has %d seconds to respond.", name, seconds_left);
							SendDealerMessage(str, CHATTEXT_DEALER_NORMAL);
						}
						our_work_was_done_flag = TRUE;
					}

					// Timeout?
					if ((int)(SecondCounter - last_input_request_time) >= time_out) {
						// It has been too long.

						pr(("%s(%d) (int)(SecondCounter - last_input_request_time) = %d, time_out = %d\n",
							_FL, (int)(SecondCounter - last_input_request_time), time_out));

						int action_taken = HandleInputRequestTimeout(player_leaving_table_flag, _FL);
						if (*game->sit_out_if_timed_out_ptr) {
							Player *p = players[input_seat].player;
						  #if 0	//kriskoin: 							// He timed out when he's not supposed to, put him
							// into sitting out mode until he gets his act together.
							if (!players[input_seat].sent_sitout_msg) {
								players[input_seat].sent_sitout_msg = TRUE;	// indicate we've told the client to enter sit-out mode
								//kriskoin: 								// game, but also they must be flagged as being in
								// post/fold mode (handled elsewhere)
								if (!tournament_table) {
									if (player_table_index >= 0) {
										p->JoinedTables[player_table_index].client_state_info.sitting_out_flag = TRUE;
									}
									players[input_seat].sitting_out_flag = TRUE;
								}
							}
						  #endif

							DealerTextForTimeout(action_taken,
								p->user_id,
								p->Gender, FALSE,
								p);

							// Log this timeout so we can do a proper postmortem
							// analysis of why people are timing out.
							AddToLog("Data/Logs/timeout.log",
									"Time\tGame #\tUserID\tAction\tContact\tConnectionState\tVer\tPings...\n",
									"%s\t%d\t%s\t%d\t%d\t%d\t%d.%02d(%d)\t"
									"%ds:%dms\t"
									"%ds:%dms\t"
									"%ds:%dms\t"
									"%ds:%dms\t"
									"%ds:%dms\t"
									"%ds:%dms\t"
									"\n",
									TimeStr(), game->GPIRequest->game_serial_number,
									p->user_id,
									action_taken,
									p->TimeSinceLastContact(),
									p->CurrentConnectionState(),
									p->client_version_number.major,
									p->client_version_number.minor,
									p->client_version_number.build & 0x00FFFF,
									SecondCounter - p->PingResults[0].time_of_ping,
									p->PingResults[0].duration_for_ping,
									SecondCounter - p->PingResults[1].time_of_ping,
									p->PingResults[1].duration_for_ping,
									SecondCounter - p->PingResults[2].time_of_ping,
									p->PingResults[2].duration_for_ping,
									SecondCounter - p->PingResults[3].time_of_ping,
									p->PingResults[3].duration_for_ping,
									SecondCounter - p->PingResults[4].time_of_ping,
									p->PingResults[4].duration_for_ping,
									SecondCounter - p->PingResults[5].time_of_ping,
									p->PingResults[5].duration_for_ping
							);

							if (p->CurrentConnectionState() != CONNECTION_STATE_GOOD) {
								if (DebugFilterLevel <= 4) {
									kp(("%s %-15.15s Timeout %-15s ConnStat=%d Ver %d.%02d(%d) Pings:",
											TimeStr(), p->ip_str, p->user_id,
											p->CurrentConnectionState(),
											p->client_version_number.major,
											p->client_version_number.minor,
											p->client_version_number.build & 0x00FFFF));
									for (int i=0 ; i<PLAYER_PINGS_TO_RECORD ; i++) {
										if (SecondCounter - p->PingResults[i].time_of_ping >= 30) {
											break;	// too old... stop printing.
										}
										kp((" %ds:%dms",
											SecondCounter - p->PingResults[i].time_of_ping,
											p->PingResults[i].duration_for_ping));
									}
									kp(("\n"));
								}
								p->SaveTimeOutTime();	// keep track of when they were last timed out
								p->SendPing();	// always send one last ping
							}
						}
						our_work_was_done_flag = TRUE;
						goto done_input_check;
					}
				} else {
					// We got something from that player.
					// Update the average response time for players at this table.
					double elapsed = (GetTickCount() - last_input_request_ms) / 1000.0;
					elapsed = min(elapsed, 10.0);	// anything over 10 seconds doesn't help us learn anything, so max out at 10.
					avg_response_time = avg_response_time * (1.0-AVG_RESPONSE_TIME_WEIGHTING) + (elapsed * AVG_RESPONSE_TIME_WEIGHTING);
				}
			}
done_input_check:;
		}

		//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);
		game->UpdateGame(&our_work_was_done_flag);
		//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);

		// If our table did any work, send out any necessary data to any players
		// joined to this table or watching.
		//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable() Just before SendDataToPlayers()",_FL);
		if (our_work_was_done_flag || resend_input_request_flag) {
			// Something changed... force resend player data to all players.
			SendDataToPlayers(&our_work_was_done_flag, TRUE);
		} else {
			// Nothing changed... only send data to players who have
			// requested it.
			SendDataToPlayers(&our_work_was_done_flag, FALSE);
		}

		//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);
		// If the game is over, delete it.
		if (game->PlayerData[0]->s_gameover) {
			pr(("%s(%d) Detected game over condition.\n",_FL));
			HandleGameOver();
			our_work_was_done_flag = TRUE;
			// modify the global gpd that we're going to send out
			for (int i=0; i<MAX_PLAYERS_PER_GAME; i++) {
				table_gpd.chips_bet_total[i] = 0;
				table_gpd.pot[i] = 0;
				// make sure everybody's chip balances are reflected correctly
				// 20000913: we spent 2 hours on this thing
				GameCommonData.chips[i] = players[i].chips;
				pr(("%s(%d) GCD.chips = %d for player %d\n", _FL, GameCommonData.chips[i], i));
			}
			table_gpd.table_serial_number = table_serial_number;
			table_gpd.disconnected_flags = disconnected_flags;
			FlagResendGameCommonDataToEveryone();
			SendDataToPlayers(&our_work_was_done_flag, TRUE);
		}
	}
	if (!game) {	// no current game.
		// We're between games.
		// Scan for any players who want to leave the table now.
		int allow_leaving = TRUE;
		if (bad_beat_payout_stage) {
			UpdateBadBeatPayoutStage();
			allow_leaving = FALSE;	// don't allow leaving while paying out.
		}
		if (special_hand_payout_stage) {
			UpdateSpecialHandPayoutStage();
			allow_leaving = FALSE;	// don't allow leaving while paying out.
		}
	#if 1
		if (tournament_table &&
			(table_tourn_state != TTS_WAITING && table_tourn_state != TTS_FINISHED) )
		{
			allow_leaving = FALSE;	// don't allow leaving while a tournament is in progress
		}
	#endif
	  #if 0	// 2022 kriskoin
		if (!iRunningLiveFlag && chip_type==CT_REAL && summary_info.player_count > 2) {
			kp(("%s %s(%d) Between games at table %s.  allow_leaving = %d\n", TimeStr(), _FL, summary_info.table_name, allow_leaving));
		}
	  #endif
		// Remove (kick off) players from the table for various reasons...
		if (allow_leaving) {
			for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
				if (players[i].status==2) {	// human?
					// Has the user selected 'leave table'?
					int index = players[i].player->SerialNumToJoinedTableIndex(table_serial_number);
					if (index >= 0 && players[i].player->JoinedTables[index].client_state_info.leave_table) {
						// Player wishes to leave the table.
						our_work_was_done_flag = TRUE;
						char str[50];
						zstruct(str);
						sprintf(str, "%s has left the table.", players[i].player->user_id);
						SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
						RemovePlayerFromTable(i);
						continue;
					}
					// Check if the player has been disconnected for too long...
					//kriskoin: 					// from here.  That needs to be done from the TTS_WAITING processing
					// so that we can be sure that they get kicked off BEFORE the tournament
					// decides to start.
					int kick_off_flag = FALSE;
					EnterCriticalSection(&players[i].player->PlayerCritSec);
					if (players[i].player->server_socket &&
						players[i].player->server_socket->disconnected &&
						SecondCounter - players[i].player->server_socket->time_of_disconnect > PlayingTableTimeout)
					{
						kick_off_flag = TRUE;
					}
					LeaveCriticalSection(&players[i].player->PlayerCritSec);
					if (kick_off_flag) {
						// They've been disconnected for too long.  Kick them from the table.
						char str[200];
						sprintf(str, "%s was disconnected for too long and was picked up from the table.",
								players[i].player->user_id);
						SendDealerMessage(str, CHATTEXT_DEALER_BLAB);
						RemovePlayerFromTable(i);
						our_work_was_done_flag = TRUE;
						continue;
					}

				}
			}

			// Check for any accounts that have been locked out.  If so, kick them off immediately.
			// Check at most every n seconds (to avoid too many database queries).
			if (SecondCounter >= time_of_last_player_database_scan + 30) {
				time_of_last_player_database_scan = SecondCounter;
				for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
					if (players[i].status && GameCommonData.player_id) {
						SDBRecord player_rec;	// the result structure
						zstruct(player_rec);
						if (SDB->SearchDataBaseByPlayerID(GameCommonData.player_id[i], &player_rec) >= 0) {
							if (player_rec.flags & SDBRECORD_FLAG_LOCKED_OUT) {
								RemovePlayerFromTable(i);
								our_work_was_done_flag = TRUE;
							}
						}
					}
				}
			}
		}
		SendDataToPlayers(&our_work_was_done_flag, FALSE);	// send any changes
	}

	// Scan for any watching players who are disconnected and should be
	// kicked off the table.
	//!!! this only needs to be done every several seconds.  Perhaps we
	// have other periodic bookkeeping that really doesn't need to be
	// done very often.  Group it together.
	if (SecondCounter >= next_watching_scan) {
		next_watching_scan = SecondCounter + 20;
restart_watching_scan:;
		connected_watching_player_count = 0;
		for (int i=0 ; i<watching_player_count ; i++) {
			if (watching_players[i].player) {
				EnterCriticalSection(&(watching_players[i].player->PlayerCritSec));
				if (!watching_players[i].player->Connected()) {
					// Anonymous players get kicked off watching tables immediately.
					// Regular players get kicked off after a timeout.
					if (ANONYMOUS_PLAYER(watching_players[i].player->player_id) ||
							(watching_players[i].player->DisconnectedSeconds() >= (int)WatchingTableTimeout)) {
						// Remove them from the list.
						LeaveCriticalSection(&(watching_players[i].player->PlayerCritSec));
						RemoveWatchingPlayer(i);
						our_work_was_done_flag = TRUE;
						goto restart_watching_scan;	// start over (our count has changed).
					}
				} else {
				  #if 0	//kriskoin: 					// Only count players who aren't seated elsewhere and
					// who are not idle.
					if (!watching_players[i].player->CountSeatedTables() &&
						!watching_players[i].player->idle_flag)
				  #endif
					{
						connected_watching_player_count++;
					}
				}
				LeaveCriticalSection(&(watching_players[i].player->PlayerCritSec));
			}
		}
	}
	BYTE8 x = (BYTE8)min(255,connected_watching_player_count);
	if (summary_info.watching_count != x) {
		summary_info.watching_count = x;
		summary_info_changed = TRUE;
	}

	MakeDealerPromptToWatchingPlayers();

	//kriskoin: 	// get updated again, force the update to occur at exactly the time
	// that we want the game to start.
	if (next_game_start_time >= SecondCounter && next_game_start_time < time_of_next_update) {
		time_of_next_update = next_game_start_time;
	}

	*work_was_done_flag |= our_work_was_done_flag;
	//MemTrackVMUsage(FALSE, "%s(%d) Table::UpdateTable()",_FL);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Update the connected/disconnected status for the table_info player list
// before sending it out.
// Try not to update the data too often.
//
void Table::UpdateTableInfoConnectionStatus(void)
{
	if (SecondCounter - last_tableinfo_connection_update < 5) {
		// Too recent... don't change anything.
		return;
	}

	EnterCriticalSection(&TableCritSec);
	WORD16 new_flags = 0;
	for (int i=0 ; i<PLAYERS_PER_TABLE_INFO_STRUCTURE ; i++) {
		table_info.players[i].flags &= ~0x02;// default to connected
		if (table_info.players[i].player_id) {
			// There's a player in this slot.
			if (!((CardRoom *)cardroom)->TestIfPlayerConnected(table_info.players[i].player_id)) {
				table_info.players[i].flags |= 0x02;	// disconnected
				new_flags |= (WORD16)(1<<i);			// disconnected
			}
		}
	}
	last_tableinfo_connection_update = SecondCounter;
	disconnected_flags = new_flags;
	LeaveCriticalSection(&TableCritSec);
}

/**********************************************************************************
 Function Table::SetNobodyNeedsToPost(void);
 date: kriskoin 2019/01/01 Purpose: tell the table that nobody needs to post a blind of any sort
***********************************************************************************/
void Table::SetNobodyNeedsToPost(void)
{
	//kp(("%s(%d) Table %s: SetNobodyNeedsToPost() has been called.\n", _FL, summary_info.table_name));
	for (int i = 0; i < MAX_PLAYERS_PER_GAME; i++) {
		GameCommonData.post_needed[i] = POST_NEEDED_NONE;
		players[i].post_needed = POST_NEEDED_NONE;
		players[i].missed_blind_count = 0;
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Create a blank GamePlayerData structure for use between games.
//
void Table::FillBlankGPD(struct GamePlayerData *gpd)
{
	zstruct(*gpd);
	gpd->table_serial_number = table_serial_number;
	gpd->game_serial_number  = GameCommonData.game_serial_number;
	gpd->disconnected_flags  = disconnected_flags;
	gpd->p_waiting_player    = (BYTE8)-1;	// make sure nobody is blinking

	// Fill all card entries with CARD_NO_CARD
	memset(gpd->cards, 		  CARD_NO_CARD, sizeof(Card)*MAX_PLAYERS_PER_GAME*MAX_PRIVATE_CARDS);
	memset(gpd->common_cards, CARD_NO_CARD, sizeof(Card)*MAX_PUBLIC_CARDS);
}	

//*********************************************************
// https://github.com/kriskoin//
// Send the same GamePlayerData structure to everyone (between games)
//
void Table::SendSameGPDToEveryone(struct GamePlayerData *gpd)
{
	if (game) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) SendSameGPDToEveryone() called during a game!", _FL);
		return;
	}

	int i;
	for (i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (players[i].status==2) {				// human?
			// Send a blank game player data structure
			players[i].player->SendGamePlayerData(gpd);
		}
	}

	for (i=0 ; i<watching_player_count ; i++) {
		if (watching_players[i].status==2) {	// human?
			watching_players[i].player->SendGamePlayerData(gpd);
		}
	}
}

/**********************************************************************************
 Function Table::ClearChipsInFrontOfPlayers(void)
 date: kriskoin 2019/01/01 Purpose: set everyone's "chips in front" to zero (no chips actually ON the table)
 This function cannot be called DURING a game.
***********************************************************************************/
void Table::ClearChipsInFrontOfPlayers(void)
{
	if (game) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) ClearChipsInFrontOfPlayers() called during a game!", _FL);
		return;
	}

	FillBlankGPD(&table_gpd);
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		GameCommonData.chips[i] = players[i].chips;
	}
	FlagResendGameCommonDataToEveryone();
}

/**********************************************************************************
 Function Table::OfferPlayerAbilityToBringMoreChips(int p_index)
 date: kriskoin 2019/01/01 Purpose: when a player is low on chips, let him bring more to the table by
		  popping up a buy-in dlg for him
***********************************************************************************/
void Table::OfferPlayerAbilityToBringMoreChips(int p_index)
{
	int multiple_required = USUAL_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN;
	if (players[p_index].allowed_short_buy) {
		multiple_required = SHORTBUY_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN;
	}
	int minimum_required = big_blind_amount * multiple_required *
		GameStakesMultipliers[game_rules - GAME_RULES_START];
	char str[200];
	zstruct(str);
	sprintf(str, "You have been sat out because you do not have enough chips at the  to play.\n");
	strcat(str, "You may elect to bring more chips to the table");
	if (players[p_index].allowed_short_buy) {
			strcat(str, ", and are entitled to a short-buy.");
	} else {
			strcat(str, ".");
	}
	players[p_index].player->SendMiscClientMessage(MISC_MESSAGE_INSUFFICIENT_CHIPS,
		table_serial_number, 0, p_index, minimum_required, 0, str);
	players[p_index].sent_out_of_money_msg = TRUE;
}

/**********************************************************************************
 Function Table::SetMinimumBuyinAllowedForPlayer
 date: 24/01/01 kriskoin Purpose: on this table, set the minimum a player can buy in for (and notify the client)
***********************************************************************************/
void Table::SetMinimumBuyinAllowedForPlayer(int p_index, int chips)
{
	if (p_index < 0 || p_index >= MAX_PLAYERS_PER_GAME) {
		return;	// illegal
	}
	if (players[p_index].player==NULL) {
		return;	// illegal
	}
	// does the client include the ability to handle this msg?
	if (players[p_index].player->client_version_number.build < 0x01090003) {
		return;
	}
	players[p_index].player->SendMiscClientMessage(MISC_MESSAGE_INSUFFICIENT_CHIPS,
		table_serial_number, 0, p_index, chips, TRUE, NULL);
}

/// *** /// *** /// *** /// *** TOURNAMENT FUNCTIONS /// *** /// *** /// *** /// ***

/**********************************************************************************
 int Table::Tourn_SetInitialButton
 Date: 20180707 kriskoin :  Purpose: for a tournament table starting up, deal everyone a card and set the button to high card
 Returns: seat number for button placement
***********************************************************************************/
int Table::Tourn_SetInitialButton(void)
{
	struct GamePlayerData gpd;
	FillBlankGPD(&gpd);

	int high_player = -1;
	// deal everyone a card
	Deck *_deck = new Deck;
	Poker *_poker = new Poker;
	if (!_deck || !_poker) {	// serious trouble
		Error(ERR_INTERNAL_ERROR, "%s(%d) Tourn_SetInitialButton couldn't alloc a Deck or Poker object -- we're gonna crash", _FL);
		return 0;	// button will be first seat, let something else blow up
	}
	_deck->ShuffleDeck();
	// keep track of highest card
	Card new_card;
	int p_index;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// note that all of these conditions should be ok for a new tournament table starting up
		// but the conditions will be left in now so we can trap odd situations not expected
		int suitable_button_player = (players[p_index].status &&
							!players[p_index].sitting_out_flag &&
							players[p_index].chips &&	// added 19:::							players[p_index].post_needed == POST_NEEDED_NONE);
		if (suitable_button_player) {	// he gets a card
			new_card = _deck->DealNextCard();
			pre_tourney_gpd.cards[p_index][0] = new_card;
			pre_tourney_gpd.player_status[p_index] = PLAYER_STATUS_PLAYING;
			if (_poker->FindBetterCard(high_card, new_card) == CARD_2) {	// found a higher card
				high_card = new_card;		// highest card so far
				high_player = p_index;	// player with high card so far
			}
		}
	}
	// send it to everyone
	SendSameGPDToEveryone(&pre_tourney_gpd);
	delete _deck;
	delete _poker;	
	return high_player;
}

/**********************************************************************************
 Function Tourn_FillPreTourneyGPD(GamePlayerData *gpd)
 date: 24/01/01 kriskoin Purpose: fill a pre-tourney GamePlayerData that we're handed
***********************************************************************************/
void Table::Tourn_FillPreTourneyGPD(GamePlayerData *gpd)
{
	int show_chip_balance_on_table = (
		(table_tourn_state == TTS_WAITING)			||
		(table_tourn_state == TTS_ANNOUNCE_START)	||
		(table_tourn_state == TTS_MOVE_CHIPS_OUT)
	);
	
	for (int i=0 ; i<MAX_PLAYERS_PER_GAME ; i++) {
		if (show_chip_balance_on_table) {
			//kp(("GCDc %d = %d\n", i, GameCommonData.chips[i]));
			gpd->chips_in_front_of_player[i] = GameCommonData.chips[i];
		} else {
			gpd->chips_in_front_of_player[i] = 0;
		}
	}
}

/**********************************************************************************
 void Table::Tourn_SendPreTourneyGPD
 Date: 20180707 kriskoin :  Purpose: send the pretourney GPD to a tournament table
***********************************************************************************/
void Table::Tourn_SendPreTourneyGPD(void)
{
	Tourn_FillPreTourneyGPD(&pre_tourney_gpd);
	SendSameGPDToEveryone(&pre_tourney_gpd);
}

/**********************************************************************************
 void Table::Tourn_BetweenGamesProcessing
 Date: 20180707 kriskoin :  Purpose: do whatever is needed between games for a tournament table
 Note: returns number of players left (1 if someone just won)
***********************************************************************************/
int Table::Tourn_BetweenGamesProcessing(void)
{
	if (!tournament_table) {	
		return -1;
	}
	// increment internal tournament hand counter in neccessary -- this sets it for next game
	tournament_hand_number++;	// for next hand
	summary_info.tournament_hand_number = (BYTE8)min(255,tournament_hand_number);

	int marked_for_removal[MAX_PLAYERS_PER_GAME]; // used below for queued removes
	
	int just_lost_count = 0, players_left = 0, i;
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		marked_for_removal[i] = FALSE;	// needs to be initialized somewhere
		if (players[i].chips > 0) {	// still has $, counts as live
			players_left++;
		}
	}
	int just_lost[MAX_PLAYERS_PER_GAME];
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (!players[i].chips && t_was_playing_last_hand[i]) {	// he's outta there
			just_lost[i] = t_chips_last_hand[i];	// might need these to break a tie
			just_lost_count++;
		} else {
			just_lost[i] = 0;
		}
	}
	// we have a number of players who just busted out and how many chips they had at the start
	// of the last hand.  figure out what place they finished in (tied if they had same number of
	// chips...
	int loop_count = 0;	// keep track how many times we ran through
	while (just_lost_count) {
		if (just_lost_count < 0) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) just_lost_count is negative - serious logic problem", _FL);
			break;
		}
		loop_count++;
		// find out the highest number of chips at the start of the hand
		int low_chip_count = 9999999;
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (just_lost[i]) {		// can't be zero
				low_chip_count = min(low_chip_count, just_lost[i]);
			}
		}
		// now find how many people had this high chip count
		int current_low_chip_players = 0;
		int current_low_chips[MAX_PLAYERS_PER_GAME]; // T/F
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (just_lost[i] == low_chip_count) {
				current_low_chips[i] = TRUE;
				current_low_chip_players++;
			} else {
				current_low_chips[i] = FALSE;
			}
		}
		// now we have a list of people who were knocked out with this many chips last hand
		int official_place_finished = players_left+just_lost_count; // 10th, 9th, tied for 8th, 6th, etc
		int prize_sum = 0;
		// if 5 people get knocked out and tie for the same place, be sure we properly
		// grab and allocate percentages above them for the payoff... ie, if 4 people tie
		// for 10th place, they'd all get nothing unless we add in 7th and 8th place for them
		int players_tied_this_level = 0;	// incrementing count of how many players tied
		int payout_percentage = 0;
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (current_low_chips[i]) {
				// figure out and do payouts
				int percentage_index = official_place_finished-players_tied_this_level;
				// test below should be > , not >= because they run from 1 to 10th
				if (percentage_index < 0 || percentage_index > MAX_PLAYERS_PER_GAME) {
					Error(ERR_ERROR,"%s(%d) percentage_index has gone to %d -- zeroing",
						_FL, percentage_index);
					percentage_index = 0;	// will reference zero
				}
				payout_percentage = PayoutPercentages[percentage_index];
				prize_sum += (payout_percentage * tournament_prize_pool / 100);
				players_tied_this_level++; // needed to add the right amount for the payout
			}
		}
		int place_finished_to_accounce = official_place_finished - players_tied_this_level+1;
		int amount_won = 0;
		if (current_low_chip_players) {
			amount_won = prize_sum / current_low_chip_players;
		}
		// ready to announce and pay off
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (current_low_chips[i]) {
				pr(("%s(%d) %s %s %d%s place, won %d, CLP(JL) = %d, jlc= %d, last hand = %d\n",
					_FL, GameCommonData.name[i],
					(current_low_chip_players == 1 ? "finished in" : "tied for"),
					place_finished_to_accounce,
					possessive_suffix[place_finished_to_accounce],
					amount_won,
					just_lost[i],
					just_lost_count,
					t_chips_last_hand[i]));

				marked_for_removal[i] = TRUE;	// he'll be reomved from the table after all announcements
				// do all announcements and payouts
				int tied_flag = (current_low_chip_players > 1);
				Tourn_AnnounceAndPayPlayer(i, tied_flag, place_finished_to_accounce, amount_won);
				just_lost[i] = FALSE;	// we're done with this player
				just_lost_count--;
			}
		}
	}
	if (loop_count > 1) {	// we ran through more than once... means we had to split a tie
		SendDealerMessage("The player who started the hand with more chips is placed higher", CHATTEXT_DEALER_WINNER);
	}
	// perhaps we have a winner?
	if (players_left == 1) {
		// find the winner
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (players[i].chips) {	// here's the winner
				int winners_share = PayoutPercentages[1] * tournament_prize_pool / 100;
				marked_for_removal[i] = TRUE;	// he'll be reomved from the table after all announcements
				Tourn_AnnounceAndPayPlayer(i, FALSE, 1, winners_share);
				break;
			}
		}
	}
	// unseat all players marked for removal from the table
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (marked_for_removal[i]) {
			// remove the player from the table
			Player *p = players[i].player;
			RemovePlayerFromTable(i, FALSE); // remove but don't close the window
			// add him back in as watching
			if (p) {
				AddWatchingPlayer(p);
			}
		}
	}
	Tourn_CalculatePartialPayouts();
	return players_left;
}

/**********************************************************************************
 void Table::Tourn_AnnounceAndPayPlayer()
 Date: 20180707 kriskoin :  Purpose: announce and payout a tournament player who has just busted out (or won)
***********************************************************************************/
void Table::Tourn_AnnounceAndPayPlayer(int p_index, int tied_flag, int official_placing, WORD32 amount_won)
{
	ttsi[p_index].level_busted_out = tournament_current_level;
	ttsi[p_index].time_busted_out = SecondCounter;
	ttsi[p_index].game_busted_out = tournament_current_game;
	ttsi[p_index].official_placing = official_placing;
	ttsi[p_index].amount_won = amount_won;
	ttsi[p_index].balance_before_payoff = SDB->GetChipsInBankForPlayerID(GameCommonData.player_id[p_index], CT_REAL);
	ttsi[p_index].balance_after_payoff = ttsi[p_index].balance_before_payoff + amount_won;
	//ttsi[p_index].refund_contribution = ttsi[p_index].buy_in - amount
	// dealer should it
	char str[200];
	zstruct(str);
	sprintf(str,"%s %s %d%s place",
		GameCommonData.name[p_index],
		(tied_flag ? "tied for" : "finished in"),
		official_placing,
		possessive_suffix[official_placing]);
	if (amount_won) {
		char curr_str1[MAX_CURRENCY_STRING_LEN];
		zstruct(curr_str1);
		sprintf(str+strlen(str), " and won %s!",
			CurrencyString(curr_str1, amount_won, CT_REAL));
	}
	SendDealerMessage(str, CHATTEXT_DEALER_WINNER);
	// personalize it for the player
	Player *p = players[p_index].player;
	if (p) {
		zstruct(str);
		if (amount_won) {
		  #if 1	// 2022 kriskoin
			strcpy(str, "Congratulations!!! ");
		  #else
			sprintf(str, "%s ", official_placing == 1 ? "CONGRATULATIONS!" : "Congratulations!");
		  #endif
		}
		sprintf(str+strlen(str), "You %s %d%s place",
			(tied_flag ? "tied for" : "finished in"),
			official_placing,
			possessive_suffix[official_placing]);
		if (amount_won) {
			char curr_str1[MAX_CURRENCY_STRING_LEN];
			char curr_str2[MAX_CURRENCY_STRING_LEN];
			char curr_str3[MAX_CURRENCY_STRING_LEN];
			zstruct(curr_str1);
			zstruct(curr_str2);
			zstruct(curr_str3);
			sprintf(str+strlen(str),
				" and won %s!\n"
				"\n"
				"Your available balance has increased from %s to %s.",
				CurrencyString(curr_str1, amount_won, CT_REAL),
				CurrencyString(curr_str2, ttsi[p_index].balance_before_payoff, CT_REAL),
				CurrencyString(curr_str3, ttsi[p_index].balance_after_payoff, CT_REAL));
		} else {
			strcat(str, ".");
		}
		// announce it to the player
		p->SendMiscClientMessage(MISC_MESSAGE_REQ_TOURN_SUMMARY_EMAIL, table_serial_number, 0,
			official_placing, 0, 0, str);
	}
	// pay the chips
	WORD32 player_id = table_info.players[p_index].player_id;
	SDB->TournamentCashout(player_id, table_serial_number,
		players[p_index].chips,	// tournament chips -- either zero or all of them
		amount_won,				// player's cash payout
		0);	// tournament fee -- never refunded from here
	// tell everyone in the tournament how many chips are left in the tournament chip universe
	// might be needed for crash recovery, so SDB needs to know too
	
	
	// 24/01/01 kriskoin:
  #if 0	// tested and works, turn it on if ever needed
		WORD32 tournament_chips_left = SDB->GetTotalTournamentChipsLeft(player_id);
		kp(("%s(%d) Setting total_tournament_chips_left to %d for everyone\n", _FL, tournament_chips_left));
		for (int j=0; j < MAX_PLAYERS_PER_GAME; j++) {
			SDB->SetTotalTournamentChipsLeft(ttsi[j].player_id, tournament_chips_left);
		}
	// we need to keep track of the highest payout we've seen so far (for early shutdown or crash)
	if (amount_won > tournament_highest_payout) {
		tournament_highest_payout = amount_won;
		kp(("%s(%d) Setting tournament_highest_payout to %d for everyone\n", _FL, tournament_highest_payout));
		for (int k=0; k < MAX_PLAYERS_PER_GAME; k++) {
			SDB->SetHighestTournamentPayout(ttsi[k].player_id, amount_won);
		}
	}
	// we need to tell everyone what the current creditable pot is
	WORD32 tournament_pot_left = SDB->GetTotalTournamentPotLeft(player_id);
	kp(("%s(%d) Setting total_tournament_pot_left to %d for everyone\n", _FL, tournament_pot_left));
	for (int m=0; m < MAX_PLAYERS_PER_GAME; m++) {
		SDB->SetTotalTournamentPotLeft(ttsi[m].player_id, tournament_pot_left);
	}
  #endif
	players[p_index].chips = 0;	// now that he's cashed out, there are no tournament chips left at the table
	if (amount_won) {	// don't log it if he didn't win anything
		char str[50];
		zstruct(str);
		sprintf(str, "Tourn #%d payout", table_serial_number);
		/// !!! TournamentRec_ID belongs in one of the two entries below
		PL->LogFinancialTransfer(LOGTYPE_TRANS_TOURNAMENT_PAYOUT,
			player_id, player_id, amount_won, 0, 0, CT_REAL, str);
		if (players[p_index].player) {
			players[p_index].player->ChipBalancesHaveChanged();
		}
	}
}

/**********************************************************************************
 void Table::Tourn_TextForSummaryEmail()
 Date: 20180707 kriskoin :  Purpose: append a line of text to the summary email for this tournament
 NOTE: 'player' can also be one of two defines below
 NOTE: if it's sent to an individual player, it is not written to admin automatically
***********************************************************************************/
// add text to the summary email
//#define TTFSE_ALL_PLAYERS	998	// send to all players (and admin)
//#define TTFSE_ADMIN_ONLY	999	// send to admin summary only

void Table::Tourn_TextForSummaryEmail(int player, char *text, ...)
{
	// validate player index
	if ( (player >=0 && player < MAX_PLAYERS_PER_GAME) || player == TTFSE_ALL_PLAYERS || player == TTFSE_ADMIN_ONLY) {
		// we're ok
	} else {
		Error(ERR_ERROR,"%s(%d) Tourn_TextForSummaryEmail called with bad index (%d)", _FL, player);
		return;
	}
	// assign temporary filenames if we don't already have them
	if (!t_summary_filename[0]) {
		MakeTempFName(t_summary_filename, "tr");
	}
	int i;
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (ttsi[i].name[0] && !ttsi[i].temp_file_name[0]) {
			MakeTempFName(ttsi[i].temp_file_name, "tr");
		}
	}
	// build the string
	char str[1000];
	zstruct(str);
	va_list arg_ptr;
	va_start(arg_ptr, text);
	vsprintf(str, text, arg_ptr);
	va_end(arg_ptr);
	// open file and write to admin summary
	FILE *out;
	if (player >= TTFSE_ALL_PLAYERS) {	// add it to admin here too
		if ((out = fopen(t_summary_filename, "at")) == NULL) {
			Error(ERR_ERROR,"%s(%d) Couldn't open t_summary file (%s) for append", _FL, t_summary_filename);
			return;
		}
		fprintf(out, "%s", str);
		FCLOSE(out);
	}
	// perhaps it was admin only
	if (player == TTFSE_ADMIN_ONLY) {
		return;
	}
	if (player == TTFSE_ALL_PLAYERS) {	// send it to all players
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (ttsi[i].name[0] && ttsi[i].temp_file_name[0]) {
				if ((out = fopen(ttsi[i].temp_file_name, "at")) == NULL) {
					Error(ERR_ERROR,"%s(%d) Couldn't open t_summary file (%s) for append", _FL, ttsi[i].temp_file_name);
					continue;
				}
				fprintf(out, "%s", str);
				FCLOSE(out);
			}
		}
	} else {	// single player
		if (ttsi[player].name[0] && ttsi[player].temp_file_name[0]) {
			if ((out = fopen(ttsi[player].temp_file_name, "at")) == NULL) {
				Error(ERR_ERROR,"%s(%d) Couldn't open t_summary file (%s) for append", _FL, ttsi[player].temp_file_name);
			} else {
				fprintf(out, "%s", str);
				FCLOSE(out);
			}
		}
	}
}

/**********************************************************************************
 Function Table::Tourn_CancelTournamentInProgress(void)
 date: 24/01/01 kriskoin Purpose: cancel a tournament that's already in progress
***********************************************************************************/
void Table::Tourn_CancelTournamentInProgress(void)
{
	// send this to everyone
	Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS,
		//         10        20        30        40        50        60        70
		"The tournament needed to be cancelled before the tournament could\n"
		"be played to completion.  As per our fairness policy, all players in\n"
		"the tournament who still have chips will a receive a full refund of\n"
		"both their buy-in and their entry fee.\n"
		"\n"
		"In addition, the remaining prize pool, if any (from the players\n"
		"who busted out) will be paid out completely to these remaining\n"
		"players. The amount awarded to each player is proportionate to\n"
		"the number of tournament chips the player was holding when play\n"
		"was stopped.  Adjustments are made to ensure players are awarded\n"
		"the minimum prize (if there is one) and that no player is\n"
		"awarded more than the first prize amount. This resolution treats\n"
		"all players fairly and ensures that the entire prize pool gets\n"
		"paid out to the players who were still playing. We apologize for\n"
		"any inconveniences.\n"
		"\n");

	int printed_explanation = FALSE;
	int found_at_least_one = FALSE;	// will be set TRUE if there is at least one cancellation
	// fix up the emails and ship them
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		// fill all info we might need
		TournamentRefundStructure trs;
		zstruct(trs);
		SDB->FillTournamentRefundStructure(ttsi[i].player_id, &trs);
		// some players will already have been eliminated -- only those live need a chip breakdown
		if (!ttsi[i].official_placing) {	// he's still in the tournament
			found_at_least_one = TRUE;
			// fix up the TTSIs bit so the summary email makes some sense
			#define CANCELLED_TOURNAMENT_PLACING	999999
			ttsi[i].time_busted_out = SecondCounter; // 20001012HK (fix -50 we saw in testing)
			ttsi[i].official_placing = CANCELLED_TOURNAMENT_PLACING;
			ttsi[i].amount_won = trs.tournament_total_refund;
			ttsi[i].balance_before_payoff = SDB->GetChipsInBankForPlayerID(ttsi[i].player_id, CT_REAL);
			ttsi[i].balance_after_payoff = ttsi[i].balance_before_payoff + ttsi[i].amount_won;
			char curr_str1[MAX_CURRENCY_STRING_LEN];
			char curr_str2[MAX_CURRENCY_STRING_LEN];
			char curr_str3[MAX_CURRENCY_STRING_LEN];
			char curr_str4[MAX_CURRENCY_STRING_LEN];
			char curr_str5[MAX_CURRENCY_STRING_LEN];
			char curr_str6[MAX_CURRENCY_STRING_LEN];
			char curr_str7[MAX_CURRENCY_STRING_LEN];
			char curr_str8[MAX_CURRENCY_STRING_LEN];
			zstruct(curr_str1);
			zstruct(curr_str2);
			zstruct(curr_str3);
			zstruct(curr_str4);
			zstruct(curr_str5);
			zstruct(curr_str6);
			zstruct(curr_str7);
			zstruct(curr_str8);
			// pop up a message if he's there
			Player *p = players[i].player;
			char str[MAX_MISC_CLIENT_MESSAGE_LEN];
			zstruct(str);
			if (p) {
				sprintf(str,
					"We apologize, we were unable to continue this tournament\n"
					"it was cancelled before completion. Details about the payout is being\n"
					"emailed to you currently:\n"
					"\n"
					"Your tournament chips: %s\n"
					"Total tournament chips: %s\n"
					"Your percentage of the total tournament chips: %2.2f%%\n"
					"Total eligible prize pool: %s\n"
					"Your share of the prize pool: %s\n\n"
					"TOTAL REFUND: %s (%s + %s + %s)",
					CurrencyString(curr_str1, trs.tournament_chips_in_play, CT_TOURNAMENT),
					CurrencyString(curr_str2, trs.tournament_total_chips_left_in_play, CT_TOURNAMENT),
					trs.percentage_held,
					CurrencyString(curr_str3, trs.tournament_creditable_pot, CT_REAL, TRUE),
					CurrencyString(curr_str4, trs.tournament_partial_payout, CT_REAL, TRUE),
					CurrencyString(curr_str5, trs.tournament_total_refund, CT_REAL, TRUE),
					CurrencyString(curr_str6, trs.tournament_partial_payout, CT_REAL, TRUE),
					CurrencyString(curr_str7, trs.buyin_amount_paid, CT_REAL, TRUE),
					CurrencyString(curr_str8, trs.tournament_fee_paid, CT_REAL, TRUE));
				// ship the message
				pr(("%s(%d) msg is %d chars long (max %d)\n", _FL, strlen(str), MAX_MISC_CLIENT_MESSAGE_LEN));
				// note: we send this to the cardroom, not the specific table... because the
				// table will shutdown and it will take the message with it
			  #if 0
				p->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, table_serial_number, 0,0,0,0, "%s", str);
			  #else
				p->SendMiscClientMessage(MISC_MESSAGE_TOURNAMENT_SHUTDOWN_NOTE, 0, 0,0,0,0, "%s", str);
			  #endif
			}
			// one-time explanation for everyone
			if (!printed_explanation) {
				printed_explanation = TRUE;
				sprintf(str,
//					"Starting total prize pool: %s\n"
//					"Total prizes paid out plus buy-in refunds: %s\n"
//					"Less total prizes paid before tournament cancellation: %s\n"
//					"Less tournament buy-in refunds: %s\n"
//					"Equals prize pool left to distribute: %s\n"
//					"\n"
					"Each of these players receives:\n"
					"%s tournament buy-in refund\n"
					"%s tournament fee refund\n"
					"PLUS:\n"
					"%d players were eliminated before the tournament cancellation\n"
					"%s available prize pool to distribute to all remaining players",
					CurrencyString(curr_str1, trs.buyin_amount_paid, CT_REAL, TRUE),
					CurrencyString(curr_str2, trs.tournament_fee_paid, CT_REAL, TRUE),
					tournament_players_eliminated,
					CurrencyString(curr_str3, trs.tournament_creditable_pot, CT_REAL, TRUE));
				Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "%s\n", str);
			}
			// add the individual entry to summary emails
			if (trs.tournament_creditable_pot) {
				// prize pool had something in it...
				sprintf(str, "  %s, %s chips (%2.2f%%). %s from prize pool, %s total payout",
					ttsi[i].name,
					CurrencyString(curr_str1, trs.tournament_chips_in_play, CT_TOURNAMENT),
					trs.percentage_held,
					CurrencyString(curr_str2, trs.tournament_partial_payout, CT_REAL, TRUE),
					CurrencyString(curr_str3, ttsi[i].amount_won, CT_REAL, TRUE));
			} else {
				// prize pool was empty (nobody busted out).  Don't rub the
				// chip imbalances in their faces.
				sprintf(str, "  %s, %s total refunded",
					ttsi[i].name,
					CurrencyString(curr_str3, ttsi[i].amount_won, CT_REAL, TRUE));
			}

			Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "%s\n", str);
			// do the actual cashout
			SDB->TournamentCashout(&trs);
		}
	}
	if (tournament_players_eliminated) {	// this will seperate cancellations from those who already busted out
		Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "\nPlayers who were eliminated before the tournament cancellation:\n");
	}
	// send the emails
	Tourn_SendEmails();
}
				
/**********************************************************************************
 Function Tourn_SendEmails(void)
 date: 24/01/01 kriskoin Purpose: finalize and send the tournament summary emails
***********************************************************************************/
void Table::Tourn_SendEmails(void)
{
	int i, amount_won_last_loop = 0;	// place break between winners and non-winners
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	char str1[100];
	zstruct(str1);
	
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		// we need his email address
		SDBRecord player_rec;	// the result structure
		zstruct(player_rec);
		if (SDB->SearchDataBaseByPlayerID(ttsi[i].player_id, &player_rec) >= 0) {
			strnncpy(ttsi[i].email_address, player_rec.email_address, MAX_EMAIL_ADDRESS_LEN);
		} else {
			Error(ERR_ERROR,"%s(%d) Couldn't find email address for '%s' (%08lx)",
				_FL, ttsi[i].name, ttsi[i].player_id);
			remove(ttsi[i].temp_file_name);
			continue;	
		}
		// run through all ttsi entries to find those that match
		for (int ttsi_index=0; ttsi_index < MAX_PLAYERS_PER_GAME; ttsi_index++) {
			if (ttsi[ttsi_index].official_placing == i+1) {	// (these run 1 to 10)
				CurrencyString(curr_str1, ttsi[ttsi_index].amount_won, CT_REAL);
				//  1st: CienFuegos  ($4,000)
				//  2nd: TheHammer  ($2,500)
				// 10th: WhyNot  ($200)
				// put a space if there was a payout last entry and now there isn't
				if (amount_won_last_loop && !ttsi[ttsi_index].amount_won) {
					amount_won_last_loop = 0;
					Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "\n");
				}
				sprintf(str1, " %2d%s: %s",
					i+1, possessive_suffix[i+1], ttsi[ttsi_index].name);
				// if he won something append it
				if (ttsi[ttsi_index].amount_won) {
					amount_won_last_loop = ttsi[ttsi_index].amount_won;
					char str2[40];
					zstruct(str2);
					sprintf(str2,", %s prize awarded", curr_str1);
					strcat(str1, str2);
				}
				sprintf(str1+strlen(str1), " (Level %s, Game #%d)",
					szRomanNumerals[ttsi[ttsi_index].level_busted_out], ttsi[ttsi_index].game_busted_out);
				int minutes = (ttsi[ttsi_index].time_busted_out - tournament_start_time + 59) / 60;
				Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "%s [%d minute%s]\n",
					str1,
					minutes,
					minutes==1 ? "" : "s");
				for (int tei=0; tei < MAX_PLAYERS_PER_GAME; tei++) {
					Tourn_TextForSummaryEmail(tei, "%s%s\n",
						str1, (ttsi_index == tei ? " ***" : ""));
				}
			}
		}
	}
	// loop again to do cash summaries
	Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "\n");
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (ttsi[i].amount_won) {	// something there
			char curr_str1[MAX_CURRENCY_STRING_LEN];
			char curr_str2[MAX_CURRENCY_STRING_LEN];
			char curr_str3[MAX_CURRENCY_STRING_LEN];
			zstruct(curr_str1);
			zstruct(curr_str2);
			zstruct(curr_str3);
			Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "%s: ", ttsi[i].name);
			Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY,
				"Balance before payout: %s | "
				"Prize payout: %s | "
				"Balance after payout: %s\n",
				CurrencyString(curr_str1, ttsi[i].balance_before_payoff, CT_REAL),
				CurrencyString(curr_str2, ttsi[i].amount_won, CT_REAL),
				CurrencyString(curr_str3, ttsi[i].balance_after_payoff, CT_REAL));
			Tourn_TextForSummaryEmail(i,
				"\n"
				"Available account balance before payout: %s\n"
				"Prize payout: %s\n"
				"Available account balance after payout: %s\n",
				CurrencyString(curr_str1, ttsi[i].balance_before_payoff, CT_REAL),
				CurrencyString(curr_str2, ttsi[i].amount_won, CT_REAL),
				CurrencyString(curr_str3, ttsi[i].balance_after_payoff, CT_REAL));
		} else {
		  #if 0
			Tourn_TextForSummaryEmail(i, "You finished out of the money\n");
		  #endif
		}
	}
	// build footer for all
	Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "\nTournament finished %s (CST) --  Game #%s\n",
		TimeStrWithYear(),
		IntegerWithCommas(curr_str1, GameCommonData.game_serial_number));
	// for admin, tell us how long it took	
	Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "Elapsed time: %d minutes\n",
			(SecondCounter-tournament_start_time+59)/60);

	int seconds_total = 0;
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		seconds_total += ttsi[i].time_busted_out - tournament_start_time;
	}
	Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY, "Total player minutes: %d\n",
			(seconds_total +59) / 60);

	// seperator for everyone
	Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS, "\n----\n");

	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (!ttsi[i].dont_send_email && ttsi[i].temp_file_name[0] && ttsi[i].email_address[0]) {
			Tourn_TextForSummaryEmail(TTFSE_ADMIN_ONLY,
				"This email was computer generated and emailed to %s (%s)\n",
				ttsi[i].name,
				ttsi[i].email_address);
			Tourn_TextForSummaryEmail(i,
				"This email was computer generated and emailed to %s\n",
				ttsi[i].email_address);
		}
	}

	Tourn_TextForSummaryEmail(TTFSE_ALL_PLAYERS,
		"If you have any questions, please see the tournament web page at\n"
		"http://www.kkrekop.io/tournaments.html\n");
	// now email it to everyone
	char msg_subject[50];
	zstruct(msg_subject);
  #if 0	//kriskoin: 	sprintf(msg_subject, "%sTournament Results",
			iRunningLiveFlag ? "" : "Test ");
  #else
	sprintf(msg_subject, "%s%s Tournament Results",
			iRunningLiveFlag ? "" : "Test ",
			CurrencyString(curr_str1, summary_info.big_blind_amount, CT_PLAY, TRUE));
  #endif
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		// we send it unless the player said he didn't want it
		pr(("%s(%d) for %s, dont_send = %d, temp_file_name = %s, email_address = %s\n",
			_FL, ttsi[i].name, ttsi[i].dont_send_email, ttsi[i].temp_file_name, ttsi[i].email_address));
		if (ttsi[i].dont_send_email) {
			pr(("%s(%d) Player '%s' has chosen to not receive a summary email for tournament serial #%d\n",
				_FL, ttsi[i].name, table_serial_number));
			remove(ttsi[i].temp_file_name);
		} else {
			if (ttsi[i].temp_file_name[0] && ttsi[i].email_address[0]) {
				Email(  ttsi[i].email_address,
					"Desert Poker",
					"support@kkrekop.io",
					msg_subject,
					ttsi[i].temp_file_name,
					NULL,	// bcc
					TRUE);	// delete when sent
			}
		}
	}
  #if 1  // for now, we'll send an admin summary to ourselves as well
	Email(  "tournaments@kkrekop.io",
			"Desert Poker",
			"support@kkrekop.io",
			msg_subject, 
			t_summary_filename, 
			NULL,	// bcc
			TRUE);	// delete when sent
  #else	// if we disable this, we have to manually delete the temp file
	remove(t_summary_filename);
  #endif
}

/**********************************************************************************
 Function Tourn_CalculatePartialPayouts(void)
 date: 24/01/01 kriskoin Purpose: calculate the partial payouts we'd use if this tournament is stopped
***********************************************************************************/
void Table::Tourn_CalculatePartialPayouts(void)
{
	int i, refundable_pool = 0, best_placing_seen = MAX_PLAYERS_PER_GAME;
	tournament_players_eliminated = 0;	// how many have been eliminated?
	tournament_summary_prizes_paid = 0;	// how much have we paid out so far?
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		// anyone with a placing is out, and therefore might be contributing to
		// the potential pool that needs to get distributed
		// note that if he's won any amount, he's pulling money out of the pool
		int potential_contribution = 0 - ttsi[i].amount_won;
		if (ttsi[i].official_placing) {	// let's get his contribution
			tournament_players_eliminated++;
			tournament_summary_prizes_paid += ttsi[i].amount_won;
			potential_contribution += ttsi[i].buy_in;
			// we need to keep track of the best placing we've seen so far
			best_placing_seen = min(ttsi[i].official_placing, best_placing_seen);
			best_placing_seen = max(1, best_placing_seen);	// in case of one-hand tournament (impossible?)
		}

		refundable_pool += potential_contribution;
	}
	pr(("%s(%d) figure a %d refundable pot for the tournament\n", _FL, refundable_pool));
	// the minimum allowed must be set for everyone -- so if it exists, apply it now
	int minimum_payout = max(0,(int)((PayoutPercentages[best_placing_seen-1]*tournament_prize_pool/100)-ttsi[0].buy_in));
	pr(("%s(%d) ppp=%d, tpp=%d, bi=%d\n",
		_FL, PayoutPercentages[best_placing_seen-1],tournament_prize_pool,ttsi[0].buy_in));
	pr(("%s(%d) minimum partial payout should be %d\n", _FL, minimum_payout));
	WORD32 payouts[MAX_PLAYERS_PER_GAME];
	zstruct(payouts);
	// we want to store the original refundable pot for display purposes
	int original_refundable_pool = refundable_pool;
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		SDB->SetCreditableTournamentPot(ttsi[i].player_id, original_refundable_pool);
		// player eligible for minimum refund?
		if (!ttsi[i].official_placing && players[i].chips && tournament_total_tourney_chips) {
			payouts[i] = minimum_payout;
			refundable_pool -= minimum_payout;
			if (refundable_pool < 0) {	// how the hell did that happen...
				Error(ERR_ERROR,"%s(%d) refundable_pool has gone below zero!! (%d)", _FL, refundable_pool);
				refundable_pool = 0;
			}
		}
	}
	// figure out some initial breakdowns
	WORD32 highest_partial_payout = 0;
	int payout_player_count = 0;
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (ttsi[i].player_id) {
			if (!ttsi[i].official_placing && players[i].chips && tournament_total_tourney_chips) {
				// 24/01/01 kriskoin:
				// for display purporses
				double percentage_held = (double)((100.0 * players[i].chips / tournament_total_tourney_chips));
				// this could result in roundoff error which will always be in favour of the
				// player, who may end up with a penny more than he should've gotten
				WORD32 partial_payout = (int)((refundable_pool*percentage_held+50) / 100);
				payouts[i] += partial_payout;
				highest_partial_payout = max(highest_partial_payout, partial_payout);
				pr(("%s(%d) figured partial payout of %d for %s\n", _FL, payouts[i], ttsi[i].name));
				payout_player_count++;
			}
		}
	}
	// figure out if we need to trim anything from first place
	int maximum_payout = (int)(PayoutPercentages[1]*tournament_prize_pool/100)-ttsi[0].buy_in;
	pr(("%s(%d) maximum partial payout should be %d\n", _FL, maximum_payout));
	int winner_excess = highest_partial_payout - maximum_payout;
	if (winner_excess > 0) {	// there's extra that needs to be dealt with
		int extra_to_award_each_player = (int)(winner_excess / (payout_player_count-1) );
		int extra_left_over = winner_excess - extra_to_award_each_player*(payout_player_count-1);
		// redistribute the extra...
		for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
			if (payouts[i]) {	// here's one of them
				// if he's the actual winner, bring him down to size
				if (payouts[i] == highest_partial_payout) {	// it's him
					payouts[i] = maximum_payout;
					pr(("%s(%d) %s got scaled down from %d to %d\n", 
						_FL, ttsi[i].name, highest_partial_payout, maximum_payout));
				} else {	// it's someone else, give them the extra
					payouts[i] += extra_to_award_each_player;
					pr(("%s(%d) %s got an extra %d\n", _FL, ttsi[i].name, extra_to_award_each_player));
					if (extra_left_over) {	// first player gets the remnant
						payouts[i] += extra_left_over;
						pr(("%s(%d) %s got a little extra %d\n", _FL, ttsi[i].name, extra_left_over));
						extra_left_over = 0;
					}
				}
			}
		}
	}
	// tell SDB the numbers in case we blow up
	for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (ttsi[i].player_id) {
			SDB->SetTournamentPartialPayout(ttsi[i].player_id, payouts[i]);
		}
	}
}
