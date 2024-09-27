
//	ecash2.cpp
//
//	Utililty ecash functions as well as CC database functions.
//
//	Nothing complicated should be put into this file; it's
//	intended for stuff you hardly ever need to look at.
//
//*********************************************************

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

#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>    
  extern SSL_CTX *MainSSL_Client_CTX;
#endif

#define MAX_CCTYPE_LEN	10

static Array CCDB;
static PPCRITICAL_SECTION CCDBCritSec;

// multithreaded transaction number verification
static WORD32 TNVPending[MAX_PENDING_TRANSACTION_VERIFICATIONS];
static WORD32 TNVPotentialLowestTransactionNumber[MAX_ECASH_THREADS];
PPCRITICAL_SECTION tnv_cs;
int tnv_critsec_is_initialized = FALSE;

int iEcashPostTime = 0;			// average of how long transactions are taking (in seconds)
double fEcashPostTime = 0.0;		// (as above, but as a float)


static void _cdecl ECash_SubmitPaypalRunThread(void *args);
/**********************************************************************************
 Function Player::AddChipsToAccount
 Purpose: add chips to a player's account
 Note:    e-cash, free play chips, etc
		  if peel_credit is true, we will grab a percentage and credit it to pending credit
		  two return values... total cash and total credit deposited
 **********************************************************************************/
ErrorType AddChipsToAccount(WORD32 player_id, int chips_to_add, ChipType chip_type, char *reason, 
							int peel_credit, int *cash_deposited, int *credit_deposited)
{
        ErrorType err = ERR_NONE;
	int _cash_deposited = chips_to_add;
	int _credit_deposited = 0;
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {

		PL->LogFinancialTransaction(LOGTYPE_TRANS_ECASH_TO_PP, player_id, 0, _cash_deposited, 
			chip_type, reason, NULL);

		if (chip_type == CT_REAL) {	// done differently for play money
			EnterCriticalSection(&SDB->SDBCritSec);	//kriskoin: 			if (!strcmp(reason, "FirePay")) {
				SDB->AddOrRemoveChipsInEcashAccount(-_cash_deposited);	// subtract when buying chips
				// add the chips to the player's account(s)
				SDB->AddToChipsInBankForPlayerID(player_id, _cash_deposited, chip_type);			
			} else {
				if (peel_credit) {
					_credit_deposited = (int)((double)CCFeeRate * (double)_cash_deposited + .5);
					_cash_deposited -= _credit_deposited;
				}
				// get chips from ecash accounts
				SDB->AddOrRemoveChipsInEcashAccount(-_cash_deposited);	// subtract when buying chips
				SDB->AddOrRemoveChipsInEcashFeeAccount(-_credit_deposited);	// subtract when buying chips
				// add the chips to the player's account(s)
				SDB->AddToChipsInBankForPlayerID(player_id, _cash_deposited, chip_type);
				SDB->AddToEcashPendingCredit(player_id, _credit_deposited);
			};//if (!strcmp(reason, "FirePay")) {

			LeaveCriticalSection(&SDB->SDBCritSec);

		} else {	// play money
			EnterCriticalSection(&SDB->SDBCritSec);	//kriskoin: 			SDB->AddChipsToUniverse(_cash_deposited, chip_type, reason);
			SDB->AddToChipsInBankForPlayerID(player_id, _cash_deposited, chip_type);
			LeaveCriticalSection(&SDB->SDBCritSec);
		};//if (chip_type == CT_REAL) {	// done differently for play money
		
	} else {
		Error(ERR_ERROR, "%s(%d) Couldn't find player id ($%08lx) trying to add %d chip_type[%d] chips", 
			_FL, player_id, chips_to_add, chip_type);
		err = ERR_ERROR;
	};//if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0)
	if (cash_deposited) {
		*cash_deposited = _cash_deposited;
	}
	if (credit_deposited) {
		*credit_deposited = _credit_deposited;
	}
	return err;
 
}

/**********************************************************************************
 Function AddToEcashLog
 Date: 2017/7/7 kriskoin Purpose: log all ecash activity into one log file
***********************************************************************************/
void AddToEcashLog(WORD32 player_id, const char *szT, int amt, const char *szCCnum,
	const char *szCCname, const char *szCCmon, const char *szCCyr,
	int ecash_id, const char *txnum, int err, int sub_err)
{
	time_t tt = time(NULL);
	struct tm tm;
	struct tm *t = localtime(&tt, &tm);
	if (!t) {
		Error(ERR_ERROR,"%s(%d) Got a null pointer from localtime() -- can't log", _FL);
		return;
	}
	char str[200];	
	sprintf(str, "%04d%02d%02d,%02d:%02d:%02d,%08lx,%s,%06d,%s,%s,%s,%s,%d,%s,%d,%d\n",
			t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec,
			player_id, szT, amt, szCCnum, szCCname, szCCmon, szCCyr, ecash_id, txnum, err, sub_err);
	AddToLog("Data/Logs/ecash.log", "Date,Time,PlayerID,Transaction,Amount,CCNumber,CCName,CCMon,CCYr,EcashID,PPTxnNumber,ErrCode,SubErrCode\n", str);
}


/**********************************************************************************
 *BuildPurchaseRequestPostString
 Date: 2017/7/7 kriskoin Purpose: build the request string used to post the request
                  see page 6/35 of @Commerce Payment Service (protocol V1.01)
                                                   Merchant Integration Guide (1.02 DRAFT)
 NOTE:  We return all sorts of fragments for this, because we're not sure yet how much
 of this needs to be built manually under Linux.  Under Win32, only the action_string gets
 used; the rest is done internally by the Win32 Internet API calls
 // 24/01/01 kriskoin:
************* Date: HK199/11/04*********************************************************************/
void BuildPurchaseRequestPostString(CCTransaction *cct, char *action_string, char *transaction_num, char *full_name, 
                 char *clean_card, char *email, char *streetAddr, char *phone, char *city, char *province, char *zip, char *country)
{
        // get the right n-letter credit card code (see SFC docs)
        char cc_str[MAX_CCTYPE_LEN];
        GetCCCode(cct->card_type, cc_str);
        char szMonth[5];
        char szYear[5];
        sprintf(szMonth,"%02d", atoi(cct->card_exp_month));
        sprintf(szYear, "%02d", (atoi(cct->card_exp_year)) % 100);

        sprintf(action_string,
                "account=%s&"
                "amount=%s&"
                //"operation=P&"
                "cardNumber=%s&"
                "cardExp=%s%%2f%s&"
                //"cvdIndicator=4&"
                //"cvdValue=0000&"
                "cardType=%s&"
                "operation=P&"
                "clientVersion=%s&"
                "custName1=%s&"
                "streetAddr=%s&"
                "phone=%s&"
                //"clientVersion=%s&"
                "email=%s&"
                "city=%s&"
                "province=%s&"
                "zip=%s&"
                "country=%s&"
                "merchantTxn=%s&"
                "merchantId=%s&"
                "merchantPwd=%s",
                /*******************************/
                ECASH_ACCOUNT_ID,
                cct->amount,
                (clean_card ? clean_card : cct->card_number),
                szMonth,
                szYear,
                cc_str, //card type
                ECashProtocolVersionStr,
                full_name,
                /*-------------*/
                streetAddr,  //"northshor",  //streetAddr,
                "6045555555", //phone,
                email,
                city,   //"vancouver", //city,
                province, //"BC", //province,
                zip, //"11222",//zip,
        country, //"CA", //country,
        transaction_num,
                ECASH_MERCHANT_ID,
                ECASH_MERCHANT_PWD

                );

}
/********************************************************************************
 Function BuildCreditRequestPostString
 Date: 2017/7/7 kriskoin Purpose: build a string necessary for a credit request
***********************************************************************************/
void BuildCreditRequestPostString(char *action_string, int amount, char *new_transaction_num, int orig_ecash_txn)
{
	// the only thing we're using from the cct is the amount
	sprintf(action_string,
		"account=%s&"
		"txnN Date: HK199/11/04umber=%d&"
		"amount=%d&"
		"operation=CR&"
		"clientVersion=%s&"
		"merchantTxn=%s&"
		//"txnNumber=%d&"
		"merchantId=%s&"
		"merchantPwd=%s&",
		//"clientVersion=%s",
		ECASH_ACCOUNT_ID,
		orig_ecash_txn,
		amount,
		ECashProtocolVersionStr,
		new_transaction_num,
		//orig_ecash_txn,
		ECASH_MERCHANT_ID,
		ECASH_MERCHANT_PWD
		/*ECashProtocolVersionStr*/);
}

/**********************************************************************************
 Function CCToHex
 Date: 2017/7/7 kriskoin Purpose: given a credit card string, return a 32-bit hex value of first and last 4 digits
 Note: also print to out a "hidden" version (1234XXXXXXXX4321)
***********************************************************************************/
WORD32 CCToHex(char *cc, char *out, char *clean_card_out, char *clean_card_spaces)
{
	if (!cc) return 0;
	char test_card[CCFIELD_LONG];	
	char spaces_card[CCFIELD_LONG+10];	// +10 just in case for spaces
	char first4[5];
	char last4[5];

	zstruct(test_card);
	zstruct(spaces_card);
	char *p = cc;
	char *d = test_card;
	char *s = spaces_card;
	// keep digits, but let's space them out
	int space_count = 0;
	while (*p) {
		if (isdigit(*p)) {
			*d++ = *p;
			*s++ = *p;
			space_count++;
			if (space_count == 4) { // add a space
				space_count = 0;
				*s++ = ' ';
			}
		}
		p++;
	}
	// string is now "clean" of all but digits
	if (clean_card_out) {
		strcpy(clean_card_out, test_card);
	}
	// string is now "clean" of all but digits and our spaces
	if (clean_card_spaces) {
		strcpy(clean_card_spaces, spaces_card);
	}
	strnncpy(first4, test_card, 5);
	zstruct(last4);
	if (strlen(test_card) >= 4) {
		strnncpy(last4, test_card+strlen(test_card)-4, 5);
	}
	if (out) {
		sprintf(out,"%s xxxx xxxx %s", first4, last4);
	}
	int first4_bcd = 0;
	int last4_bcd = 0;
	sscanf(first4, "%x", &first4_bcd);
	sscanf(last4, "%x", &last4_bcd);
	WORD32 result = (first4_bcd<<16) | last4_bcd;
	//kp(("%s(%d) Converted CC '%s' to %04X xxxx xxxx %04x\n", _FL, cc, result>>16, result & 0x00FFFF));
	return result;
}

//*********************************************************
// https://github.com/kriskoin//
// Disable cashier functions for a particular player.
// (set the SDBRECORD_FLAG_NO_CASHIER bit)
//
void DisableCashierForPlayer(WORD32 player_id)
{
	SDBRecord player_rec;
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		if (!(player_rec.flags & SDBRECORD_FLAG_NO_CASHIER)) {
			player_rec.flags |= SDBRECORD_FLAG_NO_CASHIER;
			SDB->WriteRecord(&player_rec);
		}
	}
}

//*********************************************************
// 2002/02/21 - kriskoin
//
// Disable cashout functions for a particular player.
// (set the SDBRECORD_FLAG_NO_CASHOUT bit)
//
void DisableCashoutForPlayer(WORD32 player_id)
{
        SDBRecord player_rec;
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
                if (!(player_rec.flags & SDBRECORD_FLAG_NO_CASHOUT)) {
                        player_rec.flags |= SDBRECORD_FLAG_NO_CASHOUT;
                        SDB->WriteRecord(&player_rec);
                }
        }
}

//*********************************************************
// 2002/02/21 - kriskoin
//
// Enable cashout functions for a particular player.
// (unset the SDBRECORD_FLAG_NO_CASHOUT bit)
//
void EnableCashoutForPlayer(WORD32 player_id)
{
        SDBRecord player_rec;
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
                if ((player_rec.flags & SDBRECORD_FLAG_NO_CASHOUT)) {
                        player_rec.flags &= ~SDBRECORD_FLAG_NO_CASHOUT;
                        SDB->WriteRecord(&player_rec);
                }
        }
}
// kriskoin 


//*********************************************************
// 2002/03/27 - kriskoin
//
// Disable Initial Deposit Bonus functions for a particular player.
// when he have already got the initial bonus
// (set the SDBRECORD_FLAG_NO_INI_BONUS bit)
//
void DisableIniBonusForPlayer(WORD32 player_id)
{
        SDBRecord player_rec;
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
                if (!(player_rec.flags & SDBRECORD_FLAG_NO_INI_BONUS)) {
                        player_rec.flags |= SDBRECORD_FLAG_NO_INI_BONUS;
                        SDB->WriteRecord(&player_rec);
                }
        }
}

//*********************************************************
// 2002/03/27 - kriskoin
//
// Determine the Initial Deposit Bonus Flag for a particular player.
// (unset the SDBRECORD_FLAG_NO_CASHOUT bit)
//
int GetIniBonusStatusForPlayer(WORD32 player_id)
{
        SDBRecord player_rec;
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
                 return(player_rec.flags & SDBRECORD_FLAG_NO_INI_BONUS);
        }
	return -1; //player_id not exist
}
// kriskoin 

//*********************************************************
// 2002/04/09 - kriskoin
//
// Check the Real_Player Flag for a particular player.
// This is useful for marketing and related to vendor_code
//
void CheckRealPlayerForPlayer(WORD32 player_id)
{
        SDBRecord player_rec;
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
                if (!(player_rec.flags & SDBRECORD_FLAG_REAL_PLAYER)) {
                        player_rec.flags |= SDBRECORD_FLAG_REAL_PLAYER;
                        SDB->WriteRecord(&player_rec);
                }
        }
}
// kriskoin 

/**********************************************************************************
 Function EcashNotifyPlayer
 Date: 2017/7/7 kriskoin Purpose: send different notifications to a player if needed
***********************************************************************************/
ErrorType EcashNotifyPlayer(WORD32 player_id, char *msg, int chips_changed)
{
	ErrorType err = ERR_NONE;
	EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	Player *player = CardRoomPtr->FindPlayer(player_id);
	if (player) {	// player object is resident in memory
		if (chips_changed) {	// notify player of chips balance change
			player->ChipBalancesHaveChanged();
		}
		if (msg && strlen(msg)) {	// text to send to client
			player->SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		}
	}
	LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	return err;
}

/**********************************************************************************
 Function EcashSendClientInfo(WORD32 player_id)
 Date: 2017/7/7 kriskoin Purpose: after we log a transaction, we'll want to update the client if possible
***********************************************************************************/
ErrorType EcashSendClientInfo(WORD32 player_id)
{
	ErrorType err = ERR_NONE;
	EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	Player *player = CardRoomPtr->FindPlayer(player_id);
	if (player) {	// player object is resident in memory
		player->send_client_info = TRUE;
	}
	LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	return err;
}

/**********************************************************************************
 Function *GetCCCode(CCType cctype, char *out)
 Date: 2017/7/7 kriskoin Purpose: return the proper SFC encoding string for this credit card type
 Note:	  returns pointer to buffer passed to it
***********************************************************************************/
char *GetCCCode(CCType cctype, char *out)
{
	 // CCTYPE_UNKNOWN, CCTYPE_VISA, CCTYPE_MASTERCARD, CCTYPE_AMEX, CCTYPE_FIREPAY
	switch (cctype) {
	case CCTYPE_VISA:
		strnncpy(out,"VI", MAX_CCTYPE_LEN);
		break;
	case CCTYPE_MASTERCARD:
		strnncpy(out,"MC", MAX_CCTYPE_LEN);
		break;
	// AMEX is not used as we don't have our own Amex merchant account
	case CCTYPE_AMEX:
		strnncpy(out,"AM", MAX_CCTYPE_LEN);
		break;
	// 24/01/01 kriskoin:
	case CCTYPE_FIREPAY:
		/// !!! We may want to switch this from FP to NEG-FP after beta testing is over
		strnncpy(out,"FP", MAX_CCTYPE_LEN);
		break;
	default:
		strnncpy(out,"??", MAX_CCTYPE_LEN);
		break;
	}
	return out;
}

/**********************************************************************************
 Function *FixEcashReplyString(char *str)
 Date: 2017/7/7 kriskoin Purpose: fix the ecash return strings -- filter and translate + and %hex strings
 Note: returns a pointer to an internal static
***********************************************************************************/
char *ECash_FixReplyString(char *input_string, char *output_string, int max_output_string_len)
{
	char *out = output_string;
	char *p = input_string;
	int index = 0;
	while (*p) {
		if (*p == '+') {
			*out++ = ' ';			
		} else if (*p == '%') {	// start of hex string
			if (*(p+1) && *(p+2)) {	// and there is valid data after it
			  #if 1	//kriskoin: 				*out++ = (char)(((*(p+1)-'0')<<4) | (*(p+2)-'0'));
			  #else
				char tmp[5];
				sprintf(tmp,"0x%c%c",*(p+1), *(p+2));
				*out++ = (char)(strtol(tmp,0,16));
			  #endif
				p += 2;	// skip all this
			} else {
				// broken string, just ignore
			}
		} else { // normal character
			*out++ = *p;
		}
		p++;
		index++;
		if (index >= max_output_string_len - 1) {
			break;	// too long.  ignore the rest.
		}
	}
	*out = 0;	// make sure it's terminated.
	return output_string;
}

/**********************************************************************************
 Function GetArgs(int maxArgc, char *argv[], char *string, char seperator)
 date: kriskoin 2019/01/01 Purpose: internal parser (used for reading ecash.log files)
***********************************************************************************/
int ECash_GetArgs(int maxArgc, char *argv[], char *string, char seperator)
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
 Function GetNextTransactionNumber
 Date: 2017/7/7 kriskoin Purpose: for a given player, build his next transaction ID
 Return:  prints to *out
***********************************************************************************/
ErrorType GetNextTransactionNumber(WORD32 player_id, char *out, int *output_transaction_number)
{
	*out = 0;	// clear if there's an error
	*output_transaction_number = 0;
	int next_number = SDB->GetNextTransactionNumberAndIncrement(player_id);
	if (next_number < 0) {	// error -- msg already printed
		return ERR_ERROR;
	}
	// got a valid next number
	// we want 19991104-000003ef-001
	time_t tt = time(NULL);
	struct tm *t = localtime(&tt);
	if (!t) {
		Error(ERR_ERROR,"%s(%d) Got a null pointer from localtime()", _FL);
		return ERR_ERROR;
	}
	sprintf(out, "%04d%02d%02d-%08lx-%03d",
			t->tm_year+1900, t->tm_mon+1, t->tm_mday, player_id, next_number);
	*output_transaction_number = next_number;
	return ERR_NONE;
}
			

/**********************************************************************************
 Function GetNextTransactionNumber
 Date: 2017/7/7 kriskoin Purpose: for a given player, build his next transaction ID
 Return:  prints to *out
***********************************************************************************/
ErrorType GetNextTransactionNumberNotIncrement(WORD32 player_id, char *out, int *output_transaction_number)
{
        *out = 0;       // clear if there's an error
        *output_transaction_number = 0;
        int next_number = SDB->GetNextTransactionNumber(player_id);
        if (next_number < 0) {  // error -- msg already printed
                return ERR_ERROR;
        }
        // got a valid next number
        // we want 19991104-000003ef-001
        time_t tt = time(NULL);
        struct tm *t = localtime(&tt);
        if (!t) {
                Error(ERR_ERROR,"%s(%d) Got a null pointer from localtime()", _FL);
                return ERR_ERROR;
        }
        sprintf(out, "%04d%02d%02d-%08lx-%03d",
                        t->tm_year+1900, t->tm_mon+1, t->tm_mday, player_id, next_number);
        *output_transaction_number = next_number;
        return ERR_NONE;
}





/**********************************************************************************
 Function MakeBCDFromCC
 date: 24/01/01 kriskoin Purpose: given a credit card string, fill a return 8-byte BCD representation
 Note: Be sure output is at least 8 bytes!
***********************************************************************************/
ErrorType MakeBCDFromCC(char *credit_card, void *output)
{
	#define MAX_CARD_LEN	17
	char sz_card[MAX_CARD_LEN+1];
	zstruct(sz_card);
	char *cc_in = credit_card;
	char *cc_out = sz_card;
	// we want no spaces
	while (*cc_in) {
		if (isdigit(*cc_in)) {
			*cc_out = *cc_in;
			cc_out++;
			if ((cc_out-sz_card) == MAX_CARD_LEN) {	// bad card given to us
				return ERR_ERROR;
			}
		}
		cc_in++;
	}
	// catch bad cards -- like the original week's cards
	if (strlen(sz_card) < 14) {
		return ERR_ERROR;
	}
	// spaces are filtered, create the BCD representation
	char bcd[8];
	zstruct(bcd);
	for (int i=0; i < 8; i++) {
		bcd[i] = (BYTE8)((sz_card[i*2]&0x0F)<<4 | (sz_card[i*2+1]&0x0f));
	}
	// Now switch the byte ordering so we can print them as hex numbers
	*(long *)(bcd+0) = ntohl(*(long *)(bcd+0));
	*(long *)(bcd+4) = ntohl(*(long *)(bcd+4));

	pr(("%s(%d) Original cc string = '%s', converted to hex: $%08lx %08lx\n",
				_FL, sz_card, *(WORD32 *)(bcd+0), *(WORD32 *)(bcd+4)));
	memcpy(output, &bcd, 8);
	return ERR_NONE;
}

/**********************************************************************************
 Function NetPurchasedInLastNHours(WORD32 player_id, int hours)
 Date: 2017/7/7 kriskoin Purpose: tell us how much this player has purchased in the last n hours
***********************************************************************************/
int NetPurchasedInLastNHours(WORD32 player_id, int hours)
{
	return NetPurchasedInLastNHours(player_id, hours, NULL, 0);
}

// 24/01/01 kriskoin:
int NetPurchasedInLastNHours(WORD32 player_id, int hours, int *total_purchases)
{
	return NetPurchasedInLastNHours(player_id, hours, total_purchases, 0);
}

// 24/01/01 kriskoin:
int NetPurchasedInLastNHours(WORD32 player_id, int hours, int *total_purchases, time_t time_in)
{
	time_t now = ( time_in ? time_in : time(NULL) );
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	int total_charged = 0;
	int _total_purchases = 0;
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		int valid_tr_index = -1, i;
		// first step -- just find the last relevant transaction
		for (i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
			ClientTransaction *ct = &player_rec.transaction[i];
			if (!ct->transaction_type) {
				continue;
			}
			if (difftime(now, ct->timestamp) < (int)(hours*3600)) {	// fits the time range
				valid_tr_index = i;	// set so we can count from here
			}
		}
		// now we do it for real, going backwards from this point towards the present
		for (i=valid_tr_index; i >= 0 ; i--) {
			ClientTransaction *ct = &player_rec.transaction[i];
			if (!ct->transaction_type) {
				continue;
			}
			if ((ct->transaction_type == CTT_PURCHASE)||
			    (ct->transaction_type == CTT_FIREPAY_PURCHASE)||
			    (ct->transaction_type == CTT_CC_PURCHASE)) { // count it (+)
				total_charged += ct->transaction_amount;
				_total_purchases += ct->transaction_amount;
			}
			
			if (ct->transaction_type == CTT_CREDIT) {	// count it (-)
				total_charged -= ct->transaction_amount;
			}
		  #if 0	// 20000228HK -- removed as it gets us no benefit, but could cause the
				// limit to be wrong if a check is improperly refunded
			if (ct->transaction_type == CTT_CHECK_REFUND) { // count it (+)
				total_charged += ct->transaction_amount;
			}
			if (ct->transaction_type == CTT_CHECK_ISSUED) { // count it (-)
				total_charged -= ct->transaction_amount;
			}
		  #endif
			// don't ever go negative!
			total_charged = max(total_charged, 0);
		}
	} else {
		Error(ERR_ERROR, "%s(%d) Couldn't find player id ($%08lx) trying to add purchases", 
			_FL, player_id);
	}
	if (total_purchases) {
		*total_purchases = _total_purchases;
	}
	return max(0,total_charged);
}

/**********************************************************************************
 Function ParseEcashReply(EcashReplyStruct *ers, char *result_str)
 Date: 2017/7/7 kriskoin Purpose: parse the reply string and fill the return structure pointers
***********************************************************************************/
ErrorType ParseEcashReply(EcashReplyStruct *ers, char *result_str)
{
  #if 1	// 2022 kriskoin
	kp(("%s(%d) --- Message we got back from server: (%d bytes) ---\n", _FL, strlen(result_str)));
	kwrites(result_str);
	kp(("\n"));
	kp(("%s(%d) --- end of what we got back --- (we supplied an extra newline)\n", _FL));
  #endif

	#define MAX_REPLY_ARGS	50
	char *argv[MAX_REPLY_ARGS];
	zstruct(argv);
	int count = ParseReplyArgs(MAX_REPLY_ARGS, argv, result_str, '&');
	if (count > MAX_REPLY_ARGS || count < 2) {
		Error(ERR_NOTE, "%s(%d) ParseReplyArgs returned %d (skipping) -- MAX_REPLY_ARGS = %d", _FL, count, MAX_REPLY_ARGS);
		kp(("%s(%d) *** ECash Parsing error *** Here is the parsed string we received from the SFC server:\n", _FL));
		for (int i=0; i < count; i++) {
			kp(("        #%2d: ", i+1));
			kwrites(argv[i]);
		}
		kp(("%s(%d) --- end of response string from transaction server ---\n", _FL));
		return ERR_ERROR;
	}
	#define MAX_RESULT_FIELD_NAME_LEN	200	// only really need 20
	char field[MAX_RESULT_FIELD_NAME_LEN];
	for (int i=0; i < count; i++) {
		//kp(("%s(%d) Parsed - %02d: %s\n", _FL, i, argv[i]));
		// we want everything after the '='
		char *eq = strstr(argv[i], "=");
		if (eq) {	// found the equal sign
			// first of all, is there anything past that string?
			eq++;
			if (*eq) { // yes, it's not null, we want this string
				zstruct(field);
				// grab whatever is to the left of the '=' sign
				strnncpy(field, argv[i], min( (eq-argv[i]),MAX_RESULT_FIELD_NAME_LEN) );
				//kp(("%s(%d) [%d] goes to (%s) and (%s)\n", _FL, (eq-argv[i]), field, eq));
				if (!stricmp(field, "status")) {
					ers->status = eq;
				} else if (!stricmp(field, "authCode")) {
					ers->authCode = eq;
				} else if (!stricmp(field, "authTime")) {
					ers->authTime = eq;
				} else if (!stricmp(field, "avsInfo")) {
					ers->avsInfo = eq;
				} else if (!stricmp(field, "creditNumber")) {
					ers->creditNumber = eq;
				} else if (!stricmp(field, "curAmount")) {
					ers->curAmount = eq;
				} else if (!stricmp(field, "amount")) {
					ers->amount = eq;
				} else if (!stricmp(field, "txnNumber")) {
					ers->txnNumber = eq;
				} else if (!stricmp(field, "serviceVersion")) {
					ers->serviceVersion = eq;
				} else if (!stricmp(field, "txnType")) {
					ers->txnType = eq;
				} else if (!stricmp(field, "errCode")) {
					ers->errCode = eq;
				} else if (!stricmp(field, "errString")) {
					ers->errString = eq;
				} else if (!stricmp(field, "subError")) {
					ers->subError = eq;
				} else if (!stricmp(field, "subErrorString")) {
					ers->subErrorString = eq;
				} else if (!stricmp(field, "pmtError")) {
					ers->pmtError = eq;
				} else if (!stricmp(field, "actionCode")) {
					ers->actionCode = eq;
				} else if (!stricmp(field, "merchantTxn")) {
					ers->merchantTxn = eq;
				} else if (!stricmp(field, "payProcResp")) {
					kp1(("%s(%d) payProcResp = '%s' (not yet handled by our code)\n", _FL, eq));
				} else {
					kp1(("%s(%d) Unhandled return string parameters (%s)\n", _FL, argv[i]));
				}
			} else {
				pr(("%s(%d) Ignoring argv[%d] (%s)\n", _FL, i, argv[i]));
			}
		} else {
			kp(("%s(%d) Got a result string with no = sign (%s)\n", _FL, argv[i]));
		}
	}
	return ERR_NONE;

}

/**********************************************************************************
 Function ParseReplyArgs(int maxArgc, char *argv[], char *string, char seperator)
 Date: 2017/7/7 kriskoin Purpose: parser for seperating out url response fields
***********************************************************************************/
int ParseReplyArgs(int maxArgc, char *argv[], char *string, char seperator)
{
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
 Function *PostTransactionToECash
 Date: 2017/7/7 kriskoin Purpose: do all handling of the actual posting of the transaction, wait and return
   the result;
   Note that this is a blocking function and might take several minutes to return.
***********************************************************************************/
ErrorType PostTransactionToECash(char *transaction_str, char *result, int max_result_len, char *url)
{
	ErrorType err = ERR_NONE;	// default to success
	int start_time = SecondCounter;
	// First, build the whole header and message for posting...
	char post_msg[1500];
	zstruct(post_msg);
	sprintf(post_msg,	"POST %s HTTP/1.0\n"
						ECASH_POSTING_TYPE"\n"
						"Content-length: %d\n\n"
						"%s",
						url,
						strlen(transaction_str), 
						transaction_str);

  #if 1	// 2022 kriskoin
	kp(("%s(%d) --- Message we're going to post to the server: (%d bytes) ---\n", _FL, strlen(post_msg)));
	kwrites(post_msg);
	kp(("\n"));
	kp(("%s(%d) --- end of what we're posting to server --- (we supplied an extra newline)\n", _FL));
  #endif

	// Post it and wait for a result.  This usually only takes several
	// seconds, but it could take a long time (more than a minute).
	int http_result = 0;
  #if INCL_SSL_SUPPORT
	#ifdef HORATIO
	  kp(("%s(%d) Ecash POST: %s\n", _FL, post_msg));
	#endif

         kp(("%s(%d) Ecash SERVER:: %s\n", _FL, ECASH_SERVER));
	 kp(("%s(%d) Ecash POST: %s\n", _FL, post_msg));

	err = GetHttpPostResult("https://" ECASH_SERVER, post_msg, result,
				max_result_len, NULL, 0,
              #if INCL_SSL_SUPPORT
        	    MainSSL_Client_CTX,
              #endif
				&http_result
	        );
  #else
	kp(("%s(%d) *** SSL support not included.  https connections not supported.\n", _FL));
	http_result = 404;
  #endif
	#ifdef HORATIO
	  kp(("%s(%d) Ecash RESULT: %s\n", _FL, result));
	#endif
	 kp(("%s(%d) Ecash RESULT: %s\n", _FL, result));

	int post_time = SecondCounter - start_time;

	//kriskoin: 	// 200 is normal (OK)
	// 204 is returned by OUR code to indicate 'no response' if we wrote something
	//     but did not get an answer back from the server.
	// 500 is returned by OUR code to indicate nothing even got sent to server
	// SFC tells us that 404 means the transaction definitely did NOT go through.
	//kp(("%s(%d) http_result = %d\n",_FL,http_result));
	int include_post_time = TRUE;
	if (err && http_result==404) {
		kp(("%s(%d) Got result 404 from an http post... assuming transaction did NOT go through.\n",_FL));
		err = ERR_ERROR;	// indicate it definitely did NOT go through.
		if (post_time < 15) {	// a really quick 404 doesn't mean anything useful.
			// don't count it in the average response time.
			include_post_time = FALSE;
		}
	}

	// keep a moving average of how long transactions are taking
	if (include_post_time) {
		if (!iEcashPostTime) {
			// first one - no weighting
			iEcashPostTime = post_time;
			fEcashPostTime = (double)post_time;
		} else {
			#define OLD_ECASH_TIME_WEIGHTING	((float).82)
			fEcashPostTime = (double)iEcashPostTime * OLD_ECASH_TIME_WEIGHTING +
							 (double)post_time * (1.0-OLD_ECASH_TIME_WEIGHTING);
			iEcashPostTime = (int)(fEcashPostTime + .9);
		}
	}

	if (DebugFilterLevel <= 9 || fEcashPostTime >= 100.0) {
		kp(("%s %s(%d) eCash POST took %3d seconds. result = %d/%d.  Avg = %5.1fs\n",
				TimeStr(), _FL, post_time, err, http_result, fEcashPostTime));
	}
        kp(("%s %s(%d) eCash POST took %3d seconds. result = %d/%d.  Avg = %5.1fs\n",
                                TimeStr(), _FL, post_time, err, http_result, fEcashPostTime));

	return err;
}

//*********************************************************
// https://github.com/kriskoin//
// Background thread to scan through all the players in the database
// and return the first bunch that have pending checks requested.
// Fills in an AdminCheckRun structure and passes it to a player_id
// to be sent to the player that request it.
//
static void _cdecl ECash_BeginCheckRunThread(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Scan4Checks");	// register this thread for stack dumps if we crash

	WORD32 player_id = (WORD32)args;
	kp(("%s %s(%d) Beginning check scan (requested by player id $%08lx)...\n", TimeStr(), _FL, player_id));

	struct AdminCheckRun acr;
	zstruct(acr);
	struct SDBRecord sdbr;
	zstruct(sdbr);
	char description[500];
	zstruct(description);
	struct AdminCheckRunEntry *e = acr.entries;
	WORD32 starting_ticks = GetTickCount();
	WORD32 last_release_ticks = starting_ticks;
	kp(("v now begin check run"));
	for (int i=0 ; i<SDB->iRecordCount ; i++) {
                
		if (SDB->SearchDataBaseByIndex(i, &sdbr)==ERR_NONE) {
			// Found a player... anything in their pending check field?
		if (sdbr.pending_check)  {
				kp(("found one pending check"));
				// Found one... add it to the check run.
				// sprintf(description, "%s, %s", sdbr.user_id, sdbr.full_name);
				e->player_id = sdbr.player_id;
				sprintf(description, "check:%s, %s", sdbr.user_id, sdbr.full_name);
				e->amount = sdbr.pending_check;
					
				strnncpy(e->description, description, ADMIN_CHECK_RUN_ENTRY_DESC_LEN);
				kp(("finish copy"));
				acr.player_count++;
                                kp(("finish ++"));
				e++;
				//kp(("%s %s(%d) add to e amount %d \n",  _FL, e->amount));
				if (acr.player_count >= MAX_PLAYERS_PER_CHECK_RUN) {
					kp(("%s %s(%d) Check scan stopped at record %d because check run is full.\n", TimeStr(), _FL, i));
					break;	// stop looking... check run is full.
				}
			}

            if (sdbr.pending_paypal) {

				kp(("find one pending paypal%d", sdbr.pending_paypal));
				e->player_id = sdbr.player_id;
				
				sprintf(description, "paypal:%s, %s", sdbr.user_id, sdbr.full_name);
		                kp(("description[59]"));	
				// description[49]= 1;
				e->amount = sdbr.pending_paypal;
				
				strnncpy(e->description, description, ADMIN_CHECK_RUN_ENTRY_DESC_LEN);
				acr.player_count++;
				e++;
				// kp(("%s %s(%d) add to e amount %d \n",  _FL, e->amount));                                  
				if (acr.player_count >= MAX_PLAYERS_PER_CHECK_RUN) {
					kp(("%s %s(%d) Check scan stopped at record %d because check run is full.\n", TimeStr(), _FL, i));
					break;	// stop looking... check run is full.
				}
			
			}
			
		}
	

		//kriskoin: 		// bandwidth (or cpu).  Disk I/O is hard to measure, so just make sure
		// we give up lots of cpu so other threads can get work done.
		#define MAX_CHECKRUN_EXECUTION_MS	(400)	// ** MUST BE LESS THAN 1000 **
		WORD32 elapsed_ms = GetTickCount() - last_release_ticks;
		if (elapsed_ms >= MAX_CHECKRUN_EXECUTION_MS) {
			//kp(("%s(%d) Check run sleeping at index %d for %dms\n", _FL, i, 1000-MAX_CHECKRUN_EXECUTION_MS));
			Sleep(1000-MAX_CHECKRUN_EXECUTION_MS);
			last_release_ticks = GetTickCount();
		}
	}

        kp(("out of the checkrun loop")); 
	// Finally, send it to the player that asked for it.
	int sent_flag = FALSE;
	if (CardRoomPtr) {
		EnterCriticalSection(&CardRoomPtr->CardRoomCritSec);
		Player *p = CardRoomPtr->FindPlayer(player_id);
		if (p) {
			p->SendDataStructure(DATATYPE_ADMIN_CHECK_RUN, &acr, sizeof(acr));
			sent_flag = TRUE;
		}
		LeaveCriticalSection(&CardRoomPtr->CardRoomCritSec);
	}
	if (!sent_flag) {
		Error(ERR_ERROR, "%s(%d) error: could not send check run because player object for id $%08lx not found!", _FL, player_id);
	}
  #if DEBUG
	WORD32 elapsed = GetTickCount() - starting_ticks;
	kp(("%s %s(%d) Check scan complete. Thread is exiting. Elapsed = %.1fs\n", TimeStr(), _FL, (double)elapsed/1000.0));
  #endif
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
  kp(("check run complete."));
}

//*********************************************************
// https://github.com/kriskoin//
// Begin a check run by scanning through all the players in the database
// and returning the first bunch that have pending checks requested.
// Fills in an AdminCheckRun structure and sends it to dest_player_id.
//
void ECash_BeginCheckRun(WORD32 dest_player_id)
{
	int result = _beginthread(ECash_BeginCheckRunThread, 0, (void *)dest_player_id);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
	}
}


//*********************************************************
// https://github.com/kriskoin//
// Background thread to process a submitted check run.
// Issue checks for each player in the submitted list.
//
static void _cdecl ECash_SubmitCheckRunThread(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Check Run");	// register this thread for stack dumps if we crash
	kp(("%s %s(%d) Beginning check run...\n", TimeStr(), _FL));
        kp(("begin Ecash_submitCheckRunThred\n"));
	char curr_str[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str);
	char summary_fname[MAX_FNAME_LEN];
	zstruct(summary_fname);
	char bank_fname[MAX_FNAME_LEN];
	zstruct(bank_fname);
	char label_fname[MAX_FNAME_LEN];
	zstruct(label_fname);
	char dhl_fname[MAX_FNAME_LEN];
	zstruct(dhl_fname);
	MakeTempFName(summary_fname, "cr");
	MakeTempFName(bank_fname, "bc");
	MakeTempFName(label_fname, "lc");
	MakeTempFName(dhl_fname, "dh");
	FILE *fd = fopen(summary_fname, "wt");
	FILE *bfd = fopen(bank_fname, "wt");
	FILE *lfd = fopen(label_fname, "wt");
	FILE *dfd = fopen(dhl_fname, "wt");



   	/*****-----------------------*****/
        char curr_str1[MAX_CURRENCY_STRING_LEN];
        zstruct(curr_str1);
        char summary_fname1[MAX_FNAME_LEN];
        zstruct(summary_fname1);
        char bank_fname1[MAX_FNAME_LEN];
        zstruct(bank_fname1);
        char label_fname1[MAX_FNAME_LEN];
        zstruct(label_fname1);
        char dhl_fname1[MAX_FNAME_LEN];
        zstruct(dhl_fname1);
        MakeTempFName(summary_fname1, "crp");
        MakeTempFName(bank_fname1, "bcp");
        MakeTempFName(label_fname1, "lcp");
        MakeTempFName(dhl_fname1, "dhp");

        FILE *fd1 = fopen(summary_fname1, "wt");
        FILE *bfd1 = fopen(bank_fname1, "wt");
        FILE *lfd1 = fopen(label_fname1, "wt");
        FILE *dfd1 = fopen(dhl_fname1, "wt");

		

        if ((fd && bfd && lfd && dfd) &&(fd1 && bfd1 && lfd1 && dfd1)) {
                int total_check_count = 0;
                int total_check_amount = 0;
				int total_paypal_count = 0;
				int total_paypal_amount = 0;

                fprintf(fd, "Check batch started %s\n\n", TimeStr());
				fprintf(fd1, "Paypal batch started %s\n\n", TimeStr());
                // label header
                fprintf(lfd, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
                        "FullName", "Addr1", "Addr2", "City", "State", "Country", "ZipCode", "PhoneNumber");
				
				fprintf(lfd1, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
                        "FullName", "Addr1", "Addr2", "City", "State", "Country", "ZipCode", "PhoneNumber");

      		struct AdminCheckRun *acr = (struct AdminCheckRun *)args;

		for (int i=0 ; i<MAX_PLAYERS_PER_CHECK_RUN && !iShutdownAfterECashCompletedFlag ; i++) {
			if (( acr->entries[i].player_id && acr->entries[i].amount) &&
				(acr->entries[i].description[0]=='c')) {
				int amount = acr->entries[i].amount;
				WORD32 player_id = acr->entries[i].player_id;
				SDBRecord sdbr;
				zstruct(sdbr);
				EnterCriticalSection(&SDB->SDBCritSec);
				int index = SDB->SearchDataBaseByPlayerID(player_id, &sdbr);
				if (index < 0) {
					LeaveCriticalSection(&SDB->SDBCritSec);
					Error(ERR_ERROR, "%s(%d) Error: check batch could not find player id $%08lx", _FL, acr->entries[i].player_id);
					continue;
				}

				kp(("check run doing!\n"));
				kp(("acr->entries[i].description[49]= %s\n", _FL, acr->entries[i].description));
  				kp(("acr->entries[i].description[59]= %d\n", _FL, acr->entries[i].description[59]));

				
				
				kp(("transfer begin check acr->entries[i].description[49]= %d", _FL, acr->entries[i].description[49]));
				if (amount > sdbr.pending_check) {
				}
				if (sdbr.pending_check) {
				// The money's there... transfer it to the pending account
				SDB->TransferChips(CT_REAL,	// real money flag
						player_id,			// from player id
						AF_PENDING_CHECK,	// from pending check field
						7,       //SDB->PendingRec_ID,	// to player id
						AF_AVAILABLE_CASH,	// to chips in bank field
						amount,
						_FL);
				PL->LogFinancialTransaction(LOGTYPE_TRANS_PLR_TO_PENDING, player_id, 0, amount, 
						CT_REAL, "Actual Check Issued", NULL);
				}
				
				
				 kp(("transfer begin check acr->entries[i].description[49]= %d", _FL, acr->entries[i].description[49]
));             
				 LeaveCriticalSection(&SDB->SDBCritSec);
				kp(("transfer completed"));
				// 24/01/01 kriskoin:
			  #if 0
				// save this client transaction reference number
				char transaction_string[CL_TRANSACTION_ID_LEN];
				int client_transaction_number = 0;
				GetNextTransactionNumber(player_id, transaction_string, &client_transaction_number);

				// Add it to the player's transaction history
				ClientTransaction ct;
				zstruct(ct);
				ct.transaction_type = CTT_CHECK_ISSUED;
				ct.timestamp = time(NULL);
				ct.transaction_amount = amount;
				ct.ecash_id = client_transaction_number;
				SDB->LogPlayerTransaction(player_id, &ct);
			  #endif	// entry in client history

				// 24/01/01 kriskoin:
			  #if 0
				// Send an email to the player
				char fname[MAX_FNAME_LEN];
				MakeTempFName(fname, "crc");
				FILE *fd2 = fopen(fname, "wt");
				if (fd2) {
					if (!iRunningLiveFlag) {
						fprintf(fd2, "*** THIS IS A SAMPLE ONLY: NOT SENT TO THE REAL CUSTOMER ***\n\n");
					}
					fprintf(fd2, "Desert Poker Check Issued Confirmation -- %s CST\n", TimeStrWithYear());
					fprintf(fd2, "\n");
					fprintf(fd2, "A %s check has been issued for Player ID ' %s '\n", CurrencyString(curr_str, amount, TRUE), sdbr.user_id);
					fprintf(fd2, "\n");
					fprintf(fd2, "Payable to:\n");
					fprintf(fd2, "    %s\n", sdbr.full_name);
					fprintf(fd2, "    %s\n", sdbr.mailing_address1);
					if (sdbr.mailing_address2[0]) {
						fprintf(fd2, "    %s\n", sdbr.mailing_address2);
					}
					fprintf(fd2, "    %s, %s, %s\n", sdbr.city, sdbr.mailing_address_state, sdbr.mailing_address_country);
					fprintf(fd2, "    %s\n", sdbr.mailing_address_postal_code);
					fprintf(fd2, "    %s\n", DecodePhoneNumber(sdbr.phone_number));
					fprintf(fd2, "\n");
					fprintf(fd2, "Check reference number: %s\n", transaction_string);
					fprintf(fd2, "\n");
					fprintf(fd2, "Please allow 10 to 15 business days for delivery of your check.\n");
					fprintf(fd2, "(note: we are currently working to streamline the check delivery\n");
					fprintf(fd2, "process and reduce the time it takes for the check to arrive on\n");
					fprintf(fd2, "your doorstep; please be patient.)\n");
					fprintf(fd2, "\n");
					fprintf(fd2, "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
					fprintf(fd2, "If you have any questions regarding this transaction,\n");
					fprintf(fd2, "please contact us at cashier@kkrekop.io\n");
					fclose(fd2);
					// Now email it to them...
					if (!iRunningLiveFlag) {
						// testing... send to support
						Email(	"support@kkrekop.io",
								"Desert Poker Cashier",
								"cashier@kkrekop.io",
								"Desert Poker Check Issued Confirmation",
								fname,
								NULL,	// bcc:
								TRUE);	// delete when sent
					} else {
						// Running live... send to the player, bcc transactions.
						Email(	sdbr.email_address,
								"Desert Poker Cashier",
								"cashier@kkrekop.io",
								"Desert Poker Check Issued Confirmation",
								fname,
								"transactions@kkrekop.io",	// bcc:
								TRUE);								// delete when sent
					}
				}
			  #endif	// no email to player

				// update detail summary email
				total_check_count++;
				total_check_amount += amount;
				fprintf(fd, "=== Check #%d ==== %s =====================\n", total_check_count, sdbr.user_id);
				fprintf(fd, "Check amount: %s\n\n", CurrencyString(curr_str, amount, CT_REAL));
				fprintf(fd, "Payable to:\n");
				fprintf(fd, "    %s\n", sdbr.full_name);
				fprintf(fd, "    %s\n", sdbr.mailing_address1);
				if (sdbr.mailing_address2[0]) {
					fprintf(fd, "    %s\n", sdbr.mailing_address2);
				}
				fprintf(fd, "    %s, %s, %s\n", sdbr.city, sdbr.mailing_address_state, sdbr.mailing_address_country);
				fprintf(fd, "    %s\n", sdbr.mailing_address_postal_code);
				fprintf(fd, "    %s\n", DecodePhoneNumber(sdbr.phone_number));
				fprintf(fd, "\n");
				// update bank summary email
				fprintf(bfd, "===========================================\n");
				fprintf(bfd, "%d. %-20s %-s\n", total_check_count, sdbr.full_name, CurrencyString(curr_str, amount, CT_REAL));
				// update label template email
				fprintf(lfd, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
					sdbr.full_name,
					sdbr.mailing_address1,
					sdbr.mailing_address2,
					sdbr.city,
					sdbr.mailing_address_state,
					sdbr.mailing_address_country,
					sdbr.mailing_address_postal_code,
					DecodePhoneNumber(sdbr.phone_number));
				// update dhl import email (see spec from DHL CONNECT software)
				// company, contact, ad1, ad2, ad3, city, state, postalcode, country, phone#, ext#, fax#, email, reserved, VAT, acc#
				fprintf(dfd, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n",
					"",					// company
					sdbr.full_name,			// contact
					sdbr.mailing_address1,	// address1
					sdbr.mailing_address2,	// address2
					"",					// address3
					sdbr.city,				// city
					sdbr.mailing_address_state,			// state,
					sdbr.mailing_address_postal_code,	// postalcode
					sdbr.mailing_address_country,		// country,
					DecodePhoneNumber(sdbr.phone_number), // phone number,
					"",	// ext #
					"",	// fax #
					"",	// email
					"",	// reserved
					"",	// VAT/EIN/SSN (not used for USA)
					""		// account #
					);

			}
		
		if ((acr->entries[i].player_id && acr->entries[i].amount) &&
				(acr->entries[i].description[0]=='p')) {
				int amount = acr->entries[i].amount;
				WORD32 player_id = acr->entries[i].player_id;
				SDBRecord sdbr;
				zstruct(sdbr);
				EnterCriticalSection(&SDB->SDBCritSec);
				int index = SDB->SearchDataBaseByPlayerID(player_id, &sdbr);
				if (index < 0) {
					LeaveCriticalSection(&SDB->SDBCritSec);
					Error(ERR_ERROR, "%s(%d) Error: check batch could not find player id $%08lx", _FL, acr->entries[i].player_id);
					continue;
				}

				kp(("paypal run doing!\n"));
				kp(("acr->entries[i].description[49]= %s\n", _FL, acr->entries[i].description));
  				kp(("acr->entries[i].description[59]= %d\n", _FL, acr->entries[i].description[59]));

				
				
				kp(("transfer begin check acr->entries[i].description[49]= %d", _FL, acr->entries[i].description[49]));
				if (amount > sdbr.pending_check) {
				}
				if (sdbr.pending_paypal) {
				// The money's there... transfer it to the pending account
				SDB->TransferChips(CT_REAL,	// real money flag
						player_id,			// from player id
						AF_PENDING_PAYPAL,	// from pending check field
						SDB->EcashRec_ID,       //SDB->PendingRec_ID,	// to player id
						AF_AVAILABLE_CASH,	// to chips in bank field
						amount,
						_FL);
				PL->LogFinancialTransaction(LOGTYPE_TRANS_PLR_TO_PENDING, player_id, 0, amount,
						CT_REAL, "Actual Check Issued", NULL);
				}
				
				
		        kp(("transfer begin check acr->entries[i].description[49]= %d", _FL, acr->entries[i].description[49]));
				LeaveCriticalSection(&SDB->SDBCritSec);
				kp(("transfer completed"));
				// 24/01/01 kriskoin:
			
				// 24/01/01 kriskoin:
			
				// update detail summary email
				total_paypal_count++;
				total_paypal_amount += amount;
				fprintf(fd1, "=== paypal #%d ==== %s =====================\n", total_paypal_count, sdbr.user_id);
				fprintf(fd1, "Paypal amount: %s\n\n", CurrencyString(curr_str, amount, CT_REAL));
				fprintf(fd1, "Payable to:\n");
				fprintf(fd1, "    %s\n", sdbr.full_name);
				fprintf(fd1, "    %s\n", sdbr.email_address);
				fprintf(fd1, "\n");
				// update bank summary email
				fprintf(bfd1, "===========================================\n");
				fprintf(bfd1, "%d. %-20s %-s\n", total_paypal_count, sdbr.email_address, CurrencyString(curr_str, amount, CT_REAL));
				// update label template email
				fprintf(lfd1, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
					sdbr.full_name,
					sdbr.mailing_address1,
					sdbr.mailing_address2,
					sdbr.city,
					sdbr.mailing_address_state,
					sdbr.mailing_address_country,
					sdbr.mailing_address_postal_code,
					DecodePhoneNumber(sdbr.phone_number));
				// update dhl import email (see spec from DHL CONNECT software)
				// company, contact, ad1, ad2, ad3, city, state, postalcode, country, phone#, ext#, fax#, email, reserved, VAT, acc#
				fprintf(dfd1, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n",
					"",					// company
					sdbr.full_name,			// contact
					sdbr.mailing_address1,	// address1
					sdbr.mailing_address2,	// address2
					"",					// address3
					sdbr.city,				// city
					sdbr.mailing_address_state,			// state,
					sdbr.mailing_address_postal_code,	// postalcode
					sdbr.mailing_address_country,		// country,
					DecodePhoneNumber(sdbr.phone_number), // phone number,
					"",	// ext #
					"",	// fax #
					"",	// email
					"",	// reserved
					"",	// VAT/EIN/SSN (not used for USA)
					""		// account #
					);

			}

		}
		if (total_check_count) {
			fprintf(fd, "=================================================\n");
			fprintf(fd, "\n");
			fprintf(fd, "%d checks total to be issued, grand total %s\n",
					total_check_count, CurrencyString(curr_str, total_check_amount, CT_REAL));
			fprintf(bfd, "=================================================\n");
			fprintf(bfd, "\n");
			fprintf(bfd, "%d checks to be issued.  TOTAL: %s\n",
					total_check_count, CurrencyString(curr_str, total_check_amount, CT_REAL));
		} else {
			fprintf(fd, "No checks to be issued.\n");
			fprintf(bfd, "No checks to be issued.\n");
			fprintf(lfd, "No checks to be issued.\n");
			fprintf(dfd, "No checks to be issued.\n");
		}
		if (total_paypal_count) {
			fprintf(fd1, "=================================================\n");
			fprintf(fd1, "\n");
			fprintf(fd1, "%d paypal total to be issued, grand total %s\n",
					total_paypal_count, CurrencyString(curr_str, total_paypal_amount, CT_REAL));
			fprintf(bfd1, "=================================================\n");
			fprintf(bfd1, "\n");
			fprintf(bfd1, "%d paypal to be issued.  TOTAL: %s\n",
					total_paypal_count, CurrencyString(curr_str, total_paypal_amount, CT_REAL));
		} else {
			fprintf(fd, "No paypal to be issued.\n");
			fprintf(bfd, "No paypal to be issued.\n");
			fprintf(lfd, "No paypal be issued.\n");
			fprintf(dfd, "No paypal to be issued.\n");
		}
		
		fclose(fd);
		fclose(bfd);
		fclose(lfd);
		fclose(dfd);

		fclose(fd1);
		fclose(bfd1);
		fclose(lfd1);
		fclose(dfd1);


		// Now email the summary letters...
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Checks Issued Detail Summary",
				summary_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Checks Issued Bank Summary",
				bank_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Check Labels",
				label_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"DHL import file",
				dhl_fname,
			  #ifdef HORATIO
				NULL,//"hkemeny@paralynx.com",	// bcc
			  #else
				NULL,	// bcc:
			  #endif
				FALSE);	// delete when sent



      /*----------------------------------------------------*/
		// Now email the summary letters...
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"PayPal Cash-Out Detail Summary",
				summary_fname1,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"PayPal Cash-Out Bank Summary",
				bank_fname1,
				NULL,	// bcc:
				FALSE);	// delete when sent
		  // Now email the summary letters...
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "Checks Issued Detail Summary",
                                summary_fname,
                                NULL,   // bcc:
                                FALSE); // delete when sent
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "Checks Issued Bank Summary",
                                bank_fname,
                                NULL,   // bcc:
                                FALSE); // delete when sent
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "Check Labels",
                                label_fname,
                                NULL,   // bcc:
                                FALSE); // delete when sent
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "DHL import file",
                                dhl_fname,
                          #ifdef HORATIO
                                NULL,//"hkemeny@paralynx.com", // bcc
                          #else
                                NULL,   // bcc:
                          #endif
                                FALSE); // delete when sent



      /*----------------------------------------------------*/
                // Now email the summary letters...
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "PayPal Cash-Out Detail Summary",
                                summary_fname1,
                                NULL,   // bcc:
                                FALSE); // delete when sent
                Email("accounting@kkrekop.io",
                                "Desert Poker Cashier",
                                "cashier@kkrekop.io",
                                "PayPal Cash-Out Bank Summary",
                                bank_fname1,
                                NULL,   // bcc:
                                FALSE); // delete when sent
		/*
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Check Labels",
				label_fname1,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"DHL import file",
				dhl_fname1,
			  #ifdef HORATIO
				NULL,//"hkemeny@paralynx.com",	// bcc
			  #else
				NULL,	// bcc:
			  #endif
				FALSE);	// delete when sent

		*/
	}
	
	else {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Couldn't open temporary files", _FL);
	}
	free(args);	// free up the malloc'd memory we got passed.

	kp(("%s %s(%d) Check run complete. Thread is exiting.\n", TimeStr(), _FL));
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}





//*********************************************************
// https://github.com/kriskoin//
// Begin processing a submitted check run.  Issue checks for
// each player in the submitted list.  The work is actually done
// by a background thread.
//
void ECash_SubmitCheckRun(struct AdminCheckRun *acr)
{
	struct AdminCheckRun *p = (struct AdminCheckRun *)malloc(sizeof(*p));
	if (!p) {
		Error(ERR_ERROR, "%s(%d) Error allocating memory for check run!", _FL);
		return;
	}
	*p = *acr;	// make a copy for ourselves.
	int result = _beginthread(ECash_SubmitCheckRunThread, 0, (void *)p);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
	}
  	/*
	result = _beginthread(ECash_SubmitPaypalRunThread, 0, (void *)p);
        if (result == -1) {
                Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
        }
	*/
}

/**********************************************************************************
 Function CCDB_Load
 date: 24/01/01 kriskoin Purpose: load the CCDB from disk
***********************************************************************************/
void CCDB_Load(void)
{
	static int initialized = FALSE;
	if (!initialized) {
		initialized = TRUE;
		PPInitializeCriticalSection(&CCDBCritSec, CRITSECPRI_CCDB, "CCDB");
		CCDB.SetParms(sizeof(CCDBEntry), (sizeof(BYTE8))*CCDB_CARDNUM_BYTES, CCDB_GROW_RATE);
		CCDB.sort_enabled = FALSE;	// doesn't matter if it's sorted for now...
	}
	if (CCDB.base) {
		return;	// already loaded... don't do it again.
	}

	// Try to load it from disk...
	EnterCriticalSection(&CCDBCritSec);
    CCDB.LoadFile(CCDB_FNAME);
	LeaveCriticalSection(&CCDBCritSec);
}
/**********************************************************************************
 Function CCDB_SaveIfNecessary
 date: 24/01/01 kriskoin Purpose:
***********************************************************************************/
void CCDB_SaveIfNecessary(void)
{
	EnterCriticalSection(&CCDBCritSec);
    CCDB.WriteFileIfNecessary(CCDB_FNAME);
	LeaveCriticalSection(&CCDBCritSec);
}

/**********************************************************************************
 Function CCDB_Entry *CCDB_GetCCEntry
 date: 24/01/01 kriskoin Purpose: return a pointer to the entry, add new one if needed
***********************************************************************************/
struct CCDBEntry *CCDB_GetCCEntry(void *cc_to_find)
{
	EnterCriticalSection(&CCDBCritSec);
	struct CCDBEntry *e = (struct CCDBEntry *)CCDB.Find(cc_to_find);
    if (e) {    // found it...
    	LeaveCriticalSection(&CCDBCritSec);
        return e;
    }

    // Add a new entry...
    struct CCDBEntry n;
    zstruct(n);
    memcpy(n.cc_num, cc_to_find, (sizeof(BYTE8))*CCDB_CARDNUM_BYTES);
	n.max_accounts_allowed = -1;	// -1 means use predefined default
	e = (struct CCDBEntry *)CCDB.Add(&n);
	LeaveCriticalSection(&CCDBCritSec);
	return e;
}

/**********************************************************************************
 Function CCDB_SetMaxAccountsAllowed
 date: 24/01/01 kriskoin Purpose: set the max_accounts_allowed for a particular card
***********************************************************************************/
void CCDB_SetMaxAccountsAllowed(void *cc_key, int max_accounts_allowed)
{
	EnterCriticalSection(&CCDBCritSec);
	struct CCDBEntry *e = CCDB_GetCCEntry(cc_key);
	if (e) {
		e->max_accounts_allowed = (INT8)max_accounts_allowed;
		CCDB.modified = TRUE;
	} else {
		Error(ERR_ERROR, "%s(%d) Unable to set %d max_accounts_allowed for c/c %08lx %08lx",
			_FL, max_accounts_allowed,
			*(WORD32 *)((char *)cc_key + 0),
			*(WORD32 *)((char *)cc_key + 4) );
	}
	LeaveCriticalSection(&CCDBCritSec);
	CCDB_SaveIfNecessary();
}

/**********************************************************************************
 Function CCDB_SetMaxAccountsAllowedForPlayerID(WORD32 player_id, int max_accounts_allowed)
 date: 24/01/01 kriskoin Purpose: scan the entire database, and set any card to this level if the player_id matches
***********************************************************************************/
void CCDB_SetMaxAccountsAllowedForPlayerID(WORD32 player_id, int max_accounts_allowed)
{
	pr(("%s(%d) Called to set (%d) for %08lx\n", _FL, max_accounts_allowed, player_id));
	if (!player_id) {	// zero is invalid
		return;
	}
	EnterCriticalSection(&CCDBCritSec);
	struct CCDBEntry *e = (struct CCDBEntry *)CCDB.base;
	for (int i=0 ; i<CCDB.member_count ; i++, e++) {
		for (int j=0; j < CCDB_CCPLAYER_IDS; j++) {
			if (e->player_ids[j] == player_id) {	// matched
				pr(("%s(%d) Changed card %08lx %08lx to %d\n", _FL,
					*(WORD32 *)(&(e->cc_num[0])),
					*(WORD32 *)(&(e->cc_num[4])),
					max_accounts_allowed));
				e->max_accounts_allowed = (INT8)max_accounts_allowed;
				CCDB.modified = TRUE;
				break;
			}
		}
	}
	LeaveCriticalSection(&CCDBCritSec);
	CCDB_SaveIfNecessary();
}

//*********************************************************
// https://github.com/kriskoin//
// Set the main player id for a particular credit card
//
void CCDB_SetMainAccountForCC(WORD32 admin_player_id, WORD32 new_main_player_id, WORD32 partial_cc_number)
{
	kp(("%s(%d) Setting main account to $%08lx for cc # %04x xxxx xxxx %04x\n",
			_FL, new_main_player_id, partial_cc_number>>16, partial_cc_number&0x00FFFF));

	EnterCriticalSection(&CCDBCritSec);
	char str[300];	// output string
	zstruct(str);
	struct CCDBEntry *e = (struct CCDBEntry *)CCDB.base;
	// Search for the credit card.
	int found_player = FALSE;
	kp(("%s(%d) found %d total entries in the CCDB\n", _FL, CCDB.member_count));
	for (int i=0 ; i<CCDB.member_count ; i++, e++) {
		WORD32 e_partial =
					(*(WORD32 *)(&(e->cc_num[0])) & 0xFFFF0000) |
					(*(WORD32 *)(&(e->cc_num[4])) & 0x0000FFFF);
		pr(("%s(%d) e->cc_num = %08lx %08lx\n",_FL,
				*(WORD32 *)&(e->cc_num[0]),
				*(WORD32 *)&(e->cc_num[4])));
		pr(("%s(%d) %d: trying to match %04x...%04x with %04x...%04x\n",
			_FL, i, partial_cc_number>>16, partial_cc_number&0x00FFFF,
			e_partial>>16, e_partial&0x00FFFF));

		if (e_partial==partial_cc_number) {
			kp(("%s(%d) matched %04x...%04x with %04x...%04x\n",
				_FL, partial_cc_number>>16, partial_cc_number&0x00FFFF,
				e_partial>>16, e_partial&0x00FFFF));
			// found a match.  Look for player id.
			for (int j=0; j < CCDB_CCPLAYER_IDS; j++) {
				kp(("%s(%d) trying to match %08lx with %08lx\n",
					_FL, e->player_ids[j], new_main_player_id));
				if (e->player_ids[j] == new_main_player_id) {	// matched
					// This is it!
					// Scroll the entries from 0 up to here up one, then
					// stick this one at the beginning.
					memmove(&e->player_ids[1], &e->player_ids[0], j*sizeof(e->player_ids[0]));
					e->player_ids[0] = new_main_player_id;
					CCDB.modified = TRUE;
					found_player = TRUE;

					// spit out a summary string...
					int accounts_allowed = e->max_accounts_allowed;
					if (accounts_allowed==-1) {
						accounts_allowed = CCMaxAllowableUsers;
					}
					char accounts[300];
					zstruct(accounts);
					BuildUserIDString(e->player_ids, CCDB_CCPLAYER_IDS, accounts);
					sprintf(str,"Updated summary for card %04x xxxx xxxx %04x:\n\n"
								"# of accounts allowed: %d%s\n\n"
								"Accounts (in order):\n"
								"%s",
								e_partial>>16, e_partial&0x00FFFF,
								accounts_allowed,
								e->max_accounts_allowed==-1 ? " (the default)" : "",
								accounts);
					break;
				}
			}
			if (found_player) {	// found someone. we're done.
				break;
			}
		}
	}
	if (!found_player) {
		sprintf(str,"Could not find player id $%08lx for card %04x xxxx xxxx %04x.\n\n"
					"No changes made.",
					new_main_player_id, partial_cc_number>>16, partial_cc_number&0x00FFFF);
	}
	LeaveCriticalSection(&CCDBCritSec);

	// Send the result as a misc client message to the administrator who
	// requested this change.
	EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	Player *p = ((CardRoom *)CardRoomPtr)->FindPlayer(admin_player_id);
	if (p) {
		struct MiscClientMessage mcm;
		zstruct(mcm);
		mcm.message_type = MISC_MESSAGE_UNSPECIFIED;
		strnncpy(mcm.msg, str, sizeof(mcm.msg));
		p->SendDataStructure(DATATYPE_MISC_CLIENT_MESSAGE, &mcm, sizeof(mcm));
	}
	LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	CCDB_SaveIfNecessary();
}

//*********************************************************
// https://github.com/kriskoin//
// Take a player id and a CCDBEntry structure and determine
// if any of the related accounts are blocked, locked out, etc.
// Returns FALSE if the charge should be declined, TRUE otherwise.
//
int CCDB_ValidateAgainstRelatedAccounts(WORD32 player_id, struct CCDBEntry *e, int *output_auto_block_flag, char *output_auto_block_reason)
{
	int charge_allowed = TRUE;
	// Check for login alerts, autoblocking, etc.
	EnterCriticalSection(&((CardRoom *)CardRoomPtr)->CardRoomCritSec);
	Player *plr = CardRoomPtr->FindPlayer(player_id);
	if (plr) {	// player object is resident in memory
		if (plr->ValidateAgainstRelatedAccounts(e->player_ids,
				CCDB_CCPLAYER_IDS, output_auto_block_flag))
		{
			// Should be blocked!
			charge_allowed = FALSE;
		}
		if (output_auto_block_flag &&	// got an auto-block flag ptr?
			*output_auto_block_flag &&	// auto-blocked?
			output_auto_block_reason)	// somewhere to store reason?
		{
			strcpy(output_auto_block_reason,
					"using same cc as ");
			BuildUserIDString(e->player_ids, CCDB_CCPLAYER_IDS,
					output_auto_block_reason+strlen(output_auto_block_reason));
		}
	}
	LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->CardRoomCritSec);
	return charge_allowed;
}

/**********************************************************************************
 Function CCDB_AddCC
 date: 24/01/01 kriskoin Purpose: add an entry to the CCDB
 returns: TRUE if charge allowed, FALSE if charge NOT allowed.
***********************************************************************************/
int CCDB_AddCC(void *cc_key, WORD32 player_id, time_t transaction_date, int test_related_accounts_flag, int *output_auto_block_flag, char *output_auto_block_reason)
{
	if (output_auto_block_flag) {
		*output_auto_block_flag = FALSE;
	}
	if (output_auto_block_reason) {
		*output_auto_block_reason = 0;
	}
	int charge_allowed = FALSE;	// default return code of 'not allowed'.
	static int max_found = 0;
	EnterCriticalSection_CardRoom();
	EnterCriticalSection(&CCDBCritSec);
	struct CCDBEntry *e = CCDB_GetCCEntry(cc_key);
	if (e) {
		int i;
		for (i=0; i < CCDB_CCPLAYER_IDS; i++) {
			if (e->player_ids[i]==player_id) {
				// It's already there... but let's make the date current
				e->last_transaction_date = max(e->last_transaction_date, transaction_date);
				int max_allowable_users = CCMaxAllowableUsers;
				if (e->max_accounts_allowed != -1) { // override
					max_allowable_users = e->max_accounts_allowed;
				}
				if (i < max_allowable_users) {
					charge_allowed = TRUE;
				} else {
				  #if 0	// 2022 kriskoin
					kp(("%s(%d) max_allowable_users = %d\n", _FL, max_allowable_users));
					kp(("%s(%d) cc entry:\n",_FL));
					khexd(e, sizeof(*e));
				  #endif
					char player_name[30];
					zstruct(player_name);
					BuildUserIDString(player_id, player_name);
					char players_str[200];
					zstruct(players_str);
					BuildUserIDString(e->player_ids, CCDB_CCPLAYER_IDS, players_str);
					SendAdminAlert(ALERT_4, "PurchBlock for %s : affects %s",
						player_name, players_str);
				}
				CCDB.modified = TRUE;

				if (test_related_accounts_flag) {
					// Check for login alerts, autoblocking, etc.
					if (!CCDB_ValidateAgainstRelatedAccounts(player_id, e,
							output_auto_block_flag, output_auto_block_reason))
					{
						charge_allowed = FALSE;
					}
				}

				LeaveCriticalSection(&CCDBCritSec);
				LeaveCriticalSection_CardRoom();
				CCDB_SaveIfNecessary();
				return charge_allowed;
			}
		}
		// It's not there... add it
		// How many do we want to preserve...?
		int locked_in = e->max_accounts_allowed;
		if (locked_in < 0) {	// use default
			locked_in = CCMaxAllowableUsers;
		}
		locked_in = min(locked_in, CCDB_CCPLAYER_IDS);
		kp1(("%s(%d) Using %d for CCMaxAllowableUsers\n", _FL, CCMaxAllowableUsers));
		// first lets try to lock it in to an empty slot available
		int index_to_use = -1;
		for (i=0; i < locked_in; i++) {
			if (!e->player_ids[i]) {	// good spot
				index_to_use = i;
				charge_allowed = TRUE;
				break;
			}
		}
		if (index_to_use == -1) {	// didn't find a good spot, scroll oldest "bad" one out
			index_to_use = min(locked_in, CCDB_CCPLAYER_IDS-1);
            // 20010116HKMB: if the entries are all full and they are all locked in, we have
			// nowhere to put the new ones, so let's instead scroll the locked in ones
			// up from the beginning (keeping the newest data and losing the oldest locked in one).
			if (locked_in >= CCDB_CCPLAYER_IDS-1) {	// are all entries "locked in"?
				index_to_use = 0;
			}
			if (index_to_use < CCDB_CCPLAYER_IDS-1) {   // are there old ones to scroll up?
    			pr(("%s(%d) ** CCDBMemMove: trying to move %d bytes (from slot %d to slot %d) for %08lx\n",
    				_FL,
    				sizeof(e->player_ids[0])*(CCDB_CCPLAYER_IDS-index_to_use-1),
    				index_to_use,
    				index_to_use+1,
    				e->player_ids[0]));

    			memmove(&e->player_ids[index_to_use+1], &e->player_ids[index_to_use], sizeof(e->player_ids[0])*(CCDB_CCPLAYER_IDS-index_to_use-1));
            }
		}
		e->player_ids[index_to_use] = player_id;
		e->last_transaction_date = transaction_date;
		CCDB.modified = TRUE;

		if (index_to_use > 0) {	// not the first one on this account?
			// Issue an alert (including email follow-up)
			char player_name[30];
			zstruct(player_name);
			BuildUserIDString(player_id, player_name);
			char players_str[200];
			zstruct(players_str);
			BuildUserIDString(e->player_ids, CCDB_CCPLAYER_IDS, players_str);
			SendAdminAlert(ALERT_6, "Dupe cc usage: %s", players_str);

			char subject[100];
			zstruct(subject);
			sprintf(subject, "Dupe cc: %s", players_str);
		  #ifndef HORATIO
			EmailStr(
				"alerts@kkrekop.io", 	// to:
				"PokerSrv",						// From (name):
				"alerts@kkrekop.io",		// From (email):
				subject,						// Subject:
				NULL,							// bcc:
				"%s"
				"%s\n\n"
				"Credit card was used by: %s\n"
				"This cc is also used by: %s\n"
				"\n",
				iRunningLiveFlag ? "" : "*** This is a test ***\n\n",
				TimeStrWithYear(),
				player_name,
				players_str);
		  #endif

			// Add an account note to each account
			for (int i=0 ; i<CCDB_CCPLAYER_IDS ; i++) {
				if (e->player_ids[i]) {
					SDB->AddAccountNote(e->player_ids[i], "%s same cc as %s", DateStrWithYear(), players_str);
				}
			}
		}

		if (!charge_allowed) {
			char player_name[30];
			zstruct(player_name);
			BuildUserIDString(player_id, player_name);
			char players_str[200];
			zstruct(players_str);
			BuildUserIDString(e->player_ids, CCDB_CCPLAYER_IDS, players_str);
			SendAdminAlert(ALERT_4, "PurchBlock for %s : affects %s",
				player_name, players_str);
		}

		if (test_related_accounts_flag) {
			// Check for login alerts, autoblocking, etc.
			if (!CCDB_ValidateAgainstRelatedAccounts(player_id, e,
					output_auto_block_flag, output_auto_block_reason))
			{
				charge_allowed = FALSE;
			}
		}

		// Now count how many player id's are associated with this credit card...
		int count = 0;
		for (i=0 ; i < CCDB_CCPLAYER_IDS; i++) {
			if (e->player_ids[i]) {
				count++;
			}
		}
	  #if 0
		if (count > 1) {
			max_found = count;
		  #if 0
			fprintf(stderr,"cc_key=%08lx %08lx, e->cc_num = %08lx %08lx\n",
					*(WORD32 *)((char *)cc_key + 0),
					*(WORD32 *)((char *)cc_key + 4),
					*(WORD32 *)((char *)e->cc_num + 0),
					*(WORD32 *)((char *)e->cc_num + 4));
		  #endif
			for (i = 0; i < CCDB_CCPLAYER_IDS; i++) {
				if (e->player_ids[i]) {
					printf("c/c = %08lx %08lx, plID = %08lx\n",
						*(WORD32 *)(&(e->cc_num[0])),
						*(WORD32 *)(&(e->cc_num[4])),
						e->player_ids[i]);
				}
			}
			printf("---------\n");
		}
	  #endif
	}
	LeaveCriticalSection(&CCDBCritSec);
	LeaveCriticalSection_CardRoom();
	CCDB_SaveIfNecessary();
	return charge_allowed;
}

/**********************************************************************************
 Function ReadEcashLog()
 date: 24/01/01 kriskoin Purpose: read an ecash log file and add it to the database
***********************************************************************************/
void ReadEcashLogForCCDB(char *filename)
{
	if (!filename) {
		fprintf(stderr,"%s(%d) ReadEcashLog must be called with a filename\n", _FL);
		return;
	}
	FILE *in;
	if ((in = fopen(filename,"rt")) == NULL) {
		fprintf(stderr,"%s(%d) Couldn't open %s for read\n", _FL, filename);
		return;
	}
	#define MAX_ECASHLOGLINE_LEN	300
	#define MAX_ECASHLOGLINE_PARMS	15
	char data_line[MAX_ECASHLOGLINE_LEN];
	char *argv[MAX_ECASHLOGLINE_PARMS];
	// read it in
	while (!feof(in) && iRunLevelDesired > RUNLEVEL_SHUTDOWN) {
		zstruct(data_line);
		fgets(data_line, MAX_ECASHLOGLINE_LEN-1, in);
		int count = ECash_GetArgs(MAX_ECASHLOGLINE_PARMS, argv, data_line, ',');
		if (count == 13) {	// potential line
			if (!strcmp(argv[3], "PurchReq") || !strcmp(argv[3], "PurchBlock")) {
				// prepare parameters
				BYTE8 bcd[CCDB_CARDNUM_BYTES];
				zstruct(bcd);
				if (MakeBCDFromCC(argv[5], bcd) == ERR_ERROR) {	// bad data, ignore
					continue;
				}
				WORD32 player_id;
				sscanf(argv[2],"%x",&player_id);
				// get the date and time
				int ye, mo, da, ho, mi, se;
				sscanf(argv[0],"%04d%02d%02d", &ye, &mo, &da);
				sscanf(argv[1],"%02d:%02d:%02d", &ho, &mi, &se);
				struct tm tms;
				zstruct(tms);
				tms.tm_year = ye - 1900;
				tms.tm_mon = mo;
				tms.tm_mday = da;
				tms.tm_hour = ho;
				tms.tm_min = mi;
				tms.tm_sec = se;
				time_t transaction_time = mktime(&tms);
			  #if 0	// testing only
				zstruct(tms);
				tms = *localtime(&transaction_time);
				printf("T = %04d%02d%02d,%02d:%02d:%02d\n",
					tms.tm_year,
					tms.tm_mon,
					tms.tm_mday,
					tms.tm_hour,
					tms.tm_min,
					tms.tm_sec);
			  #endif
				// add it to the database
			  #if 0
				fprintf(stderr, "*** Adding for %08lx -=> %08lx %08lx\n", player_id,
							*(WORD32 *)(&bcd[0]),
							*(WORD32 *)(&bcd[4]));
			  #endif
				CCDB_AddCC(bcd, player_id, transaction_time, FALSE, NULL, NULL);
			}
		}
	}
  #if 1
	// write out the whole thing in a useful format
	SDBRecord player_rec;
	struct CCDBEntry *e = (struct CCDBEntry *)CCDB.base;
	printf("Total Cards  = %d\n", CCDB.member_count);
	for (int i=0 ; i<CCDB.member_count ; i++, e++) {
		printf("%08lx %08lx\n", *(WORD32 *)(&e->cc_num[0]), *(WORD32 *)(&e->cc_num[4]));
		if (e->cc_num[0]) {
			// more than 1?
			if (e->player_ids[1]) {	// yes, display all
				int count = 0;
				char tmp[300];
				char tmp2[30];
				zstruct(tmp);
				for (int j=0; j < CCDB_CCPLAYER_IDS; j++) {
					if (e->player_ids[j]) {
						count++;
						zstruct(player_rec);
						if (SDB->SearchDataBaseByPlayerID(e->player_ids[j], &player_rec) < 0) {	// dunno who he is
							continue;
						}
						sprintf(tmp2, " %s / ", player_rec.user_id);
						strcat(tmp, tmp2);
					}
				}
				printf("%02d : %s%08lx %08lx\n",
					count, tmp, *(WORD32 *)(&e->cc_num[0]), *(WORD32 *)(&e->cc_num[4]));
			}
		}
	}
  #endif
	fclose(in);			
}

/**********************************************************************************
 Function GetCCPurchaseLimitForPlayer
 date: 24/01/01 kriskoin Purpose: for this player record, find limit given the suppled time frame
 NOTE: these limits are defined in dollars; return value here is also DOLLARS, not pennies
**********************************************************************************/
int GetCCPurchaseLimitForPlayer(WORD32 player_id, int time_frame)
{
	SDBRecord player_rec;
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) GetCCPurchaseLimitForPlayer couldn't find player_id (%08lx)",
			_FL, player_id);
		return 0;
	}

	int higher_limits = (player_rec.flags & SDBRECORD_FLAG_HIGH_CC_LIMIT);
	int allowed_limit = 0;
	if (time_frame == CCLimit1Days) {
		if (player_rec.cc_override_limit1) {
			allowed_limit = player_rec.cc_override_limit1;
		} else {
			allowed_limit = (higher_limits ? CCLimit1Amount2 : CCLimit1Amount);
		}
	} else if (time_frame == CCLimit2Days) {
		if (player_rec.cc_override_limit2) {
			allowed_limit = player_rec.cc_override_limit2;
		} else {
			allowed_limit = (higher_limits ? CCLimit2Amount2 : CCLimit2Amount);
		}
	} else if (time_frame == CCLimit3Days) {
		if (player_rec.cc_override_limit3) {
			allowed_limit = player_rec.cc_override_limit3;
		} else {
			allowed_limit = (higher_limits ? CCLimit3Amount2 : CCLimit3Amount);
		}
	} else {
		Error(ERR_INTERNAL_ERROR, "%s(%d) GetCCPurchaseLimitForPlayer called with invalid time frame (%d)",
			_FL, time_frame);
	}
	return allowed_limit;
}

/**********************************************************************************
 Functions related to multithreaded transaction number verification start here...
 date: 24/01/01 kriskoin
 The TNVPendingArray is set to something like [0] = 500000, [1] = 500001 ... [n] = n[0]+n
 In a sense, it's redundant information stored within it, where each element could just be
 a T/F based on index position+base value... but it keeps an actual list of transaction
 numbers that we expect to see

***********************************************************************************/

/**********************************************************************************
 Function DisplayTNVPendingArray
 date: 24/01/01 kriskoin Purpose: display the TNV array (debug and error only)
***********************************************************************************/
void DisplayTNVPendingArray(int line_number)
{
	kp(("%s(%d) DisplayTNVPendingArray called from line %d\n", _FL, line_number));
	for (int j=0; j < MAX_PENDING_TRANSACTION_VERIFICATIONS; j++) {
		kp(("%s(%d) TNVPending[%d] = %d\n", _FL, j, TNVPending[j]));
	}
	NOTUSED(line_number);
}

/**********************************************************************************
 Function InitializeTNVPendingArray
 date: 24/01/01 kriskoin Purpose: initial setup for the array
 Note: assumes the proper value exists in ServerVars.highest_transact_num_seen
***********************************************************************************/
void InitializeTNVPendingArray(void)
{
  #if TEST_SKIPPED_TRANSACTION_CODE
	ServerVars.highest_transact_num_seen = 499999;
	ServerVars.lowest_verified_transact_num = 499999;
  #endif
	// use the biggest number we've seen (and set them equal as they should be at this point)
	kp(("%s(%d) highest_transact_num_seen = %d, lowest_verified_transact_num = %d\n", _FL, ServerVars.highest_transact_num_seen, ServerVars.lowest_verified_transact_num));
	EnterCriticalSection(&tnv_cs);
	if (ServerVars.highest_transact_num_seen != ServerVars.lowest_verified_transact_num) {
		// problem of some sort
		Error(ERR_ERROR, "%s(%d) Mismatch of transaction numbers (%d vs %d) -- setting to highest seen",
			_FL, ServerVars.highest_transact_num_seen, ServerVars.lowest_verified_transact_num);
		// set both to the higher of the two
		ServerVars.highest_transact_num_seen =
			max(ServerVars.highest_transact_num_seen, ServerVars.lowest_verified_transact_num);
		ServerVars.lowest_verified_transact_num = ServerVars.highest_transact_num_seen;

	}
	// no matter what it was, this is our best guess as to what it should be
	TNVPending[0] = ServerVars.highest_transact_num_seen+1;
	// now let's properly fill the array
	for (int j=1; j < MAX_PENDING_TRANSACTION_VERIFICATIONS; j++) {
		TNVPending[j] = TNVPending[j-1]+1;
	}
	// DisplayTNVPendingArray(__LINE__);
	LeaveCriticalSection(&tnv_cs);
	WriteServerVars();			// make sure they're saved
}
	
/**********************************************************************************
 Function TransactionNumberReceived(int transaction_number)
 date: 24/01/01 kriskoin Purpose: deal with having recieved (verified) a transaction number
***********************************************************************************/
void TransactionNumberReceived(int thread_number, WORD32 transaction_number)
{
	// this is a good time to scan for missed transaction numbers
	if (thread_number >= 0) {	// if it's -ve, we know we've skipped one and are dealing with it now
		ScanForMissedTransactions();
	}

	EnterCriticalSection(&tnv_cs);
	// array element 0 is the bottom, ie lowest number -- the one that may scroll off
	int index_found = -1;
	for (int i=0; i < MAX_PENDING_TRANSACTION_VERIFICATIONS; i++) {
		if (TNVPending[i] == transaction_number) {
			index_found = i;
			break;
		}
	}
	if (index_found < 0) {	// wasn't found at all -- major error, shouldn't happen
		Error(ERR_INTERNAL_ERROR, "%s(%d) Didn't find transaction #%d in our array.  Here is the whole array:", _FL, transaction_number);
		DisplayTNVPendingArray(__LINE__);
		ServerVars.highest_transact_num_seen = transaction_number;
		InitializeTNVPendingArray();
	} else if (index_found == 0) {	// bottom entry, scroll it off
		// sift them all down so that element zero holds the lowest number we can ever expect to see
		ServerVars.lowest_verified_transact_num = TNVPending[0];
		int endless_loop_count = 0;
		do {
			endless_loop_count++;	// avoid lockup in case this got very confused
			if (endless_loop_count > MAX_PENDING_TRANSACTION_VERIFICATIONS) {	// major screwup
				break;	// deal with the aftermath below -- we've lost track somehow
			}
			memmove(&TNVPending[0], &TNVPending[1], sizeof(int)*(MAX_PENDING_TRANSACTION_VERIFICATIONS-1));
			TNVPending[MAX_PENDING_TRANSACTION_VERIFICATIONS-1] = TNVPending[MAX_PENDING_TRANSACTION_VERIFICATIONS-2]+1;
		} while (TNVPending[0] == 0);	// wait till a valid number we're waiting for scrolls in
		// DisplayTNVPendingArray(__LINE__);
	} else {
		// somewhere in the middle of the array, clear it as we're no longer waiting for it
		TNVPending[index_found] = 0;
		// DisplayTNVPendingArray(__LINE__);
	}
	// sanity check
	if (!TNVPending[0]) {	// big trouble
		Error(ERR_INTERNAL_ERROR, "%s(%d) The array of expected transaction numbers is all zero -- re-initializing", _FL);
		ServerVars.highest_transact_num_seen = transaction_number;
		InitializeTNVPendingArray();
	} else {	// track it as needed
		ServerVars.highest_transact_num_seen = max(ServerVars.highest_transact_num_seen, transaction_number);
	}
	// clear entry in the array of threads' lowest potential transaction number
	if (thread_number < 0 || thread_number >= MAX_ECASH_THREADS) {	// bad parameter
		kp(("%s(%d) invalid index(%d) for transaction #%d -- probably forcing it with -1 thread id\n",
			_FL, thread_number, transaction_number));
	} else {
		for (int j=0; j < MAX_ECASH_THREADS; j++) {
			if (TNVPotentialLowestTransactionNumber[j] == transaction_number) {	// waiting for this one?
				TNVPotentialLowestTransactionNumber[j] = 0;
			}
		}
	}	
	LeaveCriticalSection(&tnv_cs);
	WriteServerVars();			// make sure they're saved
	// this is a good time to scan for missed transaction numbers
	ScanForMissedTransactions();
}

/**********************************************************************************
 Function SetPotentialLowestTransactionNumber()
 date: 24/01/01 kriskoin Purpose: ecash threads use this function to notify the lowest transaction number they expect to see
 NOTE: TRUE for the valid number, FALSE for zero
***********************************************************************************/
void SetPotentialLowestTransactionNumber(int thread_number)
{
	// set it to the valid lowest number we can expect to see
	SetPotentialLowestTransactionNumber(thread_number, TRUE);
}

void ClearPotentialLowestTransactionNumber(int thread_number)
{
	SetPotentialLowestTransactionNumber(thread_number, FALSE);
}

void SetPotentialLowestTransactionNumber(int thread_number, int action_flag)
{
	if (thread_number < 0 || thread_number >= MAX_ECASH_THREADS) {	// bad parameter
		Error(ERR_INTERNAL_ERROR, "%s(%d) SPLTN was passed a invalid thread index(%d)",
			_FL, thread_number);
		return;
	}
	EnterCriticalSection(&tnv_cs);
	pr(("%s(%d) setting lowest potential for thread %d to %d\n", _FL, thread_number, ServerVars.highest_transact_num_seen+1));
	TNVPotentialLowestTransactionNumber[thread_number] =
		action_flag ? ServerVars.highest_transact_num_seen+1 : 0;
	LeaveCriticalSection(&tnv_cs);
}

/**********************************************************************************
 Function ScanForMissedTransaction
 date: 24/01/01 kriskoin Purpose: return T/F if it looks like a transaction was skipped
***********************************************************************************/
int ScanForMissedTransactions(void)
{
	EnterCriticalSection(&tnv_cs);
	WORD32 lowest_number_being_waited_for = 0;
	for (int i=0; i < MAX_ECASH_THREADS; i++) {
		if (TNVPotentialLowestTransactionNumber[i]) {	// waiting for something?
			if (!lowest_number_being_waited_for) {		// haven't seen one yet
				lowest_number_being_waited_for = TNVPotentialLowestTransactionNumber[i];
			} else {
				lowest_number_being_waited_for =
					min(lowest_number_being_waited_for, TNVPotentialLowestTransactionNumber[i]);
			}
		}
	}
	// we now know the lowest number any thread is waiting for -- check it
	int skipped_transaction = FALSE;
	if (lowest_number_being_waited_for) {
		int loop_again;
		pr(("%s(%d) testing %d as lowest number being waited for\n", _FL, lowest_number_being_waited_for));
		do {
			loop_again = FALSE;
			for (int j=0; j < MAX_PENDING_TRANSACTION_VERIFICATIONS; j++) {
				if (TNVPending[j] && TNVPending[j] < lowest_number_being_waited_for) {	// it was skipped
					pr(("%s(%d) thread index (%d) found pending of %d, implies skipped\n",
						_FL, j, TNVPending[j]));
					NotifySkippedTransactionNumber(TNVPending[j]);
					skipped_transaction = TRUE;
					loop_again = TRUE;
					break;	// only do one at a time as array will have shifted
				}
			}
		} while (loop_again);
	}
	LeaveCriticalSection(&tnv_cs);
	return skipped_transaction;
}

/**********************************************************************************
 Function NotifySkippedTransactionNumber(WORD32 transaction_number)
 date: 24/01/01 kriskoin Purpose: do whatever needs doing when we find we've skipped a transaction number
***********************************************************************************/
void NotifySkippedTransactionNumber(WORD32 transaction_number)
{
	// alert on monitor and send an email to support
	Error(ERR_WARNING, "%s(%d) Transaction #%d was skipped",_FL, transaction_number);
	SendAdminAlert(ALERT_9, "Transaction #%d was skipped", transaction_number);
	char subject[80];
	zstruct(subject);
	sprintf(subject, "Transaction %d was skipped", transaction_number);
  #if TEST_SKIPPED_TRANSACTION_CODE
	kp(("%s(%d) Email to support reporting skipped transaction %d would have gone out\n", _FL, transaction_number));
  #else
	static time_t last_email_sent_time;
	time_t time_now = time(NULL);
	if (difftime(time_now , last_email_sent_time) > 5*60) {	// only one per 5 minutes
		last_email_sent_time = time_now ;
		EmailStr("support@kkrekop.io",
				"Cashier",
				"cashier@kkrekop.io",
				subject,
				NULL,
				"%s"
				"%s CST\n\n"
				"The recent confirmation numbers we've seen imply that\n"
				"transaction #%d was skipped. This should be investigated\n"
				"ASAP as it is likely someone's credit card was charged but\n"
				"the account was not credited properly.\n"
				"\n"
				"This should be investigated via the web interface which can\n"
				"be accessed at https://admin.firepay.com.test as well as the player's\n"
				"account to verify that his History does not show this transaction.\n"
				"\n"
				"When you have confirmed who the player is and the fact that\n"
				"his card was charged, the appropriate amount should be\n"
				"transferred into his account with the \"() Make C/C Purchase\"\n"
				"selected (and the transaction number entered). This will allow\n"
				"the transaction to be properly credited back in the future.\n"
				"\n"
				,
				iRunningLiveFlag ? "":"*** THIS IS ONLY A TEST ***\n\n",
				TimeStr(),
				transaction_number);
	}
  #endif

	// this will remove it so we don't keep seeing it again
	TransactionNumberReceived(-1, transaction_number);
}

//*********************************************************
// https://github.com/kriskoin//
// Background thread to process a submitted check run.
// Issue checks for each player in the submitted list.
//
static void _cdecl ECash_SubmitPaypalRunThread(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Paypal Run");	// register this thread for stack dumps if we crash
	kp(("%s %s(%d) Beginning paypal run...\n", TimeStr(), _FL));

	char curr_str[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str);
	char summary_fname[MAX_FNAME_LEN];
	zstruct(summary_fname);
	char bank_fname[MAX_FNAME_LEN];
	zstruct(bank_fname);
	char label_fname[MAX_FNAME_LEN];
	zstruct(label_fname);
	char dhl_fname[MAX_FNAME_LEN];
	zstruct(dhl_fname);
	MakeTempFName(summary_fname, "crp");
	MakeTempFName(bank_fname, "bcp");
	MakeTempFName(label_fname, "lcp");
	MakeTempFName(dhl_fname, "dhp");
	FILE *fd = fopen(summary_fname, "wt");
	FILE *bfd = fopen(bank_fname, "wt");
	FILE *lfd = fopen(label_fname, "wt");
	FILE *dfd = fopen(dhl_fname, "wt");
        kp(("%s %s(%d) 4 file opened...\n", TimeStr(), _FL));

	if (fd && bfd && lfd && dfd) {
		int total_paypal_count = 0;
		int total_paypal_amount = 0;
		if (!iRunningLiveFlag) {
			fprintf(fd,  "*** THIS IS A SAMPLE: NOT A REAL CHECK RUN ***\n\n");
			fprintf(bfd, "*** THIS IS A SAMPLE: NOT A REAL CHECK RUN ***\n\n");
			fprintf(lfd, "*** THIS IS A SAMPLE: NOT A REAL CHECK RUN ***\n\n");
			fprintf(dfd, "*** THIS IS A SAMPLE: NOT A REAL CHECK RUN ***\n\n");
		}
		fprintf(fd, "Paypal batch started %s\n\n", TimeStr());
		// label header
		fprintf(lfd, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
			"FullName", "Addr1", "Addr2", "City", "State", "Country", "ZipCode", "PhoneNumber");
		struct AdminCheckRun *acr = (struct AdminCheckRun *)args;
		for (int i=0 ; i<MAX_PLAYERS_PER_CHECK_RUN && !iShutdownAfterECashCompletedFlag ; i++) {
			if ((acr->entries[i].player_id && acr->entries[i].amount) &&
                (acr->entries[i].description[0]=='p' ))
			{
				int amount = acr->entries[i].amount;
				WORD32 player_id = acr->entries[i].player_id;
				SDBRecord sdbr;
				zstruct(sdbr);
				EnterCriticalSection(&SDB->SDBCritSec);
				int index = SDB->SearchDataBaseByPlayerID(player_id, &sdbr);
				if (index < 0) {
					LeaveCriticalSection(&SDB->SDBCritSec);
					Error(ERR_ERROR, "%s(%d) Error: check batch could not find player id $%08lx", _FL, acr->entries[i].player_id);
					continue;
				}

				// Do they have enough in their pending field?
				if (amount > sdbr.pending_check) {
					LeaveCriticalSection(&SDB->SDBCritSec);
					Error(ERR_ERROR, "%s(%d) Error: player $%08lx didn't have enough in their pending check field!", _FL, acr->entries[i].player_id);
					continue;
				}

				// The money's there... transfer it to the pending account
				SDB->TransferChips(CT_REAL,	// real money flag
						player_id,			// from player id
						AF_PENDING_PAYPAL,	// from pending check field
						7,	// to player id
						AF_AVAILABLE_CASH,	// to chips in bank field
						amount,
						_FL);

				PL->LogFinancialTransaction(LOGTYPE_TRANS_PLR_TO_PENDING, player_id, 0, amount,
						CT_REAL, "Payapl CASHOUT", NULL);
				LeaveCriticalSection(&SDB->SDBCritSec);

  			// update detail summary email
				total_paypal_count++;
				total_paypal_amount += amount;
				fprintf(fd, "=== Paypal #%d ==== %s =====================\n", total_paypal_count, sdbr.user_id);
				fprintf(fd, "Paypal amount: %s\n\n", CurrencyString(curr_str, amount, CT_REAL));
				fprintf(fd, "Payable to:\n");
				fprintf(fd, "    %s\n", sdbr.full_name);
				fprintf(fd, "    %s\n", sdbr.mailing_address1);
				if (sdbr.mailing_address2[0]) {
					fprintf(fd, "    %s\n", sdbr.mailing_address2);
				}
				fprintf(fd, "    %s, %s, %s\n", sdbr.city, sdbr.mailing_address_state, sdbr.mailing_address_country);
				fprintf(fd, "    %s\n", sdbr.mailing_address_postal_code);
				fprintf(fd, "    %s\n", DecodePhoneNumber(sdbr.phone_number));
				fprintf(fd, "\n");
				// update bank summary email
				fprintf(bfd, "===========================================\n");
				fprintf(bfd, "%d. %-20s %-s\n", total_paypal_count, sdbr.full_name, CurrencyString(curr_str, amount, CT_REAL));
				// update label template email
				fprintf(lfd, "%s|%s|%s|%s|%s|%s|%s|%s~\n",
					sdbr.full_name,
					sdbr.mailing_address1,
					sdbr.mailing_address2,
					sdbr.city,
					sdbr.mailing_address_state,
					sdbr.mailing_address_country,
					sdbr.mailing_address_postal_code,
					DecodePhoneNumber(sdbr.phone_number));
				// update dhl import email (see spec from DHL CONNECT software)
				// company, contact, ad1, ad2, ad3, city, state, postalcode, country, phone#, ext#, fax#, email, reserved, VAT, acc#
				fprintf(dfd, "\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"\n",
					"",					// company
					sdbr.full_name,			// contact
					sdbr.mailing_address1,	// address1
					sdbr.mailing_address2,	// address2
					"",					// address3
					sdbr.city,				// city
					sdbr.mailing_address_state,			// state,
					sdbr.mailing_address_postal_code,	// postalcode
					sdbr.mailing_address_country,		// country,
					DecodePhoneNumber(sdbr.phone_number), // phone number,
					"",	// ext #
					"",	// fax #
					"",	// email
					"",	// reserved
					"",	// VAT/EIN/SSN (not used for USA)
					""		// account #
					);

				
			}
		}
		if (total_paypal_count) {
			fprintf(fd, "=================================================\n");
			fprintf(fd, "\n");
			fprintf(fd, "%d checks total to be issued, grand total %s\n",
					total_paypal_count, CurrencyString(curr_str, total_paypal_amount, CT_REAL));
			fprintf(bfd, "=================================================\n");
			fprintf(bfd, "\n");
			fprintf(bfd, "%d checks to be issued.  TOTAL: %s\n",
					total_paypal_count, CurrencyString(curr_str, total_paypal_amount, CT_REAL));
		} else {
			fprintf(fd, "No checks to be issued.\n");
			fprintf(bfd, "No checks to be issued.\n");
			fprintf(lfd, "No checks to be issued.\n");
			fprintf(dfd, "No checks to be issued.\n");
		}
		fclose(fd);
		fclose(bfd);
		fclose(lfd);
		fclose(dfd);

		// Now email the summary letters...
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Checks Issued Detail Summary",
				summary_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Checks Issued Bank Summary",
				bank_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"Check Labels",
				label_fname,
				NULL,	// bcc:
				FALSE);	// delete when sent
		Email("transaction@kkrekop.io",
				"Desert Poker Cashier",
				"cashier@kkrekop.io",
				"DHL import file",
				dhl_fname,
			  #ifdef HORATIO
				NULL, //"hkemeny@paralynx.com",	// bcc
			  #else
				NULL,	// bcc:
			  #endif
				FALSE);	// delete when sent
	} else {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Couldn't open temporary files", _FL);
	}
	free(args);	// free up the malloc'd memory we got passed.

	kp(("%s %s(%d) Check run complete. Thread is exiting.\n", TimeStr(), _FL));
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}
