#ifndef _ECASH_H_INCLUDED
#define _ECASH_H_INCLUDED

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif


#include <stdio.h>
#include <time.h>
#include "pplib.h"
#include "gamedata.h"

struct EcashReplyStruct {
	// successful reply fields
	char *status;
	char *authCode;
	char *authTime;
	char *avsInfo;
	char *curAmount;
	char *amount;
	char *txnNumber;
	char *serviceVersion;
	char *txnType;
	// error reply fields
	char *errCode;
	char *errString;
	char *subError;
	char *subErrorString;
	char *pmtError;
	// New 1.03 fields:
	char *actionCode;
	char *creditNumber;
	char *merchantTxn;
};

ErrorType LaunchEcashHandlerThread(void);	// thread launcher
void _cdecl EcashHandlerThread(void *args);
ErrorType AddEcashRequestToQueue(CCTransaction *cct_in);
ErrorType AddEcashRequestToQueue(CCStatementReq *ccsr_in);
int TransactEcashRequest(CCTransaction *tr, WORD32 player_id);
ErrorType TransactStatementRequest(CCTransaction *tr, WORD32 player_id);
char *GetCCCode(CCType cctype, char *out);
/*
void BuildPurchaseRequestPostString(CCTransaction *cct, 
	char *action_string, char *transaction_num, char *full_name, char *clean_card, char *email,
	char *streetAddr, char *phone, char *city, char *province, char *zip, char *country);
*/

void BuildPurchaseRequestPostString(CCTransaction *cct, char *action_string, char *transaction_num, char *full_name, 
char *clean_card, char *email, char *streetAddr, char *phone, char *city, char *province, char *zip, char *country);

#define MAX_ECASH_THREADS			20      // max # of ecash threads we support

// multithreaded transaction number verification
#define MAX_PENDING_TRANSACTION_VERIFICATIONS	MAX_ECASH_THREADS*10
extern PPCRITICAL_SECTION tnv_cs;
extern int tnv_critsec_is_initialized;
void InitializeTNVPendingArray(void);
void TransactionNumberReceived(int thread_number, WORD32 transaction_number);
void SetPotentialLowestTransactionNumber(int thread_number, int action_flag);
void SetPotentialLowestTransactionNumber(int thread_number);
void ClearPotentialLowestTransactionNumber(int thread_number);
int ScanForMissedTransactions(void);
void NotifySkippedTransactionNumber(WORD32 transaction_number);

#define TEST_SKIPPED_TRANSACTION_CODE	0

void BuildCreditRequestPostString(char *action_string, int amount, char *new_transaction_num, 
	int orig_ecash_txn);

// Parse the reply string and fill the return structure pointers
ErrorType ParseEcashReply(EcashReplyStruct *ers, char *result_str);
int ParseReplyArgs(int maxArgc, char *argv[], char *string, char seperator);

void ShutdownEcashProcessing(void);
ErrorType AddChipsToAccount(WORD32 player_id, int chips_to_add, ChipType chip_type, char *reason, int peel_credit,
							int *cash_deposited, int *credit_deposited);
void AddToEcashLog(WORD32 player_id, const char *tr, int amt, const char *szCC,
	const char *szCCname, const char*szCCmon, const char *szCCyr,
	int ecash_id, const char *txnum, int err, int sub_err);
ErrorType GetNextTransactionNumber(WORD32 player_id, char *out, int *transaction_number);
ErrorType GetNextTransactionNumberNotIncrement(WORD32 player_id, char *out, int *transaction_number);
ErrorType EcashNotifyPlayer(WORD32 player_id, char *msg, int chips_changed);
ErrorType EcashSendClientInfo(WORD32 player_id);
char *ClientTransactionDescription(ClientTransaction *ct, char *out);
int NetPurchasedInLastNHours(WORD32 player_id, int hours);
int NetPurchasedInLastNHours(WORD32 player_id, int hours, int *total_purchases);
int NetPurchasedInLastNHours(WORD32 player_id, int hours, int *total_purchases, time_t time_in);
int IsPlayerInEcashQueue(WORD32 player_id);

int ECash_GetArgs(int maxArgc, char *argv[], char *string, char seperator);
WORD32 CCToHex(char *cc, char *out, char *clean_card_out, char *clean_card_spaces);

char *ECash_FixReplyString(char *input_string, char *output_string, int max_output_string_len);

// Do all handling of the actual posting of the transaction, wait and return the result.
// Note that this is a blocking function and might take several minutes to return.
ErrorType PostTransactionToECash(char *transaction_str, char *result, int max_result_len, char *url);

// Disable cashier functions for a particular player.
// (set the SDBRECORD_FLAG_NO_CASHIER bit)
void DisableCashierForPlayer(WORD32 player_id);
// kriskoin 
void DisableCashoutForPlayer(WORD32 player_id);
void EnableCashoutForPlayer(WORD32 player_id);
void DisableIniBonusForPlayer(WORD32 player_id);
int GetIniBonusStatusForPlayer(WORD32 player_id);
void CheckRealPlayerForPlayer(WORD32 player_id);
// end kriskoin 

// Begin a check run by scanning through all the players in the database
// and returning the first bunch that have pending checks requested.
// Fills in an AdminCheckRun structure and sends it to dest_player_id.
void ECash_BeginCheckRun(WORD32 dest_player_id);

// Begin processing a submitted check run.  Issue checks for
// each player in the submitted list.  The work is actually done
// by a background thread.
void ECash_SubmitCheckRun(struct AdminCheckRun *acr);

// Return the number of items currently in the ecash queue.
int Ecash_GetQueueLen(void);

extern int iEcashPostTime;	// average of how long transactions are taking (in seconds)

// Credit card database
#define CCDB_CARDNUM_BYTES	8
#define CCDB_CCPLAYER_IDS	5
#define CCDB_GROW_RATE		10
#define CCDB_FNAME			"ccdb.bin"

struct CCDBEntry {
	BYTE8  cc_num[CCDB_CARDNUM_BYTES];		// keyed off here
	time_t last_transaction_date;
	INT8   max_accounts_allowed;
	BYTE8  _padding[3];
	// 16 bytes
	WORD32 player_ids[CCDB_CCPLAYER_IDS];	// array of plr id's associated with this card
	// 36 bytes
	BYTE8  _unused[12];
	// 48 bytes
};

//extern Array CCDB;
//extern PPCRITICAL_SECTION CCDBCritSec;

void ReadEcashLogForCCDB(char *filename);
ErrorType MakeBCDFromCC(char *credit_card, void *output);

void CCDB_Load(void);
void CCDB_SaveIfNecessary(void);

struct CCDBEntry *CCDB_GetCCEntry(void *cc_to_find);
void CCDB_SetMaxAccountsAllowed(void *cc_key, int max_accounts_allowed);
void CCDB_SetMaxAccountsAllowedForPlayerID(WORD32 player_id, int max_accounts_allowed);
int CCDB_AddCC(void *cc_key, WORD32 player_id, time_t transaction_date, int test_related_accounts_flag, int *output_auto_block_flag, char *output_auto_block_reason);
void CCDB_SetMainAccountForCC(WORD32 admin_player_id, WORD32 new_main_player_id, WORD32 partial_cc_number);
int GetCCPurchaseLimitForPlayer(WORD32 player_id, int time_frame);

/*
We have setup a FirePay test environment for our clients. 
Please use this server for your test purposes 
https://realtime.test.firepay.com/servlet/DPServlet 

and these parameters 

merchantId=idpmerchant 
merchantPwd=2RlPROUQ0J6YkHwxyL9 
account=400511 
*/

// all of these come from SFC documentation specific to us
#if WIN32
  #define USE_TEST_SERVER	1	// 0 = REAL, 1 = TESTING	// using test accounts on test servers
#else
  #define USE_TEST_SERVER	0	// 0 = REAL, 1 = TESTING	// using test accounts on test servers
#endif

#if USE_TEST_SERVER
 #if 1	//kriskoin:    #define ECASH_SERVER_TYPE 		"SFC FIREPAY FAKE MONEY TEST SERVER"
  #if 0	//kriskoin:    #define ECASH_MERCHANT_ID		"idpmerchant_111"
   #define ECASH_MERCHANT_PWD		"2RlPROUQ0J6YkHwxyL9_111"
   #define ECASH_ACCOUNT_ID			"400511_111"
  #else // ** 20010313HK : FirePay beta test
   #define ECASH_MERCHANT_ID		"test"  //"idpmerchant"
 //  #define ECASH_MERCHANT_ID		"desertpokermerchant"  //"idpmerchant"
   #define ECASH_MERCHANT_PWD		"G3MK443XB67YTY89C4P"  //"66gN77WH2TGJHb446W2"
   #define ECASH_ACCOUNT_ID		"402888"   //"50000819"
  #endif
   #define ECASH_SERVER				"realtime.test.firepay.com"  //"realtime.firepay.com"
  #if 1	// 2022 kriskoin
   #define ECASH_CHARGE_URL			"/servlet/DPServlet"
   #define ECASH_CREDIT_URL			"/servlet/DPServlet"
   #define ECASH_QUERY_URL			"/servlet/DPServlet"
   #define ECASH_FAILURE_LOOKUP_URL	"/servlet/DPServlet"
   #define ECASH_POSTING_TYPE		"Content-type: application/x-www-form-urlencoded"
  #else // now unused, maybe will never be used again
   #define ECASH_CHARGE_URL			"/servlet/AuthorizeServlet"
   #define ECASH_CREDIT_URL			"/servlet/CreditServlet"
   #define ECASH_QUERY_URL			"/servlet/QueryServlet"
   #define ECASH_FAILURE_LOOKUP_URL	"/servlet/FailureLookupServlet"
   #define ECASH_POSTING_TYPE		"Content-type: application/x-www-form-urlencoded"
  #endif
 #else	// emergis test server
   #define ECASH_SERVER_TYPE 		"EMERGIS FAKE MONEY TEST SERVER"
  #if 1	//kriskoin:    #define ECASH_ACCOUNT_ID			"401123_111"
   #define ECASH_MERCHANT_ID		"testaccount1_111"
   #define ECASH_MERCHANT_PWD		"testpwd1_111"
  #else
   #define ECASH_ACCOUNT_ID			"401021_111"
   #define ECASH_MERCHANT_ID		"testaccount_111"
   #define ECASH_MERCHANT_PWD		"testpwd1_111"
  #endif
   #define ECASH_SERVER				"testtranfe.bellxperttest.com"
   #define ECASH_CHARGE_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_CREDIT_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_QUERY_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_FAILURE_LOOKUP_URL	"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_POSTING_TYPE		"Content-type: application/x-www-form-urlencoded"
 #endif
#else	// this is our real account info
 #if 0	// old emergis server
   #define ECASH_SERVER_TYPE 		"REAL SFC/EMERGIS TRANSACTION SERVER"
   #define ECASH_ACCOUNT_ID			"400511_111"
   #define ECASH_MERCHANT_ID		"idpmerchant_111"
   #define ECASH_MERCHANT_PWD		"id984mer_111"
   #define ECASH_SERVER				"prodtranfe.solutionxpert.com"
   #define ECASH_CHARGE_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_CREDIT_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_QUERY_URL			"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_FAILURE_LOOKUP_URL	"/tms-ts/payService/merchant/paymentServiceM.cgi"
   #define ECASH_POSTING_TYPE		"Content-type: application/x-www-form-urlencoded"
 #else	// new FirePay server
   #define ECASH_SERVER_TYPE 		"SFC FIREPAY REAL SERVER" //"Test Server" 
   #define ECASH_ACCOUNT_ID		"50000819"	//"400511_111"
//   #define ECASH_MERCHANT_ID		"merchant"  //"idpmerchant_111"
   #define ECASH_MERCHANT_ID		"test"  //"idpmerchant_111"
   #define ECASH_MERCHANT_PWD		 "66gN77WH2TGJHb446W2"  //"jV6Lid0fofyRJLrH8oH_111"	// 2000/11/29 email
   #define ECASH_SERVER				"realtime.firepay.com" //"realtime.test.firepay.com"
   #define ECASH_CHARGE_URL			"/servlet/DPServlet"
   #define ECASH_CREDIT_URL			"/servlet/DPServlet"
   #define ECASH_QUERY_URL			"/servlet/DPServlet"
   #define ECASH_FAILURE_LOOKUP_URL	"/servlet/DPServlet"
   #define ECASH_POSTING_TYPE		"Content-type: application/x-www-form-urlencoded"
  #endif
#endif
#if 0	// 2022 kriskoin
  #define ECASH_PROCESSOR_NAME	"SFC"
#else
  #define ECASH_PROCESSOR_NAME	"DesertPoker-ecash"
#endif


#define ECASH_RETURN_BUF_LEN	1000	// transaction result string
/* struct used for queuing ecash requests */
struct CCTQueue {
	WORD32 player_id;			// player id for this player
	struct CCTransaction cct;	// the actual transaction details
	struct CCTQueue *next;		// next in linked list
};

#if USE_TEST_SERVER
// Queue up an entire log file for processing at once.
// This is ONLY used for testing the queuing on our end and
// on the transaction server end.  It MUST NOT be used on
// a live server.
void ECash_QueueLogFileForTesting(WORD32 player_id, char *filename);
#endif	// USE_TEST_SERVER

#endif // !_ECASH_H_INCLUDED
