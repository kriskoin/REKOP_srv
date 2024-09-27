/**********************************************************************************

 COMMON.CPP -- PPLIB misc functions common to both clients and the server



***********************************************************************************/



#include <stdio.h>

#include <stdlib.h>

#include <ctype.h>

#include <time.h>

#include "gamedata.h"



//*********************************************************


//

// Fill in the client platform information

//

void FillClientPlatformInfo(char *str, struct ClientPlatform *cp, WORD32 client_version)

{

	// If client_platform information is empty, say "unknown"

	*str = 0;

	struct ClientPlatform cpz;

	zstruct(cpz);

	if (!memcmp(&cpz, cp, sizeof(cp))) {

		// all zero...

		strcpy(str, "Computer information not known.\r\n");

		return;

	}



	char processor_str[30], mhz_str[30], pcount_str[30];

	processor_str[0] = mhz_str[0] = pcount_str[0] = 0;

	char *version = "unknown";

	switch (cp->version) {

	case 0:

		version = "Win95";

		break;

	case 1:

		version = "Win98";

		break;

	case 4:

		version = "NT4";

		break;

	case 5:

		version = "W2K";

		break;

	}

	char *processor = "unknown CPU";

	switch (cp->cpu_level) {

	case 0:

		break;

	case 3:

		processor = "80386";

		break;

	case 4:

		processor = "80486";

		break;

	case 5:

		processor = "Pentium";

		break;

	case 6:

		{

			// Lots of Intel CPUs show up as level 6 (Family 6).  Check model to

			// differentiate.

			processor = "PPro,PII,or P3";

			switch (cp->cpu_revision >> 8) {	// switch on model field

			case 1:

				processor = "P-Pro";

				break;

			case 3:

			case 4:

			case 5:

			case 6:

				processor = "P-II";

				break;

			case 7:

				processor = "P-III";

				break;

			case 8:

				processor = "P-III (CM)";

				break;

			}

		}

		break;

	case 15:

		{

			// Lots of Intel CPUs show up as Family 15.  Check model to differentiate.

			processor = "P-4";

			switch (cp->cpu_revision >> 8) {	// switch on model field

			case 0:



				processor = "P-4";

				break;

			default:

				sprintf(processor_str, "P-4, model %d", cp->cpu_revision>>8);

				processor = processor_str;

				break;

			}

		}

		break;

	default:

		sprintf(processor_str, "Processor=%d/%d", cp->cpu_level, cp->cpu_revision>>8);

		processor = processor_str;

		break;

	}

	if (cp->cpu_mhz) {

		sprintf(mhz_str, "%dMHz ", cp->cpu_mhz);

	}

	if (cp->cpu_count > 1) {

		sprintf(pcount_str, "%d CPUs - ", cp->cpu_count);

	}

	sprintf(str+strlen(str), "%s %s %s %dMB %dx%dx%dbpp\r\n",

			mhz_str, processor, version, cp->installed_ram,

			cp->screen_width, cp->screen_height, cp->screen_bpp);

	if (cp->cpu_vendor_str[0] || cp->cpu_identifier_str[0]) {

		// : assume Connectix is a Mac running Virtual PC

		if (strstr(cp->cpu_vendor_str, "Connectix")) {

			sprintf(str+strlen(str), "Apple Macintosh running Virtual PC\r\n");

		} else {

			sprintf(str+strlen(str), "CPU: %s, %s\r\n",

					cp->cpu_vendor_str, cp->cpu_identifier_str);

		}

	}

	long tz = cp->time_zone * 450;

	sprintf(str+strlen(str), "%sTime zone: %+dh%02dm - %s\r\nClient version %d.%02d (build %d)\r\n",

				pcount_str,

				tz/3600, (tz/60)%60,

				(cp->flags & CPFLAG_LARGE_FONTS) ? "Large Fonts" : "Small Fonts",

				(client_version>>24) & 0x00FF,

				(client_version>>16) & 0x00FF,

				client_version & 0x00FFFF);



	int div1 = cp->disk_space_on_our_drive > 9000 ? 1024 : 1;

	int div2 = cp->disk_space_on_system_drive > 9000 ? 1024 : 1;

	sprintf(str+strlen(str), "Drive %c: %d%s free,   %c: %d%s free",

				cp->our_drive_letter, cp->disk_space_on_our_drive/div1,

				div1==1 ? "KB" : "MB",

				cp->system_drive_letter, cp->disk_space_on_system_drive/div2,

				div2==1 ? "KB" : "MB");



	sprintf(str+strlen(str), "   s/n: %d", cp->computer_serial_num);

}



/**********************************************************************************

 Function *ClientTransactionDescription


 Purpose: given a ClientTransaction, fill a reply buffer with a description

***********************************************************************************/

char *ClientTransactionDescription(ClientTransaction *ct, char *out, int admin_flag)

{

	char credit_card[20];	// 1234XXXXXXXX6789 

	zstruct(credit_card);

	char curr_str1[MAX_CURRENCY_STRING_LEN];

	zstruct(curr_str1);

	char curr_str2[MAX_CURRENCY_STRING_LEN];

	zstruct(curr_str2);

	char trans_desc[50];

	zstruct(trans_desc);

	switch (ct->transaction_type) {	// in gamedata.h

	case CTT_NONE: 

		sprintf(trans_desc, "NO TRANSACTION"); 

		break;

	case CTT_PURCHASE: 

//	sprintf(trans_desc, "C/C PURCHASE"); 

		sprintf(trans_desc, "PAYPAL PURCHASE"); //Tony, Dec 11, 2001

		sprintf(credit_card, "%04x...%04x", (ct->partial_cc_number>>16), ct->partial_cc_number & 0x00FFFF);

		break;

	case CTT_CREDIT: 

		sprintf(trans_desc, "C/C CASH OUT"); 

		sprintf(credit_card, "%04x...%04x", (ct->partial_cc_number>>16), ct->partial_cc_number & 0x00FFFF);

		break;

	case CTT_CHECK_ISSUED: 

		sprintf(trans_desc, "CHECK ISSUED"); 

		break;

	case CTT_CHECK_REFUND: 

		sprintf(trans_desc, "CHECK REFUNDED"); 

		break;

	case CTT_WIRE_IN: 

		sprintf(trans_desc, "WIRE DEPOSIT"); 

		break;

	case CTT_WIRE_OUT: 

		sprintf(trans_desc, "WIRE ISSUED"); 

		break;

	case CTT_DRAFT_IN: 

		sprintf(trans_desc, "DRAFT DEPOSIT"); 

		break;

	case CTT_DRAFT_OUT: 

		sprintf(trans_desc, "DRAFT ISSUED"); 

		break;

	case CTT_TRANSFER_IN: 

		sprintf(trans_desc, "TRANSFER IN"); 

		break;

	case CTT_TRANSFER_OUT: 

		sprintf(trans_desc, "TRANSFER OUT"); 



		break;

	case CTT_TRANSFER_INTERNAL: 

		sprintf(trans_desc, "INTERNAL XFER"); 

		break;

	case CTT_TRANSFER_FEE: 

		sprintf(trans_desc, "C/C FEE REFUND"); 

		break;

	case CTT_PRIZE_AWARD: 

		sprintf(trans_desc, "PRIZE AWARD"); 

		break;

	case CTT_BAD_BEAT_PRIZE: 

		sprintf(trans_desc, "BAD BEAT JACKPOT"); 

		break;

	case CTT_TRANSFER_TO: 

		sprintf(trans_desc, "TRANSFER OUT"); // changed as per MM

		break;

	case CTT_TRANSFER_FROM: 

		sprintf(trans_desc, "TRANSFER IN"); // changed as per MM

		break;

	case CTT_MISC: 

		sprintf(trans_desc, "MISC TRANSFER"); 

		break;

	default:

		sprintf(trans_desc, "(unknown)");

	}

	// admin only, show creditable

	char tmp[50];

	zstruct(tmp);

    if (admin_flag) {

		if (ct->transaction_type == CTT_PURCHASE && ct->credit_left) {

			//  we hide the fact the the transaction is not creditable

			// by inflating the creditable amount by the original transaction

			// amount... so if it's greater, display it as such

			if (ct->credit_left > ct->transaction_amount) {

				sprintf(tmp, "X {%-s}", CurrencyString(curr_str1, ct->credit_left-ct->transaction_amount, CT_REAL, TRUE));

			} else {

				sprintf(tmp, "  {%-s}", CurrencyString(curr_str1, ct->credit_left, CT_REAL, TRUE));

			}

		}

	}

	// the money line(s) -- for all but check issued transactions

	if (ct->transaction_type != CTT_CHECK_ISSUED) {

	  #if 0	//

		if (ct->transaction_type == CTT_BAD_BEAT_PRIZE || ct->transaction_type == CTT_PRIZE_AWARD) {

			sprintf(out," %-21s  %-16s  %-10s  #%-11d", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), ct->ecash_id);

		} else if (ct->transaction_type == CTT_TRANSFER_FROM || 

				   ct->transaction_type == CTT_TRANSFER_TO)

		{

			char str[TR_SPARE_SPACE];

			zstruct(str);

			strnncpy(str, ct->str, TR_SPARE_SPACE);

			char detail[20];

			zstruct(detail);

			sprintf(detail,"%s %s", (ct->transaction_type == CTT_TRANSFER_FROM ? "from" : "to"), str);

			sprintf(out," %-21s  %-16s  %-7s  %-14s  %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), detail, ct->ecash_id, tmp);

		} else if (ct->transaction_type == CTT_MISC) {

			//  decided to put the MISC comment in the TYPE, not REF field

			strnncpy(trans_desc, ct->str, CT_SPARE_SPACE+1);

			sprintf(out," %-21s  %-16s  %-10s  %-11s  %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), " ", ct->ecash_id, tmp);

		} else {

			sprintf(out," %-21s  %-16s  %-10s  %-11s  %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), credit_card, ct->ecash_id, tmp);

		}

	  #else

		if (ct->transaction_type == CTT_BAD_BEAT_PRIZE || ct->transaction_type == CTT_PRIZE_AWARD) {

			sprintf(out," %-21s  %-16s %10s #%-13d", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), ct->ecash_id);

		} else if (ct->transaction_type == CTT_TRANSFER_FROM || 

				   ct->transaction_type == CTT_TRANSFER_TO)

		{

			char str[TR_SPARE_SPACE];

			zstruct(str);

			strnncpy(str, ct->str, TR_SPARE_SPACE);

			char detail[20];

			zstruct(detail);

			sprintf(detail,"%s %s", (ct->transaction_type == CTT_TRANSFER_FROM ? "from" : "to"), str);

//		sprintf(out," %-21s  %-16s %10s %-14s %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

//			CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), detail, ct->ecash_id, tmp);

			sprintf(out," %-21s  %-16s  %10s      %-8d %-14s %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), ct->ecash_id, detail, tmp);

		} else if (ct->transaction_type == CTT_MISC) {

			// 24/01/01 kriskoin:

			strnncpy(trans_desc, ct->str, CT_SPARE_SPACE+1);

//		sprintf(out," %-21s  %-16s %10s %-14s %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

//		CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), " ", ct->ecash_id, tmp);

			sprintf(out," %-21s  %-16s  %10s      %-8d %-14s %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), ct->ecash_id, " ", tmp);

		} else {

//	sprintf(out," %-21s  %-16s %10s %-14s %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

//		CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), credit_card, ct->ecash_id, tmp);
	
			sprintf(out," %-21s  %-16s  %10s      %-8d %-14s %s", TimeStr(ct->timestamp,TRUE), trans_desc,

				CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), ct->ecash_id, credit_card, tmp);

		}

  #endif

	}

	// both admin and non admin use this different check_issued format

	#define MAX_TRACKINGNUMBER_LEN	20

	char delivery_str[8][15] = { " ", "DHL", "TransExp", "FedEx", "Certified", "Registered", "Express", "Priority" };

	if (ct->transaction_type == CTT_CHECK_ISSUED) {

		ClientCheckTransaction *cctp = (ClientCheckTransaction *)ct;

		char tracking_str[40];

		zstruct(tracking_str);

		char courier_str[20];

		zstruct(courier_str);

		if (cctp->delivery_method) {	// delivery info exists

			char tracking_number[MAX_TRACKINGNUMBER_LEN];

			zstruct(tracking_number);

			memcpy(tracking_number, cctp->first_eight, 8);

			memcpy(tracking_number+8, cctp->last_ten, 10);

			tracking_number[MAX_TRACKINGNUMBER_LEN-1] = 0;

			sprintf(courier_str, "%s", delivery_str[cctp->delivery_method]);

			if (tracking_number[0]) {

				sprintf(tracking_str, "(%s)", tracking_number);

			}

		}

	  #if 0	//

		sprintf(out," %-21s  %-16s  %-10s  %-11s  %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

			CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), 

			courier_str, cctp->ecash_id, tracking_str);

	  #else

//	sprintf(out," %-21s  %-16s %10s %-14s %-08d %s", TimeStr(ct->timestamp,TRUE), trans_desc,

//		CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), 

//		courier_str, cctp->ecash_id, tracking_str);

		sprintf(out," %-21s  %-16s  %10s      %-8d %-14s %s", TimeStr(ct->timestamp,TRUE), trans_desc,

			CurrencyString(curr_str1, ct->transaction_amount, CT_REAL, TRUE), 

			cctp->ecash_id, courier_str, courier_str, tracking_str);

	  #endif

	}

	NOTUSED(admin_flag); // non-admin build

	return out;

}

/**********************************************************************************

 Function *ConverStringToLowerCase(char *str)


 Purpose: convert passed string to lower case (MODIFIES source string, return ptr to it

***********************************************************************************/

char *ConverStringToLowerCase(char *str)

{

	char *p = str;

	while (*p) {

		*p = (char)(tolower(*p));

		p++;

	}

	return str;

}



							// 0	1    2    3    4    5    6    7    8    9   10   11  12   13   14  null

char PhoneNumberChars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '(', ')','+', '-', ' ', 0 };



/**********************************************************************************

 int MapPhoneNumberCharacter


 Purpose: used exclusively by functions below to map an index to our "encrypted" phone number

***********************************************************************************/

char MapPhoneNumberCharacter(char c)

{

	char i = 0;

	while (PhoneNumberChars[i]) {

		if (PhoneNumberChars[i] == c) {	// found it

			pr(("%s(%d) mapped %c to %d\n", _FL, c, i));

			return i;

		}

		i++;

	}

	// didn't find it, default to space

	return 14;	// see definition above, space = 14

}



/**********************************************************************************

 char *EncodePhoneNumber();


 Purpose: encrypt 16-char phone number into our 8-byte version

 Note: fills result string dest, returns a pointer to it

 n.b. dest MUST be at least 8 bytes and will not be null terminated

***********************************************************************************/

char *EncodePhoneNumber(char *src)

{

	// use this with a series of internal static buffers -- simply return ptr to current one

	#define PHONE_NUMBER_BUFFERS 8

	static char phone_number[PHONE_NUMBER_BUFFERS][PHONE_NUM_LEN+1];

	static int buf_index;

	buf_index = (buf_index+1) % PHONE_NUMBER_BUFFERS;	// increment to next usable one

	zstruct(phone_number[buf_index]);

	EncodePhoneNumber(phone_number[buf_index], src);

	return phone_number[buf_index];

}	



char *EncodePhoneNumber(char *dest, char *src)

{

	#define MAX_LOCAL_TMP_SRC 50

	char local_source[MAX_LOCAL_TMP_SRC];	// make sure it's way big enough

	memset(local_source, 0, MAX_LOCAL_TMP_SRC);

	int local_source_index = 0;

	strnncpy(local_source, src, MAX_LOCAL_TMP_SRC);

	#define MAX_TMP_EPN	PHONE_NUM_LEN+5

	char tmp_dest[MAX_TMP_EPN];	// PHONE_NUM_LEN should be enough

	zstruct(tmp_dest);

	int dest_index = 0;

	forever {

		char left_char = local_source[local_source_index];

		char right_char = local_source[local_source_index+1];

		if (!left_char) {	// are we done?

			break;

		}

		if (!right_char) {	// could be blank if phone # was odd number of digits

			right_char = ' ';

		}

		// that's there for sure		

		char left_map = MapPhoneNumberCharacter(left_char);

		char right_map = MapPhoneNumberCharacter(right_char);

		int this_char = (left_map << 4) | right_map;

		pr(("%s(%d) source=%c, source+1=%c, lm=%d, rm=%d this_char =  %c\n", 

		   _FL,  left_char, right_char, left_map, right_map, (char)this_char));

		tmp_dest[dest_index] = (char)this_char;

		local_source_index += 2;

		dest_index++;

		if (dest_index >= MAX_TMP_EPN) {

			// we're done

			break;

		}

	}

	memcpy(dest, tmp_dest, PHONE_NUM_LEN);

	return dest;

}





/**********************************************************************************

 char *DecodePhoneNumber();

 Purpose: decrypt 16-char phone number from our 8-byte version

 Note: fills result string dest, returns a pointer to it

 n.b. dest MUST be at least 16 bytes!

***********************************************************************************/

char *DecodePhoneNumber(char *src)

{

	// use this with a series of internal static buffers -- simply return ptr to current one

	static char phone_number[PHONE_NUMBER_BUFFERS][PHONE_NUM_EXPANDED_LEN+1];

	static int buf_index;

	buf_index = (buf_index+1) % PHONE_NUMBER_BUFFERS;	// increment to next usable one

	zstruct(phone_number[buf_index]);

	DecodePhoneNumber(phone_number[buf_index], src);

	return phone_number[buf_index];

}	



char *DecodePhoneNumber(char *dest, char *src)

{

	char tmp_dest[PHONE_NUM_EXPANDED_LEN+1];	

	memset(tmp_dest, 0, PHONE_NUM_EXPANDED_LEN+1);

	int dest_index = 0;

	char *source = src;

	forever {

		if (!*source) {	// are we done?

			break;

		}

		int left_index = *source >> 4 & 0x0f;

		int right_index = *source & 0x0f;

		pr(("%s(%d) li = %d, ri = %d\n", _FL, left_index, right_index));

		tmp_dest[dest_index] = PhoneNumberChars[left_index];

		tmp_dest[dest_index+1] = PhoneNumberChars[right_index];

		source++;

		dest_index += 2;

		if (dest_index >= PHONE_NUM_EXPANDED_LEN) {

			break;	

		}

	}

	strcpy(dest, tmp_dest);

	pr(("%s(%d) decrypt: src = %s, dest = %s\n", _FL, src, dest));

	return dest;

}









