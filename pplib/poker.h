/**********************************************************************************
 CLASS Poker
 Date: 20180707 kriskoin :  "Poker" is an object defining and evaluating the rules of poker
***********************************************************************************/
#ifndef _POKER_H_INCLUDED
#define _POKER_H_INCLUDED

#include "pplib.h"
#include "hand.h"

#define CARDS_IN_POKER_HAND	5	// at the lowest level, this never changes
enum WinningHand { HAND_TIE, HAND_1, HAND_2 } ;	// used for comparing two test hands (internal)
enum BetterCard { CARD_TIE, CARD_1, CARD_2 } ;	// used for comparing two test cards (internal)

class Poker {

public:
	Poker(void);
	~Poker(void);
	// FindBestHand is fed a pocket and a flop, and it figures out the best combo
	// and a literal description as well
	void FindBestHand(GameRules, Hand, Hand, Hand *high_hand_out, Hand *low_hand_out);
	void GetHandCompareDescription(Hand *hand1, Hand *hand2, char *out, int high_flag);
	void GetHandCompareDescription(Hand *hand1, Hand *hand2, char *out);
	WinningHand FindBetterHand(Hand hand_1, Hand hand_2);
	BetterCard FindBetterCard(Card card_1, Card card_2);
	int ValidLowHand(Hand *low_hand);	// does a hand qualify for low? (T/F)
	void EvaluateHand(Hand *high_hand_out);	// evaluate this hand
	void EvaluateHand(Hand *high_hand_out, Hand *low_hand_out);	// evaluate this hand

private:
	void EvaluatePokerHand(Hand *out, int high_hand_flag);	// evaluate a test hand
	void SetTestHandType(Hand *, int high_hand_flag);	// what type of hand is it?
	void SetTestHandValue(Hand *, int high_hand_flag);	// evaluate it -- set values
	void FindBestHandFrom6Cards(Hand, Hand *high_hand_out, Hand *low_hand_out);
	void FindBestHandFrom7Cards(Hand, Hand *high_hand_out, Hand *low_hand_out);
	void FindBestOmahaHand(Hand pocket_hand, Hand flop_hand, Hand *high_hand_out, Hand *low_hand_out);

};

#endif // !_POKER_H_INCLUDED
