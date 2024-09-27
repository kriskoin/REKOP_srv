//****************************************************************
// https://github.com/kriskoin//
// WinBlit.cpp : Misc. blitting related tools for Windows
//
//****************************************************************

#define DISP 0

#define WIN32_LEAN_AND_MEAN		// Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include "pplib.h"

#if 0	//kriskoin: // This is a BAD hack... apparently these are no longer in the header files!
#ifndef C1_TRANSPARENT
  #define NEWTRANSPARENT  3
  #define CAPS1                   94              // other caps
  #define C1_TRANSPARENT          0x0001          // new raster cap
#endif
#endif

#define PALETTE_TESTING	0

#if PALETTE_TESTING
HPALETTE hBlitPalette;
#endif

// Version notes:
//  1.07-3 didn't do any caching - no real problems but kinda slow
//  1.07-4 did mask cache and hdc caching - some people had problems
//  1.07-5 did mask cache but no hdc caching - no reports yet

#define INCL_MASK_CACHE	1		// enable pre-computing and caching of bitmap masks?
#define INCL_HDC_CACHE	0		// cache hDC's?

#if INCL_MASK_CACHE
#define MAX_CACHED_MASKS	400		// max # of cached entries (we probably only need 150)

int iMaskCacheEntryCount;	// # of items currently in the MaskCacheEntries[] array
struct MaskCacheEntry {
	HBITMAP src_hbitmap;	// HBITMAP of the original bitmap
	HBITMAP hbmMaskedSrc;
	HDC     hdcMaskedSrc;
	HBITMAP hbmMaskedSrcOld;
	HDC		hdcInverseMask;
	HBITMAP	hbmInverseMask;
	HBITMAP	hbmInverseMaskOld;
	int		w,h;
	WORD32  last_used;		// GetTickCount() when this entry was last used.
} MaskCacheEntries[MAX_CACHED_MASKS];

//*********************************************************
// https://github.com/kriskoin//
// Free up the stuff in a MaskCacheEntry
//
void MaskCache_FreeEntry(struct MaskCacheEntry *e)
{
	if (e->hdcInverseMask) {
		// hdc still exists...
		SelectObject(e->hdcInverseMask, e->hbmInverseMaskOld);
		DeleteDC(e->hdcInverseMask);
	}
	DeleteObject(e->hbmInverseMask);

	if (e->hdcMaskedSrc) {
		// hdc still exists...
		SelectObject(e->hdcMaskedSrc, e->hbmMaskedSrcOld);
		DeleteDC(e->hdcMaskedSrc);
	}
	DeleteObject(e->hbmMaskedSrc);
	zstruct(*e);
}

//*********************************************************
// https://github.com/kriskoin//
// Find (or create) a mask for the passed HBITMAP
//
struct MaskCacheEntry *MaskCache_FindMask(HBITMAP src_hbitmap, HDC dest_hdc)
{
	if (src_hbitmap == NULL) {
		return NULL;
	}

	struct MaskCacheEntry *e = MaskCacheEntries;
	// First step... see if we've got it handy.
	for (int i=0 ; i<iMaskCacheEntryCount ; i++, e++) {
		if (e->src_hbitmap == src_hbitmap) {
			// Found it!
			e->last_used = GetTickCount();
			return e;
		}
	}

	// We didn't find it... look for somewhere to put it.
	if (iMaskCacheEntryCount < MAX_CACHED_MASKS) {
		// put it in the next empty slot.
		e = MaskCacheEntries + iMaskCacheEntryCount++;
	} else {
		// cache table is full... delete the oldest
		e = MaskCacheEntries;
		struct MaskCacheEntry *t = e;
		for (i=0 ; i<MAX_CACHED_MASKS ; i++, t++) {
			if (t->last_used < e->last_used) {
				e = t;	// found an older one
			}
		}
		// Free up e (the oldest one)
		MaskCache_FreeEntry(e);	
	}

	// Create a new mask and put it in slot e
	// 	1)	pre-compute the inverse mask (monochrome with 1's where the
	//		background needs to show through and 0's elsewhere)
	//	2)	pre-compute a 'masked source' copy of the source bitmap
	//		and put 0's in the areas which should be transparent.

	//kp(("%s(%d) Creating a new MaskCache entry (#%d) for bitmap $%08lx\n", _FL, e-MaskCacheEntries, src_hbitmap));

	BITMAP bm;
	zstruct(bm);
	GetObject(src_hbitmap, sizeof(BITMAP), (LPSTR)&bm);	// fetch dimensions of original
	e->src_hbitmap = src_hbitmap;
	e->w = bm.bmWidth;
	e->h = bm.bmHeight;
	e->last_used = GetTickCount();

	HDC 	hdcMask    = NULL;
	HBITMAP hbmMask    = NULL;
	HBITMAP hbmMaskOld = NULL;

	// Make a copy of the source bitmap
	e->hbmMaskedSrc = DuplicateHBitmap(src_hbitmap);
	e->hdcMaskedSrc = CreateCompatibleDC(dest_hdc);
	if (!e->hdcMaskedSrc) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	e->hbmMaskedSrcOld = (HBITMAP)SelectObject(e->hdcMaskedSrc, e->hbmMaskedSrc);

	// Retrieve the top left pixel from the source bitmap and use
	// it as the transparent color.
	COLORREF transparent_color = GetPixel(e->hdcMaskedSrc, 0, 0);

	// Set the background color of the source DC to the color.
	// contained in the parts of the bitmap that should be transparent
	SetBkColor(e->hdcMaskedSrc, transparent_color);

	// Create a monochrome bitmap to hold the mask
	e->hdcInverseMask = CreateCompatibleDC(dest_hdc);
	if (!e->hdcInverseMask) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	e->hbmInverseMask = CreateBitmap(e->w, e->h, 1, 1, NULL);
	if (!e->hbmInverseMask) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	e->hbmInverseMaskOld = (HBITMAP)SelectObject(e->hdcInverseMask, e->hbmInverseMask);

	// Create the object mask for the bitmap by performing a BitBlt
	// from the duplicated source bitmap (color) to a monochrome bitmap.
	// *** note: in 256 color mode this is producing solid black which
	// causes masking to work incorrectly in that mode.
	BOOL success = BitBlt(e->hdcInverseMask, 0, 0, e->w, e->h, e->hdcMaskedSrc, 0, 0, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	// hbmInverseMask is now complete (it contains 1's everywhere that
	// should be transparent).

	// Now we need a normal mask (inverse of the inverse mask) so we can do a
	// SRCAND blit to the MaskedSrc and finish it.
	// Create a monochrome bitmap to hold the mask
	hdcMask = CreateCompatibleDC(dest_hdc);
	if (!hdcMask) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	hbmMask = CreateBitmap(e->w, e->h, 1, 1, NULL);
	if (!hbmMask) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	hbmMaskOld = (HBITMAP)SelectObject(hdcMask, hbmMask);
	success = BitBlt(hdcMask, 0, 0, e->w, e->h, e->hdcInverseMask, 0, 0, NOTSRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	// hbmMask is now complete (it contains 0's everywhere that
	// should be transparent).

	// Now use that mask to finish off hbmMaskedSrc
	SetBkColor(e->hdcMaskedSrc, RGB(255,255,255));	// set to something predictable
	success = BitBlt(e->hdcMaskedSrc, 0, 0, e->w, e->h, hdcMask, 0, 0, SRCAND);
	// hbmMaskedSrc is now complete.

	DeleteObject(SelectObject(hdcMask, hbmMaskOld));
	DeleteDC(hdcMask);
	return e;
}

//*********************************************************
// https://github.com/kriskoin//
// Free all cached mask bitmaps
//
void MaskCache_FreeAll(void)
{
	for (int i=0 ; i<iMaskCacheEntryCount ; i++) {
		if (MaskCacheEntries[i].src_hbitmap) {	// is this entry used?
			MaskCache_FreeEntry(&MaskCacheEntries[i]);
		}
	}
	iMaskCacheEntryCount = 0;	// nothing left in the array, reset the counter.
}
#endif //INCL_MASK_CACHE

//*********************************************************
// https://github.com/kriskoin//
// Blit an HBITMAP with transparency into a DC given a dest point.
// This routine is not particularly fast because it must create
// a mask first.  The transparent color is whatever the top left pixel is.
//
// Note: NT5 and Win98 have a new function called TransparentBlt() which
// is very similar.  It's probably faster than this brain-dead method and
// therefore it could probably be used on those platforms.
//
// 2000/02/16MB:
// Also, this is the function which does almost all of the main drawing.
// It is used for cards, chips, player id boxes, buttons, and anything
// else that has transparent bits to it.  If anything needs optimizing,
// this is it.  Since animations slow considerably when the table starts
// to fill up with cards (e.g. in 7CS), we can assume that this function
// could definitely use some optimizing.
//
void BlitBitmapToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt)
{
	if (!dest_hdc) {
		return;	// nowhere to draw.
	}

	if (!src_hbitmap) {
		kp(("%s(%d) BlitBitmapToDC(): src_hbitmap = NULL\n",_FL));
		return;	// nothing to draw.
	}

#if INCL_MASK_CACHE	//kriskoin: 	// Overview of blitting method:
	// 	1)	pre-compute the inverse mask (monochrome with 1's where the
	//		background needs to show through and 0's elsewhere)
	//	2)	pre-compute a 'masked source' copy of the source bitmap
	//		and put 0's in the areas which should be transparent.
	//	3)	AND-draw the inverse mask to the destination
	//	4)	OR-draw the masked source bitmap to the destination.

	struct MaskCacheEntry *e = MaskCache_FindMask(src_hbitmap, dest_hdc);
	if (e) {
		if (!e->hdcMaskedSrc) {
			e->hdcMaskedSrc = CreateCompatibleDC(dest_hdc);
			if (!e->hdcMaskedSrc) {
				kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
			}
			e->hbmMaskedSrcOld = (HBITMAP)SelectObject(e->hdcMaskedSrc, e->hbmMaskedSrc);
		}

		if (!e->hdcInverseMask) {
			// Create a monochrome bitmap to hold the mask
			e->hdcInverseMask = CreateCompatibleDC(dest_hdc);
			if (!e->hdcInverseMask) {
				kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
			}
			e->hbmInverseMaskOld = (HBITMAP)SelectObject(e->hdcInverseMask, e->hbmInverseMask);
		}

		BitBlt(dest_hdc, dest_pt->x, dest_pt->y, e->w, e->h, e->hdcInverseMask, 0, 0, SRCAND);
		BitBlt(dest_hdc, dest_pt->x, dest_pt->y, e->w, e->h, e->hdcMaskedSrc,   0, 0, SRCPAINT);

	  #if !INCL_HDC_CACHE
	  	// Don't cache hDC's... free them up asap.
		if (e->hdcInverseMask) {
			// hdc still exists...
			SelectObject(e->hdcInverseMask, e->hbmInverseMaskOld);
			e->hbmInverseMaskOld = 0;
			DeleteDC(e->hdcInverseMask);
			e->hdcInverseMask = 0;
		}
		if (e->hdcMaskedSrc) {
			// hdc still exists...
			SelectObject(e->hdcMaskedSrc, e->hbmMaskedSrcOld);
			e->hbmMaskedSrcOld = 0;
			DeleteDC(e->hdcMaskedSrc);
			e->hdcMaskedSrc = 0;
		}
	  #endif

	  #if 0
		// forget about things (leak resources and memory like crazy)
		kp1(("%s(%d) **** Leaking memory and other resources with every blit! ****\n",_FL));
		zstruct(*e);
	  #endif
	}
#else	// !INCL_MASK_CACHE
	// --------------- old method ------------------
  #if 0	// these do NOT seem widely supported
	static int tested_new_transparent = FALSE;
	static int new_transparent_supported = FALSE;
	if (!tested_new_transparent) {
		if (GetDeviceCaps(dest_hdc, CAPS1) & C1_TRANSPARENT) {
			kp(("%s(%d) NEWTRANSPARENT blits ARE supported by this driver.\n",_FL));
			new_transparent_supported = TRUE;
		} else {
			kp(("%s(%d) NEWTRANSPARENT blits are NOT supported by this driver.\n",_FL));
		}
		tested_new_transparent = TRUE;
	}
  #endif

	//kp(("%s(%d) ---------------- start of BlitBitmapToDC() ------------------\n",_FL));
	// See Knowledgebase article Q79212 for more details.
	BITMAP     bm;
	COLORREF   cColor;
	HBITMAP    bmAndBack, bmAndObject, bmAndMem, bmSave;
	HBITMAP    bmBackOld, bmObjectOld, bmMemOld, bmSaveOld;
	HDC        hdcMem, hdcBack, hdcObject, hdcTemp, hdcSave;
	POINT      ptSize;

	hdcTemp = CreateCompatibleDC(dest_hdc);
	if (!hdcTemp) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
		return;
	}
	SelectObject(hdcTemp, src_hbitmap);   // Select the source bitmap

  #if PALETTE_TESTING && 0
	if (hBlitPalette) {
		SelectPalette(hdcTemp, hBlitPalette, FALSE);
		kp(("%s(%d) Selecting palette $%08lx\n",_FL));
	}
	kp(("%s(%d) pixel (0,0) from original bitmap = $%06lx\n",_FL, GetPixel(hdcTemp, 0, 0)));
  #endif

	zstruct(bm);
	int result = GetObject(src_hbitmap, sizeof(BITMAP), (LPSTR)&bm);
	if (!result) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
		DeleteDC(hdcTemp);
		return;
	}
	ptSize.x = bm.bmWidth;            // Get width of bitmap
	ptSize.y = bm.bmHeight;           // Get height of bitmap
	DPtoLP(hdcTemp, &ptSize, 1);      // Convert from device to logical points

	// Make a copy of the original bitmap so we don't destroy it.
	hdcSave   = CreateCompatibleDC(dest_hdc);
	if (!hdcSave) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
  #if 1	// 2022 kriskoin
	bmSave = CreateCompatibleBitmap(dest_hdc, ptSize.x, ptSize.y);
  #else
	bmSave = CreateBitmap(ptSize.x, ptSize.y, bm.bmPlanes, bm.bmBitsPixel, NULL);
  #endif
	if (!bmSave) {
		kp(("%s(%d) BlitBitmapToDC() had a failure (requested size=%dx%d). bm.size=%dx%d. GetLastError()=%d\n", _FL, ptSize.x, ptSize.y, bm.bmWidth, bm.bmHeight, GetLastError()));
	}
 
	bmSaveOld = (HBITMAP)SelectObject(hdcSave, bmSave);
  #if PALETTE_TESTING && 0
	if (hBlitPalette) {
		SelectPalette(hdcSave, hBlitPalette, FALSE);
	}
  #endif
	SetMapMode(hdcTemp, GetMapMode(dest_hdc));
	BOOL success = BitBlt(hdcSave, 0, 0, ptSize.x, ptSize.y, hdcTemp, 0, 0, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BitBlt() failed. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Now we're done with the original bitmap and dc
	DeleteDC(hdcTemp);
	hdcTemp = 0;

	// Create some DCs to hold temporary data.
	hdcBack   = CreateCompatibleDC(dest_hdc);
	if (!hdcBack) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	hdcObject = CreateCompatibleDC(dest_hdc);
	if (!hdcObject) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	hdcMem    = CreateCompatibleDC(dest_hdc);
	if (!hdcMem) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Create a bitmap for each DC. DCs are required for a number of
	// GDI functions.
	bmAndBack   = CreateBitmap(ptSize.x, ptSize.y, 1, 1, NULL);
	if (!bmAndBack) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	bmAndObject = CreateBitmap(ptSize.x, ptSize.y, 1, 1, NULL);
	if (!bmAndObject) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	bmAndMem    = CreateCompatibleBitmap(dest_hdc, ptSize.x, ptSize.y);
	if (!bmAndMem) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

  #if 0	// 2022 kriskoin
	//BITMAP bm;
	zstruct(bm);
	GetObject(src_hbitmap, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) src_hbitmap size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
	GetObject(bmAndBack, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) bmAndBack   size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
	GetObject(bmAndObject, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) bmAndObject size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
	GetObject(bmAndMem, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) bmAndMem    size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
	GetObject(bmSave, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) bmSave      size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
  #endif

	// Each DC must select a bitmap object to store pixel data.
	bmBackOld   = (HBITMAP)SelectObject(hdcBack,   bmAndBack);		// monochrome
	bmObjectOld = (HBITMAP)SelectObject(hdcObject, bmAndObject);	// monochrome
	bmMemOld    = (HBITMAP)SelectObject(hdcMem,    bmAndMem);		// color

  #if PALETTE_TESTING && 0
	if (hBlitPalette) {
		//SelectPalette(hdcBack, hBlitPalette, FALSE);
		//SelectPalette(hdcObject, hBlitPalette, FALSE);
		SelectPalette(hdcMem, hBlitPalette, FALSE);
	}
  #endif

	// Retrieve the top left pixel from the source bitmap and use
	// it as the transparent color.
	COLORREF transparent_color = GetPixel(hdcSave, 0, 0);
  #if PALETTE_TESTING
	kp(("%s(%d) Transparent_color from hdcSave = $%06lx\n", _FL, transparent_color));
  #endif

	// Set the background color of the source DC to the color.
	// contained in the parts of the bitmap that should be transparent
	cColor = SetBkColor(hdcSave, transparent_color);

  #if 0	// 2022 kriskoin
	kp(("%s(%d) Showing just hdcSave (a copy of the original)\n",_FL));
	BitBlt(dest_hdc, dest_pt->x, dest_pt->y, ptSize.x, ptSize.y, hdcSave, 0, 0, SRCCOPY);
	goto done;
  #endif

	// Create the object mask for the bitmap by performing a BitBlt
	// from the source bitmap (color) to a monochrome bitmap.
	// !!!!!!!! this is producing solid black in 256 color mode.  Therein lies the problem.
	success = BitBlt(hdcObject, 0, 0, ptSize.x, ptSize.y, hdcSave, 0, 0, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

  #if 0	// 2022 kriskoin
	kp(("%s(%d) Showing just hdcObject (the mask)\n",_FL));
	BitBlt(dest_hdc, dest_pt->x, dest_pt->y, ptSize.x, ptSize.y, hdcObject, 0, 0, SRCCOPY);
	goto done;
  #endif


  #if 0	//kriskoin: 	// Set the background color of the source DC back to the original
	// color.
	//kp(("%s(%d) Setting hdcSave background back to $%06lx\n", _FL, cColor));
	SetBkColor(hdcSave, cColor);
  #else
	SetBkColor(hdcSave, RGB(255,255,255));	// set to something predictable
  #endif

	// Create the inverse of the object mask.
	// hdcObject is the inverse of the image mask.
	// It's 1x1 monochrome and is white where/ the object is transparent
	// and black where the object should show through.
	success = BitBlt(hdcBack, 0, 0, ptSize.x, ptSize.y, hdcObject, 0, 0, NOTSRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Copy the background of the main DC to the destination.
	success = BitBlt(hdcMem, 0, 0, ptSize.x, ptSize.y, dest_hdc, dest_pt->x, dest_pt->y, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Mask out the places where the bitmap will be placed.
	success = BitBlt(hdcMem, 0, 0, ptSize.x, ptSize.y, hdcObject, 0, 0, SRCAND);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Mask out the transparent colored pixels on the bitmap.
	success = BitBlt(hdcSave, 0, 0, ptSize.x, ptSize.y, hdcBack, 0, 0, SRCAND);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// XOR the bitmap with the background on the destination DC.
	success = BitBlt(hdcMem, 0, 0, ptSize.x, ptSize.y, hdcSave, 0, 0, SRCPAINT);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	// Copy the destination to the screen (or offscreen destination hdc)
	success = BitBlt(dest_hdc, dest_pt->x, dest_pt->y, ptSize.x, ptSize.y, hdcMem, 0, 0, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BlitBitmapToDC() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

//done:
	// Delete the memory bitmaps.
	DeleteObject(SelectObject(hdcBack,   bmBackOld));
	DeleteObject(SelectObject(hdcObject, bmObjectOld));
	DeleteObject(SelectObject(hdcMem,    bmMemOld));
	DeleteObject(SelectObject(hdcSave,   bmSaveOld));

	// Delete the memory DCs.
	DeleteDC(hdcMem);
	DeleteDC(hdcBack);
	DeleteDC(hdcObject);
	DeleteDC(hdcSave);
#endif	// old method
}

//*********************************************************
// https://github.com/kriskoin//
// Blit an HBITMAP without transparency into a DC given a dest point.
//
#define TIME_DRAWING	(DEBUG && 0)
void BlitBitmapToDC_nt(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt)
{
	if (!src_hbitmap) {
		kp(("%s(%d) BlitBitmapToDC_nt() called with NULL src_hbitmap\n", _FL));
		return;
	}
	if (!dest_hdc) {
		kp(("%s(%d) BlitBitmapToDC_nt() called with NULL dest_hdc\n", _FL));
		return;
	}

  #if TIME_DRAWING
	WORD32 tsc0 = (WORD32)rdtsc();
  #endif

	HDC hdcTemp = CreateCompatibleDC(dest_hdc);
	if (!hdcTemp) {
		kp(("%s(%d) BlitBitmapToDC_nt() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

  #if TIME_DRAWING
	WORD32 tsc1 = (WORD32)rdtsc();
  #endif

	BITMAP bm;
	zstruct(bm);
	SelectObject(hdcTemp, src_hbitmap);   // Select the source bitmap
	GetObject(src_hbitmap, sizeof(BITMAP), (LPSTR)&bm);
	//kp(("%s(%d) Non-transparent: src_hbitmap size = %dx%dx%d\n", _FL, bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
	POINT ptSize;
	zstruct(ptSize);
	ptSize.x = bm.bmWidth;            // Get width of bitmap
	ptSize.y = bm.bmHeight;           // Get height of bitmap
	DPtoLP(hdcTemp, &ptSize, 1);      // Convert from device to logical points

  #if TIME_DRAWING
	WORD32 tsc2 = (WORD32)rdtsc();
  #endif
	pr(("%s(%d) BitBlt parameters: $%08lx, dest=(%d,%d), size=(%d,%d), $%08lx, 0, 0, SRCCOPY)\n",
				_FL, dest_hdc, dest_pt->x, dest_pt->y, ptSize.x, ptSize.y, hdcTemp));
	BOOL success = BitBlt(dest_hdc, dest_pt->x, dest_pt->y, ptSize.x, ptSize.y, hdcTemp, 0, 0, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BitBlt() failed. GetLastError()=%d\n", _FL, GetLastError()));
	}
  #if TIME_DRAWING
	WORD32 tsc3 = (WORD32)rdtsc();
  #endif
	DeleteDC(hdcTemp);

  #if TIME_DRAWING
	WORD32 now = (WORD32)rdtsc();
	WORD32 elapsed_tsc1 = tsc1 - tsc0;
	WORD32 elapsed_tsc2 = tsc2 - tsc1;
	WORD32 elapsed_tsc3 = tsc3 - tsc2;
	#define TSC_DIVISOR	10000
	kp(("%s(%d) BlitBitmapToDC_nt timing: %5u/%5u/%5u/%5u (total %5u)\n",
			_FL,
			elapsed_tsc1 / TSC_DIVISOR,
			elapsed_tsc2 / TSC_DIVISOR,
			elapsed_tsc3 / TSC_DIVISOR,
			(now - tsc3) / TSC_DIVISOR,
			(now - tsc0) / TSC_DIVISOR));
  #endif
}

// Same as BlitBitmapToDC_nt but pass (0,0) as a dest point.
void BlitBitmapToDC_nt(HBITMAP src_hbitmap, HDC dest_hdc)
{
	POINT pt;
	zstruct(pt);
	BlitBitmapToDC_nt(src_hbitmap, dest_hdc, &pt);
}	

//*********************************************************
// https://github.com/kriskoin//
// Blit a non-transparent rectangle from a source bitmap to a point
// in a DC.
//
void BlitBitmapRectToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPRECT src_rect)
{
  #if 0	// 2022 kriskoin
	// Use top left of src rect as dest point.
	// Offset by the origin so that we're not using local coordinates
	// for the source rect.
	POINT origin;
	zstruct(origin);
	BOOL success = GetViewportOrgEx(dest_hdc, &origin);
	kp(("%s(%d) success = %d, origin = %d,%d\n", _FL, success, origin.x, origin.y));
	RECT sr = *src_rect;
	sr.left   -= origin.x;
	sr.right  -= origin.x;
	sr.top    -= origin.y;
	sr.bottom -= origin.y;
	BlitBitmapRectToDC(src_hbitmap, dest_hdc, (LPPOINT)src_rect, &sr);
  #else
	BlitBitmapRectToDC(src_hbitmap, dest_hdc, (LPPOINT)src_rect, src_rect);
  #endif
}

void BlitBitmapRectToDC(HBITMAP src_hbitmap, HDC dest_hdc, LPPOINT dest_pt, LPRECT src_rect)
{
	if (!src_hbitmap) {
		kp(("%s(%d) BlitBitmapRectToDC() called with NULL src_hbitmap\n", _FL));
		return;
	}
	if (!dest_hdc) {
		kp(("%s(%d) BlitBitmapRectToDC() called with NULL dest_hdc\n", _FL));
		return;
	}
	HDC hdcTemp = CreateCompatibleDC(dest_hdc);
	if (!hdcTemp) {
		kp(("%s(%d) BlitBitmapToDC_nt() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	//SetViewportOrgEx(hdcTemp, 0, 0, NULL);
	SelectObject(hdcTemp, src_hbitmap);   // Select the source bitmap
	pr(("%s(%d) BlitBitmapRectToDC: src = %d,%d, dest = %d,%d, size = %dx%d\n",
				_FL, src_rect->left, src_rect->top, dest_pt->x, dest_pt->y,
				src_rect->right - src_rect->left,
				src_rect->bottom - src_rect->top));
	//Sleep(50);
	BOOL success = BitBlt(dest_hdc, dest_pt->x, dest_pt->y,
			src_rect->right - src_rect->left,	// width
			src_rect->bottom - src_rect->top,	// height
			hdcTemp,
			src_rect->left, src_rect->top, SRCCOPY);
	//Sleep(50);
	if (!success) {
		kp(("%s(%d) BitBlt() failed. GetLastError()=%d\n", _FL, GetLastError()));
	}
	DeleteDC(hdcTemp);
}

//*********************************************************
// https://github.com/kriskoin//
// Duplicate an HBITMAP, including the memory used to hold
// the bitmap.  This is useful for creating a bitmap we can
// draw into while preserving the original for erasing purposes.
// Resulting bitmap will always be compatible with the screen
// device, even if that's a different format than the original
// bitmap passed to this function.  Use DeleteObject() to delete
// it when you're done with it.
//
HBITMAP DuplicateHBitmap(HBITMAP hbm_original)
{
	return DuplicateHBitmap(hbm_original, NULL);
}

HBITMAP DuplicateHBitmap(HBITMAP hbm_original, HPALETTE hpalette)
{
	// Fetch size of original bitmap
	BITMAP bm;
	zstruct(bm);
	GetObject(hbm_original, sizeof(BITMAP), (LPSTR)&bm);
	//kp(("%s(%d) hbm_original = $%08lx. Size = %dx%dx%d\n", _FL, hbm_original,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));

	// Create the duplicate bitmap
	HDC hdc = CreateCompatibleDC(NULL);
	if (!hdc) {
		Error(ERR_ERROR, "%s(%d) DuplicateHBitmap() had a CreateCompatibleDC() failure. GetLastError()=%d", _FL, GetLastError());
	}
	if (hpalette) {
		SelectPalette(hdc, hpalette, FALSE);
		RealizePalette(hdc);
	}
	//kp(("%s(%d) hdc = $%08lx. Bits per pixel=%d\n", _FL, hdc, GetDeviceCaps(hdc, BITSPIXEL)));
	int planes = GetDeviceCaps(hdc, PLANES);
	int bpp = GetDeviceCaps(hdc, BITSPIXEL);
  #if 0	// 2022 kriskoin
	if (!hpalette && bpp <= 8) {
		kp1(("%s(%d) !!! This code does not work because we can't put a 24bpp bitmap into an 8bpp DC\n",_FL));
		// Palettized and we don't have a palette... leave as 24bpp.
		planes = bm.bmPlanes;
		bpp = bm.bmBitsPixel;
	}
  #endif
	HBITMAP hbm_result = CreateBitmap(bm.bmWidth, bm.bmHeight, planes, bpp, NULL);
	if (!hbm_result) {
		Error(ERR_ERROR, "%s(%d) DuplicateHBitmap() had a CreateBitmap() failure. GetLastError()=%d", _FL, GetLastError());
	}

  #if 0	// 2022 kriskoin
	GetObject(hbm_result, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) hbm_result = $%08lx. Size = %dx%dx%d\n", _FL, hbm_result,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
  #endif
	SelectObject(hdc, hbm_result);
	BlitBitmapToDC_nt(hbm_original, hdc);	// copy original to new one
	//kp(("%s(%d) pixel (0,0) of %dx%dx%d bitmap is color $%06lx\n",_FL,bm.bmWidth, bm.bmHeight, bpp, GetPixel(hdc, 0, 0)));
	DeleteDC(hdc);
	return hbm_result;
}

//*********************************************************
// https://github.com/kriskoin//
// Duplicate an HBITMAP, including the memory used to hold
// the bitmap, but flip it horizontally in the process.
// Resulting bitmap will always be compatible with the screen
// device, even if that's a different format than the original
// bitmap passed to this function.  Use DeleteObject() to delete
// it when you're done with it.
//
HBITMAP DuplicateHBitmapFlipped(HBITMAP hbm_original)
{
	return DuplicateHBitmapFlipped(hbm_original, NULL);
}

HBITMAP DuplicateHBitmapFlipped(HBITMAP hbm_original, HPALETTE hpalette)
{
	// Fetch size of original bitmap
	BITMAP bm;
	zstruct(bm);
	GetObject(hbm_original, sizeof(BITMAP), (LPSTR)&bm);
	//kp(("%s(%d) hbm_original = $%08lx. Size = %dx%dx%d\n", _FL, hbm_original,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));

	// Create the duplicate bitmap
	HDC hdc = CreateCompatibleDC(NULL);
	if (!hdc) {
		kp(("%s(%d) DuplicateHBitmap() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	if (hpalette) {
		SelectPalette(hdc, hpalette, FALSE);
		RealizePalette(hdc);
	}
	//kp(("%s(%d) hdc = $%08lx. Bits per pixel=%d\n", _FL, hdc, GetDeviceCaps(hdc, BITSPIXEL)));
	int planes = GetDeviceCaps(hdc, PLANES);
	int bpp = GetDeviceCaps(hdc, BITSPIXEL);
  #if 0	// 2022 kriskoin
	if (!hpalette && bpp <= 8) {
		kp1(("%s(%d) !!! This code does not work because we can't put a 24bpp bitmap into an 8bpp DC\n",_FL));
		// Palettized and we don't have a palette... leave as 24bpp.
		planes = bm.bmPlanes;
		bpp = bm.bmBitsPixel;
	}
  #endif
	HBITMAP hbm_result = CreateBitmap(bm.bmWidth, bm.bmHeight, planes, bpp, NULL);
	if (!hbm_result) {
		kp(("%s(%d) DuplicateHBitmap() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

  #if 0	// 2022 kriskoin
	GetObject(hbm_result, sizeof(BITMAP), (LPSTR)&bm);
	kp(("%s(%d) hbm_result = $%08lx. Size = %dx%dx%d\n", _FL, hbm_result,bm.bmWidth,bm.bmHeight,bm.bmBitsPixel));
  #endif
	SelectObject(hdc, hbm_result);

	HDC hdcTemp = CreateCompatibleDC(hdc);
	SelectObject(hdcTemp, hbm_original);		// Select the source bitmap
	POINT ptSize;
	zstruct(ptSize);
	ptSize.x = bm.bmWidth;            // Get width of bitmap
	ptSize.y = bm.bmHeight;           // Get height of bitmap
	DPtoLP(hdcTemp, &ptSize, 1);      // Convert from device to logical points
	BOOL success = StretchBlt(hdc, ptSize.x-1, 0, -ptSize.x, ptSize.y,
					hdcTemp, 0, 0, ptSize.x, ptSize.y, SRCCOPY);
	if (!success) {
		kp(("%s(%d) BitBlt() failed. GetLastError()=%d\n", _FL, GetLastError()));
	}
	DeleteDC(hdcTemp);

	//kp(("%s(%d) pixel (0,0) of %dx%dx%d bitmap is color $%06lx\n",_FL,bm.bmWidth, bm.bmHeight, bpp, GetPixel(hdc, 0, 0)));
	DeleteDC(hdc);
	return hbm_result;
}

//************* Bitmap blit queue class (not thread safe) ***********
// This class is not thread safe, but it shouldn't need to be since all
// drawing should be done in response to WM_PAINT messages.

//*********************************************************
// https://github.com/kriskoin//
// BlitQueue constructor/destructor
//
BlitQueue::BlitQueue(void)
{
	count = 0;	// empty queue
}

BlitQueue::~BlitQueue(void)
{
	count = 0;	// empty queue
}

//*********************************************************
// https://github.com/kriskoin//
// Initialize the queue.  Remove all objects from it, etc.
// This should be called at the start of each draw loop.
//
void BlitQueue::Init(void)
{
	count = 0;	// empty queue
}

//*********************************************************
// https://github.com/kriskoin//
// Compare and swap functions for sorting.
//
static int BlitQueueQSortCompare(int n1, int n2, void *base)
{
	struct BlitQueueEntry *q = (struct BlitQueueEntry *)base;
	return q[n1].sort_value - q[n2].sort_value;
}	

static void BlitQueueQSortSwap(int n1, int n2, void *base)
{
	struct BlitQueueEntry *q = (struct BlitQueueEntry *)base;
	struct BlitQueueEntry t = q[n1];
	q[n1] = q[n2];
	q[n2] = t;
}

//*********************************************************
// https://github.com/kriskoin//
// Draw the queue to the specified HDC.  Queue is left empty.
//
void BlitQueue::DrawQueue(HDC dest_hdc)
{
	// First, sort it.
	QSort(count, BlitQueueQSortCompare, BlitQueueQSortSwap, queue);

	// Now, draw it.
	struct BlitQueueEntry *q = queue;
	for (int i=0 ; i<count ; i++, q++) {
		switch (q->blit_type) {
		case 0:		// BlitBitmapToDC()
			BlitBitmapToDC(q->hbm, dest_hdc, &q->pt);
			break;
		case 1:		// BlitBitmapToDC_nt()
			BlitBitmapToDC_nt(q->hbm, dest_hdc, &q->pt);
			break;
		default:
			kp(("%s(%d) Warning: unhandled type (%d) in BlitQueue::DrawQueue()\n", _FL, q->blit_type));
			break;
		}
	}
	count = 0;	// empty queue
}

//*********************************************************
// https://github.com/kriskoin//
// Queue a bitmap given a drawing type (internal routine)
//
void BlitQueue::AddBitmap(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority, int blit_type)
{
	if (count >= MAX_BLIT_QUEUE_SIZE) {
	  #if DEBUG
		Error(ERR_ERROR, "%s(%d) BlitQueue::AddBitmap() failed because queue is full.", _FL);
	  #endif
		return;
	}
	if (src_hbitmap) {
		struct BlitQueueEntry *q = queue + count++;
		zstruct(*q);
		q->hbm = src_hbitmap;
		q->pt = *dest_pt;
		q->sort_value = draw_priority*65536+count;
		q->blit_type = blit_type;
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Queue a bitmap with transparency.  Eventually calls BlitBitmapToDC()
// Lower draw priorities get drawn first (behind higher priority items)
//
void BlitQueue::AddBitmap(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority)
{
	AddBitmap(src_hbitmap, dest_pt, draw_priority, 0);
}

//*********************************************************
// https://github.com/kriskoin//
// Queue a bitmap without transparency.  Eventually calls BlitBitmapToDC_nt()
// Lower draw priorities get drawn first (behind higher priority items)
//
void BlitQueue::AddBitmap_nt(HBITMAP src_hbitmap, LPPOINT dest_pt, int draw_priority)
{
	AddBitmap(src_hbitmap, dest_pt, draw_priority, 1);
}

#if 0	// 2022 kriskoin
//*********************************************************
// https://github.com/kriskoin//
// Darken a rect on a dc.
//
void DarkenRect(HDC dest_hdc, RECT *r)
{
	if (!dest_hdc) {
		return;	// nowhere to draw.
	}

	// Create the duplicate bitmap
	HDC hdc_mask = CreateCompatibleDC(NULL);
	if (!hdc_mask) {
		kp(("%s(%d) DuplicateHBitmap() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}

	RECT src_rect;
	zstruct(src_rect);
	src_rect.right = r->right - r->left;
	src_rect.bottom = r->bottom - r->top;

	HBITMAP hbm_mask = CreateBitmap(src_rect.right, src_rect.bottom,
				GetDeviceCaps(hdc_mask, PLANES),
				GetDeviceCaps(hdc_mask, BITSPIXEL), NULL);
	if (!hbm_mask) {
		kp(("%s(%d) DarkenRect() had a failure. GetLastError()=%d\n", _FL, GetLastError()));
	}
	SelectObject(hdc_mask, hbm_mask);
  #if 1	// 2022 kriskoin
	HBRUSH hbr = CreateSolidBrush(RGB(0xE0,0xE0,0xE0));
  #else
	HBRUSH hbr = CreateSolidBrush(RGB(0x7F,0x7F,0x7F));
  #endif
	FillRect(hdc_mask, &src_rect, hbr);
	DeleteObject(hbr);

	BitBlt(dest_hdc, r->left, r->top, src_rect.right, src_rect.bottom, hdc_mask, 0, 0, SRCAND);
	DeleteDC(hdc_mask);
	DeleteObject(hbm_mask);
}
#endif
