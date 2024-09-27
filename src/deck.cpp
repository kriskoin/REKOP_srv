/**********************************************************************************
 Member functions for Deck object
 Date: 20180707 kriskoin : **********************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include "deck.h"
#include "pokersrv.h"	// needed for random number generator

/**********************************************************************************
 Function Deck::Deck();
 Date: 20180707 kriskoin :  Purpose: default constructor -- one deck
***********************************************************************************/
Deck::Deck(void)
{
	_number_of_decks = 1;
	CreateDeck();	// whoever uses this deck _should_ shuffle before dealing
}

/**********************************************************************************
 Function ::Deck(int number_of_decks)
 Date: 20180707 kriskoin :  Purpose: constructor if we're using more than one deck
***********************************************************************************/
Deck::Deck(int number_of_decks)
{
	_number_of_decks = number_of_decks;
	CreateDeck();	// whoever uses this deck _should_ shuffle before dealing
	// we don't yet support multiple decks
	//DIE("Tried to create multiple decks (unsupported)");
}

/**********************************************************************************
 Function Deck::~Deck()
 Date: 20180707 kriskoin :  Purpose: destructor
***********************************************************************************/
Deck::~Deck(void)
{
	// nothing to do for now
}

/**********************************************************************************
 Function Deck::CreateDeck(void)
 Date: 20180707 kriskoin :  Purpose: create the deck -- cards will be in order
***********************************************************************************/
void Deck::CreateDeck(void)
{
	// "crack" open a new deck...
	// run through and initialze all 52 cards.
	// they will be in order 2c,2d,2h,2s,3c --> Ad, Ah, As...  until shuffled
	int deck_index = 0;	// keep track of where we are in the deck
	for (Card rank = 0; rank < CARD_RANKS; rank++) {
		for (Card suit = 0; suit < CARD_SUITS; suit++) {
			_deck[deck_index++] = MAKECARD(rank,suit);
		}
	}
	// initialize card index
	_next_card_index = 0;
}

/**********************************************************************************
 Function Deck::ShuffleDeck(void)
 date: kriskoin 2019/01/01 Purpose: shuffle using default number of shuffles
***********************************************************************************/
void Deck::ShuffleDeck(void)
{
	#define RUNS_THROUGH_DECK	25  // how many times do we go through?
	ShuffleDeck(RUNS_THROUGH_DECK);
}

/**********************************************************************************
 Function Deck::ShuffleDeck(int number_of_shuffles)
 Date: 20180707 kriskoin :  Purpose: shuffle the deck of cards
***********************************************************************************/
void Deck::ShuffleDeck(int number_of_shuffles)
{
	// Determine the number of cards remaining in the deck
	int card_count = CARDS_IN_DECK - _next_card_index;
	if (card_count <= 0) {
		return;	// no cards left... no shuffling to do.
	}
  #if 0	//kriskoin: 	// Now shuffle all remaining cards
	for (int run_through = 0; run_through < number_of_shuffles; run_through++) {
		for (int card_index = 0; card_index < card_count; card_index++) {
			// find a random spot in the deck where to move this card
			int new_spot;  // we'll pick a new spot in the deck where to swap this card
			new_spot = (RNG_NextNumber() % card_count);
			// we allow it to swap with itself (ie, new_spot could equal current index)
			Card temp_card = _deck[_next_card_index + new_spot];
			_deck[_next_card_index + new_spot] = _deck[_next_card_index + card_index];
			_deck[_next_card_index + card_index] = temp_card;
		}
	}
  #else	//kriskoin: 	// Tests indicate that one pass is as good as 10 passes.
	// Shuffle all remaining cards in deck
	Card *base = _deck + _next_card_index;
	while (number_of_shuffles-- > 0) {
		for (int i = card_count-1 ; i > 0 ; i--) {
			int j = (RNG_NextNumber() % (i+1));
			Card t  = base[j];
			base[j] = base[i];
			base[i] = t;
		}
	}
  #endif
}

/**********************************************************************************
 Function Deck::DealNextCard()
 Date: 20180707 kriskoin :  Purpose: deals the next card off the deck
***********************************************************************************/
Card Deck::DealNextCard(void)
{
	if (_next_card_index == CARDS_IN_DECK) {
		// FATAL ERROR : We're out of cards
		DIE("Tried to deal another card but we're out of cards");
	} else {
		return _deck[_next_card_index++];
	}
}

/**********************************************************************************
 Function Deck::ValidateDeck(void)
 Date: 20180707 kriskoin :  Purpose: validate the intergrity of the deck
***********************************************************************************/
void Deck::ValidateDeck(void)
{
	#define CARD_IS_OK	1
	// validate the deck by filling a blank test_deck with markers
	zstruct(_test_deck);
	int card_index;
	for (card_index = 0; card_index < CARDS_IN_DECK; card_index++) {
		_test_deck[CARDINDEX(_deck[card_index])] = CARD_IS_OK;
	}
	// now they should all be OK -- if not, something trashed the deck
	for (card_index = 0; card_index < CARDS_IN_DECK; card_index++) {
		if (_test_deck[card_index] != CARD_IS_OK) {
			// FATAL ERROR : The deck has been stomped upon
			DIE("Deck has been damaged");
		}
	}
}

/**********************************************************************************
 Function Deck::GetCardsLeft(void)
 date: kriskoin 2019/01/01 Purpose: returns the number of cards left in the deck
***********************************************************************************/
int Deck::GetCardsLeft(void)
{
	return CARDS_IN_DECK - _next_card_index;
}

/**********************************************************************************
 Function Deck::GetCard(int card_index)
 date: kriskoin 2019/01/01 Purpose: pull a card out of the middle of the deck
***********************************************************************************/
Card Deck::GetCard(int card_index)
{
	if (card_index < 0 || card_index >= CARDS_IN_DECK) {
		return CARD_NO_CARD;	// trap bad index
	}
	return _deck[card_index];
}
