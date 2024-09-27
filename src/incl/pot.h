/**********************************************************************************
 CLASS POT
 Date: 20180707 kriskoin :  "POT" is an object with fancy handling for betting into pots
***********************************************************************************/
#ifndef _POT_H_INCLUDED
#define _POT_H_INCLUDED

#include "game.h"

class Pot {

public:
	Pot(void);
	~Pot(void);
	void NewBettingRound(int increment, int cap);	// prepare for a new round of betting
	void ClearChipsBetThisRound(void);
	void AddPlayer(int p_index, int chips);
	void AddUpPots(void);			// add up all pots
	int GetTotalPot(void);			// return the total pot amount
	int GetPot(int pot_num);		// get a specific side-pot
	void SetPot(int pot_num, int chips);	// set a specific pot to a certain amount
	int GetRake(void);				// return the total rake
	int GetPlayersLeft(void);		// how many left?
	int GetActingPlayersLeft(void);	// how many left still playing (ie not all in)
	int GetGoneAround(void);		// T/F set whether the betting has lapped
	int GetLastPlayerToRaise(void); // who raised last?
	int ExpectingATurn(int p_index);// T/F player may be getting (another) turn this round
	void CloseBettingRound(void);	// called to close off the end of a betting round
	void SetMaxRake(int max_rake);	// set the max rake we're allowed to take
	void SetMaxRake(void);			// figure out what the max rake should be, and set it
	void SetRakeablePlayers(int players); // how many players do we base the rake on?
	void SetRakeProfile(RakeType rake_profile);	// what rake structure to use?
	int GetFirstActivePlayer(int all_in_allowed_flag);	// will return the first player it finds still playing
	void SetCap(int cap);			// set the specific cap amount

	int GetChipsLeft(int p_index);
	int GetChipsBetTotal(int p_index);
	int GetChipsBetRound(int p_index);
	int GetChipsGotBack(int p_index);
	int GetPostedThisRound(int p_index);	// posted a blind this betting round?
	void RefundChipsForPlayer(int p_index);
	void RefundAllChips(void);

	int AllInOnCall(int p_index);	// return T/F if player all-in on a call
	int AllInOnRaise(int p_index);	// return T/F if player all-in on a raise

	int GetCallAmount(int);			// amount needed to call for this player
	int GetRaiseAmount(int);		// amount needed to raise for this player

	void Fold(int p_index);		// fold a player from the pot
	void Bet(int p_index);		// player bets
	void Call(int p_index);		// player calls
	void Check(int p_index);	// player checks
	void Raise(int p_index);	// player raises
	
	void ForceAllIn(int p_index);	// player is forced all in (call or check)

	void Bet(int p_index, int amount);			// bets a specific amount
	void BringIn(int p_index, int amount);		// player brings it in low
	void PostBlind(int p_index, int amount);	// player posts a blind
	void PostDead(int p_index, int amount);		// post a dead amount

	void SetFirstToAct(int p_index);	// tell the pot who is the first player to act
	void SetBetOverride(int chips_bet);	// override the bet amount
	int  GetBetOverride(void);			// return the override bet amount
	int  GetBetIncrement(void);			// return the current standard bet size
	int PotMatchAmount(void);			// what the pot is up to
	int BettingRoundIsFinished(void);	// T/F if betting round is finished
	int PotWasJustCapped(void);			// T/F if it's been capped since last we checked
	int PotIsCapped(void);				// T/F if we're capped
	int PlayerIsLiveForPot(int p_index, int pot_number);	// T/F if eligible for pot
	void PotHasCap(int cap_flag);		// set whether this pot has a cap or not
	void SetMaxRaisesToCap(int bets);	// set how many bets cap a betting round
	int PlayerJustWentAllIn(void);		// T/F is last player who acted went all-in
	int ClearChipsInFrontOfPlayers(void);	// clear chips in front of all players
	int IsPlaying(int p_index);	// T/F if this player is playing

	// sometimes we need to tell a betting round that the minimum betting increment is being
	// enforced, such as the pre-flop betting round in a tournament game where the blinds
	// must be matched
	void SetMinimumEnforcedBettingIncrement(int enforced_increment);

private:
	void ValidatePlayerIndex(int);	// make sure it's a valid index, DIE if not
	void EvaluateRake(void);		// evaluate what rake should be at (internal)
	void SetPlayerAllIn(int p_index);// set a player all-in (internal)
	void _bet(int p_index, int amount, int live_amount_flag, int force_all_in_flag);	// place a bet (internal)
	int _bet_increment;
	int _cap;

	int _pot[MAX_PLAYERS_PER_GAME];	// each individual pot
	int _total_pot;					// all pots summed add up to this
	int _pot_index;					// index into current pot

	int _player_count;	// how many players are in the pot?
	int _players_all_in;// how many players are all in?
	int _first_to_act;	// who will be first to act?
	int _last_round_total_pot;	// pot from last betting round
	int _rake;			// current rake
	int _max_rake;		// what's the max rake we can take?
	RakeType _rake_profile;	// what rake structure are we using?
	int _rakeable_players;		// base the rake on how many players?
	int _last_player_to_raise;	// last player to bet/raise
	int _bring_in_live_amount;// last bet was a bring-in
	int _chips_left[MAX_PLAYERS_PER_GAME];
	int _chips_got_back[MAX_PLAYERS_PER_GAME];
	int _chips_bet_total[MAX_PLAYERS_PER_GAME];
	int _chips_bet_this_round[MAX_PLAYERS_PER_GAME];
	int _total_pot_playing_for[MAX_PLAYERS_PER_GAME]; // total player can win
	int _playing_for_this_round[MAX_PLAYERS_PER_GAME];// total playing for this round
	int _live_for_pot[MAX_PLAYERS_PER_GAME][MAX_PLAYERS_PER_GAME];
	int	_raises_this_round;	// how many betting raises this betting round?
	int _bet_override_amount;	// a bet is usually the same as a raise, but not always
	int _max_raises_to_cap;	// how many bets cap this thing?
	// sometimes we need to tell a betting round that the minimum betting increment is being
	// enforced, such as the pre-flop betting round in a tournament game where the blinds
	// must be matched
	int _min_increment_this_betting_round;

	BOOL8 _acted_this_round[MAX_PLAYERS_PER_GAME];
	BOOL8 _posted_this_round[MAX_PLAYERS_PER_GAME];
	BOOL8 _playing[MAX_PLAYERS_PER_GAME];
	BOOL8 _folded[MAX_PLAYERS_PER_GAME];
	BOOL8 _winner[MAX_PLAYERS_PER_GAME];
	BOOL8 _all_in[MAX_PLAYERS_PER_GAME];
	BOOL8 _all_in_on_call[MAX_PLAYERS_PER_GAME];
	BOOL8 _all_in_on_raise[MAX_PLAYERS_PER_GAME];
	BOOL8 _forced_all_in_this_round[MAX_PLAYERS_PER_GAME];
	BOOL8 _player_just_went_all_in;
	BOOL8 _capped;			// pot is capped
	BOOL8 _pot_has_cap;		// some pots don't have a cap (head to head)
	BOOL8 _pot_was_capped;	// was capped last time we checked?
};

#endif // !_POT_H_INCLUDED
