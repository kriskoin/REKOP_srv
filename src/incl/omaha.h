/**********************************************************************************
 CLASS Omaha
 Date: 20180707 kriskoin :  "Omaha" is an object defining the rules and gameplay of Omaha
 Note:  This is so similar to Holdem that we just inherit the entire holdem object
 and implement minor differences in the logic based on _game_rules
***********************************************************************************/
#ifndef _OMAHA_H_INCLUDED
#define _OMAHA_H_INCLUDED

#include "commonro.h"
#include "holdem.h"

#define MAX_NUMBER_OF_OMAHA_PLAYERS	10	// our Omaha tables only fit 10

class Omaha : public Holdem {	// most of Omaha is identical to Hold'em

public:
	Omaha(void);
	~Omaha(void);
	
	// InitializeGame(how many players, ptr to Game, small blind, big blind)
	void InitializeGame(WORD32 serial_num, int number_players, int sb, int bb,
			RakeType rake_profile, void *table_ptr, struct GameCommonData *gcd, ChipType chip_type);

};
#endif // !_OMAHA_H_INCLUDED
