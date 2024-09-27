//*********************************************************

// https://github.com/kriskoin
//

//	Misc. process related tools (mostly linux).

//	Also include the critical section related stuff.

//

//*********************************************************



#if 0	//kriskoin:   #define DEADLOCK_WARNING_TIME_MS		(2)	// time in ms before printing a deadlock warning

#else

  #define DEADLOCK_WARNING_TIME_MS		(3*1000)	// time in ms before printing a deadlock warning

#endif

#if WIN32	// 2022 kriskoin

  #define DEADLOCK_TIME					(10*1000)	// after n ms, assume this is a deadlock.

#else

  #define DEADLOCK_TIME					(120*1000)	// after n ms, assume this is a deadlock.

#endif

#if 1	// 2022 kriskoin

  #define CRITSEC_OWNED_DURATION_WARNING	1000	// # of ms before issuing warnings when INCLUDE_FUNCTION_TIMING set.

#else

  #define CRITSEC_OWNED_DURATION_WARNING	150		// # of ms before issuing warnings when INCLUDE_FUNCTION_TIMING set.

#endif



#define DISP 0



#include <stdio.h>

#include <stdlib.h>

#ifdef WIN32

  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers

  #if (/*HORATIO &&*/ 0)	// admin client and debugging only.. won't work on Win95/Win98!

    #define _WIN32_WINNT 0x0400	// include NT4+ only stuff (should normally be off)

  #endif

  #include <windows.h>

  #include <winbase.h>

#else

  #include <errno.h>

  #include <sys/types.h>	// needed for daemonize()

  #include <sys/stat.h>		// needed for daemonize()

  #include <sys/ipc.h>

  #include <sys/sem.h>

  #include <pthread.h>

  #include <signal.h>



  #if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)

    /* union semun is defined by including <sys/sem.h> */

  #else

    /* according to X/OPEN we have to define it ourselves */

    union semun {

      int val;                    /* value for SETVAL */

      struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */

      unsigned short int *array;  /* array for GETALL, SETALL */

      struct seminfo *__buf;      /* buffer for IPC_INFO */

    };

  #endif

#endif

#include <time.h>

#include "pplib.h"



//*********************************************************

// https://github.com/kriskoin
//

// Add the current pid to a table of threads we dump if we detect a crash

// (we'll send a SIGALRM to each thread).

//

#define MAX_THREADS_TO_DUMP	30

#define MAX_THREAD_NAME_LEN	20

static volatile int iThreadCount;

static int iThreadPIDs[MAX_THREADS_TO_DUMP];

static char szThreadNames[MAX_THREADS_TO_DUMP][MAX_THREAD_NAME_LEN];



void RegisterThreadForDumps(char *thread_name)

{

	int i;

	for (i=0 ; i<iThreadCount ; i++) {

		if (!iThreadPIDs[i]) {

			break;

		}

	}

	if (i >= iThreadCount) {

		// Didn't find an empty spot... make a new one.

		if (iThreadCount >= MAX_THREADS_TO_DUMP) {

			Error(ERR_INTERNAL_ERROR, "%s(%d) RegisterThreadForDumps('%s') ran out of room to store threads", _FL, thread_name);

			return;

		}

		i = iThreadCount++;

	}

	iThreadPIDs[i] = getpid();

	strnncpy(szThreadNames[i], thread_name, MAX_THREAD_NAME_LEN);

  #if !WIN32

	kp(("%s(%d) pid %d: %s\n", _FL, iThreadPIDs[i], thread_name));

  #endif

  #if 0	// 2022 kriskoin

	kp(("%s(%d) After RegisterThreadForDumps():\n",_FL));

	PrintAllThreadNames();

  #endif

}



//*********************************************************

// https://github.com/kriskoin
//

// Remove the current pid from the thread table.

// (opposite of RegisterThreadForDumps()

//

void UnRegisterThreadForDumps(void)

{

	int pid = getpid();

	for (int i=0 ; i<iThreadCount ; i++) {

		if (iThreadPIDs[i]==pid) {

			memset(szThreadNames[i], 0, MAX_THREAD_NAME_LEN);

			iThreadPIDs[i] = 0;

		  #if 0	// 2022 kriskoin

			kp(("%s(%d) After UnRegisterThreadForDumps():\n",_FL));

			PrintAllThreadNames();

		  #endif

			return;

		}

	}

  #if 0	// 2022 kriskoin

	kp(("%s(%d) After UnRegisterThreadForDumps():\n",_FL));

	PrintAllThreadNames();

  #endif

}



#if !WIN32

//*********************************************************

// https://github.com/kriskoin
//

// Turn this program into a daemon under Linux.

// stdin, stdout, stderr will all get closed.

// See http://www.erlenstar.demon.co.uk/unix/faq_2.html#SEC16 for details.

//

void daemonize(void)

{

	int child_pid = fork();

	if (child_pid==-1) {

		printf("Unable to fork.  daemonize() aborted.\n");

		exit(10);

	}

	if (child_pid) {

		// We're the primary parent.  Exit.

		// Sleep first, so the forked versions have a chance to start and

		// print their messages before the command prompt re-appears.

		Sleep(1000);

		//kp(("%s(%d) process %d is exiting.  Our parent is %d\n", _FL, getpid(), getppid()));

		_exit(0);

	}

	//kp(("%s(%d) process %d is running.  Our parent is %d\n", _FL, getpid(), getppid()));

	setsid();

	child_pid = fork();

	if (child_pid==-1) {

		printf("Unable to fork.  daemonize() aborted.\n");

		exit(10);

	}

	if (child_pid) {

		// We're the second parent.  Exit.

		//kp(("%s(%d) process %d is exiting.  Our parent is %d\n", _FL, getpid(), getppid()));

		_exit(0);

	}

	//kp(("%s(%d) process %d is running.  Our parent is %d\n", _FL, getpid(), getppid()));

	// chdir("/");	// if we're not directory bound, release current directory.

	//umask(0);	// don't inherit any funky umask values from starting shell.

	printf("Running as daemon with pid=%d\n", getpid());

  #if 1	// 2022 kriskoin

	// redirect stdin, stdout, and stderr to /dev/null

	freopen("/dev/null", "r", stdin);

	freopen("/dev/null", "w", stdout);

	freopen("/dev/null", "w", stderr);

  #else

	// close stdin, stdout, and stderr.

	close(0);

	close(1);

	close(2);

  #endif

}



void SendSIGALRMToAllThreads(void)

{

	for (int i=0 ; i<iThreadCount ; i++) {

		if (iThreadPIDs[i]) {

			kp(("\n\n%s(%d) Sending SIGALRM to thread %d (%s), then waiting 3s...\n", _FL, iThreadPIDs[i], szThreadNames[i]));

			kill(iThreadPIDs[i], SIGALRM);

			Sleep(3000);	// give it time to be processed.

			kp(("%s(%d) Done sending SIGALRM to thread %d (%s) and waiting 3s.\n\n", _FL, iThreadPIDs[i], szThreadNames[i]));

		}

	}

}

#endif // !WIN32



//*********************************************************

// https://github.com/kriskoin
//

// Return the name of a thread (pid) if known.

// Never returns NULL (returns "unknown thread name" instead)

//

char *GetThreadName(int pid)

{

	for (int i=0 ; i<iThreadCount ; i++) {

		if (pid==iThreadPIDs[i]) {

			return szThreadNames[i];

		}

	}

	return "unknown thread name";

}



char *GetThreadName(void)

{

	return GetThreadName(getpid());

}



//*********************************************************

// https://github.com/kriskoin
//

// Print out (to debwin) all the thread names we know about.

//

void PrintAllThreadNames(void)

{

	for (int i=0 ; i<iThreadCount ; i++) {

		if (iThreadPIDs[i]) {

			kp(("#%2d: Thread pid %5d: %s\n", i, iThreadPIDs[i], szThreadNames[i]));

	  #if 1	// 2022 kriskoin

		} else {

			kp(("#%2d: Thread array entry %d is empty.\n", i, i));

	  #endif

		}

	}

}





#if WIN32

//*********************************************************

// 1999/06/11 - MB   (WIN32 version)

//

// Constructor/destructor for the Mutex class

//

Mutex::Mutex(void)

{

	sem_id = 0;

}



Mutex::~Mutex(void)

{

	Close();

}



//*********************************************************

// 1999/06/11 - MB   (WIN32 version)

//

// Open/create the Mutex semaphore for this Mutex object.

// success: returns 0

// failure: returns an error number (see .../include/asm/errno.h)



int Mutex::Open(char *program_name, int sem_index)

{

	char mutex_name[MAX_FNAME_LEN], name_suffix[20];

	GetNameFromPath(program_name, mutex_name);

	sprintf(name_suffix, "-%d", sem_index);

	strcat(mutex_name, name_suffix);

	pr(("%s(%d) mutex name = '%s'\n", _FL, mutex_name));

	sem_id = CreateMutex(NULL, FALSE, mutex_name);

	if (sem_id==NULL) {

		int err = GetLastError();

		pr(("%s(%d) CreateMutex failed. err = %d\n", _FL, err));

		return err;

	}



	// We opened it successfully.

	pr(("%s(%d) Mutex::Open succeeded. sem_id=%d\n", _FL, sem_id));

	return 0;

}



//*********************************************************

// 1999/06/11 - MB   (WIN32 version)

//

// Close the Mutex semaphore created with Open().

// Set force_delete to TRUE if you want it to be removed

// from the system as well (all other processes get an error).

//

void Mutex::Close(int force_delete_flag)

{

	if (sem_id) {

		CloseHandle(sem_id);

		sem_id = NULL;

	}

	NOTUSED(force_delete_flag);

}



void Mutex::Close(void)

{

	Close(FALSE);

}	



//*********************************************************

// 1999/06/11 - MB   (WIN32 version)

//

// Request a Mutex.  This grants us exclusive access to

// the mutex.  No other process may own it at the same time.

// wait_flag indicates if we should wait or return immediately.

// success: 0

// failure: returns an error number (see .../include/asm/errno.h)

//

int Mutex::Request(int wait_flag)

{

	DWORD result = WaitForSingleObject(sem_id, wait_flag ? INFINITE : 0);

	if (result != WAIT_OBJECT_0) {

		pr(("%s(%d) Mutex::Request() for sem_id $%08lx failed with error %d (GetLastError() = %d)\n", _FL, sem_id, result, GetLastError()));

		return TRUE;

	}

	pr(("%s(%d) Mutex::Request() succeeded.\n", _FL));

	return 0;	// success

}



//*********************************************************

// 1999/06/11 - MB   (WIN32 version)

//

// Release a Mutex.  We must already have owned it with Request().

//

void Mutex::Release(void)

{

	ReleaseMutex(sem_id);

}

#else	// !WIN32

//*********************************************************

// 1999/06/11 - MB   (!WIN32 version)

//

// Constructor/destructor for the Mutex class

//

Mutex::Mutex(void)

{

	sem_id = 0;

}



Mutex::~Mutex(void)

{

	Close();

}



//*********************************************************

// 1999/06/11 - MB   (!WIN32 version)

//

// Open/create the Mutex semaphore for this Mutex object.

// success: returns 0

// failure: returns an error number (see .../include/asm/errno.h)

//

int Mutex::Open(char *program_name, int sem_index)

{

	// get main key

	int key = ftok(program_name, sem_index);

	if (key == -1) {

		// Failed... this isn't good... we can't open the semaphore at all.

		kp(("%s(%d) Mutex::Open(%s, %d) ftok failed. errno=%d\n", _FL, program_name, sem_index, errno));

		return errno;

	}



	// We found the key, now open the semaphore.

	#define SEM_PERMISSIONS	(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH)

	pr(("%s(%d) ftok() succeeded.  key = $%08lx\n", _FL, key));

	int sem = semget(key, 1, SEM_PERMISSIONS);

	if (sem==-1) {

		pr(("%s(%d) first semget($%08lx, %d, %04x) failed. errno=%d\n", _FL, key, 1, SEM_PERMISSIONS, errno));

		// That failed... perhaps it doesn't exist yet.  Try to create it.

		sem = semget(key, 1, IPC_CREAT | SEM_PERMISSIONS);

		if (sem==-1) {

			pr(("%s(%d) second semget failed. errno=%d\n", _FL, errno));

			// That also failed... perhaps another process just created it.

			sem = semget(key, 1, SEM_PERMISSIONS);

			if (sem==-1) {

				pr(("%s(%d) third semget failed. errno=%d\n", _FL, errno));

			}

		}

	}

	if (sem==-1) {

		// We couldn't open it.

		kp(("%s(%d) Mutex::Open(%s, %d) failed. errno=%d\n", _FL, program_name, sem_index, errno));

		return errno;

	}



	// We opened it successfully.

	sem_id = sem;	// save our semaphore id.

	pr(("%s(%d) Mutex::Open succeeded. sem_id=%d\n", _FL, sem_id));

	return 0;

}



//*********************************************************

// 1999/06/11 - MB   (!WIN32 version)

//

// Close the Mutex semaphore created with Open().

// Set force_delete to TRUE if you want it to be removed

// from the system as well (all other processes get an error).

//

void Mutex::Close(int force_delete_flag)

{

	if (sem_id) {

		if (force_delete_flag) {	// delete it from the system?

			union semun su;

			zstruct(su);

			semctl(sem_id, 0, IPC_RMID, su);

		}

		sem_id = 0;

	}	

}



void Mutex::Close(void)

{

	Close(FALSE);

}	



//*********************************************************

// 1999/06/11 - MB   (!WIN32 version)

//

// Request a Mutex.  This grants us exclusive access to

// the mutex.  No other process may own it at the same time.

// wait_flag indicates if we should wait or return immediately.

// success: 0

// failure: returns an error number (see .../include/asm/errno.h)

//

int Mutex::Request(int wait_flag)

{

	static struct sembuf request_op_nw[2] = {{0, 0, IPC_NOWAIT},

											 {0, 1, IPC_NOWAIT | SEM_UNDO}};

	static struct sembuf request_op_w[2]  = {{0, 0, 0},

											 {0, 1, SEM_UNDO}};

	int result = semop(sem_id, wait_flag ? request_op_w : request_op_nw, 2);

	if (result==-1) {

		pr(("%s(%d) Mutex::Request() failed with error %d\n", _FL, errno));

		return errno;

	}

	pr(("%s(%d) Mutex::Request() succeeded.\n", _FL));

	return 0;	// success

}



//*********************************************************

// 1999/06/11 - MB   (!WIN32 version)

//

// Release a Mutex.  We must already have owned it with Request().

//

void Mutex::Release(void)

{

	static struct sembuf release_op = {0, -1, IPC_NOWAIT | SEM_UNDO};

	int result = semop(sem_id, &release_op, 1);

	if (result==-1) {

		pr(("%s(%d) Mutex::Release() failed with error %d.\n", _FL, errno));

	} else {

		pr(("%s(%d) Mutex::Release() succeeded.\n", _FL));

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Wrapper for converting _beginthread() to pthread_create().

//

unsigned long _beginthread(void (* funcptr)(void *), unsigned stack_size, void *lparam)

{

	if (stack_size) {

		// In this case, we probably need to call pthread_attr_init(),

		// set the stack size on the attribute, then use it for the

		// pthread_create() call.

		Error(ERR_INTERNAL_ERROR, "%s(%d) _beginthread() can't handle non-default stack size yet (%d).", _FL, stack_size);

		//kriskoin: 		// default to 2MB and are allocated as needed.  I understand

		// that on Alpha linux the default is 8MB.

		// Recent pthread implementations may include pthread_attr_setstacksize

		// but the RH6 and RH5.2 versions do not.

	}

	pthread_t pt = 0;

	int error = pthread_create(&pt, NULL, (void *(*)(void *))funcptr, lparam);

	if (error) {

		return (unsigned long)-1;	// error.

	}

	pr(("%s(%d) _beginthread() just started thread %d\n", _FL, pt));

	return pt;	// return thread id.

}



//*********************************************************

// https://github.com/kriskoin
//

// Wrapper for converting InitializeCriticalSection() to

// pthread_mutex_init().

//

void InitializeCriticalSection(CRITICAL_SECTION *crit_sec_ptr)

{

	*crit_sec_ptr = (CRITICAL_SECTION)PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

}

#endif // !WIN32



#if WIN32

//*********************************************************

// https://github.com/kriskoin
//

// Read the processor time stamp counter

//

ULONGLONG rdtsc(void)

{

	ULONGLONG result;
/*
	_asm {

		_emit 0x0f

		_emit 0x31

 		mov dword ptr [result+4],edx

		mov dword ptr [result],eax

	}
  */
	return result;

}

#endif	 // WIN32





// Fix up the #define's for calling the real functions.

#undef EnterCriticalSection

#undef LeaveCriticalSection

#if !WIN32

  #define EnterCriticalSection(crit_sec_ptr)	pthread_mutex_lock(crit_sec_ptr)

  #define TryEnterCriticalSection(crit_sec_ptr)	(!pthread_mutex_trylock(crit_sec_ptr))

  #define LeaveCriticalSection(crit_sec_ptr)	pthread_mutex_unlock(crit_sec_ptr)

  #define DeleteCriticalSection(crit_sec_ptr)	pthread_mutex_destroy(crit_sec_ptr)

#endif



#define MAX_CRITSEC_THREADS		30	// max # of threads we can handle critsecs for

static struct CritSecThreadInfoStruct {

	WORD32 thread_id;	// thread id this array entry refers to

	PPCRITICAL_SECTION *last_crit_sec_owned;	// ptr to the last critical section we entered.  This is like the top of the 'stack'.

} CritSecThreadInfo[MAX_CRITSEC_THREADS];

static volatile int iCritSecThreadInfoCount;	// # of entries in the CritSecThreadInfo[] array



//*********************************************************

// https://github.com/kriskoin
//

// Return the current (or allocate one) CritSecThreadInfoStruct entry

// from the CritSecThreadInfo[] array.

//

static struct CritSecThreadInfoStruct *GetThreadInfoStruct(void)

{

	static volatile int done_init = FALSE;

	static CRITICAL_SECTION threadinfo_crit_sec;

	if (!done_init) {

		InitializeCriticalSection(&threadinfo_crit_sec);

		done_init = TRUE;

	}



	WORD32 thread_id = getpid();



	static volatile int prev_index=0;	// used only as a hint. could EASILY be wrong

  #if 1	//kriskoin: 	// First, guess it's the same as last time.	

	struct CritSecThreadInfoStruct *result = CritSecThreadInfo + prev_index;

	if (result->thread_id==thread_id) {

		return result;

	}

  #else

	struct CritSecThreadInfoStruct *result;

  #endif



	// Search the array for this thread id...

	result = CritSecThreadInfo;

	for (int i=0 ; i<iCritSecThreadInfoCount ; i++, result++) {

		if (result->thread_id == thread_id) {

			// Found it.

			prev_index = i;

			return result;

		}

	}



	// Not found... allocate a new one if there is space.

	EnterCriticalSection(&threadinfo_crit_sec);

	if (iCritSecThreadInfoCount < MAX_CRITSEC_THREADS) {

		result = &CritSecThreadInfo[iCritSecThreadInfoCount];

		zstruct(*result);

		result->thread_id = thread_id;

		iCritSecThreadInfoCount++;

	} else {

		kp1((ANSI_ERROR"%s(%d) ERROR: too many threads for proc.cpp to handle!\n",_FL));

		kp1(("%s(%d) Note: this could be because nothing is ever removed from this list!\n", _FL));

		result = NULL;

	}

	LeaveCriticalSection(&threadinfo_crit_sec);

	return result;

}



#if DEBUG

//*********************************************************

// https://github.com/kriskoin
//

// Print a list of the critical sections owned given a starting crit sec.

// Returns the depth (total # of critical sections owned).

//

int PrintOwnedCriticalSections(PPCRITICAL_SECTION *crit_sec_ptr)

{

	static volatile int done_init = FALSE;

	static CRITICAL_SECTION cs;

	if (!done_init) {

		InitializeCriticalSection(&cs);

		done_init = TRUE;

	}

	if (!crit_sec_ptr) {

		return 0;

	}



	EnterCriticalSection(&cs);

	// Follow the chain to get the total depth, then start working our way back up.

	int depth = 1;

	PPCRITICAL_SECTION *p = crit_sec_ptr;

	while (p->prev_crit_sec_owned && depth < 20) {

		depth++;

		p = p->prev_crit_sec_owned;

	}

	//kp(("%s(%d) CritSec depth = %d\n", _FL, depth));

	// Now loop through (from bottom to top) and display where we are.

	int old_pri = -1;

	for (int i=0 ; i<depth ; i++) {

		p = crit_sec_ptr;

		int j = depth - i;

		while (--j > 0 && p->prev_crit_sec_owned) {

			p = p->prev_crit_sec_owned;

		}

		char src[100];

		zstruct(src);

		if (p->owner_src_fname) {

			sprintf(src, "%s(%d)", GetNameFromPath2(p->owner_src_fname), p->owner_src_line);

		} else {

			strcpy(src, "unknown src");

		}

		int elapsed = GetTickCount() - p->acquired_time;

		kp(("%-20s #%2d: Enter CritSec %-16.16s priority %2d, nested %2d deep, owned for %4dms",

				src, i+1, p->name, p->priority,

				p->owner_nest_count, elapsed));

		if (old_pri >= p->priority) {

			kp((" (out of order)"));

		}

		kp(("\n"));

		old_pri = p->priority;

	}

	LeaveCriticalSection(&cs);

	return depth;

}



//*********************************************************

// https://github.com/kriskoin
//

// Display any critical sections currently owned by this thread

//

void PrintOwnedCriticalSections(void)

{

	struct CritSecThreadInfoStruct *tis = GetThreadInfoStruct();

	if (!tis) {

		return;

	}



	int depth = PrintOwnedCriticalSections(tis->last_crit_sec_owned);

	if (!depth) {

		kp(("(no critical sections owned by this thread (%s))\n", GetThreadName()));

	}

}

#endif	// DEBUG



//*********************************************************

// https://github.com/kriskoin
//

// Our own EnterCriticalSection() which does timeouts and

// stack dumps when deadlocks are encountered.

// This function is automatically called when you use

// EnterCriticalSection because it is #define'd to call this.

// The *output_elapsed_ms ptr MAY hold the number of milliseconds

// required to aquire the lock.

//

static void PPEnterCriticalSection(PPCRITICAL_SECTION *crit_sec_ptr, int *output_elapsed_ms)

{

	CRITICAL_SECTION *cs = &crit_sec_ptr->cs;

	*output_elapsed_ms = 0;	// set to zero in case we don't know.

  //#pragma message("pplib/proc.cpp WARNING: Critical section deadlock detection disabled for performance testing.")

  //#if 0	//!!! TEMP !!!

  #if(!WIN32 || _WIN32_WINNT >= 0x0400)	// is TryEnterCriticalSection() available?

	static int done_init = FALSE;

	static CRITICAL_SECTION deadlock_crit_sec;

	if (!done_init) {

		InitializeCriticalSection(&deadlock_crit_sec);

		done_init = TRUE;

	   #if WIN32

	  	#pragma message("WARNING: THIS CODE WILL ONLY WORK ON NT4 or Win2000!")

	   #endif

	}



  #if !WIN32	// 2022 kriskoin

	/* Duration of sleep (in nanoseconds) when we can't acquire a spinlock

	   after MAX_SPIN_COUNT iterations of sched_yield().

	   With the 2.0 and 2.1 kernels, this MUST BE > 2ms.

	   (Otherwise the kernel does busy-waiting for realtime threads,

	    giving other threads no chance to run.) */

	struct timespec tm;

	#ifndef SPIN_SLEEP_DURATION

	  #define SPIN_SLEEP_DURATION 2000001

	#endif

  #endif	// !WIN32



  #if 0	//kriskoin: 	//kriskoin: 	// critical section, check to see if we should sleep rather than

	// stealing it from them.  Only do this if nobody owns it and

	// we were the previous owner.

	if (     crit_sec_ptr->waiting_threads > 1					// more than just us waiting?

		 && !crit_sec_ptr->owner_nest_count						// nobody owns it (especially not us)?

		 &&  crit_sec_ptr->waiting_ticks						// we know how long they've been waiting?

		 &&  crit_sec_ptr->prev_owner_thread_id == getpid()		// previous owner was us?

		 &&  crit_sec_ptr->waiting_ticks - GetTickCount() > 30	// they've been waiting long enough?

	) {

		// Other threads are waiting, nobody owns it right now, and we were the

		// previous owner. Try not to hog it.

		//kp(("%s %s(%d) Sleeping briefly before entering critsec\n",TimeStr(),_FL));

	  #if WIN32

		Sleep(2);

	  #else

		tm.tv_sec = 0;

		tm.tv_nsec = SPIN_SLEEP_DURATION;

		nanosleep(&tm, NULL);

	  #endif

	}

  #endif



	int success = TryEnterCriticalSection(cs);

	if (success) {

		return;

	}



	// We didn't get it the first time... loop for a while trying

	// regularly to get it.



	int start_time = GetTickCount();

	if (!crit_sec_ptr->waiting_ticks) {

		crit_sec_ptr->waiting_ticks = start_time;

	}

	int end_time = start_time + DEADLOCK_TIME;

	int now = 0;

	int warning_printed = FALSE;



	//---------- From /usr/src/glibc-2.1.3/linuxthreads/spinlock.c:

	/* The retry strategy is as follows:

	   - We test and set the spinlock MAX_SPIN_COUNT times, calling

	     sched_yield() each time.  This gives ample opportunity for other

	     threads with priority >= our priority to make progress and

	     release the spinlock.

	   - If a thread with priority < our priority owns the spinlock,

	     calling sched_yield() repeatedly is useless, since we're preventing

	     the owning thread from making progress and releasing the spinlock.

	     So, after MAX_SPIN_LOCK attemps, we suspend the calling thread

	     using nanosleep().  This again should give time to the owning thread

	     for releasing the spinlock.

	     Notice that the nanosleep() interval must not be too small,

	     since the kernel does busy-waiting for short intervals in a realtime

	     process (!).  The smallest duration that guarantees thread

	     suspension is currently 2ms.

	   - When nanosleep() returns, we try again, doing MAX_SPIN_COUNT

	     sched_yield(), then sleeping again if needed. */



	  #if 0

	    if (cnt < MAX_SPIN_COUNT) {

	      sched_yield();

	      cnt++;

	    } else {

	      tm.tv_sec = 0;

	      tm.tv_nsec = SPIN_SLEEP_DURATION;

	      nanosleep(&tm, NULL);

	      cnt = 0;

	    }

	  #endif

	//----------



	int cnt = 0;

  #if 1	// 2022 kriskoin

	#define MAX_SPIN_COUNT 50

  #else

	#define MAX_SPIN_COUNT 200

  #endif



	do {

	    if (cnt < MAX_SPIN_COUNT) {

			sched_yield();

			cnt++;

	    } else {

		  #if WIN32

			Sleep(2);

		  #else

			tm.tv_sec = 0;

			tm.tv_nsec = SPIN_SLEEP_DURATION;

			nanosleep(&tm, NULL);

			cnt = 0;

		  #endif

	    }

		success = TryEnterCriticalSection(cs);

		if (success) {

			int elapsed = GetTickCount() - start_time;

			*output_elapsed_ms = elapsed;

			if (warning_printed) {

				kp(("%s **** Early warning was a false alarm. CritSec took %.2fs to acquire.\n",TimeStr(), elapsed/1000.0));

			}

			return;	// we got it. We're done.

		}

		now = GetTickCount();

		int elapsed = now - start_time;

		if (!warning_printed && elapsed >= DEADLOCK_WARNING_TIME_MS) {

			warning_printed = TRUE;

			kp(("%s **** Early warning: possible deadlock detected by %s\n", TimeStr(), GetThreadName()));

			char *fname = crit_sec_ptr->owner_src_fname;

//Wilson			fname = fname ? GetNameFromPath2(fname) : "unknown";

			if (fname) GetNameFromPath2(fname); else strcpy(fname,"unknown");

			kp(("%s      Possible current %s CritSec owner: %s(%d) %s\n",

				TimeStr(),

				crit_sec_ptr->name,

				fname, crit_sec_ptr->owner_src_line,

				GetThreadName(crit_sec_ptr->owner_thread_id)));

		}

	} while (now < end_time);



	// A deadlock occurred...

	EnterCriticalSection(&deadlock_crit_sec);

	Error(ERR_INTERNAL_ERROR, "%s(%d) Critical Section deadlock detected.", _FL);

	kp(("--------- DEADLOCK INFO ---------\n"));

	kp(("The deadlock was detected by thread %d (%s)\n", getpid(), GetThreadName()));



	// Dump a list of the critical sections we own (in order)

  #if DEBUG	// 2022 kriskoin

	struct CritSecThreadInfoStruct *tis = GetThreadInfoStruct();

	if (tis) {

		kp(("Here's a list of the critical sections owned by %s\n", GetThreadName()));

		PrintOwnedCriticalSections();

	}

  #endif



   #if INCL_STACK_CRAWL

	kp(("Stack crawl from critical section deadlock detection...\n"));

	DisplayStackCrawl();

   #endif //INCL_STACK_CRAWL

	kp(("Here's a summary of all the threads that have been registered:\n"));

	PrintAllThreadNames();

	kp(("\n"));

	LeaveCriticalSection(&deadlock_crit_sec);

	Sleep(20*1000);	// sleep a little longer

   #if !WIN32

	kp(("All deadlocks should be dumped by now (waited 20s).  Sending SIGALRM to all threads...\n"));

	SendSIGALRMToAllThreads();

	kp(("SIGALRM sending complete.  Exiting.\n"));

   #endif

	exit(10);	// exit immediately.

  #else	// Windows (less than NT4) (TryEnterCriticalSection() not available)

  	// TryEnterCriticalSection is not available, so just call the regular

	// one.  This doesn't give us any deadlock detection.

	EnterCriticalSection(cs);

  #endif	// Windows (less than NT4) (TryEnterCriticalSection() not available)

}



//*********************************************************

// https://github.com/kriskoin
//

// Initialize a critical section, with priority.

// See CRITSECPRI_* for priority conventions.

// The crit_sec_name is used for debugging display purposes only.

//

void PPInitializeCriticalSection(PPCRITICAL_SECTION *crit_sec_ptr, int crit_sec_priority, char *crit_sec_name)

{

	zstruct(*crit_sec_ptr);

	strnncpy(crit_sec_ptr->name, crit_sec_name, CRIT_SEC_NAME_LEN);

	crit_sec_ptr->priority = crit_sec_priority;

	InitializeCriticalSection(&crit_sec_ptr->cs);

}



//*********************************************************

// https://github.com/kriskoin
//

// Enter one of our critical sections, with error checking.

// This is the highest level function and it includes out-of-order

// checking.

//

void PPEnterCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line)

{

	PPEnterCriticalSection0(crit_sec_ptr, src_fname, src_line, FALSE);

}

void PPEnterCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line, int allow_nesting_lower_flag)

{

	// First, enter it, then do some error checking.

	int elapsed = 0;

  #if DEBUG

	int prev_acquire_count = crit_sec_ptr->acquire_count;

  #endif

	crit_sec_ptr->waiting_threads++;	// update the estimate... one more waiting

	PPEnterCriticalSection(crit_sec_ptr, &elapsed);



	// Update the estimate of the number of threads waiting for this

	// crit sec.  Try not to go negative.

	int new_wait_count = crit_sec_ptr->waiting_threads - 1;

	// every once-in-a-while, subtract an extra one in case it was growing

	// due to unusual concurrency issues.

	if (!(crit_sec_ptr->acquire_count & 0x3f)) {

		new_wait_count--;

	}

	if (new_wait_count < 0) {

		new_wait_count = 0;

	}

	crit_sec_ptr->waiting_threads = new_wait_count;



  #if INCLUDE_FUNCTION_TIMING	// 2022 kriskoin

	if (SecondCounter > 1) {	// never print at very start of program (conflicts with debwin initialization)

		kp1((ANSI_BLACK_ON_YELLOW"%s(%d) **** EnterCriticalSection() timing is enabled!  Don't leave this on normally!\n",_FL));

	}

	if (elapsed > 200)

  #else

      	if (SecondCounter > 1 && elapsed >= DEADLOCK_WARNING_TIME_MS)

  #endif

	{

		kp(("%s %s(%d) %s CritSec took %5dms to enter by %s.\n",

				TimeStr(), GetNameFromPath2(src_fname), src_line,

				crit_sec_ptr->name , elapsed, GetThreadName()));

		kp(("%s %s(%d) It was acquired %d other times while we waited.\n",

				TimeStr(), GetNameFromPath2(src_fname), src_line,

				crit_sec_ptr->acquire_count - prev_acquire_count));

		kp(("%s %s(%d) Previous owner of %s CritSec may have been:\n",

				TimeStr(), GetNameFromPath2(src_fname), src_line, 

				crit_sec_ptr->name));

		kp(("%s %s(%d) (%s, released %dms ago)\n",

				TimeStr(),

				crit_sec_ptr->prev_owner_src_fname ? GetNameFromPath2(crit_sec_ptr->prev_owner_src_fname) : "unknown",

				crit_sec_ptr->prev_owner_src_line,

				GetThreadName(crit_sec_ptr->prev_owner_thread_id),

				GetTickCount() - crit_sec_ptr->prev_owner_release_time));

	}



	crit_sec_ptr->owner_nest_count++;	// we own it one more time

	if (crit_sec_ptr->owner_nest_count==1) {	// first time?

		//kp(("%s(%d) PPEnterCriticalSection0('%s'...)\n",_FL,crit_sec_ptr->name));

		// This is the first time we're owning this critsec...

		// Keep track of some info...

		crit_sec_ptr->owner_thread_id = getpid();	// keep track of who owns it.

		crit_sec_ptr->owner_src_fname = src_fname;

		crit_sec_ptr->owner_src_line = src_line;

		crit_sec_ptr->prev_min_pri = CRITSECPRI_MAX;	// default



	  #if INCLUDE_FUNCTION_TIMING || 1

		crit_sec_ptr->acquired_time = GetTickCount();

		crit_sec_ptr->hold_time_warning_printed = 0;

	  #endif

		crit_sec_ptr->acquire_count++;



		struct CritSecThreadInfoStruct *tis = GetThreadInfoStruct();

		if (tis) {

			crit_sec_ptr->prev_crit_sec_owned = tis->last_crit_sec_owned;

			tis->last_crit_sec_owned = crit_sec_ptr;

		}

		// do some error checking... make sure we're grabbing in order.

		if (crit_sec_ptr->prev_crit_sec_owned == crit_sec_ptr) {

			// it was us... this is caused by a bad release order.  Since we've

			// already printed the error message for that, simply clear this ptr.

			crit_sec_ptr->prev_crit_sec_owned = NULL;

		}

		if (crit_sec_ptr->prev_crit_sec_owned) {

			// This thread already owns another critsec...

			// Inherit previous min priority

			crit_sec_ptr->prev_min_pri = crit_sec_ptr->prev_crit_sec_owned->prev_min_pri;



			// Check priority

			int priority_prob = FALSE;

			if (crit_sec_ptr->prev_min_pri >= CRITSECPRI_MAX) {

				// No minimum has been specified... simply compare with previous

				if (crit_sec_ptr->priority <= crit_sec_ptr->prev_crit_sec_owned->priority) {

					priority_prob = TRUE;

				}

			} else {

				// A minimum was specified somewhere along the way, use it.

				if (crit_sec_ptr->priority < crit_sec_ptr->prev_min_pri) {

					priority_prob = TRUE;

				}

			}

			if (priority_prob) {

				// Aack! They've been entered out of order.  Print some debug

				// info to help track down the problem.

				static volatile int done_init = FALSE;

				static CRITICAL_SECTION cs;

				if (!done_init) {

					InitializeCriticalSection(&cs);

					done_init = TRUE;

				}

				EnterCriticalSection(&cs);

				kp((ANSI_ERROR"%s(%d) ******** Warning: critical sections entered out of order *********",_FL));

				if (crit_sec_ptr->prev_min_pri < CRITSECPRI_MAX) {

					kp((" (min priority override = %d)\n",crit_sec_ptr->prev_min_pri));

				} else {

					kp((" (no minimum pri specified)\n"));

				}



				kp(("%s(%d) our thread name = %s (thread id %d)\n", _FL, GetThreadName(), getpid()));

			  #if DEBUG

				PrintOwnedCriticalSections(crit_sec_ptr);

			  #endif

			  #if 0	// 2022 kriskoin

				kp(("%s(%d)          CritSec '%s', priority %d, was first entered at %s(%d)\n",

						_FL,

						crit_sec_ptr->prev_crit_sec_owned->name,

						crit_sec_ptr->prev_crit_sec_owned->priority,

			GetNameFromPath2(crit_sec_ptr->prev_crit_sec_owned->owner_src_fname),crit_sec_ptr->prev_crit_sec_owned->owner_src_line));

			kp(("%s(%d)          CritSec '%s', priority %d, was then  entered at %s(%d)\n",_FL,crit_sec_ptr->name,

						crit_sec_ptr->priority,

						GetNameFromPath2(crit_sec_ptr->owner_src_fname),

						crit_sec_ptr->owner_src_line));

			  #endif

			   #if INCL_STACK_CRAWL

			kp(("%s(%d)          Stack crawl from critical section order entry detection:\n",_FL));

				DisplayStackCrawl();

			   #endif //INCL_STACK_CRAWL

				LeaveCriticalSection(&cs);

			}

		}

		// Update the prev_min_pri field for anyone after us.

		int our_min = allow_nesting_lower_flag ? crit_sec_ptr->priority : CRITSECPRI_MAX;

		crit_sec_ptr->prev_min_pri = min(our_min, crit_sec_ptr->prev_min_pri);

	  #if 0	// 2022 kriskoin

		if (allow_nesting_lower_flag) {

			kp(("%s(%d) Set prev_min_pri to %d\n", _FL, crit_sec_ptr->prev_min_pri));

		}

	  #endif

	} else {	// not the first time... we owned it before we got called.

		if (allow_nesting_lower_flag) {	// change allowed nesting pri level?

			crit_sec_ptr->prev_min_pri = min(crit_sec_ptr->priority, crit_sec_ptr->prev_min_pri);

		}



	  #if INCLUDE_FUNCTION_TIMING

		int elapsed = GetTickCount() - crit_sec_ptr->acquired_time;

		if (elapsed > CRITSEC_OWNED_DURATION_WARNING && crit_sec_ptr->hold_time_warning_printed < 3) {

			crit_sec_ptr->hold_time_warning_printed++;

kp(("%s %s(%d) Warning: %s CritSec already owned by %s for %dms. NestCount now %d, related info:\n",TimeStr(), GetNameFromPath2(src_fname), src_line,crit_sec_ptr->name, GetThreadName(), elapsed,	crit_sec_ptr->owner_nest_count));

			PrintOwnedCriticalSections();

		}

	  #endif

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Leave one of our critical sections, with error checking.

//

void PPLeaveCriticalSection0(PPCRITICAL_SECTION *crit_sec_ptr, char *src_fname, int src_line)

{

	crit_sec_ptr->owner_nest_count--;	// nested one less deep.

	int now = GetTickCount();

  #if INCLUDE_FUNCTION_TIMING

	int elapsed = now - crit_sec_ptr->acquired_time;

	if (elapsed > CRITSEC_OWNED_DURATION_WARNING && crit_sec_ptr->hold_time_warning_printed < 3) {

		crit_sec_ptr->hold_time_warning_printed++;

		kp(("%s %s(%d) Warning: %s CritSec owned by %s for %dms. NestCount now %d, related info:\n",				TimeStr(), GetNameFromPath2(src_fname), src_line,		crit_sec_ptr->name, GetThreadName(), elapsed,crit_sec_ptr->owner_nest_count));

		PrintOwnedCriticalSections();

	}

  #endif



	int yield_on_exit = FALSE;

	if (!crit_sec_ptr->owner_nest_count) {

		// Time to release this one...



		// Make sure we're leaving in the same order we came in...

		// We should be on the top of the stack for this thread.

		struct CritSecThreadInfoStruct *tis = GetThreadInfoStruct();

		if (tis) {

			if (tis->last_crit_sec_owned && tis->last_crit_sec_owned != crit_sec_ptr) {

				kp((ANSI_ERROR"%s(%d) ******** Warning: critical sections released in wrong order! (%s(%d), critsecname='%s')\n",_FL, src_fname, src_line, crit_sec_ptr->name));

				tis->last_crit_sec_owned = NULL;

			} else {

				tis->last_crit_sec_owned = crit_sec_ptr->prev_crit_sec_owned;

			}

			crit_sec_ptr->prev_owner_thread_id  = tis->thread_id;

		}

		crit_sec_ptr->prev_owner_src_fname = crit_sec_ptr->owner_src_fname;

		crit_sec_ptr->prev_owner_src_line  = crit_sec_ptr->owner_src_line;

		crit_sec_ptr->prev_owner_release_time  = now;



		crit_sec_ptr->prev_crit_sec_owned = NULL;

		crit_sec_ptr->owner_src_fname = NULL;

		crit_sec_ptr->owner_src_line = 0;

		crit_sec_ptr->owner_thread_id = 0;

		crit_sec_ptr->prev_min_pri = CRITSECPRI_MAX;



		yield_on_exit = crit_sec_ptr->waiting_threads;	// if other threads are waiting... yield the cpu

		if (!crit_sec_ptr->waiting_threads) {

			// nobody is waiting... clear the start wait time.

			crit_sec_ptr->waiting_ticks = 0;

		}

	  #if 0	// 2022 kriskoin

		{

			static int total = 0;

			static int yielded = 0;

			total++;

			if (yield_on_exit) {

				yielded++;

			}

			if (!(total % 50000)) {

				double percentage = (double)yielded*100.0 / (double)total;

				if (percentage >= 1.0) {

					kp(("%s(%d) Yielding %6.3f%% of the time\n", _FL, percentage));

				}

				total /= 2;

				yielded /= 2;

			}

		}

	  #endif

	}

	LeaveCriticalSection(&crit_sec_ptr->cs);

	if (yield_on_exit) {

		sched_yield();

	}

	NOTUSED(src_fname);

	NOTUSED(src_line);

}



//*********************************************************

// https://github.com/kriskoin
//

// Delete one of our critical sections.

//

void PPDeleteCriticalSection(PPCRITICAL_SECTION *crit_sec_ptr)

{

	if (crit_sec_ptr->owner_nest_count) {

		kp((ANSI_ERROR"%s(%d) ******** Warning: deleting an owned critical section! ('%s', owned by %s(%d))\n",	_FL, crit_sec_ptr->name,GetNameFromPath2(crit_sec_ptr->owner_src_fname),crit_sec_ptr->owner_src_line));

		struct CritSecThreadInfoStruct *tis = GetThreadInfoStruct();

		if (tis) {

			tis->last_crit_sec_owned = NULL;

		}

	}

	DeleteCriticalSection(&crit_sec_ptr->cs);

	zstruct(*crit_sec_ptr);

}



#if INCL_STACK_CRAWL||1

//*********************************************************

// https://github.com/kriskoin
//

// Zero out some memory used by the stack to help make

// subsequent stack dumps simpler and more relevant.

//

void ZeroSomeStackSpace(void)

{

	#define BYTES_TO_ZERO	10000

	char bytes_to_zero[BYTES_TO_ZERO];

	memset(bytes_to_zero, 0, BYTES_TO_ZERO);

}

#endif



