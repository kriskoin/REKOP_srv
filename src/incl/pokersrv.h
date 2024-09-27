//*********************************************************
//
//	Poker Server main header file.
//
// 
//
//*********************************************************

#ifndef _POKERSRV_H_INCLUDED
#define _POKERSRV_H_INCLUDED

#ifndef _PPLIB_H_INCLUDED
  #include "pplib.h"
#endif
#include "hand.h"
#include "gamedata.h"

#ifdef _CARDROOM_H_INCLUDED
  extern CardRoom *CardRoomPtr;		// global cardroom ptr
#endif

// adate: as of very soon, we don't need the dealer to send out messages that are spoofed by
// the client.  if we need for those to be sent from the server, change #define below to 1
#define DEALER_REDUNDANT_MESSAGES	0

#define FORCE_WAITING_LIST			0	// testing: ALWAYS use a waiting list, even for empty tables?

enum {
	RUNLEVEL_EXIT,					// Completely exit this program
	RUNLEVEL_SHUTDOWN,				// Nothing running, but program is still running (this is the initial state)
	RUNLEVEL_ACCEPT_CONNECTIONS,	// Accept connections
	RUNLEVEL_TABLES_OPEN,			// Tables are open.  Games cannot yet start
	RUNLEVEL_GAMES_CAN_START,		// New games can start
	RUNLEVEL_ACCEPT_PLAYERS,		// Accept new player logins
};

// Priorities for entering critical sections.  They must ALWAYS be entered in order.
enum  {
	CRITSECPRI_CARDROOM = CRITSECPRI_USER_START,
	CRITSECPRI_INPUT_THREAD,
	CRITSECPRI_PARMFILE,
	CRITSECPRI_TABLELIST,
	CRITSECPRI_WAITLIST,
	CRITSECPRI_TABLE,
	CRITSECPRI_CCDB,
	CRITSECPRI_OUTPUTTHREAD0,
	CRITSECPRI_OUTPUTTHREAD1,
	CRITSECPRI_OUTPUTTHREAD2,
	CRITSECPRI_OUTPUTTHREAD3,
	CRITSECPRI_OUTPUTTHREAD4,
	CRITSECPRI_OUTPUTTHREAD5,
	CRITSECPRI_OUTPUTTHREAD6,
	CRITSECPRI_OUTPUTTHREAD7,
	CRITSECPRI_PLAYER,
	CRITSECPRI_OUTPUTQUEUE,
	CRITSECPRI_TRANSACTIONNUMBER,
	CRITSECPRI_SERNUMDB,
	CRITSECPRI_RNG,
	CRITSECPRI_SDB,
	CRITSECPRI_LOGGING,
};

extern int iRunLevelDesired;	// Desired run level
extern int iRunLevelCurrent;	// Current run level
void SetRunLevelDesired(int new_run_level, char *reason, ...);
extern int iShutdownPlayMoneyGamesFlag;			// start shutting down play money games?
extern int iShutdownAfterGamesCompletedFlag;	// set to shut down after games are completed.
extern int iShutdownAfterECashCompletedFlag;	// set to shut down after ecash is completed
extern int iShutdownIsBriefFlag;				// set if the shutdown is expected to be brief (for msg purposes)
extern int iRunningLiveFlag;		// set if we're the real thing and we're allowed to email people.
extern int iShotClockETA;			// updated by cardroom: eta (in seconds) before shot clock goes off.
extern WORD32 ShotClockExpirySecondCounter;	// SecondCounter when we expect the shot clock to expire (derived from iShotClockETA)
extern int iShotClockChangedFlag;	// set whenever the shot clock is changed
extern int iTournamentTableCreationAllowed;	// set if ManageTables() is allowed to create new tournament tables

// Logfile object pointers
#ifdef LOGFILE_DEFINED
extern LogFile *ChatLog;
extern LogFile *ConnectionLog;
#if 0	//kriskoin: extern LogFile *AlertLog;
extern LogFile *PPErrorLog;
extern LogFile *UsageLog;
extern LogFile *TimeOutLog;
extern LogFile *NewAccountsLog;
extern LogFile *ClientErrLog;
#endif
#endif // LOGFILE_DEFINED

// Enter the cardroom critical section and lock down most of
// the related critical sections as well (such as the player
// list and the tables).
// These functions should not be called directly.  Use the
// macros EnterCriticalSection_CardRoom() and
//		  LeaveCriticalSection_CardRoom().
#define EnterCriticalSection_CardRoom()	EnterCriticalSection_CardRoom0(__FILE__, __LINE__)
#define LeaveCriticalSection_CardRoom()	LeaveCriticalSection_CardRoom0(__FILE__, __LINE__)
void EnterCriticalSection_CardRoom0(char *file, int line);
void LeaveCriticalSection_CardRoom0(char *file, int line);

int TestIfPlayerConnected(WORD32 player_id);	// non-cardroom object access function

// Return our best guess about the quality of the connection to
// a player given just their 32-bit player_id.
// Returns CONNECTION_STATE_* (see player.h)
int GetPlayerConnectionState(WORD32 player_id);

// Count the number of all-in's allowed for an arbitrary player
int AllowedAutoAllInCount(WORD32 player_id);

// Reset the number of available all-ins for a player.
void ResetAllInsForPlayer(WORD32 player_id, int send_email_flag);

// Add a future all-in reset to the queue.
// when is in SecondCounter units.
void AddFutureAllInResetToQueue(WORD32 when, WORD32 player_id);

// Update the all in reset queue... reset players if necessary.
// This function should be called periodically (every 10s or so).
void UpdateAllInResetQueue(void);

// Set a 'work_was_done_flag'.  Print details if details
// are turned on (iPrintWorkWasDoneFlagDetails > 0)
void SetWorkWasDoneFlag(int *work_was_done_flag, char *format_str, ...);
extern int iPrintWorkWasDoneFlagDetails;	// set to n > 0 to print the details n times.

extern WORD32 dwLastWidespreadRoutingProblem;	// SecondCounter of last widespread routing delay (if any)
extern WORD32 dwEndWidespreadRoutingProblem;	// SecondCounter of end of last widespread routing delay (if any)
extern int iRoutingProblemPlayerCount;			// # of players online when routing problem was detected
extern int iRoutingProblemPoorConnections;		// # of recent poor connections when routing problem was detected
extern int iRoutingProblemPeakPoorConnections;	// peak poor connections during a routing problem
extern int iRoutingProblemGoodConnections;		// good connections (only updated every 15s) during routing problem

extern float AvgHandHistoryQueueTime;

extern struct VersionInfo ServerVersionInfo;

extern int ReReadParmFileFlag;	// set if we should re-read the .INI file (usually from SIGHUP)
void ReadOurParmFile(void);
#if defined(_WINDOWS_) || !defined(WIN32)
  extern PPCRITICAL_SECTION ParmFileCritSec;	// CritSec to control access to Parm file vars
#endif
extern WORD32 NextAnonymousPlayerID;			// our global allocator of anon player IDs

// Add a computer serial number to the "block" list.
void AddComputerBlock(WORD32 computer_serial_num);

// Remove a computer serial number to the "block" list.
void RemoveComputerBlock(WORD32 computer_serial_num);

// Check if a computer has been blocked.  Return TRUE if it has.
int CheckComputerBlock(WORD32 computer_serial_num);

// Return a unique computer serial number (next one from the pool)
WORD32 GetNextComputerSerialNum(void);

#define COMPUTER_PLAYER_ID	((WORD32)-1)	// player_id used for computer players

// Persistent server variables (written to disk)
struct PersistentServerVars {
	WORD32 file_serial_num;				// sn of file... incremented each time file written.
	WORD32 next_computer_serial_num;	// next sn we'll hand out to clients
	WORD32 bad_beat_count;				// number of bad beats today
	WORD32 bad_beat_payout;				// today's bad beat payout total
	WORD32 gross_bets;					// gross real money bets (this WILL wrap around regularly!)
	WORD32 gross_tournament_buyins;		// gross tournament buyins (+fee) (this WILL wrap around regularly)
	// 224/01/01 kriskoin:
	WORD32 highest_transact_num_seen;	// highest transaction number we've ever seen
	WORD32 lowest_verified_transact_num;// lowest transaction number we've verified as having gone through
	//** 20001204MB note: when adding things, see YesterdaySavedStats in
	// cardroom.cpp to see if that's a more appropriate place for your new variable.
};
extern struct PersistentServerVars ServerVars;

// Send an admin alert to admin clients and log to alerts.log
void SendAdminAlert(enum ChatTextType alert_level, char *alert_msg, ...);

// Build a string of user id's from an array of player ID's
// (for display).  String should be at least count*20 long.
void BuildUserIDString(WORD32 *player_id_array, int count, char *output_str);
// Retrieve a single user id string
void BuildUserIDString(WORD32 player_id, char *output_str);

#define SERNUMDB_PLAYER_IDS_TO_RECORD	5
struct SerNumDB_Entry {
	WORD32 computer_serial_num;	// s/n for this entry (also array search key)
	WORD32 player_ids[SERNUMDB_PLAYER_IDS_TO_RECORD];
	WORD32 unused[2];
};
#if CRITICAL_SECTIONS_DEFINED
extern PPCRITICAL_SECTION SerNumDBCritSec;	// CritSec to control access to Parm file vars
#endif

// Return a pointer to the entry for a particular computer
// serial number.  Add an entry if necessary.
// Returns ptr to entry or NULL if there was a problem.
struct SerNumDB_Entry *SerNumDB_GetSerNumEntry(WORD32 computer_serial_num);

// Add a player ID to the computer serial number database.
void SerNumDB_AddPlayerID(WORD32 computer_serial_num, WORD32 player_id);

// Review a computer key database entry and issue dup account
// alerts if necessary.
void SerNumDB_CheckForDupes(WORD32 computer_serial_num, int add_admin_notes_flag, int send_alerts_flag);

// Build a string of user id's associated with a computer serial num
// (for display).  String should be at least 300 long.
void SerNumDB_BuildUserIDString(WORD32 computer_serial_num, char *str);

// Insert up to one blank player id for a serial number.
// This allows them to create new accounts.
void SerNumDB_InsertBlankPlayerID(WORD32 computer_serial_num);

//************* Random Number Generator (thread safe) ***********

// Do our best to initialize the seed for our random number
// generator.  This function should only be called ONCE.
void RNG_InitializeSeed(void);

// Add to the entropy pool for our seed.  Pass any somewhat
// unpredictable 32-bit value.  The more the merrier.
// A Hardware RNG can inject its data here.
void RNG_AddToEntropy(WORD32 any_number);

// Use the Pentium's RDTSC instruction to read the CPU's
// time stamp counter (which counts clock cycles) and add it
// to the entropy pool.  On a 366MHz machine, this thing
// wraps around every 11 seconds, so there's a fair amount
// of unpredictability to it.  It's not perfect, but it's
// fairly good.
void RNG_AddToEntropyUsingRDTSC(void);

// Return the next random number from our sequence.  Note that
// if you know the seed, this number will be predictable.  That's
// why the entropy should be added to as often as possible, possibly
// even in between subsequent calls to RNG_NextNumber().
WORD32 RNG_NextNumber(void);

//************* end Random Number Generator (thread safe) ***********

//************* Player data output (compression&encryption) stuff ***********

// Launch the compression and encryption thread(s).
void PlrOut_LaunchThreads(void);

// Add a packet to the player data output queue for a particular player.
// *pkt gets cleared if the packet was successfully queued.
// This function is usually called directly by the player object
// and the player object's critical section is probably already owned
// by this thread.
void PlrOut_QueuePacket(class Player *plr, class Packet **pkt);

// Remove any packets which have been queued for a particular
// player object (probably because it's about to be deleted
// by the cardroom).
void PlrOut_RemovePacketsForPlayer(class Player *plr);

#define NUMBER_OF_PLAYER_OUTPUT_THREADS		2	// typically one per CPU is best.
extern volatile int iPlrOutQueueLens[NUMBER_OF_PLAYER_OUTPUT_THREADS];	// current depth of the player data output queue

//************* end Player data output (compression&encryption) stuff ***********


//*********** .INI file stuff ***********
extern int    PortNumber;				// TCP/IP port number we listen on
extern int    PortNumber_SSL;			// TCP/IP port number we listen on for SSL/TLS connections
extern int    InitialConnectionTimeout;	// # of seconds after connect() before we give up on a socket.
extern int    CompressionLevel;			// desired zlib compression level for outgoing packets (0-9)
extern int    DebugFilterLevel;			// amount of debug output (to debwin) to filter out
extern int    ArchiveDirCount;			// # of archive directories to look for .hal files in (logs/archive[0-n])
extern int    DatabaseRecordCount;		// max # of records the database will be initialized to handle
extern WORD32 HandHistorySleepInterval;	// # of ms between Sleep() calls for hh thread
extern WORD32 HandHistorySleepTime;		// # of ms to sleep when the interval is reached
extern WORD32 MainLoopIdleSleep;		// # of ms for main loop to Sleep() when idle
extern WORD32 MainLoopActiveSleep;		// # of ms for main loop to Sleep() when it just did work (0=call sched_yield() instead)

extern int    MaxAcceptedConnections;	// max # of accepted connections.  Beyond this, we don't accept ANY new connections
extern int    MaxLoggedInConnections;	// max # of logged in connections (must be less than MaxAcceptedConnections).
extern int    MaxRealTableCount;		// max # of real money tables to create (there should ALWAYS be a limit)
extern int    MaxPlayTableCount;		// max # of play money tables to create (there should ALWAYS be a limit)
extern int    MaxTournamentTableCount;	// max # of tournament tables to create (there should ALWAYS be a limit)

extern WORD32 DelayBeforeDeletingTables;// # of secs after last game before deleting a table
extern int    TimeBetweenGames;			// Leave n seconds between games
extern int    TimeBetweenHiLoGames;		// Leave n seconds between hi/lo games
extern int    TimeBetweenHiLoGames_HiOnly;// Leave n seconds between Hi/Lo games when only a high wins

extern WORD32 FirstInputWarningSeconds;	// # of secs before warning a player to respond to input request soon
extern WORD32 SecondInputWarningSeconds;// # of secs before 2nd warning
extern WORD32 InputTimeoutSeconds;		// # of secs before timing out a player input request.

extern WORD32 TimeoutShowOnlyHand;		// wait max n seconds when everyone folds to you (one player left)
extern WORD32 TimeoutShowFirstHand;		// wait max n seconds for first show (when more than one player left)
extern WORD32 TimeoutShowOtherHands;	// wait max n seconds for additional shows (after first)
extern WORD32 TimeoutMuckHand;			// wait max n seconds for manual show/muck (auto-muck turned off)
extern WORD32 TimeoutAutoMuckHand;		// wait max n seconds for auto-muck
extern WORD32 AutoShowdownSpacing;		// when automatic showdowns are enabled, put n secs between players

extern WORD32 ComputerPlayerAnswerDelay;// # of secs the computer should take before making a move.
extern int    ComputerPlayersPerTable;			// # of computer players at each table
extern int    ComputerPlayersPerHoldemTable;	// # of computer players at each Holdem table (testing only)
extern int    ComputerPlayersPerOmahaTable;		// # of computer players at each Omaha table (testing only)
extern int    ComputerPlayersPerSevenCSTable;	// # of computer players at each Seven card stud table (testing only)
extern int    ComputerPlayersPerOneOnOneTable;	// # of computer players at each One on One Holdem table (testing only)

extern WORD32 WaitListTotalTimeout;		// # of secs from first empty seat notice to timing out.
extern WORD32 WaitListReminderInterval;	// # of secs between reminder notices while seat is open
extern WORD32 WatchingTableTimeout;		// # of secs after disconnect before player is kicked from watching tables.
extern WORD32 PlayingTableTimeout;		// # of secs after disconnect before player is kicked from playing at a table
extern WORD32 WaitListDisconnectTimeout;// # of secs after disconnect before player is kicked from waiting lists

extern WORD32 ShotClockDate;			// time_t when shot clock "goes off"
extern WORD32 ShotClockFlags;			// SCUF_* (from gamedata.h)
extern char   ShotClockMessage1[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
extern char   ShotClockMessage2[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
extern char   ShotClockMessage1_Expired[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
extern char   ShotClockMessage2_Expired[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock

extern int    MaxRake;					// max $ for rake (all table and game types)
extern int    RealMoneyTablesOpen;		// 0=real money tables closed, 1=open
extern int    ECashDisabled;			// set if all ecash functions should be disabled.
extern int    CashoutsDisabled;			// set if cashouts are currently disabled (usually the cashier is still open)
#define ECASH_PROTOCOL_VER_STR_LEN	6
extern char   ECashProtocolVersionStr[ECASH_PROTOCOL_VER_STR_LEN];	// "1.01", "1.03", etc.
extern int    ECashThreads;				// # of ecash threads to launch (range is 1 to MAX_ECASH_THREADS (20))
extern int    ECashHoursBeforeCashout;	// min # of hours required after a purchase before a cashout is allowed

extern int    AllInsAllowed;			// # of all-ins allowed per 24 hour period.
extern int    GoodAllInsAllowed;		// # of good all-ins allowed for automatic reset
extern WORD32 AllInsResetTime;			// time when global all-ins should be considered 'reset'
extern int	  MaxAccountsPerComputer;	// maximum # of accounts a user is allowed to create per computer serial #
extern int    PromotionalGameNumberMultiple; // 10,000 or 100,000, etc. for making sure game is real money with 4 or more players.
extern int    PromotionalGameNumberMinPlayers; // min # of players allowed to play one of the important game numbers

#define GAME_DISABLED_MESSAGE_LEN	80
#define GDBITNUM_CLOSE_TOURNAMENTS	(1)	// this bit is hardcoded and shared with SCUF_CLOSE_TOURNAMENTS code
#define GDB_CLOSE_TOURNAMENTS	(0x01<<GDBITNUM_CLOSE_TOURNAMENTS)	// this bit is hardcoded and shared with SCUF_CLOSE_TOURNAMENTS code

extern char   GameDisabledMessage[GAME_DISABLED_MESSAGE_LEN];	// message displayed when a game is disabled
extern int    GameDisableBits;			// bits to match up with table bits in Cardroom::ManageTables().  Games don't play if matching bits set.
extern int    GameCloseBits;			// these bits will actually keep a table closed (won't be visible)
// Allow restricting the total number of tables for a particular game type
// by using the game disable bits as an indicator of which game types have
// maximums we need to impose.  If set to zero, there is no special limit.
extern int    MaxTables_GDB[8];			// one entry for each of 8 bits

extern int    CCLimit1Amount;			// limit (in $'s) for first n days
extern int    CCLimit1Amount2;			// higher limit (in $'s) for first n days
extern int    CCLimit1Days;				// # of days this limit applies for
extern int    CCLimit2Amount;			// limit (in $'s) for first n days
extern int    CCLimit2Amount2;			// hugher limit (in $'s) for first n days
extern int    CCLimit2Days;				// # of days this limit applies for
extern int    CCLimit3Amount;			// limit (in $'s) for first n days
extern int    CCLimit3Amount2;			// higher limit (in $'s) for first n days
extern int    CCLimit3Days;				// # of days this limit applies for
extern int    CCPurchaseMinimum;		// lower limit for purchases
extern int	  CCMaxAllowableUsers;		// maximum number of users allowed per credit card
extern float  CCFeeRate;				// fee rate to apply to cc purchases
// kriskoin : Deposit bonus and cashout control function
extern int    DepositBonusStartDate;
extern int    DepositBonusEndDate;
extern int    DepositBonusRate;
extern int    DepositBonusCashoutPoints;
extern int    DepositBonusMax;
// end kriskoin 

#define CCCHARGE_NAME_LEN	80
extern char   CCChargeName[CCCHARGE_NAME_LEN];	// what shows up on client statements? (e.g. "SFC_POKERCOSMO BRIDGETOWN")

// bad beats
#define BAD_BEAT_HAND_STRING	50
extern Hand Hand_BadBeat_holdem;
extern Hand Hand_BadBeat_omaha;
extern Hand Hand_BadBeat_stud;
extern char BadBeatDefinition_holdem[BAD_BEAT_HAND_STRING];
extern char BadBeatDefinition_omaha[BAD_BEAT_HAND_STRING];
extern char BadBeatDefinition_stud[BAD_BEAT_HAND_STRING];
extern int BadBeatJackpotActive_holdem;
extern int BadBeatJackpotActive_omaha;
extern int BadBeatJackpotActive_stud;
extern int BadBeatJackpot_holdem;		// in pennies, what we distribute for a Bad Beat
extern int BadBeatJackpot_omaha;		// in pennies, what we distribute for a Bad Beat
extern int BadBeatJackpot_stud;			// in pennies, what we distribute for a Bad Beat
extern int BadBeatHandWinnerCut;		// percentage the winner of the hand gets
extern int BadBeatHandLoserCut;			// what percentage the loser of the hand gets
extern int BadBeatParticipantCut;		// what percentage other players at the table get

// Special Hand Prize variables
extern WORD32 SpecialHandNumber;		// the hand number where we're awarding a prize
extern WORD32 SpecialHandPrize;			// in pennies, the amount each participating player gets
extern int SpecialHandWinnerBonus;		// in pennies, the bonus split among hand winners

//*********** End .INI file stuff ***********

// kriskoin  2002/04/08 hihand structures
struct HighHand {
        time_t  timestamp;
        INT32   game_serial_num;
        char    user_id[MAX_PLAYER_USERID_LEN];
        Hand    hand_rec;
};
// end kriskoin 

#endif // _POKERSRV_H_INCLUDE
