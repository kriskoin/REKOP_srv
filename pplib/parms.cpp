//**********************************************************
//
//	Parameter file functions
//	(similar to windows.ini file format)
//
//	Written by Mike Benna, March 1995
//	Copyright (c) 1995 Mike Benna
//
//**********************************************************

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include "pplib.h"


#define MAX_PARM_LINE_LEN	500


/****************************************************************/
/*  Mon March 13/95 - MB										*/
/*																*/
/*	Skip over white space										*/
/*																*/
/****************************************************************/

void SkipWhiteSpace(char **p)
{
		char *s = *p;
		while (*s && (*s <= ' ' || *s=='\t'))
			s++;
		*p = s;
}

/****************************************************************/
/*  Thu June 13/96 - MB											*/
/*																*/
/*	Trim any trailing white space from a string					*/
/*																*/
/****************************************************************/

void TrimTrailingWhiteSpace(char *str)
{
		int len = strlen(str);
		char *p = str + len - 1;
		while (len > 0 && (*p==' ' || *p=='\t')) {
			*p-- = 0;
			len--;
		}
}


/****************************************************************/
/*  Fri March 3/95 - MB											*/
/*																*/
/*	Process a line from a parm file								*/
/*																*/
/****************************************************************/

void ParmProcess(ParmStruc *ps, char *line_ptr)
{
	static const char *separators = "= :|&";
	static const char integer_flags[PARMTYPE_COUNT] = {0,0,1,1,1,1,1,0,0,0};

	int or_result_flag = FALSE;
	int and_result_flag = FALSE;

	  #if DEBUG
		char *original_line_ptr = line_ptr;
	  #endif
		SkipWhiteSpace(&line_ptr);

		// extract the token from the parm line
		char token[MAX_PARM_LINE_LEN];
		zstruct(token);
		char *p = token;
		while (*line_ptr && !strchr(separators, *line_ptr))
			*p++ = *line_ptr++;
		*p = 0;
		SkipWhiteSpace(&line_ptr);
		if (*line_ptr=='|') {
			or_result_flag = TRUE;
			line_ptr++;
		}
		if (*line_ptr=='&') {
			and_result_flag = TRUE;
			line_ptr++;
		}
		while (*line_ptr && strchr(separators, *line_ptr))
			line_ptr++;	// skip over separators
		SkipWhiteSpace(&line_ptr);

//		kp(("token = '%s', parms = '%s'\n", token, line_ptr));

		// search for a match
		while (ps->token_name && ps->token_name[0]) {
			if (!stricmp(ps->token_name, token)) {	// found a match!
				long integer_value = 0;

				// Skip spaces and $'s as necessary...
				while (*line_ptr==' ') line_ptr++;
				if (ps->type != PARMTYPE_STRING)
					while (*line_ptr=='$') line_ptr++;
				while (*line_ptr==' ') line_ptr++;

				if (integer_flags[ps->type]) {
					// It's an integer value.  We've got extra things we can
					// do with it (such as |=, &=, and ~ to invert it.
					long original_value = 0;
					switch (ps->type) {	// parse based on parm format type
					case PARMTYPE_INT:
						original_value = *(int *)ps->data_ptr;
						break;
					case PARMTYPE_WORD:
					case PARMTYPE_SHORT:
						original_value = *(short *)ps->data_ptr;
						break;
					case PARMTYPE_LONG:
						original_value = *(long *)ps->data_ptr;
						break;
					case PARMTYPE_BYTE:
						original_value = *(byte *)ps->data_ptr;
						break;
					}

					// convert Yes, On, No, Off, etc. to 1 or 0
					if (!strnicmp(line_ptr, "Yes", 3) || !strnicmp(line_ptr, "On", 2))
						line_ptr = "1";
					else if (!strnicmp(line_ptr, "No", 2) || !strnicmp(line_ptr, "Off", 3))
						line_ptr = "0";

					int bitwise_invert_flag = FALSE;
					if (*line_ptr=='~') {
						bitwise_invert_flag = TRUE;
						line_ptr++;
						SkipWhiteSpace(&line_ptr);
					}
					if (!strncmp(line_ptr, "0x", 2)) {	// hex?
						line_ptr += 2;	// skip the '0x'
						// Convert from hex.
						sscanf(line_ptr, "%x", &integer_value);
					} else {	// default... treat as decimal
						integer_value = atol(line_ptr);
					}

					if (bitwise_invert_flag) {
						//kp(("%s(%d) inverting %02x to %02x\n", _FL, integer_value, ~integer_value));
						integer_value = ~integer_value;
					}

					if (or_result_flag) {
						//kp(("%s(%d) ORing %02x with %02x\n", _FL, integer_value, original_value));
						integer_value |= original_value;
					}
					if (and_result_flag) {
						//kp(("%s(%d) ANDing %02x with %02x\n", _FL, integer_value, original_value));
						integer_value &= original_value;
					}

					//kp(("%s(%d) Setting %s to %d (0x%02x, ~0x%02x)\n",  _FL, token, integer_value, integer_value, ~integer_value));
					// drop the integer_value result down to the reset of the code.
				}

				switch (ps->type) {	// parse based on parm format type
				case PARMTYPE_FLOAT:
					*(float *)ps->data_ptr = (float)atof(line_ptr);
					break;
				case PARMTYPE_DOUBLE:
					*(double *)ps->data_ptr = atof(line_ptr);
					break;
				case PARMTYPE_INT:
					*(int *)ps->data_ptr = integer_value;
					break;
				case PARMTYPE_WORD:
				case PARMTYPE_SHORT:
					*(short *)ps->data_ptr = (short)integer_value;
					break;
				case PARMTYPE_LONG:
					*(long *)ps->data_ptr = integer_value;
					break;
				case PARMTYPE_BYTE:
					*(byte *)ps->data_ptr = (byte)integer_value;
					break;
				case PARMTYPE_STRING:
					strnncpy((char *)ps->data_ptr, line_ptr, ps->max_length);
					break;
				case PARMTYPE_TIME:
					{ int hours, minutes, seconds;
						hours = minutes = seconds = 0;
						sscanf(line_ptr, "%d:%d:%d", &hours, &minutes, &seconds);
						*(long *)ps->data_ptr = hours*3600+minutes*60+seconds;
					}
					break;
				case PARMTYPE_DATE:
					{	struct tm t;
						zstruct(t);
						sscanf(line_ptr, "%d/%d/%d,%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec);
						if (t.tm_year > 1900)	// mktime wants years since 1900.
							t.tm_year -= 1900;
						t.tm_mon--;	// tm_mon goes from 0 to 11.
						*(long *)ps->data_ptr = mktime(&t);
					}
					break;
				}
				return;	// all done.
			}
			ps++;
		}

		kp((ANSI_ERROR"Parm file error: could not process line '%s'\n", original_line_ptr));
}

/****************************************************************/
/*  Fri January 5/96 - MB										*/
/*																*/
/*	Main Parsing function.  Takes a file descriptor.			*/
/*																*/
/****************************************************************/

void ReadParmFD(FILE *fd, char *section_name, ParmStruc *ps, int section_name_optional_flag)
{
	static int recursion_level;
	int found_our_section = FALSE;

		if (++recursion_level > 10) {
			kp((ANSI_ERROR"%s(%d) Error: recursion_level = %d.  Exiting to prevent endless loop.\n",
					__FILE__, __LINE__, recursion_level));
		}

		// We only read the global section if our recursion
		// level is 1 (first time through)
		int our_section = FALSE;
		if (recursion_level==1)
			our_section = TRUE;

		while (!feof(fd)) {
			char src_line[MAX_PARM_LINE_LEN];
			zstruct(src_line);
			fgets(src_line, MAX_PARM_LINE_LEN-1, fd);
			TrimNewlines(src_line);		// strip newline from end

			// strip white space from the beginning
			char *line_ptr = src_line;
			SkipWhiteSpace(&line_ptr);

			// replace tab characters with spaces
			char *p = line_ptr;
			while (*p) {
				if (*p=='\t') *p = ' ';
				p++;
			}
			TrimTrailingWhiteSpace(line_ptr);

			if (*line_ptr == '[') {	// section name
				// parse new section name
				line_ptr++;
				SkipWhiteSpace(&line_ptr);
				our_section = FALSE;
				p = strchr(line_ptr, ']');
				if (p) *p = 0;
				if (section_name && !stricmp(line_ptr, section_name)) {
					found_our_section = TRUE;
					our_section = TRUE;
				}
				if (!stricmp(line_ptr, "global")) {	// global section?
					// We only read the global section if our recursion
					// level is 1 (first time through)
					if (recursion_level==1)
						our_section = TRUE;
				}
			  #if 0
				kp(("section [%s], compared to [%s], result = %d\n",
						line_ptr, section_name, our_section));
			  #endif
			} else if (our_section && *line_ptr && *line_ptr!=';') {
			  #if 0
				kp(("%s(%d)\n", __FILE__, __LINE__));
			  #endif
				if (!strnicmp(line_ptr, "inherit ", 8)) {
					// Inherit another token's settings.
					// Extract the section name.
					char inherited_section[MAX_PARM_LINE_LEN];
					zstruct(inherited_section);
					char *p = inherited_section;
					line_ptr += 8;
					SkipWhiteSpace(&line_ptr);
					while (*line_ptr >= 32)
						*p++ = *line_ptr++;
					*p = 0;
					TrimTrailingWhiteSpace(inherited_section);
		//			kp(("Processing inherited section '%s'\n", inherited_section));
					long old_pos = ftell(fd);
					fseek(fd, 0, SEEK_SET);
					ReadParmFD(fd, inherited_section, ps, FALSE);
					fseek(fd, old_pos, SEEK_SET);
				} else {
					ParmProcess(ps, line_ptr);
				}
			}
		}
		if (!found_our_section && section_name && section_name[0] && !section_name_optional_flag) {
			// If this was our first time through and there's a year override
			// number, try replacing any digits in the section name with a lowercase
			// y and then search again.
			kp((ANSI_ERROR"%s(%d): cannot find section '%s'\n",_FL,section_name));
		}
		recursion_level--;
}


/****************************************************************/
/*  Fri March 3/95 - MB											*/
/*																*/
/*	Read and parse a parameter file								*/
/*	returns 0 for success, else error code						*/
/*																*/
/****************************************************************/

int ReadParmFile(char *fname, char *section_name, ParmStruc *ps, int section_name_optional_flag)
{
	FILE *fd;

		fd = fopen(fname, "rt");
		if (!fd) {
			kp((ANSI_ERROR"Error reading parameter file '%s'\n", fname));
			return 1;
		}

		ReadParmFD(fd, section_name, ps, section_name_optional_flag);

	  #if 0
		kp(("%s(%d)\n", __FILE__, __LINE__));
	  #endif
		fclose(fd);
		return 0;
}
