/*******************************************************************************
 CLASS SDB (SimpleDataBase)
 date: kriskoin 2019/01/01
 This is an implementation of a simple data base class
 *******************************************************************************/

#ifndef _SDB_H_INCLUDED
#define _SDB_H_INCLUDED

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif


//#ifndef _OPAF_H_INCLUDED
//#include "OPAF.h"
//#endif

#ifndef _OPA_H_INCLUDED
#include "OPA.h"
#endif

#include <time.h>
#include "pplib.h"
#include "gamedata.h"

//*** struct SDBRecord has been moved to gamedata.h so the client
// can share it.

#define TOURNAMENT_CHIP_ACCOUNT_NAME	"Tournaments"	// account chips and money go to/from
#define BLACKHOLE_CHIP_ACCOUNT_NAME		"BlackHole"		// account name where chips vanish to...

typedef struct SDBIndexEntry {
	WORD32 hash;	// hash (e.g. player_id or CalcCRC32ForStr(strtolower(userid)))
	WORD32 index;	// record number in database file.
} SDBIndexEntry;

typedef struct TournamentRefundStructure {	// used when we need to refund a tournament
	WORD32 player_id;
	WORD32 table_serial_number;
	WORD32 tournament_creditable_pot;	// tournament's prize pool left
	WORD32 tournament_total_chips_left_in_play;	// tournament's chip universe at present
	WORD32 tournament_chips_in_play;	// this player's total tournament chips
	WORD32 tournament_fee_paid;			// tournament fee paid by player
	WORD32 buyin_amount_paid;			// buyin paid by player
	float  percentage_held;				// the percentage of tournament chips he held
	WORD32 tournament_total_refund;		// TOTAL of tournament_refund+buyin+fee
	WORD32 tournament_partial_payout;	// the partial payout we'll make
} TournamentRefundStructure;

enum SpecialChipTransfer { SCT_RAKE_TO_MISC, SCT_MISC_TO_RAKE, SCT_PLAYER_TO_MISC, SCT_MISC_TO_PLAYER,
	SCT_ECASH_TO_PLAYER, SCT_PLAYER_TO_ECASH, SCT_MARKETING_TO_PLAYER, SCT_PLAYER_TO_MARKETING };

enum AccountFields { AF_AVAILABLE_CASH, AF_PENDING_CC_REFUND, AF_PENDING_CHECK, AF_PENDING_PAYPAL} ;

#define SDB_RECORD_FIELD_NUM	18	// how many fields are there in the text record?

class SimpleDataBase {
public:
	SimpleDataBase(char *database_name, int max_record_count,OPA *OPA_ptr);
	~SimpleDataBase();

   void PrintRecord(int i) {printf("\n Pid= %d | idA= %s\n",(&_SDB[i])->player_id, (&_SDB[i])->idAffiliate);};
	void UseDataBase(char *database_name);	// specify which database we're using
	void AddToChipsInBankForPlayerID(WORD32 player_id, int real_in_bank, ChipType chip_type);
	void SubtractFromRakeAccount(WORD32 chips, ChipType chip_type);
	void SetChipsInBankForPlayerID(WORD32 player_id, int chips, ChipType chip_type);
	void SetChipsInPlayForPlayerID(WORD32 player_id, int chips, ChipType chip_type);
	int GetChipsInBankForPlayerID(WORD32 player_id, ChipType chip_type);
	int GetChipsInPlayForPlayerID(WORD32 player_id, ChipType chip_type);
	int GetPendingCreditForPlayerID(WORD32 player_id);
	void HandleEndGameChipChange(WORD32 player_id, WORD32 chips, ChipType chip_type);
	void AddToRakeAccount(WORD32 chips, ChipType chip_type);// we collect the rake here
	WORD32 GetRake(ChipType chip_type);	// get current quantity in rake account
	void ClearRake(ChipType chip_type);	// clear the rake account
	WORD32 CountAllChips(WORD32 game_serial_number);
	int IncrementAddressChangeCount(WORD32 player_id);
	
	// given a playerID, fill a TournamentRefundStructure
	ErrorType FillTournamentRefundStructure(WORD32 player_id, TournamentRefundStructure *trs);	
	ErrorType FillTournamentRefundStructure(SDBRecord *r, TournamentRefundStructure *trs);
	
	// different search methods -- all fill a return structure
  #if 0
	int SearchDataBaseByName(char *last_name, char *first_name, SDBRecord &result);
  #endif
	int SearchDataBaseByPlayerID(WORD32 player_id);
	int SearchDataBaseByPlayerID(WORD32 player_id, SDBRecord *result);
	int SearchDataBaseByUserID(char *user_id);
	int SearchDataBaseByUserID(char *user_id, SDBRecord *result);
	ErrorType SearchDataBaseByIndex(int index, SDBRecord *result);
	void PlayerSawPocket(WORD32 player_id);	// we log this
	void PlayerSawFlop(WORD32 player_id);	// we log this
	void PlayerSawRiver(WORD32 player_id);	// we log this
	void AddGameToPlayerHistory(WORD32 player_id, WORD32 game_serial_number);


	void SimpleDataBase::LoadMoneyData(char *user_id);   //J Fonseca   05/12/2003
  bool SimpleDataBase::IsRobot(char *user_id);    //J Fonseca   03/01/2004
	char * Trim(char *str);

	int SearchDataBaseByEmail(char *email_address);
	int SearchDataBaseByEmail(char* email_address, SDBRecord *result);
	int AddToChipsForMoneyTransaction(MoneyTransaction *tm);

	// Return the next transaction number for a client (the exact same
	// number as GetNextTransactionNumber() would return) and then increment
	// the number (to keep the number returned unique).
	int GetNextTransactionNumberAndIncrement(WORD32 player_id);
	int GetNextTransactionNumber(WORD32 player_id);




	// chip transferring
	void TransferChips(WORD32 from_player_id, WORD32 to_player_id, WORD32 chips);

	// Generic transfer chips function.  Capable of transferring both
	// real or play money chips from any account to any account.  Includes
	// a variable to indicate which field of the account to transfer to/from:
	// fields: 0=in bank, 1=pending CC refund field, 2=pending check field
	// No logging is done by this function - that should be done at a higher level.
	void SimpleDataBase::TransferChips(ChipType chip_type, WORD32 from_player_id,
			int from_account_field, WORD32 to_player_id, int to_account_field, int chips,
			char *calling_file, int calling_line);

	// Ecash and credit handling
	void MoveEcashCreditToCash(WORD32 player_id, int credit_amt);
	void AddToEcashPendingCredit(WORD32 player_id, int credit_amt);
	void AddOrRemoveChipsInEcashAccount(int chips);
	void AddOrRemoveChipsInEcashFeeAccount(int chips);
	void ShutDown(void);	// should be called when we're shutting down
	// tranferrring to/from pending account
	void TransferChipsToPendingAccount(WORD32 player_id, int chips);	
	void TransferChipsFromPendingAccount(WORD32 player_id, int chips);	
	// transfer from pending back to ecash
	void TransferChipsFromPendingToEcash(int chips);
	// modify how much credit left a previous transaction has left
	void SetCreditLeftForTransaction(WORD32 player_id, int tr_num, int cr_left);
	void SetCreditLeftForTransaction(WORD32 player_id, WORD32 ecash_id, BYTE8 tr_type, int cr_left);
	// modify/update check transaction entry
	void ModifyCheckTransaction(WORD32 player_id, WORD32 check_number, 
		int check_amt, char *tracking_number, BYTE8 delivery_method);
	// fee credit points handling
	int GetCreditFeePoints(WORD32 player_id);
        // kriskoin 
        int GetTotalBonus(WORD32 player_id, WORD32 start_date, WORD32 end_date);
        void AddToGoodRakedGames(WORD32 player_id, int points);
        int GetGoodRakedGames(WORD32 player_id);
        // end kriskoin 

	void AddToCreditFeePoints(WORD32 player_id, int points);
	void SimpleDataBase::SetCreditPoints(WORD32 player_id, INT32 left_to_credit_back);

	void SimpleDataBase::LoadDataBaseByUserID(char *user_id); //J Fonseca

	void SimpleDataBase::RefundPaypalCredit(WORD32 player_id, INT32 left_to_credit_back);
	void ClearCreditFeePoints(WORD32 player_id);
        int SimpleDataBase::GetCreditPoints(WORD32 player_id); 
	// Return the total amount of all pending checks for a player.
	int GetPendingCheckAmount(WORD32 player_id);
	int GetPendingPaypalAmount(WORD32 player_id);
	// Find an empty record (player_id==0) and return a unique
	// player ID to allow access to that record.
	// Returns new player_id or 0 if no empty records are available.
	WORD32 SimpleDataBase::CreateNewRecord(char *user_id_string);

	// Write most of the fields from an sdb record into the database
	// Uses the player_id field to determine which database record to
	// overwrite.
	ErrorType SimpleDataBase::WriteRecord(SDBRecord *ir);


	// Return the privilege level for a particular user (ACCPRIV_*)
	// Returns 0 if not found (ACCPRIV_LOCKED_OUT)
	BYTE8 SimpleDataBase::GetPrivLevel(WORD32 player_id);

	// Set the privilege level for a particular user (ACCPRIV_*)
	void SimpleDataBase::SetPrivLevel(WORD32 player_id, BYTE8 priv_level, char *reason);

	// Save login information for a player to his player record.
	void SavePlayerLoginInfo(WORD32 player_id, time_t login_time, WORD32 ip_address, struct ClientPlatform *client_platform, struct VersionNumber *client_version);

	// Set the client_version and client_platform fields without
	// recording any additional info.
	void SimpleDataBase::SetClientPlatformInfo(WORD32 player_id, struct ClientPlatform *client_platform, struct VersionNumber *client_version);

	// Save time when player is forced all-in
	void SimpleDataBase::SavePlayerAllInInfo(WORD32 player_id, time_t all_in_time, int worst_connection_state, WORD32 game_serial_number);

	// Save a player transaction
	void SimpleDataBase::LogPlayerTransaction(WORD32 player_id, struct ClientTransaction *ct);
	
	// Add (or remove) new chips to/from the chip universe.
	void SimpleDataBase::AddChipsToUniverse(int amount, ChipType chip_type, char *reason);

	// Retrieve the number of user records which currently exist
	int SimpleDataBase::GetUserRecordCount(void);

	// Add a note to the comments section of an account record.
	// Appends to end and clips if there's no more room.
	void SimpleDataBase::AddAccountNote(WORD32 player_id, char *fmt_string, ...);

	// Buy into a tournament.  You always get STARTING_TOURNAMENT_CHIPS tournament chips.
	// The money comes from the player's real in bank, the fee goes straight into rake.
	// Player is left with a value in tournament_chips_in_play.
	// If an error is returned, nothing was done.
	ErrorType SimpleDataBase::TournamentBuyIn(WORD32 player_id,
			WORD32 tournament_table_serial_number,
			int buyin_amount, int tournament_fee, 
			WORD32 tournament_pot, WORD32 total_tournament_chips);

	// Cash out of a tournament.
	// The money comes from the player's real in bank, the fee goes straight into rake.
	// Player is left with a value in tournament_chips_in_play.
	// If an error is returned, nothing was done.
	ErrorType SimpleDataBase::TournamentCashout(TournamentRefundStructure *trs);
	ErrorType SimpleDataBase::TournamentCashout(WORD32 player_id,
			WORD32 tournament_table_serial_number,	// for error checking purposes
			int tournament_chips_to_cash_out,		// for error checking purposes
			int cashout_amount,						// real $'s to cash out (no error checking)
			int tournament_fee_to_refund);			// amount of fee to refund (usually zero)

	// we may need to check and set local tournament chip universes during tournaments so that
	// sdb needs to know how many were left in case of a crash or early shutdown
	WORD32 SimpleDataBase::GetTotalTournamentChipsLeft(WORD32 player_id);
	ErrorType SimpleDataBase::SetTotalTournamentChipsLeft(WORD32 player_id, WORD32 chips_left);
	// we need to check and set local tournament pot sizes during tournaments so that
	// it's there for post-crash or shutdown payoffs
	ErrorType SimpleDataBase::SetCreditableTournamentPot(WORD32 player_id, WORD32 creditable_tournament_pot);

	// in case of a server crash, we want to know what the partial payout would be for this player
	ErrorType SimpleDataBase::SetTournamentPartialPayout(WORD32 player_id, WORD32 payout_amt);

	PPCRITICAL_SECTION SDBCritSec;	// Critical section to lock the entire database
	PPCRITICAL_SECTION SDBCritSec2;	// Critical section to lock the entire database

//	OPAF *the_OPA; //connect to data base
	OPA *the_OPA; //connect to data base
	OPA *theOPAptr; //J Fonseca  11/02/2004


	void *cardroom_ptr;	// ptr to the CardRoom which created us.

	WORD32 PendingRec_ID;	// player_id for the "Pending" record


	WORD32 EcashRec_ID;		// player_id for the "Ecash" record
	WORD32 DraftsInRec_ID;	// player_id for the "Drafts" record
	WORD32 PrizesRec_ID;	// player_id for the "Prizes" record
	WORD32 BadBeatRec_ID;	// player_id for the "BadBeat" record
	WORD32 TournamentRec_ID;	// player_id for the "Tournaments" record
	int iRecordCount;		// # of records in database including blanks (file grows to this size)
	int number_of_accounts_purged;	// # of account purged at startup
	int stale_account_count;	// # of accounts in the stale.bin file

	// Note about _SDB: never use it outside of sdb.cpp unless it's in something
	// completely offline like dbfilter.
	SDBRecord *_SDB;		// the database array (memory mapped file)

	WORD32 cash_in_play_at_startup;	// real cash in play play at startup.
	WORD32 cash_in_accounts_at_tables_at_startup;	// real cash in player's entire account for all players with cash in play at startup

private:
	void ReadDataBase(void);// (internal) read whole database
	void WriteDataBase(int force_write);// write both bin/txt
	void MoveChipsFromPlayingToBank(SDBRecord *rec, ChipType chip_type);
	int CheckForNewTextFile(void);		// need to read in the txt file?
	int ParseTextToRecord(char *text_line, SDBRecord *sdbr); // parse a text record
	int GetArgs(int maxArgc, char *argv[], char *string, char seperator); //parser
	char _sdb_name[MAX_COMMON_STRING_LEN];
	char _txt_name[20];
	char _bin_name[20];
	time_t time_now;
	time_t old_time;
	SDBRecord *RakeRec;		// special pointer to the rake database entry
	SDBRecord *MarketingRec;// special pointer to the marketing account
	SDBRecord *EcashRec;	// special pointer to the ecash (SFC for now) account
	SDBRecord *EcashFeeRec;	// special pointer to the EcashFee (SFC for now) account
	SDBRecord *MiscRec;		// special pointer to the "misc" account
	SDBRecord *ErrorRec;	// special pointer to the error account
	SDBRecord *DraftsInRec;	// special pointer to the error account
	SDBRecord *PendingRec;	// ptr to the Pending credits account
	SDBRecord *PrizesRec;	// ptr to Prizes account
	SDBRecord *BadBeatRec;	// ptr to BadBeat account
	SDBRecord *TournamentRec;// ptr to Tournaments account
	
	long RealChipUniverse; // total sum of all real chips at startup
	long FakeChipUniverse; // total sum of all fake chips at startup
	long TournamentChipUniverse;// total sum of all fake chips at startup
	long RealChipChange;	// checksum: net change from all transactions (should net to zero)
	long FakeChipChange;	// checksum: net change from all transactions (should net to zero)
	long TournamentChipChange;	// // checksum: net change from all transactions (should net to zero)
	int _clean_shutdown;	// T/F upon termination
	int iRecordLength;		// total length of each record (in bytes) (used to align records in the database file - may be larger than sizeof(SDBRecord))

	int indexes_need_sorting;		// T/F to indicate if indexes need resorting
	SDBIndexEntry *index_player_id;	// index array by player_id
	int index_player_id_count;		// # of entries in the index_player_id array
	SDBIndexEntry *index_user_id;	// index array by user_id
	int index_user_id_count;		// # of entries in the index_user_id array
	void SortIndexArrays(void);		// sort the index arrays for quick searching.


	int iEmptyRecordSearchStart;	// where to start searching for an empty record.
	WORD32 dwHighestPlayerID;		// highest player ID we've seen

	// Perform a binary search on an index array to find a particular hash.
	// Returns -1 if not found.
	// If found, it returns the FIRST entry that matches.  The caller can
	// then step forward from that location to handle any hash collisions.
	int SimpleDataBase::FindIndexEntry(SDBIndexEntry *index, int index_count, WORD32 hash_to_search_for);

	WORD32 CalcStringHash(char *s);	// calculate a hash from a string

	// ---- Database memory file mapping related stuff ----
	// Open the database file as a memory mapped file.
	// If the file fails to open successfully, DIE() is called.
	void SimpleDataBase::OpenDatabaseFile(void);

	// Close the database file if it is open.
	void SimpleDataBase::CloseDatabaseFile(void);

	// Flush the database (or part of it) to disk.  It may not
	// get written immediately, but it will get queued immediately.
	void SimpleDataBase::FlushDatabaseFile(void);
	void SimpleDataBase::FlushDatabaseFile(WORD32 start_offset, WORD32 length);

	// Create a new binary database file
	// WARNING: this function touches every record of the database.
	void SimpleDataBase::CreateNewDatabaseFile(void);

	HANDLE hDatabaseFile;	// handle to binary database file (if open)
  #if WIN32
	HANDLE hDatabaseMapping;// handle to database file mapping object
  #endif

	// Notify (by email) a player that there was a server problem
	// but that his chips were taken care of properly.
	// (send game cancellation notice)
	void SimpleDataBase::NotifyPlayerOfServerProblem(SDBRecord *r);

};

extern SimpleDataBase *SDB;		// global database access point for entire server

#endif	//!_SDB_INCLUDED
