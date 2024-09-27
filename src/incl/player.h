//*********************************************************
//
//	Player routines.
//	Server side only.
//
// 
//
//*********************************************************

#ifndef _PLAYER_H_INCLUDED
#define _PLAYER_H_INCLUDED

#ifndef _LLIP_H_INCLUDED
  #include "llip.h"
#endif
#ifndef _GAME_H_INCLUDED
  #include "game.h"
#endif

#ifndef _OPA_H_INCLUDED
  #include "OPA.h"
#endif

#define CLIENT_KEEP_ALIVE_SPACING	(30)	// Time between keep alives from client
#define PLAYER_PINGS_TO_RECORD	10			// keep track of last n pings for this client

enum CONNECTION_STATE {
	CONNECTION_STATE_GOOD,	// well connected, no problems.
	CONNECTION_STATE_POOR,	// some delays. we may not have heard from the guy for a while (10s?)
	CONNECTION_STATE_BAD,	// longer delays. we may not have heard from the guy for a while
	CONNECTION_STATE_LOST,	// connection has been lost.  Socket closed or it's been WAY too long since we've heard from them
};

struct JoinedTableData {
	WORD32 table_serial_number;	// table serial number for this entry
	char table_name[MAX_TABLE_NAME_LEN];
	struct GameCommonData gcd;	// most recent game common data sent to player for this table
	struct GamePlayerData gpd;	// most recent game player data sent to player for this table
	struct GamePlayerInputRequest input_request;// most recent input request sent to player for this table
	struct GamePlayerInputResult input_result;	// most recent input result structure received from remote
	struct GamePlayerInputResult input_result2;	// copy of most recent input result structure received from remote (never cleared)
	struct ClientStateInfo client_state_info;	// the most recent copy of ClientStateInfo received from remote
	int watching_flag;							// set if we're just watching.
	WORD32 joined_time;			// SecondCounter when player sat down or started watching this table
};

class Player {
public:
	Player(void *calling_cardroom_ptr);
	~Player(void);

	// When we get created we get a unique 32-bit player ID associated with
	// us.  This is guaranteed to be unique amongst all players.  This value
	// never changes for the life of this object (except when migrating 
	// from anonymous to logged in).
	ErrorType SetPlayerInfo(WORD32 input_player_id);

	// When a client calls up, they get a ServerSocket. That ServerSocket is
	// then used to login and validate a player.  Once that happens, a Player
	// object is created (if necessary) or the previous Player object (if they
	// are reconnecting) is used.  All future communications with the player
	// will go through the player object.  The CardRoom will keep track of
	// which Player objects are currently allocated and which 32-bit player_id
	// they belong to.  If we previously had a ServerSocket then we should
	// destroy it because clearly the user has reconnected from somewhere else.
	ErrorType SetServerSocket(ServerSocket **input_server_socket);

	// Return whether the player's client socket is currently connected.
	int Connected(void);

	// Return how long a player has been disconnected for (or 0 if
	// still connected or never connected)
	int DisconnectedSeconds(void);

	// Return how many seconds overdue contact with this player is (in seconds).
	// Returns 0 if we're in good contact, otherwise returns the number
	// of seconds it is past when we expected to hear from them.
	int OverdueContactTime(void);

	// Return our best guess about the quality of the connection to
	// this player.  Returns CONNECTION_STATE_* (see player.h)
	CONNECTION_STATE Player::CurrentConnectionState(void);

	// Typically it's the card room that will cause us to join, watch, or leave a table.
	// It has not yet been determined if these functions will also be used to
	// notify the client of the change in joined table status.
	ErrorType JoinTable(struct GameCommonData *gcd, int watching_flag, char *table_name);

	// Leave a table we are currently playing at or watching.
	// Usually it's the table which asks us to leave in between games.
	ErrorType LeaveTable(WORD32 table_serial_number);
	ErrorType LeaveTable(WORD32 table_serial_number, int notify_player_flag);

	// This stuff will probably get called by the Table.
	ErrorType SendGameCommonData(struct GameCommonData *game_common_data);	// Send data to the player
	ErrorType SendGamePlayerData(struct GamePlayerData *game_player_data);	// Send data to the player

	// Send a ping to this client.  Only sends if client is new enough to handle it.
	ErrorType Player::SendPing(void);

	// Send an input request structure to the player
	ErrorType SendPlayerInputRequest(struct GamePlayerInputRequest *game_player_input_request);
	ErrorType SendPlayerInputRequestCancel(struct GamePlayerInputRequestCancel *gpirc);

	// Maintain a flag to indicate if we're waiting for input from this player.
	// This flag is NOT reliable, it's merely used as a hint for latency reduction
	// techiques.
	int waiting_for_input;

	int send_account_info;	// set if player object should re-send data to player
	int send_client_info;	// set if player object should re-send data to player

	// Send (and cache) a seat avail notification (only one outstanding per client)
	ErrorType Player::SendSeatAvail(struct CardRoom_SeatAvail *sa);
	struct CardRoom_SeatAvail saved_seat_avail;

	// Send a struct MiscClientMessage to this player.
	ErrorType SendMiscClientMessage(int message_type, WORD32 table_serial_number, WORD32 flags, 
		WORD32 misc_data_1, WORD32 misc_data_2, WORD32 misc_data_3, char *fmt_message, ...);

	// Retrieve an input result structure from the player (if ready).
	// *output_ready_flag is set to TRUE or FALSE depending on whether
	// the output is ready or not.  If it is ready, then the input result
	// is copied to *output_game_player_input_result.
	ErrorType GetPlayerInputResult(WORD32 table_serial_number, int *output_ready_flag, struct GamePlayerInputResult *output_game_player_input_result);

	// Verify that we're actually joined (or watching) a particular table
	ErrorType VerifyJoinedTable(WORD32 table_serial_number, int *output_watching_flag);
	ErrorType VerifyJoinedTable(WORD32 table_serial_number);

	// Count the number of tables we're actually seated at.
	int Player::CountSeatedTables(void);

	// Determine how long it has been since we heard from this player (in seconds)
	int Player::TimeSinceLastContact(void);
	int idle_flag;	// flag to indicate if this player's client program is idle or not

	// Read any incoming packets from the client and deal with them
	// appropriately.  This function must be called periodically by
	// some higher level object such as the CardRoom.
	// It's quite possible (or even likely) this will be called from
	// a completely separate thread than the rest of the Player() stuff.
	ErrorType ReadIncomingPackets(void);

	// Send an arbitrary data structure to the remote client.
	ErrorType SendDataStructure(int data_type, void *data_structure_ptr, int data_structure_len);
	ErrorType SendDataStructure(int data_type, void *data_structure_ptr, int data_structure_len, int disable_packing_flag, int bypass_player_queue_flag);

	// Send the player all the data we have on a particular table
	// he's joined to.
	void SendAllTableData(int table_index);
	void SendAllTableData(WORD32 table_serial_number);

	// Take a table serial number and return the appropriate index into the
	// JoinedTables[] array.
	int SerialNumToJoinedTableIndex(WORD32 table_serial_number, int notjoined_ok_flag);
	int SerialNumToJoinedTableIndex(WORD32 table_serial_number);

	// Get a ptr to the CLientStateInfo structure given a table serial number.
	// Returns NULL if we're not joined to the table.
	struct ClientStateInfo *Player::GetClientStateInfoPtr(WORD32 table_serial_number);


	OPA *theOPA; //connect to data base
	WORD32 player_id;		// the unique 32-bit player ID associated with this player.
	char user_id[MAX_PLAYER_USERID_LEN];
	char City[MAX_COMMON_STRING_LEN];

	char   email_address[MAX_EMAIL_ADDRESS_LEN]; ////

	BYTE8  Gender;		// GENDER_MALE/GENDER_FEMALE/etc (enum type in pplib.h)
	WORD32 RealInBank;	// how many real chips he has in the bank
	WORD32 RealInPlay;	// how many real chips he has in play
	WORD32 FakeInBank;	// how many fake chips he has in the bank
	WORD32 FakeInPlay;	// how many fake chips he has in play
	WORD32 EcashCredit;	// pending ecash credit
	int CreditFeePoints;// accumulating points for ecash fee credit
	int LoginStatus;	// see pplib.h LOGIN_* definitions
	BYTE8 priv;			// account privilege level (ACCPRIV_*)

	// notification and chip management functions
	void BuyingIntoTable(WORD32 chips, ChipType chip_type);
	void LeavingTable(WORD32 chips, ChipType chip_type);
	void ChipBalancesHaveChanged(void);	// notify player object that they've changed
	void ChipBalancesHaveChanged(int send_it_now);

  #if 0	//kriskoin: 	// return T/F for player being allowed to go automatically all-in
	int Player::AllowedAutoAllIn(void);
  #endif

	// Count the number of all-in's allowed for this player.
	int Player::AllowedAutoAllInCount(void);

	// Keep track of when this player object last got timed out on an input
	// request. This value is local to the player object and has nothing
	// to do with the player database.
	void Player::SaveTimeOutTime(void);

	// Auto-block this player's current computer serial number
	// and issue alerts as necessary.
	void Player::AutoBlock(char *trigger_reason);
	void Player::AutoBlock(char *trigger_reason, char *override_user_id, WORD32 override_player_id);

	// Loop through an array of player id's associated with us and handle:
	//  - auto-blocking
	//  - login alerts
	//  - chat squelching
	// Returns TRUE if we should be blocked (auto-block or otherwise)
	// (mainly used for bailing on cc purchases)
	// If player_id should be auto-blocked, it sets *output_auto_block_flag = TRUE;
	// (call can then call plr->AutoBlock(reason) to perform the blocking).
	int Player::ValidateAgainstRelatedAccounts(WORD32 *player_id_array, int count, int *output_auto_block_flag);

	// Check if the SDBRECORD_FLAG_NO_CASHIER bit has been set and
	// send a message to the client if appropriate.
	// returns TRUE if the cashier is disabled for this client only,
	// returns FALSE otherwise.
	int Player::TestIfCashierDisabledForClient(void);

	// Tony Tu, Dec 21, 2001
	// Check if the SDBRECORD_FLAG_NO_CASHOUT bit has been set and
	// send a message to the client if appropriate.
	// returns TRUE if the cash out is disabled for this client only,
	// returns FALSE otherwise.
	int Player::TestIfCashOutDisabledForClient(void);

	// The ServerSocket object we'll use to communicate with the player.
	// We will be the ones who are ultimately in control of this object,
	// which means that once it has been passed to us, we are responsible
	// for deleting it when we are done.  If we suddenly get connected to a
	// different ServerSocket, we must delete the previous one.
	ServerSocket *server_socket;
	WORD32 socket_set_time;			// SecondCounter when this player object was given the server_socket ptr.

	WORD32 next_send_attempt_ms;	// minimum GetTickCount() when we should try ProcessSendQueue() again.

	// VersionNumber struct for the remote client
	struct VersionNumber client_version_number;

	int JoinedTableCount;	// # of tables in the JoinedTables[] array

	// The JoinedTables[] array is always filled from the bottom up.
	// There are no empty slots. JoinedTableCount tells how many slots
	// contain data.
	struct JoinedTableData JoinedTables[MAX_GAMES_PER_PLAYER];

	// Flags for which game types the player wants a
	// CardRoom_TableSummaryList for.
	BYTE8 requesting_table_summary_list[MAX_CLIENT_DISPLAY_TABS];

	WORD32 table_info_request_1;	// serial # of table we want a CardRoom_TableInfo structure for
	WORD32 table_info_request_2;	// serial # of table we want a CardRoom_TableInfo structure for
	WORD32 table_info_subscription;	// serial # of table we want to subscribe to CardRoom_TableInfo structure for

	// Pending requests to join or unjoin tables (for playing or watching).
	int pending_joins_count;
	struct CardRoom_JoinTable pending_joins[MAX_GAMES_PER_PLAYER];

	// Pending requests to join or unjoin waiting lists
	int pending_waitlist_count;
	struct CardRoom_JoinWaitList pending_waitlists[MAX_GAMES_PER_PLAYER];

	// Pending chat messages
	#define PLAYER_CHAT_QUEUE_LEN	8
	struct GameChatMessage chat_queue[PLAYER_CHAT_QUEUE_LEN];
	int chat_queue_head;
	int chat_queue_tail;

	// CritSec to make Player class calls thread safe.
	PPCRITICAL_SECTION PlayerCritSec;

	int last_input_processing_ticks;	// GetTickCount() when input was last processed
	int last_low_latency_update_processing_ticks;	// GetTickCount() when cardroom thread last updated us (low latency work)
	int last_high_latency_update_processing_ticks;	// GetTickCount() when cardroom thread last updated us (high latency work)
	int update_now_flag;				// set when we want the cardroom to update us asap
	WORD32 input_result_ready_table_serial_number;	// table serial num last input result was received for (if any)

	void *cardroom_ptr;	// ptr to the CardRoom which created us.

	WORD32 ip_address;					// player's current IP address in IPADDRESS format
	char ip_str[MAX_COMMON_STRING_LEN];	// player's current IP address converted to text for display purposes.

	// Ping related stuff...
	struct PingResult {
		WORD32 time_of_ping;			// SecondCounter when ping result received.
		WORD32 duration_for_ping;		// ms for ping packet to make round trip.
	} PingResults[PLAYER_PINGS_TO_RECORD];
	WORD32 PingSendTimes[PLAYER_PINGS_TO_RECORD];	// ms counter when pings sent
	WORD32 last_ping_sent_time;			// SecondCounter when a ping was last sent
	struct ClientPlatform client_platform;
	WORD32 suppress_tournament_email_serial_number;	// serial number of tournament for which we want no email summary
	WORD32 tournament_table_serial_number;	// table_serial_number of any tournament tables we're SEATED at.

	// storage for ONE barsnack request.  If two come in from the client before
	// the first can be processed, the second one will overwrite the first and
	// the first will never be processed.
	WORD32 bar_snack_request_table;		// non-zero if a request exists
	BAR_SNACK bar_snack_request;

	volatile unsigned int serial_num;	// a sequential number for each player object used for determining which I/O thread to use

	// Admin features the cardroom's UpdatePlayers() function can pick
	// up and process from the main cardroom loop (rather than the player input
	// thread).  This is to work around CritSec priority problems.
	WORD32 admin_set_player_id_for_cc;	// if set, player id to make active for a cc
	WORD32 admin_set_cc_number;			// partial cc number for admin_set_player_id_for_cc
	WORD32 admin_move_player_to_top_wait_lists;	// if set, player id to move to top of waiting lists.
	WORD32 admin_set_max_accounts_for_cc;		// if set, player id whose cc's to find and set max usage for
	int    admin_set_max_accounts_for_cc_count;	// count to use for above request
	
	//cris 14-1-2004
	ErrorType ProcessMoneyTransfer(struct MoneyTransaction *mt, int input_structure_len);
	//end cris 14-1-2004

private:
	//	Verify a few of the fields in an incoming packet to make
	//	sure that it looks at least remotely valid.
	//	In particular, check the length to make sure it matches up
	//	properly with what is expected.
	ErrorType VerifyIncomingPacketFormat(Packet *p, int expected_size);

	// Handle the receipt of a struct AccountRecord
	ErrorType ProcessAccountRecord(struct AccountRecord *input_account_record, int input_structure_len);

	// Handle the receipt of a struct AdminCheckRun
	ErrorType Player::ProcessAdminCheckRun(struct AdminCheckRun *input_check_run, int input_structure_len);

	// Process a request from this client to have their all-ins reset.
	void Player::ProcessAllInResetRequest(void);

	// Process a DATATYPE_SHUTDOWN_MSG message if we're allowed to.
	void Player::ProcessClientShutdownRequest(void);

	// Handle the receipt of a struct CCTransaction
	ErrorType ProcessCreditCardTransaction(struct CCTransaction *cct, int input_structure_len);

	// Handle the receipt of a struct CCStatement(for a credit card statement)
	ErrorType ProcessStatementRequest(struct CCStatementReq *ccsr, int input_structure_len);

	// Handle the receipt of a struct PlayerLoginRequest.
	ErrorType ProcessPlayerLoginRequest(struct PlayerLoginRequest *input_login_request, int input_structure_len);


	//J Fonseca  Handle the receipt of a struct PlayerLoginRequest.
	ErrorType ProcessPlayerInfoRequest(struct PlayerLoginRequest *input_login_request, int input_structure_len);

	// Handle a request to refund pending checks
	ErrorType Player::ProcessCheckRefund(void);

	// Handle the receipt of a struct ClientErrorString
	ErrorType Player::ProcessClientErrorString(struct ClientErrorString *ces, int input_structure_len);

	// Handle the receipt of a struct ClientPlatform
	ErrorType Player::ProcessClientPlatform(struct ClientPlatform *cp, int input_structure_len);

	// Handle the receipt of a struct ClientStateInfo
	ErrorType ProcessClientStateInfo(struct ClientStateInfo *input_state_info, int input_structure_len);

	// Handle the receipt of a struct GameChatMessage
	ErrorType ProcessClientChatMessage(struct GameChatMessage *gcm, int input_structure_len);

	// Handle the receipt of a struct ConnectionClosing.
	ErrorType ProcessConnectionClosing(struct ConnectionClosing *cc, int input_structure_len);

	// Handle the receipt of a GamePlayerInputResult structure.
	ErrorType ProcessInputResult(struct GamePlayerInputResult *ir, int input_structure_len);

	// Handle client's request to unjoin/join/watch a table.
	ErrorType ProcessJoinTableRequest(struct CardRoom_JoinTable *jt, int input_structure_len);

	// Handle client's request to unjoin/join a waiting list.
	ErrorType ProcessJoinWaitListRequest(struct CardRoom_JoinWaitList *jwl, int input_structure_len);

	// Process the receipt of a struct KeepAlive
	ErrorType ProcessKeepAlive(struct KeepAlive *ka, int input_structure_len);

	// Handle the receipt of a struct MiscClientMessage
	ErrorType Player::ProcessMiscClientMessage(struct MiscClientMessage *mcm, int input_structure_len);

	// Process a received struct Ping structure
	ErrorType Player::ProcessPing(struct Ping *ping, int input_structure_len);

	// Handle a request to generate a hand history
	ErrorType Player::ProcessReqHandHistory(struct CardRoom_ReqHandHistory *crhh, int input_structure_len);

	// Handle a request to update the shot clock (admin function)
	ErrorType Player::ProcessShotclockUpdate(struct ShotClockUpdate *scu, int input_structure_len);

	// Handle a request to change the amount left creditable on a transaction
	ErrorType Player::ProcessSetTransactionCreditableAmt(struct MiscClientMessage *mcm);

	// Handle a request to change the tracking info on a check
	ErrorType Player::ProcessSetCheckTrackingInfo(struct MiscClientMessage *mcm);

	// Handle a player's submission of their phone number
	ErrorType Player::ProcessSubmitPhoneNumber(struct MiscClientMessage *mcm);
	
	// Handle a request to send/not send a tournament summary email
	ErrorType Player::ProcessTournSummaryEmailReq(struct MiscClientMessage *mcm);

	// Handle a request to transfer money (admin function)
	ErrorType Player::ProcessTransferRequest(struct TransferRequest *tr, int input_structure_len);

	// Handle a request to send a credit limits email to player
	ErrorType Player::ProcessSendCreditLimitEmail(struct MiscClientMessage *mcm);

	// Handle a request to send a transfer email to players where $ was transferred to/from
	ErrorType Player::ProcessSendXferEmail(struct MiscClientMessage *mcm);
	
	// Process DATATYPE_PLAYER_LOGOFF
	ErrorType ProcessPlayerLogoff(void);

	// Process a received DATATYPE_VERSION_NUMBER packet
	void Player::ProcessVersionNumber(struct VersionNumber *cvn, int input_structure_len);

	// Internally handle actions that have been selected as default overrides
	// like "always ante", "always post", "fold in turn", etc
	int HandleSelectedAction(struct GamePlayerInputRequest *gpir);

  #if 0	// 2022 kriskoin
	// Used to broadcast a chat message, created by this player, to everyone
	void BroadcastChatMessage(struct GameChatMessage *gcm);
  #endif

	// Add chat message to chat.log
	void AddToChatLog(struct GameChatMessage *gcm, char *table_name);

	// Used for Ecash postings -- time estimate string
	char *Player::GetEcashPostEstimateString(char *time_estimate_str);
	
	// Select a bar snack for a table we're joined to
	void Player::SelectBarSnack(WORD32 table_serial_number, BAR_SNACK bs);

	// Send an email validation code letter out to this player's email address
	void Player::SendEmailValidationCode(char *email_address);
	void Player::SendEmailValidationCodeNewAccount(char *email_address ,char *player_id);

	// Sent to update individual players on their account/chip status
	// See also the variable: send_account_info
	ErrorType SendAccountInfo(void);

	// Send the admin stats out.  It is assumed the player is logged
	// in with sufficient privilege level
	void Player::SendAdminStats(void);

	// Queue up a chat structure for processing by the cardroom
	void Player::AddChatToQueue(struct GameChatMessage *gcm);

	// Send all account info and table joined info to a client
	// so the client can be brought up to date after a new connection
	// or a login.
	// See also the variable: send_client_info
	ErrorType Player::SendClientInfo(void);
	//cris july 30 20003
	//Process the admin request to add a robot in to especific table
	ErrorType Player::AddRobotTable(struct TableInfoRobot* tableInfo, int structLen);
	//end cris july 30 20003

	//ricardoGANG October 3 2003
	ErrorType Player::AddTable(struct TableInfo* tableInfo, int structLen);
	//end ricardoGANG October 3 2003


	// Figure out how much this player will be allowed to purchase in next N hours
	WORD32 Player::GetPurchasableInNHours(WORD32 player_id, int *hours_out);

	// Process a request to generate a chargeback letter for this player
	void ProcessAdminReqChargebackLetter(WORD32 admin_player_id, WORD32 player_id);

	// Process a request to generate all-in letters for this player
	void ProcessAdminReqAllInLetter(WORD32 admin_player_id, WORD32 player_id);
	
	// Process a 'playerinfo' request from an admin client
	void ProcessAdminReqPlayerInfo(WORD32 admin_player_id, WORD32 player_id, WORD32 start_date, WORD32 number_of_days, WORD32 email_mask);

	// Profanity filter -- catch profanities and maybe modify the offending string (T/F if something was caught)
	int ProfanityFilter(char *str, int allow_modification_flag);

	int disconnect_message_printed;	// set when socket disconnect has been reported.

	WORD32 time_of_last_received_packet;	// SecondCounter when we last received something from this player
	WORD32 time_of_next_expected_packet;	// SecondCounter when we expect the next packet.
	WORD32 last_timeout_seconds;			// SecondCounter when this player object last got timed out.
	char   last_timeout_ip_str[MAX_COMMON_STRING_LEN];	// player's IP when they timed out
	struct KeepAlive last_keep_alive;		// last KeepAlive packet received from this player
	CONNECTION_STATE previous_connection_state; // connection state from last time CurrentConnectionState() figured it out.
	WORD32 last_admin_stats_sent;			// time_t when admin stats last sent (if an admin priv user is connected to us)
	int    old_AllInsAllowed;				// our old copy of the global
	WORD32 old_AllInsResetTime;				// our old copy of the global
	int    old_GoodAllInsAllowed;			// our old copy of the global
	int    got_login_packet;				// set if we have received a login packet of any sort.
	WORD32 next_login_packet_check;			// SecondCounter when we should next check the got_login_packet flag
	int    player_io_disabled;				// set if i/o with player is disabled.
	int    sent_login_alert_flag;			// set if we've sent a login alert already
	int    chat_disabled;					// set if chat disabled due to related account chat squelch
};

#endif // !_PLAYER_H_INCLUDED
