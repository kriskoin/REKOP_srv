//************************************************************
// Desert Poker  Server Player Object -- admin functions
// broken out from player.cpp 20:::
#define DISP 0
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#if !WIN32
  #include <unistd.h>
#endif
#include "player.h"
#include "sdb.h"
#include "logging.h"
#include "cardroom.h"
#include "pokersrv.h"
#include "ecash.h"
#if DEBUG_SOCKET_TIMEOUTS
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <sys/time.h>
#endif //DEBUG_SOCKET_TIMEOUTS

#if WIN32	// 2022 kriskoin
  #define ACCEPT_CLIENT_SHUTDOWN_REQUESTS	1	// allow client to ask server to shut down?
#else
  #define ACCEPT_CLIENT_SHUTDOWN_REQUESTS	0	// allow client to ask server to shut down?
#endif


//*********************************************************
// https://github.com/kriskoin//
// Auto-block this player's current computer serial number
// and issue alerts as necessar
//
void Player::AutoBlock(char *trigger_reason)
{
	AutoBlock(trigger_reason, user_id, player_id);
}

void Player::AutoBlock(char *trigger_reason, char *override_user_id, WORD32 override_player_id)
{
	//*** Important note: this function is sometimes called before logging
	// in is complete, and therefore must use the override user id and
	// player id variables (if supplied).
	// This function CANNOT use the regular user_id and player_id variables.

	LoginStatus = LOGIN_BAD_PASSWORD;
	if (client_platform.computer_serial_num) {
		AddComputerBlock(client_platform.computer_serial_num);

		// Send DATATYPE_CLOSING_CONNECTION packet.
		struct ConnectionClosing cc;
		zstruct(cc);
		cc.reason = 2;	// blocked
		cc.error_code = 221;	// auto-blocked
		SendDataStructure(DATATYPE_CLOSING_CONNECTION, &cc, sizeof(cc));
		player_io_disabled = TRUE;	// no further i/o with this player allowed.

	  	SendAdminAlert(ALERT_7, "Auto-blocking computer #%5d (triggered by %s)",
				client_platform.computer_serial_num, trigger_reason);
		SDB->AddAccountNote(override_player_id, "%s: auto-blocked #%d (%s)",
				DateStrWithYear(), client_platform.computer_serial_num, ip_str);

		// Set our own auto-block flag and write it back to the database
		int did_new_block = FALSE;
		SDBRecord player_rec2;
		zstruct(player_rec2);
		if (SDB->SearchDataBaseByPlayerID(override_player_id, &player_rec2) >= 0) {
			if (!(player_rec2.flags & SDBRECORD_FLAG_AUTO_BLOCK)) {
				player_rec2.flags |= SDBRECORD_FLAG_AUTO_BLOCK;
				SDB->WriteRecord(&player_rec2);
				did_new_block = TRUE;
			}
		}

		// Send an email about this auto-blocking occurance.
		char subject[100];
		zstruct(subject);
		sprintf(subject, "Autoblock: %s (s/n %d)", override_user_id, client_platform.computer_serial_num);
		EmailStr(
			"alerts@kkrekop.io", 	// to:
			"PokerSrv",						// From (name):
			"alerts@kkrekop.io",		// From (email):
			subject,						// Subject:
			NULL,							// bcc:
			"%s"
			"%s\n\n"
			"Computer serial number %d was auto-blocked.\n"
			"This auto-block was triggered by %s\n"
			"\n"
			"Current player id: %s%s\n"
			"Current ip address: %s\n",
			iRunningLiveFlag ? "" : "*** This is a test ***\n\n",
			TimeStrWithYear(),
			client_platform.computer_serial_num,
			trigger_reason,
			override_user_id,
			did_new_block ? " (now autoblocked as well)" : "",
			ip_str);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Send the admin stats out.  It is assumed the player is logged
// in with sufficient privilege level
//
void Player::SendAdminStats(void)
{
	// time to send new admin stats.
	last_admin_stats_sent = CurrentAdminStats[0].time;
	struct AdminStats as = CurrentAdminStats[0];
	// Zero out some fields if non-admin
	if (priv < ACCPRIV_ADMINISTRATOR) {
		as.rake_today = 0;
		as.rake_balance = 0;
		as.ecash_today = 0;
		as.ecashfee_today = 0;
		as.rake_per_hour = 0;
		as.est_rake_for_today = 0;
		as.est_ecash_for_today = 0;
		as.gross_bets_today = 0;
		as.gross_tournament_buyins_today = 0;
	}
	SendDataStructure(DATATYPE_ADMIN_STATS, &as, sizeof(as));
}	

//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct AdminCheckRun
//
ErrorType Player::ProcessAdminCheckRun(struct AdminCheckRun *input_check_run, int input_structure_len)
{
	kp(("enter the processAdminCheckRun\n")) ;
	if (sizeof(*input_check_run) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) AdminCheckRun structure was wrong length (%d vs %d). Disconnecting socket.",_FL,sizeof(*input_check_run),input_structure_len);
		ProcessPlayerLogoff();
		LoginStatus = LOGIN_INVALID;
		return ERR_ERROR;	// do not process.
	}
	if (priv < ACCPRIV_ADMINISTRATOR) {
		Error(ERR_ERROR, "%s(%d) AdminCheckRun structure received without high enough priv level.", _FL);
		return ERR_ERROR;	// do not process.
	}
	ECash_SubmitCheckRun(input_check_run);

	SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"Your check run has been received by the\n"
				"server and accepted for processing.");

	return ERR_NONE;
}

/**********************************************************************************
 ErrorType Player::ProcessStatementRequest
 Date: 20180707 kriskoin :  Purpose: process a credit card statement request
***********************************************************************************/
ErrorType Player::ProcessStatementRequest(struct CCStatementReq *ccsr, int input_structure_len)
{
	if (sizeof(*ccsr) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) CCStatementReq packet was wrong length (%d vs %d) from player_id $%08lx.  Disconnecting.",
			_FL,sizeof(*ccsr),input_structure_len, player_id);
		return ERR_ERROR;	// do not process.
	}
	pr(("%s(%d) Received ccsr struct for %08lx\n", _FL, ccsr->player_id));
	if (priv >= ACCPRIV_CUSTOMER_SUPPORT) {
		ccsr->admin_player_id = player_id;
	}
	return AddEcashRequestToQueue(ccsr);
}

//*********************************************************
// https://github.com/kriskoin//
// Process a DATATYPE_SHUTDOWN_MSG message if we're allowed to.
//
void Player::ProcessClientShutdownRequest(void)
{
  #if ACCEPT_CLIENT_SHUTDOWN_REQUESTS
   #if !WIN32 // for testing under WIN32, even anon players can shut down.
	if (priv >= ACCPRIV_SUPER_USER)
   #endif
	{
		char message[100];
		sprintf(message, "Client with player_id $%08lx asked everyone to shut down", player_id);
		PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
		((CardRoom *)cardroom_ptr)->SendShutdownMessageToPlayers(TRUE, message);
		LeaveCriticalSection(&PlayerCritSec);

	  #if WIN32	// 2022 kriskoin
		//printf("%s(%d) Shutdown message sent to all clients... pausing before exiting.\n",_FL);
	  	//kp(("%s(%d) Sleeping for %dms to let messages get sent out.\n", _FL, wait_ms));
		//Sleep(wait_ms);	// give them time to be sent out.
	  	//kp(("%s(%d) Done sleeping... changing run level.\n",_FL));
		SetRunLevelDesired(RUNLEVEL_EXIT, message);
	  #endif
	}
  #endif	// ACCEPT_CLIENT_SHUTDOWN_REQUESTS
}	


//*********************************************************
// https://github.com/kriskoin//
// Handle a request to update the shot clock (admin function)
// DATATYPE_SHOTCLOCK_UPDATE
//
ErrorType Player::ProcessShotclockUpdate(struct ShotClockUpdate *scu, int input_structure_len)
{
	// Process struct ShotClockUpdate
	if (sizeof(*scu) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) ShotClockUpdate was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*scu),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// 24/01/01 kriskoin:
	if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"You must be customer support to perform this action.");
		return ERR_ERROR;
	}

	if (scu->flags & SCUF_GLOBAL_ALLIN_SETTING) {
		// This is really a global all-in setting, not a shot clock update.
		AllInsResetTime = scu->shotclock_time;
		AllInsAllowed = scu->misc_value1;
		GoodAllInsAllowed = scu->misc_value2;
		kp(("%s %-15.15s All-Ins were set to %d and reset as of %s\n",
				TimeStr(), ip_str, AllInsAllowed, TimeStr(AllInsResetTime)));
		return ERR_NONE;	// all done.		
	}

	// assume it's a shot clock update setting...
	ShotClockDate = scu->shotclock_time;
	strnncpy(ShotClockMessage1, scu->shotclock_message1, SHOTCLOCK_MESSAGE_LEN);
	strnncpy(ShotClockMessage2, scu->shotclock_message2, SHOTCLOCK_MESSAGE_LEN);
	strnncpy(ShotClockMessage1_Expired, scu->shotclock_message1_expired, SHOTCLOCK_MESSAGE_LEN);
	strnncpy(ShotClockMessage2_Expired, scu->shotclock_message2_expired, SHOTCLOCK_MESSAGE_LEN);
	if (priv < ACCPRIV_SUPER_USER) {
		// disable some flags if not superuser
		#define SCUF_SUPER_USER_ONLY_FLAGS (SCUF_SHUTDOWN_WHEN_DONE|SCUF_SHUTDOWN_IS_BRIEF|SCUF_SHUTDOWN_AUTO_INSTALLNEW)
		scu->flags &= ~SCUF_SUPER_USER_ONLY_FLAGS;
		scu->flags |= ShotClockFlags & SCUF_SUPER_USER_ONLY_FLAGS;
	}
  #if 0	// 2022 kriskoin
	if (priv < ACCPRIV_ADMINISTRATOR) {
		// disable some flags if not administrator
		#define SCUF_ADMIN_USER_ONLY_FLAGS (SCUF_CLOSE_TOURNAMENTS|SCUF_NO_TOURNAMENT_SITDOWN)
		scu->flags &= ~SCUF_ADMIN_USER_ONLY_FLAGS;
		scu->flags |= ShotClockFlags & SCUF_ADMIN_USER_ONLY_FLAGS;
		scu->max_tournament_tables = (WORD16)MaxTables_GDB[GDBITNUM_CLOSE_TOURNAMENTS];
	}
  #endif

	int new_game_disable_bits = GameDisableBits & ~GDB_CLOSE_TOURNAMENTS;
	if (scu->flags & SCUF_CLOSE_TOURNAMENTS) {
		new_game_disable_bits |= GDB_CLOSE_TOURNAMENTS;
	}
	int new_game_close_bits = GameCloseBits & ~GDB_CLOSE_TOURNAMENTS;
	if (scu->flags & SCUF_NO_TOURNAMENT_SITDOWN) {
		new_game_close_bits |= GDB_CLOSE_TOURNAMENTS;
		new_game_disable_bits |= GDB_CLOSE_TOURNAMENTS;
	}
	if (new_game_disable_bits != GameDisableBits || new_game_close_bits != GameCloseBits) {	// something changed.
		pr(("%s %s(%d) user '%s' (%s) has turned %s tournaments.\n",
				TimeStr(), _FL, user_id, ip_str,
				(new_game_disable_bits & GDB_CLOSE_TOURNAMENTS) ? "OFF" : "ON"));
		SendAdminAlert(ALERT_6,
				"User '%s' (%s) has turned %s tournaments (max %d tables).",
				user_id, ip_str,
				(new_game_disable_bits & GDB_CLOSE_TOURNAMENTS) ? "OFF" : "ON",
				scu->max_tournament_tables);

		if (new_game_disable_bits & GDB_CLOSE_TOURNAMENTS) {
			pr(("%s      The message users will see: %s\n",
					TimeStr(), GameDisabledMessage));
			SendAdminAlert(ALERT_6, "The message users will see: %s", GameDisabledMessage);
		}
		GameDisableBits = new_game_disable_bits;
		GameCloseBits = new_game_close_bits;
	}
	ShotClockFlags = scu->flags;
	MaxTables_GDB[GDBITNUM_CLOSE_TOURNAMENTS] = scu->max_tournament_tables;
	iShotClockChangedFlag = TRUE;	// force instant re-send to clients
	NextAdminStatsUpdateTime = 0;	// update/send admin stats packet asap

	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Handle a request to transfer money (admin function)
//
ErrorType Player::ProcessTransferRequest(struct TransferRequest *tr, int input_structure_len)
{
	// Process struct TransferRequest
	if (sizeof(*tr) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) TransferRequest was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*tr),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// Look up an account based on user id or player id...
	// You must be an ACCPRIV_CUSTOMER_SUPPORT to perform this task
	if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"You must be customer support to perform this action.");
		return ERR_ERROR;
	}
	if (!tr->from_id || !tr->to_id || !tr->amount || !tr->reason) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"Some of the TransferRequest fields were empty.\nNo action taken.\n");
		return ERR_ERROR;
	}
	
	struct SDBRecord sdb1;
	struct SDBRecord sdb2;
	zstruct(sdb1);
	zstruct(sdb2);
        WORD32 fromecashid, toecashid;
	int index = SDB->SearchDataBaseByPlayerID(tr->from_id, &sdb1);
	if (index < 0) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"Cannot find player $%08lx\nNo action taken.\n", tr->from_id);
		return ERR_ERROR;
	}
	index = SDB->SearchDataBaseByPlayerID(tr->to_id, &sdb2);
	if (index < 0) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"Cannot find player $%08lx\nNo action taken.\n", tr->to_id);
		return ERR_ERROR;
	}

	// Make sure this user has the required priv level to perform the xfer
	if (priv < sdb1.priv || priv < sdb2.priv) {
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
				"You do not have the required priviledge level to perform this transfer.\n"
				"No action taken.\n");
		return ERR_ERROR;
	}

	// Now we've verified the data... transfer the money.
	pr(("%s(%d) This transfer's reason = '%s'\n", _FL, tr->reason));

	PL->LogFinancialTransfer(LOGTYPE_TRANSFER, tr->from_id, tr->to_id, tr->amount,
		tr->from_account_field, tr->to_account_field, (ChipType)tr->chip_type, tr->reason);
	SDB->TransferChips((ChipType)tr->chip_type, tr->from_id, tr->from_account_field,
				tr->to_id, tr->to_account_field, tr->amount, _FL);

	char email_title[100];
        char email_buff[500];
        char curr_str[16];
        zstruct(curr_str);

        char from_emailaddress[40],to_emailaddress[40];
	zstruct (email_title);
        memset(email_buff, 0, 500);
        char from_str[CT_SPARE_SPACE];
        char to_str[CT_SPARE_SPACE];
        char vtimestr[30];
        WORD32 from_playerid, to_playerid;
        zstruct(vtimestr);
        zstruct(from_str);
        zstruct(to_str);
        zstruct(vtimestr);
        SDBRecord player_rec;
        // do the 'from' side
        zstruct(player_rec);

	
	
        if (SDB->SearchDataBaseByPlayerID(tr->from_id, &player_rec) >=0) {
                memcpy(from_str, player_rec.user_id, CT_SPARE_SPACE);   // no null terminator!!
                from_playerid = player_rec.player_id;
                sprintf(from_emailaddress, player_rec.email_address);
        } else {        // didn't find the ID name -- ??? will do
                Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
                        _FL, player_id);
                strnncpy(from_str, "?????", CT_SPARE_SPACE);
        }
        // do the 'to' side
        zstruct(player_rec);
        if (SDB->SearchDataBaseByPlayerID(tr->to_id, &player_rec) >=0) {
                memcpy(to_str, player_rec.user_id, CT_SPARE_SPACE);     // no null terminator!!
                to_playerid = player_rec.player_id;
                sprintf(to_emailaddress, player_rec.email_address);
        } else {        // didn't find the ID name -- ??? will do
                Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
                        _FL, player_id);
                strnncpy(from_str, "?????", CT_SPARE_SPACE);
        }

	
	// log it to transaction history
	if (tr->chip_type == CT_REAL) {	// only for real money
		if (!(tr->flags & TRF_NO_HISTORY_ENTRY)) {	// we allowed to make a history entry?
			ClientTransaction ct;
			zstruct(ct);
			ct.credit_left = 0;
			ct.timestamp = time(NULL);
			ct.transaction_amount = tr->amount;
			// these two strings below are used only with CTT_MISC or for specific account names
			//char from_str[CT_SPARE_SPACE];	
			//char to_str[CT_SPARE_SPACE];
			//zstruct(from_str);
			//zstruct(to_str);
			if (tr->from_id != tr->to_id) {	// from one account to another
				// figure out the proper CTT for both sides
				ClientTransactionType ctt_in = CTT_TRANSFER_IN;
				ClientTransactionType ctt_out = CTT_TRANSFER_OUT;
				// better details if possible, knowing the accounts
				if (tr->from_id == SDB->DraftsInRec_ID) {
					ctt_in = CTT_DRAFT_IN;
					ctt_out = CTT_DRAFT_OUT;
				}
				//kp1(("%s(%d) this should be enabled after 1.09 build 3 is released\n", _FL)); // 20:::			  #if 1	// 1.09 build 3 and onwards; do not turn on till client is released after July 3rd
				if (tr->from_id == SDB->PrizesRec_ID) {
					ctt_in = CTT_PRIZE_AWARD;	// the client will see this
				}
				if (tr->from_id == SDB->BadBeatRec_ID) {
					ctt_in = CTT_BAD_BEAT_PRIZE;	// the client will see this
				}
			  #endif

				// if it's coming or going to the pending field, that's more important
				if (tr->from_account_field == AF_PENDING_CC_REFUND) {
					ctt_out = CTT_TRANSFER_FEE;
				}
				if (tr->to_account_field == AF_PENDING_CC_REFUND) {
					ctt_in = CTT_TRANSFER_FEE;
				}
				
				if (tr->flags & TRF_PLAYER_TO_PLAYER) {
					// we want to find the actual name on these accounts and
					// jam them in so they can be displayed in the history
					ctt_in = CTT_TRANSFER_FROM;
					ctt_out = CTT_TRANSFER_TO;
					SDBRecord player_rec;
					// do the 'from' side
					zstruct(player_rec);
					if (SDB->SearchDataBaseByPlayerID(tr->from_id, &player_rec) >=0) {
						memcpy(from_str, player_rec.user_id, CT_SPARE_SPACE);	// no null terminator!!
					} else {	// didn't find the ID name -- ??? will do
						Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
							_FL, player_id);
						strnncpy(from_str, "?????", CT_SPARE_SPACE);
					}
					// do the 'to' side
					zstruct(player_rec);
					if (SDB->SearchDataBaseByPlayerID(tr->to_id, &player_rec) >=0) {
						memcpy(to_str, player_rec.user_id, CT_SPARE_SPACE);	// no null terminator!!
					} else {	// didn't find the ID name -- ??? will do
						Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
							_FL, player_id);
						strnncpy(from_str, "?????", CT_SPARE_SPACE);
					}
				}
				if (tr->flags & TRF_CUSTOM_MESSAGE) {
					// copy the 11-byte string that gets used with a CTT_MISC transaction
					ctt_in = CTT_MISC;
					ctt_out = CTT_MISC;
					memcpy(from_str, tr->str, CT_SPARE_SPACE);	// no null terminator!!
					memcpy(to_str, tr->str, CT_SPARE_SPACE);	// no null terminator!!
				}
				if (tr->flags & TRF_TREAT_AS_CC_PURCHASE) {
					// This should show up like a CC purchase or credit
					// note: the player does not currently get charged the
					// 5.25% transaction fee.  This is due to lazyness on my
					// part at this particular time.  It's more thinking and
					// testing than I care to do and my todo list is so long
					// I don't even think it's all that important right now.
					// Down the road, somebody should definitely break it
					// up and do two transfers of the correct amount.
					// The other thing that should be done is that the
					// credit card number shows up as zeroes.
					ct.ecash_id = tr->transaction_number;
					ct.transaction_type = CTT_PURCHASE;
					ct.credit_left = ct.transaction_amount;	// assume full amount can be credited
					SDB->LogPlayerTransaction(tr->to_id, &ct);
					ct.transaction_type = CTT_CREDIT;		// the other account should see this as a credit
					ct.credit_left = 0;						// for credits, this should always be zero
					SDB->LogPlayerTransaction(tr->from_id, &ct);
				} else {
					// two entries; one for each side
					ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(tr->from_id);

					fromecashid  = ct.ecash_id;					
					ct.transaction_type = (BYTE8)ctt_out;
					sprintf(ct.str, to_str);	// no null terminator!!
					SDB->LogPlayerTransaction(tr->from_id, &ct);
					ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(tr->to_id);
					toecashid = ct.ecash_id ;
					
					ct.transaction_type = (BYTE8)ctt_in;
					sprintf(ct.str, from_str);	// no null terminator!!
					kp(("Transfer ct.str %s\n", ct.str)) ;
					kp(("%s(%d) Transfer ct.str = %s\n", _FL, ct.str));
					SDB->LogPlayerTransaction(tr->to_id, &ct);
				}
			} else {	// internal transfer of some sort -- only one entry
				ClientTransactionType ctt = CTT_TRANSFER_INTERNAL;
				// 0=in bank, 1=pending CC refund field, 2=pending check field
				  kp(("Transfer tr->from_account_field %d\n", tr->from_account_field)) ;
				if (tr->from_account_field == 0 && tr->to_account_field == 2) {
					ctt = CTT_CHECK_ISSUED;
				} else if (tr->from_account_field == 1 && tr->to_account_field == 0) {
					ctt = CTT_TRANSFER_FEE;
				} else if (tr->from_account_field == 2 && tr->to_account_field == 0) {
					ctt = CTT_CHECK_REFUND;
				} else if (tr->from_account_field == 3 && tr->to_account_field == 0) {
					ctt = CTT_PAYPAL_REFUND;
				        ct.credit_left = ct.transaction_amount;
				}
				// only one entry
				ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(tr->to_id);
				ct.transaction_type = (BYTE8)ctt;
				SDB->LogPlayerTransaction(tr->to_id, &ct);
			}
		}
	}
	// notify both players if possible
	EnterCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
	Player *p = ((CardRoom *)cardroom_ptr)->FindPlayer(tr->to_id);
	if (p) {	// found him
		p->ChipBalancesHaveChanged(FALSE);	// false means it won't be sent (as next line sends it)
		p->send_client_info = TRUE;
	}
	p = ((CardRoom *)cardroom_ptr)->FindPlayer(tr->from_id);
	if (p) {
		p->ChipBalancesHaveChanged(FALSE);	// false means it won't be sent (as next line sends it)
		p->send_client_info = TRUE;
	}
	LeaveCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
        sprintf(vtimestr, TimeStrWithYear( ));
	vtimestr[10] = 0 ;
	CurrencyString(curr_str, tr->amount, CT_REAL);
        zstruct (email_title);
        memset(email_buff, 0, 500);
	FILE *file_out;
	FILE *file_out1;
	FILE *file_out2;
	FILE *file_out3;

	if (tr->from_id ==9) {
	    zstruct(email_title);	
            sprintf(email_title, "DraftsIn Deposit" );

	    sprintf(email_buff, "A Bank Draft, Money Order or Western Union Wire in the amount of %s"
                "has been transferred to the account of %s . (transfer-in transaction #%s - %08lx - %d"
                " ).\n\n"
                "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n"
                "If you have any questions regarding this transaction,\n"
                "please contact us at cashier@kkrekop.io\n", curr_str, to_str, vtimestr ,tr->to_id ,toecashid);
	

                if ((file_out = fopen("ece.txt", "wt")) == NULL) {
                        Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL,"ece.txt" );
                } else {
                        fputs(email_buff, file_out);    // dump the whole thing out
                        fclose(file_out);
                        Email("transaction@kkrekop.io", "DesertPoker Cashier", "cashier@kkrekop.io",
                                        email_title, "ece.txt","answers@kkrekop.io",FALSE);
                	

			if (SDB->SearchDataBaseByPlayerID(tr->to_id, &player_rec) >=0) {
         		       sprintf(to_str, player_rec.user_id);     // no null terminator!!
                		to_playerid = player_rec.player_id;
                		sprintf(to_emailaddress, player_rec.email_address);
        		} else {        // didn't find the ID name -- ??? will do
               			 Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
                        	_FL, player_id);
                	strnncpy(from_str, "?????", CT_SPARE_SPACE);
        		}
			kp(("%s(%d) Transfer player email: %s\n", _FL, player_rec.email_address));
			Email(player_rec.email_address, "DesertPoker Cashier", "cashier@kkrekop.io",
                                        email_title, "ece.txt",NULL,FALSE);
		
		}

	
	}
	else
        {
                zstruct(email_title);
		sprintf(email_title, "Money Transfer from %s to %s ", from_str, to_str);
		sprintf(email_buff,
		"A transfer of funds has been made from %s  to %s in the amount of %s. "
		"(transfer-out transaction #%s - %08lx - %d ), "
		"(transfer-in transaction #%s - %08lx - %d ), Details of this transaction appear in "
		"the Cashier History log for each Player.\n\n"
                "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n"
                "If you have any questions regarding this transaction,\n"
                "please contact us at cashier@kkrekop.io\n",from_str, to_str, curr_str,
		vtimestr ,tr->from_id ,fromecashid, vtimestr ,tr->to_id ,toecashid );

                if ((file_out1 = fopen("ece1.txt", "wt")) == NULL) {
                        Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL,"ece.txt" );
                } else {
                        fputs(email_buff, file_out1);    // dump the whole thing out
                        if ((file_out2 = fopen("ece2.txt", "wt")) == NULL) {
                        	Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL,"ece.txt" );
                	} else {
				fputs(email_buff, file_out2);    // dump the whole thing out
			}
			 if (SDB->SearchDataBaseByPlayerID(tr->to_id, &player_rec) >=0) {
                               strnncpy(to_str, player_rec.user_id, CT_SPARE_SPACE);     // no null terminator!!
                                to_playerid = player_rec.player_id;
                                sprintf(to_emailaddress, player_rec.email_address);
                        } else {        // didn't find the ID name -- ??? will do
                                 Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
                                _FL, player_id);
                        strnncpy(from_str, "?????", CT_SPARE_SPACE);
                        }
                 	kp(("%s(%d) Transfer player email: %s\n", _FL, player_rec.email_address));

		}
		/*
		zstruct(email_title);
		zstruct(email_buff);
		sprintf(email_title, "Money Transfer from %s  to %s \n", from_str, to_str);
                sprintf(email_buff,
                "A transfer of funds has been made from %s  to %s in the amount of %s. "
                "(transfer-out transaction # %s - %08lx - %d),(transfer-in transaction # %s - %08lx - %d). Details of this transaction appear in "
                "the Cashier History log for each Player.\n\n"
                "-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n"
                "If you have any questions regarding this transaction,\n"
                "please contact us at cashier@kkrekop.io\n",from_str, to_str, curr_str, vtimestr ,tr->from_id ,fromecashid, vtimestr ,tr->to_id ,toecashid );
        	*/
                if ((file_out3 = fopen("ece3.txt", "wt")) == NULL) {
                        Error(ERR_ERROR,"%s(%d) Couldn't open email file (%s) for write", _FL,"ece.txt" );
                } else {
                        fputs(email_buff, file_out3);    // dump the whole thing out

        	if (SDB->SearchDataBaseByPlayerID(tr->from_id, &player_rec) >=0) {
                	memcpy(from_str, player_rec.user_id, CT_SPARE_SPACE);   // no null terminator!!
                	from_playerid = player_rec.player_id;
                	sprintf(from_emailaddress, player_rec.email_address);
        	} else {        // didn't find the ID name -- ??? will do
                	Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
                        	_FL, player_id);
                	strnncpy(from_str, "?????", CT_SPARE_SPACE);
        	}
		kp(("%s(%d) Transfer player email: %s\n", _FL, player_rec.email_address));
		
                }
		fclose(file_out1);
		fclose(file_out2);
		fclose(file_out3);

		Email("transaction@kkrekop.io",
			"DesertPoker Cashier",
			 "cashier@kkrekop.io",
                                     email_title,
					"ece1.txt",
					"answers@kkrekop.io",
					FALSE);
		
		
		Email(to_emailaddress,
			"DesertPoker Cashier",
			"cashier@kkrekop.io",
                                    email_title,
			     	"ece1.txt",
				NULL,
				FALSE);

	 	Email(from_emailaddress,
			"DesertPoker Cashier",
			"cashier@kkrekop.io",
                                    email_title,
				     "ece1.txt",
					NULL,
					FALSE);
	
		}


	return ERR_NONE;
}

/**********************************************************************************
 Function Player::ProcessSetTransactionCreditableAmt
 date: 24/01/01 kriskoin Purpose: set creditable left on a transaction
***********************************************************************************/
ErrorType Player::ProcessSetTransactionCreditableAmt(struct MiscClientMessage *mcm)
{
	// a misc client message was used -- but field names don't correspond
	// to see the source of this message, see client:cashier.cpp(314)
	SDB->SetCreditLeftForTransaction(mcm->table_serial_number,
		mcm->misc_data_1, CTT_PURCHASE, mcm->misc_data_2);
	return ERR_NONE;
}

/**********************************************************************************
 ErrorType Player::ProcessSetCheckTrackingInfo
 Date: 20180707 kriskoin :  Purpose: modify tracking info for a player's check delivery and possibly send email
***********************************************************************************/
ErrorType Player::ProcessSetCheckTrackingInfo(struct MiscClientMessage *mcm)
{
	WORD32 player_id = mcm->table_serial_number;
	int check_number = mcm->misc_data_1;
	int check_amount = mcm->misc_data_2;
	// misc_data_3 holds which button was pushed
	int action = mcm->misc_data_3;	// 0 = apply&email, 1 = apply/no email, 2 = only email
	BYTE8 delivery_method = (BYTE8)(mcm->display_flags);
	#define MAX_TRACKINGNUMBER_LEN	20
	char tracking_number[MAX_TRACKINGNUMBER_LEN]; // 20 = 8+10+null (see gamedata.h for ClientCheckTransaction)
	zstruct(tracking_number);
	strnncpy(tracking_number, mcm->msg, MAX_TRACKINGNUMBER_LEN);
	// we only have 18 chars to store this in the client transaction -- it will be
	// stored as two chunks of data with no null
	pr(("Received request for player %08lx to set tracking number to %s for check %d (for %d) with courier %d, action %d\n",
		player_id, tracking_number, check_number, check_amount, delivery_method, action));
	// do we update SDB?
	if (action == 0 || action == 1) {
		SDB->ModifyCheckTransaction(player_id, check_number, check_amount, tracking_number, delivery_method);
	}
	// do we send an email?
	if (action == 0 || action == 2) {
		char delivery_str[8][30] = { " ", "DHLX", "Trans-Express", "Federal Express", "Certified Mail", "Registered Mail", "ExpressMail", "Priority Mail" };
		char websites_str[8][50] = { " ", "www.dhl.com", "www.transexpress.com/eng/index.asp", "www.fedex.com", "www.usps.com", "www.usps.com", "www.usps.com", "www.usps.com" };
		SDBRecord player_rec;
		zstruct(player_rec);
		if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
				_FL, player_id);
			return ERR_ERROR;
		}
		// email ok to send?
		if (player_rec.flags & (SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES)) {
			Error(ERR_WARNING, "%s(%d) Couldn't send check notification email to %08lx because of bad email address",
				_FL, player_id);
			return ERR_WARNING;
		}
		char fname[MAX_FNAME_LEN];
		zstruct(fname);
		MakeTempFName(fname, "chtr");
		FILE *fd = fopen(fname, "wt");
		if (fd) {
			if (!iRunningLiveFlag) {
				fprintf(fd, "*** THIS IS A SAMPLE ONLY: PLEASE IGNORE ***\n\n");
			}
			char curr1[MAX_CURRENCY_STRING_LEN];
			zstruct(curr1);
			fprintf(fd, "Dear %s,\n\nA DesertPoker bank draft in the amount of %s is on its way to you by ",
				player_rec.full_name, CurrencyString(curr1, check_amount, CT_REAL));
			if (delivery_method == CDT_DHL || delivery_method == CDT_TRANS_EXPRESS || delivery_method == CDT_FEDEX) {	// courier
				fprintf(fd, "courier, via %s International Courier Services.\n\nThe courier tracking number for your bank draft is %s. If desired, you may visit the ",
					delivery_str[delivery_method], tracking_number);
				fprintf(fd, "%s website ( %s ) and check on the status of your delivery by using this tracking number.\n\n",
					delivery_str[delivery_method], websites_str[delivery_method]);
				fprintf(fd, "%s deliveries are made during regular business hours.\n\n",
					delivery_str[delivery_method]);
			} else {	// some sort of mail
				fprintf(fd, "US Mail, via %s. \n\n", delivery_str[delivery_method]);
				fprintf(fd, "This bank draft will be issued by our bank in Belize and then forwarded through \n");
				fprintf(fd, "to the US Postal Service via Interlink, a company that will courier it as far as Miami.\n\n");
			}
			fprintf(fd, "Best Wishes,\n\nDesertPoker Cashier\n");

			fprintf(fd, "\n");
			fclose(fd);
			// Now email it...
			char subject[100];
			zstruct(subject);
			sprintf(subject, "Your DesertPoker payment delivery information...");
			Email(	player_rec.email_address,
					"DesertPoker Cashier",
					"checktracking@kkrekop.io",
					subject,
					fname,
					"checktracking@kkrekop.io",	// bcc:
					TRUE);	// delete when sent
		}
	}
	return ERR_NONE;
}

/**********************************************************************************
 Function ProcessAdminReqChargebackLetter();
 date: 24/01/01 kriskoin Purpose: generate a chargeback letter for this player
***********************************************************************************/
void Player::ProcessAdminReqChargebackLetter(WORD32 admin_player_id, WORD32 player_id)
{
	struct AdminInfoBlock aib;
	zstruct(aib);
	SDBRecord player_rec;
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		sprintf(aib.info, "ERROR: %08lx not found trying to generate chargeback text\r\n", player_id);
	} else {
		// fill in address block
		char address_block[300];
		zstruct(address_block);
		sprintf(address_block,
			"   %s\r\n"
			"   %s\r\n"
			"%s%s%s"
			"   %s, %s, %s\r\n"
			"   %s",
			player_rec.full_name,
			player_rec.mailing_address1,
			player_rec.mailing_address2[0] ? "   " : "",
			player_rec.mailing_address2,
			player_rec.mailing_address2[0] ? "\r\n" : "",
			player_rec.city,
			player_rec.mailing_address_state,
			player_rec.mailing_address_country,
			player_rec.mailing_address_postal_code);
		// fill in transaction block
		char transaction_block[TRANS_TO_RECORD_PER_PLAYER*150];
		zstruct(transaction_block);
		char tmp[150];
		zstruct(tmp);
		for (int i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
			ClientTransaction *ct = &player_rec.transaction[i];
			if (ct->transaction_type == CTT_PURCHASE) {
				ClientTransactionDescription(ct, tmp, FALSE);
				strcat(tmp, "\r\n");	// append CR/LF
				strcat(transaction_block, tmp);
			}
		}
		// fill in connection block
		char connection_block[LOGINS_TO_RECORD_PER_PLAYER*50];
		zstruct(connection_block);
		// also build a signature based on the real serial number
		#define SERIAL_NUMBER_OBFUSCATION	0xFADE	// something to change it a little
		for (int j=0 ; j < LOGINS_TO_RECORD_PER_PLAYER ; j++) {
			if (!player_rec.last_login_times[j])
				break;
			char ip_str[20];
			IP_ConvertIPtoString(player_rec.last_login_ip[j], ip_str, 20);
			sprintf(connection_block+strlen(connection_block), " #%02d: %s from %s (p%x)\r\n",
					j+1, TimeStr(player_rec.last_login_times[j], FALSE, TRUE, SERVER_TIMEZONE),
					ip_str, player_rec.last_login_computer_serial_nums[j] & SERIAL_NUMBER_OBFUSCATION);
		}
		// fill in computer description block
		char comp_desc_str[1000];
		zstruct(comp_desc_str);
		FillClientPlatformInfo(comp_desc_str, &player_rec.client_platform, player_rec.client_version);
		// modify this a bit to have our new serial number
		char serial_number_str[20];
		zstruct(serial_number_str);
		sprintf(serial_number_str, "p%x", player_rec.last_login_computer_serial_nums[0] & SERIAL_NUMBER_OBFUSCATION);
		char *p = strstr(comp_desc_str, "s/n");
		if (p) {
			sprintf(p, "s/n: %s", serial_number_str);	// at the end of the description block
		}
		// build the email
		sprintf(aib.info,
			//       10        20        30        40        50        60        70  v     80
			"Attn:\r\n"
			"%s\r\n"
			"\r\n"
			"Account: %s\r\n"
			"\r\n"
			"Dear %s,\r\n"
			"\r\n"
			"We have received a notice from our credit card processor that you have "
			"requested the charges you made on your credit card (below) are to be "
			"reversed:\r\n"
			"\r\n"
			"%s"
			"\r\n"
			"We wish to clarify this situation immediately as credit card fraud is a "
			"criminal offence and we will pursue the matter vigilantly.\r\n"
			"\r\n"
			"This is obviously a very serious matter. Our records indicate that you "
			"logged into the site on many occasions from the following IP addresses "
			"and computer:\r\n"
			"\r\n"
			"%s"
			"\r\n"
			"%s\r\n"
			"\r\n"
			"We point out that your machine leaves a very specific signature which "
			"has been assigned to you specifically, in this case %s. "
			"This signature, along with your specific IP address, "
			"are two very specific identifying factors which have been used in the past to "
			"successfully retrieve the funds from the credit card company.\r\n"
			"\r\n"
			"We trust and hope there was some sort of misunderstanding or error that caused "
			"this request to reverse this charge.  Perhaps you were unaware that your charges "
			"would appear as WWW.FIRECASH.COM/41825 PORT OF SPAIN or as %s. In this case we "
			"will obviously cease our investigation towards this matter, which in essence is "
			"tantamount to credit card fraud.\r\n"
			"\r\n"
			"Please reply immediately as to the status of this situation. As I'm sure you "
			"understand, we have had to restrict access to your account while we investigate "
			"this matter.\r\n"
			"\r\n"
			"DesertPoker Security\r\n"
			"\r\n",
			address_block,
			player_rec.user_id,
			player_rec.full_name,
			transaction_block,
			connection_block,
			comp_desc_str,
			serial_number_str,
			CCChargeName
		);
		NOTUSED(admin_player_id);
		SendDataStructure(DATATYPE_ADMIN_INFO_BLOCK, &aib, sizeof(aib));
	}
}

/**********************************************************************************
 Function Player::ProcessAdminReqAllInLetter()
 date: 24/01/01 kriskoin Purpose: generate a set of all-in letters about this player to send to admin client
***********************************************************************************/
void Player::ProcessAdminReqAllInLetter(WORD32 admin_player_id, WORD32 player_id)
{
	struct AdminInfoBlock aib;
	zstruct(aib);
	SDBRecord player_rec;
	zstruct(player_rec);
	char all_ins[ALLINS_TO_RECORD_PER_PLAYER*30];
	zstruct(all_ins);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		sprintf(aib.info, "ERROR: %08lx not found trying to generate all-in text\r\n", player_id);
	} else {
		// build the all-ins detail block
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
			sprintf(all_ins+strlen(all_ins), "%11.11s (%s)\r\n",
					TimeStr(player_rec.all_in_times[i], FALSE, TRUE, SERVER_TIMEZONE), connection_state);
		}
		// build the email block
		sprintf(aib.info, // first, the negative response
			//       10        20        30        40        50        60        70  v     80
			"**************************************************************************\r\n"
			"****** ALL-IN RESPONSE - NEGATIVE - FILL IN PLAYER NAME, LOOK OVER ALL-INS\r\n"
			"**************************************************************************\r\n"
			"\r\n"
			"Hello,\r\n"
			"\r\n"
			"After reviewing the hand, we've found that there was no wrong-doing.\r\n"
			"\r\n"
			"We take all of these allegations very seriously and diligently examine "
			"every detail.  In this case, '%s' had a bad connection.  The thing "
			"about a bad connection is that it is very difficult to fake.  When someone "
			"has a good connection and goes all-in, it's obvious.  When someone "
			"disconnects, that can be suspicious as well (hanging up on purpose). However, "
			"a bad connection (delays, latency, etc) are very difficult to make happen on "
			"purpose.\r\n"
			"\r\n"
			"%s's connection status:\r\n"
			"\r\n"
			"Real Money All-ins (CST):\r\n"
			"%s\r\n"
			"It seems, genuinely, that '%s' had a bad connection and it affected "
			"him at an untimely moment.\r\n"
			"\r\n"
			"We thank you for the report; we appreciate it, as keeping the games honest "
			"is our highest priority.\r\n"
			"\r\n"
			"Regards,\r\n"
			"\r\n"
			"DesertPoker Security\r\n"
			"\r\n"
			"\r\n",
			player_rec.user_id,
			player_rec.user_id,
			all_ins,
			player_rec.user_id
			);
		sprintf(aib.info+strlen(aib.info), // now, the positive response
			//       10        20        30        40        50        60        70  v     80
			"**************************************************************\r\n"
			"***** ALL-IN RESPONSE - POSITIVE - FILL IN PLAYER NAME, GAME #\r\n"
			"**************************************************************\r\n"
			"\r\n"
			"Hello \r\n"
			"\r\n"
			"We have investigated the time out of '%s' in game #*****.\r\n"
			"\r\n"
			"We agree that timing out all-in under these circumstances appears suspicious.\r\n"
			"\r\n"
			"We internally monitor players' actions with respect to collusion and all-in "
			"abuses. We also review and collect complaints filed by other players, such "
			"as yourself, regarding \"suspicious\" activities committed by some players. "
			"We keep all suspicious activities logged in a notes section on their account. "
			"A player that suffers a repeat occurrence faces being permanently banned from "
			"the games, as the integrity of our games is of paramount importance to us.\r\n"
			"\r\n"
			"'%s' did not have any previous all-ins, and we have added the details of his "
			"all-in to his file.  He has been reminded of our strict policy by email.\r\n"
			"\r\n"
			"His all-in was suspicious but obviously can not be proven 100%% "
			"intentional. While this was his first apparent intentional all-in, we "
			"consider this absolutely unacceptable. If he repeats this action, we will "
			"have the details of the hand you were involved in as further evidence and "
			"will take more drastic action if necessary.\r\n"
			"\r\n"
			"We thank you for the report; we appreciate it, as keeping the games honest "
			"is our highest priority.\r\n"
			"\r\n"
			"Regards,\r\n"
			"\r\n"
			"DesertPoker Security\r\n"
			"\r\n",
			player_rec.user_id,
			player_rec.user_id
			);
		sprintf(aib.info+strlen(aib.info), // now, letter to violator
			//       10        20        30        40        50        60        70  v     80
			"**********************************************************\r\n"
			"******  LETTER TO VIOLATOR - FILL IN GAME #, CHECK ALL-INS\r\n"
			"**********************************************************\r\n"
			"\r\n"
			"Hello %s,\r\n"
			"\r\n"
			"We have received a report from another player regarding a \"suspicious\" "
			"all-in that involved you timing-out in game #xxxx. Our statistics show "
			"that you had a good connection to the game site at the time.\r\n"
			"\r\n"
			"Real Money All-ins (CST):\r\n"
			"%s"
			"\r\n"
			"We trust you agree on the surface the timing of the all-in can be "
			"construed as suspicious.\r\n"
			"\r\n"
			"To ensure the integrity of our games for all players, including yourself, "
			"we monitor all-in timeouts very closely of all players.  Please understand "
			"we consider such violations to be quite serious.  If intentional, all-ins "
			"are a form of cheating which not only can rob winners of extra bets but, "
			"if intentional, can allow players to render potentially successful bluffs "
			"entirely impotent. Whether intentional or not, they ruin the integrity of "
			"the game for all of the players at the table. A repeat occurrence by any "
			"player may result in being banned from the games.\r\n"
			"\r\n"
			"We await your reply.\r\n"
			"\r\n"
			"Regards,\r\n"
			"\r\n"
			"DesertPoker Security\r\n"
			"\r\n",
			player_rec.full_name,
			all_ins
			);
		}
	}
	// ship it to the admin client	
	SendDataStructure(DATATYPE_ADMIN_INFO_BLOCK, &aib, sizeof(aib));
	NOTUSED(admin_player_id);
}

/**********************************************************************************
 Function ProcessAdminReqPlayerInfo()
 date: 24/01/01 kriskoin Purpose: run 'playerinfo' (which will email results to the admin that requested it)
***********************************************************************************/
void Player::ProcessAdminReqPlayerInfo(WORD32 admin_player_id, WORD32 player_id, WORD32 start_date, WORD32 number_of_days, WORD32 email_mask)
{
	SDBRecord admin_player_rec;
	zstruct(admin_player_rec);
	if (SDB->SearchDataBaseByPlayerID(admin_player_id, &admin_player_rec) < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
			_FL, admin_player_id);
		return;
	}
	// convert time to a string we can use
	time_t tt = (time_t)start_date;
	struct tm tm;
	struct tm *t = localtime(&tt, &tm);
	char date_str[10];
	zstruct(date_str);
	sprintf(date_str, "%4d%02d%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday);

	char command_line[500];
	zstruct(command_line);

	// for cosmetic reasons (putting the player's UserID in the email subject line)
	SDBRecord player_rec;
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
			_FL, player_id);
		return;
	}

	// fix up parameters so we don't send bad stuff to the command line
	char _user_id[MAX_PLAYER_USERID_LEN];
	DelimitIllegalCommandLineChars(player_rec.user_id, _user_id, sizeof(_user_id));

	char _admin_email[MAX_EMAIL_ADDRESS_LEN];
	DelimitIllegalCommandLineChars(admin_player_rec.email_address, _admin_email, sizeof(_admin_email));
	
//ccv
#if 1
  sprintf(command_line, "/poker/misc/plrlog \"%x\" \"%s\" \"%d\" \"%s\" \"%s\" \"%s\" &",
		player_id,
		date_str,
		number_of_days,
		(email_mask & MMRI_PLAYERINFO_EMAIL_REQUESTER ? _admin_email : " "),
		(email_mask & MMRI_PLAYERINFO_EMAIL_SUPPORT ? "support@kkrekop.io": " "),
		_user_id);
#else
   //sprintf(command_line, "/poker/misc/plrlog ""3ea"" ""20030422"" ""1"" ""cris7775@hotmail.com"" ""cris@e-mediasoftware.com"" ""cris""");
   strcpy(command_line, "/poker/misc/plrlog 3ea 20030422 1 cris7775@hotmail.com cris@e-mediasoftware.com KAKA2");
#endif
	kp(("%s %s(%d) system call = %s\n", TimeStr(), _FL, command_line));
	int rc = system(command_line);
	if (rc) {
		int errcode = errno;
		Error(ERR_ERROR, "%s(%d) plrinfo()'s system() returned %d/%d -- see DebWin", _FL, rc, errcode);
		kp(("%s(%d) plrinfo()'s system() returned %d  : errno said %d:", _FL, rc,errcode));
		switch (errcode) {
		case E2BIG:
			kp(("E2BIG: Argument list (which is system-dependent) is too big\n"));
			break;
		case ENOENT:
			kp(("ENOENT: Command interpreter cannot be found.\n"));
			break;
		case ENOEXEC:
			kp(("ENOEXEC: Command-interpreter file has invalid format and is not executable.\n"));
			break;
		case ENOMEM:
			kp(("ENOMEM: Not enough memory is available to execute command; or available memory has been corrupted;\n"));
			kp(("        or invalid block exists, indicating that process making call was not allocated properly.\n"));
			break;
		default:
			kp(("*** unknown errno(%d)\n", errcode));
		}
		kp(("%s(%d) Command line passed to system() = %s\n", _FL, command_line));
	  #if !WIN32
		// This code was only tested for Linux.  I have no idea
		// what it might do under Windows.
		if (rc==-1) {
			// The problem is quite serious and probably indicates
			// corrupted memory on the server.
			// Alert any administrators of the problem.
			IssueCriticalAlert("plrlog failed with return code = -1. Memory might be corrupted.");
		}
	  #endif
	}
}

/**********************************************************************************
 Function Player::ProcessSendXferEmail()
 date: 24/01/01 kriskoin Purpose: handle a request to send a transfer email to players where $ was transferred to/from
***********************************************************************************/
ErrorType Player::ProcessSendXferEmail(struct MiscClientMessage *mcm)
{
	WORD32 player_id_from = mcm->misc_data_1;
	WORD32 player_id_to = mcm->misc_data_2;
	WORD32 amount = mcm->misc_data_3;
	WORD32 email_flags = mcm->misc_data_4;

	int send_email_from = email_flags & XFER_EMAIL_FROM;
	int send_email_to = email_flags & XFER_EMAIL_TO;

	SDBRecord player_rec_from;
	zstruct(player_rec_from);
	if (SDB->SearchDataBaseByPlayerID(player_id_from, &player_rec_from) < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
			_FL, player_id_from);
		return ERR_ERROR;
	}
	SDBRecord player_rec_to;
	zstruct(player_rec_to);
	if (SDB->SearchDataBaseByPlayerID(player_id_to, &player_rec_to) < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
			_FL, player_id_to);
		return ERR_ERROR;
	}
	// the last transactions on record for both must be transfers, otherwise it may not have gotten through...
	if (player_rec_from.transaction[0].transaction_type != CTT_TRANSFER_TO) {
		Error(ERR_ERROR, "%s(%d) Should have found last transaction for %08lx to be TRANSFER_FROM but it was %d -- see src",
			_FL, player_id_from, player_rec_from.transaction[0].transaction_type);
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
			"There was a problem sending the XFER email -- check both accounts' last transaction\n");
		return ERR_ERROR;
	}
	if (player_rec_to.transaction[0].transaction_type != CTT_TRANSFER_FROM) {
		Error(ERR_ERROR, "%s(%d) Should have found last transaction for %08lx to be TRANSFER_TO but it was %d -- see src",
			_FL, player_id_to, player_rec_from.transaction[0].transaction_type);
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
			"There was a problem sending the XFER email -- check both accounts' last transaction\n");
		return ERR_ERROR;
	}
	// looks good to send both emails
	char str[1000];
	zstruct(str);
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	if (send_email_from) {
		// build the string and ship it
		sprintf(str,
			"%sDear %s,\n"
			"\n"
			"As per your request, we have now completed the transfer of %s\n"
			"from your account (%s) to the account (%s).\n"
			"\n"
			"Your account balance before the transfer: %s\n"
			"Your account balance after the transfer: %s\n"
			"\n"
			"If you have any questions regarding this transaction,\n"
			"please contact us at cashier@kkrekop.io\n"
			"\n"
			"Best regards,\n"
			"\n"
			"DesertPoker Cashier\n"
			"\n",
			(iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n"),
			player_rec_from.full_name,
			CurrencyString(curr_str1, amount, CT_REAL, TRUE),
			player_rec_from.user_id,
			player_rec_to.user_id,
			CurrencyString(curr_str2, (player_rec_from.real_in_bank+player_rec_from.real_in_play)+amount, CT_REAL, TRUE),
			CurrencyString(curr_str3, player_rec_from.real_in_bank+player_rec_from.real_in_play, CT_REAL, TRUE));
		// ship the email
		char subject[80];
		zstruct(subject);
		sprintf(subject, "Transfer to %s", player_rec_to.user_id);
		EmailStr(
			player_rec_from.email_address, 		// to:
			"DesertPoker Cashier",			// from (name):
			"cashier@kkrekop.io",		// from (email):
			subject,							// subject
			"answers@kkrekop.io",		// bcc:
			"%s", str);
	}
	if (send_email_to) {
		// build the string and ship it
		sprintf(str,
			"%sDear %s,\n"
			"\n"
			"We have now completed the transfer of %s to your account (%s).\n"
			"This was received from account (%s).\n"
			"\n"
			"Your account balance before the transfer: %s\n"
			"Your account balance after the transfer: %s\n"
			"\n"
			"If you have any questions regarding this transaction,\n"
			"please contact us at cashier@kkrekop.io\n"
			"\n"
			"Best regards,\n"
			"\n"
			"DesertPoker Cashier\n"
			"\n",
			(iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n"),
			player_rec_to.full_name,
			CurrencyString(curr_str1, amount, CT_REAL, TRUE),
			player_rec_to.user_id,
			player_rec_from.user_id,
			CurrencyString(curr_str2, (player_rec_from.real_in_bank+player_rec_from.real_in_play)-amount, CT_REAL, TRUE),
			CurrencyString(curr_str3, player_rec_from.real_in_bank+player_rec_from.real_in_play, CT_REAL, TRUE));
		// ship the email
		char subject[80];
		zstruct(subject);
		sprintf(subject, "Transfer from %s", player_rec_from.user_id);
		EmailStr(
			player_rec_to.email_address, 		// to:
			"DesertPoker Cashier",			// from (name):
			"cashier@kkrekop.io",		// from (email):
			subject,							// subject
			"answers@kkrekop.io",		// bcc:
			"%s", str);
	}
	NOTUSED(amount);
	return ERR_NONE;
}


/**********************************************************************************
 Function Player::ProcessSendCreditLimitEmail(struct MiscClientMessage *mcm)
 date: 24/01/01 kriskoin Purpose: send email to account outlining current credit limits
***********************************************************************************/
ErrorType Player::ProcessSendCreditLimitEmail(struct MiscClientMessage *mcm)
{
	NOTUSED(mcm);
	return ERR_NONE;
}
