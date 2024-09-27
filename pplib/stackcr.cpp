//******************************************************************
//                                                 
//	Stack crawl functions
//
//  by Mike Benna
//                                                 
//	These functions should be compilable on most platforms
//	with minor changes for .MAP file formats and CPU opcodes.                                                 
//                                                 
//******************************************************************

// Note: I couldn't find the symbol to indicate GCC so I'm just using LINUX.

#define DISP 			0
#define DISP_SYMFILE	0

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include "pplib.h"

#if INCL_STACK_CRAWL
extern "C" {
	int main(void);
  #ifdef __WATCOMC__
	extern char **_argv;
	extern int __begtext, _int_code_end;
  #endif
	int StackCrawlDisable;
}

#define SYMFILE_MAX_SYMBOLS			8000	// max # of symbols we reserve space for
#define MAX_SYMBOLNAME_LEN			32
#define SYMFILE_NAMESPACE_SIZE		(SYMFILE_MAX_SYMBOLS*MAX_SYMBOLNAME_LEN/2)

#define MAX_SYMBOL_STRING_COPIES	6		// number of independant names we maintain

void SymFile_Load(void);
int iSymFile_Loaded;				// set if we've tried to load the symbol file.
dword dwSymFile_SymbolCount;		// # of symbols loaded
char *SymFile_NameSpace;			// ptr to base of area we store names in
char *SymFile_NextNameStoragePtr;	// ptr to where we'll store the next name
int iSymFile_LoadError;				// set if there was an error loading the symbol file
dword dwSymFile_RelocationOffset;	// relocation offset for symbols
char *SymFile_ExecutableName;		// ptr to argv[0] (if _argv[0] not available on this compiler)

struct SymFile_SymbolEntry {
	dword value;
	char *name;
} *SymFile_EntryTable;


//****************************************************************
//  Tue November 18/97 - MB
//
//	Add a symbol to our internal symbol table
//
void SymFile_AddSymbol(char *label, dword val)
{
		if (!SymFile_NameSpace) {	// Allocate space for the names...
			//SetAllocName("Symbol names (stack crawl)");
			SymFile_NameSpace = (char *)malloc(SYMFILE_NAMESPACE_SIZE);
			if (!SymFile_NameSpace) DIE("Can't allocate SymFile name space");
			SymFile_NextNameStoragePtr = SymFile_NameSpace;
		}
		if (!SymFile_EntryTable) {
			//SetAllocName("Symbol values & ptrs (stack crawl)");
			SymFile_EntryTable = (struct SymFile_SymbolEntry *)malloc(sizeof(*SymFile_EntryTable)*SYMFILE_MAX_SYMBOLS);
			if (!SymFile_EntryTable) DIE("Can't allocate SymFile value table");
			dwSymFile_SymbolCount = 0;
		}

		if (dwSymFile_SymbolCount < SYMFILE_MAX_SYMBOLS) {
			SymFile_EntryTable[dwSymFile_SymbolCount].value = val;
			SymFile_EntryTable[dwSymFile_SymbolCount].name = SymFile_NextNameStoragePtr;
			int len = strlen(label) + 1;
			if (SymFile_NextNameStoragePtr + len >= SymFile_NameSpace + SYMFILE_NAMESPACE_SIZE)
				DIE("Symbol name table full");
			strcpy(SymFile_NextNameStoragePtr, label);
			SymFile_NextNameStoragePtr += len;
		  #if DISP_SYMFILE
			kp(("%s(%d) SymFile_AddSymbol: $%08lx %s\n", _FL,
					SymFile_EntryTable[dwSymFile_SymbolCount].value,
					SymFile_EntryTable[dwSymFile_SymbolCount].name));
		  #endif
			dwSymFile_SymbolCount++;
		} else {
			DIE("Symbol entry table full");
		}
}

//****************************************************************
//  Tue November 18/97 - MB
//
//	Search for a name in the symbol table.  Only returns an
//	exact match.  NULL if none.
//
struct SymFile_SymbolEntry *SymFile_SearchByName(char *name)
{
		if (!iSymFile_Loaded) {
			SymFile_Load();
		}

		// Since this isn't going to be called very often and I'd rather
		// have reliable code than fast code, I've chosen to do a
		// linear search of the symbol table rather than a binary search
		// (which would require an alphabetically sorted list).
		// I know it's less efficient, but I don't feel the time savings
		// is worth the additional coding time and increased chance of bugs.

		struct SymFile_SymbolEntry *e = SymFile_EntryTable;
		for (dword i=0 ; i<dwSymFile_SymbolCount ; e++, i++) {
			if (!strcmp(e->name, name))
				return e;
		}
		return NULL;
}

//****************************************************************
//  Tue November 18/97 - MB
//
//	Load the symbol file for our .EXE
//
void SymFile_Load(void)
{
		if (iSymFile_Loaded) return;	// don't try again.
		iSymFile_Loaded = TRUE;
		iSymFile_LoadError = 0;
		char fname[MAX_FNAME_LEN];
		memset(fname, 0, MAX_FNAME_LEN);	// always initialize

	  #ifdef __WATCOMC__	//:::		strnncpy(fname,_argv[0], MAX_FNAME_LEN);
	  #elif LINUX	// GCC
		strnncpy(fname, SymFile_ExecutableName, MAX_FNAME_LEN);
	  #else
		strnncpy(fname,__argv[0], MAX_FNAME_LEN);
	  #endif
		SetExtension(fname,"map");

		FILE *fd = fopen(fname, "rt");
		if (fd) {
			// Parse the ASCII file into an array of symbols
			#define MAX_SYMFILE_LINE_LEN	200
			char line[MAX_SYMFILE_LINE_LEN];
			int file_type = 0;
			while (fgets(line, MAX_SYMFILE_LINE_LEN, fd) != NULL) {
				// Remove trailing newline chars
				char *p = strchr(line, '\n');
				if (p) *p = 0;
				p = strchr(line, '\r');
				if (p) *p = 0;

				switch (file_type) {
				case 0:	// determine file type
					if (strstr(line, "WATCOM Linker")) {
						file_type = 1;	// type 1 = Watcom .MAP file.
					} else if (strstr(line, "    Start     Stop   Length")) {
						file_type = 2;	// type 2 = PsyQ .MAP file for PSX
					} else if (strstr(line, " Start         Length     Name")) {
						file_type = 3;	// type 3 = Visual Studio .MAP file for Windows
					} else if (strstr(line, "GCC map file")) {
						file_type = 4;	// type 4 = GCC under linux
					} else {
					  #if DISP_SYMFILE
						kp(("%s(%d) Discarding line '%s'\n", _FL, line));
					  #endif
					}
					break;

				case 1: // watcom for Win32
					// If the line starts with "0001:" then it's probably
					// a text (code segment) symbol.  This is obviously a hack.
					if (!strncmp(line, "0001:", 5)) {
						dword val;
						if (sscanf(line+5, "%x", &val)!=1)
							val = (dword)-1;
						p = strchr(line, ' ');
						if (p) {
							while (*p==' ') p++;
							SymFile_AddSymbol(p, val);
						} else {
						  #if DISP_SYMFILE
							kp(("%s(%d) Discarding line '%s'\n", _FL, line));
						  #endif
						}
					} else {
					  #if DISP_SYMFILE
						kp(("%s(%d) Discarding line '%s'\n", _FL, line));
					  #endif
					}
					break;

				case 2: // PsyQ for PSX
				  #if DISP_SYMFILE
					kp(("%s(%d) Discarding line '%s'\n", _FL, line));
				  #endif
					break;

				case 3: // type 3 = Visual Studio .MAP file for Windows
					// If the line starts with " 0001:" then it's probably
					// a text (code segment) symbol.  This is obviously a hack.
					if (!strncmp(line, " 0001:", 6)) {
						dword val;
						if (sscanf(line+6, "%x", &val)!=1)
							val = (dword)-1;
						p = strchr(line+6, ' ');
						if (p) {
							while (*p==' ') p++;
							// If p points to another number, it's probably
							// not a symbol name, it's a length, so skip over it.
							if (*p=='0') {
								p = strchr(p, ' ');	// search for next space
								if (p) {
									while (p && *p==' ') p++;
								}
							}
							if (p) {
								// Trim symbol at first space
								char *space = strchr(p, ' ');
								if (space) {
									*space = 0;
								}
								SymFile_AddSymbol(p, val);
							}
						} else {
						  #if DISP_SYMFILE
							kp(("%s(%d) Discarding line '%s'\n", _FL, line));
						  #endif
						}
					} else {
					  #if DISP_SYMFILE
						kp(("%s(%d) Discarding line '%s'\n", _FL, line));
					  #endif
					}
					break;
				case 4: // type 4 = GCC under linux
					// Look for lines that look like this:
					// 0804a1e0 T AcceptThreadEntry__8CardRoom
					if (strlen(line) >= 11 && line[9]=='T') {
						dword val;
						if (sscanf(line, "%x", &val)!=1)
							val = (dword)-1;
						p = line+11;
						SymFile_AddSymbol(p, val);
					} else {
					  #if DISP_SYMFILE
						kp(("%s(%d) Discarding line '%s'\n", _FL, line));
					  #endif
					}
					break;
				}
			}
			fclose(fd);

			// Determine the relocated offset for symbols.
		  #ifdef __WATCOMC__
			struct SymFile_SymbolEntry *e = SymFile_SearchByName("___begtext");
			if (!e) DIE("Could not calculate relocation offset (no .MAP file?)");
			dwSymFile_RelocationOffset = (dword)&__begtext - e->value;
		  #elif _MSC_VER
		  	// use memset() because it's very likely to be linked in.
			struct SymFile_SymbolEntry *e = SymFile_SearchByName("_memset");
			if (!e) DIE("Could not calculate relocation offset (no .MAP file?  memset() not used?");
			dwSymFile_RelocationOffset = (dword)memset - e->value;
		  #elif LINUX
		  	// use main() because it's very likely to be linked in.
			struct SymFile_SymbolEntry *e = SymFile_SearchByName("main");
			if (!e) DIE("Could not calculate relocation offset (no .MAP file?  memset() not used?");
			dwSymFile_RelocationOffset = (dword)main - e->value;
		  #else
			DIE("Symbol relocation not yet supported on this platform.");
		  #endif
		  	//kp(("%s(%d) dwSymFile_RelocationOffset = $%08lx\n", _FL, dwSymFile_RelocationOffset));
		} else {
			kp(("%s(%d) Warning: could not load .MAP file ('%s')\n",_FL,fname));
			iSymFile_LoadError = TRUE;
		}
}

//****************************************************************
//  Tue November 18/97 - MB
//
//	Search for a value in the symbol table.  Returns the
//	highest symbol value less than or equal to val.  NULL if none.
//
struct SymFile_SymbolEntry *SymFile_SearchByValue(dword val)
{
		// Since this isn't going to be called very often and I'd rather
		// have reliable code than fast code, I've chosen to do a
		// linear search of the symbol table rather than a binary search.
		// I know it's less efficient, but I don't feel the time savings
		// is worth the additional coding time and increased chance of bugs.

		//kp(("%s(%d) SymFile_SearchByValue: %08lx, %d symbols are loaded.\n", _FL, val, dwSymFile_SymbolCount));
		struct SymFile_SymbolEntry *best_e = NULL;
		struct SymFile_SymbolEntry *e = SymFile_EntryTable;
		for (dword i=0 ; i<dwSymFile_SymbolCount ; e++, i++) {
			//kp(("%s(%d)    looking for $%08lx, found $%08lx (diff = $%08lx)\n", _FL, val, e->value, val - e->value));
			if (e->value <= val && val - e->value < 5000)  {	// near?
				if (best_e) {
					// Make sure it's closer than what we already have.
					if (e->value > best_e->value) {
						best_e = e;
					}
				} else {
					best_e = e;
				}
			}
		}
		//kp(("%s(%d) best symbol we found: %s\n", _FL, best_e ? best_e->name : "(none)"));
		return best_e;
}

//****************************************************************
//  Tue November 18/97 - MB
//
//	Lookup an address in the symbol file and return an ASCII string
//	suitable for printing.
//
//	This function maintains a list of several internal strings
//	so the return paramter can be passed to kprintf().  This means
//	you can rely on the returned string to be valid until this
//	function has been called many times (originally 6).
//
char *SymFile_Lookup(byte *ptr)
{
	static char symbol_strings[MAX_SYMBOL_STRING_COPIES][MAX_SYMBOLNAME_LEN];
	static int symbol_string_index;

		// If our symbol file isn't loaded yet, load it now.
		if (!iSymFile_Loaded) {
			SymFile_Load();
		}

		dword unrelocated_value = (dword)ptr - dwSymFile_RelocationOffset;
		char *p = symbol_strings[symbol_string_index];
		symbol_string_index = (symbol_string_index+1)%MAX_SYMBOL_STRING_COPIES;

		struct SymFile_SymbolEntry *e = SymFile_SearchByValue(unrelocated_value);
		if (e) {
			if (e->value == unrelocated_value) {
				strnncpy(p, e->name, MAX_SYMBOLNAME_LEN);
			} else {
				char offset[20];
			  #if 1	// adate: hex offset
				sprintf(offset, "+$%x", unrelocated_value - e->value);
			  #else	// decimal offset
				sprintf(offset, "+%u", unrelocated_value - e->value);
			  #endif
				strnncpy(p, e->name, MAX_SYMBOLNAME_LEN-strlen(offset));
				strcat(p, offset);
			}
		} else {
			sprintf(p, "$%08lx", ptr);
		}

		return p;
}

//****************************************************************
//  Wed March 25/98 - MB
//
//	Determine the stack pointer and call DisplayStackCrawlEx()
//
void DisplayStackCrawl(void)
{
	  #if _MSC_VER
		// disable: warning C4611: interaction between '_setjmp' and C++ object destruction is non-portable
		#pragma warning( disable : 4611 )
	  #endif
		jmp_buf env;
		zstruct(env);
		setjmp(env);

		if (dprintf_Disabled) return;	// if we can't print it, don't do it.
		if (StackCrawlDisable) {
			kp(("(stack crawl disabled)\n"));
			return;
		}
	  #if 0	//:::		// Try to reverse engineer the format of the jmp_buf...
		kp(("%s(%d) Here are the values in the jmp_buf array:\n",_FL));
		//khexdump(&env, sizeof(env), 8, 4);
		for (int i=0 ; i<sizeof(env)/4 ; i++) {
			kp(("   #%2d: $%08lx\n", i, ((dword *)&env)[i]));
		}
		kp(("%s(%d) For reference, the address of a stack variable is $%08lx\n",
				_FL, env));
	  #endif
		dword *stack_ptr = NULL;
	  #ifdef __WATCOMC__
		// extract stack ptr from jmp environment.  This is highly compiler
		// and platform dependant.  It is subject to easy breaking.  It
		// is also undocumented... I figured it out by reverse engineering.
		// The offset is 0x1C, which is 7*4.
		stack_ptr = (dword *)env[7];
	  #elif _MSC_VER	// Microsoft C?
		stack_ptr = (dword *)(((_JUMP_BUFFER *)env)->Esp);
		//kp(("%s(%d) stack_ptr = $%08lx\n", _FL, stack_ptr));
	  #elif LINUX	// GCC?
		stack_ptr = (dword *)((dword *)&env)[JB_SP];
		//kp(("%s(%d) stack_ptr (from jmpbuf) = $%08lx\n", _FL, stack_ptr));
	  #else
		kp((ANSI_ERROR"%s(%d) Stack Crawl not yet implemented for this platform.\n",_FL));
		return;
	  #endif
		DisplayStackCrawlEx(stack_ptr, 0, GetThreadName());
}

//****************************************************************
//  Tue November 18/97 - MB
//
//	Display a stack crawl to the debug window
//
void DisplayStackCrawlEx(dword *stack_ptr, dword eip, char *thread_name)
{
		if (dprintf_Disabled) return;	// if we can't print it, don't do it.
		if (StackCrawlDisable) {
			kp(("(stack crawl disabled)\n"));
			return;
		}

		void *code_start, *code_end;
	  #ifdef __WATCOMC__
		// Calculate (roughly) the start and end of the code segment
		// so we can avoid reading from non-code areas and causing GPF's.
		code_start = &__begtext;
		code_end = &_int_code_end;
	  #elif _MSC_VER
		{
			code_start = code_end = NULL;
			struct SymFile_SymbolEntry *se;
			se = SymFile_SearchByName(".text");
			if (se) {
				code_start = (void *)(se->value + dwSymFile_RelocationOffset);
			}
			se = SymFile_SearchByName(".textbss");
			if (se) {
				code_end = (void *)(se->value + dwSymFile_RelocationOffset);
			}
			if (!code_start || !code_end) {
			  #ifndef HORATIO
				kp((ANSI_ERROR"%s(%d) Could not locate code_start and code_end\n",_FL));
			  #endif
				return;
			}
		}
	  #elif LINUX // (GCC)
		extern int _start, _end;
		code_start = &_start;
		code_end = &_end;
	  #else
		kp((ANSI_ERROR"%s(%d) Stack Crawl not yet implemented for this platform.\n",_FL));
		return;
	  #endif

		// Try to estimate the top of the stack so we know when to
		// stop searching.
	  #if 1	//:::		//kp(("%s(%d) searching a LOT of the stack\n",_FL));
		dword *stack_top = stack_ptr + 25000;
	  #else
		dword *stack_top = stack_ptr + 512;	// !!! hack... we should do something better.
	  #endif

	  #if 0	//:::		kp(("%s(%d) Functions are probably between $%08lx and %08lx\n",
				_FL, code_start, code_end));
		kp(("%s(%d) Stack is probably between $%08lx and %08lx\n",
				_FL, stack_ptr, stack_top));
	  #endif

		// Step through the stack and look for possible return addresses
		kp(("-------------- Stack Crawl for stack at $%08lx"));
		if (thread_name) {
			kp((" (thread '%s')", thread_name));
		}
		kp((" --------------\n", stack_ptr));
		kp(("eip        Called from                        Function                           Possible parms                       Stack bytes used\n",_FL));
		if (eip) {
			kp(("0x%08lx %-34s (instruction pointer when interrupted)\n", eip, SymFile_Lookup((byte *)eip)));
		}
		dword *last_stack_ptr = stack_ptr;
		while (stack_ptr < stack_top) {
			if (*stack_ptr == TOP_OF_STACK_SIGNATURE) {
				//kp(("%s(%d) Found TOP_OF_STACK_SIGNATURE at $%08lx\n", _FL, stack_ptr));
				break;
			}
			if ((void *)*stack_ptr > code_start &&
				(void *)*stack_ptr < code_end) {
				// This dword points into the code area... check if
				// it points just past a function call.  If so,
				// it's likely a return address.
			  #if 0	//:::				kp(("%s(%d)  possible return address: $%08lx\n", _FL, *stack_ptr));
				khexdump((void *)(*stack_ptr-8), 8, 8, 1);
			  #endif

				// For the 386, the most common call opcodes:
				//  0xe8 [32-bit offset]
				//  (others not done yet)
				byte *base_ptr = (byte *)(*stack_ptr-5);
				byte opcode = *base_ptr;
				int  operand32 = *(int *)(base_ptr+1);
				byte *call_source = NULL;
				byte *call_dest = NULL;
				if (opcode==0xe8) {	// call [32-bit offset]
					call_source = base_ptr;
					call_dest = base_ptr+operand32+5;
//					kp(("%s(%d) opcode = $%02x, operand32 = $%08lx, call_dest = $%08lx\n",_FL, opcode, operand32, call_dest));
				} else {
					// Not a known opcode.
//					kp(("%s(%d) opcode = $%02x, operand32 = $%08lx\n",_FL, opcode, operand32));
				}

				// If we found something, print the info on it.
				//kp(("%s(%d) call_source = %08lx call_dest = %08lx code_start = %08lx code_end = %08lx\n", _FL, call_source, call_dest, code_start, code_end));
				if (call_source && call_dest >= code_start && call_dest < code_end) {
					char call_source_str[80], call_dest_str[80];
					strnncpy(call_source_str, SymFile_Lookup(call_source), sizeof(call_source_str));
					strnncpy(call_dest_str, SymFile_Lookup(call_dest), sizeof(call_dest_str));
					kp(("0x%08lx %-34s %-34s %08lx %08lx %08lx %08lx  %-5u   $%08lx\n",
							call_source, call_source_str, call_dest_str,
							stack_ptr[1],stack_ptr[2],stack_ptr[3],stack_ptr[4],
							(dword)stack_ptr - (dword)last_stack_ptr, stack_ptr));
					last_stack_ptr = stack_ptr;
				}
			}
			stack_ptr++;
		}
}
#endif	//INCL_STACK_CRAWL
