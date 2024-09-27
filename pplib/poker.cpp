/**********************************************************************************
 Member functions for Poker object
 Date: 20180707 kriskoin : **********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "poker.h"
#include "hand.h"

extern char cRanks[];	// "23456789TJQKA"

/**********************************************************************************
 Function Poker::Poker();
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Poker::Poker(void)
{
}

/**********************************************************************************
 Function Poker::~Poker()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Poker::~Poker(void)
{
	// nothing to do for now
}

/************************************************************************************
 Function ::FindBestHand(Hand pocket_hand, Hand flop_hand)
 Date: 20180707 kriskoin :  Purpose: given a pocket and a flop, find the best hand and return it in the hand_out
*************************************************************************************/
void Poker::FindBestHand(GameRules rule_set, Hand pocket_hand, Hand flop_hand, Hand *high_hand_out, Hand *low_hand_out)
{
	Hand test_hand, best_high_hand, best_low_hand;	// all evaluations use these -- and eventually return best_hand
	switch (rule_set) {

		case GAME_RULES_STUD7:
		case GAME_RULES_STUD7_HI_LO:
			// it is possible to have a common card dealt if we're running low on cards
			// we expect: 6 pocket + 1 common
			if (flop_hand.CardCount() == 1 && pocket_hand.CardCount() == 6) {
				test_hand = pocket_hand;				// the player's six
				test_hand.Add(flop_hand.GetCard(0));	// plus the river
			} else if (flop_hand.CardCount() == 0 && pocket_hand.CardCount() > 4) {
				// we expect: 5 or more cards in the pocket and 0 on the flop
				test_hand = pocket_hand;				// the player's six
			} else {	// we got garbage
				Error(ERR_FATAL_ERROR, "pocket = %d, flop = %d",
					pocket_hand.CardCount(), flop_hand.CardCount());
				break;
			}
			if (test_hand.CardCount() == 5) {
				best_high_hand = test_hand;
				best_low_hand = test_hand;
				EvaluatePokerHand(&best_high_hand, TRUE);
				EvaluatePokerHand(&best_low_hand, FALSE);
			} else if (test_hand.CardCount() == 6) {
				FindBestHandFrom6Cards(test_hand, &best_high_hand, &best_low_hand);
			} else if (test_hand.CardCount() == 7) {
				FindBestHandFrom7Cards(test_hand, &best_high_hand, &best_low_hand);
			} else {
				Error(ERR_INTERNAL_ERROR,"%s(%d) Unable to evaluate a hand with %d cards\n",
					_FL, test_hand.CardCount());
			}

			//memcpy(&best_hand, &test_hand, sizeof(Hand));
			// test them all
			// FindBestHandFrom7Cards(test_hand, &best_hand);
			break;

		case GAME_RULES_HOLDEM:
			// we expect: 2 cards in the pocket and 3 or more on the flop
			if (pocket_hand.CardCount() != 2 || flop_hand.CardCount() < 3) {
				Error(ERR_FATAL_ERROR, "pocket = %d, flop = %d",
					pocket_hand.CardCount(), flop_hand.CardCount());
				break;
			}
			// set up the test hand (flop + pockets cards)
			// first set the flop...
			int card_index;
			for (card_index = 0; card_index < flop_hand.CardCount(); card_index++) {
				test_hand.Add(flop_hand.GetCard(card_index));
			}
			// set the two pocket cards
			test_hand.Add(pocket_hand.GetCard(0));
			test_hand.Add(pocket_hand.GetCard(1));
			// and find the best...
			if (test_hand.CardCount() == 5) {
				best_high_hand = test_hand;
				best_low_hand = test_hand;
				EvaluatePokerHand(&best_high_hand, TRUE);
				EvaluatePokerHand(&best_low_hand, FALSE);
			} else if (test_hand.CardCount() == 6) {
				FindBestHandFrom6Cards(test_hand, &best_high_hand, &best_low_hand);
			} else if (test_hand.CardCount() == 7) {
				FindBestHandFrom7Cards(test_hand, &best_high_hand, &best_low_hand);
			} else {
				Error(ERR_INTERNAL_ERROR,"%s(%d) Unable to evaluate a hand with %d cards\n",
					_FL, test_hand.CardCount());
			}
			//EvaluateHand(&test_hand);
			//memcpy(&best_hand, &test_hand, sizeof(Hand));
			// FindBestHandFrom7Cards(test_hand, &best_hand);
			break;

		case GAME_RULES_OMAHA_HI:
		case GAME_RULES_OMAHA_HI_LO:
			// hand validation happens in FindBestOmahaHand()
			FindBestOmahaHand(pocket_hand, flop_hand, &best_high_hand, &best_low_hand);
			break;

		default: 
			Error(ERR_INTERNAL_ERROR,"%s(%d) Unable to evaluate a hand with %d cards\n",
				_FL, test_hand.CardCount());

	}
	if (high_hand_out) {
		memcpy(high_hand_out, &best_high_hand, sizeof(Hand));	// copy the best hand to output hand
	}
	if (low_hand_out) {
		memcpy(low_hand_out, &best_low_hand, sizeof(Hand));	// copy the best hand to output hand
	}
}

/**********************************************************************************
 Function Poker::EvaluateHand(Hand *test_hand)
 date: kriskoin 2019/01/01 Purpose: let an outsider evalute a poker hand
***********************************************************************************/
void Poker::EvaluateHand(Hand *high_hand)
{
	EvaluateHand(high_hand, NULL);
}

void Poker::EvaluateHand(Hand *high_hand, Hand *low_hand)
{
	Hand temp_high_hand = *high_hand;
	Hand temp_low_hand;
	if (low_hand) {
		temp_low_hand = *low_hand;
	}
	switch (temp_high_hand.CardCount()) {
		case 5 :
			EvaluatePokerHand(&temp_high_hand, TRUE);
			if (low_hand) {
				EvaluatePokerHand(&temp_low_hand, FALSE);
			}
			break;
		case 6 :
			FindBestHandFrom6Cards(*high_hand, &temp_high_hand, &temp_low_hand);
			break;
		case 7 :
			FindBestHandFrom7Cards(*high_hand, &temp_high_hand, &temp_low_hand);
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"Unable to evaluate a hand with %d cards\n",
				temp_high_hand.CardCount());
			return;
	}
	// set the test hand's evaluation values
	for (int i=0; i < HAND_VALUE_SIZE; i++) {
		high_hand->SetValue(i, temp_high_hand.GetValue(i));
		if (low_hand) {
			low_hand->SetValue(i, temp_low_hand.GetValue(i));
		}
	}
	high_hand->SetEvalState(TRUE);	// let the hand know it's in a valid evaluated state
	if (low_hand) {
		low_hand->SetEvalState(TRUE);	// let the hand know it's in a valid evaluated state
	}
}

/**********************************************************************************
 Function Poker::EvaluatePokerHand(Hand test_hand)
 Date: 20180707 kriskoin :  Purpose: at the lowest level, we want to evaluate and grade a proper 5-card hand
 24/01/01 kriskoin:
***********************************************************************************/
void Poker::EvaluatePokerHand(Hand *test_hand, int high_hand_flag)
{
	// this is purely an internal function -- the lowest level of evaluation
	// both functions called here assume we're feeding a proper 5-card hand
	// if we're not, we should trap it now
	if (test_hand->CardCount() !=5) {
		Error(ERR_FATAL_ERROR,"EvaluatePokerHand was fed a %d-card hand (must be 5)", test_hand->CardCount());
		return;
	}
	Hand internal_test_hand;	// this one is sortable for sure
	for (int i=0; i < test_hand->CardCount(); i++) {
		internal_test_hand.Add(test_hand->GetCard(i));
	}
	// ok, run both tests -- both will internally fill the value array of the hand
	SetTestHandType(&internal_test_hand, high_hand_flag);
	SetTestHandValue(&internal_test_hand, high_hand_flag);
	internal_test_hand.SetEvalState(TRUE);	// let the hand know it's in a valid evaluated state
	memcpy(test_hand, &internal_test_hand, sizeof(Hand));	// copy it to the output hand
}

/**********************************************************************************
 Function Poker::SetTestHandType(TestHand *test_hand)
 Date: 20180707 kriskoin :  Purpose: given a test hand, we just want to evaluate what sort of hand it is
 24/01/01 kriskoin:
***********************************************************************************/
void Poker::SetTestHandType(Hand *test_hand, int high_hand_flag)
{

	#define T_RANK(c)    (RANK(test_hand->GetCard(c)))
	#define T_SUIT(c)    (SUIT(test_hand->GetCard(c)))
	#define S_VALUE(s,v) (test_hand->SetValue((Card)(s),(Card)(v)))

	int fFlush = FALSE;
	int fStraight = FALSE;
	// cards are already sorted by rank, so just start at the first card and work up

	if (high_hand_flag) {	// ignore straights and flushes for low
		// first of all, is this a type of straight?
		fStraight = (  (T_RANK(1) == T_RANK(0) + 1 && T_RANK(2) == T_RANK(1) + 1 &&
						T_RANK(3) == T_RANK(2) + 1 && T_RANK(4) == T_RANK(3) + 1)
					|| // is it the low (A,2,3,4,5) straight?
					   (T_RANK(0) == Two && T_RANK(1) == Three && T_RANK(2) == Four &&
						T_RANK(3) == Five && T_RANK(4) == Ace) );

		// are all the cards of the same suit?
		fFlush = (T_SUIT(0) == T_SUIT(1) && T_SUIT(0) == T_SUIT(2) &&
				  T_SUIT(0) == T_SUIT(3) && T_SUIT(0) == T_SUIT(4));
	}

	// now we can go do the precedence list in order

	// test for straight flush
	if (fFlush && fStraight) { // straight + flush = straight flush
		S_VALUE(0, STRAIGHTFLUSH);
		return;
	}

	// check for 4 of a kind
	if ((T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2) && T_RANK(0) == T_RANK(3)) ||	// AAAA B
		(T_RANK(1) == T_RANK(2) && T_RANK(1) == T_RANK(3) && T_RANK(1) == T_RANK(4))) {	// A BBBB
		S_VALUE(0, FOUROFAKIND);
		return;
	}

	// check for full house
	if ((T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2) && T_RANK(3) == T_RANK(4)) ||	// AAA BB
		(T_RANK(0) == T_RANK(1) && T_RANK(2) == T_RANK(3) && T_RANK(2) == T_RANK(4))) {	// AA BBB
		S_VALUE(0, FULLHOUSE);
		return;
	}

	// next best would be a flush
	if (fFlush) {
		S_VALUE(0, FLUSH);
		return;
	}

	// next best would be a straight
	if (fStraight) {
		S_VALUE(0, STRAIGHT);
		return;
	}

	// check for three of a kind
	if ((T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2)) ||	// AAA B C
		(T_RANK(1) == T_RANK(2) && T_RANK(1) == T_RANK(3)) ||	// A BBB C
		(T_RANK(2) == T_RANK(3) && T_RANK(2) == T_RANK(4))) {	// A B CCC
		S_VALUE(0, THREEOFAKIND);
		return;
	}

	// check for two pairs
	if ((T_RANK(0) == T_RANK(1) && T_RANK(2) == T_RANK(3)) ||	// AA BB C
		(T_RANK(0) == T_RANK(1) && T_RANK(3) == T_RANK(4)) ||	// AA B CC
		(T_RANK(1) == T_RANK(2) && T_RANK(3) == T_RANK(4))) {	// A BB CC
	    S_VALUE(0, TWOPAIR);
		return;
	}

	// check for one pair
	if (T_RANK(0) == T_RANK(1) ||	// AA B C D
		T_RANK(1) == T_RANK(2) ||	// A BB C D
		T_RANK(2) == T_RANK(3) ||	// A B CC D
		T_RANK(3) == T_RANK(4)) {	// A B C DD
		S_VALUE(0, PAIR);
		return;
	}

	// only thing left is high card
	S_VALUE(0, HIGHCARD);
}

/**********************************************************************************
 Function Poker::SetTestHandValue(Hand *test_hand, int high_hand_flag)
 Date: 20180707 kriskoin :  Purpose: given a test hand, evaluate its specific values for comparison to others
 24/01/01 kriskoin:
***********************************************************************************/
void Poker::SetTestHandValue(Hand *test_hand, int high_hand_flag)
{

	int card_marker1 = -1;	// used in setting proper values
	int card_marker2 = -1;
	int card_marker3 = -1;
	int card_marker4 = -1;
	int index, slot;	// used in various places

	#define G_VALUE(s)	 (test_hand->GetValue(s))

	// calculate the hand value based on its type
	switch (G_VALUE(0)) {	// VALUE(0) holds the hand type -- we'll start adding at 1 onward
		case HIGHCARD :
			// 24/01/01 kriskoin:
			// since it's high card, we can assume there's no pair -- so if there's an
			// Ace, it'll be last and everything else is shifted up
			// This has implications for the evaluation which must also be
			// done as an exception
			if (!high_hand_flag && T_RANK(4) == Ace) {
				// check for an Ace, which will be at the top, of course
				S_VALUE(1, T_RANK(3));
				S_VALUE(2, T_RANK(2));
				S_VALUE(3, T_RANK(1));
				S_VALUE(4, T_RANK(0));
				S_VALUE(5, T_RANK(4));
			} else {	// normal evaluation
				// since the hand is sorted by rank, from lowest to highest, just
				// count backward to assign the hand value
				S_VALUE(1, T_RANK(4));
				S_VALUE(2, T_RANK(3));
				S_VALUE(3, T_RANK(2));
				S_VALUE(4, T_RANK(1));
				S_VALUE(5, T_RANK(0));
			}
			break;

		case PAIR :
		// we want the relevant value to be what we have a pair of, then the rest in
		// descending order
		// "mark" cards as we assign them so we know not to assign them again
			if (T_RANK(0) == T_RANK(1)) {
				S_VALUE(1, T_RANK(0));
				card_marker1 = 0; card_marker2 = 1;
			} else if (T_RANK(1) == T_RANK(2)) {
				S_VALUE(1, T_RANK(1));
				card_marker1 = 1; card_marker2 = 2;
			} else if (T_RANK(2) == T_RANK(3)) {
				S_VALUE(1, T_RANK(2));
				card_marker1 = 2; card_marker2 = 3;
			} else if (T_RANK(3) == T_RANK(4)) {
				S_VALUE(1, T_RANK(3));
				card_marker1 = 3; card_marker2 = 4;
			}
			// slots 0 and 1 are now used; fill 2,3,4 -- descending from highest card to lowest
			// that is still unaccounted for
			for (slot = 2, index = 4; index >= 0; index--) {
				if (index != card_marker1 && index != card_marker2) {
					S_VALUE(slot++, T_RANK(index));
				}
			}
			break;

		case TWOPAIR :
			// this is a little convoluted, but let's remember the simple idea -- group the two pairs,
			// highest first, then 2nd, and then the kicker
			card_marker1 = -1;	// set invalid until we override it

			if (T_RANK(0) == T_RANK(1)) {	// found the first pair
				S_VALUE(1, T_RANK(0));
				card_marker1 = 0; card_marker2 = 1;
			}
			if (T_RANK(1) == T_RANK(2)) {	// found a pair
				if (card_marker1 != -1) {	// first or 2nd pair?
					S_VALUE(2, T_RANK(1));	// 2nd pair
					card_marker3 = 1; card_marker4 = 2;
				} else {
					S_VALUE(1, T_RANK(1));	// still first pair
					card_marker1 = 1; card_marker2 = 2;
				}
			}
			if (T_RANK(2) == T_RANK(3)) {	// found a pair...
				if (card_marker1 != -1) {	// first or 2nd pair?
					S_VALUE(2, T_RANK(2));	// 2nd pair
					card_marker3 = 2; card_marker4 = 3;
				} else {
					S_VALUE(1, T_RANK(2));	// still first pair
					card_marker1 = 2; card_marker2 = 3;
				}
			}
			if (T_RANK(3) == T_RANK(4)) {	// if we're still looking for the last pair, this is it
				S_VALUE(2, T_RANK(3));		// 2nd pair
				card_marker3 = 3; card_marker4 = 4;
			}
			// make sure pair values are in order
			if (G_VALUE(1) < G_VALUE(2)) {	// swap them into order if needed
				HandValueCell temp_value = G_VALUE(1);
				S_VALUE(1, G_VALUE(2));
				S_VALUE(2, temp_value);
			}
			// ... and find the RANK of the 5th card (kicker)
			for (index = 0; index < 5; index++) {
				if (index != card_marker1 && index != card_marker2 &&
					index != card_marker3 && index != card_marker4) {
						S_VALUE(3, T_RANK(index));	// only unaccounted-for card
						break;
				}
			}
			break;

		case THREEOFAKIND :
			// we want the 3-card value first, then the two left over
			if (T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2)) {			// AAA B C
				S_VALUE(1, T_RANK(0));
				S_VALUE(2, T_RANK(4));
				S_VALUE(3, T_RANK(3));
			} else if (T_RANK(1) == T_RANK(2) && T_RANK(1) == T_RANK(3)) {	// A BBB C
				S_VALUE(1, T_RANK(1));
				S_VALUE(2, T_RANK(4));
				S_VALUE(3, T_RANK(0));
			} else if (T_RANK(2) == T_RANK(3) && T_RANK(2) == T_RANK(4)) {	// A B CCC
				S_VALUE(1, T_RANK(2));
				S_VALUE(2, T_RANK(1));
				S_VALUE(3, T_RANK(0));
			}
			break;

		case STRAIGHT :
			// value of the straight is the order of the cards -- unless A2345
			if (T_RANK(0)== Two && T_RANK(4)== Ace) {	// lowest straight (A2345)
				S_VALUE(1, T_RANK(3));
				S_VALUE(2, T_RANK(2));
				S_VALUE(3, T_RANK(1));
				S_VALUE(4, T_RANK(0));
				S_VALUE(5, T_RANK(0));
			} else {	// normal straight
				S_VALUE(1, T_RANK(4));
				S_VALUE(2, T_RANK(3));
				S_VALUE(3, T_RANK(2));
				S_VALUE(4, T_RANK(1));
				S_VALUE(5, T_RANK(0));
			}
			break;

		case FLUSH :
			// flush is based on the ranking of the cards
			S_VALUE(1, T_RANK(4));
			S_VALUE(2, T_RANK(3));
			S_VALUE(3, T_RANK(2));
			S_VALUE(4, T_RANK(1));
			S_VALUE(5, T_RANK(0));
			break;

	    case FULLHOUSE :
			// only two possibilties
			if (T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2)) { // AAA BB
				S_VALUE(1, T_RANK(0));
				S_VALUE(2, T_RANK(3));
			} else { // AA BBB
				S_VALUE(1, T_RANK(2));
				S_VALUE(2, T_RANK(0));
			}
			break;

		case FOUROFAKIND :
			// only two possibilties
			if (T_RANK(0) == T_RANK(1) && T_RANK(0) == T_RANK(2) && T_RANK(0) == T_RANK(3)) { // AAAA B
				S_VALUE(1, T_RANK(0));
				S_VALUE(2, T_RANK(4));
			} else { // A BBBB
				S_VALUE(1, T_RANK(1));
				S_VALUE(2, T_RANK(0));
			}
			break;

		case STRAIGHTFLUSH :
			// value is the rank of the cards
			if (T_RANK(0)== Two && T_RANK(4)== Ace) {	// lowest straight flush (A2345)
				S_VALUE(1, T_RANK(3));
				S_VALUE(2, T_RANK(2));
				S_VALUE(3, T_RANK(1));
				S_VALUE(4, T_RANK(0));
				S_VALUE(5, T_RANK(0));
			} else {	// normal straight
				S_VALUE(1, T_RANK(4));
				S_VALUE(2, T_RANK(3));
				S_VALUE(3, T_RANK(2));
				S_VALUE(4, T_RANK(1));
				S_VALUE(5, T_RANK(0));
			}
			break;

		default: 
			Error(ERR_INTERNAL_ERROR,"IMPOSSIBLE: in SetTestHandValue with no valid hand value");
	}
}

/**********************************************************************************
 Function Poker::FindBetterHand(TestHand hand_1, TestHand hand_2)
 Date: 20180707 kriskoin :  Purpose: determine which of two test hands is better (or if it's a tie)
***********************************************************************************/
WinningHand Poker::FindBetterHand(Hand hand_1, Hand hand_2)
{
	// work through the value array until we find a difference
	for (int index = 0; index  < HAND_VALUE_SIZE; index ++) {
		if (hand_1.GetValue(index) != hand_2.GetValue(index)) {
			// 24/01/01 kriskoin:
			// we can assume it's a low evaluation -- this will break if 5-ACES is a 
			// valid hand one day.  Until then, no exception... and an Ace at the end
			// is lower than anything else
			if (index == 5 && (hand_1.GetValue(index)==12 || hand_2.GetValue(index)==12)) { // yes
				// the last card is an ACE, meaning the better hand (higher) is the non-ace
				return (hand_1.GetValue(index)==12 ? HAND_2: HAND_1);
			}
			return (hand_1.GetValue(index) > hand_2.GetValue(index) ? HAND_1: HAND_2);
		}
	}
	// no difference at all to this point -- they're the same
	return HAND_TIE;
}

/**********************************************************************************
 Function Poker::FindBetterCard(Card card_1, Card, card_2)
 date: kriskoin 2019/01/01 Purpose: return which card is ranked higher -- CARD_1 or CARD_2 or TIE
***********************************************************************************/
BetterCard Poker::FindBetterCard(Card card_1, Card card_2)
{
	int rank_1 = RANK(card_1);
	int rank_2 = RANK(card_2);
	if (rank_1 != rank_2) {	// different ranks, return card with highest rank
		return (rank_1 > rank_2 ? CARD_1 : CARD_2);
	}
	int suit_1 = SUIT(card_1);
	int suit_2 = SUIT(card_2);
	if (suit_1 != suit_2) {	// different suits, return card with highest suit
		return (suit_1 > suit_2 ? CARD_1 : CARD_2);
	}
	// cards must be the same
	return CARD_TIE;
}

/**********************************************************************************
 Function ::GetHandCompareDescription(Hand hand, char *str)
 Date: 20180707 kriskoin :  Purpose: given 2 5-card hands, describe hand1 (better hand) as it relates to hand2
 Note: tho we test for it, we're assuming hand1 is better (or tied) with hand2. the
       descriptions this gives out are based on them being the next line printed after
	   hand2's description. If there's no hand2, just describe hand1
 990722: if hand2 is better, we'll write something descriptive about hand one
***********************************************************************************/
void Poker::GetHandCompareDescription(Hand *hand1, Hand *hand2, char *str)
{
	// by default, use high evaluations
	GetHandCompareDescription(hand1, hand2, str, TRUE);
}
void Poker::GetHandCompareDescription(Hand *hand1, Hand *hand2, char *str, int high_flag)
{
	if (!str) {	// we were fed a null pointer?
		Error(ERR_WARNING,"GetHandDescription was fed a null ptr");
		return;
	}

	int two_hands = FALSE;	// do we have one or two hands to work with?
	if (hand2) {
		two_hands = TRUE;
	}

	char szSingleCards[13][8] = { "two","three","four","five","six","seven","eight",
								  "nine","ten","jack","queen","king","ace" } ;

	char szPluralCards[13][8] = { "twos","threes","fours","fives","sixes","sevens",
								  "eights","nines","tens","jacks","queens",
								  "kings","aces" } ;

	// for now, this only supports a 5-card hands
	if (hand1->CardCount() !=5) {
		Error(ERR_NOTE, "GetHandCompareDescription called with a %d hand1",
			hand1->CardCount());
		*str = 0;	// blank result string
		return;
	}

	if (two_hands && hand2->CardCount() !=5) {
		Error(ERR_INTERNAL_ERROR, "GetHandCompareDescription called with a %d hand2",
			hand2->CardCount());
		*str = 0;	// blank result string
		return;
	}

	// make sure that the hands have been evaluated; if not, evaluate it now
	if (!hand1->GetEvalState()) {
		EvaluatePokerHand(hand1, TRUE);
	}
	if (two_hands && !hand2->GetEvalState()) {
		EvaluatePokerHand(hand2, TRUE);
	}

	// hand1 should be better than 2... let's check
	int hand_2_is_better = FALSE;
	if (two_hands) {
		WinningHand wh = FindBetterHand(*hand1, *hand2);
		if (wh == HAND_TIE) {	// they're the same
			sprintf(str, "the same hand");
			return;
		}
		if (wh == HAND_2) {	// 2 is better than 1?  unexpected -- just describe hand1 on its own
			hand_2_is_better = TRUE;
		}
	}

	#define GH_VALUE1(s)	 (hand1->GetValue(s))
	#define GH_VALUE2(s)	 (hand2->GetValue(s))

//	if (GH_VALUE1(0) != GH_VALUE2(0)) {	// they're entirely different, simple description is ok

	switch (GH_VALUE1(0)) {	// VALUE(0) holds the hand type
	case HIGHCARD :
		// 24/01/01 kriskoin:
		// extern char cRanks[];	// "23456789TJQKA"
		// low hand is always high-card something... but we'll check
		if (!high_flag) {
			if (GH_VALUE1(0)==HIGHCARD) {
				sprintf(str, "%c,%c,%c,%c,%c", 
					cRanks[GH_VALUE1(1)],
					cRanks[GH_VALUE1(2)],
					cRanks[GH_VALUE1(3)],
					cRanks[GH_VALUE1(4)],
					cRanks[GH_VALUE1(5)]);
			} 
		} else {		
			sprintf(str, "high card %s", szSingleCards[GH_VALUE1(1)]);
		}
		break;

	case PAIR :
		sprintf(str, "a pair of %s", szPluralCards[GH_VALUE1(1)]);
		break;

	case TWOPAIR :
		sprintf(str, "two pair, %s and %s", szPluralCards[GH_VALUE1(1)], szPluralCards[GH_VALUE1(2)]);
		break;

	case THREEOFAKIND :
		sprintf(str, "three of a kind, %s", szPluralCards[GH_VALUE1(1)]);
		break;

	case STRAIGHT :
		// we have to check for the low straight as an exception
		if (GH_VALUE1(4) == GH_VALUE1(5))	{ // both are zero in the low straight
			sprintf(str, "a straight, ace to five");
		} else {
			sprintf(str, "a straight, %s to %s",
				szSingleCards[GH_VALUE1(5)], szSingleCards[GH_VALUE1(1)]);
		}
		break;

	case FLUSH :
		sprintf(str, "a flush, %s high", szSingleCards[GH_VALUE1(1)]);
		break;

	case FULLHOUSE :
		sprintf(str, "a full house, %s full of %s",
			szPluralCards[GH_VALUE1(1)], szPluralCards[GH_VALUE1(2)]);
		break;

	case FOUROFAKIND :
		sprintf(str, "four of a kind, %s",
			szPluralCards[GH_VALUE1(1)]);
		break;

	case STRAIGHTFLUSH :
		// we have to check for the low straight flush as an exception
		if (GH_VALUE1(4) == GH_VALUE1(5))	{ // both are zero in the low
			sprintf(str, "a straight flush, ace to five");
		} else if (GH_VALUE1(1) == 12) {	// royal flush
			sprintf(str, "a royal flush");
		} else {
			sprintf(str, "a straight flush, %s to %s",
				szSingleCards[GH_VALUE1(5)], szSingleCards[GH_VALUE1(1)]);
		}
		break;

	default:
		Error(ERR_INTERNAL_ERROR, "In GetHandDescription() with no valid hand VALUE1(1)");
		*str = 0;	// blank result string
		return;
	}

	if (two_hands && GH_VALUE1(0) == GH_VALUE2(0)) {	// same kind of hand -- kickers may play
		char szKicker[20];
		szKicker[0] = 0;
		switch (GH_VALUE1(0)) {	// VALUE(0) holds the hand type
		case HIGHCARD :
			if (!high_flag) {	// no kickers with low evaluation
				break;
			}
			if (GH_VALUE1(1) != GH_VALUE2(1)) {
				// they're different high cards -- no need to mention kickers
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower kicker");
				break;
			}
			// go down the line, looking for what'll play as kicker
			if (GH_VALUE1(2) > GH_VALUE2(2)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(2)]);
				break;
			}
			if (GH_VALUE1(3) > GH_VALUE2(3)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(3)]);
				break;
			}
			if (GH_VALUE1(4) > GH_VALUE2(4)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(4)]);
				break;
			}
			if (GH_VALUE1(5) > GH_VALUE2(5)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(5)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find a high-card kicker", _FL);
			break;

		case PAIR :
			if (GH_VALUE1(1) != GH_VALUE2(1)) {
				// they're different pairs -- no need to mention kickers
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower kicker");
				break;
			}
			if (GH_VALUE1(2) > GH_VALUE2(2)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(2)]);
				break;
			}
			if (GH_VALUE1(3) > GH_VALUE2(3)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(3)]);
				break;
			}
			if (GH_VALUE1(4) > GH_VALUE2(4)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(4)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find a pair kicker", _FL);
			break;

		case TWOPAIR :
			if ( (GH_VALUE1(1) != GH_VALUE2(1)) || (GH_VALUE1(2) != GH_VALUE2(2)) ) {
				// they're different 2 pairs -- no need to mention kicker
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower kicker");
				break;
			}
			if (GH_VALUE1(3) > GH_VALUE2(3)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(3)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find a pair kicker", _FL);
			break;

		case THREEOFAKIND :
			if (GH_VALUE1(1) != GH_VALUE2(1)) {
				// they're different 3-of-a-kind -- no need to mention kickers
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower kicker");
				break;
			}
			if (GH_VALUE1(2) > GH_VALUE2(2)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(2)]);
				break;
			}
			if (GH_VALUE1(3) > GH_VALUE2(3)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(3)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find a 3-kind kicker", _FL);
			break;

		case STRAIGHT :
			// no kickers with a straight -- all five cards are in play
			break;

		case FLUSH :
			if (GH_VALUE1(1) != GH_VALUE2(1)) {
				// they're different top cards -- no need to mention kickers
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower cards");
				break;
			}
			if (GH_VALUE1(2) > GH_VALUE2(2)) {
				sprintf(szKicker," -- %s plays", szSingleCards[GH_VALUE1(2)]);
				break;
			}
			if (GH_VALUE1(3) > GH_VALUE2(3)) {
				sprintf(szKicker," -- %s plays", szSingleCards[GH_VALUE1(3)]);
				break;
			}
			if (GH_VALUE1(4) > GH_VALUE2(4)) {
				sprintf(szKicker," -- %s plays", szSingleCards[GH_VALUE1(4)]);
				break;
			}
			if (GH_VALUE1(5) > GH_VALUE2(5)) {
				sprintf(szKicker," -- %s plays", szSingleCards[GH_VALUE1(4)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find flush kicker", _FL);
			break;

		case FULLHOUSE :
			// no kickers with a full house -- all five cards are in play
			break;

		case FOUROFAKIND :
			if (GH_VALUE1(1) != GH_VALUE2(1)) {
				// they're different top cards -- no need to mention kickers
				break;
			}
			// perhaps hand 2 is better -- just tell us we have a lower kicker
			if (hand_2_is_better) {
				sprintf(szKicker," -- lower kicker");
				break;
			}
			if (GH_VALUE1(2) > GH_VALUE2(2)) {
				sprintf(szKicker," -- %s kicker", szSingleCards[GH_VALUE1(2)]);
				break;
			}
			Error(ERR_INTERNAL_ERROR, "%s(%d) Fell through trying to find 4-of-a-kind kicker", _FL);
			break;

		case STRAIGHTFLUSH :
			// no kickers with a straight flush -- all five cards are in play
			break;

		default:
			Error(ERR_INTERNAL_ERROR, "In GetHandDescription() with no valid hand VALUE1(2)");
			*str = 0;	// blank result string
			return;
		}
		if (szKicker[0]) {
			strcat(str, szKicker);
		}
	}
}

/**********************************************************************************
 Function Poker::FindBestOmahaHand(Hand test_hand, Hand* hand_out)
 date: kriskoin 2019/01/01 Purpose: given a proper omaha pocket and flop, find the best hand
***********************************************************************************/
void Poker::FindBestOmahaHand(Hand pocket_hand, Hand flop_hand, Hand *high_hand_out, Hand *low_hand_out)
{

	// we expect: 4 cards in the pocket and at least 3 on the flop
	if (pocket_hand.CardCount() != 4 || flop_hand.CardCount() < 3) {
		Error(ERR_FATAL_ERROR, "%s(%d) bad FindBestOmahaHand pocket = %d, flop = %d", _FL,
			pocket_hand.CardCount(), flop_hand.CardCount());
	}
	// evaluations use these -- and eventually return best_hand
	Hand best_high_hand;	// the best so far goes in here
	Hand best_low_hand;		// the best so far goes in here
	Hand test_hand;
	Card test_cards[5];	// all potential cards go in here -- 2 from pocket, 3 from flop
	int first_call = TRUE;	// used for setting low
	for (int combo = 0; combo < 6; combo++) {	// 6 combos for omaha pockets
		// fill slots 0 and 1 with pocket cards
		switch (combo) {
		case 0 : // A B
			test_cards[0] = pocket_hand.GetCard(0);
			test_cards[1] = pocket_hand.GetCard(1);
			break;
		case 1 : // A C
			test_cards[0] = pocket_hand.GetCard(0);
			test_cards[1] = pocket_hand.GetCard(2);
			break;
		case 2 : // A D
			test_cards[0] = pocket_hand.GetCard(0);
			test_cards[1] = pocket_hand.GetCard(3);
			break;
		case 3 : // B C
			test_cards[0] = pocket_hand.GetCard(1);
			test_cards[1] = pocket_hand.GetCard(2);
			break;
		case 4 : // B D
			test_cards[0] = pocket_hand.GetCard(1);
			test_cards[1] = pocket_hand.GetCard(3);
			break;
		case 5 : // C D
			test_cards[0] = pocket_hand.GetCard(2);
			test_cards[1] = pocket_hand.GetCard(3);
			break;
		}
		for (int flop_count = 0; flop_count < 10; flop_count++) {
		// we have the pocket -- let's add the flop (slots 2,3,4)
			switch (flop_count) {
			case 0 : // A B C
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(1);
				test_cards[4] = flop_hand.GetCard(2);
				break;
			case 1 : // A B D
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(1);
				test_cards[4] = flop_hand.GetCard(3);
				break;
			case 2 : // A B E
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(1);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			case 3 : // A C D
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(2);
				test_cards[4] = flop_hand.GetCard(3);
				break;
			case 4 : // A C E
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(2);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			case 5 : // A D E
				test_cards[2] = flop_hand.GetCard(0);
				test_cards[3] = flop_hand.GetCard(3);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			case 6 : // B C D
				test_cards[2] = flop_hand.GetCard(1);
				test_cards[3] = flop_hand.GetCard(2);
				test_cards[4] = flop_hand.GetCard(3);
				break;
			case 7 : // B C E
				test_cards[2] = flop_hand.GetCard(1);
				test_cards[3] = flop_hand.GetCard(2);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			case 8 : // B D E
				test_cards[2] = flop_hand.GetCard(1);
				test_cards[3] = flop_hand.GetCard(3);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			case 9 : // C D E
				test_cards[2] = flop_hand.GetCard(2);
				test_cards[3] = flop_hand.GetCard(3);
				test_cards[4] = flop_hand.GetCard(4);
				break;
			}
			// now we've got all the cards -- we can build the test hand
			test_hand.ClearHandCards();
			int good_hand_to_evaluate = TRUE;
			for (int card_index = 0; card_index < 5; card_index++) {
				if (test_cards[card_index] == CARD_NO_CARD) {
					good_hand_to_evaluate = FALSE;
					break;
				}
				test_hand.Add(test_cards[card_index]);
			}
			// and find the best...
			if (good_hand_to_evaluate) {
				EvaluatePokerHand(&test_hand, TRUE);// eval for high
				if (FindBetterHand(test_hand, best_high_hand) == HAND_1) {
					best_high_hand = test_hand;
				}
				EvaluatePokerHand(&test_hand, FALSE);// eval for low
				if (first_call) {
					first_call = FALSE;
					best_low_hand = test_hand;
				} else {	// evaluate for low
					if (FindBetterHand(test_hand, best_low_hand) == HAND_2) {
						best_low_hand = test_hand;
					}
				}
			}
		}
	}
	if (high_hand_out) {
		memcpy(high_hand_out, &best_high_hand, sizeof(Hand));	// copy the best hand to output hand
	}
	if (low_hand_out) {
		memcpy(low_hand_out, &best_low_hand, sizeof(Hand));	// copy the best hand to output hand
	}
}

/**********************************************************************************
 Function ::FindBestHandFrom7Cards(Hand test_hand, Hand *hand_out)
 Date: 20180707 kriskoin :  Purpose: find the best 5-card hand from a 7-card hand
***********************************************************************************/
void Poker::FindBestHandFrom7Cards(Hand test_hand, Hand *high_hand_out, Hand *low_hand_out)
{
	// let's make sure we actually have exactly 7 cards
	// we expect: 7 cards in the pocket and 0 on the flop (flop is not used)
	if (test_hand.CardCount() != 7) {
		Error(ERR_FATAL_ERROR, "%s(%d) CardCount = %d (should be 7)", _FL, test_hand.CardCount());
	}
	// set up a loop that tests all 21 combinations;
	// build all 5-card combinations from these 7, and test them
	Card test_cards[7];	// all potential cards go in here
	// set the test cards
	int card_index;
	for (card_index = 0; card_index < test_hand.CardCount(); card_index++) {
		test_cards[card_index] = test_hand.GetCard(card_index);
	}
	// we'll set up an array which we'll use to filter out cards as we build combinations
	// of five to test -- we pick two of them (different, of course) and filter them
	// out -- the resulting five are what we test
	int good_card[7];
	Hand best_high_hand;	// the best so far goes in here
	Hand best_low_hand;		// the best so far goes in here
	int first_call = TRUE;	// used for setting low
	for (int x = 0; x < 7; x++) {		// loop for all cards
		for (int y = x; y < 7; y++) {	// 2nd loop below the first
			if (x == y) continue;	// duplicate, no good (don't eliminate the same card)
			int z;
			for (z = 0; z < 7; z++) {
				good_card[z] = TRUE;	// reset the hand-making template (for now, all OK)
			}
			// blank those two, we're left with five
			good_card[x] = FALSE;	// the two cards we marked are "removed"
			good_card[y] = FALSE;
			card_index = 0;
			// now, build the hand -- 5 cards that are left over
			test_hand.ClearHandCards();
			for (z = 0; z < 7; z++) {
				if (good_card[z]) {
					test_hand.Add(test_cards[z]);
				}
			}
			EvaluatePokerHand(&test_hand, TRUE);// eval for high
			if (FindBetterHand(test_hand, best_high_hand) == HAND_1) {
				best_high_hand = test_hand;
			}
			EvaluatePokerHand(&test_hand, FALSE);// eval for low
			if (first_call) {
				first_call = FALSE;
				best_low_hand = test_hand;
			} else {	// evaluate for low
				if (FindBetterHand(test_hand, best_low_hand) == HAND_2) {
					best_low_hand = test_hand;
				}
			}
		}
	}
	if (high_hand_out) {
		memcpy(high_hand_out, &best_high_hand, sizeof(Hand));	// copy the best hand to output hand
	}
	if (low_hand_out) {
		memcpy(low_hand_out, &best_low_hand, sizeof(Hand));	// copy the best hand to output hand
	}
}

/**********************************************************************************
 Function ::FindBestHandFrom6Cards(Hand test_hand, Hand *hand_out)
 Date: 20180707 kriskoin :  Purpose: find the best 5-card hand from a 6-card hand
***********************************************************************************/
void Poker::FindBestHandFrom6Cards(Hand test_hand, Hand *high_hand_out, Hand *low_hand_out)
{
	// let's make sure we actually have exactly 6 cards
	// we expect: 6 cards in the pocket and 0 on the flop (flop is not used)
	if (test_hand.CardCount() != 6) {
		Error(ERR_FATAL_ERROR, "%s(%d) CardCount = %d (should be 6)", _FL, test_hand.CardCount());
	}
	// set up a loop that tests all 6 combinations;
	// build all 5-card combinations from these 6, and test them
	Card test_cards[6];	// all potential cards go in here
	// set the test cards
	int card_index;
	for (card_index = 0; card_index < test_hand.CardCount(); card_index++) {
		test_cards[card_index] = test_hand.GetCard(card_index);
	}
	// we'll set up an array which we'll use to filter out cards as we build combinations
	// of five to test -- we pick one them and filter it out -- test the resulting five
	int good_card[6];
	Hand best_high_hand;	// the best so far goes in here
	Hand best_low_hand;		// the best so far goes in here
	int first_call = TRUE;	// used for setting low
	for (int x = 0; x < 6; x++) {		// loop for all cards
		int z;
		for (z = 0; z < 6; z++) {
			good_card[z] = TRUE;	// reset the hand-making template (for now, all OK)
		}
		// blank the one, we're left with five
		good_card[x] = FALSE;	// the card we marked is "removed"
		card_index = 0;
		// now, build the hand -- 5 cards that are left over
		test_hand.ClearHandCards();
		for (z = 0; z < 6; z++) {
			if (good_card[z]) {
				test_hand.Add(test_cards[z]);
			}
		}
		test_hand.ClearHandValue();
		EvaluatePokerHand(&test_hand, TRUE);// eval for high
		if (FindBetterHand(test_hand, best_high_hand) == HAND_1) {
			best_high_hand = test_hand;
		}
		EvaluatePokerHand(&test_hand, FALSE);// eval for low
		if (first_call) {
			first_call = FALSE;
			best_low_hand = test_hand;
		}
		if (FindBetterHand(test_hand, best_low_hand) == HAND_2) {
			best_low_hand = test_hand;
		}
	}
	if (high_hand_out) {
		memcpy(high_hand_out, &best_high_hand, sizeof(Hand));	// copy the best hand to output hand
	}
	if (low_hand_out) {
		memcpy(low_hand_out, &best_low_hand, sizeof(Hand));	// copy the best hand to output hand
	}
}

/**********************************************************************************
 Function ValidLowHand(Hand *low_hand)
 date: 24/01/01 kriskoin Purpose: return T/F if a low hand qualifies as high-card 8 or better
***********************************************************************************/
int Poker::ValidLowHand(Hand *low_hand)
{
	if (!low_hand) {
		Error(ERR_FATAL_ERROR, "%s(%d) ValidLowHand() called with NULL", _FL);
		return FALSE;
	}
	// make sure that the hand has been evaluated as LOW
	if (low_hand->GetEvalState() == FALSE) {	// hasn't been evaluated
		EvaluatePokerHand(low_hand, FALSE);
	}
	// is it a high-card hand?
	#define LH_VALUE(s)	 (low_hand->GetValue(s))
	if (LH_VALUE(0) != HIGHCARD) {
		return FALSE;
	}
	// highest card > 8?
	if (LH_VALUE(1) > Eight) {
		return FALSE;
	}
	// passed -- it qualifies
	return TRUE;
}

