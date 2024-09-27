/****************************************************/
/*                                                  */
/*  MindSpan Library                                */
/*  ================                                */
/*                                                  */
/*  Filename Utilities Module                       */
/*  by Mike Benna & Jeff Sember                     */
/*  file created August 1, 1990                     */
/*  Copyright (c) 1991 MindSpan Technologies Corp.  */
/*                                                  */
/****************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>
  #include <direct.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include "pplib.h"

#ifdef WIN32
  #define DIR_CHAR		'\\'
  #define DIR_CHAR_STR	"\\"
#else
  #define DIR_CHAR		'/'
  #define DIR_CHAR_STR	"/"
#endif

// These routines have been modified to be HPFS filename compatible.

/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Extract the name & extension from a full path               */
/*                                                              */
/****************************************************************/

void GetNameFromPath(const char *full_path, char *dest)
{
    const char *s;
    char c;

        s = full_path;
        while ((c = *full_path++) != 0)
            if (c=='\\' || c=='/' || c==':')
                s = full_path;
        strcpy(dest, s);
        FixPath(dest);
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Extract the drive & directory from a full path              */
/*                                                              */
/****************************************************************/

void GetDirFromPath(const char *full_path, char *dest)
{
    char *s,*l,c;

        strcpy(dest, full_path);
        FixPath(dest);
        s = dest;
        l = NULL;
        while ((c = *s) != 0) {
            if (c=='\\' || c=='/' || c==':')
                l = s;
            s++;
        }
        if (l)
            *(++l) = 0;
}

//*********************************************************
// https://github.com/kriskoin//
// Extract the root path from a full path (e.g. "c:" or "\\machine\share")
// The destination string must be at least as long as the source string.
//
void GetRootFromPath(const char *full_path, char *dest)
{
	strcpy(dest, full_path);	// start by copying.
	// If the 2nd character is a colon, it's drive letter format.
	if (dest[1]==':') {
		dest[0] = (char)toupper(dest[0]);
		dest[2] = 0;	// terminate it and we're done.
	} else {	// UNC format
		// It should start with '\\'
		char *s = dest;
		while (*s=='/' || *s=='\\') s++;	// skip "\\"
		while (*s!='/' && *s!='\\') s++;	// skip machine name
		while (*s=='/' || *s=='\\') s++;	// skip "\"
		while (*s!='/' && *s!='\\') s++;	// skip share name
		*s = 0;
	}
}

/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Trim off existing filename extension (if any)               */
/*                                                              */
/****************************************************************/

void TrimExtension(char *full_path)
{
    char *s,*d,c;

        FixPath(full_path);
        s = full_path;
        d = NULL;

        while ((c = *s) != 0) {
            if (c=='\\' || c=='/' || c==':')
                d = NULL;
            else if (c == '.')
                d = s;
            s++;
        }
        if (d)
            *d = 0;
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Extract the extension portion of a full path                */
/*                                                              */
/****************************************************************/

void GetExtension(const char *full_path, char *dest)
{
    const char *s, *d;
	char c;

        s = full_path;
        d = NULL;
        while ((c = *s++) != 0) {
            if (c=='\\' || c=='/' || c==':')
                d = NULL;
            else if (c == '.')
                d = s;
        }
        if (d)
            strcpy(dest, d);
        else
            *dest = 0;
        FixPath(dest);
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Set filename extension.  Trims existing one if necessary.   */
/*                                                              */
/****************************************************************/

void SetExtension(char *full_path, const char *new_extension)
{
        TrimExtension(full_path);
        if (*new_extension!='.')
            strcat(full_path, ".");
        strcat(full_path, new_extension);
        FixPath(full_path);
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Determine the current directory.  Adds '\' if necessary.    */
/*                                                              */
/****************************************************************/

void GetCurrentDir(char *dest)
{
    int i;

        getcwd(dest,MAX_FNAME_LEN);
        FixPath(dest);
        i = strlen(dest)-1;
        if (i<0 || dest[i] != DIR_CHAR)
            strcat(dest,DIR_CHAR_STR);
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Add a '\' to the end of a drive & dir path if none exists   */
/*                                                              */
/****************************************************************/

void AddBackslash(char *drive_and_dir)
{
    int i;
    char c;

        FixPath(drive_and_dir);
        i = strlen(drive_and_dir);
        if (i) {
            c = drive_and_dir[i-1];
            if (c != DIR_CHAR)
                strcat(drive_and_dir,DIR_CHAR_STR);
        }
}


/****************************************************************/
/*  Thu 08-01-1991 - MB                                         */
/*                                                              */
/*  Turn a path into uppercase and generally fix it up          */
/*                                                              */
/****************************************************************/

void FixPath(char *full_path)
{
    char *s;

    /* remove any trailing spaces */

    if (strlen(full_path)) {
        s = full_path + strlen(full_path) - 1;
		// adate: added trimming of \n
        while ((*s==' ' || *s=='\n') && s >= full_path) {
        	*s-- = 0;
        }
    }

    /* convert '/' to '\' */

    s = full_path;
    while (*s) {
	  #ifdef WIN32
        if (*s == '/') *s = '\\';
	  #else
        if (*s == '\\') *s = '/';
	  #endif
        s++;
    }
}

//*********************************************************
// https://github.com/kriskoin//
// Turn all backslashes into forward slashes for URL's
//
void FixUrl(char *full_path)
{
	/* remove any trailing spaces */
	char *s;
    if (strlen(full_path)) {
        s = full_path + strlen(full_path) - 1;
        while (*s==' ' && s >= full_path) *s-- = 0;
    }

    /* convert '\' to '/' */

    s = full_path;
    while (*s) {
        if (*s == '\\') *s = '/';
        s++;
    }
}


#if 0	// not currently implemented
/****************************************************************/
/*  Thu December 8/94 - MB										*/
/*																*/
/*	Return the volume name for a drive given a pathname			*/
/*																*/
/****************************************************************/

void GetVolumeName(char *srcpath, char *destname)
{
  #ifdef FF_LABEL
	struct findblk fdata;
	unsigned drive_letter;
	char fname[MAX_FNAME_LEN];

		zstruct(fdata);
		destname[0] = 0;
		fname[0] = srcpath[0];
		drive_letter = srcpath[0];
		if (!srcpath[0] || srcpath[1]!=':' || !isalpha(drive_letter)) {
	        _dos_getdrive(&drive_letter);
			drive_letter += 'A';
		}
		sprintf(fname, "%c:\\*.*", drive_letter);
		CritErrFlag = FALSE;
		if (!ffirst(fname, &fdata, FF_LABEL))
			if (!CritErrFlag)
				strcpy(destname, fdata.name);
		  #if DEBUG
			else
				kputs("Critical error occurred reading volume label.");
		  #endif
		CritErrFlag = FALSE;
//		kprintf("volume label for drive '%s' is '%s'\n", fname, destname);
  #else
		kp(("%s(%d) GetVolumeName() not yet supported\n",_FL));
		*destname = 0;
  #endif
}
#endif

//**************************************************
// 
//
//	Offset the input string pointer so that it points
//	to the name portion of a pathname.  This is useful
//	mainly for the _FL macro which needs to filter out
//	the pathname.
//
char *GetNameFromPath2(char *full_path)
{
    char *s, c;

	s = full_path;
    while ((c = *full_path++) != 0)
		if (c=='\\' || c=='/' || c==':')
			s = full_path;
	return s;
}

//*********************************************************
// https://github.com/kriskoin//
// Read a file into a block of memory.
//
ErrorType ReadFile(char *fname, void *buffer, long buffer_len, long *bytes_read_ptr)
{
	long bytes_read = 0;
	if (bytes_read_ptr) {
		*bytes_read_ptr = bytes_read;
	}
	FILE *fd = fopen(fname, "rb");
	if (!fd) {
		return ERR_ERROR;	// could not open file.
	}
	bytes_read = fread(buffer, 1, buffer_len, fd);
	fclose(fd);
	if (bytes_read_ptr) {
		*bytes_read_ptr = bytes_read;
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Write a block of memory to a file.
//
ErrorType WriteFile(char *fname, void *buffer, long buffer_len)
{
	long bytes_written = 0;
	FILE *fd = fopen(fname, "wb");
	if (!fd) {
		return ERR_ERROR;	// could not open file.
	}
	bytes_written = fwrite(buffer, 1, buffer_len, fd);
	fclose(fd);
	if (bytes_written != buffer_len) {
		return ERR_ERROR;
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Load an entire file into memory.  Allocate just enough
// memory to hold the file.  Use free() to free it when done.
//
void *LoadFile(char *fname, long *bytes_read_ptr)
{
	// Open the file and determine the file length
	void *ptr = NULL;
	long bytes_read = 0;
	if (bytes_read_ptr) {
		*bytes_read_ptr = bytes_read;
	}
	FILE *fd = fopen(fname, "rb");
	if (!fd) {
		//kp(("%s(%d) LoadFile('%s') failed to open file.\n", _FL, fname));
		return NULL;	// could not open file.
	}
	// Seek to the end to get the length
	fseek(fd, 0, SEEK_END);
	long len = ftell(fd);
	//kp(("%s(%d) file length of '%s' is %d bytes\n", _FL, fname, len));
	fseek(fd, 0, SEEK_SET);
	// Allocate memory
	ptr = malloc(len+1);
	if (!ptr) {
		//kp(("%s(%d) could not allocate %d bytes for file '%s'\n", _FL, len+1, fname));
		fclose(fd);
		return NULL;
	}

	bytes_read = fread(ptr, 1, len, fd);
	fclose(fd);
	if (bytes_read_ptr) {
		*bytes_read_ptr = bytes_read;
	}
	*((char *)ptr + bytes_read) = 0;	// always make sure it's NULL terminated (AFTER the data)
	return ptr;
}

#if WIN32
//*********************************************************
// https://github.com/kriskoin//
// Set the timestamp for a file given time_t
//
int SetFileTime_t(char *filename, time_t t)
{
	//kriskoin: 	// function which takes time_t as a parameter, just one
	// that takes FILETIME.  Convert from time_t to FILETIME.
	HANDLE handle = CreateFile(filename,
			GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
			NULL, OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,
			NULL);
	if (handle == INVALID_HANDLE_VALUE) {
		return 1;	// error
	}
	// open succeeded.
	FILETIME ft;
	zstruct(ft);
    LONGLONG ll = Int32x32To64(t, 10000000) + 116444736000000000;
    ft.dwLowDateTime = (DWORD)ll;
    ft.dwHighDateTime = (DWORD)(ll >> 32);
	SetFileTime(handle, &ft, &ft, &ft);
	CloseHandle(handle);
	return 0;	// no error.
}	

//*********************************************************
// https://github.com/kriskoin//
// Calculate available disk space on a drive.
// If there is more than 4GB free, this function maxes it
// out at 4GB so that it will fit into a WORD32.
// Use "c:\blah blah blah" or "\\machine\share\" as typical root paths.
// NULL can be passed to use the current directory.
//
WORD32 CalcFreeDiskSpace(char *root_path)
{
	DWORD sectors_per_cluster = 0;
	DWORD bytes_per_sector = 0;
	DWORD free_clusters = 0;
	DWORD total_clusters = 0;
	// Special case fix: if the user passed 'C:' instead of 'C:\', append a backslash.
	char new_path[5];
	zstruct(new_path);
	if (strlen(root_path)>=2 && root_path[1]==':') {
		strnncpy(new_path, root_path, 3);
		strcat(new_path, "\\");
		root_path = new_path;
	}
	BOOL success = GetDiskFreeSpace(root_path, &sectors_per_cluster,
			&bytes_per_sector, &free_clusters, &total_clusters);
	if (!success) {
		kp(("%s(%d) GetDiskFreeSpace('%s',...) failed.  Error = %d\n", _FL, root_path, GetLastError()));
	}
	// Convert the results into total bytes avail.
    ULONGLONG ull_total_avail = UInt32x32To64(sectors_per_cluster*bytes_per_sector, free_clusters);
	// Don't go over 4GB free (so it fits in a DWORD)
	DWORD total_avail = (DWORD)ull_total_avail;
	if (ull_total_avail > (DWORD)-1) {
		total_avail = (DWORD)-1;
	}
	return total_avail;
}	



//*********************************************************
// https://github.com/kriskoin//
// Look for a file in the current directory and the data/media
// directory.  Return a pointer to the filename.
// Warning: this function has only one static buffer for
// the filename; if called twice in a row, the first result
// will be overwritten.  This also makes it usable ONLY from
// a single thread.
// Added by Allen Ko to change the cards bitmap from the data
// directory to data\media directory.
// 9-24-2001
char *FindMediaFile(char *fname)
{
	if (!fname) {
		return NULL;
	}
	struct stat s;
	zstruct(s);
	if (!stat(fname, &s)) {
		// Found it.
		return fname;
	}
	char test_name[MAX_FNAME_LEN], basename[MAX_FNAME_LEN];
	strcpy(test_name, "data\\media\\");
	GetNameFromPath(fname, basename);
	strcat(test_name, basename);
	if (!stat(test_name, &s)) {
		// Found it.
		static char data_fname[MAX_FNAME_LEN];	// note: only one copy of this buffer.
		strcpy(data_fname, test_name);	// copy to static storage.
		return data_fname;
	}
	// Not found, just return regular name.
	return fname;
}	

//*********************************************************
// https://github.com/kriskoin//
// Look for a file in the current directory and the data/
// directory.  Return a pointer to the filename.
// Warning: this function has only one static buffer for
// the filename; if called twice in a row, the first result
// will be overwritten.  This also makes it usable ONLY from
// a single thread.
//
char *FindFile(char *fname)
{
	if (!fname) {
		return NULL;
	}
	struct stat s;
	zstruct(s);
	if (!stat(fname, &s)) {
		// Found it.
		return fname;
	}
	char test_name[MAX_FNAME_LEN], basename[MAX_FNAME_LEN];
	strcpy(test_name, "data\\");
	GetNameFromPath(fname, basename);
	strcat(test_name, basename);
	if (!stat(test_name, &s)) {
		// Found it.
		static char data_fname[MAX_FNAME_LEN];	// note: only one copy of this buffer.
		strcpy(data_fname, test_name);	// copy to static storage.
		return data_fname;
	}
	// Not found, just return regular name.
	return fname;
}	
#endif //WIN32






//*********************************************************
// https://github.com/kriskoin//
// Create a temporary filename.
//
static int iTempFNameIndex;

void MakeTempFName(char *dest, char *first_few_chars)
{
	forever {
	  #if WIN32
	  	char *tmp = getenv("TEMP");
		if (!tmp) {
			tmp = "c:\\temp";
		}
		strcpy(dest, tmp);
		strcat(dest, "\\");
	  #else
		strcpy(dest, "/tmp/");
	  #endif
		sprintf(dest+strlen(dest), "%s%07d.tmp", first_few_chars, iTempFNameIndex++);

		struct stat s;
		zstruct(s);
		if (stat(dest, &s)) {
			// Didn't find it.
			return;;
		}
		// We found it... try another name.
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Calculate how long since a file was last modified (in seconds)
// Returns -1 if file age could not be determined.
//
long GetFileAge(char *fname)
{
	struct stat fstat;
	zstruct(fstat);
	int result = stat(fname, &fstat);
	if (result == 0) {	// success
		time_t now = time(NULL);
		return now - fstat.st_mtime;
	} else {	// _fstat() failed.
		return -1;	// age could not be determined.
	}
}

