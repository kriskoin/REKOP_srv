/**********************************************************************************
 Member functions for Pot object
 Date: 20180707 kriskoin : **********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#include <stdlib.h>
#include "pokersrv.h"
#include "game.h"
#include "pot.h"

/**********************************************************************************
 Function Pot::Pot();
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Pot::Pot(void)
{
	// init values
	_total_pot = 0;
	_pot_index = 0;
	_rake = 0;
	_max_rake = 300;	// default, can be changed externally ($3.00)
	_player_count = 0;
	_players_all_in = 0;
	_first_to_act = 0;
	_last_player_to_raise = 0;
	_last_round_total_pot = 0;
	_pot_has_cap = TRUE;	// by default, all pots have a cap
	_bet_increment = 0;
	_rakeable_players = 0;
	_max_raises_to_cap = 0;
	_rake_profile = RT_NONE;
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		for (int j=0; j < MAX_PLAYERS_PER_GAME; j++) {
			_live_for_pot[i][j] = FALSE;
		}
		_playing[i] = FALSE;	// nobody plays by default
		_folded[i] = FALSE;
		_all_in[i] = FALSE;
		_winner[i] = FALSE;
		_all_in_on_call[i] = FALSE;
		_all_in_on_raise[i] = FALSE;
		_chips_left[i] = 0;
		_chips_bet_total[i] = 0;
		_total_pot_playing_for[i] = 0;
		_pot[i] = 0;	// blank all sidepots
	}
	NewBettingRound(0,0);	// init local state variables too
}

/**********************************************************************************
 Function Pot::~Pot()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Pot::~Pot(void)
{
}

/**********************************************************************************
 Function ::AddPlayer
 date: kriskoin 2019/01/01 Purpose: Add a player to our pot
***********************************************************************************/
void Pot::AddPlayer(int p_index, int chips)
{
	// pr(("Adding %d (%d chips)in Pot::AddPlayer\n", p_index, chips));
	_playing[p_index] = TRUE;
	_chips_left[p_index] = chips;
	_player_count++;
}

/**********************************************************************************
 Function Pot::IsPlaying(int p_index)
 date: kriskoin 2019/01/01 Purpose: T/F -- is this player actually playing?
***********************************************************************************/
int Pot::IsPlaying(int p_index)
{
	// we can't call validate because it's circular...
	if (p_index < 0 || p_index >= MAX_PLAYERS_PER_GAME) {
		return FALSE;
	}
	return _playing[p_index];
}
/**********************************************************************************
 Function Pot::AddUpPots(void)
 date: kriskoin 2019/01/01 Purpose: add up all pots and sidepots
***********************************************************************************/
void Pot::AddUpPots(void)
{
	_total_pot = 0;
	// add up all pots
	for (int pot_num=0; pot_num < MAX_PLAYERS_PER_GAME; pot_num++) {
		_total_pot += _pot[pot_num];
	}
}

/**********************************************************************************
 Function GetTotalPot(void)
 date: kriskoin 2019/01/01 Purpose: return the total value of the pot
***********************************************************************************/
int Pot::GetTotalPot(void)
{
	return _total_pot;
}

/**********************************************************************************
 Function Pot::GetPot(int pot_num);
 date: kriskoin 2019/01/01 Purpose: return the value of a specific pot
***********************************************************************************/
int Pot::GetPot(int pot_num)
{
	return _pot[pot_num];
}

/**********************************************************************************
 Function Pot::SetPot
 date: 24/01/01 kriskoin Purpose: set a pot to a certain amount
***********************************************************************************/
void Pot::SetPot(int pot_num, int chips)
{
	if (pot_num < 0 || pot_num >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad pot index %d in SetPot", _FL, pot_num);
		return;
	}
	_pot[pot_num] = chips;
}

/**********************************************************************************
 Function Pot::GetRake(void)
 date: kriskoin 2019/01/01 Purpose: return how much this pot raked
***********************************************************************************/
int Pot::GetRake(void)
{
	return _rake;
}

/**********************************************************************************
 Function Pot::ClearChipsBetThisRound(void)
 date: kriskoin 2019/01/01 Purpose: clear the chips bet by all players this round
***********************************************************************************/
void Pot::ClearChipsBetThisRound(void)
{
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		_chips_bet_this_round[i] = 0;	// start everyone from scratch
	}
}

/**********************************************************************************
 Function Pot::NewBettingRound(int increment)
 date: kriskoin 2019/01/01 Purpose: reset state variables to prepare for a new round of betting
***********************************************************************************/
void Pot::NewBettingRound(int increment, int cap)
{
	_bet_increment = increment;
	_cap = cap;
	_capped = FALSE;
	_pot_was_capped = FALSE;
	_bring_in_live_amount = 0;
	_last_player_to_raise = _first_to_act;	// might get changed, might not
	_raises_this_round = 0;
	_bet_override_amount = 0;
	_player_just_went_all_in = FALSE;
	ClearChipsBetThisRound();
	_min_increment_this_betting_round = 0;
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		_chips_bet_this_round[i]  = 0;
		_posted_this_round[i] = FALSE;
		_acted_this_round[i] = FALSE;
		_forced_all_in_this_round[i] = FALSE;
		_playing_for_this_round[i] = 0;
		_chips_got_back[i] = 0;
	}
}

/**********************************************************************************
 Function Pot::SetCap(int cap)
 date: 24/01/01 kriskoin Purpose: specifically set the cap amount (useful if we're expanding
		  the cap during a hand
***********************************************************************************/
void Pot::SetCap(int cap)
{
	_cap = cap;
}

/**********************************************************************************
 Function ::GetChipsLeft(int p_index)
 date: kriskoin 2019/01/01 Purpose: returns chips left for this player
***********************************************************************************/
int Pot::GetChipsLeft(int p_index)
{
	return _chips_left[p_index];
}

/**********************************************************************************
 Function Pot::GetChipsGotBack(int p_index)
 date: kriskoin 2019/01/01 Purpose: did this player get any chips back this round?
***********************************************************************************/
int Pot::GetChipsGotBack(int p_index)
{
	return _chips_got_back[p_index];
}

/**********************************************************************************
 Function ::GetChipsBetTotal(int p_index)
 date: kriskoin 2019/01/01 Purpose: returns the total number of chips this player has bet
***********************************************************************************/
int Pot::GetChipsBetTotal(int p_index)
{
	return _chips_bet_total[p_index];
}

/**********************************************************************************
 Function ::GetChipsBetRound(int p_index)
 date: kriskoin 2019/01/01 Purpose: returns the number of chips this player has bet this round
***********************************************************************************/
int Pot::GetChipsBetRound(int p_index)
{
	return _chips_bet_this_round[p_index];
}

/**********************************************************************************
 Function SetRakeablePlayers(int players)
 Date: 2017/7/7 kriskoin Purpose: set the number of players to base the rake on
***********************************************************************************/
void Pot::SetRakeablePlayers(int players)
{
	if (players < 0 || players > MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Something tried to set rakeable players to %d",
			_FL, players);
	} else {
		_rakeable_players = players;
	}
}
/**********************************************************************************
 Function Pot::SetMaxRaisesToCap
 Date: 2017/7/7 kriskoin Purpose: tell us how many bets cap a betting round
***********************************************************************************/
void Pot::SetMaxRaisesToCap(int bets)
{
	_max_raises_to_cap = bets;
}

/**********************************************************************************
 Function Pot::SetMinimumEnforcedBettingIncrement
 date: 24/01/01 kriskoin Purpose: sometimes we need to tell a betting round that the minimum betting increment 
		  is being enforced like the pre-flop betting round in a tournament game
***********************************************************************************/
void Pot::SetMinimumEnforcedBettingIncrement(int enforced_increment)
{
	_min_increment_this_betting_round = enforced_increment;
}
/**********************************************************************************
 Function ::ValidatePlayerIndex(int p_index)
 date: kriskoin 2019/01/01 Purpose: validate the player index
***********************************************************************************/
void Pot::ValidatePlayerIndex(int p_index)
{
	if (p_index < 0 || p_index >= MAX_PLAYERS_PER_GAME) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad player index %d in Validate in Pot object", _FL, p_index);
		DIE("Invalid player index");
	}
	if (!_playing[p_index]) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad player index %d in Validate in Pot object", _FL, p_index);
		DIE("Player is marked as not playing in this hand");
	}
}

/**********************************************************************************
 Function Pot::GetCallAmount(int p_index)
 date: kriskoin 2019/01/01 Purpose: returns amount needed to call for this player
	      also sets an all-in flag if neccessary
***********************************************************************************/
int Pot::GetCallAmount(int p_index)
{
	ValidatePlayerIndex(p_index);
	// the call amount is equal to the diff between what he's bet and the highest
	// so far in the round -- but if the previous player was all-in, he still has
	// to put in the minimum bet_increment if he has enough

	// if there's no bet increment, obviously 0
	if (!_bet_increment) {
		return 0;
	}
	int call_amount = PotMatchAmount() - _chips_bet_this_round[p_index];

	// old way:
	// if call_amount is less than the increment, and this player hasn't acted yet
	// this round, it's some sort of all-in situation -- he needs to bet the increment
	// if he's the last one and it's unmatched, he'll get the difference back anyway
	// new way: it was decided to let him call for the odd amount, even if lower than
	// the increment

  #if 0	// adate: changed back after discussion over lunch at Milestones
	if (call_amount &&
		_player_just_went_all_in &&
		GetActingPlayersLeft() > 1 && 
		call_amount <_bet_increment) {
		// previous player may have been all-in, but this guy has to put in the full increment
			call_amount = _bet_increment;
	}
  #endif
	// 24/01/01 kriskoin:
	// the blinds), the minimum call amount is always the bet_increment, no matter what... so 
	// at levels of 100/200, even if EVERYONE has less, they're all in... the betting increment
	// is fixed.  This doesn't apply to post-flop betting rounds.  This variable is set from holdem.cpp
	if (_min_increment_this_betting_round &&// it's been set
		_chips_bet_this_round[p_index] < _min_increment_this_betting_round &&
		!_posted_this_round[p_index] &&		// wasn't one of the blinds
//		!_acted_this_round[p_index] &&		// only the first time around
		GetActingPlayersLeft() > 1)			// all moot if no one else is left acting
	{
			int potential_new_call_amount = _min_increment_this_betting_round - _chips_bet_this_round[p_index];
			pr(("pnca = %d, old call amt = %d\n", potential_new_call_amount, call_amount ));
			if (potential_new_call_amount > call_amount) {
				call_amount =  potential_new_call_amount;
				pr(("%s(%d) call_amount cranked up to %d chips for player %d\n",
					_FL, call_amount, p_index));
			}
	}

  #if 0	// debug only
	if (call_amount % _bet_increment) {
		kp(("%s(%d) ** GCA call with  amt=%d, pjwai=%d, GetActingPlayers=%d, _bet_inc=%d\n",
		_FL, call_amount,_player_just_went_all_in,GetActingPlayersLeft(),_bet_increment));
	}
  #endif
	// check to see if he'd be all in
	if (_chips_left[p_index] - call_amount <= 0) {	// he'd be all in
		call_amount = _chips_left[p_index];
		_all_in_on_call[p_index] = TRUE;
	} else {
		_all_in_on_call[p_index] = FALSE;
	}
	return call_amount;
}

/**********************************************************************************
 Function Pot::GetRaiseAmount(int p_index)
 date: kriskoin 2019/01/01 Purpose: returns amount needed to raise for this player
	      also sets an all-in flag if neccessary
***********************************************************************************/
int Pot::GetRaiseAmount(int p_index)
{
	ValidatePlayerIndex(p_index);

	if (_capped) {	// override, capped
		return 0;	// no raising allowed if we're capped
	}
	// get call amount from above
	int call_amount = GetCallAmount(p_index);
	// no raising if we'd be all-in on a call
	if (_all_in_on_call[p_index]) {
		return 0;
	}
	// no raising if we're the last player left (we're calling, possibly)
	if (GetActingPlayersLeft() == 1) {
		return 0;
	}
	// if there's no bet increment, obviously 0
	if (!_bet_increment) {
		return 0;
	}
	
	// adate: this was decided via talks with Mark -- see comments in GetCallAmt()
	// 24/01/01 kriskoin:
	// agreed solution was that if we have a _min_increment_this_betting_round, there should
	// be no decrementation from the PotMatchAmount
	// 24/01/01 kriskoin:
	// in the Angelina incident... we were beyond the increment, so it's OK to scale it back
	// 20001219HKMBMM:
	//	Tournament example 1: 100/200
	//		A posts BB all-in 70
	//		B should see Call 70, raise 100 (as opposed to call 70, raise 170)
	//	
	//	Tournament example 2: 100/200
	//		A bets all-in 70
	//		B calls 70
	//		C raises all-in 90
	//		D calls 90
	//		B should see call 20, raise 30

	int bet_increment = _bet_increment; // we may need to change it if there was an all-in
	// 24/01/01 kriskoin:
	// ability in tournament games to raise past the bet increment.  It was not correctly
	// compensated for and as a result it only messed up tournaments. Removing it will let
	// tournament games work in the same way as ring games, which are fine
	// !!! --> WRONG: if (!_min_increment_this_betting_round) {	// 24/01/01 kriskoin:

	// 24/01/01 kriskoin:
	// but will also make sure that if the blinds or anyone at the start of a betting round was
	// all-in for less than where we should be, this will set the increment correctly.
	int _pot_match_amount = max(_min_increment_this_betting_round, PotMatchAmount());
	int difference = _pot_match_amount % bet_increment;	// are we on an odd amount?
	if (difference) {	// yes -- the raise amount is only enough to top to structured amount
		bet_increment -= difference;	// next raise will be back on track
	}
  	
	// raise amount is just the call amount plus the structured amount
	int raise_amount;
	// make sure we're not raising past the cap
	if ( _pot_has_cap &&
			((_chips_bet_this_round[p_index] + call_amount + bet_increment - _bring_in_live_amount) > _cap) ) {
		raise_amount =  _cap - _chips_bet_this_round[p_index];
	} else {	// ok to raise the increment
		raise_amount = call_amount + bet_increment;
	}

	if (_chips_left[p_index] - raise_amount <= 0) {	// he'd be all in
		raise_amount = _chips_left[p_index];
		_all_in_on_raise[p_index] = TRUE;
	} else {
		_all_in_on_raise[p_index] = FALSE;
	}
	// if the raise amount turns out to be less or equal to the call amount, skip it
	if (raise_amount <= call_amount) {
		return 0;
	}
	// return whatever we came up with
	return raise_amount;
}

/**********************************************************************************
 Function Pot::_bet(int p_index, int amount, int live_amount_flag, int all_in_flag)
 date: kriskoin 2019/01/01 Purpose: player bets this amount (internal)
		  ** ALL BETS/POSTS OF ALL TYPES MUST GO THROUGH THIS FUNCTION **
***********************************************************************************/
void Pot::_bet(int p_index, int amount, int live_amount_flag, int force_all_in_flag)
{
	ValidatePlayerIndex(p_index);

	_chips_bet_total[p_index] += amount;
	// if this is a "dead bet" that the player is required to throw into the middle,
	// it doesn't count towards the amount he's bet this round
	if (live_amount_flag) {
		_chips_bet_this_round[p_index] += amount;
	}
	_chips_left[p_index] -= amount;
	// figure out if he's all-in
	_player_just_went_all_in = FALSE;	// will be set TRUE if needed
	if (force_all_in_flag || _chips_left[p_index] <= 0) {	// less then 0 is an error -- we'll trap it
		if (!_all_in[p_index]) {	// just went all in -- deal with it
			// do all handling needed for a player that just went all-in
			SetPlayerAllIn(p_index);
		} else {	// this is an error condition -- a serious one as it shouldn't happen
			Error(ERR_ERROR,"%s(%d) player %d has %d chips???", _FL, p_index, _chips_left[p_index]);
			Error(ERR_ERROR,"%s(%d) An all-in player just tried to act -- check move-setup logic", _FL);
		}
		// test for negative chips, which should *never* happen
		if (_chips_left[p_index] < 0) {	// serious error
			Error(ERR_ERROR,"*** player %d has gone into negative chips (%d)",
				p_index, _chips_left[p_index]);
			DIE("Negative chip value - investigate betting trail to this point");
		}
	}
  #ifdef POT_DEBUG
	kp(("PLAYER %d puts in %d\n",p_index,amount));
  #endif
	//_total_pot += amount;
	if (_pot_has_cap) {
		if ( (_chips_bet_this_round[p_index] >= _cap) ||
			 (_raises_this_round == (_max_raises_to_cap)) ) {	// we're capped
			_capped = TRUE;
			if (_chips_bet_this_round[p_index] > _cap) {
				Error(ERR_FATAL_ERROR,"Gone over betting cap (%d... cap is %d)",
					_chips_bet_this_round[p_index], _cap);
			}
		}
	}
	_bet_override_amount = 0; // whether it was used or not, we can clear it now
}

/**********************************************************************************
 Function Pot::SetPlayerAllIn(int p_index)
 date: kriskoin 2019/01/01 Purpose: do all handling to set a player all-in
***********************************************************************************/
void Pot::SetPlayerAllIn(int p_index)
{
	_all_in[p_index] = TRUE;
	_player_just_went_all_in = TRUE;
	_players_all_in++;		// keep track how many players are all-in
}

/**********************************************************************************
 Function Pot::SetRakeProfile(RakeType rake_profile)
 date: 24/01/01 kriskoin Purpose: set the rake structure that we'll be using
***********************************************************************************/
void Pot::SetRakeProfile(RakeType rake_profile)
{
	_rake_profile = rake_profile;
}

/**********************************************************************************
 Function ::EvaluateRake(void)
 date: kriskoin 2019/01/01 Purpose: evaluate and set what the rake should be based on current pot level
***********************************************************************************/
void Pot::EvaluateRake(void)
{
	// rake = 5% of rakeable pot, up to $3 for now.  this will likely change in the
	// future, but until it does, these amounts are hard-coded.
	int new_rake = 0; // temp assignment
	int rakeable_pot = _total_pot;

	switch (_rake_profile) {
	case RT_NONE:
		new_rake = 0;
		break;
	case RT_PRO1:	// normal 20/40/60 raking ($1 at each level, $3 max)
		if (rakeable_pot >= 6000) {			// 60 or above maxes out our rake at $3
			new_rake = 300;
		} else if (rakeable_pot >= 4000) {	// 5% of 40 but less than 60 = $2
			new_rake = 200;
		} else if (rakeable_pot >= 2000) {	// 5% of 20 but less than 40 = $1
			new_rake = 100;
		} else {
			new_rake = 0;					// anything less, we don't rake
		}
		break;
	case RT_PRO2:	// higher limit 40/70/100 raking ($1 at each level, $3 max)
		if (rakeable_pot >= 10000) {		// 100 or above maxes out our rake at $3
			new_rake = 300;
		} else if (rakeable_pot >= 7000) {	// 5% of 70 but less than 100 = $2
			new_rake = 200;
		} else if (rakeable_pot >= 4000) {	// 5% of 40 but less than 70 = $1
			new_rake = 100;
		} else {
			new_rake = 0;					// anything less, we don't rake
		}
		break;
	case RT_PRO3:	// 50-cent raking at $10 intervals (10/20/30/40/50/60 = $3 max)
		if (rakeable_pot >= 6000) {			// 60 or above maxes out our rake at $3
			new_rake = 300;
		} else if (rakeable_pot >= 5000) {	// 2.5% of 50 but less than 50 = $2.50
			new_rake = 250;
		} else if (rakeable_pot >= 4000) {	// 2.5% of 40 but less than 40 = $2
			new_rake = 200;
		} else if (rakeable_pot >= 3000) {	// 2.5% of 30 but less than 30 = $1.50
			new_rake = 150;
		} else if (rakeable_pot >= 2000) {	// 2.5% of 20 but less than 20 = $1
			new_rake = 100;
		} else if (rakeable_pot >= 1000) {	// 2.5% of 10 but less than 10 = 50 cents
			new_rake = 50;
		} else {
			new_rake = 0;					// anything less, we don't rake
		}
		break;
	case RT_PRO4:	// special case for 15/30 one on one, we rake $1 at $30 (capped $1)
		if (rakeable_pot >= 3000) {		// $1 when we hit $30
			new_rake = 100;
		} else {
			new_rake = 0;					// anything less, we don't rake
		} 
		break;
	case RT_PRO5:	// 50 cents at $20, 50 cents at $40, capped at $1
		if (rakeable_pot >= 4000) {			// $40 or above maxes out our rake at $1
			new_rake = 100;
		} else if (rakeable_pot >= 2000) {	// $20 or above but less than $40 = $0.50
			new_rake = 50;
		} else {
			new_rake = 0;					// anything less, we don't rake
		}
		break;
	case RT_PRO6:	// 25-cent raking at $5 intervals (5/10/15/20 = $1 max)
		if (rakeable_pot >= 2000) {			// $20 or above maxes out our rake at $1
			new_rake = 100;
		} else if (rakeable_pot >= 1500) {	// $15 or higher, we're at $0.75
			new_rake = 75;
		} else if (rakeable_pot >= 1000) {	// $10 or higher, we're at $0.50
			new_rake = 50;
		} else if (rakeable_pot >= 500) {	// $5 or higher, we're at $0.25
			new_rake = 25;
		} else {
			new_rake = 0;					// anything less, we don't rake
		}
		break;
	
	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) _rake_profile is set to %d (impossible)\n", _FL, _rake_profile);
	}
	new_rake = min(new_rake, _max_rake);	// we may have set an external limit on it
	if (new_rake > _rake) {	// yes... we'll rake something
		// new way -- don't rake little sidepots -- we rake from bigger of two pots
		int rake_amount = new_rake - _rake;
		// main pot is index 0... side pots go up from 1
		int rake_index = _pot_index; // assume for the moment it comes from the main pot
	  #if 1 
		// 20001020: we both agreed that we think this deals with the obscure problem 
		// where raking a sidepot of zero may happen if someone goes all-in on the river
		// and we end up with a sidepot of zero... so here we simply look back till we
		// are sure we're about to try to rake from a pot with something in it (or a 
		// previous pot which is the one that should be raked because it has more in it)
		while (rake_index >= 0 && !_pot[rake_index]) {
			// we want to be sure that we're raking from a sidepot that actually has something in it
			rake_index--;
		}
	  #endif
		if (rake_index && (_pot[rake_index] < _pot[rake_index-1]) ) { // current pot is smaller than previous
			rake_index--;	// rake from previous, bigger pot
		}
		// only rake if there's enough in the pot to handle it
		if (_pot[rake_index] > rake_amount) {
		  #if 0
			kp(("%s(%d) Raking %d from $%d pot[%d]\n",
				_FL, rake_amount, _pot[rake_index], rake_index));
		  #endif
			_pot[rake_index] -= rake_amount;
			_rake += rake_amount;
		}
		// keep _total_pot up-to-date
		AddUpPots();
	}
}

/**********************************************************************************
 Function PotMatchAmount(void)
 date: kriskoin 2019/01/01 Purpose: returns the amount the round's pot is at -- the amount to match to
***********************************************************************************/
int Pot::PotMatchAmount(void)
{
	int match_amount = 0;
	// this number is just the highest of the current "chips_bet_this_round"
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_playing[p_index]) { // live player
			match_amount = max(_chips_bet_this_round[p_index], match_amount);
		}
	}
	// pr(("Pot match amount = %d\n", match_amount));
	return match_amount;
}

/**********************************************************************************
 Function Pot::Fold(int p_index)
 date: kriskoin 2019/01/01 Purpose: fold a player from the pot
***********************************************************************************/
void Pot::Fold(int p_index)
{
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_folded[p_index] = TRUE;
	_player_count--;
}

/**********************************************************************************
 Function Pot::ForceAllIn(int p_index)
 date: kriskoin 2019/01/01 Purpose: force a player all in
***********************************************************************************/
void Pot::ForceAllIn(int p_index)
{
	// similar to call, but we tell _bet() that he's all in, even if he has chips left
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_forced_all_in_this_round[p_index] = TRUE;
	// a player being forced all-in is not putting more chips in
	_bet(p_index, 0, TRUE, TRUE);
}

/**********************************************************************************
 Function Pot::Call(int p_index)
 date: kriskoin 2019/01/01 Purpose: player calls
***********************************************************************************/
void Pot::Call(int p_index)
{
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_bet(p_index, GetCallAmount(p_index), TRUE, FALSE);
}

/**********************************************************************************

 Function Pot::Check(int p_index)
 date: kriskoin 2019/01/01 Purpose: player checks
***********************************************************************************/
void Pot::Check(int p_index)
{
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_bet(p_index, 0, TRUE, FALSE);	// _bet does other things too -- stay consistent
}

/**********************************************************************************
 Function Pot::Bet(int p_index)
 date: kriskoin 2019/01/01 Purpose: player bets
***********************************************************************************/
void Pot::Bet(int p_index)
{
	// a "Bet" is just a raise from zero, unless there's an override
	if (_bet_override_amount) {
		Bet(p_index, _bet_override_amount);
	} else {
		Raise(p_index);
	}
}

/**********************************************************************************
 Function Pot::Bet(int p_index, int amount)
 date: kriskoin 2019/01/01 Purpose: player bets a specific amount
***********************************************************************************/
void Pot::Bet(int p_index, int amount)
{
	// Error(ERR_ERROR,"Using non-standard Pot::Bet() function");
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_raises_this_round++;	// _bet will set cap flag if needed
	_bet(p_index, amount, TRUE, FALSE);
	_last_player_to_raise = p_index;
}

/**********************************************************************************
 Function Pot::BringIn(int p_index, int amount)
 date: kriskoin 2019/01/01 Purpose: player brings in Stud7 with a specific amount
 Note: BringIn is only used for 7-card stud
***********************************************************************************/
void Pot::BringIn(int p_index, int amount)
{
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_bet(p_index, amount, TRUE, FALSE);
	_bring_in_live_amount = amount;	// when it's brought in low, it affects next bet
	_last_player_to_raise = p_index;
}

/**********************************************************************************
 Function Pot::Raise(int p_index)
 date: kriskoin 2019/01/01 Purpose: player raises
***********************************************************************************/
void Pot::Raise(int p_index)
{
	ValidatePlayerIndex(p_index);
	_acted_this_round[p_index] = TRUE;
	_raises_this_round++;	// _bet will set cap flag if needed
	_bet(p_index, GetRaiseAmount(p_index), TRUE, FALSE);
	_bring_in_live_amount = 0; // as soon as it's been raised,we're back to normal
	// we want to know the last player to raise -- he's the first to show cards
	_last_player_to_raise = p_index;
}

/**********************************************************************************
 Function Pot::PostBlind(int p_index, int amount)
 date: kriskoin 2019/01/01 Purpose: post a blind is like a bet, but we don't reigister that the player has
          acted this round, so that it'll come around to him again (option)
***********************************************************************************/
void Pot::PostBlind(int p_index, int amount)
{
	ValidatePlayerIndex(p_index);
	// treated like a straight bet
	_bet(p_index, amount, TRUE, FALSE);
	_posted_this_round[p_index] = TRUE;
	// the blind is not raked until it's called... so the max blind bet is the amount
	// that we'll need to match before we can start to rake
}

/**********************************************************************************
 Function Pot::PostDead(int p_index, int amount)
 date: kriskoin 2019/01/01 Purpose: posts a dead amount -- it doesn't count towards what he's bet
 **********************************************************************************/
void Pot::PostDead(int p_index, int amount)
{
	ValidatePlayerIndex(p_index);
	_posted_this_round[p_index] = TRUE;
	_pot[_pot_index] += amount;
	_bet(p_index, amount, FALSE, FALSE); // treated like a straight bet, but flagged as dead
}

/**********************************************************************************
 Function BettingRoundIsFinished(void)
 date: kriskoin 2019/01/01 Purpose: returns TRUE if the current round of betting is complete
***********************************************************************************/
int Pot::BettingRoundIsFinished(void)
{
	// only one player left playing?  everyone folded to him -- it's all over
	if (GetPlayersLeft() == 1) {
		return TRUE;
	}

	// only one player left acting..?  it's possible that there were two left, and then
	// the guy before went all in -- if that's the case, we want this guy to deal with it
	// this is not the way to do this -- need to test if there's a callable amount here...!!!
	// if (GetActingPlayersLeft() == 1 && !_player_just_went_all_in) {
	
	// adate: changed to this to fix problem of last player in round where others went
	// all in (but not the last player) and the last player didn't get chance to call
	// ie, the round is finished if there's one player left, and there's no call amount to the
	// one player left -- the player who is active (ie playing, not all-in)

	if (GetActingPlayersLeft() == 1 && GetCallAmount(GetFirstActivePlayer(FALSE)) == 0) {
		return TRUE;
	}
	
	if (_player_count < 1) {	// serious error
		Error(ERR_INTERNAL_ERROR,"%s(%d) %d players left in BettingRound", _FL, _player_count);
		DIE("Should never be less than one player left in a betting round");
	}
	if (!GetGoneAround()) {
		return FALSE;	// haven't even been around one time
	}
	// adate: we need to trap one exception here... when there are one or more players
	// left, but someone went all-in... it's fair to say that if we've gone all the way
	// around (which we have if we're here) but someone still has an amount to call, the
	// round isn't finished
	for (int q_index=0; q_index < MAX_PLAYERS_PER_GAME; q_index++) {
		if (_playing[q_index] && !_folded[q_index] && !_all_in[q_index]) { // live player
			if (GetCallAmount(q_index)) {
				return FALSE;
			}
		}
	}
	// if we got this far, we made it through the loop without finding a difference
	return TRUE;
}

/**********************************************************************************
 Function Pot::PotIsCapped(void)
 date: kriskoin 2019/01/01 Purpose: T/F is pot capped?
***********************************************************************************/
int Pot::PotIsCapped(void)
{
	return _capped;
}

/**********************************************************************************
 Function Pot::ExpectingATurn(int p_index);
 date: kriskoin 2019/01/01 Purpose: T/F player can expect to possibly act (again) this betting round
***********************************************************************************/
int Pot::ExpectingATurn(int p_index)
{
	if (!_playing[p_index] || _all_in[p_index] || _folded[p_index]) {
		// not playing or folded or all in = no more betting this round
		return FALSE;
	}
	// ok, he's actively playing - if he hasn't acted at all this round, for sure he will
	if (!_acted_this_round[p_index]) {
		return TRUE;
	}
	// he's acted -- but if there's a call amount, he'll be acting
	if (GetCallAmount(p_index)) {
		return TRUE;
	}
	// he's acted and there's nothing to call at the moment -- assume no more
	return FALSE;
}

/**********************************************************************************
 Function Pot::GetGoneAround(void)
 date: kriskoin 2019/01/01 Purpose: returns whether the betting has gone all the way around
***********************************************************************************/
int Pot::GetGoneAround(void)
{
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		// a player hasn't acted if he's playing and not folded and not acted this round
		// and not all-in
		if (_playing[p_index] &&
			!_folded[p_index] &&
			!_acted_this_round[p_index] &&
			!_all_in[p_index]) {
			return FALSE;
		}
	}
	return TRUE; // got through everyone
}

/**********************************************************************************
 Function Pot::GetPlayersLeft(void)
 date: kriskoin 2019/01/01 Purpose: returns how many players are left in the pot
***********************************************************************************/
int Pot::GetPlayersLeft(void)
{
  #ifdef POT_DEBUG
//	kp(("GPL is returning %d players left\n",_player_count));
  #endif
	return _player_count;
}

/**********************************************************************************
 Function Pot::GetActivePlayersLeft(void)
 date: kriskoin 2019/01/01 Purpose: returns how many players are left actively (ie not all in) playing
***********************************************************************************/

int Pot::GetActingPlayersLeft(void)
{
	int count = 0;
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_playing[p_index] && !_folded[p_index] && !_all_in[p_index]) {
			count++;
		}
	}
  #ifdef POT_DEBUG
//	kp(("GAPL returning %d acting players left\n",count));
  #endif
	return count;
}

/**********************************************************************************
 Function Pot::GetFirstActivePlayer(int all_in_allowed_flag)
 date: kriskoin 2019/01/01 Purpose: returns the first player on our list still active
 Note:    if the flag is TRUE, he can be all_in -- otherwise, if FALSE, player must
          still be active, taking turns
***********************************************************************************/
int Pot::GetFirstActivePlayer(int all_in_allowed_flag)
{
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (all_in_allowed_flag) {		
			if (_playing[p_index] && !_folded[p_index]) {
				return p_index;
			}
		} else {
			// can't be all in
			if (_playing[p_index] && !_folded[p_index] && !_all_in[p_index]) {
				return p_index;
			}
		}
	}
	// if we get to here, it means there are no active players left...
  #if 0	// 24/01/01 kriskoin:
	Error(ERR_ERROR,"%s(%d) call to Pot::GetFirstActivePlayer but there aren't any", _FL);
  #endif
	return -1;	// cause a problem for whoever called it...
}

/**********************************************************************************
 Function Pot::AllInOnCall(int p_index)
 date: kriskoin 2019/01/01 Purpose: return T/F, telling us if player would be all in on a call
***********************************************************************************/
int Pot::AllInOnCall(int p_index)
{
	return _all_in_on_call[p_index];
}

/**********************************************************************************
 Function Pot::AllInOnRaise(int p_index)
 date: kriskoin 2019/01/01 Purpose: return T/F, telling us if player would be all in on a raise
***********************************************************************************/
int Pot::AllInOnRaise(int p_index)
{
	return _all_in_on_raise[p_index];
}

/**********************************************************************************
 Function Pot::CloseBettingRound(void)
 date: kriskoin 2019/01/01 Purpose: close off the betting round
***********************************************************************************/
void Pot::CloseBettingRound(void)
{
	WORD32 chips_left_to_allocate[MAX_PLAYERS_PER_GAME];	// local copy
	int will_need_new_side_pot = FALSE;
	int player;	// common loop index
	for (player=0; player < MAX_PLAYERS_PER_GAME; player++) {
		chips_left_to_allocate[player] = _chips_bet_this_round[player];
	}
	// add any dead amount to the pot -- already done
	// loop till we've allocated or refunded anything left to deal with
	forever {
		int left_to_allocate = FALSE;	// figure out what's left to deal with
		int players_left = 0;		// how many have something left to deal with?
		int last_player_found = -1;
		WORD32 min_someone_is_playing_for = 0;	// will be set to the allocation amount
		WORD32 max_someone_is_playing_for = 0;	// will be set to the allocation amount
		int first_time_through = TRUE;
		for (player=0; player < MAX_PLAYERS_PER_GAME; player++) {
			// adate: a bug was reported that sometimes when there was an all-in, no new
			// side pot was being created and that the player had a shot at all the chips.
			// there had been the same report a few weeks ago re 7cs, same problem... that was
			// due to the betting round not being closed after the antes.  in this situation,
			// it can happen all the time due to the fact that if the round was checked all around
			// and there was an all-in, we forgot about it... so we will at this point set it
			// to TRUE... a few lines down it may get acted upon.  if not, a few lines after that
			// it's cleared again
			if (_forced_all_in_this_round[player]) {
				will_need_new_side_pot = TRUE;
			}
			if (chips_left_to_allocate[player] || _forced_all_in_this_round[player]) {	// he's got something
				players_left++;
				last_player_found = player;
				left_to_allocate = TRUE;
				if (first_time_through) {	// first call
					first_time_through = FALSE;
					min_someone_is_playing_for = chips_left_to_allocate[player];
					max_someone_is_playing_for = chips_left_to_allocate[player];
				} else {	// possibly less than it was
					min_someone_is_playing_for = 
						min(min_someone_is_playing_for, chips_left_to_allocate[player]);
					max_someone_is_playing_for = 
						max(max_someone_is_playing_for, chips_left_to_allocate[player]);
				}
			}
		}
		// 99116HK: fixed this freaking all-in on a checked betting round bug once and for all
		if (!max_someone_is_playing_for) {	// checked all around, nothing to do -- exit
			left_to_allocate = FALSE;
		}
		// test to see if we're done
		if (!left_to_allocate) {
			// if someone smoothly went all-in, we'll need a sidepot next betting round
			if (will_need_new_side_pot) {	// for the next betting round...
				_pot_index++;
			}
			break;
		}
		// test if there's only one player left.  if so, he gets it back
		if (players_left == 1) {
			// refund whatever he has left is now done below
			break;
		}
		
		// if we're here, more than one player is left... allocate to proper pot
		will_need_new_side_pot = FALSE;
		for (player=0; player < MAX_PLAYERS_PER_GAME; player++) {
			// he's eligible for part of the pot
			if (chips_left_to_allocate[player] || _forced_all_in_this_round[player]) {	
				// whether this flag was set or not, we can clear it now
				_forced_all_in_this_round[player] = FALSE;
				_pot[_pot_index] += min_someone_is_playing_for;
				chips_left_to_allocate[player] -= min_someone_is_playing_for;
				_live_for_pot[player][_pot_index] = TRUE;
				if (chips_left_to_allocate[player] == 0 && _all_in[player]) {
					will_need_new_side_pot = TRUE;
				}
				if (chips_left_to_allocate[player] < 0) {
					Error(ERR_ERROR,"(%s)%d player with %d chips had %d taken away", 
						_FL, chips_left_to_allocate[player], min_someone_is_playing_for);
				}
			}
		}

		// check to see if we need to advance to the next side pot.  if any player has
		// chips to allocate, that's the case
		for (player=0; player < MAX_PLAYERS_PER_GAME; player++) {
			if (chips_left_to_allocate[player]) {	// he's got something -- next sidepot needed
				if (will_need_new_side_pot) {
					_pot_index++;
				  #ifdef POT_DEBUG
					kp(("Incremented _pot_index to %d\n",_pot_index));
				  #endif
					will_need_new_side_pot = FALSE;	// already allocated new side pot
					if (_pot_index == MAX_PLAYERS_PER_GAME) {
						Error(ERR_ERROR,"(%s)%d _pot_index = MAX_PLAYERS_PER_GAME (%d)", 
							_FL, _pot_index);
						DIE("Should never happen");
					}
					break;
				}
			}
		}
		// loop back...
	}
	// out of the loop
	// adate: everyone with unallocated amounts (usually only one person) get it back
	// this was moved from the (if player == 1) test above because we were trapping folds
	// to one player OK, but not all-in givebacks
	for (int j = 0; j < MAX_PLAYERS_PER_GAME; j++) {
		if (chips_left_to_allocate[j]) {
			_chips_bet_this_round[j] -= chips_left_to_allocate[j];
			_chips_bet_total[j] -= chips_left_to_allocate[j];
			_chips_got_back[j] = chips_left_to_allocate[j];
		}
		_chips_bet_this_round[j]  = 0;	// !!!!!!!!!
	
	}
	AddUpPots();
	EvaluateRake();	// keep rake current
  #ifdef POT_DEBUG
	kp(("---> Closed betting round <--------\n"));
  #endif
}

/**********************************************************************************
 Function Pot::PlayerIsLiveForPot(int p_index, int pot_number)
 date: kriskoin 2019/01/01 Purpose: return T/F if player is live for this particular pot
***********************************************************************************/
int Pot::PlayerIsLiveForPot(int p_index, int pot_number)
{
	return _live_for_pot[p_index][pot_number];
}

/**********************************************************************************
 Function Pot::GetLastPlayerToRaise(void)
 date: kriskoin 2019/01/01 Purpose: who raised last? (he'll be first to show cards)
***********************************************************************************/
int Pot::GetLastPlayerToRaise(void)
{
	return 	_last_player_to_raise;
}

/**********************************************************************************
 Function Pot::SetFirstToAct(int p_index);
 date: kriskoin 2019/01/01 Purpose: set who is the first player to act (eg, last to raise)
***********************************************************************************/
void Pot::SetFirstToAct(int p_index)
{
	ValidatePlayerIndex(p_index);
	_first_to_act = p_index;
}

/**********************************************************************************
 Function Pot::SetMaxRake(int max_rake)
 date: kriskoin 2019/01/01 Purpose: set the maximum rake we're allowed to take
***********************************************************************************/
void Pot::SetMaxRake(int max_rake)
{
	_max_rake = max_rake;
}

/**********************************************************************************
 Function Pot::SetMaxRake(void)
 date: kriskoin 2019/01/01 Purpose: set the max rake based on some rules we agreed to
***********************************************************************************/
void Pot::SetMaxRake(void)
{
	int players_to_base_rake = _player_count;
	// 24/01/01 kriskoin:
	if (_rakeable_players) {
		players_to_base_rake = _rakeable_players;
	}

	if (players_to_base_rake <= 3) {	// 3 or less players, rake = 1
		_max_rake = 100;
	} else if (players_to_base_rake <= 5) {	// 4 to 5 players, rake = 2
		_max_rake = 200;
	} else {
		_max_rake = 300;	// anything else, rake is max 3
	}
	// Limit it to the MaxRake setting from the .INI file so we
	// can host 'no rake happy hour', etc.
	_max_rake = min(_max_rake, MaxRake*100);
}

/**********************************************************************************
 Function Pot::PotWasJustCapped(void)
 date: kriskoin 2019/01/01 Purpose: T/F if pot has been capped since last time we checked
***********************************************************************************/
int Pot::PotWasJustCapped(void)
{
	if (PotIsCapped() && !_pot_was_capped) {	// changed
		_pot_was_capped = TRUE;
		return TRUE;
	}
	return FALSE;
}

/**********************************************************************************
 Function Pot::SetBetOverride(int chips_bet)
 date: kriskoin 2019/01/01 Purpose: the bet amount will not be the same as the raise amount...
***********************************************************************************/
void Pot::SetBetOverride(int chips_bet)
{
	_bet_override_amount = chips_bet;
}

/**********************************************************************************
 Function Pot::GetBetOverride(void)
 date: kriskoin 2019/01/01 Purpose: the bet amount will not be the same as the raise amount...
***********************************************************************************/
int Pot::GetBetOverride(void)
{
	return _bet_override_amount;
}


/**********************************************************************************
 Function Pot::PotHasCap(int cap_flag)
 date: kriskoin 2019/01/01 Purpose: set whether this pot has a cap or not
***********************************************************************************/
void Pot::PotHasCap(int cap_flag)
{
	_pot_has_cap = (BYTE8)cap_flag;
}

/**********************************************************************************
 Function Pot::GetBetIncrement(void)
 date: kriskoin 2019/01/01 Purpose: returns the standard bet for this round (good for stack sizes)
***********************************************************************************/
int Pot::GetBetIncrement(void)
{
	return _bet_increment;
}

/**********************************************************************************
 Function Pot::PlayerJustWentAllIn(void)
 date: kriskoin 2019/01/01 Purpose: return T/F if the last player to act went all in
***********************************************************************************/
int Pot::PlayerJustWentAllIn(void)
{
	return _player_just_went_all_in;
}

/**********************************************************************************
 Function Pot::GetPostedThisRound(int p_index)
 date: kriskoin 2019/01/01 Purpose: T/F if player posted a blind this betting round
***********************************************************************************/
int Pot::GetPostedThisRound(int p_index)
{
	return _posted_this_round[p_index];
}

/**********************************************************************************
 Function Pot::RefundChipsForPlayer(int p_index)
 date: 24/01/01 kriskoin Purpose: refund chips for one particular player
***********************************************************************************/
void Pot::RefundChipsForPlayer(int p_index)
{
	_chips_bet_total[p_index] = 0;
	_chips_bet_this_round[p_index] = 0;
	_chips_got_back[p_index] = 0;
}
/**********************************************************************************
 Function Pot::RefundAllChips(void)
 date: kriskoin 2019/01/01 Purpose: refund all chips bet to all players
***********************************************************************************/
void Pot::RefundAllChips(void)
{
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		RefundChipsForPlayer(p_index);
	}
}

/**********************************************************************************
 Function Pot::ClearChipsInFrontOfPlayers(void)
 date: kriskoin 2019/01/01 Purpose: clear all chips in front of all players
***********************************************************************************/
int Pot::ClearChipsInFrontOfPlayers(void)
{
	int need_to_send_update = FALSE;
	for (int p_index=0; p_index < MAX_PLAYERS_PER_GAME; p_index++) {
		if (_chips_got_back[p_index]) {	// need to update clients?
			need_to_send_update = TRUE;
		}
		_chips_got_back[p_index] = 0;
		_chips_bet_this_round[p_index] = 0;
	}
	return need_to_send_update;
}
