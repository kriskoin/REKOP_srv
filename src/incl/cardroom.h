//*********************************************************
//
//	Card Room routines.
//	Server side only.
//
// 
//
//*********************************************************

#ifndef _CARDROOM_H_INCLUDED
#define _CARDROOM_H_INCLUDED

#ifndef _TABLE_H_INCLUDED
  #include "table.h"
#endif

#ifndef _OPA_H_INCLUDED
  #include "OPA.h"
#endif

#if WIN32
  #define USE_SELECT_FOR_INPUT	1
#else
  #define USE_SELECT_FOR_INPUT	1	// try to use select() for input?
#endif

#define MAX_SERVICEABLE_PLAYERS	16300

extern time_t NextAdminStatsUpdateTime;
extern struct AdminStats CurrentAdminStats[];
extern struct PacketStats CurrentPacketStats[DATATYPE_COUNT];

extern void WriteServerVars(void);	// pokersrv function

struct TableType {
	ClientDisplayTabIndex client_display_tab_index;
	GameRules game_rules;
	int game_disable_bits;
	int max_players;
	int small_blind;
	int big_blind;
	ChipType chip_type;	// the type of chips this table plays for
	RakeType rake_profile;
};

struct WaitListEntry {
	WORD32 player_id;			// player this entry relates to.
	WORD32 table_serial_number;	// 0 if 'any' table, else specific table number.
	WORD32 request_sent_table;	// set if a request has been sent to a player from this table serial num
	INT32  desired_stakes;		// desired stakes (big_blind_chips)
	BYTE8  min_players_required;// min # of players (including self) player requires before joining 'any' table.
	ChipType chip_type;			// the chip type we're looking for
	GameRules game_rules;		// the game_rules that apply for this entry
};

class WaitList {
public:
	WaitList(void);
	~WaitList(void);

	// Test if a player is allowed to be called to a seat.
	// Tests connection state as well as whether they have been
	// called to another table or not.
	// Returns TRUE if they qualify, FALSE if they do not.
	int WaitList::TestIfPlayerCanFillSeat(struct WaitListEntry *wle, WORD32 player_id);

	// Add a filled in waiting list entry to the queue.  Re-allocates
	// the list if more space is needed.
	ErrorType AddWaitListEntry(struct WaitListEntry *wle);

	// Determine how many people are ahead of the passed entry.
	// If wle->player_id is 0, it returns the # of people in the queue.
	// If wle->player_id is non-zero, it returns that player's
	// position in the queue (1..queue_len) (or 0 if not in queue).
	int GetWaitListPosition(struct WaitListEntry *wle);

	// Determine which player_id is in a particular waiting list
	// position (1st in line, 2nd, etc.).  This can be used to build
	// a list of the players in line.
	// Input: wle is the same as GetWaitListPosition
	//		  position counts from 0 (0=1st in line).
	// Returns: player_id or 0 if no player in that position.
	WORD32 GetPlayerInWaitListPosition(struct WaitListEntry *wle, int position);

	// Count the number of willing players in a queue given
	// an estimate of how many players there would be at a table.
	int CountPotentialPlayers(struct WaitListEntry *wle, int total_players);

	// Move a player to the top of the waiting list if he's already on it.
	void WaitList::MovePlayerToTop(WORD32 player_id);

	// Remove a particular entry from the queue.  Only removes
	// the first instance of it.
	// Returns ERR_NONE if it was removed successfully, else an
	// error code if not found.
	ErrorType RemoveWaitListEntry(struct WaitListEntry *wle);

	// Remove all references to a particular player_id from the
	// waiting list.  This would typically be used before
	// destroying the player object.
	ErrorType RemoveAllEntriesForPlayer(WORD32 player_id);

	// When a single seat is available, check the waiting list to
	// see if anyone can take that seat.  A single table must be specified.
	// Does not remove player from queue, but does flag him as waiting to get
	// on that table (making him ineligible for other tables).
	ErrorType WaitList::CheckFillSeat(struct WaitListEntry *input_wle, int players_at_table, struct WaitListEntry *output_wle, int *output_skipped_players);

	// Count how many times a player is in a list.
	int CountPlayerEntries(WORD32 player_id);

private:
	// CritSec for making each Waiting List itself thread safe.
	PPCRITICAL_SECTION WaitListCritSec;

	struct WaitListEntry *entries;	// ptr to array of entries
	int entries_max;				// max # of slots in entries[]
	int entries_count;				// current # of entries in entries[]
};

class CardRoom {
public:
	CardRoom(OPA* OPApointer);
	~CardRoom(void);
    OPA *theOPAptr; //connect to data base 
	// Cardroom main loop.  Once the I/O threads are all launched,
	// the main loop takes care of table creation and all that.
	// This function doesn't return until server shutdown.
	ErrorType MainLoop(void);

	// Launch the thread which creates the listen socket and accepts
	// incoming connections.
	ErrorType LaunchAcceptThread(void);

	// Launch the thread which handles I/O for Player objects.
	ErrorType LaunchPlayerIOThread(void);

	// Entry point for the accept() thread.
	// Do not call this function externally.
	void AcceptThreadEntry(void);

	// Entry point for the Player I/O thread.
	// Do not call this function externally.
	void PlayerIOThreadEntry(void);

	// Any time a player object is going to be deleted, it MUST
	// be done using this function so that anyone who relies on
	// player pointers has a chance to be told it is going to be
	// deleted and can no longer use it.
	// Sets src ptr to null from inside the appropriate critical sections.
	void CardRoom::DeletePlayerObject(class Player **plr);

	// Find a connected Player object ptr given a player's ID.
	Player *FindPlayer(WORD32 player_id);

	// Test if a player is connected given just their player_id
	int TestIfPlayerConnected(WORD32 player_id);

	// Test if a player is being called to a table.  Return
	// table serial number if they are.
	WORD32 CardRoom::TestIfPlayerCalledToTable(WORD32 player_id);

	// Determine which tournament table (if any) a player is seated at.
	WORD32 CardRoom::GetPlayerTournamentTable(WORD32 player_id);

	// Return our best guess about the quality of the connection to
	// a player given just their 32-bit player_id.
	// Returns CONNECTION_STATE_* (see player.h)
	int CardRoom::GetPlayerConnectionState(WORD32 player_id);

	// If a player is connected, re-send their client info to them.
	// (usually because something changed).
	// The client info INCLUDES their account record.
	// If they're not connected, don't do anything.
	void CardRoom::SendClientInfo(WORD32 player_id);

	// Send a chat structure to admin clients for monitoring
	void CardRoom::SendAdminChatMonitor(GameChatMessage *egcm, char *table_name, ChipType chip_type);

	// Return the next sequential game serial number (and increment for next time)
	WORD32 NextGameSerialNumberAndIncrement(void);

	// Set/Get serial number for next game starting up
	WORD32 GetNextGameSerialNumber(void);	// don't increment
	void SetNextGameSerialNumber(WORD32 serial_num);

	// Flags to indicate when we need to update our waiting lists.
	// They need to get updated at exactly these times:
	//	1. When a seat becomes available at any table.
	//	2. When a new player gets added to a waiting list.
	//	3. When a new table gets created.
	int update_waiting_list[MAX_CLIENT_DISPLAY_TABS];

	// Check for any previous player objects for a player_id.  Pass the
	// server_socket on to them if found.
	void CheckForPreviousPlayerLogin(Player *new_player_object, WORD32 new_player_id);

	// Get a table ptr from a table serial number
	// Returns NULL if not found.
	// HK990621 moved from private to public
	Table *TableSerialNumToTablePtr(WORD32 serial_num);

	// T/F if a game serial number is currently being played
	int IsGameStillActive(WORD32 game_serial_number);

	// Send a shutdown message to all connected players.
	void SendShutdownMessageToPlayers(int shutdown_client_flag, char *message);

	// Send a struct MiscClientMessage to all connected players.
	void CardRoom::SendBroadcastMessage(struct MiscClientMessage *mcm);

	// Move a player to the top of any waiting lists they're joined to
	void CardRoom::MovePlayerToTopOfWaitLists(WORD32 player_id);
	
	// Process a JoinTable request from a player
	void CardRoom::ProcessPlayerJoinTableRequest(struct CardRoom_JoinTable *jt, Player *p, int *work_was_done_flag);

	// Validate the table name list
	ErrorType CardRoom::ValidateTableNames(void);

	// Send a dealer message to all tables (24/01/01 kriskoin:
	void SendDealerMessageToAllTables(char *msg, int text_type, int gcm_flags);

	volatile int connected_players;	// # of active Player objects
	int active_player_connections;	// # of player objects actually connected to live sockets right now
	int active_good_connections;	// # of player objects currently with good connections
	int active_tables;				// # of active tables (for stat purposes ONLY!)
	int active_real_tables;			// # of active real money tables (for stat purposes ONLY!)
	int active_tables_temp;			// # of active tables while counting them
	int active_real_tables_temp;	// # of active real money tables while counting them
	int active_unseated_players;	// # of connected players not seated and not idle
	int connected_idle_players;		// # of connected players not seated but not actively doing anything
	int multi_seated_players;		// # of players seated at more than one table

	int daily_peak_active_players;	// for summary email purposes only
	int daily_peak_active_tables;	// for summary email purposes only
	int daily_peak_active_real_tables;	// for summary email purposes only
	int daily_low_active_players;		// for summary stat purposes only
	int daily_low_active_tables;		// for summary stat purposes only
	int daily_low_active_real_tables;	// for summary stat purposes only
//	int bad_beat_count;				// how many bad beats today
//	int bad_beat_payout;			// how much have we have paid out in bad beats today
	time_t daily_peak_active_players_time;
	time_t daily_peak_active_tables_time;
	time_t daily_peak_active_real_tables_time;
	int tournaments_played_today;		// # of tournaments started today
	WORD32 lowest_active_game_number;	// lowest game serial number being played

	// Linear array of copies to the ptrs to all tables (see table_tree
	// for the master copy in tree form).
	Table **tables;		// ptrs to each table (no NULL ptrs)
	int table_count;	// # of entries in the tables[] array.

	PPCRITICAL_SECTION CardRoomCritSec;	// CritSec for making the Card Room itself thread safe.
	//PPCRITICAL_SECTION TableListCritSec;// table list access only
	PPCRITICAL_SECTION PlrInputCritSec;	// controls access to the plr input thread (formerly the player I/O thread)

	// Add a new table to our card room.
	ErrorType AddNewTable(
				ClientDisplayTabIndex client_display_tab_index,
				GameRules game_rules,
				int max_number_of_players,
				int small_blind_amount, int big_blind_amount, char *table_name,
				int add_computer_players_flag, ChipType chip_type,
				int game_disable_bits, RakeType rake_profile);

private:
	volatile Player **players;
	int max_connected_players;		// # of ptrs available in the current players[] array.

	// Accept incoming connections and create Player objects for them
  #if INCL_SSL_SUPPORT
	ErrorType CardRoom::AcceptConnections(ServerListen *sl, int *work_was_done_flag, int port_number, void *ssl_ctx);
  #else
	ErrorType CardRoom::AcceptConnections(ServerListen *sl, int *work_was_done_flag, int port_number);
  #endif

	// Process the player I/O for one player.
	// Internal to the player input thread.
	void CardRoom::ProcessPlayerDataInput(Player *p);

	// A binary tree for all the tables (one tree for all types)
	// This is the master list of tables.  The tables[] array is
	// a linear copy of this tree.
	BinaryTree table_tree;

	// Recursively walk through a tree of tables and add each Table * to
	// our tables[] array.
	// CardRoomCritSec must be held before calling this function.
	void AddTreeToTableArray(struct BinaryTreeNode *tree);

	// Rebuild our tables[] array.
	void RebuildTableArray(void);

	// Delete a table from the cardroom.
	void DeleteTable(WORD32 table_serial_number);

	// Update all tables (do the stuff which requires low latency)
	void UpdateTables_LowLatencyWork(int *work_was_done_flag);

	// Update all tables (do the stuff which does not require low latency)
	void UpdateTables_HighLatencyWork(int *work_was_done_flag);

	// Recursively walk through a tree of tables and update the waiting
	// list for each one.
	// Must be executed from the main card room thread.  It does NOT
	// grab the CritSec for the tree structure.  This is OK because
	// the tree is only modified by this particular thread.
	void UpdateTableWaitLists(ClientDisplayTabIndex client_display_tab_index);

	// Update everything related to waiting lists (if necessary).
	void UpdateWaitingLists(void);

	// Recursively walk through a tree of tables and assign each one
	// of the right type a sequential index (for the table_summary_list[]'s).
	// Must be executed from the main card room thread.  It does NOT
	// grab the CritSec for the tree structure.  This is OK because
	// the tree is only modified by this particular thread.
	void CountAndIndexTables(ClientDisplayTabIndex client_display_tab_index, int *counter);

	// Recursively walk through a tree of tables and for this game type,
	// update each one's game summary info, regardless of whether it has
	// changed recently.  This is used when rebuilding the entire table.
	// Must be executed from the main card room thread.  It does NOT
	// grab the CritSec for the tree structure.  This is OK because
	// the tree is only modified by this particular thread.
	void UpdateTableSummaryInfo(ClientDisplayTabIndex client_display_tab_index);

	// Recursively walk through a tree of tables and update the waiting
	// list length for each one.
	// Must be executed from the main card room thread.  It does NOT
	// grab the CritSec for the tree structure.  This is OK because
	// the tree is only modified by this particular thread.
	void UpdateTableWaitListCounts(ClientDisplayTabIndex client_display_tab_index);

	// Rebuild all the table summaries for a particular game type.
	void RebuildTableSummaries(ClientDisplayTabIndex client_display_tab_index);

	// Update main loop background tasks (every 1 or 2s)
	void CardRoom::UpdateMainLoopBgndTasks(int *work_was_done_flag);

	// If any summary data has changed, update the serial numbers
	// and send out it out to all clients.
	void UpdateSummaryData(int *work_was_done_flag);

	// Send a struct CardRoom_TableSummaryList to player p.
	void SendSummaryListToPlayer(ClientDisplayTabIndex client_display_tab_index, Player *p);

	// Send a struct CardRoom_TableInfo to player p.
	void SendTableInfoToPlayer(WORD32 table_serial_number, Player *p);

	// Recursively walk through the tree of tables and reset all of
	// the table_info_changed flags.
	void ClearTableInfoChangedFlags(void);

	// Count how many tables and 'empty' tables there are and mark any extras
	// for deletion.
	void CountEmptyTables(struct TableType *tt, int *table_count, int *empty_table_count);

	// Take care of over-all table management.  Create new tables,
	// delete old (empty) tables, etc.
	void ManageTables(void);

	// Handle any processing for individual players, such as joining
	// and unjoining games, etc.
	void UpdatePlayers_LowLatencyWork(int *work_was_done_flag);
	void UpdatePlayers_HighLatencyWork(int *work_was_done_flag);

	// If a disconnected player has been kicked off all his tables
	// and waiting lists, delete the player object; it's no longer needed.
	void DeleteDisconnectedPlayers(void);

	// Retrieve an account balance based solely on the user id,
	int CardRoom::GetAccountBalance(char *user_id);

	// Every night at midnight, send out a summary email
	void CardRoom::SendSummaryEmailIfNecessary(void);

  #if 0	// 24/01/01 kriskoin:
	// Send a dealer message to all tables
	void SendDealerMessageToAllTables(char *msg, int text_type, int gcm_flags);
  #endif

	WORD32 next_summary_email_seconds;

	// Waiting lists for each game type
	WaitList waiting_lists[MAX_CLIENT_DISPLAY_TABS];

	// Serial number to use for the next table we create.
	WORD32 next_table_serial_number;
	// Serial number to use for the next game we create.
	WORD32 next_game_serial_number;

	WORD32 total_bytes_sent;	// grand total of bytes transmitted to all players (over TCP)
	WORD32 total_bytes_received;// grand total of bytes received from all players (over TCP)

	struct CardRoom_TableSummarySerialNums summary_serial_nums;
	int summary_info_changed[MAX_CLIENT_DISPLAY_TABS];
	int write_out_server_vars_now;
	// Ptrs to the malloc()'d versions of the table summary lists.
	CardRoom_TableSummaryList *table_summary_lists[MAX_CLIENT_DISPLAY_TABS];

	WORD32 kill_table;	// if set, indicates serial # of a table which needs deleting.
	WORD32 next_manage_tables_time;	// SecondCounter when ManageTables() should do work again.
	WORD32 next_summary_data_check_time;	// SecondCounter when we should check summary data serial numbers again.
	WORD32 next_mainloop_bgndtask_time;		// Second counter when main loop should try running some of the background tasks again

	int comm_thread_finished_flag;		// set by comm thread when it is finished
	int accept_thread_finished_flag;	// set by accept thread when it is finished.
	int comm_thread_shutdown_flag;		// set to ask comm thread to shutdown
	int accept_thread_shutdown_flag;	// set to ask accept thread to shutdown

	int iPlayMoneyTableCount;			// current # of play money tables
	int iRealMoneyTableCount;			// current # of real money tables
	int iTournamentTableCount;			// current # of tournament tables

	int cardroom_threadid;				// the thread id of the main loop.

	WORD32 client_count_newer;			// # of clients connected with newer than current version
	WORD32 client_count_current;		// # of clients connected with the current version
	WORD32 client_count_old;			// # of clients connected with old version
	WORD16 table_names_used;			// # of real money table names currently used
	WORD16 table_names_avail;			// # of real money table names available to be used (total)

	WORD32 peak_active_player_connections_time;	// SecondCounter when active_player_connections last peaked.
	WORD32 next_routing_update_message;	// during routing problem: time when stats should be printed again
	FILE *routing_update_email_fd;		// fd for routing problem email (if open)
	char routing_update_email_fname[MAX_FNAME_LEN];

	double avg_response_time_real_money;// avg response time for entire cardroom (real money tables)
	double avg_response_time_play_money;// avg response time for entire cardroom (play money tables)
	double accept_avg_ms;				// average ms needed to process a single Accept().
	double mainloop_avg_ms;				// average ms needed to process entire main loop
	double inputloop_avg_ms;			// average ms needed to process entire input loop
	WORD32 total_table_update_ms;		// total # of ms used while updating tables
	WORD32 total_tables_updated;		// total # of tables updated (related to total_table_update_ms)

	#define ADMIN_CHAT_QUEUE_LEN	100
	int admin_chat_queue_head;
	int admin_chat_queue_tail;
	struct GameChatMessage admin_chat_queue[ADMIN_CHAT_QUEUE_LEN];

  #if USE_SELECT_FOR_INPUT
	fd_set readset;	// file descriptor set for read waiting with select().
	int max_fd;		// max file descriptor in the read set.
	int new_sockets_for_readset;	// set if a new fd has been opened (incoming socket)
  #endif
};

extern CardRoom *CardRoomPtr;		// global cardroom ptr

#endif // !_CARDROOM_H_INCLUDED
