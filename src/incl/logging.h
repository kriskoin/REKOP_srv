/*******************************************************************************
 CLASS PokerLog
 date: kriskoin 2019/01/01
 This object handles all aspects in logging/hand retrieval functionality
 specifically for Tropicana Poker.
 *******************************************************************************/

#ifndef _LOGGING_H_INCLUDED
#define _LOGGING_H_INCLUDED

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <stdio.h>
#include <time.h>
#include "pplib.h"
#include "gamedata.h"
#include "poker.h"
#include "OPA.h"

#define MAX_LOG_FILENAME_LEN 30
#define MAX_LOG_STRING_LEN	 1024	// was 200 bytes 19:::#if 1	//kriskoin:   #define MAX_LOG_LINE_FIELDS	 60
#else
  #define MAX_LOG_LINE_FIELDS	 20
#endif
#define MAX_HAND_CARDS_LEN		100
#define MAX_LOG_CHAT_MSG_LEN 150
#define MAX_LOG_DESCRIPTION	 50
#define HAND_HISTORY_BUFFER	 10000	// 10k enough

#define OLD_HEADSUP_RULE_LOGGING_OFFSET		50	// see in hand-history generation & table call to LogGameStart

#define LSC	9	// Log Seperator Character (TAB)

#define FCLOSE(x) { if (x) fclose(x); x = NULL; } 

enum LogType { LOG_ACTION, LOG_AUDIT } ;

enum LogTypeMessage {   LOGTYPE_UNDEFINED,			//  0
						LOGTYPE_COMMENT,			//  1
						LOGTYPE_LOGIN,				//  2
						LOGTYPE_LOGOUT,				//  3
					    LOGTYPE_GAME_START,			//  4
						LOGTYPE_GAME_PLAYER,		//  5
						LOGTYPE_GAME_ACTION,		//  6
					    LOGTYPE_GAME_CARD_DEALT,	//  7
						LOGTYPE_GAME_END_PLAYER,	//  8
						LOGTYPE_GAME_END,			//  9
					    LOGTYPE_GAME_CHAT_MSG,		// 10
						LOGTYPE_NEXT_LOGFILE,		// 11
						LOGTYPE_GAME_ALL_7CS_CARDS,	// 12
						LOGTYPE_GAME_TOURNEY_INFO,	// 13
						LOGTYPE_RESERVED3,			// 14
						LOGTYPE_RESERVED4,			// 15
						LOGTYPE_RESERVED5,			// 16
						LOGTYPE_RESERVED6,			// 17
						LOGTYPE_RESERVED7,			// 18
						LOGTYPE_RESERVED8,			// 19
						LOGTYPE_TRANS_ECASH_TO_PP,	 // 20
						LOGTYPE_TRANS_CASH_TO_PP,	 // 21
						LOGTYPE_TRANS_WIRE_TO_PP,	 // 22
						LOGTYPE_TRANS_MISC_TO_PP,	 // 23
						LOGTYPE_TRANS_CREATE_TO_PP,	 // 24
						LOGTYPE_TRANS_PP_TO_ECASH,	 // 25
						LOGTYPE_TRANS_PP_TO_CASH,	 // 26
						LOGTYPE_TRANS_PP_GAME_RESULT,// 27
						LOGTYPE_TRANS_PP_TO_WIRE,	 // 28
						LOGTYPE_TRANS_MANUAL_CREDIT, // 29
						LOGTYPE_TRANS_TOURNAMENT_PAYOUT,// 30
						LOGTYPE_TRANS_PLR_TO_PENDING,// 31: transferring from player's account (any field) to Pending checks account (in bank)
						LOGTYPE_TRANS_CREDIT,		// 32
						LOGTYPE_TRANS_CHECK,		// 33
						LOGTYPE_TRANSFER,			// 34
						LOGTYPE_CHIPS_TO_TABLE,		// 35
						// some reserved transfer types (36 to 39)
						LOGTYPE_TRANSFER_3,			// 36
						LOGTYPE_TRANSFER_4,			// 37
						LOGTYPE_TRANSFER_5,			// 38
						LOGTYPE_TRANSFER_6,			// 39
						LOGTYPE_CC_FEE_REFUND,		// 40
					} ;

/* LOGTYPE_LOGIN */
typedef struct Log_Login {
	char date[9];
	char time[9];
	char user_id[MAX_PLAYER_USERID_LEN];
	WORD32 player_id;
	IPADDRESS ip_address;
	WORD32 real_in_bank;
	WORD32 real_in_play;
	WORD32 fake_in_bank;
	WORD32 fake_in_play;
} Log_Login;

/* LOGTYPE_LOGOUT */
typedef struct Log_Logout {
	char date[9];
	char time[9];
	char user_id[MAX_PLAYER_USERID_LEN];
	WORD32 player_id;
	IPADDRESS ip_address;
	WORD32 real_in_bank;
	WORD32 real_in_play;
	WORD32 fake_in_bank;
	WORD32 fake_in_play;
} Log_Logout;

/* LOGTYPE_GAME_START */
typedef struct Log_GameStart {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	WORD32 table_serial_number;
	char table_name[MAX_TABLE_NAME_LEN];
	int game_rules;
	ChipType chip_type;
	INT32 big_blind_amount;
	INT32 small_blind_amount;
	int button;
	int tournament_buyin;
} Log_GameStart;

 #if 0	//kriskoin: /* LOGTYPE_GAME_PLAYER */
typedef struct Log_GamePlayer {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int seating_position;
	WORD32 player_id;
	char name[MAX_COMMON_STRING_LEN];
	INT32 chips;
	int sitting_out;
} Log_GamePlayer;
 #endif

// A single entry for LOGTYPE_GAME_PLAYER
typedef struct Log_GamePlayerEntry {
    WORD32 player_id;
    char user_id[MAX_PLAYER_USERID_LEN];
    int seating_position;
    int chips;
    int sitting_out_flag;
} Log_GamePlayerEntry;

/* LOGTYPE_GAME_ACTION */
typedef struct Log_GameAction {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int action_serial_number;
	int seating_position;
	int action;
	int action_amount;
	int game_rake;
} Log_GameAction;

/* LOGTYPE_GAME_CARD_DEALT */
typedef struct Log_GameCardDealt {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int index;	// player index, -1 for common cards
	Card card;
} Log_GameCardDealt;

/* LOGTYPE_GAME_CHAT_MSG */
typedef struct Log_GameChatMsg {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	char name[MAX_COMMON_STRING_LEN];
	char chat_msg[MAX_LOG_CHAT_MSG_LEN];
} Log_GameChatMsg;

/* LOGTYPE_GAME_END_PLAYER */
typedef struct Log_GameEndPlayer {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int seating_position;
	int showed_hand;
	WORD32 chips_net_change;
	char hand_desc[MAX_HAND_DESC_LEN*2];
} Log_GameEndPlayer;

/* LOGTYPE_GAME_ALL_7CS_CARDS */
typedef struct Log_GameAll7csCards {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int seating_position;
	int card1;
	int card2;
	int card3;
	int card4;
	int card5;
	int card6;
	int card7;
} Log_GameAll7csCards;

/* LOGTYPE_GAME_END */
typedef struct Log_GameEnd {
	char date[9];
	char time[9];
	WORD32 game_serial_number;
	int rake;
	INT32 pot[MAX_PLAYERS_PER_GAME];
} Log_GameEnd;


/* LOGTYPE_TRANS_* */
typedef struct Log_Transfer {
	char date[9];
	char time[9];
	LogTypeMessage transaction_type;
	WORD32 from_id;
	WORD32 to_id;
	INT32 amount;
	int from_account_field;
	int to_account_field;
	ChipType chip_type;
	char reason[MAX_TRANSFER_REASON_LEN];
} Log_Transfer;

/* LOGTYPE_TRANSFER */
typedef struct Log_Transaction {
	char date[9];
	char time[9];
	LogTypeMessage transaction_type;
	WORD32 player_id;
	WORD32 game_serial_number;
	WORD32 chips;
	ChipType chip_type;
	char description[MAX_LOG_DESCRIPTION];
	char comment[MAX_LOG_DESCRIPTION];
} Log_Transaction;

/* struct used for queuing hand history requests */
struct HandHistoryQueueStruct {
	HandHistoryRequestType hhrt;
	WORD32 hand_num;
	WORD32 src_player_id;	// player who's hands we want to grab
	WORD32 dest_player_id;	// player we're going to email them to
	int admin_flag;
	WORD32 queue_time;		// SecondCounter when it was queued
	struct HandHistoryQueueStruct *next;
} ;

/* Class definition */
class PokerLog {
public:
	PokerLog();
  #if 0	// 24/01/01 kriskoin:
	PokerLog(char *action_log_name, char *audit_log_name);
  #else
	PokerLog(char *action_log_name,OPA * OPApointer);
  #endif
	~PokerLog();
  #if 0	// 24/01/01 kriskoin:
	void UseLog(char *action_log_name, char *audit_log_name);
  #else
	void UseLog(char *action_log_name);
  #endif
	void ShutDown();
	int QueueHandHistoryReq(HandHistoryRequestType hhrt, WORD32 hand_num,
			WORD32 dest_player_id, WORD32 src_player_id, int admin_flag);
	// setting/getting next game serial number
	void SetNextGameSerialNumber(WORD32 serial_num);
	WORD32 GetNextGameSerialNumber(void);
	WORD32 GetLastLoggedGameSerialNumber(void);
	// logging functions...
	void LogComment(char *comment);
	
	void LogLogin(struct Log_Login *);
	void LogLogin(char *name, WORD32 player_id, IPADDRESS ip, 
		WORD32 rib, WORD32 rip, WORD32 fib, WORD32 fip);

	void LogLogout(struct Log_Logout *);
	void LogLogout(char *name, WORD32 player_id, IPADDRESS ip, 
		WORD32 rib, WORD32 rip, WORD32 fib, WORD32 fip);

	void LogGameStart(struct Log_GameStart *);
	void LogGameStart(WORD32 game_serial_number, WORD32 table_serial_number, char *table_name,
		int game_rules, ChipType chip_type, INT32 big_blind_amount, INT32 small_blind_amount,
		int button, int tournament_buyin);

	void PokerLog::LogGamePlayers(WORD32 game_serial_number, int player_count, Log_GamePlayerEntry *lgpe);

  #if 0	// 2022 kriskoin
	void LogGamePlayer(struct Log_GamePlayer *);
	void LogGamePlayer(WORD32 game_serial_number, int seating_position,
		WORD32 player_id, char *name, INT32 chips, int sitting_out);
  #endif

	void LogGameAction(struct Log_GameAction *);
	void LogGameAction(WORD32 game_serial_number, int action_serial_number,
		int seating_position, int action, int action_amount, int game_rake);

	void LogGameCardsDealt(WORD32 game_serial_number, int plr_card_pair_count, int *plr_card_pairs);
	void LogGameCardDealt(WORD32 game_serial_number, int index, Card card);
	//void LogGameCardDealt(struct Log_GameCardDealt *lgcd);

	void LogGameChatMsg(struct Log_GameChatMsg *lgcm);
	void LogGameChatMsg(WORD32 game_serial_number, char *name, char *chat_msg);
	
	void LogGameEndPlayer(struct Log_GameEndPlayer *lgep);
	void LogGameEndPlayer(WORD32 game_serial_number, int seating_position,
		int showed_hand, WORD32 chips_net_change, char *hand_desc);
	
	void LogGameAll7csCards(struct Log_GameAll7csCards *lga7csc);
	void LogGameAll7csCards(WORD32 game_serial_number, int seating_position,
		int c1, int c2, int c3, int c4, int c5, int c6, int c7);
	
	void LogGameEnd(struct Log_GameEnd *lge);
	void LogGameEnd(WORD32 game_serial_number, int rake, WORD32 pot[]);

	// audit trail
	void LogFinancialTransaction(struct Log_Transaction *lt);
	void LogFinancialTransaction(LogTypeMessage transaction_type, WORD32 player_id,
		WORD32 game_serial_number, WORD32 chips, ChipType chip_type,
		char *description, char *comment);

	void LogFinancialTransfer(struct Log_Transfer *lt);
	void LogFinancialTransfer(LogTypeMessage transaction_type, WORD32 from_id, WORD32 to_id,
		INT32 amount, int from_account_field, int to_account_field, ChipType chip_type, char *reason);

	// thread related functions
	void HandReqHandlerThread(void);
	int LaunchHandReqThread(void);


private:
	void CommonInit();
	void RotateLogFiles(void);
	void CreateLogFileNames(void);
	void GetLogFilenameForGame(WORD32 game_number, char *filename, WORD32 *output_starting_offset);
	void FlushCurrentLogFiles(void);
	void WriteToLog(LogType log_type, char *str);
	void WriteToLog(LogType log_type, char *str, int force_time_output_flag);
	void LogNextGameSerialNumber(WORD32 next_game_serial_number);
	void WriteGameToIndex(WORD32 game_number, char *log_filename);
	void GetIndexNameForExt(char *ext, char *out);
	void GetHandDescription(GameRules game_rules, int p_index, char *high_out, char *low_out);
	int GetNextLogLineForGame(char *filename, FILE **in, int game_number, char *out);
	int VerifyLogLine(int packet_type, int field_count, char *filename);
	int GetArgs(int maxArgc, char *argv[], char *string, char seperator);
	void FixTournamentBlindsAndLevels(int sb, int bb, int *low_limit, int *high_limit, int *tourn_level, int *tourn_game);
	int BuildHandHistory(WORD32 game_number, WORD32 player_id, int admin_flag, FILE *out, int highlight_all_ins_flag);
	void FinishedQueueRequest(char *filename);

	// Output some info about a player starting a game (internal to building a hand history)
	void PokerLog::OutputPlayerInfo(char *str, int admin_flag, ChipType chip_type, int player_index, WORD32 player_id, char *user_id, int chips, int sitting_out_flag);

	PPCRITICAL_SECTION cs;
	FILE *fhActionLog;
  #if 0	// 24/01/01 kriskoin:
	FILE *fhAuditLog;
  #endif
	FILE *fhNextGameSerialNumber;
	Poker *_poker;							// our poker evaluation object
	int _clean_shutdown;	// T/F upon termination
	int _pending_index_write;	// T/F if we need to write an index entry
	WORD32 _last_index_game_number_written;	// game number when we last wrote an index.
	int _tmp_file;			// counter used for creating tmp file names
	WORD32 _last_game_logged;	// serial number of last game logged
	char _action_log_extension[MAX_COMMON_STRING_LEN];
	char _action_filename[MAX_LOG_FILENAME_LEN];
	char _action_filename_in_use[MAX_LOG_FILENAME_LEN];
//	char _audit_log_extension[MAX_COMMON_STRING_LEN];
//	char _audit_filename[MAX_LOG_FILENAME_LEN];
//	char _audit_filename_in_use[MAX_LOG_FILENAME_LEN];
	char _next_game_filename_1[MAX_LOG_FILENAME_LEN];
	char _next_game_filename_2[MAX_LOG_FILENAME_LEN];
	time_t last_flush_time;
	time_t last_action_log_time_t;	// last time a full time was output to the file
//	time_t last_audit_log_time_t;	// last time a full time was output to the file
	struct HandHistoryQueueStruct *_hh_queue;
	struct HandHistoryQueueStruct **_hh_last;
	Card  cards[MAX_PLAYERS_PER_GAME][MAX_PRIVATE_CARDS];
	Card  common_cards[MAX_PUBLIC_CARDS];
	
	int _hrht_is_running;	// T/F other thread is running
	int _quit_hrht;			// T/F tell thread to shutdown

	WORD32 last_sleep_ms;		// last time we slept (to free up cpu time) in ms.
	int lines_since_sleep_test;	// # of lines processed since we last tested whether we should sleep
	OPA * theOPA;
};

static void _cdecl LaunchHandReqThreadLauncher(void *args);

extern PokerLog *PL;			// global access point for logging object

#endif	//!_LOGGING_H_INCLUDED
