/**********************************************************************************
 Member functions for PokerLog
 date: kriskoin 2019/01/01
 /***********************************************************************************/


#ifdef HORATIO
	#define DISP 0
#endif


#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <exception>
#include <iostream>
#if WIN32
  #include <process.h>
#endif
#include "logging.h"
#include "sdb.h"
#include "cardroom.h"
#include "pokersrv.h"

#if 0	// 2022 kriskoin
extern char *ActionStrings[];
extern char cSuits[];	// "cdhs"
extern char cRanks[];	// "23456789TJQKA"
#endif


/**********************************************************************************
 Function PokerLog
 date: kriskoin 2019/01/01 Purpose: default constructor
***********************************************************************************/
PokerLog::PokerLog()
{
	CommonInit();
}

/**********************************************************************************
 Function PokerLog(char *action_log_extension, char* audit_log_extension)
 date: kriskoin 2019/01/01 Purpose: constructor with log name supplied
***********************************************************************************/
PokerLog::PokerLog(char *action_log_extension/*, char* audit_log_extension*/,OPA * OPApointer)
{
	theOPA=OPApointer; 
	CommonInit();
//	UseLog(action_log_extension, audit_log_extension);
	UseLog(action_log_extension);
}

/**********************************************************************************
 Function ::CommonInit()
 date: kriskoin 2019/01/01 Purpose: initialize variables
***********************************************************************************/
void PokerLog::CommonInit()
{
	PPInitializeCriticalSection(&cs, CRITSECPRI_LOGGING, "PokerLog");
	_action_log_extension[0] = 0;	// blank the logfile name till it's initialized
//	_audit_log_extension[0] = 0;	// blank the logfile name till it's initialized
	_pending_index_write = TRUE;	// for indexing -- first game's entry
	_last_index_game_number_written = 0;
	_tmp_file = 0;
	last_flush_time = time(NULL);
	_hh_queue = NULL;
	_hh_last = NULL;
	_hrht_is_running = FALSE;	// is other thread running?
	_quit_hrht = FALSE;
	fhActionLog = NULL;
    last_action_log_time_t = 0;	// last time a full time was output to the file
  #if 0	// 24/01/01 kriskoin:
	fhAuditLog = NULL;
	last_audit_log_time_t = 0;	// last time a full time was output to the file
  #endif
	last_sleep_ms = 0;
	lines_since_sleep_test = 0;
	// poker object
	_poker = new Poker;		// deleted in destructor
}

/**********************************************************************************
 Function ~PokerLog
 date: kriskoin 2019/01/01 Purpose: default destructor
***********************************************************************************/
PokerLog::~PokerLog()
{
	if (!_clean_shutdown) {
		ShutDown();
	}
	if (_poker) {
		delete (_poker);
	}
	PPDeleteCriticalSection(&cs);
}

/**********************************************************************************
 Function PokerLog::ShutDown(void)
 date: kriskoin 2019/01/01 Purpose: we're told we're shutting down -- do whatever cleanup is needed
***********************************************************************************/
void PokerLog::ShutDown(void)
{
	if (_hrht_is_running) {
		_quit_hrht = TRUE;	// tell other thread to shutdown
		int loop_count = 0;
		while (_hrht_is_running) {
			if (loop_count > 100) {	// give up, other thread may be hung or taking way too long
				Error(ERR_NOTE,"%s(%d) gave up waiting for hhrq thread to shut down", _FL);
				break;
			}
			Sleep(200);
			loop_count ++;
		}
	}
	LogComment("Closed log file");
	FlushCurrentLogFiles();
	FCLOSE(fhActionLog);
	fhActionLog = NULL;
  #if 0	// 20:::	FCLOSE(fhAuditLog);
	fhAuditLog = NULL;
  #endif
	_clean_shutdown = TRUE;
}

/**********************************************************************************
 Function ::UseLog(char *action_extension, *audit_extension)
 date: kriskoin 2019/01/01 Purpose: specifies which database we'll be using... do whatever startup is needed
***********************************************************************************/
void PokerLog::UseLog(char *action_ext/*, char *audit_ext*/)
{
	_clean_shutdown = FALSE;
	// define/create names
	EnterCriticalSection(&cs);
	strnncpy(_action_log_extension, action_ext, MAX_COMMON_STRING_LEN);
	_action_log_extension[3] = 0;	// whatever it was, we want only the first 3 characters
  #if 0	// 20:::	strnncpy(_audit_log_extension, audit_ext, MAX_COMMON_STRING_LEN);
	_audit_log_extension[3] = 0;	// whatever it was, we want only the first 3 characters
  #endif
	RotateLogFiles();	// figure out which logfile to write to
	// we do two of these for redundancy

//	sprintf(_next_game_filename_1,"logs/%s.nh1", action_ext);
//	sprintf(_next_game_filename_2,"logs/%s.nh2", action_ext);
	sprintf(_next_game_filename_1,"Data/History/%s.nh1", action_ext);
	sprintf(_next_game_filename_2,"Data/History/%s.nh2", action_ext);

	LeaveCriticalSection(&cs);
	LogComment("Opened log file");
}

/**********************************************************************************
 Function PokerLog::RotateLogFiles(void)
 date: kriskoin 2019/01/01 Purpose: tell us which logfile to write to and do proper handling if needed
***********************************************************************************/
void PokerLog::RotateLogFiles(void)
{
	char str[MAX_LOG_FILENAME_LEN+50];
	zstruct(str);

	CreateLogFileNames();
	if (fhActionLog && !strcmp(_action_filename_in_use, _action_filename)) {	// no change
		return;
	}
	// when a new game fires up, log it to the index as the first for this logfile
	_pending_index_write = TRUE;
	// the new name becomes the current one
	strnncpy(_action_filename_in_use, _action_filename, MAX_LOG_FILENAME_LEN);
  #if 0	// 20:::	strnncpy(_audit_filename_in_use, _audit_filename, MAX_LOG_FILENAME_LEN);
  #endif
	// if it's changed... close the old, open the new
	if (fhActionLog) {
		// write the next-filename-marker to the old log file
		sprintf(str, "%d%c%s", LOGTYPE_NEXT_LOGFILE, LSC, _action_filename_in_use);
		WriteToLog(LOG_ACTION, str, TRUE);
		FCLOSE(fhActionLog);
		fhActionLog = NULL;
	}
	// Open the new one.
	if ((fhActionLog = fopen(_action_filename_in_use, "at")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open action log file (%s) for append",
			_FL, _action_filename_in_use);
	}

  #if 0	// 20:::	// rotate audit trail as well
	if (fhAuditLog) {
		FCLOSE(fhAuditLog);
		fhAuditLog = NULL;
	}
	// Open the new one.
	if ((fhAuditLog = fopen(_audit_filename_in_use, "at")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open audit log file (%s) for append",
			_FL, _audit_filename_in_use);
	}
    last_audit_log_time_t = last_action_log_time_t = 0; // reset both
  #endif
    last_action_log_time_t = 0; // reset
}

/**********************************************************************************
 Function PokerLog::WriteToLog(LogType log_type, char *str)
 date: kriskoin 2019/01/01 Purpose: write a string to the log file
***********************************************************************************/
void PokerLog::WriteToLog(LogType log_type, char *str)
{
    WriteToLog(log_type, str, FALSE);
}

void PokerLog::WriteToLog(LogType log_type, char *str, int force_time_output)
{
	char log_string[MAX_LOG_STRING_LEN];
	zstruct(log_string);

	EnterCriticalSection(&cs);
	RotateLogFiles();	// rotate log files if necessary
	if (!fhActionLog) {
		LeaveCriticalSection(&cs);
		return;
	}
  #if 0	// 24/01/01 kriskoin:
	if (!fhAuditLog) {
		LeaveCriticalSection(&cs);
		return;
	}
  #endif

	time_t tt = time(NULL);
    time_t time_diff = 0;
  #if 0	// 24/01/01 kriskoin:
	switch (log_type) {
	case LOG_ACTION : 
        time_diff = tt - last_action_log_time_t;
		break;
	case LOG_AUDIT : 
        time_diff = tt - last_audit_log_time_t;
		break;
    }
  #else
	time_diff = tt - last_action_log_time_t;
  #endif

    if (time_diff >= 60) {  // write out a real time at least every 60 seconds.
        force_time_output = TRUE;
    }

    // Try to align to minute boundaries, but don't do too much work trying to do that.
    #define MAX_SECONDS_BETWEEN_ALIGNTMENT_TESTS    20
    if (!force_time_output && time_diff >= MAX_SECONDS_BETWEEN_ALIGNTMENT_TESTS) {
        // it has been a while since we wrote something and we're not already due
        // to do it this time through.
	    struct tm tm;
	    struct tm *t = localtime(&tt, &tm);
	    if (t && t->tm_sec < MAX_SECONDS_BETWEEN_ALIGNTMENT_TESTS) { // near the top of a minute?
            // we're near the top of a minute, write out a time.
            force_time_output = TRUE;
	    }
    }

    if (force_time_output) {    // write out a full date and time?  (yyyymmdd \t hh:mm:ss \t)
	    struct tm tm;
	    struct tm *t = localtime(&tt, &tm);
	    if (!t) {
		    Error(ERR_ERROR,"%s(%d) Got a null pointer from localtime() -- can't log", _FL);
		    LeaveCriticalSection(&cs);
		    return;
	    }
	    sprintf(log_string, "%04d%02d%02d%c%02d:%02d:%02d%c",
			    t->tm_year+1900, t->tm_mon+1, t->tm_mday, LSC,
			    t->tm_hour, t->tm_min, t->tm_sec, LSC);

	  #if 0	// 24/01/01 kriskoin:
    	switch (log_type) {
    	case LOG_ACTION : 
            last_action_log_time_t = tt;
    		break;
    	case LOG_AUDIT : 
            last_audit_log_time_t = tt;
    		break;
        }
	  #else
		last_action_log_time_t = tt;
	  #endif
    } else {    // just write out the time difference in seconds (diff \t\t)
	    sprintf(log_string, "%d%c%c", time_diff, LSC, LSC);
    }
	strcat(log_string, str);
	log_string[MAX_LOG_STRING_LEN-1] = 0;
  #if 0	// 24/01/01 kriskoin:
	switch (log_type) {
	case LOG_ACTION : 
		fprintf(fhActionLog, "%s\n", log_string);
		break;
	case LOG_AUDIT : 
		fprintf(fhAuditLog, "%s\n", log_string);
		break;
	default:
		Error(ERR_ERROR,"%s(%d) Unknown log type specified (%d)", _FL, log_type);
	}
  #else
	fprintf(fhActionLog, "%s\n", log_string);
  #endif
	#define FLUSH_INTERVAL	5	// flush every 5 seconds
	if (difftime(tt, last_flush_time) > FLUSH_INTERVAL) {
		last_flush_time = tt;
		FlushCurrentLogFiles();
	}
	LeaveCriticalSection(&cs);
	log_type = LOG_ACTION;	// suppress compiler warning for now, remove eventually
}

/**********************************************************************************
 Function PokerLog::LogComment(char *comment);
 date: kriskoin 2019/01/01 Purpose: log a comment
***********************************************************************************/
void PokerLog::LogComment(char *comment)
{
	char str[MAX_LOG_STRING_LEN];
	if (!comment) {
		return;
	}
	sprintf(str,"%d%c### %s", LOGTYPE_COMMENT, LSC, comment);
	WriteToLog(LOG_ACTION, str, TRUE);
}

/**********************************************************************************
 Function PokerLog::LogLogin(Log_Login *ll)
 date: kriskoin 2019/01/01 Purpose: log a user login
***********************************************************************************/
void PokerLog::LogLogin(Log_Login *ll)
{
	#define MAX_IP_STRING_LEN	20
	char str[MAX_LOG_STRING_LEN], ip[MAX_IP_STRING_LEN];
	IP_ConvertIPtoString(ll->ip_address, ip, MAX_IP_STRING_LEN);
	sprintf(str,"%d%c%lx%c%s%c%s%c%d%c%d%c%d%c%d",
		LOGTYPE_LOGIN, LSC, ll->player_id, LSC, ll->user_id, LSC, ip,
		LSC, ll->real_in_bank, LSC, ll->real_in_play,
		LSC, ll->fake_in_bank, LSC, ll->fake_in_play);
	WriteToLog(LOG_ACTION, str);
}
void PokerLog::LogLogin(char *name, WORD32 player_id, IPADDRESS ip, WORD32 rib, WORD32 rip, WORD32 fib, WORD32 fip)
{
	Log_Login ll;
	zstruct(ll);
	ll.ip_address = ip;
	ll.player_id = player_id;
	strnncpy(ll.user_id, (name ? name : "(??)"), MAX_PLAYER_USERID_LEN);
	ll.real_in_bank = rib;
	ll.real_in_play = rip;
	ll.fake_in_bank = fib;
	ll.fake_in_play = fip;
	LogLogin(&ll);
}

/**********************************************************************************
 Function PokerLog::LogLogout(Log_Logout *ll)
 date: kriskoin 2019/01/01 Purpose: log a user logout
***********************************************************************************/
void PokerLog::LogLogout(Log_Logout *ll)
{
	char str[MAX_LOG_STRING_LEN], ip[MAX_IP_STRING_LEN];
	IP_ConvertIPtoString(ll->ip_address, ip, MAX_IP_STRING_LEN);
	sprintf(str,"%d%c%lx%c%s%c%s%c%d%c%d%c%d%c%d",
		LOGTYPE_LOGOUT, LSC, ll->player_id, LSC, ll->user_id, LSC, ip,
		LSC, ll->real_in_bank, LSC, ll->real_in_play,
		LSC, ll->fake_in_bank, LSC, ll->fake_in_play);
	
	
	WriteToLog(LOG_ACTION, str);
}
void PokerLog::LogLogout(char *name, WORD32 player_id, IPADDRESS ip, WORD32 rib, WORD32 rip, WORD32 fib, WORD32 fip)
{
	Log_Logout ll;
	zstruct(ll);
	ll.ip_address = ip;
	ll.player_id = player_id;
	strnncpy(ll.user_id, (name ? name : "(??)"), MAX_PLAYER_USERID_LEN);
	ll.real_in_bank = rib;
	ll.real_in_play = rip;
	ll.fake_in_bank = fib;
	ll.fake_in_play = fip;
	LogLogout(&ll);
		
}

/**********************************************************************************
 Function ::LogNextGameSerialNumber
 date: kriskoin 2019/01/01 Purpose: write it out as soon as we know it -- in case of server crash
***********************************************************************************/
void PokerLog::LogNextGameSerialNumber(WORD32 next_game_serial_number)
{
	FILE *out;
	// we do this twice, just in case the server blows up right in the middle of
	// writing one of them
	if ((out = fopen(_next_game_filename_1, "wt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open %s for write", _FL, _next_game_filename_1);
	} else {
		fprintf(out,"%d\n", next_game_serial_number);
	}
	FCLOSE(out);
	// now the 2nd one
	if ((out = fopen(_next_game_filename_2, "wt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open %s for write", _FL, _next_game_filename_2);
	} else {
		fprintf(out,"%d\n", next_game_serial_number);
	}
	FCLOSE(out);
}

/**********************************************************************************
 Function PokerLog::LogGameStart(Log_GameStart *lgs)
 date: kriskoin 2019/01/01 Purpose: log the defintion of a game about to begin
***********************************************************************************/
void PokerLog::LogGameStart(Log_GameStart *lgs)
{
	char str[MAX_LOG_STRING_LEN];
	zstruct(str);

	// check if we need to write an index entry
  #if 1	// 2022 kriskoin
	#define GAME_INDEX_INTERVAL	2000	// write an index entry every n games
  #else
	#define GAME_INDEX_INTERVAL	250		// write an index entry every n games
  #endif
	if (_pending_index_write ||
			lgs->game_serial_number >= _last_index_game_number_written + GAME_INDEX_INTERVAL)
	{
		_pending_index_write = FALSE;
		_last_index_game_number_written = lgs->game_serial_number;
		WriteGameToIndex(lgs->game_serial_number, _action_filename_in_use);
	}
	_last_game_logged = lgs->game_serial_number;
	
	// at 20,000 hands per day, this will break in ~600 years.
	LogNextGameSerialNumber(lgs->game_serial_number+1);
	
	sprintf(str,"%d%c%d%c%d%c%s%c%d%c%d%c%d%c%d%c%d%c%d",
		LOGTYPE_GAME_START, LSC, lgs->game_serial_number, LSC, lgs->table_serial_number,
		LSC, lgs->table_name, LSC, lgs->game_rules, LSC, lgs->chip_type, LSC, lgs->big_blind_amount, 
		LSC, lgs->small_blind_amount, LSC, lgs->button, LSC, lgs->tournament_buyin);	
	
	/***************************   kriskoin  20/11/2003 ************************************/
	
	//add a record in the database (game_commissions) record the init of a game
	
	time_t tt = time(NULL);
    struct tm tm;
    struct tm *t = localtime(&tt, &tm);
	char tempMessage[200];

	sprintf(tempMessage, "%s%d%s%d%s%04d%02d%02d%s%02d%s%02d%s%02d%s", "SELECT sp_begin_game(",lgs->game_serial_number,",",lgs->table_serial_number,
	        ",'",t->tm_year+1900,t->tm_mon+1,t->tm_mday,"','",t->tm_hour,":",t->tm_min,":", t->tm_sec,"')");
	theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
	
	/***************************   kriskoin  20/11/2003 ************************************/
		
	WriteToLog(LOG_ACTION, str, TRUE);	
	
}

void PokerLog::LogGameStart(WORD32 game_serial_number, WORD32 table_serial_number, char *table_name,
	int game_rules, ChipType chip_type, INT32 big_blind_amount, INT32 small_blind_amount,
	int button, int tournament_buyin)
{
	Log_GameStart lgs;
	zstruct(lgs);
	lgs.game_serial_number = game_serial_number;
	lgs.table_serial_number = table_serial_number;
	strnncpy(lgs.table_name, (table_name ? table_name : "??"), MAX_TABLE_NAME_LEN);
	lgs.game_rules = game_rules;
	lgs.chip_type = chip_type;
	lgs.big_blind_amount = big_blind_amount;
	lgs.small_blind_amount = small_blind_amount;
	lgs.button = button;
	lgs.tournament_buyin = tournament_buyin;
	LogGameStart(&lgs);
}

//*********************************************************
// https://github.com/kriskoin//
// Log player entries at the start of a game
//
void PokerLog::LogGamePlayers(WORD32 game_serial_number, int player_count, Log_GamePlayerEntry *lgpe)
{
	char str[MAX_LOG_STRING_LEN];
    zstruct(str);
    // Build the sitting out flags.
    int sitting_out_flags = 0;
    int i;
    for (i=0 ; i<player_count ; i++) {
        if (lgpe[i].sitting_out_flag) {
            sitting_out_flags |= 1 << lgpe[i].seating_position;
        }
    }
	sprintf(str,"%d%c%d%c%x", LOGTYPE_GAME_PLAYER, LSC, game_serial_number, LSC, sitting_out_flags);

	/***************************   kriskoin  20/11/2018 ************************************/
	char tempMessage[200];
	/***************************   kriskoin  20/11/2018 ************************************/
	
	for (i=0 ; i<player_count ; i++) {
    	sprintf(str+strlen(str),
                    // argv[5+i*4+0] = seat_index
                    // argv[5+i*4+1] = player_id
                    // argv[5+i*4+2] = user_id
                    // argv[5+i*4+3] = chips
    	        "%c%d"  // seating_position
    	        "%c%x"  // player_id
    	        "%c%s"  // user_id
    	        "%c%d", // chips
                LSC, lgpe[i].seating_position,
                LSC, lgpe[i].player_id,
                LSC, lgpe[i].user_id,
                LSC, lgpe[i].chips);
		
		/***************************   kriskoin  20/11/2003 ************************************/
				
				sprintf(tempMessage, "%s%d%s%s%s%d%s%d%s", "SELECT sp_add_game_player(", game_serial_number,
					    ",'", lgpe[i].user_id, "',", lgpe[i].seating_position,",",(lgpe[i].chips)/100,")");
				theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
		
		/***************************   kriskoin  20/11/2003 ************************************/

	}
	
	WriteToLog(LOG_ACTION, str);

}
#if 0	//kriskoin: void PokerLog::LogGamePlayer(WORD32 game_serial_number, int seating_position,
	WORD32 player_id, char *user_id, INT32 chips, int sitting_out)
{
    struct Log_GamePlayerEntry lgpe;
    zstruct(lgpe);

	lgpe.seating_position = seating_position;
	lgpe.player_id = player_id;
	strnncpy(lgpe.user_id, (user_id ? user_id : "??"), MAX_PLAYER_USERID_LEN);
	lgpe.chips = chips;
	lgpe.sitting_out_flag = sitting_out;
	LogGamePlayers(game_serial_number, 1, &lgpe);
}
#endif

#if 0	// 2022 kriskoin
/**********************************************************************************
 Function PokerLog::LogGamePlayer(Log_GamePlayer *lgp)
 date: kriskoin 2019/01/01 Purpose: log a player entry at the start of a game
***********************************************************************************/
void PokerLog::LogGamePlayer(Log_GamePlayer *lgp)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%d%c%lx%c%s%c%d%c%d",
		LOGTYPE_GAME_PLAYER, LSC, lgp->game_serial_number, LSC, 
		lgp->seating_position, LSC, lgp->player_id, LSC, lgp->name, LSC, 
		lgp->chips, LSC, lgp->sitting_out);
	WriteToLog(LOG_ACTION, str);
}
void PokerLog::LogGamePlayer(WORD32 game_serial_number, int seating_position,
	WORD32 player_id, char *name, INT32 chips, int sitting_out)
{
	Log_GamePlayer lgp;
	zstruct(lgp);
	lgp.game_serial_number = game_serial_number;
	lgp.seating_position = seating_position;
	lgp.player_id = player_id;
	strnncpy(lgp.name, (name ? name : "??"), MAX_COMMON_STRING_LEN);
	lgp.chips = chips;
	lgp.sitting_out = sitting_out;
	LogGamePlayer(&lgp);
}
#endif

/**********************************************************************************
 Function LogGameAction(struct Log_GameAction *);
 date: kriskoin 2019/01/01 Purpose: log a player's action during a game
***********************************************************************************/
void PokerLog::LogGameAction(struct Log_GameAction *lga)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%d%c%d%c%d%c%d",
		LOGTYPE_GAME_ACTION, LSC, lga->game_serial_number, LSC, 
		lga->action_serial_number, LSC, lga->seating_position, LSC, 
		lga->action, LSC, lga->action_amount);
	
	/***************************   kriskoin  20/11/2003 ************************************/
	time_t tt = time(NULL);
    struct tm tm;
    struct tm *t = localtime(&tt, &tm);
	char tempMessage[200];
	
	sprintf(tempMessage, "%s%d%s%d%s%d%s%04d%02d%02d%s%02d%s%02d%s%02d%s%d%s%d%s", "SELECT sp_add_game_player_action(", lga->game_serial_number,",",lga->seating_position,
	        ",",lga->action_serial_number,",'",t->tm_year+1900,t->tm_mon+1,t->tm_mday,"','",t->tm_hour,":",t->tm_min,":", t->tm_sec,"',",
	        lga->action,",",(lga->action_amount/100),")");		
	theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
	
	/***************************   kriskoin  20/11/2003 ************************************/
	
	WriteToLog(LOG_ACTION, str);
}

void PokerLog::LogGameAction(WORD32 game_serial_number, int action_serial_number,
	int seating_position, int action, int action_amount, int game_rake)
{
	Log_GameAction lga;
	zstruct(lga);
	lga.game_serial_number = game_serial_number;
	lga.action_serial_number = action_serial_number;
	lga.seating_position = seating_position;
	lga.action = action;
	lga.action_amount = action_amount;
	lga.game_rake = game_rake;
	LogGameAction(&lga);
}

/**********************************************************************************
 void PokerLog::LogGameAll7csCards(struct Log_GameAll7csCards *lga7csc)
 Date: 20180707 kriskoin :  Purpose: log a string of a final order of 7cs cards (after shuffling pcoket cards)
***********************************************************************************/
void PokerLog::LogGameAll7csCards(struct Log_GameAll7csCards *lga7csc)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d%c%d",
		LOGTYPE_GAME_ALL_7CS_CARDS, LSC, lga7csc->game_serial_number, LSC, lga7csc->seating_position, LSC,
		lga7csc->card1, LSC, lga7csc->card2, LSC, lga7csc->card3, LSC, lga7csc->card4, LSC, 
		lga7csc->card5, LSC, lga7csc->card6, LSC, lga7csc->card7);
	WriteToLog(LOG_ACTION, str);
}

void PokerLog::LogGameAll7csCards(WORD32 game_serial_number, int seating_position,
	int c1, int c2, int c3, int c4, int c5, int c6, int c7)
{
	Log_GameAll7csCards lga7csc;
	zstruct(lga7csc);
	lga7csc.game_serial_number = game_serial_number;
	lga7csc.seating_position = seating_position;
	lga7csc.card1 = c1;
	lga7csc.card2 = c2;
	lga7csc.card3 = c3;
	lga7csc.card4 = c4;
	lga7csc.card5 = c5;
	lga7csc.card6 = c6;
	lga7csc.card7 = c7;
	LogGameAll7csCards(&lga7csc);
}

//*********************************************************
// https://github.com/kriskoin//
// Log some cards dealt during a game
// The input is an int pointer to an array which contains pairs of
// player seat indexes (or -1 for common) and card pairs.
//
void PokerLog::LogGameCardsDealt(WORD32 game_serial_number, int plr_card_pair_count, int *plr_card_pairs)
{
	char str[MAX_LOG_STRING_LEN];
	zstruct(str);
	sprintf(str,"%d%c%d",
		LOGTYPE_GAME_CARD_DEALT, LSC, game_serial_number);
	
	/***************************   kriskoin  20/11/2003 ************************************/
	
	time_t tt = time(NULL);
    struct tm tm;
    struct tm *t = localtime(&tt, &tm);
	char tempMessage[200];
	
	/***************************   kriskoin  20/11/2003 ************************************/
		
	for (int i=0 ; i<plr_card_pair_count ; i++) {
		sprintf(str + strlen(str), "%c%d%c%d",
				LSC, plr_card_pairs[i*2],
				LSC, plr_card_pairs[i*2+1]);
		
		/***************************   kriskoin  20/11/2003 ************************************/
	
		sprintf(tempMessage, "%s%d%s%04d%02d%02d%s%02d%s%02d%s%02d%s%d%s%d%d%s", "SELECT sp_add_game_dealer_action(",game_serial_number,",'",
		        t->tm_year+1900,t->tm_mon+1,t->tm_mday,"','",t->tm_hour,":",t->tm_min,":", t->tm_sec,"',",plr_card_pairs[i*2],",",SUIT(plr_card_pairs[i*2+1]),RANK(plr_card_pairs[i*2+1]),")");
		theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
	
		/***************************   kriskoin  20/11/2003 ************************************/
	
	}
		
	WriteToLog(LOG_ACTION, str);
}

void PokerLog::LogGameCardDealt(WORD32 game_serial_number, int index, Card card)
{
	int plr_card_pair[2];
	plr_card_pair[0] = index;
	plr_card_pair[1] = card;
	LogGameCardsDealt(game_serial_number, 1, plr_card_pair);
}

#if 0	// 2022 kriskoin
/**********************************************************************************
 Function PokerLog::LogGameCardDealt(struct Log_GameCardDealt *lgcd)
 date: kriskoin 2019/01/01 Purpose: log any card that's dealt
***********************************************************************************/
void PokerLog::LogGameCardDealt(struct Log_GameCardDealt *lgcd)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%d%c%d",
		LOGTYPE_GAME_CARD_DEALT, LSC, lgcd->game_serial_number, LSC, 
		lgcd->index, LSC, lgcd->card);
	WriteToLog(LOG_ACTION, str);
}
void PokerLog::LogGameCardDealt(WORD32 game_serial_number, int index, Card card)
{
	Log_GameCardDealt lgcd;
	zstruct(lgcd);
	lgcd.game_serial_number = game_serial_number;
	lgcd.index = index;
	lgcd.card = card;
	LogGameCardDealt(&lgcd);
}
#endif

/**********************************************************************************
 Function PokerLog::LogGameChatMsg(struct Log_GameChatMsg *lgcm)
 date: kriskoin 2019/01/01 Purpose: log what a player said in chat
***********************************************************************************/
void PokerLog::LogGameChatMsg(struct Log_GameChatMsg *lgcm)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%s%c%s",
		LOGTYPE_GAME_CHAT_MSG, LSC, lgcm->game_serial_number, LSC, 
		lgcm->name, LSC, lgcm->chat_msg);
	WriteToLog(LOG_ACTION, str);
}
void PokerLog::LogGameChatMsg(WORD32 game_serial_number, char *name, char *chat_msg)
{
	Log_GameChatMsg lgcm;
	zstruct(lgcm);
	lgcm.game_serial_number = game_serial_number;
	strnncpy(lgcm.name, (name ? name : "(??)") , MAX_COMMON_STRING_LEN);
	strnncpy(lgcm.chat_msg, (chat_msg ? chat_msg : "(??)"), MAX_LOG_CHAT_MSG_LEN);
	LogGameChatMsg(&lgcm);
}

/**********************************************************************************
 Function PokerLog::LogGameEndPlayer(struct Log_GameEndPlayer *lgep)
 date: kriskoin 2019/01/01 Purpose: log a player's state after a game
***********************************************************************************/
void PokerLog::LogGameEndPlayer(struct Log_GameEndPlayer *lgep)
{
	char str[MAX_LOG_STRING_LEN];
	sprintf(str,"%d%c%d%c%d%c%d%c%d%c%s",
		LOGTYPE_GAME_END_PLAYER, LSC, lgep->game_serial_number, LSC, lgep->seating_position,
		LSC, lgep->showed_hand, LSC, lgep->chips_net_change, 
		LSC, lgep->hand_desc);	

	/***************************   kriskoin  20/11/2003 ************************************/
	
		char tempMessage[200];
		sprintf(tempMessage, "%s%d%s%d%s%d%s", "SELECT sp_player_earnings(",lgep->game_serial_number,",",lgep->seating_position,",",(lgep->chips_net_change)/100,")");
	    theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
	
	/***************************   kriskoin  20/11/2003 ************************************/
	
	WriteToLog(LOG_ACTION, str);
}

void PokerLog::LogGameEndPlayer(WORD32 game_serial_number, int seating_position,
	int showed_hand, WORD32 chips_net_change, char *hand_desc)
{
	Log_GameEndPlayer lgep;
	zstruct(lgep);
	lgep.game_serial_number = game_serial_number;
	lgep.seating_position = seating_position;
	lgep.showed_hand = showed_hand;
	lgep.chips_net_change = chips_net_change;
	strnncpy(lgep.hand_desc, (hand_desc ? hand_desc : " "), MAX_LOG_DESCRIPTION*2);
	LogGameEndPlayer(&lgep);
}

/**********************************************************************************
 Function PokerLog::LogGameEnd(struct Log_GameEnd *lge)
 date: kriskoin 2019/01/01 Purpose: log the end of the game and all its pots
***********************************************************************************/
void PokerLog::LogGameEnd(struct Log_GameEnd *lge)
{
	char str[MAX_LOG_STRING_LEN];
	int thePot = 0;
	sprintf(str,"%d%c%d%c%d%",
		LOGTYPE_GAME_END, LSC, lge->game_serial_number, LSC, lge->rake);
	char szPot[20];
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		sprintf(szPot,"%c%d", LSC, lge->pot[i]);
		strcat(str, szPot);
		thePot += (int)lge->pot[i];
	}
		
	/***************************   kriskoin  20/11/2003 ************************************/
	
	//modify a record in the database (game_commissions) record the end of a game
	
	time_t tt = time(NULL);
    struct tm tm;
    struct tm *t = localtime(&tt, &tm);
	char tempMessage[200];
	sprintf(tempMessage, "%s%d%s%d%s%d%s%04d%02d%02d%s%02d%s%02d%s%02d%s", "SELECT sp_end_game(", lge->game_serial_number,",",(thePot/100),",",
	        (lge->rake/100),",'",t->tm_year+1900,t->tm_mon+1,t->tm_mday,"','",t->tm_hour,":",t->tm_min,":", t->tm_sec,"')");
	theOPA->AddMessage(tempMessage, DB_NORMAL_QUERY);
	
	/***************************   kriskoin  20/11/2003 ************************************/
		
	WriteToLog(LOG_ACTION, str);
	
}

void PokerLog::LogGameEnd(WORD32 game_serial_number, int rake, WORD32 pot[])
{
	Log_GameEnd lge;
	zstruct(lge);
	lge.game_serial_number = game_serial_number;
	lge.rake = rake;
	for (int i=0; i < MAX_PLAYERS_PER_GAME; i++) {
		lge.pot[i] = pot[i];
	}
	LogGameEnd(&lge);
}

/**********************************************************************************
 Function PokerLog::LogFinancialTransaction(struct Log_Transaction *lt)
 date: kriskoin 2019/01/01 Purpose: general purpose function for adding audit trail entries
***********************************************************************************/
void PokerLog::LogFinancialTransaction(struct Log_Transaction *lt)
{
	if (lt->chips) {	// only log non-zero changes.
		char str[MAX_LOG_STRING_LEN];
		zstruct(str);

		sprintf(str,"%d%c%lx%c%d%c%d%c%d",
			lt->transaction_type, LSC, lt->player_id, LSC, lt->game_serial_number, LSC,
			lt->chips, LSC, lt->chip_type);
		if (lt->comment[0] || lt->description[0]) {
			sprintf(str+strlen(str),
					"%c%s",
					LSC, lt->description);
			if (lt->comment[0]) {
				sprintf(str+strlen(str),
						"%c%s",
						LSC, lt->comment);
			}
		}
	  #if 0	// 24/01/01 kriskoin:
		WriteToLog(LOG_AUDIT, str);
	  #else
		WriteToLog(LOG_ACTION, str);
	  #endif
	}
}

void PokerLog::LogFinancialTransaction(LogTypeMessage transaction_type, WORD32 player_id,
	WORD32 game_serial_number, WORD32 chips, ChipType chip_type, char *description, char *comment)
{
	Log_Transaction lt;
	zstruct(lt);
	lt.transaction_type = transaction_type;
	lt.player_id = player_id;
	lt.game_serial_number = game_serial_number;
	lt.chips = chips;
	lt.chip_type = chip_type;
	strnncpy(lt.description, (description ? description : ""), MAX_LOG_DESCRIPTION);
	strnncpy(lt.comment, (comment ? comment : ""), MAX_LOG_DESCRIPTION);
	LogFinancialTransaction(&lt);
}

/**********************************************************************************
 Function PokerLog::LogFinancialTransfer(struct Log_Transfer *lt)
 Date: 2017/7/7 kriskoin Purpose: transfer from one player account to another
***********************************************************************************/
void PokerLog::LogFinancialTransfer(struct Log_Transfer *lt)
{
	if (lt->amount) {
		char str[MAX_LOG_STRING_LEN];
		sprintf(str,"%d%c%lx%c%lx%c%d%c%d%c%d%c%d%c%s",
			lt->transaction_type, LSC, lt->from_id, LSC, lt->to_id, LSC, lt->amount, LSC, lt->from_account_field,
			LSC, lt->to_account_field, LSC, lt->chip_type, LSC, lt->reason);
		  #if 0	// 24/01/01 kriskoin:
			WriteToLog(LOG_AUDIT, str);
		  #else
			WriteToLog(LOG_ACTION, str);
		  #endif
	}
}

void PokerLog::LogFinancialTransfer(LogTypeMessage transaction_type, WORD32 from_id, WORD32 to_id,
	INT32 amount, int from_account_field, int to_account_field, ChipType chip_type, char *reason)
{
	Log_Transfer lt;
	zstruct(lt);
	lt.transaction_type = transaction_type;
	lt.from_id = from_id;
	lt.to_id = to_id;
	lt.amount = amount;
	lt.from_account_field = from_account_field;
	lt.to_account_field = to_account_field;
	lt.chip_type = chip_type;
	strnncpy(lt.reason, (reason ? reason : ""), MAX_TRANSFER_REASON_LEN);
	LogFinancialTransfer(&lt);
}

/**********************************************************************************
 Function PokerLog::QueueHandHistoryReq(HandHistoryRequestType hhrt, WORD32 hand_num, WORD32 player_id, int admin_flag)
 date: kriskoin 2019/01/01 Purpose: entry point for external hand history requests
 Returns: T/F if something got queued
***********************************************************************************/
int PokerLog::QueueHandHistoryReq(HandHistoryRequestType hhrt, WORD32 hand_num,
			WORD32 dest_player_id, WORD32 src_player_id, int admin_flag)
{
	if (!_hrht_is_running) {	// can't queue if there's no thread to handle it
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to queue a hhrt, but no thread running", _FL);
		return FALSE;
	}
	if (_quit_hrht) {	// can't queue now, we're shutting down
		kp(("%s(%d) HandHistory req received while shutting down -- ignoring", _FL));
		return FALSE;
	}
	struct HandHistoryQueueStruct *qs;
	// malloc a struct for this queue entry
	qs = (struct HandHistoryQueueStruct *)malloc(sizeof(HandHistoryQueueStruct));
	if (!qs) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Couldn't malloc a hhqs", _FL);
		return FALSE;
	}
	qs->admin_flag = admin_flag;
	qs->hhrt = hhrt;
	qs->hand_num = hand_num;
	qs->next = NULL;
	qs->src_player_id = src_player_id;
	qs->dest_player_id = dest_player_id;
	qs->queue_time = SecondCounter;

	EnterCriticalSection(&cs);
	// find its place
	if (!_hh_queue) {	// first entry
		_hh_queue = qs;
	} else {	// stuff there already
		*_hh_last = qs;
	}
	// store the last one for next use
	_hh_last = &qs->next;
	LeaveCriticalSection(&cs);
	return TRUE;
}
/**********************************************************************************
 Function FixTournamentBlindsAndLevels(&low_limit, &high_limit, &tourn_level, &tourn_game);
 date: 24/01/01 kriskoin Purpose: fixes all of these parameters after unscaling hidden information from them
***********************************************************************************/
void PokerLog::FixTournamentBlindsAndLevels(int small_blind, int big_blind, 
		int *low_limits, int *high_limits, int *tourn_level, int *tourn_game)
{
	#define SCALING_FACTOR	1000000	// see parallel code in table.cpp
	*tourn_level = big_blind / SCALING_FACTOR;
	*tourn_game = small_blind / SCALING_FACTOR;
	*low_limits = *low_limits % SCALING_FACTOR;
	*high_limits = *high_limits % SCALING_FACTOR;
}

//*********************************************************
// https://github.com/kriskoin//
// Output some info about a player starting a game (internal to building a hand history)
//
void PokerLog::OutputPlayerInfo(char *str, int admin_flag, ChipType chip_type, int player_index, WORD32 player_id, char *user_id, int chips, int sitting_out_flag)
{
	BLANK(str);	// no entry in logfile for player sitting out (non-admin)
    char cs1[MAX_CURRENCY_STRING_LEN];
	if (admin_flag) {	// admin gets more info
		sprintf(str,"Seat #%2d: [0x%06lx] %s  (%s in chips) -- %s\n",
			player_index+1, player_id, user_id, 
			CurrencyString(cs1, chips, chip_type, TRUE),
			sitting_out_flag ? "sitting out" : "playing");
	} else {	// typical user gets this
		if (!sitting_out_flag) {
			sprintf(str,"Seat %2d: %s  (%s in chips)\n",
				player_index+1, user_id, 
				CurrencyString(cs1, chips, chip_type));
		}
	}
}	

/**********************************************************************************
 Function BuildHandHistory(int game_number, int admin_flag, FILE *out)
 date: kriskoin 2019/01/01 Purpose: rebuild a transcript of a hand from the log file and write it out
		  to *out (which should be a buffer of size HAND_HISTORY_BUFFER at least
 Returns: TRUE if it built something useful, FALSE if it didn't
***********************************************************************************/
int PokerLog::BuildHandHistory(WORD32 game_number, WORD32 player_id, int admin_flag, FILE *out, int highlight_all_ins_flag)
{
	char log_filename[MAX_FNAME_LEN];
	zstruct(log_filename);
	char prefix[MAX_FNAME_LEN];
	zstruct(prefix);
	char basename[MAX_FNAME_LEN];
	zstruct(basename);
	int field_index = 0;

	if (!out) {
		Error(ERR_ERROR,"%s(%d) BuildHandHistory was passed a null FILE *out", _FL);
		return FALSE;
	}
	
	// check if the game is currently being played
	if ((CardRoom *)(CardRoomPtr->lowest_active_game_number <= game_number)) {
		if ((CardRoom *)(CardRoomPtr->IsGameStillActive(game_number))) {
			// it's still being played --
			fprintf(out,"****GAME #%d IS STILL IN PROGRESS\n", game_number);
			fprintf(out,"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
			pr(("%s(%d) Game is still in progress\n", _FL));
			return TRUE; // let it continue
		}
	}
	
	// get the initial filename
	WORD32 start_offset = 0;
	GetLogFilenameForGame(game_number, log_filename, &start_offset);
	if (!log_filename[0]) {
		Error(ERR_ERROR,"%s(%d) Couldn't find name of initial log file for game %d", _FL, game_number);
		return FALSE;
	}
	
	/*************************** kriskoin ***********************************/
	
	/*FILE *debug = NULL;
			if ((debug = fopen(log_filename, "rb")) != NULL) {
			break;	// we could open it, we're done looking.
		}

	
	/******************************************************************************/
	
	FILE *in = NULL;
	GetDirFromPath(log_filename, prefix);
	GetNameFromPath(log_filename, basename);
	int i;
	for (i=0 ; i <= ArchiveDirCount ; i++) {
		if (i>=1) {
			sprintf(log_filename, "%sarchive%d/%s", prefix, i, basename);
		}
		pr(("%s(%d) Trying to open %s\n", _FL, log_filename));
		if ((in = fopen(log_filename, "rb")) != NULL) {
			break;	// we could open it, we're done looking.
		}
	}
	if (in == NULL) {
	  #if DEBUG
		char game_str[MAX_CURRENCY_STRING_LEN];
		kp(("%s(%d) Couldn't open log file %s for game %s (plr $%08lx)\n",
				_FL, basename, IntegerWithCommas(game_str, game_number), player_id));
	  #endif
		return FALSE;
	}
	if (start_offset > 0) {
		// Seek a char back and scan to next newline in case
		// we seeked to the middle of a line.
		fseek(in, start_offset-1, SEEK_SET);	// seek to starting offset in log file (might be zero)
		forever {
			int c = fgetc(in);
			if (c=='\n' || feof(in)) {
				break;
			}
		}
	} else {
		fseek(in, start_offset, SEEK_SET);	// seek to starting offset in log file (might be zero)
	}
	//kp(("%s(%d) Seeked to position %d in file '%s', position is now %d\n", _FL, start_offset, log_filename, ftell(in)));
  #if 0
	{
		static WORD32 old_ticks = 0;
		WORD32 new_ticks = GetTickCount();
		if (!old_ticks) {
			old_ticks = new_ticks;
		}
		WORD32 elapsed = new_ticks - old_ticks;
		old_ticks = new_ticks;
		kp(("%s(%d) BuildHandHistory(): game #%d (elapsed for prev=%dms)\n", _FL, game_number, elapsed));
	}
  #endif

	// cards, etc for each player
	memset(cards, CARD_NO_CARD, sizeof(Card)*MAX_PLAYERS_PER_GAME*MAX_PRIVATE_CARDS);
	memset(common_cards, CARD_NO_CARD, sizeof(Card)*MAX_PUBLIC_CARDS);
	int card_index[MAX_PLAYERS_PER_GAME];
	memset(card_index, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int showed_hand[MAX_PLAYERS_PER_GAME];
	memset(showed_hand, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int total_bet[MAX_PLAYERS_PER_GAME];
	memset(total_bet, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int total_bet_this_round[MAX_PLAYERS_PER_GAME];
	memset(total_bet_this_round, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int player_show_action[MAX_PLAYERS_PER_GAME];
	memset(player_show_action, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int player_all_in[MAX_PLAYERS_PER_GAME];
	memset(player_all_in, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	int sitting_out[MAX_PLAYERS_PER_GAME];
	memset(showed_hand, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
	char player_user_ids[MAX_PLAYERS_PER_GAME][MAX_PLAYER_USERID_LEN];
	memset(player_user_ids, 0, sizeof(char)*MAX_PLAYERS_PER_GAME*MAX_PLAYER_USERID_LEN);
	char player_total[MAX_PLAYERS_PER_GAME][MAX_LOG_STRING_LEN];
	memset(player_total, 0, sizeof(char)*MAX_PLAYERS_PER_GAME*MAX_LOG_STRING_LEN);
	char player_cards_str[MAX_PLAYERS_PER_GAME][MAX_HAND_CARDS_LEN];
	memset(player_cards_str, 0, sizeof(char)*MAX_PLAYERS_PER_GAME*MAX_HAND_CARDS_LEN);

	int common_card_index = 0;
	char common_card_str[MAX_LOG_STRING_LEN];
	zstruct(common_card_str);
	char table_name[MAX_TABLE_NAME_LEN];
	zstruct(table_name);
	char szFlop[MAX_LOG_DESCRIPTION];
	zstruct(szFlop);
	char szTurn[MAX_LOG_DESCRIPTION];
	zstruct(szTurn);

	int line_count = 0;
	int we_can_continue = TRUE;
	char data_line[MAX_LOG_STRING_LEN+1];
	zstruct(data_line);
	char tmp_str[MAX_LOG_STRING_LEN+1];
	zstruct(tmp_str);
	char *argv[MAX_LOG_LINE_FIELDS];
	char str[HAND_HISTORY_BUFFER];	// our temp output string
	zstruct(str);
	char cs1[MAX_CURRENCY_STRING_LEN];
	char cs2[MAX_CURRENCY_STRING_LEN];
	char cs3[MAX_CURRENCY_STRING_LEN];
	zstruct(cs1);
	zstruct(cs2);
	zstruct(cs3);
	WORD32 player_ids[MAX_PLAYERS_PER_GAME];
	int field_count;
	// variables used inside the switch
	int show_card_now;
	int last_fieldtype_handled = -1;
	int low_limit = 0;
	int high_limit = 0;
	int tourn_level = 0;
	int tourn_game = 0;
	int max_bet_this_round = 0;
	GameRules game_rules = GAME_RULES_HOLDEM;
	ChipType chip_type = CT_NONE;
	int one_on_one_game = FALSE;
	int clear_bets = FALSE;
	int action_amount;
	int action_index;
	int player_index;
	int net_change;
	int collected;
	int offset;
	int before_override;
	int showed_a_card;
	int game_rules_read;
	char *p;
	int date, ye, mo, da;
	int j;
	Card card;
	while (we_can_continue) {
		we_can_continue = GetNextLogLineForGame(log_filename, &in, game_number, data_line);
		if (!we_can_continue || !strlen(data_line)) { // something valid?
			break;	// exit loop
		}

		//-----
		//kriskoin: 		// only gets triggered when a single game contains more than 2000
		// lines of data.  If the game end is missing, it's the responsibility
		// of the GetNextLogLineForGame() function to detect the problem.
		// Therefore, this code doesn't detect what it was intended to detect.
		// It wasn't removed today because I didn't want to break anything today.
		line_count++;
		// if we're searching for what seems to be too long, give up
		#define MAX_LINES_TO_SCAN_BEFORE_QUITTING	2000
		if (line_count > MAX_LINES_TO_SCAN_BEFORE_QUITTING) {	// no end of game found
			Error(ERR_WARNING,"%s(%d) Went beyond %d lines trying to find an end to game #%d",
				_FL, MAX_LINES_TO_SCAN_BEFORE_QUITTING, game_number);
			FCLOSE(in);
			return FALSE;
		}
		//-----

		// to trap all related lines, for now, print the unformatted line if we don't parse it
		data_line[strlen(data_line)-1] = 0;	// trim newline
		sprintf(str, "%s\n", data_line);
		field_count = GetArgs(MAX_LOG_LINE_FIELDS, argv, data_line, LSC);
		switch (atoi(argv[2])) {
			case LOGTYPE_GAME_START:
				if (!VerifyLogLine(LOGTYPE_GAME_START, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				strnncpy(table_name, argv[5], MAX_TABLE_NAME_LEN);
				game_rules_read = atoi(argv[6]);
				// 24/01/01 kriskoin:
				// we have added OLD_HEADSUP_RULE_LOGGING_OFFSET to this if it's a one on one
				if (game_rules_read > OLD_HEADSUP_RULE_LOGGING_OFFSET) {	// see table.cpp where we call LogGameStart
					game_rules_read -= OLD_HEADSUP_RULE_LOGGING_OFFSET;
					one_on_one_game = TRUE;
				}
				// figure out if it's the old or new rule set
				switch (game_rules_read) {
				// we will stagger all cases of old and new
				case 0:
				case GAME_RULES_HOLDEM:
					game_rules = GAME_RULES_HOLDEM;
					break;
				case 1:
				case GAME_RULES_OMAHA_HI:
					game_rules = GAME_RULES_OMAHA_HI;
					break;
				case 2:
				case GAME_RULES_STUD7:
					game_rules = GAME_RULES_STUD7;
					break;
				case 3:	// this was the old one-on-one
					game_rules = GAME_RULES_HOLDEM;
					break;
				case 4:
				case GAME_RULES_OMAHA_HI_LO:
					game_rules = GAME_RULES_OMAHA_HI_LO;
					break;
				case 5:
				case GAME_RULES_STUD7_HI_LO:
					game_rules = GAME_RULES_STUD7_HI_LO;
					break;
				default:
					Error(ERR_ERROR,"%s(%d) Logfile(%s) bad data? - unknown game type(%d)",
						_FL, log_filename, game_rules_read);
					FCLOSE(in);
					return FALSE;
				}
				chip_type = (ChipType)atoi(argv[7]);
				char game_name[20];
				switch (game_rules) {
				case GAME_RULES_HOLDEM:
					low_limit = atoi(argv[8]);
					high_limit = 2*low_limit;
					sprintf(game_name,"Hold'em");
					break;
				case GAME_RULES_OMAHA_HI:
					low_limit = atoi(argv[8]);
					high_limit = 2*low_limit;
					sprintf(game_name,"Omaha");
					break;
				case GAME_RULES_OMAHA_HI_LO:
					low_limit = atoi(argv[8]);
					high_limit = 2*low_limit;
					sprintf(game_name,"Omaha Hi Lo");
					break;
				case GAME_RULES_STUD7:
					low_limit = 2*atoi(argv[8]);
					high_limit = 2*low_limit;
					sprintf(game_name,"7-Card Stud");
					break;
				case GAME_RULES_STUD7_HI_LO:
					low_limit = 2*atoi(argv[8]);
					high_limit = 2*low_limit;
					sprintf(game_name,"7-Card Stud Hi Lo");
					break;
				default:
					Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - unknown game type(%d)",
						_FL, log_filename, game_rules);
					FCLOSE(in);
					return FALSE;
				}
				// build game-log-title string
				date = atoi(argv[0]);
				da = date % 100;
				mo = (date % 10000) / 100;
				ye = date / 10000;
				char game_type_str[20];
				zstruct(game_type_str);
				switch (chip_type) {
					case CT_NONE:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
						break;
					case CT_PLAY:
						sprintf(game_type_str,"play chips");
						break;
					case CT_REAL:
						sprintf(game_type_str,"real money");
						break;
					case CT_TOURNAMENT:
						if (field_count == 12) {
							sprintf(game_type_str, "%s tournament",
								CurrencyString(cs1, atoi(argv[11]), CT_REAL));
						} else {
							sprintf(game_type_str, "tournament");
						}
						break;
					default:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
				}
				if (chip_type == CT_TOURNAMENT) {
					int sb = atoi(argv[9]);
					int bb = atoi(argv[8]);
					FixTournamentBlindsAndLevels(sb, bb, &low_limit, &high_limit, &tourn_level, &tourn_game);
					char tmp_str[100];
					zstruct(tmp_str);
					sprintf(tmp_str,"Game #%s (Level %s, Game #%d)",
						argv[3],
						szRomanNumerals[tourn_level],
						tourn_game);
					sprintf(str,"%s - %s/%s %s%s- %4d/%02d/%02d-%s (CST)\n",
						tmp_str,
						CurrencyString(cs1, low_limit, chip_type), 
						CurrencyString(cs2, high_limit, chip_type), 
						game_name, 
						(one_on_one_game ? " (1-on-1) " : " "),
						ye, mo, da, argv[1]);
				} else {
					sprintf(str,"Game #%s - %s/%s %s%s- %4d/%02d/%02d-%s (CST)\n",
						argv[3],  
						CurrencyString(cs1, low_limit, chip_type), 
						CurrencyString(cs2, high_limit, chip_type), 
						game_name, 
						(one_on_one_game ? " (1-on-1) " : " "),
						ye, mo, da, argv[1]);
				}
				if (admin_flag) {
					if (game_rules == GAME_RULES_STUD7 || game_rules == GAME_RULES_STUD7_HI_LO) {	// no button in 7cs
						sprintf(tmp_str,"Table \"%s\" [%s] (%s)\n",	table_name, argv[4], game_type_str);
					} else {
						sprintf(tmp_str,"Table \"%s\" [%s] (%s) -- Seat %d is the button\n",
							table_name, argv[4], game_type_str, atoi(argv[10])+1);
					}
				} else {
					if (game_rules == GAME_RULES_STUD7 || game_rules == GAME_RULES_STUD7_HI_LO) {	// no button in 7cs
						sprintf(tmp_str,"Table \"%s\" (%s)\n", table_name, game_type_str);
					} else {
						sprintf(tmp_str,"Table \"%s\" (%s) -- Seat %d is the button\n",
							table_name, game_type_str, atoi(argv[10])+1);
					}
				}
				strcat(str, tmp_str);
				last_fieldtype_handled = LOGTYPE_GAME_START;
				break;
			case LOGTYPE_GAME_PLAYER:
				// validate the line
				if (!VerifyLogLine(LOGTYPE_GAME_PLAYER, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				//kriskoin: 				BLANK(str);	// might be used, might not
				if (field_count==9) {	// old style
					player_index = atoi(argv[4]);
					if (*argv[5]=='$') {
						sscanf(argv[5]+1, "%x", &player_ids[player_index]);
					} else {
						sscanf(argv[5], "%x", &player_ids[player_index]);
					}
					strnncpy(player_user_ids[player_index], argv[6], MAX_PLAYER_USERID_LEN);
					sitting_out[player_index] = atoi(argv[8]);
					OutputPlayerInfo(str, admin_flag, chip_type,
					            player_index,
					            player_ids[player_index],
								player_user_ids[player_index],	// user_id
								atoi(argv[7]),		// chips
								atoi(argv[8]));		// sitting out flag

				} else {	// new style (more than one player per line)
                    // argv[4]     = sitting out flags (bitmask, one bit for each seat)
                    // argv[5+i*4+0] = seat_index
                    // argv[5+i*4+1] = player_id
                    // argv[5+i*4+2] = user_id
                    // argv[5+i*4+3] = chips
                    int sitting_out_flags = 0;
				    sscanf(argv[4], "%x", &sitting_out_flags);
                    pr(("%s(%d) sitting_out_flags = %02x\n", _FL, sitting_out_flags));
                    player_index = 0;
				    for (field_index=5 ; field_index<field_count ; field_index+=4) {	// loop through all pairs of plr/cards we found
					    player_index = atoi(argv[field_index]);
					    sscanf(argv[field_index+1], "%x", &player_ids[player_index]);
					    strnncpy(player_user_ids[player_index], argv[field_index+2], MAX_PLAYER_USERID_LEN);
                        int chips = atoi(argv[field_index+3]);
					    sitting_out[player_index] = (sitting_out_flags >> player_index) & 1;
                        pr(("%s(%d) sitting_out[%d] = %d\n", _FL, player_index, sitting_out[player_index]));
    					OutputPlayerInfo(str, admin_flag, chip_type,
   					            player_index,
   					            player_ids[player_index],
								player_user_ids[player_index],	// user_id
								chips,                  		// chips
								sitting_out[player_index]);		// sitting out flag
					    if (str[0]) {
						    fputs(str, out);
    					    BLANK(str);
					    }
        			}
				}
				last_fieldtype_handled = LOGTYPE_GAME_PLAYER;
				break;

			case LOGTYPE_COMMENT:
				if (admin_flag) {	// admins get to see embedded comments
					sprintf(str,"%s", argv[3]);
				}
				break;
			
			case LOGTYPE_GAME_ACTION:
				// validate the line
				if (!VerifyLogLine(LOGTYPE_GAME_ACTION, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				action_index = atoi(argv[6]);
				// copy the action string without the leading ampersand
				offset = 0;
				if (ActionStrings[action_index][0] == '&') {
					offset = 1;
				}
				sprintf(tmp_str, ActionStrings[action_index]+offset);
				// override any strings here
				switch (action_index) {
				case ACT_TOSS_HAND:
					sprintf(tmp_str,"Winner, does not show cards");
					break;
				case ACT_SHOW_HAND:
				case ACT_MUCK_HAND:
					BLANK(tmp_str);	// no entry
					BLANK(str);
					break;
				case ACT_SHOW_SHUFFLED:
					// it was decided not to disclose whether he hit [show] or [show shuffled]
					sprintf(tmp_str, ActionStrings[ACT_SHOW_HAND]);
					break;
				}
				// convert the newline in the middle into a space
				p = strchr(tmp_str, '\n');
				if (p) {
					*p = ' ';
				}
				// get rid of & in the middle
				p = strchr(tmp_str, '&');
				if (p) {
					*p = ' ';
				}
				player_index = atoi(argv[5]);
				action_amount = atoi(argv[7]);	
				// keep track for show hand status
				if (action_index == ACT_TOSS_HAND || 
					action_index == ACT_MUCK_HAND ||
					action_index == ACT_FOLD) {
						player_show_action[player_index] = action_index;
				}
				total_bet[player_index] += action_amount;				
				total_bet_this_round[player_index] += action_amount;				
				max_bet_this_round = max(max_bet_this_round, total_bet_this_round[player_index]);
				// keep track of all-ins
				if (action_index == ACT_BET_ALL_IN		||
					action_index == ACT_CALL_ALL_IN		||
					action_index == ACT_RAISE_ALL_IN	||
					action_index == ACT_POST_ALL_IN		||
					action_index == ACT_FORCE_ALL_IN	||
					action_index == ACT_BRING_IN_ALL_IN) {
						player_all_in[player_index] = TRUE;
						if (highlight_all_ins_flag) {
							CurrencyString(
								cs1, 
								max_bet_this_round- total_bet_this_round[player_index],
								chip_type);
							char tmp_str2[50];
							zstruct(tmp_str2);
							sprintf(tmp_str2, " <<<***(%s)***>>>", cs1);
							strcat(tmp_str, tmp_str2);
						}
				}
				char sz_action[20];
				BLANK(sz_action);
				if (action_amount) {
					CurrencyString(cs1, action_amount, chip_type);
					sprintf(sz_action, "(%s)", cs1);
				}
				if (tmp_str[0]) {	// don't bother if it's blank
					sprintf(str,"%-8s: %s %s\n", player_user_ids[player_index],
						tmp_str, sz_action);
				}
				last_fieldtype_handled = LOGTYPE_GAME_ACTION;
				break;
			case LOGTYPE_GAME_CARD_DEALT:
				// validate the line
				pr(("%s(%d) game #%d: cards dealt field count = %d (%d cards)\n",
						_FL, game_number, field_count, (field_count - 4) / 2));
				if (!VerifyLogLine(LOGTYPE_GAME_CARD_DEALT, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}

				for (field_index=4 ; field_index<field_count ; field_index+=2) {	// loop through all pairs of plr/cards we found
					BLANK(str);	// might be used, might not
					player_index = atoi(argv[field_index]);
					card = (Card)atoi(argv[field_index+1]);
					pr(("%s(%d) game #%d:    player %2d got card %2d\n", _FL, game_number, player_index, card));
					if (player_index < 0) {	// common card
						if (common_card_index >= MAX_PUBLIC_CARDS) {
							Error(ERR_INTERNAL_ERROR,
								"%s(%d) Logfile %s found too many common cards",
								_FL, log_filename, game_number);
							FCLOSE(in);
							return FALSE;
						}
						common_cards[common_card_index++] = card;
					} else {
						if (card_index[player_index] >= MAX_PRIVATE_CARDS) {
							Error(ERR_INTERNAL_ERROR,
								"%s(%d) Logfile %s found too many cards for player %08lx in game %d",
								_FL, log_filename, player_ids[player_index], game_number);
							FCLOSE(in);
							return FALSE;
						}
						cards[player_index][card_index[player_index]] = card;
						card_index[player_index]++;
					}
					// figure out if we print it now
					show_card_now = FALSE;
					if (player_index < 0) show_card_now = TRUE; // common card
					if (admin_flag) show_card_now = TRUE;
					if (player_ids[player_index] == player_id) show_card_now = TRUE;
					if (game_rules == GAME_RULES_STUD7 || game_rules==GAME_RULES_STUD7_HI_LO) {
						// carefull!! it's been incremented already
						if (card_index[player_index] > 2 && card_index[player_index] < 7) {
							show_card_now = TRUE;
						}
					}
					clear_bets = FALSE;
					if (!show_card_now) {	// nothing much to show for now
						if (last_fieldtype_handled != LOGTYPE_GAME_CARD_DEALT) {
							sprintf(str, "Dealing...\n");
						} 
						last_fieldtype_handled = LOGTYPE_GAME_CARD_DEALT;
					} else if (player_index < 0) {	// common card
						if (game_rules != GAME_RULES_STUD7 && game_rules != GAME_RULES_STUD7_HI_LO) {
							if (common_card_index == 1) {	// 1st of flop
								sprintf(common_card_str,"****FLOP**** : > %c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
								sprintf(szFlop, "[ %c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
								sprintf(szTurn, "[ %c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
							} else if (common_card_index == 2) {	// 2nd of flop
								sprintf(tmp_str,"%c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
								strcat(common_card_str, tmp_str);
								strcat(szFlop, tmp_str);
								strcat(szTurn, tmp_str);
							} else if (common_card_index == 3) {	// 3rd of flop
								sprintf(tmp_str,"%c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
								strcat(szFlop, tmp_str);
								strcat(szTurn, tmp_str);
								strcat(common_card_str, tmp_str);
								strcat(common_card_str, "<\n");
								strcat(szFlop, ">");
								strcpy(str, common_card_str);
								clear_bets = TRUE;	// card always means new betting round
							} else if (common_card_index == 4) {	// turn card
								char tmp2[10];
								zstruct(tmp2);
								sprintf(tmp2, "%c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
								sprintf(str, "****TURN**** : %s > %s <\n", szFlop, tmp2);
								strcat(szTurn, tmp2);
								strcat(szTurn, "]");
								clear_bets = TRUE;	// card always means new betting round
							} else {	/// river card
								sprintf(str,"****RIVER**** : %s > %c%c <\n", szTurn, cRanks[RANK(card)], cSuits[SUIT(card)]);
								clear_bets = TRUE;	// card always means new betting round
							}
						} else {	// 7-card stud -- rare
							clear_bets = TRUE;	// card always means new betting round
							sprintf(str,"****COMMON CARD**** : > %c%c <\n", cRanks[RANK(card)], cSuits[SUIT(card)]);
						}
					} else {	// player's card
						sprintf(str,"Dealt to %s < %c%c >\n", 
							player_user_ids[player_index],
							cRanks[RANK(card)], cSuits[SUIT(card)] );
						last_fieldtype_handled = LOGTYPE_GAME_CARD_DEALT;
					}
					// cards dealt means we're between betting rounds
					if (clear_bets) {
						max_bet_this_round = 0;
						memset(total_bet_this_round, 0, sizeof(int)*MAX_PLAYERS_PER_GAME);
					}
					// If we found anything, write it out now.
					if (str[0]) {
						fputs(str, out);
						str[0] = 0;
					}
				}
				break;
			case LOGTYPE_GAME_CHAT_MSG:
				if (!VerifyLogLine(LOGTYPE_GAME_CHAT_MSG, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
			  #if 0	//kriskoin: 				{
					// Make sure the chat line does not contain any weird characters.
					unsigned char *p = (unsigned char *)argv[5];
					while (*p) {
						if (*p < 32 || *p >= 0x80) {
							kp(("%s(%d) Warning: found character $%02x in chat text.\n", _FL, *p));
							*p = '*';
						}
						p++;
					}
				}
			  #endif
				sprintf(str,"%s said, \"%s\"\n", argv[4], argv[5]);
				last_fieldtype_handled = LOGTYPE_GAME_CHAT_MSG;
				break;

			case LOGTYPE_GAME_ALL_7CS_CARDS:
				// received a sortecd bunch of cards for this player, which is what we should display for him
				// validate the line
				if (!VerifyLogLine(LOGTYPE_GAME_ALL_7CS_CARDS, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				BLANK(str);	// nothing to show at this time
				player_index = atoi(argv[4]);
				BLANK(player_cards_str[player_index]);
				{
					sprintf(player_cards_str[player_index]," [");
					for (int lga=0; lga < MAX_PRIVATE_CARDS; lga++) {
						card = (Card)atoi(argv[lga+5]);
						sprintf(player_cards_str[player_index]+strlen(player_cards_str[player_index]),
							" %c%c ", cRanks[RANK(card)], cSuits[SUIT(card)]);
					}
					sprintf(player_cards_str[player_index]+strlen(player_cards_str[player_index]),"]");
				}
				break; 
				
			case LOGTYPE_GAME_END_PLAYER:
				// validate the line
				if (!VerifyLogLine(LOGTYPE_GAME_END_PLAYER, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				BLANK(str);	// nothing to show at this time
				player_index = atoi(argv[4]);
				showed_hand[player_index] = atoi(argv[5]);
				net_change = atoi(argv[6]);
				collected = net_change + total_bet[player_index];
				CurrencyString(cs1, total_bet[player_index], chip_type);
				CurrencyString(cs2, abs(collected), chip_type);
				CurrencyString(cs3, abs(net_change), chip_type);
				if (net_change != 0 || total_bet[player_index]) {	// some chips net change activity
					if (net_change < 0 && abs(net_change) == total_bet[player_index]) {	// lost all he bet
						sprintf(player_total[player_index],"%s lost %s ", 
							player_user_ids[player_index], cs1);
					} else {	// some sort of net change
						sprintf(player_total[player_index],"%s bet %s, collected %s, netting %c%s ",
							player_user_ids[player_index], cs1, cs2, 
							(net_change < 0 ? '-' : '+'), cs3);
					}
				} else {	// no chip movement
					sprintf(player_total[player_index],"%s did not bet ", player_user_ids[player_index]);
				}
				// tag line for showing
				switch (player_show_action[player_index]) {
					case ACT_TOSS_HAND:
					case ACT_MUCK_HAND:
						sprintf(tmp_str, " ");	// just nothing if either of the above
						break;
					case ACT_FOLD:
						sprintf(tmp_str, "(folded) ");
						break;
					default:
						sprintf(tmp_str, " ");
						break;
				}
				if (showed_hand[player_index]) {
					sprintf(tmp_str, "(showed hand)");
				}
				strcat(player_total[player_index], tmp_str);
				// 24/01/01 kriskoin:
				show_card_now = showed_hand[player_index];
				if (!show_card_now) {	// didn't show on purpose, but we might flip them anyway
					int psa = player_show_action[player_index];
					if (psa == ACT_TOSS_HAND) {	// last one left
						show_card_now = FALSE;
					} else if (psa == ACT_MUCK_HAND) {	// he mucked, meaning called on the river
						show_card_now = TRUE;
					} else if (psa == ACT_FOLD) {
						show_card_now = FALSE; // he folded, don't show
					}
				}
				// adate: show everyone's cards who was all in
				if (player_all_in[player_index]) show_card_now = TRUE;
				// show everything for admin
				if (admin_flag) show_card_now = TRUE;
				// it's the player himself, show cards
				if (player_ids[player_index] == player_id) show_card_now = TRUE;
				// draw cards
				before_override = show_card_now;	// need to restore to this
				showed_a_card = FALSE;
				if (player_cards_str[player_index][0]) {	// override is there, just display that
					strcat(player_total[player_index], player_cards_str[player_index]);
				} else {
					for (j = 0; j < card_index[player_index]; j++) {
						if  (game_rules == GAME_RULES_STUD7 || game_rules==GAME_RULES_STUD7_HI_LO) {
							// show cards 2,3,4,5 (0,1 and 6 are possibly hidden)
							if (j > 1 && j < 6) {
								show_card_now = TRUE;
							} else {
								show_card_now = before_override;
							}
						}
						if (show_card_now) {
							showed_a_card = TRUE;
							/// this doesn't work for 7cs if pocket isn't revealed!!
							if (!j) {	// works for most cases
								strcat(player_total[player_index], " [");
							}
							// in 7cs, if we haven't shown the pocket, we need an opening bracket on 3rd st
							if (j == 2 && before_override == FALSE && (game_rules == GAME_RULES_STUD7 || game_rules==GAME_RULES_STUD7_HI_LO)) {
								strcat(player_total[player_index], " [");
							}
							sprintf(tmp_str," %c%c ", 
								cRanks[RANK(cards[player_index][j])], 
								cSuits[SUIT(cards[player_index][j])]);
							strcat(player_total[player_index], tmp_str);
						}
					}
					if (showed_a_card) {
						strcat(player_total[player_index], "] ");
					}
				}
				// restore to show/don't show hand
				show_card_now = before_override;
				if (show_card_now) {
					char szHi[MAX_LOG_DESCRIPTION];
					char szLo[MAX_LOG_DESCRIPTION];
					zstruct(szHi);
					zstruct(szLo);
					GetHandDescription(game_rules, player_index, szHi, szLo);					
					if  (game_rules == GAME_RULES_OMAHA_HI_LO || game_rules==GAME_RULES_STUD7_HI_LO) {
						if (szLo[0]) {
							sprintf(tmp_str,"\n    LO: %s", szLo);
							strcat(player_total[player_index], tmp_str);
						}
						if (szHi[0]) {
							sprintf(tmp_str,"\n    HI: %s", szHi);
							strcat(player_total[player_index], tmp_str);
						}
					} else {
						if (szHi[0]) {
							sprintf(tmp_str, " (%s)", szHi);
							strcat(player_total[player_index], tmp_str);
						}
					}
				}

				strcat(player_total[player_index], "\n");
				last_fieldtype_handled = LOGTYPE_GAME_END_PLAYER;
				break;

			case LOGTYPE_GAME_END:
				if (!VerifyLogLine(LOGTYPE_GAME_END, field_count, log_filename)) {
					FCLOSE(in);
					return FALSE;
				}
				sprintf(str,"****RESULTS SUMMARY****\n");
				CurrencyString(cs1, atoi(argv[5]), chip_type);
				sprintf(tmp_str, "Pot: %s | ", cs1);
				strcat(str, tmp_str);
				for (player_index = 1; player_index < MAX_PLAYERS_PER_GAME; player_index++) {
					if (atoi(argv[player_index+5])) {
						CurrencyString(cs1, atoi(argv[player_index+5]), chip_type);
						sprintf(tmp_str, "Side Pot %d: %s | ", player_index, cs1);
						strcat(str, tmp_str);
					}
				}
				if (chip_type != CT_TOURNAMENT) {	// don't need to see "T0" everytime
					CurrencyString(cs1, atoi(argv[4]), chip_type);
					sprintf(tmp_str, "Rake: %s\n", cs1);
					strcat(str, tmp_str);
				}
				// common cards
				if (common_card_index) {	// there's a flop
					sprintf(tmp_str,"Board: [");
					strcat(str, tmp_str);
					for (i=0; i < common_card_index; i++) {
						sprintf(tmp_str," %c%c ", 
							cRanks[RANK(common_cards[i])], 
							cSuits[SUIT(common_cards[i])]);
						strcat(str, tmp_str);
					}
					strcat(str, "]\n");
				}
				fputs(str, out);
				for (i=0; i < MAX_PLAYERS_PER_GAME; i++) {
					if (player_user_ids[i][0]) {	// someone there
						fputs(player_total[i], out);
					}
				}
				fprintf(out,"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
				FCLOSE(in);
				FlushCurrentLogFiles();	// keep up to date with game ends
				return TRUE;
		}
		if (str[0]) {
			fputs(str, out);
			str[0] = 0;
		}
	}
	// we're done
	fprintf(out,"*** END OF GAME #%d WAS NOT FOUND\n", game_number);
	fprintf(out,"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
	kp(("%s(%d) Hand history for game #%d is incomplete\n", _FL, game_number));
	FCLOSE(in);
	return FALSE;
}

/**********************************************************************************
 Function PokerLog::VerifyLogLine(int packet_type, int field_count, char *filename)
 date: kriskoin 2019/01/01 Purpose: validate a data line, print and return FALSE if it isn't
***********************************************************************************/
int PokerLog::VerifyLogLine(int packet_type, int field_count, char *filename)
{
	int valid_count;
	switch (packet_type) {
	case LOGTYPE_GAME_START:
		if (field_count < 11 || field_count > 12) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be 11 or 12",
				_FL, filename, packet_type, field_count);
			return FALSE;
		}
		break;
	case LOGTYPE_GAME_PLAYER:
		valid_count = 9;
		if (field_count < valid_count || ((field_count-5) % 4)) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be %d",
				_FL, filename, packet_type, field_count, valid_count);
			return FALSE;
		}
		break;
	case LOGTYPE_GAME_ACTION:
		valid_count = 8;
		if (field_count != valid_count) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be %d",
				_FL, filename, packet_type, field_count, valid_count);
			return FALSE;
		}
		break;
	case LOGTYPE_GAME_CARD_DEALT:
		//kriskoin: 		valid_count = 6;
		if (field_count < valid_count || (field_count & 1)) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be %d",
				_FL, filename, packet_type, field_count, valid_count);
			return FALSE;
		}
		break;
	case LOGTYPE_GAME_CHAT_MSG:
		valid_count = 6;
		if (field_count != valid_count) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be %d",
				_FL, filename, packet_type, field_count, valid_count);
			return FALSE;
		}
		break;
	case LOGTYPE_GAME_END_PLAYER:
		if (field_count < 7 || field_count > 8) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be 7 or 8",
				_FL, filename, packet_type, field_count);
			return FALSE;
		}
		break;

	case LOGTYPE_GAME_ALL_7CS_CARDS:
		valid_count = 12;
		if (field_count != valid_count) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be 12",
				_FL, filename, packet_type, field_count);
			return FALSE;
		}
		break;
	
	case LOGTYPE_GAME_END:
		valid_count = 15;
		if (field_count != valid_count) {
			Error(ERR_WARNING,"%s(%d) Logfile(%s) bad_data_line - type(%d), %d args, should be %d",
				_FL, filename, packet_type, field_count, valid_count);
			return FALSE;
		}
		break;
	}
	// no problem
	return TRUE;
}

/**********************************************************************************
 Function GetNextLogLineForGame(FILE *in, int game_number, char *out)
 date: kriskoin 2019/01/01 Purpose: internal, find next data line for this game number
 Note:    this will set eof to TRUE if it reaches the end of the current file
		  *out should be at least of size MAX_LOG_STRING_LEN
 Returns: TRUE if we can keep going, FALSE if we can't
 N.B. *filename parameter may get modified, and must be size MAX_LOG_FILENAME_LEN
***********************************************************************************/
int PokerLog::GetNextLogLineForGame(char *filename, FILE **in, int game_number, char *out)
{
	char str[MAX_LOG_STRING_LEN+1], parsable[MAX_LOG_STRING_LEN+1];
	char *argv[MAX_LOG_LINE_FIELDS];
	zstruct(str);
	zstruct(parsable);
	zstruct(argv);
	char log_filename[MAX_FNAME_LEN];
	zstruct(log_filename);
	char prefix[MAX_FNAME_LEN];
	zstruct(prefix);
	char basename[MAX_FNAME_LEN];
	zstruct(basename);
	int count;
	*out = 0; // blank
	do {
		if (*in == NULL) {	// bad file
			Error(ERR_ERROR,"%s(%d) GetNextLogLineForGame called with null file (%d)",_FL, *in);
				return FALSE;
		}
		zstruct(str);
		fgets(str, MAX_LOG_STRING_LEN, *in);
		strnncpy(parsable, str, MAX_LOG_STRING_LEN+1);
		count = GetArgs(MAX_LOG_LINE_FIELDS, argv, parsable, LSC);
		if (count > 2) {
			switch (atoi(argv[2])) {
			case LOGTYPE_GAME_START:
			case LOGTYPE_GAME_PLAYER:
			case LOGTYPE_GAME_ACTION:
			case LOGTYPE_GAME_CARD_DEALT:
			case LOGTYPE_GAME_CHAT_MSG:
			case LOGTYPE_GAME_END_PLAYER:
			case LOGTYPE_GAME_ALL_7CS_CARDS:

			case LOGTYPE_GAME_END:
				if (count < 4) {	// broken line
					return FALSE;
				}
				if (atoi(argv[3]) == game_number) {	// found what we wanted
					strnncpy(out, str, MAX_LOG_STRING_LEN);
					return TRUE;
				} 
				break;
			case LOGTYPE_NEXT_LOGFILE:
			  #if 0	// 2022 kriskoin
				{
					kp(("%s(%d) Found LOGTYPE_NEXT_LOGFILE: argcount=%d... original line='%s'  args:\n",_FL, count, str));
					for (int i=0 ; i<count ; i++) {
						kp(("%s(%d) #%2d:   \"%s\"\n", _FL, i, argv[i])); 
					}
					kp(("%s(%d) previous line was '%s'\n",_FL, prev_line));
				}
			  #endif
				if (count < 4) {	// broken line
					return FALSE;
				}
				// we've reached the end of this logfile -- go on to the next one
				//kp(("%s(%d) Closing old log file ('%s') ftell=%d\n", _FL, filename, ftell(*in)));
				FCLOSE(*in);

				strnncpy(log_filename, argv[3], MAX_LOG_FILENAME_LEN);
				FixPath(log_filename);
				GetDirFromPath(log_filename, prefix);
				GetNameFromPath(log_filename, basename);
				int i;
				for (i=0 ; i <= ArchiveDirCount ; i++) {
					if (i>=1) {
						sprintf(log_filename, "%sarchive%d/%s", prefix, i, basename);
					}
					pr(("%s(%d) Trying to open %s\n", _FL, log_filename));
					if ((*in = fopen(log_filename, "rb")) != NULL) {
						break;	// we could open it, we're done looking.
					}
				}

				if (*in == NULL) {
					Error(ERR_ERROR,"%s(%d) Couldn't transfer from logfile \"%s\" to logfile \"%s\"",
						_FL, filename, basename);
					return FALSE;
				}
				strnncpy(filename, log_filename, MAX_LOG_FILENAME_LEN);	// update filename in use
				//kp(("%s(%d) New log opened successfully. filename='%s'\n", _FL, filename));
				break;
			}
		}

		// Sleep occasionally to prevent tying up the CPU too much.
		if (lines_since_sleep_test++ > 1000) {
			lines_since_sleep_test = 0;
			if (GetTickCount() - last_sleep_ms > HandHistorySleepInterval) {
				Sleep(HandHistorySleepTime);
				last_sleep_ms = GetTickCount();
			}
		}

	} while (!feof(*in));
	// we've hit the end without finding what we wanted
	pr(("%s(%d) Reached end of all log files\n", _FL));
	return FALSE;
}

/**********************************************************************************
 Function GetArgs(int maxArgc, char *argv[], char *string, char seperator)
 date: kriskoin 2019/01/01 Purpose: internal parser
***********************************************************************************/
int PokerLog::GetArgs(int maxArgc, char *argv[], char *string, char seperator)
{
	memset(argv, 0, sizeof(argv[0])*maxArgc);	// start by setting all ptrs to null
	int argc = 0;
	while (*string) {
		argv[argc++] = string;
		if (argc >= maxArgc)
			return -1;
		while (*string && *string != seperator)
			++string;
		if (!*string)
			break;
		*(string++) = '\0';
	}
	return argc;
}

/**********************************************************************************
 Function CreateLogFileNames(void)
 date: kriskoin 2019/01/01 Purpose: create the name of the current logfile to write to
***********************************************************************************/
void PokerLog::CreateLogFileNames(void)
{
	time_t tt = time(NULL);
	struct tm tm;
	struct tm *t = localtime(&tt, &tm);
	if (!t) {
		Error(ERR_ERROR,"%s(%d) Got a null pointer from localtime() -- can't log", _FL);
		DIE("localtime() is returning NULL");
		return;
	}

//	sprintf(_action_filename, "logs/%04d%02d%02d.%s",
	sprintf(_action_filename, "Data/History/%04d%02d%02d.%s",

	t->tm_year+1900, t->tm_mon+1, t->tm_mday, _action_log_extension);
  #if 0	// 24/01/01 kriskoin:
//	sprintf(_audit_filename, "logs/%04d%02d%02d.%s",
	sprintf(_audit_filename, "Data/History/%04d%02d%02d.%s",
		t->tm_year+1900, t->tm_mon+1, t->tm_mday, _audit_log_extension);
  #endif
}

/**********************************************************************************
 Function PokerLog::WriteGameToIndex(WORD32 game_number, char *filename)
 date: kriskoin 2019/01/01 Purpose: when we've rolled to a new logfile, write the next game to start to the index
***********************************************************************************/
void PokerLog::WriteGameToIndex(WORD32 game_number, char *log_filename)
{
	// determine the current length of the log file (or 0 if not found)
	long current_file_length = 0;
	if (fhActionLog) {
		fflush(fhActionLog);
	}
	FILE *fd = fopen(log_filename, "rb");
	if (fd) {
		fseek(fd, 0, SEEK_END);
		current_file_length = ftell(fd);
		fclose(fd);
	}

	// build the proper name for this index

	char index_name[MAX_LOG_FILENAME_LEN];
	GetIndexNameForExt(_action_log_extension, index_name);
	FILE *out;
	if ((out = fopen(index_name, "at")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open log file index (%s) for append", _FL, index_name);
		return;
	}
	// write the entry
	fprintf(out,"%s%c%d%c%u\n", log_filename, LSC, game_number, LSC, current_file_length);
	FCLOSE(out);
}

/**********************************************************************************
 Function PokerLog::GetIndexNameForExt(char *ext, char *out)
 date: kriskoin 2019/01/01 Purpose: given an extension of a series of logfiles, get the name of the index
***********************************************************************************/
void PokerLog::GetIndexNameForExt(char *ext, char *out)
{
	char str[MAX_LOG_FILENAME_LEN];
	sprintf(str,"Data/History/%sindex.idx", ext);
	strnncpy(out, str, MAX_LOG_FILENAME_LEN);
}

/**********************************************************************************
 Function GetLogFilenameForGame(WORD32 game_number, char *filename);
 date: kriskoin 2019/01/01 Purpose: given a game number, find the filename of the logfile where it begins
***********************************************************************************/
void PokerLog::GetLogFilenameForGame(WORD32 game_number, char *filename, WORD32 *output_starting_offset)
{
	// blank the name till we find it
	// build the proper name for this index
	char index_name[MAX_LOG_FILENAME_LEN];
	*output_starting_offset = 0;
	GetIndexNameForExt(_action_log_extension, index_name);
	FILE *in;
	if ((in = fopen(index_name, "rt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open log file index (%s) for input", _FL, index_name);
		*filename = 0; // blank it
		return;
	}
	char *argv[MAX_LOG_LINE_FIELDS];
	char data_line[MAX_LOG_STRING_LEN];
	do {
		fgets(data_line, MAX_LOG_STRING_LEN, in);
		int count = GetArgs(MAX_LOG_LINE_FIELDS, argv, data_line, LSC);
		WORD32 starting_offset = 0;	// default to no known starting offset
		if (count == 3) {			// does this line include the file offset?
			starting_offset = atoi(argv[2]);	// save the starting offset
			//kp(("%s(%d) count==3, starting_offset = %d\n", _FL, starting_offset));
		} else if (count != 2) {
			continue;
		}
		// check if we want the last entry; if not, check if we've gone past what we're looking for
		if (atoi(argv[1]) > (int)game_number) {	// we've gone past; last one was the one
			FCLOSE(in);
			// we've got the relevant logfile name
			pr(("%s(%d) game #%d is in %s somewhere after position %d\n",
						_FL, game_number, filename, *output_starting_offset));
			return;
		}
		strnncpy(filename, argv[0], MAX_LOG_FILENAME_LEN);	// current "live" name 
		*output_starting_offset = starting_offset;

		// Sleep occasionally to prevent tying up the CPU too much.
		if (lines_since_sleep_test++ > 500 && GetTickCount() - last_sleep_ms > HandHistorySleepInterval) {
			Sleep(HandHistorySleepTime);
			last_sleep_ms = GetTickCount();
			lines_since_sleep_test = 0;
		}
	} while (!feof(in));
	// reached eof, didn't find it -- assume it's on the current log file
	FCLOSE(in);
}

/**********************************************************************************
 Function PokerLog::SetNextGameSerialNumber(WORD32 serial_num)
 date: kriskoin 2019/01/01 Purpose: write this to the index log (used on server's next startup)
***********************************************************************************/
void PokerLog::SetNextGameSerialNumber(WORD32 serial_num)
{
	WriteGameToIndex(serial_num, _action_filename_in_use);
	LogNextGameSerialNumber(serial_num);	// so they'll all be the same
}

/**********************************************************************************
 Function PokerLog::GetNextGameSerialNumber(void)
 date: kriskoin 2019/01/01 Purpose: retrieve from the index log what the next game serial number should be
***********************************************************************************/
WORD32 PokerLog::GetNextGameSerialNumber(void)
{
	char index_name[MAX_LOG_FILENAME_LEN];
	GetIndexNameForExt(_action_log_extension, index_name);
	FILE *in;
	if ((in = fopen(index_name, "rt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open log file index (%s) for input", _FL, index_name);
		return 1;
	}
	char *argv[MAX_LOG_LINE_FIELDS];
	char data_line[MAX_LOG_STRING_LEN];
	WORD32 next_game = 1;
	do {
		fgets(data_line, MAX_LOG_STRING_LEN, in);
		int count = GetArgs(MAX_LOG_LINE_FIELDS, argv, data_line, LSC);
		if (count != 2 && count != 3) continue;
		next_game = atoi(argv[1]);
	} while (!feof(in));
	FCLOSE(in);
	// reached eof, now check if it correlates
	WORD32 sn1 = 0, sn2 = 0;
	#define MAX_SERIAL_NUM_LEN 20
	char str[MAX_SERIAL_NUM_LEN];
	// grab the first backup
	if ((in = fopen(_next_game_filename_1, "rt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open %s for read", _FL, _next_game_filename_1);
	} else {
		fgets(str, MAX_SERIAL_NUM_LEN, in);
		sn1 = atoi(str);
	}
	FCLOSE(in);
	// now grab the second backup
	if ((in = fopen(_next_game_filename_2, "rt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open %s for read", _FL, _next_game_filename_2);
	} else {
		fgets(str, MAX_SERIAL_NUM_LEN, in);
		sn2 = atoi(str);
	}
	FCLOSE(in);
	// they should all be the same
	if (next_game == sn1 && sn1 == sn2) {	// all is ok, result of clean shutdown
		if (DebugFilterLevel <= 0) {
			kp(("%s(%d) Next game serial number is OK -- setting next to %d\n", _FL, next_game));
		}
		return next_game;
	}
	// if this is the first time creating backups, we'll have to use the sn from the log file
	if (!sn1 && !sn2) {
		Error(ERR_NOTE,"%s(%d) Backup game serial numbers don't exist -- using %d from logfile", _FL, next_game);
		return next_game;
	}
	// potential trouble, lets figure it out
	if (sn1 == sn2) { // these two are the same, implies server crash
		Error(ERR_NOTE,"%s(%d) Game serial numbers don't match (server crash?) -- correcting and setting to %d", _FL, sn1+1);
		return sn1+1;
	}
	// 	if any two of the three match, we'll use that.
	if (next_game == sn1 || next_game == sn2) {
		Error(ERR_NOTE,"%s(%d) Server crash? But two next game serial numbers agree, therefore using %d", _FL, next_game+1);
		return next_game+1;
	}
	// very serious disagreement: jump way ahead	
	WORD32 new_sn = max(max(sn1,sn2),next_game) + 100000;	// get the highest we have, add 100,000
	Error(ERR_ERROR,"%s(%d) Serious crash -- no idea what the next game serial number should be, setting %d", _FL, new_sn);
	return new_sn;
}

/**********************************************************************************
 Function PokerLog::FlushCurrentLogFiles(void)
 date: kriskoin 2019/01/01 Purpose: flush the active log file
***********************************************************************************/
void PokerLog::FlushCurrentLogFiles(void)
{
	if (fhActionLog) {
		fflush(fhActionLog);
	}
  #if 0	// 20:::	if (fhAuditLog) {
		fflush(fhAuditLog);
	}
  #endif
}

/**********************************************************************************
 Function PokerLog::GetLastLoggedGameSerialNumber(void)
 date: kriskoin 2019/01/01 Purpose: return the serial number of the last game we logged
***********************************************************************************/
WORD32 PokerLog::GetLastLoggedGameSerialNumber(void)
{
	return _last_game_logged;
}

/**********************************************************************************
 Function void _cdecl LaunchHandReqThreadLauncher(void *args)
 date: kriskoin 2019/01/01 Purpose: non class function for LaunchHandReqThread to call
***********************************************************************************/
static void _cdecl LaunchHandReqThreadLauncher(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Hand history thread");	// register this thread for stack dumps if we crash
	//kp(("%s(%d) LaunchHandReqThreadLauncher: pid = %d\n", _FL, getpid()));
	((PokerLog *)args)->HandReqHandlerThread();
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}

/**********************************************************************************
 Function PokerLog::LaunchHandReqThread(void)
 date: kriskoin 2019/01/01 Purpose: launch the thread which handles hand history requests
***********************************************************************************/
int PokerLog::LaunchHandReqThread(void)
{
	//kp(("%s(%d) Accept thread calling _beginthread. Our pid = %d\n", _FL, getpid()));
	int result = _beginthread(LaunchHandReqThreadLauncher, 0, (void *)this);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
		return ERR_FATAL_ERROR;
	}
	//kp(("%s(%d) Accept thread back from _beginthread. Our pid = %d\n", _FL, getpid()));
	return ERR_NONE;
}

/**********************************************************************************
 Function PokerLog::FinishedQueueRequest(char *filename)
 date: kriskoin 2019/01/01 Purpose: do all handling when we're finished dealing with current queue structure
***********************************************************************************/
void PokerLog::FinishedQueueRequest(char *filename)
{
	if (_hh_queue) {
		EnterCriticalSection(&cs);
		HandHistoryQueueStruct *tmp = _hh_queue;
		_hh_queue = _hh_queue->next;
		free(tmp);
		LeaveCriticalSection(&cs);
	}

  #if 0	// leave them around
	if (filename) {
		remove(filename);
	}
  #endif
	NOTUSED(filename);	// suppress warning if we're not deleting them
}

/**********************************************************************************
 Function PokerLog::HandReqHandlerThread(void)
 date: kriskoin 2019/01/01 Purpose: thread for handling hand history requests
***********************************************************************************/
void PokerLog::HandReqHandlerThread(void)
{
	_hrht_is_running = TRUE;
	while (!_quit_hrht) {
		if (!_hh_queue) {
			Sleep(300);
		} else {	// something got queued
			WORD32 src_player_id = _hh_queue->src_player_id;
			WORD32 dest_player_id = _hh_queue->dest_player_id;
			HandHistoryRequestType hhrt = _hh_queue->hhrt;
			int admin_flag = _hh_queue->admin_flag;
			WORD32 hand_num = _hh_queue->hand_num;
			WORD32 begin_time = _hh_queue->queue_time;

			FlushCurrentLogFiles();	// as up to date as possible
			SDBRecord player_rec;	// the result structure for the requesting player
			zstruct(player_rec);

			// First extract the info for the person we want to email this
			// thing to.
			int found_player = SDB->SearchDataBaseByPlayerID(dest_player_id, &player_rec);
			if (found_player < 0) {	// didn't find the account
				Error(ERR_ERROR, "%s(%d) Didn't find SDB record for player %08lx", _FL, dest_player_id);
				FinishedQueueRequest(NULL);
				continue;
			}
			pr(("%s(%d) dest_player_id = $%08lx, rec.user_id = '%s', rec.email = '%s'\n",
					_FL, dest_player_id, player_rec.user_id, player_rec.email_address));
			char dest_userid[MAX_PLAYER_USERID_LEN];
			strnncpy(dest_userid, player_rec.user_id, MAX_PLAYER_USERID_LEN);
			char dest_email[MAX_EMAIL_ADDRESS_LEN];
			strnncpy(dest_email, player_rec.email_address, MAX_EMAIL_ADDRESS_LEN);

			if (!strlen(dest_email)) {
				kp(("%s %s(%d) Warning: dest_player_id = $%08lx, rec.user_id = '%s', rec.email = '%s'\n",
					TimeStr(), _FL, dest_player_id, player_rec.user_id, player_rec.email_address));
			}

			// Now look up the source account...
			found_player = SDB->SearchDataBaseByPlayerID(src_player_id, &player_rec);
			if (found_player < 0) {	// didn't find the account
				Error(ERR_ERROR, "%s(%d) Didn't find SDB record for player %08lx", _FL, src_player_id);
				FinishedQueueRequest(NULL);
				continue;
			}
			// generate the temp file
			char file_out[MAX_FNAME_LEN];
			MakeTempFName(file_out, "h");
			//kp1(("%s(%d) temp output filename = '%s'\n", _FL, file_out));
			FILE *out = NULL;
			if ((out = fopen(file_out, "wt")) == NULL) {
				Error(ERR_ERROR,"%s(%d) Couldn't open tmp log file (%s) for write", _FL, file_out);
				FinishedQueueRequest(NULL);
				continue;
			}
			// 24/01/01 kriskoin:
			if (hhrt == HHRT_ADMIN_GAIR_LETTER) {
				// we will build it as a temporary file here... which we can email directly or pop up in the admin info?
				if (!iRunningLiveFlag) {
					fprintf(out, "*** THIS CAME FROM THE TEST SERVER -- NOT THE LIVE SERVER***\n\n");
				}
				fprintf(out,
				// header
					"Hello,\n"
					"\n"
					"We have received a request from you to reset your All-Ins. Our server "
					"reports that during this All-In you had a good connection to our server.\n"
					"\n"
					"We kindly ask that you explain the circumstances surrounding your All-In.\n"
					"\n"
					"\n"
					"Regards,\n"
					"Desert Poker Support\n"
					"\n"
					"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n"
					"All-In reset requested by ' %s ' after hand #%d:\n",
					player_rec.user_id,
					hand_num);
				// hand history
				int rc = BuildHandHistory(hand_num,	player_rec.player_id, FALSE, out, TRUE);
				if (!rc) {	// problem getting the hh
					kp(("(%s)%d Unable to send GAIR letter to %08lx regarding hand #%d\n",
						_FL, player_rec.player_id, hand_num));
					FCLOSE(out);
					FinishedQueueRequest(NULL);
					continue;
				}
				fprintf(out, "Connection status for recent All-Ins:\n");
				// connection status
				for (int i=0 ; i<ALLINS_TO_RECORD_PER_PLAYER ; i++) {
					char *connection_state = "unknown";
					switch (player_rec.all_in_connection_state[i]) {
					case 0:
						connection_state = "good";
						break;
					case 1:
						connection_state = "poor";
						break;
					case 2:
						connection_state = "bad";
						break;
					case 3:
						connection_state = "lost";
						break;
					}
					if (player_rec.all_in_times[i]) {
						fprintf(out, " %s (%s) game # %d\n",
							TimeStr(player_rec.all_in_times[i], FALSE, TRUE, SERVER_TIMEZONE),
							connection_state,
							player_rec.all_in_game_numbers[i]);
					}
				}
				// done writing the file -- ship it
				FCLOSE(out);
				char _subject[80];
				zstruct(_subject);
				sprintf(_subject, "All-In Reset request from %s", player_rec.user_id);
				Email(player_rec.email_address, "Desert Poker All-In Reset",
					 "support@kkrekop.io", _subject, file_out, "answers@kkrekop.io", TRUE);
				FinishedQueueRequest(NULL);
				continue;
			}
			// 24/01/01 kriskoin:
			if (admin_flag) {
				fprintf(out, "Admin: processing began at %s (%d seconds after request was queued)\n\n",
					TimeStr(), SecondCounter - begin_time);
				begin_time = SecondCounter;	// reset for timing actual run
			}
			// 24/01/01 kriskoin:
			int _hh_queue_time = SecondCounter - begin_time;
			#define AVG_HAND_HISTORY_WEIGHTING	0.92	
			AvgHandHistoryQueueTime = (float)(
					(AVG_HAND_HISTORY_WEIGHTING * AvgHandHistoryQueueTime) +
					((1.0 - AVG_HAND_HISTORY_WEIGHTING) * _hh_queue_time)
				);
			// opened the file -- write a header
			if (hhrt == HHRT_INDIVIDUAL_HAND) {
				fprintf(out,"Transcript of game #%d requested by %s (%s) %s\n",
					hand_num, player_rec.full_name, player_rec.user_id,
					(admin_flag ? "(admin mode)" : ""));
			} else if (hhrt == HHRT_LAST_N_ALLIN_HANDS) {
				if (src_player_id==dest_player_id) {
					fprintf(out,"Transcript of last %d All-In games requested by %s (%s)%s\n",
						hand_num, player_rec.full_name, player_rec.user_id,
						(admin_flag ? " (admin mode)" : ""));
				} else {
					fprintf(out,"Transcript of last %d All-In games for %s (%s)%s\n",
						hand_num, player_rec.full_name, player_rec.user_id,
						(admin_flag ? " (admin mode)" : ""));
				}
			} else {
				if (src_player_id==dest_player_id) {
					fprintf(out,"Transcript of last %d games requested by %s (%s)%s\n",
						hand_num, player_rec.full_name, player_rec.user_id,
						(admin_flag ? " (admin mode)" : ""));
				} else {
					fprintf(out,"Transcript of last %d games for %s (%s)%s\n",
						hand_num, player_rec.full_name, player_rec.user_id,
						(admin_flag ? " (admin mode)" : ""));
				}
			}
			fprintf(out,"This email was computer generated and emailed to %s\n", dest_email);
			fprintf(out,"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
			int rc;
			WORD32 found_hand_count = 0;
			if (hhrt == HHRT_INDIVIDUAL_HAND) {	// just one
				rc = BuildHandHistory(hand_num, src_player_id, admin_flag, out, FALSE);
				if (!rc && !admin_flag) {
					//kp(("%s(%d) BuildHandHistory returned %d for game #%d\n", _FL, rc, hand_num));
					found_hand_count = 0;
				} else {
					found_hand_count = 1;
				}
			} else {	// multiple, build from newest to oldest
				for (int i=0; i < (int)hand_num && !_quit_hrht; i++) {
					WORD32 game = 0;
					if (hhrt == HHRT_LAST_N_ALLIN_HANDS) {
						game = player_rec.all_in_game_numbers[i];
					} else {
						game = player_rec.most_recent_games[i];
					}
					if (game) {	// something there?
						rc = BuildHandHistory(game, src_player_id, admin_flag, out, hhrt==HHRT_LAST_N_ALLIN_HANDS);
						if (!rc) {
							pr(("%s(%d) BuildHandHistory returned %d for game #%d [index %d]\n",
								_FL, rc, player_rec.most_recent_games[i], i));
						} else {
							found_hand_count++;
						}
					}
				}
			}
			if (!found_hand_count) {	// nothing useful
				fprintf(out,"We are sorry but we were unable to locate the hand histories you requested.\n");
				fprintf(out,"Hand histories are only kept for a few days and are then archived.\n");
				fprintf(out,"These are not available for retrieval at this time.\n\n");
				fprintf(out,"We are working on having a hand server available soon.\n\n");
				fprintf(out,"Hand histories of games in play are only available when the hand has finished.\n");
				fprintf(out,"===============================================================\n");
			}
			// footer
			fprintf(out,"Should you have any questions regarding this hand history,\nplease contact us at support@kkrekop.io\n");
			// admin timing footer
			if (admin_flag) {
				fprintf(out, "\nAdmin: processing finished at %s (%d seconds after processing began)\n",
					TimeStr(), SecondCounter - begin_time);
			}
			FCLOSE(out);
			if (!_quit_hrht) {	// only send it if we didn't get interrupted.
				// email it to the user...
				char subject[120];
				if (hhrt == HHRT_INDIVIDUAL_HAND) {
					sprintf(subject, "Transcript of hand #%d", hand_num);
				} else if (hhrt == HHRT_LAST_N_ALLIN_HANDS) {
					if (src_player_id==dest_player_id) {
						if (found_hand_count != hand_num) {
							sprintf(subject, "Transcript of last %d All-In hands (%d requested)",
								found_hand_count, hand_num);
						} else {
							sprintf(subject, "Transcript of last %d All-In hands", hand_num);
						}
					} else {
						sprintf(subject, "Transcript of last %d All-In hands for %s",
							found_hand_count, player_rec.user_id);
					}
				} else {
					if (src_player_id==dest_player_id) {
						if (found_hand_count != hand_num) {
							sprintf(subject, "Transcript of last %d hands (%d requested)",
								found_hand_count, hand_num);
						} else {
							sprintf(subject, "Transcript of last %d hands", hand_num);
						}
					} else {
						sprintf(subject, "Transcript of last %d hands for %s",
							found_hand_count, player_rec.user_id);
					}
				}
				if (DebugFilterLevel <= 8) {
					kp(("%s Emailing (%s) to %s\n", TimeStr(), subject, dest_email));
				}
				Email(dest_email, "Desert Poker Hand History",
					 "support@kkrekop.io", subject, file_out, NULL, TRUE);
			}
			// last thing we do is update the queue pointer and remove the temp  file
			FinishedQueueRequest(file_out);
		}
	}
	// thread is exiting; free up anything in the queue
	while (_hh_queue) {
		HandHistoryQueueStruct *tmp = _hh_queue;
		_hh_queue = _hh_queue->next;
		free(tmp);
	}
	_hrht_is_running = FALSE;
}

/**********************************************************************************
 Function GetHandDescription(Hand hPlayer, Hand hCommon);
 date: 24/01/01 kriskoin Purpose: get a description of this hand, if possible
***********************************************************************************/
void PokerLog::GetHandDescription(GameRules game_rules, int p_index, char *high_out, char *low_out)
{
	// blank the output string if we don't build a description
	*high_out = 0;
	*low_out = 0;
	// build the pocket hand
	Hand h_pocket;
	for (int i=0; i < MAX_PRIVATE_CARDS; i++) {
		Card pocket_card = cards[p_index][i];
		if (pocket_card != CARD_NO_CARD && pocket_card != CARD_HIDDEN) {
			h_pocket.Add(pocket_card);
		}
	}
	// build the flop
	Hand h_flop;
	for (int j=0; j < MAX_PUBLIC_CARDS; j++) {
		Card flop_card = common_cards[j];
		if (flop_card != CARD_NO_CARD && flop_card != CARD_HIDDEN) {
			h_flop.Add(flop_card);
		}
	}
	int pocket_count = h_pocket.CardCount();
	int flop_count = h_flop.CardCount();
	// for whatever we're testing, there must be at least 5 cards to work with
	Hand best_hand;
	Hand low_hand;
	int testable_hand = TRUE;
	switch (game_rules) {
	case GAME_RULES_HOLDEM:
		// full pocket and 3+ flop
		if (pocket_count != 2 || flop_count < 3) {
			testable_hand = FALSE;
		}
		break;
	case GAME_RULES_STUD7_HI_LO:
	case GAME_RULES_STUD7:
		// at least 5 cards
		if (pocket_count < 5) {
			testable_hand = FALSE;
		}
		break;
	case GAME_RULES_OMAHA_HI_LO:
	case GAME_RULES_OMAHA_HI:
		// full pocket and 3+ flop
		if (pocket_count !=4 || flop_count < 3) {
			testable_hand = FALSE;
		}
		break;
	default:
		Error(ERR_INTERNAL_ERROR, "%s(%d) Invalid game rules (%d) trying to describe hand",_FL, game_rules);
		return;
	}
	// hand is valid to describe..?
	if (testable_hand) {
		if (_poker) {
			_poker->FindBestHand(game_rules, h_pocket, h_flop, &best_hand, &low_hand);
			_poker->GetHandCompareDescription(&best_hand, NULL, high_out, TRUE);
			int show_low = (game_rules == GAME_RULES_OMAHA_HI_LO || game_rules == GAME_RULES_STUD7_HI_LO);
			if (show_low && _poker->ValidLowHand(&low_hand)) {
				_poker->GetHandCompareDescription(&low_hand, NULL, low_out, FALSE);
			}
		} else {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Couldn't access Poker object",_FL);
		}
	}	
	
}
