#define WRITE_LEAKS	0
#if WRITE_LEAKS
  //void write_leaks(void);
#endif
 

//*********************************************************
//
//	CardRoom routines - Server side.
//
// 
//
//*********************************************************

#define LOTS_OF_COMPUTER_PLAYERS	1// put in lots of computer players at many tables?
#define LOTS_OF_TOURNAMENT_COMPUTER_PLAYERS	1	// put in lots of computer players at many tables?
#define TEST_SUMMARY_EMAIL  		0	// testing: normally 0

#define DISP 0
#define DISP_BANDWIDTH	0

#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>	// Needed for CritSec stuff
  #include <process.h>	// needed for _beginthread() stuff.

#else
  #include <malloc.h>

#endif
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include "cardroom.h"
#include "pokersrv.h"
#include "sdb.h"
#include "ecash.h"
#if USE_SELECT_FOR_INPUT && !WIN32
  #include <sys/time.h>
#endif
#if INCL_SSL_SUPPORT
  #include <openssl/ssl.h>
  #include <openssl/crypto.h>
  extern SSL_CTX *MainSSL_Server_CTX;
#endif

#define ADMIN_STATS_TO_KEEP		20	// keep stats on 1 minute intervals for n minutes
struct AdminStats CurrentAdminStats[ADMIN_STATS_TO_KEEP];
struct PacketStats CurrentPacketStats[DATATYPE_COUNT];
time_t NextAdminStatsUpdateTime;
time_t NextHeapCompactTime;
static int iTotalComputerPlayersAdded;

static struct TableNames  {
	char *name;
	WORD32 table_serial_number;	// set to the table which is currently using this name (if any)
} TableNames[] =  {
	// Note: MAX_TABLE_NAME_LEN is currently set to 16.
  //"123456789ABCDEF" (max 15 chars leaving room for a NULL)

"DesertPoker",0,  //"Tahiti", 0,
"Moon", 0,  //"Cayman", 0,
"Sun", 0,   //"Bermuda", 0,
"Jupiter",0 , //"Aruba", 0,
"Mercury", 0, //"Martinique", 0,
"Mars", 0,   //"St. Kitts", 0,
"Venus",0,   //"Rum Cay", 0,
"Yvette", 0,
"Yvonne",0,                          
"Abbe",0,
"Zamudio",0,                          
"Indira",0,                          
"Inkeri",0,                          
"Janice",0,                          
"Jhirad",0,                          
"Johnson",0,                         
"Kasra",0,                           
"Lagalla",0,                         
"Aachen",0,  
"Belinda",0,                          
"Bell",0,                            
"Belz",0,                            
"Caesar",0,                          
"Cepheus",0,                         
"Chom",0,                            
"Dersu",0, 
"Diana",0,                           
"Emden",0,                           
"Eridania",0,                        
"Eugenia",0,                         
"Fingal",0,
"Fiona",0,
"Gator",0,
"Gauss",0, 
"Gauguin",0, 
"Hall",0,
"Halley",0,
"Hios",0,
"Hope",0, 
"Howe",0,                            
"Icheko",0,
"Idas",0,                             
"Igaluk",0,                           
"Joliot",0,                          
"Jomo",0,                            
"Karoo",0,                           
"Kartabo",0,                         
"Kasabi",0,                          
"Fischer",0,                          
"Flag",0,                             
"Flank",0,                            
"Gamba",0,
"Gardo",0,

"Kasper ",0,                          
"Laica",0,                          
"Lalande",0,                          
"Lamas",0,                           
"Lamb",0,                            
"Mackin",0,                          
"Madeleine",0,                        
"Nardo",0,                           
"Olom",0,                            
"Madrid",0,                          
"Magadi",0,                          
"Magelhaens",0,                      
"Nanna",0,                           
"Nansen",0,                          
"Naomi",0,                            
"Palos",0,                            
"Perrine",0,                         
"Nedko",0,                            
"Ocampo",0,                          
"Ogier",0,                           
"Ogulbek",0,                          
"Oivit",0,                           
"Pettit",0,                          
"Wilma",0,  
"Windfall",0,                         
"Quimby",0,                           
"Quslu",0,                            
"Randver",0,                         
"Raphael",0,                         
"Zaqar",0,                           
"West",0,                             
"Palana",0,                          
"Pallas",0,                           
"Caieta",0,                          
"Cairns",0,                          
"Bellot",0,                          
"Beltra",0,                          
"Dido",0,                            
"Diment",0,                          
"Dollond",0,                         
"Eimmart",0,                         
"Eini",0,                            
"Aananin",0,
"Aban",0,
"Abans",0, 

		NULL, 0
};

struct SavedStats  {
	WORD32 game_number;
	WORD32 accounts;
	INT32  ecash_balance;
	INT32  rake_balance;


	INT32  ecashfee_balance;
	WORD32 gross_bets;				// gross real money bets (this number DOES wrap)
	WORD32 gross_tournament_buyins;	// gross tournament buyins (+fee) (this number DOES wrap)
} YesterdaySavedStats;

//*********************************************************
// https://github.com/kriskoin//
// Placeholder under Windows for the write_leaks function under linux
//
#if WRITE_LEAKS
void write_leaks(void)
{
	result = RegQueryValueEx(HKEY_PERFORMANCE_DATA,
				str, 0, KEY_ALL_ACCESS, &keyhandle);
	RegCloseKey(HKEY_PERFORMANCE_DATA);


  #if 0	// 2022 kriskoin
	PROCESS_MEMORY_COUNTERS meminfo;
	zstruct(meminfo);
	meminfo.cb = sizeof(meminfo);
	BOOL success = GetProcessMemoryInfo(GetCurrentProcess(), &meminfo, sizeof(meminfo));
	if (success) {
		static DWORD old_mem_usage;
		DWORD total = meminfo.WorkingSetSize + meminfo.PagefileUsage;
		AddToLog("Data/Logs/memusage.log", "Time\tWorking\tPaged\tTotal\tDiff\n",
				"%s\t%d\t%d\t%d\t%+d\n",
				TimeStr(),
				meminfo.WorkingSetSize,
				meminfo.PagefileUsage,
				total,
				(long)(total - old_mem_usage));
		old_mem_usage = total;
	}
  #endif
}
#endif

//*********************************************************
// https://github.com/kriskoin//
// Enter the cardroom critical section and lock down most of
// the related critical sections as well (such as the player
// list and the tables).
//
// These functions should not be called directly.  Use the
// macros EnterCriticalSection_CardRoom() and
//		  LeaveCriticalSection_CardRoom().
//
void EnterCriticalSection_CardRoom0(char *file, int line)
{
	PPEnterCriticalSection0(&CardRoomPtr->CardRoomCritSec, file, line);
	PPEnterCriticalSection0(&CardRoomPtr->PlrInputCritSec, file, line);
	PPEnterCriticalSection0(&ParmFileCritSec, file, line);
	//PPEnterCriticalSection0(&CardRoomPtr->TableListCritSec, file, line);
}
void LeaveCriticalSection_CardRoom0(char *file, int line)
{
	//PPLeaveCriticalSection0(&CardRoomPtr->TableListCritSec, file, line);
	PPLeaveCriticalSection0(&ParmFileCritSec, file, line);
	PPLeaveCriticalSection0(&CardRoomPtr->PlrInputCritSec, file, line);
	PPLeaveCriticalSection0(&CardRoomPtr->CardRoomCritSec, file, line);
}

//****************************************************************
// 
//
// Constructor/destructor for the CardRoom object
//
CardRoom::CardRoom(void)
{
	cardroom_threadid = getpid();
	connected_players = 0;
	max_connected_players = 0;
	players = NULL;
	next_table_serial_number = 1000;	// initialize to something non-zero.
	next_game_serial_number = 1;
	next_manage_tables_time = 0;
	next_summary_data_check_time = 0;
	next_mainloop_bgndtask_time = 0;	// Second counter when main loop should try running some of the background tasks again
	PPInitializeCriticalSection(&CardRoomCritSec, CRITSECPRI_CARDROOM, "CardRoom");
	PPInitializeCriticalSection(&PlrInputCritSec, CRITSECPRI_INPUT_THREAD, "PlrInput");
	//PPInitializeCriticalSection(&TableListCritSec, CRITSECPRI_TABLELIST, "TableList");

	zstruct(summary_serial_nums);

	memset(summary_info_changed, 0, sizeof(summary_info_changed[0])*MAX_CLIENT_DISPLAY_TABS);
	memset(table_summary_lists,  0, sizeof(table_summary_lists[0])*MAX_CLIENT_DISPLAY_TABS);
	memset(update_waiting_list,  0, sizeof(update_waiting_list[0])*MAX_CLIENT_DISPLAY_TABS);

	//pr(("%s(%d) sizeof(table_summary_lists[0]*MAX_GAME_TYPES) = %d\n", _FL, sizeof(table_summary_lists[0])*MAX_GAME_TYPES));

	kill_table = 0;	// no table needs killing yet.
	comm_thread_finished_flag = 0;
	accept_thread_finished_flag = 0;
	comm_thread_shutdown_flag = 0;
	accept_thread_shutdown_flag = 0;
	iPlayMoneyTableCount = iRealMoneyTableCount = iTournamentTableCount = 0;
	active_player_connections = 0;
	active_good_connections = 0;
	active_unseated_players = 0;
	active_tables = active_tables_temp = 0;
	active_real_tables = active_real_tables_temp = 0;
	connected_idle_players = 0;
	multi_seated_players = 0;
	daily_peak_active_players = 0;
	daily_peak_active_tables = 0;
	daily_peak_active_players_time = 0;


	daily_peak_active_tables_time = 0;
	daily_peak_active_real_tables = 0;
	daily_peak_active_real_tables_time = 0;
	daily_low_active_players = 0;
	daily_low_active_tables = 0;
	daily_low_active_real_tables = 0;

	lowest_active_game_number = 0;
	tables = NULL;		// ptrs to each table (no NULL ptrs)
	table_count = 0;	// # of entries in the tables[] array.
	admin_chat_queue_head = 0;
	admin_chat_queue_tail = 0;
	write_out_server_vars_now = FALSE;
	client_count_newer = 0;		// # of clients connected with newer than current version
	client_count_current = 0;	// # of clients connected with the current version
	client_count_old = 0;		// # of clients connected with old version
	avg_response_time_real_money = 0.0;	// avg response time for entire cardroom (real money tables)
	avg_response_time_play_money = 0.0;	// avg response time for entire cardroom (play money tables)
	accept_avg_ms = 50.0;		// average ms needed to process a single Accept().
	mainloop_avg_ms = 50.0;		// average ms needed to process entire main loop
	inputloop_avg_ms = 50.0;	// average ms needed to process entire input loop
	total_table_update_ms = 0;	// total # of ms used while updating tables
	total_tables_updated = 0;	// total # of tables updated (related to total_table_update_ms)
	table_names_used = 0;		// # of real money table names currently used
	table_names_avail = 0;		// # of real money table names available to be used (total)
	tournaments_played_today = 0;
	next_routing_update_message = 0;
	routing_update_email_fd = NULL;
	zstruct(routing_update_email_fname);

  #if USE_SELECT_FOR_INPUT
	FD_ZERO(&readset);
	max_fd = INVALID_SOCKET;
  #endif

  #if TEST_SUMMARY_EMAIL
	kp1(("%s(%d) WARNING: SUMMARY EMAIL BEING SENT AT CARDROOM STARTUP!\n",_FL));
	next_summary_email_seconds = SecondCounter + 90;
  #else
	next_summary_email_seconds = 0;
  #endif
}

CardRoom::~CardRoom(void)
{
	pr(("%s(%d) CardRoom destructor begin.\n",_FL));
	// We should wait here until we know that all of our threads
	// have exited.
	// signal our other threads to exit.
	comm_thread_shutdown_flag = TRUE;
	accept_thread_shutdown_flag = TRUE;

	while (!comm_thread_finished_flag && !accept_thread_finished_flag) {
		Sleep(50);
	}

	EnterCriticalSection(&CardRoomCritSec);
	//EnterCriticalSection(&TableListCritSec);
	// Delete any tables we still have allocated.
	while (table_tree.tree_root) {
		DeleteTable(((Table *)table_tree.tree_root->object_ptr)->table_serial_number);
	}
	//LeaveCriticalSection(&TableListCritSec);
	LeaveCriticalSection(&CardRoomCritSec);
	//PPDeleteCriticalSection(&TableListCritSec);
	//zstruct(TableListCritSec);
	PPDeleteCriticalSection(&CardRoomCritSec);
	zstruct(CardRoomCritSec);
	pr(("%s(%d) CardRoom destructor end.\n",_FL));
}


//****************************************************************
// 
//
// Non-class _cdecl function for _beginthread to call.  It will
// actually call the CardRoom->AcceptThreadEntry() point.
//
static void _cdecl AcceptThreadLauncher(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Accept thread");	// register this thread for stack dumps if we crash
	//kp(("%s(%d) AcceptThread: pid = %d, parent = %d\n", _FL, getpid(), getppid()));
	((CardRoom *)args)->AcceptThreadEntry();
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}

//****************************************************************



// 
//
// Launch the thread which creates the listen socket and accepts
// incoming connections.
//
ErrorType CardRoom::LaunchAcceptThread(void)
{
	//kp(("%s(%d) main thread calling _beginthread. Our pid = %d, ppid = %d\n", _FL, getpid(), getppid()));
	int result = _beginthread(AcceptThreadLauncher, 0, (void *)this);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
		return ERR_FATAL_ERROR;
	}
	//kp(("%s(%d) main thread back from _beginthread. Our pid = %d, ppid = %d\n", _FL, getpid(), getppid()));
	return ERR_NONE;
}


//****************************************************************
// 
//
// Non-class _cdecl function for _beginthread to call.  It will
// actually call the CardRoom->PlayerIOThreadEntry() point.
//
static void _cdecl PlayerIOThreadLauncher(void *args)
{
  #if INCL_STACK_CRAWL
	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl
  #endif
	RegisterThreadForDumps("Player input thread");	// register this thread for stack dumps if we crash
	//kp(("%s(%d) PlayerIOThread: pid = %d, parent = %d\n", _FL, getpid(), getppid()));
	((CardRoom *)args)->PlayerIOThreadEntry();
	UnRegisterThreadForDumps();
  #if INCL_STACK_CRAWL
	NOTUSED(top_of_stack_signature);
  #endif
}

//****************************************************************
// 
//
// Launch the thread which handles I/O for Player objects.
//
ErrorType CardRoom::LaunchPlayerIOThread(void)
{
	//kp(("%s(%d) Accept thread calling _beginthread. Our pid = %d, ppid = %d\n", _FL, getpid(), getppid()));
	int result = _beginthread(PlayerIOThreadLauncher, 0, (void *)this);
	if (result == -1) {
		Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);
		return ERR_FATAL_ERROR;
	}
	//kp(("%s(%d) Accept thread back from _beginthread. Our pid = %d, ppid = %d\n", _FL, getpid(), getppid()));
	return ERR_NONE;
}

//****************************************************************

// 
//
// Entry point for the accept() thread.
//
void CardRoom::AcceptThreadEntry(void)
{
	pr(("%s(%d) accept() thread is now active.\n",_FL));
	ServerListen *sl = new ServerListen;
	if (!sl) {
		Error(ERR_FATAL_ERROR, "%s(%d) Unable to create ServerListen object.",_FL);
		exit(ERR_FATAL_ERROR);
	}
  #if INCL_SSL_SUPPORT
	ServerListen *sl_ssl = new ServerListen;
	if (!sl_ssl) {
		Error(ERR_FATAL_ERROR, "%s(%d) Unable to create SSL ServerListen object.",_FL);
		exit(ERR_FATAL_ERROR);
	}
  #endif

	// Now that the socket has been created, wait to accept connections.

	pr(("%s(%d) Just above the AcceptConnection loop...\n",_FL));
	while (!accept_thread_shutdown_flag) {
		int work_was_done_flag = FALSE;
	  #if INCL_SSL_SUPPORT
	   #if 0	//kriskoin: 		AcceptConnections(sl, &work_was_done_flag, PortNumber, NULL);
	   #endif
		AcceptConnections(sl_ssl, &work_was_done_flag, PortNumber_SSL, MainSSL_Server_CTX);
	  #else
		AcceptConnections(sl, &work_was_done_flag, PortNumber);
	  #endif
		// If we didn't do anything this time through the loop, sleep for a moment.
		if (work_was_done_flag) {
			// short sleep when we did work (just enough to not tie up the cpu).
			// We put this sleep in here so there's a fairly long period of time
			// when we're guaranteed not to be holding any critical sections.
			Sleep(20);
		} else {
			// Much longer sleep when we didn't need to do anything.
			// This longer sleep increases the liklihood that we'll batch up
			// a few accepts the next time through.
			Sleep(500);
		}
	}

  #if INCL_SSL_SUPPORT
	delete sl_ssl;
	sl_ssl = NULL;
  #endif

	delete sl;
	sl = NULL;

	pr(("%s(%d) Accept thread is now exiting.\n", _FL));
	accept_thread_finished_flag = TRUE;	// signal that we're exiting.
}

//****************************************************************
// 
//
// Accept incoming connections and create Player objects for them
// work_was_done_flag is updated to indicate if any work was done.
//
#if INCL_SSL_SUPPORT
ErrorType CardRoom::AcceptConnections(ServerListen *sl, int *work_was_done_flag, int port_number, void *ssl_ctx)
#else
ErrorType CardRoom::AcceptConnections(ServerListen *sl, int *work_was_done_flag, int port_number)
#endif
{
	#define MAX_CONNECTIONS_ACCEPTED_PER_PASS	20	// max # we accept during each pass through the loop
	Player *new_players[MAX_CONNECTIONS_ACCEPTED_PER_PASS];
	zstruct(new_players);

	// First, make sure we've got room for at least one new connection.
	if (connected_players+MAX_CONNECTIONS_ACCEPTED_PER_PASS >= max_connected_players) {
		// We're out of room.  Re-allocate the array.
		EnterCriticalSection(&CardRoomCritSec);
		EnterCriticalSection(&PlrInputCritSec);
		int new_max = max(MAX_CONNECTIONS_ACCEPTED_PER_PASS,max_connected_players)*2;
		int new_len = sizeof(players[0])*new_max;
	  #if 0	//kriskoin: 		if (players) {	// not first time...
			kp(("%s(%d) Re-allocating player array to make room for %d players (previous max was %d). Size=%d bytes\n",
						_FL, new_max, max_connected_players, new_len));
		}
	  #endif
		volatile Player **new_players = (volatile Player **)malloc(new_len);
		if (!new_players) {
			Error(ERR_FATAL_ERROR, "%s(%d) Could not allocated space for player[] array (new_max=%d).", _FL, new_max);
			DIE("Memory problem");
		}
		memset(new_players, 0, new_len);	// always zero it out.
		// We've got the new array allocated.
		if (players) {	// previous array existed... copy it over.
			memmove(new_players, players, max_connected_players*sizeof(players[0]));
			free(players);
		}
		players = new_players;
		max_connected_players = new_max;
		LeaveCriticalSection(&PlrInputCritSec);
		LeaveCriticalSection(&CardRoomCritSec);
	}

	// If we've got room, accept new connections...
	WORD32 start_ticks = GetTickCount();
	#define TIME_ACCEPT_THREAD	0
  #if TIME_ACCEPT_THREAD	// 2022 kriskoin
	WORD32 now = start_ticks;
	WORD32 prev_ms = start_ticks;
	WORD32 accept_ms = 0;
	WORD32 logging_ms = 0;
  #endif
	int new_connections = 0;
	int got_a_new_socket;
	do {
		got_a_new_socket = FALSE;
		if (connected_players+new_connections < max_connected_players &&
			connected_players+new_connections < MaxAcceptedConnections) {
			// We've got room for another connection...
			// Make sure our listen_socket is open.
			if (sl->listen_socket==INVALID_SOCKET) {
			  #if INCL_SSL_SUPPORT
				ErrorType err = sl->OpenListenSocket((short)port_number, ssl_ctx);
			  #else

				ErrorType err = sl->OpenListenSocket((short)port_number);
			  #endif
				if (err) {
					//!!!!24/01/01 kriskoin:
					//!!!! I don't have time to deal with this right now, but it should be done
					// at some point in the near future.
					Error(ERR_FATAL_ERROR, "%s(%d) OpenListenSocket(%d) failed.",_FL,port_number);
					exit(ERR_FATAL_ERROR);
				}
			}

			// check if there are any new connections to accept
			ServerSocket *ss;
		  #if TIME_ACCEPT_THREAD
		  	prev_ms = GetTickCount();
		  #endif
			//kriskoin: 			// takes about 60ms per call on a 700MHz P3 Xeon. I'm not sure if
			// the connection latency makes that worse or not (i.e. whether the
			// call is blocking or not).
			ErrorType err = sl->AcceptConnection(&ss);
		  #if TIME_ACCEPT_THREAD
			now = GetTickCount();
			accept_ms += now - prev_ms;
			prev_ms = now;
		  #endif

			if (err==ERR_NONE && ss) {


				// A new connection has been accepted.  Create a Player
				// object to take care of it.
				char str[20];
				IP_ConvertIPtoString(ss->connection_address.sin_addr.s_addr, str, 20);
				// port: ntohs(ss->connection_address.sin_port)
			  #if 0	// 2022 kriskoin
				kp(("%s %-15.15s New connection. socket $%04lx\n",
						TimeStr(), str, ss->sock));
			  #endif
				ConnectionLog->Write(
						"%s %-15.15s New sock %04lx\n",
						TimeStr(), str, ss->sock);

				ss->InitialConnectionTimeout = InitialConnectionTimeout;	// copy .INI file setting to Socket

			  #if 0	// 2022 kriskoin
				kp(("%s(%d) BEFORE 'new Player()'\n",_FL));
				MemDisplayMemoryMap();
				Player *new_player = new Player((void *)this);
				kp(("%s(%d) AFTER  'new Player()'\n",_FL));
				MemDisplayMemoryMap();
			  #else 
				Player *new_player = new Player((void *)this);
			  #endif
				if (new_player) {
					err = new_player->SetServerSocket(&ss);
					if (err) {
						Error(ERR_ERROR, "%s(%d) new Player structure wouldn't accept ServerSocket.",_FL);
					} else {
						// Always send a version packet as the first thing.
						// Make a copy of the version info structure and fill in
						// the caller's IP address (big brother is watching :)
						EnterCriticalSection(&ParmFileCritSec);
						struct VersionInfo vi = ServerVersionInfo;
						LeaveCriticalSection(&ParmFileCritSec);
						
						kp(("%s(%d) serial  number %d\n", _FL, new_player->client_platform.computer_serial_num));
						vi.source_ip = new_player->server_socket->connection_address.sin_addr.s_addr;
						vi.server_time = time(NULL);
						new_player->SendDataStructure(DATATYPE_SERVER_VERSION_INFO, &vi, sizeof(vi), TRUE, FALSE);

						new_players[new_connections++] = new_player;
						got_a_new_socket = TRUE;
						new_player = NULL;
					}
				} else {
					Error(ERR_ERROR, "%s(%d) Could not create new Player for a new connection.",_FL);
				}

				// If any resource pointers fell through due to errors, delete them.
				if (new_player) {
					DeletePlayerObject(&new_player);
				}
				if (ss) {
					delete ss;
					ss = NULL;
				}

				SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
			  #if TIME_ACCEPT_THREAD
				now = GetTickCount();
				logging_ms += now - prev_ms;
				prev_ms = now;
			  #endif
			} else if (err) {
				// An error was encountered trying to accept a connection
				// We're in non-blocking mode, so probably nothing is there to accept.
				//Error(ERR_ERROR, "%s(%d) An error was encountered while accepting a connection",_FL);
			}
		} else {
			// No room for more connections... close the listen socket.
			if (sl->listen_socket!=INVALID_SOCKET) {
				kp(("%s(%d) Warning... closing listen_socket because too many connections have been made (%d)\n",_FL,connected_players));
				sl->CloseListenSocket();
			}
		}
	} while (got_a_new_socket && new_connections < MAX_CONNECTIONS_ACCEPTED_PER_PASS);

	// Add any new ones we got to the cardroom arrays.
	// Note: waiting on these critsecs can take a long time if the card room is busy.
	if (new_connections) {
	  #if TIME_ACCEPT_THREAD
		kp(("%s(%d) Adding %d new connections during this accept pass (times = %4dms, %4dms).\n",
				_FL, new_connections, accept_ms, logging_ms));
	  #endif
		EnterCriticalSection(&CardRoomCritSec);
		EnterCriticalSection(&PlrInputCritSec);

		for (int i=0 ; i<new_connections ; i++) {
		  #if USE_SELECT_FOR_INPUT
		  	// add it to our read set right away
			max_fd = max(max_fd, (int)new_players[i]->server_socket->sock);
			#pragma warning(disable : 4127)
			FD_SET(new_players[i]->server_socket->sock, &readset);
			#pragma warning(default : 4127)
		  #endif // USE_SELECT_FOR_INPUT

			players[connected_players++] = new_players[i];
		}

		LeaveCriticalSection(&PlrInputCritSec);
		LeaveCriticalSection(&CardRoomCritSec);

		// update the average time we spent dealing with this new connection.
		WORD32 elapsed = GetTickCount() - start_ticks;
		#define ACCEPT_THREAD_OLD_TIME_WEIGHTING	(.985)


		accept_avg_ms = (accept_avg_ms * ACCEPT_THREAD_OLD_TIME_WEIGHTING) +
						(double)elapsed * (1.0-ACCEPT_THREAD_OLD_TIME_WEIGHTING);
	}

	return ERR_NONE;	// nothing happened that we didn't already report.
}

//*********************************************************
// https://github.com/kriskoin//
// Process the player I/O for one player.
// Internal to the player input thread.
//
void CardRoom::ProcessPlayerDataInput(Player *p)
{
	// Read all pending input from a player
	EnterCriticalSection(&(p->PlayerCritSec));
	if (p->server_socket && p->server_socket->sock!=INVALID_SOCKET) {
		// Read any raw data into unencrypted, unpacked packets.
	  #if WIN32
		unsigned int socket = p->server_socket->sock;
	  #else
		int socket = p->server_socket->sock;
	  #endif
		p->server_socket->ReadDataToQueue();

		if (p->server_socket) {	// we still have a socket?
		  #if 0	// 2022 kriskoin
			// do this temporarily to assist in debugging
			kp1(("%s(%d) Warning: processing send queue from input thread!\n", _FL));

			p->server_socket->ProcessSendQueue();
		  #endif

			total_bytes_sent += p->server_socket->total_bytes_sent_resettable;
			p->server_socket->total_bytes_sent_resettable = 0;
			total_bytes_received += p->server_socket->total_bytes_received_resettable;
			p->server_socket->total_bytes_received_resettable = 0;

		  #if USE_SELECT_FOR_INPUT
			if (p->server_socket->disconnected && socket != INVALID_SOCKET) {
				// No longer connected... remove their socket descriptor
				// from the read set
				//kp(("%s(%d) Found disconnect, removing socket %d from read set.\n", _FL, socket));
				#pragma warning(disable : 4127)
				FD_CLR(socket, &readset);
				#pragma warning(default : 4127)
			}
		  #endif
		}
	}

	// Process incoming packets from queue
	p->ReadIncomingPackets();	// read incoming packets from queue and parse them
	LeaveCriticalSection(&(p->PlayerCritSec));
}	

//****************************************************************
// 
//
// Entry point for the Player I/O thread.
//
void CardRoom::PlayerIOThreadEntry(void)
{
	pr(("%s(%d) Player I/O thread is now active.\n",_FL));
  #if DISP_BANDWIDTH	// 2022 kriskoin
	#define IO_SUMMARY_PRINT_INTERVAL	60
	WORD32 next_summary_print = SecondCounter + IO_SUMMARY_PRINT_INTERVAL;
  #endif

	int loops_left = 3;
  #if USE_SELECT_FOR_INPUT
	WORD32 last_readset_build = 0;
	WORD32 last_select_call = 0;
	WORD32 last_full_pass = 0;
  #endif
	while (loops_left-- > 0) {
	  #if INCLUDE_FUNCTION_TIMING	// 2022 kriskoin
		kp1((ANSI_BLACK_ON_YELLOW"%s(%d) **** PlayerIO thread timing is enabled!  Don't leave this on normally!\n",_FL));
		WORD32 start_ticks = GetTickCount();
		int crit_sec_grab_count = 0;
	  #endif
		int i;

	  #if USE_SELECT_FOR_INPUT
	  	// Re-build the list of handles every once in a while (not constantly)
		// We do this to make sure that anything which has closed in the
		// background gets removed from the handle list.
		WORD32 now = GetTickCount();
		if (now - last_readset_build >= 10000) {
			// Time to do it again...
			//kp(("%s(%d) Rebuilding handle list. elapsed = %dms\n", _FL, now - last_readset_build));
			EnterCriticalSection(&PlrInputCritSec);
			max_fd = INVALID_SOCKET;
			FD_ZERO(&readset);		// zero the old array out
			if (players) {
				for (i=0 ; i<connected_players ; i++) {
					Player *p = (Player *)players[i];
					if (p &&
						p->server_socket &&
						p->server_socket->sock != INVALID_SOCKET &&
						p->server_socket->connected_flag &&
					   !p->server_socket->disconnected)
					{
						//kp(("%s(%d) Adding socket %d to the set.\n", _FL, p->server_socket->sock));
						max_fd = max(max_fd, (int)p->server_socket->sock);
						#pragma warning(disable : 4127)
						FD_SET(p->server_socket->sock, &readset);
						#pragma warning(default : 4127)
					}
				}
			}
			now = GetTickCount();
			last_readset_build = now;
			LeaveCriticalSection(&PlrInputCritSec);
		}

		// If we call select() too often, we only get back one or two file
		// descriptors with data and we end up doing lots of extra work
		// in overhead.  If we impose a minimum, we get a bunch at a time
		// and the processing work is a lot more efficient.
		#define MINIMUM_SELECT_SPACING	32	// call select() at most every n ms
		WORD32 elapsed = now - last_select_call;

		//kriskoin: 		// to give the other threads a chance to grab the PlrInputCritSec.  If we
		// loop constantly just because there's work to do, they'll never get a
		// chance to grab it and will starve.  This min delay prevents starvation.
		int sleep_amount = MINIMUM_SELECT_SPACING - elapsed;
		sleep_amount = max(10, sleep_amount);	// always sleep at least n ms.
		Sleep(sleep_amount);
		now = GetTickCount();
		last_select_call = now;

		struct timeval tv;
		zstruct(tv);
		tv.tv_usec = 125000;	// 125ms timeout
		fd_set tempset = readset;
		int result;
		if (connected_players) {
			result = select(max_fd+1, &tempset, NULL, NULL, &tv);
		} else {
			Sleep(20);
			result = 0;
		}
		WORD32 input_loop_start_ticks = GetTickCount();
		if (result > 0) {
			// Someone has data available right now...
			//kp(("%s(%d) someone has data available now!  Highest handle = %d\n", _FL, result));

			// Loop through everyone again and process anyone who's
			// descriptor is in the list...
			EnterCriticalSection(&PlrInputCritSec);
			now = GetTickCount();
			for (i=0 ; i<connected_players ; i++) {
				Player *p = (Player *)players[i];
				if (p && p->server_socket && p->server_socket->sock != INVALID_SOCKET) {
					if (!p->PlayerCritSec.owner_nest_count && FD_ISSET(p->server_socket->sock, &tempset)) {
						//kp(("%s(%d) Processing player $%08lx because data is available (%dms)\n", _FL, p->player_id, now - p->last_input_processing_ticks));
						ProcessPlayerDataInput(p);
						p->last_input_processing_ticks = now;
					}
				}
			}
			LeaveCriticalSection(&PlrInputCritSec);
		} else if (result == -1) {
			// linux error numbers are described in asm/errno.h
			// 	#define EINTR            4      /* Interrupted system call */
		  #if WIN32
			int err = WSAGetLastError();
			if (err==WSAENOTSOCK || err==WSAEINVAL)
		  #else
			int err = errno;
			if (err==EBADF)
		  #endif
			{	// bad file descriptor passed... force a rebuild.
				last_readset_build = 0;
			} else {
				kp(("%s(%d) select() returned error %d\n", _FL, err));
				Sleep(20);	// don't eat all cpu time.
			}
		}

		// Loop through everyone periodically and make sure they all get
		// processed at least every few seconds.  This takes care of anything
		// such as timeouts or other operations which are not triggered solely
		// by incoming data.
		now = GetTickCount();
		if (now - last_full_pass >= 2000) {
			// Time to do it again...
			//kp(("%s(%d) Doing a full pass. elapsed = %dms\n", _FL, now - last_full_pass));
			last_full_pass = now;
			EnterCriticalSection(&PlrInputCritSec);
			if (players) {
				WORD32 now = GetTickCount();
				for (i=0 ; i<connected_players ; i++) {
					if (players && i<connected_players && players[i]) {
						Player *p = (Player *)players[i];
						if (p) {
							WORD32 elapsed = now - p->last_input_processing_ticks;
							// Try to process this player.  Skip him if the critical section
							// is already owned by someone else.  Don't do that too often.
							if ((elapsed >= 2000 && !p->PlayerCritSec.owner_nest_count) ||
								(elapsed >= 6000))
							{
								//kp(("%s(%d) Processing input for player $%08lx (%dms since last)\n", _FL, p->player_id, elapsed));
								ProcessPlayerDataInput(p);
								p->last_input_processing_ticks = now;
							}
						}
					}
				}
			}
			LeaveCriticalSection(&PlrInputCritSec);
		}
	  #else	// non select() version... (for NT)
		WORD32 start_ticks = GetTickCount();
		int got_crit_sec = FALSE;
		int crit_sec_grab_count = 0;
		for (i=0 ; i<connected_players ; i++) {
			if (!got_crit_sec) {
				EnterCriticalSection(&PlrInputCritSec);

				got_crit_sec = TRUE;
				crit_sec_grab_count++;
			}
			if (players && i<connected_players && players[i]) {
				Player *p = (Player *)players[i];
				// Try to process this player.  Skip him if the critical section
				// is already owned by someone else.  Don't do that too often.
				if (p->last_input_processing_ticks - start_ticks >= 2000 || !p->PlayerCritSec.owner_nest_count)
				{
					ProcessPlayerDataInput(p);
					p->last_input_processing_ticks = start_ticks;
				}
			}
			// only release the PlrInput crit sec occassionally.  This is to prevent
			// constant grab/release sequences.  We don't want to hold it for too long,
			// but likewise we don't want to hold it for too short.  Every n players
			// seems like a good compromise.
			if (!(i%64)) {
				LeaveCriticalSection(&PlrInputCritSec);
				got_crit_sec = FALSE;
			}
		}
		if (got_crit_sec) {		// release it if we still have it.
			LeaveCriticalSection(&PlrInputCritSec);
		}
	  #endif	// non select() version... (for NT)

	  #if DISP_BANDWIDTH	// 2022 kriskoin
		if (SecondCounter > next_summary_print) {
			next_summary_print = SecondCounter + IO_SUMMARY_PRINT_INTERVAL;
			int total_games_played = max(1, next_game_serial_number - 1);
			int players_times_games = max(1, connected_players*total_games_played);
			static int old_packets_sent;
			static WORD32 old_game_serial_number;
			int packet_diff = 0;
			int player_0_bytes_sent = 0;
			if (players[0] && players[0]->server_socket) {
				packet_diff = players[0]->server_socket->total_packets_sent - old_packets_sent;
				old_packets_sent = players[0]->server_socket->total_packets_sent;
				player_0_bytes_sent = players[0]->server_socket->total_bytes_sent;
			}
			int game_diff = next_game_serial_number - old_game_serial_number;
			old_game_serial_number = next_game_serial_number;
			kp1(("%s(%d) Note: bandwidth calculations will be incorrect if there are any computer players or more than one table.\n",_FL));
			kp(("%s(%d) I/O summary: rcvd = %4luK  sent = %4luK (%lu bytes per player per game after %d games, %d plrs)\n",
						_FL, total_bytes_received>>10, total_bytes_sent>>10,
						total_bytes_sent/players_times_games, total_games_played,
						connected_players));
			kp(("%s(%d) Plr0: total: %d packets for %d games (%d packets/game), recent: %d packets for %d games (%d packets/game)\n",
						_FL, old_packets_sent, total_games_played, old_packets_sent / total_games_played,
						packet_diff, game_diff, packet_diff/max(1,game_diff)));
			// Make some assumptions and summarize
			//	30 hands per hour
			//	40 bytes of TCP/IP overhead per packet
			#define ASSUMED_HANDS_PER_HOUR	50
			#define ASSUMED_TCPIP_OVERHEAD	40
			int total_bytes = old_packets_sent * ASSUMED_TCPIP_OVERHEAD + player_0_bytes_sent;
			kp(("%s(%d) At %d hands/hr and %d bytes of TCP/IP overhead, this works out to %d bytes/sec for each playing player\n",
					_FL, ASSUMED_HANDS_PER_HOUR, ASSUMED_TCPIP_OVERHEAD,
					(ASSUMED_HANDS_PER_HOUR * total_bytes) / (total_games_played * 3600)));
		}
	  #endif
	  #if INCLUDE_FUNCTION_TIMING	// 2022 kriskoin
		elapsed = GetTickCount() - start_ticks;
		if (elapsed > 250) {
			kp(("%s %s(%d) Warning: Player IO thread took %5dms for a single loop (grabbed crit sec %d times).\n", TimeStr(), _FL, elapsed, crit_sec_grab_count));
		}
	  #endif
		// update the average time we spent dealing with this iteration
		elapsed = GetTickCount() - input_loop_start_ticks;
		#define INPUTLOOP_OLD_TIME_WEIGHTING	(.995)
		inputloop_avg_ms = (inputloop_avg_ms * INPUTLOOP_OLD_TIME_WEIGHTING) +
						  (double)elapsed * (1.0-INPUTLOOP_OLD_TIME_WEIGHTING);

	#if !USE_SELECT_FOR_INPUT	// delay if not using select()
	  #if 0	// 2022 kriskoin
		kp1(("*** WARNING: LONG DELAYS IN PLAYER INPUT THREAD!\n", _FL));
		Sleep(100);
	  #else
		Sleep(10);
	  #endif
	#endif
		if (!comm_thread_shutdown_flag) {
			loops_left = 3;	// loop at least a few more times.
		}		
	}
	pr(("%s(%d) Communications thread is now exiting.\n", _FL));
	comm_thread_finished_flag = TRUE;	// signal that we've exited.
}

//*********************************************************
// https://github.com/kriskoin//
// Any time a player object is going to be deleted, it MUST
// be done using this function so that anyone who relies on
// player pointers has a chance to be told it is going to be
// deleted and can no longer use it.
// Sets src ptr to null from inside the appropriate critical sections.
//
void CardRoom::DeletePlayerObject(class Player **plr)
{
	EnterCriticalSection(&CardRoomCritSec);
	EnterCriticalSection(&PlrInputCritSec);
	PlrOut_RemovePacketsForPlayer(*plr);
	delete *plr;
	*plr = NULL;
	LeaveCriticalSection(&PlrInputCritSec);
	LeaveCriticalSection(&CardRoomCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Find a connected Player object ptr given a player's ID.
// Note: the socket does not need to be currently connected but
// the player object does need to be in memory.
//
Player * CardRoom::FindPlayer(WORD32 player_id)
{
	// Lock either the cardroom or the player input crit sec
	// depending on which thread we were called from.
	int cardroom = (getpid() == cardroom_threadid);
	//kp(("%s(%d) cardroom_threadid = %d, getpid = %d, cardroom = %d\n", _FL, cardroom_threadid, getpid(), cardroom));
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	for (int i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (p && p->player_id == player_id) {
			// found him.
			if (cardroom) {
				LeaveCriticalSection(&CardRoomCritSec);
			} else {
				LeaveCriticalSection(&PlrInputCritSec);
			}
			return p;
		}
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);
	}
	return NULL;
}

//*********************************************************
// https://github.com/kriskoin//
// Test if a player is connected given just their player_id
//
int CardRoom::TestIfPlayerConnected(WORD32 player_id)

{
	// Lock either the cardroom or the player input crit sec
	// depending on which thread we were called from.
	int cardroom = (getpid() == cardroom_threadid);
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	int connected = FALSE;
	Player *p = FindPlayer(player_id);
	if (p && p->Connected()) {
		connected = TRUE;
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);
	}
	pr(("%s(%d) Player $%08lx connected = %d\n", _FL, player_id, connected));
	return connected;	
}

//*********************************************************
// https://github.com/kriskoin//
// Test if a player is being called to a table.  Return
// table serial number if they are.
//
WORD32 CardRoom::TestIfPlayerCalledToTable(WORD32 player_id)
{
	// Lock either the cardroom or the player input crit sec
	// depending on which thread we were called from.
	int cardroom = (getpid() == cardroom_threadid);
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	WORD32 table_serial_number = 0;
	Player *p = FindPlayer(player_id);
	if (p && p->Connected()) {
		// They're only being called if the timeout is non-zero.
		if (p->saved_seat_avail.timeout) {
			table_serial_number = p->saved_seat_avail.table_serial_number;
		}
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);

	}
	return table_serial_number;
}

//*********************************************************
// https://github.com/kriskoin//
// Determine which tournament table (if any) a player is seated at.
//
WORD32 CardRoom::GetPlayerTournamentTable(WORD32 player_id)
{
	// Lock either the cardroom or the player input crit sec
	// depending on which thread we were called from.
	int cardroom = (getpid() == cardroom_threadid);
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	WORD32 table_serial_number = 0;
	Player *p = FindPlayer(player_id);
	if (p) {
		table_serial_number = p->tournament_table_serial_number;
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);
	}
	return table_serial_number;
}

//*********************************************************
// https://github.com/kriskoin//
// Return our best guess about the quality of the connection to
// a player given just their 32-bit player_id.
// Returns CONNECTION_STATE_* (see player.h)
//
int CardRoom::GetPlayerConnectionState(WORD32 player_id)
{
	// Lock either the cardroom or the player input crit sec
	// depending on which thread we were called from.
	int cardroom = (getpid() == cardroom_threadid);
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	int result = CONNECTION_STATE_LOST;
	Player *p = FindPlayer(player_id);
	if (p) {
		result = p->CurrentConnectionState();
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);
	}
	pr(("%s(%d) Player $%08lx connection state = %d\n", _FL, player_id, result));
	return result;
}

//*********************************************************
// https://github.com/kriskoin//
// If a player is connected, re-send their client info to them.
// (usually because something changed).
// The client info INCLUDES their account record.

// If they're not connected, don't do anything.
//
void CardRoom::SendClientInfo(WORD32 player_id)
{
	int cardroom = (getpid() == cardroom_threadid);
	if (cardroom) {
		EnterCriticalSection(&CardRoomCritSec);
	} else {
		EnterCriticalSection(&PlrInputCritSec);
	}
	Player *p = FindPlayer(player_id);
	if (p) {
		p->send_client_info = TRUE;
	}
	if (cardroom) {
		LeaveCriticalSection(&CardRoomCritSec);
	} else {
		LeaveCriticalSection(&PlrInputCritSec);
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Get a table ptr from a table serial number
// Returns NULL if not found.
// You MUST have a lock on the cardroom before calling this function.
// It should ONLY be called from the cardroom thread.  NOWHERE ELSE!
//
Table * CardRoom::TableSerialNumToTablePtr(WORD32 serial_num)
{
	if (getpid() != cardroom_threadid) {
		kp(("%s(%d) Warning: TableSerialNumToTablePtr() called from %s\n", _FL, GetThreadName()));
	}
	Table *t = NULL;
	//EnterCriticalSection(&TableListCritSec);
	struct BinaryTreeNode *n = table_tree.FindNode(serial_num);
	if (n) {
		t = (Table *)n->object_ptr;
	}
	//LeaveCriticalSection(&TableListCritSec);
	return t;
}

//****************************************************************
// https://github.com/kriskoin//
// Recursively walk through a tree of tables and assign each one
// of the right type a sequential index (for the table_summary_list[]'s).
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
//
void CardRoom::CountAndIndexTables(ClientDisplayTabIndex client_display_tab_index, int *counter)
{
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];
		if (t->client_display_tab_index == client_display_tab_index) {	// if this is the right game type...
			t->summary_index = (*counter)++;
		}
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Recursively walk through a tree of tables and for this game type,
// update each one's game summary info, regardless of whether it has
// changed recently.  This is used when rebuilding the entire table.
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
//
void CardRoom::UpdateTableSummaryInfo(ClientDisplayTabIndex client_display_tab_index)
{
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];

		if (t->client_display_tab_index == client_display_tab_index) {	// if this is the right game type...
			// Now copy the data from the table into our array.
			if (t->summary_index < 0) {
				Error(ERR_INTERNAL_ERROR, "%s(%d) table's summary_index = %d", _FL, t->summary_index);
			} else {
				table_summary_lists[client_display_tab_index]->tables[t->summary_index] = t->summary_info;
			}
		}
	}
}

//****************************************************************
// 
//
// Rebuild all the table summaries for a particular game type.
//
void CardRoom::RebuildTableSummaries(ClientDisplayTabIndex client_display_tab_index)
{
	//EnterCriticalSection(&TableListCritSec);
	//!!! this could use improving... we re-alloc() this structure
	// _Every_ time there is a new table.  It would be better to
	// only re-alloc when it needs to get bigger.
	if (table_summary_lists[client_display_tab_index]) {
		free(table_summary_lists[client_display_tab_index]);
		table_summary_lists[client_display_tab_index] = NULL;
	}

	// First, walk through the table tree and count the number of
	// games of this type.  While we're doing that, assign those
	// index numbers to the tables so we can trace backwards.
	int count = 0;
	CountAndIndexTables(client_display_tab_index, &count);

	// Allocate a new one.
	int len = sizeof(struct CardRoom_TableSummaryList) +
			  sizeof(struct CardRoom_TableSummaryInfo) * (count-1);
	table_summary_lists[client_display_tab_index] = (struct CardRoom_TableSummaryList *)malloc(len);
	if (!table_summary_lists[client_display_tab_index]) {
		Error(ERR_ERROR, "%s(%d) Could not malloc(%d) for TableSummaryList",_FL,len);
	} else {
		memset(table_summary_lists[client_display_tab_index], 0, len);	// always set to zero.
		table_summary_lists[client_display_tab_index]->table_count = count;
		table_summary_lists[client_display_tab_index]->length = len;
		table_summary_lists[client_display_tab_index]->client_display_tab_index = (BYTE8)client_display_tab_index;

		// Loop through all entries and fill them in (make them as up
		// to date as possible).
		UpdateTableWaitListCounts(client_display_tab_index);
		UpdateTableSummaryInfo(client_display_tab_index);
	}
	summary_info_changed[client_display_tab_index] = TRUE;	// this _definitely_ warrants considering it changed.
	int work_was_done_flag = FALSE;			// we don't actually care about this result.
	summary_serial_nums.serial_num[client_display_tab_index]++;	// change serial number
	next_summary_data_check_time = 0;		// make sure the summary data gets updated THIS call
	UpdateSummaryData(&work_was_done_flag);	// update all the shot clock info for the summary lists

	//LeaveCriticalSection(&TableListCritSec);
}

/**********************************************************************************
 void CardRoom::ValidateTableNames(void)
 Date: 20180707 kriskoin :  Purpose: make sure that all table names are unique
***********************************************************************************/
ErrorType CardRoom::ValidateTableNames(void)
{
	int test_table_index = -1;	// the table we're currently checking
	int found_problem = FALSE;
	forever {
		test_table_index++;
		if (!TableNames[test_table_index].name) {
			break;	// done sifting
		}
		if (strlen(TableNames[test_table_index].name) >= MAX_TABLE_NAME_LEN) {
			Error(ERR_FATAL_ERROR, "%s(%d) Table name '%s' is too long -- must be fixed",
					_FL, TableNames[test_table_index]);
			found_problem = TRUE;

		}
		int sift_table_index = test_table_index+1;
		while (TableNames[sift_table_index].name) {
			if (!stricmp(TableNames[sift_table_index].name, TableNames[test_table_index].name)) {
				Error(ERR_FATAL_ERROR, "%s(%d) Found duplicate table name '%s' -- must be fixed",
						_FL, TableNames[sift_table_index]);
				found_problem = TRUE;
			}
			sift_table_index++;
		}
	}
	
	if (found_problem) {
                  kp(("%s(%d) Found %d unique table names\n", _FL, test_table_index)); 
		return ERR_FATAL_ERROR;	// must be fixed before proceeding
	} 
	kp(("%s(%d) Found %d unique table names\n", _FL, test_table_index));
	return ERR_NONE;
}

//****************************************************************
// https://github.com/kriskoin//
// Recursively walk through a tree of tables and add each Table * to
// our tables[] array.
// CardRoomCritSec must be held before calling this function.
//
void CardRoom::AddTreeToTableArray(struct BinaryTreeNode *tree)
{
	// Process tables in order.  Do this by walking left first,
	// then do our node, then walk down the right.
	if (tree->left) {
		AddTreeToTableArray(tree->left);
	}

	// Do the work for this node.
	Table *t = (Table *)tree->object_ptr;
	tables[table_count++] = t;

	// Walk down the right side.
	if (tree->right) {
		AddTreeToTableArray(tree->right);
	}
}

/**********************************************************************************
 Function IsGameStillActive
 date: 24/01/01 kriskoin Purpose: return T/F if a game serial number is still being played
***********************************************************************************/
int CardRoom::IsGameStillActive(WORD32 game_serial_number)
{
	int found = FALSE;
	WORD32 new_lowest_active_game_number = 0;
	EnterCriticalSection(&CardRoomCritSec);
	//EnterCriticalSection(&TableListCritSec);
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];
		if (!t || !t->game) {	// no active game at the table
			continue;
		}
		WORD32 gsn = t->GameCommonData.game_serial_number;

		if (!new_lowest_active_game_number) {	// first time
			new_lowest_active_game_number = gsn;
		}
		if (gsn == game_serial_number) {
			found = TRUE;
		}
		new_lowest_active_game_number = (WORD32)(min(new_lowest_active_game_number, gsn));
		// loop the entire array so we catch the lowest number
	}
	lowest_active_game_number = new_lowest_active_game_number;
	//LeaveCriticalSection(&TableListCritSec);
	LeaveCriticalSection(&CardRoomCritSec);
	return found;
}

//****************************************************************
// https://github.com/kriskoin//
// Rebuild our tables[] array.
//
void CardRoom::RebuildTableArray(void)
{
	if (tables) {
		free(tables);
		tables = NULL;
	}
	table_count = 0;
	int count = table_tree.CountNodes();	// count total tables.
	if (count) {
		tables = (Table **)malloc(count * sizeof(tables[0]));
		if (!tables) {
			Error(ERR_FATAL_ERROR, "%s(%d) could not malloc space for %d Table ptrs.", _FL, table_count);
			DIE("Memory problem");
		}
	}
	if (table_tree.tree_root) {
		AddTreeToTableArray(table_tree.tree_root);
		if (table_count != count) {
			Error(ERR_INTERNAL_ERROR, "%s(%d) table_count does not match # of tables in tree (%d vs %d)", _FL, table_count, count);
		}
	}
}

//****************************************************************
// 
//
// Add a new table to our card room.
//
ErrorType CardRoom::AddNewTable(
				ClientDisplayTabIndex client_display_tab_index,
				GameRules game_rules,
				int max_number_of_players,
				int small_blind_amount, int big_blind_amount, char *table_name,
				int add_computer_players_flag, ChipType chip_type,
				int game_disable_bits, RakeType rake_profile)
{
	WORD32 table_serial_number = next_table_serial_number++;
	char temp_name[MAX_TABLE_NAME_LEN];

	switch (chip_type) {
	case CT_NONE:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
		break;
	case CT_PLAY:
		iPlayMoneyTableCount++;	// one more play money table is active
		break;
	case CT_REAL:
		iRealMoneyTableCount++;	// one more real money table is active
		break;
	case CT_TOURNAMENT:
		iTournamentTableCount++;	// one more tournament table is active
		break;
	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}

	// Pick a name for this table (if we weren't handed one)
	char *name = table_name;
	if (!name && chip_type == CT_PLAY) {
		// Search for an unused play money table number
		int play_table_name_index = 1;	// start at table 1
		int found_dup;
		do {

			sprintf(temp_name, "Play Money%2d", play_table_name_index);
			// Search for it...
			found_dup = FALSE;
			//EnterCriticalSection(&CardRoomCritSec);
			for (int i=0 ; i<table_count ; i++) {
				if (!stricmp(tables[i]->summary_info.table_name, temp_name)) {
					play_table_name_index++;	// pick a new name
					found_dup = TRUE;
				}
			}
			//LeaveCriticalSection(&CardRoomCritSec);
		} while (found_dup);
		name = temp_name;
	} else if (!name && (chip_type == CT_REAL || chip_type == CT_TOURNAMENT)) {
		static int table_name_count;
		if (!table_name_count) {
			// First time... count the # of table names we have.
			while (TableNames[table_name_count].name) {
				table_name_count++;
			}
		}
		// Choose a default name (the serial number)
		sprintf(temp_name, "Real  %d", table_serial_number);	// default name
		name = temp_name;

		// Now pick a name randomly from our list of table names
	  #if 0	// 2022 kriskoin
		int index = random(table_name_count);	// random starting position
	  #else
		int index = 0;	// go through list in order.
	  #endif
		int original_index = index;
		while (TableNames[index].table_serial_number) {
			index++;	// skip forward.
			if (!TableNames[index].name) {	// last entry?
				index = 0;	// wrap to the beginning.
			}
			if (index==original_index) {
				// They're all used.  Stop looking.
				break;
			}
		}
		if (!TableNames[index].table_serial_number && TableNames[index].name) {
			// Found a name... use it.
			TableNames[index].table_serial_number = table_serial_number;
			name = TableNames[index].name;
		}

		//kriskoin: 		int used = 0;
		int avail = 0;
		while (TableNames[avail].name) {
			if (TableNames[avail].table_serial_number) {
				used++;
			}
			avail++;
		}
		if (SecondCounter >= 60) {
			pr(("%s(%d) Real money table names: %3d of %3d used. New table is %s\n",
						_FL, used, avail, name));
		}
		table_names_used = (WORD16)used;		// # of real money table names currently used

		table_names_avail = (WORD16)avail;		// # of real money table names available to be used (total)
	}
	Table *new_table = new Table(this, table_serial_number, name);
	if (!new_table) {
		Error(ERR_ERROR, "%s(%d) Unable to create a new Table.",_FL);
		return ERR_ERROR;
	}

	ErrorType err = new_table->SetGameType(
				client_display_tab_index, game_rules,
				max_number_of_players, small_blind_amount, big_blind_amount, 
				chip_type, game_disable_bits, rake_profile);
	if (err) {
		Error(ERR_ERROR, "%s(%d) Failed while trying to set parms for a new table.", _FL);
		delete new_table;
		return err;
	}

	if (max_number_of_players ==5) 
	{
	   strcat(new_table->summary_info.table_name, "(5 Max)");
	}

	//EnterCriticalSection(&CardRoomCritSec);
	//EnterCriticalSection(&TableListCritSec);

	// Table has been successfully created.  Add it to our table list.
	table_tree.AddNode(&(new_table->treenode));
	new_table = NULL;

	// Since we add tables infrequently, we may as well keep the tree
	// perfectly balanced by balancing it every time we add a table.
	table_tree.BalanceTree();

	// Rebuild our tables[] array each time.
	RebuildTableArray();

	// Update the size of our various CardRoom_TableSummaryList
	// structures.  If they are not big enough, free them and
	// allocate new ones.
	RebuildTableSummaries(client_display_tab_index);

	// Whenever a new table gets created, tell the cardroom to update waiting
	// lists for this game type.
	update_waiting_list[client_display_tab_index] = TRUE;

  #if 1
	//!!! TEMP: add computer players to tables.
	int computer_players = 0;
	switch (client_display_tab_index) {
    	case DISPLAY_TAB_HOLDEM:
    		computer_players = ComputerPlayersPerHoldemTable;
    		break;
    	case DISPLAY_TAB_OMAHA_HI:
    	case DISPLAY_TAB_OMAHA_HI_LO:
    		computer_players = ComputerPlayersPerOmahaTable;
    		break;
    	case DISPLAY_TAB_STUD7:
    	case DISPLAY_TAB_STUD7_HI_LO:
    		computer_players = ComputerPlayersPerSevenCSTable;
    		break;

    	case DISPLAY_TAB_ONE_ON_ONE:
    		computer_players = ComputerPlayersPerOneOnOneTable;
    		break;
    	case DISPLAY_TAB_TOURNAMENT:
    	  //#if HORATIO
        #if 0
    		computer_players = 0;
    	  #else
    		computer_players = ComputerPlayersPerHoldemTable;	// !! using Hold'em numbers
    	  #endif
    		break;
	}//switch
	computer_players = max(computer_players, ComputerPlayersPerTable);
	if (computer_players && add_computer_players_flag) {
		 //
		int seat_positions[10]={3,6,8,7,-1,1,2,-1,-1,-1};		  
		if (client_display_tab_index==DISPLAY_TAB_ONE_ON_ONE){
			/*seat_positions [1]=-1;
			seat_positions [2]=-1;
			seat_positions [3]=-1;
			seat_positions [4]=1;
			seat_positions [5]=-1;
			seat_positions [6]=2;
			seat_positions [7]=-1;
			seat_positions [8]=-1;
			seat_positions [9]=-1;*/
		}else{
		    //seat_positions =  {1,2,3,7,-1,4,5,-1,-1,-1};	
		};//if	
		
		if (ComputerPlayersPerTable) {
			if (DebugFilterLevel <= 0) {

				kp1(("%s(%d) Adding %d computer players to every table (change in .ini file)\n", _FL,ComputerPlayersPerTable));
			} //if
		}//if
		Table *t = TableSerialNumToTablePtr(table_serial_number);
   //ccv
  	kp1(("%s(%d) Cristiancito agrego %d computer players y el TAB : %d !", _FL,client_display_tab_index));
		for (int i=0 ; i<min(computer_players,t->max_number_of_players) ; i++) {
			t->AddComputerPlayer(seat_positions[i]);
			iTotalComputerPlayersAdded++;
			next_manage_tables_time = 0;	// go through the table list again asap.
		}//for
	}//if
  #endif

	//LeaveCriticalSection(&TableListCritSec);
	//LeaveCriticalSection(&CardRoomCritSec);
	return ERR_NONE;
}

//****************************************************************
// https://github.com/kriskoin//
// Delete a table from the cardroom.
//
void CardRoom::DeleteTable(WORD32 table_serial_number)
{
	//EnterCriticalSection(&CardRoomCritSec);
	//EnterCriticalSection(&TableListCritSec);
	Table *t = TableSerialNumToTablePtr(table_serial_number);
	if (!t) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) DeleteTable(%d)... table does not exist.", _FL, table_serial_number);
		//LeaveCriticalSection(&TableListCritSec);
		//LeaveCriticalSection(&CardRoomCritSec);
		return;
	}

	// First, remove it from the tree.
	ErrorType err = table_tree.RemoveNode(table_serial_number);
	if (err) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Error when deleting table.  RemoveNode returned %d", _FL, err);
		//LeaveCriticalSection(&TableListCritSec);
		//LeaveCriticalSection(&CardRoomCritSec);
		return;
	}

	switch (t->chip_type) {
	case CT_NONE:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
		break;
	case CT_PLAY:
		iPlayMoneyTableCount--;
		break;
	case CT_REAL:
		iRealMoneyTableCount--;
		break;
	case CT_TOURNAMENT:
		iTournamentTableCount--;
		break;
	default:
		Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}

	// It's out of the tree now... delete it.
	ClientDisplayTabIndex client_display_tab_index = t->client_display_tab_index;	// preserve for rebuilding (below).
	delete t;

  #if DISP
	kp(("%s(%d) Tree after deleting table %d (before balancing)\n",_FL, table_serial_number));
	table_tree.DisplayTree();
  #endif

	// Since we remove tables infrequently, we may as well keep the tree
	// perfectly balanced by balancing it every time we delete a table.
	table_tree.BalanceTree();

  #if DISP
	kp(("%s(%d) Tree after deleting table %d (after balancing)\n",_FL, table_serial_number));
	table_tree.DisplayTree();
  #endif
	// Rebuild our tables[] array each time.
	RebuildTableArray();

	RebuildTableSummaries(client_display_tab_index);

	// Free up the name of that table.
	struct TableNames *tn = TableNames;
	while (tn->name && tn->table_serial_number != table_serial_number) {
		tn++;
	}
	if (tn->table_serial_number==table_serial_number) {
		tn->table_serial_number = 0;
	}
	//LeaveCriticalSection(&TableListCritSec);
	//LeaveCriticalSection(&CardRoomCritSec);
}

//****************************************************************
// https://github.com/kriskoin//
// Loop through the list of tables and update each one.
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
// This function only does the work which requires low latency.
//
void CardRoom::UpdateTables_LowLatencyWork(int *work_was_done_flag)
{
	WORD32 now = GetTickCount();

	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];

		// Do the work for this table
		if (SecondCounter < t->time_of_next_update) {
			// Too soon; nothing to be done here.
			continue;
		}
	  #if 0	// 2022 kriskoin
		kp1(("%s(%d) *** DO NOT LEAVE THIS CODE ENABLED ON A LIVE SERVER ***\n", _FL));
		WORD32 pre_update_ticks = GetTickCount();
		t->UpdateTable(work_was_done_flag);
		WORD32 elapsed = GetTickCount() - pre_update_ticks;
		if (elapsed >= 100) {
			kp(("%s %s(%d) Warning: table %4d took %dms to update.\n",
						TimeStr(), _FL, t->table_serial_number, elapsed));
		}
	  #else
		t->UpdateTable(work_was_done_flag);
	  #endif
		total_tables_updated++;

		//kriskoin: 		// to the high latency function or not.  It doesn't really belong here
		// I don't think.
		kp1(("%s(%d) hk: Could bad beat processing be moved to the high latency function?\n", _FL));
		// Do any bad beat processing that may be needed
		if (t->bad_beat_game_number) {
			// For BadBeatJackpotActive_*
			// 0 = totally disabled
			// 1 = test mode where we look for them and send ourselves email (for our info)
			// 2 = look for them, announce them, and pay them off (used for when they're active)
			// 3 = same as '2' but set it to 1 after paying it out
			int display_badbeat_message = FALSE;
			switch (t->game_rules) {
			case GAME_RULES_HOLDEM:
				if (BadBeatJackpotActive_holdem >=2) {
					display_badbeat_message = TRUE;
					if (BadBeatJackpotActive_holdem == 3) {	// one-time payout
						BadBeatJackpotActive_holdem = 1;	// back to reporting mode
						kp(("%s(%d) Hold'em BadBeats have now been set to reporting mode only\n", _FL));
					}
				}
				break;			
			case GAME_RULES_OMAHA_HI:
			case GAME_RULES_OMAHA_HI_LO:
				if (BadBeatJackpotActive_omaha >=2) {
					display_badbeat_message = TRUE;
					if (BadBeatJackpotActive_omaha == 3) {	// one-time payout
						BadBeatJackpotActive_omaha = 1;	// back to reporting mode
						kp(("%s(%d) Omaha BadBeats have now been set to reporting mode only\n", _FL));
					}
				}
				break;			
			case GAME_RULES_STUD7:
			case GAME_RULES_STUD7_HI_LO:
				if (BadBeatJackpotActive_stud >=2) {
					display_badbeat_message = TRUE;
					if (BadBeatJackpotActive_stud == 3) {	// one-time payout
						BadBeatJackpotActive_stud = 1;	// back to reporting mode
						kp(("%s(%d) Stud-7 BadBeats have now been set to reporting mode only\n", _FL));
					}
				}
				break;
			default:
				kp(("%s(%d) Unknown game_rules (%d) -- see src\n", _FL, t->game_rules));
				break;
			}

			if (display_badbeat_message >=2) {
				char msg[150];
				zstruct(msg);
				char cs1[MAX_CURRENCY_STRING_LEN];
				char cs2[MAX_CURRENCY_STRING_LEN];
				zstruct(cs1);
				zstruct(cs2);
				sprintf(msg, "The %s Bad Beat Jackpot was just awarded for game #%s!",
						CurrencyString(cs1, t->bad_beat_payout, t->chip_type, FALSE),
						IntegerWithCommas(cs2, t->bad_beat_game_number));
				SendDealerMessageToAllTables(msg, CHATTEXT_ADMIN, 0);
			}
			ServerVars.bad_beat_count++;
			ServerVars.bad_beat_payout += t->bad_beat_payout;
			// reset them for next game
			t->bad_beat_game_number = 0;
			t->bad_beat_payout = 0;
			write_out_server_vars_now = TRUE;	// main cardroom loop will take care of it
		}
	}
	WORD32 elapsed = GetTickCount() - now;
	total_table_update_ms += elapsed;

	if (elapsed >= 2500) {
		kp(("%s %s(%d) Warning: UpdateTables_LowLatencyWork() took %.2fs to execute.\n", TimeStr(), _FL, elapsed/1000.0));
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Loop through the list of tables and update each one.
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
// This function only does the stuff which is not latency sensitive
// and in fact it assumes it's only called every 10s or possibly even
// less often.
//
void CardRoom::UpdateTables_HighLatencyWork(int *work_was_done_flag)
{
	double total_response_time_real_money = 0.0;
	double total_response_time_play_money = 0.0;
	double total_players_real_money = 0.0;	// suitable only for avg response time calculations!
	double total_players_play_money = 0.0;	// suitable only for avg response time calculations!
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];

		// If the summary info for that table changed, keep track of it.
		if (t->summary_info_changed) {
			if (t->summary_index < 0) {
				Error(ERR_INTERNAL_ERROR, "%s(%d) table's summary_index = %d", _FL, t->summary_index);
			} else {
				// Make sure the memory has actually changed before marking it
				// as needing to be sent out.
				if (memcmp(&table_summary_lists[t->client_display_tab_index]->tables[t->summary_index],
							&t->summary_info, sizeof(t->summary_info))) {
					// the memory is different... save new version.
					table_summary_lists[t->client_display_tab_index]->tables[t->summary_index] = t->summary_info;
					summary_info_changed[t->client_display_tab_index] = TRUE;
					SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
				}
			}// 2022 kriskoin
			t->summary_info_changed = FALSE;	// reset table's flag.
		}

		// Update any empty seat requests we're waiting for.
		struct WaitListCallInfo *wlci = t->wait_list_called_players;
		for (int i=0 ; i<t->wait_list_called_player_count ; i++, wlci++) {
			if (SecondCounter >= wlci->next_send_time) {
				// Send (another) SeatAvail packet to them.
				struct CardRoom_SeatAvail sa;
				zstruct(sa);
				sa.table_serial_number = t->table_serial_number;
				int timeout = max(0, (int)(wlci->request_time + WaitListTotalTimeout - SecondCounter));
				sa.timeout = (WORD16)timeout;
				// If timeout went to zero, the client will automatically remove
				// themselves from the request.  We handle that situation after
				// sending (a little below here).
				sa.number_of_players = t->summary_info.player_count;
				sa.potential_players = t->wait_list_potential_players;
				sa.skipped_players = wlci->skipped_players;

				//EnterCriticalSection(&CardRoomCritSec);
				Player *p = FindPlayer(wlci->player_id);
				if (p) {
					pr(("%s %s(%d) Sending SeatAvail request to player $%08lx for table %s (timeout=%ds)\n",
								TimeStr(), _FL, wlci->player_id, t->summary_info.table_name, sa.timeout));
					SendSummaryListToPlayer((ClientDisplayTabIndex)t->client_display_tab_index, p);
					p->SendSeatAvail(&sa);
				} else {
					kp(("%s %s(%d) Warning: player %06lx is being called to table %s but does not have a player object!\n", TimeStr(), _FL, wlci->player_id, t->summary_info.table_name));
					sa.timeout = 0;	//kriskoin: 				}
				//LeaveCriticalSection(&CardRoomCritSec);
				WORD32 timeout_time = wlci->request_time + WaitListTotalTimeout;
				if (timeout_time < SecondCounter) {
					timeout_time = SecondCounter;
				}
				wlci->next_send_time = SecondCounter + WaitListReminderInterval;
				if (wlci->next_send_time > timeout_time) {
					wlci->next_send_time = timeout_time;
				}
				pr(("%s(%d) sa.timeout = %d\n", _FL, sa.timeout));
				if (sa.timeout <= 0) {
					// This request timed out!  Handle that situation.
					// The client did not respond in time.  Take them off
					// the waiting list and make the spot available to
					// someone else.
					pr(("%s(%d) Player $%08lx did not respond to his SeatAvail request at table %s.  Removing from waitlist.\n",
								_FL, wlci->player_id, t->summary_info.table_name));
					struct WaitListEntry wle;
					zstruct(wle);
					wle.table_serial_number = t->table_serial_number;
					wle.desired_stakes = t->initial_big_blind_amount;
					wle.chip_type = t->chip_type;
					wle.game_rules = t->game_rules;
					wle.player_id = wlci->player_id;
					waiting_lists[t->client_display_tab_index].RemoveWaitListEntry(&wle);
					t->RemoveCalledPlayer(wlci->player_id);
					update_waiting_list[t->client_display_tab_index] = TRUE;	// force re-eval.
					SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
				}
			}
		}

		// Update the average response time variables.
		if (t->summary_info.player_count >= 5) {	// must have at least 5 players for us to care
			if (t->chip_type==CT_PLAY) {
				total_response_time_play_money += t->avg_response_time * t->summary_info.player_count;
				total_players_play_money += t->summary_info.player_count;
			} else {	// real and tournament tables are both considered 'real'.
				total_response_time_real_money += t->avg_response_time * t->summary_info.player_count;
				total_players_real_money += t->summary_info.player_count;
			}
		}
	}

	// Finish calculating the average response time for the entire cardroom.
	// Break the data up into real and // 2022 kriskoinplay money.
	if (total_players_real_money != 0.0) {
		avg_response_time_real_money = total_response_time_real_money / total_players_real_money;
	}
	if (total_players_play_money != 0.0) {
		avg_response_time_play_money = total_response_time_play_money / total_players_play_money;
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Update the waiting list for each table in the table array.
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
//
void CardRoom::UpdateTableWaitLists(ClientDisplayTabIndex client_display_tab_index)
{
	if (iShutdownAfterECashCompletedFlag || iShutdownAfterGamesCompletedFlag) {
		return;	// never call anyone while shutting down.
	}
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];

		int players_to_call = t->max_number_of_players -
							  t->summary_info.player_count -
							  t->wait_list_called_player_count;
		if (t->chip_type==CT_TOURNAMENT && t->table_tourn_state != TTS_WAITING) {
			players_to_call = 0;	// never call anyone to a tournament that has started.
		}

		//kriskoin: 		if (GameCloseBits & t->game_disable_bits) {
			players_to_call = 0;

			// Try to empty the list, one person at a time.
			struct WaitListEntry input_wle, output_wle;
			zstruct(input_wle);
			input_wle.table_serial_number = t->table_serial_number;
			input_wle.desired_stakes = t->initial_big_blind_amount;
			input_wle.chip_type = t->chip_type;
			input_wle.game_rules = t->game_rules;
			int skipped_players = 0;
			waiting_lists[t->client_display_tab_index].CheckFillSeat(&input_wle,
					t->max_number_of_players, &output_wle, &skipped_players);
			if (output_wle.player_id) {
				waiting_lists[t->client_display_tab_index].RemoveWaitListEntry(&output_wle);
			}
		}

		if (t->client_display_tab_index==client_display_tab_index && (t->summary_info.flags & TSIF_WAIT_LIST_REQUIRED)) {
			// If we're not already waiting for a response and there's
			// a seat at this table, try to call someone new.
			if (players_to_call > 0) {
				// There's a seat available (or more than one and not enough
				// players called to fill all the seats).
				// Look for someone to fill it.
			  #if 0	// 2022 kriskoin
				if (t->summary_info.player_count >= 2) {
					kp(("%s(%d) There's are %d seats available at table %s, %d being called. %d more could be called\n",
						_FL, t->max_number_of_players - t->summary_info.player_count,
						t->summary_info.table_name,
						t->wait_list_called_player_count,
						players_to_call));
				}
			  #endif

				struct WaitListEntry input_wle, output_wle;
				zstruct(input_wle);
				input_wle.table_serial_number = t->table_serial_number;
				input_wle.desired_stakes = t->initial_big_blind_amount;
				input_wle.chip_type = t->chip_type;
				input_wle.game_rules = t->game_rules;
				int skipped_players = 0;
				waiting_lists[t->client_display_tab_index].CheckFillSeat(&input_wle,
						t->summary_info.player_count, &output_wle, &skipped_players);
				if (output_wle.player_id) {
					// We found someone!
					pr(("%s(%d) Player $%08lx is a candidate to fill empty seat #%d at table %s\n",
								_FL, output_wle.player_id, players_to_call, t->summary_info.table_name));

					// Check if this player is already seated at the table (somehow).
					// If they are, remove them from the list asap.
					if (t->CheckIfPlayerSeated(output_wle.player_id)) {
						// player was already seated.  Don't add them
						pr(("%s(%d) Player $%08lx was already at table %s.  Ignoring new seat avail for him\n", _FL, output_wle.player_id, t->summary_info.table_name));
						waiting_lists[t->client_display_tab_index].RemoveWaitListEntry(&output_wle);
					} else {
						// Update the table vars which indicate we're waiting for someone.
						pr(("%s(%d) Player $%08lx was NOT at table %s... inviting him to join\n", _FL, output_wle.player_id, t->summary_info.table_name));
						struct WaitListCallInfo wlci;
						zstruct(wlci);
						wlci.player_id = output_wle.player_id;
						wlci.request_time = SecondCounter;
						wlci.next_send_time = 0;	// send ASAP.
						wlci.skipped_players = (BYTE8)min(255,skipped_players);
						t->wait_list_potential_players = 0;	// we don't want to tell them anything like this
						t->AddCalledPlayer(&wlci);
					}
					players_to_call = 0;	// don't call anyone else this time around
				} else {
					// There's an empty seat and nobody on the waiting
					// list to fill it.  This table no longer needs a waiting
					// list.
				  #if !FORCE_WAITING_LIST	// 2022 kriskoin
					pr(("%s(%d) The waiting list for table %s is now empty.  Clearing WAIT_LIST_REQUIRED.\n",_FL, t->summary_info.table_name));
					t->summary_info.flags   &= ~TSIF_WAIT_LIST_REQUIRED;
					t->summary_info_changed = TRUE;
					t->GameCommonData.flags &= ~GCDF_WAIT_LIST_REQUIRED;
					t->FlagResendGameCommonDataToEveryone();	// re-send whenever waiting list status changes
				  #endif
				}
			}
		}

		// If there are empty seats at this table, see if we can grab enough
		// players who are waiting for 'any' table and try to start a new table.
		if (t->client_display_tab_index==client_display_tab_index && players_to_call > 0) {
			// There are seats available at this table.
			// Look for enough other players to get this table started.
			struct WaitListEntry input_wle;
			zstruct(input_wle);
			input_wle.table_serial_number = t->table_serial_number;
			input_wle.desired_stakes = t->initial_big_blind_amount;
			input_wle.chip_type = t->chip_type;
			input_wle.game_rules = t->game_rules;
			int potential_waiting_players = waiting_lists[t->client_display_tab_index].GetWaitListPosition(&input_wle);
		  #if 0	// 2022 kriskoin
			if (potential_waiting_players) {
				kp(("%s(%d) Table %s ha// 2022 kriskoin.table_name,
						t->summary_info.player_count,
						t->wait_list_called_player_count,
						potential_waiting_players));
			}
		  #endif

			// Start by assuming all waiting players will play.  Do a count of
			// how many would join with that many total players.  In all
			// likelyhood, that won't be enough players for someone and so our
			// potential players diminishes.
			// Keep doing this until our waiting list players is the same as our
			// potential players.
			while (potential_waiting_players > 0) {
				int total_players = t->summary_info.player_count +
									t->wait_list_called_player_count +
									potential_waiting_players;
				int willing_players = waiting_lists[t->client_display_tab_index].CountPotentialPlayers(&input_wle, total_players);
				pr(("%s(%d) with %d total players, we have %d willing players.\n", _FL, total_players, willing_players));
				if (willing_players >= potential_waiting_players) {
					// We found enough willing players to start calling people
					// to this table.
					pr(("%s(%d) Looks like %d total players is enough to have %d join.\n",
								_FL, total_players, willing_players));
					// Given an estimate of the total number of players,
					// ask the first one in the list to join us.
					// Update the table vars which indicate we're waiting
					// for someone.
					struct WaitListEntry output_wle;
					int skipped_players = 0;
					waiting_lists[t->client_display_tab_index].CheckFillSeat(&input_wle,
							total_players, &output_wle, &skipped_players);
					if (output_wle.player_id) {
						// Check if this player is already seated at the table (somehow).
						// If they are, remove them from the list asap.
						if (t->CheckIfPlayerSeated(output_wle.player_id)) {
							// player was already seated.  Don't add them
							pr(("%s(%d) Player $%08lx was already at table %s.  Ignoring new seat avail for him\n", _FL, output_wle.player_id, t->summary_info.table_name));
							waiting_lists[t->client_display_tab_index].RemoveWaitListEntry(&output_wle);
						} else {
							pr(("%s(%d) Player $%08lx was NOT at table %s... inviting him to join\n", _FL, output_wle.player_id, t->summary_info.table_name));
							struct WaitListCallInfo wlci;
							zstruct(wlci);
							wlci.player_id = output_wle.player_id;
							wlci.request_time = SecondCounter;
							wlci.next_send_time = 0;	// send ASAP.
							wlci.skipped_players = (BYTE8)min(255,skipped_players);
							t->wait_list_potential_players = (BYTE8)willing_players;
							t->AddCalledPlayer(&wlci);
						}
					} else {
						Error(ERR_INTERNAL_ERROR, "%s(%d) Waiting list problem... we should have found a player.", _FL);
					}
					break;	// done looping.
				}
				potential_waiting_players--;
			}
		}


	  #if FORCE_WAITING_LIST	// 2022 kriskoin
		kp1(("%s(%d) Forcing all tables (even empty ones) to use waiting lists (for testing).\n",_FL));
		t->summary_info.flags   |= TSIF_WAIT_LIST_REQUIRED;
		t->GameCommonData.flags |= GCDF_WAIT_LIST_REQUIRED;
		t->summary_info_changed = TRUE;
	  #endif
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Update everything related to waiting lists (if necessary).
//
void CardRoom::UpdateWaitingLists(void)
{
	if (iShutdownAfterGamesCompletedFlag || !table_tree.tree_root)
		return;	// don't update if we're about to shut down.

	for (int i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
		if (update_waiting_list[i])  {
			// Loop through each table and for any free seats, try
			// to place someone.
			UpdateTableWaitLists((ClientDisplayTabIndex)i);

			// Now update the number of people waiting for each table.
			UpdateTableWaitListCounts((ClientDisplayTabIndex)i);
			update_waiting_list[i] = FALSE;
		}
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Recursively walk through a tree of tables and update the waiting
// list length for each one.
// Must be executed from the main card room thread.  It does NOT
// grab the CritSec for the tree structure.  This is OK because
// the tree is only modified by this particular thread.
//
void CardRoom::UpdateTableWaitListCounts(ClientDisplayTabIndex client_display_tab_index)
{

	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];
		if (t->client_display_tab_index==client_display_tab_index) {
		  #if 0	// 2022 kriskoin
			if (t->chip_type==CT_TOURNAMENT && t->table_tourn_state!=TTS_WAITING) {
				// This is a tournament table that has already started.  Nobody new will
				// ever be allowed to sit at this table again, therefore the waiting list
				// is always empty and we're not in line for it.
				t->summary_info.waiting_list_length = 0;
				for (int i=MAX_PLAYERS_PER_GAME ; i<PLAYERS_PER_TABLE_INFO_STRUCTURE ; i++) {
					zstruct(t->table_info.players[i]);
				}
			} else
		  #endif
			{
				struct WaitListEntry wle;
				zstruct(wle);

				wle.table_serial_number = t->table_serial_number;
				wle.desired_stakes = t->initial_big_blind_amount;
				wle.chip_type = t->chip_type;
				wle.game_rules = t->game_rules;
				int queue_len = waiting_lists[client_display_tab_index].GetWaitListPosition(&wle);
				//kp(("%s(%d) queue_len = %d\n", _FL, queue_len));

				if (t->summary_info.waiting_list_length != queue_len) {
					t->summary_info.waiting_list_length = (BYTE8)min(queue_len,250);
					t->summary_info_changed = TRUE;
					t->table_info_changed = TRUE;
				}
				// Update the list of players waiting for this table.  Add as
				// many as we have room for.
				for (int i=MAX_PLAYERS_PER_GAME ; i<PLAYERS_PER_TABLE_INFO_STRUCTURE ; i++) {
					zstruct(t->table_info.players[i]);
					WORD32 player_id = waiting_lists[client_display_tab_index].GetPlayerInWaitListPosition(&wle, i-MAX_PLAYERS_PER_GAME);
					if (player_id) {
						t->table_info.players[i].flags |= 0x01;	// on waiting list
						// Scan through and see if they're being called to this table.
						for (int j=0 ; j<t->wait_list_called_player_count ; j++) {
							if (t->wait_list_called_players[j].player_id == player_id) {
								// this played is being called to this table.
								t->table_info.players[i].flags |= 0x04;	// being called to the table
								break;	// done checking who is called to this table.
							}
						}

						t->table_info.players[i].player_id = player_id;
						Player *p = FindPlayer(player_id);
						if (p) {
							strnncpy(t->table_info.players[i].name, p->user_id, MAX_COMMON_STRING_LEN);
							strnncpy(t->table_info.players[i].city, p->City, MAX_COMMON_STRING_LEN);
							if (p->CountSeatedTables()) {
								t->table_info.players[i].flags |= 0x08;	// already seated somewhere
							}

						} else {
							kp(("%s(%d) Warning: player id $%08lx is on a waiting list but not in memory!\n", _FL, player_id));
						}
					}
				}
			}
		}
	}
}

//****************************************************************
// 
//
// Send a struct CardRoom_TableSummaryList to player p.
//
void CardRoom::SendSummaryListToPlayer(ClientDisplayTabIndex client_display_tab_index, Player *p)
{
	if (!table_summary_lists[client_display_tab_index]) {
		//Error(ERR_ERROR, "%s(%d) Table Summary List for game type %d does not yet exist!", _FL, client_display_tab_index);
		return;
	}

	// Fill in this client's wait list position for each table.
	struct CardRoom_TableSummaryInfo *tsi = table_summary_lists[client_display_tab_index]->tables;
	struct WaitListEntry wle;
	zstruct(wle);
	wle.player_id = p->player_id;
	for (int i=0 ; i<(int)table_summary_lists[client_display_tab_index]->table_count ; i++, tsi++) {
		wle.table_serial_number = tsi->table_serial_number;
		wle.desired_stakes = tsi->big_blind_amount;
		if (tsi->flags & TSIF_REAL_MONEY) {
			wle.chip_type = CT_REAL;
		} else if (tsi->flags & TSIF_TOURNAMENT) {
			wle.chip_type = CT_TOURNAMENT;
		} else {
			wle.chip_type = CT_PLAY;
		}
		wle.game_rules = (GameRules)tsi->game_rules;
	  #if 0	// 2022 kriskoin
		if (wle.chip_type!=CT_TOURNAMENT || tsi->tournament_state == TOURN_STATE_WAITING)
	  #endif
		{
			tsi->user_waiting_list_pos = (BYTE8)min(250, waiting_lists[client_display_tab_index].GetWaitListPosition(&wle));







		}

		// If this is an administrator, fill in the rake/hr stat
		tsi->rake_per_hour = 0;
		tsi->avg_response_time = 0;
		if (p->priv >= ACCPRIV_ADMINISTRATOR) {
			Table *t = TableSerialNumToTablePtr(tsi->table_serial_number);
			if (t) {
				tsi->rake_per_hour = t->rake_per_hour;
				// Fill in the average player reponse time.
				WORD32 response_time = (WORD32)(t->avg_response_time * 10.0 + .5);
				response_time = min(255,response_time);
				tsi->avg_response_time = (BYTE8)response_time;

			}
		}
	}

	// Some information only gets sent to high priv users
	if (p->priv >= ACCPRIV_CUSTOMER_SUPPORT) {
		table_summary_lists[client_display_tab_index]->unseated_players = (WORD16)min(65535,active_unseated_players);
		table_summary_lists[client_display_tab_index]->idle_players = (WORD16)min(65535, connected_idle_players);
		table_summary_lists[client_display_tab_index]->active_real_tables = (WORD16)min(65535,active_real_tables);
		table_summary_lists[client_display_tab_index]->number_of_accounts = SDB->GetUserRecordCount();
	} else {
		table_summary_lists[client_display_tab_index]->unseated_players = 0;
		table_summary_lists[client_display_tab_index]->idle_players = 0;
		table_summary_lists[client_display_tab_index]->active_real_tables = 0;
		table_summary_lists[client_display_tab_index]->number_of_accounts = 0;
	}
	p->SendDataStructure(DATATYPE_CARDROOM_TABLE_LIST,
			table_summary_lists[client_display_tab_index], table_summary_lists[client_display_tab_index]->length);
}



//****************************************************************
// 
//
// Send a struct CardRoom_TableInfo to player p.
//
void CardRoom::SendTableInfoToPlayer(WORD32 table_serial_number, Player *p)
{
	Table *t = TableSerialNumToTablePtr(table_serial_number);
	if (!t) {
		// They requested a table which does not exist.
		// Try to being them more up to date.
		if (DebugFilterLevel <= 0) {
			kp(("%s(%d) Player $%08lx request Table Info for non-existant table (%d)\n",
				_FL, p->player_id, table_serial_number));
		}
		for (int i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
			p->requesting_table_summary_list[i] = TRUE;	// flag that we want it sent.
		}
		return;
	}

	// Before sending, go through the list of players and set/clear and
	// disconnected flags.
	t->UpdateTableInfoConnectionStatus();

	pr(("%s(%d) Sending table %d TableInfo structure to player $%08lx (crc = $%08lx)\n",
				_FL, table_serial_number, p->player_id, CalcCRC32(&(t->table_info), sizeof(t->table_info))));
	p->SendDataStructure(DATATYPE_CARDROOM_TABLE_INFO, &(t->table_info), sizeof(t->table_info));
}


//****************************************************************
// https://github.com/kriskoin//
// Recursively walk through the tree of tables and reset all of
// the table_info_changed flags.
//
void CardRoom::ClearTableInfoChangedFlags(void)
{
	for (int i=0; i < table_count; i++) {
		tables[i]->table_info_changed = FALSE;
	}
}

//*********************************************************

// https://github.com/kriskoin//
// Update main loop background tasks (every 1 or 2s)
//

void CardRoom::UpdateMainLoopBgndTasks(int *work_was_done_flag)
{
	time_t now = time(NULL);
	// Check if the shot clock has 'gone off'
	if (ShotClockDate) {
		iShotClockETA = max(0,(long)(ShotClockDate - now));
	} else {
		iShotClockETA = 0;
	}
	ShotClockExpirySecondCounter = SecondCounter + iShotClockETA;	// calculate SecondCounter when we expect the shot clock to expire.

	if (ShotClockDate && iShotClockETA <= 60) {	// only a little time left?
		if (!iShutdownPlayMoneyGamesFlag && (ShotClockFlags & SCUF_SHUTDOWN_WHEN_DONE)) {
			// Shut down play money games
			kp(("%s(%d) Not much time left... shutting play money games down now.\n",_FL));
			iShutdownPlayMoneyGamesFlag// 2022 kriskoin = TRUE;
			if (ShotClockFlags & SCUF_SHUTDOWN_IS_BRIEF) {
				iShutdownIsBriefFlag = TRUE;	// treat as a brief shutdown (for msg purposes)
			} else {
				iShutdownIsBriefFlag = FALSE;	// treat as a long shutdown (for msg purposes)
			}
		}

	} else {
		iShutdownPlayMoneyGamesFlag = FALSE;
	}

	if (ShotClockDate && iShotClockETA <= 0) {
		if (!iShutdownAfterECashCompletedFlag && (ShotClockFlags & SCUF_SHUTDOWN_WHEN_DONE)) {
			// Start the server shutdown procedure.
			kp(("%s(%d) Shot clock has counted down to zero... starting shutdown procedure.\n",_FL));
			kp(("%s(%d) Closing cashier...\n", _FL));
			ShotClockFlags |=  SCUF_CLOSE_CASHIER;		// close cashier
			ShotClockFlags &= ~SCUF_ECASH_AUTO_SHUTDOWN;	// disable auto up/dn

			iShotClockChangedFlag = TRUE;
			NextAdminStatsUpdateTime = 0;	// update/send admin stats packet asap
			iShutdownAfterECashCompletedFlag = TRUE;
			if (ShotClockFlags & SCUF_SHUTDOWN_IS_BRIEF) {
				iShutdownIsBriefFlag = TRUE;	// treat as a brief shutdown (for msg purposes)
			} else {
				iShutdownIsBriefFlag = FALSE;	// treat as a long shutdown (for msg purposes)
			}
		  #if !WIN32
		  	kp(("%s(%d) Shutting down srvmon...\n",_FL));
			system("killall srvmon");
		  #endif
		}
	}

	// Periodically update the admin stats...
	if (now >= NextAdminStatsUpdateTime) {

		// Time to update them...
	  #if WRITE_LEAKS
	  	kp(("%s %s(%d) Freeing packets and calling write_leaks(). Living packets = %d before PktPool_FreePackets()\n", TimeStr(), _FL, dwLivingPackets));
		PktPool_FreePackets();	// free up any packets in the packet pool.
		kp(("%s %s(%d) Heap compaction done.  Living packets = %d after PktPool_FreePackets().\n", TimeStr(), _FL, dwLivingPackets));
		write_leaks();
	  #endif

		// First calculate seconds until midnight...
		time_t tomorrow = now + 24*60*60;	// calculate this time tomorrow
		struct tm t;
		localtime(&tomorrow, &t);
		t.tm_sec = 0;
		t.tm_min = 1;
		t.tm_hour = 0;	// set time to 12:01am
		// Turn it back into a time_t
		time_t midnight = mktime(&t);
		long seconds_left_today = max(1, midnight - now);
		long seconds_so_far_today = max(60, 24*3600 - seconds_left_today);

		if (!YesterdaySavedStats.accounts) {
			ReadFile("savedstats.bin", &YesterdaySavedStats, sizeof(YesterdaySavedStats), NULL);
		}

		// Scroll our saved data down by a field
		// Don't throw out old data if the last data was collected too recently.
		if (CurrentAdminStats[0].time - CurrentAdminStats[1].time >= 60) {
			memmove(&CurrentAdminStats[1], &CurrentAdminStats[0], sizeof(CurrentAdminStats[0])*(ADMIN_STATS_TO_KEEP-1));
		}
		zstruct(CurrentAdminStats[0]);
		struct AdminStats *as = &CurrentAdminStats[0];
		struct AdminStats *old_as = NULL;
		// Look for the oldest stats from today that are available
		int i;
		for (i=ADMIN_STATS_TO_KEEP-1 ; i >= 1 ; i--) {
			old_as = &CurrentAdminStats[i];
			if (old_as->time && old_as->time >= midnight - (unsigned long)24*3600 + 60) {
				//kp(("%s(%d) found time in location %d\n", _FL, i));
				break;	// done looking.
			}
		}
		//kp(("%s(%d) using slot %d for old\n", _FL, old_as - as));

		as->active_player_connections = active_player_connections;
		as->player_count_high = daily_peak_active_players;

		as->player_count_low  = daily_low_active_players;
		as->real_tables_high = daily_peak_active_real_tables;
		as->play_tables_high = daily_peak_active_tables - daily_peak_active_real_tables;
		as->real_tables_low = daily_low_active_real_tables;
		as->play_tables_low = daily_low_active_tables - daily_low_active_real_tables;
		as->real_tables = active_real_tables;
		as->play_tables = active_tables - active_real_tables;
		as->server_uptime = SecondCounter;
		as->tournaments_today = tournaments_played_today;
		as->ecash_queue_len = Ecash_GetQueueLen();
		as->ecash_post_time = iEcashPostTime;
		as->ecash_threads = ECashThrea// 2022 kriskoinds;
		if (table_summary_lists[0]) {
			as->money_in_play = table_summary_lists[0]->money_in_play;
			as->money_logged_in = table_summary_lists[0]->money_logged_in;
		}
		as->unseated_players = active_unseated_players;
		as->idle_players = connected_idle_players;
		as->multi_seated_players = multi_seated_players;
		
		// bad beat stats
		as->bad_beats_today = ServerVars.bad_beat_count;
		as->bad_beats_payout = ServerVars.bad_beat_payout;
		as->gross_bets_today = ServerVars.gross_bets - YesterdaySavedStats.gross_bets;
		as->gross_tournament_buyins_today = ServerVars.gross_tournament_buyins - YesterdaySavedStats.gross_tournament_buyins;

		// copy credit card purchase limits
		as->CCLimit1Amount  = CCLimit1Amount;
		as->CCLimit1Amount2 = CCLimit1Amount2;
		as->CCLimit2Amount  = CCLimit2Amount;
		as->CCLimit2Amount2 = CCLimit2Amount2;
		as->CCLimit3Amount  = CCLimit3Amount;
		as->CCLimit3Amount2 = CCLimit3Amount2;

		// Copy current packet stats...
		memmove(as->packet_stats, CurrentPacketStats, sizeof(as->packet_stats[0])*DATATYPE_COUNT);
		as->living_packets = dwLivingPackets;			// # of packets still allocated.
		as->packets_allocated = dwPacketsAllocated;		// stat only: total # of packets we've ever allocated in PktPool_Alloc()
		as->packets_constructed = dwPacketsConstructed;	// stat: total # of packets we've ever constructed (packet contructor)

		for (i=0 ; i<min(PKTPOOL_COUNT// 2022 kriskoin,STATS_PKTPOOL_COUNT) ; i++) {
			as->pool_stats[i].packet_count = PktPools[i].packet_count;
			as->pool_stats[i].alloc_count = PktPools[i].alloc_count;
			as->pool_stats[i].max_pool_size = PktPools[i].max_pool_size;
		}

		as->output_queue_lens[0] = (int)iPlrOutQueueLens[0];	// current depth of the player data output queue
		as->output_queue_lens[1] = (int)iPlrOutQueueLens[1];	// current depth of the player data output queue

	  #if 0	//19991207MB  not filled in yet:
		WORD32 packets_sent;		// total # of packets sent
		WORD32 packets_rcvd;		// total # of packets received
	  #endif

		as->next_game_number = GetNextGameSerialNumber();
		as->games_today = as->next_game_number - YesterdaySavedStats.game_number;
		as->rake_balance = GetAccountBalance("Rake");
		as->rake_today = as->rake_balance - YesterdaySavedStats.rake_balance;
		if (as->rake_today < 0) {
			as->rake_today = 0;
		}
		as->ecash_today = -GetAccountB// 2022 kriskoinalance("Ecash") - YesterdaySavedStats.ecash_balance;
		as->ecashfee_today = -GetAccountBalance("EcashFee") - YesterdaySavedStats.ecashfee_balance;

		as->client_count_newer = client_count_newer;	// # of clients connected with newer than current version
		as->client_count_current = client_count_current;// # of clients connected with the current version
		as->client_count_old = client_count_old;		// # of clients connected with old version
		as->table_names_used = table_names_used;		// # of real money table names currently used
		as->table_names_avail = table_names_avail;		// # of real money table names available to be used (total)
		as->number_of_accounts = SDB->GetUserRecordCount();
		as->number_of_accounts_purged = SDB->number_of_accounts_purged;
		as->number_of_accounts_today = as->number_of_accounts - YesterdaySavedStats.accounts + SDB->number_of_accounts_purged;
		as->database_size = SDB->iRecordCount;
		as->avg_response_time_real_money = (float)avg_response_time_real_money;	// avg response time for entire cardroom (real money tables)
		as->avg_response_time_play_money = (float)avg_response_time_play_money;	// avg response time for entire cardroom (play money tables)
		as->accept_avg_ms = (float)accept_avg_ms;
		as->mainloop_avg_ms = (float)mainloop_avg_ms;
		as->inputloop_avg_ms = (float)inputloop_avg_ms;
		if (total_tables_updated) {
			as->table_avg_ms = (float)((double)total_table_update_ms / (double)total_tables_updated);
			total_table_update_ms = 0;	// reset
			total_tables_updated = 0;	// reset
		}

		strnncpy(as->shotclock_message1, ShotClockMessage1, SHOTCLOCK_MESSAGE_LEN);
		strnncpy(as->shotclock_message2, ShotClockMessage2, SHOTCLOCK_MESSAGE_LEN);
		strnncpy(as->shotclock_message1_expired, ShotClockMessage1_Expired, SHOTCLOCK_MESSAGE_LEN);
		strnncpy(as->shotclock_message2_expired, ShotClockMessage2_Expired, SHOTCLOCK_MESSAGE_LEN);
		as->shotclock_flags = ShotClockFlags;
		as->hand_history_queue_time = AvgHandHistoryQueueTime;

		FILE *fd = NULL;
	  #if WIN32
		as->os_up_time = (WORD32)GetTickCount()/1000;
	  #else
		// Read the load average from /proc/loadavg and convert to integers
		fd = fopen("/proc/loadavg", "rt");

		if (fd) {// 2022 kriskoin
			float la1, la5, la15;
			la1 = la5 = la15 = 0.0;
			fscanf(fd, "%f %f %f", &la1, &la5, &la15);
			fclose(fd);
			as->load_avg[0] = (WORD32)(la1*100.0);
			as->load_avg[1] = (WORD32)(la5*100.0);
			as->load_avg[2] = (WORD32)(la15*100.0);
		} else {
			kp(("%s(%d) Error: could not open /proc/loadavg\n",_FL));
		}

		// Read the uptime and cpu idle time from /proc/uptime
		fd = fopen("/proc/uptime", "rt");
		if (fd) {
			double total_uptime, total_idle_time;
			total_uptime = total_idle_time = 0.0;
			fscanf(fd, "%lf %lf", &total_uptime, &total_idle_time);
			fclose(fd);

			as->os_up_time = (WORD32)total_uptime;
			as->os_idle_time = total_idle_time;

			// calculate most recent idle time
			WORD32 time_diff = now - CurrentAdminStats[1].time;
			if (time_diff) {
				as->recent_idle_percentage = (float)((as->os_idle_time - CurrentAdminStats[1].os_idle_time)*100.0 / (double)time_diff);
			}
		} else {
			kp(("%s(%d) Error: could not open /proc/uptime\n",_FL));
		}
	  #endif

		as->bytes_sent = total_bytes_sent;
		as->bytes_rcvd = total_bytes_received;
		if (old_as->time) {
			WORD32 time_diff = now - old_as->time;
			if (time_diff > 0) {
				as->bytes_sent_per_second = (as->bytes_sent - old_as->bytes_sent) / time_diff;
				as->bytes_rcvd_per_second = (as->bytes_rcvd - old_as->bytes_rcvd) / time_diff;
				as->games_per_hour = (as->next_game_number - old_as->next_game_number) * 3600 / time_diff;
				as->rake_per_hour = (as->rake_balance - old_as->rake_balance) * 3600 / time_diff;
				float daily_rake_per_second = (float)as->rake_today / (float)seconds_so_far_today;
				as->est_rake_for_today = (WORD32)(as->rake_today + max((float)as->rake_per_hour/3600., daily_rake_per_second) * seconds_left_today);
				float daily_ecash_per_second = (float)as->ecash_today / (float)seconds_so_far_today;
				as->est_ecash_for_today = (WORD32)(as->ecash_today + daily_ecash_per_second * seconds_left_today);

			}

		}

	  #if !WIN32
		struct mallinfo mi = mallinfo();
		as->mem_allocated  = mi.arena;		// total memory allocated from the operating system
		as->mem_used	   = mi.uordblks;	// total memory currently used (of what is allocated)
		as->mem_not_used   = mi.fordblks;	// total memory currently not used (of what is allocated)
	  #endif
		as->time = now;	// flag it as "new"

		//kriskoin: 		fd = fopen("stats.txt.new", "wt");
		if (fd) {
			fprintf(fd, "%d\n", active_player_connections);
			fprintf(fd, "%d\n", active_tables);
      fprintf(fd, "%4.2f\n", as->avg_response_time_real_money*1000.0);
      fprintf(fd, "%4.2f\n", as->avg_response_time_play_money*1000.0);
			fclose(fd);
			// Replace old file with new one...
		  #if WIN32
		  	remove("stats.txt");
			rename("stats.txt.new", "stats.txt");
		  #else
			system("mv stats.txt.new stats.txt");
		  #endif
		}
		// Calculate when the stats should be calculated again
	  #if 1	// 2022 kriskoin
		localtime(&now, &t);
		t.tm_sec = 0;
		t.tm_min++;								// do again at top of next minute

		NextAdminStatsUpdateTime = mktime(&t);	// Turn it back into a time_t
	  #else
		kp1(("%s(%d) *** WARNING: UPDATING ADMIN STATS CONSTANTLY ***\n",_FL));
		NextAdminStatsUpdateTime = now+1;
	  #endif
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
	}
}

//****************************************************************
// 
//
// If any summary data has changed, update the serial numbers
// and send out it out to all clients.
//
void CardRoom::UpdateSummaryData(int *work_was_done_flag)
{
	int changes = FALSE;
	// Only check every 20s or so to see if the summary lists have changed.
	if (iShotClockChangedFlag) {
		next_summary_data_check_time = 0;	// allow resending right now.
	}
	if (SecondCounter >= next_summary_data_check_time) {
		int ecash_people_ahead_of_us = Ecash_GetQueueLen();
	  #if 0	//kriskoin: 		ecash_people_ahead_of_us = 10;
		iEcashPostTime = 60;
	  #endif
		if (ECashThreads > 1) {
			ecash_people_ahead_of_us /= ECashThreads;
		}
		int ecash_estimated_time = (ecash_people_ahead_of_us + 1) * iEcashPostTime;
		int ecash_estimated_minutes = (ecash_estimated_time + 59) / 60;

		long eta = 0;
		if (ShotClockDate) {
			eta = max(0,(long)(ShotClockDate - time(NULL)));
		}
		if (ECashDisabled) {	// disabled from .ini file?
			ShotClockFlags |= SCUF_CLOSE_CASHIER;	// make sure clients know
		}

		// Check to see if tournaments should be opened/closed.
		if (!eta) {	// shot clock expired?
			// The shot clock has expired... make sure the tournament status matches the

			// SCUF_TOURNAMENTS_OPEN bit.
			if (ShotClockFlags & SCUF_TOURNAMENTS_OPEN) {
				// Tournaments should be open right now.
				ShotClockFlags  &= ~(SCUF_CLOSE_TOURNAMENTS|SCUF_NO_TOURNAMENT_SITDOWN);	// clear bits
				GameDisableBits &= ~GDB_CLOSE_TOURNAMENTS;
				GameCloseBits   &= ~GDB_CLOSE_TOURNAMENTS;
				if (!iTournamentTableCreationAllowed) {
					iTournamentTableCreationAllowed = TRUE;
					next_manage_tables_time = 0;	// do table management asap
					kp(("%s %s(%d) Tournaments are now open!\n", TimeStr(), _FL));
				}
			} else {
				if (iTournamentTableCreationAllowed) {
					iTournamentTableCreationAllowed = FALSE;
					kp(("%s %s(%d) Tournaments are now closed!\n", TimeStr(), _FL));
				}
			}
		}

		for (int i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
			if (table_summary_lists[i]) {
				table_summary_lists[i]->total_players = active_player_connections;
				table_summary_lists[i]->active_tables = (WORD16)min(65535,active_tables);
				table_summary_lists[i]->shotclock_eta = eta;
				table_summary_lists[i]->shotclock_date = ShotClockDate;
				table_summary_lists[i]->shotclock_flags = ShotClockFlags;
				if (eta) {	// pre-expiry
					strnncpy(table_summary_lists[i]->shotclock_msg1, ShotClockMessage1, SHOTCLOCK_MESSAGE_LEN);
					strnncpy(table_summary_lists[i]->shotclock_msg2, ShotClockMessage2, SHOTCLOCK_MESSAGE_LEN);
				} else {	// shot clock has expired... use secondary messages
					strnncpy(table_summary_lists[i]->shotclock_msg1, ShotClockMessage1_Expired, SHOTCLOCK_MESSAGE_LEN);
					strnncpy(table_summary_lists[i]->shotclock_msg2, ShotClockMessage2_Expired, SHOTCLOCK_MESSAGE_LEN);
				}
				table_summary_lists[i]->cc_processing_estimate = (BYTE8)min(255,ecash_estimated_minutes);
				table_summary_lists[i]->max_tournament_tables = (WORD16)MaxTables_GDB[GDBITNUM_CLOSE_TOURNAMENTS];
				if (summary_info_changed[i] || iShotClockChangedFlag) {
					summary_info_changed[i] = FALSE;
					summary_serial_nums.serial_num[i]++;	// change serial number
					changes = TRUE;
				}
			}
		}
		iShotClockChangedFlag = FALSE;		// reset it

	  #if 1	// 2022 kriskoin
		int min_send_interval = min(20,2+active_tables*2);
		//kriskoin: 		if (eta && eta < min_send_interval) {
			min_send_interval = eta;	// run again exactly at expiry.
			next_manage_tables_time = SecondCounter + eta;
			next_mainloop_bgndtask_time = SecondCounter + eta;
		}
		next_summary_data_check_time = SecondCounter + min_send_interval;
	  #else
		next_summary_data_check_time = SecondCounter + 20;

	  #endif

		// Update the all in reset queue... reset players if necessary.
		// This function should be called periodically (every 10s or so).
		UpdateAllInResetQueue();
	}

	// If there were changes, send out to all clients.
	if (changes) {
		// Before sending it out, get an up-to-date total of the money in play.
		WORD32 money_in_play = 0;
		WORD32 money_logged_in = 0;
		// Count how much is in play and logged in...
		int i;

		for (i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p) {
				money_in_play += p->RealInPlay;
				money_logged_in += p->RealInPlay + p->RealInBank;
			}
		}
		// Save the resulting total to the various structures.
		for (i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
			if (table_summary_lists[i]) {

				table_summary_lists[i]->money_in_play   = money_in_play;
				table_summary_lists[i]->money_logged_in = money_logged_in;
			}
		}

		// Now send the updated serial numbers out to everyone.
		for (i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p && p->player_id && p->server_socket && !p->server_socket->disconnected) {	// if player exists and is logged in...
				p->SendDataStructure(DATATYPE_CARDROOM_SERIAL_NUMS, &summary_serial_nums, sizeof(summary_serial_nums));

			}
		}
		SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
	}
}

//****************************************************************
// 
//
// Count how many tables and 'empty' tables there are and mark any extras
// for deletion.
//
void CardRoom::CountEmptyTables(struct TableType *tt, int *output_table_count, int *empty_table_count)
{
	for (int i=0; i < table_count; i++) {
		Table *t = tables[i];

		// Verify this is the correct type of game...
		if (t->client_display_tab_index == tt->client_display_tab_index &&
			t->game_rules == tt->game_rules &&
			t->max_number_of_players == tt->max_players &&
			t->initial_small_blind_amount == tt->small_blind &&
			t->initial_big_blind_amount == tt->big_blind &&
			t->chip_type == tt->chip_type)
		{
			// This is the right kind of table.
			if (t->chip_type==CT_TOURNAMENT) {
				// We don't count tournament tables that are finished, otherwise
				// we never add new ones.  They'll never play again or do anything
				// else interesting, so they're cheap to leave around.  They die off
				// automatically.
				if (t->table_tourn_state != TTS_FINISHED) {
					(*output_table_count)++;
				}
			} else {
				(*output_table_count)++;
			}

			if (t->table_is_active != Table::TABLE_ACTIVE_FALSE) {
				// If we're shutting down, only consider them active if they're
				// actually playing right now.
				if (iShutdownAfterGamesCompletedFlag) {
					if (t->game) {	// a game object active right now?
						active_tables_temp++;	// consider this one 'active'
						if (t->chip_type != CT_PLAY) {
							active_real_tables_temp++;
						}
					}
				} else {
					// standard method...
					active_tables_temp++;	// consider this one 'active'
					if (t->chip_type != CT_PLAY) {
						active_real_tables_temp++;
					}
				}
			}

			// Determine if this table is killable or not.
			// If we've got too many tables open, use different rules for
			// determining when to close them.
			int too_many_open = FALSE;
			if (tt->chip_type == CT_REAL) {
				if (iRealMoneyTableCount > MaxRealTableCount) {
					too_many_open = TRUE;
				}
			} else if (tt->chip_type == CT_PLAY) {
				if (iPlayMoneyTableCount > MaxPlayTableCount) {
					too_many_open = TRUE;
				}
			} else if (tt->chip_type == CT_TOURNAMENT) {
				if (iTournamentTableCount > MaxTournamentTableCount) {
					too_many_open = TRUE;
				}
				if (!iTournamentTableCreationAllowed) {	// not allowed to create more tournaments?
					too_many_open = TRUE;
				}

			}

			// If this table type is supposed to be closed, try to close
			// any tables of this type.
			if (tt->game_disable_bits & GameCloseBits) {

				too_many_open = TRUE;	// treat the same as if too many are open
			}

			int killable = FALSE;
			if (too_many_open) {
				// Too many of these tables are open.
				// As long as it's empty, it gets killed asap.
				if (t->chip_type==CT_TOURNAMENT) {
					// Tournament tables can be killed immediately if
					// they are finished or waiting but empty.
				  #if 0	//kriskoin: 					if (t->table_tourn_state == TTS_FINISHED) {
						killable = TRUE;
					}
				  #endif
					if (!t->summary_info.player_count && t->table_tourn_state == TTS_WAITING) {
						killable = TRUE;
					}
				} else {
					if (!t->summary_info.player_count) {
						killable = TRUE;
					}
				}
			} else {	// normal...
				// If there is nobody at this table and it has not played a game
				// for a while, mark it as killable.
				if (!t->summary_info.player_count &&
						(SecondCounter - t->last_game_end_time > DelayBeforeDeletingTables)) {

					killable = TRUE;
				}

				// If this is the last empty table, it's not killable.
				if (!*empty_table_count) {
					killable = FALSE;	// don't kill last table
				}
			}

			// Finished tournament table handling...
			if (t->chip_type==CT_TOURNAMENT &&
				!t->summary_info.player_count &&
				t->table_tourn_state==TTS_FINISHED)
			{
				// This is an empty finished tournament table.  If has finished playing
				// for at least 2 minutes, it's killable.
				if (SecondCounter - t->last_game_end_time > 2*60) {
					killable = TRUE;
				} else {
					// Too soon since tournament ended, not killable yet.
					killable = FALSE;
				}
			}

			if (killable)  {
				pr(("%s(%d) Marking table %d for deletion. It's empty and hasn't played a game for %ds.\n",
						_FL,t->table_serial_number, SecondCounter - t->last_game_end_time));
				kill_table = t->table_serial_number;
			}

			// Determine if it is 'empty' or not (for deciding when to add new tables)
			// If it's half empty and no waiting list is required, that's good
			// enough to be considered 'empty enough' that we don't need to
			// create a new table.
			if (t->chip_type==CT_TOURNAMENT) {
				// If a tournament table is waiting and not full, it's considered empty.
				// If it's playing or full, it's considered full.
				if (t->table_tourn_state==TTS_WAITING && t->summary_info.player_count < t->max_number_of_players) {
					(*empty_table_count)++;	// increase counter.
				}
			} else if ((t->summary_info.player_count < t->max_number_of_players / 2) &&
				!(t->summary_info.flags & TSIF_WAIT_LIST_REQUIRED)) {
				// this table is considered 'empty'.
				(*empty_table_count)++;	// increase counter.
			}
		}
	}
}

//****************************************************************
// 
//
// Take care of over-all table management.  Create new tables,
// delete old (empty) tables, etc.
//
void CardRoom::ManageTables(void)
{
	// Current GameDisableBits defined (corresponds to the .ini file entries)
	//  1  Hold'em $15/$30 and $20/$40
	//  2  Tournaments *** NOTE: SCUF_CLOSE_TOURNAMENTS code is HARD-CODED for this bit!

	// Rake profile descriptions:
	//  RT_PRO1:	// normal 20/40/60 raking ($1 at each level, $3 max)
	//  RT_PRO2:	// higher limit 40/70/100 raking ($1 at each level, $3 max)
	//  RT_PRO3:	// 50-cent raking at $10 intervals (10/20/30/40/50/60 = $3 max)
	//  RT_PRO4:	// special // for 15/30 one on one, we rake $1 at $30 (capped $1)
	//  RT_PRO5:	// 50 cents at $20, 50 cents at $50, capped at $1
	//  RT_PRO6:	// 25-cent raking at $5 intervals (5/10/15/20 = $1 max)
	
	RakeType RT_1ON1 = RT_PRO5;	// new way

	static struct TableType StandardTableTypes[] =  {
		// display tab			rules					gdb	plrs sb      bb		chip_type	rake profile
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  1*100,  3*100, CT_PLAY,	RT_NONE, // 3/6 (note: actually added at MainLoop())
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  1*100,  3*100, CT_PLAY,	RT_NONE, // 3/6
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  1*100,  3*100, CT_PLAY,	RT_NONE, // 3/6
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,   1*100,  2*100, CT_PLAY,	RT_NONE, // 4/8  (ante is $1, bring-in is $2)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,   1*100,  2*100, CT_PLAY,	RT_NONE, // 4/8  (ante is $1, bring-in is $2)
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   1*100,  3*100, CT_PLAY,	RT_NONE, // 3/6

		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		4,	10,     25,     50, CT_REAL,	RT_PRO6, // $.50/$1
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,     50,  1*100, CT_REAL,	RT_PRO1, // $1/$2 (20020207HK)
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  1*100,  2*100, CT_REAL,	RT_PRO1, // $2/$4
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  1*100,  3*100, CT_REAL,	RT_PRO1, // $3/$6
//		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  2*100,  4*100, CT_REAL,	RT_PRO1, // $4/$8
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  2*100,  5*100, CT_REAL,	RT_PRO1, // $5/$10
//		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  3*100,  6*100, CT_REAL,	RT_PRO1, // $6/$12
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  4*100,  8*100, CT_REAL,	RT_PRO1, // $8/$16
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10,  5*100, 10*100, CT_REAL,	RT_PRO1, // $10/$20

		DISPLAY_TAB_HOLDEM,             GAME_RULES_HOLDEM,              0,      5, 10*100, 20*100, CT_REAL,     RT_PRO1, //$20/$40 needs $40 (25+5+5+5) (added to 1.06-3)


		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		1,	10, 10*100, 15*100, CT_REAL,	RT_PRO2, // $15/$30
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		1,	10, 10*100, 20*100, CT_REAL,	RT_PRO2, // $20/$40 needs $40 (25+5+5+5) (added to 1.06-3)
		// DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	5, 10*100, 20*100, CT_REAL,	RT_PRO2, // $20/$40 needs $40 (25+5+5+5) (added to 1.06-3)


	  #if 0	// 2022 kriskoin
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10, 10*100, 25*100, CT_REAL,	RT_PRO2, // $25/$50
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10, 15*100, 30*100, CT_REAL,	RT_PRO2, // $30/$60 needs $60 (25+25+5+5) (added to 1.06-3)
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10, 20*100, 40*100, CT_REAL,	RT_PRO2, // $40/$80 needs $40 (25+5+5+5) and $80 (25+25+25+5) (added to 1.06-3)
		DISPLAY_TAB_HOLDEM,		GAME_RULES_HOLDEM,		0,	10, 25*100, 50*100, CT_REAL,	RT_PRO2, // $50/$100
	  #endif

		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	4,	10,     25,     50, CT_REAL,	RT_PRO6, // $0.50/$1
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,     50,  1*100, CT_REAL,	RT_PRO6, // $1/$2 (20020207HK)
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  1*100,  2*100, CT_REAL,	RT_PRO1, // $2/$4
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  1*100,  3*100, CT_REAL,	RT_PRO1, // $3/$6
//		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  2*100,  4*100, CT_REAL,	RT_PRO1, // $4/$8
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  2*100,  5*100, CT_REAL,	RT_PRO1, // $5/$10

//		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  3*100,  6*100, CT_REAL,	RT_PRO1, // $6/$12
//		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  4*100,  8*100, CT_REAL,	RT_PRO1, // $8/$16
		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  5*100, 10*100, CT_REAL,	RT_PRO1, // $10/$20
//		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  8*100, 15*100, CT_REAL,	RT_PRO2, // $15/$30
//		DISPLAY_TAB_OMAHA_HI,	GAME_RULES_OMAHA_HI,	0,	10,  10*100,20*100, CT_REAL,	RT_PRO2, // $20/$40

		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	4,	10,     25,     50, CT_REAL,	RT_PRO6, // $0.50/$1
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,     50,  1*100, CT_REAL,	RT_PRO6, // $1/$2 (20020207HK)
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  1*100,  2*100, CT_REAL,	RT_PRO1, // $2/$4
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  1*100,  3*100, CT_REAL,	RT_PRO1, // $3/$6
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  2*100,  5*100, CT_REAL,	RT_PRO1, // $5/$10
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  4*100,  8*100, CT_REAL,	RT_PRO1, // $8/$16
		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  5*100, 10*100, CT_REAL,	RT_PRO1, // $10/$20
//		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  8*100, 15*100, CT_REAL,	RT_PRO2, // $15/$30
//		DISPLAY_TAB_OMAHA_HI_LO,GAME_RULES_OMAHA_HI_LO,	0,	10,  10*100,20*100, CT_REAL,	RT_PRO2, // $20/$40

		// display tab			rules					gdb	plrs  ante bring-in chip_type	rake profile
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		4,	8,	     0,     25, CT_REAL,	RT_PRO6, // $0.50/$1   (0 ante, $0.25 bring-in)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	    25,     50, CT_REAL,	RT_PRO6, // $1/$2   (.25 ante, $0.50 bring-in) (20020207HK)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	    25,  1*100, CT_REAL,	RT_PRO1, // $2/$4   (.25 ante, $1 bring-in)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	    50,  2*100, CT_REAL,	RT_PRO1, // $4/$8   (.50 ante, $2 bring-in)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	    50,  3*100, CT_REAL,	RT_PRO1, // $6/$12  (.50 ante, $2 bring-in)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	 1*100,  4*100, CT_REAL,	RT_PRO1, // $8/$16  ($1 ante, $4 bring-in)
		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	 1*100,  5*100, CT_REAL,	RT_PRO1, // $10/$20 ($1 ante, $5 bring-in)
//		DISPLAY_TAB_STUD7,		GAME_RULES_STUD7,		0,	8,	 2*100,  6*100, CT_REAL,	RT_PRO1, // $12/$24 ($2 ante, $6 bring-in)

		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	4,	8,	     0,     25, CT_REAL,	RT_PRO6, // $0.50/$1   (0 ante, $0.25 bring-in)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	    25,     50, CT_REAL,	RT_PRO6, // $1/$2   (.25 ante, $0.50 bring-in) (20020207HK)

		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	    25,  1*100, CT_REAL,	RT_PRO1, // $2/$4   (.25 ante, $1 bring-in)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	    50,  2*100, CT_REAL,	RT_PRO1, // $4/$8   (.50 ante, $2 bring-in)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	    50,  3*100, CT_REAL,	RT_PRO1, // $6/$12  (.50 ante, $2 bring-in)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	 1*100,  4*100, CT_REAL,	RT_PRO1, // $8/$16  ($1 ante, $4 bring-in)
		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	 1*100,  5*100, CT_REAL,	RT_PRO1, // $10/$20 ($1 ante, $5 bring-in)
//		DISPLAY_TAB_STUD7_HI_LO,GAME_RULES_STUD7_HI_LO,	0,	8,	 2*100,  6*100, CT_REAL,	RT_PRO1, // $12/$24 ($2 ante, $6 bring-in)



//		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   1*100,  2*100, CT_REAL,	RT_1ON1, // $2/$4 ** Probably very low rake
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   1*100,  3*100, CT_REAL,	RT_1ON1, // $3/$6
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   2*100,  5*100, CT_REAL,	RT_1ON1, // $5/$10
//		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   4*100,  8*100, CT_REAL,	RT_1ON1, // $8/$16
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,   5*100, 10*100, CT_REAL,	RT_1ON1, // $10/$20
//		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,		0,	2,  10*100, 15*100, CT_REAL,	RT_1ON1, // $15/$30 ($1 rake at $30)
//		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_HOLDEM,	    0,	2,  10*100, 20*100, CT_REAL,	RT_1ON1, // $20/$40

		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI,	0,	2,   1*100,  3*100, CT_REAL,	RT_1ON1, // $3/$6
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI,	0,	2,   2*100,  5*100, CT_REAL,	RT_1ON1, // $5/$10
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI,	0,	2,   5*100, 10*100, CT_REAL,	RT_1ON1, // $10/$20

		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI_LO,	0,	2,   1*100,  3*100, CT_REAL,	RT_1ON1, // $3/$6
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI_LO,	0,	2,   2*100,  5*100, CT_REAL,	RT_1ON1, // $5/$10
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_OMAHA_HI_LO,	0,	2,   5*100, 10*100, CT_REAL,	RT_1ON1, // $10/$20

		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7,		0,	2,	    25,  1*100, CT_REAL,	RT_1ON1, // $2/$4   (.25 ante, $1 bring-in)
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7,		0,	2,	    50,  2*100, CT_REAL,	RT_1ON1, // $4/$8   (.50 ante, $2 bring-in)
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7,		0,	2,	 1*100,  4*100, CT_REAL,	RT_1ON1, // $8/$16  ($1 ante, $4 bring-in)

		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7_HI_LO,	0,	2,	    25,  1*100, CT_REAL,	RT_1ON1, // $2/$4   (.25 ante, $1 bring-in)
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7_HI_LO,	0,	2,	    50,  2*100, CT_REAL,	RT_1ON1, // $4/$8   (.50 ante, $2 bring-in)
		DISPLAY_TAB_ONE_ON_ONE,	GAME_RULES_STUD7_HI_LO,	0,	2,	 1*100,  4*100, CT_REAL,	RT_1ON1, // $8/$16  ($1 ante, $4 bring-in)

		// Tournament tables.  SB is the fee, BB is the buy-in amount.

		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  1*100,  5*100, CT_TOURNAMENT,	RT_PRO1, // $5  ($1)   20%
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  1*100, 10*100, CT_TOURNAMENT,	RT_PRO1, // $10 ($1)   10%
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  2*100, 20*100, CT_TOURNAMENT,	RT_PRO1, // $20 ($2)   10%

		DISPLAY_TAB_TOURNAMENT, GAME_RULES_HOLDEM,              2,      5,   2*100, 20*100, CT_TOURNAMENT,      RT_PRO1,

		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  3*100, 30*100, CT_TOURNAMENT,	RT_PRO1, // $30 ($3)   10%
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  5*100, 50*100, CT_TOURNAMENT,	RT_PRO1, // $50 ($5)   10%
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10,  9*100,100*100, CT_TOURNAMENT,	RT_PRO1, // $100 ($9)   9%
//		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10, 18*100,200*100, CT_TOURNAMENT,	RT_PRO1, // $200 ($18)  9%
//		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_HOLDEM,		2,	10, 40*100,500*100, CT_TOURNAMENT,	RT_PRO1, // $500 ($40)  8%

	  #if 0	// 2022 kriskoin
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI,	2,	10,  2*100, 20*100, CT_TOURNAMENT,	RT_PRO1, // $20 ($2)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI,	2,	10,  4*100, 50*100, CT_TOURNAMENT,	RT_PRO1, // $50 ($4)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI,	2,	10,  8*100,100*100, CT_TOURNAMENT,	RT_PRO1, // $100 ($8)

		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI_LO,	2,	10,  2*100, 20*100, CT_TOURNAMENT,	RT_PRO1, // $20 ($2)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI_LO,	2,	10,  4*100, 50*100, CT_TOURNAMENT,	RT_PRO1, // $50 ($4)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_OMAHA_HI_LO,	2,	10,  8*100,100*100, CT_TOURNAMENT,	RT_PRO1, // $100 ($8)

		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7,		2,	 8,  2*100, 20*100, CT_TOURNAMENT,	RT_PRO1, // $20 ($2)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7,		2,	 8,  4*100, 50*100, CT_TOURNAMENT,	RT_PRO1, // $50 ($4)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7,		2,	 8,  8*100,100*100, CT_TOURNAMENT,	RT_PRO1, // $100 ($8)

		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7_HI_LO,	2,	 8,  2*100, 20*100, CT_TOURNAMENT,	RT_PRO1, // $20 ($2)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7_HI_LO,	2,	 8,  4*100, 50*100, CT_TOURNAMENT,	RT_PRO1, // $50 ($4)
		DISPLAY_TAB_TOURNAMENT,	GAME_RULES_STUD7_HI_LO,	2,	 8,  8*100,100*100, CT_TOURNAMENT,	RT_PRO1, // $100 ($8)
	  #endif

		(ClientDisplayTabIndex)0,(GameRules)0,0,0,0,0,CT_NONE,RT_NONE	// mark end of list.
	};


	// Count the number of tables of each type
	struct TableType *tt = StandardTableTypes;

	active_tables_temp = 0;			// reset each time - this is for stat purposes ONLY!
	active_real_tables_temp = 0;	// reset each time - this is for stat purposes ONLY!

	while (tt->max_players) {	// loop until we get to the end of the array
		int empty_tables = 0;
		int table_count = 0;
		int desired_max_tables = 1000000;	// something large
		// If there is a restriction on the number of tables of this type,
		// (judging by the game disable bits), then figure it out here.
		if (tt->game_disable_bits) {
			int bitnum = 0;
			// find the first bit that's set....
			while (bitnum < 32 && !(tt->game_disable_bits & (1<<bitnum))) {
				bitnum++;
			}
			if (bitnum <= 7 && MaxTables_GDB[bitnum]) {
				desired_max_tables = MaxTables_GDB[bitnum];
			}
		}

		//EnterCriticalSection(&CardRoomCritSec);
		//EnterCriticalSection(&TableListCritSec);
		CountEmptyTables(tt, &table_count, &empty_tables);

	  #if 0	// 2022 kriskoin
		static int tested = FALSE;
		if (!tested) {
			tested = TRUE;
			kp(("%s(%d) ---- Here's a sample from calling PrintOwnedCriticalSections() ----\n", _FL));
			PrintOwnedCriticalSections();
			kp(("%s(%d) ---- End of PrintOwnedCriticalSections() sample ----\n", _FL));
		}
	  #endif

		int time_for_a_new_table = FALSE;
		int desired_empty_tables = 1;	// usually we want 1 empty table.
		if (tt->chip_type==CT_TOURNAMENT) {
			// See how long the waiting list is for this type of table.
			// For every 20 eligible players, add one new empty table.
			struct WaitListEntry input_wle;
			zstruct(input_wle);
			input_wle.desired_stakes = tt->big_blind;
			input_wle.chip_type = tt->chip_type;
			input_wle.game_rules = tt->game_rules;
			int willing_players = waiting_lists[tt->client_display_tab_index].CountPotentialPlayers(&input_wle, MAX_PLAYERS_PER_GAME);
			int extra_tables = willing_players / 20;
		  #if WIN32	// 2022 kriskoin
			if (willing_players) {
				kp(("%s(%d) %d Tournament has %d willing players on waiting lists, adding %d extra empty tables\n",
						_FL, tt->big_blind, willing_players, extra_tables));
			}
		  #endif
			desired_empty_tables += extra_tables;

		}
		if (empty_tables < desired_empty_tables) {	// enough empty tables?
			// nope, time for a new one.
			time_for_a_new_table = TRUE;
		}

	  #if 0	// 2022 kriskoin
		if (SecondCounter < 4 && table_count < 5) {
			kp(("%s(%d) WARNING: CREATING LOTS OF EXTRA TABLES !!!\n", _FL));
			time_for_a_new_table = TRUE;
		}
	  #endif

		// If this is play money, make sure there are at least n
		// tables open (as long as we've been up for at least 30 seconds)
		if (tt->chip_type == CT_PLAY && SecondCounter >= 3) {
			#define DESIRED_MIN_PLAY_TABLES		3
			if (table_count < DESIRED_MIN_PLAY_TABLES) {
				//kp(("%s(%d) Need another play money table (only %d).\n", _FL, table_count));
				time_for_a_new_table = TRUE;
			} else if (table_count <= DESIRED_MIN_PLAY_TABLES) {
				kill_table = 0;
			}
		}


		// If this table type is supposed to be closed, don't open
		// a new table of this type.
		if (tt->game_disable_bits & GameCloseBits) {

			time_for_a_new_table = FALSE;
		}

	  #if 0	// 2022 kriskoin

		if (tt->chip_type==CT_TOURNAMENT && tt->big_blind==500) {
			kp(("%s(%d) table_count = %d, desired_max_tables = %d, empty_tables = %d, time_for_a_new_table = %d\n",
					_FL, table_count, desired_max_tables, empty_tables, time_for_a_new_table));
		}
	  #endif

		// If there is a restriction on the number of tables of this type,
		// (judging by the game disable bits), then impose it here.
		if (table_count >= desired_max_tables) {

			time_for_a_new_table = FALSE;
		}

		// If any tables need deleting (we know this after CountEmptyTables())
		// then we should delete them now.  We only catch one table per
		// iteration, but they take so long to die anyway, we don't care.
		if (kill_table && !time_for_a_new_table) {
			//kp(("%s(%d) Deleting table %d.\n", _FL, kill_table));
			DeleteTable(kill_table);
			kill_table = 0;
		}

		if (time_for_a_new_table) {
			// Time for a new table of this type.
			// Impose a maximum number of tables for both real and play money
			// (two different limits).
			int create_flag = TRUE;
			if (tt->chip_type == CT_REAL) {
				if (iRealMoneyTableCount >= MaxRealTableCount) {
					kp1(("%s(%d) Reached max # of real money tables (%d).  No more will be created.\n",_FL,iRealMoneyTableCount));
					create_flag = FALSE;
				}
			} else if (tt->chip_type == CT_PLAY) {
				if (iPlayMoneyTableCount >= MaxPlayTableCount) {
					kp1(("%s(%d) Reached max # of play money tables (%d).  No more will be created.\n",_FL,iPlayMoneyTableCount));
					create_flag = FALSE;
				}
			} else if (tt->chip_type == CT_TOURNAMENT) {

				if (iTournamentTableCount >= MaxTournamentTableCount) {
					kp1(("%s(%d) Reached max # of tournament tables (%d).  No more will be created.\n",_FL,iTournamentTableCount));
					create_flag = FALSE;
				}
				if (!iTournamentTableCreationAllowed) {	// not allowed to create more tournaments?
					// nope, tournaments are closed.
					create_flag = FALSE;
				}
			}

			if (create_flag) {
				pr(("%s(%d) Time for a new table: type = %d, max_players = %d, $%d/$%d\n",
							_FL, tt->game_rules, tt->max_players, tt->big_blind, tt->big_blind*2));
			  #if 1	//kriskoin: 				int add_computer_players = FALSE;
			   #if 0	//	Add them to play money tables only?
				if (!tt->real_money_flag)
			   #endif
				{
					static int added_computer_players_for_display_tab[MAX_CLIENT_DISPLAY_TABS];
					if (!added_computer_players_for_display_tab[tt->client_display_tab_index]) {
						add_computer_players = table_count ? FALSE : TRUE;
						added_computer_players_for_display_tab[tt->client_display_tab_index] = add_computer_players;
					}//if
				}
			  #endif
			  #if LOTS_OF_COMPUTER_PLAYERS	//!!! temp for screen shots
          #if 1
             add_computer_players = TRUE;
          #else
    			  if (tt->client_display_tab_index==CLIENT_TAB_HOLDEM)
    				{
    				  #if 1	//kriskoin:     					add_computer_players = TRUE;
    				  #else	// add computer players to first two tables of each type.
    					add_computer_players = table_count < 2;
    				  #endif
    					kp1(("%s(%d) Adding computer players at many tables\n",_FL));
    				}
          #endif
			  #endif
			  #if LOTS_OF_TOURNAMENT_COMPUTER_PLAYERS
				if (tt->chip_type==CT_TOURNAMENT)
				{
				  #if 1	//kriskoin: 					add_computer_players = TRUE;
				  #else	// add computer players to first two tables of each type.
					add_computer_players = table_count < 2;
				  #endif
					kp1(("%s(%d) Adding computer players at many tournament tables\n",_FL));
				} //if
			  #endif
				AddNewTable(tt->client_display_tab_index, tt->game_rules, tt->max_players, tt->small_blind,
						tt->big_blind, NULL, add_computer_players,
						tt->chip_type, tt->game_disable_bits, tt->rake_profile);

				if (DebugFilterLevel <= 0) {
					if (add_computer_players && iTotalComputerPlayersAdded >= 200) {
						kp(("%s(%d) Total computer players added so far = %d\n", _FL, iTotalComputerPlayersAdded));
					}//if
				}//if
			} //
		}

		//LeaveCriticalSection(&TableListCritSec);
		//LeaveCriticalSection(&CardRoomCritSec);
		tt++;
	}
	//kriskoin: 	// variable table counts with the final number (rather than the variable we were
	// counting with).

	active_tables = active_tables_temp;
	active_real_tables = active_real_tables_temp;

	// If we've been asked to shut down after all games are completed,
	// check for that situation now.
	if (iShutdownAfterGamesCompletedFlag) {
		int playing_table_found = FALSE;
		//EnterCriticalSection(&TableListCritSec);
		for (int i=0; i < table_count; i++) {
			// if this is a tournament table, be sure it's in the TTS_FINISHED state
			// (or waiting to start)
			if (tables[i]->chip_type == CT_TOURNAMENT) {

				if (tables[i]->table_tourn_state != TTS_FINISHED &&
					tables[i]->table_tourn_state != TTS_WAITING)
				{	// not done yet
					playing_table_found = TRUE;
					break;
				}
			} else {	// regular table, check for a game object
				if (tables[i]->game) {
					playing_table_found = TRUE;
					break;
				}
			}
		}
		//LeaveCriticalSection(&TableListCritSec);
		if (!playing_table_found) {
			// There aren't any tables playing... shut down now.
			SetRunLevelDesired(RUNLEVEL_EXIT, "Tables are finished playing.");
		}
	}

  #if WIN32 && 0	// testing only
	IssueCriticalAlert("CardRoom::ManageTables() is issuing ALERT_10's for testing.");
  #endif
}

/**********************************************************************************
 Function CardRoom::ProcessPlayerJoinTableRequest
 date: 24/01/01 kriskoin Purpose: deal with a JoinTable request that's been queued for this player
***********************************************************************************/
void CardRoom::ProcessPlayerJoinTableRequest(struct CardRoom_JoinTable *jt, Player *p, int *work_was_done_flag)

{
	// Now we've got one out of the list...
	Table *t = TableSerialNumToTablePtr(jt->table_serial_number);
	if (!t) {
		// They asked to join a table which does not exist.
		if (DebugFilterLevel <= 0) {
			kp(("%s %s(%d) %s tried to join/leave table %d but it no longer exists.\n",
					TimeStr(), _FL, p->user_id, jt->table_serial_number));
		}
		// Send the most recent table lists to the player (they are
		// clearly out of date).
		for (int i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
			p->requesting_table_summary_list[i] = TRUE;	// flag that we want it sent.
		}
		if (jt->status != JTS_UNJOIN) {
			p->SendMiscClientMessage(MISC_MESSAGE_GHOST,0,0,0,0,0,
					"Sorry, that table no longer exists.  Please select another one.");
		}
		return;	// just move on to the next player.
	}
	// 24/01/01 kriskoin:
	// when the tournament starts
	int tournament_table = (t->chip_type == CT_TOURNAMENT);
	// there are no rebuys supported (yet?) in tournaments

	if (tournament_table && jt->status==JTS_REBUY) {
		Error(ERR_ERROR, "%s(%d) Error: $%08lx tried to rebuy in a tournament (hack?)", _FL, p->player_id);
		return;	// do nothing
	}

	// Do some of the validation that used to be done by the player object but had to be moved here due to
	// critical section ordering problems.

	// verified he's logged in
	if (ANONYMOUS_PLAYER(p->player_id) && (jt->status==JTS_JOIN || jt->status==JTS_REBUY) ) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Error: an anonymous player tried to sit down (%s)", _FL, p->ip_str);
		return;	// just move on to the next player.
	}

	// 24/01/01 kriskoin:
	// seating position to be the seat we're already sitting in.
	int current_seating_position = 0;
	if (t->CheckIfPlayerSeated(p->player_id, &current_seating_position)) {
		jt->seating_position = (BYTE8)current_seating_position;
	}

	// If this is a re-buy, only process it between hands
	if (jt->status==JTS_REBUY && t &&
				t->IsPlayerInvolvedInHand(p->player_id)) {
		// They're involved in a hand!  Can't do it right now.
		// Add the request back to the END of the join table request queue
		EnterCriticalSection(&(p->PlayerCritSec));
		if (p->pending_joins_count < MAX_GAMES_PER_PLAYER) {
			p->pending_joins[p->pending_joins_count++] = *jt;
		} else {
			Error(ERR_ERROR, "%s(%d) Could not re-queue rebuy request for player '%s'", _FL, p->user_id);
		}
		LeaveCriticalSection(&(p->PlayerCritSec));
		//kp(("%s(%d) Delaying re-buy until player '%s' is no longer in the hand.\n", _FL, p->user_id));
		return;	// just move on to the next player.
	}

	int watching_flag = FALSE;
	int joined_state = JTS_UNJOIN;
	if (p->VerifyJoinedTable(jt->table_serial_number, &watching_flag) == ERR_NONE) {
		joined_state = JTS_JOIN;
		
	}
	if (joined_state!=JTS_UNJOIN && watching_flag) {
		joined_state = JTS_WATCH;	// we're joined and watching, not playing.
	}

	// in case this is a hacked packet, validate the buy-in chips

	// sanity check -- if somehow a screwy number got sent to us, we'll range-check it
	int chips = 0;	// by default, he's not going to sit down
	if ((jt->status == JTS_JOIN || jt->status==JTS_REBUY)) {
		// 24/01/01 kriskoin:
		// to that no matter what we were sent
		ChipType chip_type_to_fetch = t->chip_type;
		if (tournament_table) {
			chips = t->initial_small_blind_amount + t->initial_big_blind_amount;
			chip_type_to_fetch = CT_REAL;	// tournaments buy in with real chips
		} else {
			chips = jt->buy_in_chips;
			// trim pennies (though they shouldn't be in there by this point)
			chips -= chips % 100;
			// range check in case of wierdness
			chips = max(0, chips);	// can't be less than 0
		}
		// if he's hacked the client cleverly enough to spoof everything

		// to this point...
		int player_chips_in_bank = SDB->GetChipsInBankForPlayerID(p->player_id, chip_type_to_fetch);
		if ((int)chips > player_chips_in_bank) {
			// This happens if he had a pending amount waiting for the end of the hand to arrive, 
			// and then bought in somewhere else...
			if (DebugFilterLevel <= 12) {
				// We decided that this error is not important enough to worry about
				// right now.  It appears to only happen if a client is pounding on rebuy
				// during a hand or doing something odd when sitting down for the first time.
				// Since we have not had a single complaint in the last year, we're simply
				// turning off this error message.  The issue is still open, however.
				char curr_str1[MAX_CURRENCY_STRING_LEN];
				char curr_str2[MAX_CURRENCY_STRING_LEN];
				zstruct(curr_str1);
				zstruct(curr_str2);
				Error(ERR_ERROR, "%s(%d) %08lx buyin request was for more chips than he has available."
								 " requested=%8s, in bank=%8s",
						_FL, p->player_id,
						CurrencyString(curr_str1, chips, chip_type_to_fetch, TRUE),
						CurrencyString(curr_str2, player_chips_in_bank, chip_type_to_fetch, TRUE));
				kp(("%s %s(%d) %08lx buyin details: original request was for %8s, current JTS_* state=%d\n",
						TimeStr(), _FL, p->player_id,
						CurrencyString(curr_str1, jt->buy_in_chips, chip_type_to_fetch, TRUE),
						joined_state));
				kp(("%s %s(%d) %08lx buyin details: jt->status=%d, chiptype=%d, %s\n",
						TimeStr(), _FL, p->player_id,
						jt->status, 
						chip_type_to_fetch,
						tournament_table ? "tourney" : "not tourney"));
				kp(("%s %s(%d) %08lx buyin details: amount currently in play (everywhere): %8s, tables windows open = %d\n",
						TimeStr(), _FL, p->player_id,
						CurrencyString(curr_str1, chip_type_to_fetch==CT_PLAY ? p->FakeInPlay : p->RealInPlay, chip_type_to_fetch, TRUE),
						p->JoinedTableCount));
				if (jt->seating_position <= MAX_PLAYERS_PER_GAME) {
					kp(("%s %s(%d) %08lx buyin details: amount currently at seat %d of table:   %8s\n",
							TimeStr(), _FL, p->player_id, jt->seating_position,

							CurrencyString(curr_str1, t->table_info.players[jt->seating_position].chips, chip_type_to_fetch, TRUE)));
				}
				kp(("%s %s(%d) %08lx current socket was inherited %ds ago.\n",
						TimeStr(), _FL, p->player_id, SecondCounter - p->socket_set_time));
				kp(("%s %s(%d) %08lx Completely ignoring this buyin request. Client will get no response!\n",
						TimeStr(), _FL, p->player_id));
			} 
			return;	// just move on to the next player.
		} 
	} 

	jt->buy_in_chips = chips;

	ErrorType err;
	// JTS_UNJOIN, JTS_JOIN, JTS_WATCH, JTS_REBUY
	// old way: 0      1         2          3

	if (jt->status == JTS_UNJOIN) {
		// Unjoin...
		if (joined_state) {
		  #if 1	// 2022 kriskoin
			//kriskoin: 		  #if WIN32 && 0
			kp(("%s %s(%d) JTS_UNJOIN: t->game = $%08lx, t->bad_beat_payout_stage = %d, t->special_hand_payout_stage = %d\n",
					TimeStr(), _FL, t->game, t->bad_beat_payout_stage, t->special_hand_payout_stage));
		  #endif
			if (!watching_flag && (t->game || t->bad_beat_payout_stage || t->special_hand_payout_stage))
			{
				// Can't do it right now.
				// Add the request back to the END of the join table request queue
			  #if WIN32 && 0
				kp(("%s %s(%d) player $%08lx cannot leave the table yet.  requeuing unjoin request.\n",
							TimeStr(), _FL, p->player_id));
			  #endif
				EnterCriticalSection(&(p->PlayerCritSec));
				if (p->pending_joins_count < MAX_GAMES_PER_PLAYER) {
					p->pending_joins[p->pending_joins_count++] = *jt;
				} else {
					Error(ERR_ERROR, "%s(%d) Could not re-queue unjoin request for player '%s'", _FL, p->user_id);
				}
				LeaveCriticalSection(&(p->PlayerCritSec));
				return;
			}
		  #endif
			// watching or there is no current game... remove them.
			err = t->RemovePlayerFromTable(p);
			if (err != ERR_NONE) {
				// Perhaps he was watching...
				err = t->RemoveWatchingPlayer(p);
				if (err != ERR_NONE) {
					Error(ERR_ERROR, "%s(%d) Attempt to unjoin player $%08lx from table %d but we can't seem to find him.",
							_FL, p->player_id, jt->table_serial_number);
				}
			}
		} else {
			kp(("%s %s(%d) %s Unjoin: Player %06lx (%s) is not joined to table %d\n",
				TimeStr(), _FL, p->ip_str, p->player_id, p->user_id, jt->table_serial_number));
		}
	} else if (jt->status == JTS_JOIN) {
		// Join...
		if (!joined_state || joined_state == JTS_WATCH) {
			// If the table has a waiting list, make sure this is
			// the player who is allowed to join.
			int joining_allowed = TRUE;
			if (t->summary_info.flags & TSIF_WAIT_LIST_REQUIRED) {
				if (!t->CheckIfCalledPlayer(p->player_id)) {
					// They're NOT being called and a waiting list is
					// still required.
					if (!(jt->flags & JOINTABLE_FLAG_AUTOJOIN)) {	// not computer autojoin request?
						if (DebugFilterLevel <= 0) {
							kp(("%s(%d) Player $%08lx (%s) tried to join table %s but he's not at front of wait list queue ($%08lx is)\n",
									_FL, p->player_id, p->user_id, t->summary_info.table_name, t->wait_list_called_players[0].player_id));
						}
					}
					joining_allowed = FALSE;
				}
			}

			// Trying to sit at a tournament table?
			if (joining_allowed && t->chip_type==CT_TOURNAMENT) {
				// Check if they are seated at any other tournament tables.
				// If so, joining is not allowed.
				if (p->tournament_table_serial_number && 
					p->tournament_table_serial_number != t->table_serial_number)
				{
					// It seems they're already seated at a tournament table.
					joining_allowed = FALSE;
					p->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
						jt->table_serial_number,	0, 0, 0, 0,
						"You are limited to playing in one tournament at a time.\n"
						"\n"
						"If you wish to join this table you must first finish the\n"
						"tournament you are already in.");
				}
			}

			//kriskoin: 			// are not allowed to sit at this one (a 3rd)
			if (joining_allowed && p->CountSeatedTables() >= 2) {
				joining_allowed = FALSE;
				p->SendMiscClientMessage(MISC_MESSAGE_UNSPECIFIED,
						jt->table_serial_number,	0, 0, 0, 0,
						"You are limited to playing two tables at once.\n"
						"If you wish to join this table you must give\n"
						"up a seat at one of your other tables.");
			}
			// 24/01/01 kriskoin:
			if (joining_allowed && jt->buy_in_chips) {
				pr(("%s(%d) Joining player $%08lx to table %d with %d chip buy-in.\n",_FL, p->player_id, jt->table_serial_number, jt->buy_in_chips));
				err = t->AddPlayer(p, (char)jt->seating_position, jt->buy_in_chips);
				// set the minimum allowed to bring to the table to $1
				if (err == ERR_NONE) { // make sure he's actually there!
					t->SetMinimumBuyinAllowedForPlayer(jt->seating_position, 100);
				}

				if (t->CheckIfCalledPlayer(p->player_id)) {	// were we waiting for him?
					// A player has just been seated at a table that
					// was waiting for him to sit down.  Update the waiting
					// list stuff now.
					// Also remove him from the waiting list.
					pr(("%s(%d) Player $%08lx has joined table %s (which was waiting for him).  Removing him from list.\n",
							_FL, t->p->player_id, t->summary_info.table_name));
					struct WaitListEntry wle;
					zstruct(wle);
					wle.table_serial_number = t->table_serial_number;
					wle.desired_stakes = t->initial_big_blind_amount;
					wle.chip_type = t->chip_type;
					wle.game_rules = t->game_rules;
					wle.player_id = p->player_id;
					waiting_lists[t->client_display_tab_index].RemoveWaitListEntry(&wle);

					update_waiting_list[t->client_display_tab_index] = TRUE;
					t->RemoveCalledPlayer(p->player_id);

					//kriskoin: 					// SeatAvail structures for this table.
					struct CardRoom_SeatAvail sa;
					zstruct(sa);
					sa.table_serial_number = t->table_serial_number;
					p->SendSeatAvail(&sa);

				}
			} else {
				// Make sure the client is up-to-date with the status.
				p->SendAllTableData(t->table_serial_number);
			}
		} else {
			pr(("%s(%d) Player $%08lx is already joined to table %d\n", _FL, p->player_id, jt->table_serial_number));
			// Perhaps the client forgot they were joined... tell
			// them again by sending DATATYPE_CARDROOM_JOIN_TABLE.
			p->SendAllTableData(t->table_serial_number);
		}
	} else if (jt->status == JTS_WATCH) {
		// Watch...
		if (!joined_state) {
			pr(("%s(%d) Adding Watching player $%08lx to table %d.\n",_FL, p->player_id, jt->table_serial_number));
			t->AddWatchingPlayer(p);
		} else {
			pr(("%s(%d) Player $%08lx is already joined to table %d\n", _FL, p->player_id, jt->table_serial_number));
			// Perhaps the client forgot they were joined... tell
			// them again by sending DATATYPE_CARDROOM_JOIN_TABLE.
			p->SendAllTableData(t->table_serial_number);
		}
	} else if (jt->status == JTS_REBUY) {	// buy more chips
		if (joined_state != JTS_JOIN) {	// trying to rebuy, but not sitting here?
			Error(ERR_ERROR, "%s(%d) player ($%08lx) tried to re-buy %d chips, but he's not joined to table (%d)",
				_FL, p->player_id, jt->buy_in_chips, jt->table_serial_number);
		} else {
			// ok to process the rebuy
			ErrorType rc = t->AddMoreChipsForPlayer(p, jt->seating_position, jt->buy_in_chips);
			// set the minimum allowed to bring to the table to $1
			if (rc == ERR_NONE) { // validly added chips?
				t->SetMinimumBuyinAllowedForPlayer(jt->seating_position, 100);
			}
		}
	} else {
		Error(ERR_ERROR, "%s(%d) Unknown status (%d) in UpdatePlayers for player ($%08lx)",
			_FL, jt->status, p->player_id);
	}
	SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
}

//****************************************************************
// 
//
// Handle any processing for individual players, such as joining
// and unjoining games, etc.  Main cardroom thread.
// This function handles the work which is very latency sensitive.
//
void CardRoom::UpdatePlayers_LowLatencyWork(int *work_was_done_flag)
{
	WORD32 now = GetTickCount();
	for (int i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (!p || !p->player_id) {
			continue;	// move on to next player
		}

		//kriskoin: 		// indicate to the table that it should be processed immediately.
		// We do this by setting the table's next update time to 0 (asap).
		if (p->input_result_ready_table_serial_number) {
			Table *t = TableSerialNumToTablePtr(p->input_result_ready_table_serial_number);
			if (t) {

				t->time_of_next_update = 0;	// update table asap

			}
			p->input_result_ready_table_serial_number = 0;
		}

		// If the critsec for this player is owned by someone else already, don't
		// update them right now.
		if (p->PlayerCritSec.owner_nest_count && now - p->last_low_latency_update_processing_ticks < 2000) {
			// Someone else owns it and he was already processed recently.
			continue;	// move on to next player
		}
		p->last_low_latency_update_processing_ticks = now;

		// If the player is waiting for table summary information to be sent to
		// them, send it out now.
		// First, check for any game summary lists
		for (int j=0 ; j<MAX_CLIENT_DISPLAY_TABS ; j++) {
			if (p->requesting_table_summary_list[j]) {
				//kp(("%s(%d) Sending game summary list[%d] to player $%08lx\n", _FL, p->player_id, j));
				SendSummaryListToPlayer((ClientDisplayTabIndex)j, p);
				p->requesting_table_summary_list[j] = FALSE;
				SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
			}
		}
		// Next, check for any table info structures...
		if (p->table_info_request_1) {
			SendTableInfoToPlayer(p->table_info_request_1, p);
			p->table_info_request_1 = 0;
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
		}
		if (p->table_info_request_2) {
			SendTableInfoToPlayer(p->table_info_request_2, p);
			p->table_info_request_2 = 0;
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
		}

		// Handle any pending chat in the queue from this player.
		if (p->player_id && p->chat_queue_head != p->chat_queue_tail) {
			// pass a queued chat message on to its table.
			struct GameChatMessage *gcm = &p->chat_queue[p->chat_queue_tail];
			if (!gcm->table_serial_number) {	// it's an admin chat msg (posted by Dealer)
				// 20:::				SendDealerMessageToAllTables(gcm->message, CHATTEXT_ADMIN, gcm->flags);
				gcm->text_type = ALERT_9;
				char str[MAX_CHAT_MSG_LEN+30];
				strcpy(str, "BROADCAST: ");
				strcat(str, gcm->message);
				strnncpy(gcm->message, str, MAX_CHAT_MSG_LEN);
				SendAdminChatMonitor(gcm, "BROADCAST", CT_NONE);
			} else {
				Table *t = TableSerialNumToTablePtr(gcm->table_serial_number);
				if (t) {
					t->SendChatMessage(gcm->name, gcm->message, gcm->text_type);
				}
			}
			p->chat_queue_tail = (p->chat_queue_tail+1) % PLAYER_CHAT_QUEUE_LEN;
		}
	}	// bottom of "for all players" loop.

	WORD32 elapsed = GetTickCount() - now;
	if (elapsed >= 2500) {
		kp(("%s %s(%d) Warning: UpdatePlayers_LowLatencyWork() took %.2fs to execute.\n", TimeStr(), _FL, elapsed/1000.0));
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Handle any processing for individual players, such as joining
// and unjoining games, etc.  Main cardroom thread.
// This function handles the work which is NOT very latency sensitive.
//
void CardRoom::UpdatePlayers_HighLatencyWork(int *work_was_done_flag)
{
	WORD32 now = GetTickCount();
	WORD32 tmp_client_count_newer = 0;	// # of clients connected with newer than current version
	WORD32 tmp_client_count_current = 0;// # of clients connected with the current version
	WORD32 tmp_client_count_old = 0;	// # of clients connected with old version

	for (int i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (!p || !p->player_id) {
			continue;	// move on to next player
		}

		if (p->client_version_number.build < ServerVersionInfo.new_client_version.build) {
			tmp_client_count_old++;
		} else if (p->client_version_number.build == ServerVersionInfo.new_client_version.build) {
			tmp_client_count_current++;
		} else {
			tmp_client_count_newer++;
		}

		// If the critsec for this player is owned by someone else already, don't
		// update them right now.
		if (p->PlayerCritSec.owner_nest_count && now - p->last_high_latency_update_processing_ticks < 1000) {
			// Someone else owns it and he was already processed recently.
			continue;	// move on to next player
		}
		p->last_high_latency_update_processing_ticks = now;

		if (p->pending_joins_count) {
			// process any pending joins...
			int max_loop_count = p->pending_joins_count;

			// Loop, but never forever because sometimes the request gets requeued
			// at the end of the array (for example if it couldn't be processed immediately)
			for (int i=0 ; i<max_loop_count && p->pending_joins_count ; i++) {
				// Pop the bottom one off the list and process it.
				//kp(("%s(%d) p->pending_joins_count = %d\n",_FL, p->pending_joins_count));
				EnterCriticalSection(&(p->PlayerCritSec));
				struct CardRoom_JoinTable jt = p->pending_joins[0];
				p->pending_joins_count--;	// one fewer in the array
				if (p->pending_joins_count) {
					// shift remaining list down to fill hole.
					memmove(&p->pending_joins[0], &p->pending_joins[1],
							sizeof(p->pending_joins[0])*p->pending_joins_count);
				}
				LeaveCriticalSection(&(p->PlayerCritSec));
				// 24/01/01 kriskoin:
				ProcessPlayerJoinTableRequest(&jt, p, work_was_done_flag);
			}
		}
		// Add/remove them from any waiting lists...
		if (p->pending_waitlist_count) {

			EnterCriticalSection(&(p->PlayerCritSec));
			// process any pending waiting list requests...
			p->pending_waitlist_count--;
			struct CardRoom_JoinWaitList jwl = p->pending_waitlists[p->pending_joins_count];
			LeaveCriticalSection(&(p->PlayerCritSec));

			//kriskoin: 			if (jwl.chip_type==CT_TOURNAMENT) {
				jwl.table_serial_number = 0;	// never allow joining a single table's list.
			}

			struct WaitListEntry wle;
			ClientDisplayTabIndex client_display_tab_index = (ClientDisplayTabIndex)jwl.client_display_tab_index;
			zstruct(wle);
			wle.table_serial_number = jwl.table_serial_number;
			wle.desired_stakes = jwl.desired_stakes;
			//kriskoin: 			// max-2 (6 for stud, 8 for holdem and omaha).
			// If one on one, don't change the cap.
			static BYTE8 max_min_players_for_display_tab[MAX_CLIENT_DISPLAY_TABS] = {8,8,6,2,8,6,10};
			wle.min_players_required = min(jwl.min_players_required,max_min_players_for_display_tab[client_display_tab_index]);
			pr(("%s(%d) wle.min_players_required = %d\n",_FL,wle.min_players_required));

			wle.player_id = p->player_id;

			wle.chip_type = (ChipType)jwl.chip_type;
			wle.game_rules = (GameRules)jwl.game_rules;
			if (jwl.table_serial_number) {
				Table *t = TableSerialNumToTablePtr(jwl.table_serial_number);
				if (t) {
					client_display_tab_index = t->client_display_tab_index;
				}
			}
			pr(("%s(%d) client_display_tab_index = %d\n", _FL, client_display_tab_index));
			p->requesting_table_summary_list[client_display_tab_index] = TRUE;	// flag that we want the summary list sent asap.
			pr(("%s(%d) Processing JoinWaitList: table = %d, client_display_tab_index = %d, status = %d chip_type = %d\n",
						_FL,
						jwl.table_serial_number,
						jwl.client_display_tab_index,
						jwl.status,
						jwl.chip_type));

			if (jwl.status) {	// add them
				// Make sure they're not already joined to this table (playing).
				int add_allowed = TRUE;
				if (jwl.table_serial_number) {	// request is for a specific table
					int watching_flag = 0;
					if (p->VerifyJoinedTable(jwl.table_serial_number, &watching_flag)==ERR_NONE)  {
						// We're joined to this particular table already.
						// If we're just watching, that's OK.
						if (!watching_flag) {
							add_allowed = FALSE;

							if (DebugFilterLevel <= 12) {
								//kriskoin: 								// only seems to happen when a player is experiencing some kind of
								// login problem.  Since they seem harmless and nobody has complained
								// about this problem recently, we'll just turn off the error message display.
								// Note that the problem is NOT fixed, we've just turned off the error message.
								kp(("%s %s(%d) Player %s ($%08lx) tried to get on waitlist for table %d but he's already playing there.\n",
											TimeStr(), _FL, p->user_id, p->player_id, jwl.table_serial_number));
								EnterCriticalSection(&p->PlayerCritSec);
								for (int i=0 ; i<p->JoinedTableCount ; i++) {
									if (p->JoinedTables[i].table_serial_number == jwl.table_serial_number) {
										// Found it.
										kp(("%s %s(%d) Joined table %ds ago, table %s, joined tables = %d, socket given to us %ds ago\n",
												TimeStr(), _FL,
												SecondCounter - p->JoinedTables[i].joined_time,

												p->JoinedTables[i].table_name,
												p->JoinedTableCount,
												SecondCounter - p->socket_set_time));
										break;
									}
								}
								LeaveCriticalSection(&p->PlayerCritSec);
							}
						}
					}
				} else {
					// Request for 'any' table of a certain stakes.
					// If they're playing at any tables of this type with
					// the same stakes, they're not allowed to join the
					// waiting list again.  Playing at one table of each
					// stakes amount is good enough.
					// The only way to filter this is to search through the
					// tables he's playing at and compare the stakes.
					for (int i=0 ; i<p->JoinedTableCount ; i++) {
						if (!p->JoinedTables[i].watching_flag) {
							// We're playing at this table (not watching)
							Table *t = TableSerialNumToTablePtr(p->JoinedTables[i].table_serial_number);
							if (t) {
								if (t->client_display_tab_index == jwl.client_display_tab_index &&
									t->initial_big_blind_amount == jwl.desired_stakes &&
									t->chip_type == jwl.chip_type &&
									t->game_rules == jwl.game_rules &&
									t->chip_type != CT_TOURNAMENT)
								{
									// This looks like a match.  Don't let them play.
									add_allowed = FALSE;
								  #if 0	//kriskoin: 									Error(ERR_ERROR, "%s(%d) Player $%08lx tried to get on waitlist for a $%d/$%d table but he's already playing on one (%d).",
											_FL, p->player_id, jwl.desired_stakes, jwl.desired_stakes*2,
											t->table_serial_number);
								  #endif
									break;	// no point searching further.
								}
							}
						}
					}
				}
				if (add_allowed) {
					waiting_lists[client_display_tab_index].AddWaitListEntry(&wle);
				}
			} else {	// remove them
				pr(("%s(%d) Removing player $%08lx from waitlist for $%d/$%d (client_display_tab_index=%d, table=%d).\n",
							_FL, wle.player_id, wle.desired_stakes, wle.desired_stakes*2, client_display_tab_index, wle.table_serial_number));
				ErrorType err = waiting_lists[client_display_tab_index].RemoveWaitListEntry(&wle);
				if (err) {
					//kp(("%s(%d) Warning: waitlist removal failed! (possibly two requests in a row?)\n",_FL));
				}
				// We must also search for the table which might have
				// been waiting for them to respond.
				Table *t = TableSerialNumToTablePtr(wle.table_serial_number);
				if (t && t->CheckIfCalledPlayer(wle.player_id)) {
					t->RemoveCalledPlayer(wle.player_id);
					update_waiting_list[t->client_display_tab_index] = TRUE;	// force re-eval.
				}
				// Make sure the client has their SeatAvail cancelled (if any).
				struct CardRoom_SeatAvail sa;
				zstruct(sa);
				sa.table_serial_number = wle.table_serial_number;
				p->SendSeatAvail(&sa);
			}
			update_waiting_list[client_display_tab_index] = TRUE;	// always update after adding/removing.
			SetWorkWasDoneFlag(work_was_done_flag, "%s(%d)", _FL);
		}

		if (p->bar_snack_request_table) {
			Table *t = TableSerialNumToTablePtr(p->bar_snack_request_table);
			if (t) {
				t->SelectBarSnack(p->player_id, p->bar_snack_request);
			}
			p->bar_snack_request = BAR_SNACK_NONE;
			p->bar_snack_request_table = 0;
		}

		// If the client is subscribed to a table, check if it
		// has changed and send it if necessary.
		if (p->table_info_subscription) {
			Table *t = TableSerialNumToTablePtr(p->table_info_subscription);
			if (t) {
				if (t->table_info_changed) {	// if it changed, send it out.
					SendTableInfoToPlayer(p->table_info_subscription, p);
				}
			} else {
				// Subscription table no longer exists.  delete subscription.
				p->table_info_subscription = 0;
			}
		}

		// Handle any requests from this player to not send a tournament summary email
		if (p->suppress_tournament_email_serial_number) {
			WORD32 serial_num = p->suppress_tournament_email_serial_number;
			Table *t = TableSerialNumToTablePtr(serial_num);
			if (t) {
				t->SuppressTournSummaryEmail(p->player_id, serial_num);
			}
			p->suppress_tournament_email_serial_number = 0;
		}

		//kriskoin: 		if (p->admin_set_player_id_for_cc) {
			CCDB_SetMainAccountForCC(p->player_id, 	// player id of admin to notify of change
					p->admin_set_player_id_for_cc,	// player id to make active
					p->admin_set_cc_number);		// partial cc number
			p->admin_set_player_id_for_cc = 0;
		}
		if (p->admin_move_player_to_top_wait_lists) {
			MovePlayerToTopOfWaitLists(p->admin_move_player_to_top_wait_lists);
			p->admin_move_player_to_top_wait_lists = 0;
		}
		if (p->admin_set_max_accounts_for_cc) {
			CCDB_SetMaxAccountsAllowedForPlayerID(p->admin_set_max_accounts_for_cc,
												  p->admin_set_max_accounts_for_cc_count);
			p->admin_set_max_accounts_for_cc = 0;
		}

	  #if 0	// 2022 kriskoin
		if (!random(15) && !iRunningLiveFlag) {
			kp1(("%s(%d) *** WARNING: testing player object queue removal *** DO NOT LEAVE IT THIS WAY ***\n", _FL));
			PlrOut_RemovePacketsForPlayer(p);
		}
	  #endif
	}	// bottom of "for all players" loop.


	client_count_newer = tmp_client_count_newer;
	client_count_current = tmp_client_count_current;
	client_count_old = tmp_client_count_old;

	// Now that we've updated everyone's subscriptions, loop through
	// all tables and clear the table_info_changed flags.
	ClearTableInfoChangedFlags();



	// Handle any pending chat that needs to be sent to administrators
	while (admin_chat_queue_head != admin_chat_queue_tail) {
		// pass a queued chat message on to all administrators
		struct GameChatMessage *gcm = &admin_chat_queue[admin_chat_queue_tail];
		for (int i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p->priv >= ACCPRIV_CUSTOMER_SUPPORT && p->Connected()) {
				p->SendDataStructure(DATATYPE_CHAT_MSG, gcm, sizeof(*gcm));
			}
		}
		admin_chat_queue_tail = (admin_chat_queue_tail+1) % ADMIN_CHAT_QUEUE_LEN;
	}
}

//****************************************************************
// 
//

// If a disconnected player has been kicked off all his tables
// and waiting lists, delete the player object; it's no longer needed.
//
void CardRoom::DeleteDisconnectedPlayers(void)
{
	int recent_poor_connections = 0;
	int recent_new_connections = 0;
	int prev_active_good_connections = active_good_connections;
	EnterCriticalSection(&CardRoomCritSec);
	int new_active_player_connections = 0;
	int new_active_good_connections = 0;
	active_unseated_players = 0;
	connected_idle_players = 0;
	multi_seated_players = 0;
	for (int i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		int kill_it = FALSE;
		if (p) {
			if (p->Connected()) {
				// This player object is currently connected.
				new_active_player_connections++;
				// Determine if he is sitting at any tables (not just watching)
				int seated_tables = p->CountSeatedTables();
				if (seated_tables == 0) {
					// Increment either the unseated players count or the idle
					// players count depending on whether they seem to be there
					// or not.
					// If we haven't heard from them in 60s or more, assume
					// they're idle.
					if (p->TimeSinceLastContact() >= 60 || p->idle_flag) {
						connected_idle_players++;
					} else {
						active_unseated_players++;
					}
				} else if (seated_tables > 1) {	// sitting at more than one table?
					multi_seated_players++;
				}
				if (p->server_socket && SecondCounter - p->server_socket->time_of_connect <= 15) {
					// This is a fairly new socket.
					recent_new_connections++;
				}
			} else {
				if (!p->JoinedTableCount) {	// only kill things joined to NO tables.
					kill_it = TRUE;	// kill anything disconnected by default.
				}
			}
			if (p->CurrentConnectionState() != CONNECTION_STATE_GOOD) {
				// This player object is NOT well connected.
				// If we lost contact with them recently, increment the number

				// of people we've lost contact with recently.
				if (p->TimeSinceLastContact() < 60) {	// it was recent
					recent_poor_connections++;
				}
			} else {
				new_active_good_connections++;	// keep track of # of good connections
			}
		}
		if (kill_it) {
			// They're disconnected and not joined to any tables... check
			// if they're on any waiting lists.
			int killable = TRUE;
			for (int j=0 ; j<MAX_CLIENT_DISPLAY_TABS ; j++) {
				if (waiting_lists[j].CountPlayerEntries(p->player_id)) {
					// If they've been disconnected long enough, remove them
					// from the waiting list.
					//kp(("%s(%d) player $%08lx has been disconnected for %ds\n", _FL, p->player_id, p->DisconnectedSeconds()));
					if (p->DisconnectedSeconds() >= (int)WaitListDisconnectTimeout) {
						pr(("%s(%d) Disconnected player $%08lx is getting kicked off waiting list for being disconnected %ds\n", _FL, p->player_id, p->DisconnectedSeconds()));
						waiting_lists[j].RemoveAllEntriesForPlayer(p->player_id);	// delete all entries for player
						update_waiting_list[j] = TRUE;	// always update after adding/removing.

					} else {
						// Leave them on the waiting list.
						killable = FALSE;
					}
				}
			}

			if (killable) {
				// Delete this player object.
				pr(("%s(%d) Disconnected player %d (id=$%08lx, socket=$%08lx) is no longer joined to any tables or waiting lists.  Deleting Player object.\n", _FL, i, p->player_id, p->server_socket));
				EnterCriticalSection(&PlrInputCritSec);
				DeletePlayerObject((class Player **)&players[i]);
				// Scroll list down to accomodate.
				int k;
				for (k=i ; k<connected_players-1 ; k++) {
					players[k] = players[k+1];
				}
				connected_players--;
				players[k] = NULL;
				LeaveCriticalSection(&PlrInputCritSec);
				pr(("%s(%d) There are now %d players in the cardroom's player[] array.\n", _FL, connected_players));
			}
		}
	}

	active_good_connections = new_active_good_connections;	// copy it over now that it's finalized.

	// Keep the peak active connections over the last 3 minutes.
	if (SecondCounter - peak_active_player_connections_time > 3*60) {
		active_player_connections = new_active_player_connections;	// reset it so we start over.
	}
	if (dwLastWidespreadRoutingProblem &&
		dwEndWidespreadRoutingProblem < dwLastWidespreadRoutingProblem)
	{
		active_player_connections = new_active_player_connections;	// reset it so we start over.
	}
	if (new_active_player_connections >= active_player_connections) {
		// Met old peak or made a new one... keep track of it and the time.
		active_player_connections = new_active_player_connections;
		peak_active_player_connections_time = SecondCounter;
	}

	LeaveCriticalSection(&CardRoomCritSec);

	// Watch for widespread routing problems and keep track if necessary.
  #if WIN32 && 0	//19991005MB	// for testing only...
  	kp1(("%s(%d) DEBUG: VERY SENSITIVE ROUTING PROBLEM DETECTION ENABLED\n",_FL));
	if (recent_poor_connections && !dwLastWidespreadRoutingProblem)
  #else	// normal code...
	int min_poor_connections = max(4, active_player_connections/3);
	if (recent_poor_connections >= min_poor_connections &&
		SecondCounter - dwLastWidespreadRoutingProblem > 4*60 &&
		!iShutdownAfterGamesCompletedFlag &&
		!ServerVersionInfo.alternate_server_ip)
  #endif
	{
		//kp(("%s(%d) active_player_connections = %d\n", _FL, active_player_connections));
		//kp(("%s(%d) min_disconnects = %d\n", _FL, min_disconnects));
		kp(("%s **** Internet routing problem detected: %d recent poor connections (of %d players)\n",
					TimeStr(), recent_poor_connections, active_player_connections));
		dwLastWidespreadRoutingProblem = SecondCounter;
		dwEndWidespreadRoutingProblem = 0;
		iRoutingProblemPlayerCount = active_player_connections;	// keep track of what it was at start of routing problem
		iRoutingProblemPoorConnections = recent_poor_connections;
		iRoutingProblemPeakPoorConnections = recent_poor_connections;
		iRoutingProblemGoodConnections = active_player_connections;	// init to something
		active_player_connections = new_active_player_connections;	// reset it so we start over.
		next_routing_update_message = 0;
		if (!routing_update_email_fd
		  #if 0	// avoid sending routing problem emails during testing?
			&& iRunningLiveFlag
		  #endif
		) {
			MakeTempFName(routing_update_email_fname, "rp");
			routing_update_email_fd = fopen(routing_update_email_fname, "wt");
			if (routing_update_email_fd) {
				fprintf(routing_update_email_fd,
					"%s"
					"An internet routing problem appears to be over.\n"
		            "\n"
					"# of players online at time of problem: %d\n"
					"It started at %s CST with %d poor connections\n"
					"\n"
					"Here's the way it looked:\n"
					"\n"
					"%s --- Routing problem first detected ---\n",
					iRunningLiveFlag ? "" : "*** This is a test ***\n\n",
					iRoutingProblemPlayerCount,
					TimeStr(),
					iRoutingProblemPoorConnections,
					TimeStr()+6);
			}

		}
	  #if 0	// 2022 kriskoin
		for (int i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p) {
				kp(("%s %-15.15s %-15s Last contact: %3ds Overdue by %2ds Connected=%d\n",
					TimeStr(), p->ip_str, p->user_id[0] ? p->user_id : "anon",
					p->TimeSinceLastContact(),
					p->OverdueContactTime(),
					p->Connected()));
			}
		}
	  #endif
	}

	pr(("%s(%d) dwLastWidespreadRoutingProblem = %d\n", _FL, dwLastWidespreadRoutingProblem));
	pr(("%s(%d) active_good_connections = %d, prev_active_good_connections = %d\n",
			_FL, active_good_connections, prev_active_good_connections));
	// Check for the end of a routing problem.
	if (dwLastWidespreadRoutingProblem &&			// have we had a problem?
		(dwEndWidespreadRoutingProblem < dwLastWidespreadRoutingProblem ||	// was it an old problem?
		 SecondCounter - dwEndWidespreadRoutingProblem <= 5*60+5)			// even if it was old, do 5 minutes of data AFTER the problem is over
	) {
		// The routing problem is still occuring or it has recently ended.
		// Update the status email.
		int poor_connections = max(0, connected_players - active_good_connections);
		double poor_connection_percentage = connected_players ? (double)poor_connections / (double)connected_players * 100.0 : 100.0;

		int end_is_possible = TRUE;		// end of routing problem test enabled.
		if (dwEndWidespreadRoutingProblem >= dwLastWidespreadRoutingProblem) {
			// The end has already been detected.  No new end is possible.
			end_is_possible = FALSE;
		} else if (poor_connections > iRoutingProblemPeakPoorConnections) {
			iRoutingProblemPeakPoorConnections = poor_connections;	// set new high

			end_is_possible = FALSE;	// don't allow end testing.
		}

		prev_active_good_connections = iRoutingProblemGoodConnections;
		if (SecondCounter >= next_routing_update_message) {
			next_routing_update_message = SecondCounter + 15;
			kp(("%s Routing update:%5d plrs,%5d good,%5d poor (%2.0f%%),%4ds elapsed\n",
					TimeStr(), connected_players, active_good_connections,
					poor_connections, poor_connection_percentage,
					SecondCounter - dwLastWidespreadRoutingProblem));
			if (routing_update_email_fd) {
				fprintf(routing_update_email_fd,
					"%s (%+4ds) %5d plrs,%5d good,%5d poor (%2.0f%%)\n",
					TimeStr()+6,
					SecondCounter - dwLastWidespreadRoutingProblem,
					connected_players, active_good_connections,
					poor_connections, poor_connection_percentage);
			}
			iRoutingProblemGoodConnections = active_good_connections;
		}

		if (end_is_possible &&
			poor_connection_percentage <= 50.0 &&
			active_good_connections >= prev_active_good_connections+10)
		{
			// This looks like the end.
			dwEndWidespreadRoutingProblem = SecondCounter;
			kp(("%s **** Looks like the end of the internet routing problem. new good connections=%d (total good=%d)\n",
					TimeStr(), active_good_connections-prev_active_good_connections, active_good_connections));
			if (routing_update_email_fd) {
				fprintf(routing_update_email_fd,
					"%s --- End detected: new good connections=%d (total good=%d) ---\n",
					TimeStr()+6, active_good_connections-prev_active_good_connections, active_good_connections);
			}
		}
	} else {
		// Routing problem not occuring.
		if (routing_update_email_fd) {
			// Email file is still open... close it off and send it.
			long duration = dwEndWidespreadRoutingProblem - dwLastWidespreadRoutingProblem;
			fprintf(routing_update_email_fd,
					"%s --- Tracking finished ------\n",
					TimeStr()+6);
			fprintf(routing_update_email_fd,
					"\n"
					"It peaked at %d poor connections.\n"
					"\n"
					"Duration was %dh %dm %ds",
					iRoutingProblemPeakPoorConnections,
					duration / 3600, (duration / 60) % 60, duration % 60);
			fclose(routing_update_email_fd);
			routing_update_email_fd = NULL;

			char subject[100];
			zstruct(subject);
			sprintf(subject, "%sRouting problem over after %dh %dm %ds",
				iRunningLiveFlag ? "" : "*testing* : ",
				duration / 3600, (duration / 60) % 60, duration % 60);
			kp(("%s %s(%d) Sending routing problem summary email.\n", TimeStr(), _FL));
			Email(
				  #if 0	//20010223MB - Never used again
					"bob@kkrekop.io",		 	// to:
				  #else
					"support@kkrekop.io", 	// to:
				  #endif
					"PokerServer",						// From (name):
					"pokerserver@kkrekop.io",	// From (email):
					subject,						// Subject:
					routing_update_email_fname);	// fname
		}
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Send a shutdown message to all connected players.
//

void CardRoom::SendShutdownMessageToPlayers(int shutdown_client_flag, char *message)
{
	// Tell all the clients we are shutting down (or ask them to shut down)
	// Send a DATATYPE_SHUTDOWN_MSG packet.
	struct ShutDownMsg sdm;
	zstruct(sdm);
	int wait_ms = 1000 + connected_players * 40;	// wait longer if more clients connected.
	sdm.seconds_before_shutdown = (wait_ms + 999) / 1000;
	sdm.shutdown_client_flag = (BYTE8)shutdown_client_flag;
	sprintf(sdm.shutdown_reason, "The server will shut down in %d seconds.  %s",
				sdm.seconds_before_shutdown, message);

	EnterCriticalSection(&PlrInputCritSec);
	int i;
	for (i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (p->server_socket) {
			//kp(("%s(%d) Sending shutdown message to player_id $%08lx\n", _FL, p->player_id));
			p->SendDataStructure(DATATYPE_SHUTDOWN_MSG, &sdm, sizeof(sdm));
		}
	}
	LeaveCriticalSection(&PlrInputCritSec);
}

/**********************************************************************************
 Function CardRoom::SendAdminChatMonitor(GameChatMessage *gcm)
 date: 24/01/01 kriskoin Purpose: post a chat message to all admin clients
 24/01/01 kriskoin:
			 from pretty much anywhere without critsec concerns.
***********************************************************************************/
void CardRoom::SendAdminChatMonitor(GameChatMessage *egcm, char *table_name, ChipType chip_type)
{
	GameChatMessage gcm;
	zstruct(gcm);
	
	// we need to set the type -- different for alerts
	if (egcm->text_type < ALERT_1) {
		switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:

			gcm.text_type = CHATTEXT_MONITOR_PLAY;
			break;
		case CT_REAL:
			gcm.text_type = CHATTEXT_MONITOR_REAL;
			break;
		case CT_TOURNAMENT:
			// we will treat this as real money chat for now
			gcm.text_type = CHATTEXT_MONITOR_REAL;
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
		}
	} else {	// alert

		gcm.text_type = egcm->text_type;
	}

	// generate a time stamp
	time_t tt = time(NULL);
	struct tm tm;
	struct tm *t = localtime(&tt, &tm);
	// better safe than sorry -- cap the chat msg len
	const int chopped_msg_len = MAX_CHAT_MSG_LEN-40; // 40 for stuff we'll pack in front of it
	char chat_msg[chopped_msg_len];	
	zstruct(chat_msg);
	strnncpy(chat_msg, egcm->message, chopped_msg_len);
	if (egcm->text_type < ALERT_1) { // normal chat monitoring
		sprintf(gcm.message, "%-14s%02d:%02d %-12s %s\n",
				table_name, t->tm_hour, t->tm_min, egcm->name, chat_msg);
	} else {	// format for alerts is different
		sprintf(gcm.message, "ALERT-%d@%02d:%02d %s\n",
			// we want alerts numbered 1 to 10	
			gcm.text_type-ALERT_1+1, t->tm_hour, t->tm_min, chat_msg);
		// log and display to debwin if it's quite important
		if (gcm.text_type >= ALERT_4) {
			AddToLog("Data/Logs/alert.log", "Security alerts\n", "%s", gcm.message);
			int alert_threshold = ALERT_4;
			if (DebugFilterLevel >= 2) {
				alert_threshold = ALERT_5;
			}
			if (DebugFilterLevel >= 4) {
				alert_threshold = ALERT_6;
			}
			if (DebugFilterLevel >= 6) {
				alert_threshold = ALERT_7;
			}
			if (DebugFilterLevel >= 8) {
				alert_threshold = ALERT_8;
			}
			if (DebugFilterLevel >= 9) {
				alert_threshold = ALERT_9;
			}
			if (gcm.text_type >= alert_threshold) {
				kp(("%s", gcm.message));
			}
		}
	}
  #if 1	// 2022 kriskoin
	// Queue it up for processing by the cardroom later...
	// The cardroom critical section does not need to be
	// owned because this is the only place things get added
	// and the queue structure is such that it should be
	// re-entrant safe.
	static int initialized;
	static PPCRITICAL_SECTION cs;
	if (!initialized) {
		initialized = TRUE;
		PPInitializeCriticalSection(&cs, CRITSECPRI_LOCAL, "admin chat queue");
	}
	EnterCriticalSection(&cs);
	int new_head = (admin_chat_queue_head+1) % ADMIN_CHAT_QUEUE_LEN;
	if (new_head != admin_chat_queue_tail) {
		admin_chat_queue[admin_chat_queue_head] = gcm;
		admin_chat_queue_head = new_head;
	} else {
		kp(("%s(%d) admin chat queue is full. discarding chat message.\n", _FL));
	}
	LeaveCriticalSection(&cs);
  #else
	// send to everyone who should get it
	EnterCriticalSection(&PlrInputCritSec);
	for (int i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (p->Connected() && p->priv >= ACCPRIV_CUSTOMER_SUPPORT) {
			p->SendDataStructure(DATATYPE_CHAT_MSG, &gcm, sizeof(gcm));

		}
	}
	LeaveCriticalSection(&PlrInputCritSec);
  #endif
}
	
//*********************************************************
// https://github.com/kriskoin//
// Send a struct MiscClientMessage to all connected players.
//
void CardRoom::SendBroadcastMessage(struct MiscClientMessage *mcm)
{
	EnterCriticalSection(&PlrInputCritSec);
	int i;
	for (i=0 ; i<connected_players ; i++) {
		Player *p = (Player *)players[i];
		if (p->Connected()) {
			// misc_data_1 contains the player_id this message is destined
			// for.  If it's zero, the message is for everyone.
			if (!mcm->misc_data_1 || mcm->misc_data_1==p->player_id) {
				// It's for them.
				p->SendDataStructure(DATATYPE_MISC_CLIENT_MESSAGE, mcm, sizeof(*mcm));
			}
		}
	}
	LeaveCriticalSection(&PlrInputCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Move a player to the top of any waiting lists they're joined to
//
void CardRoom::MovePlayerToTopOfWaitLists(WORD32 player_id)
{
	for (int i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
		waiting_lists[i].MovePlayerToTop(player_id);
	}
}

//****************************************************************
// https://github.com/kriskoin//
// Return the next sequential game serial number (and increment)
//
WORD32 CardRoom::NextGameSerialNumberAndIncrement(void)
{
	return next_game_serial_number++;
}

//*********************************************************
// https://github.com/kriskoin//
// Check for any previous player objects for a player_id.  Pass the
// server_socket on to them if found.
//
void CardRoom::CheckForPreviousPlayerLogin(Player *new_player_object, WORD32 new_player_id)
{
	for (int i=0 ; i<connected_players ; i++) {

		EnterCriticalSection(&PlrInputCritSec);
		Player *p = (Player *)players[i];
		if (p && p->player_id==new_player_id) {
			// This player object has the same player_id.  It should
			// inherit the server_socket from the caller.
			char *ip_str = p->ip_str;


			if (!strcmp(p->ip_str, new_player_object->ip_str)) {
				// same ip address
				ip_str = "(same ip)";
			}
			if (DebugFilterLevel <= 2) {
				kp(("%s %-15.15s ReLogin %-15s $%08lx Old:%s\n",
						TimeStr(), new_player_object->ip_str,
						p->user_id, p->player_id, ip_str));
				kp(("%s %-15.15s ReLogin disconnect duration: %ds (%s IP)\n",
						TimeStr(), new_player_object->ip_str,
						p->TimeSinceLastContact(),
						strcmp(p->ip_str, new_player_object->ip_str) ? "new" : "same"));
			}
			pr(("%s(%d) Detected previous login for player_id $%08lx.  Passing socket to previous player object.\n",_FL,new_player_id));
			ConnectionLog->Write(
						"%s %-15.15s ReLogin '%s' %06lx sock %04x (old socket was %04x)\n",
						TimeStr(),
						new_player_object->ip_str,
						p->user_id,
						p->player_id,
						new_player_object->server_socket ? new_player_object->server_socket->sock : -1,
						p->server_socket ? p->server_socket->sock : -1);

			// If the old socket is still connected, there's a chance someone
			// is logging in from two locations (or someone has access to their
			// account.  We should send a message before disconnecting the
			// older socket.
			if (p->Connected()) {
				struct ConnectionClosing cc;
				zstruct(cc);
				cc.reason = 1;	// client logged in on another socket
				if (new_player_object->server_socket) {
					cc.new_ip_address = new_player_object->server_socket->connection_address.sin_addr.s_addr;
				}
				char str[20];
				IP_ConvertIPtoString(cc.new_ip_address, str, 20);
				char str2[20];
				WORD32 oldip = 0;
				int old_socket_handle = 0;
				if (p->server_socket) {
					oldip = p->server_socket->connection_address.sin_addr.s_addr;
					old_socket_handle = p->server_socket->sock;
				}
				IP_ConvertIPtoString(oldip, str2, 20);
				pr(("%s %-15.15s Previous login for '%s' ($%08lx): closing old socket to %s\n",
						TimeStr(), str, p->user_id, p->player_id, str2));
				ConnectionLog->Write(
						"%s %-15.15s Previous login for '%s' (%06lx): closing old socket %04x to %s\n",
						TimeStr(), str, p->user_id, p->player_id, old_socket_handle, str2);
			  #if 0 // 2022 kriskoin
				// This code doesn't work very well with the output queue
				// threads because by the time the packet gets to the front
				// of the queue, it goes to the new socket rather than the
				// old one.
				kp1(("%s(%d) Warning: relogin code no longer tells the old connection it's closing.\n", _FL));
				if ( (new_player_object->client_platform.computer_serial_num !=
					  p->client_platform.computer_serial_num) &&
					 (cc.new_ip_address != oldip) )
				{
					SendAdminAlert(ALERT_4, "Account '%s' at %s got logged out by a connection from %s",
								p->user_id, str2, str);
				}
			  #else
				//kriskoin: 				// the regular output queue and sends it immediately.
				p->SendDataStructure(DATATYPE_CLOSING_CONNECTION, &cc, sizeof(cc), FALSE, TRUE);
			  #endif
			}

		  #if 0	// 2022 kriskoin
			// purge anything still in the output queue which was supposed to
			// go to the old socket.
			// temporarily disabled due to critical section deadlocks.
			// this code needs to be thought through and solved.
			PlrOut_RemovePacketsForPlayer(p);
		  #else
			kp1(("%s(%d) *** We should be purging some packets here (relogin).\n", _FL));
		  #endif

			p->SetServerSocket(&(new_player_object->server_socket));
			p->send_client_info = TRUE;	// flag to send client their new login status, joined tables, etc.
			p->client_version_number = new_player_object->client_version_number;
			p->client_platform       = new_player_object->client_platform;
			SDB->SetClientPlatformInfo(p->player_id, &p->client_platform, &p->client_version_number);

			// If our anonymous login (new_player_object) is joined to any
			// tables, we need to join our original player object to them
			// as well.
			struct JoinedTableData *jtd = new_player_object->JoinedTables;
			for (int j=0 ; j<new_player_object->JoinedTableCount ; j++, jtd++) {
				pr(("%s(%d) Joining table %d for player $%08lx during migrate.\n",_FL,jtd->table_serial_number,p->player_id));
			  #if 1	//kriskoin: 				// Find an empty slot in the pending_joins array.
				EnterCriticalSection(&p->PlayerCritSec);
				if (p->pending_joins_count < MAX_GAMES_PER_PLAYER) {
					struct CardRoom_JoinTable jt;
					zstruct(jt);

					jt.table_serial_number = jtd->table_serial_number;
					jt.status = (BYTE8)(jtd->watching_flag ? JTS_WATCH : JTS_JOIN);
					p->pending_joins[p->pending_joins_count++] = jt;
				} else {
					kp(("%s(%d) Warning: no room for table during migrate!\n", _FL));
				}
				LeaveCriticalSection(&p->PlayerCritSec);
			  #else	// old method...
				EnterCriticalSection(&TableListCritSec);
				Table *t = TableSerialNumToTablePtr(jtd->table_serial_number);
				if (t) {
					ErrorType err = t->AddWatchingPlayer(p);
					if (err != ERR_NONE) {
						// That join failed for some reason.  Likely
						// scenarios include:
						//   - We're already joined (perhaps even playing)
						//   - We're already joined to too many tables.
						// If we're joined to too many tables, we should unjoin
						// this table before passing the socket on to the original
						// player object.
						if (p->JoinedTableCount >= MAX_GAMES_PER_PLAYER) {
							// Too many tables joined.  Unjoin before continuing.
							// This would be really bad if the anonymous player was
							// actually playing at a table.  In theory that should
							// never happen because we can only watch while logged
							// in anonymously.
							kp(("%s(%d) Joining table %d for player $%08lx failed during login migrate.  Removing anon player from table.\n",_FL,jtd->table_serial_number,p->player_id));
							t->RemoveWatchingPlayer(new_player_object);
						}
					}
				} else {
					Error(ERR_ERROR, "%s(%d) Could not find table %d to remove anon player from", _FL, jtd->table_serial_number);
				}
				LeaveCriticalSection(&TableListCritSec);
			  #endif
			}
		}
		LeaveCriticalSection(&PlrInputCritSec);
	}
}

/**********************************************************************************
 Function SendDealerMessageToAllTables
 date: 24/01/01 kriskoin Purpose: send a message to all tables from the dealer
***********************************************************************************/
void CardRoom::SendDealerMessageToAllTables(char *msg, int text_type, int gcm_flags)
{
	if (!(gcm_flags & GCMF_SEND_TO_ALL_TABLES)) {
		// no flags set... default to sending to everyone.
		gcm_flags |= GCMF_SEND_TO_ALL_TABLES;
	}

	for (int i=0 ; i<table_count ; i++) {
		Table *t = tables[i];
		if (t /*&& t->table_is_active*/) {
			int send_to_table = FALSE;
			if (t->GameCommonData.flags & GCDF_REAL_MONEY) {
				// Real money table
				if (gcm_flags & GCMF_SEND_TO_REAL_TABLES) {
					send_to_table = TRUE;
				}
			} else if (t->GameCommonData.flags & GCDF_TOURNAMENT) {
				// Tournament table
				if (gcm_flags & GCMF_SEND_TO_TOURNAMENT_TABLES) {
					send_to_table = TRUE;
				}

			} else {
				// Play money table
				if (gcm_flags & GCMF_SEND_TO_PLAY_TABLES) {
					send_to_table = TRUE;
				}
			}
			if (send_to_table) {
				t->SendDealerMessage(msg, text_type);
			}
		}
	}
}

/**********************************************************************************
 Function CardRoom::GetNextGameSerialNumber()
 date: kriskoin 2019/01/01 Purpose: return next game's serial number

***********************************************************************************/
WORD32 CardRoom::GetNextGameSerialNumber(void)
{
	return next_game_serial_number;
}

/**********************************************************************************
 Function CardRoom::SetNextGameSerialNumber(WORD32 serial_num)
 date: kriskoin 2019/01/01 Purpose: set the next game's serial number
***********************************************************************************/
void CardRoom::SetNextGameSerialNumber(WORD32 serial_num)
{
	next_game_serial_number = serial_num;
}

//*********************************************************
// https://github.com/kriskoin//
// Retrieve an account balance based solely on the user id,
//

int CardRoom::GetAccountBalance(char *user_id)
{
	SDBRecord r;
	zstruct(r);
	SDB->SearchDataBaseByUserID(user_id, &r);
	// If it wasn't found, r is still zeroed.
	return r.real_in_bank;
}

//*********************************************************
// https://github.com/kriskoin//
// Every night at midnight, send out a summary email
//

void CardRoom::SendSummaryEmailIfNecessary(void)
{
  #if !TEST_SUMMARY_EMAIL
	if (!iRunningLiveFlag){
		return;
	}
	if (ServerVersionInfo.alternate_server_ip) {
		// We're not the primary server... don't send an email.
		return;
	}
  #endif
	if (!next_summary_email_seconds) {
		// First time through... set up a counter for when to send
		// the next email (midnight tonight)
		time_t now = time(0L);
		time_t tomorrow = now + 24*60*60;	// calculate this time tomorrow
		struct tm t;
		localtime(&tomorrow, &t);
		t.tm_sec = 0;
		t.tm_min = 1;
		t.tm_hour = 0;	// set time to 12:01am
		// Turn it back into a time_t
		time_t midnight = mktime(&t);
		//kp(("%s(%d) Seconds until midnight = %d\n", _FL, midnight - now));
		next_summary_email_seconds = SecondCounter + midnight - now;
	}
	if (SecondCounter >= next_summary_email_seconds) {
		next_summary_email_seconds = 0;	// reset for next time.
		kp(("%s %s(%d) Time to send a summary email.\n", TimeStr(), _FL));

		// Load up our saved stats from yesterday.
		struct SavedStats today;
		zstruct(today);
		zstruct(YesterdaySavedStats);
		ReadFile("savedstats.bin", &YesterdaySavedStats, sizeof(YesterdaySavedStats), NULL);

		// Save our stats from today (for tomorrow to use)
		today.game_number = GetNextGameSerialNumber();
		today.accounts = SDB->GetUserRecordCount();
		today.ecash_balance = -GetAccountBalance("Ecash");
		today.rake_balance = GetAccountBalance("Rake");
		today.ecashfee_balance = -GetAccountBalance("EcashFee");
		today.gross_bets = ServerVars.gross_bets;
		today.gross_tournament_buyins = ServerVars.gross_tournament_buyins;
		WriteFile("savedstats.bin", &today, sizeof(today));

		char fname[MAX_FNAME_LEN];
		MakeTempFName(fname, "s");
		FILE *fd = fopen(fname, "wt");
		if (fd) {
			if (!iRunningLiveFlag) {
			  	fprintf(fd, "These are test stats... ignore them.\n\n");
			}
			fprintf(fd, "Server stats as of %s Belize time:\n\n", TimeStr());
			fprintf(fd, "Next game number: %d (%d played today)\n",
					today.game_number, today.game_number - YesterdaySavedStats.game_number);
			fprintf(fd, "Total online accounts: %d (%d created today, %d moved to stale today)\n",
					today.accounts,

					today.accounts - YesterdaySavedStats.accounts + SDB->number_of_accounts_purged,
					SDB->number_of_accounts_purged);
			fprintf(fd, "Grand total accounts: %d (%d online + %d stale)\n",
					today.accounts + SDB->stale_account_count,
					today.accounts, SDB->stale_account_count);
			SDB->number_of_accounts_purged = 0;	// reset

			struct tm tm;
			struct tm *t = localtime(&daily_peak_active_players_time, &tm);
			fprintf(fd, "Peak users: %d (at %d:%02d)\n", daily_peak_active_players, t->tm_hour, t->tm_min);

			t = localtime(&daily_peak_active_tables_time, &tm);
			fprintf(fd, "Peak tables: %d (at %d:%02d)\n", daily_peak_active_tables, t->tm_hour, t->tm_min);

			t = localtime(&daily_peak_active_real_tables_time, &tm);
			fprintf(fd, "Peak real tables: %d (at %d:%02d)\n", daily_peak_active_real_tables, t->tm_hour, t->tm_min);

			char curr1[MAX_CURRENCY_STRING_LEN], curr2[MAX_CURRENCY_STRING_LEN];
			zstruct(curr1);
			zstruct(curr2);

			CurrencyString(curr1, today.rake_balance, CT_REAL);
			CurrencyString(curr2, today.rake_balance - YesterdaySavedStats.rake_balance, CT_REAL);
			fprintf(fd, "Rake account balance:  %s (%s today)\n", curr1, curr2);

			CurrencyString(curr1, today.ecash_balance, CT_REAL);
			CurrencyString(curr2, today.ecash_balance - YesterdaySavedStats.ecash_balance, CT_REAL);
			fprintf(fd, "Ecash account balance: %s (%s today)\n", curr1, curr2);

			int gross_cc = 0;
			if (CCFeeRate) {
				gross_cc = (int)(
						(double)(today.ecashfee_balance - YesterdaySavedStats.ecashfee_balance) / CCFeeRate + .5);
			}
			fprintf(fd, "Gross CC deposits today: %s\n",
						CurrencyString(curr1, gross_cc, CT_REAL));
			fprintf(fd, "Gross real money bets today: %s\n",
						CurrencyString(curr1, (ServerVars.gross_bets - YesterdaySavedStats.gross_bets), CT_REAL));
			fprintf(fd, "Gross tournament buy-ins today: %s\n",
						CurrencyString(curr1, (ServerVars.gross_tournament_buyins - YesterdaySavedStats.gross_tournament_buyins), CT_REAL));
			fprintf(fd, "Tournaments played today: %d\n", tournaments_played_today);

			int days    =  SecondCounter / (24*60*60);
			int hours   = (SecondCounter % (24*60*60)) / (60*60);
			int minutes = (SecondCounter % (60*60))    / (60);
			fprintf(fd, "Server uptime: %d day%s %d hour%s %d minute%s\n",
					days,    days==1    ? "" : "s",
					hours,   hours==1   ? "" : "s",
					minutes, minutes==1 ? "" : "s");
			fclose(fd);
		  #if TEST_SUMMARY_EMAIL
			#define EMAIL_RECIPIENTS	"mike@kkrekop.io"
		  #else
			#define EMAIL_RECIPIENTS	"management@kkrekop.io"
		  #endif
			Email(EMAIL_RECIPIENTS,
					"PokerSrv",
					"management@kkrekop.io",
					"PokerSrv stats",
					fname,
					NULL,		// bcc:
					TRUE);		// delete when done?
		}
		daily_low_active_players = daily_peak_active_players;
		daily_low_active_real_tables = daily_peak_active_real_tables;
		daily_low_active_tables = daily_peak_active_tables;
		daily_peak_active_players = 0;		// reset
		daily_peak_active_tables = 0;		// reset
		daily_peak_active_real_tables = 0;	// reset
		ServerVars.bad_beat_count = 0;		// reset 
		ServerVars.bad_beat_payout = 0;		// reset
		tournaments_played_today = 0;		// reset
		YesterdaySavedStats = today;

		// pick a low time of the day for heap compaction (but not at the
		// top of any hour because it interferes with the backup processes).
		NextHeapCompactTime = time(NULL) + 5*3600+45*60;
	}
}

//****************************************************************
// 
//
// Cardroom main loop.  Once the I/O threads are all launched,
// the main loop takes care of table creation and all that.
// This function doesn't return until server shutdown.
//
ErrorType CardRoom::MainLoop(void)
{
	cardroom_threadid = getpid();
	//kp(("%s(%d) MainLoop: cardroom_threadid = %d ($%08lx)\n", _FL, cardroom_threadid, cardroom_threadid));

	if (ValidateTableNames() != ERR_NONE) {	// 24/01/01 kriskoin:
		exit(ERR_FATAL_ERROR);
	}

	// Always create at least ONE table so that the table
	// tree is NEVER empty.  That prevents having to test for
	// that condition all over the place.
	EnterCriticalSection_CardRoom();

/////////
// SOME ROBOTS
  AddNewTable(DISPLAY_TAB_HOLDEM, GAME_RULES_HOLDEM, 10, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
/*  AddNewTable(DISPLAY_TAB_HOLDEM, GAME_RULES_HOLDEM, 1, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_STUD7_HI_LO, GAME_RULES_STUD7, 8, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_OMAHA_HI, GAME_RULES_OMAHA_HI, 10, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_OMAHA_HI_LO, GAME_RULES_OMAHA_HI_LO, 10, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_ONE_ON_ONE, GAME_RULES_HOLDEM, 2, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_ONE_ON_ONE, GAME_RULES_OMAHA_HI, 2, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
  AddNewTable(DISPLAY_TAB_ONE_ON_ONE, GAME_RULES_STUD7, 2, 1*100, 3*100, NULL, TRUE, CT_PLAY, 0, RT_NONE);
*/
///////////
	//kriskoin: 	// there are no tables on it.
	int i;
	for (i=0 ; i<MAX_CLIENT_DISPLAY_TABS ; i++) {
		RebuildTableSummaries((ClientDisplayTabIndex)i);
	}

	LeaveCriticalSection_CardRoom();

	ErrorType err = LaunchPlayerIOThread();
	if (err) {
		Error(ERR_FATAL_ERROR, "%s(%d) LaunchPlayerIOThread() failed.",_FL);
		exit(ERR_FATAL_ERROR);
	}

	LaunchAcceptThread();

	iRunLevelCurrent = RUNLEVEL_ACCEPT_CONNECTIONS;

	int consecutive_runs = 0;
	forever {
		WORD32 main_loop_start_ticks = GetTickCount();
		UpdateSecondCounter();

		int work_was_done_flag = FALSE;

		{
			EnterCriticalSection(&CardRoomCritSec);

			// Handle any processing for individual players, such as joining
			// and unjoining games, etc.
			UpdatePlayers_LowLatencyWork(&work_was_done_flag);

		  #if 1	//kriskoin: 			LeaveCriticalSection(&CardRoomCritSec);
			EnterCriticalSection(&CardRoomCritSec);
		  #endif

			// Update all tables (do the stuff which requires low latency)
			UpdateTables_LowLatencyWork(&work_was_done_flag);

		  #if 0	// 2022 kriskoin
			kp1(("%s(%d) ****** DO NOT LEAVE THIS ON! SLEEPING IN MAIN LOOP WHILE OWNING CRITSEC! *****\n",_FL));
			kp1(("%s(%d) ****** DO NOT LEAVE THIS ON! SLEEPING IN MAIN LOOP WHILE OWNING CRITSEC! *****\n",_FL));
			kp1(("%s(%d) ****** DO NOT LEAVE THIS ON! SLEEPING IN MAIN LOOP WHILE OWNING CRITSEC! *****\n",_FL));
			kp1(("%s(%d) ****** DO NOT LEAVE THIS ON! SLEEPING IN MAIN LOOP WHILE OWNING CRITSEC! *****\n",_FL));
			kp1(("%s(%d) ****** DO NOT LEAVE THIS ON! SLEEPING IN MAIN LOOP WHILE OWNING CRITSEC! *****\n",_FL));
			Sleep(100);
		  #endif
			LeaveCriticalSection(&CardRoomCritSec);
		}


	  #if INCLUDE_FUNCTION_TIMING	// 2022 kriskoin
		#define MAX_MAIN_LOOP_TIMING	250
	  	{
			WORD32 elapsed = GetTickCount() - main_loop_start_ticks;
			if (elapsed > MAX_MAIN_LOOP_TIMING) {

				kp(("%s %s(%d) Warning: Main Loop took %5dms to this point.\n", TimeStr(), _FL, elapsed));
			}
		}
	  #endif

		// If the shot clock was just forced to change (e.g. new data),

		// run the less important tasks asap (rather than waiting).
		// note: iShotClockChangedFlag is only cleared by UpdateSummaryData()
		// so we must ensure that it gets called.
		if (iShotClockChangedFlag) {
			next_mainloop_bgndtask_time = 0;	// execute asap
			next_manage_tables_time = 0;		// execute asap
		}


		// Do some stuff infrequently (like every 2s or so)
		if (SecondCounter >= next_mainloop_bgndtask_time) {
			EnterCriticalSection_CardRoom();
			next_mainloop_bgndtask_time = SecondCounter + 2;	// every 2s is plenty often

			// Handle any processing for individual players, such as joining
			// and unjoining games, etc.
			UpdatePlayers_HighLatencyWork(&work_was_done_flag);

			// Handle misc bgnd tasks the main loop needs to deal with			
			UpdateMainLoopBgndTasks(&work_was_done_flag);

			// Every night at midnight, send out a summary email
			SendSummaryEmailIfNecessary();

			LeaveCriticalSection_CardRoom();

			// Check if the server should be shutting down.
			if (iRunLevelDesired < RUNLEVEL_ACCEPT_CONNECTIONS) {
				break;
			}


			// try to run again right when the shot clock has expired (if necessary)
			if (iShotClockETA && ShotClockExpirySecondCounter < next_mainloop_bgndtask_time) {
				next_mainloop_bgndtask_time = ShotClockExpirySecondCounter;
			}
		}

		// Do this stuff very infrequently (like every 10s or so)
		if (SecondCounter >= next_manage_tables_time) {
			EnterCriticalSection_CardRoom();
		  #if 0	//kriskoin: 		  	// We need to make sure this runs often enough that players are booted
			// from tables between games (if locked out) and players who ask to leave
			// are also removed from tables between games, therefore this should run
			// at least once between any two games.
			next_manage_tables_time = SecondCounter + 3;
		  #else
			next_manage_tables_time = SecondCounter + 10;	// every 10s is plenty often
		  #endif

			// try to run again right when the shot clock has expired (if necessary)
			if (iShotClockETA && ShotClockExpirySecondCounter < next_manage_tables_time) {
				next_manage_tables_time = ShotClockExpirySecondCounter;
			}

			// Re-read our parm file if necessary (on SIGHUP)
			if (ReReadParmFileFlag) {
				kp(("%s(%d) Re-reading .INI file (probably because of SIGHUP received)\n",_FL));
				ReadOurParmFile();
			}

			// Update all tables (do the stuff which doesn't require low latency)
			UpdateTables_HighLatencyWork(&work_was_done_flag);

			// Take care of over-all table management.  Create new tables,
			// delete old (empty) tables, etc.
			ManageTables();

			// Update waiting lists for each game type (if necessary)
			UpdateWaitingLists();

			// Update any summary data and send it to clients if necessary.
			UpdateSummaryData(&work_was_done_flag);

			// If a disconnected player has been kicked off all his tables
			// and waiting lists, delete the player object; it's no longer needed.
			DeleteDisconnectedPlayers();

			// Write out the server vars file if it has changed.
			if (write_out_server_vars_now) {
				write_out_server_vars_now = FALSE;
				WriteServerVars();	// pokersrv function
			}

			// Flag all waiting lists as needing updating periodically
			for (int g=0 ; g<MAX_CLIENT_DISPLAY_TABS ; g++) {
				update_waiting_list[g] = TRUE;
			}

			// If any tables have fired up, notify all the other tables
			// so they can notify their watching players...
			for (int i=0 ; i<table_count ; i++) {
				Table *t = tables[i];
				if (t->table_is_active != Table::TABLE_ACTIVE_FALSE &&
					t->prev_table_is_active == Table::TABLE_ACTIVE_FALSE &&
					t->chip_type == CT_REAL &&
					t->max_number_of_players > 2 &&
					SecondCounter > 200)
				{
					// This table has activated... tell the other tables.
					char msg[100];
					char curr_str1[MAX_CURRENCY_STRING_LEN];
					char curr_str2[MAX_CURRENCY_STRING_LEN];
					char *game_names[MAX_GAME_RULES] = {
						"Hold'em",
						"Omaha Hi",
						"Omaha Hi/Lo 8",
						"7 Card Stud",
						"7 Card Stud Hi/Lo 8",
					};

					if (game_names[t->game_rules - GAME_RULES_START]) {
						sprintf(msg, "A new %s/%s %s table has just opened.",
								CurrencyString(curr_str1, t->big_blind_amount * GameStakesMultipliers[t->game_rules - GAME_RULES_START], t->chip_type),
								CurrencyString(curr_str2, t->big_blind_amount*2*GameStakesMultipliers[t->game_rules - GAME_RULES_START], t->chip_type),
								game_names[t->game_rules - GAME_RULES_START]);
						for (int j=0 ; j<table_count ; j++) {
							Table *t2 = tables[j];
							if (t != t2) {
								t2->SendDealerMessageToWatchingPlayers(msg, CHATTEXT_DEALER_NORMAL);
							}
						}
					}
				}
				t->prev_table_is_active = t->table_is_active;
			}

			LeaveCriticalSection_CardRoom();

			// Compact the heap occasionally
		  #if 0	//kriskoin: 			if (!NextHeapCompactTime && !iRunningLiveFlag) {
				NextHeapCompactTime = time(NULL) + 5;
			}
		  #endif
			if (NextHeapCompactTime && time(NULL) >= NextHeapCompactTime) {
				NextHeapCompactTime = 0;
				// note: I couldn't find a routine to do garbage collection and/or
				// heap compaction in the standard CRT library.  Windows has some
				// functions, but the GNU stuff didn't seem to.
				kp(("%s Heap compaction time...  Living packets = %d before PktPool_FreePackets().\n", TimeStr(), dwLivingPackets));
				PktPool_FreePackets();	// free up any packets in the packet pool.
			  #if WRITE_LEAKS
				write_leaks();
			  #endif
				kp(("%s Heap compaction done.    Living packets = %d after PktPool_FreePackets().\n", TimeStr(), dwLivingPackets));
			}

			// Print some info to debwin.log periodically to keep connections alive.
			{
				static WORD32 last_display_time = 0;
				static int peak_active_players = 0;
				static int peak_active_tables = 0;
				static int peak_active_real_tables = 0;
				if (active_player_connections > peak_active_players) {
					peak_active_players = active_player_connections;
				}
				if (active_tables > peak_active_tables) {
					peak_active_tables = active_tables;
					NextAdminStatsUpdateTime = 0;	// force immediate update
				}
				if (active_real_tables > peak_active_real_tables) {
					peak_active_real_tables = active_real_tables;
					NextAdminStatsUpdateTime = 0;	// force immediate update
				}

				if (daily_peak_active_players < active_player_connections) {
					daily_peak_active_players = active_player_connections;
					daily_peak_active_players_time = time(NULL);
					NextAdminStatsUpdateTime = 0;	// force immediate update
				}

				if (daily_peak_active_tables < active_tables) {
					daily_peak_active_tables = active_tables;
					daily_peak_active_tables_time = time(NULL);
					NextAdminStatsUpdateTime = 0;	// force immediate update
				}
				if (daily_peak_active_real_tables < active_real_tables) {
					daily_peak_active_real_tables = active_real_tables;
					daily_peak_active_real_tables_time = time(NULL);
					NextAdminStatsUpdateTime = 0;	// force immediate update

				}

				if (SecondCounter >= 15*60) {	// have we been up for at least n minutes?
					// Track lows properly
					if (active_player_connections < daily_low_active_players) {
						// a new low has been set
						daily_low_active_players = active_player_connections;
						// Also reset the highs
						daily_peak_active_players = active_player_connections;
						daily_peak_active_players_time = time(NULL);
						daily_peak_active_tables = active_tables;
						daily_peak_active_tables_time = time(NULL);
						daily_peak_active_real_tables = active_real_tables;
						daily_peak_active_real_tables_time = time(NULL);
					}
					if (active_real_tables < daily_low_active_real_tables) {
						daily_low_active_real_tables = active_real_tables;
					}
					if (active_tables < daily_low_active_tables) {
						daily_low_active_tables = active_tables;
					}
				} else {
					// Server just came up, don't let the lows stay at zero.
					daily_low_active_players = daily_peak_active_players;
					daily_low_active_real_tables = daily_peak_active_real_tables;
					daily_low_active_tables = daily_peak_active_tables;
				}

			  #if 0 // test alert levels
				{
					static alert_level;
					static unsigned old_seconds_counter;
					if (SecondCounter != old_seconds_counter) {

						old_seconds_counter = SecondCounter;
						alert_level = ((alert_level+1) % 10);
						//SendAdminAlert((ChatTextType)(ALERT_1+alert_level), "This is a level %d alert", alert_level);
						SendAdminAlert((ChatTextType)(ALERT_10), "This is a level %d alert", alert_level);
					}
				}
			  #endif

			  #if 0	// 2022 kriskoin
				#define DISPLAY_INTERVAL	(5)	// 5s (for testing)
			  #else
				#define DISPLAY_INTERVAL	(1*60)	// 15 minutes to 1 minustes
			  #endif

				if (SecondCounter - last_display_time >= DISPLAY_INTERVAL) {
					ConnectionLog->Write(
							"%s ***%5d connected players,%4d active tables (peak:%5d players,%4d tables)\n",
							TimeStr(), active_player_connections, active_tables,
							peak_active_players, peak_active_tables);
					AddToLog("Data/Logs/usage.log", "Time\t\tConnected\tTables\tAvg Response (real)\tAvg Response (play)\n",
							"%s\t%d\t%d\t%d\t%.0f\t%.0f\n",
							TimeStr(),
							peak_active_players,
							peak_active_tables,
							peak_active_real_tables,
							avg_response_time_real_money*1000.0,
							avg_response_time_play_money*1000.0);

					kp(("%s ***%5d Players connected,%4d active tables,%4d real, resp%5.0f/%4.0f\n",
							TimeStr(), peak_active_players, peak_active_tables, peak_active_real_tables,
							avg_response_time_real_money*1000.0,
							avg_response_time_play_money*1000.0));
					last_display_time = SecondCounter;
					peak_active_players = 0;
					peak_active_tables = 0;
					peak_active_real_tables = 0;

				  #if 0	// 2022 kriskoin
					// Loop through all player objects and print out any that have
					// been disconnected for a long time...
					EnterCriticalSection_CardRoom();
					for (int i=0 ; i<connected_players ; i++) {
						Player *p = (Player *)players[i];
						if (p) {
							if (!p->server_socket) {
								kp(("%s(%d) Player id $%08lx ('%s') has no server socket.\n",
									_FL,p->player_id, p->user_id));
							}
							if (!p->Connected() && ANONYMOUS_PLAYER(p->player_id) && p->DisconnectedSeconds() > 600) {
								kp(("%s(%d) Player id $%08lx (anonymous) has been disconnected for %ds\n",
									_FL,p->player_id, p->DisconnectedSeconds()));
							}
						  #if 0	// 2022 kriskoin
							if (p->DisconnectedSeconds() >= DISPLAY_INTERVAL) {
								kp(("%s(%d) Player id $%08lx ('%s') has been disconnected for %d minutes but player object is still in memory.\n",
									_FL,p->player_id, p->user_id, p->DisconnectedSeconds()/60));
							}
						  #endif
						} else {
							kp(("%s(%d) players[%d] entry is NULL for some reason.\n", _FL, i));
						}
					}
					LeaveCriticalSection_CardRoom();
				  #endif
				}
			}
		}	// end of 'every 10 seconds' if.

		// Watch for work_was_done_flag related programming bugs.
		if (consecutive_runs++ >= 2000) {
			kp(("%s %s(%d) Warning: we've run 2000 times consecutively with no pauses.  Usage spike or programming bug?\n",TimeStr(),_FL));
		  #if WIN32
			MEMORYSTATUS ms;
			zstruct(ms);
			ms.dwLength = sizeof(ms);
			GlobalMemoryStatus(&ms);	// fetch info about memory
		    kp(("%s Memory: Total Free Physical RAM: %luK (%luM) Virtual mem: %luM  Memory Load = %d%%\n",
						TimeStr(),
						ms.dwAvailPhys    >> 10, ms.dwAvailPhys  >> 20,
						ms.dwAvailVirtual >> 20,
						ms.dwMemoryLoad));
		  #else	 //!WIN32
			// struct mallinfo {
			//	  int arena;    /* total space allocated from system */
			//	  int ordblks;  /* number of non-inuse chunks */
			//	  int smblks;   /* unused -- always zero */
			//	  int hblks;    /* number of mmapped regions */
			//	  int hblkhd;   /* total space in mmapped regions */
			//	  int usmblks;  /* unused -- always zero */
			//	  int fsmblks;  /* unused -- always zero */
			//	  int uordblks; /* total allocated space */
			//	  int fordblks; /* total non-inuse space */
			//	  int keepcost; /* top-most, releasable (via malloc_trim) space */
			//	};
			struct mallinfo mi = mallinfo();
			kp(("%s Memory: Allocated from system: %luK Currently used: %luK Unused: %luK\n",
						TimeStr(), mi.arena>>10, mi.uordblks>>10,mi.fordblks>>10));
		  #endif //!WIN32
			Sleep(500);		// don't chew up all the cpu time... leave some for other threads.
			consecutive_runs = 0;	// reset counter.
			iPrintWorkWasDoneFlagDetails = 50;	// print details for the next 50 times it gets set.
		}
	  #if INCLUDE_FUNCTION_TIMING && 0
	  	{
			WORD32 elapsed = GetTickCount() - main_loop_start_ticks;
			if (elapsed > MAX_MAIN_LOOP_TIMING) {
				kp(("%s %s(%d) Warning: Main Loop took %5dms to this point.\n", TimeStr(), _FL, elapsed));
			}
		}
	  #endif
	#if 0
		// TEMP: debug only
		{
			static WORD32 last_mem_map_dump_time = 0;
			if (SecondCounter - last_mem_map_dump_time >= 10) {
				MemDisplayMemoryMap();
			  #if WIN32	// 2022 kriskoin
				MemTrackVMUsage(TRUE, "%s(%d) CardRoom::MainLoop() at %u seconds",_FL, SecondCounter);
			  #elif 0
				struct mallinfo mi = mallinfo();
				int trim_size = mi.keepcost;
				int result = malloc_trim(trim_size);
				kp(("%s(%d) Called malloc_trim(%d). result = %d\n", _FL, trim_size, result));
				MemDisplayMemoryMap();
			  #endif
				last_mem_map_dump_time = SecondCounter;
			}
		}
	#endif

		// update the average time we spent dealing with this iteration
		WORD32 elapsed = GetTickCount() - main_loop_start_ticks;
		#define MAINLOOP_OLD_TIME_WEIGHTING	(.995)
		mainloop_avg_ms = (mainloop_avg_ms * MAINLOOP_OLD_TIME_WEIGHTING) +
						  (double)elapsed * (1.0-MAINLOOP_OLD_TIME_WEIGHTING);

	  #if INCLUDE_FUNCTION_TIMING	// 2022 kriskoin
		kp1((ANSI_BLACK_ON_YELLOW"%s(%d) **** Main Loop thread timing is enabled!  Don't leave this on normally!\n",_FL));
		if (elapsed > MAX_MAIN_LOOP_TIMING) {
			kp(("%s %s(%d) Warning: Main Loop took %5dms for a single loop.\n", TimeStr(), _FL, elapsed));
		}
	  #endif
		if (!work_was_done_flag) {
			// We didn't get anything done this time through the loop,
			// so sleep for a short period of time so we don't suck up
			// all the CPU time for no reason.
			Sleep(MainLoopIdleSleep);
			consecutive_runs = 0;
		} else {
			// Loop again immediately, but now's a really good time to let
			// any other threads have a chance to run (if they're high enough
			// priority).

		  #if 1	// 2022 kriskoin
			// ALWAYS let other threads get a shot at the CardRoom CritSec.  They
			// sleep for 2ms when they can't get access, so make sure we sleep for
			// at least more than that.  The small penalty we suffer in latency
			// here is far less than we suffer when other threads get starved.
			if (MainLoopActiveSleep) {
				Sleep(MainLoopActiveSleep);
			} else {
				sched_yield();
			}
		  #else
			sched_yield();
		  #endif
		}
	}	// bottom of main loop's forever()

	// Tell the accept thread to terminate
	accept_thread_shutdown_flag = TRUE;

	if (iShutdownAfterGamesCompletedFlag) {
		// Wait for the accept thread to terminate before telling clients
		// we're shutting down.
		while (!accept_thread_finished_flag) {
			Sleep(100);
		}

		// Tell all the clients we are shutting down.
		// Send a DATATYPE_SHUTDOWN_MSG packet.
		struct ShutDownMsg sdm;
		zstruct(sdm);
	  #if 1	//kriskoin: 			// the data really does seem to go out within one second.
		int wait_ms = 5000;
	  #else
		int wait_ms = 5000 + connected_players * 50;	// wait longer if more clients connected.
		wait_ms = min(wait_ms, 15000);	// max 15s

	  #endif
		sdm.seconds_before_shutdown = (wait_ms + 999) / 1000;
		if (iShutdownIsBriefFlag) {
			sprintf(sdm.shutdown_reason,
				"The server is about to restart.\n\n"
				"The restart process should be finished in 90 seconds.\n"
				"Play will resume as soon as the system has restarted.\n\n"
				"When the system returns please attempt to \n"
				"return to the table you were playing at..");
		} else {
			sprintf(sdm.shutdown_reason,
				"The server now shutting down.\n\n"
				"Please check the network status web page for an\n"
				"estimate of when the server will be back on-line.\n");
		}
		EnterCriticalSection_CardRoom();
		for (int i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p->server_socket) {
				//kp(("%s(%d) Sending shutdown message to player_id $%08lx\n", _FL, p->player_id));
				p->SendDataStructure(DATATYPE_SHUTDOWN_MSG, &sdm, sizeof(sdm));
			}
		}
		LeaveCriticalSection_CardRoom();
		Sleep(wait_ms);	// give them time to be sent out.
	}

	// Tell the communications thread to terminate.
	comm_thread_shutdown_flag = TRUE;

	// Wait for the communications thread and accept thread to terminate
	// before we return.
  #if !WIN32
	kp(("%s %s(%d) Waiting for comm thread and accept thread to terminate.\n",TimeStr(),_FL));
  #endif
	while (!comm_thread_finished_flag && !accept_thread_finished_flag) {
		Sleep(50);
	}

	// Wait for the data output threads to finish sending...
	time_t end_time = time(NULL) + 10;
	int queues_have_something = FALSE;
  #if !WIN32
	kp(("%s %s(%d) Waiting for output threads to finish sending... (queues=%d/%d)\n",
			TimeStr(),_FL,iPlrOutQueueLens[0], iPlrOutQueueLens[1]));
  #endif
	while (time(NULL) < end_time) {
		int i;
		queues_have_something = FALSE;

		for (i=0 ; i<NUMBER_OF_PLAYER_OUTPUT_THREADS ; i++) {
			if (iPlrOutQueueLens[i]) {
				queues_have_something = TRUE;
				break;

			}
		}

		EnterCriticalSection_CardRoom();
		for (i=0 ; i<connected_players ; i++) {
			Player *p = (Player *)players[i];
			if (p && p->server_socket) {
				EnterCriticalSection(&(p->PlayerCritSec));
				if (p->server_socket) {
					p->server_socket->ProcessSendQueue();
				  #if 0	//kriskoin: 				  		// none of this code is actually needed.  If we couldn't
						// send it right away, we don't care.  It's as simple as that.
					if (!p->server_socket->SendQueueEmpty()) {
						// If it's blocked and can't send right away, presume
						// that the guy's actually disconnected and we don't really
						// need to wait.
						if (p->server_socket->SendQueueHead &&
							p->server_socket->SendQueueHead->desired_send_ticks >= GettickCount())
						{
							// We can't send to this guy... we're just waiting.
						} else {
							// We CAN send to this guy, but haven't done so.
							queues_have_something = TRUE;
						}
					}
				  #endif
				}
				LeaveCriticalSection(&(p->PlayerCritSec));
			}
		}
		LeaveCriticalSection_CardRoom();


		if (!queues_have_something) {
			break;
		}
		Sleep(50);
	}
	if (queues_have_something) {
		kp(("%s %s(%d) *** WARNING: cardroom exiting while send queues still have data! (%d/%d)\n",
				TimeStr(), _FL, iPlrOutQueueLens[0], iPlrOutQueueLens[1]));
	}
  #if !WIN32
	kp(("%s %s(%d) comm and accept threads have terminated; deleting tables and player objects.\n",TimeStr(),_FL));
  #endif

	EnterCriticalSection_CardRoom();
	// Delete any tables we still have allocated.
	while (table_tree.tree_root) {
		DeleteTable(((Table *)table_tree.tree_root->object_ptr)->table_serial_number);
	}

	// Delete all player objects we're responsible for
	for (i=0 ; i<connected_players ; i++) {
		DeletePlayerObject((class Player **)&players[i]);
	}
	connected_players = 0;
	if (players) {
		free(players);
		players = NULL;
		max_connected_players = 0;

	}
	LeaveCriticalSection_CardRoom();

  #if !WIN32
	if (DebugFilterLevel <= 0 || iRunningLiveFlag) {
		kp(("%s(%d) Living packets = %d before PktPool_FreePackets().\n", _FL, dwLivingPackets));
	}
	PktPool_FreePackets();	// free up any packets in the packet pool.
   #if WRITE_LEAKS
	write_leaks();
   #endif

	if (DebugFilterLevel <= 0 || iRunningLiveFlag) {
		kp(("%s(%d) Living packets = %d after PktPool_FreePackets().\n", _FL, dwLivingPackets));
	}
  #endif

	pr(("%s(%d) finished deleting player objects; leaving cardroom.\n",_FL));

	iRunLevelCurrent = RUNLEVEL_ACCEPT_CONNECTIONS - 1;
	return ERR_NONE;
}
