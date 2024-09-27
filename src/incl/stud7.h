/**********************************************************************************
 CLASS Stud7
 Date: 20180707 kriskoin :  "Stud7" is an object defining the rules and gameplay of Stud7
***********************************************************************************/
#ifndef _STUD7_H_INCLUDED
#define _STUD7_H_INCLUDED

#include "commonro.h"

#define MAX_NUMBER_OF_STUD7_PLAYERS	8	// our stud7 tables only fit 8

// used to pass/return info about a partial hand evaluated to kick off betting rounds

typedef struct HighHandEval {
	// we'll pass the hands in as well
	Card test_card[4];	// max 4 upcards
	// rest are output variables
	int hand_type;
	int hand_value;
	int kicker[3];	// max 3 kickers
} HighHandEval;

class Stud7 : public CommonRules {

	ErrorType EvalComputerizedPlayerInput(struct GameCommonData *gcd, struct GamePlayerData *gpd, 
		struct GamePlayerInputRequest *gpir, struct GamePlayerInputResult *result);

public:
	Stud7(void);
	~Stud7(void);
	
	// InitializeGame(how many players, ptr to Game, small blind, big blind)
	void InitializeGame(WORD32 serial_num, int number_players, int sb, int bb,
			RakeType rake_profile, void *table_ptr, struct GameCommonData *gcd, ChipType chip_type);
	void SetGameRules(GameRules game_rules);// override game rules
	void StartGame(void);					// start the game
	void PlayOn(int *work_was_done_flag);	// move the game along to the next stage

	// Ask the game module to evaluate using CommonData, PlayerData[seating_position],
	// and GPIRequest to fill in GPIResult.  Computer logic should not have
	// access to any other variables.

private:
	// private member functions unique to Stud7
	int  EvaluateGame(int *work_was_done_flag);	// game evaluation loop
	int  HandleActions(void);					// handle user responses
	BYTE8 GetPlayerWithLowestDoorCard(void);	// player with low card to open
	BYTE8 GetPlayerWithBestUpCards(void);	// used to start middle betting rounds
	// HighHandEval is called by the 3 functions above
	void HighHandEvaluate(HighHandEval *hhe);
	// used by function above to find better of two hhe structs
	WinningHand FindBetterHandEvalStruct(struct HighHandEval *hhe_1, struct HighHandEval *hhe_2);
	// Stud7 has wierd quirks in its first two betting rounds
	void RequestNextMoveFromPlayerRound2(BYTE8 p_index);
	void Shuffle7csHoleCards(BYTE8 player_index);
	int player_is_opening_bettor;	// T/F, used for quirks in Stud7 betting
	int	_bring_in_player;			// keep track of who brought it in
	int round_has_been_raised;		// T/F, used for quirks in Stud7 betting
	int pair_on_board;				// T/F, used for quirks in Stud7 betting
};
#endif // !_STUD7_H_INCLUDED

