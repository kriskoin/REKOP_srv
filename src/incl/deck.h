/**********************************************************************************
 CLASS Deck
 Date: 20180707 kriskoin :  A "Deck" is a deck of cards and its associated internal functions
***********************************************************************************/
#ifndef _DECK_H_INCLUDED
#define _DECK_H_INCLUDED

#include "pplib.h"

class Deck {

public:
        Card _deck[CARDS_IN_DECK];      // the deck array
        int _test_deck[CARDS_IN_DECK]; // used for validating the deck
        int _next_card_index;           // index of next card in deck that will be dealt
        int _number_of_decks;           // we can handle multiple 52-card decks

	Deck(void);
	Deck(int decks);	// multiple deck constructor (unsupported)
	~Deck(void);
	void ShuffleDeck(void);	// shuffle any remaining cards in the deck
	void ShuffleDeck(int number_of_shuffles);	// specify number of shuffles
	void ValidateDeck(void);
	Card DealNextCard(void);
	int GetCardsLeft(void);	// return number of cards left in the deck
	void CreateDeck(void);	// create a new fresh deck (unshuffled)
	Card GetCard(int card_index);	// grab a card from the middle of the deck

};

#endif // !_DECK_H_INCLUDED

