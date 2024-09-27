/**********************************************************************************
 Member functions for Holdem/Omaha object
 
 :Date: 20180707 kriskoin : **********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <stdlib.h>
#include "pokersrv.h"
#include "holdem.h"
#include "omaha.h"
#include "logging.h"

extern PokerLog *PL;			// global access point for logging object

/**********************************************************************************
 Function Holdem::Holdem(int game_type);
 Date: 20180707 kriskoin :  Purpose: default constructor
 Note: Since this may be Omaha as well, we assign that now
***********************************************************************************/
Holdem::Holdem(void)
{
}

/**********************************************************************************
 Function Holdem::~Holdem()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Holdem::~Holdem(void)
{
}

/**********************************************************************************
 Function Holdem::InitializeGame
 Date: 20180707 kriskoin :  Purpose: start a game from scratch, and let us know the Game that's running it
***********************************************************************************/
void Holdem::InitializeGame(WORD32 serial_number, int max_number_of_players,
			int sb, int bb, RakeType rake_profile, void *table_ptr, 
			struct GameCommonData *gcd, ChipType chip_type)
{
	if (max_number_of_players > MAX_NUMBER_OF_HOLDEM_PLAYERS) {
		Error(ERR_FATAL_ERROR,"%s(%d) bad parameters fed to InitializeGame(%d (max %d), %d, %d)",
			_FL, max_number_of_players, MAX_NUMBER_OF_HOLDEM_PLAYERS, sb, bb);
		DIE("Too many players in InitializeGame");
	}
	_chip_type = chip_type;
	_game_rules = GAME_RULES_HOLDEM;	// default rules, may be overriden
	_max_this_game_players = max_number_of_players;	// max players allowed to be playing
	_table_ptr = table_ptr;
	_rake_profile = rake_profile;
	MoveButton = FALSE;	// will be set at the proper time (when BB is posted)
	CommonGameInit(serial_number, sb, bb, gcd);
}

/**********************************************************************************
 Function Holdem::SetGameRules(PokerGame game_rules)
 date: kriskoin 2019/01/01 Purpose: if it's a minor variation of the game, we may need to set the rules
***********************************************************************************/
void Holdem::SetGameRules(GameRules game_rules)
{
	_game_rules = game_rules;
	CommonData->game_rules = (BYTE8)game_rules;
	_split_pot_game = (	_game_rules == GAME_RULES_OMAHA_HI_LO );
	// in heads up, we allow an extra two bets per round
	if (_max_this_game_players == 2) {	// extra betting allowed in heads up
		_max_raises_to_cap += 2;	// allow it to go an extra two for heads up
	}
}

/**********************************************************************************
 Function Holdem::StartGame(void)
 Date: 20180707 kriskoin :  Purpose: start the game
***********************************************************************************/
void Holdem::StartGame(BYTE8 button)
{
	if (_player_count < 2) {
		Error(ERR_ERROR,"Tried to start a game with %d players -- we need at least 2",
			_player_count);
		_game_state = CANCEL_GAME;
		int work_was_done_flag = FALSE;
		int run_again = FALSE;
		// kick it off enough to have the game aborted
		do {
			run_again = EvaluateGame(&work_was_done_flag);
			work_was_done_flag |= run_again;
		} while (run_again);
	} else {
		SetButton(button);			// a call to SetButton()
		_initializing = FALSE;
		UpdateStructures();	// make sure they're all up to date
	}
}

/**********************************************************************************
 Function Holdem::SetButton(int player)
 Date: 20180707 kriskoin :  Purpose: tell us who the virtual dealer is -- action runs left off this player (internal)
***********************************************************************************/
void Holdem::SetButton(BYTE8 player)
{
	#define BUTTON_MESSAGES	0	// print out messages relating to button handing-off
	int loop_count = 0;
	while (_internal_data.player_status[player] != PLAYER_STATUS_PLAYING) {
	  #if BUTTON_MESSAGES	// :::		Error(ERR_NOTE,"(%s%d)game %d tried to give the button to player %d but he's not playing",
			_FL, CommonData->game_serial_number, player);
	  #endif
		// let's find someone else to give it to
		player = GetNextPlayerInTurn(player);
	  #if BUTTON_MESSAGES	// :::		Error(ERR_NOTE,"(%s%d)trying to give button to player %d instead", _FL, player);
	  #endif
		if (loop_count++ > MAX_PLAYERS_PER_GAME) {	// big trouble, no valid players
		  #if BUTTON_MESSAGES	// :::			Error(ERR_INTERNAL_ERROR,"(%s%d)wrapped trying to give button", _FL);
		  #endif
			_game_state = GAME_OVER;
			int unused_flag = FALSE;
			// set all flags indicating game is over -- should be handled from above
			EvaluateGame(&unused_flag);
			return;
		}
	}
	CommonData->p_button = player;
	_game_state = START_COLLECT_BLINDS;
	// this call to EvaluateGame is to kick things off... it's a bit of an exception
	// in that there's no call to HandleActions before it.. but nevertheless, it may
	// need to be called more than once, so we'll test it's return value and keep
	// calling it as much as it needs
	int work_was_done_flag = FALSE;
	int run_again = FALSE;	// might want to loop through a few times
	do {
		run_again = EvaluateGame(&work_was_done_flag);
		work_was_done_flag |= run_again;
	} while (run_again);
}

/**********************************************************************************
 Function Holdem::PlayOn(void)
 Date: 20180707 kriskoin :  Purpose: called externally to tell us to move on as much as we can
***********************************************************************************/
void Holdem::PlayOn(int *work_was_done_flag)
{
	if (_b_end_game_handled) {	// already finished the game
		return;	// nothing to do but wait for the destruction of this object
	}
	PostDealerMessageQueue();// clear before we do our next steps
	// first, check if we're waiting for something and deal with it
	// HandleActions returns TRUE if it's still waiting for something
	//kp(("%s(%d) PlayOn\n",_FL));
	if (!HandleActions()) {
		// HandleActions() didn't do anything -- no need to evaluate
		//kp(("%s(%d) Not calling EvaluateGame because HandleActions didn't do anything\n", _FL));
		return;
	}
	UpdateStructures();	// make sure they're all up to date
	// and continue evaluating
	int run_again = TRUE;	// might want to loop through a few times
	//kp(("%s(%d) PlayOn\n",_FL));
	int loop_count = 0;
	while (run_again && SecondCounter >= next_eval_time) {
		loop_count++;
		//kp(("%s(%d) PlayOn: Calling EvaluateGame()\n",_FL));
		run_again = EvaluateGame(work_was_done_flag);
		*work_was_done_flag |= run_again;
		UpdateStructures();	// make sure they're all up to date

		// If a delay before continuing was specified, set it up now...
		if (delay_before_next_eval) {
			next_eval_time = SecondCounter + delay_before_next_eval;
			delay_before_next_eval = 0;	// reset it.
		}
	}
	if (DebugFilterLevel <= 8) {
		if (loop_count >= 25) {	// 24/01/01 kriskoin:
			WORD32 game_serial_num = 0;
			if (CommonData) {
				game_serial_num = CommonData->game_serial_number;
			}
			kp(("%s %s(%d) WARNING: table loop_count was %d. game %d\n", TimeStr(), _FL, loop_count, game_serial_num));
		}
	}
}

/**********************************************************************************
 Function Holdem::HandleActions(void)
 Date: 20180707 kriskoin :  Purpose: handle any player actions that we're expecting
 Return:  TRUE if it handled something, FALSE if it didn't handle anything
***********************************************************************************/
int Holdem::HandleActions(void)
{
	if (_b_end_game_handled) {	// already finished the game
		return FALSE;	// nothing to do but wait for the destruction of this object
	}
	// let's do some checking on what we recieved -- and see if we want to handle it
	int handle_action = TRUE;	// assume it's ok
	int tmp_last_action_amt = 0;// may be used below on overrides

	// adate: If we're not waiting for anything... let the game continue.
	if (!_GPIResult.ready_to_process && !_waiting_for_user_action) {
		return TRUE;
	}

	int action = _GPIResult.action;
	forever {	// only loop once, but we might want to break out
		// we shouldn't be here unless we're expecting something
		if (_GPIResult.ready_to_process && !_waiting_for_user_action) {
			Error(ERR_INTERNAL_ERROR,"%s(%d) Call to HandleActions with a GPIResult not expected", _FL);
			handle_action = FALSE;
			break;
		}
		// check if there's anything there
		if (!_GPIResult.ready_to_process && _waiting_for_user_action) {
			// nothing there yet... we're waiting for something
			//pr(("Call to HandleActions but nothing in the GPIResult struct yet (waiting)\n"));
			handle_action = FALSE;
			break;
		}
		if (!_GPIResult.ready_to_process && !_waiting_for_user_action) {
			// nothing there yet... and we're not even waiting
			pr(("Call to HandleActions but nothing in the GPIResult struct yet (not waiting)\n"));
			handle_action = FALSE;
			break;
		}
		// there is something there -- validate it
		if (_GPIRequest.game_serial_number != _GPIResult.game_serial_number) {
			// we've recieved a reply to the WRONG GAME -- BADLY out of sync
			//kp(("%s(%d) _GPIResult.ready_to_process = %d, _wating_for_user_action = %d\n",_FL, _GPIResult.ready_to_process, _waiting_for_user_action));
		  #if 0	//24/01/01 kriskoin:
			Error(ERR_INTERNAL_ERROR,"Received a user response action for game %d while expecting %d",
				_GPIResult.game_serial_number, _GPIRequest.game_serial_number);
		  #endif
			handle_action = FALSE;
			break;
		}
		if (_GPIRequest.input_request_serial_number != _GPIResult.input_request_serial_number) {
			// we've recieved a reply to the WRONG ACTION -- out of sync
			//24/01/01 kriskoin:
			// are internet delays and this guy's turn comes around again
			// before a duplicate answer from the previous input request
			// arrived.  No need to print an error message any more.
		  #if 0	//19:::			Error(ERR_INTERNAL_ERROR,"Received a user response for serial #%d while expecting serial #%d",
				_GPIResult.input_request_serial_number, _GPIRequest.input_request_serial_number);
		  #endif
			handle_action = FALSE;
			break;
		}
		// right response -- is it a valid action?
		// adate: allow unexpected FORCE_ALL_IN action
		if (action != ACT_FORCE_ALL_IN && !GET_ACTION(_GPIRequest.action_mask, action)) {	// invalid response!?
			Error(ERR_ERROR,"%s(%d) Received action %d from player %d (%s) while expecting from bitmap %x (game #%d, serial #%d)",
				_FL, action, _GPIResult.seating_position,
				CommonData->name[_GPIResult.seating_position],
				_GPIRequest.action_mask,
				_GPIResult.game_serial_number,
				_GPIResult.input_request_serial_number);
			handle_action = FALSE;
			break;
		}
		break;	// all is ok
	}
	if (!handle_action) {	// something was wrong with it
		// we didn't handle anything -- leave the flag and wait for the next thing
		_GPIResult.ready_to_process = FALSE;	//24/01/01 kriskoin:
		return FALSE;	// FALSE means we didn't handle anything
	}

	// action is valid one to deal with
	BYTE8 player_index = _GPIResult.seating_position;

	// let everyone know what the last action performed for this player was
	_internal_data.last_action[player_index] = (BYTE8)action;
	switch (action) {

	/* PLAYER WAS FORCED ALL IN */
	case ACT_FORCE_ALL_IN:
		pr(("%s(%d) HandleAction ACT_FORCE_ALL_IN\n", _FL));
		// a player being forced all-in doesn't put any more $ in the pot at all...
		_internal_data.last_action_amount[player_index] = 0;
		_pot->ForceAllIn(player_index);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		_next_player_to_act = GetNextPlayerInTurn(player_index);
		SendDealerMessage(CHATTEXT_DEALER_NORMAL, "%s is being treated as all-in", CommonData->name[player_index]);
		break;
		
	/* POST / DON'T POST SMALL BLIND */
	case ACT_POST_SB:
		pr(("HandleAction ACT_POST_SB\n"));
		// we got someone to post the SB -- assign his index
		_internal_data.p_small_blind = player_index;
		_posted_blind[player_index] = TRUE;	// posted something this game?
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
		tmp_last_action_amt = CommonData->small_blind_amount;	// unless we override it, post the BB
		// 24/01/01 kriskoin:
		if (CommonData->chips[player_index] <= CommonData->small_blind_amount) {	// he'll be all-in on post
			// a change was also made where we don't POST ALL IN in this case, it's
			// always an ACT_POST_SB... because we're trapping the all-in state here
			tmp_last_action_amt = CommonData->chips[player_index];
			_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		}
		// all checks out; post the bb
		_internal_data.last_action_amount[player_index] = tmp_last_action_amt;
		// we tell the pot who is "first to act" because it needs to know
		_pot->SetFirstToAct(_internal_data.p_small_blind);
		_pot->PostBlind(player_index, tmp_last_action_amt);
		// we set the BB to this player as well, as the first call to COLLECT_BB
		// will properly increment this to the next player
		_internal_data.p_big_blind = _internal_data.p_small_blind;
		// all done, increment the game state
		_game_state = COLLECT_BB;
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts the small blind", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_SIT_OUT_SB:
		pr(("HandleAction ACT_SIT_OUT_SB\n"));
		// this player has chosen to sit out -- fold him out and mark him as not sitting out
		_internal_data.player_status[player_index] = PLAYER_STATUS_DID_NOT_POST_SB;
		// we need to notify that he's now owing a post
		// NOTE:  He missed SB -- but now we want a BB and dead post for him to get back in
		// 24/01/01 kriskoin:
	  #if 1
		CommonData->post_needed[player_index] = POST_NEEDED_INITIAL;
	  #else
		CommonData->post_needed[player_index] = POST_NEEDED_BOTH;
	  #endif
		_posted_blind[player_index] = FALSE;	// posted something this game?
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
		// we're still trying to collect the SB
		_game_state = COLLECT_SB;
		break;

	/* POST / DON'T POST BIG BLIND */
	case ACT_POST_BB:
		pr(("HandleAction ACT_POST_BB\n"));
		// we got someone to post the BB -- assign his index
		_internal_data.p_big_blind = player_index;
		_posted_blind[player_index] = TRUE;	// posted something this game?
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
		tmp_last_action_amt = CommonData->big_blind_amount;	// unless we override it, post the BB
		// 24/01/01 kriskoin:
		if (CommonData->chips[player_index] <= CommonData->big_blind_amount) {	// he'll be all-in on post
			// a change was also made near line 809 where don't POST ALL IN in this case, it's
			// always an ACT_POST_BB... because we're trapping the all-in state here
			tmp_last_action_amt = CommonData->chips[player_index];
			_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		}
		// all checks out; post the bb
		_internal_data.last_action_amount[player_index] = tmp_last_action_amt;
		// in heads up, things are flipped -- important for river
		if (_max_this_game_players == 2) {
			_pot->SetFirstToAct(_internal_data.p_big_blind);
		}
		_pot->PostBlind(player_index, tmp_last_action_amt);	// place the actual chips
		// all done, increment the game state
		_game_state = COLLECT_POSTS;
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts the big blind", CommonData->name[player_index]);
	  #endif
		// if the BB is posted, the button should move next game
		MoveButton = TRUE;
		break;

	case ACT_SIT_OUT_BB:
		pr(("HandleAction ACT_SIT_OUT_BB\n"));
		// this player has chosen to sit out -- fold him out and mark him as not sitting out
		_internal_data.player_status[player_index] = PLAYER_STATUS_DID_NOT_POST_BB;
		// we need to notify that he's now owing a post
		CommonData->post_needed[player_index] = POST_NEEDED_BOTH;
		_posted_blind[player_index] = FALSE;	// posted something this game?
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
		// we're still trying to collect the BB
		_game_state = COLLECT_BB;
		//SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s sits out this hand", CommonData->name[player_index]);
		break;

	/* POST / DON'T POST INITIAL POST */
	case ACT_POST:
		pr(("HandleAction ACT_POST\n"));
		// this player posted... which means a big blind amount
		_posted_blind[player_index] = TRUE;	// posted something this game?
		// post the bb as live
		_internal_data.last_action_amount[player_index] = CommonData->big_blind_amount;
		_pot->PostBlind(player_index, CommonData->big_blind_amount);
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_POST_ALL_IN:
		pr(("HandleAction ACT_POST_ALL_IN\n"));
		// this player posted all-in ... which means a big blind amount
		_posted_blind[player_index] = TRUE;	// posted something this game?
		// post the bb as live
		_internal_data.last_action_amount[player_index] = CommonData->big_blind_amount;
		_pot->PostBlind(player_index, CommonData->big_blind_amount);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts", CommonData->name[player_index]);
		break;

	case ACT_SIT_OUT_POST:
		pr(("HandleAction ACT_SIT_OUT_POST\n"));
		// this player has chosen to sit out for now -- fold him out
		_internal_data.player_status[player_index] = PLAYER_STATUS_DID_NOT_POST_INITIAL;
		// we need to notify that he's now owing a post
		CommonData->post_needed[player_index] = POST_NEEDED_INITIAL;
		_posted_blind[player_index] = FALSE;	// posted something this game?
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
		//SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s sits out this hand", CommonData->name[player_index]);
		break;

	/* POST / DON'T POST BOTH (BIG + SMALL DEAD) */
	case ACT_POST_BOTH:
		pr(("HandleAction ACT_POST_BOTH\n"));
		// this player just posted both blinds -- BB is live, SB is dead
		_posted_blind[player_index] = TRUE;	// posted something this game?
		_internal_data.last_action_amount[player_index] =
			(CommonData->big_blind_amount + CommonData->small_blind_amount);
		// post the bb as live
		_pot->PostBlind(player_index, CommonData->big_blind_amount);
		// post the sb as dead
		_pot->PostDead(player_index, CommonData->small_blind_amount);
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts bb & dead money", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_SIT_OUT_BOTH:
		pr(("HandleAction ACT_SIT_OUT_POST_BOTH\n"));
		// this player has chosen to sit out for now -- fold him out
		_internal_data.player_status[player_index] = PLAYER_STATUS_DID_NOT_POST_BOTH;
		// we need to notify that he's now owing a post
		CommonData->post_needed[player_index] = POST_NEEDED_BOTH;
		_posted_blind[player_index] = FALSE;	// posted something this game?
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
		//SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s sits out this hand", CommonData->name[player_index]);
		break;

	/* BETTING ROUND ACTIONS */
	case ACT_FOLD:
		pr(("HandleAction ACT_FOLD\n"));
		// if he folds, we mark him as such
		_internal_data.player_status[player_index] = PLAYER_STATUS_FOLDED;
		RemoveCards(player_index);
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
		if (_pot->GetPlayersLeft() == 1) {
			pr(("only one player left in the game\n"));
			// it's an early end to the betting round -- so close it off
			_pot->CloseBettingRound();
			_game_state = END_GAME_START;

		} else {	// only look for next player if there IS another player
			_next_player_to_act = GetNextPlayerInTurn(player_index);
		}
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s folds", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_CALL:
		pr(("HandleAction ACT_CALL\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetCallAmount(player_index);
		_pot->Call(player_index);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s calls", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_BET:
		pr(("HandleAction ACT_BET\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetRaiseAmount(player_index);
		_pot->Bet(player_index);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s bets", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_CHECK:
		pr(("HandleAction ACT_CHECK\n"));
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Check(player_index);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s checks", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_RAISE:
		pr(("HandleAction ACT_RAISE\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetRaiseAmount(player_index);
		_pot->Raise(player_index);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s raises", CommonData->name[player_index]);
	  #endif
		break;

	// in case of ALL_IN bets, it may be possible for there not to be a next player in turn
	// this happens
	case ACT_CALL_ALL_IN:
		pr(("HandleAction ACT_CALL_ALL_IN\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetCallAmount(player_index);
		_pot->Call(player_index);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s calls all-in", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_BET_ALL_IN:
		pr(("HandleAction ACT_BET_ALL_IN\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetRaiseAmount(player_index);
		_pot->Bet(player_index);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s bets all-in", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_RAISE_ALL_IN:
		pr(("HandleAction ACT_RAISE_ALL_IN\n"));
		_internal_data.last_action_amount[player_index] = _pot->GetRaiseAmount(player_index);
		_pot->Raise(player_index);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s raises all-in", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_SHOW_HAND:
		pr(("HandleAction ACT_SHOW_HAND\n"));
		ShowHandForPlayer(player_index);
		_internal_data.last_action_amount[player_index] = 0;
		break;

	case ACT_MUCK_HAND:
		pr(("HandleAction ACT_MUCK_HAND\n"));
		MuckHandForPlayer(player_index);
		_internal_data.last_action_amount[player_index] = 0;
		break;

	case ACT_TOSS_HAND:
		pr(("HandleAction ACT_TOSS_HAND\n"));
		_show_hand[player_index] = SHOW_HAND_TOSS;
		// adate: only toss his hand if there are no more sidepots to deal with as 
		// there may be pots below the current one that he will have to show his cards for
		if (!_pot_number) {	// we're at the bottom (or only) pot
			RemoveCards(player_index);
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s doesn't show %s hand",
				CommonData->name[player_index], GenderPronoun(player_index));
		}
		_internal_data.last_action_amount[player_index] = 0;
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) Unhandled action %d in HandleActions", _FL, action);
		DIE("trying to deal with an unhandled action");

	}
	// we handled something -- clear the flag
	_waiting_for_user_action = FALSE;
	// log the action
	PL->LogGameAction(CommonData->game_serial_number,_GPIResult.input_request_serial_number,
		player_index, action, _internal_data.last_action_amount[player_index],_pot->GetRake());
	return TRUE;	// TRUE means we handled something
}
/**********************************************************************************
 Function Holdem::EvaluateGame(void)
 Date: 20180707 kriskoin :  Purpose: this is the main processing of the game... it figures out what state we're
          in, what needs to be evaluated... and it goes forward as far as it can --
		  till it needs something... and asks for it and returns.  the next call to
		  PlayOn() will wind up back here, hopefully in an advanced state
 Returns: TRUE/FALSE whether it should be called again
***********************************************************************************/
int Holdem::EvaluateGame(int *work_was_done_flag)
{
	if (_b_end_game_handled) {	// already finished the game
		return FALSE;	// nothing to do but wait for the destruction of this object
	}
	BYTE8 p_index;	// used below
	int evaluate_again = FALSE;	// don't repeat by default
	int loop_count = 0;				// used to avoid infinite loop
	Card card;	// used when dealing

	switch (_game_state) {
	/* COLLECT BLINDS */
	case START_COLLECT_BLINDS:
		// for this gametype, tell the pot how far a betting round can go
		_pot->SetMaxRaisesToCap(_max_raises_to_cap);	
		pr(("EvalGame got START_COLLECT_BLINDS\n"));
		// in HEADS UP, there's no betting cap 
		// adate: we've decided for now to cap heads-up -- like all other games
	  #if 0
		if (_game_rules == GAME_HEADS_UP_HOLDEM) {
			_pot->PotHasCap(FALSE);
		}
	  #endif
		// SendDealerMessage(CHATTEXT_DEALER_BLAB, "�.�`�`�.��.�`�`�.��.�`�`�.��.�`�`�.��.�`�`�.�");
		// adate: set to BLAB from NORMAL
		SendDealerMessage(CHATTEXT_DEALER_BLAB,"Starting a new hand...");
		_pot->NewBettingRound(_low_limit, _low_limit*_max_raises_to_cap);
		// the following line initializes the search point for where the small blind
		// will come from.  the first call to COLLECT_SB moves one away from where it is,
		// so here we'll set it to the button... and the first player asked for the SB will
		// be, as it should, the player after the button
		_internal_data.p_small_blind = CommonData->p_button;
		evaluate_again = TRUE;		// we'll loop to start blinds collection
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = COLLECT_SB;
		break;

	case COLLECT_SB:	// we need to collect the small blind from some player
		pr(("EvalGame got COLLECT_SB\n"));
		// whoever set up this call (either START_COLLECT_BLINDS or a pervious COLLECT_SB)
		// will have set _internal_data.p_small_blind properly.  either it was the button, and
		// we're advancing past it, or it was a "failed" call to COLLECT_SB (player chose to
		// sit out, so now we're looking to the next player to collect
		_internal_data.p_small_blind = GetNextPlayerInTurn(_internal_data.p_small_blind, FALSE);
		// 24/01/01 kriskoin:
		if (_max_this_game_players == 2) {
			_internal_data.p_small_blind = CommonData->p_button;
		}
		// 24/01/01 kriskoin:
		if (_tournament_game && _pot->GetPlayersLeft() == 2) {
			_internal_data.p_small_blind = CommonData->p_button;
		}
		// we'll check all seated players -- even those sitting out -- because we need to
		// know if they sat out and missed a blind
		if (SittingOut(_internal_data.p_small_blind)) {	// he is sitting out
			// if it's heads up, we have to cancel
			if (_max_this_game_players == 2) {
				_game_state = CANCEL_GAME;	// advance to next state -- deal pockets, as no BB either
				SendDealerMessage(CHATTEXT_DEALER_NORMAL, "SB chose to sit out -- game is cancelled");
				evaluate_again = TRUE;
				SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
				break;
			} else {	// normal game
				// we need to notify that he's now owing a post
				_internal_data.player_status[_internal_data.p_small_blind] = PLAYER_STATUS_DID_NOT_POST_SB;
			  #if 1	// 24/01/01 kriskoin:
				if (CommonData->post_needed[_internal_data.p_small_blind] == POST_NEEDED_NONE) {
					CommonData->post_needed[_internal_data.p_small_blind] = POST_NEEDED_INITIAL;
				}
			  #else
				CommonData->post_needed[_internal_data.p_small_blind] = POST_NEEDED_BOTH;
			  #endif
				evaluate_again = TRUE;		// we'll loop around to the next player
				SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
				break;
			}
		}
		// if the request gets back to the button, we're in a wierd situation -- likely, we might
		// not want to play the hand as there aren't enough players... or, we can continue without
		// collecting any more blinds  -- decided we cancel the game
		// 24/01/01 kriskoin:
		if ((_internal_data.p_small_blind == CommonData->p_button) && _max_this_game_players != 2 && !_tournament_game) {
			// pr(("small_blind = button -- wrapped, so we can't play -- cancel\n"));
			_game_state = CANCEL_GAME;	// advance to next state -- deal pockets, as no BB either
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Game is cancelled -- not enough players");
			evaluate_again = TRUE;
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		// similar, but backwards, for one-on-one
		if ((_internal_data.p_small_blind != CommonData->p_button) && _max_this_game_players == 2) {
			// wrapped, so we can't play -- cancel\n"));
			_game_state = CANCEL_GAME;	// advance to next state -- deal pockets, as no BB either
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Game is cancelled");
			evaluate_again = TRUE;
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}

		if (_pot->GetPlayersLeft() > 2 && CommonData->post_needed[_internal_data.p_small_blind] != POST_NEEDED_NONE) {	// player can't play
			_internal_data.player_status[_internal_data.p_small_blind] = PLAYER_STATUS_SITTING_OUT;
			// we need to notify that he's now owing a post - BOTH, as has been discussed
			// 990722HK we don't want a player who's never played a hand to have to post dead 
			// money -- we won't let him in, but he'll only have to post initial amount, no
			// dead amount, when he finally gets to play
			if (CommonData->post_needed[_internal_data.p_small_blind] != POST_NEEDED_INITIAL) {
				CommonData->post_needed[_internal_data.p_small_blind] = POST_NEEDED_BOTH;
			}
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "%s will be allowed to play after the button", CommonData->name[_internal_data.p_small_blind]);
			_posted_blind[_internal_data.p_small_blind] = FALSE;	// posted something this game?
			_pot->Fold(_internal_data.p_small_blind);
			// proper small blind will be assigned on next go around
			evaluate_again = TRUE;		// we'll loop to keep looking for SB
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		// we have the player who's the small blind... ask him to post -- if he hasn't already posted something
		// if he's already posted, for whatever reason, assign him as SB but don't ask for more $$
		if (_posted_blind[_internal_data.p_small_blind]) {	// already posted something
			_game_state = COLLECT_BB;	// advance to next state -- collect BB
			// we set the BB to this player as well, as the first call to COLLECT_BB
			// will properly increment this to the next player
			_internal_data.p_big_blind = _internal_data.p_small_blind;
			evaluate_again = TRUE;		// we'll loop to start collecting BB
		} else {	// hasn't posted -- ask him for it
			RequestActionFromPlayer(_internal_data.p_small_blind);	// first, prepare to ask for actions from this player
		  #if 0	// 24/01/01 kriskoin:
			//if (CommonData->chips[_internal_data.p_small_blind] == CommonData->small_blind_amount) {
			//	SetValidAction(ACT_POST_ALL_IN);
			//} else {
			//	SetValidAction(ACT_POST_SB);
			//}
		  #else
			SetValidAction(ACT_POST_SB);
		  #endif
			SetValidAction(ACT_SIT_OUT_SB);
			// 24/01/01 kriskoin:
			_GPIRequest.call_amount = min(CommonData->small_blind_amount, CommonData->chips[_internal_data.p_small_blind]);
			PostAction();
		}
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case COLLECT_BB:	// we need to collect the big blind from some player
		pr(("EvalGame got COLLECT_BB\n"));
		// let's find the next player over -- the big blind
		_internal_data.p_big_blind = GetNextPlayerInTurn(_internal_data.p_big_blind, FALSE);
		// we'll check all seated players -- even those sitting out -- because
		// we need to know if they sat out and missed a blind
		if (SittingOut(_internal_data.p_big_blind)) {	// he is sitting out
			// we need to notify that he's now owing a post
			_internal_data.player_status[_internal_data.p_big_blind] = PLAYER_STATUS_DID_NOT_POST_BB;
			CommonData->post_needed[_internal_data.p_big_blind] = POST_NEEDED_BOTH;
			evaluate_again = TRUE;		// we'll loop around to the next player
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		if (_internal_data.p_big_blind == _internal_data.p_small_blind) {	// not enough players
			// Error(ERR_NOTE,"big_blind = small_blind --> not enough players --> cancel game");
			_game_state = CANCEL_GAME;
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Game is cancelled -- not enough players");
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Refunding small blind");
			evaluate_again = TRUE;		// we'll loop to continue to next step
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		// we have the player who's the big blind... ask him to post
		// if he's already posted, for whatever reason, assign him as BB but don't as for more $$
		if (_posted_blind[_internal_data.p_big_blind]) {	// already posted
			_game_state = COLLECT_POSTS;// advance to next state -- collecting posts due
			evaluate_again = TRUE;		// we'll loop to continue to next step
		} else {	// hasn't posted -- ask him for it
			RequestActionFromPlayer(_internal_data.p_big_blind);
		  #if 0	// 24/01/01 kriskoin:
				// status in the ACT_POST_BB, so we just need to set the two options here
			//if (CommonData->chips[_internal_data.p_big_blind] == CommonData->big_blind_amount) {
			//	SetValidAction(ACT_POST_ALL_IN);
			//} else {
			//	SetValidAction(ACT_POST_BB);
			//}
		  #else
			SetValidAction(ACT_POST_BB);
		  #endif
			SetValidAction(ACT_SIT_OUT_BB);
			// 24/01/01 kriskoin:
			_GPIRequest.call_amount = min(CommonData->big_blind_amount, CommonData->chips[_internal_data.p_big_blind]);
			PostAction();
		}
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case COLLECT_POSTS:	// we need to collect posts from players that owe
		pr(("EvalGame got COLLECT_POSTS\n"));
		// we need to run through the list, looking for live players... and asking them
		// to post if they're flagged as owing.  once we get here and there are no pending
		// players required to act on whether to post or not, we can advance the game state
		// to dealing pocket cards
		p_index = CommonData->p_button;	// start at the button
		#define NOBODY_NEEDS_TO_POST	99	// some non-out-of-range invalid index number
		_next_player_to_act = (BYTE8)NOBODY_NEEDS_TO_POST;	// flag as nobody for now
		do {
			loop_count++;
			if (loop_count > MAX_PLAYERS_PER_GAME) {	// that's enough
				pr(("%s(%d) Avoiding infinite loop\n", _FL));
				break;

			}
			p_index = GetNextPlayerInTurn(p_index);	// advance player
			//pr(("$$$ player=%d  cd.pn=%d  pb=%d  so=%d\n",p_index,
			//CommonData->post_needed[p_index], _posted_blind[p_index], SittingOut(p_index)));
			if (CommonData->post_needed[p_index] && !_posted_blind[p_index] && !SittingOut(p_index) 
				// 990906: added test to see if they even have enough for the minimum
				&& CommonData->chips[p_index] >= CommonData->big_blind_amount) {
				// player fits criteria for a player we need a post from
				_next_player_to_act = p_index;
				break;
			}
		} while (p_index != CommonData->p_button);
		if (_next_player_to_act != NOBODY_NEEDS_TO_POST) {	// found someone that needs to post
			pr(("%d needs to post type %d\n", _next_player_to_act, CommonData->post_needed[p_index]));
			RequestActionFromPlayer(_next_player_to_act);	// first, prepare to ask
			// could be required to post one of a few things :
			// POST_NEEDED_NONE, POST_NEEDED_BB, POST_NEEDED_SB, POST_NEEDED_BOTH
			switch (CommonData->post_needed[p_index]) {
				case POST_NEEDED_INITIAL:
					if (CommonData->chips[p_index] == CommonData->big_blind_amount) {
						SetValidAction(ACT_POST_ALL_IN);
					} else {
						SetValidAction(ACT_POST);
					}
					SetValidAction(ACT_SIT_OUT_POST);
					break;
				case POST_NEEDED_BB:
				  #if 0	// 24/01/01 kriskoin:
						// status in the ACT_POST_BB, so we just need to set the two options here
					if (CommonData->chips[p_index] == CommonData->big_blind_amount) {
						SetValidAction(ACT_POST_ALL_IN);
					} else {
						SetValidAction(ACT_POST_BB);
					}
				  #else
					SetValidAction(ACT_POST_BB);
				  #endif
					SetValidAction(ACT_SIT_OUT_BB);
					break;
				case POST_NEEDED_SB:
					if (CommonData->chips[p_index] == CommonData->small_blind_amount) {
						SetValidAction(ACT_POST_ALL_IN);
					} else {
						SetValidAction(ACT_POST_SB);
					}
					SetValidAction(ACT_SIT_OUT_SB);
					break;
				case POST_NEEDED_BOTH:
					// adate: we're going to forgive him the dead amount if he's down to less than
					// bb+sb -- just ask for bb
					if (CommonData->chips[p_index] > 
							(CommonData->big_blind_amount + CommonData->small_blind_amount) ) {
						SetValidAction(ACT_POST_BOTH);
					// is it between the two? if so, ask for BB
					} else if (CommonData->chips[p_index] > CommonData->big_blind_amount) {
					  #if 1	// 24/01/01 kriskoin:
						SetValidAction(ACT_POST);
					  #else
						SetValidAction(ACT_POST_BB);
					  #endif
					} else { 
						SetValidAction(ACT_POST_ALL_IN);
					}
					SetValidAction(ACT_SIT_OUT_BOTH);
					break;
				case POST_NEEDED_UNKNOWN:
					// this should have been caught and set by the table function above us
					// if it wasn't, it implies an error in the setup to AddPlayer at the
					// table level -- it shouldn't ever feed us back "UNKNOWN"
					Error(ERR_INTERNAL_ERROR,"%s(%d) SERIOUS ERROR: post_needed_unknown (see src)", _FL);
					break;
				default:
					Error(ERR_INTERNAL_ERROR,"%s(%d) SERIOUS ERROR: unknown post_needed type (%d)",
						_FL, CommonData->post_needed[p_index]);
			}
			PostAction();
		} else {	// we've looked through, no one left to post -- advance state
			evaluate_again = TRUE;	// we'll loop to start the dealing of pockets
			_game_state = DEAL_POCKETS;
		}
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	/* DEAL CARDS */
	case DEAL_POCKETS:
		// We got here, but let's do a sanity check and make sure there are
		// enough live players to continue -- if there aren't, cancel the game
		if (_pot->GetPlayersLeft() < 2) {	// not enough posts to kick off the game
			_game_state = CANCEL_GAME;	// advance to cancelling game
			SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Game is cancelled -- not enough players");
			evaluate_again = TRUE;		// we'll loop to start collecting BB
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING POCKETS **");
		pr(("EvalGame got DEAL_POCKETS\n"));
		// let's deal pocket cards to all playing players, starting left of the button
		_deck->CreateDeck();
		_deck->ShuffleDeck();
		// deal the pockets
		_cards_dealt = 0;	// reset
		// 000115HK the second TRUE are telling us player can be all in but still get cards
		DealEveryoneOneCard(TRUE, TRUE);	// first card....
		DealEveryoneOneCard(TRUE, TRUE);	// second card...
		if (_game_rules == GAME_RULES_OMAHA_HI || _game_rules == GAME_RULES_OMAHA_HI_LO) {	// in Omaha, everyone gets 4 cards
			DealEveryoneOneCard(TRUE, TRUE);	// third card....
			DealEveryoneOneCard(TRUE, TRUE);	// fourth card...
		}

		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_1;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_pocket = (BYTE8)(_pot->GetPlayersLeft());
		LogPlayerStayedThisFar(DEAL_POCKETS);	// keep track for all players
		break;

	case DEAL_FLOP:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING FLOP **");
		pr(("EvalGame got DEAL_FLOP\n"));
		// deal three cards to the flop
		{
			_cards_dealt = 0;	// reset
			int plr_card_pairs[6];
			plr_card_pairs[0] = plr_card_pairs[2] = plr_card_pairs[4] = -1;
			// 1
			plr_card_pairs[1] = card = _deck->DealNextCard();
			_flop->Add(card);
			// 2
			plr_card_pairs[3] = card = _deck->DealNextCard();
			_flop->Add(card);
			// 3
			plr_card_pairs[5] = card = _deck->DealNextCard();
			_flop->Add(card);
			PL->LogGameCardsDealt(CommonData->game_serial_number,3,plr_card_pairs);
		}

		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_2;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_flop = (BYTE8)(_pot->GetPlayersLeft());
		LogPlayerStayedThisFar(DEAL_FLOP);	// keep track for all players
		break;

	case DEAL_TURN:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING TURN **");
		pr(("EvalGame got DEAL_TURN\n"));
		// deal next card to the flop
		_cards_dealt = 0;	// reset
		card = _deck->DealNextCard();
		_flop->Add(card);
		PL->LogGameCardDealt(CommonData->game_serial_number,-1,card);
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_3;
		break;

	case DEAL_RIVER:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING RIVER **");
		pr(("EvalGame got DEAL_RIVER\n"));
		// deal last card to the flop
		_cards_dealt = 0;	// reset
		card = _deck->DealNextCard();
		_flop->Add(card);
		PL->LogGameCardDealt(CommonData->game_serial_number,-1,card);
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_4;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_river = (BYTE8)(_pot->GetPlayersLeft());
		_dealt_river = TRUE;
		LogPlayerStayedThisFar(DEAL_RIVER);	// keep track for all players
		break;

	/* INITIALIZE BETTING ROUNDS */
	case START_BETTING_ROUND_1:	// first call to start the loop of the first betting round

	        int PokerData;
 
		pr(("EvalGame got START_BETTING_ROUND_1\n"));
		_internal_data.s_gameflow = GAMEFLOW_DURING_GAME;
		// set the maximum rake based on the number of players
		_pot->SetRakeablePlayers(_pot->GetPlayersLeft());
		// first time in, make sure we start in the right place (player after big blind)
		// works very well for one-on-one too -- goes right to the sb (button) where it should be
		_next_player_to_act = GetNextPlayerInTurn(_internal_data.p_big_blind);
		_pot->SetMaxRake(0);	// no rake till the flop
		// 24/01/01 kriskoin:
		// the blinds), the minimum call amount is always the bet_increment, no matter what... so 
		// at levels of 100/200, even if EVERYONE has less, they're all in... the betting increment
		// is fixed.  This doesn't apply to post-flop betting rounds.  This variable is set from holdem.cpp
		if (_tournament_game) {
			_pot->SetMinimumEnforcedBettingIncrement(CommonData->big_blind_amount);
		}
		_game_state = BETTING_ROUND_1;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_2:	// first call to start the loop of the 2nd betting round
	
		pr(("EvalGame got START_BETTING_ROUND_2\n"));
		_pot->NewBettingRound(_low_limit, _low_limit*_max_raises_to_cap);	// still low limit
		// first time in, make sure we start in the right place (player after button)
		_next_player_to_act = GetNextPlayerInTurn(CommonData->p_button);
		_game_state = BETTING_ROUND_2;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_3:	// first call to start the loop of the 3rd betting round
		pr(("EvalGame got START_BETTING_ROUND_3\n"));
		_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// now we're high limit
		// first time in, make sure we start in the right place (player after button)
		_next_player_to_act = GetNextPlayerInTurn(CommonData->p_button);
		_game_state = BETTING_ROUND_3;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_4:	// first call to start the loop of the last betting round
		pr(("EvalGame got START_BETTING_ROUND_4\n"));
		_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// we're at high limit
		// first time in, make sure we start in the right place (player after button)
		_next_player_to_act = GetNextPlayerInTurn(CommonData->p_button);
		_game_state = BETTING_ROUND_4;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	/* BETTING ROUNDS */
	case BETTING_ROUND_1:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_1\n"));
		// the betting round ends when it's come all the way around to the
		// instigator, and it hasn't been raised to him -- exception is big blind
		// first time around
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 1 is finished\n"));
			// now we can specify the max rake
			// 19991203HK - no rake till the flop
			if (_chip_type != CT_REAL) {	// only rake real money games
				_pot->SetMaxRake(0);// no rake for play money
			} else {
				_pot->SetMaxRake();	// pot internally sets its max rake
			}
			// rake is set,close off the betting round
			_pot->CloseBettingRound();	// do any end of round processing
			evaluate_again = TRUE;	// we'll loop to deal the flop
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_FLOP;
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	//20:::			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_tournament_game && _pot->GetActingPlayersLeft() == 2) {
		  #if 0	// 20:::			_pot->PotHasCap(FALSE);
		  #else
			_pot->SetMaxRaisesToCap(_max_raises_to_cap+2);	// this will make it 6
			_pot->SetCap(_low_limit*(_max_raises_to_cap+2));
		  #endif
		} else {
			if (_pot->PotWasJustCapped()) {
				SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
			}
		}
		p_index = _next_player_to_act;	// the current player
		RequestNextMoveFromPlayer(p_index);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case BETTING_ROUND_2:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_2\n"));
		// the betting round ends when it's come all the way around to the
		// instigator, and it hasn't been raised to him
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 2 is finished\n"));
			_pot->CloseBettingRound();	// do any end of round processing
			evaluate_again = TRUE;	// we'll loop to deal the turn
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_TURN;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	//20:::			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_tournament_game && _pot->GetActingPlayersLeft() == 2) {
		  #if 0	// 20:::			_pot->PotHasCap(FALSE);
		  #else
			_pot->SetMaxRaisesToCap(_max_raises_to_cap+2);	// this will make it 6
			_pot->SetCap(_low_limit*(_max_raises_to_cap+2));
		  #endif
		} else {
			if (_pot->PotWasJustCapped()) {
				SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
			}
		}
		p_index = _next_player_to_act;	// the current player
		RequestNextMoveFromPlayer(p_index);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case BETTING_ROUND_3:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_3\n"));
		// the betting round ends when it's come all the way around to the
		// instigator, and it hasn't been raised to him
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 3 is finished\n"));
			_pot->CloseBettingRound();	// do any end of round processing
			evaluate_again = TRUE;	// we'll loop to deal the river
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_RIVER;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	//20:::			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_tournament_game && _pot->GetActingPlayersLeft() == 2) {
		  #if 0	// 20:::			_pot->PotHasCap(FALSE);
		  #else
			_pot->SetMaxRaisesToCap(_max_raises_to_cap+2);	// this will make it 6
			_pot->SetCap(_high_limit*(_max_raises_to_cap+2));
		  #endif
		} else {
			if (_pot->PotWasJustCapped()) {
				SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
			}
		}
		p_index = _next_player_to_act;	// the current player
		RequestNextMoveFromPlayer(p_index);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case BETTING_ROUND_4:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_4\n"));
		// the betting round ends when it's come all the way around to the
		// instigator, and it hasn't been raised to him
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 4 is finished\n"));
			//SendDataNow();	// update all clients
			_pot->CloseBettingRound();	// do any end of round processing
			evaluate_again = TRUE;		// we'll loop to deal with the endgame
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = END_GAME_START;	// advance to next state
			break;
		}
		if (_tournament_game && _pot->GetActingPlayersLeft() == 2) {
		  #if 0	// 20:::			_pot->PotHasCap(FALSE);
		  #else
			_pot->SetMaxRaisesToCap(_max_raises_to_cap+2);	// this will make it 6
			_pot->SetCap(_high_limit*(_max_raises_to_cap+2));
		  #endif
		} else {
			if (_pot->PotWasJustCapped()) {
				SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
			}
		}
		p_index = _next_player_to_act;	// the current player
		RequestNextMoveFromPlayer(p_index);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	/* END OF GAME PROCESSING STARTS HERE */
	case END_GAME_START:
		// game is over because we got to the end, or everyone (but one) folded
		pr(("We are at END_GAME_START\n"));
		_internal_data.s_gameflow = GAMEFLOW_AFTER_GAME;
		_pot->ClearChipsBetThisRound();
		EvaluateLiveHands();
		evaluate_again = TRUE;		// we'll loop to deal with the pot distribution now
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = POT_DISTRIBUTION;	// advance to next state
		break;

	case POT_DISTRIBUTION:
		pr(("We are at POT_DISTRIBUTION\n"));
		// handle pot distribution (and card showing, which goes in the same order)
		// if there is no pot in current distribution, build the next eligible one
		if (!_pot_is_being_distributed) {	// nothing yet, build it
			_pot_number--;	// started off with highest potential side pot
			if (_pot_number < 0) {	// we're done
				_game_state = GAME_OVER;
			} else if (_pot->GetPot(_pot_number)) {	// there's a pot there; deal with it
				// build the list of players playing for this pot
				_pot_is_being_distributed = TRUE;
				BuildPotentialWinnersList(_pot_number);
			}
			evaluate_again = TRUE;		// we'll loop to deal with the new pot -- or end
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		} else {	// we're in the middle of distributing a pot
			// look through the list to see who, if anyone, needs to act
			if (AnyPlayerLeftToAskAboutShowingHand()) {	// we need to ask at least one player
				// ask a player to show cards -- we start at the last player to raise
				// if a player is marked to force a show of his hand, this will take care of it
				// the return value of this function tells us whether we need to loop again or
				// not.  if we are waiting for a response from a player, we don't loop...
				evaluate_again = RequestOrShowHandFromPlayer();
			} else {
				// no one left to ask/show cards -- let's distribute the pot
				DistributePot();
				_pot_is_being_distributed = FALSE;
				// kriskoin 
				SaveAndLogHiHand();
				// end kriskoin 
				AnnounceWinners(_pot_number, TRUE);	// T for first pass
				AnnounceWinners(_pot_number, FALSE);// F for second pass
				evaluate_again = TRUE;		// we'll loop to deal with another pot
			}
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		}
		break;

	case CANCEL_GAME:
		_game_was_cancelled = TRUE;
		// no break -- fall through to GAME_OVER
	case GAME_OVER:
		// nothing left to do
		if (_game_was_cancelled) {
			RefundChips();
		} else {
			UpdateDataBaseChipCounts();
		}
		CheckForBadBeat();
		// this is where we let the table know whether to move the button or not
		// 24/01/01 kriskoin:
		// the button shouldn't move for the next game
		if (_tournament_game && MoveButton) {
			int sb_final_chips = CommonData->chips[_internal_data.p_small_blind] +
								_internal_data.chips_won[_internal_data.p_small_blind] - 
								_internal_data.chips_bet_total[_internal_data.p_small_blind];
			if (!sb_final_chips) {
				pr(("%s(%d) Button not moving because the sb went all in (tournament game)\n", _FL));
				MoveButton = FALSE;
			}
		}

		_internal_data.s_gameover = (BYTE8)(MoveButton ? GAMEOVER_MOVEBUTTON : GAMEOVER_DONTMOVEBUTTON);
		_b_end_game_handled = TRUE;
		UpdateStructures();			// make sure they're all up to date for the very end

	  #if 0	//20:::		if (!iRunningLiveFlag) {
			kp1(("%s(%d) **** TESTING: testing chips_bet_total\n", _FL));
			for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
				if (_internal_data.chips_bet_total[i]) {
					kp(("%s %s(%d) Player %2d still has %5d chips_bet_total at end of game.\n",
							TimeStr(), _FL, i, _internal_data.chips_bet_total[i]));
				}
			}
		}
	  #endif

		PostDealerMessageQueue();	// last minute comments
		LogEndGame();
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) Unhandled _game_state (%d) in game %d",
			_FL, _game_state, CommonData->game_serial_number);
		DIE("EvaluateGame got something it didn't understand");
	}
  #if 0	//19:::	if (evaluate_again) {
		kp(("%s(%d) game #%d player_id[0] = $%08lx, [1]=$%08lx, [2]=$%08lx\n",
				_FL, CommonData->game_serial_number, CommonData->player_id[0], CommonData->player_id[1], CommonData->player_id[2]));
	}
  #endif
	return evaluate_again;
}
