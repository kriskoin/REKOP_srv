//*********************************************************

//

//	JPEG reading routines

//


//*********************************************************



#define DISP 0



#include <stdio.h>

extern "C"  {

  #include "../jpeg-6b/jpeglib.h"

}

#ifdef WIN32

  #define WIN32_LEAN_AND_MEAN

  #include <windows.h>

#endif

#include "pplib.h"



int iNVidiaBugWorkaround;	// set to work around some nvidia driver bugs



//*********************************************************



//

// Read a .JPG file and convert it to a bitmap.  Returns

// a object handle that you can use as a parameter to SelectObject().

// Call DeleteObject(handle) when you're done with it.

// If you're going to use this function, you must link with jpeg.lib.

// success: returns non-zero handle.

// failure: returns zero.

//

HBITMAP LoadJpegAsBitmap(char *fname)

{

	return LoadJpegAsBitmap(fname, NULL);

}	



HBITMAP LoadJpegAsBitmap(char *fname, HPALETTE hpalette)

{

	FILE *fd = fopen(fname, "rb");

	if (!fd) {

		Error(ERR_ERROR, "%s(%d) Could not load '%s'", _FL, fname);

		return NULL;

	}



	struct jpeg_decompress_struct cinfo;

	struct jpeg_error_mgr jerr;

	zstruct(cinfo);

	zstruct(jerr);



	cinfo.err = jpeg_std_error(&jerr);

	jpeg_create_decompress(&cinfo);

	jpeg_stdio_src(&cinfo, fd);

	jpeg_read_header(&cinfo, TRUE);

	jpeg_start_decompress(&cinfo);

	pr(("%s(%d) file '%s' is %dx%d pixels\n", _FL, fname, cinfo.output_width, cinfo.output_height));



	int bytes_per_scanline = cinfo.output_width * cinfo.output_components;

	bytes_per_scanline = (bytes_per_scanline + 3) & ~3;	// must be dword multiple



	pr(("%s(%d) bytes_per_scanline = %d (%d*%d)\n", _FL, bytes_per_scanline, cinfo.output_width, cinfo.output_components));



	// Allocate some memory to decode the jpeg file into...

	size_t len = bytes_per_scanline * cinfo.output_height;

	void *data_ptr = malloc(len);

	if (!data_ptr) {

		jpeg_destroy_decompress(&cinfo);

		fclose(fd);

		Error(ERR_ERROR, "%s(%d) out of memory.  Attempted %d", _FL, len);

		return NULL;

	}



	// Now uncompress the data into the array...

	byte *dest_ptr = (byte *)data_ptr;

	for (unsigned int i=0 ; i<cinfo.output_height ; i++, dest_ptr += bytes_per_scanline) {

		jpeg_read_scanlines(&cinfo, &dest_ptr, 1);

		// The data is now in RGB order, but Windows will want it in

		// BGR order.  Switch that around now.

		byte *p = dest_ptr;

		for (unsigned int j=0 ; j<cinfo.output_width ; j++, p+=3) {

			byte t = p[0];

			p[0] = p[2];

			p[2] = t;

		}

	}

	jpeg_finish_decompress(&cinfo);


	

	fclose(fd);	// we're done reading the file.



	// Now that we've got the data in 24-bit RGB format, convert it

	// to a device dependent bitmap.

	BITMAPINFOHEADER bmih;

	BITMAPINFO bmi;

	zstruct(bmih);

	zstruct(bmi);

	bmih.biSize = sizeof(bmih);

	bmih.biWidth = cinfo.output_width;

	bmih.biHeight = -(long)cinfo.output_height;

	bmih.biPlanes = 1;

	bmih.biBitCount = 24;

	bmih.biCompression = BI_RGB;

	bmi.bmiHeader = bmih;	// all the parameters are the same.



	// Create a dc compatible with the desktop.  We don't want to hold

	// onto the desktop dc while converting colors and such because it

	// can take a long time and it gives the appearance of the computer hanging.

	//!!!! I can't seem to get this to work... it always freezes the screen

	// and mouse during CreateDIBitmap().

	HDC hdc = CreateCompatibleDC(NULL);

	pr(("%s(%d) hdc=$%08lx, hdc colors=%d, bpp=%d\n",

			_FL, hdc, GetDeviceCaps(hdc, NUMCOLORS), GetDeviceCaps(hdc, BITSPIXEL)));

	if (!hdc) {

		Error(ERR_ERROR, "%s(%d) CreateCompatibleDC failed. Error=%d", _FL,GetLastError());

	}



	// Give this new DC the right color format (i.e. do all conversions at load time)

	HDC desktop_dc = GetDC(NULL);

	HBITMAP hbm1 = CreateCompatibleBitmap(desktop_dc, 1, 1);

	ReleaseDC(NULL, desktop_dc);

	HANDLE oldhbm = SelectObject(hdc, hbm1);

	pr(("%s(%d) hdc=$%08lx, hbm1=$%08lx, oldhbm=$%08lx, hdc colors=%d, bpp=%d\n",

			_FL, hdc, hbm1, oldhbm, GetDeviceCaps(hdc, NUMCOLORS), GetDeviceCaps(hdc, BITSPIXEL)));

	//kp(("%s(%d) Sleep(1000);\n", _FL));Sleep(1000);

	if (hpalette) {

		SelectPalette(hdc, hpalette, FALSE);

		RealizePalette(hdc);

	}

	//kp(("%s(%d) Sleep(1000); then calling CreateDIBitmap()...\n", _FL));Sleep(1000);

	HBITMAP hbm = CreateDIBitmap(hdc, &bmih, CBM_INIT,

			data_ptr, &bmi, DIB_RGB_COLORS);

	//kp(("%s(%d) Back from CreateDIBitmap.  Sleep(1000);\n", _FL));Sleep(1000);

	if (!hbm) {

		Error(ERR_ERROR, "%s(%d) CreateDIBitmap failed. Error=%d (hdc=$%08lx fname='%s' w=%d h=%d)",

				_FL,GetLastError(), hdc, fname, bmih.biWidth, bmih.biHeight);

	}

	SelectObject(hdc, oldhbm);

	DeleteObject(hbm1);

	DeleteDC(hdc);

	free(data_ptr);

	//kp(("%s(%d) Sleep(1000);\n", _FL));Sleep(1000);



	// Now that we've done all that, we've got something in a

	// format that the video hardware isn't quite optimized for (if you can

	// believe that).  Call the regular DuplicateHBitmap() function to improve

	// that situation.

	// The NVidia drivers seem to have a bug in them which causes

	// these next few lines to produce graphics that are half black

	// (lower half of picture is black).  Therefore, only do this if

	// we're not trying to work around that bug.

	if (!iNVidiaBugWorkaround) {

		HBITMAP hbm2 = DuplicateHBitmap(hbm, hpalette);

		if (hbm2) {

			// DuplicateHBitmap succeeded... use the new one instead

			DeleteObject(hbm);	// we're now done with this temporary version.

			hbm = hbm2;

		}

	}

	return hbm;

}	



//*********************************************************


//

// Read a .BMP file and convert it to a bitmap.  Returns

// an object handle that you can use as a parameter to SelectObject().

// Call DeleteObject(handle) when you're done with it.

// This function is similar to LoadImage() but it converts the .BMP

// file to be compatible with the current screen device format.

// success: returns non-zero handle.

// failure: returns zero.

//

HBITMAP LoadBMPasBitmap(char *fname)

{

	return LoadBMPasBitmap(fname, NULL);

}



HBITMAP LoadBMPasBitmap(char *fname, HPALETTE hpalette)

{

	if (!fname) {

		return NULL;

	}

	// First, use LoadImage() to load it in as 24bpp.

	HBITMAP original_hbm = (HBITMAP)LoadImage(NULL,

					FindFile(fname), IMAGE_BITMAP, 0, 0,

					LR_CREATEDIBSECTION | LR_DEFAULTSIZE | LR_LOADFROMFILE);

	if (!original_hbm) {

		kp(("%s(%d) LoadBMPasBitmap: Failed to load %s using LoadImage().  Error = %d\n", _FL, fname, GetLastError()));

		return NULL;

	}



	// Now convert it to be compatible with the current screen device.

	// If the current screen is palettized and we don't have a palette,

	// leave it as 24bpp.

	HBITMAP result_hbm = DuplicateHBitmap(original_hbm, hpalette);

	DeleteObject(original_hbm);	// delete original

	return result_hbm;

}	



//*********************************************************


//

// Load a palette (.act) file if we're in palette mode.

// ACT files are what Photoshop writes out... they're simply arrays

// of RGB values (in that order).  No headers or anything else.

//

HPALETTE LoadPaletteIfNecessary(char *fname)

{

	HPALETTE result = NULL;

	HDC hdc = GetDC(NULL);	// get DC for primary display

	int raster_caps = GetDeviceCaps(hdc, RASTERCAPS);

	ReleaseDC(NULL, hdc);

	if (raster_caps & RC_PALETTE) {	// palette device?

		// Load the palette

		FILE *fd = fopen(fname, "rb");

		if (fd) {

			int palette_len = sizeof(LOGPALETTE)+sizeof(PALETTEENTRY)*256;

			LOGPALETTE *palette = (LOGPALETTE *)malloc(palette_len);

			if (palette) {

				memset(palette, 0, palette_len);

				palette->palVersion = 0x300;	// I have no idea what goes here, I just found this in an example.

				while (!feof(fd) && palette->palNumEntries < 256) {

					PALETTEENTRY *e = &palette->palPalEntry[palette->palNumEntries++];

					e->peRed   = (BYTE)fgetc(fd);

					e->peGreen = (BYTE)fgetc(fd);

					e->peBlue  = (BYTE)fgetc(fd);

					// If that was black, don't add it.  Photoshop writes

					// a bunch of zeroes at the end of the file to pad the

					// color table out to 256 entries.  There's no point

					// adding duplicate blacks.

					if (!e->peRed && !e->peGreen && !e->peBlue) {

						palette->palNumEntries--;	// remove it.

					}

				}

				pr(("%s(%d) we read %d colors into the palette.\n", _FL, palette->palNumEntries));

				result = CreatePalette(palette);

				//kp(("%s(%d) hCardRoomPalette = $%08lx. GetLastError = %d\n", _FL, hCardRoomPalette, GetLastError()));

				free(palette);	// we're done with this now.

			}

			fclose(fd);

		}

	}

	return result;

}	

