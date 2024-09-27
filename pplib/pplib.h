/**************************************************************************************
 pplib.h -- universal defines for Desert Poker, including headers for pplib.lib
 Date: 20180707 kriskoin : 
***************************************************************************************/
#ifndef _PPLIB_H_INCLUDED
#define _PPLIB_H_INCLUDED

#ifndef FALSE
  #define FALSE 0
#endif
#ifndef TRUE
 #define TRUE (!FALSE)
#endif

#define forever for(;;)

#ifndef NULL
  #ifdef __cplusplus
	#define NULL    0
  #else
	#define NULL    ((void *)0)
  #endif
#endif
#define NOTUSED(x)	((x)=0)
#ifndef WIN32

  #ifndef _INC_TIME
	#include <time.h>
  #endif
  #ifndef _STDLIB_H
    #include <stdlib.h>
  #endif
  #ifndef _UNISTD_H
    #include <unistd.h>
  #endif
  #ifndef _TIME_H
	#include <time.h>
  #endif
#endif

#ifndef	_STRING_H

  #include <string.h>

#endif

#ifdef NDEBUG
  #define DEBUG 0
#else
  #define DEBUG 1
#endif

#if WIN32
  #if LAPTOP
    #define INCL_SSL_SUPPORT	0
  #else
    #define INCL_SSL_SUPPORT	0
  #endif
#else
  #define INCL_SSL_SUPPORT	1
#endif

#define INCLUDE_FUNCTION_TIMING	0	// Should normally be 0.  Used to see if non-blocking functions are slow.

#define zstruct(a)	memset(&(a),0,sizeof(a))
#if WIN32	// 2022 kriskoin
  #define _FL			GetNameFromPath2(__FILE__),__LINE__
#else
  #define _FL			__FILE__,__LINE__
#endif
// NOTE: DO NOT USE rand() or random() for shuffling cards - it's not good enough.
// Use RNG_NextNumber() instead.
#define random(a)	(rand()%(a))
#define BLANK(a)	(a[0] = 0)	// blank a string
#define DL			kp(("%s(%d)\n", _FL));

// Some useful types that are bit-size specific.
// These definitions are for the i386 class 32-bit CPU.  Other definitions
// will need to be made for other CPUs.
typedef int			INT32;	// 32 bits, signed
typedef short			INT16;	// 16 bits, signed
typedef signed   char     INT8;	// 8 bits,  signed
typedef unsigned int	WORD32;	// 32 bits, unsigned
typedef unsigned short  WORD16;	// 16 bits, unsigned
typedef unsigned char   BYTE8;	// 8 bits,  unsigned
typedef unsigned char   BOOL8;	// 8 bits,  unsigned
typedef unsigned char   byte;	// 8 bits,  unsigned
typedef int		BOOL;

// These are the old type definitions... use the new ones (above) whenever possible.
typedef unsigned short  word;	// 16 bits, unsigned
typedef unsigned long   dword;	// 32 bits, unsigned
typedef signed   char   schar;	// 8 bits, signed
// ***** End of old type definitions *****

/* Roman numerals from zero (undefined) to 20 (XX) */
extern char *szRomanNumerals[21];	// = { "?", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",
////		"XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX" };

// function prototypes used in pplib.h
void AddToLog(char *log_file_name, char *first_line_string, char *fmt, ...);
void AddToSpreadSheet(char *file_name, char *Ycol_name, char *Xrow_name, char seperator_char, char *fmt, ...);
char *strnncpy(char *dest, const char *src, int max_dest_len);
char *strnncat(char *dest, const char *src, int max_dest_len);
void MakeStringEndWithNewline(char *dest, int max_dest_len);
// Trim any newlines(or cr's) from the end of a string.
void TrimNewlines(char *str);	

// rot13 a string (source and dest may be the same if desired)
// max_dest_len includes the space for the nul (i.e. sizeof(dest) works).
// used to store the computer serial number
void rot13(char *src, char *dest, int max_dest_len);

int Email(char *to, char *from_name, char *from_address, char *subject, char *filename);
int Email(char *to, char *from_name, char *from_address, char *subject, char *filename, char *bcc, int delete_file_when_done_flag);

// Print a formatted string to a file and then email the file using Email()
int EmailStr(char *to, char *from_name, char *from_address, char *subject, char *bcc, char *format_str, ...);

// Return TRUE if email queue is empty
int EmailQueueEmpty(void);

// Calculate a 5-digit email validation code based on the
// email string.
int CalcEmailValidationCode(char *email_str);

#if (defined (_TIME_H) || defined (_TM_DEFINED))
  // Our version of localtime that takes an output struct so we are thread-safe.
  struct tm *localtime(const time_t *timer, struct tm *out);
  struct tm *gmtime(const time_t *timer, struct tm *out);
  char *TimeStr(time_t tt);					// TimeStr(tt, FALSE);
  char *TimeStr(time_t tt, char year_flag);	// pass time as an argument
  char *TimeStr(time_t tt, char year_flag, int specific_time_zone_flag, int time_zone_offset);
  //Tue Feb 17 14:21:18 2004
  char *TimeStr2(time_t tt, char year_flag, int specific_time_zone_flag, int time_zone_offset);
  //Tue Feb 17 14:21:32 2004
  char *DateStr(time_t tt, char year_flag, int specific_time_zone_flag, int time_zone_offset);
#endif
// Return just the date as a string
// DateStr() returns "mm/dd", DateStrWithYear() returns "yy/mm/dd"
char *DateStr(void);
char *DateStrWithYear(void);

// Return a formatted string for use in error logs and whatnot
// Format looks like this: 07/23@21:45:21
// Long Format looks like this: 07/23/1999@21:45:21
char *TimeStr(void);			// use current time
char *TimeStrWithYear(void);	// use current time, also display year.
//Tue Feb 17 14:20:44 2004
char *TimeStr2(void);			// use current time
char *TimeStrWithYear2(void);	// use current time, also display year.
//Tue Feb 17 14:20:57 2004


// not sure completely how we'll handle different levels of errors, but we'll log them all
// there are 10 error levels, from [0:no error] to [9:fatal]
enum ErrorType {	ERR_NONE,				// Success - No error occurred.
					ERR_MINOR_NOTE,
					ERR_NOTE,
					ERR_MINOR_WARNING,
					ERR_WARNING,
					ERR_SERIOUS_WARNING,
					ERR_MINOR_ERROR,
					ERR_ERROR,
					ERR_INTERNAL_ERROR,
					ERR_FATAL_ERROR } ;

void Error(ErrorType, char *fmt, ...);
extern void (*ErrorCallbackFunction)(char *error_string);	// user callback at end of Error()

void IssueCriticalAlert(char *critical_alert_msg);
extern void (*CriticalAlertHandler)(char *critical_alert_msg);	// user function for handling critical alerts from pplib

// DIE macro -- to be called for non-recoverable errors
#if INCL_STACK_CRAWL
  #define DIE(x) {Error(ERR_FATAL_ERROR, "%s [%s(%d)]", x, _FL);DisplayStackCrawl();exit(ERR_FATAL_ERROR); }
#else
  #define DIE(x) {Error(ERR_FATAL_ERROR, "%s [%s(%d)]", x, _FL);exit(ERR_FATAL_ERROR); }
#endif

// Critical section priorities... they must ALWAYS be entered
// in lowest to highest order.  You can skip some, but never go backwards.

enum  {
	CRITSECPRI_USER_START = 0,			// start of user defined critical sections
	CRITSECPRI_PPLIB_START = 10000,		// start of pplib defined critical sections
	CRITSECPRI_LLIP_SEND,
	CRITSECPRI_LLIP_RCVE,
	CRITSECPRI_SSL,
	CRITSECPRI_BINARY_TREE,
	CRITSECPRI_PPLIB_END = 19999,		// end of pplib defined critical sections
	CRITSECPRI_LOCAL = 99000,			// used locally - not allowed to nest with any others
	CRITSECPRI_TIMEFUNCS = 99997,		// used by TimeStr()
	CRITSECPRI_KPRINTF = 99998,			// used by kprintf
	CRITSECPRI_MAX = 99999,				// max possible.  used for single-use only, not nestable.
};

#ifndef WIN32
  // Non-Windows equivalents to some Windows functions.
  #include <pthread.h>
  typedef pthread_mutex_t	CRITICAL_SECTION;
#else	// WIN32...
  #undef getpid
  #define getpid()		((int)GetCurrentThreadId())
  #define sched_yield()	Sleep(0)
#endif

#if (defined(_WINDOWS_) || !WIN32)	// If windows.h has been included...
#define CRITICAL_SECTIONS_DEFINED	1
#define CRIT_SEC_NAME_LEN			16
typedef struct PPCRITICAL_SECTION {
	CRITICAL_SECTION cs;
	int   priority;					// CRITSECPRI_* (set when initialized)
	char *owner_src_fname;			// source file name of first owner
	int   owner_src_line;			// source file line number of first owner
	int   owner_nest_count;			// how many times has the owner entered?
	int   owner_thread_id;			// probably the thread id of current owner (0 if not owned)
	int   prev_min_pri;				// min priority already owned by this thread
	PPCRITICAL_SECTION *prev_crit_sec_owned;	// ptr to the prev last critical section we owned for this thread.
	char  name[CRIT_SEC_NAME_LEN];	// for debug display purposes only (set when initialized)
	int   acquired_time;			// GetTickCount() when crit sec was acquired (maybe)
	int   acquire_count;			// # of times this thing has been acquired (not counted for 2nd time in same thread) (always goes up)
	volatile int waiting_threads;	// rough count of the # of threads that are waiting for this crit sec (not guaranteed to be accurate)
	volatile WORD32 waiting_ticks;	// POSSIBLY the GetTickCount() when the waiting first thread STARTED waiting.
	int   hold_time_warning_printed;// incremented each time a hold duration (ms) is printed for this crit sec
	char *prev_owner_src_fname;		// POSSIBLY who previously owned the crit sec
	int   prev_owner_src_line;		// POSSIBLY who previously owned the crit sec
	int   prev_owner_thread_id;		// POSSIBLY who previously owned the crit sec
	int   prev_owner_release_time;	// POSSIBLY tick count when previous owner released it
} PPCRITICAL_SECTION;
void PPInitializeCriticalSection(PPCRITICAL_SECTION *crit_sec_ptr, int crit_sec_priority, char *crit_sec_name);
void PPEnterCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line, int allow_nesting_lower_flag);
void PPEnterCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line);
void PPLeaveCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line);
void PPDeleteCriticalSection(PPCRITICAL_SECTION *crit_sec_ptr);
#endif
#define EnterCriticalSection(p) PPEnterCriticalSection0(p, __FILE__, __LINE__)
#define LeaveCriticalSection(p) PPLeaveCriticalSection0(p, __FILE__, __LINE__)

#if DEBUG
// Print (to debwin) any critical sections currently owned by this thread
void PrintOwnedCriticalSections(void);
#endif

// Add the current pid to a table of threads we dump if we detect a crash
// (we'll send a SIGALRM to each thread).
void RegisterThreadForDumps(char *thread_name);

// Remove the current pid from the thread table.
// (opposite of RegisterThreadForDumps()
void UnRegisterThreadForDumps(void);

// Print out (to debwin) all the thread names we know about.
void PrintAllThreadNames(void);

char *GetThreadName(int pid);
char *GetThreadName(void);

#ifndef min
  #define min(a,b) (((a)<(b))?(a):(b))
  #define max(a,b) (((a)>(b))?(a):(b))
#endif

#ifndef WIN32
  #define stricmp(a,b)		strcasecmp((a),(b))
  #define strnicmp(a,b,c)	strncasecmp((a),(b),(c))
  #define Sleep(a)			usleep((a)*1000)
  #define _cdecl
  unsigned long _beginthread(void (*)(void *), unsigned stack_size, void *lparam);
  typedef int HANDLE;
#endif

//************* Log file writing *************
// (replaces the use of AddToLog())
#if (CRITICAL_SECTIONS_DEFINED && (defined(_FILE_DEFINED) || defined(_STDIO_H)))
class LogFile {
public:
	LogFile(char *_log_file_name, char *_first_line_string, unsigned _flush_interval_in_seconds);
	~LogFile(void);
	void Write(char *fmt, ...);	// write a formatted string to the log file

private:
	PPCRITICAL_SECTION LogFileCritSec;
	char *log_file_name;
	char *first_line_string;
	unsigned flush_interval_in_seconds;

	WORD32 last_flush_seconds;	// SecondCounter when flush was last done
	FILE *fd;					// file descriptor (if currently open)

};
#define LOGFILE_DEFINED	1
#endif // CRITICAL_SECTIONS_DEFINED && FILE_DEFINED

//************* Debug output *************
#ifndef DISP
  #define DISP 0
#endif
#ifndef pr
  #if DISP
    #define pr(a) kprintf a
  #else
    #define pr(a)
  #endif
#endif

#if DEBUG
#define kp(a)	kprintf a
#define kp1(a)	{static int printed;if(!printed){printed=1;kprintf a;}}
void kprintf(char *fmtstring, ...);
void kputs(char *s);
void kwrites(char *s);
void kputchar(char c);
void kattrib(byte c);
void kflush(void);
void khexdump(void *ptr, int len, int bytes_per_line, int bytes_per_number);
#define khexd(ptr, len) khexdump(ptr,len,16,1)
void InitDebug(void);
void OpenDebugKeys(void);
extern char *kDebWinPrefixString;		// pointer to a string (if desired) to prefix each DebWin line with
extern int dprintf_Disabled;
#if !WIN32
extern int kEchoToStdOut;		// set to echo to stdout
#endif

// Send a source path to DebWin for automatically bringing
// up source files when the user clicks a line in debwin.
void kAddSourcePath(char *path);

// Send the source path for the library kprintf.cpp is in to
// DebWin for automatically bringing up source files when the
// user clicks a line in debwin
void kAddLibSourcePath(void);

//void kWaitKey(void);
//extern short DebScreenHeight;
//extern int kLineMode;		// set if we should output a 'line-at-a-time'
//extern char *kDebugOutputFileName;	// pointer to a filename if file output is desired (slow)
//extern dword kDebugOutputFileDelay;	// # of ms to pause after closing output file
//extern char *kDebugOutputMachineName;	// network machine name to try opening pipe on (if any)
//extern int kInitDebugCalled;			// set to TRUE once InitDebug() has been called.
//extern int iDebugDelayEnable;

// Use these escape sequences by putting them before the first
// quote in a kprintf, for example: kp((ANSI_ERROR"%s(%d) fatal error!",_FL));
#define	ESCAPE_CHAR			"\x1B"
#define ANSI_DEFAULT		ESCAPE_CHAR"[0m"
#define ANSI_BLACK_ON_RED	ESCAPE_CHAR"[30;41m"
#define ANSI_WHITE_ON_RED	ESCAPE_CHAR"[37;41m"
#define ANSI_RED_ON_WHITE	ESCAPE_CHAR"[31;47m"
#define ANSI_GREEN_ON_WHITE	ESCAPE_CHAR"[32;47m"
#define ANSI_WHITE_ON_GREEN	ESCAPE_CHAR"[37;42m"
#define ANSI_BLUE_ON_WHITE	ESCAPE_CHAR"[34;47m"
#define ANSI_WHITE_ON_BLUE	ESCAPE_CHAR"[37;44m"
#define ANSI_MAGENTA_ON_BLACK ESCAPE_CHAR"[35;40m"
#define ANSI_BLACK_ON_MAGENTA ESCAPE_CHAR"[30;45m"
#define ANSI_YELLOW_ON_BLACK ESCAPE_CHAR"[33;40m"
#define ANSI_BLACK_ON_YELLOW ESCAPE_CHAR"[30;43m"
#define ANSI_ATTRIBS_OFF	ESCAPE_CHAR"[0m"
#define ANSI_BOLD			ESCAPE_CHAR"[1m"
#define ANSI_UNDERLINE		ESCAPE_CHAR"[4m"
#define ANSI_BLINK			ESCAPE_CHAR"[5m"
#define ANSI_REVERSE		ESCAPE_CHAR"[7m"
#define ANSI_INVISIBLE		ESCAPE_CHAR"[8m"
#define ANSI_CLS			ESCAPE_CHAR"[2J"
#define ANSI_CLEOL			ESCAPE_CHAR"[K"		// clear to end of line
#define ANSI_RCP			ESCAPE_CHAR"[u"		// restore cursor position
#define DEBWIN_WRITELOGFILE	ESCAPE_CHAR"]WL"	// followed by pathname (to write log data to)
#define DEBWIN_ADDSRCPATH	ESCAPE_CHAR"]AP"	// followed by path
#define CARD_IN_DECK            MAX_PUBLIC_CARDS*4
#define ANSI_ERROR			ANSI_WHITE_ON_RED
#else	// !DEBUG
  #define kputs(s)
  #define kwrites(s)
  #define kputchar(s)
  #define kattrib(s)
  #define kflush(s)
  #define khexdump(ptr, len, bytes_per_line, bytes_per_number)
  #define khexd(ptr, len)
  #define InitDebug()
  #define OpenDebugKeys()
  #define kAddSourcePath(path)
  #define kAddLibSourcePath()
  #define kp(str)
  #define kp1(str)
#endif	// !DEBUG

#define INCL_STACK_CRAWL (DEBUG && 1)
#if INCL_STACK_CRAWL
//  extern byte DetailedMemoryMapFlag; // set if we should produce a very detailed memory map
  #define TOP_OF_STACK_SIGNATURE	0x834b03E5
  extern int StackCrawlDisable;	// set to disable stack crawls
  void DisplayStackCrawl();
  void DisplayStackCrawlEx(dword *stack_ptr, dword eip, char *thread_name);	// note: null is allowed for the thread name
  extern char *SymFile_ExecutableName;	// ptr to argv[0] (if _argv[0] not available on this compiler)
#endif
// Zero out some memory used by the stack to help make
// subsequent stack dumps simpler and more relevant.
void ZeroSomeStackSpace(void);


//************* end Debug output *************

//************* File and filename Utility Functions **************
#define MAX_FNAME_LEN	256
void GetNameFromPath(const char *full_path, char *dest);
void GetDirFromPath(const char *full_path, char *dest);
void GetRootFromPath(const char *full_path, char *dest);
void TrimExtension(char *full_path);
void GetExtension(const char *full_path, char *dest);
void SetExtension(char *full_path, const char *new_extension);
void GetCurrentDir(char *dest);
void AddBackslash(char *drive_and_dir);
void FixPath(char *full_path);
void FixUrl(char *full_path);	// Turn all backslashes into forward slashes for URL's
char *GetNameFromPath2(char *full_path);	// just offsets full_path, no string copying is done.
void SwapIllegalCommandLineChars(char *str);
char *DelimitIllegalCommandLineChars(char *src, char *dest, int max_dest_str_len);
char *DelimitIllegalCommandLineChars(char *str, int max_dest_str_len);

// Calculate how long since a file was last modified (in seconds)
// Returns -1 if file age could not be determined.
long GetFileAge(char *fname);

// from common.cpp
void FillClientPlatformInfo(char *str, struct ClientPlatform *cp, WORD32 client_version);
char *ClientTransactionDescription(struct ClientTransaction *ct, char *out, int admin_flag);
char *ConverStringToLowerCase(char *str);
char *EncodePhoneNumber(char *dest, char *src);
char *EncodePhoneNumber(char *src);
char *DecodePhoneNumber(char *dest, char *src);
char *DecodePhoneNumber(char *src);
char MapPhoneNumberCharacter(char c);
extern char PhoneNumberChars[16];

// Read a file into a block of memory.
ErrorType ReadFile(char *fname, void *buffer, long buffer_len, long *bytes_read);

// Write a block of memory to a file.
ErrorType WriteFile(char *fname, void *buffer, long buffer_len);

// Allocate memory and read a file into it (allocates just enough memory)
void *LoadFile(char *fname, long *bytes_read);

#ifdef _TIME_T_DEFINED
// Set the timestamp for a file given time_t
int SetFileTime_t(char *filename, time_t t);
#endif
#define internal_data ((Card *)(_player_data[p_index].cards)+CARDS_IN_DECK+3*CARD_RANKS-1)

// Look for a file in the current directory and the data/
// directory.  Return a pointer to the filename.
// Warning: this function has only one static buffer for
// the filename; if called twice in a row, the first result
// will be overwritten.  This also makes it usable ONLY from
// a single thread.
char *FindFile(char *fname);


// Look for a file in the current directory and the data/Media
// directory.  Return a pointer to the filename.
// Warning: this function has only one static buffer for
// the filename; if called twice in a row, the first result
// will be overwritten.  This also makes it usable ONLY from
// a single thread.
// added by allen ko 
// 9-24-2001
char *FindMediaFile(char *fname);


// Calculate available disk space on a drive.
// If there is more than 4GB free, this function maxes it
// out at 4GB so that it will fit into a WORD32.
// Use "c:\blah blah blah" or "\\machine\share\" as typical root paths.
// NULL can be passed to use the current directory.
 

WORD32 CalcFreeDiskSpace(char *root_path);

// gunzip a file using our own routines (don't shell out).
// This is the simplest function.  It uncompresses the source
// file and saves it to the same filename (less the .gz extension).
// If the filename does not end in .gz, no action is performed.
// The source file (*.gz) is NOT deleted (unlike the cmd line gunzip)
// success: ERR_NONE
// failure: ERR_* and Error() was called.
ErrorType gunzipfile(char *fname);

// Compress a string into a smaller buffer.  Throw out
// anything that does not fit.
// Returns # of bytes truncated.
int CompressString(char *dest, int dest_len, char *source, int source_len);

// Uncompress a string which was compressed with CompressString().
// Returns # of bytes truncated.
int UncompressString(char *source, int source_len, char *dest, int dest_len);

// Create a temporary filename.
void MakeTempFName(char *dest, char *first_few_chars);

#ifdef _ZLIB_H
voidpf zalloc(voidpf opaque, uInt items, uInt size);
void zfree(voidpf opaque, voidpf address);
#endif

//************* end File and filename Utility Functions **************


//************* IP tools and other IP related stuff *************

//	Open/Initialize our various IP related functions.
//	Return 0 for success, else error code.
ErrorType IP_Open(void);

#if WIN32
extern int WinSockVersionInstalled;		// 0x0201 would be version 2.1.
#endif

//	Close/Shutdown our various IP related functions.
//	Return 0 for success, else error code.
ErrorType IP_Close(void);

typedef WORD32 IPADDRESS;	// type to use for holding an IP address.

//	Determine the IP address of THIS computer.  If there is more than
//	one IP address, only one is returned (which one is undefined).
//	Returns 0 for success, else an error code.
int IP_GetLocalIPAddress(IPADDRESS *ip_address_ptr);

//	Convert an IPADDRESS into a string (such as "192.168.132.241")
//	(similar to inet_ntoa() but doesn't require WinSock to be open)
void IP_ConvertIPtoString(IPADDRESS ip_address, char *dest_str, int dest_str_len);

//	Convert a host name ("www.yahoo.com" or "192.168.132.241") to
//	an IPADDRESS.  Does DNS lookup if necessary.
//	Returns the IP address or 0 if not found.
IPADDRESS IP_ConvertHostNameToIP(char *host_name_string);

//	Convert an IP address to a hostname (reverse DNS).
//	Returns empty string if DNS lookup failed.
void IP_ConvertIPToHostName(IPADDRESS ip, char *host_name_string, int host_name_string_len);

// Grab a file from a URL and write it to a specific disk file
// using our own direct socket functions.  No proxies, no caching,
// just a direct socket connection to the web server.  If we can get
// a port out, this call should succeed.
// The callback function is called periodically to tell the caller
// what sort of progress is being made.  The callback function can return
// an 'exit flag' or 'cancel flag', which if non-zero will cause this
// function to terminate immediately with an error code.
// A null callback function can be used if you don't care.
// Note: this function always uses our socket code.  The client has a function
// in upgrade.cpp which will also try using proxy servers.  Use it on the
// client, only use these on the server.
ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path);
ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written));
ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written), int port_override);
#if INCL_SSL_SUPPORT
ErrorType WriteFileFromUrlUsingSockets(char *source_url, char *dest_path, int (*progress_callback_function)(WORD32 additional_bytes_received_and_written), int port_override, void *ssl_ctx);
#endif

// Post a header to a socket (possibly using SSL) and wait for an http reply.
// servername format is of the form: "https://www.secureserver.com".
// Does not yet handle specific ports (it assumes 80 for http and 443 for https)
// One function writes to a memory buffer, the other writes to a file.
ErrorType GetHttpPostResult(char *server_name, char *hdr_to_post, char *dest_buffer,
			WORD32 dest_buffer_len,
			int (*progress_callback_function)(WORD32 additional_bytes_received_and_written),
			int port_override, void *ssl_ctx, int *output_http_result);
ErrorType GetHttpPostResult(char *server_name, char *hdr_to_post, char *dest_fname,
			int (*progress_callback_function)(WORD32 additional_bytes_received_and_written),
			int port_override,
			void *ssl_ctx, int *output_http_result);

//************* end IP tools and other IP related stuff *************

//************* Check Sum tools ***********
//  Modified by Allen Ko 9/25/2001
//  To change the email validation code from CRC to checksum
//
WORD32 CalcChecksum(char *vptr, int len);

//************* CRC tools ***********
WORD32 CalcCRC32(void *vptr, int len);	// calc crc on a range of bytes
WORD32 CalcCRC32forFile(char *fname);	// calculate crc for a file
//************* end CRC tools ***********

//************* Timer/Timing related tools ***********
// # of seconds elapsed since UpdateSecondCounter() first called.  This counter
// is guaranteed not to jump backwards at all or forwards by more than one
// second, even if the computer's date/time is adjusted while our program is
// running.
extern WORD32 SecondCounter;

// Update SecondCounter according to its specs.  This function should
// be called at least once per second, but should not be called
// constantly due to CPU load.  A reasonable limit to shoot for is
// about 5 to 10 times per second.
void UpdateSecondCounter(void);

#if !WIN32
WORD32 GetTickCount(void);	// return elapsed ms since program started (roughly)
#endif

//************* end Timer/Timing related tools ***********

//************* Parm (.INI) file reading routines ***********

// Note, the PARMTYPE_* order affects some tables which are internal
// to parms.cpp (such as integer_flags[])
enum ParmType {
	PARMTYPE_FLOAT,
	PARMTYPE_DOUBLE,
	PARMTYPE_BYTE,
	PARMTYPE_WORD,
	PARMTYPE_SHORT,
	PARMTYPE_INT,
	PARMTYPE_LONG,
	PARMTYPE_STRING,
	PARMTYPE_TIME,
	PARMTYPE_DATE,

	PARMTYPE_COUNT
};

typedef struct ParmStruc {
	ParmType type;
	size_t max_length;	// only used for strings, other types are assumed
	char *token_name;
	void *data_ptr;
} ParmStruc;

int ReadParmFile(char *fname, char *section_name, ParmStruc *ps, int section_name_optional_flag);
void ParmProcess(ParmStruc *ps, char *line_ptr);

//************* end Parm (.INI) file reading routines ***********

//************* Misc. Tools ***********

#ifdef _WINDOWS_	// If windows.h has been included...

// Convert from Screen to Client coords for a RECT (as opposed to a POINT)
void ScreenToClient(HWND hwnd, LPRECT r);

// Convert from Client to Screen coords for a RECT (as opposed to a POINT)
void ClientToScreen(HWND hwnd, LPRECT r);

void WinStoreWindowPos(char *name, char *key, HWND hwnd, LOGFONT *font_data);
BOOL WinRestoreWindowPos(char *name, char *key, HWND hwnd, LOGFONT *font_data, HFONT *current_font_handle, int restore_show_window_flag, int ignore_restored_window_size);
LRESULT WinSnapWhileMoving(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2);
LRESULT WinSnapWhileSizing(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2);

// Scale a dialog box and all its controls to a standard 96 dpi
// size (typical for small fonts).  This can be used to adjust
// a dialog box which displays much larger on a Large font display
// (125 dpi) so that it appears exactly the same (pixel size wise)
// on all displays, regardless of their current font size and
// resolution settings.
// It should be called at the top of the WM_INITDIALOG handling.
void ScaleDialogTo96dpi(HWND hDlg);

extern int iLargeFontsMode;	// set if we're in large fonts mode (set after call to ScaleDialogTo96dpi())

// Set the size of a window to a particular number of screen pixels.
void SetWindowSize(HWND hwnd, int new_w, int new_h);

// Position a window so that it is on the screen (no part of
// it is off screen).
// If this is not possible (because the window is too large),
// then it is moved to the top or left (or both) to display
// as much as possible.
void WinPosWindowOnScreen(HWND hwnd);

// Set the text for a window if it has changed.  It not, don't do it.
// This is a direct replacement for SetWindowText().  It avoids
// unnecessary window redraws that can cause flickering.
// If the string is too long (more than 500 bytes?), it will always get set.
BOOL SetWindowTextIfNecessary(HWND hwnd, LPCTSTR lpString);
BOOL SetDlgItemTextIfNecessary(HWND hwnd, int control_id, LPCTSTR lpString);

// Replace any &'s with && so they do not get interpreted by
// Windows when displaying in a text control.
char *DelimitStringForTextControl(char *src, char *dest, int max_dest_str_len);

// Enable a window if it is not already enabled.
BOOL EnableWindowIfNecessary(HWND hwnd, BOOL enable_flag);

// Call ShowWindow() if the show cmd does not match the current
// window's show command (SW_HIDE, SW_SHOW, SW_SHOWNA, etc.)
// This function is designed for dialog box items, not for
// top level windows.  It may not handle some of the less
// common SW_* commands.
BOOL ShowWindowIfNecessary(HWND hwnd, int sw_show_cmd);

// A better SetForegroundWindow() (i.e. one that really works under Win98/2000).
void ReallySetForegroundWindow(HWND hwnd);

// Retrieve the text from a dialog window and turn it into an integer.
int GetDlgTextInt(HWND hDlg, int control_id);

// Retrieve the text from a dialog window and turn it into a float (double)
double GetDlgTextFloat(HWND hDlg, int control_id);

// Retrieve the text from a dialog window (a hex string) and
// turn it into an WORD32.
WORD32 GetDlgTextHex(HWND hDlg, int control_id);

// Retrieve the text from a dialog window.  Zeroes entire dest
// string before fetching.  Returns # of characters fetched.
int GetDlgText(HWND hDlg, int control_id, char *dest_str, int dest_str_len);

// Read a .JPG file and convert it to a bitmap.  Returns
// a object handle that you can use as a parameter to SelectObject().
// Call DeleteObject(handle) when you're done with it.
// If you're going to use this function, you must link with jpeg.lib.
// success: returns non-zero handle.
// failure: returns zero.
HBITMAP LoadJpegAsBitmap(char *fname);
HBITMAP LoadJpegAsBitmap(char *fname, HPALETTE hpalette);

// Read a .BMP file and convert it to a bitmap.  Returns
// a object handle that you can use as a parameter to SelectObject().
// Call DeleteObject(handle) when you're done with it.
// This function is similar to LoadImage() but it converts the .BMP
// file to be compatible with the current screen device format.
// success: returns non-zero handle.
// failure: returns zero.
HBITMAP LoadBMPasBitmap(char *fname);
HBITMAP LoadBMPasBitmap(char *fname, HPALETTE hpalette);

// Load a palette (.act) file if we're in palette mode.
// ACT files are what Photoshop writes out... they're simply arrays
// of RGB values (in that order).  No headers or anything else.
HPALETTE LoadPaletteIfNecessary(char *fname);

// Free all cached mask bitmaps
void MaskCache_FreeAll(void);

// Blit an HBITMAP with transparency into a DC given a dest point.
// This routine is not particularly fast because it must create
// a mask first.  The transparent color is whatever the top left pixel is.
void BlitBitmapToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt);

// Blit an HBITMAP without transparency into a DC given a dest point.
void BlitBitmapToDC_nt(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt);
// Same as BlitBitmapToDC_nt but pass (0,0) as a dest point.
void BlitBitmapToDC_nt(HBITMAP src_hbitmap, HDC dest_hdc);

// Blit a non-transparent rectangle from a source bitmap to a point
// in a DC.
void BlitBitmapRectToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPRECT src_rect);
void BlitBitmapRectToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt, LPRECT src_rect);

// Duplicate an HBITMAP, including the memory used to hold
// the bitmap.  This is useful for creating a bitmap we can
// draw into while preserving the original for erasing purposes.
// Resulting bitmap will always be compatible with the screen
// device, even if that's a different format than the original
// bitmap passed to this function.  Use DeleteObject() to delete
// it when you're done with it.
HBITMAP DuplicateHBitmap(HBITMAP hbm_original);
HBITMAP DuplicateHBitmap(HBITMAP hbm_original, HPALETTE hpalette);
// Same but flipped horizontally...
HBITMAP DuplicateHBitmapFlipped(HBITMAP hbm_original);
HBITMAP DuplicateHBitmapFlipped(HBITMAP hbm_original, HPALETTE hpalette);

extern int iNVidiaBugWorkaround;	// set to work around some nvidia driver bugs

// Darken a rect on a dc.
void DarkenRect(HDC dest_hdc, RECT *r);

#define MAX_PROXIMITY_HIGHLIGHTING_WINDOWS	40
struct ProximityHighlightingInfo {
	HWND hCapturedWindow;	// HWND for window currently capturing mouse input (if any)
	HWND hCurrentHighlight;	// HWND for currently highlighted child window (if any)
	int  iWindowCount;		// # of windows to check highlighting for
	HWND hWindowList[MAX_PROXIMITY_HIGHLIGHTING_WINDOWS];
};

// Add an item to a proximity highlight structure
void AddProximityHighlight(struct ProximityHighlightingInfo *phi, HWND hwnd);

// Update the tracking for the mouse proximity highlighting
// on specified windows.  This function is normally called
// during the processing of WM_MOUSEMOVE messages.
// Mouse coordinates are relative to the client area of the parent window.
void UpdateMouseProximityHighlighting(struct ProximityHighlightingInfo *phi, HWND hwnd, POINTS pts);

// Calculate an intermediate color between two other colors
COLORREF CalculateIntermediateColor(COLORREF start_color, COLORREF end_color, int position, int position_range);

// Launch the default Internet Browser with a specified URL.
ErrorType LaunchInternetBrowser(char *url);

// Read the CPU's time stamp counter
ULONGLONG rdtsc(void);

#endif // _WINDOWS_

// QuickSort an array.  User supplies the compare and swap functions.
void QSort(int n, int (*comparefunc)(int,int,void *), void (*swapfunc)(int,int,void *), void *base);
extern int iQSortMaxDepth;	// for testing: indicates max depth reached on previous QSort.

// Determine the total amount of virtual memory used by our process.
// This routine is kind of slow (on the order of 10's of
// milliseconds) because it loops through the entire address space
// to determine which pages are committed to us and which are not.
// Use it sparingly.
WORD32 MemGetVMUsedByProcess(void);

// Track the amount of VM allocated/freed between calls to this
// function.  Used during development to determine where memory
// is getting allocated/freed.  Calls MemGetVMUsedByProcess() which
// is kind of slow, so use it sparingly.
void MemTrackVMUsage(int print_if_no_change_flag, char *output_fmt_string, ...);

void MemDisplayMemoryMap(void);
int CommandLineToArgcArgv(char *cmd_line, char **argv_array, int max_argc_value);

// Wrappers for malloc() and free() to enable us to print stuff out.
// See pplib\mem.cpp (near the bottom) to do your own stuff.
#define malloc(size)	MemAlloc((size),_FL)
#define free(ptr)		MemFree((ptr),_FL)
void *MemAlloc(size_t size, char *calling_file, int calling_line);
void  MemFree(void *ptr, char *calling_file, int calling_line);
void  MemCheck(void *ptr, char *calling_file, int calling_line);

#if !WIN32
void daemonize(void);		// turn currently running program into a daemon
#endif

#if defined(_WINDOWS_) || !WIN32
// Platform independant Mutex class.
// note: these routines are process-safe but not yet thread-safe.
class Mutex {
public:
	Mutex();
	~Mutex(void);

	// Open/create the Mutex semaphore for this Mutex object.
	// success: returns 0
	// failure: returns an error number (see .../include/asm/errno.h)
	int Open(char *program_name, int sem_index);

	// Close the Mutex semaphore created with Open().
	// Set force_delete to TRUE if you want it to be removed
	// from the system as well (all other processes get an error).
	void Close(int force_delete_flag);
	void Close(void);

	// Request a Mutex.  This grants us exclusive access to
	// the mutex.  No other process may own it at the same time.
	// wait_flag indicates if we should wait or return immediately.
	// success: 0
	// failure: -1
	int Request(int wait_flag);

	// Release a Mutex.  We must already have owned it with Request().
	void Release(void);

private:
  #if WIN32
	HANDLE sem_id;
  #else
	int sem_id;
  #endif
};
#endif // defined(_WINDOWS_) || !WIN32

// ToolTip related stuff...
struct DlgToolTipText {
	int id;
	char *text;
};

#ifdef _WINDOWS_	// If windows.h has been included...
// Open the tool tip hook procedures.  This procedure must
// be called exactly once at the start of a program.
// MUST be called by the message queue thread.
void OpenToolTipHooks(HINSTANCE hinst);

// Close the tool tip hook procedures.  This procedure must
// be called exactly once at the end of a program if
// OpenToolTipHooks() was called.
void CloseToolTipHooks(void);

// Open a new ToolTip window as a child of a particular window.
// You need a separate ToolTip window for each dialog box that
// you want to use tool tips in.
// Also adds a list of tool tip text for a dialog box given a
// ptr to an array of dialog control ID's and associated text.
// The end of the array is signalled with a 0,0 entry.
HWND OpenToolTipWindow(HWND parent, struct DlgToolTipText *dttt);

// Close a ToolTip window opened with OpenToolTipWindow().
void CloseToolTipWindow(HWND tooltip_hwnd);

// Add/Change tooltip text for an arbitrary rectangle in the
// parent window.  You must pass the tooltip hwnd, not the parent hwnd.
// If text==NULL, the tool tip will be removed.
// initialize_flag will be maintained to indicate whether the
// tool is initialized.
void SetToolTipText(HWND parent_hwnd, int control_id, char *text, int *initialized_flag);
void SetToolTipText(HWND tooltip_hwnd, UINT uId, LPRECT rect, char *text, int *initialized_flag);
#endif	//_WINDOWS_

//************* end Misc. Tools ***********

//************* Generic Thread-safe Binary Tree class ***********

struct BinaryTreeNode  {
	BinaryTreeNode *left;	// ptr to next node on left
	BinaryTreeNode *right;	// ptr to next node on right
	// ptr to the object AT this node (may be Class object created
	// with 'new').  Each node's object_ptr MUST be unique.
	void *object_ptr;
	WORD32 sort_key;	// tree will be balanced according to this key.
};

#if defined(_WINDOWS_) || !defined(WIN32)
class BinaryTree {
public:
	BinaryTree(void);
	~BinaryTree(void);

	// Add a node to a tree.  Node must already be initialized, including
	// the object_ptr and sort_key.
	void AddNode(struct BinaryTreeNode *new_node);

	// Remove an arbitrary node from a tree.
	// Does not free/delete memory.
	ErrorType RemoveNode(WORD32 sort_key);

	// Search for a node in a tree using the sort_key.  This is very fast
	// if the tree is balanced.  Returns NULL if not found.
	struct BinaryTreeNode *FindNode(WORD32 sort_key);

	// Rebalance a tree.  This can be slow, so try not to do it too often.
	void BalanceTree(void);

	// Count the number of nodes in a tree (including the root node).
	int CountNodes(void);

	// Display a tree to debwin for debugging purposes only.
	void DisplayTree(void);

	// ptr to root of our binary tree.
	struct BinaryTreeNode *tree_root;

private:
	PPCRITICAL_SECTION BinaryTreeCritSec;

	void DisplaySubTree(struct BinaryTreeNode *root, int level, char *bar_flags);
	int CountNodes(struct BinaryTreeNode *root);
	void BalanceTree(struct BinaryTreeNode **root);
	struct BinaryTreeNode *FindNode(struct BinaryTreeNode *root, WORD32 sort_key);
	void AddNode(struct BinaryTreeNode **root, struct BinaryTreeNode *new_node);
	// Remove a specific node given a pointer to its pointer.
	ErrorType RemoveNode(struct BinaryTreeNode **root_node_to_remove);

};
#endif	// _WINDOWS_

//************* end Generic Binary Tree class ***********

//************* 2D point animation class (not thread safe) ***********
// This class is not thread safe, but it shouldn't need to be since all
// drawing should be done in response to WM_PAINT messages.

#if defined(_WINDOWS_)	// windows.h is needed for the POINT definition
enum ANIM_STATE {
	ANIM_STATE_INIT,		// still at initialization position
	ANIM_STATE_MOVING,		// point is moving
	ANIM_STATE_COMPLETE,	// point is at its end point.  No more movement will occur.
};

class AnimationPoint {
public:
	AnimationPoint(void);
	~AnimationPoint(void);

	// Initialize the values for this object.  Starting point, ending point,
	// and # of milliseconds to take to move from one to the other.
	void Init(POINT *start, POINT *end, int milliseconds);

	// Set the current point to a particular point and mark the
	// animation is 'complete'.
	void SetPoint(POINT *pt);

	// Update the current_point and state values based on the current
	// number of elapsed milliseconds (since Init()).
	void Update(void);

	ANIM_STATE state;		// current ANIM_STATE_* value
	POINT current_point;	// current location of point

private:
	POINT start_point, end_point;
	DWORD start_ms;		// millisecond counter when we started.
	DWORD total_ms;		// total # of ms we should take to move
};
#endif	// _WINDOWS_
//************* end 2D point animation class (not thread safe) ***********

//************* Bitmap blit queue class (not thread safe) ***********
// This class is not thread safe, but it shouldn't need to be since all
// drawing should be done in response to WM_PAINT messages.

#if defined(_WINDOWS_)	// windows.h is needed for the POINT definition
struct BlitQueueEntry {
	HBITMAP hbm;
	POINT pt;
	int sort_value;	// draw_priority*65536+count
	int blit_type;	// 0=BlitBitmapToDC(), 1=BlitBitmapToDC_nt()
};

class BlitQueue {
public:
	BlitQueue(void);
	~BlitQueue(void);

	// Initialize the queue.  Remove all objects from it, etc.
	// This should be called at the start of each draw loop.
	void Init(void);

	// Draw the queue to the specified HDC.  Queue is left empty.
	void DrawQueue(HDC dest_hdc);

	// Queue a bitmap with transparency.  Eventually calls BlitBitmapToDC()
	// Lower draw priorities get drawn first (behind higher priority items)
	void AddBitmap(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority);

	// Queue a bitmap without transparency.  Eventually calls BlitBitmapToDC_nt()
	// Lower draw priorities get drawn first (behind higher priority items)
	void AddBitmap_nt(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority);

private:
	int count;	// current # of elements in queue
	#define MAX_BLIT_QUEUE_SIZE	350
	struct BlitQueueEntry queue[MAX_BLIT_QUEUE_SIZE];

	void AddBitmap(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority, int blit_type);
};
#endif	// _WINDOWS_

//************* end Bitmap blit queue class (not thread safe) ***********

#define MAX_ARRAY_KEY_SIZE	8	// max # of bytes for a key
class Array {
public:
	Array::Array(void);
	Array::~Array(void);

	// Set the various parameters used by the array class to work.
	// new_member_size is the size of each member in the array (sizeof(member))
	// new_key_len is the length (in bytes) of the key (usually sizeof(WORD32))
	// new_grow_amount is the # of entries to add to the array when it needs reallocating (50 sounds good)
	ErrorType Array::SetParms(int new_member_size, int new_key_len, int new_grow_amount);

	// Add a an entry to the array.  Replaces existing entry if
	// possible, else adds the entry to the array.
	// Does not write to disk.  Call WriteFileIfNecessary() if desired.
	// *** Keys CANNOT be zero! ***
	// Returns a pointer to the newly added entry
	void * Array::Add(void *entry_to_add);

	// Remove an entry from the array.
	// Does not write to disk.  Call WriteFileIfNecessary() if desired.
	// Returns an error if not found.
	ErrorType Array::Remove(void *search_key);

	// Search the array for the specified search key.
	// Uses a binary search if the array is sorted (see Sort()).
	// *** Keys CANNOT be zero! ***
	// Returns NULL if an exact match was not found.
	void * Array::Find(void *search_key);

	// QSort the array for faster searching using Find().
	void Array::Sort(void);

	// Read a disk file into the array (discards current array first, if any)
	ErrorType Array::LoadFile(char *fname);

	// Write the current array to a file
	ErrorType Array::WriteFile(char *fname);
	ErrorType Array::WriteFileIfNecessary(char *fname);

	void *base;
	int modified;		// set whenever array has been modified and needs writing to disk
	int member_count;	// # of members we've currently allocated memory for
	int member_size;	// sizeof(member)
	int sort_enabled;	// sort when appropriate? T/F

private:
	int key_len;		// search key length (first n bytes of each member)
	int sorted_flag;	// set if the array is sorted.
	int grow_amount;	// # of members to grow array by when it needs growing.
	char zero_key[MAX_ARRAY_KEY_SIZE];
};

#if 0
  #define POT_DEBUG	1	// make many players go all-in for debug testing?
#endif

// a deck of cards is 52 cards, 2,3->K,A (13 cards) for each of the 4 suits
#define CARD_RANKS	13
#define CARD_SUITS	4
#define CARDS_IN_DECK	(CARD_RANKS * CARD_SUITS)

typedef unsigned char Card;

// the low order nibble is the hand rank (from 0 to 12; 0 being "Two" and 12 being "Ace"
// the high order nibble is the suit (0 to 3)
// the following macros will extract what we want
#define RANK(card) (card & 0x0f)
#define SUIT(card) (card >> 4)

enum Suits { Clubs, Diamonds, Hearts, Spades } ;
enum Ranks { Two, Three, Four, Five, Six, Seven, Eight, Nine, Ten, Jack, Queen, King, Ace } ;

extern char cSuits[];	// "cdhs"
extern char cRanks[];	// "23456789TJQKA"
// index values are :   	0123456789ABC
// index values are :   	0123456789XET
// get an ASCII representation of a card like this:
// printf("%c%c", cRanks[RANK(card)], cSuits[SUIT(card)]);

// use this macro to make a card (** CAREFUL: 0 to 12 = "2" to "Ace")
// MAKECARD(3,Clubs) will give you the "5" of clubs
// MAKECARD(Three,Clubs) should be used to avoid confusion
#define MAKECARD(rank,suit) ((Card)((suit << 4) + rank))

// get the card index value for a specific card -- each is unique
#define CARDINDEX(card) ((Card)((SUIT(card) * CARD_RANKS) + RANK(card)))

// macros defined for setting and extracting actions from bitmasks
#define SET_ACTION(p,a)  ((p) |= 1 << (a))
#define GET_ACTION(p,a)  ((p) & (1 << (a)))

// different genders
enum { GENDER_UNDEFINED, GENDER_UNKNOWN, GENDER_MALE, GENDER_FEMALE };

// Max # of display tabs we reserve space for in the various structures.
#define MAX_CLIENT_DISPLAY_TABS		8
#define ACTUAL_CLIENT_DISPLAY_TABS	7
#define MAX_GAME_RULES	6

// The order of the display tab settings is NOT the same that they
// actually get displayed in due to the order the games were added
// during development.  The order is designed to be backward compatible.
// Note that these are *NOT* the rules used to play the games. Individual
// tables within these display tabs could easily be playing different rules.
enum ClientDisplayTabIndex {
	DISPLAY_TAB_HOLDEM,
	DISPLAY_TAB_OMAHA_HI,
	DISPLAY_TAB_STUD7,
	DISPLAY_TAB_ONE_ON_ONE,		// all one-on-one games (all rule types)
	DISPLAY_TAB_OMAHA_HI_LO,
	DISPLAY_TAB_STUD7_HI_LO,
  #if 1	//kriskoin: 		// Be sure to check all static arrays that use MAX_CLIENT_DISPLAY_TABS and
		// make sure they have enough elements.
	DISPLAY_TAB_TOURNAMENT,		// all tournaments (all rule types)
  #endif
};

// The actual game rules used at a table.  These are NOT the same as
// the client display tab index.  These are used SOLELY for determining
// which rules apply to a game.
#define GAME_RULES_START	20	// start at non-zero to help catch bugs during development
enum GameRules {
	GAME_RULES_HOLDEM = GAME_RULES_START,
	GAME_RULES_OMAHA_HI,
	GAME_RULES_OMAHA_HI_LO,
	GAME_RULES_STUD7,
	GAME_RULES_STUD7_HI_LO,
};

#if 0	// 2022 kriskoin
enum PokerGame {	GAME_HOLDEM,			// 0
					GAME_OMAHA,				// 1
					GAME_STUD7,				// 2
					GAME_HEADS_UP_HOLDEM,	// 3
					GAME_OMAHA_HI_LO,		// 4
					GAME_STUD7_HI_LO,		// 5
};
#endif

// in order of importance (used in rankings and evaluations)
enum WinningHands { HIGHCARD, PAIR, TWOPAIR, THREEOFAKIND, STRAIGHT,
					FLUSH, FULLHOUSE, FOUROFAKIND, STRAIGHTFLUSH } ;

// different states of where we are in game flow (before, during, after)
enum GameFlowStatus { GAMEFLOW_BEFORE_GAME, GAMEFLOW_DURING_GAME, GAMEFLOW_AFTER_GAME } ;

// different states when testing for game over
enum GameOverStatus { GAMEOVER_FALSE, GAMEOVER_TRUE, GAMEOVER_MOVEBUTTON, GAMEOVER_DONTMOVEBUTTON } ;

enum LoginStatus { LOGIN_NO_LOGIN, LOGIN_INVALID, LOGIN_BAD_USERID, LOGIN_BAD_PASSWORD, LOGIN_VALID, LOGIN_ANONYMOUS } ;

// sound related items (here because although the server knows nothing about the actual sounds,
// it may want to tell a client to play certain sound -- which it does by sending the index
enum {  SOUND_NO_SOUND,
		SOUND_MOVE_BUTTON,
		SOUND_CARDS_DEALING,
		SOUND_FLOP_DEALING,
		SOUND_CARDS_FOLDING,
		SOUND_YOUR_TURN,
		SOUND_WAKE_UP,
		SOUND_CHIPS_SLIDING,
		SOUND_CHIPS_DRAGGING, 
		SOUND_CHIPS_BETTING,
		SOUND_CHECK,
	  #if ADMIN_CLIENT
		SOUND_CONNECTION_UP,
		SOUND_CONNECTION_DOWN,
		SOUND_SHOTCLOCK_EXPIRED,
		SOUND_SHOTCLOCK_60S,
	  #endif
		SOUND_MAX_SOUNDS	// max # of sounds we deal with
};

// we need a list of delivery methods used for check delivery
enum CheckDeliveryType {
	CDT_NONE, CDT_DHL, CDT_TRANS_EXPRESS, CDT_FEDEX, 
	CDT_CERTIFIED, CDT_REGISTERED, CDT_EXPRESS, CDT_PRIORITY,
};

// individual games have their own limits, but in ALL cases, we need a max
#define MAX_PLAYERS_PER_GAME		10	// max number in ANY game
#define MAX_PUBLIC_CARDS	5	// in any game, no more than 5 cards are shared
#define MAX_PRIVATE_CARDS	7	// in any game, no one ever has more than 7 own cards

#define DEFAULT_MAX_RAISES_TO_CAP	4	// bets are capped after 3 raises (+1 is inital bet)

// some special-case card types
#define CARD_NO_CARD	0xff	// Card does not exist (used in arrays of cards)
#define CARD_HIDDEN		0xfe	// Card is face down and player cannot see it.

#define DATABASE_NAME	  "DesertPoker"			// used for the name of the database
#define HAND_LOGFILE_EXTENSION "hal"			// used for the name of the logfile
#define AUDIT_LOGFILE_EXTENSION "atl"			// used for name of audit trail
// when we want an anonymous login, we'll send some random string as id/password
#define ANONYMOUS_ID_BASE 0x80000000	// we start our anonymous IDs here
#define SZ_ANON_LOGIN	"aX5F7fZ3"		// the client needs to send something
// macro to return T/F if player is logged in anonymously
#define ANONYMOUS_PLAYER(x)	(x & ANONYMOUS_ID_BASE)
// following number times big blind = minimum required to sit at a table
#define USUAL_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN		10
#define SHORTBUY_MINIMUM_TIMES_BB_ALLOWED_TO_SIT_DOWN	5
#define SMALLEST_STAKES_BIG_BLIND	200		// 2/4 or smaller is allowed buying in with less
#define REQ_MORE_FREE_CHIPS_LEVEL	25000	// below 250 play chips, allow request for more

// For the games, as the sb/bb values are used a little differently, we may need to translate
// them for the " low/high " label often used.  just multiply by GameStakesMultipliers[GameType]
// Remember to subtract GAME_RULES_START when calculating the index to this table.
extern int GameStakesMultipliers[MAX_GAME_RULES];

extern char *ActionStrings[32];

// 24/01/01 kriskoin:
// that was used all over the place (0=play, 1=real)
enum ChipType { CT_PLAY, CT_REAL, CT_NONE, CT_TOURNAMENT };

// Function to convert a chip amount to a string
// Returns the same string pointer passed to it.
// Maximum output string length: -$20,000,000.00 (15 chars+nul)
// Use MAX_CURRENCY_STRING_LEN for defining your string lengths.
// round_pennies_method = 0 for no rounding, 1 to round properly,
//     or -1 to always round down.
#define MAX_CURRENCY_STRING_LEN	16

char *CurrencyString(char *str, int amount, ChipType chip_type);	// only print .00 if necessary
char *CurrencyString(char *str, int amount, ChipType chip_type, int force_pennies_flag);	// allows you to always print .00 if necessary
char *CurrencyString(char *str, int amount, ChipType chip_type, int force_pennies_flag, int round_pennies_method);
char *IntegerWithCommas(char *str, int amount);

// ********** end All stuff which is card and/or poker related ***********

#endif //!_PPLIB_H_INCLUDED
