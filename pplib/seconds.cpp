//******************************************************************
//
//      Misc. Second Timer tools
// 
//
//******************************************************************

#define DISP 0

#include <time.h>
#if !WIN32
  #include <sys/time.h>
  #include <unistd.h>
#endif
#include "pplib.h"

// # of seconds elapsed since UpdateSecondCounter() first called.  This counter
// is guaranteed not to jump backwards at all or forwards by more than one
// second, even if the computer's date/time is adjusted while our program is
// running.
WORD32 SecondCounter = 1;	// always start at a non-zero value.

#define MAX_FORWARD_SECOND_JUMP	10	// never jump forward by more than 10s.

//****************************************************************
// https://github.com/kriskoin//
// Update SecondCounter according to its specs.  This function should
// be called at least once per second, but should not be called
// constantly due to CPU load.  A reasonable limit to shoot for is
// about 5 to 10 times per second.
//
void UpdateSecondCounter(void)
{
	static time_t old_time;
	time_t new_time = time(NULL);
	if (!old_time) {	// first time through?
		old_time = new_time;	// yes, avoid warning messages.
	}
	if (new_time < old_time) {
		// Time stepped backwards... someone must be messing with the clock.
		kp(("%s %s(%d) Time stepped backwards by %d seconds. Compensating.\n",TimeStr(),_FL,old_time-new_time));
		SecondCounter++;		// step forward by exactly 1 second.
		old_time = new_time;	// reset now so we don't step any more
	} else if (new_time - old_time > MAX_FORWARD_SECOND_JUMP) {
		kp(("%s %s(%d) Time stepped forwards by %d seconds. Compensating.\n",TimeStr(),_FL,new_time - old_time));
		SecondCounter += MAX_FORWARD_SECOND_JUMP;
		old_time = new_time;	// reset now so we don't step any more
	}
	SecondCounter += new_time - old_time;
	old_time = new_time;		// keep updated for next time.
}

#if !WIN32
//*********************************************************
// https://github.com/kriskoin//
// Return the number of elapsed milliseconds since our program
// started (actually, since this function was first called).
// This function is designed to be similar to the Windows function
// of the same name.
//
WORD32 GetTickCount(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);

	static WORD32 initial_ms_value;
	if (!initial_ms_value) {
		// We've never initialized the initial ms value. Do so now.
		initial_ms_value = (now.tv_sec * 1000) + (now.tv_usec / 1000);
	}
	return (now.tv_sec * 1000) + (now.tv_usec / 1000) - initial_ms_value;
}
#endif
