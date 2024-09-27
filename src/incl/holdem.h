/**********************************************************************************
 CLASS Holdem
 Date: 20180707 kriskoin :  "Holdem" is an object defining the rules and gameplay of Texas Hold'em
***********************************************************************************/
#ifndef _HOLDEM_H_INCLUDED
#define _HOLDEM_H_INCLUDED

#include "commonro.h"

#define MAX_NUMBER_OF_HOLDEM_PLAYERS	10	// our Hold'em tables only fit 10

class Holdem : public CommonRules {

	ErrorType EvalComputerizedPlayerInput(struct GameCommonData *gcd, struct GamePlayerData *gpd,
		struct GamePlayerInputRequest *gpir, struct GamePlayerInputResult *result);


public:
	Holdem(void);
	~Holdem(void);

	// InitializeGame(how many players, ptr to Game, small blind, big blind)
	void InitializeGame(WORD32 serial_num, int number_players, int sb, int bb,
			RakeType rake_profile, void *table_ptr, struct GameCommonData *gcd, ChipType chip_type);
	void SetGameRules(GameRules game_rules);// override game rules
	void StartGame(BYTE8 button);			// start the game
	void PlayOn(int *work_was_done_flag);	// move the game along to the next stage

	// Ask the game module to evaluate using CommonData, PlayerData[seating_position],
	// and GPIRequest to fill in GPIResult.  Computer logic should not have
	// access to any other variables.


private:
	// private member functions unique to Hold'em
	void SetButton(BYTE8 player_number);		// this player is the "button"
	int  EvaluateGame(int *work_was_done_flag);	// game evaluation loop
	int  HandleActions(void);					// handle user responses
	int  MoveButton;							// T/F move button next game
};
#endif // !_HOLDEM_H_INCLUDED

