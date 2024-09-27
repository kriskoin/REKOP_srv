//**********************************************************

//

//	Misc. Window tools

//

//**********************************************************



//#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers

#define _WIN32_IE 0x0200	// For commctrl.h: 0x200=NT4, Win95 and later.  IE3, IE4, etc. not required.

#include <windows.h>

#include <commctrl.h>



#include <stdio.h>

#include "pplib.h"



int iLargeFontsMode;	// set if we're in large fonts mode.



#define MAX_TOOLTIP_WINDOWS 10	// max # of windows we can track tooltips for

static struct ToolTipWindowEntry  {

	HWND tooltip_hwnd;

	HWND parent_hwnd;

} ToolTipWindowEntries[MAX_TOOLTIP_WINDOWS];

static HHOOK ToolTipMsgQueueHook;

static HINSTANCE ToolTipHInst;



static BOOL CALLBACK ScaleChildTo96dpi(HWND hwnd, LPARAM lParam);



//*********************************************************

// 

//

// Convert from Screen to Client coords for a RECT (as opposed to a POINT)

//

void ScreenToClient(HWND hwnd, LPRECT r)

{

	ScreenToClient(hwnd, (LPPOINT)r);

	ScreenToClient(hwnd, (LPPOINT)r+1);

}



//*********************************************************

// 

//

// Convert from Client to Screen coords for a RECT (as opposed to a POINT)

//

void ClientToScreen(HWND hwnd, LPRECT r)

{

	ClientToScreen(hwnd, (LPPOINT)r);

	ClientToScreen(hwnd, (LPPOINT)r+1);

}



/****************************************************************/

/*																*/

/*	Store the window position to the registry					*/

/*	(emulates the WinStoreWindowPos() function in OS/2)			*/

/*																*/

/****************************************************************/



struct SavedWindowData {

	WINDOWPLACEMENT placement;

	LOGFONT logfont;

	int minimized_flag;

	int full_screen_flag;

};



void WinStoreWindowPos(char *name, char *key, HWND hwnd, LOGFONT *font_data)

{

	char str[256];

	HKEY keyhandle;

	DWORD disposition;

	LONG result;



		sprintf(str, "Software\\DesertPoker\\%s", name);

		result = RegCreateKeyEx(HKEY_CURRENT_USER,

				str, 0, "Data", 0, KEY_ALL_ACCESS,

				NULL, &keyhandle, &disposition);

		if (result==ERROR_SUCCESS) {

			struct SavedWindowData swd_info;

			BOOL result;

			zstruct(swd_info);

			swd_info.placement.length = sizeof(swd_info.placement);

			result = GetWindowPlacement(hwnd, &swd_info.placement);

		  #if 0	//:::
			kprintf("%s(%d) GetWindowPlacement() result = %d GetLastError()=%d", _FL, result, GetLastError());

		  #endif

			if (font_data)

				swd_info.logfont = *font_data;

			swd_info.minimized_flag = IsIconic(hwnd);

			swd_info.full_screen_flag = FALSE;	// was: iWinFullScreenFlag;

		  #if 0	//:::
			kprintf("%s(%d) storing data: $%08lx %08lx %08lx %08lx %08lx etc...",_FL,

					*(DWORD *)((BYTE *)&swd_info+0),

					*(DWORD *)((BYTE *)&swd_info+4),

					*(DWORD *)((BYTE *)&swd_info+8),

					*(DWORD *)((BYTE *)&swd_info+12),

					*(DWORD *)((BYTE *)&swd_info+16));

		  #endif

			RegSetValueEx(keyhandle, key, 0, REG_BINARY,

					(CONST BYTE *)&swd_info, sizeof(swd_info));

			RegCloseKey(keyhandle);

		} else {

			kp(("%s(%d) RegCreateKeyEx returned an error.", _FL));

		}

}



/****************************************************************/

/*																*/

/*	Restore the window position and font from registry info		*/

/*	(emulates the WinRestoreWindowPos() function in OS/2)		*/

/*	(returns TRUE if a key was found and the window restored)	*/

/*																*/

/****************************************************************/



BOOL WinRestoreWindowPos(char *name, char *key, HWND hwnd, LOGFONT *font_data, HFONT *current_font_handle, int restore_show_window_flag, int ignore_restored_window_size)

{

	char str[256];

	HKEY keyhandle;

	DWORD len;

	DWORD data_type;

	LONG result;



		sprintf(str, "Software\\DesertPoker\\%s", name);

		keyhandle = 0;

		result = RegOpenKeyEx(HKEY_CURRENT_USER,

				str, 0, KEY_ALL_ACCESS, &keyhandle);

//		kprintf("result from RegOpenKeyEx('%s') = %d", str, result);

		if (result==ERROR_SUCCESS) {

			struct SavedWindowData swd_info;

			zstruct(swd_info);

			len = sizeof(swd_info);

			result = RegQueryValueEx(keyhandle, key, NULL, &data_type,

					(BYTE *)&swd_info, &len);

		  #if 0	//:::
			kprintf("%s(%d) result from RegQueryValueEx($%08lx, '%s',...) = %d",

					_FL, keyhandle, key, result);

		  #endif

			RegCloseKey(keyhandle);

			if (result==ERROR_SUCCESS) {

			  #if 0	//:::
				kprintf("Restoring font info: name='%s', height = %d (unknown units)",

						swd_info.logfont.lfFaceName, swd_info.logfont.lfHeight);

			  #endif



				// If the user does not want to restore the size (such as for

				// dialog boxes), retrieve the current window size and use it.

				if (ignore_restored_window_size) {

					WINDOWPLACEMENT current_wp;

					zstruct(current_wp);

					current_wp.length = sizeof(current_wp);

					if (GetWindowPlacement(hwnd, &current_wp)) {

						// Adjust bottom right so that the window stays its current size.

						int w = current_wp.rcNormalPosition.right  - current_wp.rcNormalPosition.left;

						int h = current_wp.rcNormalPosition.bottom - current_wp.rcNormalPosition.top;

						swd_info.placement.rcNormalPosition.right  = swd_info.placement.rcNormalPosition.left + w;

						swd_info.placement.rcNormalPosition.bottom = swd_info.placement.rcNormalPosition.top + h;

						//kp(("%s(%d) Current window size = %dx%d\n", _FL, w, h));

					}

				}

				if (font_data)

					*font_data = swd_info.logfont;

			  #if 0	//:::
				kp(("%s(%d) window placement: normal pos = %d,%d,%d,%d\n",

						_FL,

						swd_info.placement.rcNormalPosition.left,

						swd_info.placement.rcNormalPosition.top,

						swd_info.placement.rcNormalPosition.right,

						swd_info.placement.rcNormalPosition.bottom));

				kp(("%s(%d) swd_info.minimized_flag = %d\n",

						_FL, swd_info.minimized_flag));

			  #endif

			  #if 1	//:::
				if (!restore_show_window_flag)

					swd_info.placement.showCmd = SW_HIDE;	// keep it hidden.  User will show.



				SetWindowPlacement(hwnd, &swd_info.placement);



				if (current_font_handle) {

					HFONT newfont = CreateFontIndirect(font_data);

					if (newfont) {

						if (*current_font_handle)

							DeleteObject(*current_font_handle);

						*current_font_handle = newfont;

					}

//					WinPostMsg(hwnd, WM_PRESPARAMCHANGED, 0, 0);

				}



				if (restore_show_window_flag) {

					if (swd_info.minimized_flag)

						ShowWindow(hwnd, SW_SHOWMINIMIZED);

					else

						ShowWindow(hwnd, SW_SHOWNORMAL);

				}



				return TRUE;

			  #endif

			} else {

				if (result != ERROR_FILE_NOT_FOUND) {

					kp(("%s(%d) warning: RegQueryValueEx() returned error %d\n",_FL,result));

				}

			}

		}



 		return FALSE;	// we didn't restore anything

}



/****************************************************************/


/*																*/

/*	Convert a command line into an argc/argv[] pair just like	*/

/*	main() expects.												*/

/*																*/

/****************************************************************/



int CommandLineToArgcArgv(char *cmd_line, char **argv_array, int max_argc_value)

{

	int argc = 1;

	int last_arg_found;

	#define MAX_EXE_PATH_LEN	200

	static char exe_path[MAX_EXE_PATH_LEN+1];

	dword chars;



		memset(argv_array, 0, sizeof(*argv_array)*max_argc_value);

//		kp(("%s(%d) cleared %d bytes\n", _FL, sizeof(*argv_array)*max_argc_value));



	  #if 1	//:::
		chars = GetModuleFileName(NULL, exe_path, MAX_EXE_PATH_LEN);

//		kp(("%s(%d) exe path = '%s'\n", _FL, exe_path));

		if (chars < MAX_EXE_PATH_LEN)

			exe_path[chars] = 0;

		else

			exe_path[0] = 0;



		argv_array[0] = exe_path;

	  #else

		argv_array[0] = "Unknown_EXE_path.exe";

	  #endif



		last_arg_found = FALSE;

		while (*cmd_line && argc < max_argc_value && !last_arg_found) {

			char *start_of_string, *end_of_arg, delimeter;

			while (*cmd_line==' ') cmd_line++;	// skip leading spaces.

			start_of_string = cmd_line;

			delimeter = ' ';	// default to using a space as the delimeter.

			if (strchr("'`\"", *cmd_line)) {	// it's a quote!

				// skip up to the next quote character.

				delimeter = *cmd_line;

				cmd_line++;	// skip current one.

			}

			// Look for the next delimeter...

			end_of_arg = strchr(cmd_line, delimeter);

			if (end_of_arg) {

				cmd_line = end_of_arg+1;

				*end_of_arg = 0;	// terminate the arg

			} else {

				last_arg_found = TRUE;

			}



			if (*start_of_string)

				argv_array[argc++] = start_of_string;

		}

		return argc;

}



//*********************************************************


//

// Hook window messages for all windows and watch for messages

// we need to relay to one of the open tooltips windows.

//

static LRESULT CALLBACK ToolTipMsgQueueHookProc(int nCode, WPARAM wParam, LPARAM lParam)

{

    if (nCode >= 0) {

	    MSG *msg = (MSG *)lParam;

		// Search for the tooltip_hwnd entry that corresponds to hwnd this

		// message is for.  If it's one of ours, relay it.

		struct ToolTipWindowEntry *ttwe = ToolTipWindowEntries;

		for (int i=0 ; i<MAX_TOOLTIP_WINDOWS ; i++, ttwe++) {

			if (ttwe->tooltip_hwnd && (

				ttwe->parent_hwnd==msg->hwnd || IsChild(ttwe->parent_hwnd,msg->hwnd))) {

				// This entry exists and the message is for it or one of its

				// child windows.

				break;

			}

		}

		if (i<MAX_TOOLTIP_WINDOWS) {

			// Found it.

			// Only relay certain messages.

		    switch (msg->message) {

		    case WM_MOUSEMOVE:

		    case WM_LBUTTONDOWN:

		    case WM_LBUTTONUP:

		    case WM_RBUTTONDOWN:

		    case WM_RBUTTONUP: 



				{

					MSG msg2;

					zstruct(msg2);

		            msg2.lParam = msg->lParam;

		            msg2.wParam = msg->wParam;

		            msg2.message = msg->message;

		            msg2.hwnd = msg->hwnd;

		            SendMessage(ttwe->tooltip_hwnd, TTM_RELAYEVENT, 0, (LPARAM)&msg2);

		        }

		        break;

		    }

	    }

    }

	return CallNextHookEx(ToolTipMsgQueueHook, nCode, wParam, lParam);

}



//*********************************************************

// https://github.com/kriskoin
//

// Open the tool tip hook procedures.  This procedure must

// be called exactly once at the start of a program.

// MUST be called by the message queue thread.

//

void OpenToolTipHooks(HINSTANCE hinst)

{

	// Install a hook procedure to monitor the message stream for mouse

    // messages intended for the controls in the dialog box.

	// There's only one hook procedure for all table windows.

	if (!ToolTipMsgQueueHook) {

		ToolTipMsgQueueHook = SetWindowsHookEx(WH_GETMESSAGE, ToolTipMsgQueueHookProc,

		        (HINSTANCE)NULL, GetCurrentThreadId());

		ToolTipHInst = hinst;

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Close the tool tip hook procedures.  This procedure must

// be called exactly once at the end of a program if

// OpenToolTipHooks() was called.

//

void CloseToolTipHooks(void)

{

	if (ToolTipMsgQueueHook) {

		UnhookWindowsHookEx(ToolTipMsgQueueHook);

		ToolTipMsgQueueHook = 0;

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Open a new ToolTip window as a child of a particular window.

// You need a separate ToolTip window for each dialog box that

// you want to use tool tips in.

// Also adds a list of tool tip text for a dialog box given a

// ptr to an array of dialog control ID's and associated text.

// The end of the array is signalled with a 0,0 entry.

//

HWND OpenToolTipWindow(HWND parent, struct DlgToolTipText *dttt)

{

	if (!ToolTipMsgQueueHook) {

		kp1(("%s(%d) Warning: OpenToolTipHooks() was not called before OpenToolTipWindow().\n",_FL));

	}



	// Find somewhere to put this new entry.

	int i=0;

	struct ToolTipWindowEntry *ttwe = ToolTipWindowEntries;

	while (ttwe->tooltip_hwnd && i<MAX_TOOLTIP_WINDOWS) {

		i++;

		ttwe++;

	}

	if (i>=MAX_TOOLTIP_WINDOWS) {

		Error(ERR_ERROR, "%s(%d) Ran out of ToolTipWindowEntries.  Increase MAX_TOOLTIP_WINDOWS.",_FL);

		return NULL;

	}



	zstruct(*ttwe);

	// Create a tooltip control/window for use within this dialog box

	ttwe->tooltip_hwnd = CreateWindowEx(0, TOOLTIPS_CLASS, (LPSTR)NULL,

	        TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,

	        CW_USEDEFAULT, parent, (HMENU)NULL, ToolTipHInst, NULL);

    if (ttwe->tooltip_hwnd) {

		ttwe->parent_hwnd = parent;

		if (dttt) {

	    	// Add any dialog items we have tooltip text for.

			while (dttt->id) {

				TOOLINFO ti;

				zstruct(ti);

				ti.cbSize = sizeof(ti);

				ti.uFlags = TTF_IDISHWND;

				ti.hwnd = GetDlgItem(ttwe->parent_hwnd, dttt->id);

				ti.uId = (UINT)ti.hwnd;

				ti.lpszText = dttt->text;

				SendMessage(ttwe->tooltip_hwnd, TTM_ADDTOOL, 0, (LPARAM)&ti);

				dttt++;

			}

		}

    }

	return ttwe->tooltip_hwnd;

}



//*********************************************************

// https://github.com/kriskoin
//

// Close a ToolTip window opened with OpenToolTipWindow().

//

void CloseToolTipWindow(HWND tooltip_hwnd)

{

	if (!tooltip_hwnd) {

		return;	// nothing to do.

	}



	// Search for the tooltip_hwnd entry

	int i=0;

	struct ToolTipWindowEntry *ttwe = ToolTipWindowEntries;

	while (ttwe->tooltip_hwnd != tooltip_hwnd && i<MAX_TOOLTIP_WINDOWS) {

		i++;

		ttwe++;

	}

	if (i>=MAX_TOOLTIP_WINDOWS) {

		kp(("%s(%d) Warning: CloseToolTipWindow() could not find the requested window.",_FL));

		return;

	}



	DestroyWindow(ttwe->tooltip_hwnd);

	zstruct(*ttwe);

}



//*********************************************************

// https://github.com/kriskoin
//

// Add/Change tooltip text for an arbitrary rectangle in the

// parent window.  You must pass the tooltip hwnd, not the parent hwnd.

// If text==NULL, the tool tip will be removed.

// initialize_flag will be maintained to indicate whether the

// tool is initialized.

//

void SetToolTipText(HWND parent_hwnd, int control_id, char *text, int *initialized_flag)

{

	// Search for the tooltip_hwnd entry

	int i=0;

	struct ToolTipWindowEntry *ttwe = ToolTipWindowEntries;

	while (ttwe->parent_hwnd != parent_hwnd && i<MAX_TOOLTIP_WINDOWS) {

		i++;

		ttwe++;

	}

	if (i>=MAX_TOOLTIP_WINDOWS) {

		kp(("%s(%d) Warning: SetToolTipText() could not find the requested window.",_FL));

		return;

	}

	RECT r;

	zstruct(r);

	GetWindowRect(GetDlgItem(parent_hwnd, control_id), &r);

	ScreenToClient(parent_hwnd, &r);

	SetToolTipText(ttwe->tooltip_hwnd, control_id, &r, text, initialized_flag);

}



void SetToolTipText(HWND tooltip_hwnd, UINT uId, LPRECT rect, char *text, int *initialized_flag)

{

	// Search for the tooltip_hwnd entry

	int i=0;

	struct ToolTipWindowEntry *ttwe = ToolTipWindowEntries;

	while (ttwe->tooltip_hwnd != tooltip_hwnd && i<MAX_TOOLTIP_WINDOWS) {

		i++;

		ttwe++;

	}

	if (i>=MAX_TOOLTIP_WINDOWS) {

		kp(("%s(%d) Warning: SetToolTipText() could not find the requested window.",_FL));

		return;

	}



	TOOLINFO ti;

	zstruct(ti);

	ti.cbSize = sizeof(ti);

	ti.uId = uId;

	ti.hwnd = ttwe->parent_hwnd;

	ti.lpszText = text;

	ti.rect = *rect;

	//ti.uFlags = TTF_CENTERTIP;	// center horizontally but put at bottom vertically

  #if 0	// 2022 kriskoin

	if (uId==0) {

		kp(("-------\n"));

	}

	kp(("%s(%d) Rect #%d: %4d,%4d,%4d,%4d (%s)\n",

				_FL, uId, rect->left, rect->top, rect->right, rect->bottom, text));

  #endif

	if (text) {

		if (!*initialized_flag) {

			// Add it for the first time.

			SendMessage(tooltip_hwnd, TTM_ADDTOOL, 0, (LPARAM)&ti);

			*initialized_flag = TRUE;

		} else {

			// If the text has not changed, don't update it.

			char old_text[500];

			zstruct(old_text);

			TOOLINFO ti2 = ti;

			ti2.lpszText = old_text;

			SendMessage(tooltip_hwnd, TTM_GETTEXT, 0, (LPARAM)&ti2);

			if (strcmp(old_text, text)) {

				// It changed... update the tooltip...

			  	//kp(("%s(%d) Tooltip changed from '%s' to '%s'\n", _FL, old_text, text));

			  #if 1	// 2022 kriskoin

				SendMessage(tooltip_hwnd, TTM_UPDATETIPTEXT, 0, (LPARAM)&ti);

			  #else

				SendMessage(tooltip_hwnd, TTM_SETTOOLINFO, 0, (LPARAM)&ti);

			  #endif

			}

		}

	} else {

		// Remove the tool if it exists...

		if (*initialized_flag) {

			SendMessage(tooltip_hwnd, TTM_DELTOOL, 0, (LPARAM)&ti);

			*initialized_flag = FALSE;

		}

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Scale a single window's client area position and size.

//

static void ScaleWindowPos(HWND hwnd, LPPOINT numerator, LPPOINT denominator)

{

	RECT r;

	GetClientRect(hwnd, &r);	// get current client size



	// Calculate scaled client size

	int w = r.right - r.left;

	int h = r.bottom - r.top;

	int new_w = (w * numerator->x + denominator->x/2) / denominator->x;

	int new_h = (h * numerator->y + denominator->y/2) / denominator->y;



	RECT r2;

	GetWindowRect(hwnd, &r2);	// get original position and size (in screen coords)



	// If this is a child window, we want to do our position calculations

	// relative to the parent's client area.  GetWindowRect() always

	// returns screen coordinates, so a conversion is required.

	HWND parent = GetParent(hwnd);

	if (parent) {

		ScreenToClient(parent, (LPPOINT)&r2);

		ScreenToClient(parent, ((LPPOINT)&r2)+1);

	}



	// Adjust the window size so the client size is correct.

	new_w = (r2.right - r2.left) + (new_w - w);

	new_h = (r2.bottom - r2.top) + (new_h - h);



	// Scale the window position

	int new_x = (r2.left * numerator->x + denominator->x/2) / denominator->x;

	int new_y = (r2.top  * numerator->y + denominator->y/2) / denominator->y;



  #if 0	// 2022 kriskoin

	if (!parent) {

		kp(("%s(%d) new parent size = %dx%d (was %dx%d). new coords = %d,%d\n",_FL,new_w, new_h, w, h, new_x, new_y));

	}

  #endif



	// MoveWindow expects screen coords for a top level window and

	// parent window client coords for a child window.  This is what we've

	// been working with, so no further conversion is necessary.

	MoveWindow(hwnd, new_x, new_y, new_w, new_h, TRUE);



  #if 0	// 2022 kriskoin

	if (!parent) {

		RECT r3;

		GetWindowRect(hwnd, &r3);	// get original position and size (in screen coords)

		kp(("%s(%d) after scaling, parent size is %dx%d\n",

				_FL,

				(r3.right - r3.left),

				(r3.bottom - r3.top)));

	}	

  #endif

}



//*********************************************************

// https://github.com/kriskoin
//

// Scale a dialog box and all its controls to a standard 96 dpi

// size (typical for small fonts).  This can be used to adjust

// a dialog box which displays much larger on a Large font display

// (125 dpi) so that it appears exactly the same (pixel size wise)

// on all displays, regardless of their current font size and

// resolution settings.

// It should be called at the top of the WM_INITDIALOG handling.

//

void ScaleDialogTo96dpi(HWND hDlg)

{

	RECT r;

	zstruct(r);

	r.right = 200;

	r.bottom = 200;

	MapDialogRect(hDlg, &r);



	// define default numerators for 96dpi (200x200 DLUs)

	#define DEFAULT_X_NUMERATOR 300

	#define DEFAULT_Y_NUMERATOR 325

	POINT denom;

	//kp(("%s(%d) 200x200 DLU's come out to %dx%d pixels at this resolution.\n", _FL, r.right, r.bottom));

	denom.x = r.right;

	denom.y = r.bottom;

	if (denom.x == DEFAULT_X_NUMERATOR && denom.y == DEFAULT_Y_NUMERATOR) {

		// numerator and denominator are identical... we must be at the

		// desired size already.  There's nothing more to do.

		iLargeFontsMode = FALSE;

		return;

	}

	if (denom.x < DEFAULT_X_NUMERATOR) {

		//kp(("%s(%d) Small fonts mode.\n", _FL));

		iLargeFontsMode = FALSE;

	} else {

		//kp(("%s(%d) Large fonts mode.\n", _FL));

		iLargeFontsMode = TRUE;

	}



	POINT numerator;

	numerator.x = DEFAULT_X_NUMERATOR;

	numerator.y = DEFAULT_Y_NUMERATOR;

	ScaleWindowPos(hDlg, &numerator, &denom);



	// Do all child windows...

	EnumChildWindows(hDlg, ScaleChildTo96dpi, (LPARAM)&denom);



	//kriskoin: 	HDC hdc = GetDC(NULL);

	int screen_w = GetDeviceCaps(hdc, HORZRES);

	int screen_h = GetDeviceCaps(hdc, VERTRES);

	ReleaseDC(NULL, hdc);

	GetWindowRect(hDlg, &r);	// get current window size

	int w = r.right - r.left;

	int h = r.bottom - r.top;

	//kp(("%s(%d) centering window again (size is now %dx%d)\n", _FL, w, h));

	MoveWindow(hDlg, (screen_w - w) / 2, (screen_h - h) / 2, w, h, TRUE);

}



//*********************************************************

// https://github.com/kriskoin
//

// Scale a child window to 96dpi.  This is an internal callback

// function used by the ScaleDialogTo96dpi() function.

//

static BOOL CALLBACK ScaleChildTo96dpi(HWND hwnd, LPARAM lParam)

{

	POINT numerator;

	numerator.x = DEFAULT_X_NUMERATOR;

	numerator.y = DEFAULT_Y_NUMERATOR;

	ScaleWindowPos(hwnd, &numerator, (LPPOINT)lParam);

	return TRUE;	// continue enumeration

}



//*********************************************************

// https://github.com/kriskoin
//

// Set the size of a window to a particular number of screen pixels.

//

void SetWindowSize(HWND hwnd, int new_w, int new_h)

{

	RECT r;

	GetWindowRect(hwnd, &r);	// get original position and size (in screen coords)

	MoveWindow(hwnd, r.left, r.top, new_w, new_h, TRUE);

}	



//*********************************************************

// https://github.com/kriskoin
//

// Position a window so that it is on the screen (no part of

// it is off screen).

// If this is not possible (because the window is too large),

// then it is moved to the top or left (or both) to display

// as much as possible.

//

void WinPosWindowOnScreen(HWND hwnd)

{

	RECT r;

	if (GetWindowRect(hwnd, &r)) {

		RECT desktop;

		zstruct(desktop);

		SystemParametersInfo(SPI_GETWORKAREA, 0, (PVOID)&desktop, 0);

		pr(("%s(%d) Desktop window rect: %d,%d,%d,%d\n", _FL,

			desktop.left, desktop.top, desktop.right, desktop.bottom));

		RECT original_rect = r;

		if (r.right > desktop.right) {

			// Window goes off right side of desktop, slide it left to fit

			OffsetRect(&r, desktop.right - r.right, 0);

		}

		if (r.bottom > desktop.bottom) {

			// Window goes off bottom side of desktop, slide it up to fit

			OffsetRect(&r, 0, desktop.bottom - r.bottom);

		}

		if (r.left < desktop.left) {

			// Window goes off left side of desktop, slide it right to fit

			OffsetRect(&r, desktop.left - r.left, 0);

		}

		if (r.top < desktop.top) {

			// Window goes off top side of desktop, slide it down to fit

			OffsetRect(&r, 0, desktop.top - r.top);

		}

		if (memcmp((void *)&r, (void *)&original_rect, sizeof(r))) {

			// It moved... set the new position.

			MoveWindow(hwnd, r.left, r.top, r.right-r.left, r.bottom-r.top, TRUE);

		}

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Launch the default Internet Browser with a specified URL.

//

ErrorType LaunchInternetBrowser(char *url)

{

  #if 1	// 2022 kriskoin

	// Apparently ShellExecute() has a bug which can cause the

	// launched application to crash when exiting if any sort of

	// DDE conversation needs to start as a result of opening

	// the document.  SEE_MASK_FLAG_DDEWAIT fixes that.

	SHELLEXECUTEINFO sei;

	zstruct(sei);

	sei.cbSize = sizeof(sei);

	sei.fMask = SEE_MASK_FLAG_DDEWAIT;

	sei.lpVerb = "open";

	sei.lpFile = url;

	sei.nShow = SW_SHOWNORMAL;

	if (ShellExecuteEx(&sei)) {

		// success...

		return ERR_NONE;

	}

	return ERR_ERROR;

  #else

	HINSTANCE err = ShellExecute(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);

	if ((int)err <= 32) {

		return ERR_ERROR;

	}

	return ERR_NONE;

  #endif

}	



//*********************************************************

// https://github.com/kriskoin
//

// Set the text for a window if it has changed.  It not, don't do it.

// This is a direct replacement for SetWindowText().  It avoids

// unnecessary window redraws that can cause flickering.

// If the string is too long (more than 500 bytes?), it will always get set.

//

BOOL SetWindowTextIfNecessary(HWND hwnd, LPCTSTR lpString)

{

	if (!hwnd) {

		kp(("%s(%d) SetWindowTextIfNecessary($%08lx, '%s') was passed a null hwnd\n",_FL,hwnd,lpString));

		return FALSE;	// failure

	}

	#define TEST_STR_LEN	500

	char old_str[TEST_STR_LEN];

	memset(old_str, 0, TEST_STR_LEN);

	GetWindowText(hwnd, old_str, TEST_STR_LEN);

	if (!strcmp(old_str, lpString)) {

		// They match... no need to set it.

		return TRUE;	// success.

	}

	// They don't match... fall through to calling the regular routine.

	return SetWindowText(hwnd, lpString);

}



BOOL SetDlgItemTextIfNecessary(HWND hwnd, int control_id, LPCTSTR lpString)

{

	HWND hwnd2 = GetDlgItem(hwnd, control_id);

	if (!hwnd2) {

		kp(("%s(%d) SetDlgItemTextIfNecessary($%08lx, %d, '%s'): control_id %d does not exist!\n",

				_FL, hwnd, control_id, lpString, control_id));

		return FALSE;	// failure

	}



	return SetWindowTextIfNecessary(hwnd2, lpString);

}



//*********************************************************

// https://github.com/kriskoin
//

// Replace any &'s with && so they do not get interpreted by

// Windows when displaying in a text control.

//

char *DelimitStringForTextControl(char *src, char *dest, int max_dest_str_len)

{

	char *result = dest;

	char *max_dest = dest + max_dest_str_len - 1;

	while (*src && dest < max_dest) {

		if (*src=='&') {

			if (dest < max_dest) {

				*dest++ = '&';

			}

		}

		*dest++ = *src++;

	}

	*dest = 0;

	return result;

}



//*********************************************************

// https://github.com/kriskoin
//

// Calculate an intermediate color between two other colors

//

COLORREF CalculateIntermediateColor(COLORREF start_color, COLORREF end_color, int position, int position_range)

{

	int base_r = (start_color & 0x0000FF);

	int base_g = (start_color & 0x00FF00) >> 8;

	int base_b = (start_color & 0xFF0000) >> 16;

	int dr =  (end_color & 0x0000FF)        - base_r;

	int dg = ((end_color & 0x00FF00) >> 8)  - base_g;

	int db = ((end_color & 0xFF0000) >> 16) - base_b;

	COLORREF result = RGB(	(dr * position) / position_range + base_r,

							(dg * position) / position_range + base_g,

							(db * position) / position_range + base_b);

	pr(("%s(%d) CalculateIntermediateColor($%08lx, $%08lx, %d, %d) = $%08lx\n",

			_FL, start_color, end_color, position, position_range, result));

	return result;

}



//*********************************************************

// https://github.com/kriskoin
//

// Retrieve the text from a dialog window and turn it into an integer.

//

int GetDlgTextInt(HWND hDlg, int control_id)

{

	char str[30];

	zstruct(str);

	GetWindowText(GetDlgItem(hDlg, control_id), str, 30-1);

	return atoi(str);

}



//*********************************************************

// https://github.com/kriskoin
//

// Retrieve the text from a dialog window and turn it into a float (double)

//

double GetDlgTextFloat(HWND hDlg, int control_id)

{

	char str[30];

	zstruct(str);

	GetWindowText(GetDlgItem(hDlg, control_id), str, 30-1);

	return atof(str);

}



//*********************************************************

// https://github.com/kriskoin
//

// Retrieve the text from a dialog window (a hex string) and

// turn it into an WORD32.

//

WORD32 GetDlgTextHex(HWND hDlg, int control_id)

{

	char str[30];

	zstruct(str);

	GetWindowText(GetDlgItem(hDlg, control_id), str, 30-1);

	WORD32 result = 0;

	sscanf(str, "%x", &result);

	return result;

}



//*********************************************************

// https://github.com/kriskoin
//

// Retrieve the text from a dialog window.  Zeroes entire dest

// string before fetching.  Returns # of characters fetched.

//

int GetDlgText(HWND hDlg, int control_id, char *dest_str, int dest_str_len)

{

	memset(dest_str, 0, dest_str_len);

	int result = GetWindowText(GetDlgItem(hDlg, control_id), dest_str, dest_str_len);

	dest_str[dest_str_len-1] = 0;	// make sure it's always null terminated.

	return result;

}



//*********************************************************

// https://github.com/kriskoin
//

// Enable a window if it is not already enabled.

//

BOOL EnableWindowIfNecessary(HWND hwnd, BOOL enable_flag)

{

	BOOL current_state = IsWindowEnabled(hwnd);

	if (current_state != enable_flag) {

		EnableWindow(hwnd, enable_flag);

	}

	return !current_state;	// return TRUE if it was previouly disabled.

}



//*********************************************************

// https://github.com/kriskoin
//

// Call ShowWindow() if the show cmd does not match the current

// window's show command (SW_HIDE, SW_SHOW, SW_SHOWNA, etc.)

// This function is designed for dialog box items, not for

// top level windows.  It may not handle some of the less

// common SW_* commands.

//

BOOL ShowWindowIfNecessary(HWND hwnd, int sw_show_cmd)

{

	BOOL previously_visible = IsWindowVisible(hwnd) ? TRUE : FALSE;

	int visibility_desired = TRUE;

	if (sw_show_cmd == SW_HIDE) {

		visibility_desired = FALSE;

	}

	if (visibility_desired != previously_visible) {

		previously_visible = ShowWindow(hwnd, sw_show_cmd);

	}

	return previously_visible;	// return TRUE if it was previouly visible

}



//*********************************************************

// https://github.com/kriskoin
//

// Add an item to a proximity highlight structure

//

void AddProximityHighlight(struct ProximityHighlightingInfo *phi, HWND hwnd)

{

	if (phi->iWindowCount < MAX_PROXIMITY_HIGHLIGHTING_WINDOWS) {

		phi->hWindowList[phi->iWindowCount] = hwnd;

		phi->iWindowCount++;

		EnableWindow(hwnd, FALSE);	// prevent it from capturing until we want it to

	} else {

		kp(("%s(%d) Error: Proximity highlight list is full.\n",_FL));

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// Try to find a top level window that is under a point.

// Ignore all hidden windows and child windows.

// This is what I would have expected WindowFromPoint() to return,

// but it seems to skip windows which are in front of us.

//

HWND TopLevelWindowFromPoint(POINT pt)

{

	HWND hwnd = GetTopWindow(NULL);

	while (hwnd != NULL) {

		if (IsWindowVisible(hwnd)) {

			RECT r;

			zstruct(r);

			GetClientRect(hwnd, &r);

			POINT ptc = pt;

			ScreenToClient(hwnd, &ptc);	// ptc should contain the point in client coordinates

			if (PtInRect(&r, ptc)) {

				break;	// found it.

			}

		}

		hwnd = GetNextWindow(hwnd, GW_HWNDNEXT);

	}

	return hwnd;

}



//*********************************************************

// https://github.com/kriskoin
//

// Update the tracking for the mouse proximity highlighting

// on specified windows.  This function is normally called

// during the processing of WM_MOUSEMOVE messages.

// Mouse coordinates are relative to the client area of the parent window.

//

void UpdateMouseProximityHighlighting(struct ProximityHighlightingInfo *phi, HWND hwnd, POINTS pts)

{

	POINT pt;

	pt.x = pts.x;

	pt.y = pts.y;

	HWND new_match_hwnd = NULL;



	POINT screen_point = pt;

	ClientToScreen(hwnd, &screen_point);



	// If the mouse is within our parent window, capture it.  If not,

	// release capture if necessary.

	RECT r;

	zstruct(r);

	GetClientRect(hwnd, &r);

	HWND capture_hwnd = NULL;	// set if we should capture

	//kp(("%s(%d) mouse = %4d,%4d, parent window = %d,%d,%d,%d\n",_FL,pt.x,pt.y,r.left,r.top,r.right,r.bottom));

	// Make sure we're active, the mouse is within our bounds, and there isn't

	// a topmost window in front of us that the mouse is actually within.

	//kp(("%s(%d)         WindowFromPoint(%d,%d) = $%08lx\n", _FL, screen_point.x, screen_point.y, WindowFromPoint(screen_point)));

	//kp(("%s(%d) TopLevelWindowFromPoint(%d,%d) = $%08lx (our hwnd = $%08lx, our parent hwnd = $%08lx)\n", _FL, screen_point.x, screen_point.y, TopLevelWindowFromPoint(screen_point), hwnd, GetParent(hwnd)));



	if (hwnd==GetActiveWindow() && PtInRect(&r, pt) && hwnd==TopLevelWindowFromPoint(screen_point)) {

		// Nobody is in front of us, we're active, and it's within our bounds.

		// Set capture if necessary.

		// We want to capture our background (parent) window as well as

		// any child windows that have proximity highlighting enabled.

		// We should release capture whenever we move over any other child window.

		capture_hwnd = hwnd;

		new_match_hwnd = ChildWindowFromPointEx(hwnd, pt, CWP_SKIPINVISIBLE);

		pr(("%s(%d) new_match_hwnd = $%08lx, passed hwnd = $%08lx\n", _FL, new_match_hwnd, hwnd));

		if (new_match_hwnd) {

			// If it's one of ours, capture it.

			if (new_match_hwnd==hwnd) {

				pr(("%s(%d) Mouse is over the parent\n", _FL));

			} else {

				for (int i=0 ; i<phi->iWindowCount ; i++) {

					if (new_match_hwnd==phi->hWindowList[i]) {

						pr(("%s(%d) Mouse is over proximity window #%d\n", _FL, i));

					  #if 0	// 2022 kriskoin

						capture_hwnd = new_match_hwnd;	// switch to capturing child window mouse movement

					  #else

						capture_hwnd = NULL;

					  #endif

						break;

					}

				}

				if (i >= phi->iWindowCount) {

					pr(("%s(%d) Mouse is over someone else's child window.\n", _FL));

					capture_hwnd = NULL;

				}

			}

		}

	}

	//kp(("%s(%d) result: capture_hwnd = $%08lx (current is $%08lx)\n", _FL, capture_hwnd, phi->hCapturedWindow));

	if (capture_hwnd) {

		if (phi->hCapturedWindow != capture_hwnd) {

			if (phi->hCapturedWindow) {	// we need to release old capture first?

				ReleaseCapture();

			}

			phi->hCapturedWindow = capture_hwnd;

			//kp(("%s(%d) **** Calling SetCapture($%08lx)\n", _FL, capture_hwnd));

			SetCapture(capture_hwnd);

		}

	} else if (phi->hCapturedWindow) {

		//kp(("%s(%d) **** Calling ReleaseCapture()\n", _FL));

		ReleaseCapture();

		phi->hCapturedWindow = NULL;

	}



	if (new_match_hwnd != phi->hCurrentHighlight) {

		// The highlight has changed.  Invalidate the old window if necessary

		if (phi->hCurrentHighlight) {

			HWND h = phi->hCurrentHighlight;

			phi->hCurrentHighlight = 0;

			// If it's one of ours, invalidate it

			for (int i=0 ; i<phi->iWindowCount ; i++) {

				if (h==phi->hWindowList[i]) {

					// It's one of ours.

					InvalidateRect(h, NULL, FALSE);

					pr(("%s(%d) Disabling proximity window #%d (was %s)\n",

							_FL, i, IsWindowEnabled(h) ? "enabled" : "disabled"));

					EnableWindow(h, FALSE);	// prevent it from capturing

					break;

				}

			}

		}

		// Now make the new one highlighted.

		phi->hCurrentHighlight = new_match_hwnd;

		if (new_match_hwnd) {	// If there's a new one, it needs redrawing.

			// If it's one of ours, invalidate it

			for (int i=0 ; i<phi->iWindowCount ; i++) {

				if (new_match_hwnd==phi->hWindowList[i]) {

					// It's one of ours.

					InvalidateRect(new_match_hwnd, NULL, FALSE);

					pr(("%s(%d) Enabling proximity window #%d (was %s)\n",

							_FL, i, IsWindowEnabled(phi->hCurrentHighlight) ? "enabled" : "disabled"));

					EnableWindow(phi->hCurrentHighlight, TRUE);	// allow it to capture

					break;

				}

			}

		}

	}

}



//*********************************************************

// https://github.com/kriskoin
//

// A better SetForegroundWindow() (i.e. one that really works under Win98/2000).

// From July 2000 Windows Developer Journal, Tech Tips.

//

void ReallySetForegroundWindow(HWND hwnd)

{

	DWORD foregroundThreadID;	// foreground window thread

	DWORD ourThreadID;			// our active thread



	// If the window is in a minimized state, maximize now.

	if (GetWindowLong(hwnd, GWL_STYLE) & WS_MINIMIZE) {

		ShowWindow(hwnd, SW_MAXIMIZE);

		UpdateWindow(hwnd);

	}



	// Check to see if we are the foreground thread

	foregroundThreadID = GetWindowThreadProcessId(GetForegroundWindow(), NULL);

	ourThreadID = GetCurrentThreadId();



	// If not, attach our thread's input to the foreground thread's

	if (foregroundThreadID != ourThreadID) {

		AttachThreadInput(foregroundThreadID, ourThreadID, TRUE);

	}



	// Bring our window to the foreground

	SetForegroundWindow(hwnd);



	// If we attached our thread, detach it now.

	if (foregroundThreadID != ourThreadID) {

		AttachThreadInput(foregroundThreadID, ourThreadID, FALSE);

	}



	// Force our window to redraw

	InvalidateRect(hwnd, NULL, TRUE);

}