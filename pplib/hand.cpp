/**********************************************************************************
 Member functions for Hand object
 Date: 20180707 kriskoin : **********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hand.h"

// Global definitions for cSuits and cRanks.
char cSuits[] = "cdhs";
char cRanks[] = "23456789TJQKA";

/**********************************************************************************
 Function Hand::Hand();
 Date: 20180707 kriskoin :  Purpose: default constructor
***********************************************************************************/
Hand::Hand(void)
{
	_sort_hand = TRUE;	// by default, sort the hand after Add()/Discard()
	ClearHandCards();	// blank them all
}

/**********************************************************************************
 Function Hand::Hand();
 Date: 20180707 kriskoin :  Purpose: parameter constructor - sets T/F for _sort_hand
***********************************************************************************/
Hand::Hand(int sort_flag)
{
	_sort_hand = sort_flag;	// we may not want this hand to auto-sort itself
	ClearHandCards();		// blank them all
}

/**********************************************************************************
 Function Hand::~Hand()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Hand::~Hand(void)
{
	// nothing to do for now
}

/**********************************************************************************
 Function ::SetSortable(int sort_flag)
 date: kriskoin 2019/01/01 Purpose: set the sort_flag state of this hand
***********************************************************************************/
void Hand::SetSortable(int sort_flag)
{
	_sort_hand = sort_flag;
}

/**********************************************************************************
 Function ::Add(Card card)
 Date: 20180707 kriskoin :  Purpose: add a card to our hand
***********************************************************************************/
void Hand::Add(Card card)
{
	// first, make sure it's a valid card
	ValidateCard(card);
	// make sure we don't already have this card
	if (GetInternalSlot(card) != CARD_NO_CARD) {
		// oh oh -- we found it
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to add a card we already own %c%c", _FL, cRanks[RANK(card)], cSuits[SUIT(card)]);
		return;
	}
	// find an empty slot for it
	#define NO_FREE_SLOTS	-1
	int card_slot = NO_FREE_SLOTS;
	for (int i=0; i < _max_number_of_cards; i++) {
		if (_hand[i] == CARD_NO_CARD) {
			// found a free slot
			card_slot = i;
			break;
		}
	}
	if (card_slot == NO_FREE_SLOTS) {
		// all full, no room
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to add a card %c%c but there's no room for it", _FL, cRanks[RANK(card)], cSuits[SUIT(card)]);
		return;
	}
	// got our slot -- this is where the card goes
	_hand[card_slot] = card;
	_number_of_cards++;
	ClearHandValue();	// the current evaluation may no longer be valid
	SortHand();			// sort resulting cards into order
}

/**********************************************************************************
 Function ::Discard(Card card)
 Date: 20180707 kriskoin :  Purpose: discard a card from our hand
***********************************************************************************/
void Hand::Discard(Card card)
{
	// first, make sure it's a valid card
	ValidateCard(card);
	// looks ok, let's find it in our hand
	int card_slot = GetInternalSlot(card);
	if (card_slot == CARD_NO_CARD) {
		// trouble -- we don't own this card
		Error(ERR_INTERNAL_ERROR,"%s(%d)Tried to discard (%c%c) but we don't own it", _FL, cRanks[RANK(card)], cSuits[SUIT(card)]);
		return;
	}
	// it's ok to delete
	_hand[card_slot] = CARD_NO_CARD;
	_number_of_cards--;
	ClearHandValue();	// the current evaluation may no longer be valid
	SortHand();			// sort resulting cards into order
}

/**********************************************************************************
 Function Hand::CardCount(void)
 Date: 20180707 kriskoin :  Purpose: return the number of cards we're holding in our hand
***********************************************************************************/
int Hand::CardCount(void)
{
	return _number_of_cards;
}

/**********************************************************************************
 Function Hand::MaxNumberOfCards(void)
 Date: 20180707 kriskoin :  Purpose: return the max number of potential cards we could be holding
***********************************************************************************/
int Hand::MaxNumberOfCards(void)
{
	return _max_number_of_cards;
}

/**********************************************************************************
 Function Hand::GetCard(int card_slot)
 Date: 20180707 kriskoin :  Purpose: return the card in this card slot
***********************************************************************************/
Card Hand::GetCard(int card_slot)
{
	if (card_slot < 0 || card_slot >= _max_number_of_cards) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) GetCard() was fed %d", _FL, card_slot);
		DIE("Invalid slot value fed to GetCard()");
	}
	return _hand[card_slot];
}

/**********************************************************************************
 Function Hand::BlankCardSlot(int index)
 Date: 20180707 kriskoin :  Purpose: set this array index entry to NO CARD
***********************************************************************************/
void Hand::BlankCardSlot(int index)
{
	if (index < 0 || index >= _max_number_of_cards) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) BlankCardSlot was fed %d", _FL, index);
		DIE("Tried to blank an invalid card index in Hand");
	}
	_hand[index] = CARD_NO_CARD;
}

/**********************************************************************************
 Function Hand::GetInternalSlot(Card card)
 Date: 20180707 kriskoin :  Purpose: find the internal slot where we hold this card
***********************************************************************************/
int Hand::GetInternalSlot(Card card)
{
	for (int i=0; i < _max_number_of_cards; i++) {
		if (_hand[i] == card) return i;
	}
	return CARD_NO_CARD;	// we didn't find it
}

/**********************************************************************************
 Function Hand::GetASCIIHand(void)
 Date: 20180707 kriskoin :  Purpose: build an ASCII string of our entire hand (for debugging)
***********************************************************************************/
void Hand::GetASCIIHand(char *str)
{
	if (!str) {	// we were fed a null pointer
		Error(ERR_WARNING,"GetASCIIHand was fed a null ptr");
		return;
	}
	char szTmp[6];
	*str = 0;	// start at the beginning of the string
	for (int i=0; i < _max_number_of_cards; i++) {
		if (_hand[i] == CARD_NO_CARD) {
			sprintf(szTmp,"[--] ");
		} else {
			sprintf(szTmp,"[%c%c] ",cRanks[RANK(_hand[i])], cSuits[SUIT(_hand[i])]);
		}
		strcat(str, szTmp);
	}
  #if 0
	strcat(str, " {");
	for (i = 0; i < HAND_VALUE_SIZE; i++) {
		sprintf(szTmp, "%c", _hand_value[i]);
		strcat(str, szTmp);
	}
	strcat(str, "}");
  #endif
}

/**********************************************************************************
 Function Hand::ValidateCard(Card card)
 Date: 20180707 kriskoin :  Purpose: internal validation for a card; make sure it's not garbage
***********************************************************************************/
void Hand::ValidateCard(Card card)
{
	// a card will be assumed valid if we can pull a valid rank and suit from it
	Card test_card = CARDINDEX(card);
	if (test_card < 52) {
		return;	// card checks out OK
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Hand object received %d as a card", _FL, card);
		DIE("Invalid card");
	}
}

/**********************************************************************************
 Function Hand::ClearHandValue(void)
 Date: 20180707 kriskoin :  Purpose: clear the hand value array (leave cards intact)
***********************************************************************************/
void Hand::ClearHandValue(void)
{
	_currently_evaluated = FALSE;
	for (int index = 0; index < HAND_VALUE_SIZE; index++) {
		_hand_value[index] = 0;
	}
}

/**********************************************************************************
 Function Hand::ClearHandCards(void)
 Date: 20180707 kriskoin :  Purpose: clear the hand's cards (and values)
***********************************************************************************/
void Hand::ClearHandCards(void)
{
	for (int index = 0; index < _max_number_of_cards; index++) {
		BlankCardSlot(index);
		_hand_value[index] = 0;
	}
	_number_of_cards = 0;
}

/**********************************************************************************
 Function Hand::SortHand(void)
 Date: 20180707 kriskoin :  Purpose: sort the hand, sifting lower cards to the front, empties to the back
***********************************************************************************/
void Hand::SortHand(void)
{
	if (!_sort_hand) return;	// don't sort this hand (like a flop...)
	Card temp_card;	// used during swap
	for (int loop = 0; loop < _max_number_of_cards; loop++) {
		for (int card_index = 0; card_index < _max_number_of_cards-1; card_index++) {
			if (RANK(_hand[card_index]) > RANK(_hand[card_index+1])) {
				temp_card = _hand[card_index];
				_hand[card_index] = _hand[card_index+1];
				_hand[card_index+1] = temp_card;
			}
		}
	}
}

/**********************************************************************************
 Function Hand::GetValue(int slot)
 Date: 20180707 kriskoin :  Purpose: return the element in a value slot
***********************************************************************************/
HandValueCell Hand::GetValue(int slot)
{
	if (slot >= 0 && slot < HAND_VALUE_SIZE) {
		return _hand_value[slot];
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad slot requested (%d) in GetValue", _FL, slot);
		DIE("Out of range in GetValue()");
	}
}


/**********************************************************************************
 Function Hand::SetValue(int slot)
 Date: 20180707 kriskoin :  Purpose: set the element in a value slot
***********************************************************************************/
void Hand::SetValue(int slot, HandValueCell data)
{
	if (slot >= 0 && slot < HAND_VALUE_SIZE) {
		_hand_value[slot] = data;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad slot indicated (%d) in SetValue", _FL, slot);
		DIE("Out of range in SetValue()");
	}
}

/**********************************************************************************
 Function Hand::GetEvalState(void)
 Date: 20180707 kriskoin :  Purpose: get current evaluation state
***********************************************************************************/
int Hand::GetEvalState(void)
{
	return _currently_evaluated;
}

/**********************************************************************************
 Function Hand::GetSortable(void)
 Date: 20180707 kriskoin :  Purpose: get current _sort_hand state
***********************************************************************************/
int Hand::GetSortable(void)
{
	return _sort_hand;
}

/**********************************************************************************
 Function Hand::SetEvalState(int state)
 Date: 20180707 kriskoin :  Purpose: set current evaluation state
***********************************************************************************/
void Hand::SetEvalState(int state)
{
	_currently_evaluated = state;
}

/**********************************************************************************
 void SwapCards(int i1, int i2);
 Date: 20180707 kriskoin :  Purpose: swap two cards from their position in our internal _hand array
***********************************************************************************/
void Hand::SwapCards(int slot1, int slot2)
{
	if (slot1 >= 0 && slot1 < MAX_PRIVATE_CARDS && slot2 >= 0 && slot2 < MAX_PRIVATE_CARDS) {
		// seems reasonable, do the swap
		Card tmp_card = _hand[slot1];
		_hand[slot1] = _hand[slot2];
		_hand[slot2] = tmp_card;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Bad slot requested (%d or %d) in SwapCards", _FL, slot1, slot2);
		// nothing done, but this is serious... have a look at what might have fed this
		// nubmers that are out of range -- should never happen
	}
}

