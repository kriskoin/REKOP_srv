#define DISP 0

#include <stdio.h>

#include <stdlib.h>

#include <stdarg.h>

#include <string.h>

#ifdef WIN32

  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers

  #include <windows.h>

  #include <io.h>

  #include <errno.h>

#endif

#include <sys/types.h>

#include <sys/stat.h>

#include <fcntl.h>

#include "pplib.h"



#if DEBUG

#define KPRINTF_MAX_STRING  2000 /* max length of output string */



int DebugPipeHandle;

int DebugPipeType;		// 0=ANSI.SYS style output, 1=HKDW style output.

int iDebugHandleType;	// 0=created with open(), 1=created with CreateFile().

int kLineMode;			// set if we should output a 'line-at-a-time'

int kInitDebugCalled;	// set to TRUE once InitDebug() has been called.

static char kappName[80];

char *kDebWinPrefixString;		// pointer to a string (if desired) to prefix each DebWin line with

#if !WIN32

int kEchoToStdOut;		// set to echo to stdout

#endif

char *kDebugOutputFileName;		// pointer to a filename if file output is desired (slow)

dword kDebugOutputFileDelay;	// # of ms to pause after closing output file



int dprintf_Disabled;

byte InitDebugDisable;	// set to make InitDebug() do nothing except dprintf_Disabled to FALSE.

short DebScreenHeight=500;	// do LOTS at a time.

void InitDebug(void);

void kwrites(char *str);



#define USE_STD_CRITSEC	0	// should normally be zero... 1 bypasses error checking



#if USE_STD_CRITSEC

CRITICAL_SECTION kprintf_CritSec;

#undef EnterCriticalSection	

#undef LeaveCriticalSection	

#else

PPCRITICAL_SECTION kprintf_CritSec;

#endif



//****************************************************************

//  Mon April 14/97 - MB

//

//	Write some data to the debug pipe.  Retry until it gets through

//

static void WriteToDebugPipe(void *data, int len)

{

	int bytes_written;

	int retry_count = 0;



	  #if DISP	//:::
		printf("%s(%d) WriteToDebugPipe: len=%d/%d string='%s'\n",

				_FL,len,strlen((char *)data),data);

	  #endif

#if WIN32

		if (kDebugOutputFileName) {

			// Write the data to a file as well...

			FILE *fd = fopen(kDebugOutputFileName, "a");

			if (fd) {

				fwrite(data, 1, len, fd);

				fclose(fd);

			  #if WIN32	//:::
				if (kDebugOutputFileDelay)

					Sleep(kDebugOutputFileDelay);

			  #endif

			}

		}



retry:	

		if (iDebugHandleType==0) {

			bytes_written = write(DebugPipeHandle, data, len);

		} else {

			// It was created with CreateFile... use WriteFile() instead.

			bytes_written = 0;

			WriteFile((HANDLE)DebugPipeHandle, data, len, (LPDWORD)&bytes_written, NULL);

		}

		if (bytes_written < len) {

			// Writing didn't succeed...

			if (bytes_written > 0) {

				// Partial write succeeded...

				data = (byte *)data + bytes_written;

				len -= bytes_written;

			}

			// Try again a few times....

			if (++retry_count < 10) {

				Sleep(50);	// give debug window a chance to catch up.

				goto retry;

			}

		}

#else // !WIN32

		// Linux only supports writing to a file (for now).

		// In the future we could write a debwin style program and use

		// some sort of IPC to communicate, but for now a file works fine.

		if (kDebugOutputFileName) {

			static int output_file = -1;

			static WORD32 output_file_open_time = 0;

			// Make sure the file gets closed at least once every 30s

			// in case it gets deleted.  This forces us to write to the

			// new file.  We don't want to do that TOO often for performance

			// reasons, but once every 30s will hardly be noticed.

			if (output_file != -1 && SecondCounter >= output_file_open_time + 30) {

				close(output_file);

				output_file = -1;

			}

			if (output_file == -1) {

				output_file = open(kDebugOutputFileName,

						O_CREAT|O_WRONLY|O_NOCTTY|O_APPEND,

						S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);

				//printf("%s(%d) output_file handle = %d\n",_FL, output_file);

				output_file_open_time = SecondCounter;	// keep track of when we opened it.

			}

			if (output_file != -1) {

				write(output_file, data, len);

			  #if 0	//kriskoin: 					// It seems that the data is at least given to the OS

					// and since linux crashes so infrequently, it doesn't

					// seem necessary to do an fdatasync with each output line.

				// If we're in line mode, we don't need to worry too much about

				// syncing the disk file.  Otherwise, do it for every line.

				if (!kLineMode) {

					fdatasync(output_file);

				}

			  #endif

			}

		}

		if (kEchoToStdOut) {

			printf("%s", data);

		}

#endif // !WIN32

}



/****************************************************************/

/*  Tue June 6/95 - MB											*/

/*																*/

/*	Flush output to the pipe if necessary						*/

/*																*/

/****************************************************************/



void kflush(void)

{

	  #if 0	//:::
		if (!dprintf_Disabled)

			fflush(DebugPipeHandle);

	  #endif

}



/****************************************************************/

/*  Tue June 6/95 - MB											*/

/*																*/

/*	kputchar()													*/

/*																*/

/****************************************************************/



void kputchar(char c)

{

		if (!kInitDebugCalled)

			InitDebug();	// force init if someone else forgot.



		if (!dprintf_Disabled) {

			EnterCriticalSection(&kprintf_CritSec);

			char str[2];

			str[0] = c;

			str[1] = 0;

			kwrites(str);

			LeaveCriticalSection(&kprintf_CritSec);

		}

}



/****************************************************************/

/*  Wed 02-24-1993 - MB                                         */

/*                                                              */

/*  kwrites()                                                   */

/*                                                              */

/****************************************************************/



void kwrites(char *str)

{

	if (!kInitDebugCalled)

		InitDebug();	// force init if someone else forgot.



    if (!dprintf_Disabled) {

		EnterCriticalSection(&kprintf_CritSec);

		// write entire string to pipe whenever it's ready to go.

		int len;

		static int partial_line_flag = TRUE;	// assume partial the first time.



		// Don't add the prefix string if it's a partial line

		// or if our string starts with an ANSI sequence.

		if (kDebWinPrefixString && !partial_line_flag && str[0]!=27) {

			len = strlen(kDebWinPrefixString);

			if (len) {

				WriteToDebugPipe(kDebWinPrefixString, len);

			}

		}



		#define MAX_CHARS_PER_LINE		150

		#define CHARS_PER_BROKEN_LINE	(90)

		len = strlen(str);

		if (len > MAX_CHARS_PER_LINE) {

			// break into multiple lines.

			do {

				char partial_line[CHARS_PER_BROKEN_LINE+2];

				strnncpy(partial_line, str, CHARS_PER_BROKEN_LINE+1);



				int chars_to_do = CHARS_PER_BROKEN_LINE;

				char *n = strchr(str, '\n');

				if (n && n-str < CHARS_PER_BROKEN_LINE-1) {

					chars_to_do = n-str+1;

					partial_line[chars_to_do] = 0;

				} else {

					partial_line[chars_to_do] = '\n';

					partial_line[chars_to_do+1] = 0;

				}

				partial_line_flag = FALSE;

				WriteToDebugPipe(partial_line, strlen(partial_line));

				str += chars_to_do;

				len = strlen(str);

			} while (len > CHARS_PER_BROKEN_LINE);

			partial_line_flag = FALSE;

		}

		if (len) {

			WriteToDebugPipe(str, len);

			if (str[len-1] == '\n')

				partial_line_flag = FALSE;

			else

				partial_line_flag = TRUE;

		}

		LeaveCriticalSection(&kprintf_CritSec);

    }

}



/****************************************************************/

/*  Wed 02-24-1993 - MB                                         */

/*                                                              */

/*  kputs()                                                     */

/*                                                              */

/****************************************************************/



void kputs(char *str)

{

    if (!dprintf_Disabled) {

		EnterCriticalSection(&kprintf_CritSec);

        kwrites(str);

        kputchar(10);

		LeaveCriticalSection(&kprintf_CritSec);

    }

}



/****************************************************************/

/*  Wed 02-24-1993 - MB                                         */

/*                                                              */

/*  kprintf()                                                   */

/*                                                              */

/****************************************************************/



void kprintf(char *fmt, ...)

{

    va_list arg_ptr;

    char str[KPRINTF_MAX_STRING];

	zstruct(str);



	if (!kInitDebugCalled)

		InitDebug();	// force init if someone else forgot.



    if (!dprintf_Disabled) {

		EnterCriticalSection(&kprintf_CritSec);

        va_start(arg_ptr, fmt);

        vsprintf(str, fmt, arg_ptr);

        va_end(arg_ptr);

        kwrites(str);

		kflush();

		LeaveCriticalSection(&kprintf_CritSec);

    }



  #if USE_STD_CRITSEC

	static int warned = FALSE;

	if (!warned) {

		warned = TRUE;

		kp((ANSI_WHITE_ON_GREEN"%s(%d) Warning: kprintf is not using safe CritSec calls\n",_FL));

	}

  #endif

}



//****************************************************************

//  Sun November 23/97 - MB

//

//	dprintf()

//



void dprintf(char *fmt, ...)

{

    va_list arg_ptr;

    char str[KPRINTF_MAX_STRING];

	zstruct(str);



        if (!dprintf_Disabled) {

			EnterCriticalSection(&kprintf_CritSec);

            va_start(arg_ptr, fmt);

            vsprintf(str, fmt, arg_ptr);

            va_end(arg_ptr);

			if (!kInitDebugCalled) {	// first call?

				// yes, assume they're setting a program name

				strcpy(kappName,str);

				InitDebug();	// force init if someone else forgot.

			} else {

				strcat(str,"\n");

	            kwrites(str);

				kflush();

			}

			LeaveCriticalSection(&kprintf_CritSec);

        }

}



//***************************************************************

// 

//

// Display some ASCII characters for the khexdump() function.

//

static void kDumpASCII(void *ptr, int chars_so_far, int bytes_per_line, int len)

{

	EnterCriticalSection(&kprintf_CritSec);

	// First, pad out the existing line depending on how many characters

	// have been printed so far (char_so_far).

	int desired_column = 12 + bytes_per_line*3 + bytes_per_line/4;

	//kp(("\n%s(%d) ptr=$%08lx, char_so_far = %d, bytes_per_line = %d, len = %d, desired_column = %d\n", _FL, ptr, chars_so_far, bytes_per_line, len, desired_column));

	for ( ; chars_so_far < desired_column ; chars_so_far++) {

		kputchar(' ');

	}

	kputchar('\'');

	byte *cptr = (byte *)ptr;

	int i;

	for (i=0 ; i<len ; i++) {

		byte c = *cptr++;

		if (c >= ' ' && c <= 0xFE)

			kputchar((char)c);

		else

			kputchar(' ');

	}

	for ( ; i<bytes_per_line ; i++) {

		kputchar(' ');

	}

	kputchar('\'');

	LeaveCriticalSection(&kprintf_CritSec);

}





/****************************************************************/

/*  Thu December 21/95 - MB										*/

/*																*/

/*	Display a block of data in hex format						*/

/*																*/

/****************************************************************/



void khexdump(void *ptr, int len, int bytes_per_line, int bytes_per_number)

{

	int byte_num, i;

	int old_kLineMode;



        if (!dprintf_Disabled) {

			EnterCriticalSection(&kprintf_CritSec);

			old_kLineMode = kLineMode;

			byte_num = 0;	// byte number for this line.

	 		kLineMode = TRUE;

			int chars_so_far = 0;	// # of characters output on this line.

			void *ptr_at_start_of_line = ptr;

			for (i=0 ; i<len ; ) {

				if (!byte_num) {

					kprintf("$%08lx:", ptr);

					chars_so_far = 10;

					ptr_at_start_of_line = ptr;

				}

				if (byte_num && !((byte_num/bytes_per_number)&3)) {

					kputchar(' ');	// add an extra space every 4 numbers.

					chars_so_far += 1;

				}

				switch (bytes_per_number) {

				case 4:

					kprintf(" %08lx", *(dword *)ptr);

					chars_so_far += 9;

					break;

				case 3:

					kprintf(" %06x", *(dword *)ptr & 0x00FFFFFF);

					chars_so_far += 7;

					break;

				case 2:

					kprintf(" %04x", *(word *)ptr);

					chars_so_far += 5;

					break;

				case 1:

					kprintf(" %02x", *(byte *)ptr);

					chars_so_far += 3;

					break;

				default:

					kprintf("** khexdump error: cannot handle %d bytes_per_number\n", bytes_per_number);

					return;

				}

				ptr = (void *)((dword)ptr+bytes_per_number);

				i += bytes_per_number;

				byte_num += bytes_per_number;

				if (byte_num >= bytes_per_line) {

					kDumpASCII(ptr_at_start_of_line, chars_so_far, bytes_per_line,

							(char *)ptr - (char *)ptr_at_start_of_line);

					kputchar('\n');

					byte_num = 0;

					chars_so_far = 0;

				}

			}

			if (byte_num) {

				kDumpASCII(ptr_at_start_of_line, chars_so_far, bytes_per_line,

							(char *)ptr - (char *)ptr_at_start_of_line);

				kputchar('\n');	// final last newline if necessary.

			}

			kLineMode = old_kLineMode;

			LeaveCriticalSection(&kprintf_CritSec);

		}

}



//*********************************************************

// https://github.com/kriskoin
//

// Send a source path to DebWin for automatically bringing

// up source files when the user clicks a line in debwin.

//

void kAddSourcePath(char *path)

{

	char msg[300];

	sprintf(msg, "%s%s;%s", DEBWIN_ADDSRCPATH, path, kDebWinPrefixString);

	int len = strlen(msg);

	WriteToDebugPipe(msg, len);

}



//*********************************************************

// https://github.com/kriskoin
//

// Send the source path for the library kprintf.cpp is in to

// DebWin for automatically bringing up source files when the

// user clicks a line in debwin.

//

void kAddLibSourcePath(void)

{

	char path[MAX_FNAME_LEN];

	GetDirFromPath(__FILE__, path);

	kAddSourcePath(path);

}



/****************************************************************/

/*  Tue June 6/95 - MB											*/

/*																*/

/*	Test if the debug device driver is out there				*/

/*																*/

/****************************************************************/



void InitDebug(void)

{

  #if DISP	//:::
	dword oldms, newms;

  #endif
 //DebugOutputFileName = "debwin.log";
	int pipes_allowed;	// set if we're allowed to test for \\.\pipe\*



		iDebugHandleType = 0;



	  #if DISP	//:::
		{oldms=newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

	  #endif



		if (kInitDebugCalled)

			return;	// no work to do 2nd time around.



		kInitDebugCalled = TRUE;		// make sure we're only called once.

	  #if USE_STD_CRITSEC

		InitializeCriticalSection(&kprintf_CritSec);

	  #else

		PPInitializeCriticalSection(&kprintf_CritSec, CRITSECPRI_KPRINTF, "kprintf");

	  #endif

#ifdef WIN32

		dprintf_Disabled = TRUE;



		if (InitDebugDisable)

			return;	// do nothing



	  #if DISP	//:::
		freopen("out.txt", "w", stdout );

		printf("%s(%d)\n", _FL);

	  #endif

		// check for the debug device driver...



	  #if DISP	//:::
		printf("%s(%d)\n", _FL);

	  #endif

		// try opening a named pipe...

		pipes_allowed = TRUE;



		// adate: if this is Win95, don't try to open pipes because they

		// take almost 5s to timeout and they aren't supported anyway.

		{

			// Determine if we're running NT or Win95

			// (because Win95 doesn't support named pipes)

			OSVERSIONINFO osvi;

			zstruct(osvi);

			osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);

			GetVersionEx(&osvi);

			if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {	// Win95?

			  #if DISP	//:::
				printf("%s(%d) Win95 detected.  Disallowing use of named pipes.\n", _FL);

			  #endif

				pipes_allowed = FALSE;

			}

		}



	  #if DISP	//:::
		printf("%s(%d)\n", _FL);

	  #endif

//		DebugPipeType = 0;	// assume ANSI.SYS style output

		DebugPipeType = 1;	// assume DebWin style output

	  #if DISP	//:::
		{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

	  #endif

		if (pipes_allowed)

			DebugPipeHandle = open("\\\\.\\PIPE\\DEBUG", O_WRONLY);

	  #if DISP	//:::
		{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

	  #endif

		if (DebugPipeHandle==-1) DebugPipeHandle = 0;

	  #if 0	// adate: I think this was only used for running under OS/2 with old debwin

		if (!DebugPipeHandle) {

			DebugPipeType = 1;	// assume DebWin style output

//			{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

			if (pipes_allowed)

				DebugPipeHandle = open("\\\\.\\PIPE\\DEBUGD", O_WRONLY);

//			{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

			if (DebugPipeHandle==-1) DebugPipeHandle = 0;

		}

	  #endif

	  #if DISP	//:::
		printf("%s(%d) DebugPipeHandle=%d\n", _FL, DebugPipeHandle);

	  #endif

		if (!DebugPipeHandle) {	// none of that worked, try a mail slot...

			HANDLE semhandle;

			DebugPipeType = 1;	// assume DebWin style output



			// adate: it seems we can open a mailslot even if

			// it doesn't exist.  This means that if DebWin isn't

			// running, our program writes to an invalid mailslot

			// every time it tries to output something.  This can be

			// very slow.  DebWin now creates a semaphore that we must

			// be able to open before allowing the mailslot test.  If

			// the semaphore open fails, then we know debwin isn't running.



		  #if DISP	//:::
			{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d  Pipe did not open... trying mailslot.\n",_FL,newms-oldms);oldms=newms;}

		  #endif

			semhandle = OpenSemaphore(SEMAPHORE_ALL_ACCESS, FALSE, "DebWinOpenSem");

		  #if DISP	//:::
			{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d  semhandle=%d\n",_FL,newms-oldms,semhandle);oldms=newms;}

		  #endif

			if (semhandle!=NULL) {	// DebWin open...

				CloseHandle(semhandle);

			  #if DISP	//:::
				{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d  DebWin's semaphore opened... trying mailslot\n",_FL,newms-oldms);oldms=newms;}

			  #endif

				char *mailslot_name = "\\\\.\\mailslot\\debug";

			#if 1	// 2022 kriskoin

				// Use CreateFile() because open() doesn't work on mailslots

				// when compiled with MSVC.

				// It works fine with the Watcom libraries.

				DebugPipeHandle = (int)CreateFile(mailslot_name, GENERIC_WRITE,

						FILE_SHARE_WRITE|FILE_SHARE_READ,

						NULL, OPEN_ALWAYS,

						FILE_ATTRIBUTE_NORMAL,

						NULL);

				if (DebugPipeHandle==(int)INVALID_HANDLE_VALUE) {	// failure?

					DebugPipeHandle = -1;

				} else {

					iDebugHandleType = 1;	// 1=created with CreateFile().

				}

			  #if DISP	//:::
			  	if (DebugPipeHandle==-1) {

					int err = GetLastError();

					printf("%s(%d) mailslot '%s' failed to open. GetLastError()=%d\n", _FL, mailslot_name, err);

			  	} else {

					printf("%s(%d) mailslot successfully opened. Handle=%d\n", _FL, DebugPipeHandle);

			  	}

				{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

			  #endif

			#else

				DebugPipeHandle = open(mailslot_name, O_CREAT|O_WRONLY|O_BINARY, S_IWRITE);

			  #if DISP	//:::
			  	if (DebugPipeHandle==-1) {

					printf("%s(%d) mailslot '%s' failed to open. errno=%d\n", _FL, mailslot_name, errno);

					if (errno==EINVAL) {

						printf("%s(%d) error #%d is EINVAL: Invalid oflag or pmode argument \n", _FL, EINVAL);

					}

			  	} else {

					printf("%s(%d) mailslot successfully opened. Handle=%d\n", _FL, DebugPipeHandle);

			  	}

				{newms=GetTickCount();printf("%s(%d) elapsed_ms=%d\n",_FL,newms-oldms);oldms=newms;}

			  #endif

			#endif

			}

			if (DebugPipeHandle==-1) DebugPipeHandle = 0;

		}

	  #if DISP	//:::
		printf("%s(%d) DebugPipeHandle=%d\n", _FL, DebugPipeHandle);

	  #endif

	  #if 1	// 2022 kriskoin

		if (!DebugPipeHandle) {

			// Pipe and mailslot both failed... try writing to debwin.log

			DebugPipeHandle = open("logs/debwin.log", _O_CREAT|_O_WRONLY, _S_IREAD | _S_IWRITE);

			if (DebugPipeHandle==-1) {

			  #if DISP	//:::
				printf("%s(%d) opening debwin.log failed as well. Debug output will go to bit bucket.\n",_FL);

			  #endif

				DebugPipeHandle = 0;

			}

		}

	  #endif



		if (DebugPipeHandle) {

		  #if DISP	//:::
			printf("%s(%d) DebugPipeHandle=%d, DebugPipeType=%d\n",

					_FL, DebugPipeHandle, DebugPipeType);

		  #endif

			dprintf_Disabled = FALSE;

			if (kappName[0]) {

				char str[100];

				strcpy(str,"+");	// we supply our own <cr>'s

				strcat(str,kappName);

				kwrites(str);

			} else

			{

				kwrites("-+");

			}

			DebScreenHeight=5000;	// do LOTS at a time.

		}



	  #if DISP	//:::
		printf("%s(%d) dprintf_Disabled = %d\n", _FL, dprintf_Disabled);

	  #endif

#else // !WIN32

		kDebugOutputFileName = "debwin.log";

#endif // !WIN32

}

#endif	 // DEBUG

