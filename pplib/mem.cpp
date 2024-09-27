//*********************************************************
//
//	Memory management related routines.
//
// 
//
//*********************************************************

#define DISP 0

#ifdef WIN32
  #define MEM_DEBUG	1
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#else
  #define MEM_DEBUG	1
#endif
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include "pplib.h"

#if MEM_DEBUG
//****************************************************************
// 
//
// Display our global heap's memory map to DebWin.
// Calls GetProcessHeap() to determine the heap handle.
// In theory this should display all blocks allocated using
// either malloc() or 'new'.
//
static int InDumpMemMapFlag;
void MemDisplayMemoryMap(void)
{
	unsigned long total_allocated = 0;

	if (!InDumpMemMapFlag) {
	    InDumpMemMapFlag++;

	  #if WIN32
		#define MAX_HEAPS	20
		HANDLE heaps[MAX_HEAPS];
		int heap_count = GetProcessHeaps(MAX_HEAPS, heaps);
		kp(("%s(%d) There are %d heaps for this process.\n",_FL,heap_count));

		for (int i=0 ; i<heap_count ; i++) {
			if (HeapLock(heaps[i])) {
				PROCESS_HEAP_ENTRY phe;
				zstruct(phe);
		        kp(("------------ Heap #%d ($%08lx) Memory Map ------------\n", i, heaps[i]));
				byte *old_end_ptr = NULL;
				forever {
					if (HeapWalk(heaps[i], &phe)) {
						// First, check if we skipped over an unallocated block.
						// Display block info according to data in phe.
						if (old_end_ptr && phe.lpData > old_end_ptr + 8) {
							kp(("$%8lX-%8lX %7ld bytes not accounted for.\n",
									old_end_ptr, phe.lpData,
									(byte *)phe.lpData-old_end_ptr));
						}
						old_end_ptr = (byte *)phe.lpData + phe.cbData + phe.cbOverhead;

						char flags_string[200];
						flags_string[0] = 0;
						if (phe.wFlags & PROCESS_HEAP_ENTRY_BUSY) {
							strcat(flags_string, " Allocated");
							total_allocated += phe.cbData + phe.cbOverhead;
						}
						if (phe.wFlags & PROCESS_HEAP_REGION) {
							strcat(flags_string, " (Region)");
						}
						if (phe.wFlags & PROCESS_HEAP_UNCOMMITTED_RANGE) {
							strcat(flags_string, " Uncommitted");
						}
						if (phe.wFlags & PROCESS_HEAP_ENTRY_MOVEABLE) {
							strcat(flags_string, " Movable");
						}
						if (phe.wFlags & PROCESS_HEAP_ENTRY_DDESHARE) {
							strcat(flags_string, " DDEShare");
						}
						if (!phe.wFlags) {
							strcat(flags_string, " Free");
						}
						kp(("$%8lX-%8lX %7ld bytes (%5dK),%s\n",
								phe.lpData, (long)phe.lpData+phe.cbData,
								phe.cbData, (phe.cbData+512)>>10, flags_string));
					} else {
						// HeapWalk failed...
						int err = GetLastError();
						if (err!=ERROR_NO_MORE_ITEMS) {
							Error(ERR_ERROR, "%s(%d) HeapWalk failed with error code %d. Trashed heap or not NT?", _FL, err);
						}
						break;	// leave our forever loop.
					}
				}

				HeapUnlock(heaps[i]);
			} else {
				Error(ERR_ERROR, "%s(%d) HeapLock() failed in MemDisplayMemoryMap()", _FL);
			}
		}
		MEMORYSTATUS ms;
		zstruct(ms);
		ms.dwLength = sizeof(ms);
		GlobalMemoryStatus(&ms);	// fetch info about memory
	    kp(("Total Allocated: %luK (%luM) Free Physical RAM: %luK (%luM) Virtual mem: %luM  Memory Load = %d%%\n",
					total_allocated   >> 10, total_allocated >> 20,
					ms.dwAvailPhys    >> 10, ms.dwAvailPhys  >> 20,
					ms.dwAvailVirtual >> 20,
					ms.dwMemoryLoad));
	  #else	// !WIN32
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
		kp(("Total Allocated from system: %luK Currently used: %luK  NotUsed: %luK\n",
					mi.arena>>10, mi.uordblks>>10,mi.fordblks>>10));
	  #endif	// !WIN32
	}
    InDumpMemMapFlag--;
}

#if WIN32
//****************************************************************
// 
//
// Track the amount of VM allocated/freed between calls to this
// function.  Used during development to determine where memory
// is getting allocated/freed.  Calls MemGetVMUsedByProcess() which
// is kind of slow, so use it sparingly.
//
void MemTrackVMUsage(int print_if_no_change_flag, char *output_fmt_string, ...)
{
	static DWORD old_vm_used;
	DWORD vm_used = MemGetVMUsedByProcess();
	long diff = (long)(vm_used - old_vm_used);
	old_vm_used = vm_used;

	// If we need to print it, first print old line if we must.
	static char old_string[250];
	if (diff && old_string[0]) {
		kwrites(old_string);
		old_string[0] = 0;
	}

	// Form the new string.
	va_list arg_ptr;
	char str[250];
    va_start(arg_ptr, output_fmt_string);
    vsprintf(str, output_fmt_string, arg_ptr);
    va_end(arg_ptr);
	sprintf(old_string, "Virtual Memory used: %6ldK (%+4dK, %+7d).  %s\n",
    		vm_used >> 10, diff >> 10, diff, str);
	if (diff || print_if_no_change_flag) {	// print it now.
		kwrites(old_string);
		old_string[0] = 0;
	}
}

//****************************************************************
// 
//
// Determine the total amount of virtual memory used by our process.
// This routine is kind of slow (on the order of 10's of
// milliseconds) because it loops through the entire address space
// to determine which pages are committed to us and which are not.
// Use it sparingly.
//
WORD32 MemGetVMUsedByProcess(void)
{
	MEMORY_BASIC_INFORMATION mbi;
	DWORD dwMemUsed = 0;
	PVOID pvAddress = 0;

	zstruct(mbi);
	while(VirtualQuery(pvAddress, &mbi, sizeof(MEMORY_BASIC_INFORMATION)) == sizeof(MEMORY_BASIC_INFORMATION))  {
		if(mbi.State == MEM_COMMIT && mbi.Type == MEM_PRIVATE)
	    	dwMemUsed += mbi.RegionSize;
	    pvAddress = ((BYTE*)mbi.BaseAddress) + mbi.RegionSize;
    }
	//kp(("%s(%d) Memory used according to method #2 = %lu bytes (%luK, %luM)\n", _FL, dwMemUsed, dwMemUsed>>10,dwMemUsed>>20));
	return dwMemUsed;
}
#endif	//WIN32

//*********************************************************
// https://github.com/kriskoin//
// malloc() and free() wrappers.
//
#undef malloc
#undef free

#define BLOCK_PRE_PADDING	32	// # of bytes of sig before user data (must be dword multiple and must be at least sizeof(int))
#define BLOCK_POST_PADDING	32	// # of bytes of sig before after data (must be dword multiple)

#define BLOCK_PADDING_SIG	0xbad3943e	// random signature used to fill padding area.

void *MemAlloc(size_t size, char *calling_file, int calling_line)
{
	int total_size = BLOCK_PRE_PADDING + size + BLOCK_POST_PADDING;
	char *baseptr = (char *)malloc(total_size);
	if (!baseptr) {
		kp(("%s(%d) malloc(%d) failed.\n", calling_file, calling_line, size));
		return NULL;	// error.
	}
	void *ptr = (void *)(baseptr + BLOCK_PRE_PADDING);

	// fill the 3 regions of this block
	WORD32 *p = (WORD32 *)baseptr;
	int i;
	for (i=0 ; i<BLOCK_PRE_PADDING/sizeof(*p) ; i++) {
		*p++ = BLOCK_PADDING_SIG;
	}
	*(int *)baseptr = total_size;	// save total size.
	memset(ptr, 0, size);
	p = (WORD32 *)(baseptr + BLOCK_PRE_PADDING + size);
	for (i=0 ; i<BLOCK_POST_PADDING/sizeof(*p) ; i++) {
		*p++ = BLOCK_PADDING_SIG;
	}

	//kp(("%s(%d) malloc(%d) returned $%08lx\n", calling_file, calling_line, size, ptr));
  #if 0	// 2022 kriskoin
	if (total_size < 500) {
		khexd(baseptr, total_size);
	}
  #endif
	NOTUSED(calling_file);
	NOTUSED(calling_line);
	return ptr;
}

void MemCheck(void *ptr, char *calling_file, int calling_line)
{
	char *base_ptr = (char *)ptr - BLOCK_PRE_PADDING;
	if (ptr) {
		int len = *(int *)base_ptr;
		//kp(("%s(%d) calling free($%08lx) (%d bytes)\n", calling_file, calling_line, ptr, len));
		// verify the pre and post padding areas...
		WORD32 *p = (WORD32 *)base_ptr + 1;	// skip saved length at start
		int i;
		for (i=1 ; i<BLOCK_PRE_PADDING/sizeof(*p) ; i++, p++) {
			if (*p != BLOCK_PADDING_SIG) {
				kp(("%s(%d) ERROR: memory block pre-padding got tromped (block = $%08lx)\n",
						calling_file, calling_line, ptr));
				kp(("%s(%d) Here is the beginning of the entire memory block:\n", calling_file, calling_line));
				khexd(base_ptr, BLOCK_PRE_PADDING + 32);
				break;
			}
		}
		p = (WORD32 *)(base_ptr + len - BLOCK_POST_PADDING);
		for (i=0 ; i<BLOCK_POST_PADDING/sizeof(*p) ; i++, p++) {
			if (*p != BLOCK_PADDING_SIG) {
				kp(("%s(%d) ERROR: memory block post-padding got tromped (block = $%08lx)\n",
						calling_file, calling_line, ptr));
				kp(("%s(%d) Here is the end of the entire memory block:\n", calling_file, calling_line));
				khexd(base_ptr + len - BLOCK_POST_PADDING - 32, BLOCK_POST_PADDING+32);
				break;
			}
		}
	}
	NOTUSED(calling_file);
	NOTUSED(calling_line);
}

void MemFree(void *ptr, char *calling_file, int calling_line)
{
	char *base_ptr = (char *)ptr - BLOCK_PRE_PADDING;
	if (ptr) {
		MemCheck(ptr, calling_file, calling_line);
		int len = *(int *)base_ptr;
		//kp(("%s(%d) calling free($%08lx) (%d bytes)\n", calling_file, calling_line, ptr, len));
		if (len > 0) {
			// zero out the entire memory block before actually freeing
			memset(base_ptr, 0, len);
		}
		free(base_ptr);
	}
}
#endif	// MEM_DEBUG
