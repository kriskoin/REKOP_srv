//*********************************************************
//
//	Player routines.
//	Server side only.
//
// 
//
//*********************************************************


#define DEBUG_SOCKET_TIMEOUTS	0	// turn on to display extra info to help debug socket timeout problems
#define DISP 0

#include <libpq-fe.h> //Connect to Postgres
#include <stdio.h>
#include <fcntl.h>
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

#define CASE_SENSITIVE_PASSWORDS	1	// make passwords case sensitive?

int TotalPlayerObjects;	// debug: # of player objects that are currently constructed.



/******J. Fonseca   22/10/2003*******/
char sql[512];   //query
char buf[256];  //query
/******J. Fonseca   22/10/2003*******/


static volatile unsigned int NextPlayerObjectSerialNum;

// kriskoin 2018/07/07extern HighHand hihand_real[2];
extern HighHand hihand_play[2];
// end kriskoin 

//****************************************************************
// 
//
// Constructor/destructor for the Player object
//
Player::Player(void *calling_cardroom_ptr)
{
	serial_num = NextPlayerObjectSerialNum++;	// used for determining which I/O thread to use.
	cardroom_ptr = calling_cardroom_ptr;
	player_id = 0;
	priv = 0;
	user_id[0] = City[0] = 0;
	ip_str[0] = 0;
	LoginStatus = LOGIN_NO_LOGIN;
	server_socket = NULL;
	socket_set_time = 0;
	next_send_attempt_ms = 0;	// minimum GetTickCount() when we should try ProcessSendQueue() again.
	disconnect_message_printed = 0;
	send_account_info = FALSE;
	send_client_info = FALSE;
	char str[100];
	sprintf(str, "Player %d", serial_num);
	PPInitializeCriticalSection(&PlayerCritSec, CRITSECPRI_PLAYER, str);
	last_input_processing_ticks = 0;
	input_result_ready_table_serial_number = 0;
	zstruct(client_version_number);
	zstruct(client_platform);
	zstruct(last_keep_alive);
	zstruct(PingResults);
	zstruct(PingSendTimes);
	zstruct(saved_seat_avail);
	for (int i=0 ; i<PLAYER_PINGS_TO_RECORD ; i++) {
		PingResults[i].time_of_ping = SecondCounter;
	}
	time_of_last_received_packet = 0;
	time_of_next_expected_packet = SecondCounter;
	last_timeout_seconds = 0;
	zstruct(last_timeout_ip_str);
	idle_flag = 0;
	JoinedTableCount = 0;
	memset(JoinedTables, 0, sizeof(JoinedTables[0])*MAX_GAMES_PER_PLAYER);
	memset(requesting_table_summary_list, 0, sizeof(requesting_table_summary_list[0])*MAX_CLIENT_DISPLAY_TABS);
	table_info_request_1 = 0;
	table_info_request_2 = 0;
	table_info_subscription = 0;
	pending_joins_count = 0;
	memset(pending_joins, 0, sizeof(pending_joins[0])*MAX_GAMES_PER_PLAYER);
	pending_waitlist_count = 0;
	memset(pending_waitlists, 0, sizeof(pending_waitlists[0])*MAX_GAMES_PER_PLAYER);
	TotalPlayerObjects++;
	pr(("%s(%d) Player constructor has been called.  There should now be %d player objects in memory.\n",_FL,TotalPlayerObjects));
	RealInBank = 0;
	RealInPlay = 0;
	FakeInBank = 0;
	FakeInPlay = 0;
	EcashCredit = 0;
	CreditFeePoints = 0;
	Gender = GENDER_UNKNOWN;
	last_admin_stats_sent = 0;
	player_io_disabled = 0;
	old_AllInsAllowed   = AllInsAllowed;	// our old copy of the global
	old_AllInsResetTime = AllInsResetTime;	// our old copy of the global
	old_GoodAllInsAllowed = GoodAllInsAllowed;	// our old copy of the global
	got_login_packet = FALSE;				// set if we have received a login packet of any sort.
	next_login_packet_check = SecondCounter + 10;	// SecondCounter when we should next check the got_login_packet flag
	sent_login_alert_flag = 0;			// set if we've sent a login alert already
	chat_disabled = 0;					// set if chat disabled due to related account chat squelch
	chat_queue_head = 0;
	chat_queue_tail = 0;
	waiting_for_input = FALSE;
	last_low_latency_update_processing_ticks = 0;	// GetTickCount() when cardroom thread last updated us
	last_high_latency_update_processing_ticks = 0;	// GetTickCount() when cardroom thread last updated us
	update_now_flag = 0;				// set when we want the cardroom to update us asap
	suppress_tournament_email_serial_number = 0;// serial number of tournament we don't want email from
	tournament_table_serial_number = 0;	// table_serial_number of any tournament tables we're SEATED at.

	admin_set_player_id_for_cc = 0;				// if set, player id to make active for a cc
	admin_set_cc_number = 0;					// partial cc number for admin_set_player_id_for_cc
	admin_move_player_to_top_wait_lists = 0;	// if set, player id to move to top of waiting lists.
	admin_set_max_accounts_for_cc = 0;			// if set, player id whose cc's to find and set max usage for
	admin_set_max_accounts_for_cc_count = 0;	// count to use for above request

  #if ACCEPT_CLIENT_SHUTDOWN_REQUESTS && !WIN32
  	kp1(("%s(%d) Warning: this server accepts shutdown requests from administrator clients.\n",_FL));
  #endif // ACCEPT_CLIENT_SHUTDOWN_REQUESTS
}

Player::~Player(void)
{
	// 24/01/01 kriskoin:
	// We should flag it as an error and move it back to the bank.
	if (player_id && !ANONYMOUS_PLAYER(player_id) &&
				iRunLevelDesired > RUNLEVEL_EXIT)	// don't execute if we got killed from command line
	{
		for (int i=0; i<3 ; i++) {
			ChipType chip_type = CT_NONE;
			if (i == 0) {
				chip_type = CT_PLAY;
			} else if (i == 1) {
				chip_type = CT_REAL;
			} else if (i == 2) {
				chip_type = CT_TOURNAMENT;
			}
			int chips_in_play = SDB->GetChipsInPlayForPlayerID(player_id, chip_type);
			if (chips_in_play) {
				char game_type_str[20];
				zstruct(game_type_str);
				switch (chip_type) {
					case CT_NONE:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
						break;
					case CT_PLAY:
						sprintf(game_type_str,"play");
						break;
					case CT_REAL:
						sprintf(game_type_str,"real");
						break;
					case CT_TOURNAMENT:
						sprintf(game_type_str,"tournament");
						break;
					default:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
						break;
				}
				Error(ERR_ERROR, "%s(%d) Player destructor: Found %d %s chips in play for player %s. Moving back to bank.",
						_FL, chips_in_play,
						game_type_str,
						user_id);
				int new_chips_in_bank = SDB->GetChipsInBankForPlayerID(player_id, chip_type) + chips_in_play;
				SDB->SetChipsInBankForPlayerID(player_id, new_chips_in_bank, chip_type);
				SDB->SetChipsInPlayForPlayerID(player_id, 0, chip_type);
				switch (chip_type) {
					case CT_NONE:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
						break;
					case CT_PLAY:
						FakeInBank = new_chips_in_bank;
						FakeInPlay = 0;
						break;
					case CT_REAL:
						RealInBank = new_chips_in_bank;
						RealInPlay = 0;
						break;
					case CT_TOURNAMENT:
						// unhandled for now
						break;
					default:
						Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
						break;
				}
			}
		}
	}

	if (server_socket) {
		// The ServerSocket is still around... delete it.
		delete server_socket;
		server_socket = NULL;
	}
	PPDeleteCriticalSection(&PlayerCritSec);
	zstruct(PlayerCritSec);
	TotalPlayerObjects--;
	pr(("%s(%d) Player destructor has been called.  There should now be %d player objects in memory.\n",_FL,TotalPlayerObjects));
}

//****************************************************************
// 
//
// Read any incoming packets from the client and deal with them
// appropriately.  This function must be called periodically by
// some higher level object such as the CardRoom.
// It's quite possible (or even likely) this will be called from
// a completely separate thread than the rest of the Player() stuff.
//
ErrorType Player::ReadIncomingPackets(void)
{
  #if 0	//kriskoin: 	if (!server_socket) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) ReadIncomingPackets(): no server socket defined.",_FL);
		return ERR_INTERNAL_ERROR;
	}
  #endif

	//kp(("%s(%d) Stack crawl from top of ReadIncomingPackets()...\n",_FL)); DisplayStackCrawl();
	//kp(("%s(%d) Player %d: ReadIncomingPackets()\n", _FL, player_id));
	// Loop forever or until we lose our server socket or we run out of data.
	while (server_socket) {
		//kriskoin: 		if (!server_socket->connected_flag && server_socket->sock != INVALID_SOCKET) {	// ever connected?
			// We've never connected... check how long we've been trying.
			long elapsed = SecondCounter - server_socket->time_of_connect;
//			kp(("%s(%d) %ds elapsed so far setting up socket.\n", _FL, elapsed));
			if (elapsed >= 40) {
				ConnectionLog->Write(
						"%s %-15.15s socket setup for socket $%04x took too long (%ds). Disconnecting.\n",
						TimeStr(), ip_str, server_socket->sock, elapsed);
			  #if !WIN32 && 0	// 2022 kriskoin
			  	kp(("%s %-15.15s %s(%d) Calling sync() before closing socket $%04x\n", TimeStr(), ip_str, _FL, server_socket->sock));
				sync();	// make sure all inodes and buffers are written to disk before closing
			  	kp(("%s %-15.15s %s(%d) Back from sync()... calling CloseSocket()\n", TimeStr(), ip_str, _FL));
			  #endif
				server_socket->CloseSocket();
				//kp(("%s %-15.15s %s(%d) Back from CloseSocket() call.\n", TimeStr(), ip_str, _FL));
			};//if elapsed time
		}; //if server_sockets

		//kriskoin: 		// message from the client, give up.  Print messages along the
		// way as well so we can see the progress.
		if (!got_login_packet && SecondCounter >= next_login_packet_check && Connected()) {
			next_login_packet_check = SecondCounter + 10;
			WORD32 elapsed = SecondCounter - server_socket->time_of_connect;
			ConnectionLog->Write(
					"%s %-15.15s player object: no login packet received from socket $%04x yet (%ds). %d bytes in %d packets received so far.\n",
					TimeStr(), ip_str, server_socket->sock, elapsed,
					server_socket->total_bytes_received,
					server_socket->total_packets_received);
			if (elapsed >= 40) {
				// time to give up.
				ConnectionLog->Write(
						"%s %-15.15s player object didn't receive login packet from socket $%04x in %ds.  Disconnecting.\n",
						TimeStr(), ip_str, server_socket->sock, elapsed);
			  #if !WIN32 && 0	// 2022 kriskoin
			  	kp(("%s %-15.15s %s(%d) Calling sync() before closing socket $%04x\n", TimeStr(), ip_str, _FL, server_socket->sock));
				sync();	// make sure all inodes and buffers are written to disk before closing
			  #endif
				server_socket->CloseSocket();
				//kp(("%s %-15.15s %s(%d) Back from CloseSocket() call.\n", TimeStr(), ip_str, _FL));
			}   ;//if elapsed >=40
		}  ;//if !got_login_packet && SecondCounter >= next_login_packet_check && Connected()

		EnterCriticalSection(&PlayerCritSec);
		if (!disconnect_message_printed && !Connected()) {
			// We've detected an unexpected disconnection.  Report it.
			WORD32 disconnect = SecondCounter;
			if (server_socket->time_of_disconnect) {
				disconnect = server_socket->time_of_disconnect;
			};//if
			WORD32 elapsed = disconnect - server_socket->time_of_connect;
			if (DebugFilterLevel <= 2 && !(client_version_number.flags & VERSIONFLAG_SIMULATED)) {
			  #if 1	// 2022 kriskoin
				kp(("%s %-15.15s Disconn %-15s Last Contact %ds\n",
						TimeStr(), ip_str, user_id, TimeSinceLastContact()));
			  #else
				kp(("%s %-15.15s Disconn %-15s $%08lx Duration %dm\n",
						TimeStr(), ip_str, user_id, player_id, elapsed/60));
			  #endif
			};//if
			ConnectionLog->Write(
					"%s %-15.15s Disconn '%s' %06lx sock %04x Duration %dm TimeSinceLastContact %ds\n",
					TimeStr(), ip_str, user_id, player_id, server_socket->sock, elapsed/60, TimeSinceLastContact());
			disconnect_message_printed = TRUE;
		};//if
		if (!server_socket) {
			LeaveCriticalSection(&PlayerCritSec);
			break;	// no socket left.  Leave this loop.
		} ;//if
		Packet *p;
		ErrorType err = server_socket->ReadPacket(&p);	// read next packet from input queue
		if (err != ERR_NONE) {
			LeaveCriticalSection(&PlayerCritSec);
			return err;
		};//if
		if (p && player_io_disabled) {	// i/o with player is disabled... throw out packet.
			PktPool_ReturnToPool(p);
			p = NULL;
		};//if

		if (!p) {
			LeaveCriticalSection(&PlayerCritSec);
			break;	// we've caught up.  Leave this loop.
		};//if
		// A packet has arrived... deal with it.
		RNG_AddToEntropyUsingRDTSC();	// any time a packet arrives, add to the entropy a little bit
	  #if 0
		kp(("%s(%d) Here's the packet which has arrived:\n",_FL));
		khexdump(p->user_data_ptr, p->user_data_length, 16, 1);
	  #endif
		// Calculate pointers to the two data regions...
		struct DataPacketHeader *hdr = (struct DataPacketHeader *)p->user_data_ptr;

		if (hdr->data_packet_type < DATATYPE_COUNT) {
		  #if 0	// 2022 kriskoin
			kp(("%s(%d) type %2d: compressed = %4d + %d, uncompressed = %4d (%4d + %d), user_data_length = %d, sizeof(struct VersionNumber)=%d\n",

				_FL, hdr->data_packet_type, p->length_when_received, TCP_HEADER_OVERHEAD_AMOUNT,
				p->length + TCP_HEADER_OVERHEAD_AMOUNT,
				p->length, TCP_HEADER_OVERHEAD_AMOUNT,
				p->user_data_length, sizeof(struct VersionNumber)));
		  #endif

			CurrentPacketStats[hdr->data_packet_type].rcvd_count++;
			CurrentPacketStats[hdr->data_packet_type].bytes_rcvd += p->length_when_received + TCP_HEADER_OVERHEAD_AMOUNT;
			CurrentPacketStats[hdr->data_packet_type].bytes_rcvd_uncompressed += p->length + TCP_HEADER_OVERHEAD_AMOUNT;
		};//if

		// Verify the length of the data.
		if ((int)(hdr->data_packet_length + sizeof(*hdr)) != p->user_data_length) {
			Error(ERR_ERROR, "%s(%d) Packet data length fields don't match (%d vs %d) from player_id $%08lx.  Disconnecting player.",
					_FL, hdr->data_packet_length + sizeof(*hdr), p->user_data_length, player_id);
		  #if 1
			kp(("%s(%d) %d bytes received so far.  Here's the packet we're going to ignore: (%d bytes)\n",
					_FL,server_socket->total_bytes_received, p->user_data_length));
			khexdump(p->user_data_ptr, p->user_data_length, 16, 1);
		  #endif
			ProcessPlayerLogoff();
			delete p;
			p = NULL;
			LeaveCriticalSection(&PlayerCritSec);
			continue;
		};//if length of the data
		LeaveCriticalSection(&PlayerCritSec);

		time_of_last_received_packet = SecondCounter;	// keep track of when we last read something from this player
		time_of_next_expected_packet = SecondCounter + CLIENT_KEEP_ALIVE_SPACING;
		if (last_timeout_seconds) {
			if (DebugFilterLevel <= 4) {
				kp(("%s %-15.15s %s was heard from %ds after a timeout (%s)\n",
						TimeStr(), ip_str, user_id, SecondCounter - last_timeout_seconds,
						strcmp(ip_str, last_timeout_ip_str) ? "Different IP" : "same IP"));
			};// if debug filter lever <=4
			AddToLog("Data/Logs/timeout.log", "",
					"%s\t\t%s\tHeard from again\t%d\t%s\n",
					TimeStr(), user_id, SecondCounter - last_timeout_seconds,
					strcmp(ip_str, last_timeout_ip_str) ? "Different IP" : "same IP");
			last_timeout_seconds = 0;
			zstruct(last_timeout_ip_str);
		} ;//if last_timeout_seconds

		void *data_ptr = (void *)((char *)p->user_data_ptr+sizeof(*hdr));

    int packet_type = hdr->data_packet_type;
		switch (hdr->data_packet_type) {
		//cris july 30 2003
		case DATATYPE_ADMIN_ADD_ROBOT:
			printf("\n%s\n","struct received") ;
			AddRobotTable((struct TableInfoRobot*)data_ptr,hdr->data_packet_length);
			break;	
        // end cris july 30 20003			
		case DATATYPE_ACCOUNT_RECORD:
			ProcessAccountRecord((struct AccountRecord *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_ADMIN_CHECK_RUN:
			ProcessAdminCheckRun((struct AdminCheckRun *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_PLAYER_INPUT_RESULT:
			ProcessInputResult((struct GamePlayerInputResult *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CHAT_MSG:
			ProcessClientChatMessage((struct GameChatMessage *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_ERROR_STRING:
			ProcessClientErrorString((struct ClientErrorString *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CLIENT_PLATFORM:
			ProcessClientPlatform((struct ClientPlatform *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CLIENT_STATE_INFO:
			ProcessClientStateInfo((struct ClientStateInfo *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CLOSING_CONNECTION:
			ProcessConnectionClosing((struct ConnectionClosing *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CREDIT_CARD_TRANSACTION:
			ProcessCreditCardTransaction((struct CCTransaction *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CARDROOM_REQ_CC_STATEMENT:
			ProcessStatementRequest((struct CCStatementReq *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_KEEP_ALIVE:
			// No processing to do.
			pr(("%s(%d) Received KEEP_ALIVE message.\n",_FL));
			break;
		case DATATYPE_KEEP_ALIVE2:
			ProcessKeepAlive((struct KeepAlive *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_MISC_CLIENT_MESSAGE:
			ProcessMiscClientMessage((struct MiscClientMessage *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_PING:
			ProcessPing((struct Ping *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_PLAYER_LOGIN_REQUEST:
			ProcessPlayerLoginRequest((struct PlayerLoginRequest *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_PLAYER_LOGOFF:
			ProcessPlayerLogoff();
			break;
		case DATATYPE_VERSION_NUMBER:
			ProcessVersionNumber((struct VersionNumber *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_TABLE_INFO:
		//	printf("\n%s\n","Request to add table received.") ;
			AddTable((struct TableInfo*)data_ptr,hdr->data_packet_length);
//			AddRobotTable((struct TableInfoRobot*)data_ptr,hdr->data_packet_length);
 			break;
		case DATATYPE_CARDROOM_REQUEST_TABLE_LIST:
			{
			//	printf("\n%s\n","Request to Update Table List Received.") ;// from CardRoom client: request table summary list for a particular game type
				if (VerifyIncomingPacketFormat(p, sizeof(struct CardRoom_RequestTableSummaryList)) != ERR_NONE) {
					break;
				}
				struct CardRoom_RequestTableSummaryList *s = (struct CardRoom_RequestTableSummaryList *)data_ptr;

				// Always pass the entropy from the client to the rng.
				RNG_AddToEntropy(s->random_bits);

				if (s->client_display_tab_index >= MAX_CLIENT_DISPLAY_TABS) {
					Error(ERR_ERROR, "%s(%d) Illegal display tax index in CardRoom_RequestTableSummaryList (%d)",_FL,s->client_display_tab_index);
					break;
				}
				requesting_table_summary_list[s->client_display_tab_index] = TRUE;	// flag that we want it sent.
				update_now_flag = TRUE;	// request the cardroom try to update us asap
			}
			break;
		
		case DATATYPE_CARDROOM_REQ_HAND_HISTORY:	// req to generate a hand history
			ProcessReqHandHistory((struct CardRoom_ReqHandHistory *)data_ptr, hdr->data_packet_length);
			break;

		case DATATYPE_CARDROOM_REQUEST_TABLE_INFO:
			// from CardRoom client: request info about a particular table
			{
				// from CardRoom client: request table summary list for a particular game type
				if (VerifyIncomingPacketFormat(p, sizeof(struct CardRoom_RequestTableInfo)) != ERR_NONE) {
					break;
				}
				struct CardRoom_RequestTableInfo *s = (struct CardRoom_RequestTableInfo *)data_ptr;
				pr(("%s(%d) Got CardRoom_RequestTableInfo for table %d\n", _FL, s->table_serial_number));
				if (s->subscribe_flag) {
					// This is a subscription...
					table_info_subscription = s->table_serial_number;
					// If they subscribed, we also want to send out ASAP, so
					// add it to the regular request queue.
				}
				// this is a single request.
				if (!table_info_request_1 || table_info_request_1==s->table_serial_number) {
					table_info_request_1 = s->table_serial_number;
				} else if (!table_info_request_2 || table_info_request_2==s->table_serial_number) {
					table_info_request_2 = s->table_serial_number;
				} else {
					//Error(ERR_WARNING, "%s(%d) No room to store table_info_request.", _FL);
					// If we didn't have room, the use is probably just scrolling
					// around in the window, therefore it's perfectly safe to overwrite
					// one of the previous requests.
					table_info_request_1 = s->table_serial_number;
				}
				update_now_flag = TRUE;	// request the cardroom try to update us asap
			}
			break;
		case DATATYPE_CARDROOM_JOIN_TABLE:
			// CardRoom: request to join a table or be told you've been joined
			// to a table (depending on whether you're the client or the server).
			ProcessJoinTableRequest((struct CardRoom_JoinTable *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_CARDROOM_JOIN_WAIT_LIST:
			// CardRoom: request to join a waiting list or be told you've been joined
			// to a waiting list (depending on whether you're the client or the server).
			ProcessJoinWaitListRequest((struct CardRoom_JoinWaitList *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_SHUTDOWN_MSG:
			ProcessClientShutdownRequest();
			break;
		case DATATYPE_TRANSFER_REQUEST:
			ProcessTransferRequest((struct TransferRequest *)data_ptr, hdr->data_packet_length);
			break;
		case DATATYPE_SHOTCLOCK_UPDATE:
			ProcessShotclockUpdate((struct ShotClockUpdate *)data_ptr, hdr->data_packet_length);
			break;
			
		//cris 14-1-2004	
		case DATATYPE_MONEY_TRANSFER:
			ProcessMoneyTransfer((struct MoneyTransaction*)data_ptr,hdr->data_packet_length);
			break;
		//end cris 14-1-2004
		
		default:
			Error(ERR_INTERNAL_ERROR, "%s(%d) Unknown or unhandled data packet type (%d) from player_id $%08lx.  Disconnecting player.",
					_FL, hdr->data_packet_type, player_id);
			ProcessPlayerLogoff();
			break;
		};//case

		// If we still have a pointer to the packet then we are responsible
		// for deleting it.
		if (p) {
			PktPool_ReturnToPool(p);
			p = NULL;
		};//if (p)

		if (!client_version_number.major && !client_version_number.minor && !client_version_number.build) {
			// If we get this type of error, it's probably an indication that
			// some of the initial communications with the client are getting lost.
			Error(ERR_WARNING, "%s(%d) Warning: packet type %d received from client but we don't know their version # yet.", _FL, packet_type);
			client_version_number.build = 1;	// don't print this error any more
		};// if (!client_version_number.major && !client_version_number.minor && !client_version_number.build) {
	};//while

	if (Connected()) {
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT &&
					CurrentAdminStats[0].time != last_admin_stats_sent &&
					client_version_number.build >= 0x0106007 // 1.06-7 or later?
		) {
			SendAdminStats();
		};//if      riv >= ACCPRIV_CUSTOMER_SUPPORT &&

		// If the global all-in count has changed, re-send their account
		// info so they know the new all-in count.
		if (old_AllInsAllowed     != AllInsAllowed ||
			old_AllInsResetTime   != AllInsResetTime ||
			old_GoodAllInsAllowed != GoodAllInsAllowed)
		{
			old_AllInsAllowed     = AllInsAllowed;		// our old copy of the global
			old_AllInsResetTime   = AllInsResetTime;	// our old copy of the global
			old_GoodAllInsAllowed = GoodAllInsAllowed;	// our old copy of the global
			//kp(("%s(%d) AllIns allowed seems to have changed.... sending to player %s\n", _FL, user_id));
			send_account_info = TRUE;
		};//if (old_AllInsAllowed     != AllInsAllowed |

		// If anyone flagged to send the account info, do it now.
		if (send_client_info) {
			SendClientInfo();
		};// if (send_client_info)
		if (send_account_info) {
			SendAccountInfo();
		};//if (send_account_info)

		// Send out keep alive's if it has been too long since we've sent something.
		// Client expects to see at least one packet every 10s so that it can
		// monitor network problems.
		if (server_socket && SecondCounter - server_socket->time_of_last_sent_packet >= 10)

		{
		  #if 1	//19991013MB Send a ping rather than a keep alive
			SendPing();
		  #else
			SendDataStructure(DATATYPE_KEEP_ALIVE, NULL, 0);
		  #endif
		};//if (server_socket && SecondCounter - server_socket->time_of_last_sent_packet >= 10)

		// If our connection is not GOOD, send a ping every 5s
		if (CurrentConnectionState() != CONNECTION_STATE_GOOD &&
				SecondCounter - last_ping_sent_time >= 5)
		{
			SendPing();
		};//if (CurrentConnectionState() != CONNECTION_STATE_GOOD &&
	};//if(Connected())
	return ERR_NONE;
};//ErrorType Player::ReadIncomingPackets(void)

//****************************************************************
// 
//
// When we get created we get a unique 32-bit player ID associated with
// us.  This is guaranteed to be unique amongst all players.  This value
// never changes for the life of this object (except when migrating 
// from anonymous to logged in).
//
ErrorType Player::SetPlayerInfo(WORD32 input_player_id)
{
	player_id = input_player_id;
	return ERR_NONE;
}

//****************************************************************
// 
//
// ***24/01/01 kriskoin:
// When a client calls up, they get a ServerSocket. That ServerSocket is
// then used to login and validate a player.  Once that happens, a Player
// object is created (if necessary) or the previous Player object (if they
// are reconnecting) is used.  All future communications with the player
// will go through the player object.  The CardRoom will keep track of
// which Player objects are currently allocated and which 32-bit player_id
// they belong to.  If we previously had a ServerSocket then we should
// destroy it because clearly the user has reconnected from somewhere else.
//
ErrorType Player::SetServerSocket(ServerSocket **input_server_socket)
{
	EnterCriticalSection(&PlayerCritSec);
	if (server_socket) {
		// The old ServerSocket is still around... delete it after flushing
		// data.
		if (!server_socket->SendQueueEmpty()) {	// something in queue?
			server_socket->ProcessSendQueue();	// try to flush the queue
			static int message_count = 0;
			static WORD32 last_message_printed = 0;
			message_count++;
			// Print the warning message at most once per hour
			if (!last_message_printed || SecondCounter >= last_message_printed + 60*60) {
				kp(("%s %s(%d) HACK: sleeping 100ms while changing sockets (%d times). Thread = %s\n",
						TimeStr(), _FL, message_count, GetThreadName()));
				message_count = 0;
				last_message_printed = SecondCounter;
			}
			Sleep(100);	// !!! This is an awful hack but it gets us through a short-term problem.
			server_socket->ProcessSendQueue();
		}
		delete server_socket;
		//kp(("%s(%d) Done deleting server socket.\n",_FL));
		server_socket = NULL;
	}

	if (!input_server_socket) {	// no new one... just close the old one.
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_NONE;
	}
	server_socket = *input_server_socket;
	socket_set_time = SecondCounter;	// SecondCounter when this player object was given the server_socket ptr.
	time_of_last_received_packet = SecondCounter;	// initialize at this point in time.
	time_of_next_expected_packet = SecondCounter + CLIENT_KEEP_ALIVE_SPACING;
	zstruct(PingSendTimes);
	*input_server_socket = NULL;

	// Convert the IP address to a string for display purposes.
	ip_address = server_socket->connection_address.sin_addr.s_addr;
	IP_ConvertIPtoString(ip_address, ip_str, MAX_COMMON_STRING_LEN);


	LeaveCriticalSection(&PlayerCritSec);

  #if DEBUG_SOCKET_TIMEOUTS
	kp(("%s(%d) This code is NOT finished... SO_SNDTIMEO does not seem to be supported under linux.\n",_FL));
	struct timeval send_timeout;
	zstruct(send_timeout);
	int result = getsockopt(server_socket->sock, SOL_SOCKET,
			SO_SNDTIMEO, &send_timeout, sizeof(send_timeout));
	kp(("%s %-15.15s socket set to $%08lx. Current send timeout = %d.%06ds (getsockopt() result = %d)\n",
				TimeStr(), ip_str, server_socket->sock,
				send_timeout.tv_sec, send_timeout.tv_usec, result));
//	struct sock *sock = (struct sock *)server_socket->sock;
  #if 0	// 2022 kriskoin
	kp(("%s %-15.15s sock->",
				TimeStr(), ip_str, server_socket->sock));
  #endif
	
  #endif // DEBUG_SOCKET_TIMEOUTS
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Select a bar snack for a table we're joined to
//
void Player::SelectBarSnack(WORD32 table_serial_number, BAR_SNACK bs)
{
	if (bar_snack_request_table && bar_snack_request_table!=table_serial_number) {
		kp(("%s %-15.15s New bar snack request overwriting an old one.\n",
				TimeStr(), ip_str));
	}
	bar_snack_request = bs;
	bar_snack_request_table = table_serial_number;
}

//*********************************************************
// https://github.com/kriskoin//
// Send all account info and table joined info to a client
// so the client can be brought up to date after a new connection
// or a login.
//
ErrorType Player::SendClientInfo(void)
{
	// If we've got a player_id then we're already logged in.
	// Re-send out data to the newly reconnected client
	SendAccountInfo();	// send account info/chips balance to player

	send_client_info = FALSE;	// it no longer needs resending

	// If the client is new enough, send certain fields of his AccountRecord
	struct AccountRecord ar;
	zstruct(ar);
	struct SDBRecord plr_rec;
	zstruct(plr_rec);


	//if (player_id && !ANONYMOUS_PLAYER(player_id) && client_version_number.build >= 0x01040003) {
  if (player_id && !ANONYMOUS_PLAYER(player_id)){
		int found_player = SDB->SearchDataBaseByPlayerID(player_id, &plr_rec);
	
 	 if (found_player >=0) {	// found it
			// Copy the fields we're allowed to send and then send it to the client
			strnncpy(ar.sdb.user_id,		plr_rec.user_id,		MAX_PLAYER_USERID_LEN);
			strnncpy(ar.sdb.email_address,	plr_rec.email_address,	MAX_EMAIL_ADDRESS_LEN);
  		strnncpy(ar.sdb.full_name,		plr_rec.full_name,		MAX_PLAYER_FULLNAME_LEN);
  		strnncpy(ar.sdb.last_name,		plr_rec.last_name,		MAX_PLAYER_LASTNAME_LEN);
			strnncpy(ar.sdb.city,			plr_rec.city,			MAX_COMMON_STRING_LEN);
			strnncpy(ar.sdb.mailing_address1,			plr_rec.mailing_address1, MAX_PLAYER_ADDRESS_LEN);
			strnncpy(ar.sdb.mailing_address2,			plr_rec.mailing_address2, MAX_PLAYER_ADDRESS_LEN);
			strnncpy(ar.sdb.mailing_address_state,		plr_rec.mailing_address_state, MAX_COMMON_STRING_LEN);
			strnncpy(ar.sdb.mailing_address_country,	plr_rec.mailing_address_country, MAX_COMMON_STRING_LEN);
			strnncpy(ar.sdb.mailing_address_postal_code,plr_rec.mailing_address_postal_code, MAX_COMMON_STRING_LEN);
			strnncpy(ar.sdb.secure_phrase,plr_rec.secure_phrase, MAX_PLAYER_SECURE_PHRASE_LEN);			


			strnncpy(ar.sdb.phone_number, plr_rec.phone_number, PHONE_NUM_LEN);	// J Fonseca   13/02/2004
			strnncpy(ar.sdb.alternative_phone, plr_rec.alternative_phone, PHONE_NUM_LEN);	// J Fonseca   13/02/2004
			strcpy(ar.sdb.user_mi, plr_rec.user_mi);	// J Fonseca   13/02/2004
			strnncpy(ar.sdb.gender1, plr_rec.gender1,7);	// J Fonseca   13/02/2004
			strcpy(ar.sdb.birth_date, plr_rec.birth_date);	// J Fonseca   13/02/2004

			memcpy(ar.sdb.phone_number, plr_rec.phone_number, PHONE_NUM_LEN);	// 20:::			memcpy(ar.sdb.transaction, plr_rec.transaction, sizeof(ClientTransaction) * TRANS_TO_RECORD_PER_PLAYER);
			// 24/01/01 kriskoin:
			if (client_version_number.build < 0x01080005) {	// older client, nuke it
				for (int i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
					if (ar.sdb.transaction[i].transaction_type != CTT_PURCHASE) {
						ar.sdb.transaction[i].credit_left = 0;
					}
				}
			}
			ar.sdb.gender = plr_rec.gender;
			ar.sdb.flags = plr_rec.flags;
			// cc limit overrides
			ar.sdb.cc_override_limit1 = plr_rec.cc_override_limit1;
			ar.sdb.cc_override_limit2 = plr_rec.cc_override_limit2;
			ar.sdb.cc_override_limit3 = plr_rec.cc_override_limit3;

			SendDataStructure(DATATYPE_ACCOUNT_RECORD2, &ar, sizeof(ar));

			// since we fetched these, save them as well.
			strnncpy(user_id, plr_rec.user_id, MAX_PLAYER_USERID_LEN);
			strnncpy(City, plr_rec.city, MAX_COMMON_STRING_LEN);
			Gender = plr_rec.gender;

			// If our chat squelch flag is set in the database, there's no
			// need to keep the override set.
			if (ar.sdb.flags & SDBRECORD_FLAG_SQUELCH_CHAT) {
				chat_disabled = FALSE;	// clear it.
			}
		}
	}


	// Notify the client about tables he has joined.
	struct CardRoom_JoinedTables jt;
	zstruct(jt);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		SendAllTableData(i);
		jt.table_serial_numbers[i] = JoinedTables[i].table_serial_number;
	}

	// Send a complete list of all joined tables.  The client is
	// responsible for closing any tables which aren't in the list.
	SendDataStructure(DATATYPE_CARDROOM_JOINED_TABLES, &jt, sizeof(jt));

	// Send a MiscClientMessage that contains their buy-in limits
	if (client_version_number.build >= 0x000000/*0x01060002*/) {	// client new enough to handle it?
		struct MiscClientMessage mcm;
		zstruct(mcm);
		mcm.message_type = MISC_MESSAGE_CC_BUYIN_LIMITS;

	  #if 0	// 2022 kriskoin
		int purchased_amt_day   = NetPurchasedInLastNHours(player_id, HOURS_IN_DAY);
		int purchased_amt_week  = NetPurchasedInLastNHours(player_id, HOURS_IN_WEEK);
		int purchased_amt_month = NetPurchasedInLastNHours(player_id, HOURS_IN_MONTH);
	  #endif

		char period1[20];
		zstruct(period1);
		if (CCLimit1Days==1) {
			strcpy(period1, "24 hour");
		} else {
			sprintf(period1, "%d day", CCLimit1Days);
		}
		sprintf(mcm.msg, "Minimum purchase is $%d.  "
						 "Maximum purchase per %s period is $%d.  "
						 "Maximum per %d days is $%d.  "
						 "Maximum per %d days is $%d.  "
						 "Your credit card statement will show a charge from %s.",
						CCPurchaseMinimum,
						period1, 
						GetCCPurchaseLimitForPlayer(player_id, CCLimit1Days),
						CCLimit2Days, 
						GetCCPurchaseLimitForPlayer(player_id, CCLimit2Days),
						CCLimit3Days, 
						GetCCPurchaseLimitForPlayer(player_id, CCLimit3Days),
						CCChargeName);

		SendDataStructure(DATATYPE_MISC_CLIENT_MESSAGE, &mcm, sizeof(mcm));
	}

	// Send a MiscClientMessage that contains the current real money
	// account setup warning.
	if (client_version_number.build >= 0x01060005) {	// client new enough to handle it?
		struct MiscClientMessage mcm;
		zstruct(mcm);
		mcm.message_type = MISC_MESSAGE_CREATE_ACCOUNT_WARNING;
		strcpy(mcm.msg,
				"Note:  Each person is allowed only ONE Real Money account.");
		SendDataStructure(DATATYPE_MISC_CLIENT_MESSAGE, &mcm, sizeof(mcm));
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Return whether the player's client socket is currently connected.
//
int Player::Connected(void)
{
	int connected = FALSE;
	EnterCriticalSection(&PlayerCritSec);
	// Socket must exist, have been fully connected at one point, and is
	// still connected.
	if (server_socket) {
		if (!server_socket->connected_flag && !server_socket->disconnected) {
			connected = TRUE;	// assume connected until we time out connecting.
		} else {
			// We were connected.  Are we still?
			if (!server_socket->disconnected) {
				connected = TRUE;
			}
		}
	}

	// If it has been too long since we expect to hear from them, assume
	// disconnected.
	if (time_of_next_expected_packet &&
			(long)(SecondCounter - time_of_next_expected_packet) >= 3*60)
	{
		// Too long since we've received something.
	  #if 0	//kriskoin: 		kp(("%s(%d) DISCONN DETECTED: %ds overdue.\n", _FL, (long)(SecondCounter - time_of_next_expected_packet)));
	  #endif
		connected = FALSE;
	}
	LeaveCriticalSection(&PlayerCritSec);
	return connected;
}	

//*********************************************************
// https://github.com/kriskoin//
// Return how long a player has been disconnected for (or 0 if
// still connected or never connected)
//
int Player::DisconnectedSeconds(void)

{
	if (Connected()) {	// if we're connected... the answer is obviously zero.
		return 0;
	}

	int seconds = 0;
	EnterCriticalSection(&PlayerCritSec);
	if (server_socket && server_socket->connected_flag && server_socket->disconnected) {
		seconds = SecondCounter - server_socket->time_of_disconnect;
	}
	LeaveCriticalSection(&PlayerCritSec);
	if (!seconds) {	// if still zero, assume we've merely lost contact.
		seconds = TimeSinceLastContact();
	}
	return seconds;
}

//*********************************************************
// https://github.com/kriskoin//
// Check if the SDBRECORD_FLAG_NO_CASHIER bit has been set and
// send a message to the client if appropriate.
// returns TRUE if the cashier is disabled for this client only,
// returns FALSE otherwise.
//
int Player::TestIfCashierDisabledForClient(void)
{
	SDBRecord player_rec;
	zstruct(player_rec);

	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		if (player_rec.flags & SDBRECORD_FLAG_NO_CASHIER) {
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0,
				"Transaction NOT PROCESSED:\n"
				"\n"
				"Cashier transactions on this account are currently disabled.\n"
				"\n"
				"This usually happens after there is an error contacting our\n"
				"transaction server with one of your previous transactions and\n"
				"the error needs to be looked into manually.\n"
				"\n"
				"Staff will correct this problem as soon as they can review the error.\n"
				"\n"
				"Please email support@kkrekop.io if you have any questions.");
			return TRUE;
		}
	}
	return FALSE;
}

//*********************************************************
// 2001/12/21 - Tony Tu
//
// Check if the SDBRECORD_FLAG_NO_CASHOUT bit has been set and
// send a message to the client if appropriate.
// returns TRUE if the cashier is disabled for this client only,
// returns FALSE otherwise.
//
int Player::TestIfCashOutDisabledForClient(void)
{

	SDBRecord player_rec;
	zstruct(player_rec);

	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		if (player_rec.flags & SDBRECORD_FLAG_NO_CASHOUT) {
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0,
				"Transaction NOT PROCESSED:\n"
				"\n"
				"You may only cash out after playing a minimum of 100 raked games,\n"
				"\n"
				"consistent with the rules of the current deposit promotion.\n"
				"\n"
				"Please email support@kkrekop.io if you are not in this situation.");
			return TRUE;
		}
	}
	return FALSE;
}

//*********************************************************
// https://github.com/kriskoin//
// Return how long it has been since we've heard from a player (in seconds)
//
int Player::TimeSinceLastContact(void)


{
	return SecondCounter - time_of_last_received_packet;
}

//*********************************************************
// https://github.com/kriskoin//
// Return how many seconds overdue contact with this player is (in seconds).
// Returns 0 if we're in good contact, otherwise returns the number
// of seconds it is past when we expected to hear from them.
//
int Player::OverdueContactTime(void)
{
	long overdue_seconds = 0;

	// Count number of outstanding pings (if any)
	int outstanding_pings = 0;
	WORD32 oldest_outstanding_ping = 0;
	for (int i=0 ; i<PLAYER_PINGS_TO_RECORD ; i++) {
		if (PingSendTimes[i]) {
			outstanding_pings++;
			if (PingSendTimes[i] < oldest_outstanding_ping || !oldest_outstanding_ping) {
				oldest_outstanding_ping = PingSendTimes[i];
			}
		}
	}

  #if 1	//kriskoin: 	if (client_version_number.flags & VERSIONFLAG_SIMULATED) {
		// Simulated clients should not be expected to return every ping.
		outstanding_pings = 0;
	}
  #endif

	if (outstanding_pings) {
		// There are outstanding pings... we can use them to determine
		// if there are connection problems.
		WORD32 overdue_ms = GetTickCount() - oldest_outstanding_ping;
		//kp(("%s(%d) There are %d outstanding pings (oldest is %dms old)\n",_FL, outstanding_pings, overdue_ms));
		overdue_seconds = (long)(overdue_ms / 1000) - 1;
	} else {
		overdue_seconds = (long)(SecondCounter - time_of_next_expected_packet);
	}
	if (overdue_seconds < 0) {
		overdue_seconds = 0;	// never go negative if we're not overdue.
	}
	return overdue_seconds;
}	

//*********************************************************
// https://github.com/kriskoin//
// Return our best guess about the quality of the connection to
// this player.  Returns CONNECTION_STATE_* (see player.h)
//
CONNECTION_STATE Player::CurrentConnectionState(void)
{
	CONNECTION_STATE new_state = CONNECTION_STATE_BAD;	// default.

	if (!Connected()) {
		new_state = CONNECTION_STATE_LOST;	// contact has been lost.


	} else {
		// Count number of outstanding pings (if any)
		int overdue_seconds = OverdueContactTime();
		if (client_version_number.flags & VERSIONFLAG_SIMULATED) {
			overdue_seconds /= 3;	// make much less sensitive for simulated clients
		}
		//kp(("%s(%d) Overdue seconds = %d\n",_FL, overdue_seconds));
		if (overdue_seconds <= 8) {
			new_state = CONNECTION_STATE_GOOD;
		} else if (overdue_seconds <= 15) {
			new_state = CONNECTION_STATE_POOR;
		}
	}
	if (previous_connection_state != new_state) {
		pr(("%s %-15.15s %-15s Connection state changed from %d to %d (overdue seconds = %d)\n",
				TimeStr(), ip_str, user_id, previous_connection_state, new_state, overdue_seconds));
		previous_connection_state = new_state;
	}
	return new_state;
}

//*********************************************************

// https://github.com/kriskoin//
// Send the player all the data we have on a particular table
// he's joined to.
//
void Player::SendAllTableData(int table_index)
{
	if (table_index < 0 || table_index > MAX_GAMES_PER_PLAYER) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) SendAllTableData(%d) called.  table_index out of range.",_FL,table_index);
		return;
	}
	struct JoinedTableData *jtd = &JoinedTables[table_index];
	struct CardRoom_JoinTable jt;
	zstruct(jt);

	// Determine if we're currently seated at ANY tournament tables.
	if (tournament_table_serial_number) {
		jt.flags |= JOINTABLE_FLAG_SEATED_AT_TOURNAMENT;
	}

	// jt.status values: 0=unjoined, 1=joined, 2=watching
	jt.status = (BYTE8)(jtd->watching_flag ? 2 : 1);
	jt.table_serial_number = jtd->table_serial_number;
	jt.game_rules = (BYTE8)jtd->gcd.game_rules;
	jt.client_display_tab_index = (BYTE8)jtd->gcd.client_display_tab_index;
	pr(("%s(%d) jt.client_display_tab_index = %d, game_rules = %d, 1 on 1 = %d\n",
			_FL, jt.client_display_tab_index, jt.game_rules, jtd->gcd.flags & GCDF_ONE_ON_ONE ? TRUE : FALSE));
	if (jtd->gcd.flags & GCDF_REAL_MONEY) {
		jt.flags |= JOINTABLE_FLAG_REALMONEY;
	}
	if (jtd->gcd.flags & GCDF_ONE_ON_ONE) {
		jt.flags |= JOINTABLE_FLAG_ONE_ON_ONE;
	}
	if (jtd->gcd.flags & GCDF_TOURNAMENT) {
		jt.flags |= JOINTABLE_FLAG_TOURNAMENT;
	}
	SendDataStructure(DATATYPE_CARDROOM_JOIN_TABLE, &jt, sizeof(jt));

	// Re-send most recent gcd, gpd, and input requests.
	if (jtd->gcd.table_serial_number) {	// does it contain anything yet?
		SendGameCommonData(&jtd->gcd);
	}
	if (jtd->gpd.table_serial_number) {	// does it contain anything yet?
		SendGamePlayerData(&jtd->gpd);
	}

	// Send the currently cached seat avail notification (if any)
	// Only send to 1.06-3 and newer clients... old clients will pop up the dialog
	// every time this structure is received.
	if (client_version_number.build >= 0x01060003) {
		if (saved_seat_avail.table_serial_number) {
			pr(("%s(%d) Re-sending SeatAvail request to player $%08lx (%s) for table #%d, timeout=%ds\n",
					_FL, player_id, user_id, saved_seat_avail.table_serial_number,
					saved_seat_avail.timeout));
		}
		SendSeatAvail(&saved_seat_avail);
	}

	if (jtd->input_request.table_serial_number==jtd->table_serial_number &&
		jtd->input_request.game_serial_number==jtd->gcd.game_serial_number) {
		// It's for this table and the current game...
		// send it out even if it has already been responded to..
		SendPlayerInputRequest(&(jtd->input_request));
	}

	//kp(("%s(%d) table serial number = %d\n", _FL, jtd->gcd.table_serial_number));
	// If they're watching/sitting down, send the sitdown message.
	if (jtd->gcd.table_serial_number &&
		client_version_number.build >= 0x01060002)
	{
		// Send a table sit-down message.  These can be customized for
		// any player, table, stakes, or game.
		if (jtd->gcd.flags & GCDF_REAL_MONEY) {	// real money?
			int count = AllowedAutoAllInCount();
			pr(("%s(%d) Sending MiscClient message with all-in count of %d for table %d\n",
					_FL, count, jtd->gcd.table_serial_number));
			// If they're out of all-ins, give them a stern warning
			if (!count) {
				SendMiscClientMessage(MISC_MESSAGE_TABLE_SITDOWN,
					jtd->gcd.table_serial_number, 0, 0, 0, 0,
					"You are reminded that this table is playing\n"
					"for REAL MONEY.\n"
					"\n"
					"You currently have ZERO ALL INs left.\n"
					"\n"
					"We recommend that you stop playing until 24 hours has\n"
					"elapsed or your all-ins are reset.  You may select\n"
					"'Request All Ins reset' from the Lobby's Options menu.\n"
					"\n"
					"If you play now, you will be 100%% responsible if you\n"
					"time out; your hand will be folded.");
			} else if (jtd->gcd.game_rules==GAME_RULES_OMAHA_HI || jtd->gcd.game_rules==GAME_RULES_OMAHA_HI_LO) {
				// special omaha message
				SendMiscClientMessage(MISC_MESSAGE_TABLE_SITDOWN,
					jtd->gcd.table_serial_number, 0, 0, 0, 0, 0,
					"You are reminded that this table is playing\n"
					"for REAL MONEY.\n\n"

					"You currently have %d ALL IN%s available to be\n"
					"used for Disconnects or Time Outs. See web site\n"
					"for further details.\n\n"

				  #if 0	// 2022 kriskoin
					"To see all called hands on the river, request a\n"
					"hand history from the lobby screen.\n\n"
				  #endif
					"Please remember that in Omaha you must use exactly\n"
					"two of your pocket cards and combine them with\n"
					"exactly three board cards. See web site for further\n"
					"details.\n\n"

					"English only at the tables and no foul language.\n\n"

					"Thank you.",
					count, count==1?"":"s");
			} else {
				SendMiscClientMessage(MISC_MESSAGE_TABLE_SITDOWN,
					jtd->gcd.table_serial_number, 0, 0, 0, 0,
					"You are reminded that this table is playing\n"
					"for REAL MONEY.\n\n"

					"You currently have %d ALL IN%s available to be\n"
					"used for Disconnects or Time Outs. See web site\n"
					"for further details.\n\n"

				  #if 0	// 2022 kriskoin
					"To see all called hands on the river, request a\n"
					"hand history from the lobby screen.\n\n"
				  #endif

					"English only at the tables and no foul language.\n\n"

					"Thank you.",
					count, count==1?"":"s");
			}
		}
	}
}

void Player::SendAllTableData(WORD32 table_serial_number)
{
	SendAllTableData(SerialNumToJoinedTableIndex(table_serial_number));
}

//****************************************************************
// 
//
// Typically it's the card room that will cause us to join, watch, or leave a table.
// It has not yet been determined if these functions will also be used to
// notify the client of the change in joined table status.
// 990726HK : we have to send this back to the client to let it know if it failed to sit down
//
ErrorType Player::JoinTable(struct GameCommonData *gcd, int watching_flag, char *table_name)
{
	EnterCriticalSection(&PlayerCritSec);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		if (JoinedTables[i].table_serial_number == gcd->table_serial_number) {
			LeaveCriticalSection(&PlayerCritSec);
			Error(ERR_INTERNAL_ERROR, "%s(%d) Detected attempt for player $%08lx to join a table we're already joined to (%d)",_FL, player_id, gcd->table_serial_number);
			return ERR_INTERNAL_ERROR;
		}
	}

	if (JoinedTableCount >= MAX_GAMES_PER_PLAYER) {
		LeaveCriticalSection(&PlayerCritSec);
	  #if 0	// 2022 kriskoin
		Error(ERR_INTERNAL_ERROR, "%s(%d) Detected attempt to join too many tables (player $%08lx)",_FL, player_id);
	  #endif
		return ERR_ERROR;
	}

	SendPing();	// sprinkle pings in a few places of the code

	zstruct(JoinedTables[JoinedTableCount]);
	JoinedTables[JoinedTableCount].watching_flag = watching_flag;
	JoinedTables[JoinedTableCount].joined_time = SecondCounter;
  #if 0	// 2022 kriskoin
	JoinedTables[JoinedTableCount].game_rules = gcd->game_rules;
  #endif
	JoinedTables[JoinedTableCount].table_serial_number = gcd->table_serial_number;
	JoinedTables[JoinedTableCount].gcd = *gcd;
	strnncpy(JoinedTables[JoinedTableCount].table_name, table_name, MAX_TABLE_NAME_LEN);
	JoinedTableCount++;

	// Keep track of which tournament table we are seated at (if any).
	if (gcd->flags & GCDF_TOURNAMENT && !watching_flag) {
		tournament_table_serial_number = gcd->table_serial_number;
		pr(("%s(%d) Setting %s's tournament_table_serial_number to %d\n", _FL, user_id, tournament_table_serial_number));
	}

	LeaveCriticalSection(&PlayerCritSec);
	pr(("%s(%d) Player $%08lx Joined table %d (%s).  We now belong to %d tables.\n",
			_FL, player_id, table_serial_number,
			watching_flag ? "watching" : "playing", JoinedTableCount));

	SendAllTableData(gcd->table_serial_number);

	if (CountSeatedTables() > 1 && !watching_flag) {
		// They just sat down at their second (or more) table.  Warn them
		// to play quickly.
		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, gcd->table_serial_number,
				0, 0, 0, 0,
				"In consideration of all players, you must play\n"
				"quickly as it is unfair to make others wait\n"
				"while you are playing at more than one table.\n\n"
				"We encourage you to use the 'in-turn' checkboxes\n"
				"whenever possible to help speed play.\n\n"
				"Thank you for your understanding.");
	}

	return ERR_NONE;


}

//****************************************************************
// 
//
// Leave a table we are currently playing at or watching.
// Usually it's the table which asks us to leave in between games.
//
ErrorType Player::LeaveTable(WORD32 table_serial_number)
{
	return LeaveTable(table_serial_number, TRUE);
}

ErrorType Player::LeaveTable(WORD32 table_serial_number, int notify_player_flag)
{
	EnterCriticalSection(&PlayerCritSec);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		if (JoinedTables[i].table_serial_number == table_serial_number) {
			// Found it... remove it from the list.
			for (int j=i ; j<JoinedTableCount-1 ; j++)
				JoinedTables[j] = JoinedTables[j+1];
			JoinedTableCount--;
			LeaveCriticalSection(&PlayerCritSec);
			pr(("%s(%d) Left table %d.  We now belong to %d tables.\n", _FL, table_serial_number, JoinedTableCount));

			// Did we leave our tournament table?
			if (table_serial_number==tournament_table_serial_number) {
				tournament_table_serial_number = 0;	// clear
				pr(("%s(%d) Setting %s's tournament_table_serial_number to %d\n", _FL, user_id, tournament_table_serial_number));
			}

			ErrorType err = ERR_NONE;
			if (notify_player_flag) {
				// Notify the remote client (DATATYPE_CARDROOM_JOIN_TABLE)
				struct CardRoom_JoinTable jt;
				zstruct(jt);
				// jt.status values: 0=unjoined, 1=joined, 2=watching
				jt.status = 0;
				jt.table_serial_number = table_serial_number;
				err = SendDataStructure(DATATYPE_CARDROOM_JOIN_TABLE, &jt, sizeof(jt));
			}
			return err;
		}
	}

	LeaveCriticalSection(&PlayerCritSec);
	Error(ERR_INTERNAL_ERROR, "%s(%d) Detected attempt to leave a table we weren't joined to (%d).",_FL, table_serial_number);
	return ERR_INTERNAL_ERROR;
}


//*********************************************************
// https://github.com/kriskoin//
// Keep track of when this player object last got timed out on an input
// request. This value is local to the player object and has nothing
// to do with the player database.
//
void Player::SaveTimeOutTime(void)
{
	last_timeout_seconds = SecondCounter;
	strnncpy(last_timeout_ip_str, ip_str, MAX_COMMON_STRING_LEN);
}

//*********************************************************
// https://github.com/kriskoin//
// Send an email validation code letter out to this player's email address
//
void Player::SendEmailValidationCode(char *email_address)
{

	// calculate a validation code which is always 5 digits.
	int validation_code = CalcEmailValidationCode(email_address);

	// Always send out an email validation letter.
	char fname[MAX_FNAME_LEN];
	zstruct(fname);
	MakeTempFName(fname, "evc");
	FILE *fd2 = fopen(fname, "wt");
	if (fd2) {
		if (!iRunningLiveFlag) {
			fprintf(fd2, "*** THIS IS A SAMPLE ONLY: PLEASE IGNORE ***\n\n");
		}
		fprintf(fd2, "Thank you for requesting a Validation Code to Real Money Games at kkrekop.io.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Your validation code for %s is %d\n", email_address, validation_code);
		fprintf(fd2, "\n");
		fprintf(fd2, "Follow these instructions To start your first Real Money Game:\n");
		fprintf(fd2, "  1. Note your 5-digit Validation Code as provided above\n");
		fprintf(fd2, "  2. Launch the Desert Poker from your desktop(or return to Desert Poker if already open)\n");
		fprintf(fd2, "  3. From the OPTIONS menu choose Change/Validate Email Address\n");
		fprintf(fd2, "  4. Enter %d in the 'Validation code' box\n", validation_code);
		fprintf(fd2, "  5. Press the 'Validate' button\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "If you have difficulties, please feel free to request assistance at support@kkrekop.io. To help\n");
		fprintf(fd2, "ensure prompt service, please be sure to include your player ID in the email.\n");
		fprintf(fd2, "\n");
		fclose(fd2);
		// Now email it...
		char subject[100];
		zstruct(subject);
		sprintf(subject, "Email Validation.");
		Email(	email_address,
				"Email validator",

				"support@kkrekop.io",
				subject,
				fname,
				"addresschange@kkrekop.io",	// bcc:-4-
				TRUE);	// delete when sent
		
	}

	FILE *fp;
	fp=fopen("new_account_errors.txt", "a+");
  fprintf(fp,"Code: %d", validation_code);
  fclose(fp);

}	


void Player::SendEmailValidationCodeNewAccount(char *email_address ,char *player_id)
{

	// calculate a validation code which is always 5 digits.
	int validation_code = CalcEmailValidationCode(email_address);

	// Always send out an email validation letter.
	char fname[MAX_FNAME_LEN];
	zstruct(fname);
	MakeTempFName(fname, "evc");
	FILE *fd2 = fopen(fname, "wt");
	if (fd2) {
		if (!iRunningLiveFlag) {
			fprintf(fd2, "*** THIS IS A SAMPLE ONLY: PLEASE IGNORE ***\n\n");
		}
		fprintf(fd2, "Dear Player.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Welcome to Desert Poker, your online Poker Authority.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "We look to forward to providing you with the best online Poker experience\n");
		fprintf(fd2, "in the industry.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Your Desert Poker ID is: %s \n",player_id);
		fprintf(fd2, "The Password that you have selected will continue being the same as it\n");
		fprintf(fd2, "was during the signup process.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, " -------------------------------------------------------------\n");
		fprintf(fd2, "WHEN PROMPTED INTO OUR SOFTWARE FOR YOUR EMAIL VALIDATION CODE,\n");
		fprintf(fd2, "YOU MUST FIRST TYPE IN THE FOLLOWING: %d \n",validation_code);
		fprintf(fd2, " -------------------------------------------------------------\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "For a Limited Time Only we are giving away a 100%% Sign-Up Bonus up to\n");
		fprintf(fd2, "$500 FREE! Go to our Promotions Pages for details about this and other\n");
		fprintf(fd2, "Desert Player Rewards! http://www.kkrekop.io/promotions.htm\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "The following tips are to ensure that you have the best possible online\n");
		fprintf(fd2, "poker experience.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "1. You have already completed the first step: downloading and registering. In order to\n");
		fprintf(fd2, "complete the registration process, please double click on our Desert Poker icon on\n");
		fprintf(fd2, "your desktop (a poker chip with a cactus in it).\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "2. You must type in the 5 digit validation code stated above to be able to play at\n");
		fprintf(fd2, "our tables. As soon as you submit your validation code, you'll be ready to hit our\n");
		fprintf(fd2, "FREE Play Money Tables!.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "3. When you're ready to win Real Money, click on the CASHIER.\n");
		fprintf(fd2, "You will be prompted to fill in appropriate information to further ensure your\n");
		fprintf(fd2, "privacy and security.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "4. We make it EASY to Deposit! At our Secure Banking Area, you can fund your\n");
		fprintf(fd2, "account using: the following methods:\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "On Demand Funds (Visa and MasterCard), Neteller, PrepaidATM, Western\n");
		fprintf(fd2, "Union, CitaDel ,Bank wire, Checks, eChecks, ACH, Privy-Pay, and other online deposit methods .For a\n");
		fprintf(fd2, "complete listing of our payment options and information on depositing and cashing\n");
		fprintf(fd2, "out, see http://www.kkrekop.io/secure_banking.htm\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "5. Tell-A-Friend about kkrekop.io and become an affiliate with the most\n");
		fprintf(fd2, "aggressive commission structure in the business. It's FUN and Easy!. For more\n");
		fprintf(fd2, "details, go to http://www.kkrekop.io/affiliates_refer.htm\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Desert Poker was launched in January 2004 and has quickly become one of the\n");
		fprintf(fd2, "favorite online poker sites in the world. We pride ourselves with our promotions,\n");
		fprintf(fd2, "tournaments and customer service. If ther is anything you may nedd, please feel\n");
		fprintf(fd2, "free to visit our Help Center for inmediate assistence:\n");
		fprintf(fd2, "http://www.kkrekop.io/helpcenter_xtd.htm.\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Thank you for signingup with Desert Poker !\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Sincerely,\n");
		fprintf(fd2, "\n");
		fprintf(fd2, "Desert Poker Customer Support Team\n");
		fprintf(fd2, "http://www.kkrekop.io\n");


		fclose(fd2);
		// Now email it...
		char subject[100];
		zstruct(subject);
		sprintf(subject, "Welcome to Desert Poker.");
		Email(	email_address,
				"Desert Poker email validator",

				"signups@kkrekop.io",
				subject,
				fname,
				"newaccounts@deserpoker.com",	// bcc:
				TRUE);	// delete when sent
		
	}

	FILE *fp;
	fp=fopen("new_account_errors.txt", "a+");
  fprintf(fp,"Account: %s Validation Code: %d made by SendEmailValidationCodeNewAccount\n ", player_id,validation_code);
  fclose(fp);

}	

//****************************************************************
// 
//
//	Send various types of data to the player.  Data gets sent through
//	these wrapper functions to give us a chance to do some error checking.
//
ErrorType Player::SendGameCommonData(struct GameCommonData *game_common_data)
{
	// Save a copy of this structure.
	int index = SerialNumToJoinedTableIndex(game_common_data->table_serial_number);
	if (index >= 0) {	// if index is valid...
		JoinedTables[index].gcd = *game_common_data;	// save a copy.
		// now send it out.
		//before send it out, retrieve hihand info
        // kriskoin 
        //Any time when sending GameCommonData, retrive the hihand info into the structure

        struct hihand {
                time_t  timestamp;
                INT32   game_serial_num;
                char    user_id[MAX_PLAYER_USERID_LEN];
                Hand    hand_rec;
        } fh_rec;

        Hand *hi_hand;
        int fp;

        hi_hand = new Hand();
        if(game_common_data->flags & GCDF_REAL_MONEY) { //real money game
                memcpy(hi_hand, &hihand_real[0].hand_rec, sizeof(Hand));
                memcpy(game_common_data->hihand_name, hihand_real[0].user_id, sizeof(hihand_real[0].user_id));
        } else {
                memcpy(hi_hand, &hihand_play[0].hand_rec, sizeof(Hand));
                memcpy(game_common_data->hihand_name, hihand_play[0].user_id, sizeof(hihand_play[0].user_id));
        }

        char str[50];
        zstruct(str);
        hi_hand->GetASCIIHand(str);
        unsigned char c;
        for (int i=0;i<5;i++) {
                if (str[5*i+1]=='T') {
                        c = 10;
                } else if (str[5*i+1]=='J') {
                        c = 11;
                } else if (str[5*i+1]=='Q') {
                        c = 12;
                } else if (str[5*i+1]=='K') {
                        c = 13;
                } else if (str[5*i+1]=='A') {
                        c = 1;
                } else {
                        c = str[5*i+1]-'0';
                }
                game_common_data->hihand[i] = c<<4;
                if (str[5*i+2] == 's'){
                        c = 1;
                } else if (str[5*i+2]=='h') {
                        c = 2;
                } else if (str[5*i+2]=='d') {
                        c = 3;
                } else if (str[5*i+2]=='c') {
                        c = 4;
                }
                game_common_data->hihand[i] = game_common_data->hihand[i] | c;
        }
        // end kriskoin 

		ErrorType err = SendDataStructure(DATATYPE_GAME_COMMON_DATA, game_common_data, sizeof(*game_common_data));
		return err;
	}
	Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to send GCD for a table we're not joined to (%d)", _FL, game_common_data->table_serial_number);
	return ERR_INTERNAL_ERROR;
}

/**********************************************************************************/
ErrorType Player::SendGamePlayerData(struct GamePlayerData *game_player_data)
{
	// Save a copy of this structure.
	int index = SerialNumToJoinedTableIndex(game_player_data->table_serial_number);
	if (index >= 0) {	// if index is valid...
		JoinedTables[index].gpd = *game_player_data;	// save a copy.
	  #if 0	//kriskoin: 		// If the table asked us to force sitout mode (usually due to a timeout),
		// then set our local copy of the client state to indicate we're sitting out.
		if (game_player_data->flags & GPDF_FORCED_TO_SIT_OUT) {
			JoinedTables[index].client_state_info.sitting_out_flag = TRUE;
		}
	  #endif

		// now send it out.
		ErrorType err = SendDataStructure(DATATYPE_GAME_PLAYER_DATA, game_player_data, sizeof(*game_player_data));
		return err;
	}
	Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to send GPD for a table we're not joined to (%d)", _FL, game_player_data->table_serial_number);
	return ERR_INTERNAL_ERROR;
}

/**********************************************************************************
 Function Player::SendAccountInfo(struct AccountInfo *ai)
 date: kriskoin 2019/01/01 Purpose: send an account info packet to the client
***********************************************************************************/
ErrorType Player::SendAccountInfo(void)
{
	send_account_info = FALSE;	// no longer needs resending.
	AccountInfo ai;
	zstruct(ai);
	ai.real_in_bank = RealInBank;
	ai.real_in_play = RealInPlay;
	ai.fake_in_bank = FakeInBank;
	ai.fake_in_play = FakeInPlay;
	ai.pending_credit = EcashCredit;
	
	// kriskoin  20020419
	//reset cashout and "promo game to play" for those who lost most of their money
	if ((RealInBank+RealInPlay)<2000 && SDB->GetCreditFeePoints(player_id)){
	//if their account_balance is less than $20
		SDB->ClearCreditFeePoints(player_id);
		EnableCashoutForPlayer(player_id);
	}
	// end kriskoin 

	ai.credit_fee_points = SDB->GetCreditFeePoints(player_id);
	// kriskoin 
	ai.good_raked_games = SDB->GetGoodRakedGames(player_id);
	// end kriskoin 
	ai.login_status = (BYTE8)LoginStatus;
	ai.login_priv   = (BYTE8)priv;
	ai.all_in_count = (BYTE8)AllowedAutoAllInCount();
	ai.all_ins_allowed = (BYTE8)AllInsAllowed;
	ai.pending_check = SDB->GetPendingCheckAmount(player_id);	// total amount of pending cash-out checks due to player
	ai.pending_paypal = SDB->GetPendingPaypalAmount(player_id);       // total amount of pending cash-out checks due to player
	if (priv >= ACCPRIV_CUSTOMER_SUPPORT) {
		ai.good_all_ins_allowed_for_auto_reset = GoodAllInsAllowed;
		ai.all_in_reset_time = AllInsResetTime;
	}
	ai.cc_purchase_fee_rate = (WORD16)((double)CCFeeRate * 10000.0 + .49);	// fee rate * 10000, for example 5.25% = .0525 is sent as 525.
	//kp(("%s(%d) cc_purchase_fee_rate = %d (%g)\n", _FL, ai.cc_purchase_fee_rate, CCFeeRate));

	strnncpy(ai.user_id, user_id, MAX_PLAYER_USERID_LEN);	

	// no verifying the table -- he might not be joined to any table
	ErrorType err = SendDataStructure(DATATYPE_ACCOUNT_INFO, &ai, sizeof(ai));
	return err;
}

/**********************************************************************************
 Function Player::HandleSelectedAction(struct GamePlayerInputRequest *gpir)
 date: kriskoin 2019/01/01 Purpose: if a checkbox for a default action has been selected, deal with it here
 Returns: TRUE if we handled something, FALSE if it didn't do anything
***********************************************************************************/
int Player::HandleSelectedAction(struct GamePlayerInputRequest *gpir)
{
	// If the user has selected 'fold in turn' and 'fold' is one of
	// our options, don't send out the GPIR.
	int index = SerialNumToJoinedTableIndex(gpir->table_serial_number);
	if (index >= 0) {	// if index is valid...
		JoinedTables[index].input_request = *gpir;	// save a copy.

		BYTE8 action_chosen = ACT_NO_ACTION;
		// If a user is allowed to check, choose that over fold
		// when fold_in_turn is set
		// If the user is disconnected or leaving the table, just fold him
		// without testing for 'check'.
		int check_allowed = TRUE;
		// a good or poor connections is good enough to check.  bad or lost is not.
		if (CurrentConnectionState() > CONNECTION_STATE_POOR) {
			check_allowed = FALSE;
		}
		if (JoinedTables[index].client_state_info.leave_table) {
			// The client is leaving the table... definitely do not check.
			check_allowed = FALSE;
		}
		if (check_allowed &&
				JoinedTables[index].client_state_info.fold_in_turn &&
				JoinedTables[index].client_state_info.in_turn_action_game_serial_number==gpir->game_serial_number &&
				GET_ACTION(gpir->action_mask, ACT_CHECK) ) {
			action_chosen = ACT_CHECK;
		} else if (JoinedTables[index].client_state_info.fold_in_turn &&
				JoinedTables[index].client_state_info.in_turn_action_game_serial_number==gpir->game_serial_number &&
				GET_ACTION(gpir->action_mask, ACT_FOLD) ) {
			action_chosen = ACT_FOLD;
		} else if (JoinedTables[index].client_state_info.post_in_turn &&
				  !JoinedTables[index].client_state_info.leave_table)
		{
			if (GET_ACTION(gpir->action_mask, ACT_POST)) {
				action_chosen = ACT_POST;
			} else if (GET_ACTION(gpir->action_mask, ACT_POST_SB)) {
				action_chosen = ACT_POST_SB;
			} else if (GET_ACTION(gpir->action_mask, ACT_POST_BB)) {
				action_chosen = ACT_POST_BB;
			} else if (GET_ACTION(gpir->action_mask, ACT_POST_BOTH)) {
				action_chosen = ACT_POST_BOTH;
			} else if (GET_ACTION(gpir->action_mask, ACT_POST_ANTE)) {
				action_chosen = ACT_POST_ANTE;
			}
		}
		if (action_chosen != ACT_NO_ACTION) {	// something to handle
			struct GamePlayerInputResult *ir = &JoinedTables[index].input_result;
			zstruct(*ir);
			ir->game_serial_number			= gpir->game_serial_number;
			ir->table_serial_number			= gpir->table_serial_number;
			ir->input_request_serial_number	= gpir->input_request_serial_number;
			ir->seating_position			= gpir->seating_position;
			ir->action = action_chosen;
			ir->ready_to_process = TRUE;
			input_result_ready_table_serial_number = ir->table_serial_number;

			// Send an input request cancel to the player so they know the
			// latest input request serial number and to stop displaying their
			// checkboxes.
			struct GamePlayerInputRequestCancel gpirc;
			zstruct(gpirc);
			gpirc.game_serial_number			= gpir->game_serial_number;
			gpirc.input_request_serial_number	= gpir->input_request_serial_number;
			gpirc.table_serial_number			= gpir->table_serial_number;
			// Send it out WITHOUT using the SendPlayerInputRequestCancel()
			// function (which clears out input result structures).
			SendDataStructure(DATATYPE_PLAYER_INPUT_REQUEST_CANCEL, &gpirc, sizeof(gpirc));
			return TRUE;
		}
	}
	return FALSE; // we didn't do anything
}

/**********************************************************************************/
ErrorType Player::SendPlayerInputRequest(struct GamePlayerInputRequest *gpir)
{
	ErrorType err = VerifyJoinedTable(gpir->table_serial_number);
	if (err!=ERR_NONE) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to send gpir for a table we're not joined to (%d)", _FL, gpir->table_serial_number);
		return ERR_INTERNAL_ERROR;
	}

	// Zero out any old results we may have sitting in memory.
	int index = SerialNumToJoinedTableIndex(gpir->table_serial_number);
	if (index >= 0) {
		zstruct(JoinedTables[index].input_result);
		zstruct(JoinedTables[index].input_result2);
	}

	// Send it out.
	if (HandleSelectedAction(gpir)) {	// was handled by checkbox/default action setting
		err = ERR_NONE;	// internal handling, no error
	} else {
		err = SendDataStructure(DATATYPE_PLAYER_INPUT_REQUEST, gpir, sizeof(*gpir));
		time_of_next_expected_packet = SecondCounter + 1;	// a keep alive should be returned immediately
		waiting_for_input = TRUE;	// we're definitely waiting now
	}
	return err;
}

//*********************************************************
// https://github.com/kriskoin//
// Send out an input request cancel and clear our local copy of the
// corresponding input request.
//
ErrorType Player::SendPlayerInputRequestCancel(struct GamePlayerInputRequestCancel *gpirc)
{
	ErrorType err = VerifyJoinedTable(gpirc->table_serial_number);
	if (err!=ERR_NONE) {
		Error(ERR_INTERNAL_ERROR, "%s %s(%d) server error: trying to send %s gpirc for table s/n %d, game %d (we're not joined to it)",
				TimeStr(), _FL, user_id,
				gpirc->table_serial_number,
				gpirc->game_serial_number);
		return ERR_INTERNAL_ERROR;
	}

	waiting_for_input = FALSE;	// we're probably NOT waiting any longer

	// Zero out any old requests we may have sitting in memory.
	int index = SerialNumToJoinedTableIndex(gpirc->table_serial_number);
	if (index >= 0) {
		zstruct(JoinedTables[index].input_result);
		zstruct(JoinedTables[index].input_result2);
		zstruct(JoinedTables[index].input_request);
	}

	// Send it out.
	err = SendDataStructure(DATATYPE_PLAYER_INPUT_REQUEST_CANCEL, gpirc, sizeof(*gpirc));
	return err;
}

//*********************************************************
// https://github.com/kriskoin//
// Send (and cache) a seat avail notification (only one outstanding per client)
//
ErrorType Player::SendSeatAvail(struct CardRoom_SeatAvail *sa)
{
	// Do some error checking... if this is a cancel, let it through.
	// If it's not a cancel, only let it through if we're not already seated
	// at the table.
	saved_seat_avail = *sa;
	if (sa->timeout) {
		int watching = FALSE;
		ErrorType err = VerifyJoinedTable(sa->table_serial_number, &watching);
		if (err==ERR_NONE && !watching) {
			pr(("%s %s(%d) Tried to resend a seat avail when client already sitting at #%d.  Changing to cancel seat.\n",TimeStr(),_FL,sa->table_serial_number));
			WORD32 table_serial_number = sa->table_serial_number;
			zstruct(saved_seat_avail);
			saved_seat_avail.table_serial_number = table_serial_number;
		}
	}
	pr(("%s %s(%d) Sending SeatAvail to %s $%08lx: sa.timeout = %d, table = %d\n", TimeStr(), _FL, user_id, player_id, saved_seat_avail.timeout, saved_seat_avail.table_serial_number));
	return SendDataStructure(DATATYPE_CARDROOM_SEAT_AVAIL, &saved_seat_avail, sizeof(saved_seat_avail));
}

//****************************************************************
// 
//
// Retrieve an input result structure from the player (if ready).
// *output_ready_flag is set to TRUE or FALSE depending on whether
// the output is ready or not.  If it is ready, then the input result
// is copied to *output_game_player_input_result.
//
ErrorType Player::GetPlayerInputResult(WORD32 table_serial_number, int *output_ready_flag, struct GamePlayerInputResult *output_game_player_input_result)
{
	if (!output_ready_flag) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL output_ready_flag ptr passed to GetPlayerInputResult()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	*output_ready_flag = FALSE;

	if (!output_game_player_input_result) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL output_game_player_input_result ptr passed to GetPlayerInputResult()",_FL);
		return ERR_INTERNAL_ERROR;
	}
	zstruct(*output_game_player_input_result);




	ErrorType err = VerifyJoinedTable(table_serial_number);
	if (err) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to read input data for a game we're not joined to", _FL);
		return ERR_INTERNAL_ERROR;
	}

	EnterCriticalSection(&PlayerCritSec);

	// Determine if the structure has been filled in or not.
	int index = SerialNumToJoinedTableIndex(table_serial_number);
	if (index < 0) {	// if index is invalid
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (JoinedTables[index].input_result.table_serial_number) {
		// Something has been filled in!
		*output_game_player_input_result = JoinedTables[index].input_result;
		*output_ready_flag = TRUE;
		zstruct(JoinedTables[index].input_result);	// clear this copy.
	}

	LeaveCriticalSection(&PlayerCritSec);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Send a struct MiscClientMessage to this player.
//
ErrorType Player::SendMiscClientMessage(int message_type, WORD32 table_serial_number, WORD32 flags,
		WORD32 misc_data_1, WORD32 misc_data_2, WORD32 misc_data_3, char *fmt_message, ...)
{
	char temp_msg[MAX_MISC_CLIENT_MESSAGE_LEN*2];
	zstruct(temp_msg);
	struct MiscClientMessage mcm;
	zstruct(mcm);
	mcm.message_type = (BYTE8)message_type;
	mcm.table_serial_number = table_serial_number;
	mcm.display_flags = flags;
	mcm.misc_data_1 = misc_data_1;
	mcm.misc_data_2 = misc_data_2;
	mcm.misc_data_3 = misc_data_3;
	if (fmt_message) {
		va_list arg_ptr;
		va_start(arg_ptr, fmt_message);
		vsprintf(temp_msg, fmt_message, arg_ptr);
		va_end(arg_ptr);
		if (strlen(temp_msg) >= MAX_MISC_CLIENT_MESSAGE_LEN) {
			Error(ERR_ERROR, "%s(%d) WARNING: MiscClientMessage (type %d) is too long!  Chopping.",_FL, message_type);
		}
		strnncpy(mcm.msg, temp_msg, MAX_MISC_CLIENT_MESSAGE_LEN);
	}
	return SendDataStructure(DATATYPE_MISC_CLIENT_MESSAGE, &mcm, sizeof(mcm));
}

//*********************************************************
// https://github.com/kriskoin//
// Send a ping to this client.  Only sends if client is new enough to handle it.
//
ErrorType Player::SendPing(void)
{
	if (client_version_number.build < 0x01030002)
		return ERR_NONE;	// don't send a ping to an old client (prior to 1.03, build 2)
	// Never send pings too often (at most every 2s)
	if (SecondCounter - last_ping_sent_time < 2)
		return ERR_NONE;	// don't send it.

	struct Ping ping;
	zstruct(ping);
	ping.local_ms_timer = GetTickCount();
	ErrorType result = SendDataStructure(DATATYPE_PING, &ping, sizeof(ping));
//	time_of_next_expected_packet = SecondCounter + 1;	// a ping should be returned immediately
	last_ping_sent_time = SecondCounter;
	EnterCriticalSection(&PlayerCritSec);
	memmove(&PingSendTimes[1], &PingSendTimes[0], sizeof(PingSendTimes[0])*(PLAYER_PINGS_TO_RECORD-1));
	PingSendTimes[0] = ping.local_ms_timer;
	LeaveCriticalSection(&PlayerCritSec);
	return result;
}

//****************************************************************
// 
//
// Send an arbitrary data structure to the remote client.
//
ErrorType Player::SendDataStructure(int data_type, void *data_structure_ptr, int data_structure_len)
{
	return SendDataStructure(data_type, data_structure_ptr, data_structure_len, FALSE, FALSE);
}

ErrorType Player::SendDataStructure(int data_type, void *data_structure_ptr, int data_structure_len, int disable_packing_flag, int bypass_player_queue_flag)
{
	EnterCriticalSection(&PlayerCritSec);
	if (data_structure_ptr==NULL && data_structure_len) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) NULL pointer passed to SendDataStructure()",_FL);
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (data_type < 0 || data_structure_len < 0 || (data_structure_ptr && data_structure_len==0)) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Illegal data_type (%d) or data_structure_len (%d)",_FL, data_type, data_structure_len);
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_INTERNAL_ERROR;
	}
	if (!server_socket) {
		//Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to send data but we have no server socket.", _FL);
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_INTERNAL_ERROR;
	}

	if (player_io_disabled) {	// disable player I/O and wait for timeout to disconnect us?
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_NONE;
	}
	Packet *p = PktPool_Alloc(sizeof(struct DataPacketHeader) + data_structure_len);
	if (p==NULL) {
		Error(ERR_ERROR, "%s(%d) new Packet allocation failed",_FL);
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_ERROR;
	}

	// Write the header into the packet.
	struct DataPacketHeader hdr;
	zstruct(hdr);
	hdr.data_packet_type = (WORD16)data_type;
	hdr.data_packet_length = (WORD16)data_structure_len;
	ErrorType err = p->WriteData((char *)&hdr, sizeof(hdr), 0);
	if (err) {
		delete p;
		LeaveCriticalSection(&PlayerCritSec);
		return err;

	}

	// Write the data into the packet.
	err = p->WriteData((char *)data_structure_ptr, data_structure_len, sizeof(hdr));
	if (err) {
		delete p;
		LeaveCriticalSection(&PlayerCritSec);
		return err;
	}

  #if 0
	kp(("%s(%d) Here's the packet we're about to send:\n",_FL));
	khexdump(p->user_data_ptr, p->user_data_length, 16, 1);
  #endif

	// We're all done building the packet... send it off.
	p->packing_disabled = disable_packing_flag;	// copy the 'disable packing' flag.

	if (!bypass_player_queue_flag) {	// normal program flow
	  	// Pass to packet queuer for player data output threads to handle...
		PlrOut_QueuePacket(this, &p);
	} else {
		// do the work from this thread right now.
		WORD32 bytes_after_compression = 0;
		//kp(("%s(%d) Calling SendPacket()... p->user_data_length = %d, p->length = %d, data_structure_len = %d\n", _FL, p->user_data_length, p->length, data_structure_len));
		err = server_socket->SendPacket(&p, &bytes_after_compression);
		if (p) {
			// It didn't get cleared by SendPacket(), therefore there must have been
			// some sort of error and we need to delete it ourselves.
			delete p;
		}

		if (data_type < DATATYPE_COUNT) {
		  #if 0	// 2022 kriskoin
			kp(("%s(%d) type %2d: compressed = %4d, uncompressed = %4d (%4d + %d + %d + %d) (packing_disabled = %d)\n",
					_FL, data_type, bytes_after_compression,
					data_structure_len + sizeof(hdr) + sizeof(struct PacketHeader) + TCP_HEADER_OVERHEAD_AMOUNT,
					data_structure_len, sizeof(hdr), sizeof(struct PacketHeader), TCP_HEADER_OVERHEAD_AMOUNT,
					disable_packing_flag));
		  #endif
			CurrentPacketStats[data_type].sent_count++;
			CurrentPacketStats[data_type].bytes_sent += bytes_after_compression;
			CurrentPacketStats[data_type].bytes_sent_uncompressed += data_structure_len + sizeof(hdr) + sizeof(struct PacketHeader) + TCP_HEADER_OVERHEAD_AMOUNT;
		}
	}

	LeaveCriticalSection(&PlayerCritSec);
	return err;
}

//*********************************************************
// https://github.com/kriskoin
//
// Count the number of tables we're actually seated at.
//
int Player::CountSeatedTables(void)
{
	int count = 0;
	EnterCriticalSection(&PlayerCritSec);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		if (JoinedTables[i].table_serial_number && !JoinedTables[i].watching_flag) {
			count++;
		}
	}
	LeaveCriticalSection(&PlayerCritSec);
	return count;
}


//*********************************************************
// https://github.com/kriskoin//
// Loop through an array of player id's associated with us and handle:
//  - auto-blocking
//  - login alerts
//  - chat squelching
//
// Returns TRUE if we should be blocked (auto-block or otherwise)
// (mainly used for bailing on cc purchases)
// If player_id should be auto-blocked, it sets *output_auto_block_flag = TRUE;
// (call can then call plr->AutoBlock(reason) to perform the blocking).
//
int Player::ValidateAgainstRelatedAccounts(WORD32 *player_id_array, int count, int *output_auto_block_flag)
{
	if (output_auto_block_flag) {
		*output_auto_block_flag = FALSE;
	}
	int result = FALSE;	// nothing bad by default.

	// Retrieve the player record for our account
	SDBRecord player_rec2;
	zstruct(player_rec2);
	SDB->SearchDataBaseByPlayerID(player_id, &player_rec2);

	for (int i=0 ; i<count ; i++) {
		if (player_id_array[i]) {
			SDBRecord player_rec;	// the result structure
			zstruct(player_rec);
			if (SDB->SearchDataBaseByPlayerID(player_id_array[i], &player_rec) >= 0) {
				// If the other account is auto-blocked, we should
				// auto-block ourselves as well.
				if (player_rec.flags & SDBRECORD_FLAG_AUTO_BLOCK) {
					if (output_auto_block_flag) {
						*output_auto_block_flag = TRUE;		// autoblock required!
					}
					// done now... we're blocking, so don't bother
					// with login alert or chat squelch.
					return TRUE;	// transaction should be blocked.
				}

			  #if 0	//kriskoin: 				// If the other account has login alert set, we should
				// set our own login alert bit and issue an alert (if necessary)
				if (!sent_login_alert_flag &&
					(player_rec.flags & SDBRECORD_FLAG_LOGIN_ALERT))
				{
					sent_login_alert_flag = TRUE;
					char related_accounts[200];
					zstruct(related_accounts);
					BuildUserIDString(player_id_array, count, related_accounts);
					SendAdminAlert(ALERT_7, "Login alert: %s (related to %s)", user_id, related_accounts);
					// Set our login alert flag and write it back to the database
					SDBRecord player_rec2;
					zstruct(player_rec2);
					if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec2) >= 0) {
						player_rec2.flags |= SDBRECORD_FLAG_LOGIN_ALERT;
						SDB->WriteRecord(&player_rec2);
					}
				}
			  #endif

			  #if 0 //kriskoin: 				// If the other account has chat squelched, we should
				// squelch chat for ourselves as well.
				if ( (player_rec.flags  & SDBRECORD_FLAG_SQUELCH_CHAT) &&
					!(player_rec2.flags & SDBRECORD_FLAG_SQUELCH_CHAT) &&
					!chat_disabled)
				{
					chat_disabled = TRUE;
					char related_accounts[200];
					zstruct(related_accounts);
					BuildUserIDString(player_id_array, count, related_accounts);
					SendAdminAlert(ALERT_4, "Chat squelched for %s (related to %s )", user_id, related_accounts);
				}
			  #endif
			}
		}
	}
	return result;
}

//****************************************************************
// 
//
// Verify that we're actually joined (or watching) a particular game
//
ErrorType Player::VerifyJoinedTable(WORD32 table_serial_number, int *output_watching_flag)
{
	if (output_watching_flag) {
		*output_watching_flag = FALSE;
	}
	EnterCriticalSection(&PlayerCritSec);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		if (JoinedTables[i].table_serial_number == table_serial_number) {
			// Found it.
			if (output_watching_flag) {
				*output_watching_flag = JoinedTables[i].watching_flag;
			}
			LeaveCriticalSection(&PlayerCritSec);
			return ERR_NONE;
		}
	}
	// Apparently we weren't joined to that game.  Return an error.
	LeaveCriticalSection(&PlayerCritSec);
	return ERR_ERROR;
}

ErrorType Player::VerifyJoinedTable(WORD32 table_serial_number)
{
	return VerifyJoinedTable(table_serial_number, NULL);
}

//****************************************************************
// 
//
// Take a game serial number and return the appropriate index into the
// JoinedTables[] array.
//
int Player::SerialNumToJoinedTableIndex(WORD32 table_serial_number, int notjoined_ok_flag)
{
	EnterCriticalSection(&PlayerCritSec);
	for (int i=0 ; i<JoinedTableCount ; i++) {
		if (JoinedTables[i].table_serial_number == table_serial_number) {
			// Found it.
			LeaveCriticalSection(&PlayerCritSec);
			return i;
		}
	}

	LeaveCriticalSection(&PlayerCritSec);
	if (!notjoined_ok_flag) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to find table index for a table we aren't joined to (%d)",_FL,table_serial_number);
	}
	return -1;
}

int Player::SerialNumToJoinedTableIndex(WORD32 table_serial_number)
{
	return SerialNumToJoinedTableIndex(table_serial_number, FALSE);
}	

//****************************************************************
// 
//
//	Verify a few of the fields in an incoming packet to make
//	sure that it looks at least remotely valid.
//	In particular, check the length to make sure it matches up
//	properly with what is expected.
//
ErrorType Player::VerifyIncomingPacketFormat(Packet *p, int expected_size)
{
	struct DataPacketHeader *hdr = (struct DataPacketHeader *)p->user_data_ptr;
	if (hdr->data_packet_length != expected_size) {
		Error(ERR_ERROR, "%s(%d) Received data packet (type %d) which was wrong size (%d vs %d) from player_id $%08lx. Disconnecting player.",
							_FL, hdr->data_packet_type, hdr->data_packet_length, expected_size, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;
	}
	return ERR_NONE;
}

//****************************************************************
// 
//
//	Handle the receipt of a struct PlayerLoginRequest.
//
ErrorType Player::ProcessPlayerLoginRequest(struct PlayerLoginRequest *input_login_request, int input_structure_len)
{
	if (sizeof(*input_login_request) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) Login Request structure was wrong length (%d vs %d). Disconnecting socket.",_FL,sizeof(*input_login_request),input_structure_len);
		ProcessPlayerLogoff();
		LoginStatus = LOGIN_INVALID;
		return ERR_ERROR;	// do not process.
	}

	got_login_packet = TRUE;

	idle_flag = FALSE;	// clear idle flag any time we hear something meaningful from the client

	SendPing();	// time to start keeping track of ping times for this guy... do at least one.
	// Make sure the login name and password strings are null terminated.
	input_login_request->user_id[MAX_PLAYER_USERID_LEN-1] = 0;
	input_login_request->password[MAX_PLAYER_PASSWORD_LEN-1] = 0;

	// if either user ID or password is blank, reject it
	if (!strlen(input_login_request->user_id) ||
		!strlen(input_login_request->password)) {
		kp(("%s %s(%d) %s Login Request structure had a blank username (%s) or password (%s)\n",
			TimeStr(),_FL,ip_str,input_login_request->user_id,input_login_request->password));
		LoginStatus = LOGIN_INVALID;
		return ERR_ERROR;	// do not process.
	}

	// If an alternate server ip address has been specified, treat everyone
	// as anonymous (their account may not exist on this server).
	if (ServerVersionInfo.alternate_server_ip) {
		LoginStatus = LOGIN_ANONYMOUS;
		return ERR_NONE;	// no further processing
	}
	
	if (player_id && !ANONYMOUS_PLAYER(player_id)) {
		// already validly logged in... we ignore these.  He should be logged out before he tries
		// to log back in.  This should be taken care of from the client.
		//Error(ERR_ERROR, "%s(%d) Login Request received when a player was already logged in.",_FL);
		SendClientInfo();	// send all account info/chips balance and joined tables to player
		return ERR_ERROR;	// do not process.
	}

	LoginStatus = LOGIN_NO_LOGIN;	// will end up set properly

	// If we're all filled up and this is not a priority login, reject
	// it right now before wasting any more resources on this client.
	pr(("%s(%d) connected_players = %d, max = %d, priority = %d\n",
				_FL, ((CardRoom *)cardroom_ptr)->connected_players,
				MaxLoggedInConnections, input_login_request->priority_flag));
	if (!input_login_request->priority_flag &&
		((CardRoom *)cardroom_ptr)->connected_players >= MaxLoggedInConnections) {
		kp(("%s(%d) Warning... rejecting player because we've reached the MaxLoggedInConnections limit (%d)\n", _FL, ((CardRoom *)cardroom_ptr)->connected_players));
		SendMiscClientMessage(MISC_MESSAGE_SERVER_FULL, 0, 0, 0, 0, 0,
				"The server is currently experiencing unusually high traffic\n"
				"and cannot accept further connections at this time.\n\n"
				"Please try back later.  Thank you.\n\n"
				"Sorry for the inconvenience.");
	}

	priv = 0;
	//	WORD32 previous_player_id = player_id;	// might need to migrate it

	// If they asked for an anonymous login, set LoginStatus to that.
	if (!strcmp(input_login_request->user_id, SZ_ANON_LOGIN) &&
		!strcmp(input_login_request->password, SZ_ANON_LOGIN)) {
			LoginStatus = LOGIN_ANONYMOUS;
	}

	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	// check for anonymous login attempt
	if (LoginStatus != LOGIN_ANONYMOUS) {
		// search for this particular userid/password combo

	  SDB->LoadMoneyData(input_login_request->user_id);

		int found_player = SDB->SearchDataBaseByUserID(input_login_request->user_id, &player_rec);
		if (found_player >=0) {	// found the user_name -- let's validate

			// does it match the password?
		  #if CASE_SENSITIVE_PASSWORDS
			if (!strcmp(player_rec.password, input_login_request->password))
		  #else
			if (!stricmp(player_rec.password, input_login_request->password))
		  #endif
			{ // password matches
				if (player_rec.priv < ACCPRIV_CUSTOMER_SUPPORT && client_platform.computer_serial_num) {
					PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
					SerNumDB_AddPlayerID(client_platform.computer_serial_num, player_rec.player_id);
					LeaveCriticalSection(&PlayerCritSec);
				}

				if (player_rec.flags & SDBRECORD_FLAG_AUTO_BLOCK) {
					//kriskoin: 					// serial number, send an alert, and send an email.
					char reason[100];
					zstruct(reason);
					sprintf(reason, "%s login", player_rec.user_id);
					AutoBlock(reason, player_rec.user_id, player_rec.player_id);
				} else if (player_rec.flags & SDBRECORD_FLAG_LOCKED_OUT) {
					// This account is locked out.  Treat like bad password
					LoginStatus = LOGIN_BAD_PASSWORD;
					if (DebugFilterLevel <= 2) {
						kp(("%s %-15.15s Account '%s' is 'Locked Out' (comp s/n = %d)\n",
								TimeStr(), ip_str, input_login_request->user_id, client_platform.computer_serial_num));
					}
				  	SendAdminAlert(ALERT_4, "Account %s tried to log in but is locked out (comp s/n=%d)",
							player_rec.user_id, client_platform.computer_serial_num);
				} else {
					// They get to be logged in now.
					LoginStatus = LOGIN_VALID;
					priv = player_rec.priv;
				}
			} else {
				// password didn't match
				LoginStatus = LOGIN_BAD_PASSWORD;
				if (DebugFilterLevel <= 2 || player_rec.priv > ACCPRIV_REAL_MONEY) {
					kp(("%s %-15.15s invalid login -- bad password for '%s'\n",
						TimeStr(), ip_str, input_login_request->user_id));
				}
			}
		} else {	// didn't find the user name
			LoginStatus = LOGIN_BAD_USERID;
			if (DebugFilterLevel <= 2) {
				kp(("%s %-15.15s invalid login -- couldn't find userid '%s'\n", TimeStr(), ip_str, input_login_request->user_id));
			}
		}
	}
	// Log in player if possible -- and either way, send the AccountInfo to the client
	EnterCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
  #if 1	// 2022 kriskoin
	//kp(("%s(%d) *** Allowing critical section out of order stuff ***\n",_FL));
	PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
  #else
	PPEnterCriticalSection0(&PlayerCritSec, _FL, FALSE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
  #endif
	if (LoginStatus == LOGIN_VALID) {
		// If this player is already logged in, pass our ServerSocket onto
		// that player object.
		((CardRoom *)cardroom_ptr)->CheckForPreviousPlayerLogin(this, player_rec.player_id);
		// If we no longer have a ServerSocket, we're about to die
		// because the previous Player instance got it instead.
		if (!server_socket) {
			LeaveCriticalSection(&PlayerCritSec);
			LeaveCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
			return ERR_NONE;	// We're disconnected now. Nothing more to do.
		}
		// We're the first instance for the player_id.
		player_id = player_rec.player_id;
		strnncpy(user_id, player_rec.user_id, MAX_PLAYER_USERID_LEN);
		strnncpy(City, player_rec.city, MAX_COMMON_STRING_LEN);

		strnncpy(email_address, player_rec.city, MAX_EMAIL_ADDRESS_LEN); ///////

		RealInBank = player_rec.real_in_bank;
		RealInPlay = player_rec.real_in_play;
		FakeInBank = player_rec.fake_in_bank;
		FakeInPlay = player_rec.fake_in_play;
		EcashCredit = player_rec.pending_fee_refund;
		CreditFeePoints = player_rec.fee_credit_points;
		Gender = player_rec.gender;

		char str[20];
		IP_ConvertIPtoString(server_socket->connection_address.sin_addr.s_addr, str, 20);
		// port: ntohs(server_socket->connection_address.sin_port),
	  #if 1	// 2022 kriskoin
		pr(("%s(%d) Client version: $%08lx, server says latest version is $%08lx (%s)\n",
				_FL, client_version_number.build, ServerVersionInfo.new_client_version.build,


				client_version_number.build == ServerVersionInfo.new_client_version.build ? "match" : "DIFFERENT"));

		if (DebugFilterLevel <= 2) {
			if (client_version_number.build != ServerVersionInfo.new_client_version.build) {
				kp(("%s %-15.15s Login   %-15s $%08lx Ver %d.%02d (build %d)\n",
						TimeStr(), str, input_login_request->user_id, player_id,
						client_version_number.major,
						client_version_number.minor,
						client_version_number.build & 0x00FFFF));
			} else {
				kp(("%s %-15.15s Login   %-15s $%08lx Hands seen =%6d\n",
						TimeStr(), str, input_login_request->user_id, player_id, player_rec.hands_seen));
			}
		}
	  #else
		kp(("%s %-15.15s Login  '%s' %lx sock %04x\n",
				TimeStr(), str, input_login_request->user_id, player_id, server_socket->sock));
	  #endif
	  	char local_ip_str[30];
		zstruct(local_ip_str);
		IP_ConvertIPtoString(client_platform.local_ip, local_ip_str, sizeof(local_ip_str));
		long tz = client_platform.time_zone * 450;
		ConnectionLog->Write(

				"%s %-15.15s Login  '%s' %06lx sock %04x comp#%d localip %s tz=%+dh%02dm\n",
				TimeStr(), str, input_login_request->user_id, player_id,
// 				  TimeStr(), str, input_login_request->user_id, email_address,
				server_socket->sock, client_platform.computer_serial_num,
				local_ip_str, tz/3600, (tz/60)%60);

		// logfile entry
		PL->LogLogin(user_id, player_id, server_socket->connection_address.sin_addr.s_addr,
			RealInBank, RealInPlay, FakeInBank, FakeInPlay);
		SDB->SavePlayerLoginInfo(player_id, time(NULL), server_socket->connection_address.sin_addr.s_addr, &client_platform, &client_version_number);
		
       sprintf(sql,"SELECT sp_update_logged_status('%s','TRUE')",user_id);
  	((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);
	
		//kriskoin: 		if (player_rec.priv > ACCPRIV_REAL_MONEY) {	// admin account of some sort?
			// Some of these get filtered out (the known IP addresses)
			static char *ip_addresses_to_filter[] = {
				"192.168.1.2",
				"24.113.34.100",	// mike
				"24.113.168.123",	// mark
				"24.113.219.89",	// allison
				"209.53.9.147",		// horatio
				"196.40.3.2",		// amnet_cable1
				"196.40.3.3",		// amnet_cable2

				"196.40.3.35",		// amnet_cable_3_35
				"196.40.30.113",	// amnet_cable3
				"196.40.30.114",	// amnet_cable4
				"196.40.30.115",	// amnet_cable5
				"127.0.0.1",	// Change to 127.0.0.1 after testing is complete
				NULL,
			};
			char **ip_str_ptr = ip_addresses_to_filter;

			int filter_it = FALSE;
			WORD32 login_ip = server_socket->connection_address.sin_addr.s_addr;
			while (*ip_str_ptr) {
				WORD32 filter_ip = IP_ConvertHostNameToIP(*ip_str_ptr);
				if (login_ip == filter_ip) {
					// Found a match. Filter it.
					filter_it = TRUE;
					break;	// we're done comparing.
				}
				ip_str_ptr++;
			}
			if (!filter_it) {
				// Print the message!
				char prev_ip_str[30];
				zstruct(prev_ip_str);
				IP_ConvertIPtoString(player_rec.last_login_ip[0], prev_ip_str, sizeof(prev_ip_str));
				kp(("%s %-15.15s Login %s, %s, prev %s\n",
						TimeStr(), ip_str, player_rec.user_id,
						player_rec.full_name, prev_ip_str));
			}
		}

	  #if WIN32 && 0
		kp(("%s(%d) **** AddAccountNote() called for each login !!!! ****\n", _FL));
		SDB->AddAccountNote(player_id, "%s %-15.15s Login  '%s' $%08lx socket $%04x",
				TimeStr(), str, input_login_request->user_id, player_id, server_socket->sock);
	  #endif

		//kriskoin: 		if (player_rec.flags & SDBRECORD_FLAG_LOGIN_ALERT) {
			sent_login_alert_flag = TRUE;
			SendAdminAlert(ALERT_7, "Login alert: %s", user_id);
		} else {
			SendAdminAlert(ALERT_1, "Login: %s", user_id);


		}

		// Check accounts related to our computer serial number and determine
		// if a login alert or chat squelch is in order.
		EnterCriticalSection(&SerNumDBCritSec);
		struct SerNumDB_Entry *e = SerNumDB_GetSerNumEntry(client_platform.computer_serial_num);
		if (e) {
			ValidateAgainstRelatedAccounts(e->player_ids, SERNUMDB_PLAYER_IDS_TO_RECORD, NULL);
		}
		LeaveCriticalSection(&SerNumDBCritSec);

	  #if 0	// disable this once it is well tested and starting to get annoying
		// print recent logins for this player...
		{
			struct SDBRecord rec;
			if (SDB->SearchDataBaseByPlayerID(player_id, &rec) >= 0) {
				kp(("%s(%d) Recent logins by %s...\n", _FL, rec.user_id));
				for (int i=0 ; i<LOGINS_TO_RECORD_PER_PLAYER ; i++) {
					if (rec.last_login_times[i]) {
						IP_ConvertIPtoString(rec.last_login_ip[i], str, 20);

						kp(("        #%02d: %s from %s\n", i+1, TimeStr(rec.last_login_times[i]), str));
					}
				}
			}
		}
	  #endif
	} else { // invalid or anonymous login of some sort
		// set anything we'd need for a non-validated player
		RealInBank = 0;
		RealInPlay = 0;
		FakeInBank = 0;
		FakeInPlay = 0;
		EcashCredit = 0;
		Gender = GENDER_UNKNOWN;
		player_id = NextAnonymousPlayerID;
		// we'll increment NextAnonymousPlayerID here... it's ok, as there's a critical section
		// wrapped around it.  if this moves from here, be sure there's some mutex on it
		NextAnonymousPlayerID++;
		if (NextAnonymousPlayerID > 0xFFFFFFF0) {	// top 16 reserved
			Error(ERR_NOTE, "%s(%d) NextAnonymousPlayerID wrapped!",_FL);
			// Let's hope we've been up so long that previous ID's have all
			// logged off :)
			// Even at 1 million connections an hour, it takes 89 days to
			// wrap, so we should be pretty safe simply wrapping.
			NextAnonymousPlayerID = ANONYMOUS_ID_BASE;
		}
		char str[20];
		IP_ConvertIPtoString(server_socket->connection_address.sin_addr.s_addr, str, 20);
		// port: ntohs(server_socket->connection_address.sin_port),
		ConnectionLog->Write(
				"%s %-15.15s AnonLogin %08lx sock %04x\n",
				TimeStr(), str, player_id, server_socket->sock);
	}
  	SendClientInfo();	// send all account info/chips balance and joined tables to player
	update_now_flag = TRUE;	// request the cardroom try to update us asap
	LeaveCriticalSection(&PlayerCritSec);
	LeaveCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
	return ERR_NONE;
}



//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct AccountRecord
//
// Allen Ko Account Record Processing
//
ErrorType Player::ProcessAccountRecord(struct AccountRecord *input_account_record, int input_structure_len){

char strLog[350];	
 if (sizeof(*input_account_record) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) AccountRecord structure was wrong length (%d vs %d). Disconnecting socket.",_FL,sizeof(*input_account_record),input_structure_len);
		ProcessPlayerLogoff();
		LoginStatus = LOGIN_INVALID;
		return ERR_ERROR;	// do not process.
	} 

	switch (input_account_record->usage) {
	case ACCOUNTRECORDUSAGE_CREATE:
		{
			// Create a new account...
			int admin_computer_flag = FALSE;
			// Test how many accounts have already been created for this
			// computer serial number and then determine if they're allowed
			// to open a new one.
			struct SerNumDB_Entry e;
			zstruct(e);
			EnterCriticalSection(&SerNumDBCritSec);
			struct SerNumDB_Entry *ep = SerNumDB_GetSerNumEntry(client_platform.computer_serial_num);
			if (ep) {
				e = *ep;
			}
			LeaveCriticalSection(&SerNumDBCritSec);

			if (e.computer_serial_num) {	// found it in database...
				int first_blank;
				for (first_blank = 0 ; first_blank < SERNUMDB_PLAYER_IDS_TO_RECORD ; first_blank++) {
					if (!e.player_ids[first_blank]) {
						break;	// found a blank
					}
				}
				//kp(("%s(%d) # of accounts for computer #%d: %d\n", _FL, e.computer_serial_num, first_blank));
				if (first_blank >= MaxAccountsPerComputer) {
					// Not permitted, unless this is an administrator machine.
					// If any of the other accounts are administrator accounts,
					// we should allow them to create a new account.
					int allowed = FALSE;
					for (int i=0 ; i<SERNUMDB_PLAYER_IDS_TO_RECORD ; i++) {
						if (e.player_ids[i]) {
							struct SDBRecord player_rec;
							zstruct(player_rec);
							if (SDB->SearchDataBaseByPlayerID(e.player_ids[i], &player_rec) >= 0) {
								if (player_rec.priv >= ACCPRIV_CUSTOMER_SUPPORT) {
									SendAdminAlert(ALERT_7,
										"Allowing multiple account creation because computer #%d looks like an administrator.",
										client_platform.computer_serial_num);
									allowed = TRUE;
									admin_computer_flag = TRUE;
									break;
								}
							}

						}
					}
					if (!allowed) {
						// Well, it looks like they're not allowed to create
						// another account.  Tell them.
						SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
								1,	// error = userID exists or is bad
								0, 0,
								"Could not create new account\n\n"
								"Our records indicate you already have multiple accounts.\n\n"
								"Our policy is to allow one account per individual.\n\n"
							    "Please write to support if you have any further questions.");
						char str[200];
						zstruct(str);
						BuildUserIDString(e.player_ids, SERNUMDB_PLAYER_IDS_TO_RECORD, str);
						SendAdminAlert(ALERT_3, "%-15.15s Attempt to create account '%s' failed. Comp #%d has too many accounts already (%s))\n",
								ip_str, input_account_record->sdb.user_id,
								client_platform.computer_serial_num, str);
						return ERR_ERROR;
					}
				}
			}
#if 0
			// Run it thru a profanity check
			if (ProfanityFilter(input_account_record->sdb.user_id, FALSE)) {
				SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
					1,	// error = userID exists or is bad
					0, 0,
					"Could not create new account.\n\n"
					"Please select another, more appropriate User ID.\n");
				SendAdminAlert(ALERT_7, "%-15.15s New account '%s' failed profanity check (comp s/n %d)",
						ip_str, input_account_record->sdb.user_id,
						client_platform.computer_serial_num);
				return ERR_ERROR;
			}
#endif
			// First, check if the user_id is already in use.
			int i = SDB->SearchDataBaseByUserID(input_account_record->sdb.user_id);
			if (i >= 0) {
				SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
						1,	// error = userID exists or is bad
						0, 0,
						"The User ID you have typed in (%s) is already\n"
						"in use.\n\n"
						"Please choose another User ID.", input_account_record->sdb.user_id);
				if (DebugFilterLevel <= 2) {
					kp(("%s %-15.15s Attempt to create account '%s' failed.  Already in use (record %d)\n", TimeStr(), ip_str, input_account_record->sdb.user_id, i));
				}
				return ERR_ERROR;
			}



           if (SDB->SearchDataBaseByEmail(input_account_record->sdb.email_address) >= 0) {
				SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
						1,	// error = userID exists or is bad
						0, 0,
						"The Email Address you have typed in (%s) is already\n"
						"in use by another customer.\n\n"
						"Please choose another Email Address.", 
			   			input_account_record->sdb.email_address);
				if (DebugFilterLevel <= 2) {
					kp(("%s %-15.15s Attempt to create account '%s' failed.  Already in use (record %d)\n", TimeStr(), ip_str, input_account_record->sdb.user_id, i));
				}
				return ERR_ERROR;
		   }


		  WORD32 pid = SDB->CreateNewRecord(input_account_record->sdb.user_id);

			if (!pid) {
				// Unable to create a new record.
				SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
						2,	// error = database full
						0, 0,
						"Desert Poker is not accepting new accounts at this time.\n\n"
						"We have chosen to limit new accounts in an effort to maintain\n"
						"our high standard of customer service for our existing clients.\n\n"
						"Please check back in a day or two; more room will open up.");
				kp(("%s %-15.15s Attempt to create account '%s' failed.  Database is full.\n", TimeStr(), ip_str, input_account_record->sdb.user_id));
				return ERR_ERROR;
			}

			// Fill in the data for the new account.
			input_account_record->sdb.player_id = pid;
			input_account_record->sdb.valid_entry = TRUE;
			input_account_record->sdb.client_platform = client_platform;
			input_account_record->sdb.flags |= SDBRECORD_FLAG_EMAIL_NOT_VALIDATED;

			SDB->WriteRecord(&input_account_record->sdb);

        	SendEmailValidationCodeNewAccount(input_account_record->sdb.email_address,input_account_record->sdb.user_id);
			//SendClientInfo();	// always re-send client info (new flags, new email, etc.)


			//kriskoin: 			// sure we record the serial number of the computer they used to
			// open the account.
			if (!admin_computer_flag) {

				//kp(("%s(%d) computer #%d was NOT an admin computer... adding player id $%08lx to the key database.\n", _FL, client_platform.computer_serial_num, pid));
				PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
				SerNumDB_AddPlayerID(client_platform.computer_serial_num, pid);
				LeaveCriticalSection(&PlayerCritSec);
   		}

			if (DebugFilterLevel <= 2) {
				kp(("%s %-15.15s New account '%s' (%s) %s,%s,%s,%s,%s,%s\n",
					TimeStr(), ip_str,
					input_account_record->sdb.user_id,
					input_account_record->sdb.email_address,
					input_account_record->mrkinfo_banner_str,

					input_account_record->mrkinfo_magazine_str,
					input_account_record->mrkinfo_newsgroup_str,
					input_account_record->mrkinfo_website_str,
					input_account_record->mrkinfo_other_str,
					input_account_record->sdb.client_platform.vendor_code
				));
			}

			// Give them some play money (1000 for free to start)
			char reason[] = "Creating new play money account";
			
			AddChipsToAccount(pid, 200000, CT_PLAY, reason, FALSE, NULL, NULL);

			SDB->SetPrivLevel(pid, ACCPRIV_PLAY_MONEY, "Create new account");
			//cris
			//if (strcmp(input_account_record->sdb.user_id,"cristian")==0){
			//	SDB->SetPrivLevel(pid, ACCPRIV_SUPER_USER, "Create new admin account");
			//};//else
			//end cris

			SendMiscClientMessage(MISC_MESSAGE_CREATEACCOUNT_RESULT, 0, 0,
						0,	// error = success (no error)
						0,
						0,
						"");

			// Finally, log the marketing information
			AddToLog("Data/Logs/new_accounts.log",
					"Date/Time\tPlayerID\tUserID\tEmail\tEmailWhenReal\tBanner\tBanner\tMag\tMag\tNews\tNews\tWeb\tWeb\tOther\tOther\tWOM\tVendorCode\n",
					"%s\t0x%08lx\t%s\t%s\t%d\t"	// TimeStr(), player id, user id, email, email_when_real
					"%d\t%s\t%d\t%s\t%d\t%s\t%d\t%s\t%d\t%s\t%d\n",
					TimeStr(),
					input_account_record->sdb.player_id,
					input_account_record->sdb.user_id,
					input_account_record->sdb.email_address,
					!input_account_record->sdb.dont_use_email2,
					input_account_record->mrkinfo_banner_checked,
					input_account_record->mrkinfo_banner_str,
					input_account_record->mrkinfo_magazine_checked,
					input_account_record->mrkinfo_magazine_str,
					input_account_record->mrkinfo_newsgroup_checked,
					input_account_record->mrkinfo_newsgroup_str,
					input_account_record->mrkinfo_website_checked,
					input_account_record->mrkinfo_website_str,
					input_account_record->mrkinfo_other_checked,
					input_account_record->mrkinfo_other_str,
					input_account_record->mrkinfo_word_of_mouth_checked
			);

			// kriskoin  04/09/2002
			//Log Vendor_Code file
			FILE *stream;

			stream = fopen("Data/Logs/account_vendor.log", "a+");
			if(strlen(input_account_record->vendor_code)==0)
				fprintf(stream, "%08lx,\t%s,\t%s\n",
                                        input_account_record->sdb.player_id,
                                        input_account_record->sdb.user_id,
                                        "PC");
			else
				fprintf(stream, "%08lx,\t%s,\t%s\n",
                                        input_account_record->sdb.player_id,
                                        input_account_record->sdb.user_id,
                                        input_account_record->vendor_code);
			fclose(stream);
			// end kriskoin 


		}
		break;
	case ACCOUNTRECORDUSAGE_LOOKUP_USERID:
	case ACCOUNTRECORDUSAGE_LOOKUP_PLAYERID:
		{
			// Look up an account based on user id or player id...
			// You must be an administrator to perform this task
			if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
				SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
						"You must be at least customer support priv level to perform this action.");
				return ERR_ERROR;
			}
			
			struct AccountRecord ar;
			zstruct(ar);
			int index = -1;
			if (input_account_record->usage==ACCOUNTRECORDUSAGE_LOOKUP_USERID) {
				index = SDB->SearchDataBaseByUserID(input_account_record->sdb.user_id, &ar.sdb);
			} else {
				index = SDB->SearchDataBaseByPlayerID(input_account_record->sdb.player_id, &ar.sdb);
			}
		  #if 0 // testing only
			pr(("%s(%d) %08lx returned %d purchased for the last 24 hours\n", _FL,
			   ar.sdb.player_id, NetPurchasedInLastNHours(ar.sdb.player_id, 24)));
			pr(("%s(%d) %08lx returned %d purchased for the last 48 hours\n", _FL,
			   ar.sdb.player_id, NetPurchasedInLastNHours(ar.sdb.player_id, 48)));
		  #endif

			// If the priv level is higher than the current user, don't send it.
			if (ar.sdb.priv > priv) {
				zstruct(ar);
			}

			if (ar.sdb.player_id) {	// found them
				// Check if the player is logged in and if so, determine
				// which tables they are seated at.
				EnterCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
				Player * p = ((CardRoom *)cardroom_ptr)->FindPlayer(ar.sdb.player_id);
				if (p) {	// found them... they're logged in.
					ar.flags |= ACRF_LOGGED_IN;	// flag that they are logged in.
					// Record all the tables they are currently seated at.
					//EnterCriticalSection(&(p->PlayerCritSec));
					int count = 0;
					for (int i=0 ; i<p->JoinedTableCount ; i++) {
						if (p->JoinedTables[i].table_serial_number && !p->JoinedTables[i].watching_flag) {
							ar.seated_tables[count++] = p->JoinedTables[i].table_serial_number;
							count++;
						}
					}
					//LeaveCriticalSection(&(p->PlayerCritSec));
				}
				LeaveCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
			}

			// Send the resulting account record (found or not)
			SendDataStructure(DATATYPE_ACCOUNT_RECORD, &ar, sizeof(ar));
		}
		break;
	case ACCOUNTRECORDUSAGE_KICKOFF_PLAYERID:	// admin: kick someone off
		// Kick someone off the sytem based on player id...
		// You must be an administrator to perform this task
		{
			if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
				SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
						"You must be at least customer support priv level to perform this action.");
				return ERR_ERROR;
			}
			EnterCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
			Player * p = ((CardRoom *)cardroom_ptr)->FindPlayer(input_account_record->sdb.player_id);
			if (p) {	// found them... they're logged in.
				PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
				p->SetServerSocket(NULL);	// disconnect them instantly.
				LeaveCriticalSection(&PlayerCritSec);
			}
			LeaveCriticalSection(&((CardRoom *)cardroom_ptr)->PlrInputCritSec);
		}
		break;
	case ACCOUNTRECORDUSAGE_RESET_ALLINS_FOR_PLAYERID:	// admin: reset all-ins for a playerid
		// Reset all of the all-ins for a particular player id
		// You must be an administrator to perform this task
		{
			if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
				SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
						"You must be at least customer support priv level to perform this action.");
				return ERR_ERROR;
			}
			if (input_account_record->sdb.player_id) {
				ResetAllInsForPlayer(input_account_record->sdb.player_id,
									 input_account_record->flags & ACRF_SEND_ALL_IN_RESET_EMAIL);
			}
		}
		break;
		
		//cris 15-2-2004
  case ACCOUNTRECORDUSAGE_MODIFY_PLAYER_ACCOUNT_COMMSERVER:
	 	  kp(("ACCOUNTRECORDUSAGE_MODIFY_PLAYER_ACCOUNT_COMMSERVER called \n"));
  #if 1
	    struct SDBRecord ar;
			zstruct(ar);
			struct SDBRecord *ir;
			ir = &input_account_record->sdb;
			int index ;
			index = SDB->SearchDataBaseByUserID(ir->user_id, &ar);
			if (index >= 0) {	// found it...
			// copy the fields we're allowed to modify...				
				// max priv level they can set is max of what it used to be and what theirs is.
				int max_priv = max(priv, ar.priv);
				ar.valid_entry = ir->valid_entry;
				strnncpy(ar.password, ir->password, MAX_PLAYER_PASSWORD_LEN);
				strcpy(ar.refered_by, ir->refered_by);
				strnncpy(ar.city, ir->city, MAX_COMMON_STRING_LEN);
				//strcpy(ar.city, ir->city);
				strnncpy(ar.secure_phrase, ir->secure_phrase, MAX_PLAYER_SECURE_PHRASE_LEN);
				strnncpy(ar.gender1 , ir->gender1,7);
				if((strcmp(ar.gender1,"Male")==0)||(strcmp(ar.gender1,"male")==0)){
					ar.gender=GENDER_MALE;
				}else{
					ar.gender=GENDER_FEMALE;
				};//if
				if (strcmp(ar.email_address,ir->email_address) != 0){
						strnncpy(ar.email_address, ir->email_address, MAX_EMAIL_ADDRESS_LEN);
						ar.flags |= SDBRECORD_FLAG_EMAIL_NOT_VALIDATED;
				}
				if (ar.flags & (SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES)) {
					// Automatically generate an email validate code letter
					SendEmailValidationCode(ar.email_address);
				}
				SDB->WriteRecord(&ar);
				if (CardRoomPtr) {
					CardRoomPtr->SendClientInfo(ir->player_id);
				}
				//loggin PROCOMM struct
				sprintf(strLog,"Player Account %s %s\n",ir->user_id,TimeStr());
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				sprintf(strLog," IR user_id:%s pass: %s redered_by: %s city %s secure_phrase %s email %s\n",
				!ir->user_id?"NULL":ir->user_id,!ir->password?"NULL":ir->password,
				!ir->refered_by?"NULL":ir->refered_by,!ir->city?"NULL":ir->city,
				!ir->secure_phrase?"NULL":ir->secure_phrase,!ir->email_address?"NULL":ir->email_address
				);
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				
				sprintf(strLog," AR user_id:%s pass: %s redered_by: %s city %s secure_phrase %s email %s\n\n",
				!ar.user_id?"NULL":ar.user_id,!ar.password?"NULL":ar.password,
				!ar.refered_by?"NULL":ar.refered_by,!ar.city?"NULL":ar.city,
				!ar.secure_phrase?"NULL":ar.secure_phrase,!ar.email_address?"NULL":ar.email_address
				);
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				
				//AddToLog("Data/PROCOMM/EditProcommServer.log","AR user_id:%s pass: %s redered_by: %s city %s secure_phrase %s email %s\n\n",ar.user_id,ar.password,ar.refered_by,ar.city,ar.secure_phrase,ar.email_address);
				
			}else{
				kp(("\n\nERROR MODIFY_PLAYER_ACCOUNT_COMMSERVER can't find user_id %s \n",ir->user_id));
			};//if (index >= 0) 
			
			
			
  #endif
		break;
 case ACCOUNTRECORDUSAGE_MODIFY_PLAYER_INFO_COMMSERVER:
	 kp(("ACCOUNTRECORDUSAGE_MODIFY_PLAYER_INFO_COMMSERVER called \n"));
  #if 1
	    struct SDBRecord ar2;
			zstruct(ar2);
			struct SDBRecord *ir2;
			ir2 = &input_account_record->sdb;
			int index2 ;			
			index2 = SDB->SearchDataBaseByUserID(ir2->user_id, &ar2);
			if (index2 >= 0) {	// found it...				
				
				//full name ->firstname
				strnncpy(ar2.full_name, ir2->full_name,MAX_PLAYER_FULLNAME_LEN);
				//last name
				strnncpy(ar2.last_name, ir2->last_name,MAX_PLAYER_LASTNAME_LEN);
				//middle name initial
				strnncpy(ar2.user_mi, ir2->user_mi,3);
				//mailing address1
				strnncpy(ar2.mailing_address1, ir2->mailing_address1,MAX_PLAYER_ADDRESS_LEN);
				//mailing address2
				strnncpy(ar2.mailing_address2, ir2->mailing_address2,MAX_PLAYER_ADDRESS_LEN);
				//mailing_address_state
				strnncpy(ar2.mailing_address_state, ir2->mailing_address_state,MAX_COMMON_STRING_LEN);
				//mailing_address_postal_code
				strnncpy(ar2.mailing_address_postal_code, ir2->mailing_address_postal_code,MAX_COMMON_STRING_LEN);
				//phone_number
				strnncpy(ar2.phone_number, ir2->phone_number,PHONE_NUM_LEN);
				//alternative_phone
				strnncpy(ar2.alternative_phone, ir2->alternative_phone,PHONE_NUM_LEN);
				//birth_date
				strnncpy(ar2.birth_date, ir2->birth_date,MAX_COMMON_STRING_LEN);
				kp(("AR: %s IR: %s \n",ar2.birth_date, ir2->birth_date));
				//mailing_address_country
				strnncpy(ar2.mailing_address_country, ir2->mailing_address_country,MAX_COMMON_STRING_LEN);
				//Gender
				strnncpy(ar2.gender1, ir2->gender1,7);
				if((strcmp(ar2.gender1,"Male")==0)||(strcmp(ar2.gender1,"male")==0)){
					ar2.gender=GENDER_MALE;
				}else{
					ar2.gender=GENDER_FEMALE;
				};//if
				//comments
				strnncpy(ar2.comments, ir2->comments,254);              
				SDB->WriteRecord(&ar2);
				if (CardRoomPtr) {
					CardRoomPtr->SendClientInfo(ir2->player_id);
				}
				//logging this transaction
				sprintf(strLog,"Player INFO %s %s\n",!ir2->user_id?"NULL":ir2->user_id,TimeStr());
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				sprintf(strLog,"IR user_id: %s First_name: %s Last_name %s Mi %s mail_addr1: %s mail_addr2: %s addr_state %s addr_postal_code: %s phone_num: %s  alt_phone: %s birth_date:%s country: %s gender1: %s gender: %d\n",  
				     !ir2->user_id?"NULL":ir2->user_id, !ir2->full_name?"NULL":ir2->full_name,
				     !ir2->last_name?"NULL":ir2->last_name, !ir2->user_mi?"NULL":ir2->user_mi,
					 !ir2->mailing_address1?"NULL":ir2->mailing_address1,!ir2->mailing_address2?"NULL":ir2->mailing_address2,
				     !ir2->mailing_address_state?"NULL":ir2->mailing_address_state,!ir2->mailing_address_postal_code?"NULL":ir2->mailing_address_postal_code,
				     !ir2->phone_number?"NULL":ir2->phone_number,!ir2->alternative_phone?"NULL":ir2->alternative_phone,
				     !ir2->birth_date?"NULL":ir2->birth_date,!ir2->mailing_address_country?"NULL":ir2->mailing_address_country,
				     !ir2->gender1?"NULL":ir2->gender1,ir2->gender
				);
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				sprintf(strLog,"AR user_id: %s First_name: %s Last_name %s Mi %s mail_addr1: %s mail_addr2: %s addr_state %s addr_postal_code: %s phone_num: %s  alt_phone: %s birth_date:%s country: %s gender1: %s gender: %d\n\n",  
				     !ar2.user_id?"NULL":ar2.user_id, !ar2.full_name?"NULL":ar2.full_name,
				     !ar2.last_name?"NULL":ar2.last_name, !ar2.user_mi?"NULL":ar2.user_mi,
					 !ar2.mailing_address1?"NULL":ar2.mailing_address1,!ar2.mailing_address2?"NULL":ar2.mailing_address2,
				     !ar2.mailing_address_state?"NULL":ar2.mailing_address_state,!ar2.mailing_address_postal_code?"NULL":ar2.mailing_address_postal_code,
				     !ar2.phone_number?"NULL":ar2.phone_number,!ar2.alternative_phone?"NULL":ar2.alternative_phone,
				     !ar2.birth_date?"NULL":ar2.birth_date,!ar2.mailing_address_country?"NULL":ar2.mailing_address_country,
				     !ar2.gender1?"NULL":ar2.gender1,ar2.gender
				);
				AddToLog("Data/PROCOMM/EditProcommServer.log","%s",strLog);
				//AddToLog("Data/Logs/EditProcommServer.log","IR: user_id %s full_name: %s \n",(ir2->user_id==NULL ? "NULL": ir2->user_id),(ir2->full_name==NULL ? "NULL" : ir2->full_name));
			}else{
				kp(("\n\nERROR MODIFY_PLAYER_INFO_COMMSERVER can't find user_id %s \n",ir2->user_id));
			};//if (index >= 0) 
  #endif
		break;
		//end cris 15-2-2004
		
	case ACCOUNTRECORDUSAGE_MODIFY:	// admin: modify anyone
		{
			// Modify an account based on player id...
			// You must be an administrator to perform this task
			if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
				SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
						"You must be at least customer support priv level to perform this action.");
				return ERR_ERROR;
			}
			
			// First, look up the existing record.

			struct SDBRecord ar;
			zstruct(ar);
			struct SDBRecord *ir = &input_account_record->sdb;
			int index = SDB->SearchDataBaseByPlayerID(ir->player_id, &ar);
			if (index >= 0) {	// found it...
				// copy the fields we're allowed to modify...

				// if the user id is support to be changed, make sure the player
				// is not presently online.
				if (strcmp(ar.user_id, ir->user_id)) {
					Player * p = ((CardRoom *)cardroom_ptr)->FindPlayer(ir->player_id);
					if (p) {	// found them... they're logged in.
						SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
								"Cannot change user_id (player name) while they are online.");
					} else {
						strnncpy(ar.user_id, ir->user_id, MAX_PLAYER_USERID_LEN);
					}
				}

				// other fields...
				ar.gender = ir->gender;
				//cris 15-02-2003
					if(ar.gender=GENDER_MALE){
						strcpy(ar.gender1,"FEMALE");
					}else{
						strcpy(ar.gender1,"FEMALE");
					};//if				
				//end cris 15-02-2003
				ar.valid_entry = ir->valid_entry;
				// max priv level they can set is max of what it used
				// to be and what theirs is.
				int max_priv = max(priv, ar.priv);
				ar.priv = (BYTE8)min(ir->priv, max_priv);	// set to less of requested and max possible
				ar.dont_use_email1 = ir->dont_use_email1;
				ar.dont_use_email2 = ir->dont_use_email2;
				ar.flags = ir->flags;
				strnncpy(ar.password, ir->password, MAX_PLAYER_PASSWORD_LEN);

			  #if 0	// 2022 kriskoin
				strnncpy(ar.password_rm, ir->password_rm, MAX_PLAYER_PASSWORD_LEN);
			  #endif
				strnncpy(ar.city, ir->city, MAX_COMMON_STRING_LEN);
				strnncpy(ar.full_name, ir->full_name, MAX_PLAYER_FULLNAME_LEN);
				strnncpy(ar.email_address, ir->email_address, MAX_EMAIL_ADDRESS_LEN);
				strnncpy(ar.mailing_address1, ir->mailing_address1, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address2, ir->mailing_address2, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address_state, ir->mailing_address_state, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_country, ir->mailing_address_country, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_postal_code, ir->mailing_address_postal_code, MAX_COMMON_STRING_LEN);
				memcpy(ar.phone_number, ir->phone_number, PHONE_NUM_LEN);	// 20:::			  #if 1	// 2022 kriskoin
				memcpy(ar.admin_notes, ir->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN);
			  #else
				strnncpy(ar.admin_notes, ir->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN);
			  #endif

				// cc limit overrides
				ar.cc_override_limit1 = ir->cc_override_limit1;
				ar.cc_override_limit2 = ir->cc_override_limit2;
				ar.cc_override_limit3 = ir->cc_override_limit3;
				// kriskoin 
				ar.fee_credit_points = ir->fee_credit_points;
				kp(("%s(%d), here fee_credit_points = %d\n", _FL, ar.fee_credit_points));
				// end kriskoin 
				SDB->WriteRecord(&ar);

				// If they're connected, re-send the new version to their client.
				if (CardRoomPtr) {
					CardRoomPtr->SendClientInfo(ir->player_id);
				}
			}
		}
		break;
	case ACCOUNTRECORDUSAGE_UPDATE:	// user: update some fields for ourselves
		{
			// First, look up the existing record.
			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
			int email_changed = FALSE;	// keep track if the email address changed
			if (index >= 0) {	// found it...
				struct SDBRecord *ir = &input_account_record->sdb;
				// Fill in the fields we're allowed to copy...
				ar.gender = ir->gender;
				if (stricmp(ar.email_address, ir->email_address)) {
					email_changed = TRUE;
				}
			  #if 0	// 2022 kriskoin
				strnncpy(ar.password_rm, ir->password_rm, MAX_PLAYER_PASSWORD_LEN);
			  #endif
				strnncpy(ar.city, ir->city, MAX_COMMON_STRING_LEN);
				strnncpy(ar.full_name, ir->full_name, MAX_PLAYER_FULLNAME_LEN);
				strnncpy(ar.last_name, ir->last_name, MAX_PLAYER_LASTNAME_LEN);
				// these may be blank; it doesn't matter
				strnncpy(ar.mailing_address1, ir->mailing_address1, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address2, ir->mailing_address2, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address_state, ir->mailing_address_state, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_country, ir->mailing_address_country, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_postal_code, ir->mailing_address_postal_code, MAX_COMMON_STRING_LEN);

				//J Fonseca  16/02/2004
				strnncpy(ar.user_mi, ir->user_mi, 3);
				strnncpy(ar.phone_number, ir->phone_number, PHONE_NUM_LEN);
				strnncpy(ar.alternative_phone, ir->alternative_phone, PHONE_NUM_LEN);
				strnncpy(ar.birth_date, ir->birth_date, MAX_COMMON_STRING_LEN);
				strnncpy(ar.gender1, ir->gender1, 7);
				//J Fonseca  16/02/2004

				if (client_version_number.build >= 0x01070002) {// does the client include the email validation code?
					strnncpy(ar.email_address, ir->email_address, MAX_EMAIL_ADDRESS_LEN);
					// an address change only would not include this information
					if (email_changed) {
						ar.flags |= SDBRECORD_FLAG_EMAIL_NOT_VALIDATED;
					}
					if (ar.flags & (SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES)) {
						// Automatically generate an email validate code letter
						SendEmailValidationCode(ar.email_address);
					}
				}

				// If this account is currently play money only, check to see if
				// there is enough information for it to be upgraded to real money.
				// don't do this if it was only an address change
				if (priv == ACCPRIV_PLAY_MONEY) {
					// Try to validate for real money and change priv if necessary
					int rm_valid = TRUE;
				  #if 0	// 2022 kriskoin
					if (strlen(ar.password_rm) < 2) rm_valid = FALSE;
				  #endif
					if (strlen(ar.city) < 2) rm_valid = FALSE;
					if (strlen(ar.full_name) < 4) rm_valid = FALSE;
					if (strlen(ar.last_name) < 4) rm_valid = FALSE;
					if (strlen(ar.email_address) < 6) rm_valid = FALSE;
					if (strlen(ar.mailing_address1) < 4) rm_valid = FALSE;
					if (strlen(ar.mailing_address_country) < 2) rm_valid = FALSE;
					if (rm_valid) {
						priv = ar.priv = ACCPRIV_REAL_MONEY;	// upgrade to real money priv
					}
				}
				SDB->WriteRecord(&ar);
				SendClientInfo();	// re-send latest version to client

#if 1
/**************************J. Fonseca   10/02/2004 ****************************************/
  		sprintf(sql,"SELECT sp_new_realmoney_account ('%s','%s','%s','%s', '%s','%s','%s','%s','%s','%s','%s','%s','%s',CURRENT_DATE,TRUE,'%s')",ar.full_name,ar.last_name, ar.mailing_address1, ar.mailing_address2, ar.city,ar.mailing_address_state, ar.mailing_address_country,ar.mailing_address_postal_code, ar.phone_number, ar.gender1,ar.user_mi,ar.alternative_phone,ar.birth_date,ar.user_id);
  	((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);			

/***************************J. Fonseca   10/02/2004 ****************************************/
#endif

				// Check if this is a duplicate account for this computer for a new account.
				// we don't need to do this for simple address updates
				if (priv >= ACCPRIV_REAL_MONEY) {
					SerNumDB_CheckForDupes(client_platform.computer_serial_num, FALSE, TRUE);
				}
			}
		}
		break;

	case ACCOUNTRECORDUSAGE_SUBMIT_ADDRESS:	// user submitted address change
		// 24/01/01 kriskoin:
		// be sure to add them here -- we only copy a few of the account record fields
		{
			
  	// First, look up the existing record.
			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
			if (index >= 0) {	// found it...
				struct SDBRecord *ir = &input_account_record->sdb;
				// we can make this test not sensitive to CAPS... LAS VEGAS -> Las Vegas
				// is not something we need to know about
				int something_important_changed = (
					stricmp(ar.city, ir->city) ||
					stricmp(ar.mailing_address1, ir->mailing_address1) ||
					stricmp(ar.mailing_address2, ir->mailing_address2) ||
					stricmp(ar.mailing_address_state, ir->mailing_address_state) ||
					stricmp(ar.mailing_address_country, ir->mailing_address_country) ||
					stricmp(ar.mailing_address_postal_code, ir->mailing_address_postal_code) ||
					stricmp(ar.phone_number, ir->phone_number) ||
					stricmp(ar.alternative_phone, ir->alternative_phone)||
					stricmp(ar.last_name, ir->last_name)||
					stricmp(ar.full_name, ir->full_name)||
					stricmp(ar.gender1, ir->gender1) ||
					stricmp(ar.birth_date, ir->birth_date)
				);
				int new_count = 0;	// we'll never see this number if the email isn't sent out
				if (something_important_changed) {
					new_count = SDB->IncrementAddressChangeCount(player_id);
				}
				// Send an email to support
				char fname[MAX_FNAME_LEN];
				zstruct(fname);
				MakeTempFName(fname, "adc");
				FILE *fd = fopen(fname, "wt");
				if (fd) {
					if (!iRunningLiveFlag) {
						fprintf(fd, "*** THIS IS A ONLY A TEST: IGNORE IT ***\n");
					}
					fprintf(fd, "An address update was processed for '%s' ($%08lx)\n\n",ar.user_id, ar.player_id);
					fprintf(fd,"This is address change #%d for this player\n\n", new_count);
					if (stricmp(ar.mailing_address_country, ir->mailing_address_country)) {
						fprintf(fd,"** NOTE: Country is changing from %s to %s **\n\n",
							ar.mailing_address_country,
							ir->mailing_address_country);
					}
				
					fprintf(fd, "OLD info:\n"
						"   %s\n"
						"   %s\n"
						"%s%s%s"
						"   %s, %s, %s\n"
						"   %s\n"
						"   %s\n\n",
						ar.full_name,
						ar.mailing_address1,
						ar.mailing_address2[0] ? "   " : "",
						ar.mailing_address2,
						ar.mailing_address2[0] ? "\n" : "",
						ar.city,
						ar.mailing_address_state,
						ar.mailing_address_country,
						ar.mailing_address_postal_code,
						DecodePhoneNumber(ar.phone_number));
				}
				// copy in the new stuff
				// Fill in the fields we're allowed to copy...
				strnncpy(ar.city, ir->city, MAX_COMMON_STRING_LEN);
				// these may be blank; it doesn't matter
				strnncpy(ar.mailing_address1, ir->mailing_address1, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address2, ir->mailing_address2, MAX_PLAYER_ADDRESS_LEN);
				strnncpy(ar.mailing_address_state, ir->mailing_address_state, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_country, ir->mailing_address_country, MAX_COMMON_STRING_LEN);
				strnncpy(ar.mailing_address_postal_code, ir->mailing_address_postal_code, MAX_COMMON_STRING_LEN);
				strnncpy(ar.phone_number, ir->phone_number, PHONE_NUM_LEN);	

				//J Fonseca    16/02/2004
				strnncpy(ar.alternative_phone, ir->alternative_phone, PHONE_NUM_LEN);	
				strnncpy(ar.full_name, ir->full_name, MAX_PLAYER_FULLNAME_LEN);
				strnncpy(ar.last_name, ir->last_name, MAX_PLAYER_LASTNAME_LEN);
				strnncpy(ar.user_mi, ir->user_mi, 3);
				strnncpy(ar.birth_date, ir->birth_date, MAX_COMMON_STRING_LEN);
				strnncpy(ar.gender1, ir->gender1, 7);

				SDB->WriteRecord(&ar);
				SendClientInfo();	// re-send latest version to client

#if 1
/***************************J. Fonseca   10/02/2004 ****************************************/
 		sprintf(sql,"SELECT sp_edit_player_info('%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s','%s')",ar.full_name,ar.last_name, ar.mailing_address1, ar.mailing_address2, ar.city,ar.mailing_address_state, ar.mailing_address_country,ar.mailing_address_postal_code, ar.phone_number, ar.gender1,ar.user_mi,ar.alternative_phone,ar.birth_date,ar.user_id);
		((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);	  	

/***************************J. Fonseca   10/02/2004 ****************************************/
#endif

				int send_the_email = TRUE;
				// 24/01/01 kriskoin:
				if (priv == ACCPRIV_PLAY_MONEY){
					send_the_email = FALSE;
				}
				// 24/01/01 kriskoin:
				if (new_count < 3) {
					send_the_email = FALSE;
				}
				// is it worth hearing about?
				if (!something_important_changed) {
					send_the_email = FALSE;
				}

				if (fd) {
					fprintf(fd, "NEW info:\n"
						"   %s\n"
						"   %s\n"
						"%s%s%s"
						"   %s, %s, %s\n"
						"   %s\n"
						"   %s\n\n",
						ar.full_name,
						ar.mailing_address1,
						ar.mailing_address2[0] ? "   " : "",
						ar.mailing_address2,
						ar.mailing_address2[0] ? "\n" : "",
						ar.city,
						ar.mailing_address_state,
						ar.mailing_address_country,
						ar.mailing_address_postal_code,
						DecodePhoneNumber(ar.phone_number));

					FCLOSE(fd);
					if (send_the_email) {
						char subject[80];
						zstruct(subject);
						sprintf(subject,"Address change for %s #%d", ar.user_id, new_count);
						Email(	"addresschange@kkrekop.io",
							"Address Change",
							"addresschange@kkrekop.io",
							subject,
							fname,
							NULL,	// bcc:
							TRUE);	// delete when sent
					} else {	// just delete it
						remove(fname);
					}
				}
			}
		}
		
//		SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
//                      "Your Mailling Address has been successfully changed.\n" );
				
			SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0, 0, 0, 0,
                      "Your Information has been successfully changed.\n" );		 //Fonseca

		break;
	
	case ACCOUNTRECORDUSAGE_CHANGE_PASSWORD:	// user: change password
		{

			// First, look up the existing record.
			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
			if (index >= 0) {	// found it...
				struct SDBRecord *ir = &input_account_record->sdb;
				strnncpy(ar.password, ir->password, MAX_PLAYER_PASSWORD_LEN);
				SDB->WriteRecord(&ar);
				SendClientInfo();	// re-send latest version to client
#if 1
/***************************J. Fonseca   14/11/2003 ****************************************/
	  	sprintf(sql,"update players_account set password='%s' where login_name='%s'", ar.password,ar.user_id);
			((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);	  	
/***************************J. Fonseca   14/11/2003 ****************************************/
#endif



			}
		}
		break;
	case ACCOUNTRECORDUSAGE_CHANGE_EMAIL:	// user: change email address
		{

			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			
			// 2001-11-01 added by allen ko
			// to add the function that when a player change their email, it should not
			// be the same with any email in the database.
                     	
			/*if (SDB->SearchDataBaseByEmail(input_account_record->sdb.email_address) >= 0) {
				SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED, 0, 0,
						1,
						0, 0,
						"The Email Address you have typed in (%s) is already\n"
						"in use by another customer.\n\n"
						"Please choose another Email Address.", input_account_record->sdb.email_address);
				return ERR_ERROR;

			}*/
			
			// 2001-11-01 added by allen ko
			// to add the function that when a player change their email, it should not
			// be the same with any email in the database.

			int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
			if (index >= 0) {	// found it...
				struct SDBRecord *ir = &input_account_record->sdb;
				// Keep track of whether the email address has actually changed.
				int different = stricmp(ar.email_address, ir->email_address);
				// Save the new one (even if it's essentially the same)
				strnncpy(ar.email_address, ir->email_address, MAX_EMAIL_ADDRESS_LEN);

				// If it was different, mark the email as invalid.
				if (different) {
					ar.flags |= SDBRECORD_FLAG_EMAIL_NOT_VALIDATED;
				}
				SDB->WriteRecord(&ar);
				SendEmailValidationCode(ar.email_address);
				SendClientInfo();	// always re-send client info (new flags, new email, etc.)
#if 1
/***************************J. Fonseca   14/11/2003 ****************************************/
				//update email in the account
 	  		sprintf(sql,"update players_account set email='%s' where login_name= '%s'", ar.email_address,ar.user_id);
				((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);	  	
/***************************J. Fonseca   14/11/2003 ****************************************/
#endif

			}
		}
		break;
	case ACCOUNTRECORDUSAGE_SENDPASSWORD:	// send this player their password via email
		{
			// First, look up the existing record.
			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			struct SDBRecord *ir = &input_account_record->sdb;
			int index = SDB->SearchDataBaseByPlayerID(ir->player_id, &ar);
			if (index >= 0) {	// found it...
				// build the body of the email
				char str[1000];
				zstruct(str);
				sprintf(str,
					"%sThis email has generated as a result of a request\n"
					"for your Desert Poker player ID password. \n"
					"This password is only sent to the email address\n"
					"that is registered to your account:\n"
					"\n"
					"Player ID:  %s\n"
					"Password:  %s  (passwords are case-sensitive)\n"
					"\n"
					"If you wish to change your password, you may do so once\n"
					"you've logged-in by selecting \"Change Password\" under\n"
					"the \"Options\" menu on the Main DesertPoker screen.\n"
					"\n"
					"\n"

					"Regards,\n"
					"\n"
					"Desert Poker Support\n"
					"\n"
					"-------------\n"
					"If you feel that your password was sent to you\n"
					"in error, please contact us at support@kkrekop.io\n",
					(iRunningLiveFlag ? "" : "*** THIS IS ONLY A TEST ***\n\n"),
					ar.user_id,
					ar.password);					
	
				// ship the email
				EmailStr(
					ar.email_address, 					// to:
					"Desert Poker Customer Support",	// from (name):
					"support@kkrekop.io",		// from (email):
					"Password for Desert Poker",		// subject
					"answers@kkrekop.io",		// bcc:
					"%s", str);
			}
		}
		break;


	case ACCOUNTRECORDUSAGE_SUBMIT_EMAIL_VALIDATION:	// user: email address was validated
		{

			// First, look up the existing record.
			struct SDBRecord ar;
			zstruct(ar);
			if (!player_id || ANONYMOUS_PLAYER(player_id) || LoginStatus!=LOGIN_VALID) {
				return ERR_ERROR;	// error: not logged in properly.
			}
			int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
			if (index >= 0) {	// found it...
				ar.flags &= ~(SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES);
				SDB->WriteRecord(&ar);
				SendClientInfo();	// always re-send client info (new flags, new email, etc.)
			}
		}
		break;
	default:
		kp(("%s %-15.15s %s(%d) Unknown/unhandled AccountRecord usage (%d)\n",
				TimeStr(), ip_str, _FL, input_account_record->usage));
		break;
	}
	return ERR_NONE;	
}

//*********************************************************
// https://github.com/kriskoin//
// Process a request from this client to have their all-ins reset.
//
#define MINIMUM_ELAPSED_SECONDS_FOR_ALL_IN	24*60*60	// 24 hours
void Player::ProcessAllInResetRequest(void)
{
	// First, review the recent all-ins for this player and determine if any
	// of the relevant ones were 'good' connections.  If they were, send an
	// email to allinreset@kkrekop.io so it can be done manually.
	int process_manually = FALSE;	// default to automatic processing

	WORD32 flags = 0;
	int hands_to_retrieve = 0;
	// the rule at the moment is 'int AllInsAllowed' per 24 hours
	SDBRecord player_rec;	// the result structure
	zstruct(player_rec);
	if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
		flags = player_rec.flags;
		if (flags & SDBRECORD_FLAG_NO_ALLIN_RESET) {
			process_manually = TRUE;	// force to manual processing only.
		}
		time_t now = time(NULL);
		int good_allins = 0;
		for (int i=0 ; i<ALLINS_TO_RECORD_PER_PLAYER ; i++) {
			// player all in times 0->n goes from most recent to oldest...
			// If this entry is relevant and good, flag process manually.
			if (!player_rec.all_in_times[i]) {
				break;	// last reset point reached... don't look any further.
			}
			if (player_rec.all_in_times[i] &&
				player_rec.all_in_times[i] >= AllInsResetTime &&
				now - player_rec.all_in_times[i] < MINIMUM_ELAPSED_SECONDS_FOR_ALL_IN &&
				player_rec.all_in_connection_state[i] == CONNECTION_STATE_GOOD
			) {
				// At least one relevant good one... process manually.
				good_allins++;
				hands_to_retrieve = i+1;
			}
		}
		if (good_allins > GoodAllInsAllowed) {	// too many to process automatically?
			process_manually = TRUE;
		}
	} else {	// couldn't find it... that's odd.
		process_manually = TRUE;
	}

	if (process_manually) {
		// Send an email to support
		char fname[MAX_FNAME_LEN];
		zstruct(fname);
		MakeTempFName(fname, "air");
		FILE *fd2 = fopen(fname, "wt");
		if (fd2) {
			if (!iRunningLiveFlag) {
				fprintf(fd2, "*** THIS IS A SAMPLE ONLY: DO NOT RESET ***\n\n");
			}
			fprintf(fd2, "An all-in reset request was received from %s\n", user_id);
			fprintf(fd2, "Real name: %s\n", player_rec.full_name);
			fprintf(fd2, "Email: %s\n", player_rec.email_address);
			fprintf(fd2, "\n");
			fprintf(fd2, "They currently have %d all-ins left.\n", AllowedAutoAllInCount());
			fprintf(fd2, "Last %d real money all-in history (CST):\n", ALLINS_TO_RECORD_PER_PLAYER);
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
				fprintf(fd2, "#%d:", i+1);
				if (player_rec.all_in_times[i]) {
					fprintf(fd2, " %s (%s) game # %d",
							TimeStr(player_rec.all_in_times[i], FALSE, TRUE, SERVER_TIMEZONE),
							connection_state,
							player_rec.all_in_game_numbers[i]);
					if (i < hands_to_retrieve) {
						fprintf(fd2, " (hand history requested)");
					}
				}
				fprintf(fd2, "\n");
			}
			fprintf(fd2, "\n");
			fprintf(fd2, "Total hands played: %d\n", player_rec.hands_seen);
			if (flags & SDBRECORD_FLAG_NO_ALLIN_RESET) {
				fprintf(fd2, "\nThis player is flagged as always needing manual resets.\n");
			}
			fprintf(fd2, "\n");
			fclose(fd2);
			// Now email it...
			char subject[100];
			zstruct(subject);
			sprintf(subject, "All-In Reset request for %s", user_id);
			Email(	"allinreset@kkrekop.io",
					player_rec.full_name,
					player_rec.email_address,
					subject,
					fname,
					NULL,	// bcc:
					TRUE);	// delete when sent

			// queue a hand history request if necessary and email it to
			// allinreset@kkrekop.io
			if (hands_to_retrieve) {
				SDBRecord plr_rec2;
				zstruct(plr_rec2);
				SDB->SearchDataBaseByUserID("AllInReset", &plr_rec2);
				if (plr_rec2.player_id) {
					PL->QueueHandHistoryReq(HHRT_LAST_N_ALLIN_HANDS,
						hands_to_retrieve,
						plr_rec2.player_id,	// the player id we want to email them to
						player_id,			// the player id who's games we want
						FALSE);
				}
			}
		}
	} else {
		// Add it to the automatic reset queue.  We need to pick a
		// time in the future to reset them as well as supply the player id.
		// Delay should be 3-10 minutes.
	  #if 0 // 24/01/01 kriskoin:
		AddFutureAllInResetToQueue(SecondCounter + 3*60 + random(7*60), player_id);
	  #else
		AddFutureAllInResetToQueue(SecondCounter + 2*60 + random(3*60), player_id);
	  #endif
	}
}

/**********************************************************************************
 Function Player::ChipBalancesHaveChanged(void)
 Date: 2017/7/7 kriskoin Purpose: update chip balances, and sent it by default
***********************************************************************************/
void Player::ChipBalancesHaveChanged(void)
{
	ChipBalancesHaveChanged(TRUE);
}

/**********************************************************************************
 Function Player::ChipBalancesHaveChanged
 Date: 2017/7/7 kriskoin Purpose: notify client that chips balances have been changed
***********************************************************************************/
void Player::ChipBalancesHaveChanged(int send_it_now)
{
	// reload them and send them off
	RealInBank = SDB->GetChipsInBankForPlayerID(player_id, CT_REAL);
	RealInPlay = SDB->GetChipsInPlayForPlayerID(player_id, CT_REAL);
	FakeInBank = SDB->GetChipsInBankForPlayerID(player_id, CT_PLAY);
	FakeInPlay = SDB->GetChipsInPlayForPlayerID(player_id, CT_PLAY);
	EcashCredit = SDB->GetPendingCreditForPlayerID(player_id);

	// kriskoin  20020419
	//reset cashout and "promo game to play" for those who lost most of their money
	if ((RealInBank+RealInPlay)<2000 && SDB->GetCreditFeePoints(player_id)){
	//if their account_balance is less than $20
		SDB->ClearCreditFeePoints(player_id);
		EnableCashoutForPlayer(player_id);
	}
	// end kriskoin 

	CreditFeePoints = SDB->GetCreditFeePoints(player_id);

	if (send_it_now) {
	  #if 1	// 2022 kriskoin
		send_account_info = TRUE;
	  #else
		SendAccountInfo();
	  #endif
	}
}

/**********************************************************************************
 Function *Player::GetEcashPostEstimateString(char *result_str)
 date: 24/01/01 kriskoin Purpose: build a string with a good guess about how long an ecash POST will take
 note: HK did not document how long the string we store the message to must be.
***********************************************************************************/
char *Player::GetEcashPostEstimateString(char *time_estimate_str)
{
	// calculate the estimated elapsed time in seconds before
	// this transactions is likely to be completed.
	int people_ahead_of_us = Ecash_GetQueueLen();
  #if 0	//kriskoin: 	people_ahead_of_us = 120;
	iEcashPostTime = 222;
  #endif
	if (ECashThreads > 1) {
		people_ahead_of_us /= ECashThreads;
	}
	int estimated_time = (people_ahead_of_us + 1) * iEcashPostTime;
	int estimated_minutes = (estimated_time + 59) / 60;
	if (!estimated_time) {	// default line - unknown time estimate
		sprintf(time_estimate_str, "This may take a few minutes. Please be patient.");
	} else if (estimated_minutes <= 1) {	// less than a minute?
		sprintf(time_estimate_str, "Your transaction should be processed within one minute.");

	} else if (estimated_minutes < 8) {		// less than n minutes?
		sprintf(time_estimate_str, "Your transaction should be processed within %d minutes.", estimated_minutes);
	} else  {
		sprintf(time_estimate_str,
				"THE CASHIER IS VERY BUSY. There are %d other people ahead of you.\n"
				"*** THIS MIGHT TAKE %d MINUTES TO PROCESS. ***", people_ahead_of_us, estimated_minutes);
	}
	return time_estimate_str;
}


/********************************************************************************
Player::AddRobotTable : Adds a robot in to a especific table
Cris July 30 2003

*********************************************************************************/
ErrorType Player::AddRobotTable(struct TableInfoRobot* tableInfo, int structLen){
	if (sizeof(*tableInfo) != structLen) {
		Error(ERR_ERROR, "%s(%d) ROBOT JoinTable request was wrong length (%d vs %d).",_FL,sizeof(*tableInfo),structLen);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	} else {
   		AddToLog("Data/Logs/AddRobot.log","Function AddRobotTable Processed on the server \n","Llamada a :%s  Table serial number: (%d) Position (%d)\n",TimeStr(),tableInfo->table_serial_number,tableInfo->pos);
			//	Table *t = ((CardRoom*)cardroom_ptr)->TableSerialNumToTablePtr(tableInfo->table_serial_number);
			//t->AddComputerPlayer(tableInfo->pos);
			return ERR_NONE;
	};
};//Player::AddRobotTable

/********************************************************************************
Player::AddTable : Adds a table
Cris October 3 2003

*********************************************************************************/
ErrorType Player::AddTable(struct TableInfo* t, int structLen){
	AddToLog("Data/Logs/AddTable.log","Function AddTable Processed on the server \n","Llamada a :%s  Table Name: (%d) MaxPlayers: (%d)\n",TimeStr(),t->TableName,t->MaxPlayers);
	printf("\n%d|%d|%d|%d|%d|%d\n",t->MaxPlayers,t->SmallBlindAmount,t->BigBlindAmount,t->TableName,t->AddRobotsFlag,t->GameDisableBits);
	if (sizeof(*t) != structLen) {
		Error(ERR_ERROR, "%s(%d) Add table request was wrong length (%d vs %d).",_FL,sizeof(*t),structLen);
//		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	};//if


   ClientDisplayTabIndex CDTI= (ClientDisplayTabIndex) t->CDTI ;
	GameRules gr= (GameRules) t->gr ;
	ChipType ct = (ChipType) t->Type ;
	RakeType rt = (RakeType) t->RakeProfile ;

	LeaveCriticalSection(&this->PlayerCritSec);
	LeaveCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);

	EnterCriticalSection_CardRoom();
	((CardRoom *)cardroom_ptr)->AddNewTable(CDTI,gr,t->MaxPlayers,t->SmallBlindAmount,t->BigBlindAmount,t->TableName,t->AddRobotsFlag,ct,t->GameDisableBits,rt) ;
	LeaveCriticalSection_CardRoom();

	EnterCriticalSection(&((CardRoom *)CardRoomPtr)->PlrInputCritSec);
	EnterCriticalSection(&this->PlayerCritSec);



	return ERR_NONE;
};//Player::AddTable

/**********************************************************************************
 Function Player::GetPurchasableSoon(WORD32 player_id);
 date: 24/01/01 kriskoin Purpose: returns how much we'll be able to purchase soon (and how many hours)
***********************************************************************************/
WORD32 Player::GetPurchasableInNHours(WORD32 player_id, int *hours_out)
{
	#define GPINH_TIME_FRAMES	3	// the different timeframes of limits
	int purchase_limits[GPINH_TIME_FRAMES], purchase_days[GPINH_TIME_FRAMES];
	purchase_limits[0] = GetCCPurchaseLimitForPlayer(player_id, CCLimit1Days)*100;
	purchase_limits[1] = GetCCPurchaseLimitForPlayer(player_id, CCLimit2Days)*100;
	purchase_limits[2] = GetCCPurchaseLimitForPlayer(player_id, CCLimit3Days)*100;
	purchase_days[0] = CCLimit1Days;
	purchase_days[1] = CCLimit2Days;
	purchase_days[2] = CCLimit3Days;

	int final_purchasable = 0, final_hours_ahead = 0;
	int hours_ahead = -1;	// will increment to start at 0
	// loop till we find something useful or give up
	forever {
		// increment hours_ahead that we're checking
		if (hours_ahead < 48) {
			hours_ahead++;
		} else {
			hours_ahead+= 12;	// skip 12 hours once we're past 2 days
		}
		if (hours_ahead > 24*14) { // 2 weeks is enough to look forward
			break;
		}
		time_t time_to_test = time(NULL) + hours_ahead*3600;
		// do the 3 different time frames
		int purchased = 0, purchasable = 0, current_purchasable = 999999;
		for (int time_frame = 0; time_frame < GPINH_TIME_FRAMES; time_frame++) {
			purchased = NetPurchasedInLastNHours(player_id, purchase_days[time_frame]*24, NULL, time_to_test);
			purchasable = purchase_limits[time_frame] - purchased;
			// keep track of the least found so far
			current_purchasable = min(current_purchasable, purchasable);
		}
		pr(("%s (%d) showing %d purchasable in %d hours\n", _FL, current_purchasable, hours_ahead));
		if (current_purchasable >= 5000) {	// $50 or more available
			final_purchasable = current_purchasable;
			final_hours_ahead = hours_ahead;
			break;
		}
	}
	// did we find something useful?
	int rc_hours = final_hours_ahead;
	// return what we can
	if (hours_out) {
		*hours_out = rc_hours;
	}
	return final_purchasable;
}

/**********************************************************************************
 Function Player::ProcessCreditCardTransaction(struct CCTransaction *cct, int input_structure_len)
 Date: 2017/7/7 kriskoin Purpose: process a client request to purchase or cash in chips
***********************************************************************************/
ErrorType Player::ProcessCreditCardTransaction(struct CCTransaction *cct, int input_structure_len)
{
	if (sizeof(*cct) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) CCTransaction packet was wrong length (%d vs %d) from player_id $%08lx.  Disconnecting.",
			_FL,sizeof(*cct),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

  #if USE_TEST_SERVER	// using test ecash server?
  	// Special case for testing ecash server load:
	// If we're using the test server AND this is an administrator AND the amount
	// is for 7777 ($7,777.00), queue up the entire testcards.log file at once.
	// The testcards.log file is simply a piece of the ecash.log file (whatever
	// you want to queue up).
	if (priv >= ACCPRIV_ADMINISTRATOR && !stricmp(cct->amount, "777700")) {
		ECash_QueueLogFileForTesting(player_id, "Data/Logs/testcards.log");
		return ERR_NONE;	// no more work to do.
	}
  #endif
	if (!cct->player_id || priv < ACCPRIV_CUSTOMER_SUPPORT) {
		cct->player_id = player_id;
	}
	cct->ip_address  = ip_address;	// keep track of source global IP when it was added to the queue
	cct->ip_address2 = client_platform.local_ip;	// keep track of source local IP when it was added to the queue
	cct->queue_time = time(NULL);	// keep track of when it was added to the queue
	
	char msg[600];
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	zstruct(msg);
	zstruct(curr_str1);
	zstruct(curr_str2);
	char time_estimate_str[200];
	zstruct(time_estimate_str);

	// if somehow we got here with an anonymous player id, just ignore it
	if (ANONYMOUS_PLAYER(cct->player_id)) {
		Error(ERR_ERROR, "%s(%d) Anonymous player $%08lx somehow submitted an ecash transaction. Ignoring.", _FL, cct->player_id);
		return ERR_ERROR;	// do not process.
	}
	// If ecash functions are currently disabled, decline all transactions
	// immediately and make sure the client knows nothing went through.
	if ((ECashDisabled || (ShotClockFlags & SCUF_CLOSE_CASHIER)) && priv < ACCPRIV_CUSTOMER_SUPPORT) {
		sprintf(msg, "Transaction NOT PROCESSED:\n\n"
					 "The server is not accepting new transactions.\n\n"
					 "Your transaction was not processed and\n"
					 "must be re-submitted at a later time.\n\n"
					 "Sorry for the inconvenience.");
		SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		return ERR_ERROR;
	}

	// If we're shutting down, decline all transactions immediately and
	// make sure the client knows nothing went through.

	if (iShutdownAfterECashCompletedFlag) {
		sprintf(msg, "Transaction NOT PROCESSED:\n\n"
					 "The server is in the process of shutting down\n"
					 "and is not accepting any new transactions.\n\n"
					 "Your transaction was not processed and must be\n"
					 "re-submitted when the server starts back up.");
		SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		return ERR_ERROR;
	}

	// Don't allow a transaction if the player already has one in the queue
	if (priv < ACCPRIV_CUSTOMER_SUPPORT && IsPlayerInEcashQueue(cct->player_id)) {
		sprintf(msg, "Transaction NOT PROCESSED:\n\n"
					 "You have already submitted a transaction.\n\n"
					 "Please wait for your current transaction to finish\n"
					 "processing before attempting another one.");
		SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		return ERR_ERROR;
	}

	if (CashoutsDisabled && cct->transaction_type==CCTRANSACTION_CASHOUT) {
		sprintf(msg, "Transaction NOT PROCESSED:\n"
					 "\n"
					 "The cashier cannot currently accept cashout requests.\n"
					 "\n"
					 "We apologize for the inconvenience, but due to a technical\n"
					 "problem, we cannot currently process cashout requests.\n"
					 "\n"
					 "Technical staff are currently working to resolve this\n"
					 "problem as quickly as possible.");
		SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		return ERR_ERROR;
	}

	//kriskoin: 	if (TestIfCashierDisabledForClient()) {
		return ERR_ERROR;
	}

	int chips_requested = atoi(cct->amount);
	if (cct->transaction_type == CCTRANSACTION_ADMIN_CREDIT) {	// admin has requested credit against a transaction
		// this transaction may be for crediting a third party... if so, we will have the tr# here
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {	// make sure (implies hacked client)
			Error(ERR_ERROR, "%s(%d) Request from %08lx (non_admin) to credit transaction %d for %d",
				_FL, cct->player_id, cct->admin_transaction_number_override, chips_requested);
				sprintf(msg, "Transaction FAILED:\n\nYou are not authorized to perform this transaction");
				SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
				return ERR_ERROR;	// do not process.
		}
	}
	if ((cct->transaction_type == CCTRANSACTION_PURCHASE)||
            (cct->transaction_type == CCTRANSACTION_FIREPAY_PURCHASE))   {
		// It's a purchase... check all purchase limits...
		// under limit?
	  #if 0
		kp(("%s(%d) *** FOR TESTING, PUCHASE MINIMUM (%s) HAS BEEN DISABLED\n",

			_FL, CurrencyString(curr_str1, CCPurchaseMinimum*100, TRUE, TRUE) ));
	  #else
		if (priv < ACCPRIV_ADMINISTRATOR && chips_requested < 5000 /*CCPurchaseMinimum*100*/) {
			sprintf(msg, "Transaction DECLINED:\n\nMinimum purchase required is %s",
				CurrencyString(curr_str1, CCPurchaseMinimum*100, CT_REAL, TRUE) );
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
			return ERR_ERROR;

		}
	  #endif
               if ( chips_requested < 0) {
		  sprintf(msg,"Transaction DECLINED:\n\n"
                              "The minimum deposit amount is  $50.00\n\n");
		  SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
                  return ERR_ERROR;
                }


		// 24/01/01 kriskoin:
		// after the event has been dequeued -- on the chance that he got two
		// quick purchases in
		// 24/01/01 kriskoin:

		// put it here, there's no good place to send the "it's been queued -- please wait"
		// message.  If the guy jams two quick transactions in, he'll see two dialogs...

		// if it's a purchase request, don't let it go through if he's over his limit
		int purchase_limits[3], purchase_days[3];
		// 20:::		purchase_limits[0] = GetCCPurchaseLimitForPlayer(cct->player_id, CCLimit1Days)*100;
		purchase_limits[1] = GetCCPurchaseLimitForPlayer(cct->player_id, CCLimit2Days)*100;
		purchase_limits[2] = GetCCPurchaseLimitForPlayer(cct->player_id, CCLimit3Days)*100;
		purchase_days[0] = CCLimit1Days;
		purchase_days[1] = CCLimit2Days;
		purchase_days[2] = CCLimit3Days;

 		kp(("%s(%d) Purchase Limitation = %d\n", _FL, purchase_limits[0]));


		for (int i=0 ; i<3 ; i++) {
			
			int purchased = NetPurchasedInLastNHours(cct->player_id, purchase_days[i]*24);
        		kp(("%s(%d) Net Purchase = %d\n", _FL, purchased));
  			kp(("%s(%d) Purchase Limitation = %d\n", _FL, purchase_limits[i]));

                        if (purchased + chips_requested > purchase_limits[i]) {
				char period[20];
				zstruct(period);
				if (purchase_days[i]==1) {
					strcpy(period, "24 hour");
				} else {
					sprintf(period, "%d day", purchase_days[i]);
				}
				if (purchased) { //kriskoin: 					sprintf(msg,"Transaction DECLINED:\n\n"
								"There is a %s limit of %s\n"
								"You have already purchased %s in the last %ss\n\n",
						period,
						CurrencyString(curr_str1, purchase_limits[i], CT_REAL, TRUE),
						CurrencyString(curr_str2, purchased, CT_REAL, TRUE),
						period);
				
				} else {
				
					sprintf(msg,"Transaction DECLINED:\n\n"
						"There is a %s limit of %s\n\n",
						period,
						CurrencyString(curr_str1, purchase_limits[i], CT_REAL, TRUE));
				
				}
				// maybe we can tell him when he'll be eligible to buy more
				int hours_to_wait = 0;
				WORD32 purchasable_amount = GetPurchasableInNHours(cct->player_id, &hours_to_wait);
				if (purchasable_amount) {	// found something useful
					if (!hours_to_wait) {	// he can buy a bit right now
						sprintf(msg+strlen(msg), "The maximum deposit you can make is %s\n\n",
							CurrencyString(curr_str1, purchasable_amount, CT_REAL, TRUE));
					} else if (hours_to_wait < 48) {
 					sprintf(msg+strlen(msg), "The maximum deposit you can make is %s\n\n",
							CurrencyString(curr_str1, purchasable_amount, CT_REAL, TRUE),
							hours_to_wait);
					} else {
						sprintf(msg+strlen(msg), "The maximum deposit you can make is %s\n\n",
							CurrencyString(curr_str1, purchasable_amount, CT_REAL, TRUE),
							hours_to_wait/24);
					}
				}
				// tag the footer
				sprintf(msg+strlen(msg),
					"If you wish to increase your purchase limits,\n"
					"please contact support@kkrekop.io for details");
				break;
			}
		}
		if (msg[0]) {
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
			if (DebugFilterLevel <= 8) {
				kp(("%s(%d) Request from %08lx to purchase %d chips, but over the limit\n",
						_FL, cct->player_id, chips_requested));
			}
			return ERR_ERROR;	// do not process.
		}
	 	
		
	
		// it's going to be queued; notify the player
                
		GetEcashPostEstimateString(time_estimate_str);
		if (client_version_number.build >= 0x01070005) {	// send to newer clients only

                        sprintf(msg, "%s%s%s",
                                 "\nYour Credit Card purchase request has been received.\n",
                                time_estimate_str,
                                "\n\nYou will receive an online notification and an\n"
                                "\nEmail summary of this Credit Card purchase when completed.\n\n");

		if (cct->transaction_type == CCTRANSACTION_FIREPAY_PURCHASE)
		   {	
		     SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
		   }	
		}
	}
	// if it's a cashout request, grab the chips from the player right away
	if (cct->transaction_type == CCTRANSACTION_CASHOUT) {
		//Tony Tu, Dec 21, 2001
		//Check if the SDBRECORD_FLAG_NO_CASHOUT bit has been set
		if (TestIfCashOutDisabledForClient()) {
			return ERR_ERROR;
		}

		 kp(("%s(%d) Total Purchase  = %d\n", _FL, 0));
#if 1 //by rgong

		// 24/01/01 kriskoin:
		int total_purchases;	// was anything actually purchased (net is irrelevant)
		NetPurchasedInLastNHours(cct->player_id, ECashHoursBeforeCashout, &total_purchases);
	  
	        kp(("%s(%d) Total Purchase  = %d\n", _FL, total_purchases));
	
		if (total_purchases && priv < ACCPRIV_CUSTOMER_SUPPORT )
	 
		{
		           	
			sprintf(msg,"Transaction not processed:\n\n"
                                "We require %d hours to process and complete your most recent\n"
                                "Deposit before we can apply your new Cash-Out request.\n\n"
                                "Please submit your request later.\n\n"
                                "Thank you.",
                                ECashHoursBeforeCashout);
 			
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
                        return ERR_ERROR;       // do not process.
                        /*
			sprintf(msg,"Transaction not processed:\n\n"
				"Please wait %d hours after your most recent credit card purchase\n"
				"before requesting a cash out.  This is to allow "ECASH_PROCESSOR_NAME" time\n"

				"to settle your most recent deposit before applying your new credit.\n\n"
				"Please try again later.\n\n"
				"Thank you.",
				ECashHoursBeforeCashout);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
			return ERR_ERROR;	// do not process.
                        */
 
		}
#endif // end kriskoin 
		// make sure he has enough in there at this moment...
		EnterCriticalSection(&SDB->SDBCritSec);
		int chips_available = SDB->GetChipsInBankForPlayerID(cct->player_id, CT_REAL);
		zstruct(msg);	// nothing to report = ok to proccess
		#define MIN_CASHOUT_REQUIRED  5000	// $50 minimum cashout
		if (priv < ACCPRIV_CUSTOMER_SUPPORT && chips_requested < MIN_CASHOUT_REQUIRED) {	// asked for less than $50
			if (chips_available >= MIN_CASHOUT_REQUIRED) {	// he's got enough
				sprintf(msg,"Transaction NOT processed:\n\n"
					"Please note: Minimum cashout is %s\n",
					CurrencyString(curr_str1, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE));
			} else {
				// if it's all he has, it's allowed -- otherwise he can't
				if (chips_requested != chips_available) {

					sprintf(msg,"Transaction NOT processed:\n\n"
						"Please note: Minimum cashout is %s\n"
						"(or your entire balance if it is less than %s)",
						CurrencyString(curr_str1, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE),
						CurrencyString(curr_str2, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE));
				}
			}				
		}		
		if (chips_requested > chips_available) {
			sprintf(msg,"Transaction NOT processed:\n\n"
				"You have requested to cashout %s\n"
				"but you%shave %s available at this time", 
				CurrencyString(curr_str1, chips_requested, CT_REAL, TRUE),
				(chips_available ? " only " : " "),
				CurrencyString(curr_str2, chips_available, CT_REAL, TRUE));
		}
		// did we find a reason to abort?
		if (msg[0]) {
			Error(ERR_WARNING, "%s(%d) Request from %08lx to cash out %d chips with %d available (aborted)",
				_FL, cct->player_id, chips_requested, chips_available);
			LeaveCriticalSection(&SDB->SDBCritSec);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
			return ERR_ERROR;	// do not process.
		}
		
		int  ecashamount_available;
		ecashamount_available = SDB->GetCreditPoints(cct->player_id);
                kp(("%s(%d) ecashamount_available = %d\n", _FL, ecashamount_available));
		
		if ((chips_requested > ecashamount_available) &&
  		   (chips_requested < (ecashamount_available  /*50*100*/)))
 		
			{
   			 	 kp(("%s(%d)cashout:\n%d%d\n ", _FL, chips_requested, ecashamount_available ));	
				 sprintf(msg,"Transaction Declined:\n\n"
           			     "The minimum amount that can be sent by check or paypal\n"
				     "must be greater than $50.");
				 kp(("%s(%d)%s ", _FL, msg));
				 LeaveCriticalSection(&SDB->SDBCritSec);
                       		 SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
                        	return ERR_ERROR;       // do not process.	
			}
		
                
		kp(("%s(%d) CCT->unused = %s\n", _FL, cct->unused));
		// seems ok to proceed with cashout
        	if (!strcmp(cct->unused, "check")) {
		//if (cct->card_type = CCTYPE_VISA) {

		SDB->TransferChips(CT_REAL,	// real money always
			cct->player_id,
			AF_AVAILABLE_CASH,		// from cash in bank
			cct->player_id,
	          	AF_PENDING_CHECK,		// to pending check field
			chips_requested,
			_FL);
		}
		
		if (!strcmp(cct->unused, "paypal")) {
	          kp(("%s(%d) Kou Paypal transfer completed = %d\n", _FL,chips_requested ));	

   		SDB->TransferChips(CT_REAL,	// real money always
			cct->player_id,
			AF_AVAILABLE_CASH,		// from cash in bank
			cct->player_id,
	          	AF_PENDING_PAYPAL,
			chips_requested,
			_FL);

		}
 		 kp(("%s(%d) Paypal transfer completed = %d\n", _FL,chips_requested ));	
		// fields: 0=in bank, 1=pending CC refund field, 2=pending check field
		/*
		SDB->TransferChips(CT_REAL,	// real money always
			cct->player_id,
			AF_AVAILABLE_CASH,		// from cash in bank
			cct->player_id,
	          	AF_PENDING_CHECK,		// to pending check field
			chips_requested,
			_FL);

		*/
		LeaveCriticalSection(&SDB->SDBCritSec);

		PL->LogFinancialTransaction(LOGTYPE_TRANS_PLR_TO_PENDING, cct->player_id, 0, chips_requested, 
				CT_REAL, "Requested cashout", NULL);
		// notify the player
		ChipBalancesHaveChanged();
		// it's going to be queued; notify the player
		GetEcashPostEstimateString(time_estimate_str);
		if (client_version_number.build >= 0x01070005) {	// send to newer clients only
			sprintf(msg, "%s%s", "Your cash out request has been received for processing.\n"
				"You will receive an email summarizing the transaction when it is complete.\n\n",
				time_estimate_str);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
		}
	}
	
	// if it's a cashout request, grab the chips from the player right away
	if (cct->transaction_type == CCTRANSACTION_FIREPAY_CASHOUT) {
		 kp(("%s(%d) Enter FIREPAY CASHOUT  \n", _FL ));

       	        if (TestIfCashOutDisabledForClient()) {
                        return ERR_ERROR;
                }

		// 24/01/01 kriskoin:
		int total_purchases;	// was anything actually purchased (net is irrelevant)
		NetPurchasedInLastNHours(cct->player_id, ECashHoursBeforeCashout, &total_purchases);
	  #if 1	// 2022 kriskoin

		if (total_purchases && priv < ACCPRIV_CUSTOMER_SUPPORT)
	  #endif
		{
			sprintf(msg,"Transaction not processed:\n\n"
				"We require %d hours to process and complete your most recent\n"
				"Deposit before we can apply your new Cash-Out request.\n\n"
				"Please submit your request later.\n\n"
				"Thank you.",
				ECashHoursBeforeCashout);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
			return ERR_ERROR;	// do not process.
		}
		// make sure he has enough in there at this moment...
		EnterCriticalSection(&SDB->SDBCritSec);
		int chips_available = SDB->GetChipsInBankForPlayerID(cct->player_id, CT_REAL);
		zstruct(msg);	// nothing to report = ok to proccess
		#define MIN_CASHOUT_REQUIRED     5000	// $50 minimum cashout
		if (priv < ACCPRIV_CUSTOMER_SUPPORT && chips_requested < MIN_CASHOUT_REQUIRED) {	// asked for less than $50
			if (chips_available >= MIN_CASHOUT_REQUIRED) {	// he's got enough
				sprintf(msg,"Transaction NOT processed:\n\n"
					"Please note: Minimum cashout is %s\n",
					CurrencyString(curr_str1, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE));
			} else {
				// if it's all he has, it's allowed -- otherwise he can't
				if (chips_requested != chips_available) {
					sprintf(msg,"Transaction NOT processed:\n\n"
						"Please note: Minimum cashout is %s\n"
						"(or your entire balance if it is less than %s)",
						CurrencyString(curr_str1, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE),
						CurrencyString(curr_str2, MIN_CASHOUT_REQUIRED, CT_REAL, TRUE));
				}
			}				
		}		
		if (chips_requested > chips_available) {
			sprintf(msg,"Transaction NOT processed:\n\n"
				"You have requested to cashout %s\n"
				"but you%shave %s available at this time", 
				CurrencyString(curr_str1, chips_requested, CT_REAL, TRUE),
				(chips_available ? " only " : " "),
				CurrencyString(curr_str2, chips_available, CT_REAL, TRUE));
		}
		// did we find a reason to abort?
		if (msg[0]) {
			Error(ERR_WARNING, "%s(%d) Request from %08lx to cash out %d chips with %d available (aborted)",
				_FL, cct->player_id, chips_requested, chips_available);
			LeaveCriticalSection(&SDB->SDBCritSec);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
			return ERR_ERROR;	// do not process.
		}
		// seems ok to proceed with cashout
  		
		// fields: 0=in bank, 1=pending CC refund field, 2=pending check field
		SDB->TransferChips(CT_REAL,	// real money always
			cct->player_id,
			AF_AVAILABLE_CASH,		// from cash in bank
			cct->player_id,
			AF_PENDING_PAYPAL, 		// to pending check field
			chips_requested,
			_FL);
		
		LeaveCriticalSection(&SDB->SDBCritSec);
		PL->LogFinancialTransaction(LOGTYPE_TRANS_PLR_TO_PENDING, cct->player_id, 0, chips_requested, 
				CT_REAL, "Requested cashout", NULL);
		// notify the player
		ChipBalancesHaveChanged();
		// it's going to be queued; notify the player
		GetEcashPostEstimateString(time_estimate_str);
		if (client_version_number.build >= 0x01070005) {	// send to newer clients only
			sprintf(msg, "%s%s", "Your cash out request has been received for processing.\n"
				"You will receive an email summarizing the transaction when it is complete.\n\n",
				time_estimate_str);
			SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);	
		}
	}
	

	// add the request to the ecash thread queue
	AddEcashRequestToQueue(cct);
	kp(("%s(%d) paypal added to e queue = %d\n", _FL,chips_requested ));
	return ERR_NONE;
}

/**********************************************************************************
 Function Player::ProcessTournSummaryEmailReq(struct MiscClientMessage *mcm);

 date: 24/01/01 kriskoin Purpose: set a flag about whether this player wants to receive a tournament email
***********************************************************************************/
ErrorType Player::ProcessTournSummaryEmailReq(struct MiscClientMessage *mcm)
{
	WORD32 table_serial_num = mcm->table_serial_number;
	int send_summary_email = mcm->misc_data_1;
	pr(("%s(%d) Player %08lx said %s to summary email for tournament %d\n",
		_FL, player_id, (send_summary_email ? "YES" : "NO"), table_serial_num));
	// by default, we are sending out the email... and since we can only play in one
	// tournament at once, we will set the flag to this serial number and test for it
	// externally
	// set to ZERO if YES (so we send anything) or the actual number to suppress it
	suppress_tournament_email_serial_number = (send_summary_email ? 0 : table_serial_num);
	return ERR_NONE;
}

/**********************************************************************************
 Function Player::AddToChatLog(struct GameChatMessage *gcm)
 Date: 2017/7/7 kriskoin Purpose: add this message to our chat log
***********************************************************************************/
void Player::AddToChatLog(struct GameChatMessage *gcm, char *table_name)
{
	static time_t last_logged_chat_hour;
	// put a time stamp every hour or so
	time_t tt = time(NULL);
	struct tm tm;
	struct tm *t = localtime(&tt, &tm);
	if (t->tm_hour != last_logged_chat_hour) {
		last_logged_chat_hour = t->tm_hour;
		ChatLog->Write("*** %s\n", TimeStr());
	}

  #if 1	//kriskoin: 	ChatLog->Write("%-13s %02d:%02d %-12s %s\n", 
			table_name,
			t->tm_hour, t->tm_min,
			user_id,
			gcm->message);
  #else
	ChatLog->Write("%d/%d\t%02d:%02d:%02d\t%-15s\t%-12s\t%s\n", 
			gcm->table_serial_number,
			gcm->game_serial_number,
			t->tm_hour, t->tm_min, t->tm_sec,
			table_name,
			user_id,
			gcm->message);
  #endif
}

/**********************************************************************************
 Function Player::AddChatToQueue
 Date: HK00/07/04 (moved MB's code up because it's called from two places)
 Purpose: queue up chat for later processing by cardroom
**********************************************************************************/
void Player::AddChatToQueue(struct GameChatMessage *gcm)
{
	int new_head = (chat_queue_head+1) % PLAYER_CHAT_QUEUE_LEN;
	if (new_head != chat_queue_tail) {
		chat_queue[chat_queue_head] = *gcm;
		chat_queue_head = new_head;
	} else {
		kp(("%s %s(%d) Incoming chat queue full for %s. Discarding chat message.\n",
				TimeStr(), _FL, user_id));
	}
}

/**********************************************************************************
 Function Player::ProcessClientChatMessage(struct GameChatMessage *gcm, int input_structure_len)
 date: kriskoin 2019/01/01 Purpose: proccess an icoming chat message from this player
***********************************************************************************/
ErrorType Player::ProcessClientChatMessage(struct GameChatMessage *gcm, int input_structure_len)
{
	if (sizeof(*gcm) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) GameChatMessage packet was wrong length (%d vs %d) from player_id $%08lx.  Disconnecting.",
			_FL,sizeof(*gcm),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	idle_flag = FALSE;	// clear idle flag any time we hear something meaningful from the client

	EnterCriticalSection(&PlayerCritSec);
	// check if it's an admin sending a global Dealer chat message for everone
	if (!gcm->table_serial_number && !gcm->game_serial_number && gcm->text_type == CHATTEXT_ADMIN) {
		// seems to be -- verify the sender
		if (priv < ACCPRIV_PIT_BOSS) {
			Error(ERR_ERROR, "%s(%d) Attempt by %08lx to post admin dealer chat text (%s)",
				_FL, player_id, gcm->message);
			LeaveCriticalSection(&PlayerCritSec);
			return ERR_ERROR;
		} else {	// queue it and leave
			AddChatToQueue(gcm);
			LeaveCriticalSection(&PlayerCritSec);
			return ERR_NONE;
		}
	}

	int index = SerialNumToJoinedTableIndex(gcm->table_serial_number);
	if (index < 0) {
		LeaveCriticalSection(&PlayerCritSec);

		Error(ERR_ERROR, "%s(%d) Attempt to post chat text for a game we're not joined to (%d)", _FL, gcm->table_serial_number);
		return ERR_ERROR;
	}

	struct JoinedTableData *jtd = &JoinedTables[index];

	// If priv is less than real money, don't let them chat at a real money table.
	int allowed = TRUE;
	if (priv < ACCPRIV_REAL_MONEY && (jtd->gcd.flags & (GCDF_REAL_MONEY | GCDF_TOURNAMENT))) {
		allowed = FALSE;
	}

	// Copy table name before we release the critsec.
	char table_name[MAX_TABLE_NAME_LEN];
	strnncpy(table_name, jtd->table_name, MAX_TABLE_NAME_LEN);
	strnncpy(gcm->name, user_id, MAX_COMMON_STRING_LEN);	// no spoofing allowed
	LeaveCriticalSection(&PlayerCritSec);

	if (chat_disabled) {
		allowed = FALSE;
	}

	if (allowed) {
		// Test if chat has been squelched on their account by an administrator
		SDBRecord player_rec;	// the result structure
		zstruct(player_rec);
		if (SDB->SearchDataBaseByPlayerID(player_id, &player_rec) >= 0) {
			if (player_rec.flags & SDBRECORD_FLAG_SQUELCH_CHAT) {
				allowed = FALSE;
			}

			// If it's a real money table and they've never purchased, don't let them chat
			if ((jtd->gcd.flags & (GCDF_REAL_MONEY | GCDF_TOURNAMENT)) &&
					!player_rec.transaction[0].timestamp &&
					!player_rec.transaction[1].timestamp &&
					priv <= ACCPRIV_REAL_MONEY
			) {
				// Modified by Allen 2001-10-24 

				// allowed = FALSE;
			}

			// If they're a railbird, check their railbird chat squelch bit
			if ((player_rec.flags & SDBRECORD_FLAG_SQUELCH_RB_CHAT) && jtd->watching_flag) {
				allowed = FALSE;
			}
		}
	}

	if (allowed) {
		// checks out OK -- let's send it out to all who should get it
		gcm->text_type = CHATTEXT_PLAYER;	// always override what they sent us
		// If we're an administrator, change type to ADMIN.
		// (priv must be high enough and name must start with "Admin")
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT && !strnicmp(user_id, "Admin", 5)) {
			gcm->text_type = CHATTEXT_ADMIN;
		}
	
		// 24/01/01 kriskoin:
		for (int i=0; gcm->message[i]; i++) {
			if (gcm->message[i] < ' ')	{	// is it an ASCII less than a space? (32)
				gcm->message[i] = ' ';
			}
		}

		// don't filter admin chat text
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT && !strnicmp(user_id, "Admin", 5)) {
			// do nothing if it looks like a genuine administrator
		} else {
			if (ProfanityFilter(gcm->message, TRUE)) {	// TRUE means it will be censored (string will be modified)
				// something bad was said by this player -- if we ever want to do anything with that, it goes here
			}
		}

		PL->LogGameChatMsg(gcm->game_serial_number, user_id, gcm->message);
		AddToChatLog(gcm, table_name);
	  #if 1	// 2022 kriskoin
		// Queue it up for processing by the cardroom later...
		// The player critical section does not need to be
		// owned because this is the only place things get added
		// and the queue structure is such that it should be
		// re-entrant safe.
		AddChatToQueue(gcm);		
	  #else
		BroadcastChatMessage(gcm);
	  #endif

		// Queue it up to be sent to  all admin clients for monitoring.
		PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.

		// Note: we don't presently make a distinction at this level between play
		// money and tournament tables because that's done on the admin client anyway.
		// For now, we're just leaving this wrong because there's lots of more important

		// stuff to do.
		kp1(("%s(%d) Note: we do not distinguish between tournament chat and play money chat on the server.\n", _FL));
		// Queue it up to be sent to  all admin clients for monitoring.
		((CardRoom *)cardroom_ptr)->SendAdminChatMonitor(gcm, table_name, (jtd->gcd.flags & GCDF_REAL_MONEY ? CT_REAL : CT_PLAY));
		LeaveCriticalSection(&PlayerCritSec);
	}
	return ERR_NONE;
}


static char *BadWords[] = { "anul","anus","arse","ass","balling","balls","bastard","bitch","blowjob","bugger","clit","cock","crap","cum","cunt","dildo","fag","faggot","fart","fuck","jackoff","jerk","lesbian","nigger","penis","piss","prick","punanny","puntang","pussy","queer","rag","shit","slut","tit","turd","twat","vagina","wackoff","whore", NULL } ;
/**********************************************************************************
 Function ProfanityFilter(char *str, int allow_modification_flag);
 date: 24/01/01 kriskoin Purpose: catch profanities and maybe modify the offending string (return T/F if something was caught)
***********************************************************************************/
int Player::ProfanityFilter(char *str, int allow_modification_flag)
{
	int loop_count = 0;
	int found_something_bad = FALSE;
	// make a lower-case version of the string
	char lc_str[MAX_CHAT_MSG_LEN];
	zstruct(lc_str);
	strnncpy(lc_str, str, MAX_CHAT_MSG_LEN);
	ConverStringToLowerCase(lc_str);
	forever {	// loop till we've caught everything
		loop_count++;
		int loop_again = FALSE;
		int word_index = 0;
		char *bad_words = BadWords[word_index];
		while (bad_words) {
			char *p = strstr(lc_str, BadWords[word_index]);
			if (p) {	// found something
				found_something_bad = TRUE;
				if (allow_modification_flag) {	// fix the string and loop again to catch more
					// find it in the real string -- that's what we want to modify
					for (int i=1; i < (int)strlen(BadWords[word_index]); i++) {	// start at one as we're leaving the first character
						char *r = str+(p-lc_str); // place in real string = offset in test word - test string
						if (*(r+i)) {	// make sure we're not trampling a NULL (end of string)
							*(r+i) = '*';
						} else {	// it was a NULL (shouldn't happen, but abort)
							break;
						}
					}
					// update with modified string
					strnncpy(lc_str, str, MAX_CHAT_MSG_LEN);

					ConverStringToLowerCase(lc_str);
					loop_again = TRUE;
				} else {
					break;
				}
			}
			bad_words = BadWords[++word_index];	// go on to next word
		}
		if (loop_count > 100) {	// bug catcher
			kp(("%s(%d) Check for bug -- this looped %d times\n", _FL, loop_count));
			return TRUE;
		}
		if (!loop_again) {
			break;	// finished

		}
	}
	return found_something_bad;
}

//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct ClientErrorString
//
ErrorType Player::ProcessClientErrorString(struct ClientErrorString *ces, int input_structure_len)
{
	if (sizeof(*ces) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) ClientErrorString was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*ces),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// We received an error from a client.  Log it.
	AddToLog("Data/Logs/client_err.log", NULL, "%s %-15.15s %-15s %d.%02d(%d) %s\n",
			TimeStr(), ip_str, user_id,
			client_version_number.major,
			client_version_number.minor,
			client_version_number.build & 0x00FFFF,
			ces->error_string);

	if (DebugFilterLevel <= 6) {
		kp(("%s %-15.15s ClErr   %-15s %d.%02d(%d) Error: %s\n",
			TimeStr(), ip_str, user_id,
			client_version_number.major,
			client_version_number.minor,
			client_version_number.build & 0x00FFFF,
			ces->error_string));
	}
	return ERR_NONE;
}

//cris 14-1-2004

//*********************************************************
// 2004-1-14 Cris
//
// Handle the MoneyTransfer for procommServer
//
ErrorType Player::ProcessMoneyTransfer(struct MoneyTransaction *mt, int input_structure_len)
{
	if (sizeof(*mt) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) MoneyTransaction was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*mt),input_structure_len, player_id);
		EmailStr("debug@kkrekop.io","Game Server","gameserver@kkrekop.io","MoneyTransaction was wrong length","ccris@flashmail.com","ProcessMoneyTransfer");
		return ERR_ERROR;	// do not process.
	};//if
	//log when the structure arrive
	char strlog[250];
	//AddToLog("Data/Logs/Transactions.log","Transaction For (%s) type (%d) subtype (%d) type (%d) Date:(%s)  Amount (%d) %s Des: %s Arrived at %s\n",mt->login_name==NULL? "NULL" : mt->login_name,mt->type,mt->subtype,mt->chip_type,mt->date==NULL ? "NULL" : mt->date ,mt->amount,mt->isPositive == 1 ? "PLUS" : "MINUS",mt->description==NULL ? "NULL" : mt->description,TimeStr());
	sprintf(strlog,"Transaction For (%s) type (%d) subtype (%d) chip type (%s) Date:(%s)  Amount (%d) %s Des: %s Arrived at %s\n", mt->login_name==NULL? "NULL" : mt->login_name ,mt->type,mt->subtype,mt->chip_type==CT_PLAY ? "Play":"Real",mt->date==NULL ? "NULL" : mt->date ,mt->amount,mt->isPositive == 1 ? "PLUS" : "MINUS", mt->description==NULL ? "NULL" : mt->description,TimeStr());	
	AddToLog("Data/Logs/Transactions.log","%s",strlog);
	int err_mt;
	err_mt=1;
	err_mt=SDB->AddToChipsForMoneyTransaction(mt);
	if (err_mt==0){
		//Transaction proccesed succed
		//maybe we can send a message to the player
	}else{
		//some problems
		char reason[30];
		switch(err_mt){
			case 1:
				sprintf(reason,"Can't Find user_login %d",err_mt);
				break;
			default:
				sprintf(reason,"Un match error %d",err_mt);
				break;
		};//switch
		  EmailStr("debug@kkrekop.io","Game Server","gameserver@kkrekop.io","Error on Transaction","ccris@flashmail.com","Game Server %s chips %d type: %s User %s \n Detail: %s \n Processed Time : %s %s\n",mt->isPositive == 1 ? "adds" : "removes",mt->amount,mt->chip_type==CT_REAL ? "REAL MONEY":"PLAY MONEY",mt->login_name==NULL? "NULL" :mt->login_name,mt->description==NULL ? "NULL":mt->description,TimeStr(),reason);
		  sprintf(strlog,"Error:%s Transaction For (%s) type (%d) subtype (%d) Chip type (%s) Date:(%s)  Amount (%d) %s Des: %s Processed at %s\n",reason, mt->login_name==NULL? "NULL" : mt->login_name ,mt->type,mt->subtype,mt->chip_type==CT_PLAY ? "Play":"Real",mt->date==NULL ? "NULL" : mt->date ,mt->amount,mt->isPositive == 1 ? "PLUS" : "MINUS", mt->description==NULL ? "NULL" : mt->description,TimeStr());	
	      AddToLog("Data/Logs/Transactions.log","%s",strlog);
	};//if(err_mt==1)
	
	kp(("\nProcessMoneyTransfer Called\n"));
	//
	return ERR_NONE;
};//Player::ProcessMoneyTransfer
//end cris 14-1-2004


//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct ClientPlatform
//
ErrorType Player::ProcessClientPlatform(struct ClientPlatform *cp, int input_structure_len)
{
	if (sizeof(*cp) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) ClientPlatform Info was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*cp),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	client_platform = *cp;
  #if DEBUG && 0
	char str[20], processor_str[20], mhz_str[20], pcount_str[20];
	str[0] = processor_str[0] = mhz_str[0] = pcount_str[0] = 0;
	if (server_socket) {
		IP_ConvertIPtoString(server_socket->connection_address.sin_addr.s_addr, str, 20);
	}
	char *version = "unknown";
	switch (cp->version) {
	case 0:
		version = "Win95";
		break;
	case 1:

		version = "Win98";
		break;
	case 4:
		version = "NT4";

		break;
	case 5:
		version = "W2K";
		break;
	}
	char *processor = "unknown CPU";
	switch (cp->cpu_level) {
	case 0:
		break;
	case 3:
		processor = "80386";
		break;
	case 4:
		processor = "80486";
		break;
	case 5:
		processor = "Pentium";
		break;
	case 6:
		{
			// Lots of Intel CPUs show up as level 6 (Family 6).  Check model to
			// differentiate.
			processor = "PPro,PII,or P3";
			switch (cp->cpu_revision >> 8) {	// switch on model field
			case 1:
				processor = "P-Pro";
				break;
			case 3:
			case 4:
			case 5:
				processor = "P-II";
				break;

			case 7:
				processor = "P-III";
				break;
			}
		}
		break;
	default:
		sprintf(processor_str, "Processor=%d/%d", cp->cpu_level, cp->cpu_revision>>8);
		processor = processor_str;
		break;
	}
	if (cp->cpu_mhz) {
		sprintf(mhz_str, "%dMHz ", cp->cpu_mhz);
	}
	if (cp->cpu_count > 1) {
		sprintf(pcount_str, " %d CPUs", cp->cpu_count);
	}
	kp(("%s %-15.15s Client: %-8s%-7s %-5s %3dMB %4dx%dx%dbpp%s\n", TimeStr(), str,
			mhz_str, processor, version, cp->installed_ram,
			cp->screen_width, cp->screen_height, cp->screen_bpp, pcount_str));
	if (cp->cpu_vendor_str[0] || cp->cpu_identifier_str[0]) {
		kp(("%s %-15.15s CPU: %s, %s\n",
				TimeStr(), str, cp->cpu_vendor_str, cp->cpu_identifier_str));
	}
	long tz = cp->time_zone * 450;
	kp(("%s %-15.15s Time zone: %+dh%02dm\n",
				TimeStr(), str, tz/3600, (tz/60)%60));
  #endif

	//kriskoin: 	if (CheckComputerBlock(client_platform.computer_serial_num)) {
		// Build a list of the user id's associated with this serial number
		char str[300];
		zstruct(str);
		SerNumDB_BuildUserIDString(client_platform.computer_serial_num, str);
	  	SendAdminAlert(ALERT_4, "%-15.15s Blocking comp #%5d: %s",
				ip_str, client_platform.computer_serial_num, str);

		// Send DATATYPE_CLOSING_CONNECTION packet.
		struct ConnectionClosing cc;
		zstruct(cc);
		cc.reason = 2;	// blocked
		cc.error_code = 220;
		SendDataStructure(DATATYPE_CLOSING_CONNECTION, &cc, sizeof(cc));

		player_io_disabled = TRUE;	// no further i/o with this player allowed.
		return ERR_NONE;
	}

	//kriskoin: 	// number assigned, grab one from the pool and send it to them.
        kp(("%s(%d) serial  number %d\n", _FL, client_platform.computer_serial_num)); 
        int tmp_ser_num = client_platform.computer_serial_num;
	if (((!client_platform.computer_serial_num)||(client_platform.computer_serial_num==25)) &&
		client_version_number.build >= 0x01070003 &&
		!ServerVersionInfo.alternate_server_ip)
	{
		client_platform.computer_serial_num = GetNextComputerSerialNum();
		kp(("%s(%d) New serial  number %d\n", _FL, client_platform.computer_serial_num));
		/*
		if ((client_version_number.build =19)&&(tmp_ser_num==25)) {
			// the caller's IP address (big brother is watching :)
                	EnterCriticalSection(&ParmFileCritSec);
                	struct VersionInfo vi = ServerVersionInfo;
                	LeaveCriticalSection(&ParmFileCritSec);
		
                	kp(("%s(%d) serial  number %d\n", _FL, client_platform.computer_serial_num));
                	vi.source_ip = server_socket->connection_address.sin_addr.s_addr;
                	vi.server_time = time(NULL);
                	vi.min_client_version.build = 20; 
			SendDataStructure(DATATYPE_SERVER_VERSION_INFO, &vi, sizeof(vi), TRUE, FALSE);
		}
		*/
		SendMiscClientMessage(MISC_MESSAGE_SET_COMPUTER_SERIAL_NUM,
				0, 0, client_platform.computer_serial_num, 0, 0, NULL);
	  	SendAdminAlert(ALERT_2, "%-15.15s Assigning new computer s/n #%5d",
				ip_str, client_platform.computer_serial_num);
	}

	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Get a ptr to the CLientStateInfo structure given a table serial number.
// Returns NULL if we're not joined to the table.
//
struct ClientStateInfo *Player::GetClientStateInfoPtr(WORD32 table_serial_number)
{
	struct ClientStateInfo *result = NULL;
	EnterCriticalSection(&PlayerCritSec);
	int index = SerialNumToJoinedTableIndex(table_serial_number);
	if (index >= 0) {
		result = &JoinedTables[index].client_state_info;
	}
	LeaveCriticalSection(&PlayerCritSec);
	return result;
}

//****************************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct ClientStateInfo.
//
ErrorType Player::ProcessClientStateInfo(struct ClientStateInfo *input_state_info, int input_structure_len)
{
	if (sizeof(*input_state_info) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) Client State Info was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*input_state_info),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// The size matches, it's probably a valid structure.

	// Always pass the entropy from the client to the rng.
	RNG_AddToEntropy(input_state_info->random_bits);
	
	// Verify that we're actually joined to the game.
	if (VerifyJoinedTable(input_state_info->table_serial_number) != ERR_NONE) {
	  #if 0	// 2022 kriskoin
		Error(ERR_ERROR, "%s(%d) Received ClientStateInfo packet for a game we're not joined to (%d), Tossing it.",
				_FL, input_state_info->table_serial_number);
	  #endif
		return ERR_ERROR;
	}

	// We're joined to that game... save the structure.
	EnterCriticalSection(&PlayerCritSec);
	int index = SerialNumToJoinedTableIndex(input_state_info->table_serial_number);
	if (index >= 0) {
	  #if 0	// 2022 kriskoin
		if (DebugFilterLevel <= 10) {
			if (!JoinedTables[index].watching_flag &&
				input_state_info->sitting_out_flag != JoinedTables[index].client_state_info.sitting_out_flag
			) {
				kp(("%s %s(%d) %s set sit-out flag to %d (was %d) %stable %s (game #%d)\n",
						TimeStr(), _FL,
						user_id,
						input_state_info->sitting_out_flag,
						JoinedTables[index].client_state_info.sitting_out_flag,
						(JoinedTables[index].gcd.flags & GCDF_TOURNAMENT) ? "tournament " : "",
						JoinedTables[index].table_name,
						JoinedTables[index].gcd.game_serial_number));
			}
		}
	  #endif

		JoinedTables[index].client_state_info = *input_state_info;	// copy it.
		// 24/01/01 kriskoin:
		if (JoinedTables[index].gcd.flags & GCDF_TOURNAMENT) {
			JoinedTables[index].client_state_info.post_in_turn = TRUE;
		}
	}
	LeaveCriticalSection(&PlayerCritSec);
	SendPing();	// sprinkle pings in a few places of the code
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct ConnectionClosing.
//
ErrorType Player::ProcessConnectionClosing(struct ConnectionClosing *cc, int input_structure_len)
{
	if (sizeof(*cc) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) struct ConnectionClosing was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*cc),input_structure_len, player_id);
		ProcessPlayerLogoff();

		return ERR_ERROR;	// do not process.
	}

	// The size matches, it's probably a valid structure.
	// For now, all we do is close the connection.
	ProcessPlayerLogoff();
	NOTUSED(cc);
	return ERR_NONE;
}

/**********************************************************************************
 Function Player::ProcessReqHandHistory(struct CardRoom_ReqHandHistory *crhh, int input_structure_len)
 date: kriskoin 2019/01/01 Purpose: process a request to generate a hand history
***********************************************************************************/
ErrorType Player::ProcessReqHandHistory(struct CardRoom_ReqHandHistory *crhh, int input_structure_len)
{
	if (sizeof(*crhh) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) ProcessReqHandHistory was wrong length (%d vs %d) from player_id $%08lx. Ignoring",
			_FL,sizeof(*crhh),input_structure_len,player_id);
		return ERR_ERROR;	// do not process.
	}
	if (ANONYMOUS_PLAYER(player_id)) {
		Error(ERR_ERROR, "%s(%d) Anonymous player $%08lx somehow requested a hand history.  Ignoring.", _FL, player_id);
		return ERR_ERROR;	// do not process.
	}
	// validate the hand_number
	if (crhh->request_type == HHRT_INDIVIDUAL_HAND && crhh->hand_number < 1) { // out of range
		Error(ERR_NOTE, "%s(%d) Request for hand history for game %d is out of range", _FL, crhh->hand_number);
		return ERR_ERROR;	// do not process.
	}
	//kriskoin: 	if (crhh->request_type == HHRT_LAST_N_HANDS) {
		crhh->hand_number = min(crhh->hand_number, GAMES_TO_RECORD_PER_PLAYER);
		crhh->hand_number = max(crhh->hand_number, 1);
	} else if (crhh->request_type == HHRT_LAST_N_ALLIN_HANDS) {
		crhh->hand_number = min(crhh->hand_number, ALLINS_TO_RECORD_PER_PLAYER);
		crhh->hand_number = max(crhh->hand_number, 1);
	}
	int show_all_cards_flag = (priv >= ACCPRIV_CUSTOMER_SUPPORT && crhh->admin_flag);
	WORD32 source_player_id = crhh->player_id;	// who's hands do we want?
	if (!source_player_id) {
		source_player_id = player_id;	// default to OUR hands
	}
	if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
		source_player_id = player_id;	// always forced to current if not an admin
	}
	int success = PL->QueueHandHistoryReq((HandHistoryRequestType)crhh->request_type,
			crhh->hand_number,
			player_id,			// the player id we want to email them to
			source_player_id,	// the player id who's games we want
			show_all_cards_flag);
	if (DebugFilterLevel <= 0) {
		kp(("%s(%d) Hand history request by player '%s' (%s - %d) %s successfully queued\n", _FL,
			user_id, (crhh->request_type == HHRT_INDIVIDUAL_HAND ? "single hand" : "multiple hands"),
			crhh->hand_number, 	success ? "was" : "WAS NOT"));
	}
	NOTUSED(success);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//

// Process a received DATATYPE_VERSION_NUMBER packet
//
void Player::ProcessVersionNumber(struct VersionNumber *cvn, int input_structure_len)
{
	// Version information about a program (local or remote).
	if (sizeof(*cvn) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) VersionNumber was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",
				_FL,sizeof(*cvn),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return;
	}

	// Save Client's VersionNumber
	client_version_number = *cvn;
  #if DISP	// 2022 kriskoin
	kp(("%s(%d) Client version is %d.%02d (build #%d) flags = $%04x\n",_FL,
			client_version_number.major,
			client_version_number.minor,
			client_version_number.build,
			client_version_number.flags));
	kp1(("%s(%d) *** We should make sure the client is not out of date.\n",_FL));
  #endif
	if (client_version_number.flags & VERSIONFLAG_SIMULATED) {
		// This isn't a real client, it's only one that is trying to simulate
		// the I/O load of a real client (for testing lots of connections).
		static int iSimulatedClientCount;
		iSimulatedClientCount++;
		if (iSimulatedClientCount <= 20 || !(iSimulatedClientCount % 100)) {
			kp(("%s(%d) Detected simulated client number %d\n", _FL, iSimulatedClientCount));
		}


		// Log them in anonymously...
		struct PlayerLoginRequest plr;
		zstruct(plr);
		strnncpy(plr.user_id, SZ_ANON_LOGIN, MAX_PLAYER_USERID_LEN);
		strnncpy(plr.password, SZ_ANON_LOGIN, MAX_PLAYER_PASSWORD_LEN);
		ProcessPlayerLoginRequest(&plr, sizeof(plr));

	  #if 0	// 2022 kriskoin
		// Auto-join them to watch a table and forget about them.
		struct CardRoom_JoinTable jt;
		zstruct(jt);
		jt.status = JTS_WATCH;

		// Now pick a table to join.
		{
			int i;
			//*** Warning: this is done WITHOUT locking the table list.
			// Since this is debug-only code, we don't particularly care,
			// but it's possible that it's ok because I think the
			// PlrInputCritSec might be locked before tables appear
			// and disappear anyway (it calls EnterCriticalSection_CardRoom()).

			// First, try to pick one randomly.
			for (i=0; i < CardRoomPtr->table_count; i++) {
				Table *t = CardRoomPtr->tables[random(CardRoomPtr->table_count)];
				if (!t || !t->game) {	// no active game at the table
					continue;
				}
				if (t->watching_player_count >= 50) {
					continue;	// too many players watching this one already.
				}
				// This one looks good.
				jt.table_serial_number = t->table_serial_number;
				break;
			}

			if (!jt.table_serial_number) {
				// didn't find anything randomly, try sequentially.
				for (i=0; i < CardRoomPtr->table_count; i++) {
					Table *t = CardRoomPtr->tables[i];
					if (!t || !t->game) {	// no active game at the table
						continue;
					}
					if (t->watching_player_count >= 10) {
						continue;	// too many players watching this one already.
					}
					// This one looks good.
					jt.table_serial_number = t->table_serial_number;
					break;
				}
			}
			if (!jt.table_serial_number) {
				// Didn't find anything, make another pass and pick any table.
				for (i=0; i < CardRoomPtr->table_count; i++) {
					Table *t = CardRoomPtr->tables[i];
					if (!t) {
						continue;
					}
					if (t->watching_player_count >= 80) {
						continue;	// too many players watching this one already.
					}
					// This one looks good.
					jt.table_serial_number = t->table_serial_number;
					break;
				}
			}
			if (jt.table_serial_number) {
				//kp(("%s(%d) Joining table number %d\n", _FL, jt.table_serial_number));
				ProcessJoinTableRequest(&jt, sizeof(jt));
			}
		}
	  #endif	// simulated players auto-joining a table to watch
	}
}	

/**********************************************************************************
 Function ProcessCheckRefund
 date: 24/01/01 kriskoin Purpose: refund player's pending check amount to his available account
***********************************************************************************/
ErrorType Player::ProcessCheckRefund(void)
{
	// don't allow a check refund if the player already has a transaction in the queue
	if (IsPlayerInEcashQueue(player_id)) {
		char msg[200];
		zstruct(msg);
		sprintf(msg, "Transaction NOT PROCESSED:\n\n"
					 "You have already submitted a transaction.\n\n"

					 "Please wait for your current transaction to finish\n"
					 "processing before attempting another one.");
		SendMiscClientMessage(MISC_MESSAGE_ECASH, 0, 0, 0, 0, 0, msg);
		return ERR_ERROR;
	}

	if (TestIfCashierDisabledForClient()) {
		return ERR_ERROR;	// not processed
	}

	//Tony Tu, Dec 21, 2001
	//Check if the SDBRECORD_FLAG_NO_CASHOUT bit has been set
	if (TestIfCashOutDisabledForClient()) {
		return ERR_ERROR;
	}
	struct SDBRecord sdb;
	zstruct(sdb);
	int index = SDB->SearchDataBaseByPlayerID(player_id, &sdb);
	if (index < 0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src", 
			_FL, player_id);
		return ERR_ERROR;
	}

	// get current pending check amount	
	INT32 pending_check_amt = sdb.pending_check;
	INT32 pending_paypal_amt = sdb.pending_paypal;
	if ((!pending_check_amt)&&(!pending_paypal_amt))  {	// nothing to do -- ignore
		return ERR_NONE;
	}
	if ( pending_check_amt) {
	PL->LogFinancialTransfer(LOGTYPE_TRANSFER, player_id, player_id, pending_check_amt, 
		2, 0, CT_REAL, "Client auto-check-refund");
        
	AddToEcashLog(player_id, "ChkRefund", pending_check_amt,"Player", "Check", "Refund", "..",0, "..", 0, 0);
	SDB->TransferChips(CT_REAL, player_id, 2, player_id, 0 , pending_check_amt, _FL);
	// log it to transaction history
	ClientTransaction ct;
	zstruct(ct);
	ct.credit_left = 0;
	ct.timestamp = time(NULL);
	ct.transaction_amount = pending_check_amt;
	ct.transaction_type = CTT_CHECK_REFUND;

	ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(player_id);
	SDB->LogPlayerTransaction(player_id, &ct);
	}
        
	if ( pending_paypal_amt) {
        PL->LogFinancialTransfer(LOGTYPE_TRANSFER, player_id, player_id, pending_paypal_amt,
                2, 0, CT_REAL, "Client auto-paypal-refund");
        
        AddToEcashLog(player_id, "PaypalRefund", pending_paypal_amt,"Player", "Paypal", "Refund", "..",0, "..", 0, 0);
        SDB->TransferChips(CT_REAL, player_id, 3, player_id, 0 , pending_paypal_amt, _FL);
        // log it to transaction history
        ClientTransaction ct;
        zstruct(ct);
        ct.credit_left = 0;
        ct.timestamp = time(NULL);
        ct.transaction_amount = pending_paypal_amt;
        ct.transaction_type = CTT_PAYPAL_REFUND;
        ct.ecash_id = SDB->GetNextTransactionNumberAndIncrement(player_id);
        SDB->LogPlayerTransaction(player_id, &ct);
	SDB->RefundPaypalCredit(player_id, pending_paypal_amt);
        }

	// notify the player
	ChipBalancesHaveChanged(FALSE);
	SendClientInfo();
	char str[200];
	char str2[MAX_CURRENCY_STRING_LEN];
	char str3[MAX_CURRENCY_STRING_LEN];
	char str4[MAX_CURRENCY_STRING_LEN];
	zstruct(str);
	zstruct(str2);
	zstruct(str3);
	zstruct(str4);
	sprintf(str,"Your %s pending check has been cancelled and returned to your account.\n\n"
		"and/or\n\nYour %s pending PayPal cashout has been cancelled and returned to your account.\n\n"
		"Your new balance is %s\n",
		CurrencyString(str2, pending_check_amt, CT_REAL, TRUE),
		CurrencyString(str3, pending_paypal_amt, CT_REAL, TRUE),
		CurrencyString(str4, RealInBank+RealInPlay+EcashCredit, CT_REAL, TRUE));
	SendMiscClientMessage(MISC_MESSAGE_ECASH, 0,0,0,0,0, str);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a GamePlayerInputResult structure.
//
ErrorType Player::ProcessInputResult(struct GamePlayerInputResult *ir, int input_structure_len)
{
	// Process struct GamePlayerInputResult
	
	if (sizeof(*ir) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) GamePlayerInputResult was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*ir),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// The size matches, it's probably a valid structure.  Verify that
	// we're actually joined to the game.
	if (VerifyJoinedTable(ir->table_serial_number) != ERR_NONE) {
		Error(ERR_ERROR, "%s(%d) Received InputResult packet for a game we're not joined to (%d), Tossing it.",
				_FL, ir->table_serial_number);
		return ERR_ERROR;
	}

	// Always pass the entropy from the client to the rng.
	// It doesn't matter if we're joined to a table or not.
	RNG_AddToEntropy(ir->random_bits);

	// We're joined to that game... save the structure.
	EnterCriticalSection(&PlayerCritSec);
	int index = SerialNumToJoinedTableIndex(ir->table_serial_number);
	if (index >= 0) {
		// If this is a duplicate of the last one we received, just
		// toss it out.  That situation happens regularly if the server
		// asked twice for the result and the request and result crossed
		// paths during the latency.  The 2nd copy would arrive shortly
		// after the first one.
		if (!memcmp(ir, &JoinedTables[index].input_result2, sizeof(*ir))) {
			// It's exactly the same.  We can just toss it out.
			LeaveCriticalSection(&PlayerCritSec);

			//kp(("%s(%d) Received a duplicate input result for game %d (#%d).  Ignoring it.\n", _FL, ir->game_serial_number, ir->input_request_serial_number));
			return ERR_NONE;
		}
	  #if 0	//kriskoin: 		// Warn if we're going to stomp on a previous input result.
		if (JoinedTables[index].input_result.table_serial_number) {
			JoinedTables[index].input_result.cpu_tsc = ir->cpu_tsc;	// copy it so compare doesn't break
			int different = memcmp(&JoinedTables[index].input_result, ir, sizeof(*ir));
			if (different) {	// don't print if it's identical.
				Error(ERR_WARNING, "%s(%d) New InputResult for game %d (#%d) is stomping on an old one for game %d (#%d) (%s)",
					_FL,
					ir->game_serial_number,
					ir->input_request_serial_number,
					JoinedTables[index].input_result.game_serial_number,
					JoinedTables[index].input_result.input_request_serial_number,
					different ? "different data" : "exactly the same data");
			}
		}
	  #endif
		JoinedTables[index].input_result = *ir;		// copy it.
		JoinedTables[index].input_result2 = *ir;	// always make a 2nd copy
		zstruct(JoinedTables[index].input_request);	// clear it so we don't send it out again.
		waiting_for_input = FALSE;	// we're probably no longer waiting.
		update_now_flag = TRUE;		// request the cardroom try to update us asap

		//kriskoin: 		// Add this table to the list of tables which should get top
		// priority (because input has just become ready).

		// We only keep track of one table serial number at a time.
		// Worst case, one gets overwritten by another, but that's pretty
		// unlikely, even when playing two tables.  If it does happen, the
		// table simply doesn't get updated instantly, there would be a
		// slight pause (up to 1 second or so) before it gets updated.
		input_result_ready_table_serial_number = ir->table_serial_number;
	}
	LeaveCriticalSection(&PlayerCritSec);
	idle_flag = FALSE;	// clear idle flag any time we hear something meaningful from the client
	return ERR_NONE;
}

//****************************************************************
// https://github.com/kriskoin//
// Handle client's request to unjoin/join/watch a table.
//
ErrorType Player::ProcessJoinTableRequest(struct CardRoom_JoinTable *jt, int input_structure_len)
{
	if (sizeof(*jt) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) JoinTable request was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*jt),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}
	// verify the type of join
	if (jt->status > JTS_REBUY) {
		Error(ERR_ERROR, "%s(%d) JoinTable request status was illegal (%d)", _FL, jt->status);
		return ERR_ERROR;	// do not process.
	}
	idle_flag = FALSE;	// clear idle flag any time we hear something meaningful from the client

	// validate table
	if (jt->table_serial_number==0) {
		return ERR_ERROR;	// do not process
	}

  #if 0	// 2022 kriskoin
	char *action = "unjoin";
	if (jt->status== JTS_JOIN)
		action = "join";
	else if (jt->status == JTS_WATCH)
		action = "watch";
	kp(("%s(%d) Player $%08lx has requested to %s table %d\n",
			_FL, player_id, action, jt->table_serial_number));
  #endif

	//kriskoin: 	// to leave (which is still pending), and now we're trying to go
	// back to the table, disallow it.
	// This code replaces all the previous ghost code.
	// For tournaments, we're allowed to go back right away since the player
	// never loses his seat until booted from the tournament.
	EnterCriticalSection(&PlayerCritSec);
	int index = SerialNumToJoinedTableIndex(jt->table_serial_number, TRUE);
	if (index >= 0 &&
		JoinedTables[index].client_state_info.leave_table &&
		!(JoinedTables[index].gcd.flags & GCDF_TOURNAMENT))
	{
		SendMiscClientMessage(MISC_MESSAGE_GHOST,0,0,0,0,0,"Please wait for the current hand to finish before returning to the table");
		LeaveCriticalSection(&PlayerCritSec);
 		return ERR_ERROR;	// do not process.
	}
	LeaveCriticalSection(&PlayerCritSec);

	// Find an empty slot in the pending_joins array.
	EnterCriticalSection(&PlayerCritSec);
	if (pending_joins_count >= MAX_GAMES_PER_PLAYER) {
		LeaveCriticalSection(&PlayerCritSec);
		//Error(ERR_ERROR, "%s(%d) Player $%08lx is waiting to join too many tables.", _FL, player_id);
		return ERR_ERROR;
	}

	//kriskoin: 	// number, don't add the new request to the queue.  He's probably just pounding
	// on the re-buy but it's not getting processed because he's still involved in
	// the current hand.  Once it gets processed, he'll be able to do another.
	if (jt->status==JTS_JOIN || jt->status==JTS_REBUY) {
		for (int i=0 ; i<pending_joins_count ; i++) {
			if (pending_joins[i].table_serial_number==jt->table_serial_number) {
				// Found a dupe!  Throw it out.
				pr(("%s %s(%d) Tossing out dupe join/rebuy request for $%08lx (table %d)\n",
						TimeStr(), _FL, player_id, jt->table_serial_number));
				LeaveCriticalSection(&PlayerCritSec);
				return ERR_NONE;
			}
		}
	}
	pending_joins[pending_joins_count++] = *jt;
	LeaveCriticalSection(&PlayerCritSec);
	return ERR_NONE;
}

//****************************************************************
// https://github.com/kriskoin//
// Handle client's request to unjoin/join a waiting list
//
ErrorType Player::ProcessJoinWaitListRequest(struct CardRoom_JoinWaitList *jwl, int input_structure_len)
{
	if (sizeof(*jwl) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) JoinWaitList request was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*jwl),input_structure_len,player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// Verify some parms...
	if (jwl->status > 1) {
		Error(ERR_ERROR, "%s(%d) JoinWaitList request status was illegal (%d)", _FL, jwl->status);
		return ERR_ERROR;	// do not process.
	}

	if (jwl->client_display_tab_index >= MAX_CLIENT_DISPLAY_TABS) {
		Error(ERR_ERROR, "%s(%d) JoinWaitList request client_display_tab_index was illegal (%d)", _FL, jwl->client_display_tab_index);
		return ERR_ERROR;	// do not process.
	}

	// 24/01/01 kriskoin:
	if (priv == ACCPRIV_PLAY_MONEY && jwl->chip_type != CT_PLAY) {
		Error(ERR_ERROR, "%s(%d) JoinWaitList by play-money player %08lx was for a chip-type %d table. Ignoring",_FL, player_id, jwl->chip_type);
		return ERR_ERROR;	// do not process.
	}

	// Support old clients that don't fill in the game_rule variable.
	if (jwl->game_rules==0) {
		static BYTE8 game_rule_mappings[MAX_CLIENT_DISPLAY_TABS] = {
			GAME_RULES_HOLDEM,
			GAME_RULES_OMAHA_HI,
			GAME_RULES_STUD7,
			GAME_RULES_HOLDEM,	// one on one hold'em
			GAME_RULES_OMAHA_HI_LO,
			GAME_RULES_STUD7_HI_LO,
			GAME_RULES_HOLDEM,	// tournaments
		};
		jwl->game_rules = game_rule_mappings[jwl->client_display_tab_index];
	}

	// jwl->status values: 0=unjoin, 1=join
	char *action = "unjoin";
	if (jwl->status==1)
		action = "join";
	//kp(("%s(%d) Player $%08lx has requested to %s a waiting list. table=%d, stakes=%d, min_players_required=%d\n", _FL, player_id, action, jwl->table_serial_number, jwl->desired_stakes, jwl->min_players_required));

	idle_flag = FALSE;	// clear idle flag any time we hear something meaningful from the client

	// Find an empty slot in the pending_waitlists array.
	EnterCriticalSection(&PlayerCritSec);
	if (pending_waitlist_count >= MAX_GAMES_PER_PLAYER) {
		LeaveCriticalSection(&PlayerCritSec);
		Error(ERR_ERROR, "%s(%d) Player $%08lx is waiting to join too many waiting lists.", _FL, player_id);
		return ERR_ERROR;
	}
	pending_waitlists[pending_waitlist_count++] = *jwl;
	LeaveCriticalSection(&PlayerCritSec);
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Process the receipt of a struct KeepAlive
//
ErrorType Player::ProcessKeepAlive(struct KeepAlive *ka, int input_structure_len)
{
	if (sizeof(*ka) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) struct KeepAlive was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*ka),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}
	last_keep_alive = *ka;

	// Always pass the entropy from the client to the rng.
	RNG_AddToEntropy(ka->random_bits);

	if (!(last_keep_alive.status_bits & 0x01)) {
		idle_flag = TRUE;	// set idle flag any time the active bit was not set.
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Handle the receipt of a struct MiscClientMessage
//
ErrorType Player::ProcessMiscClientMessage(struct MiscClientMessage *mcm, int input_structure_len)
{
	if (sizeof(*mcm) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) struct MiscClientMessage was wrong length (%d vs %d) from player_id $%08lx. Disconnecting.",_FL,sizeof(*mcm),input_structure_len, player_id);
		ProcessPlayerLogoff();
		return ERR_ERROR;	// do not process.
	}

	// The size matches, it's probably a valid structure.
	if (mcm->message_type == MISC_MESSAGE_CHOOSE_BAR_SNACK) {
		SelectBarSnack(mcm->table_serial_number, (BAR_SNACK)mcm->misc_data_1);
		return ERR_NONE;	// done processing.
	}

	// Client requesting to receive/not receive their tournament summary email
	if (mcm->message_type == MISC_MESSAGE_REQ_TOURN_SUMMARY_EMAIL) {
		ProcessTournSummaryEmailReq(mcm);
		return ERR_NONE;
	}
	// Client requesting to have their all-ins reset?
	if (mcm->message_type == MISC_MESSAGE_REQ_ALLIN_RESET) {
		ProcessAllInResetRequest();
		return ERR_NONE;	// done processing.
	}
	// Client submitting their phone number?
	if (mcm->message_type == MISC_MESSAGE_SEND_PHONE_NUMBER) {
		return ProcessSubmitPhoneNumber(mcm);
	}
	// Admin client request to set a specifc player_id's cards to a max_allowable_usage
	if (mcm->message_type == MISC_MESSAGE_SET_MAX_ALLOWABLE_USAGE) {
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT) {
			admin_set_max_accounts_for_cc = mcm->misc_data_1;
			admin_set_max_accounts_for_cc_count = (int)mcm->misc_data_2;
		}
		return ERR_NONE;
	}

	// Admin client request to set main account for a particular cc?
	if (mcm->message_type == MISC_MESSAGE_SET_ACTIVE_ACCOUNT_FOR_CC) {
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT) {
			admin_set_player_id_for_cc = mcm->misc_data_1,	// player id to make active
			admin_set_cc_number = mcm->misc_data_2;			// partial cc number
		}
		return ERR_NONE;
	}

	if (mcm->message_type == MISC_MESSAGE_INSERT_BLANK_PLAYERID) {
		// admin: insert a blank player id in the computer serial num
		// database so they can create another new account
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT && mcm->misc_data_1) {
			SerNumDB_InsertBlankPlayerID(mcm->misc_data_1);
		}
		return ERR_NONE;
	}

	if (mcm->message_type == MISC_MESSAGE_MOVE_TO_TOP_OF_WAITLISTS) {
		// admin: move a player to the top of any waiting lists they're on.
		if (priv >= ACCPRIV_CUSTOMER_SUPPORT && mcm->misc_data_1) {
			admin_move_player_to_top_wait_lists = mcm->misc_data_1;
		}
		return ERR_NONE;
	}

	// admin: request some information from the server (MMRI_*)
	if (mcm->message_type == MISC_MESSAGE_REQ_INFO) {
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
			Error(ERR_ERROR, "%s(%d) Admin info request received from a non-admin user ($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;

		}
		switch (mcm->display_flags) {
		case MMRI_CHARGEBACK:		// generate an email when player charges back
			ProcessAdminReqChargebackLetter(player_id, mcm->misc_data_1);
			break;
		case MMRI_AIL:				// generate Good-All-In letter for player
			ProcessAdminReqAllInLetter(player_id, mcm->misc_data_1);
			break;
		case MMRI_PLAYERINFO:		// generate a call to 'playerinfo' (which will email results)
			ProcessAdminReqPlayerInfo(player_id, mcm->misc_data_1, mcm->misc_data_2, mcm->misc_data_3, mcm->misc_data_4);
			break;
		default:
			Error(ERR_ERROR, "%s(%d) Admin info request received with unsupported request (%d). Tossing.", _FL, mcm->display_flags);
		}

		return ERR_NONE;	// done processing.
	}		

	// admin: send email to account outlining current credit limits
	if (mcm->message_type == MISC_MESSAGE_SEND_CREDIT_LIMITS_EMAIL) {
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
			Error(ERR_ERROR, "%s(%d) send credit limits email request received from a non-admin user ($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;
		}
		return ProcessSendCreditLimitEmail(mcm);
	}
	
	// admin: send email to accounts where $ was just xfered
	if (mcm->message_type == MISC_MESSAGE_SEND_XFER_EMAIL) {
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
			Error(ERR_ERROR, "%s(%d) Xfer email request received from a non-admin user ($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;
		}
		return ProcessSendXferEmail(mcm);
	}

	// Admin client request to block/unblock a computer serial number?
	if (mcm->message_type == MISC_MESSAGE_SET_COMPUTER_SERIAL_NUM_BLOCK) {
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
			Error(ERR_ERROR, "%s(%d) Computer block request received from a non-admin user ($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;
		}
		if (mcm->misc_data_1) {
			if (mcm->misc_data_2) {
				AddComputerBlock(mcm->misc_data_1);		// block this computer
			} else {
				RemoveComputerBlock(mcm->misc_data_1);	// unblock this computer
			}
		}
		return ERR_NONE;	// done processing.
	}


	// Admin client requesting we start a check run?
	if (mcm->message_type == MISC_MESSAGE_START_CHECK_RUN) {
		if (priv < ACCPRIV_ADMINISTRATOR) {
			Error(ERR_ERROR, "%s(%d) Check run request received from a non-admin user ($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;
		}
		ECash_BeginCheckRun(player_id);	// scan for clients that need checks.
		return ERR_NONE;
	}

	// broadcast message?
	if (mcm->message_type == MISC_MESSAGE_CL_REQ_BROADCAST_MESSAGE) {
		if (priv < ACCPRIV_CUSTOMER_SUPPORT) {
			Error(ERR_ERROR, "%s(%d) Broadcast message received from a non CUSTOMER_SUPPORT($%08lx). Tossing.", _FL, player_id);
			return ERR_ERROR;
		}
		// Ask cardroom to broadcast it to everyone.
		PPEnterCriticalSection0(&PlayerCritSec, _FL, TRUE);	// *** DO NOT COPY this line unless you know EXACTLY what you're getting into.
		((CardRoom *)cardroom_ptr)->SendBroadcastMessage(mcm);
		LeaveCriticalSection(&PlayerCritSec);
		return ERR_NONE;
	}

	// request for more free chips
	if (mcm->message_type == MISC_MESSAGE_CL_REQ_FREE_CHIPS) {
		// verify that he didn't hack a client to give himself endless free chips
		if (FakeInBank+FakeInPlay > REQ_MORE_FREE_CHIPS_LEVEL) {
			Error(ERR_ERROR, "%s(%d) PlayerID $%08lx somehow requested more free chips, but already has %d",
				_FL, player_id, FakeInBank+FakeInPlay);
		} else {
			// top him up to 2000 play chips
			int chips_to_give = 200000 - (FakeInBank+FakeInPlay);
			AddChipsToAccount(player_id, chips_to_give, CT_PLAY, "Requested more free chips", FALSE, NULL, NULL);
			FakeInBank += chips_to_give;	// since we have an active player object (we're it)
			send_account_info = TRUE;	// flag it needs resending to the player
		}
		return ERR_NONE;
	}

	// request to have current balance of chips in pending checks -- refunded
	if (mcm->message_type == MISC_MESSAGE_CASHOUT_REFUND) {
		return ProcessCheckRefund();
	}

	// request to change creditable left on a particular transaction
	if (mcm->message_type == MISC_MESSAGE_SET_TRANS_CREDIT) {
	     //	return ProcessSetTransactionCreditableAmt(mcm);
	}

	// request to modify tracking info for a particular check
	if (mcm->message_type == MISC_MESSAGE_SET_CHECK_TRACKING_INFO) {
		return ProcessSetCheckTrackingInfo(mcm);
	}

	// unknown misc message type
	Error(ERR_ERROR, "%s(%d) ProcessMiscClientMessage received unknown message type %d for player_id $%08lx",
		_FL, mcm->message_type, player_id);
	return ERR_ERROR;
}

//*********************************************************
// https://github.com/kriskoin//
// Process a received struct Ping structure
// DATATYPE_PING
//
ErrorType Player::ProcessPing(struct Ping *ping, int input_structure_len)
{
	if (sizeof(*ping) != input_structure_len) {
		Error(ERR_ERROR, "%s(%d) Ping was wrong length (%d vs %d)",_FL,sizeof(*ping),input_structure_len);
		return ERR_ERROR;	// do not process.
	}

	// Always pass the entropy from the client to the rng.
	RNG_AddToEntropy(ping->random_bits);

	// If it came from the server, send it back.
	if (ping->flags & 0x01) {	// client to server and back?
		// It was one of theirs... send it back
		SendDataStructure(DATATYPE_PING, ping, sizeof(*ping));
	} else {
		WORD32 elapsed = GetTickCount() - ping->local_ms_timer;
		// It originated on the server... keep track of elapsed time.
		// Add it to our array of recent pings for this player.
		EnterCriticalSection(&PlayerCritSec);
		memmove(&PingResults[1], &PingResults[0], sizeof(PingResults[0])*(PLAYER_PINGS_TO_RECORD-1));
		zstruct(PingResults[0]);
		PingResults[0].time_of_ping = SecondCounter;	// record when we heard back
		PingResults[0].duration_for_ping = elapsed;
		//kp(("%s(%d) Got back ping from %s: elapsed = %dms\n", _FL, user_id, elapsed));

		// Remove it from the sent array (if it's there) so we know that
		// it's not outstanding anymore.
		for (int i=0 ; i<PLAYER_PINGS_TO_RECORD ; i++) {
			if (PingSendTimes[i] && PingSendTimes[i] <= ping->local_ms_timer) {
				PingSendTimes[i] = 0;	// this one matched up.  Remove from pending list.
			}
		}
		LeaveCriticalSection(&PlayerCritSec);
		//kp(("%s(%d) Elapsed time for ping from server to client and back: %dms\n", _FL, elapsed));
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Process DATATYPE_PLAYER_LOGOFF
//
ErrorType Player::ProcessPlayerLogoff(void)
{
	// Client program is exiting... there is no point in any
	// further communications; they're gone.
	// We may as well close the connection and mark the player as
	// disconnected.
	// For now, we won't unjoin him from tables and waiting lists
	// on the off chance he wants to call back up immediately.  Our
	// regular code should kick him off tables properly anyway.
	EnterCriticalSection(&PlayerCritSec);
	if (server_socket) {
		char str[20];
		IP_ConvertIPtoString(server_socket->connection_address.sin_addr.s_addr, str, 20);
		WORD32 disconnect = SecondCounter;
		if (server_socket->time_of_disconnect) {
			disconnect = server_socket->time_of_disconnect;
		}
		WORD32 elapsed_time = disconnect - server_socket->time_of_connect;
		if (DebugFilterLevel <= 2) {
			kp(("%s %-15.15s Logoff  %-15s $%08lx Duration %dm\n",
					TimeStr(), str, user_id, player_id, elapsed_time/60));
		}
		ConnectionLog->Write(
				"%s %-15.15s Logoff '%s' %06lx sock %04x Duration %dm\n",
				TimeStr(), str, user_id, player_id, server_socket->sock, elapsed_time/60);
		// logfile entry	
		PL->LogLogout(user_id, player_id, server_socket->connection_address.sin_addr.s_addr,
			RealInBank, RealInPlay, FakeInBank, FakeInPlay);
		disconnect_message_printed = TRUE;
		server_socket->CloseSocket();
	}
	sprintf(sql,"SELECT sp_update_logged_status('%s','FALSE')",user_id);
  	((CardRoom *)cardroom_ptr)->theOPAptr->AddMessage(sql, DB_NORMAL_QUERY);
	LeaveCriticalSection(&PlayerCritSec);
	return ERR_NONE;
}

/**********************************************************************************
 Function Player::BuyingIntoTable()
 date: kriskoin 2019/01/01 Purpose: Player is buying into a table with this many chips
***********************************************************************************/
void Player::BuyingIntoTable(WORD32 chips, ChipType chip_type)
{
	// convert chip_type to CT_REAL if we're buying into a tournament
	chip_type = (chip_type == CT_TOURNAMENT ? CT_REAL : chip_type);
	WORD32 chips_in_bank = SDB->GetChipsInBankForPlayerID(player_id, chip_type);
	WORD32 chips_in_play = SDB->GetChipsInPlayForPlayerID(player_id, chip_type);
	switch (chip_type) {
	case CT_NONE:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
		break;
	case CT_PLAY:
		FakeInBank = chips_in_bank - chips;
		FakeInPlay = chips_in_play + chips;
		SDB->SetChipsInBankForPlayerID(player_id, FakeInBank, chip_type);
		SDB->SetChipsInPlayForPlayerID(player_id, FakeInPlay, chip_type);
		break;
	case CT_REAL:
		RealInBank = chips_in_bank - chips;
		RealInPlay = chips_in_play + chips;
		SDB->SetChipsInBankForPlayerID(player_id, RealInBank, chip_type);
		SDB->SetChipsInPlayForPlayerID(player_id, RealInPlay, chip_type);
		break;
	case CT_TOURNAMENT:	// can't happen, see above (should have been converted to CT_REAL)
		Error(ERR_INTERNAL_ERROR,"%s(%d) Should've been converted to CT_REAL -- see src", _FL);
		break;
	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	send_account_info = TRUE;	// flag it needs resending to the player
}

/**********************************************************************************
 Function Player::LeavingTable(WORD32 chips, int real_money_flag)
 date: kriskoin 2019/01/01 Purpose: Player is leaving a table with this many chips
***********************************************************************************/
void Player::LeavingTable(WORD32 chips, ChipType chip_type)
{
	if (!chips) {
		return;	// no changes to be made... don't bother updating the database.
	}

	WORD32 chips_in_bank = SDB->GetChipsInBankForPlayerID(player_id, chip_type);
	WORD32 chips_in_play = SDB->GetChipsInPlayForPlayerID(player_id, chip_type);
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			FakeInBank = chips_in_bank + chips;
			FakeInPlay = chips_in_play - chips;
			SDB->SetChipsInBankForPlayerID(player_id, FakeInBank, chip_type);
			SDB->SetChipsInPlayForPlayerID(player_id, FakeInPlay, chip_type);
			break;
		case CT_TOURNAMENT:
			// if the tournament hasn't started, he gets his money back (real money)
			// this should not be called once the tournament has started!!
			// (fall through to real money)
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_TOURNAMENT (chips = %d)", _FL, chips);
			break;
		case CT_REAL:
			RealInBank = chips_in_bank + chips;
			RealInPlay = chips_in_play - chips;
			SDB->SetChipsInBankForPlayerID(player_id, RealInBank, chip_type);
			SDB->SetChipsInPlayForPlayerID(player_id, RealInPlay, chip_type);
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	send_account_info = TRUE;	// flag it needs resending to the player
}

//*********************************************************
// https://github.com/kriskoin//
// Count the number of all-in's allowed for this player.
//
int Player::AllowedAutoAllInCount(void)
{
	return ::AllowedAutoAllInCount(player_id);	// now handled in pokersrv.cpp
}


/**********************************************************************************
 Function Player::ProcessSubmitPhoneNumber()
 date: 24/01/01 kriskoin Purpose: handle a phone number submission from a player
***********************************************************************************/
ErrorType Player::ProcessSubmitPhoneNumber(struct MiscClientMessage *mcm)
{
	char encoded_number[PHONE_NUM_EXPANDED_LEN+1];
	zstruct(encoded_number);
	EncodePhoneNumber(encoded_number, mcm->msg);

	struct SDBRecord ar;
	zstruct(ar);
	int index = SDB->SearchDataBaseByPlayerID(player_id, &ar);
	if (index >= 0) {	// found it...
		memcpy(ar.phone_number, encoded_number, PHONE_NUM_LEN);
		SDB->WriteRecord(&ar);
		SendClientInfo();	// re-send latest version to client
	} else {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Should have found SDB record for %08lx but we didn't -- see src",
			_FL, player_id);
		return ERR_ERROR;
	}
	return ERR_NONE;
}
