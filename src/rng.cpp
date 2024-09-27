//*********************************************************

//
//	Random Number Generator routines.
//	Server side only.
//
// 
//
//*********************************************************

#define DISP 0

#if WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#else
	//#include <asm/system.h>		// needed for rdtsc()
	static __inline__ unsigned long long rdtsc(void)
	{
		unsigned long long ret;
	        __asm__ __volatile__("rdtsc"
				    : "=A" (ret)
				    : /* no inputs */);
	        return ret;
	}
#endif
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include "pokersrv.h"

#define STATE_ARRAY_SIZE	256
static long int RNG_StateArray[STATE_ARRAY_SIZE/sizeof(long)+1];
static long int random32(void);

static PPCRITICAL_SECTION RNG_CritSec;

#if 1
/*
 * Copyright (c) 1983 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of California, Berkeley.  The name of the
 * University may not be used to endorse or promote products derived
 * from this software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

/*
 * This is derived from the Berkeley source:
 *	@(#)random.c	5.5 (Berkeley) 7/6/88
 * It was reworked for the GNU C Library by Roland McGrath.
 */

#ifndef ULONG_MAX
  #define	ULONG_MAX  ((unsigned long)(~0L))     /* 0xFFFFFFFF for 32-bits */
  #define	LONG_MAX   ((long)(ULONG_MAX >> 1))   /* 0x7FFFFFFF for 32-bits*/
#endif
#ifndef NULL
  #ifdef __STDC__
  #  define NULL (void *) 0
  #else
  #  define NULL 0
  #endif
#endif
#ifndef PTR
  #ifdef __STDC__
  #  define PTR void *
  #else
  #  define PTR char *
  #endif
#endif

/* An improved random number generation package.  In addition to the standard
   rand()/srand() like interface, this package also has a special state info
   interface.  The initstate() routine is called with a seed, an array of
   bytes, and a count of how many bytes are being passed in; this array is
   then initialized to contain information for random number generation with
   that much state information.  Good sizes for the amount of state
   information are 32, 64, 128, and 256 bytes.  The state can be switched by
   calling the setstate() function with the same array as was initiallized
   with initstate().  By default, the package runs with 128 bytes of state
   information and generates far better random numbers than a linear
   congruential generator.  If the amount of state information is less than
   32 bytes, a simple linear congruential R.N.G. is used.  Internally, the
   state information is treated as an array of longs; the zeroeth element of
   the array is the type of R.N.G. being used (small integer); the remainder
   of the array is the state information for the R.N.G.  Thus, 32 bytes of
   state information will give 7 longs worth of state information, which will
   allow a degree seven polynomial.  (Note: The zeroeth word of state
   information also has some other information stored in it; see setstate
   for details).  The random number generation technique is a linear feedback
   shift register approach, employing trinomials (since there are fewer terms
   to sum up that way).  In this approach, the least significant bit of all
   the numbers in the state table will act as a linear feedback shift register,
   and will have period 2^deg - 1 (where deg is the degree of the polynomial
   being used, assuming that the polynomial is irreducible and primitive).
   The higher order bits will have longer periods, since their values are
   also influenced by pseudo-random carries out of the lower bits.  The
   total period of the generator is approximately deg*(2**deg - 1); thus
   doubling the amount of state information has a vast influence on the
   period of the generator.  Note: The deg*(2**deg - 1) is an approximation
   only good for large deg, when the period of the shift register is the
   dominant factor.  With deg equal to seven, the period is actually much
   longer than the 7*(2**7 - 1) predicted by this formula.  */



/* For each of the currently supported random number generators, we have a
   break value on the amount of state information (you need at least thi
   bytes of state info to support this random number generator), a degree for
   the polynomial (actually a trinomial) that the R.N.G. is based on, and
   separation between the two lower order coefficients of the trinomial.  */

/* Linear congruential.  */
#define	TYPE_0		0
#define	BREAK_0		8
#define	DEG_0		0
#define	SEP_0		0

/* x**7 + x**3 + 1.  */
#define	TYPE_1		1
#define	BREAK_1		32
#define	DEG_1		7
#define	SEP_1		3

/* x**15 + x + 1.  */
#define	TYPE_2		2
#define	BREAK_2		64
#define	DEG_2		15
#define	SEP_2		1

/* x**31 + x**3 + 1.  */
#define	TYPE_3		3
#define	BREAK_3		128
#define	DEG_3		31
#define	SEP_3		3

/* x**63 + x + 1.  */
#define	TYPE_4		4
#define	BREAK_4		256
#define	DEG_4		63
#define	SEP_4		1


/* Array versions of the above information to make code run faster.
   Relies on fact that TYPE_i == i.  */

#define	MAX_TYPES	5	/* Max number of types above.  */

static int degrees[MAX_TYPES] = { DEG_0, DEG_1, DEG_2, DEG_3, DEG_4 };
static int seps[MAX_TYPES] = { SEP_0, SEP_1, SEP_2, SEP_3, SEP_4 };



/* Initially, everything is set up as if from:
	initstate(1, randtbl, 128);
   Note that this initialization takes advantage of the fact that srandom
   advances the front and rear pointers 10*rand_deg times, and hence the
   rear pointer which starts at 0 will also end up at zero; thus the zeroeth
   element of the state information, which contains info about the current
   position of the rear pointer is just
	(MAX_TYPES * (rptr - state)) + TYPE_3 == TYPE_3.  */

static long int randtbl[DEG_3 + 1] =
  { TYPE_3,
      0x9a319039, 0x32d9c024, 0x9b663182, 0x5da1f342, 
      0xde3b81e0, 0xdf0a6fb5, 0xf103bc02, 0x48f340fb, 
      0x7449e56b, 0xbeb1dbb0, 0xab5c5918, 0x946554fd, 
      0x8c2e680f, 0xeb3d799f, 0xb11ee0b7, 0x2d436b86, 
      0xda672e2a, 0x1588ca88, 0xe369735d, 0x904f35f7, 
      0xd7158fd6, 0x6fa6f051, 0x616e6b96, 0xac94efdc, 
      0x36413f93, 0xc622c298, 0xf5a42ab8, 0x8a88d77b, 
      0xf5ad9d0e, 0x8999220b, 0x27fb47b9
    };

/* FPTR and RPTR are two pointers into the state info, a front and a rear
   pointer.  These two pointers are always rand_sep places aparts, as they
   cycle through the state information.  (Yes, this does mean we could get
   away with just one pointer, but the code for random is more efficient
   this way).  The pointers are left positioned as they would be from the call:
	initstate(1, randtbl, 128);
   (The position of the rear pointer, rptr, is really 0 (as explained above
   in the initialization of randtbl) because the state table pointer is set
   to point to randtbl[1] (as explained below).)  */

static long int *fptr = &randtbl[SEP_3 + 1];
static long int *rptr = &randtbl[1];



/* The following things are the pointer to the state information table,
   the type of the current generator, the degree of the current polynomial
   being used, and the separation between the two pointers.
   Note that for efficiency of random, we remember the first location of
   the state information, not the zeroeth.  Hence it is valid to access
   state[-1], which is used to store the type of the R.N.G.
   Also, we remember the last location, since this is more efficient than
   indexing every time to find the address of the last element to see if
   the front and rear pointers have wrapped.  */

static long int *state = &randtbl[1];
static long int *end_ptr = &randtbl[sizeof(randtbl) / sizeof(randtbl[0])];

static int rand_type = TYPE_3;
static int rand_deg = DEG_3;
static int rand_sep = SEP_3;


/* Initialize the random number generator based on the given seed.  If the
   type is the trivial no-state-information type, just remember the seed.
   Otherwise, initializes state[] based on the given "seed" via a linear
   congruential generator.  Then, the pointers are set to known locations
   that are exactly rand_sep places apart.  Lastly, it cycles the state
   information a given number of times to get rid of any initial dependencies
   introduced by the L.C.R.N.G.  Note that the initialization of randtbl[]
   for default usage relies on values produced by this routine.  */
void srandom (unsigned int x)
{
  state[0] = x;
  if (rand_type != TYPE_0)
    {
      register long int i;
      for (i = 1; i < rand_deg; ++i)
		state[i] = (1103515145 * state[i - 1]) + 12345;
      fptr = &state[rand_sep];
      rptr = &state[0];
      for (i = 0; i < 10 * rand_deg; ++i)
		random32();
    }
}

/* Initialize the state information in the given array of N bytes for
   future random number generation.  Based on the number of bytes we
   are given, and the break values for the different R.N.G.'s, we choose
   the best (largest) one we can and set things up for it.  srandom is
   then called to initialize the state information.  Note that on return
   from srandom, we set state[-1] to be the type multiplexed with the current
   value of the rear pointer; this is so successive calls to initstate won't
   lose this information and will be able to restart with setstate.
   Note: The first thing we do is save the current state, if any, just like
   setstate so that it doesn't matter when initstate is called.
   Returns a pointer to the old state.  */
static void *initstate(unsigned int seed, PTR *arg_state, unsigned long n)
{
  PTR ostate = (PTR) &state[-1];

  if (rand_type == TYPE_0)
    state[-1] = rand_type;
  else
    state[-1] = (MAX_TYPES * (rptr - state)) + rand_type;
  if (n < BREAK_1)
    {
      if (n < BREAK_0)
	{
	  errno = EINVAL;
	  return NULL;
	}
      rand_type = TYPE_0;
      rand_deg = DEG_0;
      rand_sep = SEP_0;
    }
  else if (n < BREAK_2)
    {
      rand_type = TYPE_1;
      rand_deg = DEG_1;
      rand_sep = SEP_1;
    }
  else if (n < BREAK_3)
    {
      rand_type = TYPE_2;
      rand_deg = DEG_2;
      rand_sep = SEP_2;
    }
  else if (n < BREAK_4)
    {
      rand_type = TYPE_3;
      rand_deg = DEG_3;
      rand_sep = SEP_3;
    }
  else
    {
      rand_type = TYPE_4;
      rand_deg = DEG_4;
      rand_sep = SEP_4;
    }

  state = &((long int *) arg_state)[1];	/* First location.  */
  /* Must set END_PTR before srandom.  */
  end_ptr = &state[rand_deg];
  srandom(seed);
  if (rand_type == TYPE_0)
    state[-1] = rand_type;
  else
    state[-1] = (MAX_TYPES * (rptr - state)) + rand_type;

  return ostate;
}

/* Restore the state from the given state array.
   Note: It is important that we also remember the locations of the pointers
   in the current state information, and restore the locations of the pointers
   from the old state information.  This is done by multiplexing the pointer
   location into the zeroeth word of the state information. Note that due
   to the order in which things are done, it is OK to call setstate with the
   same state as the current state
   Returns a pointer to the old state information.  */

PTR setstate (PTR arg_state)
{
  register long int *new_state = (long int *) arg_state;
  register int type = new_state[0] % MAX_TYPES;
  register int rear = new_state[0] / MAX_TYPES;
  PTR ostate = (PTR) &state[-1];

  if (rand_type == TYPE_0)
    state[-1] = rand_type;
  else
    state[-1] = (MAX_TYPES * (rptr - state)) + rand_type;

  switch (type)
    {
    case TYPE_0:
    case TYPE_1:
    case TYPE_2:
    case TYPE_3:
    case TYPE_4:
      rand_type = type;
      rand_deg = degrees[type];
      rand_sep = seps[type];
      break;
    default:
      /* State info munged.  */
      errno = EINVAL;
      return NULL;
    }

  state = &new_state[1];
  if (rand_type != TYPE_0)
    {
      rptr = &state[rear];
      fptr = &state[(rear + rand_sep) % rand_deg];
    }
  /* Set end_ptr too.  */
  end_ptr = &state[rand_deg];

  return ostate;
}

/* If we are using the trivial TYPE_0 R.N.G., just do the old linear
   congruential bit.  Otherwise, we do our fancy trinomial stuff, which is the
   same in all ther other cases due to all the global variables that have been
   set up.  The basic operation is to add the number at the rear pointer into
   the one at the front pointer.  Then both pointers are advanced to the next
   location cyclically in the table.  The value returned is the sum generated,
   reduced to 31 bits by throwing away the "least random" low bit.
   Note: The code takes advantage of the fact that both the front and
   rear pointers can't wrap on the same call by not testing the rear
   pointer if the front one has wrapped.  Returns a 31-bit random number.  */

static long int random32(void)
{
	if (rand_type == TYPE_0) {
		kp1(("%s(%d) Warning: rand_type==TYPE_0!\n",_FL));
		state[0] = ((state[0] * 1103515245) + 12345) & LONG_MAX;
		return state[0];
	} else {
		long int i;
		*fptr += *rptr;
		/* Chucking least random bit.  */
		i = (*fptr >> 1) & LONG_MAX;
		//kp(("%s(%d) fptr=$%08lx  rptr=$%08lx, i=$%08lx\n",_FL, fptr, rptr, i));
		if (++fptr >= end_ptr) {
			fptr = state;
		}
		if (++rptr >= end_ptr) {
		    rptr = state;
		}
		return i;
    }
}
#endif

#if 0	// 2022 kriskoin
//*********************************************************
// https://github.com/kriskoin//
// Print the RNG_StateArray
//
static void PrintStateArray(void)
{
	long *p = RNG_StateArray;
	while (p < RNG_StateArray + (STATE_ARRAY_SIZE/sizeof(long))) {
		kp(("             %3d: %08lx %08lx %08lx %08lx\n",
					p - RNG_StateArray, *p, *(p+1), *(p+2), *(p+3)));
		p += 4;
	}
}
#endif

//*********************************************************
// https://github.com/kriskoin//
// Do our best to initialize the seed for our random number
// generator.  This function should only be called ONCE.
//
void RNG_InitializeSeed(void)
{
	static int initialized = FALSE;
	if (!initialized) {
		PPInitializeCriticalSection(&RNG_CritSec, CRITSECPRI_RNG, "RNG");
		unsigned int initial_seed = time(NULL) ^ (WORD32)rdtsc();
		initstate(initial_seed, (PTR *)RNG_StateArray, STATE_ARRAY_SIZE);
	  #if 0	// 2022 kriskoin
		kp(("%s(%d) initial rng state array:\n", _FL));
		PrintStateArray();
	  #endif
		RNG_AddToEntropy((WORD32)time(NULL));
		RNG_AddToEntropyUsingRDTSC();
	} else {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Error: RNG_InitializeSeed() called more than once!", _FL);
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Add to the entropy pool for our seed.  Pass any somewhat
// unpredictable 32-bit value.  The more the merrier.
// A hardware RNG can inject its data here.
// Other good source include mouse movements and cpu
// time stamp counters sampled at unpredictable times.
//
void RNG_AddToEntropy(WORD32 any_number)
{
	EnterCriticalSection(&RNG_CritSec);

  #if 0	// 2022 kriskoin
	kp(("%s(%d) rng state array before AddToEntropy($%08lx):\n", _FL, any_number));
	PrintStateArray();
  #endif

	static long *entropy_ptr;
	if (entropy_ptr < state) {
		entropy_ptr = state;
	}
	if (++entropy_ptr >= end_ptr) {
		entropy_ptr = state;
	}
  #if 0	// 2022 kriskoin
	kp1(("%s(%d) entropy_ptr = $%08lx, state = $%08lx, end_ptr = $%08lx\n",
				_FL, entropy_ptr, state, end_ptr));
  #endif
  #if 0	// 2022 kriskoin
	kp(("%s(%d) RNG_AddToEntropy: $%08lx  = $%08lx + $%08lx\n", _FL, entropy_ptr, *entropy_ptr, any_number));
  #endif
  #if 0	// 2022 kriskoin
	static WORD32 old_number;
	if (any_number - old_number > 9000000) {
		kp(("%s(%d) RNG_AddToEntropy: $%08lx  = $%08lx + $%08lx\n", _FL, entropy_ptr, *entropy_ptr, any_number));
	}
	old_number = any_number;
  #endif
	*entropy_ptr += any_number;

  #if 0	// 2022 kriskoin
	kp(("%s(%d) rng state array after  AddToEntropy($%08lx):\n", _FL, any_number));
	PrintStateArray();
  #endif

	LeaveCriticalSection(&RNG_CritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Use the Pentium's RDTSC instruction to read the CPU's
// time stamp counter (which counts clock cycles) and add it
// to the entropy pool.  On a 366MHz machine, this thing
// wraps around every 11 seconds, so there's a fair amount
// of unpredictability to it.  It's not perfect, but it's
// fairly good.
//
void RNG_AddToEntropyUsingRDTSC(void)
{
  #if 0	// used for testing rdtsc() results.
	WORD32 r1 = (WORD32)rdtsc();
	WORD32 r2 = (WORD32)rdtsc();
	kp(("%s(%d) two subsequent rdtsc() calls = $%08lx/$%08lx (diff = %4d) High 32-bits = $%08lx\n",_FL,
			r1, r2, r2-r1, (WORD32)(rdtsc()>>32)));
  #endif
	RNG_AddToEntropy((WORD32)rdtsc());
}

//*********************************************************
// https://github.com/kriskoin//
// Return the next random number from our sequence.  Note that
// if you know the seed, this number will be predictable.  That's
// why the entropy should be added to as often as possible, possibly
// even in between subsequent calls to RNG_NextNumber().
//
WORD32 RNG_NextNumber(void)
{
	WORD32 result;
	EnterCriticalSection(&RNG_CritSec);
	result = (WORD32)random32();
	RNG_AddToEntropyUsingRDTSC();

  #if 0
	// Do some testing of the result... make sure all bits are distributed
	// roughly evenly.
	static int bitcounts[32];
	long total = 0;
	int i;
	for (i=0 ; i<31 ; i++) {
		if (result & (1<<i))
			bitcounts[i]++;
		total += bitcounts[i];
	}
	static int delay = 0;
	if (++delay >= 2000) {
		delay = 0;
		kp(("%s(%d) RNG_NextNumber() bit distribution:\n",_FL));
		for (i=0 ; i<31 ; i++) {
			kp(("      #%2d: %6d (%+4d)\n", i, bitcounts[i], bitcounts[i] - total/31));
		}
	}
  #endif
	LeaveCriticalSection(&RNG_CritSec);
  #if 0	// 2022 kriskoin
	kp1(("%s(%d) ***** WARNING: RNG_NextNumber() is not returning all bits ******\n",_FL));
	result &= 32767;
  #endif
	//kp(("%s(%d) RNG_NextNumber() = $%08lx\n", _FL, result));
	return result;
}
