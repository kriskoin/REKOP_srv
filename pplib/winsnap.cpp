#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include "pplib.h"

#define WINSNAP_API

//******************************************************************
//
//      Window 'snapping' functions
//		Win32 only
//
//      by Mike Benna, created February 1999
//      Copyright (c) 1999 Mike Benna
//
//******************************************************************

#define SNAP_DISTANCE	10	// if within n pixels, snap to screen edge
#define MAX_SNAP_LINES	40	// max number of lines we'll test against.

typedef struct SnapVars {
	HWND hwnd;
	LPRECT lprect;
	int iVertSnapLineCount, iHorzSnapLineCount;
	LONG lVertSnapLines[MAX_SNAP_LINES], lHorzSnapLines[MAX_SNAP_LINES];
} SnapVars;

#define zstruct(a)	memset(&(a),0,sizeof(a))
void cdecl dprintf(char *fmt, ...);

//***************************************************************
//  Thu Feburary 25, 1999 - MB
//
//	Internal window enumeration procedure for window snapping.
//
//***************************************************************

static BOOL CALLBACK WinSnapEnumWindowsProc(HWND hwnd, LPARAM lParam)
{
	RECT r;
	WINDOWPLACEMENT wp;

		SnapVars *sv = (SnapVars *)lParam;
		if (hwnd && sv && hwnd!=sv->hwnd && IsWindowVisible(hwnd) && GetWindowPlacement(hwnd, &wp)) {
			// If the window is showing normally, check it's rect.
			if (wp.showCmd == SW_NORMAL && GetWindowRect(hwnd, &r)) {
				// We found a normal window... check if its coordinates are something we should
				// consider snapping to.
				//dprintf("Window $%08lx, rect = %d,%d,%d,%d", hwnd, r.left, r.top, r.right, r.bottom);
				// Check for horz overlap and add horz lines if there is some.
				if (sv->lprect->left <= r.right && sv->lprect->right >= r.left) {
					// There is horizontal overlap.
					//dprintf("Horz overlap.  Adding %d and %d as horizontal lines.", r.top, r.bottom);
					if (sv->iHorzSnapLineCount < MAX_SNAP_LINES)
						sv->lHorzSnapLines[sv->iHorzSnapLineCount++] = r.top;
					if (sv->iHorzSnapLineCount < MAX_SNAP_LINES)
						sv->lHorzSnapLines[sv->iHorzSnapLineCount++] = r.bottom;
				}

				// Check for vert overlap and add vert lines if there is some.
				if (sv->lprect->top <= r.bottom && sv->lprect->bottom >= r.top) {
					// There is vertical overlap.
					//dprintf("Vert overlap.  Adding %d and %d as vertical lines.", r.left, r.right);
					if (sv->iVertSnapLineCount < MAX_SNAP_LINES)
						sv->lVertSnapLines[sv->iVertSnapLineCount++] = r.left;
					if (sv->iVertSnapLineCount < MAX_SNAP_LINES)
						sv->lVertSnapLines[sv->iVertSnapLineCount++] = r.right;
				}
			}
		}
		return TRUE;	// TRUE=finish enumerating, FALSE=stop enumerating.
}


//***************************************************************
//  Wed Feburary 24, 1999 - MB
//
//	Build the list of lines we'll snap to
//
//***************************************************************

static void BuildSnapList(SnapVars *sv)
{
		// Start with the desktop. Fetch width and height of desktop...
		RECT desktop_rect;
		zstruct(desktop_rect);
		if (!SystemParametersInfo(SPI_GETWORKAREA,0,&desktop_rect,0)) {
			// Failed.. probably an older system...
			GetWindowRect(GetDesktopWindow(), &desktop_rect);
		}
		//dprintf("-------- Desktop rect = %d,%d,%d,%d", desktop_rect.left, desktop_rect.top, desktop_rect.right, desktop_rect.bottom);

		sv->lVertSnapLines[sv->iVertSnapLineCount++] = desktop_rect.left;
		sv->lVertSnapLines[sv->iVertSnapLineCount++] = desktop_rect.right;
		sv->lHorzSnapLines[sv->iHorzSnapLineCount++] = desktop_rect.top;
		sv->lHorzSnapLines[sv->iHorzSnapLineCount++] = desktop_rect.bottom;

		//kriskoin: 		// dealing with multiple monitors or not.
		#define NORMAL_DESKTOP_RATIO	(4.0/3.0)	// normal screen width/height ratio (e.g. 1280/1024, 1024/768).
		{
			LONG w = max(1, (desktop_rect.right - desktop_rect.left));
			LONG h = max(1, (desktop_rect.bottom - desktop_rect.top));
			double r = ((double)w/NORMAL_DESKTOP_RATIO)/(double)h;
			int horz_monitor_count = max(1, (int)(r+.5));	// round to nearest integer >= 1
			int vert_monitor_count = max(1, (int)(1.0/r + .5));
//			dprintf("Number of monitors horizontally = %.2f (%d), vertically = %.2f (%d)", r, horz_monitor_count, 1.0/r, vert_monitor_count);

			// Given the number of monitors, add new vertical and horizontal lines to
			// snap test grid.
			for (int i=1 ; i<horz_monitor_count ; i++) {
				if (sv->iVertSnapLineCount < MAX_SNAP_LINES)
					sv->lVertSnapLines[sv->iVertSnapLineCount++] = w*i/horz_monitor_count;
			}
			for (i=1 ; i<vert_monitor_count ; i++) {
				if (sv->iHorzSnapLineCount < MAX_SNAP_LINES)
					sv->lHorzSnapLines[sv->iHorzSnapLineCount++] = h*i/vert_monitor_count;
			}
		}

		//kriskoin: 		// and add their lines if they are nearby in the other dimension.
		EnumWindows(WinSnapEnumWindowsProc, (LPARAM)sv);

		//dprintf("%d horz snap lines and %d vert snap lines to test against.", iHorzSnapLineCount, iVertSnapLineCount);
}


//***************************************************************
//  Sat Feburary 27, 1999 - MB
//
//	Snap a particular coordinate to the already calculated list
//	of lines.  There is one function for vertical lines and another
//	function for horizontal lines.
//
//	This function returns the number of pixels that the coord should
//	be adjusted by to snap to any lines.
//
//***************************************************************

static int SnapToVertLine(SnapVars *sv, LONG coord, LONG accumulated_adjust, LONG *adjust)
{
	int did_snap = FALSE;
	int i;

		*adjust = 0;
		for (i=0 ; i<sv->iVertSnapLineCount ; i++) {
			if (abs(coord - accumulated_adjust - sv->lVertSnapLines[i]) < SNAP_DISTANCE) {
				// Snap this coord
				*adjust = sv->lVertSnapLines[i] - coord;
				did_snap = TRUE;
				break;
			}
		}
		return did_snap;
}

static LONG SnapToHorzLine(SnapVars *sv, LONG coord, LONG accumulated_adjust, LONG *adjust)
{
	int did_snap = FALSE;
	int i;

		*adjust = 0;
		for (i=0 ; i<sv->iHorzSnapLineCount ; i++) {
			if (abs(coord - accumulated_adjust - sv->lHorzSnapLines[i]) < SNAP_DISTANCE) {
				// Snap this coord
				*adjust = sv->lHorzSnapLines[i] - coord;
				did_snap = TRUE;
				break;
			}
		}
		return did_snap;
}


//***************************************************************
//  Wed Feburary 24, 1999 - MB
//
//	Snap the window position to the borders of the screen while
//	it is being moved around (WM_MOVING).
//
//	This function should be called from the message handler for a
//	window when WM_MOVING is the message.  All Parms come directly
//	from the WM_MOVING message.
//	Message handling should be completed by calling the default handler.
//
//***************************************************************

WINSNAP_API LRESULT WinSnapWhileMoving(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	static int iNetHorzAdjust, iNetVertAdjust;
	SnapVars sv;

		zstruct(sv);
		sv.hwnd = hwnd;
		sv.lprect = (LPRECT)mp2;

		//dprintf("Got WM_MOVING: flags = $%04x rect = %d,%d,%d,%d", mp1, sv.lprect->left, sv.lprect->top, sv.lprect->right, sv.lprect->bottom);

		// Start by building the table of lines we'll try snapping to.
		BuildSnapList(&sv);

		// Test for vertical snaps...
		LONG adjust;
		int did_snap = SnapToVertLine(&sv, sv.lprect->left, iNetHorzAdjust, &adjust);
		if (!did_snap)
			did_snap = SnapToVertLine(&sv, sv.lprect->right, iNetHorzAdjust, &adjust);
		if (did_snap) {
			iNetHorzAdjust   += adjust;
			sv.lprect->left  += adjust;
			sv.lprect->right += adjust;
		} else {
			sv.lprect->left  -= iNetHorzAdjust;
			sv.lprect->right -= iNetHorzAdjust;
			iNetHorzAdjust = 0;
		}

		// Test for horizontal snaps...
		did_snap = SnapToHorzLine(&sv, sv.lprect->top, iNetVertAdjust, &adjust);
		if (!did_snap)
			did_snap = SnapToHorzLine(&sv, sv.lprect->bottom, iNetVertAdjust, &adjust);
		if (did_snap) {
			iNetVertAdjust    += adjust;
			sv.lprect->top    += adjust;
			sv.lprect->bottom += adjust;
		} else {
			sv.lprect->top    -= iNetVertAdjust;
			sv.lprect->bottom -= iNetVertAdjust;
			iNetVertAdjust = 0;
		}
		NOTUSED(mp1);
		NOTUSED(msg);
		return 0;
}


//***************************************************************
//  Wed Feburary 24, 1999 - MB
//
//	Snap the window edge to the borders of the screen while
//	it is being sized (WM_SIZING).
//
//	This function should be called from the message handler for a
//	window when WM_SIZING is the message.  All Parms come directly
//	from the WM_SIZING message.
//	Message handling should be completed by calling the default handler.
//
//***************************************************************

WINSNAP_API LRESULT WinSnapWhileSizing(HWND hwnd, UINT msg, WPARAM mp1, LPARAM mp2)
{
	SnapVars sv;
	LONG adjust;

		zstruct(sv);
		sv.hwnd = hwnd;
		sv.lprect = (LPRECT)mp2;
		//dprintf("Got WM_SIZING: flags = $%04x rect = %d,%d,%d,%d", mp1, sv.lprect->left, sv.lprect->top, sv.lprect->right, sv.lprect->bottom);

		// Start by building the table of lines we'll try snapping to.
		BuildSnapList(&sv);

		// Snap the top or bottom
//		kprintf("%s(%d)\n", __FILE__, __LINE__);
		if (mp1==WMSZ_BOTTOM || mp1==WMSZ_BOTTOMLEFT || mp1==WMSZ_BOTTOMRIGHT) {
			//kprintf("%s(%d)\n", __FILE__, __LINE__);
			SnapToHorzLine(&sv, sv.lprect->bottom, 0, &adjust);
			sv.lprect->bottom += adjust;
		}
		if (mp1==WMSZ_TOP || mp1==WMSZ_TOPLEFT || mp1==WMSZ_TOPRIGHT) {
			//kprintf("%s(%d)\n", __FILE__, __LINE__);
			SnapToHorzLine(&sv, sv.lprect->top, 0, &adjust);
			sv.lprect->top += adjust;
		}

		// Snap the left or right
		if (mp1==WMSZ_LEFT || mp1==WMSZ_BOTTOMLEFT || mp1==WMSZ_TOPLEFT) {
			SnapToVertLine(&sv, sv.lprect->left, 0, &adjust);
			sv.lprect->left += adjust;
		}
		if (mp1==WMSZ_RIGHT || mp1==WMSZ_BOTTOMRIGHT || mp1==WMSZ_TOPRIGHT) {
			SnapToVertLine(&sv, sv.lprect->right, 0, &adjust);
			sv.lprect->right += adjust;
		}

		NOTUSED(msg);
		return 0;
}


#if 0
HANDLE SnapDLLhModule;
BOOL APIENTRY DllMain( HANDLE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	SnapDLLhModule = (HANDLE) hModule;
    return TRUE;
}
#endif

#define HOOK_ALL_WINDOWS 0
// Note: the HOOK_ALL_WINDOWS code does not yet work because the hook procedure
// must be in a DLL in order for it to hook windows of other processes.
// See SetWindowsHookEx() for more info.
#if HOOK_ALL_WINDOWS
HHOOK SnapAllWindowsHookHandle;
LRESULT CALLBACK SnapAllWindowsHookProc(int nCode, WPARAM wParam, LPARAM lParam);

WINSNAP_API void SetHookParms(HHOOK hhook)
{
	SnapAllWindowsHookHandle = hhook;
}

//19990226MB ---------------- Trap WM_MOVING calls for ALL windows -----------------
WINSNAP_API LRESULT CALLBACK SnapAllWindowsHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
		if (nCode == HC_ACTION && lParam) {
			CWPSTRUCT *cwp = (CWPSTRUCT *)lParam;
			if (cwp->hwnd) {
				switch (cwp->message) {
				case WM_MOVING:
					WinSnapWhileMoving(cwp->hwnd, cwp->message, cwp->wParam, cwp->lParam);
					break;
				case WM_SIZING:
					WinSnapWhileSizing(cwp->hwnd, cwp->message, cwp->wParam, cwp->lParam);
					break;
				}
			}
		}
		return CallNextHookEx(SnapAllWindowsHookHandle, nCode, wParam, lParam);
}
//19990226MB ---------------- end Trap WM_MOVING calls for ALL windows -----------------

#endif // HOOK_ALL_WINDOWS
