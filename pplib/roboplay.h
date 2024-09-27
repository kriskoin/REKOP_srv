#include "gamedata.h"


// roboplay function definitions -- documented in roboplay.cpp
int ChooseActionFromConfidence(int confidence, WORD32 actions);
int ChooseFirstValidAction(WORD32 actions);
int SameSuited(int player, GamePlayerData *gpd);
int ChooseFromThree(int r1, int w1, int r2, int w2, int r3, int w3);
ErrorType EvalComputerizedPlayerInput(struct GameCommonData *gcd, struct GamePlayerData *gpd,
	struct GamePlayerInputRequest *gpir, struct GamePlayerInputResult *result);
