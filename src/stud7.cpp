/**********************************************************************************
 Member functions for Stud7 object
 Date: 20180707 kriskoin : **********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <stdlib.h>
#include "pokersrv.h"
#include "stud7.h"
#include "logging.h"

extern PokerLog *PL;			// global access point for logging object

/**********************************************************************************
 Function Stud7::Stud7();
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Stud7::Stud7(void)
{
	_bring_in_player = -1;	// cause an error if this is set incorrectly
}

/**********************************************************************************
 Function Stud7::~Stud7()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Stud7::~Stud7(void)
{
}

/**********************************************************************************
 Function Stud7::InitializeGame
 Date: 20180707 kriskoin :  Purpose: start a game from scratch, and let us know the Game that's running it
***********************************************************************************/
void Stud7::InitializeGame(WORD32 serial_number, int max_number_of_players, 
			int sb, int bb, RakeType rake_profile, void *table_ptr, struct GameCommonData *gcd, ChipType chip_type)
{
	if (max_number_of_players > MAX_NUMBER_OF_STUD7_PLAYERS) {
		Error(ERR_FATAL_ERROR,"%s(%d) bad parameters fed to InitializeGame(%d (max %d), %d, %d)", 
			_FL, max_number_of_players, MAX_NUMBER_OF_STUD7_PLAYERS, sb, bb);
		DIE("Too many players in InitializeGame");
	}	
	_chip_type = chip_type;
	_game_rules = GAME_RULES_STUD7;								// rule set to use
	_max_this_game_players = max_number_of_players;			// max players allowed to be playing
	_table_ptr = table_ptr;
	_rake_profile = rake_profile;
	_river_7cs_dealt_to_everyone = FALSE;
	CommonGameInit(serial_number, sb, bb, gcd);
}

/**********************************************************************************
 Function Stud7::StartGame(void)
 Date: 20180707 kriskoin :  Purpose: start the game
***********************************************************************************/
void Stud7::StartGame(void)
{
	if (_player_count < 2) {	// we don't handle this yet
		Error(ERR_ERROR,"(aborting) tried to start a game with %d players -- we need 2 at least",
			_player_count);
		_game_state = CANCEL_GAME;
	} else {
		_initializing = FALSE;
		for (int index = 0; index < MAX_PLAYERS_PER_GAME; index++) {
			// the table will overriden these
			if (_internal_data.player_status[index] == PLAYER_STATUS_PLAYING) {
				CommonData->post_needed[index] = POST_NEEDED_ANTE;
			}
		}
		// adate: there is no button in 7cs, but the card animation on the client uses this
		// number to know which player to animate getting cards first... always left of the button
		// in 7cs, player left of the DEALER gets cards first, so we'll just set the button to
		// the last possible seat (right of the dealer) and, as dealing will start left of that player
		// it will look fine.  this is needed because the structure that sends all the cards to the
		// clients gets updated in one shot.
		CommonData->p_button = (BYTE8)_max_this_game_players; // for proper dealing animation
		UpdateStructures();	// make sure they're all up to date
		_game_state = START_COLLECT_ANTES;	
	}
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
 Function Stud7::SetGameRules(PokerGame game_rules)
 date: kriskoin 2019/01/01 Purpose: if it's a minor variation of the game, we may need to set the rules
***********************************************************************************/
void Stud7::SetGameRules(GameRules game_rules)
{
	_game_rules = game_rules;
	CommonData->game_rules = (BYTE8)game_rules;
	_split_pot_game = (	_game_rules == GAME_RULES_STUD7_HI_LO );
	if (_max_this_game_players == 2) {	// extra betting allowed in heads up
		_max_raises_to_cap += 2;	// allow it to go an extra two for heads up
	}
}

/**********************************************************************************
 Function Stud7::PlayOn(void)
 Date: 20180707 kriskoin :  Purpose: called externally to tell us to move on as much as we can
***********************************************************************************/
void Stud7::PlayOn(int *work_was_done_flag)
{
	if (_b_end_game_handled) {	// already finished the game
		return;	// nothing to do but wait for the destruction of this object
	}
	PostDealerMessageQueue();// clear before we do our next steps
	// first, check if we're waiting for something and deal with it
	// HandleActions returns TRUE if it's still waiting for something
	if (!HandleActions()) {
		// HandleActions() didn't do anything -- no need to evaluate
		// pr(("Not calling EvaluateGame because HandleActions didn't do anything\n"));
		return;
	}
	UpdateStructures();	// make sure they're all up to date
	// and continue evaluating	
	int run_again = TRUE;	// might want to loop through a few times
	while (run_again && SecondCounter >= next_eval_time) {
		run_again = EvaluateGame(work_was_done_flag);
		*work_was_done_flag |= run_again;
		UpdateStructures();	// make sure they're all up to date

		// If a delay before continuing was specified, set it up now...
		if (delay_before_next_eval) {
			next_eval_time = SecondCounter + delay_before_next_eval;
			delay_before_next_eval = 0;	// reset it.
		}
	}
}

/**********************************************************************************
 Function Stud7::HandleActions(void)
 Date: 20180707 kriskoin :  Purpose: handle any player actions that we're expecting
 Return:  TRUE if it handled something, FALSE if it didn't handle anything
***********************************************************************************/
int Stud7::HandleActions(void)
{
	if (_b_end_game_handled) {	// already finished the game
		return FALSE;	// nothing to do but wait for the destruction of this object
	}
	// let's do some checking on what we recieved -- and see if we want to handle it
	int handle_action = TRUE;	// assume it's ok

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
		  #if 0	//kriskoin: 			Error(ERR_INTERNAL_ERROR,"Received a user response action for game %d while expecting %d",
				_GPIResult.game_serial_number, _GPIRequest.game_serial_number);
		  #endif
			handle_action = FALSE;
			break;
		}
		if (_GPIRequest.input_request_serial_number != _GPIResult.input_request_serial_number) {
			// we've recieved a reply to the WRONG ACTION -- out of sync
		  #if 0	//kriskoin: 			Error(ERR_INTERNAL_ERROR,"Received a user response action  %d while expecting %d",
				_GPIResult.input_request_serial_number, _GPIRequest.input_request_serial_number);
		  #endif
			handle_action = FALSE;
			break;
		}
		// right response -- is it a valid action?
		// adate: allow unexpected FORCE_ALL_IN action
		if (action != ACT_FORCE_ALL_IN && !GET_ACTION(_GPIRequest.action_mask, action)) {	// invalid response!?
			Error(ERR_ERROR,"%s(%d) Received action %d from player %d (%s) while expecting from bitmap $%x (game #%d, serial #%d)",
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
		_GPIResult.ready_to_process = FALSE;	//kriskoin: 		return FALSE;	// FALSE means we didn't handle anything
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

	/* POST / DON'T POST ANTE */
	case ACT_POST_ANTE:
		pr(("HandleAction ACT_POST_ANTE\n"));
		_posted_blind[player_index] = TRUE;	// posted something this game?
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;
		// all checks out; post ante as sb
		_internal_data.last_action_amount[player_index] = CommonData->small_blind_amount;
		_pot->PostBlind(player_index, CommonData->small_blind_amount);	
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "%s posts the ante", CommonData->name[player_index]);
	  #endif
		break;	

	case ACT_SIT_OUT_ANTE:
		pr(("HandleAction ACT_SIT_OUT_ANTE\n"));
		// this player has chosen to sit out -- fold him out and mark him as not sitting out
		_internal_data.player_status[player_index] = PLAYER_STATUS_SITTING_OUT;
		CommonData->post_needed[player_index] = POST_NEEDED_NONE;	// we don't care, just doesn't play
		_posted_blind[player_index] = FALSE;	// posted something this game?
		_internal_data.last_action_amount[player_index] = 0;
		_pot->Fold(player_index);
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
		pr(("Got call amt of %d\n", _internal_data.last_action_amount[player_index]));
		_pot->Call(player_index);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
	  #if DEALER_REDUNDANT_MESSAGES		
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s calls", CommonData->name[player_index]);
	  #endif
		break;

	case ACT_BRING_IN: // exclusive to Stud7
		pr(("HandleAction ACT_BRING_IN\n"));
		_internal_data.last_action_amount[player_index] = CommonData->big_blind_amount;
		pr(("Got bring in amt of %d\n", _internal_data.last_action_amount[player_index]));
		_pot->BringIn(player_index, CommonData->big_blind_amount);
		_next_player_to_act = GetNextPlayerInTurn(player_index);
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s brings-in low", CommonData->name[player_index]);
		break;

	case ACT_BRING_IN_ALL_IN: // exclusive to Stud7
		pr(("HandleAction ACT_BRING_IN_ALL_IN\n"));
		_internal_data.last_action_amount[player_index] = CommonData->big_blind_amount;
		pr(("Got bring in amt of %d\n", _internal_data.last_action_amount[player_index]));
		_pot->BringIn(player_index, CommonData->big_blind_amount);
		_internal_data.player_status[player_index] = PLAYER_STATUS_ALL_IN;
		_next_player_to_act = GetNextPlayerInTurn(player_index);
		SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s brings-in all-in", CommonData->name[player_index]);
		break;

	case ACT_BET:
		pr(("HandleAction ACT_BET\n"));
		// if there'a a bet override amount, that's the action amount.. otherwise, default
		_internal_data.last_action_amount[player_index] = 
			(_pot->GetBetOverride()) ? _pot->GetBetOverride() :	_pot->GetRaiseAmount(player_index);
		pr(("Got bet amt of %d\n", _internal_data.last_action_amount[player_index]));
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
		pr(("Got raise amt of %d\n", _internal_data.last_action_amount[player_index]));
		_pot->Raise(player_index);
		round_has_been_raised = TRUE;	// betting rounds 1 and 2 care about this
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
		
	case ACT_SHOW_SHUFFLED:
		pr(("HandleAction ACT_SHOW_SHUFFLED\n"));
		Shuffle7csHoleCards(player_index);
		// we need to log the displayed order for hand histories
		PL->LogGameAll7csCards(CommonData->game_serial_number, player_index, 
			_hand[player_index]->GetCard(0),
			_hand[player_index]->GetCard(1),
			_hand[player_index]->GetCard(2),
			_hand[player_index]->GetCard(3),
			_hand[player_index]->GetCard(4),
			_hand[player_index]->GetCard(5),
			_hand[player_index]->GetCard(6));
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
	PL->LogGameAction(CommonData->game_serial_number,_GPIResult.input_request_serial_number,
		player_index, action, _internal_data.last_action_amount[player_index], _pot->GetRake());
	return TRUE;	// TRUE means we handled something
}
/**********************************************************************************
 Function Stud7::EvaluateGame(void)
 Date: 20180707 kriskoin :  Purpose: this is the main processing of the game... it figures out what state we're
          in, what needs to be evaluated... and it goes forward as far as it can --
		  till it needs something... and asks for it and returns.  the next call to 
		  PlayOn() will wind up back here, hopefully in an advanced state
 Returns: TRUE/FALSE whether it should be called again	
***********************************************************************************/
int Stud7::EvaluateGame(int *work_was_done_flag)
{
	if (_b_end_game_handled) {	// already finished the game
		return FALSE;	// nothing to do but wait for the destruction of this object
	}
	BYTE8 p_index;	// used below
	int evaluate_again = FALSE;	// don't repeat by default
	int more_than_one_active_player = (_pot->GetActingPlayersLeft() > 1);
	switch (_game_state) {
	/* COLLECT ANTES */
	case START_COLLECT_ANTES:
		// for this gametype, tell the pot how far a betting round can go
		_pot->SetMaxRaisesToCap(_max_raises_to_cap);	
		pr(("EvalGame got START_COLLECT_ANTES\n"));
		// adate: set to BLAB from NORMAL
		SendDealerMessage(CHATTEXT_DEALER_BLAB,"Starting a new hand...");
		_internal_data.p_small_blind = (BYTE8)-1;	// trigger error if this gets used
		// we collect antes, starting left of the dealer and going around
		evaluate_again = TRUE;		// we'll loop to start blinds collection
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		// 24/01/01 kriskoin:
		if (CommonData->small_blind_amount) {
			_pot->NewBettingRound(_low_limit, _low_limit*_max_raises_to_cap);
			_game_state = COLLECT_ANTES;
		} else {
			_game_state = DEAL_POCKETS;
		}
		break;

	case COLLECT_ANTES:	// we need to collect antes from everyone
		pr(("EvalGame got COLLECT_ANTES\n"));
		// we will use the small_blind as the ante collector
		_internal_data.p_small_blind = GetNextPlayerInTurn(_internal_data.p_small_blind, TRUE);
		// we keep going around till we hit a player we've asked before
		if (CommonData->post_needed[_internal_data.p_small_blind] != POST_NEEDED_ANTE) {	// we've wrapped
			// if less than 2 antes, refund and cancel the game
			if (_pot->GetPlayersLeft() < 2) {
				SendDealerMessage(CHATTEXT_DEALER_NORMAL, "Game is cancelled -- not enough players.  Refunding antes.");
				_game_state = CANCEL_GAME;	// abort
			} else {
				SendDataNow();	// update all clients
				_game_state = DEAL_POCKETS;	// advance to next state -- deal cards to everyone
			}
			evaluate_again = TRUE;		// we'll loop to start collecting BB
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			break;
		}
		// we have a player that  we need to ask to post an ante
		RequestActionFromPlayer(_internal_data.p_small_blind);	// first, prepare to ask for actions from this player
		SetActionTimeOut(20, TRUE);		// wait only 20 seconds...
		SetValidAction(ACT_POST_ANTE);	// two choices: post the ante
		SetValidAction(ACT_SIT_OUT_ANTE);	// or don't post the ante
		PostAction();
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;


	/* DEAL CARDS */
	case DEAL_POCKETS:
		// 24/01/01 kriskoin:
		if (CommonData->small_blind_amount) {	// only needed if we collected antes
			_pot->CloseBettingRound();
			SendDataNow();	// update all clients
		}
		_pot->NewBettingRound(_low_limit, _low_limit*_max_raises_to_cap);
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING **");
		pr(("EvalGame got DEAL_POCKETS\n"));
		// let's deal pocket cards to all playing players, starting left of the dealer
		_deck->CreateDeck();
		_deck->ShuffleDeck();
		// deal the pockets
		pair_on_board = FALSE;		// no cards = no pairs
		_cards_dealt = 0;	// reset
		DealEveryoneOneCard(FALSE, TRUE);	// first card... (down)
		DealEveryoneOneCard(FALSE, TRUE);	// second card...(down)
		DealEveryoneOneCard(FALSE, TRUE);	// third card... (up)
		evaluate_again = TRUE;		// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_1;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_pocket = (BYTE8)(_pot->GetPlayersLeft());
		LogPlayerStayedThisFar(DEAL_POCKETS);	// keep track for all players
		break;

	case DEAL_4TH:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING 4TH STREET **");
		pr(("EvalGame got DEAL_4TH\n"));
		_cards_dealt = 0;	// reset
		DealEveryoneOneCard(FALSE, TRUE);	// 4th card... (up)
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_2;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_flop = (BYTE8)(_pot->GetPlayersLeft());
		LogPlayerStayedThisFar(DEAL_FLOP);	// keep track for all players
		break;

	case DEAL_5TH:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING 5TH STREET**");
		pr(("EvalGame got DEAL_5TH\n"));
		_cards_dealt = 0;	// reset
		DealEveryoneOneCard(FALSE, TRUE);	// 5th card... (up)
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_3;
		break;

	case DEAL_6TH:
		SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING 6TH STREET **");
		pr(("EvalGame got DEAL_6TH\n"));
		_cards_dealt = 0;	// reset
		DealEveryoneOneCard(FALSE, TRUE);	// 6th card... (up)
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_4;
		break;

	case DEAL_RIVER:
		pr(("EvalGame got DEAL_RIVER\n"));
		_cards_dealt = 0;	// reset
		if (_pot->GetPlayersLeft() > _deck->GetCardsLeft()) { // not enough cards
			SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING COMMON RIVER CARD **");
			Card card = _deck->DealNextCard();
			_flop->Add(card);
			PL->LogGameCardDealt(CommonData->game_serial_number,-1,card);
		} else {
			SendDealerMessage(CHATTEXT_DEALER_BLAB_NOBUFFER, "** DEALING RIVER **");
			_river_7cs_dealt_to_everyone = TRUE;
			DealEveryoneOneCard(FALSE, TRUE);	// 7th card... (down)
		}
		evaluate_again = TRUE;	// we'll loop to start the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		_game_state = START_BETTING_ROUND_5;
		// notify the common structure how many players were in at this point
		CommonData->players_saw_river = (BYTE8)(_pot->GetPlayersLeft());
		_dealt_river = TRUE;
		LogPlayerStayedThisFar(DEAL_RIVER);	// keep track for all players
		break;

	/* INITIALIZE BETTING ROUNDS */
	case START_BETTING_ROUND_1:	// first call to start the loop of the first betting round
		pr(("EvalGame got START_BETTING_ROUND_1\n"));
		_internal_data.s_gameflow = GAMEFLOW_DURING_GAME;
		// set the maximum rake based on the number of players
	  #if 1
		if (_chip_type == CT_REAL) { // only real games rake
			_pot->SetMaxRake();	// pot internally sets its max rake
		} else {
			_pot->SetMaxRake(0);// no rake for anything else
		}
	  #else
		kp1(("%s(%d) 7-Card stud -- play money tables are raking\n",_FL));
		_pot->SetMaxRake();	// pot internally sets its max rake
	  #endif
		// first time in, make sure we start in the right place (player with low card)
		_next_player_to_act = GetPlayerWithLowestDoorCard();
		_bring_in_player = _next_player_to_act;
		if (more_than_one_active_player) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s opens with the lowest card",
				CommonData->name[_next_player_to_act]);
		}
		_pot->SetFirstToAct(_next_player_to_act);
		// 24/01/01 kriskoin:
		if (!CommonData->small_blind_amount) {	// no ante means no ante betting round
			_internal_data.p_waiting_player = (BYTE8)_bring_in_player;
			//kriskoin: 			SendDataNow();
		}
		player_is_opening_bettor = TRUE;	// his choices are different
		round_has_been_raised = FALSE;
		_game_state = BETTING_ROUND_1;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_2:	// first call to start the loop of the 2nd betting round
		pr(("EvalGame got START_BETTING_ROUND_2\n"));
		// figure out who acts first

		_next_player_to_act = GetPlayerWithBestUpCards();
		// in Stud7, in the 2nd betting round, there's a wierd rule:  if there is an open pair
		// on the board, the betting round is actually a high-limit round -- but it doesn't go
		// high limit until someone bets the higher limit... then it's stuck on the higher limit
		// 24/01/01 kriskoin:
		if (pair_on_board && !_split_pot_game) {	// makes a difference here...
			if (more_than_one_active_player) {
				SendDealerMessage(CHATTEXT_DEALER_NORMAL, "There's a pair on board");
			}
			_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// high limit
		} else  {
			_pot->NewBettingRound(_low_limit, _low_limit*_max_raises_to_cap);	// still low limit
		}
		if (more_than_one_active_player) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s will open", CommonData->name[_next_player_to_act]);
		}
		_pot->SetFirstToAct(_next_player_to_act);
		round_has_been_raised = FALSE;
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		_game_state = BETTING_ROUND_2;
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_3:	// first call to start the loop of the 3rd betting round
		pr(("EvalGame got START_BETTING_ROUND_3\n"));
		_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// now we're high limit
		// figure out who acts first
		_next_player_to_act = GetPlayerWithBestUpCards();
		if (more_than_one_active_player) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s will open", CommonData->name[_next_player_to_act]);
		}
		_pot->SetFirstToAct(_next_player_to_act);
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		_game_state = BETTING_ROUND_3;
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_4:	// first call to start the loop of the 4th betting round
		pr(("EvalGame got START_BETTING_ROUND_4\n"));
		_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// we're at high limit
		// figure out who acts first
		_next_player_to_act = GetPlayerWithBestUpCards();
		if (more_than_one_active_player) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s will open", CommonData->name[_next_player_to_act]);
		}
		_pot->SetFirstToAct(_next_player_to_act);
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		_game_state = BETTING_ROUND_4;
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	case START_BETTING_ROUND_5:	// first call to start the loop of the last betting round
		pr(("EvalGame got START_BETTING_ROUND_5\n"));
		_pot->NewBettingRound(_high_limit, _high_limit*_max_raises_to_cap);	// we're at high limit
		// figure out who acts first
		_next_player_to_act = GetPlayerWithBestUpCards();
		if (more_than_one_active_player) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "%s will open", CommonData->name[_next_player_to_act]);
		}
		_pot->SetFirstToAct(_next_player_to_act);
		_next_player_gets_delay = TRUE;	// first to acts gets extra time
		_game_state = BETTING_ROUND_5;
		evaluate_again = TRUE;		// we'll loop to continue the betting round
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;

	/* BETTING ROUNDS */
	case BETTING_ROUND_1:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_1\n"));
		// the betting round ends when it's come all the way around to the
		// instigator
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 1 is finished\n"));
			_pot->CloseBettingRound();	// do any end of round processing	
			evaluate_again = TRUE;	// we'll loop to deal 4h street
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_4TH;
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	// 2022 kriskoin
			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_pot->PotWasJustCapped()) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
		}
		p_index = _next_player_to_act;	// the current player
		if (player_is_opening_bettor) {
			// opening bettor in Stud7 is different -- handled here, and assumes no all-in
			player_is_opening_bettor = FALSE;	// only once
			RequestActionFromPlayer(p_index);	// first, prepare to ask for actions from this player
			if (_next_player_gets_delay) {
				_next_player_gets_delay = FALSE;	// reset for next player
				AddToActionTimeOut(CARD_ANIMATION_DELAY	* _cards_dealt);
			}
			// adate: if he came into the game with 3 chips, one has been used for ante -- he's
			// got 2 left -- enough only for all-in bring-in
			if (CommonData->chips[p_index] == 
					(CommonData->big_blind_amount + CommonData->small_blind_amount) ) {
				SetValidAction(ACT_BRING_IN_ALL_IN);	// bring it in all-in (only choice)
			} else {
				SetValidAction(ACT_BRING_IN);	// bring it in with the low amount
				// check if he came in with just enough to be all-in here
				if (CommonData->chips[p_index] <= 
						(_pot->GetRaiseAmount(p_index) + CommonData->small_blind_amount) ) {
					SetValidAction(ACT_BET_ALL_IN);		// bet all_in
				} else {
					SetValidAction(ACT_BET);		// bet the normal high amount
				}
			}
			// bring in amount is taken from the big blind... no need to set it
			_GPIRequest.bet_amount = _pot->GetRaiseAmount(p_index);
			PostAction();
		} else {
			RequestNextMoveFromPlayer(p_index);
		}
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
			evaluate_again = TRUE;	// we'll loop to deal 5th street
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_5TH;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	// 2022 kriskoin
			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_pot->PotWasJustCapped()) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
		}
		p_index = _next_player_to_act;	// the current player
		// n.b. we call a different RequestNextMove function here...
		RequestNextMoveFromPlayerRound2(p_index);	// Stud7 override
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
			evaluate_again = TRUE;	// we'll loop to deal 6th street
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_6TH;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	// 2022 kriskoin
			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_pot->PotWasJustCapped()) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
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
			_pot->CloseBettingRound();	// do any end of round processing	
			evaluate_again = TRUE;		// we'll loop to deal river card
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = DEAL_RIVER;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	// 2022 kriskoin
			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_pot->PotWasJustCapped()) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
		}
		p_index = _next_player_to_act;	// the current player
		RequestNextMoveFromPlayer(p_index);
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
		break;		

	case BETTING_ROUND_5:	// we're somewhere in the betting round
		pr(("EvalGame got BETTING_ROUND_5\n"));
		// the betting round ends when it's come all the way around to the
		// instigator, and it hasn't been raised to him
		if (_pot->BettingRoundIsFinished()) {	// it's done, move on
			SendDataNow();	// update all clients
			pr(("** Betting round 5 is finished\n"));
			_pot->CloseBettingRound();	// do any end of round processing	
			evaluate_again = TRUE;		// we'll loop to deal with the endgame
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);	// something was actually done during this call.
			_game_state = END_GAME_START;	// advance to next state
		  #if INCLUDE_EXTRA_SEND_DATA_CALLS	// 2022 kriskoin
			SendDataNow();	// update all clients
		  #endif
			break;
		}
		if (_pot->PotWasJustCapped()) {
			SendDealerMessage(CHATTEXT_DEALER_BLAB, "Betting is capped");
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
		} else if (!CommonData->small_blind_amount && !_pot->GetPot(0)) {	// no ante, bring in refunded
			SendDealerMessage(CHATTEXT_DEALER_WINNER, "%s takes back his bring-in", 
				CommonData->name[_bring_in_player]);
		} else {
			UpdateDataBaseChipCounts();
		}
		CheckForBadBeat();
		_b_end_game_handled = TRUE;
		_internal_data.s_gameover = GAMEOVER_TRUE; // all ready for the clients to know
		UpdateStructures();		// make sure they're all up to date for the very end		
		PostDealerMessageQueue();// last minute comments
		LogEndGame();
		break;

	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) Unhandled _game_state (%d) in game %d",
			_FL, _game_state, CommonData->game_serial_number);
		DIE("EvaluateGame got something it didn't understand");
	}
	return evaluate_again;
}

/**********************************************************************************
 Function Stud7::GetPlayerWithLowestDoorCard(void)
 date: kriskoin 2019/01/01 Purpose: return the player with the lowest door card (opens betting in 7-card stud)
***********************************************************************************/
BYTE8 Stud7::GetPlayerWithLowestDoorCard(void)
{
	BYTE8 p_index = INVALID_PLAYER_INDEX;	// will be incremented properly in loop
	// we don't want all-in players, so this is called with FALSE
	p_index = GetNextPlayerEligibleForCard(p_index, FALSE);	// move to actual first player
	if (p_index == INVALID_PLAYER_INDEX) {	// everyone is all in? 19:::		return 0;	// doesn't matter what we return; it will be ignored
	}
	BYTE8 first_player_checked = p_index;
	Card lowest_card = _hand[p_index]->GetCard(2);	// 2 is the first face-up card
	BYTE8 lowest_player = p_index;
	do {
		Card test_card = _hand[p_index]->GetCard(2);
		// trying to find LOWER card, so test below is "backwards" -- careful
		if (_poker->FindBetterCard(lowest_card, test_card) == CARD_1) {	// found a LOWER card
			lowest_card = test_card;	// lowest card so far
			lowest_player = p_index;	// player with low card so far
		}
		p_index = GetNextPlayerEligibleForCard(p_index, FALSE);	// move to next player
	} while (p_index != first_player_checked);	// already checked this player -- exit loop
	return lowest_player;
}

/**********************************************************************************
 Function Stud7::GetPlayerWithBestUpCards(void)
 date: kriskoin 2019/01/01 Purpose: for middle betting rounds, returns player that goes first
 Note: the middle upcards are indexes 2,3,4,5
***********************************************************************************/
BYTE8 Stud7::GetPlayerWithBestUpCards(void)
{
	HighHandEval hhe;		// used for setting/getting eval info
	HighHandEval best_hhe;	// used for setting/getting best eval info so far
	zstruct(best_hhe);
	BYTE8 p_index = INVALID_PLAYER_INDEX;	// will be incremented properly in loop
	p_index = GetNextPlayerEligibleForCard(p_index, FALSE);	// move to actual first player
	if (p_index == INVALID_PLAYER_INDEX) {	// everyone is all in? 19:::		return 0;	// doesn't matter what we return; it will be ignored
	}
  #if 0	// we'll never get here -- remove
	if (p_index >= _max_this_game_players) {
		// This is bad... how could we get here?
		Error(ERR_INTERNAL_ERROR, "%s(%d) no players left eligible for cards! (got %d)", _FL, p_index);
		p_index = 0;	// don't crash?  we don't know which is worse.
	}
  #endif
	BYTE8 first_player_checked = p_index;
	BYTE8 best_player = p_index;
	do {
		zstruct(hhe);
		// any cards that haven't been dealt are autoset already to CARD_NO_CARD
		hhe.test_card[0] = _hand[p_index]->GetCard(2);
		hhe.test_card[1] = _hand[p_index]->GetCard(3);
		hhe.test_card[2] = _hand[p_index]->GetCard(4);
		hhe.test_card[3] = _hand[p_index]->GetCard(5);
		HighHandEvaluate(&hhe);
		if (FindBetterHandEvalStruct(&hhe, &best_hhe) == HAND_1) {	// new one is better
			best_player = p_index;
			memcpy(&best_hhe, &hhe, sizeof(HighHandEval));
		}
		p_index = GetNextPlayerEligibleForCard(p_index, FALSE);	// move to next player
	} while (p_index != first_player_checked);	// already checked this player -- exit loop
	return best_player;
}

/**********************************************************************************
 Function Stud7::FindBetterHandEvalStruct(HighHandEval hhe_1, HighHandEval hhe_2)
 date: kriskoin 2019/01/01 Purpose: used by function above to find better of two HHE structures
***********************************************************************************/
WinningHand Stud7::FindBetterHandEvalStruct(HighHandEval *hhe_1, HighHandEval *hhe_2)
{
	// different hand types?
	if (hhe_1->hand_type != hhe_2->hand_type) {
		return (hhe_1->hand_type  > hhe_2->hand_type ? HAND_1: HAND_2);
	}
	// different hand values?
	if (hhe_1->hand_value != hhe_2->hand_value) {
		return (hhe_1->hand_value > hhe_2->hand_value ? HAND_1: HAND_2);
	}
	// better kickers..?
	if (hhe_1->kicker[0] != hhe_2->kicker[0]) {
		return (hhe_1->kicker[0] > hhe_2->kicker[0] ? HAND_1: HAND_2);
	}
	if (hhe_1->kicker[1] != hhe_2->kicker[1]) {
		return (hhe_1->kicker[1] > hhe_2->kicker[1] ? HAND_1: HAND_2);
	}
	if (hhe_1->kicker[2] != hhe_2->kicker[2]) {
		return (hhe_1->kicker[2] > hhe_2->kicker[2] ? HAND_1: HAND_2);
	}
	// at this point, identical
	return HAND_TIE;
}

/**********************************************************************************
 Function void Stud7::HighHandEval()
 date: kriskoin 2019/01/01 Purpose: used to evaluate 2,3 and 4 card open cards to see who goes first
 Note: get and returns info in the hhe structure
***********************************************************************************/
void Stud7::HighHandEvaluate(HighHandEval *hhe)
{
	int card_values[CARD_RANKS];
	int rank_index;
	int kicker_index;	// highest to lowest values, from index 0 to 3
	// zero and init
	for (rank_index=0; rank_index < CARD_RANKS; rank_index++) {
		card_values[rank_index] = 0;
	}
	hhe->kicker[0] = -1;
	hhe->kicker[1] = -1;
	hhe->kicker[2] = -1;
	// add in totals for each
	if (hhe->test_card[0] != CARD_NO_CARD) card_values[RANK(hhe->test_card[0])]++;
	if (hhe->test_card[1] != CARD_NO_CARD) card_values[RANK(hhe->test_card[1])]++;
	if (hhe->test_card[2] != CARD_NO_CARD) card_values[RANK(hhe->test_card[2])]++;
	if (hhe->test_card[3] != CARD_NO_CARD) card_values[RANK(hhe->test_card[3])]++;
	// what have we got..?
	int pairs_found = 0;
	int top_pair = -1;		// mark as unknown
	int high_card = -1;
	for (rank_index = CARD_RANKS-1; rank_index >= 0; rank_index--) {	// best to worst
		// keep track if this is the highest card we've seen
		if (high_card == -1 && card_values[rank_index]) {
			high_card = rank_index;
		}
		// check for 4 of a kind
		if (card_values[rank_index] == 4) {	// he's got 4 of a kind (!)
			hhe->hand_type = FOUROFAKIND;
			hhe->hand_value = rank_index;
			return;
		}
		// check for 3 of a kind
		if (card_values[rank_index] == 3) {	// he's got 3 of a kind
			hhe->hand_type = THREEOFAKIND;
			hhe->hand_value = rank_index;
			return;
		}
		// check for a pair
		if (card_values[rank_index] == 2) {	// he's got a pair (perhaps two)
			pairs_found++;
			pair_on_board = TRUE;	// betting round 2 cares about this (and no one else!)
			if (top_pair == -1) {
				top_pair = rank_index;
				hhe->hand_value = top_pair;
			} else {	// it's the 2nd pair; we have enough to set and exit
				// hhe->hand_value has already been set to top_pair
				hhe->kicker[0] = rank_index;
				hhe->hand_type = TWOPAIR;
				return;
			}
		}
	}
	// at this point, we've found nothing better than a pair or high card
	if (pairs_found) {	// there was a single pair -- set kickers and exit
		// hhe->hand_value has already been set to top_pair
		hhe->hand_type = PAIR;
		// set kickers
		kicker_index = 0;	// highest to lowest values, from index 0 to 3
		rank_index = CARD_RANKS-1;
		do {
			// all single cards are eligible kickers
			if (card_values[rank_index] && card_values[rank_index] != top_pair) {
				hhe->kicker[kicker_index] = rank_index;
				kicker_index++;
			}
			rank_index--;
		} while (rank_index);
		return;
	}
	// nothing -- just a high card -- set all kickers
	hhe->hand_type = HIGHCARD;
	hhe->hand_value = high_card;
	kicker_index = 0;	// highest to lowest values, from index 0 to 3
	rank_index = CARD_RANKS-1;
	do {
		// all single cards are eligible kickers
		if (card_values[rank_index] && rank_index != high_card) {
			hhe->kicker[kicker_index] = rank_index;
			kicker_index++;
		}
		rank_index--;
	} while (rank_index);
}


/**********************************************************************************
 Function Stud7::RequestNextMoveFromPlayerRound2(BYTE8 p_index)
 date: kriskoin 2019/01/01 Purpose: similar to RequestNextMoveFromPlayer(), tuned for Stud7 round2 quirks
***********************************************************************************/
void Stud7::RequestNextMoveFromPlayerRound2(BYTE8 p_index)
{
	// 24/01/01 kriskoin:
	if (_split_pot_game) {
		RequestNextMoveFromPlayer(p_index);	// no need to override
		return;
	}
	// if there's no pair showing, the round has been defined as low-limit -- nothing
	// to override -- all default actions are ok
	if (!pair_on_board) {
		RequestNextMoveFromPlayer(p_index);	// no need to override
		return;
	}
	// if there was a pair on board, but the round has already been raised to the
	// higher limit, all defaults are ok
	if (pair_on_board && round_has_been_raised) {
		RequestNextMoveFromPlayer(p_index);	// no need to override
		return;
	}
	// at this point, there's a pair on board... but we're still having to offer
	// the low limit -- as the high limit hasn't been bet yet
	// we'll offer the low bet in the "bet" slot, the higher in the "raise"
	
	// ask this player for his next move after figuring out his options
	RequestActionFromPlayer(p_index);
	if (_next_player_gets_delay) {
		_next_player_gets_delay = FALSE;	// reset for next player
		AddToActionTimeOut(CARD_ANIMATION_DELAY	* _cards_dealt);
	}
	SetValidAction(ACT_FOLD);		// FOLDing is always an option
	
	// the raise amount is the interval that's been set for the pot
	_GPIRequest.raise_amount = _pot->GetRaiseAmount(p_index);
	// we want a flag set telling us if he'd be all in on a raise
	int all_in_on_raise = _pot->AllInOnRaise(p_index);

	_GPIRequest.call_amount = _pot->GetCallAmount(p_index);
	// we want a flag set telling us if he'd be all in on a call
	int all_in_on_call = _pot->AllInOnCall(p_index);

	// the bet amount alone is the low limit
	_GPIRequest.bet_amount = (_GPIRequest.call_amount + _low_limit);
	_pot->SetBetOverride(_GPIRequest.bet_amount);
	
	// now we know all the options; structure the action mask for the player
	if (!_GPIRequest.call_amount) {	// call amount is zero -- he can check/bet
		SetValidAction(ACT_CHECK);
		// if he'd be all in on the raise, we'll make it his only choice
		if (all_in_on_raise) {
			SetValidAction(ACT_RAISE_ALL_IN);
		// if he's got chips -- his choice to bet the low or high limit
		} else {
			SetValidAction(ACT_BET);
			SetValidAction(ACT_RAISE);
		}
	} else {	// it's more than 0 to keep going... call or raise
		if (all_in_on_call) {	// check if we'd be all in just on the call
			SetValidAction(ACT_CALL_ALL_IN);	// all-in if we call
		} else {
			SetValidAction(ACT_CALL);			// normal call
		}
		// reraise allowed if the pot isn't capped provided we're not all in on the call
		if (!all_in_on_call &&
			!_pot->PotIsCapped() && 
			_pot->GetActingPlayersLeft() > 1 &&
			_GPIRequest.raise_amount) {
				if (all_in_on_raise) {		// are we all in if we raise?
					// same as above -- let him raise all in
					SetValidAction(ACT_RAISE_ALL_IN);
				} else {
					// his choice, bet/raise either level
					SetValidAction(ACT_BET);
					// adate: raise amount must be greater than bet amount for it
					// to be a valid option
					if (_GPIRequest.raise_amount > _GPIRequest.bet_amount) {
						SetValidAction(ACT_RAISE);
					}
				}
		}
	}
	// close off the structure and post it
	PostAction();
}

/**********************************************************************************
 void Stud7::Shuffle7csHoleCards()
 Date: 20180707 kriskoin :  Purpose: when player hits [Show Shuffled], shuffle cards 1,2,7 from his hand
***********************************************************************************/
void Stud7::Shuffle7csHoleCards(BYTE8 p_index)
{
	pr(("%s(%d) shuffling for player %d\n", _FL, p_index));
	pr(("%s(%d) original c1=%c%c, c2=%c%c, c3=%c%c\n", _FL,
		cRanks[RANK(card[0])], cSuits[SUIT(card[0])],
		cRanks[RANK(card[1])], cSuits[SUIT(card[1])],
		cRanks[RANK(card[6])], cSuits[SUIT(card[6])] ));
	// resulting shuffle can be in any order execept the original
	Card original0 = _hand[p_index]->GetCard(0);
	Card original1 = _hand[p_index]->GetCard(1);
	for (int i=0; i < 10; i++) {
		// get two random numbers of 0,1 or 6
		int r1 = random(3);	// 0,1 or 2
		r1 = (r1 == 2 ? 6 : r1);	// map to 0,1,6
		int r2 = random(3);
		r2 = (r2 == 2 ? 6 : r2);	// map to 0,1,6
		_hand[p_index]->SwapCards(r1,r2);
	}
	// if they happen to be the same, just call this again
	if (_hand[p_index]->GetCard(0) == original0 && _hand[p_index]->GetCard(1) == original1) {
		Shuffle7csHoleCards(p_index);
	}
}
