//*********************************************************

//
//	Table routines.
//	Server side only.
//
// 
//
//*********************************************************

#ifndef _TABLE_H_INCLUDED
#define _TABLE_H_INCLUDED

#ifndef _PLAYER_H_INCLUDED
  #include "player.h"
#endif
#ifndef _GAME_H_INCLUDED
  #include "game.h"
#endif
#ifndef _COMMONRO_H_INCLUDED
  #include "commonro.h"
#endif

// kriskoin 
#include "ecash.h"
// end kriskoin 
struct WaitListCallInfo {
	WORD32 player_id;			// player_id we're waiting for a response from
	WORD32 request_time;		// SecondCounter when we first offered the seat to the user.
	WORD32 next_send_time;		// SecondCounter when we should next (re-)send the SeatAvail packet
	BYTE8  skipped_players;		// # of players we skipped ahead of (because they were playing elsewhere)
};

struct TTournamentSummaryInfo {
	char name[MAX_COMMON_STRING_LEN];			// player's name
	char email_address[MAX_EMAIL_ADDRESS_LEN];	// player's email address
	char temp_file_name[MAX_FNAME_LEN];			// temp email file for this player
	WORD32 player_id;					// player's player ID
	WORD32 buy_in;
	WORD32 starting_account_balance;	// how much he had in his account before tourney
	WORD32 balance_before_payoff;	// how much before we paid him off
	WORD32 amount_won;				// prize money awarded
	WORD32 balance_after_payoff;	// how much before we paid him off
	WORD32 tournament_creditable_pot;// how much is in the creditable pot?
	WORD32 refund_contribution;		// if we need to do refunds, how much this player puts in
	WORD32 potential_refund;		// how much he'd get back from tourney pool if we do a refund
	int official_placing;	// position he finished (may not be unique)
	int level_busted_out;	// level player was at when he busted out
	int game_busted_out;	// torunament game # player was at when he busted out
	int dont_send_email;	// T/F, defaults to sending email
	WORD32 time_busted_out;	// SecondCounter when this player busted out
};

class Table {
public:
	// The Table() constructor must know who the card room was that created
	// it, but we can't used CardRoom * because cardroom.h includes table.h
	// first.  It's one of those chicken and the egg problems.
	Table(void *input_cardroom_ptr, WORD32 input_table_serial_number, char *table_name);
	~Table(void);

	// Set the game parameters for this table.  These parameters can only be
	// set once per table.  If a different game is to be played, a new table
	// object should be used instead.
	// Blind amounts are specified in 'chips', not dollars.
	ErrorType SetGameType(ClientDisplayTabIndex client_display_tab_index,
						  GameRules game_rules,
						  int max_number_of_players,
						  int small_blind_amount,
						  int big_blind_amount,
						  ChipType chip_type,
						  int passed_game_disable_bits,
						  RakeType rake_profile);

	// Check if a player is seated at this table
	// returns: TRUE for seated, FALSE for not seated.
	int Table::CheckIfPlayerSeated(WORD32 player_id);
	int Table::CheckIfPlayerSeated(Player *input_player_ptr);
	int Table::CheckIfPlayerSeated(WORD32 player_id, int *output_seating_position);

	// Determine if a player is currently involved in a hand.
	// If they are watching, sitting out, folded, or not at the table,
	// then they are not involved.
	// If they are playing or are All-In, then they ARE in involved
	// in a hand.
	// returns: TRUE if involved in a hand, FALSE otherwise.
	int Table::IsPlayerInvolvedInHand(WORD32 player_id);

	// Add a player to a table.  They will be added to the seated and
	// playing list.  If there is no room at the table or there is
	// some other reason a player can't be added to the table, an
	// error is returned.  Players can be added during a game but they
	// won't get to play until the next game starts.
	// Pass -1 for seating position if you don't care.
	ErrorType AddPlayer(Player *input_player_ptr, int seating_position, WORD32 chips);

	// Buy more chips for a player that's already sitting
	ErrorType AddMoreChipsForPlayer(Player *input_player_ptr, int seating_position, WORD32 buy_in_chips);

	// Add a computer player to a table.  Similar to AddPlayer() but for computer players.
	ErrorType AddComputerPlayer(int seating_position);

	// Add a player to a table's 'watching' list.  If there is no
	// room at the table or there is some other reason a player
	// can't be added to the table, an error is returned.
	ErrorType AddWatchingPlayer(Player *input_player_ptr);
	
	//cris 4-2-2004
	int Table::findPlayer4LoginName(char *loginName);
    //end cris 4-2-2004
	
	// Remove a player from a table.  This can be used to remove him
	// for any number of reasons.  This function will make sure
	// his chips total gets updated and will notify the player object
	// that it no longer is joined to this table.  The player object
	// in turn will take care of notifying the remote client.
	ErrorType RemovePlayerFromTable(int seating_index);
	ErrorType RemovePlayerFromTable(int seating_index, int notify_player);
	ErrorType RemovePlayerFromTable(Player *player_ptr);

	// Remove a player from the watching list
	ErrorType RemoveWatchingPlayer(int watching_player_index);
	ErrorType RemoveWatchingPlayer(int watching_player_index, int notify_player_flag);
	ErrorType RemoveWatchingPlayer(Player *input_player_ptr);
	ErrorType RemoveWatchingPlayer(Player *input_player_ptr, int notify_player_flag);

	// Do anything possible for this table.  Process incoming input
	// packets, start new games, move current games along, etc.
	// This function will not block.
	// *work_was_done_flag is OR'd into if work was actually performed.
	// If no work was performed (for instance if we're blocked),
	// *work_was_done_flag will not be modified.
	ErrorType UpdateTable(int *work_was_done_flag);

	// Send a chat message out to all players at this table.
	ErrorType SendChatMessage(char *source_name, char *message, int text_type);
	ErrorType SendChatMessage(char *source_name, char *message, int text_type, int watching_players_only);

	// Send a chat message out from the dealer to all players at this table.
	ErrorType SendDealerMessage(char *message, int text_type);
	ErrorType SendDealerMessage(char *message, int text_type, int watching_players_only);
	ErrorType SendDealerMessageToWatchingPlayers(char *message, int text_type);

	// Send the GameCommonData (if necessary) and the GamePlayerData
	// to a particular player (not for watching players).
	void Table::SendDataToPlayer(int *work_was_done_flag,
			int force_send_player_data_flag, int plrnum);

	// on this table, set the minimum a player can buy in for (and notify the client)
	void Table::SetMinimumBuyinAllowedForPlayer(int player_index, int chips);
	
	// Send game common data, player data, and input requests out to
	// any players who need them.
	ErrorType SendDataToPlayers(int *work_was_done_flag, int force_send_player_data_flag);

	// Select a bar snack for a player at this table.
	void Table::SelectBarSnack(WORD32 player_id, BAR_SNACK bs);

	// Set flags for all players and watching players so that
	// the GameCommonData structure will be sent to them again.
	void FlagResendGameCommonDataToEveryone(void);

  #if 0	// 2022 kriskoin
	// Functions dealing with tracking ghosts on tables (players pending leaving between games)
	int IsGhost(WORD32 player_id);	// T/F if player is a ghost on this table
	void ClearGhosts();
	void SetGhost(WORD32 player_id);// tell us this player is a ghost on this table
  #endif

	// The 32-bit unique serial number for this table.
	WORD32 table_serial_number;

	// Local storage of the parameters for this table.  New games are created
	// using these parameters.
	ClientDisplayTabIndex client_display_tab_index;
	GameRules game_rules;
	ChipType chip_type;				// what kind of chips does table play for?
	TableTournament_State table_tourn_state;	// TTS_* internal tournament states
	int max_number_of_players;		// 10, 8, or 2
	int small_blind_amount;
	int big_blind_amount;
	int initial_small_blind_amount;	// sb when table was created
	int initial_big_blind_amount;	// bb when table was created
	int previous_big_blind_seat;	// who was the big blind last hand? (-1 = don't care)
	int game_disable_bits;			// which bits control whether we can play?
	RakeType rake_profile;			// rake structure for this table

	// bad beat variables
	WORD32 bad_beat_game_number;
	int bad_beat_payout;
	int pending_bad_beat_payout_amount;	// saved payout amount for animating the payout

	// Global structures which get harvested and passed to the
	// clients (defined in gamedata.h)
	int summary_info_changed;	// set when summary_info has changed.  Must be reset externally.
	struct CardRoom_TableSummaryInfo summary_info;
	int summary_index;			// index into Card Room's summary list (maintained by card room).
	WORD32 last_tableinfo_connection_update;	// SecondCounter when connection flags were last updated in the summary_info structure

	int table_info_changed;		// set when table_info has changed.  Must be reset externally.
	struct CardRoom_TableInfo table_info;

	// Update the connected/disconnected status for the table_info player list
	// before sending it out.
	void Table::UpdateTableInfoConnectionStatus(void);

	// Misc. other public vars...
	struct BinaryTreeNode treenode;	// node data for this object.
	WORD32 last_game_end_time;		// SecondCounter when our last game ended
	WORD32 game_start_time;			// SecondCounter when current game started
	WORD32 time_of_next_update;		// SecondCounter when UpdateTable() should be called next.
	WORD16 rake_per_hour;			// calculated rake per hour
	// If we average 26 moves per game (2+10+4+3+3+2+2), try to
	// average over the last 5 games.
	#define AVG_RESPONSE_TIME_WEIGHTING  (1.0/(26.0*5.0))
	double avg_response_time;		// average response time per player (in seconds)

	// GameCommonData for our table.  Updated by the game itself during
	// games, but the table sometimes stores a few things in there between
	// games.  In any case, it gets completely re-initialized each time
	// we start a new game.
	struct GameCommonData GameCommonData;

	// Information about who is at the head of our waiting list.
  #if 1	// 2022 kriskoin
	int wait_list_called_player_count;
	struct WaitListCallInfo wait_list_called_players[MAX_PLAYERS_PER_GAME];
  #else
	WORD32 wait_list_player_id;			// player_id we're waiting for a response from (only one at a time)
	WORD32 wait_list_request_time;		// SecondCounter when we first offered the seat to the user.
	WORD32 wait_list_next_send_time;	// SecondCounter when we should next (re-)send the SeatAvail packet
  #endif
	BYTE8  wait_list_potential_players; // # of potential players we expect to join table.

	// Our current game object (if any).  There's only ever one of these.
	// Table is responsible for creating and destroying it when necessary.
	Game *game;

	enum {  TABLE_ACTIVE_FALSE,		// table has nothing going on
			TABLE_ACTIVE_WAITING,	// table is in countdown waiting for game to start
			TABLE_ACTIVE_TRUE } ;	// table is playing

	int table_is_active;		// enum above table recently played a game or what?
	int prev_table_is_active;	// previous state (managed by the cardroom)

	// Ptr to the card room we belong to.  We can't use CardRoom * because
	// cardroom.h includes table.h first.  We must cast it to a CardRoom *
	// any time we use it.  It's one of those chicken and the egg problems.
	void *cardroom;

	// Count the number of players at this table.
	int UpdatePlayerCount(void);

	// Add a player to the list of player's this table is currently
	// calling to empty seats from the waiting list(s).
	// Must be called from the cardroom thread.  Does not grab critsecs.
	// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
	ErrorType Table::AddCalledPlayer(struct WaitListCallInfo *wlci);

	// Add a player to the list of player's this table is currently
	// calling to empty seats from the waiting list(s).
	// Must be called from the cardroom thread.  Does not grab critsecs.
	// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
	ErrorType Table::RemoveCalledPlayer(WORD32 player_id);

	// Check if a player is currently being called to this table
	// from a waiting list.
	// Must be called from the cardroom thread.  Does not grab critsecs.
	// Related functions: AddCalledPlayer(), RemoveCalledPlayer(), CheckIfCalledPlayer().
	// Returns FALSE if not being called, TRUE if they are being called.
	int Table::CheckIfCalledPlayer(WORD32 player_id);

	// Returns T/F if this is or isn't a tournament table
	int Table::TournamentTable(void);

	// Suppress a Tournament Summary Email for this player for this tournament
	void SuppressTournSummaryEmail(WORD32 player_id, WORD32 tourn_serial_number);

	int watching_player_count;				// # of entries in the watching_players array
	int had_best_hand_last_game[MAX_PLAYERS_PER_GAME];	// T/F if player held the best hand

	int bad_beat_payout_stage;				// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing
	int special_hand_payout_stage;			// 0=none, 1=pausing, 2=displaying prize pool, 3=animating prize pool then pausing

private:

	// Start a new game if necessary.
	ErrorType StartNewGameIfNecessary(int *work_was_done_flag);

	// Start a new tournament game if necessary.
	ErrorType StartNewTournamentGameIfNecessary(int *work_was_done_flag);

	// Handle the various game over issues, including deleting the game object.
	ErrorType HandleGameOver(void);

	// Update the end-of-game bad beat payout animation
	void Table::UpdateBadBeatPayoutStage(void);

	// Update the end-of-game special hand payout animation
	void Table::UpdateSpecialHandPayoutStage(void);

	// Create a blank GamePlayerData structure for use between games.
	void Table::FillBlankGPD(struct GamePlayerData *gpd);

	// Send the same GamePlayerData structure to everyone (between games)
	void Table::SendSameGPDToEveryone(struct GamePlayerData *gpd);

	// Handle the timeout situation for an input request (returns index of action taken)
	int HandleInputRequestTimeout(int player_leaving_table_flag, char *calling_file, int calling_line);

	// Set all players at the table to not needing to post a blind, even if they're owing one
	void SetNobodyNeedsToPost(void);

	// Set all player's chips in front of them to zero
	void ClearChipsInFrontOfPlayers(void);
	
	// dealer output to everyone explaining a timeout
	void DealerTextForTimeout(int action_taken, char *name, int gender, int on_purpose, Player *plr);

	// if we want to pop up a buy-in dlg for addition chips for a client
	void OfferPlayerAbilityToBringMoreChips(int p_index);

	// fill in these numbers we need when attempting to sit down or bring more chips
	ErrorType FillBuyMinimums(int p_index, WORD32 *usual_required, WORD32 *shortbuy_required, WORD32 *minimum_required);

	// Send dealer chat to any watching players explaining how to
	// start a new table, sit down, join waiting lists, etc.
	void MakeDealerPromptToWatchingPlayers(void);
	int prev_dealer_prompt_watching_count;	// previous watching count
	WORD32 last_dealer_prompt_time;			// SecondCounter when we last said something

	// Process an input request destined for a human player.  If
	// they have set a matching in-turn action, use it as a result,
	// otherwise send it out to the player object for passing directly
	// to the remote player.
	void Table::ProcessGPIRequestForHuman(void);
	WORD16 last_input_request_serial_numbers[MAX_PLAYERS_PER_GAME];	// record of the last input request serial number we sent to each player

	// If the game has asked for a particular action and we have
	// that action in our action_mask, fill in the first match to the
	// input request.
	// This is similar to the client's comm.cpp function of the same name.
	// Returns: FALSE for no match, TRUE for match found and result sent out.
	int Table::CheckAutoRespondToInput(int action_mask);

  #if 0	// 2022 kriskoin
	// set the disconnected status flags for all players
	void Table::SetDisconnectedFlags(void);
  #endif
  	
	// ** // TOURNAMENT // ** //
	// announce and payout a player leaving (or winning) the tournament
	void Tourn_AnnounceAndPayPlayer(int p_index, int tied_flag, int official_placing, WORD32 amount_won);

	// do all handling needed between tournament games
	int Tourn_BetweenGamesProcessing(void);
	
	// calculate and fill in partial tournament payouts
	void Tourn_CalculatePartialPayouts(void);
	
	// cancel a tournament that's already in progress
	void Tourn_CancelTournamentInProgress(void);

	// fill a PreTourneyGPD that we're handed
	void Tourn_FillPreTourneyGPD(GamePlayerData *gpd);

	// send the special GamePlayerData tossed around for display purposes (pre-tournament)
	void Tourn_SendPreTourneyGPD(void);
	
	// send the tournament summary emails
	void Tourn_SendEmails(void);
	
	// fancy handling for selecting and displaying the initial tournament button player
	int Tourn_SetInitialButton(void);

	// add text to the summary email
	#define TTFSE_ALL_PLAYERS	998	// send to all players (and admin)
	#define TTFSE_ADMIN_ONLY	999	// send to admin summary only
	void Tourn_TextForSummaryEmail(int player, char *text, ...);	// for a specific player

	// on a special game number, announce to everyone (and particular table)
	void Table::AnnounceSpecialGame(void);
	
	// CritSec for making the Table itself thread safe.
	PPCRITICAL_SECTION TableCritSec;

	// fucntion common to all StartNewGames() functions
	ErrorType Table::SNG_LaunchIfNeccessary(int *work_was_done_flag, int valid_players);

	// Information about a player and how he relates to this particular table.
	// The players[] array holds info about whichever player is sitting in
	// any particular seat.  Only the array positions up to max_number_of_players
	// are filled in.  Empty seats are indicated with a NULL player pointer.
	// post_needed = one of POST_NEEDED_NONE, POST_NEEDED_BB, POST_NEEDED_SB, POST_NEEDED_BOTH
	struct PlayerTableInfo {
		int status;					// 0=empty, 1=computer, 2=human
		// note: WORD32 player_id can be retrieved from GameCommonData.player_id[] array
		int chips;					// # of chips he currently has at the table.
		int sitting_out_flag;		// flag for whether player is currently sitting out.
		int force_to_sit_out_flag;	// set if they timed out and we want to ask their client to enter sit out mode
		BYTE8 force_to_sit_out_serial_num;	// s/n we send to client each time we want to force them to sit out
		int sent_timeout_msg;		// set if the client has been sent a pop-up message talking about going all-in,folding,etc.
		int sent_sitout_msg;		// set if the client has been told to enter sit out mode after timing out
		int old_sitting_out_flag;	// flag for whether player last hand was sitting out.
		int timed_out_this_hand;	// T/F if he's timed out already once this hand
		int missed_blind_count;		// # of times the player has missed the big blind (used for kicking off the table)
		int post_needed;			// the type of post they must make before playing
		int game_common_data_sent;	// Bool: set once GameCommonData has been sent.
		int sent_out_of_money_msg;	// Bool: set if we sent the message indicating they're sitting out due to lack of funds.
		int played_a_hand;			// T/F played a hand at this table?
		int allowed_short_buy;		// T/F player is allowed a short buy
		WORD32 bad_beat_prize;		// set to something if this player got part of the bad beat jackpot
		WORD32 special_hand_prize;	// set to something if player particiapted in a special prize hand
		WORD32 special_hand_playerid;// the player id deserving the prize... in case someone sits there
		BYTE8 gender;				// GENDER_MALE/GENDER_FEMALE/etc (enum type in pplib.h)
		Player *player;				// Pointer to Player class object (only valid when human)
		int worst_connection_state;	// worst CONNECTION_STATE_* since they last answered.
	} players[MAX_PLAYERS_PER_GAME];

	struct BadBeatPrizes bad_beat_prizes;	// copy of structure from commonro (saved after game)

	// The watching_players[] array holds pointers to any watching players.
	// The array entries are filled from the bottom up and watching_player_count
	// is used to determine how many of the entries are filled.
	#define MAX_WATCHING_PLAYERS_PER_TABLE 500	//kriskoin: 	int connected_watching_player_count;	// informational: # of watching players actually connected
	struct PlayerTableInfo watching_players[MAX_WATCHING_PLAYERS_PER_TABLE];
	WORD32 next_watching_scan;

	WORD16 last_input_request_serial_number;// last serial number we sent out an IR for.
	WORD32 last_input_request_game_serial_number; // game serial # when last_input_request_serial_number was set
	WORD32 last_input_request_time;			// SecondCounter when last input request was sent out.
	WORD32 input_request_sent_time;			// SecondCounter when input request last sent to player
	WORD32 last_input_player_ping_time;		// SecondCounter when last sent a ping to the input player
	WORD32 last_input_request_ms;			// GetTickCount() when input request first arrived from game object

	int first_input_timeout_warning_sent;	// set when the first warning is sent for not responding to input
	int second_input_timeout_warning_sent;	// set when the second warning is sent for not responding to input
	int input_player_disconnected_msg_sent;	// set when we've told other players the input player has been disconnected.
	int worst_connection_state_for_input_player;	// the worst state seen while waiting for someone
	int button_seat;						// the seat which is to be the button.
	int new_players_must_post;				// set when new players must post
	int resend_input_request_flag;			// set when we must re-send the input request to the player we're waiting for.
	int move_button_next_game;				// T/F whether the button moves next new game
	int last_start_time_announce;			// last time we announced how long before game starts
	int shut_down_announced;				// set if iShutdownAfterGamesCompletedFlag message announced
	int showed_not_enough_players_msg;		// T/F if this was shown for this table
	int last_shotclock_announce_minutes;	// minutes left when dealer last announced the shot clock msg
	WORD32 next_game_start_time;			// earliest time a new game can start
	WORD32 time_of_computerized_eval;		// set to non-zero time of when computer should eval an input request (in ms)
	WORD32 last_routing_problem_notice;		// SecondCounter of the last time dealer sent a routing problem notice
	WORD32 time_of_next_action;				// SecondCounter of next time table is allowed to do something (used to slow things down, such as promotional hands)
	WORD32 time_of_last_player_database_scan;// SecondCounter when we last scanned for locked out players
	int game_type_was_disabled;				// set if this game type was disabled the last time we tried to start a new game
	WORD32 next_bad_beat_payout_stage;		// SecondCounter when we should switch to next stage.
	WORD32 next_special_hand_payout_stage;	// SecondCounter when we should switch to next stage.
	WORD16 disconnected_flags;				// disconnected status flags for all players

	// tournament stuff
	int tournament_table;					// T/F if this is a tournament table
	int tournament_button_override;			// used in initial tournament button position
	int tournament_hand_number;				// how many hands of this tournament have been played
	int tournament_current_level;			// current tournament level we're on
	int tournament_current_game;			// current tournament game we're on
	int tournament_total_tourney_chips;		// total number of tournament chips in local universe
	int tournament_buyin;					// buyin amount for the tournament
	WORD32 tournament_prize_pool;			// how much is in the total pool
	WORD32 tournament_highest_payout;		// keep track of the highest we've paid out in a tournament so far
	WORD32 time_no_tourn_starting_msg;		// SecondCounter of last time we announced this msg
	WORD32 time_of_next_tournament_stage;	// used in timing interval delays during tournaments
	char t_summary_filename[MAX_FNAME_LEN];	// summary email filename
	TTournamentSummaryInfo ttsi[MAX_PLAYERS_PER_GAME];	// keep track of results summaries
	Card high_card;							// used in tournaments for initial button
	GamePlayerData	pre_tourney_gpd;		// used in tournaments to do pre-game things
	GamePlayerData table_gpd;				// a common gpd for all players to be used between hands (copied from watching at the end of each game)
	int t_last_level_msg_displayed;			// keep track of the last level we announced
	int t_chips_last_hand[MAX_PLAYERS_PER_GAME];		// keep track of chip counts between hands
	int t_was_playing_last_hand[MAX_PLAYERS_PER_GAME];	// T/F if players was in last tournament hand
	WORD32 tournament_start_time;			// SecondCounter of tournament start time
	int tournament_announced_start;			// T/F if we've announced the start
	int tournament_summary_prizes_paid;		// how much have we paid out
	int tournament_players_eliminated;		// how many are left?
};

#endif // !_TABLE_H_INCLUDED
