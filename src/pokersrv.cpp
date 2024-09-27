//*********************************************************
//
//	Poker Server main()
//
// 
//
//*********************************************************
 
#define DISP 0

#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>	// Needed for CritSec stuff
  #include <process.h>	// needed for _beginthread() stuff.
#else
  #include <fcntl.h>  // kriskoin 
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <signal.h>
//#include <sigcontext.h>
  #include <malloc.h>
  #include <linux/limits.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include "hand.h"
#include "pokersrv.h"
#include "cardroom.h"
#include "player.h"
#include "sdb.h"
#include "logging.h"
#include "ecash.h"
#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>
  #include <openssl/crypto.h>
  #include <openssl/err.h>
#endif

int iRunLevelDesired;	// Desired run level
int iRunLevelCurrent;	// Current run level
int iRunningLiveFlag = 1;	// set if we're the real thing and we're allowed to email people.

int iShutdownPlayMoneyGamesFlag;		// start shutting down play money games?
int iShutdownAfterGamesCompletedFlag;	// set to shut down after games are completed.
int iShutdownAfterECashCompletedFlag;	// set to shut down after ecash is completed
int iShutdownIsBriefFlag;				// set if the shutdown is expected to be brief (for msg purposes)
int ReReadParmFileFlag;		// set if we should re-read the .INI file (usually from SIGHUP)
int iShotClockETA;			// updated by cardroom: eta (in seconds) before shot clock goes off.
WORD32 ShotClockExpirySecondCounter;	// SecondCounter when we expect the shot clock to expire (derived from iShotClockETA)
int iShotClockChangedFlag;	// set whenever the shot clock is changed
int iTournamentTableCreationAllowed;	// set if ManageTables() is allowed to create new tournament tables

PPCRITICAL_SECTION ParmFileCritSec;	// CritSec to control access to Parm file vars
WORD32 NextAnonymousPlayerID;		// our global allocator of anon player IDs
SimpleDataBase *SDB;		// global database access point for entire server
PokerLog *PL;				// global access point for logging object
CardRoom *CardRoomPtr;		// global cardroom ptr
WORD32 dwLastWidespreadRoutingProblem;	// SecondCounter of last widespread routing delay (if any)
WORD32 dwEndWidespreadRoutingProblem;	// SecondCounter of end of last widespread routing delay (if any)
int iRoutingProblemPlayerCount;			// # of players online when routing problem was detected
int iRoutingProblemPoorConnections;		// # of recent poor connections when routing problem was detected
int iRoutingProblemPeakPoorConnections;	// peak poor connections during a routing problem
int iRoutingProblemGoodConnections;		// good connections (only updated every 15s) during routing problem

#if INCL_SSL_SUPPORT
SSL_CTX *MainSSL_Client_CTX;
SSL_CTX *MainSSL_Server_CTX;
#endif

struct VersionInfo ServerVersionInfo;
char   AlternateServerIP[MAX_VERSION_URL_LEN];	// ip address of real server (as a string)

struct PersistentServerVars ServerVars;

// Logfile object pointers
#ifdef LOGFILE_DEFINED
LogFile *ChatLog;
LogFile *ConnectionLog;
#if 0	//kriskoin: LogFile *AlertLog;
LogFile *PPErrorLog;
LogFile *UsageLog;
LogFile *TimeOutLog;
LogFile *NewAccountsLog;
LogFile *ClientErrLog;
#endif
#endif // LOGFILE_DEFINED

static int iAddDupeAccountNotes;	// rarely used command line parameter
static int iReadEcashLog;	// rarely used command line parameter
void SerNumDB_Load(void);
void SerNumDB_AddDupeAccountNotes(void);
void OurCriticalAlertHandler(char *critical_alert_msg);

// Bad beat variables
// Hold'em
char  BadBeatDefinition_holdem[BAD_BEAT_HAND_STRING];	// string that we read in to get our bad-beat minimum
Hand Hand_BadBeat_holdem;		// where we store our BadBeat minimum standard for Hold'em
int BadBeatJackpotActive_holdem;// enabled/disabled state of bad beat jackpots for Hold'em
int BadBeatJackpot_holdem;		// in pennies, what we distribute for a Bad Beat for Hold'em
// Omaha
char  BadBeatDefinition_omaha[BAD_BEAT_HAND_STRING];	// string that we read in to get our bad-beat minimum
Hand Hand_BadBeat_omaha;		// where we store our BadBeat minimum standard for Omaha
int BadBeatJackpotActive_omaha;	// enabled/disabled state of bad beat jackpots  for Omaha
int BadBeatJackpot_omaha;		// in pennies, what we distribute for a Bad Beat for Omaha
// Stud
char  BadBeatDefinition_stud[BAD_BEAT_HAND_STRING];	// string that we read in to get our bad-beat minimum
Hand Hand_BadBeat_stud;			// where we store our BadBeat minimum standard for Stud
int BadBeatJackpotActive_stud;	// enabled/disabled state of bad beat jackpots for Stud
int BadBeatJackpot_stud;		// in pennies, what we distribute for a Bad Beat for Stud
// All Bad Beats
int BadBeatHandWinnerCut;		// percentage the winner of the hand gets
int BadBeatHandLoserCut;		// what percentage the loser of the hand gets
int BadBeatParticipantCut;		// what percentage other players at the table get

// Special Hand Prize variables
WORD32 SpecialHandNumber;		// the hand number where we're awarding a prize
WORD32 SpecialHandPrize;		// in pennies, the amount each participating player gets
int SpecialHandWinnerBonus;		// in pennies, the bonus split among hand winners

// Average Hand History Queue Time
float AvgHandHistoryQueueTime = 0.0;

// .INI file entries
ParmStruc INIParms[] = {
	// type				size	token name					address
	PARMTYPE_INT,		0,		"PortNumber",				&PortNumber,
	PARMTYPE_INT,		0,		"PortNumber_SSL",			&PortNumber_SSL,
	PARMTYPE_INT,		0,		"InitialConnectionTimeout",	&InitialConnectionTimeout,
	PARMTYPE_INT,		0,		"CompressionLevel",			&CompressionLevel,
	PARMTYPE_INT,		0,		"DebugFilterLevel",			&DebugFilterLevel,
	PARMTYPE_INT,		0,		"ArchiveDirCount",			&ArchiveDirCount,
	PARMTYPE_INT,		0,		"DatabaseRecordCount",		&DatabaseRecordCount,
	PARMTYPE_INT,		0,		"HandHistorySleepInterval",	&HandHistorySleepInterval,
	PARMTYPE_INT,		0,		"HandHistorySleepTime",		&HandHistorySleepTime,
	PARMTYPE_INT,		0,		"MainLoopIdleSleep",		&MainLoopIdleSleep,
	PARMTYPE_INT,		0,		"MainLoopActiveSleep",		&MainLoopActiveSleep,

	PARMTYPE_INT,		0,		"TimeBetweenGames",			&TimeBetweenGames,
	PARMTYPE_INT,		0,		"TimeBetweenHiLoGames",		&TimeBetweenHiLoGames,
	PARMTYPE_INT,		0,		"TimeBetweenHiLoGames_HiOnly",&TimeBetweenHiLoGames_HiOnly,

	PARMTYPE_INT,		0,		"DelayBeforeDeletingTables",&DelayBeforeDeletingTables,
	PARMTYPE_INT,		0,		"MaxRealTableCount",		&MaxRealTableCount,
	PARMTYPE_INT,		0,		"MaxPlayTableCount",		&MaxPlayTableCount,
	PARMTYPE_INT,		0,		"MaxTournamentTableCount",	&MaxTournamentTableCount,
	PARMTYPE_INT,		0,		"MaxAcceptedConnections",	&MaxAcceptedConnections,
	PARMTYPE_INT,		0,		"MaxLoggedInConnections",	&MaxLoggedInConnections,

	PARMTYPE_INT,		0,		"FirstInputWarningSeconds",	&FirstInputWarningSeconds,
	PARMTYPE_INT,		0,		"SecondInputWarningSeconds",&SecondInputWarningSeconds,
	PARMTYPE_INT,		0,		"InputTimeoutSeconds",		&InputTimeoutSeconds,

	PARMTYPE_INT,		0,		"TimeoutShowOnlyHand",		&TimeoutShowOnlyHand,
	PARMTYPE_INT,		0,		"TimeoutShowFirstHand",		&TimeoutShowFirstHand,
	PARMTYPE_INT,		0,		"TimeoutShowOtherHands",	&TimeoutShowOtherHands,
	PARMTYPE_INT,		0,		"TimeoutMuckHand",			&TimeoutMuckHand,
	PARMTYPE_INT,		0,		"TimeoutAutoMuckHand",		&TimeoutAutoMuckHand,
	PARMTYPE_INT,		0,		"AutoShowdownSpacing",		&AutoShowdownSpacing,

	PARMTYPE_INT,		0,		"ComputerPlayerAnswerDelay",		&ComputerPlayerAnswerDelay,
	PARMTYPE_INT,		0,		"ComputerPlayersPerTable",			&ComputerPlayersPerTable,
	PARMTYPE_INT,		0,		"ComputerPlayersPerHoldemTable",	&ComputerPlayersPerHoldemTable,
	PARMTYPE_INT,		0,		"ComputerPlayersPerOmahaTable",		&ComputerPlayersPerOmahaTable,
	PARMTYPE_INT,		0,		"ComputerPlayersPerSevenCSTable",	&ComputerPlayersPerSevenCSTable,
	PARMTYPE_INT,		0,		"ComputerPlayersPerOneOnOneTable",	&ComputerPlayersPerOneOnOneTable,

	PARMTYPE_INT,		0,		"WaitListTotalTimeout",		&WaitListTotalTimeout,
	PARMTYPE_INT,		0,		"WaitListReminderInterval",	&WaitListReminderInterval,
	PARMTYPE_INT,		0,		"WatchingTableTimeout",		&WatchingTableTimeout,
	PARMTYPE_INT,		0,		"PlayingTableTimeout",		&PlayingTableTimeout,
	PARMTYPE_INT,		0,		"WaitListDisconnectTimeout",&WaitListDisconnectTimeout,

	PARMTYPE_DATE,		0,		"ShotClockDate",			&ShotClockDate,
	PARMTYPE_INT,		0,		"ShotClockFlags",			&ShotClockFlags,

	PARMTYPE_INT,		0,		"SpecialHandNumber",		&SpecialHandNumber,
	PARMTYPE_INT,		0,		"SpecialHandPrize",			&SpecialHandPrize,
	PARMTYPE_INT,		0,		"SpecialHandWinnerBonus",	&SpecialHandWinnerBonus,

	PARMTYPE_INT,		0,		"BadBeatJackpotActive_holdem",	&BadBeatJackpotActive_holdem,
	PARMTYPE_INT,		0,		"BadBeatJackpot_holdem",		&BadBeatJackpot_holdem,
	PARMTYPE_INT,		0,		"BadBeatJackpotActive_omaha",	&BadBeatJackpotActive_omaha,
	PARMTYPE_INT,		0,		"BadBeatJackpot_omaha",			&BadBeatJackpot_omaha,
	PARMTYPE_INT,		0,		"BadBeatJackpotActive_stud",	&BadBeatJackpotActive_stud,
	PARMTYPE_INT,		0,		"BadBeatJackpot_stud",			&BadBeatJackpot_stud,
	PARMTYPE_INT,		0,		"BadBeatHandWinnerCut",		&BadBeatHandWinnerCut,
	PARMTYPE_INT,		0,		"BadBeatHandLoserCut",		&BadBeatHandLoserCut,
	PARMTYPE_INT,		0,		"BadBeatParticipantCut",	&BadBeatParticipantCut,
	PARMTYPE_STRING,	BAD_BEAT_HAND_STRING,	"BadBeatHand_holdem",	BadBeatDefinition_holdem,
	PARMTYPE_STRING,	BAD_BEAT_HAND_STRING,	"BadBeatHand_omaha",	BadBeatDefinition_omaha,
	PARMTYPE_STRING,	BAD_BEAT_HAND_STRING,	"BadBeatHand_stud",		BadBeatDefinition_stud,

	PARMTYPE_STRING,	SHOTCLOCK_MESSAGE_LEN,	"ShotClockMessage1",	ShotClockMessage1,
	PARMTYPE_STRING,	SHOTCLOCK_MESSAGE_LEN,	"ShotClockMessage2",	ShotClockMessage2,
	PARMTYPE_STRING,	SHOTCLOCK_MESSAGE_LEN,	"ShotClockMessage1_Expired",	ShotClockMessage1_Expired,
	PARMTYPE_STRING,	SHOTCLOCK_MESSAGE_LEN,	"ShotClockMessage2_Expired",	ShotClockMessage2_Expired,

	PARMTYPE_INT,		0,		"MaxRake",					&MaxRake,
	PARMTYPE_INT,		0,		"RealMoneyTablesOpen",		&RealMoneyTablesOpen,
	PARMTYPE_INT,		0,		"ECashDisabled",			&ECashDisabled,
	PARMTYPE_INT,		0,		"CashoutsDisabled",			&CashoutsDisabled,
	PARMTYPE_STRING,	ECASH_PROTOCOL_VER_STR_LEN, "ECashProtocolVersionStr", ECashProtocolVersionStr,
	PARMTYPE_INT,		0,		"ECashThreads",				&ECashThreads,
	PARMTYPE_INT,		0,		"ECashHoursBeforeCashout",	&ECashHoursBeforeCashout,

	PARMTYPE_INT,		0,		"AllInsAllowed",			&AllInsAllowed,
	PARMTYPE_INT,		0,		"GoodAllInsAllowed",		&GoodAllInsAllowed,
	PARMTYPE_DATE,		0,		"AllInsResetTime",			&AllInsResetTime,
	PARMTYPE_INT,		0,		"MaxAccountsPerComputer",	&MaxAccountsPerComputer,
	PARMTYPE_INT,		0,		"PromotionalGameNumberMultiple",&PromotionalGameNumberMultiple,
	PARMTYPE_INT,		0,		"PromotionalGameNumberMinPlayers",&PromotionalGameNumberMinPlayers,

	PARMTYPE_STRING,	GAME_DISABLED_MESSAGE_LEN,	"GameDisabledMessage",	GameDisabledMessage,
	PARMTYPE_INT,		0,		"GameDisableBits",			&GameDisableBits,
	PARMTYPE_INT,		0,		"GameCloseBits",			&GameCloseBits,
	PARMTYPE_INT,		0,		"MaxTables_GDB0",			&MaxTables_GDB[0],
	PARMTYPE_INT,		0,		"MaxTables_GDB1",			&MaxTables_GDB[1],
	PARMTYPE_INT,		0,		"MaxTables_GDB2",			&MaxTables_GDB[2],
	PARMTYPE_INT,		0,		"MaxTables_GDB3",			&MaxTables_GDB[3],
	PARMTYPE_INT,		0,		"MaxTables_GDB4",			&MaxTables_GDB[4],
	PARMTYPE_INT,		0,		"MaxTables_GDB5",			&MaxTables_GDB[5],
	PARMTYPE_INT,		0,		"MaxTables_GDB6",			&MaxTables_GDB[6],
	PARMTYPE_INT,		0,		"MaxTables_GDB7",			&MaxTables_GDB[7],

	PARMTYPE_INT,		0,		"CCLimit1Amount",			&CCLimit1Amount,
	PARMTYPE_INT,		0,		"CCLimit1Amount2",			&CCLimit1Amount2,
	PARMTYPE_INT,		0,		"CCLimit1Days",				&CCLimit1Days,
	PARMTYPE_INT,		0,		"CCLimit2Amount",			&CCLimit2Amount,
	PARMTYPE_INT,		0,		"CCLimit2Amount2",			&CCLimit2Amount2,
	PARMTYPE_INT,		0,		"CCLimit2Days",				&CCLimit2Days,
	PARMTYPE_INT,		0,		"CCLimit3Amount",			&CCLimit3Amount,
	PARMTYPE_INT,		0,		"CCLimit3Amount2",			&CCLimit3Amount2,
	PARMTYPE_INT,		0,		"CCLimit3Days",				&CCLimit3Days,
	PARMTYPE_INT,		0,		"CCPurchaseMinimum",		&CCPurchaseMinimum,
	PARMTYPE_INT,		0,		"CCMaxAllowableUsers",		&CCMaxAllowableUsers,
	PARMTYPE_FLOAT,		0,		"CCFeeRate"			,		&CCFeeRate,
	PARMTYPE_STRING,	CCCHARGE_NAME_LEN, "CCChargeName",	CCChargeName,

	PARMTYPE_BYTE,		0,		"ServerVersion_major",		&ServerVersionInfo.server_version.major,
	PARMTYPE_BYTE,		0,		"ServerVersion_minor",		&ServerVersionInfo.server_version.minor,
	PARMTYPE_WORD,		0,		"ServerVersion_build",		&ServerVersionInfo.server_version.build,
	PARMTYPE_WORD,		0,		"ServerVersion_flags",		&ServerVersionInfo.server_version.flags,
	PARMTYPE_STRING,	MAX_VERSION_STRING_LEN,	"ServerVersion_string",	ServerVersionInfo.server_version_string,

	PARMTYPE_BYTE,		0,		"MinClientVersion_major",	&ServerVersionInfo.min_client_version.major,
	PARMTYPE_BYTE,		0,		"MinClientVersion_minor",	&ServerVersionInfo.min_client_version.minor,
	PARMTYPE_WORD,		0,		"MinClientVersion_build",	&ServerVersionInfo.min_client_version.build,
	PARMTYPE_WORD,		0,		"MinClientVersion_flags",	&ServerVersionInfo.min_client_version.flags,

	PARMTYPE_BYTE,		0,		"NewClientVersion_major",	&ServerVersionInfo.new_client_version.major,
	PARMTYPE_BYTE,		0,		"NewClientVersion_minor",	&ServerVersionInfo.new_client_version.minor,
	PARMTYPE_WORD,		0,		"NewClientVersion_build",	&ServerVersionInfo.new_client_version.build,
	PARMTYPE_WORD,		0,		"NewClientVersion_flags",	&ServerVersionInfo.new_client_version.flags,
	PARMTYPE_STRING,	MAX_VERSION_STRING_LEN,	"NewClientVersionString",	ServerVersionInfo.new_version_string,
	PARMTYPE_STRING,	MAX_VERSION_URL_LEN,	"NewClientVersionUserURL",	ServerVersionInfo.new_ver_user_url,
	PARMTYPE_STRING,	MAX_VERSION_URL_LEN,	"NewClientVersionAutoURL",	ServerVersionInfo.new_ver_auto_url,
	PARMTYPE_DATE,		0,		"NewClientVersionReleaseDate",	&ServerVersionInfo.new_version_release_date,

	PARMTYPE_STRING,	MAX_VERSION_URL_LEN,	"AlternateServerIP",	AlternateServerIP,
// kriskoin : Deposit bonus and cashout control function
        PARMTYPE_DATE,          0,      "DepositBonusStartDate",        &DepositBonusStartDate,
        PARMTYPE_DATE,          0,      "DepositBonusEndDate",          &DepositBonusEndDate,
        PARMTYPE_INT,           0,      "DepositBonusRate",             &DepositBonusRate,
        PARMTYPE_INT,           0,      "DepositBonusCashoutPoints",    &DepositBonusCashoutPoints,
        PARMTYPE_INT,           0,      "DepositBonusMax",              &DepositBonusMax,
// end kriskoin 

	PARMTYPE_INT,0,0,0		// mark end.
};
// Modifiec by Allen 9/20/2001
//int    PortNumber=26004;			// TCP/IP port number we listen on
//int    PortNumber_SSL=26005;		// TCP/IP port number we listen on for SSL/TLS connections

//int    PortNumber=50000;			// TCP/IP port number we listen on
//int    PortNumber_SSL=50003;		// TCP/IP port number we listen on for SSL/TLS connections

int	PortNumber=38500;		// TCP/IP port number we listen on
int	PortNumber_SSL=38502;		// TCP/IP port number we listen on for SSL/TLS connections

int    InitialConnectionTimeout=30;	// # of seconds after connect() before we give up on a socket.
int    CompressionLevel=6;			// desired zlib compression level for outgoing packets (0-9)
int    DebugFilterLevel=0;			// amount of debug output (to debwin) to filter out
int    ArchiveDirCount=0;			// # of archive directories to look for .hal files in (logs/archive[1-n])
int    DatabaseRecordCount=0;		// max # of records the database will be initialized to handle
WORD32 HandHistorySleepInterval;	// # of ms between Sleep() calls for hh thread
WORD32 HandHistorySleepTime;		// # of ms to sleep when the interval is reached
WORD32 MainLoopIdleSleep;			// # of ms for main loop to Sleep() when idle
WORD32 MainLoopActiveSleep;			// # of ms for main loop to Sleep() when it just did work (0=call sched_yield() instead)

int    MaxAcceptedConnections;		// max # of accepted connections.  Beyond this, we don't accept ANY new connections
int    MaxLoggedInConnections;		// max # of logged in connections (must be less than MaxAcceptedConnections).
int    MaxRealTableCount;			// max # of real money tables to create (there should ALWAYS be a limit)
int    MaxPlayTableCount;			// max # of play money tables to create (there should ALWAYS be a limit)
int    MaxTournamentTableCount;		// max # of tournament tables to create (there should ALWAYS be a limit)

WORD32 DelayBeforeDeletingTables;	// # of secs after last game before deleting a table
int    TimeBetweenGames=5;			// Leave n seconds between games
int    TimeBetweenHiLoGames=10;		// Leave n seconds between hi/lo games
int    TimeBetweenHiLoGames_HiOnly=10;// Leave n seconds between Hi/Lo games when only a high wins

WORD32 FirstInputWarningSeconds;	// # of secs before warning a player to respond to input request soon
WORD32 SecondInputWarningSeconds;	// # of secs before 2nd warning
WORD32 InputTimeoutSeconds;			// # of secs before timing out a player input request.
WORD32 TimeoutShowOnlyHand;			// wait max n seconds when everyone folds to you (one player left)
WORD32 TimeoutShowFirstHand;		// wait max n seconds for first show (when more than one player left)
WORD32 TimeoutShowOtherHands;		// wait max n seconds for additional shows (after first)
WORD32 TimeoutMuckHand;				// wait max n seconds for manual show/muck (auto-muck turned off)
WORD32 TimeoutAutoMuckHand;			// wait max n seconds for auto-muck
WORD32 AutoShowdownSpacing;			// when automatic showdowns are enabled, put n secs between players

WORD32 ComputerPlayerAnswerDelay;		// # of secs the computer should take before making a move.
int    ComputerPlayersPerTable;			// # of computer players at each table (testing only)
int    ComputerPlayersPerHoldemTable;	// # of computer players at each Holdem table (testing only)
int    ComputerPlayersPerOmahaTable;	// # of computer players at each Omaha table (testing only)
int    ComputerPlayersPerSevenCSTable;	// # of computer players at each Seven card stud table (testing only)
int    ComputerPlayersPerOneOnOneTable;	// # of computer players at each One on One Holdem table (testing only)

WORD32 WaitListTotalTimeout;		// # of secs from first empty seat notice to timing out.
WORD32 WaitListReminderInterval;	// # of secs between reminder notices while seat is open
WORD32 WatchingTableTimeout;		// # of secs after disconnect before player is kicked from watching tables.
WORD32 PlayingTableTimeout;			// # of secs after disconnect before player is kicked from playing at a table
WORD32 WaitListDisconnectTimeout;	// # of secs after disconnect before player is kicked from waiting lists

WORD32 ShotClockDate;				// time_t when shot clock "goes off"
WORD32 ShotClockFlags;				// SCUF_* (from gamedata.h)
char   ShotClockMessage1[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
char   ShotClockMessage2[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
char   ShotClockMessage1_Expired[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock
char   ShotClockMessage2_Expired[SHOTCLOCK_MESSAGE_LEN];	// message to be displayed with shot clock

int    MaxRake;					// max $ for rake (all table and game types)
int    RealMoneyTablesOpen;		// 0=real money tables closed, 1=open
int    ECashDisabled;			// set if all ecash functions should be disabled.
int    CashoutsDisabled;		// set if cashouts are currently disabled (usually the cashier is still open)
char   ECashProtocolVersionStr[ECASH_PROTOCOL_VER_STR_LEN];	// "1.01", "1.03", etc.
int    ECashThreads;			// # of ecash threads to launch (range is 1 to MAX_ECASH_THREADS (20))
int    ECashHoursBeforeCashout;	// min # of hours required after a purchase before a cashout is allowed

int    AllInsAllowed;			// # of all-ins allowed per 24 hour period.
int    GoodAllInsAllowed;		// # of good all-ins allowed for automatic reset
WORD32 AllInsResetTime;			// time when global all-ins should be considered 'reset'
int    MaxAccountsPerComputer;	// maximum # of accounts a user is allowed to create per computer serial #
int    PromotionalGameNumberMultiple; // 10,000 or 100,000, etc. for making sure game is real money with 4 or more players.
int    PromotionalGameNumberMinPlayers; // min # of players allowed to play one of the important game numbers

char   GameDisabledMessage[GAME_DISABLED_MESSAGE_LEN];	// message displayed when a game is disabled
int    GameDisableBits;			// bits to match up with table bits in Cardroom::ManageTables().  Games don't play if matching bits set.
int    GameCloseBits;			// these bits will actually keep a table closed (won't be visible)
// Allow restricting the total number of tables for a particular game type
// by using the game disable bits as an indicator of which game types have
// maximums we need to impose.  If set to zero, there is no special limit.
int	   MaxTables_GDB[8];

int    CCLimit1Amount;			// limit (in $'s) for first n days
int    CCLimit1Amount2;			// higher limit (in $'s) for first n days
int    CCLimit1Days;			// # of days this limit applies for
int    CCLimit2Amount;			// limit (in $'s) for first n days
int    CCLimit2Amount2;			// higher limit (in $'s) for first n days
int    CCLimit2Days;			// # of days this limit applies for
int    CCLimit3Amount;			// limit (in $'s) for first n days
int    CCLimit3Amount2;			// higher limit (in $'s) for first n days
int    CCLimit3Days;			// # of days this limit applies for
int    CCPurchaseMinimum;		// lower limit for purchases
int	   CCMaxAllowableUsers;		// maximum number of users allowed per credit card
float  CCFeeRate;				// fee rate to apply to cc purchases
char   CCChargeName[CCCHARGE_NAME_LEN];	// what shows up on client statements? (e.g. "SFC_Desert PokerPOKER BRIDGETOWN")

// kriskoin : Deposit bonus and cashout control function
int     DepositBonusStartDate;
int     DepositBonusEndDate;
int     DepositBonusRate;
int     DepositBonusCashoutPoints;
int     DepositBonusMax;
// end kriskoin 
// kriskoin 2018/07/07HighHand hihand_real[2];
HighHand hihand_play[2];
// end kriskoin 

//****************************************************************
// 
//
// Set a new desired run level.
//
void SetRunLevelDesired(int new_run_level, char *reason, ...)
{
	if (iRunLevelDesired != new_run_level) {
	  #if DEBUG
		int old_run_level = iRunLevelDesired;
		iRunLevelDesired = new_run_level;
	    va_list arg_ptr;
	    char str[200];
		va_start(arg_ptr, reason);
        vsprintf(str, reason, arg_ptr);
        va_end(arg_ptr);

		kp(("\r%s Chg DesiredRunLevel: %d to %d (Cur=%d) Reason: %s\n",
				TimeStr(), old_run_level, new_run_level, iRunLevelCurrent, str));
	  #else
		iRunLevelDesired = new_run_level;
		NOTUSED(reason);
	  #endif
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Test if a player is connected given just their player_id
//
int TestIfPlayerConnected(WORD32 player_id)
{
	int connected = FALSE;
	if (CardRoomPtr) {
		connected = CardRoomPtr->TestIfPlayerConnected(player_id);
	}
	return connected;
}	

//*********************************************************
// https://github.com/kriskoin//
// Return our best guess about the quality of the connection to
// a player given just their 32-bit player_id.
// Returns CONNECTION_STATE_* (see player.h)
//
int GetPlayerConnectionState(WORD32 player_id)
{
	int result = CONNECTION_STATE_LOST;
	if (CardRoomPtr) {
		result = CardRoomPtr->GetPlayerConnectionState(player_id);
	}
	return result;
}


//*********************************************************
// https://github.com/kriskoin//
// Count the number of all-in's allowed for an arbitrary player
//
int AllowedAutoAllInCount(WORD32 player_id)
{
	int count = 0;
	// the rule at the moment is 'int AllInsAllowed' per 24 hours
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		time_t now = time(NULL);
		for (int i=0 ; i<AllInsAllowed ; i++) {
			// player all in times 0->n goes from recent to oldest...
			#define MINIMUM_ELAPSED_SECONDS_FOR_ALL_IN	24*60*60	// 24 hours
			if (!player_rec.all_in_times[i] ||
				now - player_rec.all_in_times[i] > MINIMUM_ELAPSED_SECONDS_FOR_ALL_IN ||
				AllInsResetTime >= player_rec.all_in_times[i])
			{
				count++;	// this one was long enough ago... another is allowed.
			}
		}
	}
	return count;
}

//*********************************************************
// https://github.com/kriskoin//
// Reset the number of available all-ins for a player.
//
void ResetAllInsForPlayer(WORD32 player_id, int send_email_flag)
{
	// If they were reset in the last 5 minutes, don't do it again unless
	// we're not sending email about it.
	struct SDBRecord r;
	zstruct(r);
	int index = SDB->SearchDataBaseByPlayerID(player_id, &r);
	if (index < 0) {
		return;	// not found, nothing to do.
	}
	time_t now = time(NULL);
//	kp(("%s(%d) difftime(%d,%d) = %d\n",_FL,now, r.all_in_reset_time,difftime(now,r.all_in_reset_time)));
	if (send_email_flag && r.all_in_reset_time &&
		now - r.all_in_reset_time < 5*60)
	{
		pr(("%s(%d) Skipping all-in reset for '%s'. Too soon since last (%d sec)\n",
				_FL,r.user_id, now - r.all_in_reset_time));
		return;	// don't do it... too soon.
	}

	int allins_reset = FALSE;
	// only add enough blanks to bring them back up to normal.
	for (int i=0 ; i<AllInsAllowed ; i++) {
		if (AllowedAutoAllInCount(player_id) >= AllInsAllowed) {
			break;	// no need to add more blank entries.
		}
		SDB->SavePlayerAllInInfo(player_id, 0, 0, 0);
		allins_reset = TRUE;	// remember we did change at least one entry.
	}
	if (allins_reset && send_email_flag) {
		// Send an email notifying them their all-ins were reset.
		char fname[MAX_FNAME_LEN];
		MakeTempFName(fname, "a");	// create a temporary filename to write this to.
		FILE *fd = NULL;
		if (((fd = fopen(fname, "wt")) != NULL)) {
			// opened the file -- write a header
			fprintf(fd,"All-Ins for player ID %s were reset on %-11.11s CST\n", r.user_id, TimeStr());
			fprintf(fd,"\n");
			fprintf(fd,"At the time this email was sent, you have %d All-In%s left.\n",
					AllInsAllowed, AllInsAllowed==1 ? "" : "s");
			fprintf(fd,"\n");
			fprintf(fd,"Best regards,\n");
			fprintf(fd,"Desert Poker  customer support\n");
			fprintf(fd,"\n");
			fclose(fd);
			fd = NULL;
			char subject[200];
			zstruct(subject);
			if (!iRunningLiveFlag) {
				strcpy(subject, "TESTING ONLY - IGNORE: ");
			}
			strcat(subject, "Desert Poker All-Ins reset for ");
			strcat(subject, r.user_id);
			if (iRunningLiveFlag) {
				// Send to the real person.
				Email(r.email_address,
						"Desert Poker",
						"support@kkrekop.io",
						subject,
						fname,
					  #if 1	// 2022 kriskoin
						NULL,							// Bcc:
					  #else
						"allinreset@kkrekop.io",	// Bcc:
					  #endif
						TRUE);							// Delete file when done
			} else {
				// Send to support so we can test it
				Email(	"support@kkrekop.io",	// To:
						"Desert Poker",				// From "name"
						"support@kkrekop.io",	// From:
						subject,
						fname,
						"allinreset@kkrekop.io",	// Bcc:
						TRUE);							// Delete file when done
			}
		} else {
			Error(ERR_ERROR,"%s(%d) Couldn't open tmp file (%s) for write", _FL, fname);
		}
	}
	// If they're logged in, make sure their new count gets sent to them.
	if (CardRoomPtr) {
		CardRoomPtr->SendClientInfo(player_id);
	}
}	

//*********************************************************
// https://github.com/kriskoin//
// Add a future all-in reset to the queue.
// when is in SecondCounter units.
//
#define ALLIN_RESET_QUEUE_LEN	1000
struct AllInResetQueueEntry {
	WORD32 when;
	WORD32 player_id;
} AllInResetQueue[ALLIN_RESET_QUEUE_LEN];
int iAllInResetQueueLen;

void AddFutureAllInResetToQueue(WORD32 when, WORD32 player_id)
{
	if (iAllInResetQueueLen < ALLIN_RESET_QUEUE_LEN) {
		zstruct(AllInResetQueue[iAllInResetQueueLen]);
		AllInResetQueue[iAllInResetQueueLen].when = when;
		AllInResetQueue[iAllInResetQueueLen].player_id = player_id;
		iAllInResetQueueLen++;
	} else {
		Error(ERR_ERROR, "%s(%d) Warning: all in reset queue is full",_FL);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Update the all in reset queue... reset players if necessary.
// This function should be called periodically (every 10s or so).
//
void UpdateAllInResetQueue(void)
{
	while (iAllInResetQueueLen && SecondCounter >= AllInResetQueue[0].when) {
		// Time to reset someone.
		ResetAllInsForPlayer(AllInResetQueue[0].player_id, TRUE);
		// Now scroll the list down...
		if (iAllInResetQueueLen > 1) {
			memmove(&AllInResetQueue[0], &AllInResetQueue[1], (iAllInResetQueueLen-1)*sizeof(struct AllInResetQueueEntry));
		}
		iAllInResetQueueLen--;	// one fewer items in the list.
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Write out the two server vars files.
//
void WriteServerVars(void)
{
	EnterCriticalSection(&SerNumDBCritSec);
	ServerVars.file_serial_num++;
	WriteFile("server_vars1.bin", &ServerVars, sizeof(ServerVars));
	ServerVars.file_serial_num++;
	WriteFile("server_vars2.bin", &ServerVars, sizeof(ServerVars));
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// 2002/04/08 - kriskoin
//
// Read in the current high hand of the day from log files.
// This should only be done at startup.
//
void ReadHighHand(void)
{
        int fp;
	zstruct(hihand_real);
	zstruct(hihand_play);

//        if(fp=open("logs/hihand_real.bin",O_RDWR)){
  if(fp=open("Data\\DB\\hihand_real.bin",O_RDWR)){
		read(fp, &hihand_real[0],sizeof(HighHand));	
		read(fp, &hihand_real[1],sizeof(HighHand));	
	}
        close(fp);

//        if(fp=open("logs/hihand.bin",O_RDWR)){
  if(fp=open("Data\\DB\\hihand.bin",O_RDWR)){
		read(fp, &hihand_play[0],sizeof(HighHand));	
		read(fp, &hihand_play[1],sizeof(HighHand));	
	}
        close(fp);
}
// end kriskoin 

//*********************************************************
// https://github.com/kriskoin//
// Read in the most recent version of ServerVars.  This should only
// be done at startup.
//
void ReadServerVars(void)
{
	EnterCriticalSection(&ParmFileCritSec);
	EnterCriticalSection(&SerNumDBCritSec);
	struct PersistentServerVars sv1, sv2;
	zstruct(sv1);
	zstruct(sv2);
	long bytes_read1 = 0;
	long bytes_read2 = 0;
	ErrorType err1 = ReadFile("server_vars1.bin", &sv1, sizeof(sv1), &bytes_read1);
	ErrorType err2 = ReadFile("server_vars2.bin", &sv1, sizeof(sv1), &bytes_read2);
	// Now decide which to use...
	if (err1 != ERR_NONE) {
		zstruct(sv1);
	}
	if (err2 != ERR_NONE) {
		zstruct(sv2);
	}
	if (bytes_read1 > bytes_read2) {
		zstruct(sv2);
	}
	if (bytes_read2 > bytes_read1) {
		zstruct(sv1);
	}
	if (sv1.file_serial_num) {
		ServerVars = sv1;
	}
	if (sv2.file_serial_num > sv1.file_serial_num) {
		ServerVars = sv2;
	}
	LeaveCriticalSection(&SerNumDBCritSec);
	LeaveCriticalSection(&ParmFileCritSec);
}

/**********************************************************************************
 Function SendAdminAlert(enum ChatTextType alert_level, char *alert_msg, ...);
 date: 24/01/01 kriskoin Purpose: send alert to all admin clients
***********************************************************************************/
void SendAdminAlert(enum ChatTextType alert_level, char *alert_msg, ...)
{
	if (CardRoomPtr) {
		GameChatMessage gcm;
		zstruct(gcm);
	    char str[MAX_CHAT_MSG_LEN*2];
		zstruct(str);
	    va_list arg_ptr;
		va_start(arg_ptr, alert_msg);
		vsprintf(str, alert_msg, arg_ptr);
	    va_end(arg_ptr);
		gcm.text_type = (BYTE8)alert_level;
		strnncpy(gcm.message, str, MAX_CHAT_MSG_LEN);
		CardRoomPtr->SendAdminChatMonitor(&gcm, NULL, CT_PLAY);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Our critical alert handler that gets called from pplib
// when something really bad happens.
//
void OurCriticalAlertHandler(char *critical_alert_msg)
{
	SendAdminAlert(ALERT_10, "*** CRITICAL ALERT: Contact Admin ASAP *** : %s", critical_alert_msg);	
}

//*********************************************************
// https://github.com/kriskoin//
// Return a unique computer serial number (next one from the pool)
//
WORD32 GetNextComputerSerialNum(void)
{
	WORD32 next = ServerVars.next_computer_serial_num++;
	WriteServerVars();	// save changes to disk asap
	return next;
}

//*********************************************************
// https://github.com/kriskoin//
// Load the list of computers to block
//
Array ComputerBlockList;

void LoadComputerBlockList(void)
{
	EnterCriticalSection(&SerNumDBCritSec);
    ComputerBlockList.SetParms(sizeof(WORD32), sizeof(WORD32), 10);
    ComputerBlockList.LoadFile("block_list.bin");
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Save the list of computers to block
//
void SaveComputerBlockList(void)
{
	EnterCriticalSection(&SerNumDBCritSec);
	ComputerBlockList.WriteFileIfNecessary("block_list.bin");
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Add a computer serial number to the "block" list.
//
void AddComputerBlock(WORD32 computer_serial_num)
{
	if (computer_serial_num) {
		EnterCriticalSection(&SerNumDBCritSec);
        ComputerBlockList.Add(&computer_serial_num);
        SaveComputerBlockList();
		LeaveCriticalSection(&SerNumDBCritSec);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Remove a computer serial number to the "block" list.
//
void RemoveComputerBlock(WORD32 computer_serial_num)
{
	if (computer_serial_num) {
		EnterCriticalSection(&SerNumDBCritSec);
        ComputerBlockList.Remove(&computer_serial_num);
        SaveComputerBlockList();
		LeaveCriticalSection(&SerNumDBCritSec);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Check if a computer has been blocked.  Return TRUE if it has.
//
int CheckComputerBlock(WORD32 computer_serial_num)
{
	if (!computer_serial_num) {
		return FALSE;	// no serial num.... definitely not blocked.
	}
	EnterCriticalSection(&SerNumDBCritSec);
    void *p = ComputerBlockList.Find(&computer_serial_num);
	LeaveCriticalSection(&SerNumDBCritSec);
    if (p) {
        return TRUE;    // found it... it's blocked.
    }
    return FALSE;       // not found... therefore not blocked.
}

//*********************************************************
// https://github.com/kriskoin//
// Set a 'work_was_done_flag'.  Print details if details
// are turned on (iPrintWorkWasDoneFlagDetails > 0)
//
int iPrintWorkWasDoneFlagDetails;	// set to n > 0 to print the details n times.
void SetWorkWasDoneFlag(int *work_was_done_flag_ptr, char *format_str, ...)
{
	if (iPrintWorkWasDoneFlagDetails && !*work_was_done_flag_ptr) {
		char str[1000];
	    va_list arg_ptr;
		va_start(arg_ptr, format_str);
		vsprintf(str, format_str, arg_ptr);
		va_end(arg_ptr);
		kp(("#%3d SetWorkWasDoneFlag() called: %s\n",iPrintWorkWasDoneFlagDetails,str));
		iPrintWorkWasDoneFlagDetails--;		// print details one fewer times
	}
	*work_was_done_flag_ptr = TRUE;
}

#if WIN32
//****************************************************************
// 
//
// Handler routine for trapping ctrl signals (such as Ctrl+C, Ctrl+Break)
//
BOOL WINAPI OurCtrlHandlerRoutine(DWORD dwCtrlType)
{
	//kp(("%s(%d) OurCtrlHandleRoutine() was called with type = %d ($%08lx)\n",_FL,dwCtrlType, dwCtrlType));
	BOOL handled = FALSE;
	switch (dwCtrlType) {
	case CTRL_C_EVENT:
		// Ctrl+c signal was received
		//kp(("%s(%d) Ctrl+C signal received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "Ctrl+C received.");
		handled = TRUE;
		break;
	case CTRL_BREAK_EVENT:
		// Ctrl+Break signal was received
	  #if 1	// 2022 kriskoin
		kp(("%s(%d) Ctrl+Break signal received. Re-reading .ini file\n",_FL));
		ReReadParmFileFlag = TRUE;
	  #else
		kp(("%s(%d) Ctrl+Break signal received. Setting iShutdownAfterECashCompletedFlag=TRUE\n",_FL));
		iShutdownAfterECashCompletedFlag = TRUE;
		if (!CardRoomPtr || CardRoomPtr->connected_players==0) {	// no real players?
			SetRunLevelDesired(RUNLEVEL_EXIT, "Ctrl+Brk received.");
		}
	  #endif
		handled = TRUE;
		break;
	case CTRL_CLOSE_EVENT:
		// A signal that the system sends to all processes attached to a
		// console when the user closes the console (either by choosing
		// the Close command from the console window's System menu, or by
		// choosing the End Task command from the Task List).
		kp(("%s(%d) Close signal received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "Close signal received.");
		handled = TRUE;
		break;
	case CTRL_LOGOFF_EVENT:
		// A signal that the system sends to all console processes when a
		// user is logging off. This signal does not indicate which user is
		// logging off, so no assumptions can be made.
		kp(("%s(%d) LogOff signal received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "LogOff signal received.");
		handled = TRUE;
		break;
	case CTRL_SHUTDOWN_EVENT:
		// A signal that the system sends to all console processes when
		// the system is shutting down.
		kp(("%s(%d) Shutdown signal received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "System Shutdown signal received.");
		handled = TRUE;
		break;
	default:
		Error(ERR_ERROR, "%s(%d) Unknown control signal received from OS (%d).",_FL,dwCtrlType);
		break;
	}

	return handled;	// return a BOOL indicating whether or not we handled this signal
}
#else // !WIN32
/* Tony, Nov 30, 2001 */
void EmailToAdmin()
{
	char email_title[100];
	char email_buffer[2048];
	sprintf(email_title, "Poker Server Shutdown");
	memset(email_buffer, 0, 2048);	// zero out our output buffer
	// footer
	strcat(email_buffer, "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
	strcat(email_buffer,
		"The server is about to restart.\n\n"
		"The restart process should be finished in 90 seconds.\n"
		"Play will resume as soon as the system has restarted.\n\n");
	// email it
	{
		FILE *file_out;
		char filename[MAX_FNAME_LEN];
		zstruct(filename);
		MakeTempFName(filename, "ece");
		if ((file_out = fopen(filename, "wt")) == NULL) {
			Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL, filename);
		} else {
			char cmd[1024];
			fputs(email_buffer, file_out);	// dump the whole thing out
			fclose(file_out);
//Temporary comment it			sprintf(cmd,  "fastmail -f \"Poker Server\" -F \"support@kkrekop.io\" -s \"%s\" \"%s\" \"6047884130@msg.telus.com\"", email_title, filename); system(cmd); remove(filename);
/*			Email(  "management@kkrekop.io",
					"Desert Poker",
					"support@kkrekop.io",
					email_title,
					filename,
					NULL,	// bcc
					TRUE);	// delete when sent
*/
		}
	}
}
//EOTony

//*********************************************************
// https://github.com/kriskoin//
// Signal handler routine for trapping ctrl signals
// See signals.h for a complete list.
// killall defaults to SIGTERM
// 'shutdown' sends SIGTERM and then we have 5 seconds before SIGKILL.
// (see 'man init' for more details).
//
void OurSignalHandlerRoutine(int signal, struct sigcontext sc)
{
	switch (signal) {
	case SIGTERM:
		// We've been asked to shut down.  Probably killall or something.
		// 'shutdown' sends SIGTERM and then we have 5 seconds before SIGKILL.
		// (see 'man init' for more details).
		//kp(("%s(%d) SIGTERM received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "SIGTERM received.");
		EmailToAdmin();	//Tony
		break;
	case SIGHUP:
		// Re-read the .INI file for our default settings...
		kp(("%s(%d) SIGHUP received. Setting ReReadParmFileFlag = TRUE (was %d)\n",_FL, ReReadParmFileFlag));
		ReReadParmFileFlag = TRUE;
		break;
	case SIGINT:	// usually ctrl+C
		//kp(("%s(%d) SIGINT received.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, "SIGINT received.");
		break;
	case SIGPIPE:
		//kp(("%s(%d) SIGPIPE received. Ignoring.\n",_FL));
		break;
	case SIGQUIT:
		if (!iShutdownAfterECashCompletedFlag)  {
			kp(("%s %s(%d) SIGQUIT received. Setting iShutdownAfterECashCompletedFlag=TRUE\n",TimeStr(),_FL));
			iShutdownAfterECashCompletedFlag = TRUE;
			if (!iRunningLiveFlag && (!CardRoomPtr || CardRoomPtr->connected_players==0)) {	// no real players?
				SetRunLevelDesired(RUNLEVEL_EXIT, "SIGQUIT with no players.");
			}
		}
		break;
	case SIGBUS:
		{ static int reentry_flag;
			if (reentry_flag) {
				kp(("SIGBUS re-entered.  Forcing immediate exit.\n"));
				_exit(20);
			}
			reentry_flag = TRUE;
			kp(("SIGBUS: instruction at %08lx failed trying to access memory at %08lx.\n", sc.eip, sc.cr2));
			kp(("      regs: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", sc.eax, sc.ebx, sc.ecx, sc.edx));
			kp(("            esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eflags=%08lx\n", sc.esi, sc.edi, sc.ebp, sc.esp, sc.eflags));
			kp(("%s Here's a current stack crawl from esp:\n", TimeStr()));
			DisplayStackCrawlEx((dword *)sc.esp, sc.eip, GetThreadName());
			Error(ERR_FATAL_ERROR, "SIGBUS received.  Exiting program.  See debwin.log for more details.");
			exit(10);
		}
	case SIGFPE:
		{ static int reentry_flag;
			if (reentry_flag) {
				kp(("SIGFPE re-entered.  Forcing immediate exit.\n"));
				_exit(20);
			}
			reentry_flag = TRUE;
			kp(("SIGFPE: instruction at %08lx failed.  Probably divide by zero.\n", sc.eip));
			kp(("            (see /usr/linux/include/siginfo.h for more about si_code)\n"));
			kp(("      regs: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", sc.eax, sc.ebx, sc.ecx, sc.edx));
			kp(("            esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eflags=%08lx\n", sc.esi, sc.edi, sc.ebp, sc.esp, sc.eflags));
			kp(("%s Here's a current stack crawl from esp:\n", TimeStr()));
			DisplayStackCrawlEx((dword *)sc.esp, sc.eip, GetThreadName());
			Error(ERR_FATAL_ERROR, "SIGFPE received.  Exiting program.  See debwin.log for more details.");
			exit(10);
		}
	case SIGSEGV:
		{ static int reentry_flag;
			if (reentry_flag) {
				kp(("SIGSEGV re-entered.  Forcing immediate exit.\n"));
				_exit(20);
			}
			reentry_flag = TRUE;
			/* struct sigcontext_struct {
					unsigned short gs, __gsh;
					unsigned short fs, __fsh;
					unsigned short es, __esh;
					unsigned short ds, __dsh;
					unsigned long edi;
					unsigned long esi;
					unsigned long ebp;
					unsigned long esp;
					unsigned long ebx;
					unsigned long edx;
					unsigned long ecx;
					unsigned long eax;
					unsigned long trapno;
					unsigned long err;
					unsigned long eip;
					unsigned short cs, __csh;
					unsigned long eflags;
					unsigned long esp_at_signal;
					unsigned short ss, __ssh;
					struct _fpstate * fpstate;
					unsigned long oldmask;
					unsigned long cr2;
			};	*/
			kp(("SEGV: instruction at %08lx failed trying to access memory at %08lx.\n", sc.eip, sc.cr2));
			kp(("      regs: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", sc.eax, sc.ebx, sc.ecx, sc.edx));
			kp(("            esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eflags=%08lx\n", sc.esi, sc.edi, sc.ebp, sc.esp, sc.eflags));
			//kp(("use 'nm pokersrv|sort>pokersrv.sym' to get map file (if compiled with -g)\n"));
			kp(("%s Here's a current stack crawl from esp:\n", TimeStr()));
			DisplayStackCrawlEx((dword *)sc.esp, sc.eip, GetThreadName());
			Error(ERR_FATAL_ERROR, "SIGSEGV received.  Exiting program.  See debwin.log for more details.");
			exit(10);
		}
	case SIGALRM:
		{ static int reentry_flag;
			if (reentry_flag) {
				kp(("SIGALRM handler re-entered.  Ignoring.\n"));
				return;
			}
			reentry_flag = TRUE;
			kp(("--------------- SIGALRM received. Here's our best guess at pid %d's state:\n", getpid()));
			kp(("regs: eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx\n", sc.eax, sc.ebx, sc.ecx, sc.edx));
			kp(("      esi=%08lx edi=%08lx ebp=%08lx esp=%08lx eflags=%08lx\n", sc.esi, sc.edi, sc.ebp, sc.esp, sc.eflags));
			kp(("      eip=%08lx\n", sc.eip));
			kp(("%s Here's a current stack crawl from esp:\n", TimeStr()));
			DisplayStackCrawlEx((dword *)sc.esp, sc.eip, GetThreadName());
			kp(("--------------- Done SIGALRM stack crawl. Attempting to resume.\n"));
			reentry_flag = FALSE;
		}
		break;
	default:
		Error(ERR_ERROR, "%s(%d) Unknown control signal received from OS (%d).",_FL,signal);
		break;
	}
}
#endif // !WIN32

/**********************************************************************************
 Function MakeBadBeatHands(void)
 Date: HK00/07/04 (modified 20000819 to add support for 3 different types)
 Purpose: create the bad beat minimum hand based on what was read from the .ini file
***********************************************************************************/
ErrorType MakeBadBeatHands(void)
{
	if (!BadBeatJackpotActive_holdem &&
		!BadBeatJackpotActive_omaha &&
		!BadBeatJackpotActive_stud) {	// disabled, so don't bother
		return ERR_NONE;
	}

	// validate the percentage payouts
	int sum_of_percentages =  BadBeatHandWinnerCut+BadBeatHandLoserCut+BadBeatParticipantCut;
	if (sum_of_percentages != 100) {
		Error(ERR_ERROR, "%s(%d) BadBeat payout percentages add up to %d (not 100) -- disabling bad beat jackpot",
			_FL, sum_of_percentages);
		return ERR_ERROR;	// this will take care of it
	}

	pr(("%s(%d) MakeBadBeatHand() received %s (%d)\n", _FL, BadBeatDefinition, BadBeatJackpotActive));
	// we've got something like: Ah Ad Ac Jd Js

	Hand *bad_beat_hand = NULL;
	char *p = NULL; // will point to defintion
	for (int i=0; i < 3; i++) {	// three times (holdem, omaha, stud)
		if (i == 0) { // hold'em
			bad_beat_hand = &Hand_BadBeat_holdem;		
			p = BadBeatDefinition_holdem;
		} else if (i == 1) {	// omaha
			bad_beat_hand = &Hand_BadBeat_omaha;		
			p = BadBeatDefinition_omaha;
		} else if (i == 2) {	// stud
			bad_beat_hand = &Hand_BadBeat_stud;		
			p = BadBeatDefinition_stud;
		} else {
			kp(("%s(%d) impossible -- see src", _FL));
		}
		bad_beat_hand->ClearHandCards();// start from scratch
//		char *p = BadBeatDefinition;
		while (p) {
			char rank_char = *p;
			char suit_char = *(p+1);
			Card CardRank = CARD_NO_CARD;
			Card CardSuit = CARD_NO_CARD;
			switch (rank_char) {
				case '2' : CardRank = Two; break;
				case '3' : CardRank = Three; break;
				case '4' : CardRank = Four; break;
				case '5' : CardRank = Five; break;
				case '6' : CardRank = Six; break;
				case '7' : CardRank = Seven; break;
				case '8' : CardRank = Eight; break;
				case '9' : CardRank = Nine; break;
				case 'T' : CardRank = Ten; break;
				case 'J' : CardRank = Jack; break;
				case 'Q' : CardRank = Queen; break;
				case 'K' : CardRank = King; break;
				case 'A' : CardRank = Ace; break;
			}
			switch (suit_char) {
				case 'c' : CardSuit = Clubs; break;
				case 'd' : CardSuit = Diamonds; break;
				case 'h' : CardSuit = Hearts; break;
				case 's' : CardSuit = Spades; break;
			}
			if (CardRank == CARD_NO_CARD || CardSuit == CARD_NO_CARD) {	// no good
				return ERR_ERROR;
			}
			bad_beat_hand->Add(MAKECARD(CardRank, CardSuit));
			p++;
			p = strpbrk(p, "23456789TJQKA");
		}
	}
	// did we wind up with something valid?
	if (bad_beat_hand->CardCount() !=5) {	// not five cards
		return ERR_ERROR;
	}
	// we are going to clear BadBeatDefinition(s) and fill it in later with real descriptions
	zstruct(BadBeatDefinition_holdem);
	zstruct(BadBeatDefinition_omaha);
	zstruct(BadBeatDefinition_stud);
	// looks ok
	return ERR_NONE;
}
//*********************************************************
// https://github.com/kriskoin//
// Read our Parm (.INI) file.
//
void ReadOurParmFile(void)
{
	EnterCriticalSection(&ParmFileCritSec);
	ReadParmFile("PokerSrv.ini", getenv("COMPUTERNAME"), INIParms, TRUE);
	ReReadParmFileFlag = FALSE;	// always clear.

	// The various build version numbers need some special post-read handling.
	// Build a combined version and build index variable for easy testing.
	#define COMBINE_VERSION_INFO(ver_num_ptr)	\
				(ver_num_ptr)->build = ((ver_num_ptr)->build & 0x00FFFF) | \
									   ((ver_num_ptr)->major << 24) | \
									   ((ver_num_ptr)->minor << 16);
	COMBINE_VERSION_INFO(&ServerVersionInfo.server_version);
	COMBINE_VERSION_INFO(&ServerVersionInfo.min_client_version);
	COMBINE_VERSION_INFO(&ServerVersionInfo.new_client_version);
	if (MakeBadBeatHands() == ERR_ERROR) {
		Error(ERR_ERROR, "%s(%d) Couldn't properly parse bad beat hand definitions -- disabling all bad beat jackpots", _FL);
		BadBeatJackpotActive_holdem = 0;
		BadBeatJackpotActive_omaha = 0;
		BadBeatJackpotActive_stud = 0;
	}
	if (AlternateServerIP[0]) {
		ServerVersionInfo.alternate_server_ip = IP_ConvertHostNameToIP(AlternateServerIP);
	} else {
		ServerVersionInfo.alternate_server_ip = 0;
	}
	if (iRunningLiveFlag) {
		ServerVersionInfo.server_version.flags |= VERSIONFLAG_RUNNINGLIVE;
	}
	if (GameCloseBits & GDB_CLOSE_TOURNAMENTS) {
		ShotClockFlags |= SCUF_NO_TOURNAMENT_SITDOWN;
	} else {
		ShotClockFlags &= ~SCUF_NO_TOURNAMENT_SITDOWN;
	}
	if (GameDisableBits & GDB_CLOSE_TOURNAMENTS) {
		ShotClockFlags |= SCUF_CLOSE_TOURNAMENTS;
		iTournamentTableCreationAllowed = FALSE;
	} else {
		ShotClockFlags &= ~SCUF_CLOSE_TOURNAMENTS;
		iTournamentTableCreationAllowed = TRUE;
	}
	iShotClockChangedFlag = TRUE;	// treat as if it just changed.
	NextAdminStatsUpdateTime = 0;	// update/send admin stats packet asap

	if (MaxAcceptedConnections > MAX_SERVICEABLE_PLAYERS) {
		kp(("%s(%d) **** WARNING: MaxAcceptedConnections (%d) is larger than MAX_SERVICEABLE_PLAYERS (%d)\n",
				_FL, MaxAcceptedConnections, MAX_SERVICEABLE_PLAYERS));
	}
  #if !WIN32	// 2022 kriskoin
	if (MaxAcceptedConnections > NR_OPEN-50) {
		// /usr/src/linux/include/linux/limits.h and fs.h contain NR_OPEN, etc.
		// which need to be increased to allow for more than 1024 file handles
		// per process.
		kp(("%s(%d) **** WARNING: MaxAcceptedConnections (%d) is larger than (or too close to) NR_OPEN (%d)\n",
				_FL, MaxAcceptedConnections, NR_OPEN));
	}
	// __FD_SETSIZE is defined in:
	// /usr/src/linux/include/linux/posix_types.h and
	// /usr/include/bits/types.h
	// It should be kept at the same value as NR_OPEN.
	if (MaxAcceptedConnections > __FD_SETSIZE-50) {
		kp(("%s(%d) **** WARNING: MaxAcceptedConnections (%d) is larger than (or too close to) __FD_SETSIZE (%d)\n",
				_FL, MaxAcceptedConnections, __FD_SETSIZE));
	}
  #endif

	LeaveCriticalSection(&ParmFileCritSec);
}

#if 0	// card testing stuff replaces main // !!!!!!!!!!!!!!!!!!!!!
#include "deck.h"
/**********************************************************************************
 Function main(int argc, char *argv[])
 date: kriskoin 2019/01/01 Purpose: test card shuffling and numbers, etc...
***********************************************************************************/
int main(int argc, char *argv[])
{
	printf("%s(%d) Shuffle Testing...\n", _FL);
	setvbuf(stdout, NULL, _IONBF, 0);
	srand((unsigned)time(NULL));	// init bad random number generator
	RNG_InitializeSeed();			// init our good random number generator

#if 1	//kriskoin: 	Deck deck;
	int counts[64][64];	// [card #] [position in final deck]
	zstruct(counts);
	int i;
	for (i=0 ; i<1000000 ; i++) {
		RNG_AddToEntropyUsingRDTSC();
		deck.CreateDeck();		// intialize all cards
		RNG_AddToEntropyUsingRDTSC();
		deck.ShuffleDeck(1);	// shuffle deck once (exactly once)
		// keep track of where each card ended up
		for (int j=0 ; j<CARDS_IN_DECK ; j++) {
			counts[CARDINDEX(deck.GetCard(j))][j]++;
		}
	}

  #if 0	// 2022 kriskoin
	for (i=0 ; i<CARDS_IN_DECK ; i++) {
		printf("Card: %d:  ", i);
		for (int j=0 ; j<CARDS_IN_DECK ; j++) {
			printf("%5d ", counts[i][j]);
		}
		printf("\n");
	}
	printf("\n");
  #else
	for (i=0 ; i<CARDS_IN_DECK ; i++) {
		printf("Card: %d (%c%c)...\n", i, cRanks[i % CARD_RANKS], cSuits[i / CARD_RANKS]);
		for (int j=0 ; j<CARDS_IN_DECK ; j++) {
			printf("%7d\n", counts[i][j]);
		}
	}
  #endif

#else	// look for duplicate shuffles...

	//kriskoin: 	//	1)	Create one large array to hold all saved decks.  Create
	//		DECK_ARRAYS arrays of pointers into that data.  Each array entry
	//		points to the start of a deck in the global pool.
	//	2)	Make two passes.  First pass, just create decks and add them
	//		to the global pool.  Second pass, do the comparisons for
	//		each of the DECK_ARRAY arrays of pointers.  You know that you
	//		only need to compare against other entries with the same
	//		starting DECK_ARRAY index.

	Deck deck;
	Card newdeck[CARDS_IN_DECK];
	int loop_count = 0;
	int found_matches = 0;

	#define DECK_ARRAYS	(64*64)
  #if 1	// 2022 kriskoin
	#define TEST_DECKS ((1000*1000000)/(CARDS_IN_DECK*CARDS_IN_DECK*CARDS_IN_DECK))
  #else
	#define TEST_DECKS ((400*1000000)/(CARDS_IN_DECK*CARDS_IN_DECK*CARDS_IN_DECK))
  #endif
	Card **saved_decks = (Card **)malloc(DECK_ARRAYS*sizeof(Card *));
	int *saved_deck_counts = (int *)malloc(DECK_ARRAYS*sizeof(int));
	for (int i=0 ; i<DECK_ARRAYS ; i++) {
		saved_deck_counts[i] = 0;
		saved_decks[i] = 0;
	}
  #if 0
	int bytes_to_compare = 4;
  #else
	int bytes_to_compare = CARDS_IN_DECK*sizeof(Card);
  #endif

	Deck *TestDeck = new Deck;
	int highest_count = 0;
	forever {
		loop_count++;
		// we want to track unique shuffles
		int found_match = FALSE;
		TestDeck->CreateDeck();		// initialize all cards
		TestDeck->ShuffleDeck(1);	// shuffle deck once

		// Extract cards into an array we can send to memcmp()
		for (int cc=0; cc < CARDS_IN_DECK; cc++) {
			newdeck[cc] = TestDeck->GetCard(cc);
		}
		
		int saved_deck_index = newdeck[0]+newdeck[1]*CARDS_IN_DECK;
		if (!saved_decks[saved_deck_index]) {
			// Allocate it...
			saved_decks[saved_deck_index] = (Card *)malloc(TEST_DECKS*sizeof(Card)*CARDS_IN_DECK);
			if (!saved_decks[saved_deck_index]) {
				printf("out of memory\n");
				DIE("Out of memory");
			}
		}

		// now see if it matches any current deck
	  #if 0	// 2022 kriskoin
		printf("%s(%d) Saving to saved_deck_index %d (%d + %d*%d)\n",
				_FL, saved_deck_index, newdeck[0], newdeck[1], CARDS_IN_DECK);
	  #endif
		Card *p = saved_decks[saved_deck_index];
		for (int i=0; i < saved_deck_counts[saved_deck_index]; i++, p+=CARDS_IN_DECK) {
			// see if cards are the same
			if (!memcmp(newdeck, p, bytes_to_compare)) {
				// Found a match!
				found_match = TRUE;
				found_matches++;
				printf("Found match at # %d\n", i);
				break;
			}
		}
		if (found_match) {
			// ?
		} else {
			// printf("No match, adding %d\n", deck_index);
			memcpy(p, newdeck, CARDS_IN_DECK*sizeof(Card));
			saved_deck_counts[saved_deck_index]++;
			if (saved_deck_counts[saved_deck_index] >= TEST_DECKS) {	// out of room
				printf("We're out of room for new decks\n");
				break;
			}
			highest_count = max(highest_count, saved_deck_counts[saved_deck_index]);
		}
		if (!(loop_count % 10000)) {
			printf("Tested %7d decks (%2d%%)... matches = %d\n", loop_count, highest_count*100/TEST_DECKS, found_matches);
		}
	}
	delete TestDeck;
#endif	// look for duplicate shuffles
	NOTUSED(argc);
	NOTUSED(argv);
	return 0;
}

#else	// the REAL main()

#define INCL_URL_FETCH 0

#if INCL_URL_FETCH
//*********************************************************
// https://github.com/kriskoin//
// Callback function while writing a url file to disk
// Returns non-zero to cancel.
//
static int WriteFileFromURLCallback(WORD32 additional_bytes_received_and_written)
{
    if (additional_bytes_received_and_written) {
        kp(("%s(%d) %d more bytes received and written\n", _FL, additional_bytes_received_and_written));
    }
	if (iRunLevelDesired < RUNLEVEL_ACCEPT_PLAYERS) {
		return 1;	// cancel
	}
	return 0;	// don't cancel.
}
#endif

#if INCL_SSL_SUPPORT
// This code is automatically generated by the following commands:
//	openssl gendh -rand /dev/random >pokersrv.key 512
//	openssl dh <pokersrv.key -C -noout

static unsigned char dh512_p[]={
        0x95,0x4D,0x91,0x5A,0x83,0x74,0x70,0x96,0x12,0x3D,0x83,0xD2,
        0x49,0xEE,0xAE,0x8B,0x53,0x7A,0x73,0x17,0x58,0x56,0x6F,0xBE,
        0x99,0x7C,0xE0,0x02,0x0D,0x25,0x1F,0xF5,0x23,0xA7,0xC3,0x2C,
        0xBD,0x18,0x98,0x83,0x7D,0xC6,0xA7,0x1A,0xAE,0x3B,0x72,0x0F,
        0x9E,0xFC,0x95,0x8D,0x48,0x54,0xC7,0x55,0x78,0x4C,0xFC,0x0E,
        0xF7,0x1A,0xDF,0x7B,
        };
static unsigned char dh512_g[]={
        0x02,
        };

DH *get_dh512()
        {
        DH *dh;

        if ((dh=DH_new()) == NULL) return(NULL);
        dh->p=BN_bin2bn(dh512_p,sizeof(dh512_p),NULL);
        dh->g=BN_bin2bn(dh512_g,sizeof(dh512_g),NULL);
        if ((dh->p == NULL) || (dh->g == NULL))
                return(NULL);
        return(dh);
        }

//*********************************************************
// https://github.com/kriskoin//
// Thread locking support for open_ssl.
// See openssl-*/doc/crypto/threads.pod for more info.
//
#define MAX_LOCKS_FOR_SSL	32
PPCRITICAL_SECTION SSL_CritSecs[MAX_LOCKS_FOR_SSL];

static void OpenSSL_Locking_Function(int mode, int n, const char *file, int line)
{
	if (mode & CRYPTO_LOCK) {
		//kp(("%s(%d) Entering lock %d for thread %d\n", _FL, n, getpid()));
		PPEnterCriticalSection0(&SSL_CritSecs[n], (char *)file, line, TRUE);
	} else {
		//kp(("%s(%d) Leaving  lock %d for thread %d\n", _FL, n, getpid()));
		LeaveCriticalSection(&SSL_CritSecs[n]);
	}
}

void Initialize_OpenSSL_CritSecs(void)
{
	//kp(("%s(%d) CRYPTO_num_locks() = %d\n", _FL, CRYPTO_num_locks()));
	if (CRYPTO_num_locks() > MAX_LOCKS_FOR_SSL) {
		kp(("%s(%d) CRYPTO_num_locks() returns too many locks! (%d vs. %d)\n", _FL, CRYPTO_num_locks(), MAX_LOCKS_FOR_SSL));
		exit(10);
	}

	for (int i=0 ; i<MAX_LOCKS_FOR_SSL ; i++) {
		PPInitializeCriticalSection(&SSL_CritSecs[i], CRITSECPRI_LOCAL, "SSL");
	}

	CRYPTO_set_locking_callback(OpenSSL_Locking_Function);
}
#endif	// INCL_SSL_SUPPORT

//*********************************************************
// https://github.com/kriskoin//
// main()
//
int main(int argc, char *argv[])
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
	SymFile_ExecutableName = argv[0];
  #endif
  #if INCL_URL_FETCH
	char *fetch_url = NULL;
  #endif
	char *read_ecash_filename = NULL;
	iRunLevelCurrent = iRunLevelDesired = RUNLEVEL_SHUTDOWN;	// set current state.
	int interactive_mode = FALSE;
	//cris 6-2-2003
	OPA * theGlobalOPA = NULL;//DB replicatror
	//end cris 6-2-2003
	CriticalAlertHandler = OurCriticalAlertHandler;

	for (int i=1 ; i<argc ; i++) {
		if (!stricmp(argv[i], "?") || !stricmp(argv[i], "-?")) {
			puts("PokerSrv - command line parameters:");
			puts("  interactive     - don't run as a daemon, run interactively");
			puts("  runninglive     - we're running live... emailing everyone is OK");
			puts("  add_dupe_notes  - add duplicate account notes to each known dup account");
			puts("  ecash [fname]   - read [fname] and add it to c/c database");
		  #if INCL_URL_FETCH
			puts("  geturl [url]    - test only: fetch url and save as testurl.bin");
		  #endif
			exit(0);
	  #if INCL_URL_FETCH
		} else if (!stricmp(argv[i], "geturl")) {
			fetch_url = argv[++i];
	  #endif
		} else if (!stricmp(argv[i], "interactive")) {
			interactive_mode = TRUE;
		} else if (!stricmp(argv[i], "add_dupe_notes")) {
			iAddDupeAccountNotes = TRUE;
		} else if (!stricmp(argv[i], "ecash")) {
			iReadEcashLog = TRUE;
			read_ecash_filename = argv[++i];
		} else if (!stricmp(argv[i], "runninglive")) {
			iRunningLiveFlag = TRUE;
		} else if (!stricmp(argv[i], "hostlookup")) {
			IP_Open();
			char str1[50];
			char str2[50];
			i++;
			IPADDRESS ip = IP_ConvertHostNameToIP(argv[i]);
			IP_ConvertIPtoString(ip, str1, sizeof(str1));
			IP_ConvertIPToHostName(ip, str2, sizeof(str2));

			printf("%s(%d) hostlookup: %s maps to %s which maps to %s\n",
					_FL, argv[i], str1, str2);
			IP_Close();
			exit(0);
	  #if !WIN32	// 2022 kriskoin
		} else if (!stricmp(argv[i], "testcrash")) {
			// Loop forever doing a few things to see if we crash.
			puts("------- crash test mode --------");
			puts("press ctrl+C to terminate.");
			int x = 0;
			forever {
				float la1, la5, la15;
				for (int i=0 ; i<10 ; i++) {
					for (int j=0 ; j<5000 ; j++) {
						x++;
						mallinfo();
						FILE *fd = fopen("/proc/loadavg", "rt");
						if (fd) {
							la1 = la5 = la15 = 0.0;
							fscanf(fd, "%f %f %f", &la1, &la5, &la15);
							fclose(fd);
						}
					}
					Sleep(5);
				}
				printf("\r%d  %.2f  %.2f  %.2f...   ", x, la1, la5, la15);
				fflush(stdout);
			}
			exit(0);
	  #endif

	  #if 0	// 2022 kriskoin
		} else if (!stricmp(argv[i], "testticks")) {
			int j;
			WORD32 oldticks = GetTickCount();
			for (j=0 ; j<20 ; j++) {
				WORD32 newticks = GetTickCount();
				printf("GetTickCount() = %5d (%+4d)\n", newticks, newticks-oldticks);
				oldticks = newticks;
			}
			for (j=0 ; j<20 ; j++) {
				WORD32 newticks = GetTickCount();
				printf("GetTickCount() = %5d (%+4d)\n", newticks, newticks-oldticks);
				oldticks = newticks;
				Sleep(500);
			}
			exit(0);
	  #endif
		} else {
			printf("Unrecognized command line parameter: '%s'.  Ignoring.  Use '?' for help.\n", argv[i]);
		}
	}

  #if WIN32
   #if DEBUG
	kDebWinPrefixString = "Server: ";
   #endif
	kp((ANSI_CLS));
	printf("Poker Server is now running... press ctrl+C to exit.\n");
	printf("See DebWin for additional debug output.\n");
	{
		// Tell DebWin where to find our source files.
		char path[MAX_FNAME_LEN];
		GetDirFromPath(__FILE__, path);
		kAddSourcePath(path);
		kAddLibSourcePath();
	}
	SetConsoleCtrlHandler(OurCtrlHandlerRoutine, TRUE);
  #else
	umask(0117);
	kEchoToStdOut = TRUE;
	kDebWinPrefixString = NULL;
	// Install our signal handler for the signals we trap.
	signal(SIGHUP,  (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGTERM, (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGINT,  (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGSEGV, (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGBUS,  (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGFPE,  (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGQUIT, (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGALRM, (void (*)(int))OurSignalHandlerRoutine);
	signal(SIGPIPE, SIG_IGN);	// ignore broken pipe signals (socket disconnects cause SIGPIPE)
   #if 0	//kriskoin: 	// Set options for malloc()...
	// don't let more than 'n' unused memory go to waste... release it back to OS.
	mallopt(M_TRIM_THRESHOLD, 500000);
   #endif
  #endif // !WIN32

  #if !WIN32
	if (interactive_mode) {
		printf("Poker Server is now running in interactive mode.\n");
		printf("Press ctrl+c to exit.\n");
		kEchoToStdOut = TRUE;
	} else {
		printf("Poker Server is now detaching itself from your console.\n");
		printf("Use 'less +F debwin.log' for additional debug output.\n");
		printf("Use 'killall pokersrv' to stop server.\n");
		daemonize();	// turn ourselves into a daemon
		//kp(("%s(%d) main thread pid = %d, ppid = %d\n", _FL, getpid(), getppid()));
		kEchoToStdOut = FALSE;
	}
	//*(dword *)0x12345678 = 0x55dd44ee;	// force a SEGV for testing.
   #if 0	// force a divide by zero for testing.
	kp(("%s(%d) Calculating 257/%d... does it cause a divide by zero?\n",_FL, interactive_mode));
	int x = 257/interactive_mode;
	kp(("%s(%d) Back from possible divide by zero. Result x = %d\n",_FL,x));
	exit(0);
   #endif
  #endif // !WIN32
	// We must register the main thread AFTER turning ourselves into a daemon
	// or we'll have registered the original pid, rather than our new one.
	RegisterThreadForDumps("Main thread");	// register this thread for stack dumps if we crash

	ChatLog       = new LogFile("Data/Logs/chat.log", 	  NULL, 60);
	ConnectionLog = new LogFile("Data/Logs/connection.log", NULL, 60);

	// Make sure we're the only instance running.
	// This must be done AFTER we become a daemon, otherwise the parent
	// process will own the mutex and then exit.
	Mutex m;
  #if 1	// 2022 kriskoin
//	int result =  m.Open("pokersrv.sem", 0);
	int result =  m.Open("pokersrv",0);
  #else
	int result =  m.Open(argv[0], 0);
  #endif
	if (result) {	// could not open semaphore
		Error(ERR_FATAL_ERROR, "%s(%d) Could not open semaphore (errno=%d). Exiting.", _FL, result);
		exit(10);
	}
	// If we can own it then we're the only instance running.
	result = m.Request(FALSE);
	if (result) {
		Error(ERR_FATAL_ERROR, "%s(%d) Could not own semaphore.  Another instance running? Exiting.", _FL);
		m.Close();
		exit(10);
	}
	//kp(("%s(%d) We now own our semaphore.\n",_FL));

	srand((unsigned)time(NULL));	// init bad random number generator
	RNG_InitializeSeed();			// init our good random number generator

	// Read the .INI file for our default settings...
	PPInitializeCriticalSection(&ParmFileCritSec, CRITSECPRI_PARMFILE, "ParmFile");
	PPInitializeCriticalSection(&SerNumDBCritSec, CRITSECPRI_SERNUMDB, "SerNumDB");
	ReadOurParmFile();			// read the .ini file
	// kriskoin  04/08/2002
	ReadHighHand();
	// end kriskoin 
	ReadServerVars();			// read most recent persistent server vars
	WriteServerVars();			// make sure they're re-written to all locations.
	LoadComputerBlockList();	// read list of computers we block

	NextAnonymousPlayerID = ANONYMOUS_ID_BASE;	

	//	kp(("%s(%d) sizeof(SDBRecord) = %d\n", _FL, sizeof(SDBRecord)));
	//exit(0);

	if (IP_Open())
		DIE("IP_Open() failed.");

#if INCL_SSL_SUPPORT
   #if WIN32
	CRYPTO_malloc_init();
   #endif
	Initialize_OpenSSL_CritSecs();
	SSLeay_add_ssl_algorithms();
    MainSSL_Client_CTX = SSL_CTX_new(SSLv23_client_method());
    if (!MainSSL_Client_CTX) {
        DIE("Could not create MainSSL_Client_CTX");
    }

	/* SSL preliminaries. We keep the certificate and key with the context. */
	SSL_load_error_strings();
	MainSSL_Server_CTX = SSL_CTX_new(SSLv23_server_method());
	if (!MainSSL_Server_CTX) {
        DIE("Could not create MainSSL_Server_CTX");
	}

	// Set the available cipher list:
	//	Default strings from ssl.h:
	//		with RSA: "ALL:!ADH:RC4+RSA:+HIGH:+MEDIUM:+LOW:+SSLv2:+EXP"
	//		w/o  RSA: "HIGH:MEDIUM:LOW:ADH+3DES:ADH+RC4:ADH+DES:+EXP"
	//SSL_CTX_set_cipher_list(MainSSL_Server_CTX, "ALL:ADH:+EDH:+RSA");

	// 24/01/01 kriskoin:
	// client connections indicate that DES is about 2% faster than RC4+MD5
	// using openssl 0.9.4.
	//		RC4-MD5/40:			168K/s
	//		DES-CBC-SHA/40:		175K/s  (weak encryption winner)
	//		DES-CBC3-SHA/192:	153K/s
	//		RC4-MD5/128:		172K/s  (strong encryption winner)
  #if 0	// 2022 kriskoin
  	// Disable DES so regular clients connect with RC4+MD5 instead.  It
	// should use about 1/3rd the CPU time.
	SSL_CTX_set_cipher_list(MainSSL_Server_CTX, "ALL:!DES");
  #else
	SSL_CTX_set_cipher_list(MainSSL_Server_CTX, "ALL");
	//SSL_CTX_set_cipher_list(MainSSL_Server_CTX, "ALL:!MEDIUM:!3DES:!CBC");
  #endif

  #if 1	// set to 1 to include RSA certificate/private key negotiation.
	// ---- Making aDH key/parameters:
	// openssl gendh -rand /dev/random >pokersrv.key 512
	// openssl dh <pokersrv.key -C -noout
	//
	// ---- Making DSA key/cert pair:
	// openssl dsaparam -rand /dev/random 512 >dsa.param
	// openssl gendsa -rand /dev/random dsa.param >pokersrv.key
	// openssl req -config pp.cnf -out pokersrv.cert -key pokersrv.key -days 2500 -new -x509
	// openssl x509 -text <pokersrv.cert|less
	//
	// ---- Making RSA key/cert pair:
	// openssl genrsa -out pokersrv.key 2048
	// openssl req -config pp.cnf -out pokersrv.cert -key pokersrv.key -days 2500 -new -x509
	// openssl x509 -text <pokersrv.cert|less

	if (SSL_CTX_use_certificate_file(MainSSL_Server_CTX, "pokersrv.cert", SSL_FILETYPE_PEM) <= 0) {
		//ERR_print_errors_fp(stderr);
        DIE("Could not find/use certificate file");
	}
	if (SSL_CTX_use_PrivateKey_file(MainSSL_Server_CTX, "pokersrv.key", SSL_FILETYPE_PEM) <= 0) {
		//ERR_print_errors_fp(stderr);
        DIE("Could not find/use key file");
	}
	if (!SSL_CTX_check_private_key(MainSSL_Server_CTX)) {
		DIE("Private key does not match the certificate public key");
	}
  #endif
  #if 1
	// Enable Anonymous DH key exchange (no cert needed)
	SSL_CTX_set_tmp_dh(MainSSL_Server_CTX, get_dh512());
  #endif

	//kp(("%s(%d) Server SSL compression methods = $%08lx\n", _FL, MainSSL_Server_CTX->comp_methods));
#endif	// INCL_SSL_SUPPORT

	SetRunLevelDesired(RUNLEVEL_ACCEPT_PLAYERS, "%s(%d) Main loop coming up.", _FL);

  #if INCL_URL_FETCH
	if (fetch_url) {
		printf("Testing the url fetch function for file '%s' (writing it to testurl.bin)\n", fetch_url);
		ErrorType err = WriteFileFromUrlUsingSockets(fetch_url, "testurl.bin", WriteFileFromURLCallback, 0
              #if INCL_SSL_SUPPORT
        	    , MainSSL_Client_CTX
              #endif
        );
		kp(("%s(%d) WriteFileFromUrlUsingSockets() returned %d\n", _FL, err));
		SetRunLevelDesired(RUNLEVEL_EXIT, "URL fetch complete.");
	}
  #endif

	//kwrites("this is a test to see what happens with a fairly long line.\n\nit contains \\n's, but it's also very long.  we'll keep writing until we get to a fairly high column and then see what happens when we go to print this thing using kwrites().\n");
	kp(("%s(%d) Using transaction server '%s' (see ecash.h)\n", _FL, ECASH_SERVER_TYPE));
  #if USE_TEST_SERVER
  	// Using the test server...
	if (iRunningLiveFlag) {
		Error(ERR_FATAL_ERROR, "%s(%d) !!!ERROR!!! USING TEST SFC SERVER INSTEAD OF REAL ONE!",_FL);
		//exit(10);
	}
  #else	// real server...
	if (!iRunningLiveFlag) {
		kp((ANSI_ERROR"%s(%d) WARNING: USING REAL SFC SERVER!\n",_FL));
	}
  #endif
		//cris 6-2-2004
		theGlobalOPA = new OPA("OPAMainLog");
	    
		if (!theGlobalOPA) {
			Error(ERR_FATAL_ERROR, "%s(%d) theGlobalOPA construction failed.",_FL);
			exit(10);
		}
		theGlobalOPA->CreateThread();
		//end cris 6-2-2004
	
		
	while (iRunLevelDesired >= RUNLEVEL_SHUTDOWN) {
		if (iRunLevelDesired >= RUNLEVEL_ACCEPT_CONNECTIONS) {
			// Fire up the logging
//			PL = new PokerLog(HAND_LOGFILE_EXTENSION, AUDIT_LOGFILE_EXTENSION);
		
			
			PL = new PokerLog(HAND_LOGFILE_EXTENSION,theGlobalOPA);
			if (!PL) {
				Error(ERR_FATAL_ERROR, "%s(%d) PL construction failed.",_FL);
				exit(10);
			}
			

			ErrorType err;
			// SDB is declared as a global above...
		  #if 0 //kriskoin: 			#define DATABASE_TEST_SIZE	2000
			kp(("%s(%d) %s Opening database with %d records...\n",_FL,TimeStr(),DATABASE_TEST_SIZE));
			SDB = new SimpleDataBase(DATABASE_NAME, DATABASE_TEST_SIZE,theGlobalOPA);
//			SDB = new SimpleDataBase(DATABASE_NAME, DATABASE_TEST_SIZE,CardRoomPtr);
			kp(("%s(%d) %s Creating %d new accounts...\n", _FL, TimeStr(), DATABASE_TEST_SIZE));
			for (int x=0 ; x<DATABASE_TEST_SIZE ; x++) {
				char str[20];
				sprintf(str, "test %d account", x);
				WORD32 player_id = SDB->CreateNewRecord(str);
				if (!(x%500)) {
					kp(("%s(%d) %s New player_id for user '%s' is $%08lx\n", _FL, TimeStr(), str, player_id));
				}
				if (!player_id) {
					kp(("%s(%d) Failed to create an account for '%s'.  Database full?  Aborting.\n", _FL, str));
					break;
				}
				if (iRunLevelDesired < RUNLEVEL_ACCEPT_CONNECTIONS) {
					break;
				}
			}
			kp(("%s(%d) %s done creating new accounts.\n", _FL, TimeStr()));
		  #else
		  	if (DatabaseRecordCount <= 0) {
				Error(ERR_FATAL_ERROR, "%s(%d) Illegal DatabaseRecordCount! (%d)",
						_FL, DatabaseRecordCount);
				exit(10);
		  	}
			SDB = new SimpleDataBase(DATABASE_NAME, DatabaseRecordCount,theGlobalOPA);

		  #endif
			if (!SDB) {
				Error(ERR_FATAL_ERROR, "%s(%d) SDB construction failed.",_FL);
				exit(10);
			}

			CardRoomPtr = new CardRoom(theGlobalOPA);
			if (!CardRoomPtr) {
				Error(ERR_FATAL_ERROR, "%s(%d) CardRoom construction failed.",_FL);
				exit(10);
			}
			
			PL->LogComment("Cardroom is initializing...");
			CardRoomPtr->SetNextGameSerialNumber(PL->GetNextGameSerialNumber());

			// Load computer serial number database.
			SerNumDB_Load();	
			if (iAddDupeAccountNotes) {
				SerNumDB_AddDupeAccountNotes();
				kp(("%s(%d) Done add dup account notes.  Exiting.\n", _FL));
				while (!EmailQueueEmpty()) {
					Sleep(100);
				}
				exit(10);
			}

			CCDB_Load(); // load the credit card database
			// Initialize ecash database by reading an ecash.log file
			if (iReadEcashLog) {
				ReadEcashLogForCCDB(read_ecash_filename);
				kp(("%s(%d) Done reading %s...  Exiting.\n", _FL, read_ecash_filename));
				exit(11);
			}

			// Launch the player data output thread(s)...
			PlrOut_LaunchThreads();
			Sleep(100);

			PL->LaunchHandReqThread();	// start the Hand Request Handler thread
			Sleep(100);

			// Launch ecash handler thread
			LaunchEcashHandlerThread();
			Sleep(100);


			err = CardRoomPtr->MainLoop();
// ricardoGANG
          SDB->PrintRecord(1) ;
// ricardoGANG

			if (iRunLevelCurrent > RUNLEVEL_SHUTDOWN) {
				iRunLevelCurrent = RUNLEVEL_SHUTDOWN;
			}


			// shutdown ecash
			ShutdownEcashProcessing();


			PL->LogComment("Cardroom has shut down");
			PL->SetNextGameSerialNumber(CardRoomPtr->GetNextGameSerialNumber());
			// remove cardroom
			delete CardRoomPtr;
			CardRoomPtr = NULL;
			// shutdown database
			SDB->ShutDown();
			delete SDB;
			SDB = NULL;
			// shutdown logging
			PL->ShutDown();
			delete PL;
			PL = NULL;
			
		}
		Sleep(200);
	}
	iRunLevelCurrent = RUNLEVEL_EXIT;
	// 24/01/01 kriskoin:
	WriteServerVars();
	
		theGlobalOPA->ShutDown();
		if (theGlobalOPA) {
			delete (theGlobalOPA); 
		}
		theGlobalOPA = NULL;
	
  #if INCL_SSL_SUPPORT
    // Close up openssl if open.
	if (MainSSL_Client_CTX != NULL) {
    	SSL_CTX_free(MainSSL_Client_CTX);
        MainSSL_Client_CTX = NULL;
	}
	if (MainSSL_Server_CTX != NULL) {
    	SSL_CTX_free(MainSSL_Server_CTX);
        MainSSL_Server_CTX = NULL;
	}
  #endif

	pr(("%s(%d) Calling IP_Close()\n",_FL));
	IP_Close();
	PPDeleteCriticalSection(&ParmFileCritSec);

	kp(("--- Server is now exiting ---\n"));
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
	m.Release();
	m.Close(TRUE);

	while (!EmailQueueEmpty()) {
		Sleep(100);
	}

	delete ChatLog;
	ChatLog = NULL;
	delete ConnectionLog;
	ConnectionLog = NULL;

	UnRegisterThreadForDumps();

	if (ShotClockFlags & SCUF_SHUTDOWN_AUTO_INSTALLNEW) {
		kp(("%s **** Executing auto_install_new_pokersrv before exiting ****\n",TimeStr()));
		system("auto_install_new_pokersrv&");
	}

	return 0;
}

#endif	// Card testing stuff -- remove eventually.....

//*********************************************************
// https://github.com/kriskoin//
// Build a string of user id's from an array of player ID's
// (for display).  String should be at least count*20 long.
//
void BuildUserIDString(WORD32 *player_id_array, int count, char *output_str)
{
    output_str[0] = 0;
	for (int i=0 ; i<count ; i++) {
		if (player_id_array[i]) {
			SDBRecord player_rec;	// the result structure
			zstruct(player_rec);
			if (SDB->SearchDataBaseByPlayerID(player_id_array[i], &player_rec) >= 0) {
				if (output_str[0]) {	// first one?
					// nope, add a comma
					strcat(output_str, " , ");
				}
				strcat(output_str, player_rec.user_id);
			}
		}
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Retrieve a single user id string
//
void BuildUserIDString(WORD32 player_id, char *output_str)
{
	BuildUserIDString(&player_id, 1, output_str);
}

//*********************************************************
// https://github.com/kriskoin//
// Computer Serial Number Database functions.
//
#define SERNUMDB_FNAME	"ser_num.bin"
#define SERNUMDB_GROW_RATE				50	// add n entries to DB at a time

static Array SerNumDB;
PPCRITICAL_SECTION SerNumDBCritSec;	// CritSec to control access to Parm file vars

//*********************************************************
// https://github.com/kriskoin//
// Load the serial number database (if available)
//
void SerNumDB_Load(void)
{
	static int initialized = FALSE;
	if (!initialized) {
		initialized = TRUE;
		SerNumDB.SetParms(sizeof(struct SerNumDB_Entry), sizeof(WORD32), SERNUMDB_GROW_RATE);
	}
	if (SerNumDB.base) {
		return;	// already loaded... don't do it again.
	}

	// Try to load it from disk...
	EnterCriticalSection(&SerNumDBCritSec);
    SerNumDB.LoadFile(SERNUMDB_FNAME);
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Save the serial number database if necessary.
//
void SerNumDB_SaveIfNecessary(void)
{
	EnterCriticalSection(&SerNumDBCritSec);
	SerNumDB.WriteFile(SERNUMDB_FNAME);
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Return a pointer to the entry for a particular computer
// serial number.  Add an entry if necessary.
// Returns ptr to entry or NULL if there was a problem.
//
struct SerNumDB_Entry *SerNumDB_GetSerNumEntry(WORD32 computer_serial_num)
{
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = (struct SerNumDB_Entry *)SerNumDB.Find(&computer_serial_num);
    if (e) {    // found it...
    	LeaveCriticalSection(&SerNumDBCritSec);
        return e;
    }

    // Add a new entry...
    struct SerNumDB_Entry n;
    zstruct(n);
    n.computer_serial_num = computer_serial_num;
	e = (struct SerNumDB_Entry *)SerNumDB.Add(&n);
	LeaveCriticalSection(&SerNumDBCritSec);
	return e;
}

//*********************************************************
// https://github.com/kriskoin//
// Build a string of user id's associated with a computer serial num
// (for display).  String should be at least 300 long.
//
void SerNumDB_BuildUserIDString(WORD32 computer_serial_num, char *str)
{
    str[0] = 0;
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = SerNumDB_GetSerNumEntry(computer_serial_num);
	if (e) {
        BuildUserIDString(e->player_ids, SERNUMDB_PLAYER_IDS_TO_RECORD, str);
	}
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Review a computer key database entry and issue dup account
// alerts if necessary.
//
void SerNumDB_CheckForDupes(struct SerNumDB_Entry *e, int add_admin_notes_flag, int send_alerts_flag)
{
	EnterCriticalSection(&SerNumDBCritSec);
	// Count how many player id's are associated with this key...
	int count = 0;
	for (int i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
		if (e->player_ids[i]) {
			count++;
		}
	}
	if (count > 1) {	// Issue a warning...
		// Build a string containing the list of accounts...
		char accounts[300];
		char accounts2[800];
		zstruct(accounts);
		zstruct(accounts2);
		int real_money_count = 0;
        int total_balance = 0;
        int total_exposure = 0;
		for (int i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
			if (e->player_ids[i]) {
				SDBRecord player_rec;	// the result structure
				zstruct(player_rec);
				if (SDB->SearchDataBaseByPlayerID(e->player_ids[i], &player_rec) >= 0) {
					if (accounts[0]) {	// first one?
						// nope, add a comma
						strcat(accounts, " , ");
					}
					strcat(accounts, player_rec.user_id);
					int real_total = 0;

					// If this account is locked out, it's already been dealt
					// with and therefore we don't need to issue a warning
					// due to too many real money accounts.
					if (player_rec.flags & (SDBRECORD_FLAG_LOCKED_OUT|SDBRECORD_FLAG_DUPES_OK)) {
						// Don't count this as a dupe...
					} else {
						// We should count this as a dupe...
						if (player_rec.priv >= ACCPRIV_REAL_MONEY) {
							real_money_count++;
						}
						real_total = player_rec.real_in_bank +
									 player_rec.real_in_play +
									 player_rec.pending_check;
					}
                    total_balance += real_total;

					// Calculate our exposure on this account
					int exposure = 0;
					for (int i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
						if (player_rec.transaction[i].transaction_type == CTT_PURCHASE) {
							exposure += player_rec.transaction[i].credit_left;
						}
					}
                    total_exposure += exposure;

					char cs1[MAX_CURRENCY_STRING_LEN];
					char cs2[MAX_CURRENCY_STRING_LEN];
					sprintf(accounts2+strlen(accounts2),
							"%s Current=%s, Exposure=%s, played %d hands, %.0f%% flops, \"%s\",  %s\n",
							DateStr(player_rec.account_creation_time, TRUE, TRUE, SERVER_TIMEZONE),
							CurrencyString(cs1, real_total, CT_REAL),
							CurrencyString(cs2, exposure, CT_REAL),
							player_rec.hands_seen,
							player_rec.flops_seen*100.0/(double)max(1,player_rec.hands_seen),
							player_rec.full_name,
							player_rec.user_id);
				}
			}
		}

//		kp(("%s(%d) rm_count=%d for %s (send_alerts_flag=%d)\n",_FL,real_money_count,accounts,send_alerts_flag));
		if (send_alerts_flag) {
			// Only send an alert if at least two of the accounts are real money
			if (real_money_count > 1) {
				SendAdminAlert(ALERT_6,
					"Dupe real accounts for #%5d: %s", e->computer_serial_num, accounts);

				char subject[300];
				zstruct(subject);
                char cs1[MAX_CURRENCY_STRING_LEN];
                char cs2[MAX_CURRENCY_STRING_LEN];
                char cs3[MAX_CURRENCY_STRING_LEN];
                zstruct(cs1);
                zstruct(cs2);
                zstruct(cs3);
				sprintf(subject, "Dupe accnts: %s", accounts);
				EmailStr(
					"alerts@kkrekop.io", 	// to:
					"PokerSrv",						// From (name):
					"alerts@kkrekop.io",		// From (email):
					subject,						// Subject:
					NULL,							// bcc:
					"%s"
					"%s\n\n"
					"Computer serial number %d has these %d accounts:\n\n"
					"%s"
                    "\n"
					"Total balances = %s, total exposure = %s, net exposure = %s (%s)\n",
					iRunningLiveFlag ? "" : "*** This is a test ***\n\n",
					TimeStrWithYear(),
					e->computer_serial_num,
					count,
					accounts2,
					CurrencyString(cs1, total_balance, CT_REAL),
					CurrencyString(cs2, total_exposure, CT_REAL),
					CurrencyString(cs3, total_exposure - total_balance, CT_REAL),
					total_exposure - total_balance < 0 ? "surplus" : "at risk");
			} else {
				SendAdminAlert(ALERT_2,
					"Dupe play accounts for #%5d: %s", e->computer_serial_num, accounts);
			}
		}

		if (add_admin_notes_flag) {
			// Add an admin note to each account.
			for (int i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
				if (e->player_ids[i]) {
					SDB->AddAccountNote(e->player_ids[i], "%s Dupes: %s",
							DateStrWithYear(), accounts);
				}
			}
		}
	}
	LeaveCriticalSection(&SerNumDBCritSec);
}

void SerNumDB_CheckForDupes(WORD32 computer_serial_num, int add_admin_notes_flag, int send_alerts_flag)
{
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = SerNumDB_GetSerNumEntry(computer_serial_num);
	if (e) {
		SerNumDB_CheckForDupes(e, add_admin_notes_flag, send_alerts_flag);
	}
	LeaveCriticalSection(&SerNumDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Loop through the entire key database and add admin notes
// to each account which shares a key with another account.
//
void SerNumDB_AddDupeAccountNotes(void)
{
	EnterCriticalSection(&CardRoomPtr->CardRoomCritSec);
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = (struct SerNumDB_Entry *)SerNumDB.base;
	for (int i=0 ; i<SerNumDB.member_count ; i++, e++) {
		if (e->computer_serial_num) {
			SerNumDB_CheckForDupes(e, TRUE, TRUE);
		}
	}
	LeaveCriticalSection(&SerNumDBCritSec);
	LeaveCriticalSection(&CardRoomPtr->CardRoomCritSec);
}	

//*********************************************************
// https://github.com/kriskoin//
// Add a player ID to the computer serial number database.
// Issue warnings if necessary.
//
void SerNumDB_AddPlayerID(WORD32 computer_serial_num, WORD32 player_id)
{
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = SerNumDB_GetSerNumEntry(computer_serial_num);
	if (e) {
		// Scan through and see if this player id is already in the
		// list for this computer serial number...
		int i;
		for (i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
			if (e->player_ids[i]==player_id) {
				// It's already there... no more work to do.
				LeaveCriticalSection(&SerNumDBCritSec);
				return;
			}
		}
		// It's not there... add it in first spot and shift list...
		memmove(&e->player_ids[1], &e->player_ids[0], sizeof(e->player_ids[0])*(SERNUMDB_PLAYER_IDS_TO_RECORD-1));
		e->player_ids[0] = player_id;
		SerNumDB.modified = TRUE;

		// Now count how many player id's are associated with this key...
		int count = 0;
		for (i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
			if (e->player_ids[i]) {
				count++;
			}
		}
		if (count > 1) {	// Issue a warning...
			SerNumDB_CheckForDupes(e, TRUE, TRUE);
		}
	}
	LeaveCriticalSection(&SerNumDBCritSec);
	SerNumDB_SaveIfNecessary();
}

//*********************************************************
// https://github.com/kriskoin//
// Insert up to one blank player id for a serial number.
// This allows them to create new accounts.
//
void SerNumDB_InsertBlankPlayerID(WORD32 computer_serial_num)
{
	EnterCriticalSection(&SerNumDBCritSec);
	struct SerNumDB_Entry *e = SerNumDB_GetSerNumEntry(computer_serial_num);
	if (e) {
		if (e->player_ids[0]) {
			// it's not blank... scroll it up and make it blank.
			memmove(&e->player_ids[1], &e->player_ids[0], sizeof(e->player_ids[0])*(SERNUMDB_PLAYER_IDS_TO_RECORD-1));
			e->player_ids[0] = 0;
			SerNumDB.modified = TRUE;
		}
		if (DebugFilterLevel <= 2) {
			kp(("%s(%d) The player_id list for computer s/n %d now starts with a blank player id entry\n",_FL,computer_serial_num));
		}
	}
	LeaveCriticalSection(&SerNumDBCritSec);
	SerNumDB_SaveIfNecessary();
}


// Maintain a seperate CritSec for each thread so we can block them if we need to
// but normally they run completely on their own.
PPCRITICAL_SECTION PlrOut_Thread_CritSecs[NUMBER_OF_PLAYER_OUTPUT_THREADS];
PPCRITICAL_SECTION PlrOut_Queue_CritSecs[NUMBER_OF_PLAYER_OUTPUT_THREADS];	// CritSec to control access to each PlrOut queue
volatile int iPlrOutQueueLens[NUMBER_OF_PLAYER_OUTPUT_THREADS];				// current depth of the player data output queue

#if 0	// 2022 kriskoin
struct PlrOutQueueEntry {
	struct PlrOutQueueEntry *next;	//** This must be first! **
	class Packet *pkt;		// the packet that needs to be sent (allocated by the packet pool)
	class Player *plr;		// the player object the packet needs to be sent to.
};

struct PlrOutQueueEntry *PlrOutQueueHeads[NUMBER_OF_PLAYER_OUTPUT_THREADS];	// head of the data output queue
struct PlrOutQueueEntry *PlrOutQueueTails[NUMBER_OF_PLAYER_OUTPUT_THREADS];	// tail of the data output queue
#else
struct PlrOutQueueEntry {
	class Packet *pkt;		// the packet that needs to be sent (allocated by the packet pool)
	class Player *plr;		// the player object the packet needs to be sent to.
};

struct PlrOutQueueList {
	struct PlrOutQueueEntry *array;	// ptr to base of array
	int array_size;					// # of items array is currently allocated for
	int head;						// index of head (if == tail then array is empty)
	int tail;						// index of tail
} PlrOutQueueList[NUMBER_OF_PLAYER_OUTPUT_THREADS];
#endif

class Player *PlrOut_SendPendingLists[NUMBER_OF_PLAYER_OUTPUT_THREADS][MAX_SERVICEABLE_PLAYERS];
volatile int iPlrOut_SendPendingMaxIndex[NUMBER_OF_PLAYER_OUTPUT_THREADS];

#define COUNT_QUEUE_LEN	0

//*********************************************************
// https://github.com/kriskoin//
// Non-class _cdecl function for _beginthread to call.
//
void _cdecl PlrOut_ThreadEntry(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Data output thread");	// register this thread for stack dumps if we crash
	//---
	int output_thread_num = (int)args;
	struct PlrOutQueueList *ql = &PlrOutQueueList[output_thread_num];

	kp1(("%s(%d) Note: PlrOut_ThreadEntry() could be made more efficient by processing entire queue before looping through players again.\n", _FL));
	while (iRunLevelCurrent > RUNLEVEL_SHUTDOWN || iRunLevelDesired >= RUNLEVEL_ACCEPT_PLAYERS) {
		// Loop constantly as long as there is data in the queue to be sent.
	  #if 0	//kriskoin: 		kp1(("%s(%d) *** WARNING: PUTTING LARGE DELAYS IN THE OUTPUT THREADS!!!\n", _FL));
		if (!iRunningLiveFlag && iPlrOutQueueLens[output_thread_num] < 500) {
			Sleep(50);
		}
	  #endif
		EnterCriticalSection(&PlrOut_Thread_CritSecs[output_thread_num]);

		// Go through any player objects that still had pending output data in
		// their queues the last time we serviced them.
		int new_max = 0;
		WORD32 now = GetTickCount();
		for (int i=0 ; i<iPlrOut_SendPendingMaxIndex[output_thread_num] ; i++) {
			pr(("%s(%d) Checking player ptr #%d ($%08lx) to see if it's output queue needs servicing\n",
					_FL, i, PlrOut_SendPendingLists[output_thread_num][i]));
			class Player *plr = PlrOut_SendPendingLists[output_thread_num][i];
			if (plr) {
				if (now >= plr->next_send_attempt_ms) {
					// Try updating this send queue...
					EnterCriticalSection(&plr->PlayerCritSec);
					if (plr->server_socket) {
						ErrorType err = plr->server_socket->ProcessSendQueue();
						plr->next_send_attempt_ms = plr->server_socket->next_send_attempt_ms;
						if (err == ERR_WARNING) {
							// Queue could not be emptied for this player object...
							// keep trying to process it.
							// Leave it in the list.
							pr(("%s %-14.14s %s(%d) Warning: send queue could not be emptied for player $%08lx. Leaving in list.\n",
									TimeStr(), plr->ip_str, _FL, plr->player_id));
							new_max = i+1;
						} else {
							// The send queue is now empty for this player.
							PlrOut_SendPendingLists[output_thread_num][i] = NULL;	// zero out the ptr
						}
					} else {
						// No server socket... assume it's an empty queue.
						PlrOut_SendPendingLists[output_thread_num][i] = NULL;	// zero out the ptr
					}
					LeaveCriticalSection(&plr->PlayerCritSec);
				} else {
					new_max = i+1;	// leave it in the queue.
				}
			}
 		}
		iPlrOut_SendPendingMaxIndex[output_thread_num] = new_max;

		// Pop the top entry off the queue
	#if 0	// 2022 kriskoin
		EnterCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
		struct PlrOutQueueEntry *p = PlrOutQueueHeads[output_thread_num];

	  #if COUNT_QUEUE_LEN
		kp1(("%s(%d) **** PERFORMANCE WARNING: counting queue length when sending packets!\n", _FL));
		int counted_length = 0;
		while (p) {
			counted_length++;
			p = p->next;
		}
		if (counted_length != iPlrOutQueueLens[output_thread_num]) {
			static int printed_count;
			if (printed_count < 100) {
				printed_count++;
				kp(("%s %s(%d) WARNING: output queue length is incorrect before sending. counted len=%d, iQueuelens[%d]=%d\n",
							TimeStr(), _FL, counted_length,
							output_thread_num, iPlrOutQueueLens[output_thread_num]));
			}
			iPlrOutQueueLens[output_thread_num] = counted_length;	// fix it.
		}
		p = PlrOutQueueHeads[output_thread_num];
	  #endif

		if (p) {	// is there something in the queue?
			PlrOutQueueHeads[output_thread_num] = p->next;	// move head to the next item.
			iPlrOutQueueLens[output_thread_num]--;			// another item popped.
			p->next = NULL;									// this one is no longer in the queue

			// If this was also the tail, it needs to be cleared.
			if (PlrOutQueueTails[output_thread_num] == p) {
				PlrOutQueueTails[output_thread_num] = NULL;
			}
		} else {
			// The queue was empty... make sure the queue length counter
			// is correct (it used to leak).
			//kriskoin: 			if (iPlrOutQueueLens[output_thread_num]) {
				static int warning_print_count;
				if (warning_print_count < 100) {
					warning_print_count++;
					kp(("%s %s(%d) Warning: output queue #%d's length is %d, but it's empty!\n",
								TimeStr(), _FL, output_thread_num, 
								iPlrOutQueueLens[output_thread_num]));
					iPlrOutQueueLens[output_thread_num] = 0;	// reset it.
				}
			}
		}
		LeaveCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
	#else	// array-based list handling
		EnterCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
		struct PlrOutQueueEntry *p, qe;
		if (ql->head != ql->tail) {
			// There's something in the queue... pop the first item.
			qe = ql->array[ql->head++];
			if (ql->head >= ql->array_size) {
				ql->head = 0;	// start over at beginning of array.
			}
			iPlrOutQueueLens[output_thread_num]--;			// another item popped.
			p = &qe;	// hack to make old code work with new method. Below code should be updated.
		} else {
			p = NULL;	// queue is empty.  No packet to deal with.
			// The queue was empty... make sure the queue length counter
			// is correct (it used to leak).
			//kriskoin: 			if (iPlrOutQueueLens[output_thread_num]) {
				static int warning_print_count;
				if (warning_print_count < 100) {
					warning_print_count++;
					kp(("%s %s(%d) Warning: output queue #%d's length is %d, but it's empty!\n",
								TimeStr(), _FL, output_thread_num, 
								iPlrOutQueueLens[output_thread_num]));
					iPlrOutQueueLens[output_thread_num] = 0;	// reset it.
				}
			}
		}
		LeaveCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
	#endif
		if (p) {
			// Process it...
			if (p->plr && p->plr->server_socket) {
				WORD32 bytes_after_compression = 0;
				EnterCriticalSection(&p->plr->PlayerCritSec);
				if (p->plr->server_socket) {

					// Change compression levels if requested.
					if (p->plr->server_socket->compression_level != CompressionLevel) {
						p->plr->server_socket->SetCompressionLevel(CompressionLevel);
					}

					p->plr->server_socket->SendPacket(&(p->pkt), &bytes_after_compression);
					ErrorType err = p->plr->server_socket->ProcessSendQueue();
					if (err == ERR_WARNING) {
						// Queue could not be emptied for this player object...
						// keep trying to process it.
						pr(("%s %-14.14s %s(%d) Warning: send queue could not be emptied for player $%08lx\n",
								TimeStr(), p->plr->ip_str, _FL, p->plr->player_id));
						// Search for an empty spot in the send queue pending list...
						for (int i=0 ; i<MAX_SERVICEABLE_PLAYERS ; i++) {
							if (!PlrOut_SendPendingLists[output_thread_num][i]) {
								pr(("%s(%d) Putting player pointer into slot %d\n", _FL, i));
								PlrOut_SendPendingLists[output_thread_num][i] = p->plr;
								if (i >= iPlrOut_SendPendingMaxIndex[output_thread_num]) {
									iPlrOut_SendPendingMaxIndex[output_thread_num] = i+1;
								}
								break;
							}
						}
					}
				}
				if (p->pkt) {
					// It didn't get cleared by SendPacket(), therefore there must have been
					// some sort of error and we need to requeue it.
					if (p->plr->Connected()) {
						kp1(("%s(%d) !!!! UNFINISHED !!!!: packet not requeued by SendPacket().\n", _FL));
					}
					delete p->pkt;
					p->pkt = NULL;
				}
				LeaveCriticalSection(&p->plr->PlayerCritSec);
			}
			// Release this thread's critsec (not the queue critsec)
			LeaveCriticalSection(&PlrOut_Thread_CritSecs[output_thread_num]);
			//kriskoin: 			// sure to free it.
			if (p->pkt) {
				delete p->pkt;
				p->pkt = NULL;
			}
		  #if 0	// 2022 kriskoin
			free(p);
		  #endif
		} else {
			// Nothing in queue... sleep for a bit.
			// Release this thread's critsec (not the queue critsec)
			LeaveCriticalSection(&PlrOut_Thread_CritSecs[output_thread_num]);
			Sleep(20);
		}
	}
  #if !WIN32
	kp(("%s %s(%d) Data output thread #%d is exiting.\n", TimeStr(), _FL, output_thread_num));
  #endif
	//---
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
	NOTUSED(args);
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Add a packet to the player data output queue for a particular player
// *pkt gets cleared if the packet was successfully queued.
// This function is usually called directly by the player object
// and the player object's critical section is probably already owned
// by this thread.
//
void PlrOut_QueuePacket(class Player *plr, class Packet **pkt)
{
  #if 0	//kriskoin: 	// Create a structure entry to hold the info about this queued packet.
	struct PlrOutQueueEntry *p = (struct PlrOutQueueEntry *)malloc(sizeof(*p));
	if (p) {
		zstruct(*p);
		p->plr = plr;
		p->pkt = *pkt;

		// Now add it to the queue...
		unsigned int output_thread_num = plr->serial_num % NUMBER_OF_PLAYER_OUTPUT_THREADS;
		EnterCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);

	  #if COUNT_QUEUE_LEN
	  	{
			kp1(("%s(%d) **** PERFORMANCE WARNING: counting queue length before queueing packets!\n", _FL));
			int counted_length = 0;
			struct PlrOutQueueEntry *p = PlrOutQueueHeads[output_thread_num];
			while (p) {
				counted_length++;
				p = p->next;
			}
			if (counted_length != iPlrOutQueueLens[output_thread_num]) {
				static int printed_count;
				if (printed_count < 100) {
					printed_count++;
					kp(("%s %s(%d) WARNING: output queue length is incorrect BEFORE queueing. counted len=%d, iQueuelens[%d]=%d\n",
								TimeStr(), _FL, counted_length,
								output_thread_num, iPlrOutQueueLens[output_thread_num]));
				}
				iPlrOutQueueLens[output_thread_num] = counted_length;	// fix it.
			}
		}
		struct PlrOutQueueEntry *original_head = PlrOutQueueHeads[output_thread_num];
		struct PlrOutQueueEntry *original_tail = PlrOutQueueTails[output_thread_num];
	  #endif

		if (PlrOutQueueTails[output_thread_num] == NULL) {
			// There is nothing at the end, therefore the head should also be null.
		  #if COUNT_QUEUE_LEN
			if (PlrOutQueueHeads[output_thread_num]) {
				kp(("%s %s(%d) Warning: PlrOutQueueHeads[%d] is $%08lx but should be null!\n",
							TimeStr(), _FL, output_thread_num, PlrOutQueueHeads[output_thread_num]));
			}
		  #endif
			PlrOutQueueHeads[output_thread_num] = p;
		} else {
			PlrOutQueueTails[output_thread_num]->next = p;
		}
		PlrOutQueueTails[output_thread_num] = p;
		iPlrOutQueueLens[output_thread_num]++;		// another item added

	  #if COUNT_QUEUE_LEN
		kp1(("%s(%d) **** PERFORMANCE WARNING: counting queue length when queueing packets!\n", _FL));
		int counted_length = 0;
		p = PlrOutQueueHeads[output_thread_num];
		while (p) {
			counted_length++;
			p = p->next;
		}
		if (counted_length != iPlrOutQueueLens[output_thread_num]) {
			static int printed_count;
			if (printed_count < 100) {
				printed_count++;
				kp(("%s %s(%d) WARNING: output queue length is incorrect after queueing. counted len=%d, iQueuelens[%d]=%d\n",
							TimeStr(), _FL, counted_length,
							output_thread_num, iPlrOutQueueLens[output_thread_num]));
				kp(("%s %s(%d)          original_head = $%08lx  new head = $%08lx\n", TimeStr(), _FL, original_head, PlrOutQueueHeads[output_thread_num]));
				kp(("%s %s(%d)          original_tail = $%08lx  new tail = $%08lx\n", TimeStr(), _FL, original_tail, PlrOutQueueTails[output_thread_num]));
			}
			iPlrOutQueueLens[output_thread_num] = counted_length;	// fix it.
		}
	  #endif

		LeaveCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
		*pkt = NULL;	// we dealt with it.  Caller no longer has access.
	}
  #else	// resizable array based method

	unsigned int output_thread_num = plr->serial_num % NUMBER_OF_PLAYER_OUTPUT_THREADS;
	struct PlrOutQueueList *ql = &PlrOutQueueList[output_thread_num];

	// Make sure there's room in the array.
	EnterCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
	int new_tail = ql->tail + 1;
	if (new_tail >= ql->array_size) {
		new_tail = 0;
	}
	if (new_tail == ql->head) {	// full (or new array)?
		// It's time to allocate some more space.
		int new_array_size = max(100, (ql->array_size*3)/2);
	  #if 1	//kriskoin: 		if (ql->array_size) {
			kp(("%s %s(%d) Output thread %d: re-allocating array from %3d to %3d elements.\n",
					TimeStr(), _FL, output_thread_num, ql->array_size, new_array_size));
		}
	  #endif
		struct PlrOutQueueEntry *qa = (struct PlrOutQueueEntry *)malloc(sizeof(struct PlrOutQueueEntry)*new_array_size);
		if (qa) {
			if (ql->array) {	// copy the old data
				// performance note: memcpy() is the function which does NOT check for overlap.
				if (ql->tail > ql->head) {	// tail is past head in the array?
					// queue is in one piece somewhere in the middle of the array
					int entries = ql->tail - ql->head;	// # of entries currently in the array
					memcpy(
						qa,						// dest: start of new array
						ql->array+ql->head,		// src: existing head in array
						sizeof(struct PlrOutQueueEntry)*entries);
					ql->tail = entries;
				} else {
					// queue is in two pieces: head to end, and beginning to tail
					int part1 = ql->array_size - ql->head;
					int part2 = ql->tail;
					memcpy(
						qa,						// dest: start of new array
						ql->array+ql->head,		// src: existing head in array
						sizeof(struct PlrOutQueueEntry)*part1);
					memcpy(
						qa+part1,				// dest: right after part1
						ql->array,				// src: from base of existing array
						sizeof(struct PlrOutQueueEntry)*part2);
					ql->tail = part1+part2;
				}
				ql->head = 0;
				free(ql->array);
			}
			ql->array = qa;
			ql->array_size = new_array_size;
			new_tail = ql->tail + 1;
		}
	}
	// Finally, store the new data into the array.
	ql->array[ql->tail].pkt = *pkt;
	ql->array[ql->tail].plr = plr;
	ql->tail = new_tail;			// update tail.
	iPlrOutQueueLens[output_thread_num]++;		// another item added
	pr(("%s(%d) Added packet: thread=%d, head = %2d tail = %2d array_size = %2d\n",
			_FL, output_thread_num, ql->head, ql->tail, ql->array_size));
	LeaveCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
	*pkt = NULL;	// we dealt with it.  Caller no longer has access.
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Remove any packets which have been queued for a particular
// player object (probably because it's about to be deleted
// by the cardroom).
//
void PlrOut_RemovePacketsForPlayer(class Player *plr)
{
	// The ENTIRE queue and each send thread must be locked for this
	// function to perform.  We must be certain that there are no lingering
	// references to this player object.
	unsigned int output_thread_num = plr->serial_num % NUMBER_OF_PLAYER_OUTPUT_THREADS;
	EnterCriticalSection(&PlrOut_Thread_CritSecs[output_thread_num]);
	EnterCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
  #if 0	// 2022 kriskoin
	struct PlrOutQueueEntry **prev_ptr = &PlrOutQueueHeads[output_thread_num];
	struct PlrOutQueueEntry *p = *prev_ptr;

  #if COUNT_QUEUE_LEN
	kp1(("%s(%d) **** PERFORMANCE WARNING: counting queue length when removing packets!\n", _FL));
	int original_length = 0;
	int removed_items = 0;
	int final_length = 0;
	int print_debug_output = FALSE;
	while (p) {
		original_length++;
		p = p->next;
	}
	if (original_length != iPlrOutQueueLens[output_thread_num]) {
		static int printed_count;
		if (printed_count < 100) {
			printed_count++;
			kp(("%s %s(%d) WARNING: output queue length is incorrect before deleting. plrid = $%08lx, counted len=%d, iQueuelens[%d]=%d\n",
						TimeStr(), _FL,
						plr->player_id, original_length,
						output_thread_num, iPlrOutQueueLens[output_thread_num]));
		}
		iPlrOutQueueLens[output_thread_num] = original_length;	// fix it.
	}
	p = *prev_ptr;
	int queue_position = 0;
  #endif
	struct PlrOutQueueEntry *new_tail = NULL;
	while (p) {
		struct PlrOutQueueEntry *next = p->next;	// preserve ptr to next item.
		if (p->plr == plr) {
			// This entry needs deleting.
			// If this was also the tail, the tail needs to be adjusted.
			if (PlrOutQueueTails[output_thread_num] == p) {
			  #if COUNT_QUEUE_LEN && 0
				kp(("%s %s(%d) Deleting object in queue position %d of %d...\n",
							TimeStr(), _FL, queue_position, original_length));
				kp(("%s %s(%d) PlrOutQueueTails[%d] was $%08lx, setting to  NULL. *prev_ptr = $%08lx, next = $%08lx\n",
							TimeStr(), _FL, output_thread_num, PlrOutQueueTails[output_thread_num], *prev_ptr, next));
				kp(("%s %s(%d) PlrOutQueueHeads[%d] is $%08lx, prev_ptr = $%08lx, original_length = %d\n",
							TimeStr(), _FL, output_thread_num, PlrOutQueueHeads[output_thread_num], prev_ptr, original_length));
				print_debug_output = TRUE;
			  #endif
				// Set the tail to the previous packet (if any).
				PlrOutQueueTails[output_thread_num] = new_tail;
			}
			*prev_ptr = next;

			// Free p and anything it owns
			delete p->pkt;
			zstruct(*p);
			free(p);
			iPlrOutQueueLens[output_thread_num]--;		// another item popped or otherwise removed
		  #if COUNT_QUEUE_LEN
			removed_items++;
		  #endif
		} else {
			// Keep this packet... just update the prev ptr.
			prev_ptr = &p->next;
			new_tail = p;
		}
		p = next;
	  #if COUNT_QUEUE_LEN
		queue_position++;
	  #endif
	}
  #else
	//kp(("%s %s(%d) Removing packets for player %06lx\n", TimeStr(), _FL, plr->player_id));
	struct PlrOutQueueList *ql = &PlrOutQueueList[output_thread_num];
	int i = ql->head;
	while (i != ql->tail) {
		if (ql->array[i].plr==plr) {
			// This one needs removing!
			// We can just clear it rather than worrying about scrolling the array.
			// The thread that reads from this array knows how to deal with
			// entries that are empty (zeroed).
			delete ql->array[i].pkt;
			zstruct(ql->array[i]);
		}
		// Move to next item in array
		i++;
		if (i >= ql->array_size) {
			i = 0;	// wrap to beginning
		}
	}
  #endif

	for (i=0 ; i<iPlrOut_SendPendingMaxIndex[output_thread_num] ; i++) {
		if (PlrOut_SendPendingLists[output_thread_num][i] == plr) {
			PlrOut_SendPendingLists[output_thread_num][i] = NULL;
		}
	}

  #if COUNT_QUEUE_LEN
	p = PlrOutQueueHeads[output_thread_num];
	while (p) {
		final_length++;
		p = p->next;
	}
	if (final_length != original_length - removed_items ||
		final_length != iPlrOutQueueLens[output_thread_num] ||
		print_debug_output)
	{
		static int printed_count;
		if (printed_count < 100) {
			printed_count++;
			kp(("%s %s(%d) WARNING: output queue length is incorrect. plrid = $%08lx, original=%d, removed=%d, final=%d, iQueuelens[%d]=%d\n",
						TimeStr(), _FL,
						plr->player_id, original_length, removed_items,
						final_length, output_thread_num, iPlrOutQueueLens[output_thread_num]));
		}
	}
  #endif

	LeaveCriticalSection(&PlrOut_Queue_CritSecs[output_thread_num]);
	LeaveCriticalSection(&PlrOut_Thread_CritSecs[output_thread_num]);
}

//*********************************************************
// https://github.com/kriskoin//
// Launch the compression and encryption thread(s).
//
void PlrOut_LaunchThreads(void)
{
	//kp(("%s(%d) Launching %d compression and encryption threads...\n", _FL, NUMBER_OF_PLAYER_OUTPUT_THREADS));
	int i;
	for (i=0 ; i<NUMBER_OF_PLAYER_OUTPUT_THREADS ; i++) {
		PPInitializeCriticalSection(&PlrOut_Queue_CritSecs[i], CRITSECPRI_OUTPUTQUEUE, "PlrOutQueue");
		PPInitializeCriticalSection(&PlrOut_Thread_CritSecs[i], CRITSECPRI_OUTPUTTHREAD0+i, "PlrOutThread");
	}
	for (i=0 ; i<NUMBER_OF_PLAYER_OUTPUT_THREADS ; i++) {
		int result = _beginthread(PlrOut_ThreadEntry, 0, (void *)i);
		if (result == -1) {
			Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
			DIE("Thread launching failed!");
		}
	}
}
