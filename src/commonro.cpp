
/**********************************************************************************
 Member functions for CommonRules object
 date: kriskoin 2019/01/01
**********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <stdio.h>
#include <fcntl.h>  // kriskoin 
#include <stdlib.h>
#include <stdarg.h>
#include "pokersrv.h"
#include "cardroom.h"
#include "commonro.h"
#include "game.h"
#include "deck.h"
#include "hand.h"
#include "poker.h"
#include "pot.h"
#include "sdb.h"
#include "logging.h"

enum { BAD_BEAT_NOTHING, BAD_BEAT_WON_HAND, BAD_BEAT_LOST_HAND, BAD_BEAT_PARTICIPATED };

// kriskoin 2018/07/07extern HighHand hihand_real[2];
extern HighHand hihand_play[2];
// end kriskoin 
/**********************************************************************************
 Function CommonRules::CommonRules();
 date: kriskoin 2019/01/01 Purpose: default constructor
***********************************************************************************/
CommonRules::CommonRules(void)
{
	// Initialize external access pointers into game structures
	zstruct(_internal_data);		// blank the common player template structure
	WatchingData = &_watch_data;	// exportable pointer to the watch data
	InternalData = &_internal_data;	// exportable pointer to the internal data
	// initialize and set the pointers to the two import/export structures
	zstruct(_GPIRequest);
	zstruct(_GPIResult);
	GPIRequest = &_GPIRequest;
	GPIResult = &_GPIResult;
	_dealer_queue_index = 0;
	// game variables
	_game_rules = GAME_RULES_HOLDEM;
	client_display_tab_index = DISPLAY_TAB_HOLDEM;
	_chip_type = CT_NONE;
	_game_state = INITIALIZE;
	_initializing = TRUE;
	_b_end_game_handled = FALSE;	// true when we reach the end of the game
	_dealt_river = FALSE;			// true when the last card has been dealt
	_total_winners = 0;
	_table_is_full = FALSE;
	_waiting_for_user_action = FALSE;
	_game_was_cancelled = FALSE;	// used for refunds
	_player_count = 0;
	_action_number = 0;	// this is the "sub"serial-number that is sent in GPIRequests
	_next_player_gets_delay = FALSE;	// some players get extra time	
	delay_before_next_eval = 0;		// # of seconds before we should eval again.
	next_eval_time = 0;				// desired SecondCounter (if any) when we should eval again (calculated from delay_before_next_eval)
	sit_out_if_timed_out = 0;
	_max_raises_to_cap = 0;
	_split_pot_game = FALSE;
	alternate_delay_before_next_game = 0;
	memset(client_state_info_ptrs, 0, sizeof(client_state_info_ptrs));
	zstruct(bad_beat_prizes);
	_tournament_game = FALSE;
	_river_7cs_dealt_to_everyone = FALSE;;// T/F if river card has been dealt in 7cs


	// pot variables
	_hi_pot_winners = 0;
	_lo_pot_winners = 0;
	_pot_is_being_distributed = FALSE;
	_pot_number = MAX_PLAYERS_PER_GAME;	// will be decremented to proper value when used
	_rake_profile = RT_NONE;
	_pot_amount_playing_for = 0;
	_live_players_for_pot = 0;
	_cards_dealt = 0;
	_show_hand_count = 0;
	_best_hand_shown = -1;	// set to invalid till initialized
	_low_hand_shown = -1;	// set to invalid till initialized
	_last_hand_compared_hi = -1;
	_last_hand_compared_lo = -1;
	// now set all the defaults
	_internal_data.p_small_blind = (BYTE8)-1;	// set to invalid till initialized
	_internal_data.p_big_blind = (BYTE8)-1;		// set to invalid till initialized
	_internal_data.p_waiting_player = (BYTE8)-1;// set to invalid till initialized
	// loop through and initialize various things
	for (int index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
		_hand[index] = NULL;			// null hand pointers till (if) they have a hand
		_best_hand[index] = NULL;		// same as for _hand
		_low_hand[index] = NULL;		// same as for _hand
		_show_hand[index] = SHOW_HAND_UNKNOWN;	// not sure yet -- we may ask to see his cards
		_live_for_payoff[index] = FALSE;		// set T/F if he's live to win something
		_winner_of_pot_hi[index] = FALSE;			// set T/F if he won something
		_winner_of_pot_lo[index] = FALSE;			// set T/F if he won something
		_chips_won_this_pot_hi[index] = 0;
		_chips_won_this_pot_lo[index] = 0;
		_posted_blind[index] = FALSE;	// nobody has posted anything yet
		_szHandDescriptionHi[index][0]=0;	// blank hand description strings
		_szHandDescriptionLo[index][0]=0;	// blank hand description strings
		zstruct(_player_data[index]);
		for (int x = 0; x < MAX_PRIVATE_CARDS; x++) {
			// set the big card array to "NO CARD" for everyone
			_internal_data.cards[index][x] = CARD_NO_CARD;
		}
		_internal_data.player_status[index] = PLAYER_STATUS_EMPTY; // nobody there
		_internal_data.last_action[index] = ACT_NO_ACTION;
		_internal_data.last_action_amount[index] = 0;
		PlayerData[index] = &_player_data[index];	// set our exportable table of pointers
	}
	// set the common cards to "NO CARD"
	for (int z = 0; z < MAX_PUBLIC_CARDS; z++) {
		_internal_data.common_cards[z] = CARD_NO_CARD;
	}
	// init dealer chat queue
	for (int i=0; i < DEALER_QUEUE_SIZE; i++) {
		DealerChatQueue[i].text_type = CHATTEXT_NONE;
		DealerChatQueue[i].message[0] = 0;
	}
	// we need a flop hand -- and it shouldn't ever be sorted, as we want to
	// preserve the order that the cards went in.  It's ok to have a hand
	// that doesn't sort itself automatically, as long as we don't try to
	// evaluate it.  If we really want to evaluate, it can be set to sortable
	// by calling Hand.SetSortable(TRUE) -- or create a new hand that's
	// sortable and copy the contents...
	_flop = new Hand(FALSE); // it's set to "unsortable"
	// kriskoin 
	_hi_hand_of_day = new Hand();
	// end kriskoin 
	// we will certainly need a deck of cards
	_deck = new Deck;
	// initialize the pot -- it's certainly needed
	_pot = new Pot;
	// poker object
	_poker = new Poker;		// deleted in destructor
}

/**********************************************************************************
 Function CommonRules::~CommonRules()
 date: kriskoin 2019/01/01 Purpose: destructor
***********************************************************************************/
CommonRules::~CommonRules(void)
{
	// deallocate as much that's within our control
	for (int index=0; index < MAX_PLAYERS_PER_GAME; index++) {
		if (_hand[index]) {
			delete _hand[index];
		}
		if (_best_hand[index]) {
			delete _best_hand[index];
		}
		if (_low_hand[index]) {
			delete _low_hand[index];
		}
	}
	// delete any object we may have created
	if (_flop) delete (_flop);
	if (_pot) delete (_pot);
	if (_poker) delete (_poker);
	if (_deck) delete (_deck);
}

/**********************************************************************************
 Function CommonRules::CommonGameInit()
 Date: 20180707 kriskoin :  Purpose: init everything common to all games
***********************************************************************************/
void CommonRules::CommonGameInit(WORD32 serial_number, int sb, int bb, struct GameCommonData *gcd)
{
	if (!gcd) {
		DIE("Tried to CommonGameInit without a valid GameCommonData *");
	}
	CommonData = gcd;		// exportable pointer to common data
	zstruct(*CommonData);	// blank the common structure
	CommonData->game_rules = (BYTE8)_game_rules;
	CommonData->client_display_tab_index = (BYTE8)client_display_tab_index;
	// each game has its own unique serial number
	CommonData->game_serial_number = serial_number;
	_internal_data.game_serial_number = serial_number;
	_pot->SetRakeProfile(_rake_profile); // specify the rake structure to us

	int index;	// used in loops below
	//strcpy((char *) CommonData->real_hihand, "2d3d4d5d6d");
	switch (CommonData->game_rules) {
	case GAME_RULES_HOLDEM:
	case GAME_RULES_OMAHA_HI:
	case GAME_RULES_OMAHA_HI_LO:
		// structured game amounts can be assumed from the blinds
		_low_limit = bb;
		_high_limit = 2 * bb;
		// we need to init some of the CommonData for the table
		CommonData->small_blind_amount = sb;
		CommonData->big_blind_amount = bb;
		for (index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
			// the table will override these to what they should be
			CommonData->post_needed[index] = POST_NEEDED_UNKNOWN;
		}
		break;
	case GAME_RULES_STUD7:
	case GAME_RULES_STUD7_HI_LO:
		// we're going to use the variables we have as follows:
		// small_blind = ante required
		// big_blind = first player's optional low-bring-in amount
		// low_limit = conventional low limit
		// high_limit = conventional high limit
		CommonData->small_blind_amount = sb;	// ie, ante $1
		CommonData->big_blind_amount = bb;		// bring in $2
		_low_limit = 2 * bb;	// low limit  $4
		_high_limit = 4 * bb;	// high limit $8
		break;

	default : 
		DIE("Unknown game rules passed to CommonGameInit()");
		break;
	}
	_split_pot_game = (	CommonData->game_rules == GAME_RULES_OMAHA_HI_LO ||  
						CommonData->game_rules == GAME_RULES_STUD7_HI_LO );
	_internal_data.s_gameflow = GAMEFLOW_BEFORE_GAME;
	_max_raises_to_cap = DEFAULT_MAX_RAISES_TO_CAP;
	_tournament_game = ((Table *)_table_ptr)->chip_type==CT_TOURNAMENT;
	memset(((Table *)_table_ptr)->had_best_hand_last_game, 0, sizeof(int) * MAX_PLAYERS_PER_GAME);
	//*(char *)0 = 0;	// cause a crash right now.
	UpdateStructures();	// make sure they're all up to date
}

/**********************************************************************************
 Function CommonRules::AddPlayer()
 Date: 20180707 kriskoin :  Purpose: add a player to this game
 Returns: no return, but sets a flag in the player data structure :::***********************************************************************************/
void CommonRules::AddPlayer(int seating_position, WORD32 player_id, char *name, char *city,
							int chips, BYTE8 gender, int sitting_out_flag, int post_needed,
							int *output_not_enough_money_flag, struct ClientStateInfo *client_state_info_ptr)
{
	BYTE8 p_index = (BYTE8)seating_position;
	BOOL8 playing = FALSE;	// to be determined if he gets to play
	if (!_initializing) {
		DIE("Tried to AddPlayer while game is in progress");
	}
	*output_not_enough_money_flag = FALSE;
	// if he's got less than the crtieria, he doesn't get to play
	int minimum_allowed_to_play;
	if (CommonData->game_rules == GAME_RULES_STUD7 || CommonData->game_rules == GAME_RULES_STUD7_HI_LO) {
		// for 7cs, he can play if he has enough for the ante and the low bring-in
		minimum_allowed_to_play = CommonData->big_blind_amount+CommonData->small_blind_amount;
	} else {	// hold'em/omaha
		// 24/01/01 kriskoin:
		minimum_allowed_to_play = (_tournament_game ? 1 : CommonData->big_blind_amount); // any chips = ok to play
	}
	
	if (chips < minimum_allowed_to_play) {
	  #if 0		
		kp1(("%s(%d) Not yet finished: %s(%s) wasn't allowed to play %d/%d because only %d in chips\n",
			_FL,name, city, _low_limit, _high_limit, chips));
	  #endif
		_internal_data.player_status[p_index] = PLAYER_STATUS_NOT_ENOUGH_MONEY;
		*output_not_enough_money_flag = TRUE;
		playing = FALSE;
	}
	if (_table_is_full) {
		Error(ERR_NOTE,"%s(%d) %s(%s) wasn't allowed to play because the table is full! (game #%d)",
			_FL, name, city, CommonData->game_serial_number);
		Error(ERR_ERROR,"Tried to add a player to a full table");
		// shouldn't really ever happen unless the card room screws up
		_internal_data.player_status[p_index] = PLAYER_STATUS_SITTING_OUT;
		playing = FALSE;
	}
	// we are using the seating position as the actual index... so it must be valid
	if (seating_position < 0 || seating_position >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_ERROR,"%s(%d) Tried to add a player to invalid seat (%d) must be 0->%d",
			_FL, seating_position, MAX_PLAYERS_PER_GAME);
		DIE("Bad seat number");
	}
	// if he meant to sit out in the first place, let him -- even without enough $
	if (sitting_out_flag) {
		_internal_data.player_status[p_index] = PLAYER_STATUS_SITTING_OUT;
		playing = FALSE;
	}
	// if we found no reason to not allow him to play and he wants to, he gets to
	if (_internal_data.player_status[p_index] == PLAYER_STATUS_EMPTY && !sitting_out_flag) {
		_internal_data.player_status[p_index] = PLAYER_STATUS_PLAYING;
		playing = TRUE;
	}
	// OK to add -- add the player here
	//pr(("Adding %d (%d chips)in CommonRules::AddPlayer() (%s) (post_needed = %d)\n", p_index, chips,
	//playing ? "playing" : "not playing", post_needed));
	strnncpy(CommonData->name[p_index], name, MAX_COMMON_STRING_LEN);
	strnncpy(CommonData->city[p_index], city, MAX_COMMON_STRING_LEN);
	CommonData->chips[p_index] = chips;		// came into game with this much
	CommonData->player_id[p_index] = player_id;	// unique player id
	//enum { POST_NEEDED_NONE, POST_NEEDED_BB, POST_NEEDED_SB, POST_NEEDED_BOTH } ;
	CommonData->post_needed[p_index] = (BYTE8)post_needed;	// do we need a post from him?
	CommonData->gender[p_index] = (BYTE8)gender;			// M/F/unknown
	if (playing) {
		_hand[p_index] = new Hand(FALSE);// this is where he'll hold his cards
		_pot->AddPlayer(p_index, chips); // only add him to the pot if he's playing
	}
	client_state_info_ptrs[p_index] = client_state_info_ptr;
	// increment for next player we add
	_player_count++;	// he's at the table, playing or not
	if (_player_count == _max_this_game_players) {	// the table just filled up
		_table_is_full = TRUE;
	}
}

/**********************************************************************************
 Function CommonRules::GetNextPlayerEligibleForCard(void)
 date: kriskoin 2019/01/01 Purpose: return the next player eligible to be dealt a card (can be all in)
***********************************************************************************/
BYTE8 CommonRules::GetNextPlayerEligibleForCard(BYTE8 current_player) {
	return GetNextPlayerEligibleForCard(current_player, TRUE);
}

/**********************************************************************************
 Function CommonRules::GetNextPlayerEligibleForCard(void)
 date: kriskoin 2019/01/01 Purpose: return the next player eligible to be dealt a card
 Note: can_be_all_in T/F -- returns a player that's all in if TRUE; FALSE will not give
       a player that's all in.  This is used because this function is used for more than
	   dealing -- it's used for evaluations where we need to know if the player is still
	   acting.  The default function called from most places sets it TRUE
***********************************************************************************/
BYTE8 CommonRules::GetNextPlayerEligibleForCard(BYTE8 current_player, int can_be_all_in)
{
	int loop_count = 0;
	BYTE8 p_index = current_player;
	forever {
		p_index = (BYTE8)(++p_index % MAX_PLAYERS_PER_GAME);	// increment index
		loop_count++;	// so we don't get stuck forever due to a bug
		if (loop_count > MAX_PLAYERS_PER_GAME) {	// gone around enough, found nothing
			// no one left live -- we can assume it'll be caught by the calling function
			// this is valid -- so we shouldn't trap it here...
			// 24/01/01 kriskoin:
			// already all in, we'll return any valid player -- he just won't get to act
			// Error(ERR_NOTE, "%s(%d) GetNextPlayerEligibleForCard returning first live player", _FL);
			return (BYTE8)_pot->GetFirstActivePlayer(TRUE);	// give us anyone valid
		}
		if (can_be_all_in) {	// we might not want all-in players
			if (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
				_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) {
				break;
			}
		} else {
			if (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING) {
				break;
			}
		}
	}
	return p_index;
}

/**********************************************************************************
 Function CommonRules::GetNextPlayerInTurn(BYTE8 current_player, int playing_player_flag)
 date: kriskoin 2019/01/01 Purpose: find the next seated player (might be active, might be sitting out)
 Note:	  if he's folded, he won't show up here at all.
          if the flag is TRUE, the next ACTIVE player is returned.  if FALSE, we might
		  get back a player that's sitting out
***********************************************************************************/
BYTE8 CommonRules::GetNextPlayerInTurn(BYTE8 current_player, int playing_player_flag)
{
	int loop_count = 0;
	BYTE8 p_index = current_player;
	forever {
		p_index = (BYTE8)(++p_index % MAX_PLAYERS_PER_GAME);	// increment index
		loop_count++;	// so we don't get stuck forever due to a bug
		if (loop_count > MAX_PLAYERS_PER_GAME) {	// gone around enough, found nothing
			// no one left live -- we can assume it'll be caught by the calling function
			// this is arguably valid -- so we shouldn't trap it here...
		  #if POT_DEBUG
			kp(("%s(%d) GetNextPlayerInTurn returning current player...", _FL));
		  #endif
			return (BYTE8)_pot->GetFirstActivePlayer(TRUE);	// give us anyone valid
		}
		if (playing_player_flag) {	// we want the next ACTIVE player
			if (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING) {
				break;
			}
		} else {	// we don't care if he's sitting out -- that's good enough
			if (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
					SittingOut(p_index) ) {
				break;
			}
		}
	}
	return p_index;
}

/**********************************************************************************
 Function CommonRules::GetNextPlayerInTurn(BYTE8 current_player)
 Date: 20180707 kriskoin :  Purpose: find the next active player in turn in the game (internal)
**********************************************************************************/
BYTE8 CommonRules::GetNextPlayerInTurn(BYTE8 current_player)
{
	return GetNextPlayerInTurn(current_player, TRUE);	// we want the next ACTIVE player
}

/**********************************************************************************
 Function CommonRules::RequestActionFromPlayer
 Date: 20180707 kriskoin :  Purpose: prepare to ask for a request from a player
***********************************************************************************/
void CommonRules::RequestActionFromPlayer(BYTE8 player)
{
	//kp(("%s(%d) RequestActionFromPlayer(%s) was called.\n",_FL,CommonData->name[player]));
	// we start building the action request here... and as it may be a multiple-
	// step operation, we set a flag (ready_to_process) to FALSE.  when this has
	// finished being built, a call to PostAction() will set it TRUE and let the
	// watching object know the request is ready to handle
	zstruct(_GPIRequest);	// delete everything in there
	zstruct(_GPIResult);	// blank the result structure too
	_GPIResult.ready_to_process = FALSE;	// will be set externally when ready
	_GPIRequest.ready_to_process = FALSE;	// don't let it be dealt with it till ready
	_GPIRequest.seating_position = player;	// the player we're requesting from
	_GPIRequest.game_serial_number = CommonData->game_serial_number;
	_GPIRequest.time_out = (BYTE8)min(255,InputTimeoutSeconds);	// default, unless overriden
	//kp(("%s(%d) setting _GPIRequest.time_out = %d\n",_FL,_GPIRequest.time_out));
	if (_next_player_gets_delay) {
		// Give the guy extra time to act if cards were just dealt.
		_next_player_gets_delay = FALSE;	// reset for next player
		AddToActionTimeOut(CARD_ANIMATION_DELAY	* _cards_dealt);
	}
	//kp(("%s(%d) _GPIRequest.time_out is now %d\n",_FL,_GPIRequest.time_out));
	// increment the internal serial number -- now unique for this action
	_GPIRequest.input_request_serial_number = ++_action_number;
	_internal_data.p_waiting_player = player;	// let everyone know who we're waiting for
	// clear his previous action... this just blanks what he last did...
	_internal_data.last_action[player] = (BYTE8)ACT_NO_ACTION;

	// 24/01/01 kriskoin:
	// (so the client animates properly if they choose the same action again).
	if (_pot->GetActingPlayersLeft() > 2) {
		int next_likely_player = GetNextPlayerInTurn(player);
		if (next_likely_player >= 0) {
			_internal_data.last_action[next_likely_player] = (BYTE8)ACT_NO_ACTION;
		}
	}

	sit_out_if_timed_out = TRUE;	// default to sitting a player out if they time out
}

/**********************************************************************************
 Function CommonRules::SetValidAction
 Date: 20180707 kriskoin :  Purpose: set a request bit on the template for a player we want an action from
***********************************************************************************/
void CommonRules::SetValidAction(ActionType action)
{
	SET_ACTION(_GPIRequest.action_mask, action); // defined in commonro.h
}

/**********************************************************************************
 Function CommonRules::SetActionTimeOut(int time_out);
 date: kriskoin 2019/01/01 Purpose: override the default timeout length for this requested action
		sit_out_if_timed_out_flag indicates whether a player should get put
		into sit out mode if he times out answering the request.
***********************************************************************************/
void CommonRules::SetActionTimeOut(int time_out, int sit_out_if_timed_out_flag)
{
	_GPIRequest.time_out = (BYTE8)min(255,time_out);
	//kp(("%s(%d) SetActionTimeOut() is setting _GPIRequest.time_out = %d\n",_FL,_GPIRequest.time_out));
	sit_out_if_timed_out = sit_out_if_timed_out_flag;
}

/**********************************************************************************
 Function CommonRules::PostAction(void)
 Date: 20180707 kriskoin :  Purpose: tells whoever is watching that the action request is ready to go
***********************************************************************************/
void CommonRules::PostAction(void)
{
	//kp(("%s(%d) PostAction() was called. Possible action mask = $%08lx\n",_FL,_GPIRequest.action_mask));
	_GPIRequest.ready_to_process = TRUE; // don't deal with it till we're ready
	_waiting_for_user_action = TRUE;
}

/**********************************************************************************
 Function CommonRules::EvaluateLiveHands(void)
 date: kriskoin 2019/01/01 Purpose: evaluate all potential hands that have a shot at the pot (or part of it)
***********************************************************************************/
void CommonRules::EvaluateLiveHands(void)
{
	if (!_dealt_river) return;	// can't evaluate unless we've got all the cards
	// we'll need a poker evaluation object to figure out the winner
	Hand temp_best_hand;	// temp holder for best hand so far
	// for all players who are left, find their best hand
	for (BYTE8 p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// must have been playing from the start and not folded and not gotten paid yet
		if (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
			_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) {
				if (!_best_hand[p_index]) {	// create it if it doesn't exist yet
					_best_hand[p_index] = new Hand;
				}
				if (_split_pot_game) {
					if (!_low_hand[p_index]) {	// create it if it doesn't exist yet
						_low_hand[p_index] = new Hand;
					}
				}
				// ok to call this with null parameter
				_poker->FindBestHand(_game_rules, *(_hand[p_index]), *(_flop), _best_hand[p_index], _low_hand[p_index]);
		}
	}
}

/**********************************************************************************
 Function RequestNextMoveFromPlayer(BYTE8 p_index)
 date: kriskoin 2019/01/01 Purpose:
***********************************************************************************/
void CommonRules::RequestNextMoveFromPlayer(BYTE8 p_index)
{
	// ask this player for his next move after figuring out his options
	RequestActionFromPlayer(p_index);
	SetValidAction(ACT_FOLD);		// FOLDing is always an option
	_GPIRequest.bet_amount = _pot->GetRaiseAmount(p_index);
	// a bet is just a raise from zero
	_GPIRequest.raise_amount = _GPIRequest.bet_amount;
	// we want a flag set telling us if he'd be all in on a raise
	int all_in_on_raise = _pot->AllInOnRaise(p_index);

	_GPIRequest.call_amount = _pot->GetCallAmount(p_index);
	// we want a flag set telling us if he'd be all in on a call
	int all_in_on_call = _pot->AllInOnCall(p_index);

	// now we know all the options; structure the action mask for the player
	if (!_GPIRequest.call_amount) {	// call amount is zero -- he can check/bet
		SetValidAction(ACT_CHECK);
		if (!_pot->PotIsCapped()) {		// hard to imagine a capped pot with no call amount
			if (all_in_on_raise) {
				SetValidAction(ACT_BET_ALL_IN);
			} else {
				// there is ONE exception here -- and that is when the Big Blind is playing
				// the option -- he can bet when it comes back called to him -- but we have to
				// call it a RAISE
				// ie, if the player is the BB AND we're in betting round 1 AND all he's put in
				// is the amount of the BB, we'll override and call it a raise... otherwise, it's
				// a normal BET
				// adate: expanding on this -- anyone who's posted, except for the small
				// blind, is raising, not betting.
				if (	_game_state == BETTING_ROUND_1 &&
						//p_index == _internal_data.p_big_blind &&
						_pot->GetPostedThisRound(p_index) && 
						_pot->GetChipsBetRound(p_index) == CommonData->big_blind_amount) {
					SetValidAction(ACT_RAISE);
				} else {
					SetValidAction(ACT_BET);
				}
			}
		}
	} else {	// it's more than 0 to keep going... call or raise
		if (all_in_on_call) {	// check if we'd be all in just on the call
			SetValidAction(ACT_CALL_ALL_IN);	// all-in if we call
		} else {
			SetValidAction(ACT_CALL);			// normal call
		}
		// reraise allowed if the pot isn't capped provided we're not all in on the call
		// and we're not the only player left

		// 24/01/01 kriskoin:
		// in that scenario, you are not allowed to re-raise.  This has been broken down to the following
		// statement: If this is not the player's first time betting this round and the call amount is less
		// than or equal to the bet, he is not allowed to re-raise.  This has been entered as its own condition
		// in this evaluation and added below.  Also note that we have to make an exception for the SB as he 
		// HAS acted this round, but his top-off amount wouldn't be enough to alow a raise

		// NOTE: there is a remnant weird case here that's still not 100% -- ask HK or MM for specifics, but
		// basically, someone on a double-check-raise on a later round after there's been an all-in will not
		// have the option, on the option to call.  This might be a complicated fix... and it may happen so 
		// rarely that even if it happens, no one may notice.  In any event, perhaps we'll fix it one day.
		// For now, let's make sure this works...
		// 24/01/01 kriskoin:
		int allowed_to_raise = TRUE;
		if (_pot->GetChipsBetRound(p_index) && 
			(_GPIRequest.call_amount <= (_pot->GetBetIncrement()/2)) ) {
			allowed_to_raise = FALSE;
		}
		// this is the SB exception
		if (!allowed_to_raise &&	// we trapped it
			_pot->GetChipsBetRound(p_index) == CommonData->small_blind_amount && // only if he's be SB and no more
			_game_state == BETTING_ROUND_1 &&	// only on the opening betting round (THE SB)
			p_index == _internal_data.p_small_blind) { // and it has to be him, of course
				allowed_to_raise = TRUE;
		}
		// this is the 7CS exception
		if (!allowed_to_raise &&	// we trapped it
			(_game_rules == GAME_RULES_STUD7 || _game_rules == GAME_RULES_STUD7_HI_LO) &&	// 7CS game
			_game_state == BETTING_ROUND_1 &&	// only on the opening "bring-in" round
			_GPIRequest.call_amount == CommonData->big_blind_amount) {	// low bring-in is stored here
				allowed_to_raise = TRUE;
		}

		// now we know whether we can raise or not
		if (allowed_to_raise && 
			!all_in_on_call &&
			!_pot->PotIsCapped() && 
			_pot->GetActingPlayersLeft() > 1 &&
			_GPIRequest.raise_amount) {
				if (all_in_on_raise) {		// are we all in if we raise?
					SetValidAction(ACT_RAISE_ALL_IN);
				} else {
					SetValidAction(ACT_RAISE);
				}
		}
	}
	// close off the structure and post it
	PostAction();
}

/**********************************************************************************
 Function CommonRules::ShowHandForPlayer(BYTE8 p_index)
 date: kriskoin 2019/01/01 Purpose: do everything needed to show a player's cards
***********************************************************************************/
void CommonRules::ShowHandForPlayer(BYTE8 p_index)
{
	// adate: we'll build the hand description at this point, since it's important
	// to know exactly what the last hand compared was too...
	// adate: changed -- we always compare to the best hand out there, that we've seen
	// so far.
	// 24/01/01 kriskoin:
	
	// first the low
	if (_split_pot_game) {
		if (_low_hand[p_index] && _poker->ValidLowHand(_low_hand[p_index]) ) {	// something there?
			if (_low_hand_shown < 0 || _last_hand_compared_lo < 0) {	// first call to it
				_poker->GetHandCompareDescription(_low_hand[p_index], NULL, _szHandDescriptionLo[p_index], FALSE);
			} else {
				_poker->GetHandCompareDescription(_low_hand[p_index],
					_low_hand[_low_hand_shown], _szHandDescriptionLo[p_index], FALSE);
			}
			_last_hand_compared_lo = p_index;
			// if we're at the point where we have valid hands to compare to each other, keep
			// track of the lowest hand we've seen so far
			_low_hand_shown = (_low_hand_shown < 0 ? p_index : _low_hand_shown );
			if (_poker->FindBetterHand(*(_low_hand[p_index]), *_low_hand[_low_hand_shown]) != HAND_1) {
				_low_hand_shown = p_index;
			}
		}
		// if we built a description, display it	
		if (_szHandDescriptionLo[p_index][0]) { // something descriptive there?
			pr(("Player %d has %s\n", p_index, _szHandDescriptionLo[p_index]));
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "%s shows %s for low",
				CommonData->name[p_index], 
				_szHandDescriptionLo[p_index]);
		}
	}
	
	// now, the high side
	if (_best_hand[p_index]) {
		if (_best_hand_shown < 0 || _last_hand_compared_hi < 0) {	// first call to it
			_poker->GetHandCompareDescription(_best_hand[p_index], NULL, _szHandDescriptionHi[p_index]);
		} else {
			_poker->GetHandCompareDescription(_best_hand[p_index],
				_best_hand[_best_hand_shown], _szHandDescriptionHi[p_index]);
		}
		_last_hand_compared_hi = p_index;
		// if we're at the point where we have valid hands to compare to each other, keep
		// track of the best hand we've seen so far
		_best_hand_shown = (_best_hand_shown < 0 ? p_index : _best_hand_shown );
			
		if (_poker->FindBetterHand(*(_best_hand[p_index]), *_best_hand[_best_hand_shown]) != HAND_2) {
			_best_hand_shown = p_index;
		}
		// if we built a description, display it	
		if (_szHandDescriptionHi[p_index][0]) { // something descriptive there?
			pr(("Player %d has %s\n", p_index, _szHandDescriptionHi[p_index]));
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "%s shows %s",
			CommonData->name[p_index], _szHandDescriptionHi[p_index]);
		}
	}

	_show_hand[p_index] = SHOW_HAND_SHOWED;
	_internal_data.last_action[p_index] = (BYTE8)ACT_SHOW_HAND;
	_show_hand_count++;
}

//*********************************************************
// https://github.com/kriskoin//
// Muck a player's hand
//
void CommonRules::MuckHandForPlayer(BYTE8 player_index)
{
	_internal_data.last_action[player_index] = (BYTE8)ACT_MUCK_HAND;
	_show_hand[player_index] = SHOW_HAND_MUCK;
	RemoveCards(player_index);
  #if DEALER_REDUNDANT_MESSAGES		
	SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s mucks %s hand",
			CommonData->name[player_index], GenderPronoun(player_index));
  #endif
}

/**********************************************************************************
 Function RequestOrShowHandFromPlayer(void)
 date: kriskoin 2019/01/01 Purpose: ask a player whether or not to show his hand after figuring out who to ask
 Returns: TRUE if we automatically showed someone's hand, FALSE if we sent a request
 Note:    ALL calls initially will be set to ASK -- it's up to this function to
          figure out if we actually need to ASK, or if we should just show it.  There
		  are situations where it's not up to the player... like he's been called,
		  or he's the next best hand in sequence.
 Note: 990625HK: we've decided that cards never get flipped over automatically.  In
       situations where the player MUST show his cards, he'll have a single "SHOW"
	   button.  If he disconnects, the default action will also be to SHOW
***********************************************************************************/
int CommonRules::RequestOrShowHandFromPlayer(void)
{
	if (!AnyPlayerLeftToAskAboutShowingHand()) {
		DIE("RequestShowHandFromPlayer called with no valid players set to SHOW_HAND_ASK");
	}

	// the only time a [Show Shuffled Hand] is allowed is in a 7cs or 7csH/L game where the river card has been dealt
	int allowed_shuffle_show = _river_7cs_dealt_to_everyone;

  #if 0
	kp1(("%s(%d) *** ACT_SHUFFLE_SHOW is ENABLED from the server -- clients older than 1.10.b14 will crash\n", _FL));
  #else
	kp1(("%s(%d) *** ACT_SHUFFLE_SHOW is disabled from the server -- pending client release(1.10.b14 and older will crash)\n", _FL));
	allowed_shuffle_show = FALSE;
  #endif

	BYTE8 p_index = (BYTE8)_pot->GetLastPlayerToRaise();
	int loop_count = 0;
	while (_show_hand[p_index] != SHOW_HAND_ASK) {
		loop_count++;
		if (loop_count > MAX_PLAYERS_PER_GAME) {	// shouldn't ever happen
			Error(ERR_FATAL_ERROR, "%s(%d) Caught in endless loop -- no SHOW_HAND_ASK", _FL);
			DIE("Caught in endless loop in RequestOrShowHandFromPlayer");
		}
		p_index = (BYTE8)((p_index+1) % MAX_PLAYERS_PER_GAME);
	}
	// there are a lot of things dependent on whether we ask or not -- utlimately,
	// the only players being actually asked are those that won alone and may want
	// to show their hands... or those who lost.  everyone else will be forced to show
	// their hands
	#define AUTO_SHOWDOWN_SPACING	2
	// Use the auto showdown spacing (instead of waiting for a regular timeout)
	// if the player is not well connected.  The only time they get a chance to
	// answer these input requests is when they are well connected.
	int use_regular_timeout = GetPlayerConnectionState(p_index)==CONNECTION_STATE_GOOD;
	// if there was only one player left, we'll give him the option of showing
	if (_live_players_for_pot == 1) {	// this player won by default
		if (_show_hand[p_index] != SHOW_HAND_SHOWED) {	// hasn't shown yet; force show
			// This player is the only player left.  He has the choice of
			// showing or not showing, but we shouldn't hold up the game
			// for very long to let him do so.
			//kp(("Asking show or toss for %s\n", CommonData->name[p_index]));
			RequestActionFromPlayer(p_index);
			SetActionTimeOut(use_regular_timeout ? TimeoutShowOnlyHand : AUTO_SHOWDOWN_SPACING, FALSE);
			SetValidAction(ACT_SHOW_HAND);
			if (allowed_shuffle_show) {	// 20:::				SetValidAction(ACT_SHOW_SHUFFLED);
			}
			SetValidAction(ACT_TOSS_HAND);
			PostAction();
			// we think that since this was a very simple ending, next game
			// can start in 3s, instead of the usual longer delay
			alternate_delay_before_next_game = 3;
			return FALSE;
		} else {
			// that one player has already shown his hand (from an earlier sidepot)
			return TRUE;	// not much to do
		}
	} else {	// more than one live player for pot... show (possibly) in order
		if (p_index == _pot->GetLastPlayerToRaise()) {	// called player must show
			if (_show_hand[p_index] != SHOW_HAND_SHOWED) {	// hasn't shown yet; force show
				//kp(("%s(%d) Forcing show (last player to raise) for %s\n", _FL, CommonData->name[p_index]));
				RequestActionFromPlayer(p_index);
				SetActionTimeOut(use_regular_timeout ? TimeoutShowFirstHand : AUTO_SHOWDOWN_SPACING, FALSE);
				SetValidAction(ACT_SHOW_HAND);
				if (allowed_shuffle_show) {	// 20:::					SetValidAction(ACT_SHOW_SHUFFLED);
				}
				PostAction();
				return FALSE;
			} else {
				return TRUE;	// already showed -- nothing to do
			}
		}
		// if he's being asked but his hand is the best so far, force the show
		// do the compare with what we know is best so far
		// make sure these have been initialized
		if (_best_hand_shown < 0) {
			// first time called, initialize _best_hand_shown
			_best_hand_shown = p_index;
		}
		if (_low_hand_shown < 0) {
			// first time called, initialize _low_hand_shown
			_low_hand_shown = p_index;
		}
		if ( (_poker->FindBetterHand(*(_best_hand[p_index]), *_best_hand[_best_hand_shown]) != HAND_2)
			// line below was added 20000301HK for hi/lo split
			  || ( (_split_pot_game &&
				 _low_hand[p_index] &&
				 _poker->ValidLowHand(_low_hand[p_index]) &&
				 _poker->FindBetterHand(*(_low_hand[p_index]), *_low_hand[_low_hand_shown]) != HAND_1) )
			
			// we'll flip it no matter what if player is all-in
			  || (_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) ) {

			// it's the best so far (or tied for the best) or he's all-in -- forced show
			//kp(("%s(%d) Forcing show (best hand) for %s, then delaying slightly.\n", _FL, CommonData->name[p_index]));
			RequestActionFromPlayer(p_index);
			// 19991130HK if he was all in, he has no choice -- flip them really quickly
			int regular_timeout_to_use = TimeoutShowOtherHands;
			if (_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) {
				regular_timeout_to_use = TimeoutAutoMuckHand;	// good short timeout
			}
			SetActionTimeOut(use_regular_timeout ? regular_timeout_to_use : AUTO_SHOWDOWN_SPACING, FALSE);
			SetValidAction(ACT_SHOW_HAND);
			if (allowed_shuffle_show) {	// 20:::				SetValidAction(ACT_SHOW_SHUFFLED);
			}
			PostAction();
			return FALSE;
		} else { // it's not the best hand -- but he has the option to show it
			// This player lost. Show or muck depending how they set the
			// checkbox on their end.
			if (client_state_info_ptrs[p_index] && client_state_info_ptrs[p_index]->muck_losing_hands) {
				// Muck with a really short delay
				RequestActionFromPlayer(p_index);
				SetActionTimeOut(use_regular_timeout ? TimeoutAutoMuckHand : AUTO_SHOWDOWN_SPACING, FALSE);
				SetValidAction(ACT_SHOW_HAND);
				if (allowed_shuffle_show) {	// 20:::					SetValidAction(ACT_SHOW_SHUFFLED);
				}
				SetValidAction(ACT_MUCK_HAND);
				PostAction();
				return FALSE;
			} else {
				// 'Muck losing hands' was NOT checked... always ask them.
				//kp(("Asking show for %s\n", CommonData->name[p_index]));
				RequestActionFromPlayer(p_index);
				SetActionTimeOut(use_regular_timeout ? TimeoutMuckHand : AUTO_SHOWDOWN_SPACING, FALSE);
				SetValidAction(ACT_SHOW_HAND);
				if (allowed_shuffle_show) {	// 20:::					SetValidAction(ACT_SHOW_SHUFFLED);
				}
				SetValidAction(ACT_MUCK_HAND);
				PostAction();
				return FALSE;
			}
		}
	}
}

/**********************************************************************************
 Function CommonRules::UpdateStructures(void)
 Date: 20180707 kriskoin :  Purpose: fill all the hand, player, card structures, etc / make up to date
***********************************************************************************/
void CommonRules::UpdateStructures(void)
{
	_internal_data.rake_total = _pot->GetRake();
	_internal_data.standard_bet_for_round = _pot->GetBetIncrement();
	int p_index;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// we'll conveniently grab all the pots here... tho p_index has nothing to do 
		// with it, the number of sidepots is the same as number of players
		_internal_data.pot[p_index] = _pot->GetPot(p_index);	// main pot -=> sidepot 9
		_internal_data.chips_bet_total[p_index] = _pot->GetChipsBetTotal(p_index);

		// If he's getting chips back, we can assume that the chips he's
		// bet are already animating their way towards the pot and therefore
		// we only need to tell the client how many chips are still in front
		// of him.
	  #if 0	//kriskoin: 		// 24/01/01 kriskoin:
		if (_game_state == GAME_OVER) {
			_internal_data.chips_in_front_of_player[p_index] = 0;
		} else 
	  #endif
		{
			if (_pot->GetChipsGotBack(p_index)) {
				_internal_data.chips_in_front_of_player[p_index] = 
					_pot->GetChipsGotBack(p_index);
			} else {
				_internal_data.chips_in_front_of_player[p_index] = 
					_pot->GetChipsBetRound(p_index);
			}
		}
	}
	// the internal data structure is our template... first thing to do is clone
	// it everywhere.  after that, we'll worry about the individualization
	_internal_data.game_state = (BYTE8)_game_state;
	FillInternalHandStructure();
	memcpy(&_watch_data, &_internal_data, sizeof(GamePlayerData));
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// make the clone here -- copy all, even if unused as not sure who uses these
		memcpy(&_player_data[p_index], &_internal_data, sizeof(GamePlayerData));
		_player_data[p_index].seating_position = (BYTE8)p_index;
		if (_pot->IsPlaying(p_index)) {
			
			// expected call/raise amounts...
			_player_data[p_index].call_amount = _pot->GetCallAmount(p_index);
			// client's raise amount is treated a little differently than the server
			_player_data[p_index].raise_amount = 
				max(_player_data[p_index].call_amount,_pot->GetRaiseAmount(p_index));
			_player_data[p_index].expected_turn_this_round = (BYTE8)_pot->ExpectingATurn(p_index);
		        
		}	// we don't care if garbage is in here if he's not playing as it doesn't get sent out
	}
	MaskCards(); // hide all cards players shouldn't see
}

/**********************************************************************************
 Function CommonRules::MaskCards(void)
 date: kriskoin 2019/01/01 Purpose: mask all cards in _player structures unless it's player's own or
          the game is over and he wants to (or has to) reveal his cards
 Note: this has been expanded to properly mask/leave cards in 7-card stud too
***********************************************************************************/
void CommonRules::MaskCards(void)
{
	for (int st_index=0; st_index < MAX_PLAYERS_PER_GAME; st_index++) {	// go through all structures
		for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
			for (int h_index=0; h_index < MAX_PRIVATE_CARDS; h_index++) {
				// mask all but his own
				if (_player_data[st_index].cards[p_index][h_index] != CARD_NO_CARD) {
					// there's a card there; mark it as required
					int hide_card = TRUE;	// hide by default, depending, might unmask
					if (st_index == p_index) hide_card = FALSE;	// it's him
					if (_show_hand[p_index] == SHOW_HAND_SHOWED) hide_card = FALSE;
					if (CommonData->game_rules == GAME_RULES_STUD7 || CommonData->game_rules == GAME_RULES_STUD7_HI_LO) {
						if (h_index > 1 && h_index < 6) // show 2,3,4,5
							hide_card = FALSE;
					}
					if (hide_card) {
						_player_data[st_index].cards[p_index][h_index] = CARD_HIDDEN;
					}
				}
			}
		}
	}
	// hide all cards in the watch structure -- unless it's game over
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		for (int h_index=0; h_index < MAX_PRIVATE_CARDS; h_index++) {
			// if there's a card there, and this player hasn't been to show his hand, hide it
			if (_watch_data.cards[p_index][h_index] != CARD_NO_CARD) {
				int hide_card = TRUE;	// hide by default, depending, might unmask
				if (_show_hand[p_index] == SHOW_HAND_SHOWED) hide_card = FALSE;
				if (CommonData->game_rules == GAME_RULES_STUD7 || CommonData->game_rules == GAME_RULES_STUD7_HI_LO) {
					if (h_index > 1 && h_index < 6) // show 2,3,4,5
						hide_card = FALSE;
				}
				if (hide_card) {
					_watch_data.cards[p_index][h_index] = CARD_HIDDEN;	// hide it
				}
			}
		}
	}
}

/**********************************************************************************
 Function FillInternalHandStructure(void)
 date: kriskoin 2019/01/01 Purpose: fill the _internal_data_ structure's card arrays with all known valid
		  cards... they will be masked out individually for players later
***********************************************************************************/
void CommonRules::FillInternalHandStructure(void)
{
	// fill the common cards -- if there are any
	if (_flop) {
		for (int h_index=0; h_index < MAX_PUBLIC_CARDS; h_index++) {
			_internal_data.common_cards[h_index] = _flop->GetCard(h_index);
		}
	}
	// do the individual cards
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_hand[p_index]) {	// there's a hand there, fill it
			for (int h_index=0; h_index < MAX_PRIVATE_CARDS; h_index++) {
				_internal_data.cards[p_index][h_index] = _hand[p_index]->GetCard(h_index);
			}
		}
	}
}

/**********************************************************************************
 Function CommonRules::SetExtraTimeoutDelayForAction(int milliseconds)
 date: kriskoin 2019/01/01 Purpose: this player may get some extra time
***********************************************************************************/
void CommonRules::AddToActionTimeOut(int milliseconds)
{
	int new_time_out = _GPIRequest.time_out + (milliseconds+999)/1000;
	_GPIRequest.time_out = (BYTE8)min(255,new_time_out);
	//kp(("%s(%d) AddToActionTimeOut() is setting _GPIRequest.time_out to %d\n",_FL,_GPIRequest.time_out));
}

/**********************************************************************************
 Function CommonRules::DealEveryoneOneCard
 date: kriskoin 2019/01/01 Purpose: starting left of the button, deal everyone one card
***********************************************************************************/
void CommonRules::DealEveryoneOneCard(int button_exists)
{
	// default call assumes player can not be all in -- overriden for 7cs
	DealEveryoneOneCard(button_exists, FALSE);
}

void CommonRules::DealEveryoneOneCard(int button_exists, int can_be_all_in)
{
	int plr_card_pairs[MAX_PLAYERS_PER_GAME*2];
	zstruct(plr_card_pairs);
	int plr_card_pair_count = 0;

	if (!_deck) {
		DIE("Tried to deal a card but we don't have a deck");
	}

	BYTE8 p_index;
	if (button_exists) {
		p_index = CommonData->p_button;
	} else {
		p_index = (BYTE8)(_max_this_game_players-1);	// will be set to 0 on first call
	}
	p_index = GetNextPlayerEligibleForCard(p_index, can_be_all_in);	// move to actual first player

	BYTE8 first_player_getting_card = p_index;
	do {
		Card card = _deck->DealNextCard();
		_hand[p_index]->Add(card);
		_cards_dealt++;
		plr_card_pairs[plr_card_pair_count*2] = p_index;
		plr_card_pairs[plr_card_pair_count*2+1] = card;
		plr_card_pair_count++;
		p_index = GetNextPlayerEligibleForCard(p_index, can_be_all_in);	// move to next player
	} while (p_index != first_player_getting_card);	// already dealt to this player -- exit loop

	// Log all the cards we just dealt
	if (plr_card_pair_count) {
		PL->LogGameCardsDealt(CommonData->game_serial_number, plr_card_pair_count, plr_card_pairs);
	}

	UpdateStructures();	// make sure they're all up to date
}

/**********************************************************************************
 Function ::SittingOut(p_index)
 date: kriskoin 2019/01/01 Purpose: returns T/F if a player is sitting out this hand for some reason
***********************************************************************************/
int CommonRules::SittingOut(BYTE8 p_index)
{
	if (_internal_data.player_status[p_index] == PLAYER_STATUS_SITTING_OUT ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_NOT_ENOUGH_MONEY ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_DID_NOT_POST_SB ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_DID_NOT_POST_BB ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_DID_NOT_POST_BOTH ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_DID_NOT_POST_INITIAL) {
			return TRUE;
	}
	return FALSE;
}

//*********************************************************
// https://github.com/kriskoin//
// Test if a particular player index is connected or not
//
int CommonRules::TestIfPlayerConnected(BYTE8 p_index)
{
	return ((CardRoom *)((Table *)_table_ptr)->cardroom)->TestIfPlayerConnected(CommonData->player_id[p_index]);
}

//*********************************************************
// https://github.com/kriskoin//
// Return our best guess about the quality of the connection to
// a player given just their seat (player)index.
// Returns CONNECTION_STATE_* (see player.h)
//
int CommonRules::GetPlayerConnectionState(BYTE8 p_index)
{
	return ((CardRoom *)((Table *)_table_ptr)->cardroom)->GetPlayerConnectionState(CommonData->player_id[p_index]);
}

//****************************************************************
// 
//
// Send a dealer message to all players.  Do this by calling
// the table's SendDealerMessage function
// adate: this function now queues up dealer messages -- we send
// them out all at once with PostDealerMessageQueue
//
void CommonRules::SendDealerMessage(int text_type, char *fmt, ...)
{
	va_list arg_ptr;
	va_start(arg_ptr, fmt);
	vsprintf(_szDealerTxt, fmt, arg_ptr);
	va_end(arg_ptr);
	_szDealerTxt[MAX_SZ_DEALER_TXT-1] = 0;	// terminate in just in case
	AddToDealerMessageQueue(text_type, _szDealerTxt);
	pr(("%s\n",_szDealerTxt));
}

/**********************************************************************************
 Function CommmonRules::AddToDealerMessageQueue(int text_type, char *msg)
 date: kriskoin 2019/01/01 Purpose: add dealer message to queue (to be sent with PostDealerMessageQueue()
***********************************************************************************/
void CommonRules::AddToDealerMessageQueue(int text_type, char *msg)
{
	if (_dealer_queue_index >= DEALER_QUEUE_SIZE) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) AddToDealerMessageQueue called with index of %d (MAX %d)",
			_FL, _dealer_queue_index, DEALER_QUEUE_SIZE);
		_dealer_queue_index = 0; // set it to 0, overwrite whatever is there
	}
	// _dealer_queue_index is current
	DealerChatQueue[_dealer_queue_index].text_type = text_type;
	strnncpy(DealerChatQueue[_dealer_queue_index].message, msg, MAX_SZ_DEALER_TXT);
	// increment index
	_dealer_queue_index++;
	if (_dealer_queue_index >= DEALER_QUEUE_SIZE) {
		Error(ERR_WARNING,"%s(%d) AddToDealerMessageQueue index exceeded (MAX %d)",
			_FL, DEALER_QUEUE_SIZE);
		_dealer_queue_index = 0; // set it to 0
	}
}

/**********************************************************************************
 Function CommonRules::PostDealerMessageQueue(void)
 date: kriskoin 2019/01/01 Purpose: 
***********************************************************************************/
void CommonRules::PostDealerMessageQueue(void)
{
	for (int i=0; i < DEALER_QUEUE_SIZE; i++) {
		if (DealerChatQueue[i].text_type != CHATTEXT_NONE) {	// something valid there
			Table_SendDealerMessage( _table_ptr, 
				DealerChatQueue[i].message, 
				DealerChatQueue[i].text_type);
			DealerChatQueue[i].text_type = CHATTEXT_NONE;	// clear it for next time
		}
	}
	_dealer_queue_index = 0;	// start again next time
}

/**********************************************************************************
 Function CommonRules::BuildPotentialWinnersList(int pot_number)
 date: kriskoin 2019/01/01 Purpose: build a list of who can win this hand and their state
***********************************************************************************/
void CommonRules::BuildPotentialWinnersList(int pot_number)
{
	// we might get here with a single winner -- even before the last card is dealt
	if (!_dealt_river) {	// must be everyone folding to a single player
		if (_pot->GetPlayersLeft() != 1) {
			Error(ERR_FATAL_ERROR,"%s(%d) IMPOSSIBLE: %d players left and no river card",
				_FL, _pot->GetPlayersLeft());
			DIE("in BuildPotentialWinnersList with one player but river hasn't been dealt");
		} else {	// ok, single winner
			int p_index = _pot->GetFirstActivePlayer(TRUE);
			_live_for_payoff[p_index] = TRUE;
			_show_hand[p_index] = SHOW_HAND_ASK;
			_winner_of_pot_hi[p_index] = TRUE;
			_total_winners = 1;
			_live_players_for_pot = 1;
			_pot_amount_playing_for = _pot->GetTotalPot();
			((Table *)_table_ptr)->had_best_hand_last_game[p_index] = TRUE;
			return;
		}
	}

	// since the sidepots are stored as "markers", the actual amount a pot plays for
	// is the difference between it and the next one
	_pot_amount_playing_for = _pot->GetPot(pot_number);
	_live_players_for_pot = 0;
	int p_index;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if ( (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
			_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) &&
			_pot->PlayerIsLiveForPot(p_index, pot_number) ) {
				_live_players_for_pot++;
				_live_for_payoff[p_index] = TRUE;
			  #ifdef POT_DEBUG
				kp(("%s is live for a pot of %d\n",
					CommonData->name[p_index], _pot_amount_playing_for));
			  #endif
				// if we're not sure of his hand_show state, we'll set to ASK for now,
				// though it may be overriden very shortly (winner, only player, etc)
				if (_show_hand[p_index] == SHOW_HAND_UNKNOWN) {
					_show_hand[p_index] = SHOW_HAND_ASK;
				}
				// adate: in fact, unless he's shown, he should be asked.  this is important
				// for sidepot showdowns
				if (_show_hand[p_index] != SHOW_HAND_SHOWED && 
					_show_hand[p_index] != SHOW_HAND_MUCK) {
					_show_hand[p_index] = SHOW_HAND_ASK;
				}

		} else {	// player isn't live
			_live_for_payoff[p_index] = FALSE;
			_winner_of_pot_hi[p_index] = FALSE;
			_winner_of_pot_lo[p_index] = FALSE;
		}
	}
	// we now know enough to mark off all winning players
	Hand best_hand;
	Hand best_low_hand;
	int first_call = TRUE;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_live_for_payoff[p_index]) {
			// test if he's got a best hand defined... if not, something is wrong
			if (!_best_hand[p_index]) {
				Error(ERR_FATAL_ERROR,"%s(%d) IMPOSSIBLE: no _best_hand for %d", _FL, p_index);
				DIE("logic error in call to BuildPotentialWinnersList");
			}
			// find the best hand out there
			if (_poker->FindBetterHand(*(_best_hand[p_index]), best_hand) == HAND_1) {
				best_hand = *(_best_hand[p_index]);
			}
			if (_split_pot_game) {// find the lowest hand out there too
				if (first_call) {
					first_call = FALSE;
					best_low_hand = best_hand;
				}
				if (_poker->FindBetterHand(*(_low_hand[p_index]), best_low_hand) == HAND_2) {
					best_low_hand = *(_low_hand[p_index]);
				}
			}
		}
	}
	// figure out if the low is low enough
	int valid_low = FALSE;
	if (_split_pot_game) {
		valid_low = _poker->ValidLowHand(&best_low_hand);
	}
	// we now know the best hand -- mark everyone who's got it
	_total_winners = 0;			// we only want how many winners are holding this hand
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// reset them all to non-winners -- it'll be set if it needs to; it may have
		// been true for now if they won a side pot, but now are losing to a lesser
		// amount sidepot
		_winner_of_pot_hi[p_index] = FALSE;
		_winner_of_pot_lo[p_index] = FALSE;
		int won_something = FALSE;
		if (_live_for_payoff[p_index]) {
			if (_poker->FindBetterHand(*(_best_hand[p_index]), best_hand) == HAND_TIE) {
				// 24/01/01 kriskoin:
				// winner of pot 0
				if (pot_number == 0) {
					((Table *)_table_ptr)->had_best_hand_last_game[p_index] = TRUE;
				}
				_winner_of_pot_hi[p_index] = TRUE;
				won_something = TRUE;
			}
			if (valid_low) {
				if (_poker->FindBetterHand(*(_low_hand[p_index]), best_low_hand) == HAND_TIE) {
					// 24/01/01 kriskoin:
					// winner of pot 0
					if (pot_number == 0) {
						((Table *)_table_ptr)->had_best_hand_last_game[p_index] = TRUE;
					}
					_winner_of_pot_lo[p_index] = TRUE;
					won_something = TRUE;
				}
			}
		  #if 0	//!!!! TEMP FOR TESTING !!!!
		  	kp1(("%s(%d) TURN THIS OFF!\n",  _FL));
			((Table *)_table_ptr)->had_best_hand_last_game[p_index] = TRUE;
		  #endif
		}
		if (won_something) {	// we want individuals
			_total_winners++;
		}
	}
  #ifdef POT_DEBUG
 	kp(("this pot(%d) had %d %s\n", pot_number, _total_winners, _total_winners == 1 ? "winner" : "winners"));
  #endif
}

/**********************************************************************************
 Function CommonRules::AnyPlayerLeftToAskAboutShowingHand(void)
 date: kriskoin 2019/01/01 Purpose: T/F -- are we still waiting for someone to show their hand?
***********************************************************************************/
int CommonRules::AnyPlayerLeftToAskAboutShowingHand(void)
{
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_live_for_payoff[p_index]) {
			if (_show_hand[p_index] == SHOW_HAND_ASK) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/**********************************************************************************
 Function CommonRules::ClearChipsInFrontOfPlayers(void)
 date: kriskoin 2019/01/01 Purpose: check if anyone has chips in front of them; clear them if they do and
		  send it out to everyone
***********************************************************************************/
void CommonRules::ClearChipsInFrontOfPlayers(void)
{
	if (_pot->ClearChipsInFrontOfPlayers()) {
		SendDataNow();
	}
}

/**********************************************************************************
 Function CommonRules::DistributePot(void)
 date: kriskoin 2019/01/01 Purpose: distribute the pot among the winners
***********************************************************************************/
void CommonRules::DistributePot(void)
{
	ClearChipsInFrontOfPlayers();	// needed for animations
	// count how many winners each pot type had
	_hi_pot_winners = 0;
	_lo_pot_winners = 0;
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		if (_winner_of_pot_hi[i]) {
			_hi_pot_winners++;
		}
		if (_winner_of_pot_lo[i]) {
			_lo_pot_winners++;
		}
	}
	// allocate pot(s) to distribute
	#define SMALLEST_CHIP_INCREMENT	25	// 25 cent chips
	int hi_pot = 0;
	int lo_pot = 0;
	if (_lo_pot_winners) {	// split the pot
		// let's not lose any $$!
		lo_pot = (int)(_pot_amount_playing_for / 2);
		// round to nearest .25
		lo_pot -= lo_pot % SMALLEST_CHIP_INCREMENT;
		hi_pot = _pot_amount_playing_for - lo_pot;
	} else {	// only one pot
		hi_pot = _pot_amount_playing_for;
	}

	// HI POT FIRST....
	int hi_win_per_player = (int)(hi_pot / _hi_pot_winners);	// might round down
	// given that we're working in pennies, we want the amount to be an even dollar amount
	// for now.  if there's anything left over, it'll get distributed properly below
	// adate: we always pay first the even dollar increments... so number below should
	// never change!  split pots, 50cents, etc.. are done below
	// 24/01/01 kriskoin:
	hi_win_per_player -= (hi_win_per_player % SMALLEST_CHIP_INCREMENT);	// round down to increment
	int p_index;	// also used below
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		_chips_won_this_pot_hi[p_index] = 0;	// cleared every pot
		if (_winner_of_pot_hi[p_index]) {	// here's one
		  #ifdef POT_DEBUG
			kp((" *** player %d gets %d chips from hi pot\n", p_index, hi_win_per_player));
		  #endif
			_internal_data.chips_won[p_index] += hi_win_per_player;
			_chips_won_this_pot_hi[p_index] += hi_win_per_player;
			hi_pot -= hi_win_per_player;
		}
	}
	// if there is a tiny remainder left in the pot, distribute it starting left of the button
	int w_index = CommonData->p_button;
	while (hi_pot) {
		if (hi_pot < 0) {	// shouldn't happen
			Error(ERR_INTERNAL_ERROR,"%s(%d) Hi pot_amount_playing_for went negative", _FL);
			break;	// abort...
		}
	  #ifdef POT_DEBUG
		kp(("Hi pot still has %d pennies left\n", hi_pot ));
	  #endif
		int loop_count = 0;	// catch an endless loop (which shouldn't be possible)
		// we'll give it to the guy in earliest position
		forever {
			w_index = (w_index+1) % MAX_PLAYERS_PER_GAME;
			if (_winner_of_pot_hi[w_index]) {	// he gets it
				// adate: to go to 50-cent chips, just change this from 100 to 50
			  #ifdef POT_DEBUG
				kp(("player %d gets %d cents from hi pot\n", w_index, SMALLEST_CHIP_INCREMENT));
			  #endif
				int amt_player_gets = min(SMALLEST_CHIP_INCREMENT, hi_pot);
				_internal_data.chips_won[w_index] += amt_player_gets;
				_chips_won_this_pot_hi[w_index] += amt_player_gets;
				hi_pot -= amt_player_gets;
				break;
			}
			if (loop_count++ > 2*MAX_PLAYERS_PER_GAME) {	// _serious_ problem
				Error(ERR_INTERNAL_ERROR,"%s(%d) IMPOSSIBLE: no _winner[] marked true", _FL);
				// if this ever happens, it means we're stuck with a pot we can't distribute
				// for this game.
				Error(ERR_INTERNAL_ERROR,"%s(%d) Game %d should be investigated",
					_FL, CommonData->game_serial_number);
				break;
			}
		}
	}
	// NOW THE LO POT....
	// if there's nothing to do here, we can just exit -- nothing below this
	if (!_lo_pot_winners) {
		return;
	}
	int lo_win_per_player = (int)(lo_pot / _lo_pot_winners);	// might round down
	// see comments above re breakage
	lo_win_per_player -= (lo_win_per_player % 25);	// round down to increment
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		_chips_won_this_pot_lo[p_index] = 0;	// cleared every pot
		if (_winner_of_pot_lo[p_index]) {	// here's one
		  #ifdef POT_DEBUG
			kp((" *** player %d gets %d chips from lo pot\n", p_index, hi_win_per_player));
		  #endif
			_internal_data.chips_won[p_index] += lo_win_per_player;
			_chips_won_this_pot_lo[p_index] += lo_win_per_player;
			lo_pot -= lo_win_per_player;
		}
	}
	// if there is a tiny remainder left in the pot, distribute it starting left of the button
	w_index = CommonData->p_button;
	while (lo_pot) {
		if (lo_pot < 0) {	// shouldn't happen
			Error(ERR_INTERNAL_ERROR,"%s(%d) Lo pot_amount_playing_for went negative", _FL);
			break;	// abort...
		}
	  #ifdef POT_DEBUG
		kp(("Lo pot still has %d pennies left\n", lo_pot));
	  #endif
		int loop_count = 0;	// catch an endless loop (which shouldn't be possible)
		// we'll give it to the guy in earliest position
		forever {
			w_index = (w_index+1) % MAX_PLAYERS_PER_GAME;
			if (_winner_of_pot_lo[w_index]) {	// he gets it
				#define SMALLEST_CHIP_INCREMENT	25	// 25 cent chips
			  #ifdef POT_DEBUG
				kp(("player %d gets %d cents from lo pot\n", w_index, SMALLEST_CHIP_INCREMENT));
			  #endif
				int amt_player_gets = min(SMALLEST_CHIP_INCREMENT, lo_pot);
				_internal_data.chips_won[w_index] += amt_player_gets;
				_chips_won_this_pot_lo[w_index] += amt_player_gets;
				lo_pot -= amt_player_gets;
				break;
			}
			if (loop_count++ > 2*MAX_PLAYERS_PER_GAME) {	// _serious_ problem
				Error(ERR_INTERNAL_ERROR,"%s(%d) IMPOSSIBLE: no _winner[] marked true", _FL);
				// if this ever happens, it means we're stuck with a pot we can't distribute
				// for this game.
				Error(ERR_INTERNAL_ERROR,"%s(%d) Game %d should be investigated",
					_FL, CommonData->game_serial_number);
				break;
			}
		}
	}
}

// kriskoin 
void CommonRules::SetFileLock(int fd, short type)
{
        struct flock arg;

        arg.l_type = type;      /* lock type setting */
        arg.l_whence = 0;       /* from start of file */
        arg.l_start = 0;        /* byte offset to begining */
        arg.l_len = 0;          /* until end of file */
        if(fcntl(fd,F_SETLK,&arg) == -1 && errno == EACCES) {  /* busy */
          kp(("%s(%d): waiting for lock of hihand files...\n", _FL));
          fcntl(fd,F_SETLKW,&arg);
        }

}

/**********************************************************************************
Function CommonRules::GetSavedHiHand(int, int)
Date: 2002/03/19 Robert Gong
Purpose: read pre-saved hi-hand info from hihand.bin file
***********************************************************************************/
time_t CommonRules::GetSavedHiHand(int flag, int tie_flag)
{

	if(flag) {
	    if(tie_flag){
		memcpy(_hi_hand_of_day, &hihand_real[1].hand_rec, sizeof(Hand));
		strcpy(_name_of_day, hihand_real[1].user_id);
		_game_num_of_day = hihand_real[1].game_serial_num;
		return(hihand_real[1].timestamp);
	    } else {
		memcpy(_hi_hand_of_day, &hihand_real[0].hand_rec, sizeof(Hand));
		strcpy(_name_of_day, hihand_real[0].user_id);
		_game_num_of_day = hihand_real[0].game_serial_num;
		return(hihand_real[0].timestamp);
	    }
	} else {
	    if(tie_flag){
		memcpy(_hi_hand_of_day, &hihand_play[1].hand_rec, sizeof(Hand));
		strcpy(_name_of_day, hihand_play[1].user_id);
		_game_num_of_day = hihand_play[1].game_serial_num;
		return(hihand_play[1].timestamp);
	    } else {
		memcpy(_hi_hand_of_day, &hihand_play[0].hand_rec, sizeof(Hand));
		strcpy(_name_of_day, hihand_play[0].user_id);
		_game_num_of_day = hihand_play[0].game_serial_num;
		return(hihand_play[0].timestamp);
	    }
	}

}
/**********************************************************************************
** Function CommonRules::SaveHiHand(Hand*, INT32, char*, int, int)
** Date: 2002/03/19 Robert Gong
** Purpose: update hi-hand info to hihand.bin file
***********************************************************************************/
void CommonRules::SaveHiHand(Hand *hand, int game_serial_num, char *user_id, int flag, int tie_flag)
{
	int fp;
	if(tie_flag){
	   if(flag) {
		hihand_real[1].game_serial_num=game_serial_num;
		hihand_real[1].timestamp=time(NULL);
		strcpy(hihand_real[1].user_id, user_id);
		memcpy(&hihand_real[1].hand_rec, hand, sizeof(Hand));
//		fp=open("logs/hihand_real.bin",O_RDWR);
		fp=open("Data\\DB\\hihand_real.bin",O_RDWR);
		SetFileLock(fp, F_WRLCK);
		write(fp, &hihand_real[1], sizeof(HighHand));
		close(fp);
	   } else {
		hihand_play[1].game_serial_num=game_serial_num;
		hihand_play[1].timestamp=time(NULL);
		strcpy(hihand_play[1].user_id, user_id);
		memcpy(&hihand_play[1].hand_rec, hand, sizeof(Hand));
//		fp=open("logs/hihand.bin",O_RDWR);
		fp=open("Data\\DB\\hihand.bin",O_RDWR);

		SetFileLock(fp, F_WRLCK);
		write(fp, &hihand_play[1], sizeof(HighHand));
		close(fp);
	   }
	} else {
	   if(flag) {
		hihand_real[0].game_serial_num=game_serial_num;
		hihand_real[0].timestamp=time(NULL);
		strcpy(hihand_real[0].user_id, user_id);
		memcpy(&hihand_real[0].hand_rec, hand, sizeof(Hand));
		memset(&hihand_real[1],0,sizeof(HighHand));

//		fp=open("logs/hihand_real.bin",O_RDWR);
		fp=open("Data\\DB\\hihand_real.bin",O_RDWR);
		SetFileLock(fp, F_WRLCK);
		write(fp, &hihand_real[0], sizeof(HighHand));
		close(fp);
	   } else {
		hihand_play[0].game_serial_num=game_serial_num;
		hihand_play[0].timestamp=time(NULL);
		strcpy(hihand_play[0].user_id, user_id);
		memcpy(&hihand_play[0].hand_rec, hand, sizeof(Hand));
		memset(&hihand_play[1],0,sizeof(HighHand));
		fp=open("logs/hihand.bin",O_RDWR);
		SetFileLock(fp, F_WRLCK);
		write(fp, &hihand_play[0], sizeof(HighHand));
		close(fp);
	   }
	}

}

/**********************************************************************************
Function CommonRules::SaveAndLogHiHand(void)
Date: 2002/03/19 Robert Gong
Purpose: compare current hi-hand with the pre-saved hi-hand from hihand.bin file
	 and save the higher hand info and log to the HAL file
***********************************************************************************/
void CommonRules::SaveAndLogHiHand(void)
{
        char str[64];
        char str_tie[64];
        zstruct(str);
        zstruct(str_tie);:::
	time_t saved_time;
	time_t cur_time;
	tm dt_cur;
	tm dt_sav;
	FILE *fp;
	int real_money_flag;

	real_money_flag = CommonData->flags & GCDF_REAL_MONEY;
	
	cur_time=time(NULL);
	memcpy(&dt_cur, localtime(&cur_time), sizeof(tm));

        for (int i=0; i< MAX_PLAYERS_PER_GAME; i++){
           if(_winner_of_pot_hi[i]){
		if(_best_hand[i]){
		   saved_time=GetSavedHiHand(real_money_flag,0);
		   if(saved_time==0){    //new file
			SaveHiHand(_best_hand[i], CommonData->game_serial_number, CommonData->name[i], real_money_flag, 0);
		   } else { 
		     memcpy(&dt_sav, localtime(&saved_time), sizeof(tm));
		     if (dt_cur.tm_mday!=dt_sav.tm_mday){  //new day
                   	_hi_hand_of_day->GetASCIIHand(str);
			
			if(real_money_flag){
//				fp=fopen("logs/hihand_real.log","a+");
				fp=fopen("Data/Logs/hihand_real.log","a+");

			} else {
//				fp=fopen("logs/hihand.log","a+");
				fp=fopen("Data/Logs/hihand.log","a+");
			}
			fprintf(fp,"%4d-%2d-%2d %2d:%2d:%2d\t%s\t%d\t%s\n",
				dt_sav.tm_year+1900,
				dt_sav.tm_mon+1,
				dt_sav.tm_mday,
				dt_sav.tm_hour,
				dt_sav.tm_min,
				dt_sav.tm_sec,
				_name_of_day,
				_game_num_of_day,
				str);
			saved_time = GetSavedHiHand(real_money_flag,1);
			if(saved_time>0 && real_money_flag){
				_hi_hand_of_day->GetASCIIHand(str_tie);
				if(strcmp(str,str_tie)==0){
					fprintf(fp,"%d-%d-%d\t%s\t%d\t%s\n",
						dt_sav.tm_year+1900,
						dt_sav.tm_mon+1,
						dt_sav.tm_mday,
						_name_of_day,
						_game_num_of_day,
						str);
				}
			}
			fclose(fp);
			SaveHiHand(_best_hand[i], CommonData->game_serial_number, CommonData->name[i], real_money_flag, 0);
		     } else {
			
		   	if(_poker->FindBetterHand(*(_hi_hand_of_day), *(_best_hand[i]))==HAND_2){
			   SaveHiHand(_best_hand[i], CommonData->game_serial_number, CommonData->name[i], real_money_flag, 0);
		   	}
		   	if(_poker->FindBetterHand(*(_hi_hand_of_day), *(_best_hand[i]))==HAND_TIE){
			   SaveHiHand(_best_hand[i], CommonData->game_serial_number, CommonData->name[i], real_money_flag, 1);
			}
		     }
		   }
		}
           }
        }
}
// end kriskoin 

/**********************************************************************************
 Function CommonRules::AnnounceWinners(void)
 date: kriskoin 2019/01/01 Purpose: announce the winner(s)
 // 24/01/01 kriskoin:
***********************************************************************************/
void CommonRules::AnnounceWinners(int pot_number, int first_pass)
{
	// pot 0 is the main pot; if it's numbered, it's a side pot
	char szPotName[10];
	zstruct(szPotName);
	char cs[MAX_CURRENCY_STRING_LEN];
	zstruct(cs);

	if (_pot->GetPot(1)) {	// at least one side pot
		sprintf(szPotName,"%s",(pot_number ? "side pot" : "main pot" ));
	} else {	// no side pots
		zstruct(szPotName);
	}
	int printed_low_message = FALSE;
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_winner_of_pot_hi[p_index] || _winner_of_pot_lo[p_index]) {
			// just to be sure, we'll reconstruct the hand descriptions (if they exist already)
			if (_szHandDescriptionHi[p_index][0]) {
				_poker->GetHandCompareDescription(_best_hand[p_index], NULL, _szHandDescriptionHi[p_index], TRUE);
			}
			if (_szHandDescriptionLo[p_index][0]) {
				_poker->GetHandCompareDescription(_low_hand[p_index], NULL, _szHandDescriptionLo[p_index], FALSE);
			}
			// figure out if just a little was one -- messages are a little different
			int only_winner_amt = _chips_won_this_pot_hi[p_index]+_chips_won_this_pot_lo[p_index];
			int won_little = (only_winner_amt < (3*CommonData->big_blind_amount));
			if (_show_hand[p_index] != SHOW_HAND_SHOWED) {	// must be only winner
				// we only show this message on the first pass
				if (first_pass) {
					if (pot_number) {	// it's a side pot
						if (won_little) {
							// won a little side pot -- didn't show
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "The little side pot is yours, %s (%s)", 
								CommonData->name[p_index],
								CurrencyString(cs, only_winner_amt, _chip_type));
						} else {
							// won a big side pot -- didn't show
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "Take down the side pot, %s (%s)", 
								CommonData->name[p_index],
								CurrencyString(cs, only_winner_amt, _chip_type));
						} 
					} else {
						if (won_little) {
							// won a little main pot -- didn't show
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "It's yours, %s", CommonData->name[p_index]);
						} else {
							// won a big main pot -- didn't show
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "Take it down, %s (%s)", 
								CommonData->name[p_index],
								CurrencyString(cs, only_winner_amt, _chip_type));
						}
					}
				}
			} else {	// hand was shown -- more detail
				if (_split_pot_game && !_lo_pot_winners && _show_hand_count > 1) {
					// we might want to show that nobody had a low hand
					// we only show this message on the first pass
					if (first_pass) {
						if (!printed_low_message) {	// only once
							printed_low_message = TRUE;
							char msg[100];
							zstruct(msg);
							if (_pot->GetPot(1)) {	// at least one side pot
								sprintf(msg, "No hands qualified for the %s pot",
									pot_number ? "side" : "main");
							} else {
								sprintf(msg,"%s","No low hand qualified");
							}
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s", msg);
						}
					}
				}
				// we only show this message on the second pass
				if (!first_pass) {
					if (_chips_won_this_pot_hi[p_index]) {
						if (_szHandDescriptionHi[p_index][0]) {
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s %s%s(%s%s%s%s%s) with %s", 
								CommonData->name[p_index],
								(_hi_pot_winners > 1 ? "ties" : "wins"),
								(_split_pot_game ? " Hi " : " "),
								CurrencyString(cs,_chips_won_this_pot_hi[p_index], _chip_type),
								(_chip_type == CT_PLAY || szPotName[0] ? " " : ""),
//								(_chip_type == CT_REAL ? "" : "play chips"),
								(_chip_type == CT_PLAY ? "play chips" : ""),
								(szPotName[0] ? (_chip_type == CT_PLAY ? ", " : "") : ""),
								szPotName,
								_szHandDescriptionHi[p_index]);
						} else {	// no description
							if (won_little) {
								SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s takes it (%s)", 
									CommonData->name[p_index],
									CurrencyString(cs,_chips_won_this_pot_hi[p_index], _chip_type));
							} else {
								SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s takes it down (%s)", 
									CommonData->name[p_index],
									CurrencyString(cs,_chips_won_this_pot_hi[p_index], _chip_type));
							}
						}
					}
				}					
				// we only show this message on the first pass
				if (first_pass) {
					if (_chips_won_this_pot_lo[p_index]) {
						if (_szHandDescriptionLo[p_index][0]) {
							SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s %s%s(%s%s%s%s%s) with %s", 
								CommonData->name[p_index], 
								(_lo_pot_winners > 1 ? "ties" : "wins"),
								(_split_pot_game ? " Lo " : " "),
								CurrencyString(cs,_chips_won_this_pot_lo[p_index], _chip_type),
								(_chip_type == CT_PLAY || szPotName[0] ? " " : ""),
//								(_chip_type == CT_REAL  ? "" : "play chips"),
								(_chip_type == CT_PLAY ? "play chips" : ""),
								(szPotName[0] ? (_chip_type == CT_PLAY ? ", " : "") : ""),
								szPotName,
								_szHandDescriptionLo[p_index]);
						} else {	// no description
							if (won_little) {
								SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s takes it (%s)", 
									CommonData->name[p_index],
									CurrencyString(cs,_chips_won_this_pot_lo[p_index], _chip_type));
							} else {
								SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s takes it down (%s)", 
									CommonData->name[p_index],
									CurrencyString(cs,_chips_won_this_pot_lo[p_index], _chip_type));
							}
						}
					}
				}
			}
		}
	}
}

/**********************************************************************************
 Function *CommonRules::GenderPronoun(BYTE8 gender)
 date: kriskoin 2019/01/01 Purpose: given a gender type, return the proper pronoun
***********************************************************************************/
char *CommonRules::GenderPronoun(BYTE8 p_index)
{
	static char male[] = "his";
	static char female[] = "her";
	static char unknown[] = "their";

	switch ((BYTE8)CommonData->gender[p_index]) {
		case GENDER_MALE   : return male;
		case GENDER_FEMALE : return female;
		case GENDER_UNKNOWN: return unknown;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) Got gender %d for player %d",
				_FL, CommonData->gender[p_index], p_index);
			return unknown;
	}
}

/**********************************************************************************
 Function CommonRules::UpdateDataBaseChipCounts(void)
 date: kriskoin 2019/01/01 Purpose: update valid chips counts for all players (including rake)
***********************************************************************************/
void CommonRules::UpdateDataBaseChipCounts(void)
{
	// add the rake to the database rake account
	SDB->AddToRakeAccount(_pot->GetRake(), _chip_type);
	// tally each individual player's chip amounts
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (CommonData->player_id[p_index]) { // ignore zeros :::			WORD32 chips_net_change = _internal_data.chips_won[p_index] - 
				_internal_data.chips_bet_total[p_index];
			SDB->HandleEndGameChipChange(CommonData->player_id[p_index],
				chips_net_change, _chip_type);
			PL->LogFinancialTransaction(LOGTYPE_TRANS_PP_GAME_RESULT, CommonData->player_id[p_index], 
				CommonData->game_serial_number, chips_net_change, _chip_type, NULL, NULL);
		}
	}
}

/**********************************************************************************
 Function CommonRules::LogPlayerStayedThisFar(CommonGameState cgs)
 date: kriskoin 2019/01/01 Purpose: players who stayed in for certain events get logged
***********************************************************************************/
void CommonRules::LogPlayerStayedThisFar(CommonGameState cgs)
{
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if ( (_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
		_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) ) {
			switch (cgs) {
			case DEAL_POCKETS: SDB->PlayerSawPocket(CommonData->player_id[p_index]); break;
			case DEAL_FLOP: SDB->PlayerSawFlop(CommonData->player_id[p_index]); break;
			case DEAL_RIVER: SDB->PlayerSawRiver(CommonData->player_id[p_index]); break;
			}
		}
	}
}

/**********************************************************************************
 Function CommonRules::RefundChips(void)
 date: kriskoin 2019/01/01 Purpose: refunds chips to all players that have posted/anted/bet/etc... used if the
          game is cancelled for whatever reason
***********************************************************************************/
void CommonRules::RefundChips(void)
{
	_pot->RefundAllChips();
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		_internal_data.chips_won[p_index] = 0;
	}
}

/**********************************************************************************
 Function CommonRules::RemoveCards(int p_index)
 date: kriskoin 2019/01/01 Purpose: remove a player's cards
***********************************************************************************/
void CommonRules::RemoveCards(int p_index)
{
	if (!_hand[p_index]) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to remove cards from a hand(%d) that doesn't exist",
			_FL, p_index);
	} else {
		_hand[p_index]->ClearHandCards();
	}
}

/**********************************************************************************
 Function CommonRules::SendDataNow(void)
 date: kriskoin 2019/01/01 Purpose: default call, updates structures
***********************************************************************************/
void CommonRules::SendDataNow(void)
{
	SendDataNow(TRUE);
}

/**********************************************************************************
 Function CommonRules::SendDataNow(int update__structures_flag)
 date: kriskoin 2019/01/01 Purpose: forces a data-structure send to all players
 Note:    this is neccessary when we're looping sometimes, without exiting processing
***********************************************************************************/
void CommonRules::SendDataNow(int update_structures_flag)
{
	if (!_table_ptr) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to send data to a null table", _FL);
		return;
	}
	pr(("%s(%d) SendDataNow() got called\n", _FL));
	// TRUE sends it to everyone
	if (update_structures_flag) {
		UpdateStructures();	// make sure they're all up to date
	}
	Table_SendDataNow(_table_ptr);
	// flush dealer queue, if there is one
	PostDealerMessageQueue();
}

/**********************************************************************************
 Function CommonRules::LogEndGame(void)
 date: kriskoin 2019/01/01 Purpose: log end game stats for players and game
***********************************************************************************/
void CommonRules::LogEndGame(void)
{
	Log_GameEnd	lge;
	zstruct(lge);
	char szStr[MAX_HAND_DESC_LEN*2+1];
	for (int p_index = 0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// build a singular hand description to log as well
		BLANK(szStr);
		if (_szHandDescriptionHi[p_index][0]) {
			sprintf(szStr, "%s", _szHandDescriptionHi[p_index]);
		} 
		if (_szHandDescriptionLo[p_index][0]) {
			sprintf(szStr, "%s~%s", _szHandDescriptionHi[p_index], _szHandDescriptionLo[p_index]);
		} 
		if (CommonData->player_id[p_index]) { // someone there..?
			PL->LogGameEndPlayer(CommonData->game_serial_number, p_index, 
				(_show_hand[p_index] == SHOW_HAND_SHOWED), 		
				_internal_data.chips_won[p_index] - _internal_data.chips_bet_total[p_index], szStr);
		}
		// while we're here, grab the pots...
		lge.pot[p_index] = _pot->GetPot(p_index);
	}
	lge.game_serial_number = CommonData->game_serial_number;
	lge.rake = _pot->GetRake();
	PL->LogGameEnd(&lge);
}

/**********************************************************************************
 Function CheckForBadBeat()
 date: 24/01/01 kriskoin Purpose: check for bad beats on the hand and make payoffs if necessary
***********************************************************************************/
void CommonRules::CheckForBadBeat()
{
	if (_chip_type != CT_REAL) {	// must be real money game
		return;
	}

	Hand* bad_beat_hand = NULL;
	int bad_beat_active = 0;
	int bad_beat_jackpot = 0;
	switch (CommonData->game_rules) {
	case GAME_RULES_HOLDEM:
		if (!BadBeatJackpotActive_holdem) {	// nothing to do
			return;
		}
		bad_beat_active = BadBeatJackpotActive_holdem;
		bad_beat_jackpot = BadBeatJackpot_holdem;
		bad_beat_hand = &Hand_BadBeat_holdem;
		if (!BadBeatDefinition_holdem[0]) {
			_poker->GetHandCompareDescription(bad_beat_hand, NULL, BadBeatDefinition_holdem);
			kp(("%s(%d) defined Hold'em BadBeatJackpot Hand: %s\n", _FL, BadBeatDefinition_holdem));
		}
		break;			
	case GAME_RULES_OMAHA_HI:
	case GAME_RULES_OMAHA_HI_LO:
		if (!BadBeatJackpotActive_omaha) {	// nothing to do
			return;
		}
		bad_beat_active = BadBeatJackpotActive_omaha;
		bad_beat_jackpot = BadBeatJackpot_omaha;
		bad_beat_hand = &Hand_BadBeat_omaha;
		if (!BadBeatDefinition_omaha[0]) {
			_poker->GetHandCompareDescription(bad_beat_hand, NULL, BadBeatDefinition_omaha);
			kp(("%s(%d) defined Omaha BadBeatJackpot Hand: %s\n", _FL, BadBeatDefinition_omaha));
		}
		break;			
	case GAME_RULES_STUD7:
	case GAME_RULES_STUD7_HI_LO:
		if (!BadBeatJackpotActive_stud) {	// nothing to do
			return;
		}
		bad_beat_active = BadBeatJackpotActive_stud;
		bad_beat_jackpot = BadBeatJackpot_stud;
		bad_beat_hand = &Hand_BadBeat_stud;
		if (!BadBeatDefinition_stud[0]) {
			_poker->GetHandCompareDescription(bad_beat_hand, NULL, BadBeatDefinition_stud);
			kp(("%s(%d) defined Stud-7 BadBeatJackpot Hand: %s\n", _FL, BadBeatDefinition_stud));
		}
		break;			
	default:
		kp(("%s(%d) Unknown game_rules (%d) -- see src\n", _FL, CommonData->game_rules));
		break;
	}
	
  #if 0	// moved above for different cases
	// build and display description of this hand
	if (!BadBeatDefinition[0]) {
		_poker->GetHandCompareDescription(&Hand_BadBeat, NULL, BadBeatDefinition);
		kp(("%s(%d) defined BadBeatJackpot Hand: %s\n", _FL, BadBeatDefinition));
	}
  #endif	

	// figure out who's eligible (anyone who finished the hand)
	int eligible[MAX_PLAYERS_PER_GAME];
	int eligible_count = 0;
	zstruct(eligible);
	int p_index;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME ; p_index++) {
		if ((_internal_data.player_status[p_index]  == PLAYER_STATUS_PLAYING || 
			_internal_data.player_status[p_index]  == PLAYER_STATUS_ALL_IN) &&
			_best_hand[p_index]) {
				eligible[p_index] = TRUE;
				eligible_count++;
		}
	}
	if (eligible_count < 2) {	// nothing to do
		return;
	}
	// more than one live player, see who has a good enough hand
	int qualified[MAX_PLAYERS_PER_GAME];
	zstruct(qualified);
	int qualified_count = 0;
	Hand best_hand; // find the best hand
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME ; p_index++) {
		if (eligible[p_index]) {
			// check if the best hand included 2 pocket cards
			int pocket_count = 0;
			for (int card_index = 0; card_index < MAX_PRIVATE_CARDS; card_index++) {
				if (_internal_data.cards[p_index][card_index] != CARD_NO_CARD &&
					_best_hand[p_index]->GetInternalSlot(_internal_data.cards[p_index][card_index]) != CARD_NO_CARD) {
					pocket_count++;
				}
			}
		#if 0	// set this to 1 to make bad beats happen a lot more often (testing only)
			kp1(("%s(%d) *** NOTE!!! BAD BEATS ARE INTENTIONALLY BROKEN FOR TESTING ***\n",_FL));
		#else
			if (pocket_count < 2) {	// doesn't qualify (this number will be 2 for HE/O8 and 5 for 7cs
				continue;
			}
		#endif
			// test against the defined hand
			int compare = _poker->FindBetterHand(*(_best_hand[p_index]), *bad_beat_hand);
			if (compare == HAND_1 || compare == HAND_TIE) {
				qualified[p_index] = TRUE;
				qualified_count++;
				if (_poker->FindBetterHand(*(_best_hand[p_index]), best_hand) == HAND_1) {
					best_hand = *(_best_hand[p_index]);
				}
			}
		}
	}
	if (qualified_count < 2) {
		return;	// nothing to do
	}
	// figure out who won and who lost 
	int winners = 0;
	int losers = 0;
	int participants = 0;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// change qualified to something useful
		if (qualified[p_index]) {
			if (_poker->FindBetterHand(*(_best_hand[p_index]), best_hand) == HAND_TIE) {
				qualified[p_index] = BAD_BEAT_WON_HAND;
				winners++;
			} else {
				qualified[p_index] = BAD_BEAT_LOST_HAND;
				losers++;:::
			}
		}
		// if he didn't qualify, he likely particiapted
		if (	(_internal_data.player_status[p_index] == PLAYER_STATUS_PLAYING ||
				_internal_data.player_status[p_index] == PLAYER_STATUS_FOLDED ||
				_internal_data.player_status[p_index] == PLAYER_STATUS_ALL_IN) 
				&&
			qualified[p_index] != BAD_BEAT_WON_HAND &&
			qualified[p_index] != BAD_BEAT_LOST_HAND) {
				qualified[p_index] = BAD_BEAT_PARTICIPATED;
				participants++;
		}
	}
	if (!losers) {	// high hands all tied for the win
		return;
	}
	// it's a bad beat
	/// !!! if MAX_GAME_RULES changes, this needs to change
	char *GameRuleNames[MAX_GAME_RULES] = { "Hold'em", "Omaha Hi", "Omaha H/L8", "7cs", "7cs-H/L8", "unknown" };

	if (GameRuleNames[CommonData->game_rules - GAME_RULES_START]) {
		kp(("%s(%d) ** BAD BEAT!  Hand #%d (%s - %d %s, %d %s, %d other %s)\n", 
			_FL, CommonData->game_serial_number,
			GameRuleNames[CommonData->game_rules - GAME_RULES_START],
			winners, (winners == 1 ? "winner" : "winners"),
			losers, (losers == 1 ? "loser" : "losers"),
			participants, (participants == 1 ? "participant" : "participants")
		  ));
	}	
	int winner_index = 0;
	for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		switch (qualified[p_index]) {
		case BAD_BEAT_WON_HAND:
			kp(("%s(%d) %s has %s (WON HAND #%d)\n", _FL, 
				CommonData->name[p_index],
				_szHandDescriptionHi[p_index],
				CommonData->game_serial_number));
			winner_index = p_index;
			break;
		case BAD_BEAT_LOST_HAND:
			kp(("%s(%d) %s has %s (LOST HAND #%d)\n", _FL, 
				CommonData->name[p_index],
				_szHandDescriptionHi[p_index],
				CommonData->game_serial_number));
			break;
		}
	}
	// We can queue the hand history request right from here as it's done via the PL object
	if (iRunningLiveFlag) {
		PL->QueueHandHistoryReq(HHRT_INDIVIDUAL_HAND, CommonData->game_serial_number,
			SDB->BadBeatRec_ID, SDB->BadBeatRec_ID, TRUE);
	}
	// are we paying it out...?
	if (bad_beat_active >= 2) {
		// 24/01/01 kriskoin:
		// UpdateTables() in the cardroom takes care of everything after we set the variables
		((Table *)_table_ptr)->bad_beat_game_number = CommonData->game_serial_number;
		((Table *)_table_ptr)->bad_beat_payout = bad_beat_jackpot;
		// this is where we do the payoffs
		int winner_share = 0, loser_share = 0, participant_share = 0;
		if (winners) {
			winner_share = (BadBeatHandWinnerCut * bad_beat_jackpot) / (winners * 100);
		}
		if (losers) {
			loser_share = (BadBeatHandLoserCut * bad_beat_jackpot) / (losers * 100);
		}
		if (participants) {
			participant_share = (BadBeatParticipantCut * bad_beat_jackpot) / (participants * 100);
		}
		// to avoid roundoff, we'll fix this so that the loser(s) get the extra pennies
		int left_to_award = bad_beat_jackpot;
		left_to_award -= (winners * winner_share);
		left_to_award -= (participants * participant_share);

		// 24/01/01 kriskoin:
		if (!participants) {
			int extra_share = (BadBeatParticipantCut * bad_beat_jackpot) / (100 * (winners+losers));
			winner_share += extra_share;
			loser_share += extra_share;
		}

		//kriskoin: 		// (among many other situations), a penny can fall through the cracks here.
		// This should be cleaned up at some point.
		//kriskoin: 		int remnant_amount = bad_beat_jackpot -
				((winners*winner_share) +
				(losers*loser_share) +
				(participants*participant_share));

		kp(("%s(%d) winner_share = %d, loser share = %d, participant share = %d, remnant = %d\n", 
				_FL, winner_share, loser_share, participant_share, remnant_amount));

		bad_beat_prizes.game_serial_number = CommonData->game_serial_number;
		bad_beat_prizes.winners = winners;
		bad_beat_prizes.winner_share = winner_share;
		bad_beat_prizes.losers = losers;
		bad_beat_prizes.loser_share = loser_share;
		bad_beat_prizes.participants = participants;
		bad_beat_prizes.participant_share = participant_share;
		bad_beat_prizes.total_prize = bad_beat_jackpot;
		bad_beat_prizes.remnant_amount = remnant_amount;

		// first log it to the BadBeat account
		ClientTransaction ct;
		zstruct(ct);
		ct.credit_left = 0;
		ct.timestamp = time(NULL);
		ct.transaction_amount = bad_beat_jackpot;
		ct.transaction_type = CTT_BAD_BEAT_PRIZE;
		ct.ecash_id = CommonData->game_serial_number;
		SDB->LogPlayerTransaction(SDB->BadBeatRec_ID, &ct);
		// now do each individual player's payout and logging
		for (p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
			switch (qualified[p_index]) {
			case BAD_BEAT_WON_HAND:
				if (winner_share) {
					ct.transaction_amount = winner_share;
					bad_beat_prizes.player_prizes[p_index] = ct.transaction_amount;
					bad_beat_prizes.player_prize_status[p_index] = 0;	// 0=winner, 1=loser, 2=participant
					SDB->TransferChips(SDB->BadBeatRec_ID, CommonData->player_id[p_index], winner_share);
					SDB->LogPlayerTransaction(CommonData->player_id[p_index], &ct);
				}
				break;
			case BAD_BEAT_LOST_HAND:
				if (loser_share) {
					ct.transaction_amount = loser_share;
					if (remnant_amount) {
						ct.transaction_amount += remnant_amount;
						remnant_amount = 0;	// never award this amount twice.
					}
					bad_beat_prizes.player_prizes[p_index] = ct.transaction_amount;
					bad_beat_prizes.player_prize_status[p_index] = 1;	// 0=winner, 1=loser, 2=participant
					SDB->TransferChips(SDB->BadBeatRec_ID, CommonData->player_id[p_index], loser_share);
					SDB->LogPlayerTransaction(CommonData->player_id[p_index], &ct);
				}
				break;
			case BAD_BEAT_PARTICIPATED:
				if (participant_share) {
					ct.transaction_amount = participant_share;
					bad_beat_prizes.player_prizes[p_index] = ct.transaction_amount;
					bad_beat_prizes.player_prize_status[p_index] = 2;	// 0=winner, 1=loser, 2=participant
					SDB->TransferChips(SDB->BadBeatRec_ID, CommonData->player_id[p_index], participant_share);
					SDB->LogPlayerTransaction(CommonData->player_id[p_index], &ct);
				}
				break;
			}
			pr(("%s(%d) bad_beat_prizes[%2d] = %7d (ptr = $%08lx)\n",
					_FL, p_index, bad_beat_prizes[p_index], &bad_beat_prizes[p_index]));
		}
	}
	SendDataNow(TRUE);
}

