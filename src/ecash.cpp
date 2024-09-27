/*********************************************************************************
 Date: 2017/7/7 kriskoin Purpose: ecash functions, etc for Desert Poker server
***********************************************************************************/


#ifdef HORATIO
	#define DISP 0
#endif

#if WIN32
  #include <windows.h>
  #include <process.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include "pokersrv.h"
#include "cardroom.h"
#include "player.h"
#include "sdb.h"
#include "logging.h"
#include "ecash.h"

#define MAX_TRANSACTION_ATTEMPTS	8	    // max failed attempts per transaction (when we KNOW it failed)
#if WIN32 && 0
#define MIN_ECASH_RETRY_TIME		(1*60)	// min # of seconds we keep retrying for (overriding MAX_TRANSACTION_ATTEMPTS)
#else
#define MIN_ECASH_RETRY_TIME		(5*60)	// min # of seconds we keep retrying for (overriding MAX_TRANSACTION_ATTEMPTS)
#endif
#define MAX_ECASH_RETRY_TIME        (10*60) // max time to spend on any single operation
#define MIN_SPACING_BETWEEN_RETRIES 15      // min # of seconds between retry attempts

static volatile int ecash_threads_running = 0;
static PPCRITICAL_SECTION ecash_cs;
static struct CCTQueue *ECash_Queue_Head;		// head of queue (if any)
static struct CCTQueue **ECash_Queue_TailPtr;	
// if there's a head, this points to the tail's 'next' field, otherwise undefined

// The iConsecutiveFailedTransactions and iConsecutiveMaybeTransactions
// variables will only get reset when something actually DOES go through.
static int iConsecutiveFailedTransactions;	// # of transactions in a row that did NOT go through.
static int iConsecutiveMaybeTransactions;	// # of transactions in a row that MIGHT have gone through
static int iMaybePurchaseOccurred;			// set if a 'maybe' purchase occurred.  Cleared when next purchase definitely goes through.
static int iLastECashID;					// last transaction # that went through
static time_t CashierCloseTime;				// time_t when cashier was automatically closed
static int iAdminMonitorToldEcashIsDown;	// set if the message was sent to the admin chat monitor to indicate the cashier is down

enum ClientTransactionTR { CLTR_UNDEFINED, CLTR_REQ_PURCHASE, CLTR_APP_PURCHASE, CLTR_REJ_PURCHASE, CLTR_ERR_PARSE, CLTR_ERR_TRANSACT,
		CLTR_REQ_CASHOUT, CLTR_REQ_CREDIT, CLTR_APP_CREDIT, CLTR_REJ_CREDIT, CLTR_CHECK, CLTR_CRADMIN, CLTR_CRNOCREDIT, CLTR_BLOCK_PURCH,
		CLTR_ERR_TRANSACT_MAYBE, CLTR_CHECK_REFUND };
static char *szTransactStr[] = { "???Undefined","PurchReq","PurchApp","PurchRej","ErrParse","ErrTransact",
		"CashoutReq", "CredReq", "CredApp", "CredRej", "CrCheck", "CrAdmin", "CrNoCredit", "PurchBlock",
		"ErrTransact - MAYBE", "ChkRefund" };

#define CL_TRANSACTION_ID_LEN	32  // 19991104-000003ef-001 22+1 (was 23)

//kriskoin: // transactions in the ecash queue.  Each thread maintains its own information
// about ONE transaction (the one it's currently working on), as well as a few details
// about what it's currently doing.
// Adding things to the queue is done outside the object.
// Access to all of the ecash variables which are common to all the threads
// is controlled by the ecash_cs critical section.

class ECash_Thread {
public:
	ECash_Thread(int _thread_number);
	~ECash_Thread(void);

	// Main loop for the ecash thread handling object.
	void ECash_Thread::MainLoop(void);

	int processing_queue_entry_flag;	// set when we are processing queue_entry (for stat purposes)
	struct CCTQueue queue_entry;		// the queue entry we are currently processing (if any).  Zeroed when done with it.

private:
	// Attempt to automatically open the cashier.  Consecutive
	// up results are required.  Only thread 0 should call this.
	void ECash_Thread::AttemptAutoOpenCashier(void);

	// Automatically close the cashier due to errors.
	void ECash_Thread::AutoCloseCashier(void);

	// Cancel the current queue_entry.  Notify the player with
	// a pop-up, an email, and refund any pending credit transactions.
	void ECash_Thread::CancelQueueEntry(void);

	// if we got back error codes, fill the details here
	void ECash_Thread::FillErrorResultBuffer(int error_code, int sub_error_code);
	
	// return T/F, if player has exceeded his buying limits. Also informs client.
	int ECash_Thread::IsPlayerOverHisBuyingLimit(WORD32 player_id, int chips_requested);

	// Pop the next queue entry off the ecash queue and save it in queue_entry.
	// Returns TRUE if there's something new there, FALSE if the queue was empty.
	int ECash_Thread::PopNextQueueEntry(void);

	// Process our current queue_entry structure.
	void ECash_Thread::ProcessQueueEntry(void);

	// Do the actual transaction with SFC.  Parses the work to other functions.
	void ECash_Thread::TransactEcashRequest(void);

	// Process a queue entry fpr the different transaction types.
	// These functions are internal to TransactEcashRequest().
	int ECash_Thread::ProcessAdminCredit(void);
	int ECash_Thread::ProcessCredit(void);
	int ECash_Thread::ProcessPurchase(void);
        int ECash_Thread::ProcessFirePayPurchase(void);
        int ECash_Thread::ProcessFirePayCredit(void);
	

	// Test if the credit card processor is up and working.
	// Return TRUE for working, FALSE for not working.
	// We only return TRUE if we get a definite response.  A 'maybe'
	// response will force a return result of FALSE.
	int ECash_Thread::TestIfECashProviderIsUp(void);

    // Try to find a transaction number given a merchant transaction number,
    // The transaction MUST have been submitted recently because
    // one of the search criteria used to narrow it down is how many seconds
    // to look back in time.
    // This function retries for up to MAX_ECASH_RETRY_TIME seconds.
    // Returns:
    //	ERR_NONE: we found and returned the transaction number, no problem.
    //	ERR_SERIOUS_WARNING: could not determine if transaction exists
    //	ERR_ERROR: merchange transaction number does not exist.
    ErrorType ECash_Thread::TransactionFailureLookup(char *merchant_txn_number, int *output_transaction_number);

    // Try to verify whether a purchase transaction went through or not
    // given a transaction number.
    // If you still need a transaction number, see TransactionFailureLookup().
    // The transaction number and the merchant_txn_number must match
    // the original request exactly.
    // This function retries for up to MAX_ECASH_RETRY_TIME seconds.
    // Returns:
    //	ERR_NONE: transaction went through, no problem.
    //	ERR_SERIOUS_WARNING: could not determine if transaction went through
    //	ERR_ERROR: transaction did not go through.
    ErrorType ECash_Thread::TransactionQuery(int transaction_number, char *merchant_txn_number);

	// Process a request for a statement
	ErrorType ECash_Thread::TransactStatementRequest(CCTransaction *tr, WORD32 player_id);
    /*
     void ECash_Thread::AddPayPalPurchaseToDatabase(	char *vDate, char *vTime, unsigned int vHexPlayerID, char *vTransactionID,
   		char *vPaymentStatus, int vAmount, char *vPaypalAccount);
     void ECash_Thread::ProcessPayPalDeposit( );
    */
       	void ECash_Thread::AddPayPalPurchaseToDatabase(
   		char *vDate,
   		char *vTime,
   		unsigned int vHexPlayerID,
   		char *vTransactionID,
   		char *vPokerTransactionID,	//Tony, Dec 14, 2001
   		char *vPaymentStatus,
   		int vAmount,
   		char *vPaypalAccount
  		);
	void ECash_Thread::ProcessPayPalDeposit(  );

	int thread_number;				// which ecash thread are we (0-n)
	WORD32 next_ecash_up_test;		// SecondCounter when we should next test if ecash is up
	WORD32 last_admin_alert_time;	// SecondCounter when admin chat monitor last told status of ecash
	int consecutive_up_results;		// for auto-opening: # of times in a row the cashier appeared "up"
	WORD32 last_ecash_thread_launch;// SecondCounter when an ecash thread was last launched.

	#define MAX_ECASH_EMAIL_TEXT_LEN	10000
	char email_buffer[MAX_ECASH_EMAIL_TEXT_LEN];	// somewhere to store the outgoing email while we build it up (one big string)
	char message_to_send_client[MAX_MISC_CLIENT_MESSAGE_LEN+500];	// message we build to send as a pop-up to the client, 400 bytes?

	#define MAX_ECASH_ERROR_TEXT_LEN	1500	
	char ecash_error_buffer[MAX_ECASH_ERROR_TEXT_LEN];	// a detailed error explanation is built here

	INT32 pennies_amount;				// amount of current transaction (in pennies, obviously).  Should always be positive.
	INT32 chips_balance_changed;		// set if the transaction we're processing has changed the client's chip balances.
	INT32 check_amount;				// set if there was a check issued
	INT32 paypal_amount;
	ClientTransactionTR tr_index;	// the type of transaction we ended up doing
	SDBRecord player_rec;			// the SDBRecord for the player we're working on
};

class ECash_Thread *ECash_Threads[MAX_ECASH_THREADS];

//*********************************************************
// https://github.com/kriskoin//
// ECash_Thread constructor.  Make sure everything is initialized.
//
ECash_Thread::ECash_Thread(int _thread_number)
{
	thread_number = _thread_number;
	next_ecash_up_test = 15;	// don't test immediately upon startup
	last_admin_alert_time = 0;	// SecondCounter when admin chat monitor last told status of ecash
	consecutive_up_results = 0;
	last_ecash_thread_launch = 0;
	processing_queue_entry_flag = FALSE;
	zstruct(queue_entry);
	memset(email_buffer, 0, MAX_ECASH_EMAIL_TEXT_LEN);
	memset(message_to_send_client, 0, MAX_MISC_CLIENT_MESSAGE_LEN);
	memset(ecash_error_buffer, 0, MAX_ECASH_ERROR_TEXT_LEN);
	pennies_amount = 0;
	chips_balance_changed = 0;
	check_amount = 0;
	tr_index = CLTR_UNDEFINED;
	zstruct(player_rec);
}

ECash_Thread::~ECash_Thread(void)
{
	processing_queue_entry_flag = FALSE;
	zstruct(queue_entry);	// make sure it's cleared before exiting.
	memset(email_buffer, 0, MAX_ECASH_EMAIL_TEXT_LEN);
	memset(message_to_send_client, 0, MAX_MISC_CLIENT_MESSAGE_LEN);
}

//*********************************************************
// https://github.com/kriskoin//
// Attempt to automatically open the cashier.  Consecutive
// up results are required.  Only thread 0 should call this.
//
void ECash_Thread::AttemptAutoOpenCashier(void)
{
	// We should be testing regularly to see if the cashier
	// should be opened.  We do this by trying to purchase
	// something on a bogus credit card every n minutes
	// until we finally get a PurchRej, at which time we
	// assume SFC has fixed their problem and we can
	// reopen the cashier.
	// Only thread 0 is allowed to test if the cashier is up.
  #if WIN32	// for testing purposes...
	#define INTERVAL_BETWEEN_ECASH_UP_TESTS	(15)
  #else
	#define INTERVAL_BETWEEN_ECASH_UP_TESTS	(1*60)
  #endif
	if (SecondCounter >= next_ecash_up_test) {
		next_ecash_up_test = SecondCounter + INTERVAL_BETWEEN_ECASH_UP_TESTS;
		if (TestIfECashProviderIsUp()) {
			consecutive_up_results++;
		} else {
			consecutive_up_results = 0;
		}
		if (consecutive_up_results >= MAX_TRANSACTION_ATTEMPTS) {
			// They seem to be up now!
			// Open up the cashier.
			SendAdminAlert(ALERT_9, "*** OPENING CASHIER");
			int elapsed = 0;
			if (CashierCloseTime) {
				elapsed = time(NULL) - CashierCloseTime;
			}
			char subject[100];
			zstruct(subject);
			sprintf(subject, "Cashier Re-opened after %dh %dm",
					elapsed / 3600, (elapsed % 3600) / 60);
			EmailStr("sfc@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"%s CST\n\n"
					"The cashier has been re-opened automatically.\n"
					"It was closed for %dh %dm (plus up to 20 min. to detect the closure).\n"
					"\n"
					"The next time a real transaction gets approved, someone\n"
					"needs to look into any of the 'maybe' transactions.\n"
					"\n"
					"The last transaction that definitely went through was %d\n"
					"\n"
					"All 'maybe' charges and credits must be looked into manually.\n"
					,
					iRunningLiveFlag ? "":"*** THIS IS ONLY A TEST ***\n\n",
					TimeStr(),
					elapsed / 3600, (elapsed % 3600) / 60,
					iLastECashID);
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			consecutive_up_results = 0;			// reset
			ShotClockFlags &= ~SCUF_CLOSE_CASHIER;	// open the cashier.
			iShotClockChangedFlag = TRUE;			// send to clients asap.
			NextAdminStatsUpdateTime = 0;			// update/send admin stats packet asap
		} else {
			if (SecondCounter - last_admin_alert_time >= 10*60) {
				last_admin_alert_time = SecondCounter;
				SendAdminAlert(ALERT_2, "Ecash is still down.");
			}
		}
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Automatically close the cashier due to errors.
//
void ECash_Thread::AutoCloseCashier(void)
{
	// Time to close the cashier...
	ShotClockFlags |= SCUF_CLOSE_CASHIER;	// close the cashier.
	iShotClockChangedFlag = TRUE;			// send to clients asap.
	NextAdminStatsUpdateTime = 0;			// update/send admin stats packet asap
	CashierCloseTime = time(NULL);			// keep track of when cashier was closed.

	SendAdminAlert(ALERT_9, "*** CLOSING CASHIER: %d failed and %d maybe failed transactions",
			iConsecutiveFailedTransactions, iConsecutiveMaybeTransactions);
	char subject[100];
	zstruct(subject);
	sprintf(subject, "Cashier Closed - last was %d", iLastECashID);
	EmailStr("sfc@kkrekop.io",
			"Cashier",
			"cashier@kkrekop.io",
			subject,
			NULL,
			"%s"
			"%s CST\n\n"
			"The cashier has been closed automatically due to\n"
			"%d transactions which definitely failed and\n"
			"%d transactions which MAY have gone through.\n"
			"\n"
			"The server will automatically re-open the cashier as soon\n"
			"as test transactions start going through again.\n"
			"\n"
			"No further action should be needed at this time, but the\n"
			"'maybe' transaction(s) will need to be looked into when it\n"
			"does come back up.\n"
			"\n"
			"The last purchase that definitely went through was %d\n"
			"\n"
			"%d other cashier requests were in the queue and will be cancelled.\n"
			,
			iRunningLiveFlag ? "":"*** THIS IS ONLY A TEST ***\n\n",
			TimeStr(),
			iConsecutiveFailedTransactions,
			iConsecutiveMaybeTransactions,
			iLastECashID,
			Ecash_GetQueueLen());

	iConsecutiveFailedTransactions = 0;	// reset
	iConsecutiveMaybeTransactions = 0;	// reset
}

//*********************************************************
// https://github.com/kriskoin//
// Cancel the current queue_entry.  Notify the player with
// a pop-up, an email, and refund any pending credit transactions.
//
void ECash_Thread::CancelQueueEntry(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char credit_msg[100];
	char curr_str[MAX_CURRENCY_STRING_LEN];
	char refund_msg[400];
	char email_address[MAX_EMAIL_ADDRESS_LEN];
	zstruct(credit_msg);
	zstruct(curr_str);
	zstruct(refund_msg);
	zstruct(email_address);
	zstruct(player_rec);

	INT32 pennies_amount = atoi(tr->amount);
	if (tr->transaction_type == CCTRANSACTION_CASHOUT) {
		sprintf(credit_msg,"%s has been returned to your account.\n\n",
			CurrencyString(curr_str, pennies_amount, CT_REAL, TRUE));
		EnterCriticalSection(&SDB->SDBCritSec);
		SDB->TransferChips(CT_REAL,	// real money always
			tr->player_id,
			AF_PENDING_CHECK, // from pending check field
			tr->player_id,
			AF_AVAILABLE_CASH, // to available cash
			pennies_amount,
			_FL);
		LeaveCriticalSection(&SDB->SDBCritSec);
	} else {
		sprintf(credit_msg, "%s", "\n\n");
	}

	if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
		sprintf(refund_msg,
				"Transaction NOT PROCESSED:\n\n"
				"The cashier has been closed temporarily.\n\n"
				"Your transaction could not be processed\n"
				"at this time.\n\n%s"
				"Sorry for the inconvenience; we will open the\n"
				"cashier as soon as possible.",
				credit_msg);
		EcashNotifyPlayer(queue_entry.player_id, refund_msg, FALSE);
	}
	// send email
	if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) >= 0) {
		strnncpy(email_address, player_rec.email_address, MAX_EMAIL_ADDRESS_LEN);
		if (email_address[0]) {
			char curr_str1[MAX_CURRENCY_STRING_LEN];
			char curr_str2[MAX_CURRENCY_STRING_LEN];
			zstruct(curr_str1);
			zstruct(curr_str2);
			char msg[500];
			zstruct(msg);
			sprintf(msg,
				"It was necessary to temporarily close the cashier.\n"
				"\n"
				"The transaction that you had pending as of %s (CST) has been cancelled.\n"
				"\n",
				TimeStr());
			if (tr->transaction_type == CCTRANSACTION_CASHOUT) {
				sprintf(msg+strlen(msg), "The credit was not processed.  The funds have been\n"
					"returned to your account.\n");
			} else if (tr->transaction_type == CCTRANSACTION_PURCHASE) {
				sprintf(msg+strlen(msg), "The purchase was not processed.  Your card was not charged.\n");
			}
			strcat(msg, "\nThe cashier may re-open at any time.  Please feel free\n"
				"to try again at your convenience.\n\n");
			strcat(msg, "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n\n");
			strcat(msg,"If you have any questions regarding this message,\n"
				"please contact us at cashier@kkrekop.io\n");
			EmailStr(email_address,
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Transaction NOT Processed",
				NULL,
				"%s",
				msg);
		}
	}
}

void ECash_Thread::AddPayPalPurchaseToDatabase( char *vDate, char *vTime, unsigned int vHexPlayerID, char *vTransactionID,
char *vPokerTransactionID,	//Tony, Dec 14, 2001
				       char *vPaymentStatus, INT32 vAmount,  char *vPaypalAccount )
{
  CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);
	char szTransaction[CL_TRANSACTION_ID_LEN]; //	32 -> 19991104-000003ef-001
	zstruct(szTransaction);
	char hidden_cc[50];
	zstruct(hidden_cc);
	char clean_card[50];
	zstruct(clean_card);
	char spaces_card[50];
	zstruct(spaces_card);
	char address_str[300];
	zstruct(address_str);
	char auto_block_reason[300];
	zstruct(auto_block_reason);
	char ip_addr_str[20];
	char ip2_addr_str[20];
	char ip_name_str[150];
	zstruct(ip_addr_str);
	zstruct(ip2_addr_str);
	zstruct(ip_name_str);
	int err_msg_sent_already = FALSE;	// some msgs we only want to send once
	int ecash_id = 0;
	int err_code = -1;
	int sub_err_code = -1;
        char message_to_send_client[100];
        zstruct(message_to_send_client);

	
	int client_transaction_number = 0;
	
    pennies_amount=vAmount;


	EcashReplyStruct ers;
	zstruct(ers);

			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset

			// fill the transaction structure
			ClientTransaction ct;
			zstruct(ct);
			//kriskoin: 			// That's probably due to a firepay beta bug.  If it doesn't look reasonable, don't use it.
			
			ct.timestamp = time(NULL); //(ers.authTime && strlen(ers.authTime)<=10) ? atol(ers.authTime) : time(NULL);	// use now if we don't have one
			ct.partial_cc_number =0; // CCToHex(tr->card_number, hidden_cc, NULL, NULL); // convert string to 32-bit summary
			ct.transaction_amount = (ers.amount ? atoi(ers.amount) : pennies_amount);
			ct.credit_left = 0 ; //ct.transaction_amount;	// eligible to have the entire amount credited back
			
			
			// it was either successful or not -- anything other than "SP" is an error of some sort
			
//Tony			ecash_id = atoi(vTransactionID);; //(ers.txnNumber ? atoi(ers.txnNumber) : 0);i
			ecash_id = atoi(vPokerTransactionID);	//Tony, Dec 14, 2001
                        kp(("%s(%d) pokertransactionID:  '%s'\n", _FL, vTransactionID));

			ct.ecash_id = ecash_id;
			iLastECashID = ecash_id;
			// keep track of the transaction number and which thread it was
			//TransactionNumberReceived(thread_number, iLastECashID);
			ct.transaction_type = (BYTE8)CTT_PURCHASE;
			// filled the ClientTransaction -- process and log
			chips_balance_changed = TRUE;
			// log it and notify the client if possible
			kp(("%s(%d) LogPlayerTransaction:  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));
			memcpy(&ct.partial_cc_number, vTransactionID, sizeof(ct.partial_cc_number));	//Tony, Dec 12, 2001: save first 8 characters of Transaction ID
			memcpy(ct.str, vTransactionID+sizeof(ct.partial_cc_number), sizeof(ct.str));	//Tony, Dec 12, 2001: save left characters of Transaction ID
			SDB->LogPlayerTransaction(vHexPlayerID, &ct);
				// update the client if possible
				EcashSendClientInfo(vHexPlayerID);
				int cash_dep, credit_dep; // will be filled when we add the chips to the account
				 kp(("%s(%d) AddChipsToAccount:  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));
//Tony			AddChipsToAccount(vHexPlayerID,pennies_amount, CT_REAL, "c/c purchase", TRUE, &cash_dep, &credit_dep);
			AddChipsToAccount(vHexPlayerID,pennies_amount, CT_REAL, "paypal purchase", FALSE, &cash_dep, &credit_dep);
 //Tony, Dec 11, 2001: disable peel_credit
				
        kp(("%s(%d) Addchip next sentence Completed:  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));
				
				AddToEcashLog(vHexPlayerID, szTransactStr[tr_index], pennies_amount,
					spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
					ecash_id, szTransaction, err_code, sub_err_code);
               			



		sprintf(message_to_send_client,"Transaction status: Deposit through PayPal has been completed:\n\nPurchased %s on Paypal\nTransaction # %s\n\nNow you may close/minimize your Internet browser to return to the Cashier.",CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), /*hidden_cc, */ vTransactionID
				);
								

        kp(("%s(%d) sprintf completed  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));
	// try to notify the client if there's anything to tell him
	if (chips_balance_changed || message_to_send_client[0])	{	// try to notify the client
		//if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
			EcashNotifyPlayer(vHexPlayerID, message_to_send_client, chips_balance_changed);
		//}
	}

        kp(("%s(%d) AddPayPal Completed:  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));

}

void ECash_Thread::ProcessPayPalDeposit(  )
{
   static unsigned int vHexPlayerID;
   static INT32  vAmount;
   INT32 vAmount1;

   unsigned int vHexPlayerID_1;
   INT32 vAmount_1;


   char filename[60];
   char tmpfilename[30];
   char tmpfilename1[30];
   zstruct(filename);
   zstruct(tmpfilename);
   zstruct(tmpfilename1);

   int  i;
   char vDate[100];
   char vTime[100];
   char vTransactionID[100];
   char vPaymentStatus[100];
   char vPaypalAccount[100];
   char vDate_1[100];
   char vTime_1[100];
   char vTransactionID_1[100];
   char vPaymentStatus_1[100];
   char vPaypalAccount_1[100];
   char vPokerTranactionID[17];
   INT32 feeamount;



   zstruct(vDate);
   zstruct(vTime);
   zstruct(vTransactionID);
   zstruct(vPaymentStatus);
   zstruct(vPaypalAccount);
   zstruct(vDate_1);
   zstruct(vTime_1);
   zstruct(vTransactionID_1);
   zstruct(vPaymentStatus_1);
   zstruct(vPaypalAccount_1);
   zstruct(vPokerTranactionID);

   char tmp[100];
   char vcommandstr[20];
   int  vRetVal;
   int  vfind=0;
   //unsigned int tmpplayerID[100];
   zstruct(tmp);
   zstruct(vcommandstr);

   #define TRANSACTIONPATH  "Data/Trans/"
//   #define TRANSACTIONPATH  "/var/www/perl/files/transactions/"
   sprintf(vcommandstr,"ls -1 %s > tmpfilename.txt", TRANSACTIONPATH); 	
   //system("ls -1  /home/derek/perl/files/transactions  > tmpfilename.txt");
   //kp((vcommandstr));
   //kp(("\n"));
   system(vcommandstr);

   FILE *stream, *stream2, *stream3;
   //char vTransactionIDArray[];
   /* Open for read (will fail if file "data" does not exist) */
   if( (stream  = fopen( "tmpfilename.txt", "r" )) == NULL )
	return; //Tony, Dec 11, 2001

        // fgets(tmp,40,stream);
	while(!feof(stream))
	   {
		zstruct(tmpfilename1);
		sprintf(tmpfilename1,TRANSACTIONPATH);
                tmpfilename1[36]=0;
                zstruct(filename);
		fgets(filename,30,stream);
		filename[21]=0;
                if (filename[0])
                {
		
		 vAmount=0;
                 zstruct(vDate);
  		 zstruct(vTime);
   		 zstruct(vTransactionID);
   		 zstruct(vPaymentStatus);
  		 zstruct(vPaypalAccount);
  		 zstruct(vDate_1);
  		 zstruct(vTime_1);
   		 zstruct(vTransactionID_1);
  		 zstruct(vPaymentStatus_1);
   		 zstruct(vPaypalAccount_1);
                 zstruct(vPokerTranactionID);
             	 feeamount=0;		

		strcat(tmpfilename1,filename);
		strcpy(filename, tmpfilename1);
                 kp(("%s(%d) filename:  '%s'\n", _FL, filename));
           	
		   //filename[57]=0;
		   stream2=fopen(filename,"r");
			   if (stream2!=NULL) {
			   //fscanf(stream2,"%s9", vDate);
			   //vDate[8]=0;
			   fgets(tmp,25,stream2);
                           fgets(tmp,2,stream2);
			   //fgets(vTime,20, stream2);
			   //vTime[8]=0;
			   fscanf(stream2, "%x",&vHexPlayerID);
			   //tmpplayerID[78]=vHexPlayerID;
			   kp(("%s(%d) PlayerID:  '%d'\n", _FL, vHexPlayerID));
			   fgets(tmp,2,stream2);
			   fscanf(stream2, "%s",vTransactionID);
			   fgets(tmp,2,stream2);
			   fscanf(stream2, "%s",vPaymentStatus);
			   fgets(tmp,2,stream2);
			   fscanf(stream2,"%d",&vAmount);
                           //vAmount=(vAmount1 * 100.0 + 1);
			   //kp(("%s(%d) vAmount1  '%f'\n", _FL, vAmount1));
			   kp(("%s(%d) vAmount  '%d'\n", _FL, vAmount));
			   fgets(tmp,2,stream2);
			   fscanf(stream2,"%s",vPaypalAccount);
			   fgets(tmp,2,stream2);	
 			   fscanf(stream2,"%s",vPokerTranactionID);
                           fgets(tmp,2,stream2);
                           fscanf(stream2,"%d",&feeamount);	
			   for (i=0; i<8; i++)
                             vPokerTranactionID[i]=vPokerTranactionID[i+8];
			   vPokerTranactionID[8]=0;

			   //fclose(stream2);
			   kp(("%s(%d) filename:  '%s'\n", _FL, filename));
			   kp(("%s(%d) PlayerID:  '%d'\n", _FL, vHexPlayerID));	
			   kp(("%s(%d) amount:  '%d'\n", _FL, vAmount));
			   kp(("%s(%d) pokertransactionID:  '%s'\n", _FL, vPokerTranactionID));

			   //kp("Paypal");
			   //kp(filename);
			   //sleep(50);
    			   //vHexPlayerID=tmpplayerID[78];	
			
			   //sprintf(vcommandstr, "rm -f %s", filename);
			   //system(vcommandstr);
		   	   stream3=fopen("Data/Logs/transact.log","r");
			   vfind=0;
			   while(!feof(stream3)) {
			
			   	for (i=0; i<20; i++)
					{
					  vTransactionID_1[i]=0;
					}

			   fgets(vTransactionID_1,20,stream3);
			   vTransactionID_1[17]=0;
			   vRetVal=strcmp(vTransactionID_1,vTransactionID);
					if (vRetVal==0){
						vfind=1;
		   			}
				}
			
			   fclose(stream3);
			   if (vfind==0)
			   {
					stream3=fopen("Data/Logs/transact.log","a");
					fprintf(stream3,"%s\n",vTransactionID);
					fclose(stream3);
					kp(("%s(%d) addpaypalpurchase:  '%s'\n", _FL, " "));
    					kp(("%s(%d) playerID  '%d'\n", _FL, vHexPlayerID));
					kp(("%s(%d) amount:  '%d'\n", _FL, vAmount));
 					kp(("%s(%d) pokertransactionID:  '%s'\n", _FL, vPokerTranactionID));

					//vHexPlayerID=tmpplayerID[78];
					AddPayPalPurchaseToDatabase(vDate,vTime,vHexPlayerID,vTransactionID,
vPokerTranactionID,	//Tony, Dec 14, 2001
					vPaymentStatus,vAmount,vPaypalAccount);
			  		SDB->AddOrRemoveChipsInEcashFeeAccount(-feeamount);	
					SDB->AddOrRemoveChipsInEcashAccount(feeamount);

					// kriskoin : If the paypal deposit is the first $200+ deposit, ...
					if (vAmount>=20000 && DepositBonusRate>0 && \
						(!GetIniBonusStatusForPlayer(vHexPlayerID))){
                                            int bonus;
                                            int i;
                                            SDBRecord r;
                                            bonus=vAmount * 2 * DepositBonusRate / 100;
					    //Inital Deposit Bonus rate is 2*DepositBonusRate
					    if (bonus>10000) bonus=10000;	//$100 bunus maximam
                                            kp(("%s(%d) bonus: %d, deposit: %d\n", _FL, \
                                                bonus, vAmount));
                                            i = SDB->SearchDataBaseByUserID("Marketing", &r);
                                            SDB->TransferChips(r.player_id, vHexPlayerID, bonus);
                                            //Log to the transaction history
                                            ClientTransaction ct;
                                            zstruct(ct);
                                            ct.credit_left = 0;
                                            ct.timestamp = time(NULL);
                                            ct.transaction_amount = bonus;
                                            ct.transaction_type = CTT_TRANSFER_IN;
                                            ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(vHexPlayerID);
                                            strcpy(ct.str, "1st Bonus");
                                            SDB->LogPlayerTransaction(vHexPlayerID, &ct);

					    DepositBonusCashoutPoints = 2 * bonus /100;
                                            //SDB->ClearCreditFeePoints(vHexPlayerID);
                                            SDB->AddToCreditFeePoints(vHexPlayerID,DepositBonusCashoutPoints);
                                            DisableCashoutForPlayer(vHexPlayerID);
					    DisableIniBonusForPlayer(vHexPlayerID);
					}

					// end kriskoin 

                                        // kriskoin : If the paypal deposit is in promotional period, do more...
                                        time_t cur_time = time(NULL);
					kp(("%s(%d) cur_time->%d, promo_start->%d, promo_end->%d\n", _FL, \
						cur_time, DepositBonusStartDate, DepositBonusEndDate));
                                        if (cur_time > DepositBonusStartDate && cur_time < DepositBonusEndDate){
                                            int bonus;
                                            int total_bonus;
                                            int i;
                                            SDBRecord r;
                                            bonus=vAmount * DepositBonusRate / 100;
                                            total_bonus = SDB->GetTotalBonus(vHexPlayerID, \
                                                        DepositBonusStartDate, DepositBonusEndDate);
                                            if ((bonus+total_bonus)>DepositBonusMax)
                                                bonus=DepositBonusMax - total_bonus;
                                            kp(("%s(%d) bonus: %d, total_bonus: %d, deposit: %d\n", _FL, \
                                                bonus, total_bonus, vAmount));

                                            if(bonus>0){
                                            i = SDB->SearchDataBaseByUserID("Marketing", &r);
                                            SDB->TransferChips(r.player_id, vHexPlayerID, bonus);
                                            //Log to the transaction history
                                            ClientTransaction ct;
                                            zstruct(ct);
                                            ct.credit_left = 0;
                                            ct.timestamp = time(NULL);
                                            ct.transaction_amount = bonus;
                                            ct.transaction_type = CTT_TRANSFER_IN;
                                            ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(vHexPlayerID);
                                            strcpy(ct.str, "Dep Bonus");
                                            SDB->LogPlayerTransaction(vHexPlayerID, &ct);

					    DepositBonusCashoutPoints = 2 * bonus /100;
                                            //SDB->ClearCreditFeePoints(vHexPlayerID);
                                            SDB->AddToCreditFeePoints(vHexPlayerID,DepositBonusCashoutPoints);
                                            DisableCashoutForPlayer(vHexPlayerID);
					    }
                                        } // end kriskoin : otherwise, don't invoke the bonus function

					CheckRealPlayerForPlayer(vHexPlayerID);
 					
					EcashNotifyPlayer(vHexPlayerID, NULL, TRUE);
						// rgong: make sure the client's chips are updated
			  }
			//kp(("%s(%d) AddChipsToAccount:  '%d,%d'\n", _FL, vHexPlayerID,pennies_amount));
			fclose(stream2);
			sprintf(vcommandstr, "cp %s transactlog", filename);
                        system(vcommandstr);
			sprintf(vcommandstr, "rm -f %s", filename);
			//sleep(1);
                        system(vcommandstr);
		        kp(("%s(%d) finish one:  '%s'\n", _FL,vcommandstr));
		}
	   }
	}
   fclose( stream );

}


/*****************************************************************************
 Function IsPlayerOverHisBuyingLimit
 date: 24/01/01 kriskoin Purpose: return T/F, if player has exceeded his buying limits. Also informs client.
***********************************************************************************/
int ECash_Thread::IsPlayerOverHisBuyingLimit(WORD32 player_id, int chips_requested)
{
	char msg[150];
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	zstruct(msg);
	zstruct(curr_str1);
	zstruct(curr_str2);
	int purchase_limits[3], purchase_days[3];
  #if 1	// 20:::	purchase_limits[0] = GetCCPurchaseLimitForPlayer(player_id, CCLimit1Days)*100;
	purchase_limits[1] = GetCCPurchaseLimitForPlayer(player_id, CCLimit2Days)*100;
	purchase_limits[2] = GetCCPurchaseLimitForPlayer(player_id, CCLimit3Days)*100;
  #else // old way, remove
	purchase_limits[0] = CCLimit1Amount*100;
	purchase_limits[1] = CCLimit2Amount*100;
	purchase_limits[2] = CCLimit3Amount*100;
  #endif
	purchase_days[0] = CCLimit1Days;
	purchase_days[1] = CCLimit2Days;
	purchase_days[2] = CCLimit3Days;
	

    	 if (chips_requested < 5000) {
            sprintf(msg,"The Minimum deposit amount is $50.");
         }
        else{


	for (int i=0 ; i<3 ; i++) {
		int purchased = NetPurchasedInLastNHours(player_id, purchase_days[i]*24);
		if (purchased + chips_requested > purchase_limits[i]) {
			char period[20];
			zstruct(period);
			if (purchase_days[i]==1) {
				strcpy(period, "24 hour");
			} else {
				sprintf(period, "%d day", purchase_days[i]);
			}
			sprintf(msg,"TRANSACTION DECLINED:\n\n"
						"There is a %s limit of %s\n"
						"You have already purchased %s in the last %ss",
				period,
				CurrencyString(curr_str1, purchase_limits[i], CT_REAL, TRUE),
				CurrencyString(curr_str2, purchased, CT_REAL, TRUE),
				period);
		
			break;
		}
	}

	}

	  int client_transaction_number = 0;
          char szTransaction[32]; //      32 -> 19991104-000003ef-001
          zstruct(szTransaction);
         if (GetNextTransactionNumber(player_id, szTransaction, &client_transaction_number) != ERR_NONE)

         /*
               {       // can't continue
                Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL,cct->player_id);
   sprintf(szTransaction,"%08lx-%d", cct->player_id, time(NULL));  }
         */

        kp(("%s(%d) approvemessage: %d",_FL,client_transaction_number));

	if ((msg[0]==0)&&(queue_entry.cct.transaction_type == CCTRANSACTION_PURCHASE))
 	{
         sprintf(msg,"APPROVED%08lx%08d",player_id,client_transaction_number);
        }
        kp(("%s(%d) approvemessage: %s",_FL,msg));

	if (msg[0]) {
		if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
		    //if ((queue_entry.cct.transaction_type == CCTRANSACTION_PURCHASE))
	            //   {
		 	 kp(("%s(%d) Notify Message: %s",_FL, msg));
			 EcashNotifyPlayer(player_id, msg, FALSE);	// send it to the player
		    //   }
		}
		return TRUE;	// do not process.
	}
	return FALSE;	// it's ok
}

//*********************************************************
// https://github.com/kriskoin//
// Main loop for the ecash thread handling object.
//
void ECash_Thread::MainLoop(void)
{
	int consecutive_up_results = 0;

	forever {
		if (thread_number >= ECashThreads) {
			// Too many are running.  We can exit.
			break;
		}

		// Test if we should be trying to automatically close an open cashier
		if (!ECashDisabled &&
			!(ShotClockFlags & SCUF_CLOSE_CASHIER) &&
			(ShotClockFlags & SCUF_ECASH_AUTO_SHUTDOWN))
		{
			// The cashier is open and auto-up/dn is enabled.
			// Should we close it automatically due to errors?
			if (iConsecutiveFailedTransactions >= MAX_TRANSACTION_ATTEMPTS ||
				 iConsecutiveMaybeTransactions >= 3)
			{
				AutoCloseCashier();
			}
		}			

		// Test if we should be trying to automatically open a closed cashier.
		if (!ECashDisabled &&
			(ShotClockFlags & SCUF_CLOSE_CASHIER) &&
			(ShotClockFlags & SCUF_ECASH_AUTO_SHUTDOWN) &&
			!iShutdownAfterECashCompletedFlag &&
			!thread_number)
		{
			// The cashier is closed and we should try automatically opening it.
			AttemptAutoOpenCashier();
		} else {
			consecutive_up_results = 0;		// we're not trying to automatically reopen. reset
		}

		// Process Transaction Number
		ProcessPayPalDeposit();
		
		// kp(("%s(%d) ProcessPayPalDeposti:  '%s'\n", _FL,"OK"));

		// Now process anything in the queue...
		if (ECash_Queue_Head && PopNextQueueEntry()) {
			// We've found something in the queue.  Process it.
			ProcessQueueEntry();	// process this entry.
			processing_queue_entry_flag = FALSE;
			zstruct(queue_entry);	// we're done with it now.
		} else {
			// The queue is empty.  We've got nothing to process.
			if (iShutdownAfterECashCompletedFlag && !thread_number && !iShutdownAfterGamesCompletedFlag) {
				if (!(ShotClockFlags & SCUF_CLOSE_CASHIER)) {
					kp(("%s(%d) Ecash queue might be empty during shutdown. Closing cashier.\n",_FL));
					ShotClockFlags |=  SCUF_CLOSE_CASHIER;			// close cashier
					ShotClockFlags &= ~SCUF_ECASH_AUTO_SHUTDOWN;	// disable auto up/dn
					iShotClockChangedFlag = TRUE;
					NextAdminStatsUpdateTime = 0;	// update/send admin stats packet asap
				}
				if (!Ecash_GetQueueLen()) {
					// We're shutting down.  Now that the ecash has finished
					// its queue and all threads have finished processing,
					// we can start shutting down the games.
					kp(("%s(%d) Ecash queue definitely is empty. Setting iShutdownAfterGamesCompletedFlag.\n",_FL));
					iShutdownAfterGamesCompletedFlag = TRUE;
				}
			}
		}	// end of "queue empty" processing

		// If we're thread 0, launch other threads if necessary.
		if (!thread_number && SecondCounter > last_ecash_thread_launch + 10) {
			last_ecash_thread_launch = SecondCounter;
			EnterCriticalSection(&ecash_cs);
			for (int i=0 ; i<ECashThreads ; i++) {
				if (!ECash_Threads[i]) {
					// This one should be launched.
					int result = _beginthread(EcashHandlerThread, 0, (void *)i);
					if (result == -1) {
						Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
					}
				}
			}
			LeaveCriticalSection(&ecash_cs);
		}
		Sleep(300);	// avoid burning cpu time.
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Pop the next queue entry off the ecash queue and save it in queue_entry.
// Returns TRUE if there's something new there, FALSE if the queue was empty.
//
int ECash_Thread::PopNextQueueEntry(void)
{
	int found_something = FALSE;
	EnterCriticalSection(&ecash_cs);
	if (ECash_Queue_Head) {
		found_something = TRUE;
		processing_queue_entry_flag = TRUE;
		queue_entry = *ECash_Queue_Head;	// make our own copy.
		// Update the head and tail as necessary.
		CCTQueue *tmp = ECash_Queue_Head;	// keep track of original pointer
		ECash_Queue_Head = ECash_Queue_Head->next;
		if (!ECash_Queue_Head) {
			ECash_Queue_TailPtr = NULL;	// we've reached the end; reset this ptr as well
		}
		zstruct(*tmp);	// zero the memory before freeing it.
		free(tmp);		// free the memory from the queue item.
	}
	LeaveCriticalSection(&ecash_cs);
	return found_something;
}

//*********************************************************
// https://github.com/kriskoin//
// Process an admin credit transaction.  Part of TransactEcashRequest().
//
int ECash_Thread::ProcessAdminCredit(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);

	sprintf(email_buffer+strlen(email_buffer),
			"Desert Poker Admin Credit Summary -- %s CST\n\n", TimeStrWithYear());
	pennies_amount = atoi(tr->amount);
	int transaction_number = tr->admin_transaction_number_override;
	if (DebugFilterLevel <= 8) {
		kp(("%s(%d) For admin %08lx received request to credit transaction %d for %s\n",
			_FL, queue_entry.player_id, transaction_number, CurrencyString(curr_str1, pennies_amount, CT_REAL)));
	}
	sprintf(email_buffer+strlen(email_buffer),"Processing request from %s (Player ID ' %s ') to credit transaction %d for US%s.\n",
		player_rec.full_name, player_rec.user_id, transaction_number,
		CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));
	BuildCreditRequestPostString(action_string, pennies_amount, "Admin Transaction", transaction_number);
	tr_index = CLTR_CRADMIN;

	ErrorType rc_post = ERR_FATAL_ERROR;
	WORD32 first_attempt_time = SecondCounter;
	for (int attempt = 0 ;
	    attempt < MAX_TRANSACTION_ATTEMPTS || SecondCounter < first_attempt_time + MIN_ECASH_RETRY_TIME ;
	    attempt++)
    {
		// log to ecash.log
		AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], pennies_amount, "Admin", "Credit",
			"Request", "??", transaction_number, "..", 0, 0);
		WORD32 last_attempt_time = SecondCounter;
		rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_CREDIT_URL);

		//kriskoin: 		// absolutely certain that nothing got sent to it, we can be sure
		// that the transaction did NOT go through.  Retry a few times in
		// case it was merely a transient communications error.
		// Break out if it went through or we're uncertain.
		// ERR_ERROR means it definitely did NOT go through
		// ERR_SERIOUS_WARNING means it MIGHT have gone through
		if (rc_post==ERR_ERROR) {
			// Definitely did NOT go through.
			AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_ERR_TRANSACT], pennies_amount,
					"Admin", "Credit", "Request", "??", transaction_number,
					"Communication Failure", 0, 0);
			iConsecutiveFailedTransactions++;
            int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
            if (delay > 0) {
                Sleep(delay*1000);
            }
		} else {	// Went through or MAY have gone through...
			break;	// don't try again.
		}
	}

	if (rc_post == ERR_NONE) {
		EcashReplyStruct ers;
		zstruct(ers);
		ErrorType rc_parse = ParseEcashReply(&ers, result_str);
	  #if 0	//kriskoin: 		int ecash_id = (ers.txnNumber ? atoi(ers.txnNumber) : 0);
	  #endif
		int amount_left = (ers.curAmount ? atoi(ers.curAmount) : 0);
		int amount_credited = (ers.amount ? atoi(ers.amount) : 0);
		if (rc_parse == ERR_NONE) {	// parsed successfully, handle it
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			// went through fine
			if (ers.status && !stricmp(ers.status, "SP")) {	// OK!
				sprintf(email_buffer+strlen(email_buffer),
					"%s credited back to transaction %d (%s left to credit back)\n",
					CurrencyString(curr_str1,amount_credited,CT_REAL,TRUE), transaction_number,
					CurrencyString(curr_str2,amount_left,CT_REAL,TRUE));
				// log it to ecash log
				AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], amount_credited, "Admin", "Credit",
					"Successful", "!!", transaction_number, "..", 0, 0);
				PL->LogFinancialTransaction(LOGTYPE_TRANS_MANUAL_CREDIT, queue_entry.player_id, 0,
					transaction_number,	CT_REAL, "Manual Credit", NULL);
				SendAdminAlert(ALERT_2, "Admin Credit: %08lx for %s - (%d)",
					queue_entry.player_id, CurrencyString(curr_str1,amount_credited, CT_REAL,TRUE), transaction_number);
			} else {	// couldn't do it
				sprintf(email_buffer+strlen(email_buffer),
					"Nothing was credited back to transaction %d\n", transaction_number);
				// log it to ecash log
				AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], amount_credited, "Admin", "Credit",
					"Failed", "xx", transaction_number, "..", 0, 0);
				SendAdminAlert(ALERT_2, "Admin Credit FAILED: %08lx for %s - (%d)",
					queue_entry.player_id, CurrencyString(curr_str1,amount_credited, CT_REAL,TRUE), transaction_number);
			}
		} else {	// parsing error
			// We got a parse error... the transaction MAY have gone through.
			iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
			tr_index = CLTR_ERR_PARSE;
			DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
			Error(ERR_INTERNAL_ERROR, "%s(%d) ParseEcashReply returned an error!! Check transaction\n", _FL);
			strnncpy(message_to_send_client,
				"Parsing error processing your request.\n\n"
				"We don't know if it went through.",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** PARSING ERROR PROCESSING THE TRANSACTION ***\n");
			SendAdminAlert(ALERT_5, "Admin Credit PARSING ERROR: %08lx for %s - (%d)",
				queue_entry.player_id, CurrencyString(curr_str1,amount_credited, CT_REAL,TRUE), transaction_number);
		}
	} else {	// error trying to transact, but it _may_ have gone through
		tr_index = CLTR_ERR_TRANSACT_MAYBE;
		if (rc_post == ERR_SERIOUS_WARNING) {	// possibly went through
			DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
			iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
			strnncpy(message_to_send_client,
				"Transaction error:\n"
				"There was an error, but some bytes were written.\n\n"
				"We don't know if it went through.",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"This transaction may have been processed and will have to be looked into manually.\n");
			SendAdminAlert(ALERT_5, "Admin Credit MAY have worked: %08lx - (%d)",
				queue_entry.player_id, transaction_number);
		} else {	// no way it went through
			// return the chips
			strnncpy(message_to_send_client,
				"Transaction not successful:\n\n"
				"There was an error, 0 bytes were written.\n",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"This transaction was not processed.  The card was not credited.\n");
			SendAdminAlert(ALERT_5, "Admin Credit DID NOT go through: %08lx - (%d)",
				queue_entry.player_id, transaction_number);
		}
	}
	return 0;
}

//*********************************************************
// https://github.com/kriskoin//
// Process a credit transaction.  Part of TransactEcashRequest().
//
int ECash_Thread::ProcessCredit(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);
	char szTransaction[CL_TRANSACTION_ID_LEN]; //	32 -> 19991104-000003ef-001
	zstruct(szTransaction);
	#define MAX_FIXED_STRING_LEN	150
	char fixed_string1[MAX_FIXED_STRING_LEN];
	char fixed_string2[MAX_FIXED_STRING_LEN];
	zstruct(fixed_string1);
	zstruct(fixed_string2);
	int send_alert_about_nothing_credited_back = FALSE;
	int amount_credited_successfully = 0;
	
	ClientTransaction last_failed_ct;
	zstruct(last_failed_ct);
        kp(("%s(%d) Enter Process Credit\n", _FL));
	sprintf(email_buffer+strlen(email_buffer),
			"Desert Poker Cash-Out Summary -- %s CST\n\n", TimeStrWithYear());
	pennies_amount = atoi(tr->amount);
	if (DebugFilterLevel <= 8) {
		kp(("%s(%d) For player %08lx received request to cash out (%s)\n",
			_FL, queue_entry.player_id, CurrencyString(curr_str1, pennies_amount, CT_REAL)));
	}
	// log to ecash.log
	tr_index = CLTR_CRNOCREDIT;
	AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_REQ_CASHOUT], pennies_amount,
		"Player", "Cashout", "Request", "..",
		0, "..", 0, 0);
	// start the summary
	EnterCriticalSection(&ecash_cs);
	sprintf(email_buffer+strlen(email_buffer),
		"Processing request from %s (Player ID ' %s ') for a cash out of US%s.\n\n",
		player_rec.full_name, player_rec.user_id, CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));
	kp(("%s(%d) Testing Zero Cashout: For player %08lx received request to cash out (%s)\n",
                        _FL, queue_entry.player_id, CurrencyString(curr_str1, pennies_amount, CT_REAL)));
	LeaveCriticalSection(&ecash_cs);
	if (!strcmp(queue_entry.cct.unused, "check")) {
        sprintf(email_buffer+strlen(email_buffer),
		"%s\n"
		"%s\n"
		"%s%s%s"
		"%s, %s, %s\n"
		"%s\n"
		"%s\n\n",
		player_rec.full_name,
		player_rec.mailing_address1, "",
		player_rec.mailing_address2,
		player_rec.mailing_address2[0] ? "\n" : "",
		player_rec.city,
		player_rec.mailing_address_state,
		player_rec.mailing_address_country,
		player_rec.mailing_address_postal_code,
		DecodePhoneNumber(player_rec.phone_number)
	);
	}
	if (!strcmp(queue_entry.cct.unused, "paypal"))	{
         sprintf(email_buffer+strlen(email_buffer),
                "%s\n\n"
                "%s\n\n",
		player_rec.full_name,
                player_rec.email_address
        );
        }
	INT32 left_to_credit_back;
	left_to_credit_back = pennies_amount;

	int print_credit_tag = FALSE;

	// we need to reserve the funds so he has no access to them
	// we will go through his last 20 transactions and try to credit against them
	int cl_trans_num;
	int we_got_an_error;
	we_got_an_error = FALSE;	// if we need to abort
	// note: cl_trans_num is INCREMENTED INSIDE THE LOOP when we add a new
	// transaction.  That's so that once the array scrolls down due to adding
	// a new entry into it, we don't skip over any transactions.
	kp(("%s(%d) Enter for loop of  Process Credit\n", _FL));
	INT32 credit_left = 0 ;

	for ( int i=TRANS_TO_RECORD_PER_PLAYER-1; i >=0; i--) {			
	  credit_left += player_rec.transaction[i].credit_left;
	}
			
        int credit_guess = pennies_amount;
        if (credit_guess >0)  {
					         kp(("%s(%d) rc parse error is none\n", _FL));
						iConsecutiveFailedTransactions = 0;	// reset
						iConsecutiveMaybeTransactions = 0;	// reset
						// fill the new transaction structure
						ClientTransaction new_ct;
						zstruct(new_ct);
						//kriskoin: 						// That's probably due to a firepay beta bug.  If it doesn't look reasonable, don't use it.
						new_ct.timestamp = time(NULL); // (ers.authTime && strlen(ers.authTime)<=10) ? atol(ers.authTime) : time(NULL);	// use now if we don't have one
						new_ct.ecash_id = 0 ; // ecash_id;
						new_ct.partial_cc_number = 0; //old_ct.partial_cc_number;
						// it was either successful or not -- anything other than "SP" is an error of some sort
			int client_transaction_number;			
                        /*
			if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE)
                        {       // can't continue
                                Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
                                sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
                        }
 			*/
			new_ct.ecash_id = client_transaction_number;
						// added by allen
						//if (ers.status && !stricmp(ers.status, "SP")) {	// OK!
						  if (1) {
                                    		/* added by allen dec21 */	
							new_ct.transaction_amount = credit_guess; // allen(ers.amount ? atoi(ers.amount) : 0);
							// it will tell us how much is left creditable, so we'll set it to that
							//if (credit_left_on_trans != amount_left) {
   							/* allen dec 20	
								Error(ERR_WARNING,"%s %s(%d) Transaction %d says there's %d left in credit but we thought it should've been %d",
									TimeStr(), _FL, ecash_id, amount_left, credit_left_on_trans);
							*/
							//}
							// filled the ClientTransaction -- process and log
							chips_balance_changed = TRUE;
							// modify this current old ct before logging a new one
							// SDB->SetCreditLeftForTransaction(queue_entry.player_id, cl_trans_num, amount_left);
							// log this as a new transaction
						        if (!strcmp(queue_entry.cct.unused, "paypal")) {
                                                		new_ct.transaction_type = (BYTE8)CTT_CREDIT;
                                               			 kp(("\ntransction type is credit\n"));
                                                	} else if (!strcmp(queue_entry.cct.unused, "check")){
                                                		new_ct.transaction_type = (BYTE8)CTT_CHECK_ISSUED;
                                                  		kp(("\ntransction type is check\n"));
                                                	}
							//SDB->LogPlayerTransaction(queue_entry.player_id, &new_ct);
							// refresh our internal copy
							if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) < 0) {	// dunno who he is
								Error(ERR_ERROR,"%s(%d) SERIOUS ERROR: No way this should happen -- see src", _FL);
								return ERR_ERROR;
							}
							// n.b. we increment the loop counter here because LogPlayerTransaction has shifted
							// everything down one.  What used to be the next transaction number is now this
							// transaction number, so we want to make an attempt against it
							cl_trans_num++;
							// update the client if possible
							EcashSendClientInfo(queue_entry.player_id);
							// 24/01/01 kriskoin:
						kp(("%s(%d) important!! ecash transfer %d\n", _FL,new_ct.transaction_amount));
						/*	
							
							if (!strcmp(queue_entry.cct.unused, "check"))
							{
							  SDB->TransferChips(CT_REAL,	// real money always
								queue_entry.player_id,
								AF_PENDING_CHECK, // from player's pending check account
								0x0007,  //SDB->EcashRec_ID,
								AF_AVAILABLE_CASH, // to pending account cash field
								new_ct.transaction_amount,
								_FL);
							}
							
							if (!strcmp(queue_entry.cct.unused, "paypal"))
                                                        {
                                                          SDB->TransferChips(CT_REAL,   // real money always
                                                                queue_entry.player_id,
                                                                AF_PENDING_PAYPAL, // from player's pending paypal account
                                                                SDB->EcashRec_ID,
                                                                AF_AVAILABLE_CASH, // to pending account cash field
                                                                new_ct.transaction_amount,
                                                                _FL);
                                                        }

						*/
							tr_index = CLTR_APP_CREDIT;
							// deduct it from what we're trying to credit towards
							// left_to_credit_back -= new_ct.transaction_amount;
							amount_credited_successfully += new_ct.transaction_amount;

							// log it to ecash log
							AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], new_ct.transaction_amount,
								"!!", "Sub", "Credit", "Approved",
								/*old_ct.ecash_id*/ 0, szTransaction, 0, 0);
							PL->LogFinancialTransaction(LOGTYPE_TRANS_CREDIT, queue_entry.player_id, 0, new_ct.transaction_amount,
								CT_REAL, "Credit Approved", NULL);
							/*	
							sprintf(email_buffer+strlen(email_buffer),
								"%s credited back to your paypal account \n",
								//"Original transaction #%d (purchase of %s)\n"
								//"Credit transaction # %s-%d\n\n",
								CurrencyString(curr_str1,new_ct.transaction_amount,CT_REAL,TRUE)
							//	(old_ct.partial_cc_number>>16), (old_ct.partial_cc_number & 0x00FFFF),
							//	old_ct.ecash_id, CurrencyString(curr_str2,old_ct.transaction_amount,CT_REAL,TRUE),
							//	szTransaction, old_ct.ecash_id
							);
							*/
							print_credit_tag = TRUE;	// there's a credit, so evetually say how long it will take
							SendAdminAlert(ALERT_2, "CredApp: %s for %s - (%d)",
							player_rec.user_id, CurrencyString(curr_str1,/*amount_credited*/new_ct.transaction_amount, CT_REAL,TRUE), /*old_ct.ecash_id*/ 0 );
						   } 						
						
         if (!left_to_credit_back)      // we're done
              strcat(email_buffer,"\n");
        }
          kp(("%s(%d) out for loop of Process Credit\n", _FL));
  	// we've run through all of his transactions
	//if (left_to_credit_back && !we_got_an_error) {
	  if (left_to_credit_back && !we_got_an_error) {	
             strcat(email_buffer,"\n");
		// 2000201HK: we'll potentially be issuing a check
		// refresh our internal copy
		if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) < 0) {
			Error(ERR_ERROR,"%s(%d) SERIOUS ERROR: No way this should happen -- see src", _FL);
			return ERR_ERROR;
		}
		// everything we might credit back has already been transferred into the pending check field
		if (player_rec.pending_check < 0 ) { // refund it, less than $50.00 total
			sprintf(email_buffer+strlen(email_buffer),
				"Checks issued must be for at least $50.00.   %s has been returned to your account\n",
				CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE));
			AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], player_rec.pending_check,
				"cf", "Credit", "Rest", "Refunded",	0, "..", 0, 0);
			// return the chips
			SDB->TransferChips(CT_REAL,	// real money always
				queue_entry.player_id,
				AF_PENDING_CHECK, // from player's pending check account
				queue_entry.player_id,
				AF_AVAILABLE_CASH, // to his available cash field
				player_rec.pending_check,
				_FL);
			if (DebugFilterLevel <= 8) {
				kp(("%s(%d) %s has been refunded to %08lx after a cashout (not enough for a check)\n",
					_FL, CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE), queue_entry.player_id));
			}
		} else {	// issue check to pending check field
			// the amount is already in pending check field, so no need to transfer it
			SendAdminAlert(ALERT_2, "Check issued: %s (%08lx) - %s",
				player_rec.user_id, queue_entry.player_id, CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE));
			if (DebugFilterLevel <= 8) {
				kp(("%s(%d) %s has been added to the pending check field of player %08lx (%s total)\n",
					_FL, CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE), queue_entry.player_id,
					CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE) ));
			}
			int client_transaction_number = 0;

			if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE)
			{	// can't continue
				Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
				sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
			}
			// issue a pending check
			ClientTransaction check_ct;
			zstruct(check_ct);
			check_ct.credit_left = 0;
			check_ct.timestamp = time(NULL);
			check_ct.transaction_type = CTT_CHECK_ISSUED;
			check_ct.transaction_amount = left_to_credit_back;
			check_ct.ecash_id = client_transaction_number;
			kp(("%s(%d) transaction number %d\n", _FL, client_transaction_number));
			// kp((Transaction Number: %d\n", client_transaction_number));
			tr_index = CLTR_CHECK;
			 // log this as a new transaction  allen
                         if (!strcmp(queue_entry.cct.unused, "paypal")) {
                               check_ct.transaction_type = (BYTE8)CTT_CREDIT;
                                kp(("\ntransction type is credit\n"));
                         } else if (!strcmp(queue_entry.cct.unused, "check")){
                                check_ct.transaction_type = (BYTE8)CTT_CHECK_ISSUED;
                                kp(("\ntransction type is check\n"));
                        }
			AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], check_ct.transaction_amount,
				"$$", "Pending", "Check", "Issued", check_ct.ecash_id, szTransaction, 0, 0);
			PL->LogFinancialTransaction(LOGTYPE_TRANS_CHECK, queue_entry.player_id, 0, check_ct.transaction_amount,
				CT_REAL, "Pending Check Issued", NULL);
			// log this as a new transaction
			kp(("log player transaction file"));
			SDB->LogPlayerTransaction(queue_entry.player_id, &check_ct);
			kp(("%s(%d) transaction amount %d\n", _FL, left_to_credit_back));	
		        /*			
                        for ( int i=TRANS_TO_RECORD_PER_PLAYER-1; i >=0; i--) {
			 kp(("%s(%d) transaction credit left: %d\n", _FL, player_rec.transaction[i].credit_left ));
         			 if ( player_rec.transaction[i].credit_left > left_to_credit_back) {
				      player_rec.transaction[i].credit_left
					= player_rec.transaction[i].credit_left - left_to_credit_back;
				      left_to_credit_back = 0;	
				 }
			         else {
					left_to_credit_back = left_to_credit_back - player_rec.transaction[i].credit_left;	
					player_rec.transaction[i].credit_left = 0;

					}
				if (left_to_credit_back == 0)
					break;
        		}			

			*/
			SDB->SetCreditPoints(queue_entry.player_id, left_to_credit_back);
			// update the client if possible
			EcashSendClientInfo(queue_entry.player_id);
			// notify us to issue the check
			if (!strcmp(queue_entry.cct.unused, "check")){
			sprintf(email_buffer+strlen(email_buffer),
				"\nA check will be issued in the amount of %s (check transaction # %s)\n",
				CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE), szTransaction);
			if (player_rec.pending_check > left_to_credit_back) {
				sprintf(email_buffer+strlen(email_buffer),
					"Your outstanding checks at the moment total %s -- a single check will be issued for the total.\n",
					CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE));
			}
			strcat(email_buffer,"Please expect between 10 and 15 business days for delivery of your check.\n\n");
			check_amount = left_to_credit_back;
			}
   		        if (!strcmp(queue_entry.cct.unused, "paypal")) {
			 sprintf(email_buffer+strlen(email_buffer),
                                "A PayPal credit will be issued to the displayed Email Account in the amount of %s (paypal transaction # %s).\n",
                                CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE), szTransaction);
                        if (player_rec.pending_paypal > left_to_credit_back) {
                                sprintf(email_buffer+strlen(email_buffer),
                                        "Your outstanding PayPal Cash-Out requests  at the moment total %s --a single PayPal credit will be issued for the total. \n",
                                        CurrencyString(curr_str1, player_rec.pending_paypal, CT_REAL,TRUE));
                        }
                        // strcat(email_buffer,"Please expect between 10 and 15 business days for delivery of your check.\n\n");
			 strcat(email_buffer,
                        "Please expect between 1 to 5 days for the transaction to complete.\n");

                        paypal_amount = left_to_credit_back;
			}
	
		}
	}

	// potential credit footer
	/*
	if (print_credit_tag) {
		strcat(email_buffer,
			"It will take 3 to 5 business days for the credit to appear on your paypal account.\n"
			ECASH_PROCESSOR_NAME" will submit the transaction within 6 hours to\n"
			"paypal company. The paypal company and your bank require\n"
			"3 to 5 days to process it.\n");
	}
	*/
	if (send_alert_about_nothing_credited_back) {
		EmailStr(
			"support@kkrekop.io",
			"Cashier",
			"cashier@kkrekop.io",
			"Failed credit needs looking into",
			NULL,
			"%s"
			"%s (CST)\n"
			"\n"
			"We just attempted to do some credits against some earlier purchases\n"
			"and the credits DID NOT go through, even though we expected them to.\n"
			"\n"
			"As far as the client is concerned, everything is perfectly normal, but\n"
			"this error should not happen under normal circumstances and so this\n"
			"particular credit should be looked into manually so we can try to prevent\n"
			"them from occurring in the future.  This is not an urgent error, however\n"
			"it should be looked into within a week.\n"
			"\n"
			"As there may have been more that one credit attempt that failed for this\n"
			"player, the details below are for the last failure seen.\n"
			"\n"			
			"Details:\n"
			"\n"
			"     User id: %s\n"
			"     Original transaction date: %s\n"
			"     Original transaction #: %d\n"
			"     Original transaction purchase amount: %s\n"
			"     Credit apparently available on the transaction: %s\n"
			"     Credit attempt amount: %s\n"
			,
			iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
			TimeStr(),
			player_rec.user_id,
			TimeStr(last_failed_ct.timestamp,TRUE),
			last_failed_ct.ecash_id,
			CurrencyString(curr_str1, last_failed_ct.transaction_amount, CT_REAL),
			CurrencyString(curr_str2, last_failed_ct.credit_left, CT_REAL),
			CurrencyString(curr_str3, pennies_amount, CT_REAL)
		);
	}
	kp(("%s(%d) exit Process Credit", _FL));

	return 0;
}


//*********************************************************
// https://github.com/kriskoin//
// Process a credit transaction.  Part of TransactEcashRequest().
//
int ECash_Thread::ProcessFirePayCredit(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);
	char szTransaction[CL_TRANSACTION_ID_LEN]; //	32 -> 19991104-000003ef-001
	zstruct(szTransaction);
	#define MAX_FIXED_STRING_LEN	150
	char fixed_string1[MAX_FIXED_STRING_LEN];
	char fixed_string2[MAX_FIXED_STRING_LEN];
	zstruct(fixed_string1);
	zstruct(fixed_string2);
	int send_alert_about_nothing_credited_back = FALSE;
	int amount_credited_successfully = 0;
	
	ClientTransaction last_failed_ct;
	zstruct(last_failed_ct);
        kp(("%s(%d) Enter FIREPAY CASHOUT  \n", _FL ));
	
	sprintf(email_buffer+strlen(email_buffer),
			"Desert Poker Cash Out Summary -- %s CST\n\n", TimeStrWithYear());
	pennies_amount = atoi(tr->amount);
	if (DebugFilterLevel <= 8) {
		kp(("%s(%d) For player %08lx received request to cash out (%s)\n",
			_FL, queue_entry.player_id, CurrencyString(curr_str1, pennies_amount, CT_REAL)));
	}
	// log to ecash.log
	tr_index = CLTR_CRNOCREDIT;
	AddToEcashLog(queue_entry.playDate: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ er_id, szTransactStr[CLTR_REQ_CASHOUT], pennies_amount,
		"Player", "Cashout", "Request", "..",
		0, "..", 0, 0);
	
	// start the summary
	/*
         sprintf(email_buffer + strlen(email_buffer),
                "This email is sent as a confirmation of a credit card purchase/deposit to the\n following"
                " Player Account:"
                "\n\nPlayer ID : %s\nEmail : %s\n\n%s\n%s\n%s, %s\n%s %s\n\n",
                player_rec.user_id, player_rec.email_address, player_rec.full_name, player_rec.mailing_address1,
                player_rec.city, player_rec.mailing_address_state,
                player_rec.mailing_address_country,
                player_rec.mailing_address_postal_code
                 );
	*/
	sprintf(email_buffer + strlen(email_buffer),
                "This email is sent as a confirmation of a credit card Cash-\nOut/wtithdrawal to the following Player Account:\n\n"
                "\n\nPlayer ID : %s\nEmail : %s\n\n%s\n%s\n%s, %s\n%s\n\%s\n\nHere are the completed transaction details:\n\n",
                player_rec.user_idDate: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ , player_rec.email_address, player_rec.full_name, player_rec.mailing_address1,
                player_rec.city, player_rec.mailing_address_state,
                player_rec.mailing_address_country,
                player_rec.mailing_address_postal_code
                );

	int left_to_credit_back;
	left_to_credit_back = pennies_amount;

	int print_credit_tag = FALSE;

	// we need to reserve the funds so he has no access to them
	// we will go through his last 20 transactions and try to credit against them
	int cl_trans_num;
	int we_got_an_error;
	we_got_an_error = FALSE;	// if we need to abort
	// note: cl_trans_num is INCREMENTED INSIDE THE LOOP when we add a new
	// transaction.  That's so that once the array scrolls down due to adding
	// a new entry into it, we don't skip over any transactions.
	for (cl_trans_num=TRANS_TO_RECORD_PER_PLAYER-1; cl_trans_num >= 0; cl_trans_num--) {
		// 24/01/01 kriskoin:
		// so we'll make a local copy and use that instead
		ClientTransaction old_ct = player_rec.transaction[cl_trans_num];
		if (old_ct.ecash_id) {
			// check if there anything left to credit here...
			// 24/01/01 kriskoin:
			// creditable amount greater than the original purchase amount... so if we find that, we skip it
			// that is controlled via the admin client which knows how to deal with (and display) bigger creditable amounts
			if (((old_ct.transaction_type == CTT_CC_PURCHASE)||(old_ct.transaction_type == CTT_FIREPAY_PURCHASE)) && old_ct.credit_left > 0 && old_ct.transaction_amount >= old_ct.credit_left) {	// live to have $ credited against it
				// build credit request string based on this transaction and what's left
				int client_transaction_number = 0;
				if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE) {	
					// can't continue
					Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
					sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
				}
				int credit_desired = min((int)(old_ct.credit_left), left_to_credit_back);
				BuildCreditRequestPostString(action_string, credit_desired, szTransaction, old_ct.ecash_id);
				//kp((action_string));//temp
				ErrorType rc_post = ERR_FATAL_ERROR;
            	WORD32 first_attempt_time = SecondCounter;
            	for (int attempt = 0 ;
            	    attempt < MAX_TRANSACTION_ATTEMPTS || SecondCounter < first_attempt_time + MIN_ECASH_RETRY_TIME ;
            	    attempt++)
                {
					// log to ecash.log
					AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_REQ_CREDIT], credit_desired,
						"??", "Sub", "Credit", "Request",
						old_ct.ecash_id, szTransaction, 0, 0);
					/* POST THE CREDIT TRANSACTION */
            		WORD32 last_attempt_time = SecondCounter;
				  #if WIN32 && 0
					kp(("%s(%d) *** WARNING: ALL CREDITS SET TO FAIL WITH COMMUNICATIONS ERRORS\n", _FL));
					rc_post = ERR_ERROR;
				  #else
					rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_CREDIT_URL);
				  #endif
					//kriskoin: 					// absolutely certain that nothing got sent to it, we can be sure
					// that the transaction did NOT go through.  Retry a few times in
					// case it was merely a transient communications error.
					// Break out if it went through or we're uncertain.
					// ERR_ERROR means it definitely did NOT go through
					// ERR_SERIOUS_WARNING means it MIGHT have gone through
				  #if WIN32 && 0
					kp(("%s(%d) *** WARNING: TREATING ALL CREDITS AS MAYBEs\n", _FL));
					rc_post = ERR_Date: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ SERIOUS_WARNING;
				  #endif
					if (rc_post==ERR_ERROR) {
						// Definitely did NOT go through.
						AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_ERR_TRANSACT], credit_desired,
							"??", "Sub", "Credit", "Request",
							old_ct.ecash_id, "Communication Failure", 0, 0);
						iConsecutiveFailedTransactions++;
                        int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
                        if (delay > 0) {
                            Sleep(delay*1000);
                        }
					} else {	// Went through or MAY have gone through...
						break;	// don't try again.
					}
				}

				if (rc_post == ERR_NONE) {
					/* RECEIVED A VALID RESPONSE -- PROCESS IT */
					EcashReplyStruct ers;
					zstruct(ers);
 					
					kp(("%s(%d) *** Enter rc_post == ERR_NONE\n", _FL));

					ErrorType rc_parse =  ParseEcashReply(&ers, result_str);
					
					kp(("%s(%d) *** rc_parse: %d\n", _FL, rc_parse));
					
					int ecash_id = (ers.txnNumber ? atoi(ers.txnNumber) : 0);
					int amount_left = (ers.curAmount ? atoi(ers.curAmount) : 0);
					int amount_credited = (ers.amount ? atoi(ers.amount) : 0);
					int creditNumber = (ers.creditNumber ? atoi(ers.creditNumber) : 0);
					
					//kp(("%s(%d) ***CreditNumber: %s\n", _FL, creditNumber));
					//kp(("%s(%d) ***amount_creditedNumber: %s\n", _FL, creditNumber));


					/*
					int ecash_id = (ers.txnNumber ? atoi(ers.txnNumber) : 0);
					int amount_left = (ers.curAmount ? atoi(ers.curAmount) : 0);
					int amount_credited = (ers.amount ? atoi(ers.amount) : 0);
					int creditNumber = (ers.creditNumber ? atoi(ers.creditNumber) : 0);
					*/

					if (creditNumber && DebugFilterLevel <= 8) {
					kp(("%s(%d) *** NEW 1.03 CREDIT RESULT FIELD: creditNumber = %d (we should be able to query it) against original transaction #%d\n", _FL, creditNumber, ecash_id));
					}
				
					int credit_left_on_trans = old_ct.credit_left - amount_credited;
 					kp(("%s(%d) ***Credit left on trans: %d\n", _FL, credit_left_on_trans));
					kp(("%s(%d) ***Old Credit left: %d\n", _FL, old_ct.credit_left));
					kp(("%s(%d) ***amount_credited: %d\n", _FL, amount_credited));

					if (rc_parse =Date: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ = ERR_NONE) {	// parsed successfully, handle it
						iConsecutiveFailedTransactions = 0;	// reset
						iConsecutiveMaybeTransactions = 0;	// reset
						// fill the new transaction structure
						ClientTransaction new_ct;
						zstruct(new_ct);
						//kriskoin: 						// That's probably due to a firepay beta bug.  If it doesn't look reasonable, don't use it.
						new_ct.timestamp = (ers.authTime && strlen(ers.authTime)<=10) ? atol(ers.authTime) : time(NULL);	// use now if we don't have one
						new_ct.ecash_id = ecash_id;
						new_ct.partial_cc_number = old_ct.partial_cc_number;
						if (old_ct.transaction_type == CTT_CC_PURCHASE) {
							new_ct.transaction_type = (BYTE8)CTT_CC_CREDIT;
						}else if(old_ct.transaction_type == CTT_FIREPAY_PURCHASE) {
						        new_ct.transaction_type = (BYTE8)CTT_FIREPAY_CREDIT;
						}
					        kp(("%s(%d) *** old  Transaction Type: %d \n", _FL, old_ct.transaction_type));
						kp(("%s(%dDate: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ ) *** New  Transaction Type: %d \n", _FL, new_ct.transaction_type));

						
						// it was either successful or not -- anything other than "SP" is an error of some sort
						if (ers.status && !stricmp(ers.status, "SP")) {	// OK!
						
							new_ct.transaction_amount = (ers.amount ? atoi(ers.amount) : 0);
							// it will tell us how much is left creditable, so we'll set it to that
							if (credit_left_on_trans != amount_left) {
								Error(ERR_WARNING,"%s %s(%d) Transaction %d says there's %d left in credit but we thought it should've been %d",
									TimeStr(), _FL, ecash_id, amount_left, credit_left_on_trans);
							}
							// filled the ClientTransaction -- process and log
							chips_balance_changed = TRUE;
							// modify this current old ct before logging a new one
							SDB->SetCreditLeftForTransaction(queue_entry.player_id, cl_trans_num, amount_left);
							// log this as a new transaction
							SDB->LogPlayerTransaction(queue_entry.player_id, &new_ct);
							// refresh our internal copy
							if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) < 0) {	// dunno who he is
								Error(ERR_ERROR,"%s(%d) SERIOUS ERROR: No way this should happen -- see src", _FL);
								return ERR_ERROR;
							}
							// n.b. we increment the loop counter here because LogPlayerTransaction has shifted
							// everything down one.  What used to be the next transaction number is now this
							// transaction number, so we want to make an attempt against it
							cl_trans_num++;
							// update the client if possible
							EcashSendClientInfo(queue_entry.player_id);
							// 24/01/01 kriskoin:
							SDB->TransferChips(CT_REAL,	// real money always
								queue_entry.player_id,
								AF_PENDING_PAYPAL, // player's pending check account
								SDB->EcashRec_ID,
								AF_AVAILABLE_CASH, // to pending account cash field
								new_ct.transaction_amount,
								_FL);

							tr_index = CLTR_APP_CREDIT;
							// deduct it from what we're trying to credit towards
							left_to_credit_back -= new_ct.transaction_amount;
							amount_credited_successfully += new_ct.transaction_amount;

							// log it to ecash log
							AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], new_ct.transaction_amount,
								"!!", "Sub", "Credit", "Approved",
								old_ct.ecash_id, szTransaction, 0, 0);
							PL->LogFinancialTransaction(LOGTYPE_TRANS_CREDIT, queue_entry.player_id, 0, new_ct.transaction_amount,
								CT_REAL, "Credit Approved", NULL);
							sprintf(email_buffer+strlen(email_buffer),
								"%s credited back to your credit card (%04x...%04x)\n"
								"Original transaction #%d (purchase of %s)\n"
								"Credit transaction # %s-%d\n\n",
								CurrencyString(curr_str1,new_ct.transaction_amount,CT_REAL,TRUE),
								(old_ct.partial_cc_number>>16), (old_ct.partial_cc_number & 0x00FFFF),
								old_ct.ecash_id, CurrencyString(curr_str2,old_ct.transaction_amount,CT_REAL,TRUE),
								szTransaction, old_ct.ecash_id);
							print_credit_tag = TRUE;	// there's a credit, so evetually say how long it will take
							SendAdminAlert(ALERT_2, "CredApp: %s for %s - (%d)",
								player_rec.user_id, CurrencyString(curr_str1,amount_credited, CT_REAL,TRUE), old_ct.ecash_id);
						} else {	// transaction rejected
							int err_code = (ers.errCode ? atoi(ers.errCode) : -1);
							int sub_err_code = (ers.subError ? atoi(ers.subError) : -1);
							tr_index = CLTR_REJ_CREDIT;
							AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], credit_desired,
								"xx", "SubCreditRejected",
								(ers.errString ? ECash_FixReplyString(ers.errString,fixed_string1,MAX_FIXED_STRING_LEN) : "??"),
								(ers.subErrorString ? ECash_FixReplyString(ers.subErrorString,fixed_string2,MAX_FIXED_STRING_LEN) : "??") ,
								old_ct.ecash_id, szTransaction, err_code, sub_err_code);
							sprintf(email_buffer+strlen(email_buffer),
								"Nothing credited back to your credit card against transaction #%d (original purchase of %s).\n\n",
								old_ct.ecash_id, CurrencyString(curr_str2,old_ct.transaction_amount,CT_REAL,TRUE));
							SendAdminAlert(ALERT_2, "CredRej: %s - (%d)", player_rec.user_id, old_ct.ecash_id);
							// we could set it to 0 and never try again, but it's in our best interest
							// to try again forever so we'll just let it do that...

							//kriskoin: 							// them manually and make sure there's not anything weird going on.
							send_alert_about_nothing_credited_back = TRUE;
							last_failed_ct = old_ct;
						}					
					} else {
						// We got a parse error... the transaction MAY have gone through.
						iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
						DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
						we_got_an_error = TRUE;
						tr_index = CLTR_ERR_PARSE;
						AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], 0,
							"xx", "SubCreditErrParse",
							(ers.errString ? ECash_FixReplyString(ers.errString,fixed_string1,MAX_FIXED_STRING_LEN) : "??"),
							(ers.subErrorString ? ECash_FixReplyString(ers.subErrorString,fixed_string2,MAX_FIXED_STRING_LEN) : "??") ,
							old_ct.ecash_id, szTransaction, 0, 0);
						Error(ERR_INTERNAL_ERROR, "%s(%d) ParseEcashReply returned an error!! Check transaction\n", _FL);
					  #if 1	// 2022 kriskoin
						strnncpy(message_to_send_client,
							"Transaction error:\n"
							"\n"
							"There was an error contacting our ecash provider.\n"
							"\n"
							"This transaction will need to be looked into manually.\n"
							"\n"
							"Please contact cashier@kkrekop.io",
							MAX_MISC_CLIENT_MESSAGE_LEN);
						strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
						strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io as this transaction will have to be looked into manually.\n");
						left_to_credit_back = 0;	// this will exit out of the loop
					  #else
						strnncpy(message_to_send_client,
							"There was an error attempting to process your request.  Please try again later",
							MAX_MISC_CLIENT_MESSAGE_LEN);
						strcat(email_buffer,"\n*** THERE WAS AN ERROR PROCESSING YOUR TRANSACTION ***\n");
						strcat(email_buffer,"This transaction was not processed.  Your card will not be credited.\n");
						strcat(email_buffer,"This problem is temporary.  Please try again in a few minutes.\n");
					  #endif
						SendAdminAlert(ALERT_5, "Credit MAY have worked: %s (%08lx) - (%d)",
							player_rec.user_id, queue_entry.player_id, old_ct.ecash_id);
					}
				} else if (rc_post == ERR_SERIOUS_WARNING) {	// possibly went through
					we_got_an_error = TRUE;
				  #if 1	// 2022 kriskoin
					//kriskoin: 					// We don't yet know whether this code will actually work.  So far,
					// we've been unable to actually see any results from a failure lookup
					// on a credit.  Once we get this code working with the real server
					// we may find that we get some meaningful information.  Who knows.
					int looked_up_transaction_number = 0;
					ErrorType verify_result = TransactionFailureLookup(szTransaction, &looked_up_transaction_number);
					kp(("%s %s(%d) Credit: TransactionFailureLookup(%s) returned %d with a transaction number of %d\n",
								TimeStr(), _FL, szTransaction, verify_result, looked_up_transaction_number));
					if (verify_result==ERR_NONE) {
						// It looked up ok... query it.
						verify_result = TransactionQuery(looked_up_transaction_number, szTransaction);
						kp(("%s %s(%d) Credit: TransactionQuery(%d, %s) returned %d.\n",
									TimeStr(), _FL, looked_up_transaction_number, szTransaction, verify_result));
					}
				   #if WIN32 && 0
					kp(("%s(%d) *** CONTINUING TO TREAT ALL CREDITS AS MAYBE'S!\n", _FL));
					verify_result = ERR_SERIOUS_WARNING;
				   #endif
				  #endif
					tr_index = CLTR_ERR_TRANSACT_MAYBE;
					iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
					DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
					AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], 0,
						"xx", "SubDate: [A-Z]{2}\d{1,2}\/\d{1,2}\/\d{2,4}\ CreditErrTransact",
						"maybe", "processed",	old_ct.ecash_id, szTransaction, 0, 0);
					strnncpy(message_to_send_client,
						"Transaction error:\n"
						"\n"
						"There was an error contacting our ecash provider.\n"
						"\n"
						"This transaction will need to be looked into manually.\n"
						"\n"
						"Please contact cashier@kkrekop.io",
						MAX_MISC_CLIENT_MESSAGE_LEN);
					strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
					strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io as this transaction will have to be looked into manually.\n");
					left_to_credit_back = 0;	// this will exit out of the loop
					SendAdminAlert(ALERT_7, "Credit MAY have worked: %s (%08lx) - (%d)",
						player_rec.user_id, queue_entry.player_id, old_ct.ecash_id);

					// Send an email to support to indicate this maybe purchase must be looked into
					char subject[200];
					zstruct(subject);
					sprintf(subject, "%sCashier: %s had a maybe credit for %s",
							iRunningLiveFlag ? "" : "Test: ",
							player_rec.user_id,
							CurrencyString(curr_str1, credit_desired, CT_PLAY, TRUE));
					EmailStr(
							"management@kkrekop.io",
							"Cashier",
							"cashier@kkrekop.io",
							subject,
							NULL,
							"%s"
							"%s (CST)\n"
							"\n"
							"User id %s had a cc credit which MAY or MAY NOT have gone through.\n"
							"\n"
							"This transaction MUST be looked into manually.  The client is\n"
							"LOCKED OUT OF THE CASHIER until this issue is resolved.\n"
							"The server attempted to determine the outcome of this transaction itself\n"
							"for up to 10 minutes but was unable to get a decent answer from\n"
							"the transaction server.  It has now given up and passed the problem\n"
							"on to the customer support department (you) for manual resolution.\n"
							"\n"
							"Details:\n"
							"\n"
							"    User ID: %s\n"
							"    Credit amount: %s\n"
							"    Merchant transaction number for this credit: %s\n"
							"    Original purchase transaction number: %d (for %s)\n"
							"    Credit left on that transaction if this DID NOT go through: %s\n"
							"    Credit left on that transaction if this DID go through: %s\n"
							"\n"
							"The money has been left in the client's pending paypal field until\n"
							"this problem gets looked into.  If the transaction went through, the\n"
							"exact amount of the transaction should be moved into ecash.\n"
							"If the transaction did not go through, the entire amount of the pending\n"
							"paypal field can be moved back into their available field.\n"
							"\n"
							"We're not sure yet, but we THINK that we don't want the refund to show\n"
							"up as a transaction in their account.  It all depends on what shows up\n"
							"w.r.t. the cashout attempt.\n"
							"\n"
							"When finished dealing with this, the client's pending paypal field should\n"
							"be zero.  If it's not, then call someone and discuss it.\n"
							,
							iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n",
							TimeStrWithYear(),
							player_rec.user_id,
							player_rec.user_id,
							CurrencyString(curr_str1, credit_desired, CT_REAL),
							szTransaction,
							old_ct.ecash_id,
							CurrencyString(curr_str2, old_ct.transaction_amount, CT_REAL),
							CurrencyString(curr_str3, old_ct.credit_left, CT_REAL),
							CurrencyString(curr_str4, old_ct.credit_left - credit_desired, CT_REAL));
				} else {	// this credit failed and there is no way it went through
					we_got_an_error = TRUE;
					tr_index = CLTR_ERR_TRANSACT;
					AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], 0,
						"xx", "SubCreditErrTransact",
						"no way", "processed",	old_ct.ecash_id, szTransaction, 0, 0);
					SendAdminAlert(ALERT_5, "Credit DID NOT go through: %s (%08lx) - (%d)",
						player_rec.user_id, queue_entry.player_id, old_ct.ecash_id);
					// return the chips
					if (left_to_credit_back) {
						SDB->TransferChips(CT_REAL,	// real money always
							queue_entry.player_id,
							AF_PENDING_PAYPAL, 		// from player's pending check account
							queue_entry.player_id,
							AF_AVAILABLE_CASH, 		// to his available cash field
							left_to_credit_back,
							_FL);
					}

					strnncpy(message_to_send_client,
						"Transaction not successful:\n\n"
						"There was an error contacting our ecash provider.\n"
						"Please try again later",
						MAX_MISC_CLIENT_MESSAGE_LEN);
					strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
					strcat(email_buffer,"This transaction was not processed.  Your card was not credited.\n\n");

					sprintf(email_buffer+strlen(email_buffer),
							"Summary:\n\n"
							"Original cashout request: %s\n"
							"Amount successfully credited: %s\n"
							,
							CurrencyString(curr_str1, pennies_amount, CT_REAL),
							CurrencyString(curr_str2, amount_credited_successfully, CT_REAL));

					if (left_to_credit_back) {
						sprintf(email_buffer+strlen(email_buffer),
							"Amount not credited: %s\n"
							"\n"
							"The %s that was not credited has been returned to your account.\n"
							,
							CurrencyString(curr_str1, left_to_credit_back, CT_REAL),
							CurrencyString(curr_str2, left_to_credit_back, CT_REAL));
					}
					strcat(email_buffer, "\n");

					time_t now = time(NULL);
					struct tm tm;
					zstruct(tm);
					struct tm *t = localtime(&now, &tm);
					if (t && t->tm_wday==1 && t->tm_hour >= 2 && t->tm_hour < 7) {
						strcat(email_buffer,
							"On Monday mornings from 2am to 6am (CST), the "ECASH_PROCESSOR_NAME" transaction\n"
							"server is down for maintenence. Please try again after the maintenence\n"
							"is completed.\n");
					} else {
						strcat(email_buffer,"This problem is probably temporary. Please feel free to try again in a few minutes.\n");
					}

					left_to_credit_back = 0;	// this will exit out of the loop
				}
			}
		}
		if (!left_to_credit_back) {	// we're done
			strcat(email_buffer,"\n");
			break;
		}
	}
	// we've run through all of his transactions
	if (left_to_credit_back && !we_got_an_error) {
		// 2000201HK: we'll potentially be issuing a check
		// refresh our internal copy
		if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) < 0) {
			Error(ERR_ERROR,"%s(%d) SERIOUS ERROR: No way this should happen -- see src", _FL);
			return ERR_ERROR;
		}
		// everything we might credit back has already been transferred into the pending check field
		if (player_rec.pending_check < 5000) { // refund it, less than $50.00 total
			sprintf(email_buffer+strlen(email_buffer),
				"Checks issued must be for at least $50.00.   %s has been returned to your account\n",
				CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE));
			AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], player_rec.pending_check,
				"cf", "Credit", "Rest", "Refunded",	0, "..", 0, 0);
			// return the chips
			SDB->TransferChips(CT_REAL,	// real money always
				queue_entry.player_id,
				AF_PENDING_CHECK, // from player's pending check account
				queue_entry.player_id,
				AF_AVAILABLE_CASH, // to his available cash field
				player_rec.pending_check,
				_FL);
			if (DebugFilterLevel <= 8) {
				kp(("%s(%d) %s has been refunded to %08lx after a cashout (not enough for a check)\n",
					_FL, CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE), queue_entry.player_id));
			}
		} else {	// issue check to pending check field
			// the amount is already in pending check field, so no need to transfer it
			SendAdminAlert(ALERT_2, "Check issued: %s (%08lx) - %s",
				player_rec.user_id, queue_entry.player_id, CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE));
			if (DebugFilterLevel <= 8) {
				kp(("%s(%d) %s has been added to the pending check field of player %08lx (%s total)\n",
					_FL, CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE), queue_entry.player_id,
					CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE) ));
			}
			int client_transaction_number = 0;
			if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE) {	// can't continue
				Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
				sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
			}
			// issue a pending check
			ClientTransaction check_ct;
			zstruct(check_ct);
			check_ct.credit_left = 0;
			check_ct.timestamp = time(NULL);
			check_ct.transaction_type = CTT_CHECK_ISSUED;
			check_ct.transaction_amount = left_to_credit_back;
			check_ct.ecash_id = client_transaction_number;
			tr_index = CLTR_CHECK;
			AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], check_ct.transaction_amount,
				"$$", "Pending", "Check", "Issued", check_ct.ecash_id, szTransaction, 0, 0);
			PL->LogFinancialTransaction(LOGTYPE_TRANS_CHECK, queue_entry.player_id, 0, check_ct.transaction_amount,
				CT_REAL, "Pending Check Issued", NULL);
			// log this as a new transaction
			SDB->LogPlayerTransaction(queue_entry.player_id, &check_ct);
			// update the client if possible
			EcashSendClientInfo(queue_entry.player_id);
			// notify us to issue the check
			sprintf(email_buffer+strlen(email_buffer),
				"\nA check will be issued in the amount of %s (check transaction # %s)\n",
				CurrencyString(curr_str1, left_to_credit_back, CT_REAL,TRUE), szTransaction);
			if (player_rec.pending_check > left_to_credit_back) {
				sprintf(email_buffer+strlen(email_buffer),
					"Your outstanding checks at the moment total %s -- a single check will be issued for the total.\n",
					CurrencyString(curr_str1, player_rec.pending_check, CT_REAL,TRUE));
			}
			strcat(email_buffer,"Please expect between 10 and 15 business days for delivery of your check.\n\n");
			check_amount = left_to_credit_back;
		}
	}

	// potential credit footer
	if (print_credit_tag) {
		strcat(email_buffer,
			"It will take 3 to 5 business days for the credit to appear on you card.\n"
			"Desert Poker submits this transaction within moments of your Cash-Out\n"
			"request. The credit card companies and your bank require 3 to 5 days to\n"
			"process it.\n");
	}

	if (send_alert_about_nothing_credited_back) {
		EmailStr(
			"management@kkrekop.io",
			"Cashier",
			"cashier@kkrekop.io",
			"Failed credit needs looking into",
			NULL,
			"%s"
			"%s (CST)\n"
			"\n"
			"We just attempted to do some credits against some earlier purchases\n"
			"and the credits DID NOT go through, even though we expected them to.\n"
			"\n"
			"As far as the client is concerned, everything is perfectly normal, but\n"
			"this error should not happen under normal circumstances and so this\n"
			"particular credit should be looked into manually so we can try to prevent\n"
			"them from occurring in the future.  This is not an urgent error, however\n"
			"it should be looked into within a week.\n"
			"\n"
			"As there may have been more that one credit attempt that failed for this\n"
			"player, the details below are for the last failure seen.\n"
			"\n"			
			"Details:\n"
			"\n"
			"     User id: %s\n"
			"     Original transaction date: %s\n"
			"     Original transaction #: %d\n"
			"     Original transaction purchase amount: %s\n"
			"     Credit apparently available on the transaction: %s\n"
			"     Credit attempt amount: %s\n"
			,
			iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
			TimeStr(),
			player_rec.user_id,
			TimeStr(last_failed_ct.timestamp,TRUE),
			last_failed_ct.ecash_id,
			CurrencyString(curr_str1, last_failed_ct.transaction_amount, CT_REAL),
			CurrencyString(curr_str2, last_failed_ct.credit_left, CT_REAL),
			CurrencyString(curr_str3, pennies_amount, CT_REAL)
		);
	}
	return 0;
}


//*********************************************************
// https://github.com/kriskoin//
// Process a purchase transaction.  Part of TransactEcashRequest().
//
int ECash_Thread::ProcessPurchase(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);
	char szTransaction[CL_TRANSACTION_ID_LEN]; //	32 -> 19991104-000003ef-001
	zstruct(szTransaction);
	char fixed_string1[MAX_FIXED_STRING_LEN];
	char fixed_string2[MAX_FIXED_STRING_LEN];
	zstruct(fixed_string1);
	zstruct(fixed_string2);
	char hidden_cc[50];
	zstruct(hidden_cc);
	char clean_card[50];
	zstruct(clean_card);
	char spaces_card[50];
	zstruct(spaces_card);
	char address_str[300];
	zstruct(address_str);
	char auto_block_reason[300];
	zstruct(auto_block_reason);
	char ip_addr_str[20];
	char ip2_addr_str[20];
	char ip_name_str[150];
	zstruct(ip_addr_str);
	zstruct(ip2_addr_str);
	zstruct(ip_name_str);
	int err_msg_sent_already = FALSE;	// some msgs we only want to send once
	int ecash_id = 0;
	int err_code = -1;
	int sub_err_code = -1;
        /*
	CCToHex(tr->card_number, hidden_cc, clean_card, spaces_card);
	//kriskoin: 	// card is allowed to be used on this account.
	{
		// Turn the card number into a bcd representation so we can
		// look it up in the cc database
		BYTE8 cc_key[CCDB_CARDNUM_BYTES];
		zstruct(cc_key);
		ErrorType err = MakeBCDFromCC(clean_card, cc_key);
		if (err) {
			// The cc number does not appear to be formatted correctly.
			// Tell the user.
			if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
				char msg[400];
				zstruct(msg);
				sprintf(msg,"Transaction DECLINED:\n\n"
						"The credit card number you entered does not\n"
						"appear to be formatted correctly.\n"
						"\n"
						"Did you enter all the numbers?");
				EcashNotifyPlayer(queue_entry.player_id, msg, FALSE);
			}
			return ERR_ERROR;
		}

		int auto_block_flag = FALSE;
		zstruct(auto_block_reason);
		int charge_allowed = CCDB_AddCC(cc_key, queue_entry.player_id, time(NULL), TRUE, &auto_block_flag, auto_block_reason);
		if (!charge_allowed || auto_block_flag) {
			// The cc is not allowed to be used on this account (or possibly
			// it's blocked on all accounts).
			// Tell the user.
			if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
				char msg[400];
				zstruct(msg);
				sprintf(msg,"Transaction DECLINED:\n\n"
							"This credit card is not permitted to\n"
							"be used with this account.\n\n"
							"Security error code #2.\n\n"
							"Please contact support@kkrekop.io\n"
							"if you have any questions.");
				EcashNotifyPlayer(queue_entry.player_id, msg, FALSE);
			}

			if (auto_block_flag) {
				// Auto-block this guy now!
				EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
				Player *plr = CardRoomPtr->FindPlayer(queue_entry.player_id);
				if (plr) {	// player object is resident in memory
					plr->AutoBlock(auto_block_reason);
				}
				LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
			}
			// log to ecash.log
			AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_BLOCK_PURCH], pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "CCDB security block", 0, 0);
			return ERR_ERROR;
		}
	}
        */

	// 24/01/01 kriskoin:
	if (IsPlayerOverHisBuyingLimit(queue_entry.player_id, pennies_amount)) {

		/*
		  Error(ERR_NOTE, "%s(%d) Request from %08lx to purchase %d chips, but over the limit",
			_FL, queue_entry.player_id, pennies_amount);
		*/
		return ERR_ERROR;
	}
	/*
        if (DebugFilterLevel <= 8) {
		kp(("%s %s(%d) For player %08lx received request to purchase (%s) on card (%s - %s/%s)\n",
			TimeStr(), _FL, queue_entry.player_id, CurrencyString(curr_str1, pennies_amount, CT_REAL), tr->card_number,
			tr->card_exp_month, tr->card_exp_year));
	}
	int client_transaction_number = 0;
	if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE) {	// can't continue
		Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
		sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
	}

	sprintf(email_buffer+strlen(email_buffer),
		"Desert Poker Purchase/Deposit Summary -- %s CST\n\n", TimeStrWithYear());
	
	sprintf(email_buffer + strlen(email_buffer),
		"This email is sent as a confirmation that you have just requested to\n"
		"purchase US%s worth of real money chips at Desert Poker.com\n\n",
		CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));

	IP_ConvertIPtoString(tr->ip_address, ip_addr_str, sizeof(ip_addr_str));
	IP_ConvertIPToHostName(tr->ip_address, ip_name_str, sizeof(ip_name_str));

	sprintf(email_buffer + strlen(email_buffer),
		"Your email address: %s\n"
		"At the time the purchase was submitted (%s CST) you\n"
		"were connected from %s",
		player_rec.email_address,
		TimeStr(tr->queue_time, TRUE, FALSE, 0),
		ip_addr_str);
	if (ip_name_str[0]) {
		sprintf(email_buffer + strlen(email_buffer),
				" (%s)", ip_name_str);
	}
	// Do they have a different local ip address (e.g. if they are
	// behind a masquerading firewall or proxy server)?
	if (tr->ip_address2 && tr->ip_address2 != tr->ip_address) {
		IP_ConvertIPtoString(tr->ip_address2, ip2_addr_str, sizeof(ip2_addr_str));
		sprintf(email_buffer + strlen(email_buffer),
				"\n(local ip address %s)", ip2_addr_str);
	}
	strcat(email_buffer, "\n\n");

	sprintf(email_buffer+strlen(email_buffer),
				"Processed request from %s (Player ID ' %s ')\n"
				"to purchase US%s on credit card.\n\n",
				player_rec.full_name, player_rec.user_id, CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));

	sprintf(address_str,
		"%s\n"
		"%s\n"
		"%s%s%s"
		"%s, %s, %s\n"
		"%s\n"
		"%s\n\n",
		player_rec.full_name,
		player_rec.mailing_address1, "",
		player_rec.mailing_address2,
		player_rec.mailing_address2[0] ? "\n" : "",
		player_rec.city,
		player_rec.mailing_address_state,
		player_rec.mailing_address_country,
		player_rec.mailing_address_postal_code,
		DecodePhoneNumber(player_rec.phone_number)
	);
	strcat(email_buffer, address_str);

	sprintf(email_buffer+strlen(email_buffer),
		"\nPurchase amount: US%s\n", CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));
	sprintf(email_buffer+strlen(email_buffer),
		"Card number: %s\n", hidden_cc);
	sprintf(email_buffer+strlen(email_buffer),
		"Card expiry: xx/xx\n\n");

	BuildPurchaseRequestPostString(tr, action_string, szTransaction, player_rec.full_name, clean_card, player_rec.email_address);
	//kwrites((action_string));
	ErrorType rc_post = ERR_FATAL_ERROR;
	WORD32 first_attempt_time = SecondCounter;
	for (int attempt = 0 ;
	    attempt < MAX_TRANSACTION_ATTEMPTS || SecondCounter < first_attempt_time + MIN_ECASH_RETRY_TIME ;
	    attempt++)
    {
		// log to ecash.log
		AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_REQ_PURCHASE], pennies_amount,
			spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
			0, szTransaction, 0, 0);

		WORD32 last_attempt_time = SecondCounter;  */
		/* POST THE PURCHASE TRANSACTION */
 		/*
		rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_CHARGE_URL);
	  #if WIN32	&& 0
		kp(("%s(%d) **** WARNING: TREATING ALL TRANSACTION RESULTS AS UNCERTAIN! (real result was %d)\n",_FL, rc_post));
		rc_post = ERR_SERIOUS_WARNING;
	  #endif
		//kriskoin: 		// absolutely certain that nothing got sent to it, we can be sure
		// that the transaction did NOT go through.  Retry a few times in
		// case it was merely a transient communications error.
		// Break out if it went through or we're uncertain.
		// ERR_ERROR means it definitely did NOT go through
		// ERR_SERIOUS_WARNING means it MIGHT have gone through
		if (rc_post==ERR_ERROR) {
			// Definitely did NOT go through.
		  #if 0
			kp(("%s(%d) **** WARNING: BAD TRANSACTION NUMBER TO MAKE IT FAIL\n",_FL));
			AddToEcashLog(queue_entry.player_id, "MAKE IT FAIL", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Communication failure", 0, 0);
		  #else
			AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_ERR_TRANSACT], pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Communication failure", 0, 0);
		  #endif
			iConsecutiveFailedTransactions++;
            int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
            if (delay > 0) {
                Sleep(delay*1000);
            }
		} else {	// Went through or MAY have gone through...
			break;	// don't try again.
		}
	}

  #if WIN32 && 0
  	// Test the 'verify transaction' code.
	kp(("%s(%d) **** TEST: VERIFYING EVERY PURCHASE ****\n",_FL));
	int looked_up_transaction_number = 0;
	ErrorType verify_result = TransactionFailureLookup(szTransaction, &looked_up_transaction_number);
	if (verify_result==ERR_NONE) {
		verify_result = TransactionQuery(looked_up_transaction_number, szTransaction);
	}
	kp(("%s(%d) Result for transaction #%d (looked up to %d) = %d\n",
			_FL, iLastECashID+1, looked_up_transaction_number, verify_result));

	// Test a failure lookup on a specific transaction
	kp(("%s(%d) **** TEST: VERIFYING 20001207-0010c488-016 ****\n",_FL));
	looked_up_transaction_number = 0;
	verify_result = TransactionFailureLookup("20001207-0010c488-016", &looked_up_transaction_number);
	if (verify_result==ERR_NONE) {
		verify_result = TransactionQuery(looked_up_transaction_number, "20001207-0010c488-016");
	}
	kp(("%s(%d) Result for transaction #%d (looked up to %d) = %d\n",
			_FL, iLastECashID+1, looked_up_transaction_number, verify_result));
  #endif

	if (rc_post == ERR_NONE) {  */
		/* RECEIVED A VALID RESPONSE -- PROCESS IT */
       /*
parsepurch:
		EcashReplyStruct ers;
		zstruct(ers);
		ErrorType rc_parse = ParseEcashReply(&ers, result_str);
		if (rc_parse == ERR_NONE) {	// parsed successfully, handle it
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset

			// fill the transaction structure
			ClientTransaction ct;
			zstruct(ct);
			//kriskoin: 			// That's probably due to a firepay beta bug.  If it doesn't look reasonable, don't use it.
			ct.timestamp = (ers.authTime && strlen(ers.authTime)<=10) ? atol(ers.authTime) : time(NULL);	// use now if we don't have one
			ct.partial_cc_number = CCToHex(tr->card_number, hidden_cc, NULL, NULL); // convert string to 32-bit summary
			ct.transaction_amount = (ers.amount ? atoi(ers.amount) : pennies_amount);
			ct.credit_left = ct.transaction_amount;	// eligible to have the entire amount credited back
			// it was either successful or not -- anything other than "SP" is an error of some sort
			
			if (ers.status && !stricmp(ers.status, "SP")) {	// OK!
				ecash_id = (ers.txnNumber ? atoi(ers.txnNumber) : 0);
				ct.ecash_id = ecash_id;
				iLastECashID = ecash_id;
				// keep track of the transaction number and which thread it was
				TransactionNumberReceived(thread_number, iLastECashID);
			  #if 0	// testing only, plug some other numbers in there
				kp(("%s(%d) WARNING: This test code must be disabled for a live server\n", _FL));
				TransactionNumberReceived(thread_number+1, iLastECashID+2);
				TransactionNumberReceived(thread_number+2, iLastECashID+5);
				TransactionNumberReceived(thread_number+3, iLastECashID+9);
				TransactionNumberReceived(thread_number+4, iLastECashID+11);
			  #endif
				ct.transaction_type = (BYTE8)CTT_PURCHASE;
				// filled the ClientTransaction -- process and log
				chips_balance_changed = TRUE;
				// log it and notify the client if possible
				SDB->LogPlayerTransaction(queue_entry.player_id, &ct);
				// update the client if possible
				EcashSendClientInfo(queue_entry.player_id);
				int cash_dep, credit_dep;	// will be filled when we add the chips to the account
				AddChipsToAccount(queue_entry.player_id, atoi(tr->amount), CT_REAL, "c/c purchase", TRUE, &cash_dep, &credit_dep);
				tr_index = CLTR_APP_PURCHASE;
				sprintf(message_to_send_client,"%s\nTransaction status: APPROVED:\n\nPurchased %s on c/c %s\nTransaction # %d\n"
					ECASH_PROCESSOR_NAME" c/c processing fee: %s\n\n%s has been credited to your account.\n%s has been added to your fee refund account.",
					address_str,
					CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc, ecash_id,
					CurrencyString(curr_str2, credit_dep, CT_REAL,TRUE),
					CurrencyString(curr_str3, cash_dep, CT_REAL,TRUE),
					CurrencyString(curr_str4, credit_dep, CT_REAL,TRUE) );
				sprintf(email_buffer+strlen(email_buffer),
					"Transaction APPROVED:\nTransaction # %s-%d\n"ECASH_PROCESSOR_NAME" c/c processing fee: %s\n\n"
					"%s has been credited to your account.\n%s has been added to your fee refund account.\n\n",
					szTransaction, ecash_id, CurrencyString(curr_str1, credit_dep, CT_REAL, TRUE),
					CurrencyString(curr_str2, cash_dep, CT_REAL, TRUE),
					CurrencyString(curr_str3, credit_dep, CT_REAL, TRUE) );
				sprintf(email_buffer+strlen(email_buffer),
					"This charge will show up on your c/c statement as:\n"
					"%s\n"
					"\n",
					CCChargeName);

				strcat(email_buffer,
					"If this purchase was made in error, you must notify the Desert Poker\n"
					"security department immediately ( security@kkrekop.io ).\n"
					"Credit card fraud is a criminal offence.\n"
					"\n"
					"\nPlease remember that your account password should be secure.  Do not\n"
					"select easy to guess short passwords.  Your password can be changed from\n"
					"the options menu of the main cardroom screen.\n"
					"\n");

				// 24/01/01 kriskoin:
				if (SDB->GetNextTransactionNumber(queue_entry.player_id) == 1) {	// 0 was the first one
					if (pennies_amount >= 40000) {
						// set to alert 5 (sounds about right)
						// 24/01/01 kriskoin:
  						// 24/01/01 kriskoin:
						SendAdminAlert(ALERT_8, "%s just purchased %s as the first transaction.",
							player_rec.user_id, CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE));
					}
				}
				SendAdminAlert(ALERT_2, "PurchApp for %s : %s - (%d - %s)",
					player_rec.user_id, CurrencyString(curr_str1,pennies_amount, CT_REAL,TRUE),
					iLastECashID, hidden_cc);
			} else {	// transaction rejected
				err_code = (ers.errCode ? atoi(ers.errCode) : -1);
				sub_err_code = (ers.subError ? atoi(ers.subError) : -1);
				tr_index = CLTR_REJ_PURCHASE;
				//kriskoin: 				// the comments in FillErrorResultBuffer() for details on the sorts
				// of things each set of error codes mean in the real world.
			  kp(("%s(%d) Result for transaction #%d \n", _FL, err_code));

				if (err_code==34 && sub_err_code==1005) {
					sprintf(message_to_send_client,
						"Purchase attempt DECLINED.\n"
						"\n"
						"Please try a different credit card.\n"
						"\n"
						"Full details can be found at the bottom of the\n"
						"email that has been sent to you now.\n"
					);
				} else {
					sprintf(message_to_send_client,
						"Transaction DECLINED:\n"
						"\n"
						"Unable to purchase %s on c/c %s\n"
						"\n"
						"%s\n"
						"%s\n"
						"\n"
						"Full details have been emailed to you.\n",
						CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc,
						(ers.errString ? ECash_FixReplyString(ers.errString,fixed_string1,MAX_FIXED_STRING_LEN) : "No error information available"),
						(ers.subErrorString ? ECash_FixReplyString(ers.subErrorString,fixed_string2,MAX_FIXED_STRING_LEN) : "No further information available") );
				}

				sprintf(email_buffer+strlen(email_buffer),
					"Transaction DECLINED:\n"
					"\n"
					"Unable to purchase %s on c/c %s\n"
					"\n",
					CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc);

				// fill error details for this transaction
				FillErrorResultBuffer(err_code, sub_err_code);
				strcat(email_buffer, ecash_error_buffer);

				SendAdminAlert(ALERT_2, "PurchRej for %s : %s (%s)",
					player_rec.user_id, CurrencyString(curr_str1,pennies_amount, CT_REAL,TRUE),
					hidden_cc);
				//kriskoin: 				// the time the request was submitted and when we issue
				// the response to the player.  This is to make it tedious
				// for people to go through lots of different credit
				// card numbers in a row trying to find one that works.
				#define MIN_PURCHREJ_ANSWER_TIME	45	// delay in seconds
				int sleep_time = tr->queue_time + MIN_PURCHREJ_ANSWER_TIME - time(NULL);
				if (iRunningLiveFlag && sleep_time > 0 && sleep_time <= MIN_PURCHREJ_ANSWER_TIME) {
					//kp(("%s(%d) Beginning to sleep for %ds due to PurchRej...\n",_FL,sleep_time));
					Sleep(sleep_time*1000);
					//kp(("%s(%d) Done sleeping for %ds due to PurchRej.\n",_FL,sleep_time));
				}
			}
		} else {
			// We got a parse error... the transaction MAY have gone through.
			iMaybePurchaseOccurred = TRUE;
			iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
			DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
			tr_index = CLTR_ERR_PARSE;
			Error(ERR_INTERNAL_ERROR, "%s(%d) ParseEcashReply returned an error!! Check transaction\n", _FL);

		  #if 1	// 2022 kriskoin
			strnncpy(message_to_send_client,
				"Transaction error:\n"
				"There was an error contacting our ecash provider.\n"
				"This transaction will have to be looked into manually.\n\n"
				"You will receive an email with more details.\n"
				"Please contact cashier@kkrekop.io",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io");
			strcat(email_buffer," as this transaction will have to be looked into manually.\n\n");
			strcat(email_buffer,"It is POSSIBLE that your card was charged. If that happened,\n");
			strcat(email_buffer,"the transaction will be reversed manually by customer support.\n");
			strcat(email_buffer,"This problem is temporary.\n");
		  #else
			strnncpy(message_to_send_client,
				"There was an error attempting to process your request.  Please try again later",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR PROCESSING YOUR TRANSACTION ***\n");
			strcat(email_buffer,"This transaction was not processed.  Your card will not be charged.\n");
			strcat(email_buffer,"This problem is temporary.  Please try again in a few minutes.\n");
		  #endif
			SendAdminAlert(ALERT_7, "ErrTransact - MAYBE (Purchase) for %s (%08lx) had a parsing error. Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}
	} else if (rc_post == ERR_SERIOUS_WARNING) {	// possibly went through
		//kriskoin: 		// went through or not.
		int looked_up_transaction_number = 0;
		ErrorType verify_result = TransactionFailureLookup(szTransaction, &looked_up_transaction_number);
		if (verify_result==ERR_NONE) {
			// It looked up ok... query it.
			verify_result = TransactionQuery(looked_up_transaction_number, szTransaction);
		}
	  #if WIN32 && 0
		kp(("%s(%d) *** CONTINUING TO TREAT ALL TRANSACTIONS AS MAYBE'S!\n", _FL));
		verify_result = ERR_SERIOUS_WARNING;
	  #endif
		if (verify_result==ERR_NONE) {
			// It definitely went through...
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			iMaybePurchaseOccurred = FALSE;

			kp(("%s %s(%d) TransactionQuery() indicates it DID go through\n",TimeStr(),_FL));
			// Spoof a result string
			zstruct(result_str);
			sprintf(result_str,
					"status=SP&"
					"authCode=Spoofed&"
					"authTime=%u&"
					"curAmount=0&"
					"amount=%d&"
					"txnNumber=%d&"
					"serviceVersion=1.03",
					time(NULL),
					pennies_amount,
					iLastECashID+1);
			AddToEcashLog(queue_entry.player_id, "SpoofAuth", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Query succeeded", 0, 0);
		  #if 0
			char subject[100];
			zstruct(subject);
			sprintf(subject,
					"Check transaction %d for %s (approved)",
					iLastECashID+1, player_rec.user_id);
			EmailStr("support@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"Purchase for %s (transaction %d) had a communication\n"
					"failure, but the new transaction verification code\n"
					"Told it actually went through and treated it as such.\n"
					"\n"
					"For the next while, someone should verify that\n"
					"this purchase really did get approved.\n",
					iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
					player_rec.user_id, iLastECashID+1);
		  #endif
			goto parsepurch;
		} else if (verify_result==ERR_ERROR) {
			// It definitely did NOT go through.
			// Treat as PurchRej.
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			iMaybePurchaseOccurred = FALSE;
			kp(("%s %s(%d) TransactionQuery() indicates it did NOT go through\n",TimeStr(),_FL));
			zstruct(result_str);
			// Spoof a result string
			zstruct(result_str);
			sprintf(result_str,
					"status=E&"
					"amount=%d&"
					"errCode=213&"	// authorization aborted
					"errString=Authorization failed - Communication Error&"
					"serviceVersion=1.03&",
					pennies_amount);
			AddToEcashLog(queue_entry.player_id, "SpoofRej", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Query failed", 0, 0);
		  #if 0
			char subject[100];
			zstruct(subject);
			sprintf(subject,
					"Check transaction %d for %s (rejected)",
					iLastECashID+1, player_rec.user_id);
			EmailStr("support@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"Purchase for %s (transaction %d) had a communication\n"
					"failure, but the new transaction verification code\n"
					"Told it was rejected and treated it as such.\n"
					"\n"
					"If it got rejected, then transaction %d should be\n"
					"the number of the next transaction that really goes\n"
					"through.\n"
					"\n"
					"For the next while, someone should verify that\n"
					"this purchase really did get rejected.\n",
					iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
					player_rec.user_id, iLastECashID+1, iLastECashID+1);
		  #endif
			goto parsepurch;
		}

		// We're still uncertain if it went through...				
		iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
		tr_index = CLTR_ERR_TRANSACT_MAYBE;
		DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
		if (!err_msg_sent_already) {
			err_msg_sent_already = TRUE;
			strnncpy(message_to_send_client,
				"Transaction error:\n"
				"There was an error contacting our ecash provider.\n"
				"This transaction will have to be looked into manually.\n\n"
				"You will receive an email with more details.\n"
				"Please contact cashier@kkrekop.io",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io");
			strcat(email_buffer," as this transaction will have to be looked into manually.\n\n");
			strcat(email_buffer,"It is POSSIBLE that your card was charged. If that happened,\n");
			strcat(email_buffer,"the transaction will be reversed manually by customer support.\n");
			strcat(email_buffer,"This problem is temporary.\n");
			SendAdminAlert(ALERT_9, "ErrTransact - MAYBE (Purchase) for %s (%08lx) MAY have gone through: Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}

		// Send an email to support to indicate this maybe purchase must be looked into
		char subject[200];
		zstruct(subject);
		sprintf(subject, "%sCashier: %s had a maybe purchase for %s",
				iRunningLiveFlag ? "" : "Test: ",
				player_rec.user_id,
				CurrencyString(curr_str1, pennies_amount, CT_PLAY, TRUE));
		EmailStr(
				"support@kkrekop.io",
				"Cashier",
				"cashier@kkrekop.io",
				subject,
				NULL,
				"%s"
				"User id %s had a purchase which MAY or MAY NOT have gone through.\n"
				"\n"
				"This transaction MUST be looked into manually.  The client is\n"
				"LOCKED OUT OF THE CASHIER until this issue is resolved.\n"
				"The server attempted to determine the outcome of this transaction itself\n"
				"for up to 10 minutes but was unable to get a decent answer from\n"
				"the transaction server.  It has now given up and passed the problem\n"
				"on to the customer support department (you) for manual resolution.\n"
				"\n"
				"Details:\n"
				"\n"
				"    User ID: %s\n"
				"    Purchase amount: %s\n"
				"    Merchant transaction number: %s\n"
				,
				iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n",
				player_rec.user_id,
				player_rec.user_id,
				CurrencyString(curr_str1, pennies_amount, CT_REAL),
				szTransaction);
	} else {	// no way it went through
		tr_index = CLTR_ERR_TRANSACT;
		if (!err_msg_sent_already) {
			err_msg_sent_already = TRUE;
			strnncpy(message_to_send_client,
				"Transaction not successful:\n\n"
				"There was an error contacting our ecash provider.\nPlease try again later",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"This transaction was not processed.  Your card will not be charged.\n\n");

			time_t now = time(NULL);
			struct tm tm;
			zstruct(tm);
			struct tm *t = localtime(&now, &tm);
		  #if WIN32 && 0	// testing...
			kp(("%s(%d) t->tm_wday = %d, t->tm_hour = %d\n", _FL, t->tm_wday, t->tm_hour));
			if (t && t->tm_wday==1 && t->tm_hour >= 21 && t->tm_hour < 22)
		  #else
			if (t && t->tm_wday==1 && t->tm_hour >= 2 && t->tm_hour < 7)
		  #endif
			{
				strcat(email_buffer,
					"On Monday mornings from 2am to 6am (CST), the "ECASH_PROCESSOR_NAME" transaction\n"
					"server is down for maintenence. Please try again after the maintenence\n"
					"is completed.\n");
			} else {
				strcat(email_buffer,"This problem is probably temporary. Please try again in a few minutes.\n");
			}

			SendAdminAlert(ALERT_5, "ErrTransact (Purchase) for %s (%08lx) DID NOT go through: Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}
	}

	// log to ecash.log
	AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], pennies_amount,
		spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
		ecash_id, szTransaction, err_code, sub_err_code);

	// try to notify the client if there's anything to tell him
	if (chips_balance_changed || message_to_send_client[0])	{	// try to notify the client
		if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
			EcashNotifyPlayer(queue_entry.player_id, message_to_send_client, chips_balance_changed);
		}
	}
	*/
	return ERR_NONE;
}


//*********************************************************
// https://github.com/kriskoin//
// Process a purchase transaction.  Part of TransactEcashRequest().
//
int ECash_Thread::ProcessFirePayPurchase(void)
{
	CCTransaction *tr = &queue_entry.cct;
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);
	char szTransaction[CL_TRANSACTION_ID_LEN]; //	32 -> 19991104-000003ef-001
	zstruct(szTransaction);
	char fixed_string1[MAX_FIXED_STRING_LEN];
	char fixed_string2[MAX_FIXED_STRING_LEN];
	zstruct(fixed_string1);
	zstruct(fixed_string2);
	char hidden_cc[50];
	zstruct(hidden_cc);
	char clean_card[50];
	zstruct(clean_card);
	char spaces_card[50];
	zstruct(spaces_card);
	char address_str[300];
	zstruct(address_str);
	char auto_block_reason[300];
	zstruct(auto_block_reason);
	char ip_addr_str[20];
	char ip2_addr_str[20];
	char ip_name_str[150];
	zstruct(ip_addr_str);
	zstruct(ip2_addr_str);
	zstruct(ip_name_str);
	int err_msg_sent_already = FALSE;	// some msgs we only want to send once
	int ecash_id = 0;
	int err_code = -1;
	int sub_err_code = -1;

        kp(("%s(%d) **** enter the firepay purchase %s\n",_FL," " ));

	CCToHex(tr->card_number, hidden_cc, clean_card, spaces_card);
	//kriskoin: 	// card is allowed to be used on this account.
	{
		// Turn the card number into a bcd representation so we can
		// look it up in the cc database
		BYTE8 cc_key[CCDB_CARDNUM_BYTES];
		zstruct(cc_key);
		ErrorType err = MakeBCDFromCC(clean_card, cc_key);
		if (err) {
			// The cc number does not appear to be formatted correctly.
			// Tell the user.
			if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
				char msg[400];
				zstruct(msg);
				sprintf(msg,"Transaction DECLINED:\n\n"
						"The credit card number you entered does not\n"
						"appear to be formatted correctly.\n"
						"\n"
						"Did you enter all the numbers?");
				EcashNotifyPlayer(queue_entry.player_id, msg, FALSE);
			}
			return ERR_ERROR;
		}

		int auto_block_flag = FALSE;
		zstruct(auto_block_reason);
		int charge_allowed = CCDB_AddCC(cc_key, queue_entry.player_id, time(NULL), TRUE, &auto_block_flag, auto_block_reason);
		if (!charge_allowed || auto_block_flag) {
			// The cc is not allowed to be used on this account (or possibly
			// it's blocked on all accounts).
			// Tell the user.
			if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
				char msg[400];
				zstruct(msg);
				sprintf(msg,"Transaction DECLINED:\n\n"
							"This credit card is not permitted to\n"
							"be used with this account.\n\n"
							"Security code 222.\n\n"
							"Please contact accounting@kkrekop.io\n"
							"if you have any questions.");
				EcashNotifyPlayer(queue_entry.player_id, msg, FALSE);
			}

			if (auto_block_flag) {
				// Auto-block this guy now!
				EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
				Player *plr = CardRoomPtr->FindPlayer(queue_entry.player_id);
				if (plr) {	// player object is resident in memory
					plr->AutoBlock(auto_block_reason);
				}
				LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
			}
			// log to ecash.log
			AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_BLOCK_PURCH], pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "CCDB security block", 0, 0);
			return ERR_ERROR;
		}
	}

	// 24/01/01 kriskoin:
        	
        if (IsPlayerOverHisBuyingLimit(queue_entry.player_id, pennies_amount)) {
		Error(ERR_NOTE, "%s(%d) Request from %08lx to purchase %d chips, but over the limit",
			_FL, queue_entry.player_id, pennies_amount);
		return ERR_ERROR;
	}
 	
	if (DebugFilterLevel <= 8) {
		kp(("%s %s(%d) For player %08lx received request to purchase (%s) on card (%s - %s/%s)\n",
			TimeStr(), _FL, queue_entry.player_id, CurrencyString(curr_str1, pennies_amount, CT_REAL), tr->card_number,
			tr->card_exp_month, tr->card_exp_year));
	}
	int client_transaction_number = 0;
	if (GetNextTransactionNumber(queue_entry.player_id, szTransaction, &client_transaction_number) != ERR_NONE) {	// can't continue
		Error(ERR_ERROR, "%s(%d) Couldn't get next transaction # player %08lx", _FL, queue_entry.player_id);
		sprintf(szTransaction,"%08lx-%d", queue_entry.player_id, time(NULL));
	}
        zstruct(email_buffer);
	sprintf(email_buffer+strlen(email_buffer),
		"Desert Poker Purchase/Deposit Summary -- %s CST\n\n", TimeStrWithYear());
	
	sprintf(email_buffer + strlen(email_buffer),
		"This email is sent as a confirmation of a credit card purchase/deposit to the\n following"
		" Player Account:"
                "\n\nPlayer ID : %s\nEmail : %s\n\n%s\n%s\n%s, %s\n%s\n\%s\n\n",
	        player_rec.user_id, player_rec.email_address, player_rec.full_name, player_rec.mailing_address1,
		player_rec.city, player_rec.mailing_address_state,
                player_rec.mailing_address_country,
                player_rec.mailing_address_postal_code
		 );	
                /*
                This email is sent as a confirmation of a credit card purchase/deposit to the following"
                "Player Account:"
                "\n%s\n%s\n%s\n%s\n\n",
                player_rec.user_id, player_rec.email_address, player_rec.full_name, player_rec.mailing_address1);
		*/	
		/*
		"purchase US%s worth of real money chips at Desert Poker.com\n\n",
		CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));*/

                 /*
	IP_ConvertIPtoString(tr->ip_address, ip_addr_str, sizeof(ip_addr_str));
	IP_ConvertIPToHostName(tr->ip_address, ip_name_str, sizeof(ip_name_str));

	sprintf(email_buffer + strlen(email_buffer),
		"Your email address: %s\n"
		"At the time the purchase was submitted (%s CST) you\n"
		"were connected from %s",
		player_rec.email_address,
		TimeStr(tr->queue_time, TRUE, FALSE, 0),
		ip_addr_str);
	if (ip_name_str[0]) {
		sprintf(email_buffer + strlen(email_buffer),
				" (%s)", ip_name_str);
	}
        */
	// Do they have a different local ip address (e.g. if they are
	// behind a masquerading firewall or proxy server)?
	/*
        if (tr->ip_address2 && tr->ip_address2 != tr->ip_address) {
		IP_ConvertIPtoString(tr->ip_address2, ip2_addr_str, sizeof(ip2_addr_str));
		sprintf(email_buffer + strlen(email_buffer),
				"\n(local ip address %s)", ip2_addr_str);
	}
	strcat(email_buffer, "\n\n");

	sprintf(email_buffer+strlen(email_buffer),
				"Processed request from %s (Player ID ' %s ')\n"
				"to purchase US%s on credit card.\n\n",
				player_rec.full_name, player_rec.user_id, CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));

	sprintf(address_str,
		"\n%s\n"
		"%s\n"
		"%s%s%s"
		"%s, %s, %s\n"
		"%s\n"
		"%s\n\n\n",
		player_rec.full_name,
		player_rec.mailing_address1, "",
		player_rec.mailing_address2,
		player_rec.mailing_address2[0] ? "" : "",
		player_rec.city,
		player_rec.mailing_address_state,
		player_rec.mailing_address_country,
		player_rec.mailing_address_postal_code,
		DecodePhoneNumber(player_rec.phone_number)
	);
	strcat(email_buffer, address_str);

	sprintf(email_buffer+strlen(email_buffer),
		"\nPurchase amount: US%s\n", CurrencyString(curr_str1, pennies_amount, CT_REAL, TRUE));
	sprintf(email_buffer+strlen(email_buffer),
		"Card number: %s\n", hidden_cc);
	sprintf(email_buffer+strlen(email_buffer),
		"Card expiry: xx/xx\n\n");
        */
	BuildPurchaseRequestPostString(tr, action_string, szTransaction, player_rec.full_name, clean_card, player_rec.email_address,player_rec.mailing_address1, DecodePhoneNumber(player_rec.phone_number),player_rec.city,player_rec.mailing_address_state,player_rec.mailing_address_postal_code,player_rec.mailing_address_country);
	//kwrites((action_string));
	ErrorType rc_post = ERR_FATAL_ERROR;
	WORD32 first_attempt_time = SecondCounter;
	for (int attempt = 0 ;
	    attempt < MAX_TRANSACTION_ATTEMPTS || SecondCounter < first_attempt_time + MIN_ECASH_RETRY_TIME ;
	    attempt++)
    {
		// log to ecash.log
		AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_REQ_PURCHASE], pennies_amount,
			spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
			0, szTransaction, 0, 0);

		WORD32 last_attempt_time = SecondCounter;
		/* POST THE PURCHASE TRANSACTION */
		rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_CHARGE_URL);
	  #if WIN32	&& 0
		kp(("%s(%d) **** WARNING: TREATING ALL TRANSACTION RESULTS AS UNCERTAIN! (real result was %d)\n",_FL, rc_post));
		rc_post = ERR_SERIOUS_WARNING;
	  #endif
		//kriskoin: 		// absolutely certain that nothing got sent to it, we can be sure
		// that the transaction did NOT go through.  Retry a few times in
		// case it was merely a transient communications error.
		// Break out if it went through or we're uncertain.
		// ERR_ERROR means it definitely did NOT go through
		// ERR_SERIOUS_WARNING means it MIGHT have Gone through
                kp(("%s(%d) **** POST it to the web %s\n",_FL, " " ));
                kp(("%s(%d) **** POST it to the web, this is the error code:  %d\n",_FL, rc_post ));

		if (rc_post==ERR_ERROR) {
			// Definitely did NOT go through.
                  kp(("%s(%d) **** post is error %s \n",_FL, " " ));

		  #if 0
			kp(("%s(%d) **** WARNING: BAD TRANSACTION NUMBER TO MAKE IT FAIL\n",_FL));
			AddToEcashLog(queue_entry.player_id, "MAKE IT FAIL", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Communication failure", 0, 0);
		  #else
			AddToEcashLog(queue_entry.player_id, szTransactStr[CLTR_ERR_TRANSACT], pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Communication failure", 0, 0);
		  #endif
			iConsecutiveFailedTransactions++;
            int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
            if (delay > 0) {
                Sleep(delay*1000);
            }
		} else {	// Went through or MAY have gone through...
			break;	// don't try again.
		}
	}

  #if WIN32 && 0
  	// Test the 'verify transaction' code.
	kp(("%s(%d) **** TEST: VERIFYING EVERY PURCHASE ****\n",_FL));
	int looked_up_transaction_number = 0;
	ErrorType verify_result = TransactionFailureLookup(szTransaction, &looked_up_transaction_number);
	if (verify_result==ERR_NONE) {
		verify_result = TransactionQuery(looked_up_transaction_number, szTransaction);
	}
	kp(("%s(%d) Result for transaction #%d (looked up to %d) = %d\n",
			_FL, iLastECashID+1, looked_up_transaction_number, verify_result));

	// Test a failure lookup on a specific transaction
	kp(("%s(%d) **** TEST: VERIFYING 20001207-0010c488-016 ****\n",_FL));
	looked_up_transaction_number = 0;
	verify_result = TransactionFailureLookup("20001207-0010c488-016", &looked_up_transaction_number);
	if (verify_result==ERR_NONE) {
		verify_result = TransactionQuery(looked_up_transaction_number, "20001207-0010c488-016");
	}
	kp(("%s(%d) Result for transaction #%d (looked up to %d) = %d\n",
			_FL, iLastECashID+1, looked_up_transaction_number, verify_result));
  #endif
         kp(("%s(%d) **** This is the post error code %d\n",_FL, rc_post ));

	if (rc_post == ERR_NONE) {
		/* RECEIVED A VALID RESPONSE -- PROCESS IT */
parsepurch:
		EcashReplyStruct ers;
		zstruct(ers);
		ErrorType rc_parse = ParseEcashReply(&ers, result_str);
                kp(("%s(%d) **** This is the firepay error code %d\n",_FL, rc_parse ));

		if (rc_parse == ERR_NONE) {	// parsed successfully, handle it
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset

			// fill the transaction structure
			ClientTransaction ct;
			zstruct(ct);
			//kriskoin: 			// That's probably due to a firepay beta bug.  If it doesn't look reasonable, don't use it.
			ct.timestamp = (ers.authTime && strlen(ers.authTime)<=10) ? atol(ers.authTime) : time(NULL);	// use now if we don't have one
			ct.partial_cc_number = CCToHex(tr->card_number, hidden_cc, NULL, NULL); // convert string to 32-bit summary
			ct.transaction_amount = (ers.amount ? atoi(ers.amount) : pennies_amount);
			if ((tr->card_type == CCTYPE_FIREPAY)||(tr->card_type == CCTYPE_VISA))
			    ct.credit_left = ct.transaction_amount;	// eligible to have the entire amount credited back
			// it was either successful or not -- anything other than "SP" is an error of some sort
         	        kp(("%s(%d) **** This is the firepay error status %s\n",_FL, ers.status ));

			if (ers.status && !stricmp(ers.status, "SP")) {	// OK!
				ecash_id = (ers.txnNumber ? atoi(ers.txnNumber) : 0);
				ct.ecash_id = ecash_id;
				iLastECashID = ecash_id;
				// keep track of the transaction number and which thread it was
				TransactionNumberReceived(thread_number, iLastECashID);
			  #if 0	// testing only, plug some other numbers in there
				kp(("%s(%d) WARNING: This test code must be disabled for a live server\n", _FL));
				TransactionNumberReceived(thread_number+1, iLastECashID+2);
				TransactionNumberReceived(thread_number+2, iLastECashID+5);
				TransactionNumberReceived(thread_number+3, iLastECashID+9);
				TransactionNumberReceived(thread_number+4, iLastECashID+11);
			  #endif

               			kp(("%s(%d) **** Card Type:  %d\n",_FL, tr->card_type ));
	
                                if (tr->card_type == CCTYPE_FIREPAY) {
				    ct.transaction_type = (BYTE8)CTT_FIREPAY_PURCHASE ;
        			    //ct.credit_left = ct.transaction_amount; // eligible to have the entire amount credited back
	
                                }
   				
				if (tr->card_type == CCTYPE_VISA) {
                                    ct.transaction_type = (BYTE8)CTT_CC_PURCHASE ;
				    // ct.credit_left = ct.transaction_amount; // eligible to have the entire amount credited back
                                }

				if (tr->card_type == CCTYPE_MASTERCARD ) {
                                    ct.transaction_type = (BYTE8)CTT_CC_PURCHASE ;
                      		   // ct.credit_left = 0; // eligible to have the entire amount credited back
	
                                }

				// ct.transaction_type = (BYTE8)CTT_PURCHASE;
                                // filled the ClientTransaction -- process and log
				chips_balance_changed = TRUE;
				// log it and notify the client if possible
				SDB->LogPlayerTransaction(queue_entry.player_id, &ct);


				// update the client if possible
				EcashSendClientInfo(queue_entry.player_id);
				int cash_dep, credit_dep;	// will be filled when we add the chips to the account
	  			if (tr->card_type == CCTYPE_FIREPAY) {
				   AddChipsToAccount(queue_entry.player_id, atoi(tr->amount), CT_REAL, "FirePay", TRUE, &cash_dep, &credit_dep);
				} else {
  				    AddChipsToAccount(queue_entry.player_id, atoi(tr->amount), CT_REAL, "c/c purchase", TRUE, &cash_dep, &credit_dep
); }

				tr_index = CLTR_APP_PURCHASE;
				zstruct(message_to_send_client);
				if (tr->card_type == CCTYPE_FIREPAY) {
			
				sprintf(message_to_send_client,"\nTransaction Approved.\n\nPurchased %s \nFirePay Number  %s \nMoney Credited to your Account %s",
                                       // Transaction # %d\n"
					//ECASH_PROCESSOR_NAME" c/c processing fee: %s\n
                                        //\n%s has been credited to your account.\n%s has
                                        //been added to your fee refund account."*/
					//address_str,
					CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc, /*ecash_id,*/
					CurrencyString(curr_str2, (credit_dep+cash_dep), CT_REAL,TRUE)
					//CurrencyString(curr_str3, cash_dep, CT_REAL,TRUE)
					/*CurrencyString(curr_str4, credit_dep, CT_REAL,TRUE)*/ );
				}else{
				sprintf(message_to_send_client,"\nTransaction Approved.\n\nPurchased %s \nCredit Card Number  %s \nCredit Card Fee (refundable)  %s\nMoney Credited to your Account %s",
                                       // Transaction # %d\n"
                                        //ECASH_PROCESSOR_NAME" c/c processing fee: %s\n
                                        //\n%s has been credited to your account.\n%s has
                                        //been added to your fee refund account."*/
                                        //address_str,
                                        CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc, /*ecash_id,*/
                                        CurrencyString(curr_str2, credit_dep, CT_REAL,TRUE),
                                        CurrencyString(curr_str3, cash_dep, CT_REAL,TRUE)
                                        /*CurrencyString(curr_str4, credit_dep, CT_REAL,TRUE)*/ );

				}
				/*
				sprintf(email_buffer+strlen(email_buffer),
					"Transaction Approved:\nTransaction # %s-%d\n"ECASH_PROCESSOR_NAME" c/c processing fee: %s\n\n"
					"%s has been credited to your account.\n%s has been added to your fee refund account.\n\n",
					szTransaction, ecash_id, CurrencyString(curr_str1, credit_dep, CT_REAL, TRUE),
					CurrencyString(curr_str2, cash_dep, CT_REAL, TRUE),
					CurrencyString(curr_str3, credit_dep, CT_REAL, TRUE) );
				sprintf(email_buffer+strlen(email_buffer),
					"This charge will show up on your c/c statement as:\n"
					"%s\n"
					"\n",
					CCChargeName);

				strcat(email_buffer,
					"If this purchase was made in error, you must notify the Desert Poker\n"
					"security department immediately ( security@kkrekop.io ).\n"
					"Credit card fraud is a criminal offence.\n"
					"\n"
					"Please remember that your account password should be secure.  Do not\n"
					"select easy to guess short passwords.  Your password can be changed from\n"
					"the options menu of the main cardroom screen.\n"
					"\n");
				*/
			char vtime[30];
			sprintf(vtime, TimeStrWithYear());
			vtime[4]=vtime[5];
			vtime[5]=vtime[6];
			vtime[6]=vtime[8];
			vtime[7]=vtime[9];
			vtime[8]=0;
			
			sprintf(email_buffer + strlen(email_buffer),
	                "Here are the completed transaction details:\n\n"
        	        "%s  Total Purchase amount(US$)\n\n"
                	"%s  Credited to your account\n"
                	"Card number: %s \n"
                	"Transaction # %s - %08lx - %d \n\n",
                        CurrencyString(curr_str2, pennies_amount, CT_REAL, TRUE),
                        CurrencyString(curr_str2, pennies_amount, CT_REAL, TRUE),
                        hidden_cc,
                        vtime, queue_entry.player_id,ecash_id);

			strcat(email_buffer,   "If this purchase was made in error, please notify us at\n");
                        strcat(email_buffer, "support@kkrekop.io. Credit card fraud is a criminal offence and we\n");
                        strcat(email_buffer, "will escalate unlawful transactions immediately for the legitimate\n");
			strcat(email_buffer, "cardholder and DesertPoker's protection.\n");
			strcat(email_buffer,"\nPlease remember that it is the Player's responsiblity to keep your Player\n");
			strcat(email_buffer,"ID and Password secure. Should you need to change your password, go to the Desert Poker\n");
			strcat(email_buffer,"Lobby and Choose \"Change Password\" from the \"Options\" menu.\n\n");

                                // kriskoin : If the firepay & c/c deposit is the first $200+ deposit, ...
                                if (ct.transaction_amount>=20000 && DepositBonusRate>0 && \
					(!GetIniBonusStatusForPlayer(queue_entry.player_id))){
                                     int bonus;
                                     int i;
                                     SDBRecord r;
                                     bonus=ct.transaction_amount * DepositBonusRate / 100;
				     if (bonus>10000) bonus=10000;	//$100 bunus maximam
                                     i = SDB->SearchDataBaseByUserID("Marketing", &r);
                                     SDB->TransferChips(r.player_id, queue_entry.player_id, bonus);
                                     //Log to the transaction history
                                     ClientTransaction ct_bonus;
                                     zstruct(ct_bonus);
                                     ct_bonus.credit_left = 0;
                                     ct_bonus.timestamp = time(NULL);
                                     ct_bonus.transaction_amount = bonus;
                                     ct_bonus.transaction_type = CTT_TRANSFER_IN;
                                     ct_bonus.ecash_id = SDB->GetNextTransactionNumberAndIncrement(queue_entry.player_id);
                                     strcpy(ct_bonus.str, "1st Bonus");
                                     SDB->LogPlayerTransaction(queue_entry.player_id, &ct_bonus);

				     DepositBonusCashoutPoints = 2 * bonus /100;
                                     //SDB->ClearCreditFeePoints(queue_entry.player_id);
                                     SDB->AddToCreditFeePoints(queue_entry.player_id,DepositBonusCashoutPoints);
                                     DisableCashoutForPlayer(queue_entry.player_id);
                                     DisableIniBonusForPlayer(queue_entry.player_id);
                                }

                                // end kriskoin 


                                // kriskoin : If the firepay or c/c deposit is in promotional period, do more...
                                time_t cur_time = time(NULL);
                                //kp(("%s(%d) cur_time->%d, promo_start->%d, promo_end->%d\n", _FL, \
                                //        cur_time, DepositBonusStartDate, DepositBonusEndDate));
                                if (cur_time > DepositBonusStartDate && cur_time < DepositBonusEndDate){
                                        int bonus;
                                        int total_bonus;
                                        int i;
                                        SDBRecord r;
                                        bonus=ct.transaction_amount * DepositBonusRate / 100;
                                        total_bonus = SDB->GetTotalBonus(queue_entry.player_id, \
                                                   DepositBonusStartDate, DepositBonusEndDate);
                                        if ((bonus+total_bonus)>DepositBonusMax)
                                            bonus=DepositBonusMax - total_bonus;
                                        kp(("%s(%d) bonus: %d, total_bonus: %d, deposit: %d\n", _FL, \
                                            bonus, total_bonus, ct.transaction_amount));

                                        if(bonus>0){
                                            i = SDB->SearchDataBaseByUserID("Marketing", &r);
                                            SDB->TransferChips(r.player_id, queue_entry.player_id, bonus);
                                            //Log to the transaction history
                                            ClientTransaction ct_bonus;
                                            zstruct(ct_bonus);
                                            ct_bonus.credit_left = 0;
                                            ct_bonus.timestamp = time(NULL);
                                            ct_bonus.transaction_amount = bonus;
                                            ct_bonus.transaction_type = CTT_TRANSFER_IN;
                                            ct_bonus.ecash_id = SDB->GetNextTransactionNumberAndIncrement(queue_entry.player_id);
                                            strcpy(ct_bonus.str, "Dep Bonus");
                                            SDB->LogPlayerTransaction(queue_entry.player_id, &ct_bonus);

					    DepositBonusCashoutPoints = 2 * bonus/100;
                                            //SDB->ClearCreditFeePoints(queue_entry.player_id);
                                            SDB->AddToCreditFeePoints(queue_entry.player_id,DepositBonusCashoutPoints);
                                            DisableCashoutForPlayer(queue_entry.player_id);
					}
                                } // end kriskoin : otherwise, don't invoke the bonus function

				CheckRealPlayerForPlayer(queue_entry.player_id);
	
				// 24/01/01 kriskoin:
				if (SDB->GetNextTransactionNumber(queue_entry.player_id) == 1) {	// 0 was the first one
					if (pennies_amount >= 40000) {
						// set to alert 5 (sounds about right)
						// 24/01/01 kriskoin:
  						// 24/01/01 kriskoin:
						SendAdminAlert(ALERT_8, "%s just purchased %s as the first transaction.",
							player_rec.user_id, CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE));
					}
				}
				SendAdminAlert(ALERT_2, "PurchApp for %s : %s - (%d - %s)",
					player_rec.user_id, CurrencyString(curr_str1,pennies_amount, CT_REAL,TRUE),
					iLastECashID, hidden_cc);
			} else {	// transaction rejected
				err_code = (ers.errCode ? atoi(ers.errCode) : -1);
				sub_err_code = (ers.subError ? atoi(ers.subError) : -1);
				tr_index = CLTR_REJ_PURCHASE;
				//kriskoin: 				// the comments in FillErrorResultBuffer() for details on the sorts
				// of things each set of error codes mean in the real world.
				if (err_code==34 && sub_err_code==1005) {
					sprintf(message_to_send_client,
						"Purchase attempt DECLINED.\n"
						"\n"
						"Please try a different credit card.\n"
						"\n"
						"Full details can be found at the bottom of the\n"
						"email that has been sent to you now.\n"
					);
				} else {
					sprintf(message_to_send_client,
						"Transaction DECLINED:\n"
						"\n"
						"Unable to purchase %s on c/c %s\n"
						"\n"
						"%s\n"
						"%s\n"
						"\n"
						"Full details have been emailed to you.\n",
						CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc,
						(ers.errString ? ECash_FixReplyString(ers.errString,fixed_string1,MAX_FIXED_STRING_LEN) : "No error information available"),
						(ers.subErrorString ? ECash_FixReplyString(ers.subErrorString,fixed_string2,MAX_FIXED_STRING_LEN) : "No further information available") );
				}

				sprintf(email_buffer+strlen(email_buffer),
					"Transaction DECLINED:\n"
					"\n"
					"Unable to purchase %s on c/c %s\n"
					"\n",
					CurrencyString(curr_str1, pennies_amount, CT_REAL,TRUE), hidden_cc);

				// fill error details for this transaction
				FillErrorResultBuffer(err_code, sub_err_code);
				strcat(email_buffer, ecash_error_buffer);

				SendAdminAlert(ALERT_2, "PurchRej for %s : %s (%s)",
					player_rec.user_id, CurrencyString(curr_str1,pennies_amount, CT_REAL,TRUE),
					hidden_cc);
				//kriskoin: 				// the time the request was submitted and when we issue
				// the response to the player.  This is to make it tedious
				// for people to go through lots of different credit
				// card numbers in a row trying to find one that works.
				#define MIN_PURCHREJ_ANSWER_TIME	45	// delay in seconds
				int sleep_time = tr->queue_time + MIN_PURCHREJ_ANSWER_TIME - time(NULL);
				if (iRunningLiveFlag && sleep_time > 0 && sleep_time <= MIN_PURCHREJ_ANSWER_TIME) {
					//kp(("%s(%d) Beginning to sleep for %ds due to PurchRej...\n",_FL,sleep_time));
					Sleep(sleep_time*1000);
					//kp(("%s(%d) Done sleeping for %ds due to PurchRej.\n",_FL,sleep_time));
				}
			}
		} else {
			// We got a parse error... the transaction MAY have gone through.
			iMaybePurchaseOccurred = TRUE;
			iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
			DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
			tr_index = CLTR_ERR_PARSE;
			Error(ERR_INTERNAL_ERROR, "%s(%d) ParseEcashReply returned an error!! Check transaction\n", _FL);

		  #if 1	// 2022 kriskoin
			strnncpy(message_to_send_client,
				"Transaction error:\n"
				"There was an error contacting our ecash provider.\n"
				"This transaction will have to be looked into manually.\n\n"
				"You will receive an email with more details.\n"
				"Please contact cashier@kkrekop.io",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io");
			strcat(email_buffer," as this transaction will have to be looked into manually.\n\n");
			strcat(email_buffer,"It is POSSIBLE that your card was charged. If that happened,\n");
			strcat(email_buffer,"the transaction will be reversed manually by customer support.\n");
			strcat(email_buffer,"This problem is temporary.\n");
		  #else
			strnncpy(message_to_send_client,
				"There was an error attempting to process your request.  Please try again later",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR PROCESSING YOUR TRANSACTION ***\n");
			strcat(email_buffer,"This transaction was not processed.  Your card will not be charged.\n");
			strcat(email_buffer,"This problem is temporary.  Please try again in a few minutes.\n");
		  #endif
			SendAdminAlert(ALERT_7, "ErrTransact - MAYBE (Purchase) for %s (%08lx) had a parsing error. Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}
	} else if (rc_post == ERR_SERIOUS_WARNING) {	// possibly went through
		//kriskoin: 		// went through or not.
		int looked_up_transaction_number = 0;
		ErrorType verify_result = TransactionFailureLookup(szTransaction, &looked_up_transaction_number);
		if (verify_result==ERR_NONE) {
			// It looked up ok... query it.
			verify_result = TransactionQuery(looked_up_transaction_number, szTransaction);
		}
	  #if WIN32 && 0
		kp(("%s(%d) *** CONTINUING TO TREAT ALL TRANSACTIONS AS MAYBE'S!\n", _FL));
		verify_result = ERR_SERIOUS_WARNING;
	  #endif
		if (verify_result==ERR_NONE) {
			// It definitely went through...
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			iMaybePurchaseOccurred = FALSE;

			kp(("%s %s(%d) TransactionQuery() indicates it DID go through\n",TimeStr(),_FL));
			// Spoof a result string
			zstruct(result_str);
			sprintf(result_str,
					"status=SP&"
					"authCode=Spoofed&"
					"authTime=%u&"
					"curAmount=0&"
					"amount=%d&"
					"txnNumber=%d&"
					"serviceVersion=1.03",
					time(NULL),
					pennies_amount,
					iLastECashID+1);
			AddToEcashLog(queue_entry.player_id, "SpoofAuth", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Query succeeded", 0, 0);
		  #if 0
			char subject[100];
			zstruct(subject);
			sprintf(subject,
					"Check transaction %d for %s (approved)",
					iLastECashID+1, player_rec.user_id);
			EmailStr("support@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"Purchase for %s (transaction %d) had a communication\n"
					"failure, but the new transaction verification code\n"
					"Told it actually went through and treated it as such.\n"
					"\n"
					"For the next while, someone should verify that\n"
					"this purchase really did get approved.\n",
					iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
					player_rec.user_id, iLastECashID+1);
		  #endif
			goto parsepurch;
		} else if (verify_result==ERR_ERROR) {
			// It definitely did NOT go through.
			// Treat as PurchRej.
			iConsecutiveFailedTransactions = 0;	// reset
			iConsecutiveMaybeTransactions = 0;	// reset
			iMaybePurchaseOccurred = FALSE;
			kp(("%s %s(%d) TransactionQuery() indicates it did NOT go through\n",TimeStr(),_FL));
			zstruct(result_str);
			// Spoof a result string
			zstruct(result_str);
			sprintf(result_str,
					"status=E&"
					"amount=%d&"
					"errCode=213&"	// authorization aborted
					"errString=Authorization failed - Communication Error&"
					"serviceVersion=1.03&",
					pennies_amount);
			AddToEcashLog(queue_entry.player_id, "SpoofRej", pennies_amount,
				spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
				0, "Query failed", 0, 0);
		  #if 0
			char subject[100];
			zstruct(subject);
			sprintf(subject,
					"Check transaction %d for %s (rejected)",
					iLastECashID+1, player_rec.user_id);
			EmailStr("accounting@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"Purchase for %s (transaction %d) had a communication\n"
					"failure, but the new transaction verification code\n"
					"Told it was rejected and treated it as such.\n"
					"\n"
					"If it got rejected, then transaction %d should be\n"
					"the number of the next transaction that really goes\n"
					"through.\n"
					"\n"
					"For the next while, someone should verify that\n"
					"this purchase really did get rejected.\n",
					iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
					player_rec.user_id, iLastECashID+1, iLastECashID+1);
		  #endif
			goto parsepurch;
		}

		// We're still uncertain if it went through...				
		iConsecutiveMaybeTransactions++;	// another one MIGHT have gone through
		tr_index = CLTR_ERR_TRANSACT_MAYBE;
		DisableCashierForPlayer(queue_entry.player_id);	// set the SDBRECORD_FLAG_NO_CASHIER bit
		if (!err_msg_sent_already) {
			err_msg_sent_already = TRUE;
			strnncpy(message_to_send_client,
				"Transaction error:\n"
				"There was an error contacting our ecash provider.\n"
				"This transaction will have to be looked into manually.\n\n"
				"You will receive an email with more details.\n"
				"Please contact cashier@kkrekop.io",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"*** PLEASE CONTACT US AT cashier@kkrekop.io");
			strcat(email_buffer," as this transaction will have to be looked into manually.\n\n");
			strcat(email_buffer,"It is POSSIBLE that your card was charged. If that happened,\n");
			strcat(email_buffer,"the transaction will be reversed manually by customer support.\n");
			strcat(email_buffer,"This problem is temporary.\n");
			SendAdminAlert(ALERT_9, "ErrTransact - MAYBE (Purchase) for %s (%08lx) MAY have gone through: Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}

		// Send an email to support to indicate this maybe purchase must be looked into
		char subject[200];
		zstruct(subject);
		sprintf(subject, "%sCashier: %s had a maybe purchase for %s",
				iRunningLiveFlag ? "" : "Test: ",
				player_rec.user_id,
				CurrencyString(curr_str1, pennies_amount, CT_PLAY, TRUE));
		EmailStr(
				"support@kkrekop.io",
				"Cashier",
				"cashier@kkrekop.io",
				subject,
				NULL,
				"%s"
				"User id %s had a purchase which MAY or MAY NOT have gone through.\n"
				"\n"
				"This transaction MUST be looked into manually.  The client is\n"
				"LOCKED OUT OF THE CASHIER until this issue is resolved.\n"
				"The server attempted to determine the outcome of this transaction itself\n"
				"for up to 10 minutes but was unable to get a decent answer from\n"
				"the transaction server.  It has now given up and passed the problem\n"
				"on to the customer support department (you) for manual resolution.\n"
				"\n"
				"Details:\n"
				"\n"
				"    User ID: %s\n"
				"    Purchase amount: %s\n"
				"    Merchant transaction number: %s\n"
				,
				iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n",
				player_rec.user_id,
				player_rec.user_id,
				CurrencyString(curr_str1, pennies_amount, CT_REAL),
				szTransaction);
	} else {	// no way it went through
		tr_index = CLTR_ERR_TRANSACT;
		if (!err_msg_sent_already) {
			err_msg_sent_already = TRUE;
			strnncpy(message_to_send_client,
				"Transaction not successful:\n\n"
				"There was an error contacting our ecash provider.\nPlease try again later",
				MAX_MISC_CLIENT_MESSAGE_LEN);
			strcat(email_buffer,"\n*** THERE WAS AN ERROR CONTACTING OUR TRANSACTION SERVER ***\n");
			strcat(email_buffer,"This transaction was not processed.  Your card will not be charged.\n\n");

			time_t now = time(NULL);
			struct tm tm;
			zstruct(tm);
			struct tm *t = localtime(&now, &tm);
		  #if WIN32 && 0	// testing...
			kp(("%s(%d) t->tm_wday = %d, t->tm_hour = %d\n", _FL, t->tm_wday, t->tm_hour));
			if (t && t->tm_wday==1 && t->tm_hour >= 21 && t->tm_hour < 22)
		  #else
			if (t && t->tm_wday==1 && t->tm_hour >= 2 && t->tm_hour < 7)
		  #endif
			{
				strcat(email_buffer,
					"On Monday mornings from 2am to 6am (CST), the "ECASH_PROCESSOR_NAME" transaction\n"
					"server is down for maintenence. Please try again after the maintenence\n"
					"is completed.\n");
			} else {
				strcat(email_buffer,"This problem is probably temporary. Please try again in a few minutes.\n");
			}

			SendAdminAlert(ALERT_5, "ErrTransact (Purchase) for %s (%08lx) DID NOT go through: Last PurchApp was %d",
				player_rec.user_id, queue_entry.player_id, iLastECashID);
		}
	}

	// log to ecash.log
	AddToEcashLog(queue_entry.player_id, szTransactStr[tr_index], pennies_amount,
		spaces_card, tr->card_name, tr->card_exp_month, tr->card_exp_year,
		ecash_id, szTransaction, err_code, sub_err_code);

	// try to notify the client if there's anything to tell him
	if (chips_balance_changed || message_to_send_client[0])	{	// try to notify the client
		if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
			EcashNotifyPlayer(queue_entry.player_id, message_to_send_client, chips_balance_changed);
		}
	}
	return ERR_NONE;
}


/**********************************************************************************
 Function ECash_Thread::FillErrorResultBuffer(void)
 date: 24/01/01 kriskoin Purpose: fill out the error message based on the codes we got back
***********************************************************************************/
void ECash_Thread::FillErrorResultBuffer(int error_code, int sub_error_code)
{
  #if WIN32 && 0	//kriskoin: 	kp(("**** WARNING: FORCING ERROR CODE NUMBERS FOR TESTING ONLY ****\n"));
	error_code=34;
	sub_error_code=1005;
  #endif
	zstruct(ecash_error_buffer);
	switch (error_code) {
	case -1:	// we sometimes set it to this, but there's no info to add for it
		break;

	//kriskoin: 	//  34,1005 occurs when a bank rejects it due to mcc coding. We don't
	//	know at this point whether that occurs for other valid decline reasons.
	//  34,1007 occurs much less frequently (.2% of the PurchRej's)
	case 34:	// Authorization refused
	case 201:	// Authorization refused
	case 221:	// Authorization failed
		if (error_code==34) {
			sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
				"Please try a different credit card.\n\n");
		}
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer), "--- Explanation: ---\n");
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			//        10        20        30        40        50        60        70
			"A DECLINE can be caused by the following:\n"
			"1) Credit Limit\n"
			"2) Many types of debit and check cards cannot be authorized.\n"
			"3) Some cards that are not internationally linked to Visa or\n"
			"    MasterCard cannot be authorized.\n"
		);
		if (error_code==34 && sub_error_code==1005) {
			sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
				//        10        20        30        40        50        60        70
				"4) Desert Poker credit card purchases are required to be coded by VISA\n"
				"    and MasterCard as 'gaming'. Several issuing banks, primarily in\n"
				"    the United States, decline transactions coded as 'gaming'.\n"
			);
		}
                if (error_code==221 && sub_error_code==1014) {
                        sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
                                //        10        20        30        40        50        60        70
                                "4) Your FirePay Email does not match your Player Account Email\n"
                        );
                }

		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			//        10        20        30        40        50        60        70
			"\n"
			"As another alternative, many players choose one of our other payment \n"
			"options. Details on our other payment options and how to use then, can\n"
			"be found at: http://www.kkrekop.io/real_money.php\n"
			"\n");
		switch (sub_error_code) {
		case 1003:	// Referral
			// not sure what this means... card was referred bad to us?  Seen only a few of
			// these... no need for additional info
			break;
		case 1005:	// Decline
			// standard and usual decline -- covered by paragraph above
			break;
		case 1007:	// Card in negative DB
			// not sure if we want to disclose this?
			kp(("%s %s(%d) player ' %s ' tried to use a c/c in the negative database\n",
				TimeStr(), _FL, player_rec.user_id));
			SendAdminAlert(ALERT_7, "%s tried to use a c/c in the negative database", player_rec.user_id);
			break;
		}
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			"This authorization failure was NOT caused by a technical problem.\n"
			"PLEASE FEEL FREE TO TRY A DIFFERENT CARD.\n"
			"\n");
		break;

	case 91:	// 3 known forms of badly formed requests
	case 93:
	case 210:
		sprintf(ecash_error_buffer, "--- Explanation: ---\n");
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			"This transaction DECLINE was caused by an input error.\n"
			"You most likely made a mistake entering the card number or expiry date.\n"
			"Please try again and ensure all numbers have been correctly entered and\n"
			"the card type (Visa/Mastercard) is correctly selected.\n"
			"\n");
			break;

	// NOT USED (credits don't go through this function)
	case 333:	// Invalid txnNumber. Please verify request parameters
		sprintf(ecash_error_buffer, "--- Explanation: ---\n");
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			"An invalid transaction number was supplied for crediting.\n"
			"\n");
		break;

	case 30:	// FirePay internal server error
	case 347:	// FirePay internal server error
		sprintf(ecash_error_buffer, "--- Explanation: ---\n");
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			"Our credit card processor has experienced an internal error in attempting\n"
			"to process this transaction.\n"
			"\n");
		break;
	default:
		sprintf(ecash_error_buffer, "--- Explanation: ---\n");
		sprintf(ecash_error_buffer+strlen(ecash_error_buffer),
			"The transaction failed with error code %d/%d\n"
			"Please contact us regarding this transaction.\n"
			"\n", error_code, sub_error_code);
		kp(("%s %s(%d)  Unhandled Ecash result error codes for specific message: %d,%d\n",
				TimeStr(), _FL, error_code, sub_error_code));
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Process our current queue_entry structure.
//
void ECash_Thread::ProcessQueueEntry(void)
{
	if (ECashDisabled || (ShotClockFlags & SCUF_CLOSE_CASHIER)) {
		// The cashier is closed...
		// Cancel this ecash queue entry and notify the player.
		// Refund any pending credit transactions if necessary.
		CancelQueueEntry();
	} else {
		// The cashier is open...
		//kp(("%s %s(%d) processing request for player_id $%08lx (ptr=$%08lx)\n", TimeStr(), _FL, queue_entry.player_id, ECash_Queue_Head));
		// 24/01/01 kriskoin:
		if (queue_entry.cct.transaction_type == CCTRANSACTION_STATEMENT_REQUEST) {
			TransactStatementRequest(&queue_entry.cct, queue_entry.player_id);
		} else {
		  #if WIN32 && 0	//kriskoin: 			#define DELAY_PER_TRANSACTION	25
			kp1(("%s(%d) *** LETTING ECASH QUEUE BUILD UP - DELAYING %ds FOR EACH TRANSACTION ***\n", _FL, DELAY_PER_TRANSACTION));
			Sleep(DELAY_PER_TRANSACTION*1000);
		  #endif
			TransactEcashRequest();
		}
		//kp(("%s %s(%d) finished processing request for player_id $%08lx\n", TimeStr(), _FL, queue_entry.player_id));
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Test if the credit card processor is up and working.
// Return TRUE for working, FALSE for not working.
// We only return TRUE if we get a definite response.  A 'maybe'
// response will force a return result of FALSE.
//
int ECash_Thread::TestIfECashProviderIsUp(void)
{
	CCTransaction tr;
	zstruct(tr);
	char action_string[1000];	// these wind up being ~210 chars
	zstruct(action_string);
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(result_str);

	tr.card_type = CCTYPE_VISA;
	tr.transaction_type = CCTRANSACTION_PURCHASE;
	strcpy(tr.amount, "100");
	strcpy(tr.user_id, "Automated Test");
	strcpy(tr.card_name, "Automated Test");
	tr.queue_time = time(NULL);
	strcpy(tr.card_number, "1234123412341234");
	strcpy(tr.card_exp_month, "10");
	strcpy(tr.card_exp_year, "04");

	BuildPurchaseRequestPostString(&tr, action_string,
				"Test Transaction", tr.user_id, tr.card_number, "test@kkrekop.io","teststreet", "123456","Vancouver", "BC", "M5E 1W1", "CA" );
	int starting_time = SecondCounter;
	int rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_CHARGE_URL);
	if (rc_post!=ERR_NONE) {
		// Didn't go through well enough for us to consider
		// the provider to be up.
		return FALSE;	// not up.
	}

	int elapsed = SecondCounter - starting_time;
	if (elapsed >= 100) {
		// too long. treat as still down.
		return FALSE;	// not up yet.
	}
	EcashReplyStruct ers;
	zstruct(ers);
	int rc_parse = ParseEcashReply(&ers, result_str);
	if (rc_parse != ERR_NONE) {	// parsed unsuccessfully
		return FALSE;	// not up.
	}
	// Doesn't matter what the result was... (although it BETTER be
	// a PurchRej!)... iT looks like the processor is now up.
	return TRUE;
}

/**********************************************************************************
 ErrorType  TransactStatementRequest
 Date: 20180707 kriskoin :  Purpose: process a request for a statement
***********************************************************************************/
ErrorType ECash_Thread::TransactStatementRequest(CCTransaction *tr, WORD32 player_id)
{
	if (tr->transaction_type != CCTRANSACTION_STATEMENT_REQUEST) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Received transaction type (%d) - see src", _FL, tr->transaction_type);
		return ERR_ERROR;
	}

	// get player we're looking for
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		Error(ERR_ERROR,"%s(%d) Couldn't find player id %08lx requesting CC statement",
			_FL, player_id);
		return ERR_ERROR;
	}

	kp(("%s(%d) Received credit card statement request for %08lx by %08lx\n", _FL, player_id, tr->admin_player_id));
	// read in all lines that we'll need
	#define MAX_STATEMENT_STR_LEN	50
	struct StatementTransactionType {	// entry for each client transaction
		char date[MAX_STATEMENT_STR_LEN];
		char time[MAX_STATEMENT_STR_LEN];
		char cc_str[MAX_STATEMENT_STR_LEN];
		char tr_str[MAX_STATEMENT_STR_LEN];
		char tr_desc[MAX_STATEMENT_STR_LEN];
		char amount_str[MAX_STATEMENT_STR_LEN];
		int amount;
		ClientTransactionType ctt;
	};	
	#define MAX_STATEMENT_LINES	1000
	StatementTransactionType statement[MAX_STATEMENT_LINES];
	memset(statement, 0, sizeof(StatementTransactionType) * MAX_STATEMENT_LINES);
	// read in loop
	#define MAX_ECASHLOGLINE_LEN	300
	#define MAX_ECASHLOGLINE_PARMS	15
	char data_line[MAX_ECASHLOGLINE_LEN];
	char *argv[MAX_ECASHLOGLINE_PARMS];
	int line_index = 0;
	int total_purchases = 0;
	int total_credits = 0;
	int total_checks = 0;
	
	#define MAX_STATEMENT_CREDIT_CARDS		30
	char cc_list[MAX_STATEMENT_CREDIT_CARDS][MAX_STATEMENT_STR_LEN];
	memset(cc_list, 0, MAX_STATEMENT_CREDIT_CARDS * MAX_STATEMENT_STR_LEN);

	FILE *in = NULL;
	char fname[MAX_FNAME_LEN];
	zstruct(fname);
	for (int loop_count = 4; loop_count >=0; loop_count--) {
		in = NULL;	// reset for every loop
		// we're reading ecash.log.*
		if (!loop_count) {
			strnncpy(fname, "Data/Logs/ecash.log", MAX_FNAME_LEN);
		} else {
			sprintf(fname,"Data/Logs/ecash.log.%d", loop_count);
		}
		// open it, NULL result is ok
		if ((in = fopen(fname,"rt")) == NULL) {
			pr(("%s(%d) Couldn't open %s for read\n", _FL, fname));;
			continue;
		}

		// exit loop if finished (though this will be caught above)
		if (!in) {
			break;
		}
		// main parsing loop
		while (!feof(in)) {
			zstruct(data_line);
			fgets(data_line, MAX_ECASHLOGLINE_LEN-1, in);
		  #if 0	//kriskoin: 		  		// This is exclusively to test how the other threads handle this situation.
			if (!iRunningLiveFlag) {
				kp1(("%s(%d) **** WARNING: SLEEPING AFTER EACH LINE OF ecash.log FILE!\n", _FL));
				Sleep(30);
			}
		  #endif
			int count = ECash_GetArgs(MAX_ECASHLOGLINE_PARMS, argv, data_line, ',');
			if (count == 13) {	// potential line
				WORD32 player_id_in;
				sscanf(argv[2],"%x",&player_id_in);
				if (player_id_in == player_id) {	// we want this line
					strnncpy(statement[line_index].date, argv[0], MAX_STATEMENT_STR_LEN);
					strnncpy(statement[line_index].time, argv[1], MAX_STATEMENT_STR_LEN);
					strnncpy(statement[line_index].tr_str, argv[9], MAX_STATEMENT_STR_LEN);
					statement[line_index].amount = atoi(argv[4]);
					CurrencyString(statement[line_index].amount_str, statement[line_index].amount, CT_REAL, TRUE);
					statement[line_index].ctt = CTT_NONE;
					if (!strcmp(argv[3], "PurchApp")) {
						statement[line_index].ctt = CTT_PURCHASE;
						strnncpy(statement[line_index].tr_desc, "PURCHASE", MAX_STATEMENT_STR_LEN);
						total_purchases += statement[line_index].amount;
						// build credit card string
						char cc_first4[6];
						zstruct(cc_first4);
						strnncpy(cc_first4, argv[5], 5);
						char *p = argv[5]+(strlen(argv[5])-5);
						if (*p == ' ') {	// difference in logging, may be a blank there
							*p++;
						}
						sprintf(statement[line_index].cc_str, "%s...%s", cc_first4, p);

						// find which cc slot these go into
						int cc_num = -1, first_blank = -1;
						for (int j=0; j < MAX_STATEMENT_CREDIT_CARDS; j++) {
							if (!stricmp(statement[line_index].cc_str, cc_list[j])) {	// found it
								cc_num = j;
								break;
							}
							// perhaps it's the first usable blank
							if (first_blank < 0 && !cc_list[j][0]) {
								first_blank = j;
							}
						}
						if (cc_num < 0 && first_blank < 0) {	// trouble
							Error(ERR_ERROR,"%s(%d) Statement generator ran out of room (see src)", _FL);
							continue;
						}
						if (cc_num < 0) {	// didn't find one, assign blank
							cc_num = first_blank;
							strnncpy(cc_list[cc_num], statement[line_index].cc_str, MAX_STATEMENT_STR_LEN);	
						}
					
					} else if (!strcmp(argv[3], "CredApp")) {
						statement[line_index].ctt = CTT_CREDIT;
						strnncpy(statement[line_index].tr_desc, "CREDIT", MAX_STATEMENT_STR_LEN);
						total_credits += statement[line_index].amount;
					} else if (!strcmp(argv[3], "CrCheck")) {
						statement[line_index].ctt = CTT_CHECK_ISSUED;
						strnncpy(statement[line_index].tr_desc, "CHECK", MAX_STATEMENT_STR_LEN);
						total_checks += statement[line_index].amount;
					}
				}
				if (statement[line_index].ctt == CTT_NONE) {	// don't care about this line, let it be overwritten
					zstruct(statement[line_index]);
					continue;
				}
				// we want it
				line_index++;
				if (line_index == MAX_STATEMENT_LINES) {	// stop reading
					Error(ERR_ERROR,"%s(%d) Ran out of statement lines for %08lx (%d)", _FL, player_id, line_index);
					break;
				}
			}
		}
		FCLOSE(in);
	}
	// fill in credit cards for credit transactions
	for (int i=0; i < line_index; i++) {
		if (statement[i].ctt == CTT_CREDIT) {	// find corresponding purchase
			for (int j=0; j < line_index; j++) {
				if (statement[j].ctt == CTT_PURCHASE && !stricmp(statement[j].tr_str, statement[i].tr_str)) {	// find corresponding purchase
					strnncpy(statement[i].cc_str, statement[j].cc_str, MAX_STATEMENT_STR_LEN);
					break;
				}
			}
		}
	}
	// build the email
	FILE *out;
	char filename[MAX_FNAME_LEN];
	zstruct(filename);
	MakeTempFName(filename, "ccst");
	if ((out = fopen(filename, "wt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL, filename);
		return ERR_ERROR;
	}
	if (!iRunningLiveFlag) {
		fprintf(out, "*** FROM THE TEST SERVER ***\n");
	}
	fprintf(out, "Credit Card Statement for %s (%s) -- %s\n\nTransactions are listed newest to oldest\n\n",
		player_rec.full_name, player_rec.user_id, DateStrWithYear());
	for (int k=line_index-1; k >=0; k--) {
		// ignore checks for now
		if (statement[k].ctt == CTT_CHECK_ISSUED) {
			continue;
		}
		fprintf(out, "%s %s %8s %10s  %-12s %-8s\n",
			statement[k].date,
			statement[k].time,
			statement[k].tr_desc,
			statement[k].amount_str,
			statement[k].cc_str,
			statement[k].tr_str);
	}
	// summaries
	fprintf(out, "\n==== TOTALS: ==========================\n");
	int purchases, purchase_count, credits, credit_count;
	char curr_str[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str);
	for (int m=0; m < MAX_STATEMENT_CREDIT_CARDS; m++) {
		if (cc_list[m][0]) {	// tally for this card
			purchases = 0;
			purchase_count = 0;
			credits = 0;
			credit_count = 0;
			fprintf(out, "CREDIT CARD %s (%s):\n",
				cc_list[m], (cc_list[m][0] == '4' ? "Visa" : "MasterCard") );
			for (int n=0; n < line_index; n++) {
				if (!stricmp(statement[n].cc_str, cc_list[m])) {
					switch (statement[n].ctt) {
					case CTT_PURCHASE:
						purchases += statement[n].amount;
						purchase_count++;
						break;
					case CTT_CREDIT:
						credits += statement[n].amount;
						credit_count++;
						break;
					}
				}
			}
			if (purchase_count) {
				fprintf(out, "  %d PURCHASE %s -- TOTAL %s\n",
					purchase_count,	
					(purchase_count == 1 ? "TRANSACTION " : "TRANSACTIONS"),
					CurrencyString(curr_str, purchases, CT_REAL, TRUE));
			} else {
				fprintf(out, "  NO PURCHASE TRANSACTIONS\n");
			}
			if (credit_count) {
				fprintf(out, "  %d CREDIT %s ---- TOTAL %s\n",
					credit_count,	
					(credit_count == 1 ? "TRANSACTION " : "TRANSACTIONS"),
					CurrencyString(curr_str, credits, CT_REAL, TRUE));
			} else {
				fprintf(out, "  NO CREDIT TRANSACTIONS\n");

			}
		}
		fprintf(out, "\n");
	}
	FCLOSE(out);
	// ship it
	char email_title[100];
	zstruct(email_title);
	sprintf(email_title, "Credit Card Statement for %s (%s) -- %s", player_rec.full_name, player_rec.user_id, DateStrWithYear());
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(tr->admin_player_id, &player_rec) < 0) {
		Error(ERR_ERROR,"%s(%d) Couldn't find player id %08lx requesting CC statement (for email address)",
			_FL, player_id);
		return ERR_ERROR;
	}
	Email(player_rec.email_address, "Desert Poker Credit Card Statement", "cashier@kkrekop.io",
		email_title, filename, NULL, TRUE);
	return ERR_NONE;
}

/**********************************************************************************
 Function TransactEcashRequest
 Date: 2017/7/7 kriskoin Purpose: do the actual transaction with SFC
***********************************************************************************/
void ECash_Thread::TransactEcashRequest(void)
{
	char email_title[100];
	char our_email_title[100];
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	zstruct (email_title);
	zstruct (our_email_title);
	zstruct(curr_str1);
	chips_balance_changed = 0;
	check_amount = 0;
	memset(email_buffer, 0, MAX_ECASH_EMAIL_TEXT_LEN);	// zero out our output buffer
	tr_index = CLTR_UNDEFINED;

	pennies_amount = atoi(queue_entry.cct.amount);
	if (pennies_amount < 0) {
		kp(("%s %s(%d) player_id $%08lx tried to do an ecash request for %d!  Aborting it.\n",
					TimeStr(), _FL, queue_entry.player_id, pennies_amount));
		return;
	}

  #if USE_TEST_SERVER
	strcpy(email_buffer, "*** This transaction is using one of the test servers, not the real one ***\n\n");
  #endif

	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(queue_entry.player_id, &player_rec) < 0) {
		kp(("%s %s(%d) ERROR: could not find player_id $%08lx in database during TransactEcashRequest()\n",
					TimeStr(), _FL, queue_entry.player_id));
		return;
	}

	int prev_ecash_id = iLastECashID;	// save previous one
        int ctransaction_ret;
	switch (queue_entry.cct.transaction_type) {
	case CCTRANSACTION_ADMIN_CREDIT:
		ProcessAdminCredit();
		break;
	case CCTRANSACTION_CASHOUT:
		ProcessCredit();
		break;
	case CCTRANSACTION_FIREPAY_CASHOUT:
                ProcessFirePayCredit();
                break;

	case CCTRANSACTION_PURCHASE:
	  #if TEST_SKIPPED_TRANSACTION_CODE
		{
			kp1(("%s(%d) ** WARNING: TESTING WITH FAKE TRANSACTION NUMBERS! **\n", _FL));
			SetPotentialLowestTransactionNumber(0);
			TransactionNumberReceived(0, 500000);
			SetPotentialLowestTransactionNumber(1);
			SetPotentialLowestTransactionNumber(2);
			SetPotentialLowestTransactionNumber(3);
			SetPotentialLowestTransactionNumber(4);
			TransactionNumberReceived(1, 500001);
			TransactionNumberReceived(2, 500002);
			TransactionNumberReceived(3, 500003);
			SetPotentialLowestTransactionNumber(1);
			SetPotentialLowestTransactionNumber(2);
			SetPotentialLowestTransactionNumber(3);
			// skip 500004
			TransactionNumberReceived(1, 500005);
			SetPotentialLowestTransactionNumber(1);
			TransactionNumberReceived(2, 500006);
			SetPotentialLowestTransactionNumber(2);
			TransactionNumberReceived(3, 500007);
			SetPotentialLowestTransactionNumber(3);
			// skip 500008
			// skip 500009
			// skip 500010
			TransactionNumberReceived(3, 500011);
			SetPotentialLowestTransactionNumber(1);
			SetPotentialLowestTransactionNumber(2);
			SetPotentialLowestTransactionNumber(3);
			TransactionNumberReceived(1, 500012);
			// skip 500013
			TransactionNumberReceived(2, 500014);
			TransactionNumberReceived(3, 500015);
			SetPotentialLowestTransactionNumber(1);
			TransactionNumberReceived(1, 500016);
			return;
		}
	  #else
		{
			SetPotentialLowestTransactionNumber(thread_number);
			int rc = ProcessPurchase();
			ClearPotentialLowestTransactionNumber(thread_number);
			if (rc != ERR_NONE) {
				// The purchase was aborted.  Player has already been notified.
				// Don't send a summary email.
				return;
			}
		}
	  #endif
		break;
          case CCTRANSACTION_FIREPAY_PURCHASE:
			SetPotentialLowestTransactionNumber(thread_number);
			ctransaction_ret = ProcessFirePayPurchase();
			ClearPotentialLowestTransactionNumber(thread_number);
			if (ctransaction_ret != ERR_NONE) {
				// The purchase was aborted.  Player has already been notified.
				// Don't send a summary email.
				return;
			}
	          break;

	
	default:
		Error(ERR_INTERNAL_ERROR, "%s(%d) Unknown transaction type! (%d) for player $%08lx\n",
			_FL, queue_entry.cct.transaction_type, queue_entry.player_id);
		return;
	}
	// footer
	strcat(email_buffer, "*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*\n\n");
	strcat(email_buffer,
		"If you have any questions regarding this transaction,\n"
		"please contact us at cashier@kkrekop.io\n");
	// email it
	if (queue_entry.cct.transaction_type == CCTRANSACTION_ADMIN_CREDIT) {
		sprintf(our_email_title,"%s - %s : Admin Credit Summary", szTransactStr[tr_index],
			(player_rec.user_id[0] ? player_rec.user_id : "(unknown)") );
		strnncpy(email_title, our_email_title, 100);
	} else if (queue_entry.cct.transaction_type == CCTRANSACTION_PURCHASE) {
		sprintf(email_title, "Desert Poker Deposit Summary");
		sprintf(our_email_title,"%s - %s : Deposit Summary", szTransactStr[tr_index],
			(player_rec.user_id[0] ? player_rec.user_id : "(unknown)") );
	} else if (queue_entry.cct.transaction_type == CCTRANSACTION_CASHOUT) {
		sprintf(email_title,"Desert Poker %s Summary", (check_amount ? "Cash Out/Check" : "Cash-Out Paypal"));
		if (check_amount) {
			sprintf(our_email_title,"%s - %s (%s): Cash Out Summary", szTransactStr[tr_index],
				(player_rec.user_id[0] ? player_rec.user_id : "(unknown)"),
				// CT_PLAY is used because we don't want $ passed to the email title
				CurrencyString(curr_str1, check_amount, CT_PLAY, TRUE) );
		} else {
			sprintf(our_email_title,"%s - %s (%s) : Cash Out Summary", "CrPayPal"/*szTransactStr[tr_index]*/,
				(player_rec.user_id[0] ? player_rec.user_id : "(unknown)"),
				CurrencyString(curr_str1, paypal_amount, CT_PLAY, TRUE) );
		}
	} else if (queue_entry.cct.transaction_type == CCTRANSACTION_FIREPAY_PURCHASE) {
                sprintf(email_title, "Desert Poker Credit Card Purchase Summary");
                sprintf(our_email_title,"%s - %s : Deposit Summary", szTransactStr[tr_index],
                        (player_rec.user_id[0] ? player_rec.user_id : "(unknown)") );
		}
	else if (queue_entry.cct.transaction_type == CCTRANSACTION_FIREPAY_CASHOUT)
		
		{
                sprintf(email_title, "Desert Poker Credit Card CASHOUT Summary");
                sprintf(our_email_title,"%s - %s : Cash Out Summary", szTransactStr[tr_index],
                        (player_rec.user_id[0] ? player_rec.user_id : "(unknown)") );
                }
	else {
		sprintf(email_title,"Unknown Ecash transaction");
		sprintf(our_email_title,"Unknown Ecash transaction");
	}

	if (!(queue_entry.cct.flags & CCTF_NO_NOTICES)) {
		FILE *file_out;
		char filename[MAX_FNAME_LEN];
		zstruct(filename);
		MakeTempFName(filename, "ece");
		if ((file_out = fopen(filename, "wt")) == NULL) {
			Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL, filename);
		} else {
			fputs(email_buffer, file_out);	// dump the whole thing out
			fclose(file_out);
			if (player_rec.flags & (SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES)) {
				Error(ERR_WARNING,"%s(%d) Couldn't email player %08lx transaction summary %s -- invalid email",
					_FL, queue_entry.player_id, filename);
			} else {
				 /*if ((queue_entry.cct.transaction_type != CCTRANSACTION_FIREPAY_PURCHASE) &&
 				   (queue_entry.cct.transaction_type != CCTRANSACTION_PURCHASE ))*/
				Email(player_rec.email_address, "Desert Poker Cashier", "cashier@kkrekop.io",
					email_title, filename);
			}
			// send ourselves a copy
			// 24/01/01 kriskoin:
			Email("transaction@kkrekop.io" /*"allen@avanira.com"*/, "Desert Poker Cashier", "cashier@kkrekop.io",
				our_email_title, filename, NULL, TRUE);
		}
		EcashNotifyPlayer(queue_entry.player_id, NULL, TRUE);	// make sure the client's chips are updated
	}

	// 24/01/01 kriskoin:
	switch (tr_index) {
	case CLTR_ERR_PARSE:
	case CLTR_ERR_TRANSACT:
	case CLTR_ERR_TRANSACT_MAYBE:
		if (!iAdminMonitorToldEcashIsDown) {	// gone into error
			iAdminMonitorToldEcashIsDown = TRUE;
			SendAdminAlert(ALERT_5, "*** Ecash may be down *** -- last PurchID = %d", iLastECashID);
		}
		break;
	case CLTR_APP_PURCHASE:
	case CLTR_APP_CREDIT:
		if (iAdminMonitorToldEcashIsDown) {	// we're back
			iAdminMonitorToldEcashIsDown = FALSE;
			SendAdminAlert(ALERT_5, "*** Ecash is up *** -- last PurchID = %d (skipped %d)",
					iLastECashID, iLastECashID - prev_ecash_id - 1);
		}
		break;
	}

	// Check for skipped transactions.
	// For now, this is ONLY usable when single-threaded.
	if (tr_index==CLTR_APP_PURCHASE && ECashThreads==1) {
		// Check how the transaction sequence number compares to what we expected...
		int skipped = iLastECashID - prev_ecash_id - 1;
	  #if WIN32 && 0
		kp(("%s(%d) *** WARNING: TEST CODE IS STILL IN HERE ***\n", _FL));
		prev_ecash_id = 1000;
		skipped = 2;
	  #endif
		if (skipped==0 && iMaybePurchaseOccurred) {
			// None of the maybe purchases went through.
			SendAdminAlert(ALERT_9, "*** Ecash is up. None of the maybe purchases went through.");
			EmailStr("sfc@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					"No transactions were skipped.",
					NULL,
					"%s"
					"%s CST\n\n"
					"The cashier is working again and NO transaction sequence\n"
					"numbers were skipped.  It looks like NONE of the 'maybe' purchases\n"
					"went through and clients were NOT charged.\n"
					"\n"
					"This does not have any impact on credits. All 'maybe' credits\n"
					"must always looked into manually to see if they went through.\n"
					"\n"
					,
					iRunningLiveFlag ? "":"*** THIS IS ONLY A TEST ***\n\n",
					TimeStr());
		}

		if (prev_ecash_id && skipped > 0) {
			// At least one transaction got skipped... issue some warnings.
			SendAdminAlert(ALERT_9, "*** Ecash is up, but %d transactions got skipped ***", skipped);
			char subject[100];
			zstruct(subject);
			if (skipped==1) {
				sprintf(subject, "Transaction %d got skipped", prev_ecash_id+1);
			} else {
				sprintf(subject, "Transactions %d - %d got skipped (%d total)",
						prev_ecash_id+1, iLastECashID-1, skipped);
			}
			EmailStr("sfc@kkrekop.io",
					"Cashier",
					"cashier@kkrekop.io",
					subject,
					NULL,
					"%s"
					"%s CST\n\n"
					"The cashier is working again, but %d transaction sequence\n"
					"number%s skipped.  Some of the 'maybe' purchases\n"
					"probably went through and clients will be charged.\n"
					"\n"
					"This does not have any impact on credits. All 'maybe' credits\n"
					"must always looked into manually to see if they went through.\n"
					"\n"
					"All 'maybe' charges and credits must be looked into manually.\n"
					,
					iRunningLiveFlag ? "":"*** THIS IS ONLY A TEST ***\n\n",
					TimeStr(),
					skipped,
					skipped==1 ? " was" : "s were");
		}
		iMaybePurchaseOccurred = FALSE;
	}
	return;
}

//*********************************************************
// https://github.com/kriskoin//
// Try to find a transaction number given a merchant transaction number,
// The transaction MUST have been submitted recently because
// one of the search criteria used to narrow it down is how many seconds
// to look back in time.
// This function retries for up to MAX_ECASH_RETRY_TIME seconds.
// Returns:
//	ERR_NONE: we found and returned the transaction number, no problem.
//	ERR_SERIOUS_WARNING: could not determine if transaction exists
//	ERR_ERROR: merchange transaction number does not exist.
//
ErrorType ECash_Thread::TransactionFailureLookup(char *merchant_txn_number, int *output_transaction_number)
{
	// Build a "Failure Lookup" string to post to the transaction server
	// See the Merchant Integration Guide under "Failure Lookup".
	char action_string[400];	// these wind up being ~210 chars
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(action_string);
	zstruct(result_str);
	*output_transaction_number = 0;	// always reset by default

	sprintf(action_string,
			"account=%s&"
			"merchantTxn=%s&"
			"operation=FT&"
			"clientVersion=%s&"
		  #if 1	// 24/01/01 kriskoin:
			// according to Rachid, the docs are wrong and this number is in SECONDS
			// we want a 2 hour lookback, 2*3600 = 7200 seconds
			"searchWindow=7200&"
		  #else
			"searchWindow=120&"		// search back up to 120 minutes (Emergis was in minutes)
		  #endif
			"merchantId=%s&"
			"merchantPwd=%s",
			ECASH_ACCOUNT_ID,
			merchant_txn_number,
			ECashProtocolVersionStr,
			ECASH_MERCHANT_ID,
			ECASH_MERCHANT_PWD);

  #if 0
	kp(("%s(%d) TransactionFailureLookup(%s) action_string =\n",_FL, merchant_txn_number));
	kwrites(action_string);
	kp(("\n"));
  #endif

    WORD32 start_time = SecondCounter;
    do {
    	zstruct(result_str);
        WORD32 last_attempt_time = SecondCounter;
    	int rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_FAILURE_LOOKUP_URL);
        if (rc_post == ERR_NONE) {
            // We got through!
			EcashReplyStruct ers;
			zstruct(ers);
			int rc_parse = ParseEcashReply(&ers, result_str);
			if (rc_parse == ERR_NONE) {	// parsed successfully
				// If we got to here, we're at the end of the road.  We got to talk
				// to the server and it returned us something about that transaction.
				// We got a proper result back and parsed it, see what it means.
				// The transaction went through if we got these status results:
				//	C - Search completed and transaction_number found.
				//	NF - No transaction found within the specified window
				kp(("%s %s(%d) TransactionFailureLookup(%s): status=%s, txnNumber=%s, error = %s\n",
						TimeStr(), _FL, merchant_txn_number,
						ers.status    ? ers.status    : "(unknown)",
						ers.txnNumber ? ers.txnNumber : "(none)",
						ers.errString ? ers.errString : "(none)"));
				if (ers.status && (
					!strcmp(ers.status, "C")
				)) {
					// Search complete and it was found.
					if (ers.txnNumber) {
						*output_transaction_number = atoi(ers.txnNumber);
					} else {
						kp(("%s %s(%d) ERROR: TransactionFailureLookup(%s) wasn't returned a transaction number!\n",
									TimeStr(), _FL, merchant_txn_number));
					    return ERR_SERIOUS_WARNING;     // we never figured anything out :(
					}
					return ERR_NONE;
				}
				if (ers.status && !strcmp(ers.status, "E")) {
					// An error condition... try to look it up a few more times... they
					// might solve their problem before we need to time out.
					kp(("%s %s(%d) TransactionFailureLookup(%s) was returned an error.  Trying again.\n",
								TimeStr(), _FL, merchant_txn_number));
				} else {
					// Anything other status codes assume it was not found.
					return ERR_ERROR;	// it was not found.
				}
	        }
        }

        // Still no firm answer.  Try again.
		int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
		if (delay > 0) {
			Sleep(delay*1000);
		}
    } while(SecondCounter - start_time < MAX_ECASH_RETRY_TIME);
    return ERR_SERIOUS_WARNING;     // we never figured anything out :(
}

//*********************************************************
// https://github.com/kriskoin//
// Try to verify whether a purchase transaction went through or not
// given a transaction number.
// If you still need a transaction number, see TransactionFailureLookup().
// The transaction number and the merchant_txn_number must match
// the original request exactly.
// This function retries for up to MAX_ECASH_RETRY_TIME seconds.
// Returns:
//	ERR_NONE: transaction went through, no problem.
//	ERR_SERIOUS_WARNING: could not determine if transaction went through
//	ERR_ERROR: transaction did not go through.
//
ErrorType ECash_Thread::TransactionQuery(int transaction_number, char *merchant_txn_number)
{
	// Build a "Query" string to post to the transaction server
	// See the Merchant Integration Guide under "Query Request".
	char action_string[400];	// these wind up being ~210 chars
	char result_str[ECASH_RETURN_BUF_LEN];
	zstruct(action_string);
	zstruct(result_str);

	sprintf(action_string,
			"account=%s&"
			"operation=Q&"
			"clientVersion=%s&"
			"txnNumber=%d&"
			"merchantTxn=%s&"
			"merchantId=%s&"
			"merchantPwd=%s",
			ECASH_ACCOUNT_ID,
			ECashProtocolVersionStr,
			transaction_number,
			merchant_txn_number,
			ECASH_MERCHANT_ID,
			ECASH_MERCHANT_PWD);

  #if 0
	kp(("%s(%d) action_string =\n",_FL));
	kwrites(action_string);
  #endif

    WORD32 start_time = SecondCounter;
    do {
    	zstruct(result_str);
        WORD32 last_attempt_time = SecondCounter;
    	int rc_post = PostTransactionToECash(action_string, result_str, ECASH_RETURN_BUF_LEN, ECASH_QUERY_URL);
        if (rc_post == ERR_NONE) {
            // We got through!
			EcashReplyStruct ers;
			zstruct(ers);
			int rc_parse = ParseEcashReply(&ers, result_str);
			if (rc_parse == ERR_NONE) {	// parsed successfully
				// If we got to here, we're at the end of the road.  We got to talk
				// to the server and it returned us something about that transaction.
				// We got a proper result back and parsed it, see what it means.
				// The transaction went through if we got these status results:
				//	P - Pending (authorized, but not settled)
				//	C - Complete (settled)
				//	A - Authorization complete
				//	S - Settlement complete
			  #if 0	//kriskoin: 				if (!ers.merchantTxn || stricmp(merchant_txn_number, ers.merchantTxn)) {
					kp(("%s %s(%d) ERROR: TransactionQuery(%d): we expected txn %s but got back %s. Treating as failed.\n",
							TimeStr(), _FL, transaction_number, merchant_txn_number,
							ers.merchantTxn ? ers.merchantTxn : "(nothing)"));
				    return ERR_SERIOUS_WARNING;     // we never figured anything out :(
				}
			  #endif
				kp(("%s %s(%d) TransactionQuery(%d): status=%s, error = %s\n",
						TimeStr(), _FL, transaction_number,
						ers.status    ? ers.status    : "(unknown)",
						ers.errString ? ers.errString : "(none)"));
				if (ers.status && (
					!strcmp(ers.status, "P") ||
					!strcmp(ers.status, "A") ||
					!strcmp(ers.status, "C") ||
					!strcmp(ers.status, "S")
				)) {
					// It went through and was approved.
					return ERR_NONE;
				}
				// Anything other status codes assume it did not go through.
				return ERR_ERROR;	// it did not go through.
	        }
        }

        // Still no firm answer.  Try again.
		int delay = (int)(last_attempt_time + MIN_SPACING_BETWEEN_RETRIES - SecondCounter);
		if (delay > 0) {
			Sleep(delay*1000);
		}
    } while(SecondCounter - start_time < MAX_ECASH_RETRY_TIME);
    return ERR_SERIOUS_WARNING;     // we never figured anything out :(
}

/**********************************************************************************
 Function EcashHandlerThread(void)
 Date: 2017/7/7 kriskoin Purpose: seperate thread for dealing with ecash requests
// args contains our thread number (0 to n)
***********************************************************************************/
void _cdecl EcashHandlerThread(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	int thread_num = (int)args;
	RegisterThreadForDumps("eCash thread");	// register this thread for stack dumps if we crash
	kp(("%s %s(%d) eCash thread #%d: active.\n", TimeStr(), _FL, thread_num));
	EnterCriticalSection(&ecash_cs);
	if (thread_num <= MAX_ECASH_THREADS && !ECash_Threads[thread_num]) {
		ecash_threads_running++;
		ECash_Threads[thread_num] = new ECash_Thread(thread_num);
		if (ECash_Threads[thread_num]) {
			LeaveCriticalSection(&ecash_cs);
			ECash_Threads[thread_num]->MainLoop();
			EnterCriticalSection(&ecash_cs);
			delete ECash_Threads[thread_num];
			ECash_Threads[thread_num] = NULL;
		}
		// we have been told to shut down
		ecash_threads_running--;
	}
	LeaveCriticalSection(&ecash_cs);
	kp(("%s %s(%d) eCash thread #%d: exiting.\n", TimeStr(), _FL, thread_num));
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}

/**********************************************************************************
 Function void _cdecl LaunchEcashHandlerThread(void *)
 Date: 2017/7/7 kriskoin Purpose: launch the ecash handling thread
***********************************************************************************/
ErrorType LaunchEcashHandlerThread(void)
{
	static int crit_sec_initialized = FALSE;
	if (!crit_sec_initialized) {
		ECash_Queue_Head = NULL;
		ECash_Queue_TailPtr = NULL;
		PPInitializeCriticalSection(&ecash_cs, CRITSECPRI_LOCAL, "Ecash");
		crit_sec_initialized = TRUE;
	}
	// initialize the critical section needed for multithreaded transaction number verification
	if (!tnv_critsec_is_initialized) {
		PPInitializeCriticalSection(&tnv_cs, CRITSECPRI_TRANSACTIONNUMBER, "TransNum");
		tnv_critsec_is_initialized = TRUE;
		InitializeTNVPendingArray();	
	}

	int thread_number = 0;
	int result = _beginthread(EcashHandlerThread, 0, (void *)thread_number);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
		return ERR_FATAL_ERROR;
	}
	//kp(("%s(%d) Accept thread back from _beginthread. Our pid = %d\n", _FL, getpid()));
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Shutdown all ecash threads before the server exits.
// This function may take many minutes if processing is still occuring,
// therefore it's best not to call this function unless the queue is empty.
// This function WILL give up early, which is yet another reason to
// make sure the queue is empty before calling it.
//
void ShutdownEcashProcessing(void)
{
	// tell them to shut down
	ECashThreads = 0;
	// now wait until they are all shut down
	int loop_count = 0;
	while (ecash_threads_running > 0) {
		if (loop_count > 100) {	// give up, other thread may be hung or taking way too long
			Error(ERR_NOTE,"%s(%d) gave up waiting for ecash thread to shut down", _FL);
			break;
		}
		Sleep(200);
		loop_count++;
	}
}

/**********************************************************************************
 ErrorType AddEcashRequestToQueue(CCStatementReq *ccsr_in)
 Date: 20180707 kriskoin :  Purpose: since we're adding statement requests to the ecash queue, let's do it all
	from the same place.  We'll call the function below, after creating a transaction
	structure and passing it along.  It'll all be handled on the receiving end
***********************************************************************************/
ErrorType AddEcashRequestToQueue(CCStatementReq *ccsr_in)
{
	CCTransaction cct;
	zstruct(cct);
	cct.transaction_type = ccsr_in->transaction_type;
	cct.player_id = ccsr_in->player_id;
	cct.admin_player_id = ccsr_in->admin_player_id;
	pr(("%s(%d) Received and QUEUED ccsr struct for %08lx\n", _FL, cct.player_id));
	return AddEcashRequestToQueue(&cct);
}

/**********************************************************************************
 Function AddEcashRequestToQueue(CCTransaction *cct_in)
 Date: 2017/7/7 kriskoin Purpose: queue up an ecash request
***********************************************************************************/
ErrorType AddEcashRequestToQueue(CCTransaction *cct_in)
{
	if (!ecash_threads_running) {	// can't queue if there's no thread to handle it
		Error(ERR_INTERNAL_ERROR,"%s(%d) Tried to queue a cct, but no thread running", _FL);
		return ERR_ERROR;
	}
	if (!ECashThreads) {	// can't queue now, we're shutting down
		kp(("%s(%d) CreditCardTransaction req received while shutting down -- ignoring", _FL));
		return ERR_ERROR;
	}
	// malloc a struct for this queue entry
	struct CCTQueue *qs = (struct CCTQueue *)malloc(sizeof(CCTQueue));
	if (!qs) {
		Error(ERR_INTERNAL_ERROR,"%s(%d) Couldn't malloc a cct struct", _FL);
		return ERR_ERROR;
	}
	// build the queue structure
	memcpy(&qs->cct, cct_in, sizeof(CCTransaction));
	qs->player_id = cct_in->player_id;
	qs->next = NULL;

	//kp(("%s %s(%d) Adding CC transaction to queue.  ptr=$%08lx\n", TimeStr(), _FL, qs));
	EnterCriticalSection(&ecash_cs);
	// find its place
	if (!ECash_Queue_Head) {	// first entry
		ECash_Queue_Head = qs;
	} else {	// stuff there already
		*ECash_Queue_TailPtr = qs;
	}
	// store the last one for next use
	ECash_Queue_TailPtr = &qs->next;
	LeaveCriticalSection(&ecash_cs);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Return the number of items currently in the ecash queue
//
int Ecash_GetQueueLen(void)
{
	int len = 0;
	EnterCriticalSection(&ecash_cs);
	// First, count how many threads are currently working on something.
	for (int i=0 ; i<MAX_ECASH_THREADS ; i++) {
		if (ECash_Threads[i] && ECash_Threads[i]->processing_queue_entry_flag) {
			len++;
		}
	}

	// Now count how many more things are in the queue waiting for be processed.
	CCTQueue *cctq = ECash_Queue_Head;
	while (cctq) {
		len++;
		cctq = cctq->next;
	}

	LeaveCriticalSection(&ecash_cs);
	return len;
}

/**********************************************************************************
 Function PlayerIsInEcashQueue(WORD32 player_id)
 date: 24/01/01 kriskoin Purpose: return T/F if this player is somewhere in the ecash queue
***********************************************************************************/
int IsPlayerInEcashQueue(WORD32 player_id)
{
	int found_player = FALSE;
	EnterCriticalSection(&ecash_cs);
	for (int i=0 ; i<MAX_ECASH_THREADS ; i++) {
		if (ECash_Threads[i] && ECash_Threads[i]->queue_entry.player_id==player_id) {
			found_player = TRUE;
			break;
		}
	}
	if (ECash_Queue_Head && !found_player) {
		CCTQueue *cctq = ECash_Queue_Head;
		while(cctq) {
			if (cctq->player_id == player_id) { // found him
				found_player = TRUE;
				break;
			}
			cctq = cctq->next;
		}
	}
	LeaveCriticalSection(&ecash_cs);
	return found_player;
}

#if USE_TEST_SERVER
//*********************************************************
// https://github.com/kriskoin//
// Queue up an entire log file for processing at once.
// This is ONLY used for testing the queuing on our end and
// on the transaction server end.  It MUST NOT be used on
// a live server.
//
void ECash_QueueLogFileForTesting(WORD32 player_id, char *filename)
{
	kp(("%s(%d) ECash_QueueLogFileForTesting(0x%08lx, \"%s\");\n", _FL, player_id, filename));
	if (iRunningLiveFlag) {
		return;	// do nothing.
	}

	FILE *in;
	if ((in = fopen(filename,"rt")) == NULL) {
		fprintf(stderr,"%s(%d) Couldn't open %s for read\n", _FL, filename);
		return;
	}

	// Pull up the account for the player who requested this
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		Error(ERR_ERROR,"%s(%d) Couldn't find player id %08lx requesting queue log", 
			_FL, player_id);
		return;
	}

	// read in loop
	#define MAX_ECASHLOGLINE_LEN	300
	#define MAX_ECASHLOGLINE_PARMS	15
	char data_line[MAX_ECASHLOGLINE_LEN];
	char *argv[MAX_ECASHLOGLINE_PARMS];
	
	while (!feof(in)) {
		zstruct(data_line);
		fgets(data_line, MAX_ECASHLOGLINE_LEN-1, in);
		int count = ECash_GetArgs(MAX_ECASHLOGLINE_PARMS, argv, data_line, ',');
		if (count == 13) {	// potential line
			if (!strcmp(argv[3], "PurchReq")) {
				struct CCTransaction cct;
				zstruct(cct);
				cct.card_type = argv[5][0]=='4' ? CCTYPE_VISA : CCTYPE_MASTERCARD;
				cct.transaction_type = CCTRANSACTION_PURCHASE;
			  #if 1	// 2022 kriskoin
				strnncpy(cct.amount, "1", CCFIELD_SHORT);
			  #else
				strnncpy(cct.amount, argv[4], CCFIELD_SHORT);
			  #endif
				strnncpy(cct.user_id, player_rec.user_id, CCFIELD_LONG);
				strnncpy(cct.card_name, player_rec.user_id, CCFIELD_LONG);
				cct.flags = CCTF_NO_NOTICES;	// no pop-ups or emails
				cct.player_id = player_id;
				cct.queue_time = time(NULL);
				cct.ip_address = IP_ConvertHostNameToIP("192.168.1.2");
				cct.ip_address2 = cct.ip_address;
				strnncpy(cct.card_number, argv[5], CCFIELD_LONG);
				strnncpy(cct.card_exp_month, argv[7], CCFIELD_SHORT);
				strnncpy(cct.card_exp_year, argv[8]+2, CCFIELD_SHORT);
				AddEcashRequestToQueue(&cct);
			}
		}
	}
	FCLOSE(in);
	in = NULL;
}
#endif

