/**********************************************************************************
 Functions for robot players -- HOLD'EM, OMAHA & SEVEN-CARD STUD
 Date: 20180707 kriskoin : **********************************************************************************/

#define DISP 0

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "pplib.h"
#include "gamedata.h"
#include "hand.h"
#include "poker.h"

#define LOOSE_PLAYERS	0	// define to 1 for robot players that play much looser
#define ALWAYS_FOLD		0	// always fold if it's an option, otherwise fall to logic

/**********************************************************************************
 Function SameSuited(int player, GamePlayerData *gpd)
 date: kriskoin 2019/01/01 Purpose: just tell us what the maximum num of cards of our best suit
***********************************************************************************/
int SameSuited(int player, GamePlayerData *gpd)
{
	int suit_count[4];
	suit_count[3] = suit_count[2] = suit_count[1] = suit_count[0] = 0;
	// count from the flop
	for (int i=0; i < MAX_PUBLIC_CARDS; i++) {
		if (gpd->common_cards[i] != CARD_NO_CARD) {
			suit_count[SUIT(gpd->common_cards[i])]++;
		}
	}
	// pocket cards
	if (gpd->cards[player][0] != CARD_NO_CARD) {
		suit_count[SUIT(gpd->cards[player][0])]++;
	}
	if (gpd->cards[player][1] != CARD_NO_CARD) {
		suit_count[SUIT(gpd->cards[player][1])]++;
	}
	return max(max(max(suit_count[3], suit_count[2]), suit_count[1]), suit_count[0]);
}

/**********************************************************************************
 Function ChooseFromThree(int line, int r1, int w1, int r2, int w2, int r3, int w3)
 date: kriskoin 2019/01/01 Purpose: given three actions and their weighted chances, pick one randomly
		  this can also be used to just pick numbers from weightings
***********************************************************************************/
int ChooseFromThree(int line, int r1, int w1, int r2, int w2, int r3, int w3)
{
	// given three event results (r1,r2,r3), randomly pick one based on their respective
	// weightings (w1,w2,w3).  weightings must add up to 100
	if (w1+w2+w3 !=100) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) call to REFTC (from line %d) adds up to %d\n", _FL, line, w1+w2+w3);
		return w1;	// return first as error default
	}
	// we'll make a big 100 element array and fill it, then choose from it... this is so that
	// we can plug wierd events ourselves in there
	int choice_array[100];
	int i;
	for (i=0; i < w1; i++) {
		choice_array[i] = r1;
	}
	for (i = w1; i < w1+w2; i++) {
		choice_array[i] = r2;
	}
	for (i = w1+w2; i < w1+w2+w3; i++) {
		choice_array[i] = r3;
	}
	// 0-99 have been filled
	return choice_array[rand() % 100];
}

/**********************************************************************************
 Function ChooseActionFromConfidence(int confidence, WORD32 actions)
 date: kriskoin 2019/01/01 Purpose: given a confidence level, choose the best valid action, or, randomly,
          a little worse
***********************************************************************************/
int ChooseActionFromConfidence(int confidence, WORD32 actions)
{
	int action_picked = ACT_FOLD;
	// build up to the highest we can
	if (confidence > 19 && GET_ACTION(actions, ACT_CHECK)) {
		action_picked = ACT_CHECK;
	}
	if (confidence > 29 && GET_ACTION(actions, ACT_CALL_ALL_IN)) {
		action_picked = ACT_CALL_ALL_IN;
	}
	if (confidence > 39 && GET_ACTION(actions, ACT_CALL)) {
		action_picked = ACT_CALL;
	}
	if (confidence > 49 && GET_ACTION(actions, ACT_BET_ALL_IN)) {
		action_picked = ACT_BET_ALL_IN;
	}
	if (confidence > 59 && GET_ACTION(actions, ACT_BET)) {
		action_picked = ACT_BET;
	}

	if (confidence > 69 && GET_ACTION(actions, ACT_RAISE_ALL_IN)) {
		action_picked = ACT_RAISE_ALL_IN;
	}
	if (confidence > 79 && GET_ACTION(actions, ACT_RAISE)) {
		action_picked = ACT_RAISE;
	}
	return action_picked;
}

/**********************************************************************************
 Function ChooseFirstValidAction(WORD32 actions);
 date: kriskoin 2019/01/01 Purpose: from a valid list of actions, just pick the first one
***********************************************************************************/
int ChooseFirstValidAction(WORD32 actions)
{
	for (int i=0; i < 32; i++) {
		if (GET_ACTION(actions, i)) {	// found one
			return i;
		}
	}
	return ACT_NO_ACTION;	// didn't find one
}
//****************************************************************
// https://github.com/kriskoin//
// Ask the game module to evaluate using CommonData, PlayerData[seating_position],
// and GPIRequest to fill in GPIResult.  Computer logic should not have
// access to any other variables.
ErrorType EvalComputerizedPlayerInput(struct GameCommonData *gcd, struct GamePlayerData *gpd,
	struct GamePlayerInputRequest *gpir, struct GamePlayerInputResult *result)
{
	// First, some sanity checks...
	if (!gcd) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) EvalComputerizedPlayerInput called with null GCD",_FL);
		return ERR_INTERNAL_ERROR;
	}

	if (!gpir->ready_to_process) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) EvalComputerizedPlayerInput called with no input request",_FL);
		return ERR_INTERNAL_ERROR;
	}

	if (!gpir->action_mask) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) No valid actions in EvalComputerizedPlayerInput input request",_FL);
		return ERR_INTERNAL_ERROR;
	}
  #if LOOSE_PLAYERS
	kp1(("%s(%d) NOTE: LOOSE_PLAYERS has been turned at the top of %s\n", _FL, __FILE__));
  #endif

	Poker *poker = NULL;
	Hand *hand = NULL;

	int action_chosen = ACT_NO_ACTION;
	WORD32 actions = gpir->action_mask;

	// we want to build a confidence level of the hand -- then map the most suitable
	// action to it that we can
	int player = gpir->seating_position;
	int same_suited = SameSuited(player, gpd); // how many of our best suit?

  #if ALWAYS_FOLD
	kp1(("%s(%d) ALWAYS_FOLD is set to 1.\n", _FL));
	if (GET_ACTION(actions, ACT_FOLD)) {
		action_chosen = ACT_FOLD;
		goto action_selected;
	}
  #endif
	
	// we'll keep trying to assign an action till we come up with something


	/* SHOW/MUCK/TOSS HAND AFTER GAME */
//  #if LOOSE_PLAYERS
  #if 0	// don't show often
	if (GET_ACTION(actions, ACT_SHOW_HAND) && !GET_ACTION(actions, ACT_MUCK_HAND) && !GET_ACTION(actions, ACT_TOSS_HAND) ) {
		// if he has no choice but to show his hand, show it
		action_chosen = ACT_SHOW_HAND;
	} else if (GET_ACTION(actions, ACT_TOSS_HAND)) {	// asked to show or not after being solo winner
		// always show
		action_chosen = ChooseFromThree(__LINE__,ACT_SHOW_HAND, 99, ACT_TOSS_HAND, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_MUCK_HAND)) {	// asked to show or not when losing
		// always show
		action_chosen = ChooseFromThree(__LINE__,ACT_SHOW_HAND, 99, ACT_MUCK_HAND, 1, ACT_NO_ACTION, 0);
	}
  #else
	if (GET_ACTION(actions, ACT_SHOW_HAND) && !GET_ACTION(actions, ACT_MUCK_HAND) && !GET_ACTION(actions, ACT_TOSS_HAND) ) {
		// if he has no choice but to show his hand, show it
		action_chosen = ACT_SHOW_HAND;
	} else if (GET_ACTION(actions, ACT_TOSS_HAND)) {	// asked to show or not after being solo winner
		// for now, rarely show
		action_chosen = ChooseFromThree(__LINE__,ACT_SHOW_HAND, 10, ACT_TOSS_HAND, 90, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_MUCK_HAND)) {	// asked to show or not when losing
		// for now, rarely show
		action_chosen = ChooseFromThree(__LINE__,ACT_SHOW_HAND, 8, ACT_MUCK_HAND, 92, ACT_NO_ACTION, 0);
	}
  #endif
	if (action_chosen != ACT_NO_ACTION) goto action_selected;

#if 0	// adate: robots ALWAYS POST EVERYTHING -- no sitting out
	/* POSTS/BLINDS/ETC */
	if (GET_ACTION(actions, ACT_POST)) {	// asked to post as new
		action_chosen = ChooseFromThree(__LINE__,ACT_POST, 99, ACT_SIT_OUT_POST, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_POST_ANTE)) {		// asked to post ante
		action_chosen = ChooseFromThree(__LINE__,ACT_POST_ANTE, 99, ACT_SIT_OUT_ANTE, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_POST_SB)) {		// asked to post sb
		action_chosen = ChooseFromThree(__LINE__,ACT_POST_SB, 99, ACT_SIT_OUT_SB, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_POST_BB)) {		// asked to post bb
		action_chosen = ChooseFromThree(__LINE__,ACT_POST_BB, 99, ACT_SIT_OUT_BB, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_POST_BOTH)) {	// asked to post both
		action_chosen = ChooseFromThree(__LINE__,ACT_POST_BOTH, 99, ACT_SIT_OUT_BOTH, 1, ACT_NO_ACTION, 0);
	} else if (GET_ACTION(actions, ACT_BRING_IN)) {		// asked to bring in Stud7 action
		action_chosen = ChooseFromThree(__LINE__,ACT_BRING_IN, 75, ACT_BET, 25, ACT_NO_ACTION, 0);
	}
#else
	if (GET_ACTION(actions, ACT_POST)) {	// asked to post as new
		action_chosen = ACT_POST;
	} else if (GET_ACTION(actions, ACT_POST_ANTE)) {		// asked to post ante
		action_chosen = ACT_POST_ANTE;
	} else if (GET_ACTION(actions, ACT_POST_SB)) {		// asked to post sb
		action_chosen = ACT_POST_SB;
	} else if (GET_ACTION(actions, ACT_POST_BB)) {		// asked to post bb
		action_chosen = ACT_POST_BB;
	} else if (GET_ACTION(actions, ACT_POST_BOTH)) {	// asked to post both
		action_chosen = ACT_POST_BOTH;
	} else if (GET_ACTION(actions, ACT_BRING_IN)) {		// asked to bring in Stud7 action
		action_chosen = ACT_BRING_IN;
	}
#endif
	
	if (action_chosen != ACT_NO_ACTION) goto action_selected;

	/* BETTING DECISION  -- OMAHA/HOLDEM */
	if (gcd->game_rules == GAME_RULES_HOLDEM || 
		gcd->game_rules == GAME_RULES_OMAHA_HI ||
		gcd->game_rules == GAME_RULES_OMAHA_HI_LO)
	{
		if (gpd->common_cards[4] != CARD_NO_CARD) {	// river has been dealt
		/* RIVER HAS BEEN DEALT */
		// sanity check -- river dealt -- do we have pocket cards?
			// build a hand to evaluate
			hand = new Hand;
			// add the flop
			hand->Add(gpd->common_cards[0]);
			hand->Add(gpd->common_cards[1]);
			hand->Add(gpd->common_cards[2]);
			hand->Add(gpd->common_cards[3]);
			hand->Add(gpd->common_cards[4]);
			// add the pockets
			hand->Add(gpd->cards[player][0]);
			hand->Add(gpd->cards[player][1]);
			poker = new Poker;
			poker->EvaluateHand(hand);
			int hand_type = hand->GetValue(0);
		  #if LOOSE_PLAYERS
			if (hand_type >= THREEOFAKIND) {	// anything trips or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
			} else if (hand_type >= TWOPAIR) {	// stay in
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 85, 20, 1), actions);
			} else {
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 4, 60, 91, 20, 5), actions);
			}
		  #else
			if (hand_type >= THREEOFAKIND) {	// anything trips or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 45, 60, 54, 20, 1), actions);
			} else if (hand_type >= TWOPAIR) {	// stay in
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 84, 20, 2), actions);
			} else {
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 4, 60, 51, 20, 45), actions);
			}
		  #endif
		} else if (gpd->common_cards[3] != CARD_NO_CARD) { // turn has been dealt
			/* TURN HAS BEEN DEALT */
			// sanity check -- turn dealt -- do we have pocket cards?
			// build a hand to evaluate
			hand = new Hand;
			// add the flop
			hand->Add(gpd->common_cards[0]);
			hand->Add(gpd->common_cards[1]);
			hand->Add(gpd->common_cards[2]);
			hand->Add(gpd->common_cards[3]);
			// add the pockets
			hand->Add(gpd->cards[player][0]);
			hand->Add(gpd->cards[player][1]);
			poker = new Poker;
			poker->EvaluateHand(hand);
			int hand_type = hand->GetValue(0);
		  #if LOOSE_PLAYERS
			if (hand_type >= THREEOFAKIND) {	// anything trips or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
			} else if (hand_type >= TWOPAIR) {	// stay in for now with a pair
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 84, 20, 2), actions);
			} else if (same_suited >= 4) {	// lame flush draw
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 84, 20, 2), actions);
			} else {
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 4, 60, 91, 20, 5), actions);
			}
		  #else
			if (hand_type >= THREEOFAKIND) {	// anything trips or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
			} else if (hand_type >= TWOPAIR) {	// stay in for now with a pair
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 64, 20, 22), actions);
			} else if (same_suited >= 4) {	// lame flush draw
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 14, 60, 60, 20, 26), actions);
			} else {
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 4, 60, 35, 20, 61), actions);
			}
		  #endif
		} else if (gpd->common_cards[2] != CARD_NO_CARD) { // flop has been dealt
			/* FLOP HAS BEEN DEALT */
			// build a hand to evaluate
			hand = new Hand;
			// add the flop
			hand->Add(gpd->common_cards[0]);
			hand->Add(gpd->common_cards[1]);
			hand->Add(gpd->common_cards[2]);
			// add the pockets
			hand->Add(gpd->cards[player][0]);
			hand->Add(gpd->cards[player][1]);
			poker = new Poker;
			poker->EvaluateHand(hand);
			int hand_type = hand->GetValue(0);
		  #if LOOSE_PLAYERS
			if (hand_type >= TWOPAIR) {	// anything 2 pairs or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
			} else if (hand_type >= PAIR) {	// stay in for now with a pair
				action_chosen = ChooseActionFromConfidence(60, actions);
			} else if (same_suited >= 4) {	// lame flush draw
				action_chosen = ChooseActionFromConfidence(40, actions);
			} else {
				action_chosen = ChooseActionFromConfidence(20, actions);
			}
		  #else
			if (hand_type >= TWOPAIR) {	// anything 2 pairs or better, raise if possible
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 65, 60, 34, 20, 1), actions);
			} else if (hand_type >= PAIR) {	// stay in for now with a pair
				action_chosen = ChooseActionFromConfidence(60, actions);
			} else if (same_suited >= 4) {	// lame flush draw
				action_chosen = ChooseActionFromConfidence(40, actions);
			} else {
				action_chosen = ChooseActionFromConfidence(20, actions);
			}
		  #endif
		} else if (gpd->cards[player][1] != CARD_NO_CARD) { // pockets have been dealt
			/* ONLY POCKETS HAVE BEEN DEALT */
			Card c1 = gpd->cards[player][0];
			Card c2 = gpd->cards[player][1];
			// depending on our pocket cards, we'll pick a confidence and just go with it
			// pairs are the best -- bet max
		  #if LOOSE_PLAYERS
			if (RANK(c1) == RANK(c2)) { // pair
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
			} else if (SUIT(c1) == SUIT(c2)) { // same suit
			// same suit would be very good... especially if one is high
				if (RANK(c1) > Queen || RANK(c2) > Queen) {
					action_chosen =
						ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 85, 60, 14, 20, 1), actions);
				} else {
					action_chosen =
						ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 45, 60, 54, 20, 1), actions);
				}
				// is one of them high at least?
			} else if (RANK(c1) > Ten || RANK(c2) > Ten) {
				action_chosen = ChooseActionFromConfidence(40, actions);
			// if they're next to each other, call
			} else if (abs(RANK(c1)-RANK(c2)) < 2) {
				action_chosen = ChooseActionFromConfidence(40, actions);
			} else {	// total crap -- check at best.. or fold
				action_chosen = ChooseActionFromConfidence(20, actions);
			}
		  #else
			if (RANK(c1) == RANK(c2)) { // pair
				action_chosen =
					ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 75, 60, 24, 20, 1), actions);
			} else if (SUIT(c1) == SUIT(c2)) { // same suit
			// same suit would be very good... especially if one is high
				if (RANK(c1) > Queen || RANK(c2) > Queen) {
					action_chosen =
						ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 75, 60, 24, 20, 1), actions);
				} else {
					action_chosen =
						ChooseActionFromConfidence(ChooseFromThree(__LINE__,90, 35, 60, 64, 20, 1), actions);
				}
				// is one of them high at least?
			} else if (RANK(c1) > Ten || RANK(c2) > Ten) {
				action_chosen = ChooseActionFromConfidence(30, actions);
			// if they're next to each other, call
			} else if (abs(RANK(c1)-RANK(c2)) < 2) {
				action_chosen = ChooseActionFromConfidence(30, actions);
			} else {	// total crap -- check at best.. or fold
				action_chosen = ChooseActionFromConfidence(20, actions);
			}
		  #endif
		}
	}
	if (action_chosen != ACT_NO_ACTION) goto action_selected;

	/* BETTING DECISION  -- 7-CARD STUD */
	if (gcd->game_rules == GAME_RULES_STUD7 || gcd->game_rules==GAME_RULES_STUD7_HI_LO) {
	  #if 0 // enable this to see the common river card
		action_chosen = ChooseActionFromConfidence(90, actions);
		goto action_selected;
	  #endif

		// 5, 6, or 7 seven dealt?
		if (gpd->cards[player][4] != CARD_NO_CARD) {
			// build a hand to evaluate
			hand = new Hand;
			// add all cards
			hand->Add(gpd->cards[player][0]);
			hand->Add(gpd->cards[player][1]);
			hand->Add(gpd->cards[player][2]);
			hand->Add(gpd->cards[player][3]);
			hand->Add(gpd->cards[player][4]);
			if (gpd->cards[player][5] != CARD_NO_CARD) hand->Add(gpd->cards[player][5]);
			if (gpd->cards[player][6] != CARD_NO_CARD) hand->Add(gpd->cards[player][6]);
			// there may be a common card if the game is really full
			if (gpd->common_cards[0] != CARD_NO_CARD) hand->Add(gpd->common_cards[0]);

			poker = new Poker;
			poker->EvaluateHand(hand);
			int hand_type = hand->GetValue(0);
			if (hand_type >= THREEOFAKIND) {	// anything trips or better, raise if possible
				action_chosen = ChooseActionFromConfidence(90, actions);
			} else if (hand_type >= TWOPAIR) {	// stay in
				action_chosen = ChooseActionFromConfidence(60, actions);// bet all in
			} else if (hand_type >= PAIR) {	// stay in
				action_chosen = ChooseActionFromConfidence(60, actions);// bet all in
			} else if (same_suited >= 4) {	// lame flush draw
				action_chosen = ChooseActionFromConfidence(55, actions);
			} else {
				action_chosen = ChooseActionFromConfidence(35, actions);// call all in !! 35
			}
		} else if (gpd->cards[player][2] != CARD_NO_CARD) { // 3 or 4 cards out
			// let's get everyone to stay after the first 3 for now
			if (gpd->cards[player][3] == CARD_NO_CARD) {	// only 3 cards (0,1,2)
				action_chosen = ChooseActionFromConfidence(60, actions);
				goto action_selected;
			}
			int card_values[CARD_RANKS];
			int rank_index;
			for (rank_index=0; rank_index < CARD_RANKS; rank_index++) {
				card_values[rank_index] = 0;
			}
			card_values[RANK(gpd->cards[player][0])]++;
			card_values[RANK(gpd->cards[player][1])]++;
			card_values[RANK(gpd->cards[player][2])]++;
			if (gpd->cards[player][3] != CARD_NO_CARD)
				card_values[RANK(gpd->cards[player][3])]++;
			int high_total = 0;
			// find highest occurrence
			for (rank_index=0; rank_index < CARD_RANKS; rank_index++) {
				high_total = max(card_values[rank_index], high_total);
			}
			if (high_total > 1) {	// anything pair or better, raise if possible
				action_chosen = ChooseActionFromConfidence(90, actions);
			} else if (same_suited >= 3) {	// flush draw
				action_chosen = ChooseActionFromConfidence(65, actions);
			} else if (same_suited >= 2) {	// lame flush draw
				action_chosen = ChooseActionFromConfidence(45, actions);
			} else {
				action_chosen = ChooseActionFromConfidence(35, actions);
			}
		}
	}
	if (action_chosen != ACT_NO_ACTION) goto action_selected;

  #if 0	// we don't care what gets picked at this point -- just pick a valid action
	Error(ERR_WARNING,"Unknown action state in computer logic (mask %x)", actions);
  #endif
	action_chosen = ChooseFirstValidAction(actions); // just pick something valid

action_selected:

	if (poker) delete poker;
	if (hand) delete hand;

	// Now that we've picked an answer, fill in the input result.
	zstruct(*result);
	result->game_serial_number			= gpir->game_serial_number;
	result->table_serial_number			= gpir->table_serial_number;
	result->input_request_serial_number	= gpir->input_request_serial_number;
	result->seating_position			= gpir->seating_position;
	result->action = (BYTE8)action_chosen;
	result->ready_to_process = TRUE;
	return ERR_NONE;
}
