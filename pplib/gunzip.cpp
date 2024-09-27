//*********************************************************
//
//	gunzip utility functions
//
// 
//
//*********************************************************

#define DISP 0

#include <stdio.h>
#include "zlib.h"
#include "pplib.h"

//*********************************************************
// https://github.com/kriskoin//
// gunzip a file using our own routines (don't shell out).
// This is the simplest function.  It uncompresses the source
// file and saves it to the same filename (less the .gz extension).
// If the filename does not end in .gz, no action is performed.
// The source file (*.gz) is NOT deleted (unlike the cmd line gunzip)
// success: ERR_NONE
// failure: ERR_* and Error() was called.
//
ErrorType gunzipfile(char *fname)
{
	ErrorType err = ERR_NONE;
	{
		char ext[MAX_FNAME_LEN];
		GetExtension(fname, ext);
		if (strcmp(ext, "gz")) {
			return ERR_NONE;	// not a .gz file.  Do nothing.
		}
	}

	gzFile src_fd = gzopen(fname, "rb");
	if (!src_fd) {
		Error(ERR_ERROR, "%s(%d) Could not open file '%s' for gunzip", _FL, fname);
		return ERR_ERROR;
	}

	FILE *dest_fd;
	{

		char dest_fname[MAX_FNAME_LEN];
		strcpy(dest_fname, fname);
		TrimExtension(dest_fname);
		dest_fd = fopen(dest_fname, "wb");
		if (!dest_fd) {
			Error(ERR_ERROR, "%s(%d) Could not open dest file '%s' for gunzip", _FL, dest_fname);
			gzclose(src_fd);
			return ERR_ERROR;
		}
	}	

	int bytes_read;
	do {
		#define INFLATE_CHUNK_SIZE	2048
		byte buffer[INFLATE_CHUNK_SIZE];
		bytes_read = gzread(src_fd, buffer, INFLATE_CHUNK_SIZE);
		if (bytes_read==-1) {
			Error(ERR_ERROR, "%s(%d) gunzip: read error from %s", _FL, fname);
			gzclose(src_fd);
			return ERR_ERROR;
		}
		if (bytes_read) {
			int bytes_written = fwrite(buffer, 1, bytes_read, dest_fd);
			if (bytes_written != bytes_read) {
				Error(ERR_ERROR, "%s(%d) Error writing %d uncompressed bytes.", _FL,bytes_read);
				err = ERR_ERROR;
				break;
			}
		}
	} while(bytes_read);
	fclose(dest_fd);
	gzclose(src_fd);

	return err;
}

//*********************************************************
// https://github.com/kriskoin//
// Compress a string into a smaller buffer.  Throw out
// anything that does not fit.
//
// Returns # of bytes truncated.
//
int CompressString(char *dest, int dest_len, char *source, int source_len)
{
	int bytes_chopped = 0;
	int starting_len = strlen(source);

  #if 0
	kp1(("%s(%d) Compression is disabled in CompressString()\n", _FL));
	if (starting_len >= dest_len) {
		bytes_chopped = starting_len - dest_len + 1;
	}
  #endif

	forever {
		int bytes_to_compress = starting_len - bytes_chopped;
		if (bytes_to_compress < dest_len) {
			// source data fits without compression.
			// simply do a strnncpy and we're done.
			strnncpy(dest, source, dest_len);
			break;
		}
		// Try to compress
		z_stream deflate_zs;
		zstruct(deflate_zs);
		deflate_zs.zalloc = zalloc;
		deflate_zs.zfree  = zfree;
		int zerr = deflateInit(&deflate_zs, Z_BEST_COMPRESSION);	// init for max. compression
		if (zerr != Z_OK) {
			kp(("%s(%d) deflateInit() returned %d\n", _FL, zerr));
			// Save what we could.
			strnncpy(dest, source, dest_len);
			break;
		} else {
			// Do the work...
			*dest = 0x01;	// signal this data is compressed (first byte = 0x01)
			deflate_zs.next_in = (Bytef *)source;
			deflate_zs.avail_in = starting_len - bytes_chopped;
			deflate_zs.next_out = (Bytef *)dest+1;
			deflate_zs.avail_out = dest_len-1;
		  #if 1
			deflate(&deflate_zs, Z_FULL_FLUSH);
			deflateEnd(&deflate_zs);
		  #else
			int result = deflate(&deflate_zs, Z_FULL_FLUSH);
			deflateEnd(&deflate_zs);
			kp(("%s(%d) src_len = %d, avail_in = %d, compressed_len = %d, bytes_chopped = %d, avail_out = %d, result = %d\n",
					_FL, starting_len, deflate_zs.avail_in, deflate_zs.total_out, bytes_chopped, deflate_zs.avail_out, result));
			//khexd(dest, deflate_zs.total_out+1);
		  #endif
			if (!deflate_zs.avail_out) {
				// No output room left... chop some input data.
				// We didn't finish compressing (result would be Z_STREAM_END
				// if it all got compressed).
				bytes_chopped++;	// chop another input byte
				continue;			// try again with fewer input bytes.
			}
			break;	// all done.
		}
	}
	NOTUSED(source_len);
	return bytes_chopped;
}

//*********************************************************
// https://github.com/kriskoin//
// Uncompress a string which was compressed with CompressString().
//
// Returns # of bytes truncated.
//
int UncompressString(char *source, int source_len, char *dest, int dest_len)
{
	if (*source==0x01) {	// compressed?
		memset(dest, 0, dest_len);
		z_stream inflate_zs;
		zstruct(inflate_zs);
		inflate_zs.zalloc = zalloc;
		inflate_zs.zfree  = zfree;
		int zerr = inflateInit(&inflate_zs);
		if (zerr != Z_OK) {
			kp(("%s(%d) inflateInit() returned %d\n", _FL, zerr));
		}
		inflate_zs.next_in = (Bytef *)source+1;
		inflate_zs.avail_in = source_len-1;
		inflate_zs.next_out = (Bytef *)dest;
		inflate_zs.avail_out = dest_len-1;	// always leave room for a null terminator
		inflate(&inflate_zs, Z_FULL_FLUSH);
		inflateEnd(&inflate_zs);
		dest[dest_len-1] = 0;	// always make sure it's null terminated
	} else {	// just a normal string...
		strnncpy(dest, source, dest_len);
	}
	NOTUSED(source_len);
	return 0;
}
