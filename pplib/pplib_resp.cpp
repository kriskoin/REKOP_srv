#ifdef WIN32

  #define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

  #include <windows.h>

#endif

#ifndef	_STDIO_H
  #include <stdio.h>
#endif

#include <stdlib.h>

#include <stdarg.h>

#include <string.h>

#include <sys/stat.h>

#include <time.h>

#include <ctype.h>

#if WIN32

  #include <io.h>

  #include <process.h>

#endif

#include <errno.h>

#include "pplib.h"



// For the games, as the sb/bb values are used a little differently, we may need to translate

// them for the " low/high " label often used.  just multiply by GameStakesMultipliers[GameType]

// Remember to subtract GAME_RULES_START when calculating the index to this table.

int GameStakesMultipliers[MAX_GAME_RULES] = { 1,1,1,2,2 };



char *ActionStrings[32] = {

		"",					//  0 ACT_NO_ACTION

		"Post",				//  1 ACT_POST (used for new players - always BB amount)

		"Post Small\nBlind",//  2 ACT_POST_SB

		"Post Big\nBlind",	//  3 ACT_POST_BB

		"Post\n",			//  4 ACT_POST_BOTH (used for missed blinds, SB is dead).

		"Sit out",			//  5 ACT_SIT_OUT_SB

		"Sit out",			//  6 ACT_SIT_OUT_BB

		"Sit out",			//  7 ACT_SIT_OUT_POST

		"Sit out",			//  8 ACT_SIT_OUT_BOTH

		"Fold",				//  9 ACT_FOLD

		"Call",				// 10 ACT_CALL

		"Check",			// 11 ACT_CHECK

		"Bring-in",			// 12 ACT_BRING_IN

		"Bet",				// 13 ACT_BET

		"Raise",			// 14 ACT_RAISE

		"Bet All-in",		// 15 ACT_BET_ALL_IN

		"Call All-in",		// 16 ACT_CALL_ALL_IN

		"Raise All-in",		// 17 ACT_RAISE_ALL_IN

		"Show Cards",		// 18 ACT_SHOW_HAND

		"You Won\nDon't Show",	// 19 ACT_TOSS_HAND

		"You Lost\nMuck Cards",	// 20 ACT_MUCK_HAND

		"Ante",				// 21 ACT_POST_ANTE

		"Sit Out",			// 22 ACT_SIT_OUT_ANTE

		"Post All-in",		// 23 ACT_FORCE_ALL_IN

		"Force All-in",		// 24 ACT_FORCE_ALL_IN

		"Bring-in All-in",	// 25 ACT_BRING_IN_ALL_IN

		"Timed out",		// 26 ACT_TIMEOUT

		"Show Cards\nShuffled"	// 27 ACT_SHOW_SHUFFLED

};



// Roman numerals from zero (undefined) to 20 (XX)

char *szRomanNumerals[21] = { "?", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X",

		"XI", "XII", "XIII", "XIV", "XV", "XVI", "XVII", "XVIII", "XIX", "XX" };



/****************************************************************/

/*  Sun June 23/96 - MB											*/

/*																*/

/*	Add a line of text to a log file							*/

/*																*/

/****************************************************************/

void AddToLog(char *log_file_name, char *first_line_string, char *fmt, ...)

{

		WORD32 start_ticks = GetTickCount();
		// First, gain access to the log file critsec
		static int first_call = TRUE;
		static PPCRITICAL_SECTION cs;
		if (first_call) {
			first_call = FALSE;
			PPInitializeCriticalSection(&cs, CRITSECPRI_LOCAL, "AddToLog");
		}
		EnterCriticalSection(&cs);
		// Now try to open the file...
		FILE *fd;
		int retry = FALSE;
		do {
			fd = fopen(log_file_name, "at");
			if (fd==NULL && errno==EACCES) {
				// Error accessing the file.  Could be someone else is writing
				// to it or it could be that we don't have access to the
				// directory it's in or the file itself.  Retry for a little

				// while and then give up.

				WORD32 now = GetTickCount();

				if (now - start_ticks >= 10000) {

					retry = FALSE;

				} else {

					retry = TRUE;

					Sleep(20+random(50));

				}

			} else {

				retry = FALSE;

			}

		} while (retry);



		if (fd) {

			// Spit out the first line header if this is the beginning

			// of the file.

			fseek(fd, 0, SEEK_END);

			if (first_line_string && ftell(fd)<=8) {	// (allow for a few <cr><lf>'s)

				fputs(first_line_string, fd);

			}



			// Spit out the formatted line of text...

		    va_list arg_ptr;

	        va_start(arg_ptr, fmt);

	        vfprintf(fd, fmt, arg_ptr);

	        va_end(arg_ptr);



			fclose(fd);

		} else {

			kp(("Error opening '%s' for appending log text (error=%d).\n", log_file_name, errno));

		}



		WORD32 now = GetTickCount();

		if (now - start_ticks > 2000) {

			kp(("%s %s(%d) Warning: AddToLog(\"%s\", ...) took %dms to execute.\n",

					TimeStr(), _FL, log_file_name, now - start_ticks));

		}



		// Release the log access semaphore.

		LeaveCriticalSection(&cs);

}



//*********************************************************

// https://github.com/kriskoin
//

// LogFile class for writing to log files

//

LogFile::LogFile(char *_log_file_name, char *_first_line_string, unsigned _flush_interval_in_seconds)

{

	PPInitializeCriticalSection(&LogFileCritSec, CRITSECPRI_LOCAL, "LogFile");

	log_file_name = _log_file_name;

	first_line_string = _first_line_string;

	flush_interval_in_seconds = _flush_interval_in_seconds;

	last_flush_seconds = 0;

	fd = NULL;

}



LogFile::~LogFile(void)

{

	EnterCriticalSection(&LogFileCritSec);

	if (fd) {

		fclose(fd);

		fd = NULL;

	}

	log_file_name = NULL;

	first_line_string = NULL;

	LeaveCriticalSection(&LogFileCritSec);

	PPDeleteCriticalSection(&LogFileCritSec);

	zstruct(LogFileCritSec);

}



//*********************************************************

// https://github.com/kriskoin
//

// Write a format string (for sprintf) to the log file of a log file class

//

void LogFile::Write(char *fmt, ...)

{

	WORD32 start_ticks = GetTickCount();



	EnterCriticalSection(&LogFileCritSec);



	// Flush the file if it's time to do so.

	if (fd && SecondCounter - last_flush_seconds >= flush_interval_in_seconds) {

		// Time to flush.  Close fd.

		//kp(("%s %s(%d) Flushing log file.\n", TimeStr(), _FL));

		fclose(fd);

		fd = NULL;

		last_flush_seconds = SecondCounter;

	}



	// Open the file if necessary...

	while (!fd) {

		last_flush_seconds = SecondCounter;

		//kp(("%s %s(%d) Opening log file '%s'\n", TimeStr(), _FL, log_file_name));

		fd = fopen(log_file_name, "at");

		if (fd==NULL && errno==EACCES) {

			// Error accessing the file.  Could be someone else is writing

			// to it or it could be that we don't have access to the

			// directory it's in or the file itself.  Retry for a little

			// while and then give up.

			WORD32 now = GetTickCount();

			if (now - start_ticks >= 10000) {

				break;

			} else {

				Sleep(20+random(50));

			}

		} else {

			if (fd) {

				// Successfully opened.

				// Spit out the first line header if this is the beginning of the file.

				fseek(fd, 0, SEEK_END);	// seek to the end.

				if (first_line_string && ftell(fd)<=8) {	// (allow for a few <cr><lf>'s)

					fputs(first_line_string, fd);

				}

			}

			break;

		}

	}



	if (fd) {

		// Spit out the formatted line of text...

	    va_list arg_ptr;

        va_start(arg_ptr, fmt);

        vfprintf(fd, fmt, arg_ptr);

        va_end(arg_ptr);

	} else {

		kp(("%s Error opening '%s' for appending log text (error=%d).\n", TimeStr(), log_file_name, errno));

	}



	WORD32 now = GetTickCount();

	if (now - start_ticks > 2000) {

		kp(("%s %s(%d) Warning: LogFile.Write() to \"%s\" took %dms to execute.\n",

				TimeStr(), _FL, log_file_name, now - start_ticks));

	}



	LeaveCriticalSection(&LogFileCritSec);

}



#ifdef WIN32	// not yet supported for non-Win32

/****************************************************************/

/*  Sat Nov 16/96 - HK											*/

/*																*/

/*	Add a cell to an XY comma-delimited spreadsheet				*/

/*																*/

/****************************************************************/

void AddToSpreadSheet(char *file_name, char *Ycol_name, char *Xrow_name, char seperator_char, char *fmt, ...)

{

#define MAX_XY_LOG_ROWS	  	 		400

#define MAX_XY_LOG_COLUMNS	   		100

#define MAX_CELL_LEN				100



char *SS_text = NULL;							// pointer to data block

int i,j,k;										// used throughout as indexes

int fFirstWrite = FALSE;						// TRUE if the file doesn't yet exist

char cell_contents[MAX_CELL_LEN];               // formatted cell contents destination

char ***SS = NULL;								// the spreadsheet matrix

//HMTX xy_log_sem = NULLHANDLE;

int fSortColumns = FALSE;

int fSortRows = FALSE;

int iRow = -1; // the row we'll eventually be using

int iColumn = -1; // the column we'll eventually be using

int cur_row = 0, cur_col = 0, total_rows = 0, total_cols = 0;



    // First, gain access to the log file

	static int first_call = TRUE;

	static PPCRITICAL_SECTION cs;

	if (first_call) {

		first_call = FALSE;

		PPInitializeCriticalSection(&cs, CRITSECPRI_LOCAL, "AddToSS");

	}

	EnterCriticalSection(&cs);



	// Initialize before we wait

	if ((SS = (char ***)calloc(1, MAX_XY_LOG_ROWS * sizeof(char *))) == NULL) {

		printf("ERROR:  Couldn't malloc %d row ptrs for spreadsheet '%s'",

			MAX_XY_LOG_ROWS, file_name);

		goto freeup;

	}

	for (i=0; i < MAX_XY_LOG_ROWS; i++) {

		if ((SS[i] = (char **)calloc(1, MAX_XY_LOG_COLUMNS * sizeof(char *))) == NULL) {

			printf("ERROR:  Couldn't malloc %d columns in row [%d] ptr for spreadsheet '%s'",

				MAX_XY_LOG_COLUMNS, i, file_name);

			goto freeup;

		}

	}

	for (i=0; i < MAX_XY_LOG_ROWS; i++) {

		for (j=0; j < MAX_XY_LOG_COLUMNS; j++) {

			SS[i][j] = NULL;

		}

	}



	// Format the new cell data...

	va_list arg_ptr;

	va_start(arg_ptr, fmt);

	vsprintf(cell_contents, fmt, arg_ptr);

	va_end(arg_ptr);



	// Now try to open the file...

	FILE *fd;

	int retry;

	retry = FALSE;

	do {

		// opening "r+" fails miserably if the file doesn't exist, but that's

		// helpful because it easily lets us know that it's the first write

		fd = fopen(file_name, "r+b");	// found the file

		if (fd==NULL && errno==EACCES) {

			retry = TRUE;

//			DosSleep(20+random(50));

		} else if (fd==NULL && errno==ENOENT) {

			fd = fopen(file_name, "a+b");	// doesn't yet exist

			fFirstWrite = TRUE;

			if (fd) retry = FALSE;

		} else {

			retry = FALSE;

		}

	} while (retry);



	long file_length;

	if (fd) {	// we've got it -- now let's read it in

		rewind(fd);

		file_length = filelength(fileno(fd));

		if ((SS_text = (char *)calloc(1,file_length+1)) == NULL) {

			printf("FATAL ERROR:  Couldn't malloc %ld bytes for '%s'",

				file_length, file_name);

			goto freeup;

		}

		// Read the whole file in one block

		fread(SS_text, file_length, 1, fd);

	} else {

		printf("Error initially opening '%s' (error=%d).", file_name, errno);

		goto freeup;

	}

	// Parse the data block

	char *ss_text_ptr;

	ss_text_ptr = SS_text;

	do {

		SS[cur_row][cur_col] = ss_text_ptr;

		forever {

			ss_text_ptr++;	// next char...

			if (*ss_text_ptr == 0 || *ss_text_ptr==26)

				break;	// HIT a NULL or EOF, we're done

			if (*ss_text_ptr == 13) *ss_text_ptr = '\0';	// remove CRs

			if (*ss_text_ptr == ',' || *ss_text_ptr == '\t') { 		// next cell

				*ss_text_ptr = '\0';

				cur_col++;

				if (cur_col > MAX_XY_LOG_COLUMNS) {

					// if there are too many columns, overwrite the last column

					printf("Too many columns in spreadsheet '%s' (max %d columns)",

						file_name, MAX_XY_LOG_COLUMNS);

					cur_col = MAX_XY_LOG_COLUMNS-1;

				}

#ifdef BCB
				total_cols = max(total_cols, cur_col+1); // first column always "exists"
#else
				total_cols = __max(total_cols, cur_col+1); // first column always "exists"
#endif
				ss_text_ptr++;	// next char...

				break;

			}

			if (*ss_text_ptr == 10) {	// LF, next line

				*ss_text_ptr = '\0';

				cur_col = 0;

				cur_row++;

				if (cur_col > MAX_XY_LOG_ROWS) {

					// if there are too many rows, overwrite the last one

					printf("Too many rows in spreadsheet '%s' (max %d rows)",

						file_name, MAX_XY_LOG_ROWS);

					cur_row = MAX_XY_LOG_ROWS-1;

				}

#ifdef BCB
				total_rows = max(total_rows, cur_row);
#else
				total_rows = __max(total_rows, cur_row);
#endif
				ss_text_ptr++;	// next char...

				break;

			}

		}

	} while (*ss_text_ptr && *ss_text_ptr!=26);



#if 0

	// DEBUG ONLY -- PRINT OUT WHAT WE'VE READ AND PARSED IN

	printf("Read R%d x C%d", total_rows, total_cols);

	for (i=0; i<total_rows; i++) {

		for (j=0; j<total_cols; j++) {

			printf("R%d:C%d -> %s", i, j, SS[i][j]);

		}

	}

#endif



	// Search for the column index we want

	for (i=0; i < total_cols; i++) {

		if (!stricmp(SS[0][i], Ycol_name)) {	// found it

			iColumn = i;

			break;

		}

	}

	if (iColumn < 0) {	// new new row, add it for as many columns as we have

		iColumn = total_cols;

		if (total_cols < MAX_XY_LOG_COLUMNS) {

			total_cols++;

		} else {

			printf("Can't add another column to '%s' (max %d columns)", file_name, MAX_XY_LOG_COLUMNS);

			iColumn = MAX_XY_LOG_COLUMNS-1;

		}

		fSortColumns = TRUE;

	}

	// Search for the row index we want

	for (i=0; i < total_rows; i++) {

		if (!stricmp(SS[i][0], Xrow_name)) {	// found it

			iRow = i;

			break;

		}

	}

	if (iRow < 0) {	// need new row, add it for as many columns as we have

		iRow = total_rows;

		if (iRow < MAX_XY_LOG_ROWS) {

			total_rows++;

		} else {

			printf("Can't add another row to '%s' (max %d rows)", file_name, MAX_XY_LOG_ROWS);

			iRow = MAX_XY_LOG_ROWS-1;

		}

		fSortRows = TRUE;

	}



	// If this is the first write, override a few things

	char Cell00[22];

	if (fFirstWrite) {

		char d_buf[9], t_buf[9];

		_strdate(d_buf);

		_strtime(t_buf);

		sprintf(Cell00,"%s %s", d_buf, t_buf);

		SS[0][0] = Cell00;

		iRow = 1;

		iColumn = 1;

		total_rows = 2;

		total_cols = 2;

		fSortRows = FALSE;

		fSortColumns = FALSE;

	}

	// Assign the new cell / overwrite existing contents

	SS[0][iColumn] = Ycol_name;	// column name

	SS[iRow][0] = Xrow_name;	// row name

	SS[iRow][iColumn] = cell_contents;



	// Sort what's needed

	if (fSortColumns) {

		char *ColumnNames[MAX_XY_LOG_COLUMNS];

		for (i=0; i < total_cols;i++)

			ColumnNames[i] = SS[0][i];

		for (i=1; i < total_cols; i++) {

			for (j=1; j < total_cols-1; j++) {

			  #if 1	//:::
				// If the first character is a digit, assume we're

				// comparing two numbers.

				int swap_flag = FALSE;

				if (isdigit(ColumnNames[j][0]) || ColumnNames[j][0]=='.') {

					float j0, j1;

					j0 = j1 = 0.0;

					sscanf(ColumnNames[j], "%f", &j0);

					sscanf(ColumnNames[j+1], "%f", &j1);

					if (j0 > j1)

						swap_flag = TRUE;

				} else {	// assume we're comparing two strings.

					if (stricmp(ColumnNames[j],ColumnNames[j+1]) > 0)

						swap_flag = TRUE;

				}

				if (swap_flag)

			  #else

				if ((stricmp(ColumnNames[j],ColumnNames[j+1]) > 0))

			  #endif

				{

					char *tmp = ColumnNames[j];

					ColumnNames[j] = ColumnNames[j+1];

					ColumnNames[j+1] = tmp;

					for (k=0; k < total_rows; k++) {

						char *tmp_row = SS[k][j];

						SS[k][j] = SS[k][j+1];

						SS[k][j+1] = tmp_row;

					}

				}

			}

		}

	}

	if (fSortRows) {

		char *RowNames[MAX_XY_LOG_ROWS];

		for (i=0; i < total_rows;i++)

			RowNames[i] = SS[i][0];

		for (i=1; i < total_rows;i++) {

			for (j=1; j < total_rows-1;j++) {

				if ((stricmp(RowNames[j], RowNames[j+1]) > 0)) {

					char *tmp = RowNames[j];

					RowNames[j] = RowNames[j+1];

					RowNames[j+1] = tmp;

					for (k=0; k < total_cols; k++) {

						char *tmp_col = SS[j][k];

						SS[j][k] = SS[j+1][k];

						SS[j+1][k] = tmp_col;

					}

				}

			}

		}

	}

	// Write it out

	rewind(fd);

	for (i=0; i<total_rows; i++) {

		for (j=0; j<total_cols-1; j++) {

			/* cSeperatorChar is not local... shouldn't be here */

			fprintf(fd, "%s%c", (SS[i][j] ? SS[i][j] : " "), seperator_char);

		}

		fprintf(fd, "%s\r\n", (SS[i][total_cols-1] ? SS[i][total_cols-1] : " "));

	}

	fputc(26, fd);		// write out an explicit EOF character

	fclose(fd);



	// Free anything we've allocated

freeup:

	if (SS_text) {

		free(SS_text);	// free the text block

		SS_text = NULL;

	}

	if (SS) {

		for (i=0; i < MAX_XY_LOG_ROWS; i++) {

			if (SS[i])

				free(SS[i]);// free each row

		}

		free(SS);		// free the pointers to the rows

		SS = NULL;

	}



	LeaveCriticalSection(&cs);

}

#endif // WIN32	// not yet supported for non-Win32



/****************************************************************/

/*  Mon July 11/94 - MB											*/

/*																*/

/*	strnncpy(): strncpy that makes sure the dest is terminated	*/

/*																*/

/****************************************************************/

char *strnncpy(char *dest, const char *src, int max_dest_len)

{

	char *p = dest;



		if (!src) {

			src = "(null)";	// avoid null pointer crashes

		}

	    while (--max_dest_len > 0 && *src)

	        *p++ = *src++;

	    *p++ = 0;

		// 24/01/01 kriskoin:

		if (max_dest_len > 0) {

			memset(p, 0, max_dest_len);

		}

	    return dest;

}



//*********************************************************

// https://github.com/kriskoin
//

// strcat() that takes a max dest length.

//

char *strnncat(char *dest, const char *src, int max_dest_len)

{

	int current_len = strlen(dest);

	int chars_left = max_dest_len - current_len;

	if (chars_left > 0) {

		strnncpy(dest + current_len, src, chars_left);

	}

	return dest;

}



//*********************************************************

// https://github.com/kriskoin
//

// Make a string end with a newline if it already contains data

// or leave it blank otherwise.  Adds a newline if necessary.

//

void MakeStringEndWithNewline(char *dest, int max_dest_len)

{

	int len = strlen(dest);

	if (len && dest[len-1]!='\n') {

		strnncat(dest, "\r\n", max_dest_len);

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Trim any newlines (or cr's) from the end of a string.

//

void TrimNewlines(char *str)

{

	forever {

		int len = strlen(str);

		if (!len) {

			break;	// done.

		}

		if (str[len-1]!='\n' && str[len-1]!='\r') {

			break;	// done.

		}

		str[len-1] = 0;	// remove the character.

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// rot13 a string (source and dest may be the same if desired)

// max_dest_len includes the space for the nul (i.e. sizeof(dest) works).

//

void rot13(char *src, char *dest, int max_dest_len)

{

	char *original_xlat = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

	char *rotated_xlat  = "nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM";

	while (*src && max_dest_len > 1) {

		max_dest_len--;

		char *p = strchr(original_xlat, *src);

		if (p) {	// found a match!

			*dest = rotated_xlat[p - original_xlat];

		} else {	// no match.  just copy.

			*dest = *src;

		}

		src++;

		dest++;

	}

	if (max_dest_len > 0) {

		*dest = 0;	// terminate it.

	}

}



/**********************************************************************************

 Function cdecl Error(ErrorType, char *fmt, ...)

 Date: 20180707 kriskoin : 
 Purpose: Log an error, and possibly do more, depending on the severity

***********************************************************************************/

void (*ErrorCallbackFunction)(char *error_string);

/**********************************************************************************

 Function *tm localtime

 Date: 2017/7/7 kriskoin
 Purpose: our own version of local time that is re-entrant and returns its result

***********************************************************************************/

static PPCRITICAL_SECTION LocalTimeCritSec;

static int LocalTimeCritSec_initialized = FALSE;



struct tm *localtime(const time_t *timer, struct tm *out)

{

	if (!LocalTimeCritSec_initialized) {

		LocalTimeCritSec_initialized = TRUE;

		PPInitializeCriticalSection(&LocalTimeCritSec, CRITSECPRI_TIMEFUNCS, "localtime");

	}

	EnterCriticalSection(&LocalTimeCritSec);

	struct tm *t = localtime(timer);

	if (out) {		// optionally fill a return structure

		if (t) {

			*out = *t;	// make a copy before releasing the critical section

		} else {

			zstruct(*out);

		}

		t = out;	// return ptr to the structure they gave us rather than localtime()'s

	}

	LeaveCriticalSection(&LocalTimeCritSec);

	return t;

}

void Error(ErrorType error_type, char *fmt, ...)

{

	#define MAX_ERROR_LOG_MESSAGE_LEN	400

	char szError[MAX_ERROR_LOG_MESSAGE_LEN];

	zstruct(szError);



	time_t tt = time(NULL);

	struct tm tm;

	struct tm *t = localtime(&tt, &tm);

	sprintf(szError,"[%1d] %02d/%02d@%02d:%02d:%02d ", error_type,

			t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);



	va_list arg_ptr;

    va_start(arg_ptr, fmt);

    vsprintf(szError+strlen(szError), fmt, arg_ptr);

    va_end(arg_ptr);



  #if DEBUG

	char *color_string = "";

   #if WIN32	// only do color under Windows for now (since less can't handle it)

	if (error_type >= ERR_MINOR_ERROR) {

		color_string = ANSI_ERROR;

	} else if (error_type >= ERR_MINOR_WARNING) {

		color_string = ANSI_WHITE_ON_GREEN;

	}

	kp(("%s%s\n", color_string, szError));

   #else

	if (strlen(color_string)) {

		kp(("%s%s"ANSI_DEFAULT"\n", color_string, szError));

	} else {

		kp(("%s\n", szError));

	}

   #endif

  #endif

	AddToLog("error.txt", NULL, "%s\n", szError);



	// If there is a user-defined callback, pass the error string to

	// the callback function.

	if (ErrorCallbackFunction) {

		(*ErrorCallbackFunction)(szError);

	}

}




struct tm *gmtime(const time_t *timer, struct tm *out)

{

	if (!LocalTimeCritSec_initialized) {

		LocalTimeCritSec_initialized = TRUE;

		PPInitializeCriticalSection(&LocalTimeCritSec, CRITSECPRI_TIMEFUNCS, "localtime");

	}

	EnterCriticalSection(&LocalTimeCritSec);

	struct tm *t = gmtime(timer);

	if (out) {		// optionally fill a return structure

		*out = *t;	// make a copy before releasing the critical section

		t = out;	// return ptr to the structure they gave us rather than localtime()'s

	}

	LeaveCriticalSection(&LocalTimeCritSec);

	return t;

}



//*********************************************************

// https://github.com/kriskoin
//

// Return a formatted string for use in error logs and whatnot

// Format looks like this: 07/23@21:45:21

//


/*********************************************************

// HK11/11/1999 -- modified to use rotating buffers      */

char *TimeStr(time_t tt, char year_flag, int specific_time_zone_flag, int time_zone_offset)

{

	#define TIME_STR_BUFFERS	8

	static char time_str[TIME_STR_BUFFERS][30];

	static int buf_index;

	buf_index = (buf_index+1) % TIME_STR_BUFFERS;	// increment to next usable one

	zstruct(time_str[buf_index]);

	struct tm tm;

	struct tm *t;

	if (specific_time_zone_flag) {

		tt -= time_zone_offset;

		t = gmtime(&tt, &tm);

	} else {

		t = localtime(&tt, &tm);

	}

	if (year_flag) {

		sprintf(time_str[buf_index], "%04d/%02d/%02d @ %02d:%02d:%02d", t->tm_year+1900, t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

	} else {

		sprintf(time_str[buf_index], "%02d/%02d@%02d:%02d:%02d", t->tm_mon+1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);

	}

	return time_str[buf_index];

}



//*********************************************************

char *TimeStr(time_t tt, char year_flag)

{

	return TimeStr(tt, year_flag, FALSE, 0);

}

char *TimeStr(time_t tt)

{

	return TimeStr(tt, FALSE, FALSE, 0);

}
char *TimeStr(void)

{

	return TimeStr(time(NULL), FALSE, FALSE, 0);

}

char *TimeStrWithYear(void)

{

	return TimeStr(time(NULL), TRUE, FALSE, 0);

}







// https://github.com/kriskoin
//

// Return just the date as a string

// DateStr() returns "mm/dd", DateStrWithYear() returns "yy/mm/dd"

//







char *DateStr(time_t tt, char year_flag, int specific_time_zone_flag, int time_zone_offset)

{

	static char time_str[TIME_STR_BUFFERS][30];

	static int buf_index;

	buf_index = (buf_index+1) % TIME_STR_BUFFERS;	// increment to next usable one

	zstruct(time_str[buf_index]);

	struct tm tm;

	struct tm *t;

	if (specific_time_zone_flag) {

		tt -= time_zone_offset;

		t = gmtime(&tt, &tm);

	} else {

		t = localtime(&tt, &tm);

	}

	if (year_flag) {

		sprintf(time_str[buf_index], "%02d/%02d/%02d", t->tm_year%100, t->tm_mon+1, t->tm_mday);

	} else {

		sprintf(time_str[buf_index], "%02d/%02d", t->tm_mon+1, t->tm_mday);

	}

	return time_str[buf_index];

}

char *DateStrWithYear(void)

{

	return DateStr(time(NULL), TRUE, FALSE, 0);

}

char *DateStr(void)

{

	return DateStr(time(NULL), FALSE, FALSE, 0);

}

//*********************************************************

// https://github.com/kriskoin
//

// Function to convert a chip amount to a string

// Returns the same string pointer passed to it.

// Maximum output string length: -$20,000,000.00 (15 chars+nul)

// Use MAX_CURRENCY_STRING_LEN for defining your string lengths.

// round_pennies_method = 0 for no rounding, 1 to round properly,

//     or -1 to always round down.

//

char *CurrencyString(char *str, int amount, ChipType chip_type)

{

	return CurrencyString(str, amount, chip_type, FALSE);

}	

char *CurrencyString(char *str, int amount, ChipType chip_type, int force_pennies_flag)

{

	return CurrencyString(str, amount, chip_type, force_pennies_flag, FALSE);

}

char *CurrencyString(char *str, int amount, ChipType chip_type, int force_pennies_flag, int round_pennies_method)

{

	pr(("%s(%d) CurrencyString: str=$%08lx, amount=%d, chip_type=%d, force_pennies=%d, round_pennies=%d\n",

				_FL, str, amount, chip_type, force_pennies_flag, round_pennies_flag));

	char *s = str;

	if (amount < 0) {

		*s++ = '-';

		amount = -amount;

	}

	switch (chip_type) {

	case CT_NONE:

		Error(ERR_INTERNAL_ERROR, "%s(%d) CurrencyString called with chip_type = CT_NONE", _FL);

		break;

	case CT_PLAY:

		break;

	case CT_REAL:

		*s++ = '$';

		break;

	case CT_TOURNAMENT:

	  #if 0	// 2022 kriskoin

		*s++ = 'T';

		*s++ = '-';

	  #endif

		break;

	default:

		Error(ERR_INTERNAL_ERROR, "%s(%d) CurrencyString called with unknown chip_type (%d)", _FL, chip_type);

	}	

	*s = 0;	// terminate string.

	if (round_pennies_method) {	// round to nearest dollar?

		if (round_pennies_method >= 0) {	// round properly or round down?

			// round properly

			amount += 50;

		}

		amount -= amount % 100;

	}

	unsigned int dollars = (unsigned int)amount / 100u;

	unsigned int pennies = (unsigned int)amount % 100u;

	int leading_digits = FALSE;

	if (dollars >= 1000000) {

		s += sprintf(s, leading_digits ? "%03u,":"%u,", dollars / 1000000);

		dollars = dollars % 1000000;

		leading_digits = TRUE;

	}

	if (dollars >= 1000 || leading_digits) {

		s += sprintf(s, leading_digits ? "%03u,":"%u,", dollars / 1000);

		dollars = dollars % 1000;

		leading_digits = TRUE;

	}

	s += sprintf(s, leading_digits ? "%03u":"%u", dollars);

	if (pennies || force_pennies_flag) {

		s += sprintf(s, ".%02u", pennies);

	}

	return str;

}	



//*********************************************************

// https://github.com/kriskoin
//

// Function to convert an integer to a string with commas between 1000's

// Returns the same string pointer passed to it.

// Maximum output string length: -$20,000,000.00 (15 chars+nul)

// Use MAX_CURRENCY_STRING_LEN for defining your string lengths.

//

char *IntegerWithCommas(char *str, int amount)

{

	char *s = str;

	if (amount < 0) {

		*s++ = '-';

		amount = -amount;

	}

	*s = 0;	// terminate string.

	int leading_digits = FALSE;

	if (amount >= 1000000000) {

		s += sprintf(s, leading_digits ? "%03d,":"%d,", amount / 1000000000);

		amount = amount % 1000000000;

		leading_digits = TRUE;

	}

	if (amount >= 1000000 || leading_digits) {

		s += sprintf(s, leading_digits ? "%03d,":"%d,", amount / 1000000);

		amount = amount % 1000000;

		leading_digits = TRUE;

	}

	if (amount >= 1000 || leading_digits) {

		s += sprintf(s, leading_digits ? "%03d,":"%d,", amount / 1000);

		amount = amount % 1000;

		leading_digits = TRUE;

	}

	s += sprintf(s, leading_digits ? "%03d":"%d", amount);

	return str;

}



//*********************************************************

// https://github.com/kriskoin
//

// Swap characters found in a string.  For each occurance of

// 'original_char', replace it with 'replacement_char'.

//

void SwapCharsInString(char *str, char original_char, char replacement_char)

{

	while (*str) {

		if (*str==original_char) {

			*str = replacement_char;

		}

		str++;

	}

}



//*********************************************************

//  2000/02/06 - MB

//

//  Calculate a 5-digit email validation code based on the

//  email string.

//  Modified by allen kou 9/25/2001

//  To change the Email Validation Code to Check Sum

//  

int CalcEmailValidationCode(char *email_str)

{

	return (CalcChecksum(email_str, strlen(email_str)) & 0x00FFFF) + 25678;

	//return (CalcCRC32(email_str, strlen(email_str)) & 0x00FFFF) + 10394;

    

}	



//*********************************************************

// https://github.com/kriskoin
//

// Insert a \ character before any characters that would be illegal

// on a regular command line.

//

char *DelimitIllegalCommandLineChars(char *src, char *dest, int max_dest_str_len)

{

	char *result = dest;

	char *max_dest = dest + max_dest_str_len - 1;

	while (*src && dest < max_dest) {

		if (*src=='&' ||

			*src=='"' ||

			*src=='\'' ||

			*src=='`' ||

			*src=='\\' ||

			*src=='|' ||

			*src=='<' ||

			*src=='>' ||

			*src=='$'

		) {

			if (dest < max_dest) {

				*dest++ = '\\';

			}

		}

		*dest++ = *src++;

	}

	*dest = 0;

	return result;

}



char *DelimitIllegalCommandLineChars(char *str, int max_str_len)

{

	char temp[500];

	DelimitIllegalCommandLineChars(str, temp, sizeof(temp));

	strnncpy(str, temp, max_str_len);

	return str;

}



//*********************************************************

// https://github.com/kriskoin
//

// Swap illegal user-supplied characters with ? for command line

// parameters.  For example, email subject, from, to, etc.

// Characters get replaced right in the source string.

// The resulting string still NEEDS to be quoted (ALWAYS!)

//

void SwapIllegalCommandLineChars(char *str)

{

	SwapCharsInString(str, '"', '?');

	SwapCharsInString(str, '`', '?');

	SwapCharsInString(str, '\'', '?');

	SwapCharsInString(str, '$', '?');

	SwapCharsInString(str, '&', '?');

	SwapCharsInString(str, '|', '?');



	// These ones shouldn't be needed because the string must be quoted

	// and none of these do anything within quotes (I think).

	//SwapCharsInString(str, '<', '?');

	//SwapCharsInString(str, '>', '?');

	//SwapCharsInString(str, '*', '?');

	//SwapCharsInString(str, '[', '?');

	//SwapCharsInString(str, ']', '?');

}



//*********************************************************

// https://github.com/kriskoin
//

// Background email thread.  This is the thread that actually sends

// out the email that got queued up by the Email() function.

//

PPCRITICAL_SECTION EmailCritSec;

static int EmailThreadLaunchedFlag;

#define EMAIL_FIELD_LEN	300

struct EmailQueueEntry  {

	struct EmailQueueEntry *next;

	char to[EMAIL_FIELD_LEN];

	char from_name[EMAIL_FIELD_LEN];

	char from_address[EMAIL_FIELD_LEN];

	char subject[EMAIL_FIELD_LEN];

	char filename[MAX_FNAME_LEN];

	char bcc[EMAIL_FIELD_LEN];

	int delete_file_when_done_flag;

};

static int iEmailBgndThreadActive = FALSE;	// set while bgnd thread doing work

struct EmailQueueEntry *EmailQueueHead = NULL;



static void _cdecl EmailBgndThread(void *args)

{

  #if INCL_STACK_CRAWL

	volatile int top_of_stack_signature = TOP_OF_STACK_SIGNATURE;	// for stack crawl

  #endif

	RegisterThreadForDumps("Email thread");	// register this thread for stack dumps if we crash

	forever {

		// Grab the first entry from the queue...

		struct EmailQueueEntry *eq = NULL;

		iEmailBgndThreadActive = TRUE;

		EnterCriticalSection(&EmailCritSec);

		if (EmailQueueHead) {

			eq = EmailQueueHead;

			EmailQueueHead = eq->next;

		}

		LeaveCriticalSection(&EmailCritSec);

		if (eq) {

			// Send it out...

			char command_line[EMAIL_FIELD_LEN*5];

			zstruct(command_line);

			char bcc_command[EMAIL_FIELD_LEN+20];

			zstruct(bcc_command);



			// Turn all " characters in the subject line into ? so that

			// the command line quoting doesn't get screwed up.

			DelimitIllegalCommandLineChars(eq->subject, sizeof(eq->subject));

			DelimitIllegalCommandLineChars(eq->from_name, sizeof(eq->from_name));

			DelimitIllegalCommandLineChars(eq->from_address, sizeof(eq->from_address));

			DelimitIllegalCommandLineChars(eq->to, sizeof(eq->to));

			DelimitIllegalCommandLineChars(eq->bcc, sizeof(eq->bcc));



		  #if WIN32

			  #define SMTP_HOST  ".com"  //"smtp1.paralynx.com"

			if (strlen(eq->bcc)) {

				sprintf(bcc_command, "-bcc:\"%s\" ", eq->bcc);

			}

			sprintf(command_line,"postie.exe -host:\"%s\" -to:\"%s\" -from:\"%s\" %s-s:\"%s\" -file:\"%s\"",

					SMTP_HOST,

					eq->to,

					eq->from_address,

					bcc_command,

					eq->subject,

					eq->filename);

		  #else

			//kriskoin: 			// we want to send html formatted email, we could use 'mailto'.

			// Type 'man mailto' for more details.

			// It's possible that metasend may need to be used instead if we

			// need to set the mime preamble area to something (for example

			// the plain text version of the email).

			// 'mailto' also does not let you set the from address :(

			// Until we get around to dealing with all those issues, keep

			// using fastmail for plaintext.

			// test: metasend -f /tmp/home.html -s "html test" -t "test@kkrekop.io" -F "test@kkrekop.io" -c "" -e 7bit -m text/richtext -b

			// Also, it's not clear how to set the "from" email address so that

			// it is routable.  fastmail lets you set it to anything.  metasend does not :(



			if (strlen(eq->bcc)) {

				sprintf(bcc_command, "-b \"%s\" ", eq->bcc);

			}

			sprintf(command_line,"fastmail -f \"%s\" -F \"%s\" -s \"%s\" %s\"%s\" \"%s\"",

					eq->from_name,

					eq->from_address,

					eq->subject,

					bcc_command,

					eq->filename,

					eq->to);

		  #endif

			int rc = 0;

			//kp(("%s %s(%d) command line: %s\n", TimeStr(), _FL, command_line));

		  #if 1

FILE *fi=fopen("ok.tst.txt","w");

fprintf(fi,"%s\n",command_line);

fclose(fi);

			rc = system(command_line);

		  #endif

			if (rc) {

				int errcode = errno;

				Error(ERR_ERROR, "%s(%d) Email()'s system() returned %d/%d -- see DebWin", _FL, rc, errcode);

				kp(("%s(%d) Email()'s system() returned %d  : errno said %d:", _FL, rc,errcode));

				switch (errcode) {

				case E2BIG: 

					kp(("E2BIG: Argument list (which is system-dependent) is too big\n"));

					break;

				case ENOENT:

					{

					  #if DEBUG

						struct stat statbuf;

						zstruct(statbuf);

						int staterror = stat(eq->filename, &statbuf);

						kp(("ENOENT: Command interpreter cannot be found.\n"));

						kp(("%s(%d) File we wanted to email was: '%s', opened successfully = %s, size = %d\n", _FL, eq->filename, staterror ? "NO" : "Yes", statbuf.st_size));

					  #endif

					}

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

			  #if DEBUG

				struct stat statbuf;

				zstruct(statbuf);

				int staterror = stat(eq->filename, &statbuf);

				kp(("%s(%d) File we wanted to email: '%s', opened successfully = %s, size = %d\n", _FL, eq->filename, staterror ? "NO" : "Yes", statbuf.st_size));

			  #endif

			  #if !WIN32

				// This code was only tested for Linux.  I have no idea

				// what it might do under Windows.

				if (rc==-1) {

					// The problem is quite serious and probably indicates

					// corrupted memory on the server.

					// Alert any administrators of the problem.

					IssueCriticalAlert("Email failed with return code = -1. Memory might be corrupted.");

				}

			  #endif

			}

			if (eq->delete_file_when_done_flag) {

				remove(eq->filename);

			}

			free(eq);

			Sleep(50);	// never eat all the cpu time

		} else {

			iEmailBgndThreadActive = FALSE;

			Sleep(500);	// sleep much longer when nothing in queue

		}

		//kp(("%s(%d) EmailBgndThread() active.\n",_FL));

	//LeaveCriticalSection(&EmailCritSec);





	}

	iEmailBgndThreadActive = FALSE;



	UnRegisterThreadForDumps();

  #if INCL_STACK_CRAWL

	NOTUSED(top_of_stack_signature);

  #endif

 	NOTUSED(args);

}



//*********************************************************

// https://github.com/kriskoin
//

// Return TRUE if email queue is empty

//

int EmailQueueEmpty(void)

{

	if (EmailQueueHead || iEmailBgndThreadActive) {

		return FALSE;

	}

	return TRUE;

}	



/**********************************************************************************

 Function Email(char *to, char *from, char *subject, char *msg)

 date: kriskoin 2019/01/01
 Purpose: send email

 Returns: TRUE if successfully sent, FALSE otherwise.

***********************************************************************************/

int Email(char *to, char *from_name, char *from_address, char *subject, char *filename)

{

	return Email(to, from_name, from_address, subject, filename, NULL, FALSE);

}

int Email(char *to, char *from_name, char *from_address, char *subject, char *filename, char *bcc, int delete_file_when_done_flag)

{

	// Start bgnd thread if this is the first time we were called.

	if (!EmailThreadLaunchedFlag) {

		EmailThreadLaunchedFlag = TRUE;

		PPInitializeCriticalSection(&EmailCritSec, CRITSECPRI_LOCAL, "Email");

		int result = _beginthread(EmailBgndThread, 0, 0);

		if (result == -1) {

			Error(ERR_FATAL_ERROR, "%s(%d) _beginthread() failed.",_FL);

			return FALSE;

		}

	}



	if (!to || !to[0] || !from_name || !from_name[0] || !from_address ||

		!from_address[0] || !subject || !subject[0] || !filename || !filename[0]) {

		Error(ERR_INTERNAL_ERROR, "%s(%d) A null parameter was passed to Email(). Failing.",_FL);

		return FALSE;

	}

	// validate email addresses

	if (!strchr(to, '@') ||

		!strchr(from_address, '@') ||

		to[0]=='-' ||

		from_address[0]=='-'

	) {	// invalid address

		Error(ERR_INTERNAL_ERROR, "%s(%d) A bad email address was passed to Email(). Failing.",_FL);

		return FALSE;

	}



	// Create a new entry for our queue...

	struct EmailQueueEntry *eq = (struct EmailQueueEntry *)malloc(sizeof(EmailQueueEntry));

	if (!eq) {

		Error(ERR_ERROR, "%s(%d) malloc() failed trying to send email.",_FL);

		return FALSE;	// error!

	}

	zstruct(*eq);

	strnncpy(eq->to, to, EMAIL_FIELD_LEN);

	strnncpy(eq->from_name, from_name, EMAIL_FIELD_LEN);

	strnncpy(eq->from_address, from_address, EMAIL_FIELD_LEN);

	strnncpy(eq->subject, subject, EMAIL_FIELD_LEN);

	strnncpy(eq->filename, filename, MAX_FNAME_LEN);

	if (bcc) {

		strnncpy(eq->bcc, bcc, EMAIL_FIELD_LEN);

	}

	eq->delete_file_when_done_flag = delete_file_when_done_flag;



	// Add the new entry to our queue...

	EnterCriticalSection(&EmailCritSec);

	struct EmailQueueEntry **p = &EmailQueueHead;

	//kp(("%s(%d) Initial value for p = $%08lx (*p = $%08lx)\n", _FL, p, *p));

	while (*p) {

		//kp(("%s(%d) current value for p = $%08lx (*p = $%08lx)\n", _FL, p, *p));

		p = &((*p)->next);	// move to end of queue

	}

	//kp(("%s(%d) final value for p = $%08lx (*p = $%08lx)\n", _FL, p, *p));

	*p = eq;

	LeaveCriticalSection(&EmailCritSec);



	return TRUE;	// successfully queued

}



//*********************************************************

// https://github.com/kriskoin
//

// Print a formatted string to a file and then email the file using Email()

//

int EmailStr(char *to, char *from_name, char *from_address, char *subject, char *bcc, char *format_str, ...)

{

	char fname[MAX_FNAME_LEN];

	zstruct(fname);

	MakeTempFName(fname, "email");

	FILE *fd = fopen(fname, "wt");

	if (fd) {

	    va_list arg_ptr;

		va_start(arg_ptr, format_str);

		vfprintf(fd, format_str, arg_ptr);

		va_end(arg_ptr);

		fclose(fd);

		return Email(to, from_name, from_address, subject, fname, bcc, TRUE);

	} else {

		Error(ERR_ERROR, "%s(%d) Error opening file '%s' in EmailStr()",_FL,fname);

	}

	return FALSE;	// unsuccessfully queued.

}



//*********************************************************

// https://github.com/kriskoin
//

// Call an application call-back with a critical alert error

// message if a callback handler was installed.

//

void (*CriticalAlertHandler)(char *critical_alert_msg);	// user function for handling critical alerts from pplib



void IssueCriticalAlert(char *str)

{

	if (CriticalAlertHandler) {

		(*CriticalAlertHandler)(str);

	}

}

