//****************************************************************
// https://github.com/kriskoin//
// anim.cpp : Animation class for moving 2D points around
//
//****************************************************************

#define DISP 0

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include "pplib.h"

//*********************************************************
// https://github.com/kriskoin//
//  AnimationPoint constructor and destructor.
//
AnimationPoint::AnimationPoint(void)
{
	state = ANIM_STATE_COMPLETE;
	zstruct(current_point);
	zstruct(start_point);
	zstruct(end_point);
//	x_vel = y_vel = 0.0;
	start_ms = total_ms = 0;
}

AnimationPoint::~AnimationPoint(void)
{
	state = ANIM_STATE_COMPLETE;
	current_point = end_point;
}

//*********************************************************
// https://github.com/kriskoin//
// Initialize the values for this object.  Starting point, ending point,
// and # of milliseconds to take to move from one to the other.
//
void AnimationPoint::Init(POINT *start, POINT *end, int milliseconds)
{
	state = ANIM_STATE_INIT;
	start_ms = GetTickCount();
	total_ms = max(1,milliseconds);	// never allow zero or less.
	current_point = start_point = *start;
	end_point = *end;
//	x_vel = y_vel = 0.0;
}

//*********************************************************
// https://github.com/kriskoin//
// Set the current point to a particular point and mark the
// animation is 'complete'.
//
void AnimationPoint::SetPoint(POINT *pt)
{
	state = ANIM_STATE_COMPLETE;
	current_point = start_point = end_point = *pt;
}

//*********************************************************
// https://github.com/kriskoin//
// Update the current_point and state values based on the current
// number of elapsed milliseconds (since Init()).
//
void AnimationPoint::Update(void)
{
	// Early out... if we're already done, don't to anything.
	if (state==ANIM_STATE_COMPLETE) {
		return;
	}
	// Figure out what portion of our time has elapsed (0.0 to 1.0)
	long elapsed_ms = (long)(GetTickCount() - start_ms);
	if (elapsed_ms < 0) {
		// Tick counter must have wrapped at a non-continuous place.  That's
		// both odd and bad.  Make elapsed_ms reasonable.
		elapsed_ms = (long)total_ms;
	}
	double elapsed_time_ratio = (double)elapsed_ms / (double)total_ms;
	if (elapsed_time_ratio >= 1.0) {
		// We're all done.
		state = ANIM_STATE_COMPLETE;	// set anim state to 'complete'.
		current_point = end_point;		// force us to exactly the end point.
		return;
	}

	// Now determine how far along our animation path we should be at
	// this point in time.
	// For now, let's just move it without using any acceleration or
	// deceleration.  Later, we'll add a more complex formula that looks
	// better.
	// x = vx*t + .5*a*t*t
	// For the center point (x=.5, t=.5):
	//	  .5 = vx * .5  +  .5a(.5*.5)
	//	 1/2 = vx / 2   +  a/8
	//	   1 = vx  +  a/4
	//	  vx = 1 - a/4
  #if 1	// 2022 kriskoin
	#define ACCELERATION_FACTOR		3.2	 // (range is 0.0 to 4.0)
  #else
	#define ACCELERATION_FACTOR		2.7	 // (range is 0.0 to 4.0)
  #endif
  #if 1	// 2022 kriskoin
  	double vx = 1.0 - (ACCELERATION_FACTOR / 4.0);
	double t = elapsed_time_ratio;
	if (elapsed_time_ratio > .5) {	// more than half way?
		t = 1.0 - t;	// make t the time from the end point
	}
	double position_ratio = vx*t + .5*ACCELERATION_FACTOR*t*t;
	if (elapsed_time_ratio > .5) {	// more than half way?
		position_ratio = 1.0 - position_ratio;
	}
  #else
	double position_ratio = elapsed_time_ratio;	// no acceleration
  #endif

	state = ANIM_STATE_MOVING;

	// Given the position ratio (0.0 to 1.0), determine where we should be
	// on the path.
	current_point.x =  start_point.x +
			(int)((double)(end_point.x - start_point.x) * position_ratio + .5);
	current_point.y =  start_point.y +
			(int)((double)(end_point.y - start_point.y) * position_ratio + .5);
}
