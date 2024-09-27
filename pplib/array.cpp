//*********************************************************
//
// Array Handling class
//
// The array handling class is designed to provide a simple
// but powerful interface to a resizable array which can
// be sorted and searched easily.
//
// The class is designed for fast access with infrequent
// additions to the array.
//
// The array is kept entirely in memory, although there are
// functions available to read/write to/from disk.
//
// This class is NOT thread-safe... that's up to a higher level.
// Two seperate objects of this class can be accessed from
// different threads, but two threads cannot access the same
// object - use locking at a higher level if that's needed.
//
// Feb 22, 2000 - MB - initial version
//
//*********************************************************

#define DISP 0

#include "pplib.h"

//*********************************************************
// https://github.com/kriskoin//
// Array class constructor/destructors
//
Array::Array(void)
{
	base = NULL;
	member_count = 0;
	sorted_flag = 0;
	sort_enabled = TRUE;
	zstruct(zero_key);
	SetParms(0, 0, 10);
}

Array::~Array(void)
{
	if (base) {			// if allocated...
		free(base);		// free it.
		base = NULL;
		member_count = 0;
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Set the various parameters used by the array class to work.
// new_member_size is the size of each member in the array (sizeof(member))
// new_key_len is the length (in bytes) of the key (usually sizeof(WORD32))
// new_grow_amount is the # of entries to add to the array when it needs reallocating (50 sounds good)
//
ErrorType Array::SetParms(int new_member_size, int new_key_len, int new_grow_amount)
{
	member_size = new_member_size;
	key_len = new_key_len;
	grow_amount = new_grow_amount;
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Add a an entry to the array.  Replaces existing entry if
// possible, else adds the entry to the array.
// Does not write to disk.  Call WriteFileIfNecessary() if desired.
// *** Keys CANNOT be zero! ***
// Returns an error if it cannot be added.
//
void *Array::Add(void *entry_to_add)
{
	void *e = Find(entry_to_add);	// is it there already?
	if (!e) {
		// It's not there... allocate a new one.
		e = Find(zero_key);	// look for a zero (unused) key
		if (!e) {
			// Still nowhere... grow the array
			int new_member_count = member_count + grow_amount;
			size_t new_size = new_member_count * member_size;
			void *new_base = malloc(new_size);
			if (!new_base) {
				Error(ERR_ERROR, "%s(%d) Error: could not malloc(%d) to grow array!", _FL, new_size);
				return NULL;	// could not grow it!
			}
			memset(new_base, 0, new_size);
			if (base) {
				// Copy the old array to it
				memcpy(new_base, base, member_count*member_size);
				free(base);	// free the old one.
			}
			// Set up to use the newly allocated memory instead of the old memory
			base = new_base;
			member_count = new_member_count;
			sorted_flag = FALSE;	// we've added empty members... no longer sorted.
			e = Find(zero_key);	// look for a zero (unused) key
			if (!e) {
				Error(ERR_ERROR, "%s(%d) Could not find empty member after growing", _FL);
				return NULL;	// could not grow it!
			}
		}
	}

	// We've got somewhere to put it... just store the new version.
	if (memcmp(e, entry_to_add, key_len)) {	// does the key change?
		sorted_flag = FALSE;	// yes, new key.  assume sorting needed.
	}
	memcpy(e, entry_to_add, member_size);

	// Keep it sorted...
	if (!sorted_flag) {
		Sort();
	}
	modified = TRUE;
	return Find(entry_to_add);
}

//*********************************************************
// https://github.com/kriskoin//
// Remove an entry from the array.
// Does not write to disk.  Call WriteFileIfNecessary() if desired.
// Returns an error if not found.
//
ErrorType Array::Remove(void *search_key)
{
	void *e = Find(search_key);	// is it there already?
	if (e) {
		memset(e, 0, member_size);
		modified = TRUE;
		sorted_flag = FALSE;
		Sort();	// keep it sorted
	}
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Search the array for the specified search key.
// Uses a binary search if the array is sorted (see Sort()).
// *** Keys CANNOT be zero! ***
// Returns NULL if an exact match was not found.
//
void * Array::Find(void *search_key)
{
	if (!base || !member_count || !key_len || !search_key) {
		return NULL;	// nothing to search
	}

	if (sorted_flag) {
		kp1(("%s(%d) Note: Array::Find() still does linear search even though data is sorted.\n", _FL));
	}
	if (key_len==sizeof(WORD32)) {
		// Specially optimized version for most common key size
		// For now, we only support a linear search
		WORD32 *e = (WORD32 *)base;
		WORD32 key = *(WORD32 *)search_key;
		for (int i=0 ; i<member_count ; i++) {
			if (*e==key) {
				return e;	// found it.
			}
			e = (WORD32 *)((char *)e + member_size);
		}
		return NULL;	// not found.
	} else {
		// Arbitrary key length search...
		// For now, we only support a linear search
		void *e = base;
		for (int i=0 ; i<member_count ; i++) {
			if (!memcmp(e, search_key, key_len)) {
				return e;	// found it.
			}
			e = (void *)((char *)e + member_size);
		}
		return NULL;	// not found.
	}
}

//*********************************************************
// https://github.com/kriskoin//
// QSort the array for faster searching using Find().
//
struct ArraySortParms {
	int member_size;
	void *base;
	int key_len;
};

static int ArrayKeyCompareFunc(int n1, int n2, void *base)
{
	struct ArraySortParms *p = (struct ArraySortParms *)base;
	return memcmp((char *)p->base + n1*p->member_size,
				  (char *)p->base + n2*p->member_size,
				  p->key_len);
}

static int ArrayKeyCompareFuncWord32(int n1, int n2, void *base)
{
	struct ArraySortParms *p = (struct ArraySortParms *)base;
	WORD32 k1 = *(WORD32 *)((char *)p->base + n1*p->member_size);
	WORD32 k2 = *(WORD32 *)((char *)p->base + n2*p->member_size);
	if (k1 < k2) {
		return -1;
	} else if (k1 > k2) {
		return 1;
	}
	return 0;
}

static void ArrayMemberSwapFunc(int n1, int n2, void *base)
{
	struct ArraySortParms *p = (struct ArraySortParms *)base;
	#define MAX_ARRAY_SWAP_SIZE		400
	char temp[MAX_ARRAY_SWAP_SIZE];
	memcpy(temp, (char *)p->base + n1*p->member_size, p->member_size);
	memcpy((char *)p->base + n1*p->member_size,
		   (char *)p->base + n2*p->member_size, p->member_size);
	memcpy((char *)p->base + n2*p->member_size, temp, p->member_size);
}

void Array::Sort(void)
{
	if (!sort_enabled) {
		return;
	}
	if (member_size > MAX_ARRAY_SWAP_SIZE) {
		kp1(("%s(%d) **** Error: Cannot sort an array with members this big (%d bytes)!\n", _FL, member_size));
		return;
	}
	struct ArraySortParms sortparms;
	sortparms.member_size = member_size;
	sortparms.base = base;
	sortparms.key_len = key_len;
	if (key_len==sizeof(WORD32)) {
		QSort(member_count, ArrayKeyCompareFuncWord32, ArrayMemberSwapFunc, &sortparms);
	} else {
		QSort(member_count, ArrayKeyCompareFunc, ArrayMemberSwapFunc, &sortparms);
	}
	modified = TRUE;
	sorted_flag = TRUE;
}

//*********************************************************
// https://github.com/kriskoin//
// Read a disk file into the array (discards current array first, if any)
//
ErrorType Array::LoadFile(char *fname)
{
	ErrorType result = ERR_NONE;
	// Free old array (if it's around)
	if (base) {			// if allocated...
		free(base);		// free it.
		base = NULL;
		member_count = 0;
	}

	long bytes_read = 0;
	base = ::LoadFile(fname, &bytes_read);
	if (base && bytes_read) {
		member_count = bytes_read / member_size;
	} else {
		// database could not be found... start a new one.
		kp(("%s(%d) Warning: %s could not be loaded.  Starting a new one.\n",_FL,fname));
		result = ERR_WARNING;
		member_count = grow_amount;
		int alloc_len = member_count*member_size;
		base = malloc(alloc_len);
		if (base) {
			memset(base, 0, alloc_len);
		} else {	// malloc failed! That's bad!
			member_count = 0;
			result = ERR_ERROR;
		}
	}
	modified = FALSE;
	Sort();
	return result;
}

//*********************************************************
// https://github.com/kriskoin//
// Write the array to disk
//
ErrorType Array::WriteFile(char *fname)
{
	ErrorType result = ERR_NONE;
	if (base) {
		result = ::WriteFile(fname, base, member_count*member_size);
		if (result==ERR_NONE) {
			modified = FALSE;
		}
	}
	return result;
}

ErrorType Array::WriteFileIfNecessary(char *fname)
{
	if (modified) {
		return WriteFile(fname);
	}
	return ERR_NONE;
}	

