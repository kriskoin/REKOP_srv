/**********************************************************************************
 Member functions for Omaha object
 Date: 20180707 kriskoin :  Note: Most is inherited from the Hold'em object
**********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#include <stdlib.h>
#include "omaha.h"

/**********************************************************************************
 Function Omaha::Omaha();
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Omaha::Omaha(void)
{
}

/**********************************************************************************
 Function Omaha::~Omaha()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Omaha::~Omaha(void)
{
}

/**********************************************************************************
 Function Omaha::InitializeGame
 Date: 20180707 kriskoin :  Purpose: start a game from scratch, and let us know the Game that's running it
***********************************************************************************/
void Omaha::InitializeGame(WORD32 serial_number, int max_number_of_players, 
			int sb, int bb, RakeType rake_profile, void *table_ptr, struct GameCommonData *gcd, ChipType chip_type)
{
	if (max_number_of_players > MAX_NUMBER_OF_OMAHA_PLAYERS) {
		Error(ERR_FATAL_ERROR,"%s(%d) bad parameters fed to InitializeGame(%d (max %d), %d, %d)", 
			_FL, max_number_of_players, MAX_NUMBER_OF_OMAHA_PLAYERS, sb, bb);
		DIE("Too many players in InitializeGame");
	}	
	_chip_type = chip_type;
	_game_rules = GAME_RULES_OMAHA_HI;								// rule set to use
	_max_this_game_players = max_number_of_players;		// max players allowed to be playing
	_table_ptr = table_ptr;
	_rake_profile = rake_profile;
	CommonGameInit(serial_number, sb, bb, gcd);
}

