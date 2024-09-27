//*********************************************************
//
//	Waiting List routines.
//	Server side only.
//
// 
//
//*********************************************************

#define DISP 0

#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>		// Needed for CritSec stuff
#endif
#include <stdio.h>
#include <malloc.h>
#include "cardroom.h"
#include "pokersrv.h"

//*********************************************************
// https://github.com/kriskoin//
// Constructor/destructor for the Waiting List object
//
WaitList::WaitList(void)
{
	PPInitializeCriticalSection(&WaitListCritSec, CRITSECPRI_WAITLIST, "WaitList");
	EnterCriticalSection(&WaitListCritSec);
	entries = NULL;
	entries_max = entries_count = 0;
	LeaveCriticalSection(&WaitListCritSec);
}

WaitList::~WaitList(void)
{
	EnterCriticalSection(&WaitListCritSec);
	if (entries) {
		free(entries);
		entries = NULL;
	}
	entries_max = entries_count = 0;
	LeaveCriticalSection(&WaitListCritSec);
	PPDeleteCriticalSection(&WaitListCritSec);
	zstruct(WaitListCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Test if a player is allowed to be called to a seat.
// Tests connection state as well as whether they have been
// called to another table or not.
// Returns TRUE if they qualify, FALSE if they do not.
//
int WaitList::TestIfPlayerCanFillSeat(struct WaitListEntry *wle, WORD32 player_id)
{
	WORD32 called_to_table = 0;
	if (CardRoomPtr) {
		called_to_table = CardRoomPtr->TestIfPlayerCalledToTable(player_id);
	}
	if (called_to_table) {
		// If already being called to this table.  They will always qualify.
		// If already called to a different table.  They don't qualify to be called
		// to this table as well (only one table at a time).
		return called_to_table==wle->table_serial_number;
	}

	// If this is a tournament table they only qualify to be called to it if
	// they are not currently seated in any other tournaments.
	if (wle->chip_type==CT_TOURNAMENT) {
		if (CardRoomPtr) {
			WORD32 tournament_table_serial_number = CardRoomPtr->GetPlayerTournamentTable(player_id);
			if (tournament_table_serial_number &&
				tournament_table_serial_number != wle->table_serial_number)
			{
				return FALSE;	// does not qualify.
			}
		}
	}

	// Finally, they only get called if they're currently connected.
	if (GetPlayerConnectionState(player_id) > CONNECTION_STATE_POOR) {
		// Not well connected.  They do not qualify.
		return FALSE;
	}

	// Everything passed.  I guess they qualify.
	return TRUE;
}

//*********************************************************
// https://github.com/kriskoin//
// Add a filled in waiting list entry to the queue.  Re-allocates
// the list if more space is needed.
//
ErrorType WaitList::AddWaitListEntry(struct WaitListEntry *wle)
{
	if (ANONYMOUS_PLAYER(wle->player_id)) {
		Error(ERR_ERROR, "%s(%d) AddWaitlistEntry() was passed an anonymous player id ($%08lx).  Ignoring.", _FL, wle->player_id);
		return ERR_ERROR;
	}
	EnterCriticalSection(&WaitListCritSec);

	pr(("%s(%d) Add Waitlist Entry: player_id = $%08lx, stakes = %d, real_money = %d\n",
				_FL, wle->player_id, wle->desired_stakes, wle->real_money));
	// If this entry is already in the array, ignore this
	// request to add it again.
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; i++, e++) {
		struct WaitListEntry test_entry = *e;
		test_entry.request_sent_table = 0;	// always zero before memcmp().
		if (!memcmp(wle, &test_entry, sizeof(*wle))) {	// is this it?
			// Found a match.  Don't add it twice.
			pr(("%s(%d) AddWaitListEntry: ignoring Add request for duplicate entry.\n",_FL));
			LeaveCriticalSection(&WaitListCritSec);
			return ERR_NONE;
		}
	}

	// Determine if there is room in the array.
	if (entries_count >= entries_max) {
		// We need more space.  Double what we have now.
		int new_entries_max = max(8, entries_max)*2;
		if (entries) {	// if we already had something.
			pr(("%s(%d) AddWaitListEntry: re-allocating waitlist array to hold %d entries (we need space for %d).\n",
					_FL, new_entries_max, entries_count+1));
		}
		struct WaitListEntry *new_entries = (struct WaitListEntry *)malloc(sizeof(struct WaitListEntry)*new_entries_max);
		if (!new_entries) {
			Error(ERR_ERROR, "%s(%d) Cannot allocate space for bigger waiting list.", _FL);
			LeaveCriticalSection(&WaitListCritSec);
			return ERR_ERROR;
		}

		// If there is something in the old array, copy it over to the new one.
		if (entries && entries_count) {
			memmove(new_entries, entries, entries_count*sizeof(struct WaitListEntry));
		}
		free(entries);
		entries = new_entries;
		entries_max = new_entries_max;
	}

	// There's space now... add it to the end.
	entries[entries_count++] = *wle;
	LeaveCriticalSection(&WaitListCritSec);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Determine how many people are ahead of the passed entry.
// If wle->player_id is 0, it returns the # of people in the queue.
// If wle->player_id is non-zero, it returns that player's
// position in the queue (1..queue_len) (or 0 if not in queue).
//
int WaitList::GetWaitListPosition(struct WaitListEntry *wle)
{
	int count = 0;
	EnterCriticalSection(&WaitListCritSec);
	// Count how many matching entries there are.
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; i++, e++) {
		if (wle->desired_stakes == e->desired_stakes &&
			wle->chip_type == e->chip_type &&
			wle->game_rules == e->game_rules
		) {
			// Stakes match.  Check table.
			if (!e->table_serial_number || !wle->table_serial_number ||
				e->table_serial_number == wle->table_serial_number) {
				// Tables match (or wildcard)
				count++;	// another player in queue for this table.
				if (wle->player_id==e->player_id) {
					// We're done searching because we searched
					// up to our position in the queue.
					LeaveCriticalSection(&WaitListCritSec);
					return count;
				}
			}
		}
	}
	if (wle->player_id) {	// were we searching for a specific player?
		count = 0;	// yes, and we didn't find him, so he's not in the queue.
	}
	pr(("%s(%d) WaitListPosition for player $%08lx table %d is %d\n",
				_FL, wle->player_id, wle->table_serial_number, count));
	LeaveCriticalSection(&WaitListCritSec);
	return count;
}

//*********************************************************
// https://github.com/kriskoin//
// Move a player to the top of the waiting list if he's already on it.
//
void WaitList::MovePlayerToTop(WORD32 player_id)
{
	EnterCriticalSection(&WaitListCritSec);
	// Count how many matching entries there are.
	int top = 0;	// top of list is currently first entry.
	struct WaitListEntry *e = &entries[top+1];
	for (int i=top+1 ; i<entries_count ; i++, e++) {
		if (e->player_id==player_id) {
			// move this entry to position 'top'
			struct WaitListEntry t = *e;
			memmove(&entries[top+1], &entries[top], sizeof(*e)*(i-top));
			entries[top] = t;
			top++;			
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Count the number of willing players in a queue given
// an estimate of how many players there would be at a table.
//
int WaitList::CountPotentialPlayers(struct WaitListEntry *wle, int total_players)
{
	int count = 0;
	EnterCriticalSection(&WaitListCritSec);
	// Count how many matching entries there are.
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; i++, e++) {
		if (wle->desired_stakes == e->desired_stakes &&
			wle->chip_type == e->chip_type &&
			wle->game_rules == e->game_rules
		) {
			// Stakes match.  Check table.
			// If this player is already being asked to take a seat
			// at a different table, he's not elligible here.
			if (!e->request_sent_table
			   #if 0	// 2022 kriskoin
				 || e->request_sent_table == wle->table_serial_number
			   #endif
			) {
				if (!e->table_serial_number || !wle->table_serial_number ||
					e->table_serial_number == wle->table_serial_number) {
					// Tables match (or wildcard)
					if (total_players >= (int)e->min_players_required) {
						// There will be enough players for this guy to want
						// to play.
						if (TestIfPlayerCanFillSeat(wle, e->player_id)) {
							count++;	// another player in queue for this table.
						}
					}
				}
			}
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	return count;
}

//*********************************************************
// https://github.com/kriskoin//
// Remove a particular entry from the queue.  Only removes
// the first instance of it.
// Returns ERR_NONE if it was removed successfully, else an
// error code if not found.
//
ErrorType WaitList::RemoveWaitListEntry(struct WaitListEntry *wle)
{
	EnterCriticalSection(&WaitListCritSec);
	for (int pass=0 ; pass<3 ; pass++) {
		struct WaitListEntry *e = entries;
		for (int i=0 ; i<entries_count ; i++, e++) {
			int match = FALSE;
			if (pass==0) {
				// First pass... look for a very close match.
				if (wle->player_id==e->player_id &&
					wle->table_serial_number==e->table_serial_number &&
					wle->desired_stakes==e->desired_stakes &&
					wle->chip_type==e->chip_type &&
					wle->game_rules == e->game_rules
				) {
					match = TRUE;
				}
			} else if (pass==1) {	// second pass, match more tables.
				// 24/01/01 kriskoin:
				// If they unjoined a specific table and we didn't
				// find that table in pass one, search for an 'any'
				// table entry in the waiting list.
				if (wle->player_id == e->player_id &&
					!e->table_serial_number &&	// must be 'any'
					wle->desired_stakes == e->desired_stakes &&
					wle->chip_type == e->chip_type &&
					wle->game_rules == e->game_rules
				) {
					match = TRUE;
				}
			} else {	// third pass, match even more tables.
				// Basically, this ignores table serial numbers.
				if (wle->player_id == e->player_id &&
					wle->desired_stakes == e->desired_stakes &&
					wle->chip_type == e->chip_type &&
					wle->game_rules == e->game_rules
				) {
					match = TRUE;
				}
			}
			if (match) {	// is this it?
				// Found a match.  Remove it from the list.
				if (!wle->table_serial_number && e->request_sent_table) {
					//20000922: we matched something for a particular table,
					// make sure the caller knows which table we matched.
					wle->table_serial_number = e->request_sent_table;
				}
				if (!wle->table_serial_number && e->table_serial_number) {
					//20000922: we matched something for a particular table,
					// make sure the caller knows which table we matched.
					wle->table_serial_number = e->table_serial_number;
				}
				entries_count--;
				memmove(e, e+1, (entries_count - i)*sizeof(*e));	// scroll list down.
				LeaveCriticalSection(&WaitListCritSec);
				return ERR_NONE;
			}
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	return ERR_ERROR;	// error: not found.
}

//*********************************************************
// https://github.com/kriskoin//
// Remove all references to a particular player_id from the
// waiting list.  This would typically be used before
// destroying the player object.
//
ErrorType WaitList::RemoveAllEntriesForPlayer(WORD32 player_id)
{
	pr(("%s(%d) Removing all entries for player $%08lx.  entries_count = %d\n", _FL, player_id, entries_count));
	ErrorType err = ERR_ERROR;	// default to not found error
	EnterCriticalSection(&WaitListCritSec);
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; ) {
		if (e->player_id == player_id) {
			// Found a match.  Remove it from the list.
			entries_count--;
			memmove(e, e+1, (entries_count - i)*sizeof(*e));	// scroll list down.
			err = ERR_NONE;
		} else {
			 i++;
			 e++;
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	pr(("%s(%d) Done removing entries for player $%08lx.  new entries_count = %d\n", _FL, player_id, entries_count));
	return err;
}

//*********************************************************
// https://github.com/kriskoin//
// When a single seat is available, check the waiting list to
// see if anyone can take that seat.  A single table must be specified.
// Does not remove player from queue, but does flag him as waiting to get
// on that table (making him ineligible for other tables).
//
ErrorType WaitList::CheckFillSeat(struct WaitListEntry *input_wle, int players_at_table, struct WaitListEntry *output_wle, int *output_skipped_players)
{
	EnterCriticalSection(&WaitListCritSec);
	zstruct(*output_wle);
	struct WaitListEntry *e = entries;
	*output_skipped_players = 0;
	for (int i=0 ; i<entries_count ; i++, e++) {
		if (e->desired_stakes == input_wle->desired_stakes &&
			e->chip_type == input_wle->chip_type &&
			e->game_rules == input_wle->game_rules &&
			players_at_table >= e->min_players_required &&
			(!e->table_serial_number || e->table_serial_number==input_wle->table_serial_number)
		) {
			// We've found the right stakes and a suitable desired table
			// serial number.
			// If this player is already being asked to take a seat
			// at a different table, he's not eligible here.
			if (!e->request_sent_table
			  #if 0	//kriskoin: 				 || e->request_sent_table == input_wle->table_serial_number
			  #endif
			) {
				// This one can take the seat if he's online and not being
				// called to another table.
				if (TestIfPlayerCanFillSeat(input_wle, e->player_id)) {
					// Player is currently well connected... give him the seat.
					pr(("%s(%d) Player $%08lx qualifies to fill this seat.\n", _FL, e->player_id));
					e->request_sent_table = input_wle->table_serial_number;	// this table is now all he qualifies for.
					*output_wle = *e;
					break;
				} else {
					(*output_skipped_players)++;
					pr(("%s(%d) Player $%08lx qualifies to fill this seat but he's disconnected.\n", _FL, e->player_id));
				}
			}
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Count how many times a player is in a list.
//
int WaitList::CountPlayerEntries(WORD32 player_id)
{
	int count = 0;
	EnterCriticalSection(&WaitListCritSec);
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; i++, e++) {
		if (e->player_id == player_id) {
			count++;
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	return count;
}

//*********************************************************
// https://github.com/kriskoin//
// Determine which player_id is in a particular waiting list
// position (1st in line, 2nd, etc.).  This can be used to build
// a list of the players in line.
// Input: wle is the same as GetWaitListPosition
//		  position counts from 0 (0=1st in line).
// Returns: player_id or 0 if no player in that position.
//
WORD32 WaitList::GetPlayerInWaitListPosition(struct WaitListEntry *wle, int position)
{
	int count = 0;
	EnterCriticalSection(&WaitListCritSec);
	// Count how many matching entries there are.
	struct WaitListEntry *e = entries;
	for (int i=0 ; i<entries_count ; i++, e++) {
		if (wle->desired_stakes == e->desired_stakes &&
			wle->chip_type == e->chip_type &&
			wle->game_rules == e->game_rules
		) {
			// Stakes match.  Check table.
			if (!e->table_serial_number || !wle->table_serial_number ||
				e->table_serial_number == wle->table_serial_number) {
				// Tables match (or wildcard)
				if (count == position) {
					// We're done searching because we searched
					// up to our position in the queue.
					WORD32 player_id = e->player_id;
					LeaveCriticalSection(&WaitListCritSec);
					return player_id;
				}
				count++;	// another player in queue for this table.
			}
		}
	}
	LeaveCriticalSection(&WaitListCritSec);
	return 0;
}
