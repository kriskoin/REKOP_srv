//*********************************************************

//

//	Game related data structures and enums.

//	Shared by Client and Server.

//	This stuff actually gets sent back and forth over the

//	communications link.

//


//

//*********************************************************



#ifndef _GAMEDATA_H_INCLUDED

#define _GAMEDATA_H_INCLUDED



#ifndef _PPLIB_H_INCLUDED

  #include "pplib.h"

#endif



#define MAX_GAMES_PER_PLAYER	40	// Max # of games a single player can simultaneously watch or play in



#define SERVER_TIMEZONE			(6*3600)

#define SERVER_TIMEZONE_NAME	"CST"



#if WIN32 && DEBUG

  #ifdef HORATIO

	#define TESTING_TOURNAMENTS		0	// HK(win32), change this for local testing

  #else

	#define TESTING_TOURNAMENTS		0	// MB(win32), change this for local testing

  #endif

#else

  #define TESTING_TOURNAMENTS		0	// LINUX (be VERY careful about setting the linux version to testing mode!)

#endif



#if TESTING_TOURNAMENTS



 #if 0

  #define STARTING_TOURNAMENT_CHIPS	12000*100	// really big stack

 #else

  #define STARTING_TOURNAMENT_CHIPS	133*100		// small stack

 #endif

#else

  #define STARTING_TOURNAMENT_CHIPS	800*100		// # of tournament chips a player enters a tournament with (*** this should find a better home)

#endif





// data types for the packets we send back and forth.

enum {

 // Administration-specific data structures and packet types...

	DATATYPE_CARDROOM_SERIAL_NUMS,			// 20 struct CardRoom_TableSummarySerialNums

	DATATYPE_CARDROOM_TABLE_LIST,			// 21 struct CardRoom_TableSummaryList

	DATATYPE_CARDROOM_TABLE_INFO,			// 22 struct CardRoom_TableInfo

	DATATYPE_CARDROOM_UNUSED2,				// 31 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED3,				// 32 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED4,				// 33 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED5,				// 34 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED6,				// 35 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED7,				// 36 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED8,				// 37 reserved for future expansion

	DATATYPE_CARDROOM_UNUSED9,				// 38 reserved for future expansion



    	DATATYPE_VERSION_NUMBER,				//  0 struct VersionNumber

	DATATYPE_SERVER_VERSION_INFO,			//  1 struct VersionInfo

	

	DATATYPE_CARDROOM_REQUEST_TABLE_LIST,	// 23 struct CardRoom_RequestTableSummaryList

	DATATYPE_CARDROOM_REQUEST_TABLE_INFO,	// 24 struct CardRoom_RequestTableInfo

	DATATYPE_CARDROOM_JOIN_TABLE,			// 25 struct CardRoom_JoinTable

	DATATYPE_CARDROOM_JOIN_WAIT_LIST,		// 26 struct CardRoom_JoinWaitList

	

	DATATYPE_PLAYER_LOGIN_REQUEST,			//  2 struct PlayerLoginRequest

	DATATYPE_PLAYER_LOGOFF,					//  3 sent to indicate the client is logging off

	

	DATATYPE_CARDROOM_SEAT_AVAIL,			// 27 struct CardRoom_SeatAvail

	DATATYPE_CARDROOM_JOINED_TABLES,		// 28 struct CardRoom_JoinedTables

	DATATYPE_CARDROOM_REQ_HAND_HISTORY,		// 29 struct CardRoom_ReqHandHistory

	DATATYPE_CARDROOM_REQ_CC_STATEMENT,		// 30 struct CardRoom_CCStatementReq

	



	DATATYPE_KEEP_ALIVE,					//  4 sent to indicate the client/server is still alive.

	DATATYPE_MISC_CLIENT_MESSAGE,			//  5 struct MiscClientMessage (see MISC_MESSAGE_*)

	DATATYPE_ACCOUNT_INFO,					//  6 struct AccountInfo

	DATATYPE_SHUTDOWN_MSG,					//  7 struct ShutDownMsg

	DATATYPE_CLOSING_CONNECTION,			//  8 struct ConnectionClosing

	DATATYPE_ACCOUNT_RECORD,				//  9 struct AccountRecord (for admin editing)

	DATATYPE_CLIENT_PLATFORM,				// 10 struct ClientPlatform

	DATATYPE_CREDIT_CARD_TRANSACTION,		// 11 struct CCTransaction

	DATATYPE_KEEP_ALIVE2,					// 12 struct KeepAlive (new keepalive - replaces old one)

	DATATYPE_PING,							// 13 struct Ping

	DATATYPE_ERROR_STRING,					// 14 struct ClientErrorString

	DATATYPE_ACCOUNT_RECORD2,				// 15 struct AccountRecord (for client's account)

	DATATYPE_TRANSFER_REQUEST,				// 16 struct TransferRequest

	DATATYPE_SHOTCLOCK_UPDATE,				// 17 struct ShotClockUpdate (for admin to send to server)

	DATATYPE_ADMIN_STATS,					// 18 struct AdminStats (admin only)

	DATATYPE_ADMIN_CHECK_RUN,				// 19 struct AdminCheckRun (admin only)

    DATATYPE_ADMIN_ADD_ROBOT,				//20



	// Game-specific data structures...

	DATATYPE_GAME_UNUSED1,					// 46 reserved for future expansion

	DATATYPE_GAME_UNUSED2,					// 47 reserved for future expansion

	DATATYPE_GAME_UNUSED3,					// 48 reserved for future expansion

	DATATYPE_GAME_UNUSED4,					// 49 reserved for future expansion

	DATATYPE_GAME_UNUSED5,					// 50 reserved for future expansion

	DATATYPE_GAME_UNUSED6,					// 51 reserved for future expansion

	DATATYPE_GAME_UNUSED7,					// 52 reserved for future expansion

	DATATYPE_GAME_UNUSED8,					// 53 reserved for future expansion

	DATATYPE_GAME_UNUSED9,					// 54 reserved for future expansion



	// More administration-specific data structures and packet types...

	// !!! Admin-type packets should be grouped together here (moved from above)

	DATATYPE_ADMIN_INFO_BLOCK,				// 55 struct AdminInfoBlock (admin only)

	DATATYPE_ADMIN_UNUSED_1,				// 56 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_2,				// 57 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_3,				// 58 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_4,				// 69 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_5,				// 60 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_6,				// 61 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_7,				// 62 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_8,				// 63 reserved for future expansion

	DATATYPE_ADMIN_UNUSED_9,				// 64 reserved for future expansion



	DATATYPE_GAME_COMMON_DATA,				// 39 struct GameCommonData

	DATATYPE_GAME_PLAYER_DATA,				// 40 struct GamePlayerData

	DATATYPE_PLAYER_INPUT_REQUEST,			// 41 struct PlayerInputRequest

	DATATYPE_PLAYER_INPUT_REQUEST_CANCEL,	// 42 used to cancel any previous input requests.

	DATATYPE_PLAYER_INPUT_RESULT,			// 43 struct PlayerInputResult

	DATATYPE_CHAT_MSG,						// 44 struct GameChatMessage

	DATATYPE_CLIENT_STATE_INFO,				// 45 struct ClientStateInfo

	 

	// Reserved for future use...

	DATATYPE_RESERVED_1,

	DATATYPE_RESERVED_20=DATATYPE_RESERVED_1+19,



	DATATYPE_COUNT,	// count of total # of reserved types.

	DATATYPE_TABLE_INFO,
	//cris 14-1-2004
	DATATYPE_MONEY_TRANSFER
	//end cris 14-1-2004


};



// Header for all data packets exchanged between client and server.

struct DataPacketHeader {

	WORD16 data_packet_type;	// see DATATYPE_* for details on types

	WORD16 data_packet_length;	// length of subsequent data structure

};



enum TableTournament_State {	// only used for tournaments

	TTS_WAITING,		// tournament has not started yet

	TTS_ANNOUNCE_START,	// announcing the start

	TTS_MOVE_CHIPS_IN,	// move buy in chips to the middle of the table (do buyins)

	TTS_CONVERT_CHIPS,	// convert chips to T-chips

	TTS_MOVE_CHIPS_OUT,	// slide them to players

	TTS_DEAL_HIGH_CARD,	// dealing the high card for the button

	TTS_SHOW_HIGH_CARD,	// display and announce anything to do with the high card

	TTS_START_GAME,		// start the actual tournament

	TTS_PLAYING_GAME,	// the tournament is playing

	TTS_WRAP_UP,		// the tournament has finished -- do final things

	TTS_FINISHED,		// the tournament has ended -- nothing left to do

};



#define MAX_COMMON_STRING_LEN	16

#define MAX_HAND_DESC_LEN 		60	// used for hand descriptions



// Data structure which contains all the information about who is playing a

// game and the game itself.  This information is common to all the players

// of a game as well as anyone watching the game.  This data remains constant

// throughout each hand of the game.

struct GameCommonData {

	WORD32 game_serial_number;	// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;	// 32-bit ID for the specific table this structure applies to



	INT32  big_blind_amount;	// amount of big blind (in chips)

	INT32  small_blind_amount;	// amount of small blind (in chips)



	BYTE8  p_button;			// who is the button?

	BYTE8  flags;				// misc flags (see GCDF_*)

	BYTE8  players_saw_pocket;	// how many players started the game?

	BYTE8  players_saw_flop;	// how many players saw the flop?



	BYTE8  players_saw_river;	// how many players saw the last card dealt?

	BYTE8  client_display_tab_index; // which tab is this game displayed on (client end) (DISPLAY_TAB_*)

	BYTE8  game_rules;			// which rule set is being used? (GAME_RULES_*)

	BYTE8  table_tourn_state;	// TTS_* if a tournament table



	WORD32 player_id[MAX_PLAYERS_PER_GAME];	// 32-bit IDs of all the players

	INT32  chips[MAX_PLAYERS_PER_GAME];		// came into the game with how many?

	INT32  player_pot[MAX_PLAYERS_PER_GAME];  //J Fonseca  17/02/2004
	INT32  last_player_amount[MAX_PLAYERS_PER_GAME];  //J Fonseca  23/02/2004
	char   player_state_game[MAX_PLAYERS_PER_GAME][15];  //J Fonseca  23/02/2004
    char   player_state[MAX_PLAYERS_PER_GAME][15];  //J Fonseca  23/02/2004
    INT32  current_rake[MAX_PLAYERS_PER_GAME]; 
	
	char   name[MAX_PLAYERS_PER_GAME][MAX_COMMON_STRING_LEN];	// player's name (user_id)

	char   city[MAX_PLAYERS_PER_GAME][MAX_COMMON_STRING_LEN];	// player's city

	//  post_needed  = commonro.h line 31

	BYTE8  post_needed[MAX_PLAYERS_PER_GAME];	// status of what post, if any, is still needed

	BYTE8  gender[MAX_PLAYERS_PER_GAME];		// gender is of type MALE/FEMALE

	// --- we're back to dword aligned now ---

	BYTE8  bar_snack[MAX_PLAYERS_PER_GAME]; 	// BARSNACK_* for each player

	BYTE8  sit_out_request_serial_num;	// serial num of sit-out request from server

	BYTE8  tournament_hand_number;		// game # for this tournament (1-255), 0 for non-tournaments

										//kriskoin: 

	//20020402, marked by rgong

	//BYTE8  _unused[24];		// room for future expansion.

	//

	//20020402, add by rgong

	BYTE8	hihand_name[19];

	BYTE8	hihand[5];

};

// GameCommonData.flags:

#define GCDF_WAIT_LIST_REQUIRED		0x01	// set if joining must be done through a waiting list

#define GCDF_REAL_MONEY				0x02	// set when table is playing for real money (mutually exclusive with GCDF_TOURNAMENT)

#define GCDF_TOURNAMENT				0x04	// set when this is a tournament table (mutually exclusive with GCDF_REAL_MONEY)

#define GCDF_ONE_ON_ONE				0x08	// set when this is a one on one table

#define GCDF_USE_REAL_MONEY_CHIPS	0x10	// set if a tournament table should be displaying real $ chips (else tournament chips)

#define GCDF_TOURNAMENT_HAS_STARTED	0x20	// set if the tournament at this table has started playing

#if 0	//kriskoin: #define GCDF_PLAYER_SITTING_OUT		0x40	// set if this player is being treated as if they're sitting out for this game (individualized for each client)

											// note about GCDF_PLAYER_SITTING_OUT: this ONLY indicates their status for THIS game.

#endif



// constants for the GamePlayerData.player_status[] array.

// note: if these get reordered, the PlayerStatusStrings[] text must

// be updated in client\table.cpp.

enum {

	PLAYER_STATUS_UNDEFINED,			// 0  not defined - used in between games

	PLAYER_STATUS_EMPTY,				// 1  empty seat.

	PLAYER_STATUS_SITTING_OUT,			// 2  sitting out by choice

	PLAYER_STATUS_NOT_ENOUGH_MONEY,		// 3  sitting out due to lack of funds

	PLAYER_STATUS_DID_NOT_POST_SB,		// 4  sitting out because they did not post small blind

	PLAYER_STATUS_DID_NOT_POST_BB,		// 5  sitting out because they did not post big blind

	PLAYER_STATUS_DID_NOT_POST_INITIAL,	// 6  sitting out because they did not post initially

	PLAYER_STATUS_DID_NOT_POST_BOTH,	// 7  sitting out because they did not post both blinds owing

	PLAYER_STATUS_PLAYING,				// 8  currently playing

	PLAYER_STATUS_FOLDED,				// 9  folded during play

	PLAYER_STATUS_ALL_IN,				// 10 all-in during play

};



// Data structure which contains the player-specific data that changes

// as the game proceeds.  There are a bunch of these because each player

// needs their own (to prevent them from being sent opponent's cards).

// For responsiveness reasons, we will make an effort to keep this

// structure below 200 bytes because it gets sent to the client pretty

// regularly.

struct GamePlayerData {

	WORD32 game_serial_number;	// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;	// 32-bit ID for the specific table this structure applies to



	INT32  pot[MAX_PLAYERS_PER_GAME];	// all pots (main pot is pot[0])



	INT32  chips_bet_total[MAX_PLAYERS_PER_GAME];		// how much has he thrown in?

	INT32  chips_won[MAX_PLAYERS_PER_GAME];				// when GAME OVER, how much won by each player (0 if none)

	INT32  chips_in_front_of_player[MAX_PLAYERS_PER_GAME];	// how much is in front of him right now?



	INT32  rake_total;			// how much have we raked this hand?

	INT32  standard_bet_for_round;	// we can use this for desired stack size

	// amount related to last_action (40 bytes)

	INT32  last_action_amount[MAX_PLAYERS_PER_GAME];



	BYTE8  p_small_blind;		// who is the small blind?

	BYTE8  p_big_blind;			// who is the big blind?

	BYTE8  p_waiting_player;	// who are we waiting for?

	// s_gameover can be used to pass info about whether the game that just

	// finished was "valid enough" to move the button next game.  most things

	// testing for gameover condition don't care, but there are situations

	// where the button shouldn't move next game (cancelled with no posts, etc)

	// GAMEOVER_FALSE, GAMEOVER_MOVEBUTTON, GAMEOVER_DONTMOVEBUTTON (pplib.h)

	BYTE8  s_gameover;			// will be "TRUE" when the game is over



	// GAMEFLOW_BEFORE_GAME, GAMEFLOW_DURING_GAME, GAMEFLOW_AFTER_GAME (pplib.h)

	BYTE8  s_gameflow;

	BYTE8  seating_position;	// this player's seating position (index into player_status array)

	// PLAYER_STATUS_*  for each player (10 bytes)

	BYTE8  player_status[MAX_PLAYERS_PER_GAME];



	//--- 32-bit aligned ---

	// last ACT_* for each player (10 bytes)

	BYTE8  last_action[MAX_PLAYERS_PER_GAME];

	// cards for all players (10 x 7 = 70 bytes)

	Card   cards[MAX_PLAYERS_PER_GAME][MAX_PRIVATE_CARDS];

	//--- 32-bit aligned ---

	// common cards (flop) (5 bytes)

	Card   common_cards[MAX_PUBLIC_CARDS];

	BYTE8  flags;				// GPDF_* (possibly individualized for each player)

	WORD16 disconnected_flags;	// one bit for each player to indicate if they're disconnected.



	//--- 32-bit aligned ---

	BYTE8  game_state;			// enum CommonGameState

	BYTE8  expected_turn_this_round;	// T/F if we're likely to have another turn

	WORD16 unused_w;



	//--- 32-bit aligned ---

	// for client in-turn move check boxes, etc

	INT32  call_amount;

	INT32  raise_amount;

	BYTE8  _unused[20];	// room for future expansion.

};



// The GPDF_* bits might be customized for each player.

//not used: #define GPDF_FORCED_TO_SIT_OUT	0x01	// set if player forced into "sit out" mode by server (post/fold mode for tournaments)



// game states are common to all games, but not all games use all states

enum CommonGameState {  INITIALIZE,				// 0

						GET_BUTTON,				// 1

						START_COLLECT_ANTES,	// 2

						COLLECT_ANTES,			// 3

						COLLECT_POSTS,			// 4

						START_COLLECT_BLINDS,	// 5

						COLLECT_SB,				// 6

						COLLECT_BB,				// 7

						DEAL_POCKETS,			// 8

						START_BETTING_ROUND_1,	// 9

						BETTING_ROUND_1,		// 10

						DEAL_FLOP,				// 11

						START_BETTING_ROUND_2,	// 12

						BETTING_ROUND_2,		// 13

						DEAL_TURN,				// 14

						START_BETTING_ROUND_3,	// 15

						BETTING_ROUND_3,		// 16

						DEAL_RIVER,				// 17

						START_BETTING_ROUND_4,	// 18

						BETTING_ROUND_4,		// 19

						START_BETTING_ROUND_5,	// 20

						BETTING_ROUND_5,		// 21

						DEAL_2ND,				// 22

						DEAL_3RD,				// 23

						DEAL_4TH,				// 24

						DEAL_5TH,				// 25

						DEAL_6TH,				// 26

						END_GAME_START,			// 27

						POT_DISTRIBUTION,		// 28

						CANCEL_GAME,			// 29

						GAME_OVER } ;			// 30



// We need an enumerations of all possible actions that might take place during a game,

// these can be used to return the actual action, to notify an action, to specify what's

// a valid or not valid action, etc.

// 19990810HK : if actions are added or reordered, ActionStrings[] in pplib.cpp must be changed

enum ActionType {

	ACT_NO_ACTION,		//  0 00000001 no action

	ACT_POST,			//  1 00000002 (used for new players - usually BB amount)

	ACT_POST_SB,		//  2 00000004 player posts the small blind

	ACT_POST_BB,		//  3 00000008 player posts the big blind

	ACT_POST_BOTH,		//  4 00000010 (used for missed blinds, SB is dead).

	ACT_SIT_OUT_SB,		//  5 00000020 player didn't want to post SB

	ACT_SIT_OUT_BB,		//  6 00000040 player didn't want to post BB

	ACT_SIT_OUT_POST,	//  7 00000080 player didn't want to post initial post

	ACT_SIT_OUT_BOTH,	//  8 00000100 player didn't want to post both

	ACT_FOLD,			//  9 00000200 player folds

	ACT_CALL,			// 10 00000400 player calls

	ACT_CHECK,			// 11 00000800 player checks

	ACT_BRING_IN,		// 12 00001000 player brings in action low

	ACT_BET,			// 13 00002000 player bets

	ACT_RAISE,			// 14 00004000 player raises

	ACT_BET_ALL_IN,		// 15 00008000 player bets all in

	ACT_CALL_ALL_IN,	// 16 00010000 player calls all in

	ACT_RAISE_ALL_IN,	// 17 00020000 players raises all in

	ACT_SHOW_HAND,		// 18 00040000 at the end of a game, player shows hand

	ACT_TOSS_HAND,		// 19 00080000 after winning the hand, player doesn't show

	ACT_MUCK_HAND,		// 20 00100000 after losing the hand, player doesn't show

	ACT_POST_ANTE,		// 21 00200000 player posts an ante

	ACT_SIT_OUT_ANTE,	// 22 00400000 player doesn't post an ante

	ACT_POST_ALL_IN,	// 23 00800000 player is posting all-in

	ACT_FORCE_ALL_IN,	// 24 01000000 player is being forced all-in

	ACT_BRING_IN_ALL_IN,// 25 02000000 player brings-in 7cs all-in

	ACT_TIMEOUT,		// 26 04000000 player timed out

	ACT_SHOW_SHUFFLED,	// 27 08000000 player shuffled cards, then showed (7cs)




	ACT_ACTION_TYPE_COUNT

};



// This data structure is passed to the player to request certain inputs.

// It describes which inputs are legal and how many tokens are associated

// with each option.

struct GamePlayerInputRequest {

	WORD32 game_serial_number;			// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;			// 32-bit ID for the specific table this structure applies to

	WORD32 action_mask;					// the player can do any of the following actions...



	INT32  bet_amount;					// amount needed to bet

	INT32  call_amount;					// amount needed to call

	INT32  raise_amount;				// amount needed to raise



	WORD16 input_request_serial_number;	// serial number for this particular input request

	BYTE8  seating_position;			// we'll toss this back and forth to to make sure it's the same player

	BOOL8  ready_to_process;			// don't pick up the request till this is true



	BYTE8  time_out;					// how many seconds to respond to this?

	BYTE8  b_unused;

	WORD16 flags;						// GPIR_FLAG_*



	BYTE8  _unused[32];	// room for future expansion.

};



#define GPIR_FLAG_LAST_POST_CHANCE		0x0001	// set if this is the last chance a player has to post before getting kicked off.





// Data structure for cancelling an input request (usually after a timeout).

struct GamePlayerInputRequestCancel {

	WORD32 game_serial_number;			// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;			// 32-bit ID for the specific table this structure applies to



	WORD16 input_request_serial_number;	// serial number for this particular input request

	BYTE8  now_sitting_out;				// should we sit him out as a result?

	BYTE8 _unused_b;



	BYTE8  _unused[32];	// room for future expansion.

};



// This data structure is passed back from the player when an Input Request

// has been filled in.

struct GamePlayerInputResult {

	WORD32 game_serial_number;			// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;			// 32-bit ID for the specific table this structure applies to



	WORD16 input_request_serial_number;	// serial number to match up with the InputRequest

	BYTE8  seating_position;			// make sure it's the same as the calling request player

	BYTE8  action;						// return the ActionType here -- it's not a bitmap, just the enumerated action



	BYTE8  ready_to_process;			// this will be set when the result is ready to handle

	BYTE8 _unused_b[3];



	WORD32 random_bits;		// some entropy from the client for adding to the random number entropy pool

	BYTE8  _unused[28];		// room for future expansion.



};



// Chat message for a particular game (same format for input and output)

enum ChatTextType {

	CHATTEXT_NONE,

	CHATTEXT_DEALER_BLAB,

	CHATTEXT_DEALER_NORMAL, 

	CHATTEXT_DEALER_BLAB_NOBUFFER,

	CHATTEXT_DEALER_NORMAL_NOBUFFER,

	CHATTEXT_PLAYER,

	CHATTEXT_ADMIN,			// administrative stuff that they should see even in silent modes

	CHATTEXT_MONITOR_PLAY,	// sent only to administrators for monitoring

	CHATTEXT_MONITOR_REAL,	// sent only to administrators for monitoring


	ALERT_1,				// sent only to administrators for monitoring

	ALERT_2,				// sent only to administrators for monitoring

	ALERT_3,				// sent only to administrators for monitoring

	ALERT_4,				// sent only to administrators for monitoring (4+ get logged)

	ALERT_5,				// sent only to administrators for monitoring

	ALERT_6,				// sent only to administrators for monitoring

	ALERT_7,				// sent only to administrators for monitoring

	ALERT_8,				// sent only to administrators for monitoring


	ALERT_9,				// sent only to administrators for monitoring

	ALERT_10,				// sent only to administrators for monitoring

	CHATTEXT_DEALER_WINNER, // Dealer summary of winning players (amounts and their hands)

	CHATTEXT_MONITOR_TOURN,	// sent only to administrators for monitoring

};



#define MAX_CHAT_MSG_LEN	300

struct GameChatMessage {

	WORD32 game_serial_number;			// 32-bit ID for the specific game this applies to

	WORD32 table_serial_number;			// 32-bit ID for the specific table this structure applies to

	BYTE8  text_type;					// see CHATTEXT_*

	BYTE8  flags;						// GCMF_*

	BYTE8  _unused2;					// padding

	BYTE8  _unused3;					// padding

	char   name[MAX_COMMON_STRING_LEN];	// name of originator (only filled in by server)

	char   message[MAX_CHAT_MSG_LEN];	// ASCII message.

};

#define GCMF_SEND_TO_REAL_TABLES		0x01	// set if broadcast chat msg should go to real tables

#define GCMF_SEND_TO_PLAY_TABLES		0x02	// set if broadcast chat msg should go to play tables

#define GCMF_SEND_TO_TOURNAMENT_TABLES	0x04	// set if broadcast chat msg should go to tournament tables

#define GCMF_SEND_TO_ALL_TABLES (GCMF_SEND_TO_REAL_TABLES|GCMF_SEND_TO_PLAY_TABLES|GCMF_SEND_TO_TOURNAMENT_TABLES)



// Client state information (state info maintained on the client end on

// a game by game basis).

struct ClientStateInfo {

	WORD32 table_serial_number;			// 32-bit ID for the specific table this structure applies to

	WORD32 in_turn_action_game_serial_number;	// game the 'in_turn_action' refers to

	// post implies posting a blind or an ante

	WORD32 post_in_turn_game_serial_number;	// game they want to post in turn for (must match)

	BYTE8  fold_in_turn;				// set if they want to fold in turn

	BYTE8  post_in_turn;				// set if they want to ante/post in turn

	BYTE8  sitting_out_flag;			// set if we're currently sitting out (or in auto post/fold mode for tournaments)

	BYTE8  leave_table;					// set if the client wants to leave the table at the end of this game

	BYTE8  _unused_byte;				// used to be GHOSTS, gone as of 20000607 (1.08(b4) used it, nothing after)

	BYTE8  in_turn_action;				// set to ACT_FOLD, ACT_CALL, ACT_RAISE if client has selected them

	BYTE8  in_turn_action_game_state;	// game_state this in turn action is to be associated with (if it doesn't match, server will ignore)

	BYTE8  muck_losing_hands;			// set if player wants to automatically muck losing hands

	WORD16 in_turn_action_last_input_request_serial_number;	// last input_request_serial_number the client saw

	WORD16 unused_word16;

	INT32  in_turn_action_amount;		// amount for in turn action (or -1 if any).  If amount doesn't match, it is ignored.

	WORD32 random_bits;	// some entropy from the client for adding to the random number entropy pool

	BYTE8  _unused[16];	// room for future expansion.

};



// Version information sent from client to server

// DATATYPE_VERSION_NUMBER

struct VersionNumber {

	BYTE8 major;	// major version number for user display

	BYTE8 minor;	// minor version number for user display

	WORD16 flags;	// might include info about DEMO, Platform, etc. (VERSIONFLAG_*)

	WORD32 build;	// sequential build number for program's internal testing (made up from major.minor.build_counter)

};

#define VERSIONFLAG_SIMULATED	0x0001	// set if this client is merely a dumb simulation client used to help simulate real world I/O

#define VERSIONFLAG_RUNNINGLIVE	0x0002	// set if the RunningLive flag has been set



// Version information sent from server to client

// DATATYPE_SERVER_VERSION_INFO

struct VersionInfo {

	struct VersionNumber server_version;	// server's current version number

	struct VersionNumber min_client_version;// min version required for client to connect

	struct VersionNumber new_client_version;// latest client version available

	#define MAX_VERSION_STRING_LEN 80

	char server_version_string[MAX_VERSION_STRING_LEN];

	char new_version_string[MAX_VERSION_STRING_LEN];// ASCII string describing newest version

	WORD32 new_version_release_date;				// time (secs since 1970) when new version was released

	#define MAX_VERSION_URL_LEN	100

	char new_ver_user_url[MAX_VERSION_URL_LEN];		// URL a user can go to for latest version

	char new_ver_auto_url[MAX_VERSION_URL_LEN];		// URL an auto-update program can go to for latest version

	WORD32 source_ip;		// ip address of the caller as the server sees it

	WORD32 server_time;		// time_t on server when this packet was sent.

	WORD32 alternate_server_ip;	// set to the IP address of the real server (or zero if this is the real server).

	BYTE8  _unused[244];	// room for future expansion.

};



// Platform information sent from client to server

// DATATYPE_CLIENT_PLATFORM

#define CPU_IDENTIFIER_STR_LEN	48

#pragma pack(2)

// allen Ko

struct ClientPlatform {

	BYTE8 base_os;			// 0=Windows, no others defined yet

	BYTE8 version;			// 0=Win95, 1=Win98, 4=NT4, 5=Windows2000

	BYTE8 screen_bpp;		// bpp for current screen depth

	BYTE8 cpu_level;		// 3=386, 4=486, 5=Pentium (see SYSTEM_INFO)



	BYTE8 cpu_count;		// # of processors

	BYTE8 flags;			// CPFLAG_*

	WORD16 cpu_mhz;			// MHz for first processor (if known)



	WORD16 cpu_revision;	// high byte is model, low byte is stepping

	WORD16 screen_width;	// current screen width (pixels)

	WORD16 screen_height;	// current screen height (pixels)

	WORD16 installed_ram;	// amount of installed RAM (MB)



	char   cpu_vendor_str[MAX_COMMON_STRING_LEN];		// e.g. "GenuineIntel"

	char   cpu_identifier_str[CPU_IDENTIFIER_STR_LEN];	// e.g. "x86 Family 6 Model 1 Stepping 6"



	char   time_zone;			// time zone in 7.5m increments (seconds from GMT divided by 450) (this number was picked to optimize resolution in 8 bits)

	char   system_drive_letter;	// drive letter (e.g. 'C') of system drive

	char   our_drive_letter;	// drive letter (e.g. 'C') we're installed on

	BYTE8  _unused_b[1];



	WORD32 disk_space_on_system_drive;	// # of free K on the system drive

	WORD32 disk_space_on_our_drive;		// # of free K on the drive we're installed on

	WORD32 computer_serial_num;			// server-assigned unique serial number for each client computer (0 if none defined yet)

	WORD32 local_ip;					// their local ip address (if defined)



	//Marked by rgong 04/09/2002 for marketing purpose

	//BYTE8  _unused2[12];				// room for future expansion

	// end kriskoin 

	// kriskoin  04/09/2002 for marketing purpose

	char vendor_code[6];

	BYTE8  _unused2[6];				// room for future expansion

	// end kriskoin 



};

#define CPFLAG_LARGE_FONTS				0x01	// set if client is in large fonts mode

#define CPFLAG_SERIAL_NUMBER_MISMATCH	0x02	// set if comp s/n's were mismatched somehow



#pragma pack()



// Player Login Request structure.  Sent from client program when it first

// connects.

struct PlayerLoginRequest {

	#define MAX_PLAYER_USERID_LEN	20

	#define MAX_PLAYER_PASSWORD_LEN	20

	char user_id[MAX_PLAYER_USERID_LEN];

	char password[MAX_PLAYER_PASSWORD_LEN];

	BYTE8  priority_flag;	// set if this client thinks it should get 'priority login' status

	BYTE8  _unused[31];	// room for future expansion.

};



enum MiscMessageRequestInfo {	// used by MISC_MESSAGE_REQ_INFO, different types of info being requested

	MMRI_CHARGEBACK,		// generate an email when player charges back

	MMRI_AIL,				// generate All-In letters for player

	MMRI_PLAYERINFO,		// request info generated by plrinfo

};



// flags the the MMRI_PLAYERINFO uses to decide who gets the result email

#define MMRI_PLAYERINFO_EMAIL_REQUESTER	0x00000001

#define MMRI_PLAYERINFO_EMAIL_SUPPORT	0x00000002



// flags used in deciding who gets an email after a $ transfer

#define XFER_EMAIL_TO	0x00000001

#define XFER_EMAIL_FROM	0x00000002



// Misc. message sent from server to client to be displayed on Client's screen.

// DATATYPE_MISC_CLIENT_MESSAGE



enum MiscMessage {

	MISC_MESSAGE_UNSPECIFIED,				// generic message - no special handling

	MISC_MESSAGE_INSUFFICIENT_CHIPS, 

	MISC_MESSAGE_MISSED_BLINDS,

	MISC_MESSAGE_GHOST,

	MISC_MESSAGE_ALLOWED_SHORT_REBUY,

	MISC_MESSAGE_ALLOWED_REBUY,

	MISC_MESSAGE_CREATEACCOUNT_RESULT,		// result from server about creating a new account

	MISC_MESSAGE_SERVER_FULL,				// server is full and cannot accept more connections

	MISC_MESSAGE_CL_REQ_BROADCAST_MESSAGE,	// client->server, post broadcast message

	MISC_MESSAGE_CL_REQ_FREE_CHIPS,			// client->server, request more free chips

	MISC_MESSAGE_ECASH,						// message relating to ecash

	MISC_MESSAGE_CC_BUYIN_LIMITS,			// msg[] contains description of CC buy-in limits for 'buy chips' screen

	MISC_MESSAGE_TABLE_SITDOWN,				// msg contains the sit-down message

	MISC_MESSAGE_CREATE_ACCOUNT_WARNING,	// msg to display to user before real money account setup (if any)

	MISC_MESSAGE_START_CHECK_RUN,			// admin client->server: start a check run (stage 1)

	MISC_MESSAGE_REQ_ALLIN_RESET,			// client->server, request all ins to be reset

	MISC_MESSAGE_CASHOUT_REFUND,			// client->server, request a pending credit or check to be cancelled and returned to account (2000/02/04: not yet implemented)

	MISC_MESSAGE_SET_TRANS_CREDIT,			// client->server, set creditable left on a transaction

	MISC_MESSAGE_SET_COMPUTER_SERIAL_NUM,	// server->client: assign a unique serial number (misc_data_1)

	MISC_MESSAGE_SET_COMPUTER_SERIAL_NUM_BLOCK,	// admin client->server: set/clear a serial num block

	MISC_MESSAGE_SET_MAX_ALLOWABLE_USAGE,	// admin: set max_allowable_usage on all player_id's cards

	MISC_MESSAGE_SET_ACTIVE_ACCOUNT_FOR_CC,	// admin: set a player id to be the main player_id for a credit card

	MISC_MESSAGE_MOVE_TO_TOP_OF_WAITLISTS,	// admin: move a player to the top of any waiting lists they're on.

	MISC_MESSAGE_INSERT_BLANK_PLAYERID,		// admin: insert a blank player id in the computer serial num database so they can create another new account

	MISC_MESSAGE_CHOOSE_BAR_SNACK,			// client->server: select a bar snack


	MISC_MESSAGE_SET_CHECK_TRACKING_INFO,	// admin: modify check tracking info for a transaction

	MISC_MESSAGE_REQ_TOURN_SUMMARY_EMAIL,	// client->server, request a tournament summary email

	MISC_MESSAGE_TOURNAMENT_SHUTDOWN_NOTE,	// server->client, message box with tournament shutdown details

	MISC_MESSAGE_REQ_INFO,					// admin: request some information from the server (MMRI_*)

	MISC_MESSAGE_SEND_XFER_EMAIL,			// admin: send email for accounts where money was xfered (both to/from)

	MISC_MESSAGE_SEND_CREDIT_LIMITS_EMAIL,	// admin: send email to account outlining current credit limits

	MISC_MESSAGE_SEND_PHONE_NUMBER,			// client->server: submit a phone number

};



struct MiscClientMessage {

	#define MAX_MISC_CLIENT_MESSAGE_LEN	400

	WORD32 table_serial_number;	// table this msg applies to (or 0 if not table related)

	WORD32 display_flags;		// bits to indicate display options (none are defined yet)

	WORD32 misc_data_1;			// misc data to send

	WORD32 misc_data_2;			// misc data to send

	BYTE8  message_type;		// MISC_MESSAGE_*

	BYTE8  _unused[3];			// room for future expansion.

	WORD32 misc_data_3;			// misc data to send

	WORD32 misc_data_4;			// misc data to send

	BYTE8  _unused2[20];		// room for future expansion.

	char msg[MAX_MISC_CLIENT_MESSAGE_LEN];

};



// CardRoom summary info about a particular table.

#define MAX_TABLE_NAME_LEN	16	// note: must be multiple of 4.

struct CardRoom_TableSummaryInfo  {

	WORD32 table_serial_number;


	INT32  avg_pot_size;		// average pot size ($)

	INT32  big_blind_amount;	// size of big blind ($)

	INT32  small_blind_amount;	// size of small blind ($)



	BYTE8  max_player_count;	// max # of players who can play at this table

	BYTE8  player_count;		// # of players currently at table

	BYTE8  players_per_flop;	// % of players seeing the flop (0-100)

	BYTE8  hands_per_hour;		// the average hands per hour rate for this table



	BYTE8  waiting_list_length;	// # of other players already on waiting list

	BYTE8  user_waiting_list_pos;// waiting list position for this user (customized before sending from server)

	BYTE8  flags;				// flags indicating table status (TSIF_*)

	BYTE8  watching_count;		// # of players currently watching



	WORD16 rake_per_hour;		// for admin clients: rake ($)/hr for this table

	BYTE8  client_display_tab_index;	// which tab does it get displayed on? (see DISPLAY_TAB_*)

	BYTE8  game_rules;			// which rule set does this game follow? (see GAME_RULES_*)



	BYTE8  tournament_state;	// TOURN_STATE_* (if a tournament table)

	BYTE8  tournament_hand_number;	// game # for this tournament (1-255), 0 for non-tournaments

									//kriskoin: 	WORD16 tournament_elapsed;	// # of seconds since tournament table started playing



	INT32  tournament_prize_pool;	// overall prize pool size for this tournament.



	BYTE8  avg_response_time;	// admin: average player response time to action requests in tenths of seconds (255 = 25.5 seconds)

	BYTE8  _unused_b[3];



	BYTE8  _unused[16];			// room for future expansion.

	char table_name[MAX_TABLE_NAME_LEN];

};

// CardRoom_TableSummaryInfo.flags:

#define TSIF_WAIT_LIST_REQUIRED		0x01	// set if joining must be done through a waiting list

#define TSIF_REAL_MONEY				0x02	// set if table is using real money (cannot be set at same time as TSIF_TOURNAMENT)

#define TSIF_TOURNAMENT 			0x04	// set if it is a tournament table (cannot be set at same time as TSIF_REAL_MONEY)



enum Tournament_State {	// used for table summary info states ONLY

	TOURN_STATE_WAITING,		// Waiting to start. Some players may be sitting or getting called, but it's not too late to leave.

	TOURN_STATE_PLAYING,		// Table is currently playing.  It's too late to join or leave.

	TOURN_STATE_FINISHED,		// Tournament is now over. Data will no longer change.

};



// CardRoom: List of tables for a particular game type.  This is a variable

// length structure dependant on the number of tables in it.

// DATATYPE_CARDROOM_TABLE_LIST

#if 1	//kriskoin:   #define SHOTCLOCK_MESSAGE_LEN	48

#else

  #define SHOTCLOCK_MESSAGE_LEN	64

#endif

struct CardRoom_TableSummaryList  {

	WORD32 length;				// total length of this structure

	WORD32 table_count;			// # of table structures to follow

	BYTE8  client_display_tab_index;	// which tab does it get displayed on? (see DISPLAY_TAB_*)

	BYTE8  cc_processing_estimate;	// estimated # of minutes for ecash processing

	WORD16 max_tournament_tables;	// admin: current max # of tournament tables (of each type) allowed to be opened.

	WORD32 total_players;		// total # of player structures allocated

	WORD32 shotclock_date;		// time_t when shot clock "goes off"

	WORD32 shotclock_eta;		// seconds until shot clock "goes off"

	char   shotclock_msg1[SHOTCLOCK_MESSAGE_LEN];	// 1st line of shot clock

	BYTE8  _unused_old_shotclock_msg[16];	//kriskoin: 	WORD16 active_tables;		// stat: # of currently active tables

	WORD16 unseated_players;	// # of players connected but not seated (and not idle)

	WORD32 number_of_accounts;	// total # of accounts currently in system

	WORD16 idle_players;		// # of players connected but idle and not seated

	WORD16 active_real_tables;	// stat: # of currently active real money tables

	WORD32 shotclock_flags;		// SCUF_* (copy of ShotClockFlags)

	WORD32 money_in_play;		// stat: total amount of real money in play at all tables

	WORD32 money_logged_in;		// stat: total amount of real money in hands of all logged in players

	char   shotclock_msg2[SHOTCLOCK_MESSAGE_LEN];	// 2nd line of shot clock

	BYTE8  _unused[28];			// room for future expansion.

	struct CardRoom_TableSummaryInfo tables[1];	// This is actually variable sized and depends table_count.

};



// DATATYPE_CARDROOM_SERIAL_NUMS

struct CardRoom_TableSummarySerialNums  {

	BYTE8 serial_num[MAX_CLIENT_DISPLAY_TABS];	// one byte for each game type

	BYTE8  _unused[32];		// room for future expansion.

};



// CardRoom: summary info about a particular player

struct CardRoom_TablePlayerInfo  {

	WORD32 player_id;	// 32-bit ID for this player

	char name[MAX_COMMON_STRING_LEN];	// player's name

	char city[MAX_COMMON_STRING_LEN];	// player's city

	INT32 chips;		// # of chips after last finished hand

	BYTE8  flags;		// 0x01 = player is on the waiting list (rather than playing)

						// 0x02 = player is currently disconnected

						// 0x04 = player is being called to the table (from waiting list)

						// 0x08 = player is already seated at at least one other table

	BYTE8  _unused[31];	// room for future expansion.

};



// CardRoom: Information about a specific table.

// DATATYPE_CARDROOM_TABLE_INFO

#define PLAYERS_PER_TABLE_INFO_STRUCTURE (MAX_PLAYERS_PER_GAME+10)	// room for 10 wait list people

struct CardRoom_TableInfo  {

	WORD32 table_serial_number;	// table serial number this info applies to.

	struct CardRoom_TablePlayerInfo players[PLAYERS_PER_TABLE_INFO_STRUCTURE];

	BYTE8  _unused[32];	// room for future expansion.

};



// CardRoom client: request table summary list for a particular game type

// DATATYPE_CARDROOM_REQUEST_TABLE_LIST

struct CardRoom_RequestTableSummaryList {

	BYTE8 client_display_tab_index;

	BYTE8 _unused_b[3];

	WORD32 random_bits;		// some entropy from the client for adding to the random number entropy pool

	BYTE8  _unused[28];		// room for future expansion.

};



// CardRoom client: request info about a particular table

// DATATYPE_CARDROOM_REQUEST_TABLE_INFO

struct CardRoom_RequestTableInfo {

	WORD32 table_serial_number;	// table serial number we want info on

	BYTE8 subscribe_flag;		// set if this is to be a subscription.

	BYTE8 _unused_b[3];

	BYTE8  _unused[32];	// room for future expansion.

};



// CardRoom: request to join a table or be told you've been joined

// to a table (depending on whether you're the client or the server).

// DATATYPE_CARDROOM_JOIN_TABLE

#define JOINTABLE_FLAG_AUTOJOIN		0x01	// computer autojoin request (don't log errors)

#define JOINTABLE_FLAG_REALMONEY	0x02	// set if this is a real money table

#define JOINTABLE_FLAG_ONE_ON_ONE	0x04	// set if this is a one on one table

#define JOINTABLE_FLAG_TOURNAMENT	0x08	// set if this is a tournament table

#define JOINTABLE_FLAG_SEATED_AT_TOURNAMENT	0x10	// set if player is seated at ANY tournament tables



struct CardRoom_JoinTable {

	WORD32 table_serial_number;	// table serial number we're talking about

	WORD32 buy_in_chips;		// if sitting down, # of chips we want to buy in with



	BYTE8 status;				// 0=not joined (unjoin), 1=join, 2=watch (JTS_*)

	BYTE8 client_display_tab_index;	// CLIENT_DISPLAY_* for this table (sent back from server)

	BYTE8 seating_position;		// requested seating position (-1(255) if 'any').

	BYTE8 flags;				// JOINTABLE_FLAG_*



	BYTE8 game_rules;			// GAME_RULES* sent from server.

	BYTE8  _unused_b[3];



	BYTE8  _unused[28];		// room for future expansion.

};



enum JoinTableStatus { JTS_UNJOIN, JTS_JOIN, JTS_WATCH, JTS_REBUY };



// CardRoom: request to join or unjoin a waiting list

// DATATYPE_CARDROOM_JOIN_WAIT_LIST

struct CardRoom_JoinWaitList {

	WORD32 table_serial_number;	// table serial number we're talking about (0 if 'any')


	INT32  desired_stakes;		// desired stakes (big_blind_chips)



	BYTE8  min_players_required;// min # of players (including self) player requires before joining 'any' table.

	BYTE8  status;				// 0=not joined (unjoin), 1=join

	BYTE8  client_display_tab_index; // used when joining a table with serial number of 0.

	BYTE8  chip_type;			// set to indicate what kind of chips this table plays for



	BYTE8  game_rules;			// used when joining a table with serial number of 0

	BYTE8 _unused_b[3];




	BYTE8  _unused[32];	// room for future expansion.

};



// Server to client: list of joined tables.  The client is

// responsible for closing any tables that aren't in the list.

// DATATYPE_CARDROOM_JOINED_TABLES

struct CardRoom_JoinedTables {

	WORD32 table_serial_numbers[MAX_GAMES_PER_PLAYER];

	BYTE8  _unused[32];	// room for future expansion.

};



// CardRoom: server tells client a seat is open or a new table is opening

// DATATYPE_CARDROOM_SEAT_AVAIL

struct CardRoom_SeatAvail {

	WORD32 table_serial_number;	// table serial number we're talking about



	WORD16 timeout;				// # of seconds left before seat goes to someone else

	BYTE8  number_of_players;	// # of confirmed players we have so far for that table

	BYTE8  potential_players;	// # of other players who are likely to join us (if new table)



	BYTE8  skipped_players;		// # of players we skipped ahead of (because they were playing elsewhere)

	BYTE8  _unused[31];	// room for future expansion.


};



// DATATYPE_CARDROOM_REQ_HAND_HISTORY

enum HandHistoryRequestType { HHRT_INDIVIDUAL_HAND, HHRT_LAST_N_HANDS, HHRT_LAST_N_ALLIN_HANDS, HHRT_ADMIN_GAIR_LETTER } ;

struct CardRoom_ReqHandHistory {

	WORD32 hand_number;			// hand number being requested

	BYTE8  request_type;		// HHRT_*

	BYTE8  admin_flag;			// admin client: set for admin version of HH

	BYTE8  _unused_b[2];		// pad to 32-bit boundary

	WORD32 player_id;			// admin client: player_id for player we want histories from (0 if current player)

	BYTE8  _unused[24];	// room for future expansion.

};



// DATATYPE_ACCOUNT_INFO

struct AccountInfo {

	char   user_id[MAX_PLAYER_USERID_LEN];	// player's user_id

	WORD32 real_in_bank;		// how many real chips does player have in the bank?

	WORD32 real_in_play;		// how many real chips does player have in play?

	WORD32 fake_in_bank;		// how many fake chips does player have in the bank?

	WORD32 fake_in_play;		// how many fake chips does player have in play?




	BYTE8  login_status;		// are we validly logged into an account?

	BYTE8  login_priv;			// account privilege level (ACCPRIV_*)

	BYTE8  all_in_count;		// # of all-ins left right now.

	// note that the alignment here IS correct because bitfields are used.  The

	// next two items only take up ONE byte.

	BYTE8  all_ins_allowed:4;	// # of all-ins allowed per 24 hour period (for everyone)

	BYTE8  good_all_ins_allowed_for_auto_reset:4; // admin accounts only: # of good all-ins allowed for automatic reset to work



	WORD32 pending_credit;		// ecash credit left to allocate back to him

	WORD32 credit_fee_points;	// ecash accumulating credit points

	// kriskoin 

	INT32  good_raked_games;

	// end kriskoin 

	INT32  pending_check;		// total amount of pending cash-out checks due to player

	WORD32 all_in_reset_time;	// admin accounts only: time_t of last global all-in reset



	WORD16 cc_purchase_fee_rate;// fee rate * 10000, for example 5.25% = .0525 is sent as 525.

	WORD16 unused_w;

        INT32  pending_paypal;

	BYTE8  _unused2[4]; //BYTE8  _unused2[8];	// room for future expansion.

};



// struct AccountRecord: sent by client to create new account or update

// existing account (admin client might update someone else's account)

// Sent by server to tell the client it's current settings (before an update).

// DATATYPE_ACCOUNT_RECORD



// The SDBRecord should be arranged as much as possible to group the

// fields which change regularly near the top, the ones which are usually

// only read during regular play next, and the ones which are accessed

// rarely (perhaps only when emailing or otherwise contacting a client) last.

// *** Since this thing is 2K, it doesn't matter at all because

// it will always be in a single 4K page.

//

// Furthermore, Intel systems page on 4K boundaries, so if this thing is

// something like 1K, 2K, 4K, or some multiple like that there will be

// somewhat fewer page faults.



// 24/01/01 kriskoin:

// of about 3.3MB/s using its EIDE drive.  500MB takes about 2.5 minutes.

// 500MB at 2K per record is 250,000 records.



enum  {

	ACCPRIV_LOCKED_OUT = 0,		// account is locked out

	ACCPRIV_PLAY_MONEY = 10,	// play money only

	ACCPRIV_REAL_MONEY = 20,	// normal user account status

	ACCPRIV_PIT_BOSS = 30,		// possibly an end-user with extra privs

	ACCPRIV_CUSTOMER_SUPPORT = 35,	// customer support with limited access to client records


	ACCPRIV_ADMINISTRATOR = 40,	// non-programmer admin

	ACCPRIV_SUPER_USER = 50		// user can do anything, even with bad consequences

};



#define MAX_EMAIL_ADDRESS_LEN		40

#define MAX_PLAYER_FULLNAME_LEN		40

#define MAX_PLAYER_LASTNAME_LEN		40

#define MAX_PLAYER_ADDRESS_LEN		32	// # of chars for each line of the player's address

#define MAX_PLAYER_ADMIN_NOTES_LEN	324	// # of chars we reserve for notes about the player

#define MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED	2000	// max length when uncompressed

#define MAX_PLAYER_SECURE_PHRASE_LEN     20


#define GAMES_TO_RECORD_PER_PLAYER	100	// # of game serial #'s to record for a player

#define LOGINS_TO_RECORD_PER_PLAYER	10	// # of login times to record for a player

#define ALLINS_TO_RECORD_PER_PLAYER	10	// # of All-in times to record for each player

#define TRANS_TO_RECORD_PER_PLAYER	20	// # of transactions to keep on hand per client



// SDBRecord.flags (WORD16):

#define SDBRECORD_FLAG_EMAIL_NOT_VALIDATED 0x0001	// set if email address has not been validated

#define SDBRECORD_FLAG_LOCKED_OUT		0x0002	// set if account is locked out

#define SDBRECORD_FLAG_NO_INI_BONUS		0x0004	// set if user does not want any news emailed to them

#define SDBRECORD_FLAG_LOGIN_ALERT		0x0008	// send an admin alert when this account logs in?

#define SDBRECORD_FLAG_AUTO_BLOCK		0x0010	// automatically block computer serial number when this account logs in?

#define SDBRECORD_FLAG_SQUELCH_CHAT		0x0020	// filter out all chat entered by this user?

#define SDBRECORD_FLAG_GOOD_ALLIN_ALERT	0x0040	// issue an alert if the player goes all-in?

#define SDBRECORD_FLAG_HIGH_CC_LIMIT	0x0080	// set if player is using CCLimit2s instead of Limit1s

#define SDBRECORD_FLAG_DUPES_OK			0x0100	// set if duplicates of this account are OK (e.g. administrators)

#define SDBRECORD_FLAG_EMAIL_BOUNCES	0x0200	// set if email address bounces

#define SDBRECORD_FLAG_NO_ALLIN_RESET	0x0400	// set if all-ins should not be automatically reset for this player

#define SDBRECORD_FLAG_VIP				0x0800	// set if player is VIP status

#define SDBRECORD_FLAG_SQUELCH_RB_CHAT	0x1000	// filter out all railbird chat entered by this user?

#define SDBRECORD_FLAG_NO_CASHIER		0x2000	// set if player is not allowed to access cashier functions

//Marked by rgong 04/09/2002 use FLAG_USED_FIREPAY for FLAG_REAL_PLAYER

//#define SDBRECORD_FLAG_USED_FIREPAY		0x4000	// set if player has ever used FirePay

#define SDBRECORD_FLAG_REAL_PLAYER		0x4000	// set if player has deposit $50+

// end kriskoin 

#define SDBRECORD_FLAG_NO_CASHOUT		0x8000	// set if player is not allowed to access cash out function. Tony, Dec 21, 2001



enum ClientTransactionType { CTT_NONE,  CTT_PURCHASE, CTT_CREDIT, CTT_CHECK_ISSUED, 

			 CTT_CHECK_QUEUED, CTT_CHECK_REFUND, CTT_WIRE_IN, CTT_WIRE_OUT, 

			 CTT_DRAFT_IN, CTT_DRAFT_OUT, CTT_TRANSFER_IN, CTT_TRANSFER_OUT, 

			 CTT_TRANSFER_INTERNAL, CTT_TRANSFER_FEE, CTT_PRIZE_AWARD,

			 CTT_BAD_BEAT_PRIZE, CTT_TRANSFER_TO, CTT_TRANSFER_FROM, CTT_MISC, CTT_PAYPAL_REFUND,

			 CTT_FIREPAY_PURCHASE, CTT_FIREPAY_CREDIT,CTT_CC_PURCHASE,CTT_CC_CREDIT};





#define  CT_SPARE_SPACE	11

struct ClientTransaction {	// log entry for a client transaction

	WORD32 timestamp;			// time_t

	WORD32 ecash_id;			// 32-bit number we get back from CC processor

	WORD32 transaction_amount;	// how much the purchase/credit was for (always positive)

	WORD32 credit_left;	// if it was a purchase, how much we have left available space to credit back (a credit modifies the original purchase field)

	WORD32 partial_cc_number;	// 8 hex digits for 1st 4 and last 4 digits of credit card

	BYTE8  transaction_type;	// enum ClientTransactionType

  #if 0	// 20:::
	BYTE8  _padding[3];			// align to dword again

	char _unused[8];			// pad to 32 byte boundary

  #else

	char str[CT_SPARE_SPACE];	// used for a displayable string (like another UserID)

  #endif

};	// 32 bytes (the same 32 bytes are shared with ClientCheckTransaction)



struct ClientCheckTransaction {	// similar to above, used only for checks

	WORD32 timestamp;			// time_t

	WORD32 ecash_id;			// 16-bit check number we assigned

	WORD32 transaction_amount;	// how much the purchase/credit was for (always positive)

	char first_eight[8];		// first 8 chars of our string

	BYTE8  transaction_type;	// enum ClientTransactionType

	BYTE8  delivery_method;

	char last_ten[10];			// last 10 chars of our string

};	// 32 bytes (the same 32 bytes are shared with ClientTransaction)


/***************** J Fonseca     19/12/2003 ******************/
struct ActionRegister {	
	INT32  games;
	INT32  won_games;
	INT32  show_downs_won;
	INT32  show_downs_fail;
	INT32  flops;
	INT32  won_games_see_flop;
	INT32  actions;
	INT32  fold_action;
	INT32  check_action;
	INT32  call_action;
	INT32  bet_action;
	INT32  raise_action;
	INT32  re_raise_action;
	INT32  folds;
	INT32  fold_before_flop;
	INT32  fold_on_flop;
	INT32  fold_on_turn;
	INT32  fold_on_river;
	INT32  no_fold;
	WORD32 session_date;
}; //ActionRegister


struct ActionPlayRegister {
	ActionRegister  play_actions;
	ActionRegister  last_play_actions;
}; //ActionPlayRegister


struct TimeRegister {
	INT8  hour;    //hours
	INT8  min;     //minutes
	INT8  sec;     //seconds
}; //TimeRegister


struct TimePlayRegister {
	TimeRegister watching;
	TimeRegister play;
	TimeRegister last_play;
};//TimePlayRegister

struct TimePlayHistory {	
	TimePlayRegister  play_money;
	TimePlayRegister  real_money;
	TimePlayRegister  tournament;
	TimePlayRegister  last_play_money;
	TimePlayRegister  last_real_money;
	TimePlayRegister  last_tournament;
};//TimePlayHistory

struct HandRegister {
	INT32 win;
	INT32 lost;
	INT32 fold;
	INT32 total;
};//HandRegister

struct HandPlayRegister {
	HandRegister play;
	HandRegister real;
	HandRegister tournament;
	HandRegister last_play;
	HandRegister last_real;
	HandRegister last_tournament;
};//HandPlayRegister

/***************** J Fonseca     19/12/2003 ******************/

#define PHONE_NUM_LEN			20	// 8 bytes will yield 16 characters (4 bits per char, 0-9,(,),+,-,space

#define PHONE_NUM_EXPANDED_LEN	PHONE_NUM_LEN*2	// expands 2:1



typedef struct SDBRecord {	// database entry

	WORD32 player_id;		// unique, unrecyclable player ID

	INT32  real_in_bank;

	INT32  real_in_play;

	INT32  fake_in_bank;

	INT32  fake_in_play;

  //use the following field as good_raked_games, changed by rgong 02/28/2002

  //INT32  fee_credit_points;

  INT32  good_raked_games;        //$1+ raked games played in total

  // end kriskoin 

	WORD32 hands_seen;

	WORD32 flops_seen;

	WORD32 rivers_seen;

	WORD32 most_recent_games[GAMES_TO_RECORD_PER_PLAYER];	// game serial numbers

	WORD32 last_login_times[LOGINS_TO_RECORD_PER_PLAYER];	// times of previous logins

	WORD32 last_login_ip[LOGINS_TO_RECORD_PER_PLAYER];		// IP addresses of previous logins

	WORD32 all_in_times[ALLINS_TO_RECORD_PER_PLAYER];		// time_t for last n all-in's.

	BYTE8  gender;			// as defined by GENDER_* in pplib.h

  char   gender1[7];
	BOOL8  valid_entry;		// set T/F externally

	BYTE8  priv;			// account privilege level (ACCPRIV_*)

	BYTE8  dont_use_email1;	// set if 1st 'don't use email address' checkbox checked

	// *** Alignment is somewhat screwed up for a little while after this...

	BYTE8  dont_use_email2;	// set if 2nd 'don't use email address' checkbox checked

	char   user_id[MAX_PLAYER_USERID_LEN];		// used for logging in

	char   password[MAX_PLAYER_PASSWORD_LEN];	// used for logging in

	char   city[MAX_COMMON_STRING_LEN];			// used for display purposes

	char   full_name[MAX_PLAYER_FULLNAME_LEN];

	char   email_address[MAX_EMAIL_ADDRESS_LEN];

	BYTE8  address_change_count;	// how many times has he changed his address?

	char   phone_number[PHONE_NUM_LEN];		// non null-terminated 'compressed' string
    char   alternative_phone[PHONE_NUM_LEN];		// non null-terminated 'compressed' string
	/*********J Fonseca ************/
	char idAffiliate[MAX_COMMON_STRING_LEN];
	char last_name[MAX_PLAYER_LASTNAME_LEN];
  char birth_date[MAX_COMMON_STRING_LEN];
  char user_mi[3];
  char comments[255];
  char refered_by[32];
  INT32 about_us;
	char secure_phrase[MAX_PLAYER_SECURE_PHRASE_LEN];

	INT32 temp_real_in_bank;
	INT32 temp_fake_in_bank;

	INT32 ini_real_in_bank;
	INT32 ini_fake_in_bank;

	INT32 temp_real_in_play;
	INT32 temp_fake_in_play;

	char secure_question[MAX_PLAYER_SECURE_PHRASE_LEN];
	char secure_answer[MAX_PLAYER_SECURE_PHRASE_LEN];
    INT8 bitmap_id;
	INT8 pro_player;

	struct ActionPlayRegister action_play_register;
	struct TimePlayHistory  time_play_history;
	struct HandPlayRegister hand_play_register;
	/*********J Fonseca ************/

	// *** alignment notes: we're now WORD aligned (not dword) due to an

	// error before user_id[].

	struct ClientPlatform client_platform;	// platform info from the last time they logged inin



	// *** we're still WORD aligned at this point.

	WORD16 flags;					// SDBRECORD_FLAG_*

	// *** Finally, we're dword aligned again.

	WORD32 account_creation_time;	// time_t when account was created.

	WORD32 client_version;			// 32bit sequential build number for client version info from the last time they logged in

	WORD32 next_transaction_number;	// ordinal that always increases for each logged transaction

	INT32  pending_fee_refund;		// how much we're slowly giving back in cc processing fees

	ClientTransaction transaction[TRANS_TO_RECORD_PER_PLAYER];	// 32 bytes * 20 = 640 bytes

	char   mailing_address1[MAX_PLAYER_ADDRESS_LEN];	// 1st line of mailing address

	char   mailing_address2[MAX_PLAYER_ADDRESS_LEN];	// 2nd line of mailing address

	char   mailing_address_state[MAX_COMMON_STRING_LEN];

	char   mailing_address_country[MAX_COMMON_STRING_LEN];

	char   mailing_address_postal_code[MAX_COMMON_STRING_LEN];

	BYTE8  all_in_connection_state[ALLINS_TO_RECORD_PER_PLAYER];	// worst connection state seen when called all-in

        //marked by rgong

        //WORD16 _unused_w;                     // pad to 32-bit boundary

        //end mark rgong

        // kriskoin 

        INT16 fee_credit_points; //A points equals to $1+ raked games being played after receive promo bonus

        // end kriskoin 

	WORD32 all_in_game_numbers[ALLINS_TO_RECORD_PER_PLAYER];		// game numbers for recent all-ins

	INT32  pending_check;		// total amount of pending cash-out checks due to player

	char   admin_notes[MAX_PLAYER_ADMIN_NOTES_LEN];	// administrative notes about this player

	WORD32 last_login_computer_serial_nums[LOGINS_TO_RECORD_PER_PLAYER];

	WORD32 all_in_reset_time;	// time_t when all ins were last reset for this player (if ever)



	WORD16 cc_override_limit1;	// in dollars, not pennies!! override cc limit for limit1

	WORD16 cc_override_limit2;	// in dollars, not pennies!! override cc limit for limit2



	WORD16 cc_override_limit3;	// in dollars, not pennies!! override cc limit for limit3

	WORD16 tournament_fee_paid;	// fee paid to enter the current tournament



	WORD32 tournament_buyin_paid;			// the real_money buyin required

	INT32  tournament_chips_in_play;		// how many chips at the current tournament

	INT32  tournament_total_chips_in_play;	// how many chips were in the current tournament?

	WORD32 tournament_table_serial_number;	// table serial # the tournament chips related to.

	INT32  tournament_creditable_pot;		// how much is in the tournament creditable pot right now?

	WORD32 tournament_partial_payout;		// what his partial refund would be if tournament stopped

	

        INT32  pending_paypal;

	// char  _unused[2048-2044];	// pad to 2K boundary

} SDBRecord;



// Usage constants to indicate the purpose of this AccountRecord structure

enum {
	//cris 7-2-2004
	ACCOUNTRECORDUSAGE_MODIFY_PLAYER_INFO_COMMSERVER ,
	
	ACCOUNTRECORDUSAGE_MODIFY_PLAYER_ACCOUNT_COMMSERVER,
	//end //cris 7-2-2004

	ACCOUNTRECORDUSAGE_MODIFY,					// admin: modify the account for specified player_id

	ACCOUNTRECORDUSAGE_CREATE,					// user:  create a new account

	ACCOUNTRECORDUSAGE_LOOKUP_USERID,			// admin: fetch account for specified user id

	ACCOUNTRECORDUSAGE_LOOKUP_PLAYERID,			// admin: fetch account for specified player id

	ACCOUNTRECORDUSAGE_UPDATE,					// user:  change some of their own settings

	ACCOUNTRECORDUSAGE_KICKOFF_PLAYERID,		// admin: kick this player_id off the system (close socket)

	ACCOUNTRECORDUSAGE_RESET_ALLINS_FOR_PLAYERID,//admin: reset all-ins for this player id

	ACCOUNTRECORDUSAGE_CHANGE_PASSWORD,			// user:  change password

	ACCOUNTRECORDUSAGE_CHANGE_EMAIL,			// user:  change email

	ACCOUNTRECORDUSAGE_SUBMIT_EMAIL_VALIDATION,	// user:  change email: submit validation code

	ACCOUNTRECORDUSAGE_SUBMIT_ADDRESS,			// user:  change address

	ACCOUNTRECORDUSAGE_SENDPASSWORD,			// admin: send player his password via email

};



struct AccountRecord {

	BYTE8 usage;		// see ACCOUNTRECORDUSAGE_*

	//Marked by rgong 04/09/2002 for marketing purpose

	//BYTE8 _unused[31];	// padding

	// end kriskoin 

	// kriskoin  04/09/2002 for marketing purpose

	char vendor_code[6];

	BYTE8 _unused[25];

	// end kriskoin 

	SDBRecord sdb;		// player database record info



	// Marketing survey info...

	BYTE8 mrkinfo_banner_checked;

	char  mrkinfo_banner_str[MAX_COMMON_STRING_LEN];

	BYTE8 mrkinfo_magazine_checked;

	char  mrkinfo_magazine_str[MAX_COMMON_STRING_LEN];

	BYTE8 mrkinfo_newsgroup_checked;

	char  mrkinfo_newsgroup_str[MAX_COMMON_STRING_LEN];

	BYTE8 mrkinfo_website_checked;

	char  mrkinfo_website_str[MAX_COMMON_STRING_LEN];

	BYTE8 mrkinfo_other_checked;

	char  mrkinfo_other_str[MAX_COMMON_STRING_LEN];

	BYTE8 mrkinfo_word_of_mouth_checked;



	// Stuff sent from server to indicate their current status:

	BYTE8  flags;			// ACRF_*

	BYTE8  _padding[1];		// take to dword boundary

	WORD32 seated_tables[MAX_GAMES_PER_PLAYER];	// serial numbers for all tables they're seated at.

	BYTE8  _unused2[1000-18];	// lots more padding (it gets compressed to almost nothing)

};

#define ACRF_LOGGED_IN					0x01	// AccountRecord.flags: user is currently logged in

#define ACRF_SEND_ALL_IN_RESET_EMAIL	0x02	// AccountRecord.flags: when resetting all-ins, set this bit to email the player automatically.



// struct TransferRequest: sent by admin client to ask for funds to be

// transferred between accounts (or even between fields in the same account).

// DATATYPE_TRANSFER_REQUEST

#define MAX_TRANSFER_REASON_LEN		80

#define TR_SPARE_SPACE	CT_SPARE_SPACE+1	// see TransactionType

struct TransferRequest {

	WORD32 from_id;

	WORD32 to_id;

	INT32  amount;				// amount to transfer (preferably positive - negative may not be tested)

	BYTE8  from_account_field;	// 0=in bank, 1=pending CC refund field, 2=pending check field

	BYTE8  to_account_field;	// 0=in bank, 1=pending CC refund field, 2=pending check field

	BYTE8  chip_type;			// what kind of chips are we transferring? (ChipType)

	BYTE8  _padding;

	char   reason[MAX_TRANSFER_REASON_LEN];

	WORD32 flags;				// TRF_*

	WORD32 transaction_number;	// sfc transaction number (if applicable)

	char str[TR_SPARE_SPACE];	// 11(+1) characters of space we can use, we'll null terminate this string

	BYTE8  _unused[492];

};



// Transfer request flags: (TransferRequest.flags)

#define TRF_TREAT_AS_CC_PURCHASE	0x00000001	// set if this should be treated as a CC purchase

#define TRF_NO_HISTORY_ENTRY		0x00000002	// set to prevent a history entry from appearing

#define TRF_PLAYER_TO_PLAYER		0x00000004	// set if we want to see the source/dest account names

#define TRF_CUSTOM_MESSAGE			0x00000008	// set to display our own misc message instead of canned strings



// struct ShutDownMsg: sent to clients when server is shutting down.

// Also for debugging, clients can send this to server to ask it to shut down.

// DATATYPE_SHUTDOWN_MSG

struct ShutDownMsg {

	WORD32 seconds_before_shutdown;	// # of seconds before server is shutting down.

	BYTE8  shutdown_client_flag;	// used only for testing: if set, client should shut down as well.

	BYTE8  _unused_b[3];

	char   shutdown_reason[256];	// reason server is shutting down (if specified)

	BYTE8  _unused[256];	// room for future expansion.

};



// struct ConnectionClosing: sent from one end to the other whenever

// the connection is about to be closed.  May contain data about why

// the connection is being closed.

// This is usually the last message that will get sent on a socket.

// DATATYPE_CLOSING_CONNECTION

struct ConnectionClosing {

	BYTE8  reason;			// 0 = not specified, 1 = client logged in on another socket, 2=blocked

	BYTE8  _unused_b[3];	// padding

	WORD32 new_ip_address;	// if reason==1, this contains the IP of the new connection.

	WORD32 error_code;		// if reason==2, this contains the error code

	BYTE8  _unused[252];	// room for future expansion.

};



// struct KeepAlive: sent from client to server periodically to indicate

// to the server that the client is still there.

// This one contains information... the previous keep alive did not.

// This one is never sent from the server to the client.

// DATATYPE_KEEP_ALIVE2

struct KeepAlive {

	BYTE8  status_bits;		// various status bits (0x01 = active app flag, others not yet defined)

	BYTE8  _unused_b[3];	// room for future expansion.

	WORD32 random_bits;		// some entropy from the client for adding to the random number entropy pool

	BYTE8  _unused[24];		// room for future expansion.

};



// struct Ping: sent from server to client and back (client just echoes)

// DATATYPE_PING

struct Ping {

	WORD32 local_ms_timer;	// ms timer when sent by original sender

	BYTE8  flags;			// 0x01 = client to server and back ping (not set = server to client and back)

	BYTE8  _unused_b[3];	// room for future expansion.



	WORD32 random_bits;		// some entropy from the client for adding to the random number entropy pool

	BYTE8  _unused[24];		// room for future expansion.

};



// struct ShotClockUpdate: sent from admin client to server to change the shot clock

// DATATYPE_SHOTCLOCK_UPDATE

struct ShotClockUpdate {

	WORD32 shotclock_time;

	char   shotclock_message1[SHOTCLOCK_MESSAGE_LEN];	// 1st line of shot clock (pre-expiry)

	BYTE8  _unused_old_shotclock_msg[16];	//kriskoin: 	WORD32 flags;			// SCUF_*

	WORD32 misc_value1;		// for all-in setting, this is the number of all-ins

	WORD32 misc_value2;		// for all-in setting, this is the max good all-ins allowed for automatic reset

	WORD16 max_tournament_tables;	// used to set the max # of tournament tables (of each type) allowed to be opened.

	WORD16 _padding;

	char   shotclock_message2[SHOTCLOCK_MESSAGE_LEN];	// 2nd line of shot clock (pre-expiry)

	char   shotclock_message1_expired[SHOTCLOCK_MESSAGE_LEN];	// 1st line of shot clock (post-expiry)

	char   shotclock_message2_expired[SHOTCLOCK_MESSAGE_LEN];	// 2nd line of shot clock (post-expiry)

	BYTE8  _unused[96];

};



#define SCUF_ANNOUNCE_AT_TABLES		0x00000001	// have dealer announce shot clock msg at table?

#define SCUF_SHUTDOWN_WHEN_DONE		0x00000002	// shutdown server when done counting down?

#define SCUF_SHUTDOWN_IS_BRIEF		0x00000004	// set when shutdown is expected to be brief

#define SCUF_GLOBAL_ALLIN_SETTING	0x00000008	// this structure is actually to set the global all-ins

#define SCUF_SHUTDOWN_AUTO_INSTALLNEW 0x00000010// run 'auto_install_new_pokersrv' at end of shutdown

#define SCUF_CLOSE_CASHIER			0x00000020	// close cashier immediately

#define SCUF_ECASH_AUTO_SHUTDOWN	0x00000040	// allow ecash auto-shutdown and startup

#define SCUF_CLOSE_TOURNAMENTS		0x00000080	// set to close new tournaments now (existing ones finish)

#define SCUF_NO_TOURNAMENT_SITDOWN	0x00000100	// ** INCORRECT COMMENT: set to disallow sitting at tournament tables ** UNSUPPORTED **: the server end of this code is not yet written !

#define SCUF_DISPLAY_SECONDS		0x00000200	// set to display seconds during last few minutes of countdown

#define SCUF_USE_SHORT_UNITS		0x00000400	// set to use short units (d/h/m/s) instead of days/hours/minutes/seconds

#define SCUF_TOURNAMENTS_OPEN		0x00000800	// set if tournaments should open when clock expires, otherwise tournaments close when clock is expired.  No effect while shot clock is NOT expired.

#define SCUF_EMERGENCY_TOURNAMENT_SHUTDOWN 0x00001000	// set to shut down tournaments IMMEDIATELY after current game (cancel all tournaments in progress)

#define SCUF_24HOUR_CLOCK			0x00002000	// set to display a 24 hour clock for %c (instead of am/pm)

#define SCUF_DISPLAY_YEAR			0x00004000	// set to display the year with %d



// struct ClientErrorString: error string sent from the client to the server

// DATATYPE_ERROR_STRING

#define CLIENT_ERROR_STRING_LEN	400

struct ClientErrorString {

	BYTE8 unused[32];

	char error_string[CLIENT_ERROR_STRING_LEN];

	BYTE8 unused2[250];

};



// struct AdminStats: sent from server to an admin for stats on various things

// about the current operation of the server.

// DATATYPE_ADMIN_STATS



struct PacketStats {

	WORD32 sent_count;		// # of these packets sent

	WORD32 rcvd_count;		// # of these packets received

	WORD32 bytes_sent;		// # of bytes sent for this packet type (+TCP_HEADER_OVERHEAD_AMOUNT)

	WORD32 bytes_rcvd;		// # of bytes received for this packet type (+TCP_HEADER_OVERHEAD_AMOUNT)

	WORD32 bytes_sent_uncompressed;	// # of bytes sent before compression (+TCP_HEADER_OVERHEAD_AMOUNT)

	WORD32 bytes_rcvd_uncompressed;	// # of bytes rcvd before compression (+TCP_HEADER_OVERHEAD_AMOUNT)

	BYTE8 unused[32];

};



#define STATS_PKTPOOL_COUNT	6	// should be same as PKTPOOL_COUNT in llip.h



#pragma pack(4)

struct AdminStats {


	WORD32 time;				// time_t when these stats were saved.

	WORD32 rake_today;			// rake so far today

	WORD32 ecash_today;			// ecash so far today

	WORD32 rake_per_hour;		// estimated rake per hour

	WORD32 est_rake_for_today;	// estimated rake account change at end of day for today only

	WORD32 est_ecash_for_today;	// estimated ecash account change at end of day for today only

	WORD32 next_game_number;	// next game number to be played.

	WORD32 games_per_hour;		// estimated # of games played per hour


	WORD32 load_avg[3];			// load averages (*100) for last 1, 5, and 15 minutes



	WORD32 bytes_sent;			// total # of bytes sent (includes estimates for TCP and IP headers)

	WORD32 bytes_rcvd;			// total # of bytes rcvd (includes estimates for TCP and IP headers)

	WORD32 packets_sent;		// total # of packets sent

	WORD32 packets_rcvd;		// total # of packets received



	WORD32 bytes_sent_per_second;	// estimated bandwidth over last 60 seconds

	WORD32 bytes_rcvd_per_second;	// estimated bandwidth over last 60 seconds



	struct PacketStats packet_stats[DATATYPE_COUNT];



	WORD32 mem_allocated;		// total memory allocated from the operating system

	WORD32 mem_used;			// total memory currently used (of what is allocated)

	WORD32 mem_not_used;		// total memory currently used (of what is allocated)

	WORD32 active_player_connections;	// # of players connected (same as cardroom display)

	WORD32 server_uptime;		// # of seconds the server has been running for



	WORD32 living_packets;		// # of packets still allocated.

	WORD32 packets_allocated;	// total # of packets we've ever allocated in PktPool_Alloc

	WORD32 packets_constructed;	// total # of packets ever constructed (Packet constructor keeps track)



	struct {

		WORD32 packet_count;

		WORD32 alloc_count;

		WORD32 max_pool_size;

	} pool_stats[STATS_PKTPOOL_COUNT];



	WORD32 play_tables;			// current play table count

	WORD32 real_tables;			// current real table count

	WORD32 play_tables_high;	// today's high for play table count

	WORD32 real_tables_high;	// today's high for real table count

	WORD32 player_count_high;	// today's high for the player counter



	double os_idle_time;		// # of seconds operating system has been idle since booted

	WORD32 os_up_time;			// # of seconds operating system has been up.

	float  recent_idle_percentage;	// % idle in last 60s or so

	WORD32 player_count_low;	// today's low for the player counter

	WORD32 play_tables_low;		// today's low for play table count

	WORD32 real_tables_low;		// today's low for real table count



	WORD32 CCLimit1Amount;		// credit card limits...

	WORD32 CCLimit1Amount2;

	WORD32 CCLimit2Amount;

	WORD32 CCLimit2Amount2;

	WORD32 CCLimit3Amount;

	WORD32 CCLimit3Amount2;



	WORD32 ecash_queue_len;		// # of items currently in ecash queue

	WORD32 money_in_play;		// stat: total amount of real money in play at all tables

	WORD32 money_logged_in;		// stat: total amount of real money in hands of all logged in players

	WORD32 rake_balance;		// stat: total present rake balance

	INT32  output_queue_lens[8];// current depth of the player data output queue

	INT32  ecash_post_time;		// avg. time per ecash POST (in seconds)



	WORD32 bad_beats_today;		// how many bad beats we've seen today

	WORD32 bad_beats_payout;	// how much we've paid out today in bad beats

	WORD32 client_count_newer;	// # of clients connected with newer than current version

	WORD32 client_count_current;// # of clients connected with the current version

	WORD32 client_count_old;	// # of clients connected with old version



	WORD16 table_names_used;	// # of real money table names currently used

	WORD16 table_names_avail;	// # of real money table names available to be used (total)

	WORD32 number_of_accounts;			// total # of accounts online (not archived)

	WORD32 number_of_accounts_purged;	// # of accounts purged from online database at startup

	WORD32 number_of_accounts_today;	// # of accounts created today



	INT32  ecashfee_today;		// change to ecash fee account so far today

	WORD32 games_today;			// # of games played so far today

	WORD32 database_size;		// # of records SDB was initialized with.

	INT32  tournaments_today;	// # of tournaments started today

	INT32  ecash_threads;		// # of ecash threads currently active



	char   shotclock_message1[SHOTCLOCK_MESSAGE_LEN];	// 1st line of shot clock (pre-expiry)

	char   shotclock_message2[SHOTCLOCK_MESSAGE_LEN];	// 2nd line of shot clock (pre-expiry)

	char   shotclock_message1_expired[SHOTCLOCK_MESSAGE_LEN];	// 1st line of shot clock (post-expiry)

	char   shotclock_message2_expired[SHOTCLOCK_MESSAGE_LEN];	// 2nd line of shot clock (post-expiry)

	WORD32 shotclock_flags;		// a duplicate copy of the cardroom shot clock flags



	WORD32 gross_bets_today;	// gross real money bets so far today

	WORD32 gross_tournament_buyins_today;	// tournament buy-ins (+fee) so far today



	float  avg_response_time_real_money;	// avg response time for entire cardroom (real money tables)

	float  avg_response_time_play_money;	// avg response time for entire cardroom (play money tables)

	float  accept_avg_ms;		// average ms needed to process a single Accept().

	float  mainloop_avg_ms;		// average ms needed to process entire main loop

	float  table_avg_ms;		// average ms needed to process a single table



	WORD32 unseated_players;	// # of players connected but not seated (and not idle) (same as cardroom display)

	WORD32 idle_players;		// # of players connected but idle and not seated (same as cardroom display)

	WORD32 multi_seated_players;// # of players seated at more than one table



	float  inputloop_avg_ms;	// average ms needed to process a single player input thread loop

	float  hand_history_queue_time;	// how long are HHs sitting in the queue?



	BYTE8 _unused[496];			// room for future expansion.

};

#pragma pack()



// DATATYPE_ADMIN_CHECK_RUN

#define MAX_PLAYERS_PER_CHECK_RUN_PAGE	90	// 3 columns of 30 each

#define MAX_PLAYERS_PER_CHECK_RUN		360	// 4 pages (24/01/01 kriskoin:

#define ADMIN_CHECK_RUN_ENTRY_DESC_LEN	60

struct AdminCheckRunEntry {

	WORD32 player_id;

	INT32  amount;

	char   description[ADMIN_CHECK_RUN_ENTRY_DESC_LEN];

};



struct AdminCheckRun {

	INT32 player_count;

	BYTE8 _unused[200];

	struct AdminCheckRunEntry entries[MAX_PLAYERS_PER_CHECK_RUN];

};



// DATATYPE_ADMIN_INFO_BLOCK (a block of information being sent from the server to the client)

#define MAX_INFO_BLOCK_SIZE	22000	// be sure this fits into MAX_PACKET_SIZE (llip.h)

struct AdminInfoBlock {

	WORD32 reserved1;

	WORD32 reserved2;

	WORD32 reserved3;

	char info[MAX_INFO_BLOCK_SIZE];

};



// DATATYPE_CREDIT_CARD_TRANSACTION

enum CCType { CCTYPE_UNKNOWN, CCTYPE_VISA, CCTYPE_MASTERCARD, CCTYPE_AMEX, CCTYPE_FIREPAY } ;

enum CCTransactionType { CCTRANSACTION_UNKNOWN, CCTRANSACTION_PURCHASE, 

						 CCTRANSACTION_CASHOUT, CCTRANSACTION_ADMIN_CREDIT, 

						 CCTRANSACTION_STATEMENT_REQUEST, CCTRANSACTION_FIREPAY_PURCHASE,

						 CCTRANSACTION_FIREPAY_CASHOUT };

#define CCFIELD_LONG	40	// these limits don't seem to be defined anywhere

#define CCFIELD_SHORT	10	// these limits don't seem to be defined anywhere



struct CCStatementReq {

	CCTransactionType transaction_type;	// always CCTRANSACTION_STATEMENT_REQUEST

	WORD32 player_id;

	WORD32 admin_player_id;

	WORD32 queue_time;	// time_t when this transaction was added to ecash queue (filled in on server end)

	BYTE8 admin_flag;

	char unused1[3];

	char card_number[CCFIELD_LONG];

	char unused[8];

};	



struct CCTransaction {

	CCType card_type;					// VISA, MC, AMEX

	CCTransactionType transaction_type;	// buying in, cashing in

	char amount[CCFIELD_SHORT];			// $ amount requested

	char user_id[CCFIELD_LONG];			// $ amount requested

	char card_name[CCFIELD_LONG];

  #if 0	// unused for now

	char card_address_1[CCFIELD_LONG];


	char card_address_2[CCFIELD_LONG];

	char card_city[CCFIELD_LONG];

	char card_state[CCFIELD_LONG];

	char card_zip[CCFIELD_LONG];

  #else

	WORD16 flags;			// CCTF_*

	//--- 32-bit aligned ---

	// admin might want to override to a specific sfc transaction #

	INT32  admin_transaction_number_override;

	WORD32 player_id;

	WORD32 queue_time;		// time_t when this transaction was added to ecash queue (filled in on server end)

	WORD32 ip_address;		// global IPADDRESS of who submitted transaction (filled in by server)

	WORD32 ip_address2;		// local  IPADDRESS of who submitted transaction (filled in by server)

	WORD32 admin_player_id;	// potentially used for overrrides

	char unused[174];

  #endif

	char card_number[CCFIELD_LONG];

	char card_exp_month[CCFIELD_SHORT];

	char card_exp_year[CCFIELD_SHORT];

};	



// CCTransaction.flags bits:

#define CCTF_NO_NOTICES		0x0001		// set to prevent pop-ups and emails from going out



// different type of rake profiles (see pot.cpp and cardroom.cpp to see what they actually do)

enum RakeType { RT_NONE, RT_PRO1, RT_PRO2, RT_PRO3, RT_PRO4, RT_PRO5, RT_PRO6 } ;



// The snacks themselves are managed on the client in snack.cpp

// The server knows very little about these things.

enum BAR_SNACK {

	BAR_SNACK_NONE,



	BAR_SNACK_CAKE,

	BAR_SNACK_MUFFIN,

	BAR_SNACK_DOUGHNUTS,		

	BAR_SNACK_STRAWBERRIES,

	BAR_SNACK_HOTDOG,		

	BAR_SNACK_HAMBURGER,		

	BAR_SNACK_BEER_DRAFT,	


	BAR_SNACK_BEER_DOMESTIC,

	BAR_SNACK_BEER_IMPORTED,

	BAR_SNACK_CHICHI,		

	BAR_SNACK_MARTINI,		

	BAR_SNACK_COSMO,			

	BAR_SNACK_LIME_MARG,		

	BAR_SNACK_PINK_MARG,		

	BAR_SNACK_RUM_AND_COKE,

	BAR_SNACK_REDWINE,		

	BAR_SNACK_CHAMPAGNE,		

	BAR_SNACK_BRANDY,		

	BAR_SNACK_IRISH_COFFEE,

	BAR_SNACK_TEA,			

	BAR_SNACK_COFFEE,		

	BAR_SNACK_ICED_TEA,		

	BAR_SNACK_COKE,			

	BAR_SNACK_WATER,			

	BAR_SNACK_LEMONADE,		



	// The smokes section is for selecting non-smoking tables.

	BAR_SNACK_SMOKES_START,

	BAR_SNACK_CIGAR = BAR_SNACK_SMOKES_START,

	BAR_SNACK_CIGARETTE,		

	BAR_SNACK_SMOKES_END = BAR_SNACK_SMOKES_START+1,



	BAR_SNACK_COUNT

};



// Prediction data for admin clients (data-?.bin file format)



#define PREDICTION_DATA_INTERVAL		(5*60)

struct Pred_Data {

	float players;

	float rake_per_hour;

	float rake_today;

	float games_today;

	float gross_cc_today;

	float new_accounts_today;

	float unused[2];

};


//structure to define the upgrade rules header
struct upgrade_rules_header {
//	int num_downloads;
    int num_rules;
	float bytes;
};


//structure to define the upgrade rules
struct upgrade_rules {
    int type;
    char source[100];
	char target[100];
	float bytes;
};

//cris 11-AUG-2003
struct TableInfoRobot{
	WORD32 table_serial_number;
	BYTE8 pos;
	char ID[30];
};
//end cris 11-AUG-2003

//ricardoGANG 2-10-2003
struct TableInfo{
		ClientDisplayTabIndex CDTI ;
		int gr ;
		int MaxPlayers ;
		int SmallBlindAmount ;
		int BigBlindAmount ;
		char TableName[20] ;
		int AddRobotsFlag ;
		ChipType Type ;
		int GameDisableBits ;
		RakeType RakeProfile ;
} ;
//end ricardoGANG 2-10-2003

//cris 14-1-2004
struct MoneyTransaction{
	    int type ;
		int subtype;
		char login_name[20] ;
	    char description[100];
		char date[20];
	    ChipType chip_type ;
		int amount ;
		int isPositive ;
	    char idTransaction[20];
	    char madeby[20];
} ;
//end cris 14-1-2004


#endif // !_GAMEDATA_H_INCLUDED
