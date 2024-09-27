/**********************************************************************************
 CLASS Hand
 Date: 20180707 kriskoin :  A "Hand" is a Hand of cards containing some number of Cards
***********************************************************************************/
#ifndef _HAND_H_INCLUDED
#define _HAND_H_INCLUDED

#include "pplib.h"

#define HAND_VALUE_SIZE		7	// size of array holding our hand values (internal)
typedef char HandValueCell;		// just an 8-bit holder
typedef HandValueCell HandValue[HAND_VALUE_SIZE];	// used in internal hand evaluations

class Hand {

public:
	Hand(void);
	Hand(int);						// if we give it a T/F, it'll set _sort_hand to it
	~Hand(void);

	void Add(Card);
	void Discard(Card);
	int CardCount(void);			// return number of cards we're holding
	int MaxNumberOfCards(void);		// return the max number of cards we could potentially have
	void GetASCIIHand(char *);		// build a display string of our hand (debug only)
	Card GetCard(int);				// return the card for a specific slot
	void SwapCards(int i1, int i2);	// swap the location of two cards in our internal _hand array
	void ClearHandValue(void);		// clear the hand value  array
	void ClearHandCards(void);		// clear the hand's cards
	HandValueCell GetValue(int s);	// used in external evaluation
	void SetValue(int slot, HandValueCell val);	// used in external evaluation
	void SetEvalState(int state);	// lets the hand know whether it's validly evaluated
	int GetEvalState(void);			// get the current evaluation state
	void SetSortable(int);			// set T/F, whether we want the hand auto-sorted
	int GetSortable(void);			// get the T/F of _sort_hand
	int GetInternalSlot(Card);		// return our slot index for this card

private:
	// max 7 cards per hand
	#define _max_number_of_cards  MAX_PRIVATE_CARDS		// defined as 7 TODO!! possible confusion?
	int _number_of_cards;				// how many cards are we holding?
	Card _hand[_max_number_of_cards];	// our array holding the cards
	void BlankCardSlot(int card_index);	// set this array entry to NO CARD
	void ValidateCard(Card);			// internal check for card validity
	HandValue _hand_value;				// used for evaluation
	void SortHand(void);				// internal sort, done after all Add() and Discard()
	int _currently_evaluated;			// TRUE/FALSE, depending if we're currently evaluated
	int _sort_hand;						// TRUE/FALSE, sort hand or not
};

#endif // !_HAND_H_INCLUDED

