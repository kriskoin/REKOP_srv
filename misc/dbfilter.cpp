/**********************************************************************************
 DBFilter
 date: kriskoin 2019/01/01 Purpose: filters through a TropicanaPoker server DataBase file and does stuff
***********************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "../src/incl/sdb.h"
#include "../pplib/pplib.h"

#ifndef DebugFilterLevel	// HK20000630 (not sure if this ever built without this here)
	int DebugFilterLevel;
#endif
//kriskoin: // determined from the file size.  The MAX should be somewhat larger
// than the actual database size so that it can be grown without
// needing to recompile dbfilter.
#define MAX_DB_SIZE 80000

int iRunningLiveFlag = FALSE;	// unused but declared as extern in sdb and needed
int iWriteMailingList = FALSE;	// set if we should write the mailing list
int iWriteLabels = FALSE;		// set if we should write out labels of some sort
int iWriteCountries = FALSE;	// set if we want a printout of countries
int ValidEmailAddress(SDBRecord *ar);
void AddXLSEntry(SDBRecord *r);
void CheckMaxedOut(SDBRecord *r);
void WriteOutTop100PlayMoney(void);
void WriteOutEmailXref(void);
void AnalyzeTransactions(void);
void ParseFlopCards(char *filename);
void WriteMailingList(void);
void WriteCountries(void);
void WriteDataProfile(char *filename);
char *FileLastModifiedStr(char *filename);
void ConvertSecondsToString(int seconds_to_go, char *dest_str, int display_seconds_flag, int display_short_units_flag, int display_field_count);

int Top100 = FALSE;	// write out top play money?
int WriteXLS = FALSE;	// write out spreadsheet?

int FindUserID = FALSE;
char szFindUserID[50];
char szCheckAmt[20];

typedef struct  CountryType {
	char name[MAX_COMMON_STRING_LEN];
	int count;
	char last_player_seen[MAX_PLAYER_USERID_LEN];
} CountryType;
#define MAX_COUNTRIES	250
static CountryType Country[MAX_COUNTRIES];
static int maxed_count[3];
int different_countries = 0;
int country_people = 0;
int potential_maxed_out_count = 0;


int total_people = 0;

int list_size;	// # of entries in fake_list, real_list, and email_list (they're all the same)
static WORD32 force_alignment[32];
static SDBRecord *fake_list[MAX_DB_SIZE];
static SDBRecord *real_list[MAX_DB_SIZE];
static SDBRecord *email_list[MAX_DB_SIZE];

#define TOP100_HTML_FILENAME	"t_table.html"
#define TOP500_HTML_FILENAME	"t_t500.html"
#define XLS_FILENAME			"PPDB.txt"
#define MAILLIST_FNAME			"names.lst"
#define LABELS_FNAME			"labels.txt"
#define TIMEZONE_FNAME			"timezones.txt"
#define MAXEDOUT_FNAME			"maxedout.txt"

int iTimeZoneCounts[24];

//*********************************************************
// https://github.com/kriskoin//
// QSort list sorting compare and swap functions...
//
int fake_list_compare(int n1, int n2, void *base)
{
	int chips1 = fake_list[n1]->fake_in_bank+fake_list[n1]->fake_in_play;
	int chips2 = fake_list[n2]->fake_in_bank+fake_list[n2]->fake_in_play;
	int diff = chips2-chips1;
	if (!diff) {
		// balances are the same, sort by player id
		diff = fake_list[n2]->player_id - fake_list[n1]->player_id;
	}
	NOTUSED(base);
	return diff;
}	

void fake_list_swap(int n1, int n2, void *base)
{
	SDBRecord *tmp;
	tmp = fake_list[n1];
	fake_list[n1] = fake_list[n2];
	fake_list[n2] = tmp;
	NOTUSED(base);
}

int real_list_compare(int n1, int n2, void *base)
{
  #if 0	// alternate sorting method -- by hands played
	int hands1 = real_list[n1]->hands_seen;
	int hands2 = real_list[n2]->hands_seen;
	int diff = hands2-hands1;
	if (!diff) {
		// balances are the same, sort by player id
		diff = real_list[n2]->player_id - real_list[n1]->player_id;
	}
	NOTUSED(base);
	return diff;
  #else
	int chips1 = real_list[n1]->real_in_bank+real_list[n1]->real_in_play;
	int chips2 = real_list[n2]->real_in_bank+real_list[n2]->real_in_play;
	int diff = chips2-chips1;
	if (!diff) {
		// balances are the same, sort by player id
		diff = real_list[n2]->player_id - real_list[n1]->player_id;
	}
	NOTUSED(base);
	return diff;
  #endif
}

void real_list_swap(int n1, int n2, void *base)
{
	SDBRecord *tmp;
	tmp = real_list[n1];
	real_list[n1] = real_list[n2];
	real_list[n2] = tmp;
	NOTUSED(base);
}

int email_list_compare(int n1, int n2, void *base)
{
	int diff = stricmp(email_list[n1]->email_address,email_list[n2]->email_address);
	if (!diff) {
		// emails are the same, sort by player id
		diff = email_list[n2]->player_id - email_list[n1]->player_id;
	}
	NOTUSED(base);
	return diff;
}	

void email_list_swap(int n1, int n2, void *base)
{
	SDBRecord *tmp;
	tmp = email_list[n1];
	email_list[n1] = email_list[n2];
	email_list[n2] = tmp;
	NOTUSED(base);
}

// countries
int country_compare(int n1, int n2, void *base)
{
	int diff = Country[n2].count - Country[n1].count;
	NOTUSED(base);
	return diff;
}	

void country_swap(int n1, int n2, void *base)
{
	CountryType tmp;
	tmp = Country[n1];
	Country[n1] = Country[n2];
	Country[n2] = tmp;
	NOTUSED(base);
}



//*********************************************************
// https://github.com/kriskoin//
// Write out a text file to be imported into the SQL database
// (writes "data_for_sql.txt")
//
void WriteTextFileForSQL(void)
{
	FILE *fd = fopen("data_for_sql.txt", "wt");
	if (!fd) {
		printf("ERROR - Can't open file data_for_sql.txt for writing.  Aborted.\n");
		return;
	}
	for (int i=0; i < list_size; i++) {
		SDBRecord *r = email_list[i];
		if (r && r->priv < ACCPRIV_CUSTOMER_SUPPORT) {
			fprintf(fd,	"$%08lx\t"	// player_id
						"%d\t%d\t"	// real_in_bank, real_in_play
						"%d\t%d\t"	// fake_in_bank, fake_in_play
						"%d\t%d\t"	// fee_credit_points, pending_fee_refund
						"%d\t"		// hands seen
						"%d\t"		// enum { GENDER_UNDEFINED, GENDER_UNKNOWN, GENDER_MALE, GENDER_FEMALE };
						"%d\t"		// flags (SDBRECORD_FLAG_*)
						"%d\t"		// account creation time (unix time_t - seconds since 1970)
						"%d.%02d-%d\t"	// client version
						"%d\t"		// next_transaction_number
						"%d\t"		// priv level
					  #if 1	// 2022 kriskoin
						"%s\t"		// user_id
						"%s\t"		// city
						"%s\t"		// full_name
						"%s\t"		// email_address
						"%s\t"		// mailing_address1
						"%s\t"		// mailing_address2
						"%s\t"		// mailing_address_state
						"%s\t"		// mailing_address_country
						"%s\t"		// mailing_address_postal_code
					  #endif
						"\n",
						r->player_id,
						r->real_in_bank,
						r->real_in_play,
						r->fake_in_bank,
						r->fake_in_play,
						r->fee_credit_points,
						r->pending_fee_refund,
						r->hands_seen,
						r->gender,
						r->flags,
						r->account_creation_time,
						(r->client_version >> 24) & 0x00FF,
						(r->client_version >> 16) & 0x00FF,
						(r->client_version & 0x00FFFF),
						r->next_transaction_number,
						r->priv,
						r->user_id,
						r->city,
						r->full_name,
						r->email_address,
						r->mailing_address1,
						r->mailing_address2,
						r->mailing_address_state,
						r->mailing_address_country,
						r->mailing_address_postal_code);

					  #if 0	//kriskoin: 						WORD32 all_in_times[ALLINS_TO_RECORD_PER_PLAYER];		// time_t for last n all-in's.
						WORD32 most_recent_games[GAMES_TO_RECORD_PER_PLAYER];	// game serial numbers
						WORD32 last_login_times[LOGINS_TO_RECORD_PER_PLAYER];	// times of previous logins
						WORD32 last_login_ip[LOGINS_TO_RECORD_PER_PLAYER];		// IP addresses of previous logins
						ClientTransaction transaction[TRANS_TO_RECORD_PER_PLAYER];	// 32 bytes * 20 = 640 bytes
					  #endif
		}
	}
	fclose(fd);
}

/**********************************************************************************
 Function WriteLabels
 date: 24/01/01 kriskoin Purpose: write out the importable label format for certain criteria
 NOTE: read what the src below does, it's been customized for whatever the last run was
***********************************************************************************/
void WriteLabels(void)
{
	FILE *fd = fopen(LABELS_FNAME, "wt");
	if (!fd) {
		printf("**** ERROR: Cannot open %s for writing\n", LABELS_FNAME);
		exit(10);
	}

	fprintf(fd, "%s|%s|%s|%s|%s|%s|%s~\n",
		"FullName", "Addr1", "Addr2", "City", "State", "Country", "ZipCode");

	#define NEEDED_PURCHASES		3
	#define MAX_ADDRESSES_OUTPUT	500
	int eligible, purch_count, found_count = 0;
	char str1[MAX_CURRENCY_STRING_LEN];
	zstruct(str1);


	for (int i=0; i < list_size; i++) {
		if (real_list[i]) {
			eligible = FALSE;
			purch_count = 0;
			SDBRecord *r = real_list[i];
			for (int j=TRANS_TO_RECORD_PER_PLAYER-1; j >= 0; j--) {
				ClientTransaction *ct = &(r->transaction[j]);
				if (ct->transaction_type == CTT_PURCHASE) {
					purch_count++;
					if (purch_count == NEEDED_PURCHASES) {
						found_count++;
						eligible = TRUE;
					}
				}
			}
			if (eligible && found_count < MAX_ADDRESSES_OUTPUT) {
				printf("%d: %s (%s) - %d purchases, %d hands, balance %s\n", 
					found_count, r->full_name, r->user_id, purch_count, r->hands_seen,
					CurrencyString(str1, r->real_in_bank+r->real_in_play, CT_REAL, TRUE));
				// update label template
				fprintf(fd, "%s|%s|%s|%s|%s|%s|%s~\n",
					r->full_name,
					r->mailing_address1,
					r->mailing_address2,
					r->city, 
					r->mailing_address_state, 
					r->mailing_address_country,
					r->mailing_address_postal_code);
			} else {
				// didn't get one, perhaps fits another criteria
				if (r->flags & SDBRECORD_FLAG_VIP) {
				fprintf(fd, "VIP %s|%s|%s|%s|%s|%s|%s~\n",
					r->full_name,
					r->mailing_address1,
					r->mailing_address2,
					r->city, 
					r->mailing_address_state, 
					r->mailing_address_country,
					r->mailing_address_postal_code);
				}
			}
		}
	}
	fclose(fd);
}
/**********************************************************************************
 Function WriteCountries
 date: 24/01/01 kriskoin Purpose: display number of different countries, etc
***********************************************************************************/
void WriteCountries(void)
{
	QSort(different_countries, country_compare,  country_swap,  NULL);
	printf("%d validated players in %d different countries\n", 
		country_people, different_countries);
	double running_total = 0.0;
	int running_people_total = 0;
	for (int ct=0; ct < MAX_COUNTRIES; ct++) {
		if (Country[ct].name[0]) {
			double piece = 100.0*Country[ct].count/country_people;
			running_total += piece;
			running_people_total += Country[ct].count;
			printf("%5d - %-16s (%5.2f%%) -- [%d - %2.2f%%] %s\n",
				Country[ct].count,
				Country[ct].name,
				piece,
				running_people_total,
				running_total,
				Country[ct].count < 4 ? Country[ct].last_player_seen : "");
		}
	}
}


//*********************************************************
// https://github.com/kriskoin//
// Write out the mailing list.
//
void WriteMailingList(void)
{
	FILE *fd = fopen(MAILLIST_FNAME, "wt");
	if (!fd) {
		printf("**** ERROR: Cannot open %s for writing\n", MAILLIST_FNAME);
		exit(10);
	}

	struct tm tm;
	zstruct(tm);
	// change below for threshold date by adjusting fixed values
	tm.tm_year = 2000-1900;
	tm.tm_mon = 6-1;
	tm.tm_mday = 26;
	time_t max_login_time = mktime(&tm);
	char log_filename[_MAX_PATH];
	zstruct(log_filename);

	int count = 0;
	int identical_emails = 1;
	for (int i=0; i < list_size; i += identical_emails) {
		SDBRecord *r = email_list[i];
		// If this record shares an email address with the next record, we
		// need to treat them all as a group and perform some extra
		// checks.
		identical_emails = 1;	// default to 1 (us).
		int j=i+1;
		while (j < list_size && !stricmp(r->email_address, email_list[j]->email_address)) {
			j++;
			identical_emails++;
		}

	  #if 0	// 2022 kriskoin
		if (identical_emails > 2) {
			printf("Found dup (%d): %s\n", identical_emails, r->email_address);
		}
	  #endif
		if (!ValidEmailAddress(r)) {
			// Email address does not look valid.  Don't send.
			continue;
		}

		// Now we know how many records have identical email addresses.
		// Go through each and if any are auto-blocked or set to
		// 'no news', skip the whole batch.
		int skip_this_one = FALSE;
		for (j=0 ; j<identical_emails ; j++) {
			SDBRecord *r2 = email_list[i+j];
			WORD32 flags = r2->flags;
			if (flags & SDBRECORD_FLAG_AUTO_BLOCK) {
				skip_this_one = TRUE;
				break;
			}
			if (flags & SDBRECORD_FLAG_NO_NEWS) {
				skip_this_one = TRUE;
				break;
			}
			if (flags & SDBRECORD_FLAG_EMAIL_BOUNCES) {
				skip_this_one = TRUE;
				break;
			}
			// Filter them out so that we only get the ones who have not logged in
			// since a particular date,
		  #if 0	// !!! everyone will get this if it's #if zeroed
			if ((time_t)r2->last_login_times[0] >= max_login_time) {
				// logged in too recently.
				skip_this_one = TRUE;
				break;
			}
		  #else
			NOTUSED(max_login_time);
		  #endif
		}

		if (skip_this_one) {
			continue;

		}

		// Now try to pick the best account to customize the email for...
		// Priorities are:
		//	1) not locked out
		//	2) real money over play money
		//	3) the one that logged in most recently
		SDBRecord *e = NULL;	// the best potential account so far
		for (j=0 ; j<identical_emails ; j++) {
			SDBRecord *r2 = email_list[i+j];
			if (r2->flags & SDBRECORD_FLAG_LOCKED_OUT) {
				continue;	// this one is no good.
			}
			if (e && e->priv > r2->priv) {
				continue;	// we've already got a candidate with higher priv level
			}
			if (e && e->last_login_times[0] > r2->last_login_times[0]) {
				continue;	// we've already got a candidate used more recently
			}
			e = r2;
		}
		if (e) {
		  #if 0// 2022 kriskoin
			if (identical_emails > 2) {
				printf("Found dup (%d): %s\n", identical_emails, r->email_address);
			}
		  #endif
			// Add them to the list!
			struct tm *t = localtime((time_t *)&e->account_creation_time);
		  #if 0
			fprintf(fd, "%s\t%s\t%s\t%d/%02d/%02d\n",
					e->email_address,
					e->user_id,
					e->full_name,
					t->tm_year+1900, t->tm_mon+1, t->tm_mday);
		  #else	// format for bulkmail importer
			NOTUSED(t);
			// Remove quotes from email address and name... they're bad when outputting
			// in this format.
			char *s = e->full_name;
			while (*s) {
				if (*s=='"') {
					*s = '\'';
				}
				s++;
			}
			fprintf(fd, "\"%s\",\"%s\"\n",
					e->email_address,
					e->full_name);
			sprintf(log_filename, "list%d.txt", (count/10005)+1);	// break up into lists of 10005
			AddToLog(log_filename, NULL, "\"%s\",\"%s\"\n",	e->email_address, e->full_name);

		  #endif
			count++;
		}

	}
	fclose(fd);
	printf("A total of %d records were written to %s\n", count, MAILLIST_FNAME);
}	

//*********************************************************
// https://github.com/kriskoin//
// Write out the iTimeZones[] array
//
void WriteTimeZones(void)
{
	FILE *fd = fopen(TIMEZONE_FNAME, "wt");
	if (fd) {
		fprintf(fd, "Time Zone\t# of accounts\n");
		for (int i=0 ; i<24 ; i++) {
			int z = i-12;
			fprintf(fd, "%d\t%d\n", z, iTimeZoneCounts[i]);
		}
		fclose(fd);
	}
}



#if 1
/**********************************************************************************
 MAIN()
 date: kriskoin 2019/01/01***********************************************************************************/
int main(int argc, char *argv[])
{
	if (argc ==1) {
		printf("\nDBFilter (%s): Sifts through a Paradise Poker database\n", __DATE__);
		printf("USAGE:     DBFilter {input_file_name} [/top100] \n");
		printf("           input_file_name is the database being sifted\n");
		printf("           output is to stdout\n");
		printf("           /top100     writes out %s\n\n", TOP100_HTML_FILENAME);
		printf("           /xls        writes out a txt spreadsheet file%s\n", XLS_FILENAME);
		printf("           /userid {UserID} {CheckAmt} writes details for that UserID\n\n");
		printf("           /flopcards  needs a dummy parameter and takes a filelist of .hal files\n");
		printf("              XX /flopcards {filelist}\n");
		printf("           /maillist   writes out %s for mailing lists\n", MAILLIST_FNAME);
		printf("           /labels	   writes out %s for mailing lists\n", LABELS_FNAME);
		printf("           /countries  display players per country\n");
		printf("           /WriteDataProfile  src_fname    Reads data-?.bin files and writes data-?.txt files\n");
		printf("\n");
		return 1;
	}

	static char full_filename[MAX_FNAME_LEN];
	strnncpy(full_filename, argv[1], MAX_FNAME_LEN);
	
	printf("DBFilter - compiled "__DATE__", "__TIME__"\n");	
	int i;
	for (i=2; i < argc; i++) {
		if (!stricmp(argv[i], "/top100")) {
			Top100 = TRUE;
			printf("Writing out %s\n", TOP100_HTML_FILENAME);
		} else if (!stricmp(argv[i], "/xls")) {
			WriteXLS = TRUE;
			printf("Writing out %s\n", XLS_FILENAME);
		} else if (!stricmp(argv[i], "/flopcards")) {
			ParseFlopCards(argv[i+1]);
			return 0;	// exit once we're done
		} else if (!stricmp(argv[i], "/maillist")) {
			iWriteMailingList = TRUE;
		} else if (!stricmp(argv[i], "/labels")) {
			iWriteLabels = TRUE;
		} else if (!stricmp(argv[i], "/countries")) {
			iWriteCountries = TRUE;
		} else if (!stricmp(argv[i], "/userid")) {
			FindUserID = TRUE;
			strcpy(szFindUserID, argv[i+1]);
			strcpy(szCheckAmt,argv[i+2]);
			i++;
			i++;
			printf("Writing out %s\n", XLS_FILENAME);
		} else if (!stricmp(argv[i], "/WriteDataProfile")) {
			WriteDataProfile(argv[i+1]);
			i++;
		} else {
			printf("Unknown command line parameter: %s\n", argv[i]);
		}
	}

	// Determine the size of the database file...
	FILE *fd = fopen(argv[1], "rb");
	if (!fd) {
		printf("*** ERROR: Could not open %s\n", argv[1]);
		exit(10);
	}
	fseek(fd, 0, SEEK_END);
	int total_db_size = ftell(fd) / sizeof(SDBRecord);
	fclose(fd);
	if (total_db_size > MAX_DB_SIZE) {
		printf("*** ERROR: Cannot handle a database with %d records (max is %d)\n", total_db_size, MAX_DB_SIZE);
		exit(10);
	}
	// remove old log files
	remove(MAXEDOUT_FNAME);
	remove(XLS_FILENAME);

	printf("%s Initializing SDB on %s (%d records)\n", TimeStr(), argv[1], total_db_size);

	char *p = strchr(argv[1], '.');	// only up to ext
	if (p) {
		*p = 0;
	}

	SimpleDataBase SDB(argv[1], total_db_size);

	printf("%s Scanning database...\n", TimeStr());
	int real_money_accounts = 0;

	SDBRecord *r = SDB._SDB;	// start at the beginning
	//printf("%s r = $%08lx\n", _FL, r);
	for (i=0; i < total_db_size; i++, r++) {
		if (r->valid_entry) {	// found someone
		  #if 0
			if (r->priv != ACCPRIV_PLAY_MONEY) {
				printf("%s doesn't have ACCPRIV_PLAY_MONEY\n", r->user_id);
			}
		  #endif
			if ((r->real_in_bank < 0 || r->real_in_play < 0) && r->priv < ACCPRIV_ADMINISTRATOR) {
				char str1[MAX_CURRENCY_STRING_LEN];
				char str2[MAX_CURRENCY_STRING_LEN];
				printf("*** ERROR Found %-12s (%15s real in bank, %s real in play)\n", 
					r->user_id,
					CurrencyString(str1, r->real_in_bank, CT_REAL, TRUE),
					CurrencyString(str2, r->real_in_play, CT_PLAY, TRUE));
			}
		  #if 1
			if (!stricmp(r->user_id,"Marketing") ||
				!stricmp(r->user_id,"ChecksIssued") ||
				!stricmp(r->user_id,"Prizes") ||
				!stricmp(r->user_id,"EcashFee") ||
				!stricmp(r->user_id,"DraftsIn") ) {
					char str1[MAX_CURRENCY_STRING_LEN];
					printf("*** %-12s (balance %15s)\n", 
						r->user_id,
						CurrencyString(str1, r->real_in_bank+r->real_in_play, CT_REAL, TRUE));
			}
		  #endif

			real_list[list_size] = r;
			fake_list[list_size] = r;
			email_list[list_size] = r;
			list_size++;
			if (r->priv==ACCPRIV_REAL_MONEY) {
				real_money_accounts++;
			}
		}
	}
	printf("%s Sorting %d entries...\n", TimeStr(), list_size);
	printf("%s    play...\n", TimeStr());
	QSort(list_size, fake_list_compare,  fake_list_swap,  NULL);
	printf("%s    real...\n", TimeStr());
	QSort(list_size, real_list_compare,  real_list_swap,  NULL);
	printf("%s    email...\n", TimeStr());
	QSort(list_size, email_list_compare, email_list_swap, NULL);
	printf("%s Done sorting.\n", TimeStr());

	char str[100] = " ";
	int count = 0;
	int total_ecash_fee = 0;
	int total_in_players_hands = 0;
	int males = 0, females = 0;
	printf("%s Looping through %d entries... (%d real money accounts)\n", TimeStr(), list_size, real_money_accounts);
	long total_wires_plus_drafts = 0;

	for (i=0; i < list_size; i++) {

		int include_in_count = TRUE;
		if (real_list[i]->priv >= ACCPRIV_CUSTOMER_SUPPORT) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Ecash")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Ecash1")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Ecash2")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Rake")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Rake1")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Rake2")) {
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"BlackHole")) {

			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"Marketing"))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"Tournaments"))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"ChecksIssued"))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"Pending"))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"Misc."))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"ChargeBack"))
			include_in_count = FALSE;
		if (!stricmp(real_list[i]->user_id,"EcashFee"))  {
			total_ecash_fee = -real_list[i]->real_in_bank;
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"WiresIn"))  {
			total_wires_plus_drafts += abs(real_list[i]->real_in_bank);
			include_in_count = FALSE;
		}
		if (!stricmp(real_list[i]->user_id,"DraftsIn"))  {
			total_wires_plus_drafts += abs(real_list[i]->real_in_bank);
			include_in_count = FALSE;
		}

		if (include_in_count) {
			total_in_players_hands += real_list[i]->real_in_bank + 
									  real_list[i]->real_in_play;
		}

	  #if 0	// enable to look for a comment within the notes field
		static char uncomp_notes[MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED];
		zstruct(uncomp_notes);
		SDBRecord *sdbr = real_list[i];
		UncompressString(sdbr->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN, uncomp_notes, MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED);
		const char *str_to_look_for = "itchen";
		if (strstr(uncomp_notes, str_to_look_for)) {
			printf("Found '%s' : player %s\n", str_to_look_for, sdbr->user_id);
		}
	  #endif

		
		if (include_in_count) {
			// count people
			total_people++;
			if (real_list[i]->gender == GENDER_MALE) {
				males++;
			} else if (real_list[i]->gender == GENDER_FEMALE) {
				females++;
			} else {
				printf("Unknown gender for %-16s\n", real_list[i]->user_id);
			}
			// check if maxed out for purchases
			CheckMaxedOut(real_list[i]);

			// count countries
			if (iWriteCountries && real_list[i]->transaction[0].timestamp) {
				char *c = real_list[i]->mailing_address_country;
				int found_country = FALSE;
				int first_blank = -1;
				for (int ct=0; ct < MAX_COUNTRIES; ct++) {
					if (!stricmp(Country[ct].name, c)) {	// found it
						found_country = TRUE;
						Country[ct].count++;
						country_people++;
						strnncpy(Country[ct].last_player_seen, real_list[i]->user_id, MAX_PLAYER_USERID_LEN);
						break;
					}
					if (first_blank < 0 && !Country[ct].name[0]) {
						first_blank = ct;
					}
				}
				if (!found_country) {
					if (first_blank >= 0) {
						different_countries++;
						strnncpy(Country[first_blank].name, c, MAX_COMMON_STRING_LEN);
						Country[first_blank].count++;
						country_people++;
						strnncpy(Country[first_blank].last_player_seen, real_list[i]->user_id, MAX_PLAYER_USERID_LEN);
					} else {
						printf("Ran out of room for countries (MAX %d)\n", MAX_COUNTRIES);
					}
				}
			}
		}

	  #if 0	// 2022 kriskoin
		if ((real_list[i]->flags & SDBRECORD_FLAG_NO_CASHIER) &&
			 real_list[i]->priv <= ACCPRIV_REAL_MONEY &&
		   !(real_list[i]->flags & SDBRECORD_FLAG_LOCKED_OUT))
		{
			printf("User id %-16s has their cashier functions disabled.\n", real_list[i]->user_id);
		}
	  #endif

		if (i) {
			if (stricmp(str, fake_list[i]->email_address)) {
				strncpy(str, fake_list[i]->email_address, 50);
				count++;
			}
#if 1
			if (WriteXLS && fake_list[i]->priv <= ACCPRIV_REAL_MONEY) {
				AddXLSEntry(fake_list[i]);
			}
#endif
/*
	char   mailing_address1[MAX_PLAYER_ADDRESS_LEN];	// 1st line of mailing address
	char   mailing_address2[MAX_PLAYER_ADDRESS_LEN];	// 2nd line of mailing address
	char   mailing_address_state[MAX_COMMON_STRING_LEN];
	char   mailing_address_country[MAX_COMMON_STRING_LEN];
	char   mailing_address_postal_code[MAX_COMMON_STRING_LEN];
*/
			
			
			if (FindUserID && !stricmp(real_list[i]->user_id, szFindUserID)) {
				char str[500];
				zstruct(str);
			  #if 0	// full version
				sprintf(str, "=== %s =========================================================\n\nCHECK: $%s\n"
					"\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n\n",
						real_list[i]->user_id,  szCheckAmt, real_list[i]->full_name,
						real_list[i]->mailing_address1, real_list[i]->mailing_address2, 
						real_list[i]->city,
						real_list[i]->mailing_address_state, real_list[i]->mailing_address_country, 
						real_list[i]->mailing_address_postal_code);
			  #else	// short version
				sprintf(str, "%s,%s,%s\n", 	real_list[i]->user_id, real_list[i]->full_name, szCheckAmt);
  			  #endif
				printf(str);
				FILE *out = fopen("checks.out","at");
				fprintf(out,str);
				fclose(out);
			}

#if 0
			if (!ValidEmailAddress(fake_list[i])) {
//				printf("REJECTED: %s (%s) [%c]\n", str, fake_list[i]->user_id, 
//					fake_list[i]->flags & SDBRECORD_FLAG_EMAIL_INVALID ? 'I' : 'V');
				continue;
			}
			if (fake_list[i]->dont_use_email2) {
//				printf("DON'T TELL ME REAL MONEY: %s (%s)\n", str, fake_list[i]->user_id);
				continue;
			}
			if (fake_list[i]->flags & SDBRECORD_FLAG_NO_NEWS) {
//				printf("DON'T TELL NEWS: %s (%s)\n", str, fake_list[i]->user_id);
				continue;
			}
			if (fake_list[i]->flags & SDBRECORD_FLAG_LOCKED_OUT) {
//				printf("LOCKED OUT: %s (%s)\n", str, fake_list[i]->user_id);
				continue;
			}
			if (fake_list[i]->flags & SDBRECORD_FLAG_EMAIL_INVALID) {
//				printf("EMAIL INVALID: %s (%s)\n", str, fake_list[i]->user_id);
				continue;
			}
			// OK
//			printf("%s%c%s%c%s\n",str,9,fake_list[i]->user_id, 9, fake_list[i]->full_name);
			
			// only real money
			// total deposits -- add them up, how many, total cashouts, how many
			printf("%s%c%s%c%s%c%s\n",str,9,real_list[i]->user_id, 9, real_list[i]->full_name, 9, real_list[i]->city);
#endif
		}
	// play chip leaders
	}
	if (Top100) {
		WriteOutTop100PlayMoney();
	}
	// add up everything in PENDING fields...
	int total_pending_refund = 0 ;
	for (int j=0; j < list_size; j++) {
		total_pending_refund += real_list[j]->pending_fee_refund;
	}
	char cs[MAX_CURRENCY_STRING_LEN];
	printf("File date: %s\n", FileLastModifiedStr(full_filename));
	printf("%12s Total pending CC fee refund (total pending in player accounts)\n", CurrencyString(cs, total_pending_refund, CT_REAL, FALSE, 1));
	printf("%12s Total CC fees refunded (total fees less total pending)\n", CurrencyString(cs, total_ecash_fee - total_pending_refund, CT_REAL, FALSE, 1));
	printf("%12s Total on deposit from players (in their hands)\n", CurrencyString(cs, total_in_players_hands, CT_REAL, FALSE, 1));
	printf("%d players, %d male (%2.2f%%)    %d female (%2.2f%%)\n",
		total_people, males,
		(100.0*males/total_people),
		females,
		(100.0*females/total_people));
	
	// write out maxed purchase totals
	if (potential_maxed_out_count) {
		char tmp[100];
		zstruct(tmp);
		sprintf(tmp, "\nTotal players at default low limits, logged in recently : %d", potential_maxed_out_count);
		printf("%s\n", tmp);
		AddToLog(MAXEDOUT_FNAME, NULL, "%s\n", tmp);
		sprintf(tmp, "Maxed out 24h: %d (%2.2f%%)", maxed_count[0], (100.0*maxed_count[0]/potential_maxed_out_count));
		printf("%s\n", tmp);
		AddToLog(MAXEDOUT_FNAME, NULL, "%s\n", tmp);
		sprintf(tmp, "Maxed out 7d: %d (%2.2f%%)", maxed_count[1], (100.0*maxed_count[1]/potential_maxed_out_count));
		printf("%s\n", tmp);
		AddToLog(MAXEDOUT_FNAME, NULL, "%s\n", tmp);
		sprintf(tmp, "Maxed out 30d: %d (%2.2f%%)", maxed_count[2], (100.0*maxed_count[2]/potential_maxed_out_count));
		printf("%s\n", tmp);
		AddToLog(MAXEDOUT_FNAME, NULL, "%s\n", tmp);
	}

  #if 0	//kriskoin: 	long total_cc = (long)((double)total_ecash_fee / .0525);
	printf("%12s Total ever purchased by players on CC\n", CurrencyString(cs, total_cc, CT_REAL, FALSE, 1));
	printf("%12s Total ever deposits (cc+drafts+wires)\n", CurrencyString(cs, total_cc + total_wires_plus_drafts, CT_REAL, FALSE, 1));
	printf("%12s Total cash currently in play (at tables)\n", CurrencyString(cs, SDB.cash_in_play_at_startup, CT_REAL, FALSE, 1));
	printf("%12s Total cash in account for players currently at tables\n", CurrencyString(cs, SDB.cash_in_accounts_at_tables_at_startup, CT_REAL, FALSE, 1));
  #endif

	//AnalyzeTransactions();

	WriteTextFileForSQL();
	WriteOutEmailXref();
	if (iWriteMailingList) {
		WriteMailingList();
	}
	if (WriteXLS) {
		WriteTimeZones();
	}
	if (iWriteLabels) {
		WriteLabels();
	}
	if (iWriteCountries) {
		WriteCountries();
	}
	return 0;
}
#else 
/**********************************************************************************
 Function main() ** ALTERNATE **
 date: 24/01/01 kriskoin Purpose: use for quick hacks
***********************************************************************************/
int main(int argc, char *argv[])
{
	char data_line[210];
	zstruct(data_line);
	FILE *in = fopen(argv[1], "rt");
	FILE *out = fopen(argv[2], "wt");
	while (!feof(in)) {
		fgets(data_line, 209, in);
		if (strstr(data_line, "14596557") && strstr(data_line, "richard466m6")) {
			break;
		} else {
			fprintf(out,"%s",data_line);
		} 
	}
	fclose(out);
	fclose(in);
	return 0;
}


#endif

#if 1
/**********************************************************************************
 Function AddXLSEntry(SDBRecord *r)
 Date: 2017/7/7 kriskoin Purpose: create an importable spreadsheet file
***********************************************************************************/
void AddXLSEntry(SDBRecord *r)
{
	
	char curr_str1[50];
	char curr_str2[50];
	char curr_str3[50];
	int account_balance = 0, total_deposits = 0, number_deposits = 0;
	int exposure = 0, diff_cards = 0, total_credits = 0, number_credits = 0;
	
	if (r->priv > ACCPRIV_PLAY_MONEY) {	// real money ready? 

		account_balance = r->real_in_bank+r->real_in_play;
		WORD32 cc[TRANS_TO_RECORD_PER_PLAYER];	// keep track of CCs used
		zstruct(cc);

		for (int i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
			ClientTransaction *ct = &(r->transaction[i]);
			// how many different cards were used?
			int found = FALSE;
			for (int x=0; x < TRANS_TO_RECORD_PER_PLAYER; x++) {
				if (r->transaction[i].partial_cc_number == cc[x]) {
					found = TRUE;
				}
			}
			if (!found) {
				cc[diff_cards++] = ct->partial_cc_number;
			}
			// add up other totals
			if (r->transaction[i].transaction_type == CTT_PURCHASE) {
				number_deposits++;
				total_deposits += r->transaction[i].transaction_amount;
				exposure += r->transaction[i].credit_left;
			}
			if (r->transaction[i].transaction_type == CTT_CREDIT || 
				r->transaction[i].transaction_type == CTT_CHECK_ISSUED) {
				number_credits++;
				total_credits += r->transaction[i].transaction_amount;
			}

		}
		// Zip	Name	 TotalDep	 email	 Add1	 UserID	
		AddToLog(XLS_FILENAME, "Zip\t TotalDep\t Email\t Add1\t UserID\t Name\t Add2\t City\t State\t Ctr\t AccBalance\t NumDep\t DiffCCard\t TotalCred\t NumCred\t Hands Seen\t TimeZone\n",
							   "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t"
							   "%s\t%s\t%d\t%d\t"
							   "%s\t%d\t%d\t%+d\n",
							   r->mailing_address_postal_code ? r->mailing_address_postal_code : " ", 
							   CurrencyString(curr_str2, total_deposits, CT_REAL, TRUE), 
							   r->email_address ? r->email_address : " ",
							   r->mailing_address1 ? r->mailing_address1 : " ", 
							   r->user_id ? r->user_id : " ",

							   r->full_name ? r->full_name : " ", 
							   r->mailing_address2 ? r->mailing_address2 : " ", 
							   r->city ? r->city : " ", 
							   r->mailing_address_state ? r->mailing_address_state : " ",
							   r->mailing_address_country ? r->mailing_address_country : " ", 
							   CurrencyString(curr_str1, account_balance, CT_REAL, TRUE),
							   number_deposits, diff_cards, 
							   CurrencyString(curr_str3,total_credits, CT_REAL, TRUE),
							   number_credits,
							   r->hands_seen,
							   r->client_platform.time_zone*450/3600);
		int time_zone = r->client_platform.time_zone*450/3600+12;
		if (time_zone < 0) {
			time_zone += 24;
		}
		if (time_zone >= 24) {
			time_zone -= 24;
		}
		if (time_zone >= 0 && time_zone < 24) {
			iTimeZoneCounts[time_zone]++;
		}
		char exp_per_hand[20];
		zstruct(exp_per_hand);
		if (r->hands_seen) {
			CurrencyString(exp_per_hand, exposure/r->hands_seen, CT_REAL, TRUE);
		}
		char exp_per_diff_card[20];
		zstruct(exp_per_diff_card);
		if (diff_cards) {
			CurrencyString(exp_per_diff_card, exposure/diff_cards, CT_REAL, TRUE);
		}
		float flop_percentage = 0.0;
		if (r->hands_seen) {
			flop_percentage = (float)(100*r->flops_seen/r->hands_seen);
		}

		if (r->priv == ACCPRIV_REAL_MONEY) {
			AddToLog("security.txt", "UserID\t Flop%%\t AccBal\t #CCs\t Exp\t Hands\n",
				"%s\t %2.0f%%\t %s\t %d\t %s\t %d\n",
					r->user_id ? r->user_id : " ",
					flop_percentage,
					CurrencyString(curr_str1, account_balance, CT_REAL, TRUE),
					diff_cards,
					CurrencyString(curr_str2, exposure, CT_REAL, TRUE),
					r->hands_seen);
		}
	}							
}
#endif
/**********************************************************************************
 Function ValidEmailAddress(SDBRecord ar)
 Date: 2017/7/7 kriskoin Purpose: validate an email adress
 Returns: T/F if it's valid
 Note:    Grabbed directly from Mike's account validation stuff in prefs.cpp (client)
***********************************************************************************/
int ValidEmailAddress(SDBRecord *ar)
{

	// Perform some more checks on the email address to make sure it's close to valid.
	// It must have exactly one '@'
	char *p1 = strchr(ar->email_address, '@');
	char *p2 = strrchr(ar->email_address, '@');
	if (!p1 || p1 != p2) {
		return FALSE;
	}

	// No commas, no spaces, semicolon, colon, brackets, etc.
	if (strpbrk(ar->email_address, " ,;:[]()*<>%!")) {
		return FALSE;
	}

	// There must be something before the '@'
	if (p1==ar->email_address) {
		return FALSE;
	}
	int domain_ok = TRUE;	// default to TRUE.
	// There must be at least one '.' after the @
	p2 = strrchr(p1, '.');	// locate final '.'
	if (!p2) {
		domain_ok = FALSE;
	} else {
		// There must be at least two alphas after the final .
		if (strlen(p2) < 3) {	// 3 = '.' plus two more characters.
			domain_ok = FALSE;
		}
	}
	// There must be at least one alpha after the @ (before the final '.')
	p2 = p1+1;
	while (*p2 && *p2!='.' && !isalpha(*p2)) {
		p2++;
	}
	if (!isalpha(*p2)) {
		domain_ok = FALSE;
	}
	if (!domain_ok) {
		return FALSE;
	}
	return TRUE;
}
/**********************************************************************************
 Function WriteOutEmailXref(void)
 Date: 2017/7/7 kriskoin
 Purpose: write out the email cross-ref file
 NOTE: 24/01/01 kriskoin:
***********************************************************************************/
void WriteOutEmailXref(void)
{
	FILE *out = fopen("PPXref.txt", "wt");
	FILE *out2 = fopen("emailref.txt", "wt");
	if (!out || !out2) {
		printf("ERROR - Couldn't open file PPXref.txt or emailref.txt for output\n");
		return;
	}
	for (int i=0; i < list_size; i++) {
		SDBRecord *r = email_list[i];
		if (r && r->priv <= ACCPRIV_REAL_MONEY) {
			char gc = '?';
			if (r->gender == GENDER_MALE) gc = 'M';
			if (r->gender == GENDER_FEMALE) gc = 'F';
			fprintf(out,"%-28s%-16s%-20s %c %6d hands seen\n",
					r->email_address, r->user_id, r->full_name, gc, r->hands_seen);
			fprintf(out2,"%08lx\t%d\t%d\t%s\t%s\t%s\n",
				r->player_id,
				r->last_login_times[0],
				r->hands_seen,
				r->user_id,
				r->email_address,
				r->full_name);
		}
	}
	fclose(out);
	fclose(out2);
}

/**********************************************************************************
 Function AnalyzeTransactions(void)
 date: 24/01/01 kriskoin Purpose: look into purchases/credits
***********************************************************************************/
void AnalyzeTransactions(void)
{

  #if 0	// calculate "life" of each transaction
	for (int i=0; i < list_size; i++) {
		if (real_list[i]) {
			SDBRecord *r = real_list[i];
//			printf("%s - ", r->user_id);
			// dig into each transaction
			for (int j=0; j < TRANS_TO_RECORD_PER_PLAYER; j++) {
				ClientTransaction *ct = &(r->transaction[j]);
				if (ct->transaction_type == CTT_PURCHASE) {
//					printf("P: %d ", ct->ecash_id);
					// it's a purchase, check for corresponding credit
					for (int k=0; k < TRANS_TO_RECORD_PER_PLAYER; k++) {
						ClientTransaction *kt = &(r->transaction[k]);
						if (kt->transaction_type == CTT_CREDIT && kt->ecash_id == ct->ecash_id) {
							printf("%s - ID: %d = %.0f\n", 
								r->user_id, ct->ecash_id, difftime(kt->timestamp, ct->timestamp));
						}
					}
				}
			}
		}
	}
  #endif
	#define HOURS_TO_TRACK	99
	static hours[HOURS_TO_TRACK];
	int total_entries = 0;
	for (int i=0; i < list_size; i++) {
		if (real_list[i]) {
			SDBRecord *r = real_list[i];
			// printf("%s - ", r->user_id);
			// dig into each transaction - start at oldest
			int stop = TRANS_TO_RECORD_PER_PLAYER;	// reset lookback
			for (int j=TRANS_TO_RECORD_PER_PLAYER-1; j >= 0; j--) {
				ClientTransaction *ct = &(r->transaction[j]);
				if (ct->transaction_type == CTT_CREDIT || ct->transaction_type == CTT_CHECK_ISSUED) {
					// it's a credit, find next purchase up to last we acted on

					for (int k=j; k >= 0; k--) {
						ClientTransaction *kt = &(r->transaction[k]);
						if (kt->transaction_type == CTT_PURCHASE && j < stop) {
							int hour = (int)(difftime(kt->timestamp, ct->timestamp)/3600);
							printf("%s - %d\n", r->user_id, hour);
							// change this credit so we don't las current thing to keep going from
							stop = k;
							if (hour >= HOURS_TO_TRACK) {	// squeeze all at end
								hour = HOURS_TO_TRACK-1;
							}
							hours[hour]++;
							total_entries++;
						}
					}
				}
			}
		}
	}
	int cummul = 0;
	for (int l=0; l < HOURS_TO_TRACK; l++) {
		cummul += hours[l];
		printf("less than %02d hours (%3d/%4d): %3.1f%%   (cummulative = %3.1f%%)\n", l+1, hours[l], total_entries,
			(float)(100.0*(float)((float)hours[l]/(float)total_entries)), 
			(float)(100.0*(float)((float)cummul/(float)total_entries)));
	}
}

/**********************************************************************************
 Function WriteOutTopPlayMoneyTable(void)
 Date: 2017/7/7 kriskoin Purpose: writes out an HTML table of the top 
***********************************************************************************/
void WriteOutTop100PlayMoney(void)
{
	#define TOP_HOW_MANY	500	
	FILE *out = fopen(TOP100_HTML_FILENAME, "wt");
	FILE *out2 = fopen(TOP500_HTML_FILENAME, "wt");
	char szDays[7][10] = { "Sunday", "Monday", "Tuesday", "Wednesday", 
							"Thursday", "Friday", "Saturday" } ;
	
	time_t tt;
	time ( &tt );
	struct tm *t = localtime(&tt);
	// int day_index = t->tm_wday;
	int prevday_index = t->tm_wday-1;
	if (prevday_index < 0) prevday_index = 6;
	int hour;
	char szAMPM[3];
	if (t->tm_hour > 12) {
		hour = t->tm_hour - 12;
		sprintf(szAMPM, "pm");
	} else {
		hour = t->tm_hour;
		sprintf(szAMPM, "am");
	}
	// exceptions
	if (t->tm_hour == 0) {
		hour = 12;
		sprintf(szAMPM, "am");
	}
	if (t->tm_hour == 12) {
		hour = 12;
		sprintf(szAMPM, "pm");
	}

	// pre-table headers
	fprintf(out,"<blockquote> <div align=\"center\"><center><table border=\"0\" cellspacing=\"15\" width=\"80%\">\n");
	fprintf(out,"<tr><td><font size=\"2\" face=\"Verdana\"><strong>\n");
	fprintf(out,"As of late %s night</strong> <br><br> </font>\n", szDays[prevday_index]);
	fprintf(out,"<table border=\"1\" cellpadding=\"3\" cellspacing=\"4\">\n");

	fprintf(out2,"<blockquote> <div align=\"center\"><center><table border=\"0\" cellspacing=\"15\" width=\"80%\">\n");
	fprintf(out2,"<tr><td><font size=\"2\" face=\"Verdana\"><strong>\n");
	fprintf(out2,"As of late %s night</strong> <br><br> </font>\n", szDays[prevday_index]);
	fprintf(out2,"<table border=\"1\" cellpadding=\"3\" cellspacing=\"4\">\n");

	char sznum[8];
	int entry = 0;
	for (int i=0; ; i++) {
		if (!fake_list[i]) continue;
		// filter out certain IDs
		if (fake_list[i]->flags & SDBRECORD_FLAG_LOCKED_OUT) continue;

		//kriskoin: 		if (fake_list[i]->priv > ACCPRIV_REAL_MONEY) {
			continue;
		}

		if (strstr(fake_list[i]->user_id, "dmin")) continue;
		if (strstr(fake_list[i]->user_id, "possoff")) continue;
		if (strstr(fake_list[i]->user_id, "MusicMan")) continue;
		// if (strstr(fake_list[i]->user_id, "Mike")) continue;
		if (strstr(fake_list[i]->user_id, "Masher")) continue;
		if (strstr(fake_list[i]->user_id, "Misc.")) continue;

		if (entry == TOP_HOW_MANY) break;	
		if (entry < 9) {
			sprintf(sznum,"# %d", entry+1);
		} else {
			sprintf(sznum,"#%d", entry+1);
		}
		if (!entry) {
			// #1 player
			fprintf(out,"<tr><td><font size=\"3\" face=\"Verdana\"><strong># 1</strong></font></td><td width=\"200\"><font size=\"3\" face=\"Verdana\"><strong>\n");
			fprintf(out,"%s</strong></font></td><td><font size=\"3\" face=\"Verdana\"><strong>%d</strong></font></td></tr>\n",
				fake_list[i]->user_id, (fake_list[i]->fake_in_bank+fake_list[i]->fake_in_play)/100);
			fprintf(out2,"<tr><td><font size=\"3\" face=\"Verdana\"><strong># 1</strong></font></td><td width=\"200\"><font size=\"3\" face=\"Verdana\"><strong>\n");
			fprintf(out2,"%s</strong></font></td><td><font size=\"3\" face=\"Verdana\"><strong>%d</strong></font></td></tr>\n",
				fake_list[i]->user_id, (fake_list[i]->fake_in_bank+fake_list[i]->fake_in_play)/100);
		} else {
			if (entry < 100) {	// top 100 is out, top 500 is out2
				fprintf(out,"<tr><td><font size=\"2\" face=\"Verdana\">%s</font></td><td width=\"200\"><font size=\"2\" face=\"Verdana\">%s</font></td>\n", sznum, fake_list[i]->user_id);
				fprintf(out,"<td><font size=\"2\" face=\"Verdana\">%d</font></td></tr>\n", (fake_list[i]->fake_in_bank+fake_list[i]->fake_in_play)/100);
			}
			fprintf(out2,"<tr><td><font size=\"2\" face=\"Verdana\">%s</font></td><td width=\"200\"><font size=\"2\" face=\"Verdana\">%s</font></td>\n", sznum, fake_list[i]->user_id);
			fprintf(out2,"<td><font size=\"2\" face=\"Verdana\">%d</font></td></tr>\n", (fake_list[i]->fake_in_bank+fake_list[i]->fake_in_play)/100);
		}
		entry++;
	}                
	// post table stuff
	fprintf(out,"</table></td></tr></table></center></div></blockquote>\n");
	fprintf(out2,"</table></td></tr></table></center></div></blockquote>\n");
	fclose(out);
	fclose(out2);
}

/**********************************************************************************
 Function GetArgs(int maxArgc, char *argv[], char *string, char seperator)
 date: kriskoin 2019/01/01 Purpose: parser
***********************************************************************************/
int GetArgs(int maxArgc, char *argv[], char *string, char seperator)
{
	memset(argv, 0, sizeof(argv[0])*maxArgc);	// start by setting all ptrs to null
	int argc = 0;
	while (*string) {
		argv[argc++] = string;
		if (argc >= maxArgc)
			return -1;
		while (*string && *string != seperator)
			++string;
		if (!*string)
			break;
		*(string++) = '\0';
	}
	return argc;
}

#if 0
	static int cards[MAX_GAMES][5];
#endif

/*#if 0

	[ ] card stats:
	[ ] game #
	[ ] # of plrs who got cards
	[ ] game type (omit stud games)
	[ ] all the public cards (3-5)
#endif*/


#define FLOP_CARDS	5
#define MAX_PLAYERS_PER_GAME	10

typedef struct {
	WORD32 game_number;
	char game_type;
	unsigned char cards[FLOP_CARDS];
	char player_cards[MAX_PLAYERS_PER_GAME];	// just a count
	ChipType chip_type;
	int low_limit;
	int high_limit;
} GameCards;

#define MAX_GAMES	6000000
static GameCards *Cards;
static int cardcount[100];
static int priv_cardcount[256];

typedef struct CardCountStruct {
	long card_count[100];
} CardCountStruct;

struct CardCountStruct CardCount;

/**********************************************************************************
 Function ParseFlopCards
 date: 24/01/01 kriskoin Purpose: read/analyze/write a flopcards file
***********************************************************************************/
void ParseFlopCards(char *filename)
{
  #if 1	// read hal files (else reads flops.bin)
	// init structure
	printf("%s(%d) Initializing...\n", _FL);
	Cards = (GameCards *)malloc(sizeof(GameCards)*MAX_GAMES);
	for(int z=0; z < MAX_GAMES; z++){
		Cards[z].game_number = 0;
		Cards[z].game_type = -1;
		int zz;
		for(zz=0; zz < FLOP_CARDS; zz++){
			Cards[z].cards[zz] = CARD_NO_CARD;
		}
		for(zz=0; zz < FLOP_CARDS; zz++){
			Cards[z].player_cards[zz] = 0;
		}
	}
	zstruct(CardCount);

	#define MAX_DATALINE_LEN	300
	char dataline[MAX_DATALINE_LEN];
	#define MAX_ARGS	60
	char *argv[MAX_ARGS];
	
	int gamenum_base = 0;

	if (!filename || !strlen(filename)) {
		fprintf(stderr,"ParseFlopCards needs a filename\n");
		return;
	}
	FILE *file_list = fopen(filename, "rt");
	if (!file_list) {
		fprintf(stderr,"%s(%d) ParseFlopCards couldn't open %s\n", _FL, filename);
		return;
	}

	char date_str[20];
	zstruct(date_str);
	forever {

		if (feof(file_list)) {
			break;	// done
		}

		zstruct(dataline);
		fgets(dataline, MAX_DATALINE_LEN, file_list);
		FixPath(dataline);
		if (!dataline[0]) {
			continue;
		}
		FILE *in = fopen(dataline, "rt");
		if (in == NULL) {
			fprintf(stderr,"%s(%d) ParseFlopCards couldn't open '%s'\n", _FL, dataline);
			continue;
		} else {
			fprintf(stderr,"%s(%d) ParseFlopCards is reading '%s'\n", _FL, dataline);
		}
		// the first 8 characters are what we can use for the date
		strnncpy(date_str, dataline, 9);

//need access log (machine number) for waldo2 on the 20th, 21st, 22nd and 23rd. 

// The original flops.txt format with the addition of stakes, money type (Real, Play, or Tournament), 
// and date hand was played for the first 40 million hands would be very useful. 
// We can drop the play money hands from the data sample as you suggested.		
		while (!feof(in)) {
			zstruct(dataline);
			fgets(dataline, MAX_DATALINE_LEN, in);
			int count = GetArgs(MAX_ARGS, argv, dataline, '\t');
			// 20000201        00:00:09        04      1946787 1288    Play money 25   0       0       300     100     
			// 20000201        00:00:09        07      1946766 5       34
			// 20000201        00:00:10        07      1946771 -1      20
			if (count < 5) {	// don't want it, whatever it is
				continue;
			}
			int gamenum = atoi(argv[3]);
			if (!gamenum_base) { // first call
				gamenum_base = gamenum;
			} 
			if (gamenum < gamenum_base) { // remnants
				continue;
			}
			// ignore if it's not a game_start or card_dealt packet
			int packet_type = atoi(argv[2]);
			if (packet_type !=4 && packet_type !=7) {
				continue;
			}
			// we want the data
			int index = gamenum-gamenum_base;
			if (index >= MAX_GAMES) {	// filled the struct?
				printf("%s(%d) Ran out of room in Cards (max = %d)\n", _FL, MAX_GAMES);
				break;
			}
			int game_rules =0 ;
			int one_on_one_game = FALSE;
			switch (packet_type) {
			case 4:	// game start
				if (count < 7) {	// don't want it, whatever it is
					break;
				}
				Cards[index].game_number = gamenum;
				game_rules = (GameRules)(atoi(argv[6]));
			#ifndef OLD_HEADSUP_RULE_LOGGING_OFFSET
			  #define OLD_HEADSUP_RULE_LOGGING_OFFSET		50	// see in hand-history generation & table call to LogGameStart
			#endif
				if (game_rules > OLD_HEADSUP_RULE_LOGGING_OFFSET) {	// see table.cpp where we call LogGameStart
					game_rules -= OLD_HEADSUP_RULE_LOGGING_OFFSET;
					one_on_one_game = TRUE;
				}
				switch (game_rules) {
				// we will stagger all cases of old and new
				case 0:
				case GAME_RULES_HOLDEM:
					Cards[index].game_type = GAME_RULES_HOLDEM;
					break;
				case 1:
				case GAME_RULES_OMAHA_HI:
					Cards[index].game_type = GAME_RULES_OMAHA_HI;
					break;
				case 2:
				case GAME_RULES_STUD7:
					Cards[index].game_type = GAME_RULES_STUD7;
					break;
				case 3:	// this was the old one-on-one
					Cards[index].game_type = GAME_RULES_HOLDEM;
					break;
				case 4:
				case GAME_RULES_OMAHA_HI_LO:
					Cards[index].game_type = GAME_RULES_OMAHA_HI_LO;
					break;
				case 5:
				case GAME_RULES_STUD7_HI_LO:
					Cards[index].game_type = GAME_RULES_STUD7_HI_LO;
					break;
				default:
					fprintf(stderr, ".HAL bad data? - unknown game type(%d)", game_rules);
					exit(1);
				}
				
				
				// enum ChipType { CT_PLAY, CT_REAL, CT_NONE, CT_TOURNAMENT };
				Cards[index].chip_type = (ChipType)atoi(argv[7]);	// tournaments = 3
				Cards[index].low_limit = atoi(argv[8]);
				Cards[index].high_limit = (2*Cards[index].low_limit);
				if (one_on_one_game) {
					Cards[index].game_type += 10;
				}
				if (Cards[index].chip_type == 3) {
					Cards[index].game_type += 20;
					Cards[index].low_limit = Cards[index].low_limit % 1000000;
					Cards[index].high_limit = Cards[index].high_limit % 1000000;
				}

				{
					// re-init cards and player counts each time a game starts.
					// This is to compensate for the few repeated games
					// in mid-september.
					int zz;
					for(zz=0; zz < FLOP_CARDS; zz++){
						Cards[index].cards[zz] = CARD_NO_CARD;
					}
					for(zz=0; zz < FLOP_CARDS; zz++){
						Cards[index].player_cards[zz] = 0;
					}
				}
				break;
			case 7:	// card dealt
				if (count < 6) {	// don't want it, whatever it is
					break;
				}
				{
					for (int field_index=4 ; field_index < count ; field_index+=2) {	// loop through all pairs of plr/cards we found
						int plr_index = atoi(argv[field_index]);
						int this_card = atoi(argv[field_index+1]);
						//printf("%s(%d) index=%d gamenum=%d card=%d player=%d\n", _FL, index, gamenum, this_card, plr_index);
						if (plr_index == -1) {	// common card
							int common_card_index = 0;
							while (Cards[index].cards[common_card_index] != CARD_NO_CARD) {
								if (common_card_index > FLOP_CARDS) {
									printf("SERIOUS ERROR - found a flop with %d cards (game #%d)\n", common_card_index, gamenum);
									break;
								}
								common_card_index++;
							}
							Cards[index].cards[common_card_index] = (unsigned char)this_card;
							 //printf("Game #%d: %c%c\n", gamenum, cRanks[RANK(this_card)], cSuits[SUIT(this_card)]);
						} else {	// private card, just count it
							Cards[index].player_cards[plr_index] += 1;
							//printf("Index %d game %d: ",index, gamenum);
							for (int x =0;x<10;x++) {
								//printf("%d:%d ",x, Cards[index].player_cards[x]);
							}
							//printf("\n");
							//printf("i=%d Game #%d: %c%c plrindex = %d\n", index, gamenum, cRanks[RANK(this_card)], cSuits[SUIT(this_card)], plr_index);
							priv_cardcount[this_card]++;
						}
					}
				}
				break;
			default:
				printf("impossible\n");
				break;
			}
		}
		if (in) {
			fclose(in);
		}
	}
	// done reading
	if (file_list) {
		fclose(file_list);
	}

	printf("Writing out pcards.txt and flops.bin...\n");
	FILE *fd = fopen("pcards.txt", "wt");
	if (fd) {
		for (int i=0; i < 100; i++) {
			if (priv_cardcount[i]) {
				fprintf(fd, "%8d\t%c%c\n", priv_cardcount[i], cRanks[RANK(i)], cSuits[SUIT(i)]);
			}
		}
		fclose(fd);
	}
   #if 0	// taken out for a run because not using it and takes long to do
	WriteFile("flops.bin", Cards, sizeof(GameCards)*MAX_GAMES);
   #endif
  #else
	// Read from the pre-calculated binary file
	long bytes_read = 0;
	printf("%s(%d) Reading flops.bin...\n", _FL);
	Cards = (GameCards *)LoadFile("flops.bin", &bytes_read);
  #endif

  #if 0
	for (int i=0; i < 100; i++) {
		if (cardcount[i]) {
			printf("%d: %c%c\n", cardcount[i], cRanks[RANK(i)], cSuits[SUIT(i)]);
		}
	}
  #endif
	// build the count
	int i;
  #if 1	// 2022 kriskoin
	char fname_str[30];
	zstruct(fname_str);
	// find first active game number
	int first_game_num = 0;
	for (int p = 0;p < 10000; p++) { 
		if (Cards[p].game_number) {
			first_game_num = Cards[p].game_number;
			break;
		}
	}
	if (!first_game_num) {
		printf("ERROR:couldn't find a game number if the first 10,000 hands -- aborting\n");
		exit(1);
	}
	// do the XLS reading/writing HK & MB talked about
	char xlstext_str[30];
	zstruct(xlstext_str);
	sprintf(xlstext_str,"cards%02d.txt", (int)(((double)first_game_num/5000000.0)) );

	char xlsbin_str[30];
	zstruct(xlsbin_str);
	sprintf(xlsbin_str,"cards%02d.bin", (int)(((double)first_game_num/5000000.0)) );

	if (ReadFile(xlsbin_str, &CardCount, sizeof(CardCountStruct), NULL) != ERR_NONE) {
		zstruct(CardCount);	// being created for the first time
	}
		
	sprintf(fname_str,"flops%02d.txt", (int)(((double)first_game_num/5000000.0)) );
	printf("%s(%d) writing to flops (%s) file...\n", _FL, fname_str);
	AddToLog("flops.idx","Date\tHand #\tFile\n","%s\t%d\t%s\n", date_str, first_game_num, fname_str);
	
	FILE *out = NULL;
	out = fopen(fname_str, "at");
	if (!out) {
		fprintf(stderr, "ERROR: couldn't open %s for writing (append)\n", fname_str);
		exit(1);
	}
	for (i=0; i < MAX_GAMES; i++) {
		// ignore stud
		if (Cards[i].game_type == GAME_RULES_STUD7 || Cards[i].game_type == GAME_RULES_STUD7_HI_LO) {
			continue;
		}
		// ignore one-on-one stud too
		if (Cards[i].game_type == GAME_RULES_STUD7+10 || Cards[i].game_type == GAME_RULES_STUD7_HI_LO+10) {
			continue;
		}
		// how many players got cards?
		int this_count = 0;
		for (int j=0; j < MAX_PLAYERS_PER_GAME; j++) {
			if (Cards[i].player_cards[j]) {
				this_count++;
				continue;
			}
		}
		
		// filter out play money
		if (Cards[i].chip_type == 0) {
			continue;
		}

		// filter games that didn't play
		if (this_count < 2) {
			continue;
		}

	  #if 0	// we do want to see hands with no flop
		// don't bother if there was no flop
		if (Cards[i].cards[0] == CARD_NO_CARD) {
			continue;
		}
	  #endif
		
		// valid data
		fprintf(out,"%d\t%d\t%d\t%d", 
			Cards[i].game_number, this_count, Cards[i].game_type-20,
			Cards[i].low_limit);
		for (int k=0; k < FLOP_CARDS; k++) {
			if (Cards[i].cards[k] != CARD_NO_CARD) {
				fprintf(out,"\t%c%c", cRanks[RANK(Cards[i].cards[k])], cSuits[SUIT(Cards[i].cards[k])]);
				if (this_count >= 7) { // count towards big count as there are 7 players or more
					char card = Cards[i].cards[k];
					CardCount.card_count[card] += 1;
				}
			}
		}
		fprintf(out,"\n");
	}
	fclose(out);
	out = fopen(xlstext_str, "wt");
	if (out) {	
		for (int k=0; k < 100; k++) {
			if (CardCount.card_count[k]) {
				fprintf(out,"%c%c: %d\n", cRanks[RANK(k)], cSuits[SUIT(k)], CardCount.card_count[k]);
			}
		}
	}
	fclose(out);
	out = NULL;
	WriteFile(xlsbin_str, &CardCount, sizeof(CardCountStruct));


  #endif

  #if 1
	#define CARDS_TO_COUNT	3
	#define MIN_PLAYERS		1
	printf("%s(%d) Counting cards...\n", _FL);
	int three_suited_cards = 0;
	int two_suited_cards = 0;
	int rainbow_flops = 0;
	int flops_tested = 0;
	for (i=0; i < MAX_GAMES; i++) {
		if (Cards[i].game_type == GAME_RULES_STUD7) {
			continue;
		}
		// how many players got cards?
		int this_count = 0;
		for (int j=0; j < MAX_PLAYERS_PER_GAME; j++) {
			if (Cards[i].player_cards[j]) {
				this_count++;
			}
		}
		// filter games that didn't play with enough players
		if (this_count < MIN_PLAYERS) {
			continue;
		}

		for (int k=0; k < CARDS_TO_COUNT ; k++) {
			if (Cards[i].cards[k] != CARD_NO_CARD) {
				cardcount[Cards[i].cards[k]]++;
			}
		}

		// Count flop suits... (just the first 3 cards)
		if (Cards[i].cards[0] != CARD_NO_CARD) {	// something got dealt...
			flops_tested++;
			int suit1 = SUIT(Cards[i].cards[0]);
			int suit2 = SUIT(Cards[i].cards[1]);
			int suit3 = SUIT(Cards[i].cards[2]);
			if (suit1==suit2 && suit2==suit3) {
				three_suited_cards++;
			} else if (suit1==suit2 || suit2==suit3 || suit1==suit3) {
				two_suited_cards++;
			} else if (suit1!=suit2 && suit2!=suit3 && suit1!=suit3) {
				rainbow_flops++;
			}
		}
	}
	
	printf("%s(%d) Card counting results...\n", _FL);
	for (i=0; i < 100; i++) {
		if (cardcount[i]) {
			printf("%8d\t%c%c\n", cardcount[i], cRanks[RANK(i)], cSuits[SUIT(i)]);
		}
	}
	printf("\n");
	printf("%s(%d) Flop suiting test results...\n",_FL);
	printf("    Total flops tested:    %d\n", flops_tested);
	printf("    Three cards same suit: %d\n", three_suited_cards);
	printf("    Two cards same suit:   %d\n", two_suited_cards);
	printf("    No cards same suit:    %d\n", rainbow_flops);
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Read in a data-?.bin file and write out a tab delimited
// text file suitable for reading in to Excel.
//
void WriteDataProfile(char *filename)
{
	long bytes_read = 0;
	struct Pred_Data *pd = (struct Pred_Data *)LoadFile(filename, &bytes_read);
	if (!pd) {
		printf("%s(%d) ERROR: Could not load '%s'.  Exiting.\n", _FL, filename);
		exit(10);
	}
	char output_fname[MAX_FNAME_LEN];
	strnncpy(output_fname, filename, MAX_FNAME_LEN);
	SetExtension(output_fname, ".txt");
	FILE *fd = fopen(output_fname, "wt");
	if (!fd) {
		printf("%s(%d) ERROR: Could not open '%s' for writing.  Exiting.\n", _FL, output_fname);
		exit(10);
	}
	printf("Reading %s and writing %s...\n", filename, output_fname);
	fprintf(fd, "Time (CST)\tPlrs\t$/hr\t$\tgames\tAccounts\n");

	// Clean up a few things in the first data sample that might be
	// left-over from the previous day.
	pd[0].new_accounts_today = 0.0;

	for (int i=0 ; i<24*3600/PREDICTION_DATA_INTERVAL ; i++) {
		struct Pred_Data *p = pd + i;
		fprintf(fd, "%02d:%02d\t%.0f\t$%.0f\t$%.0f\t%.0f\t%.0f\n",
				(i*PREDICTION_DATA_INTERVAL) / 3600,
				((i*PREDICTION_DATA_INTERVAL) / (60) ) % 60,
				p->players,
				p->rake_per_hour / 100.0,
				p->rake_today / 100.0,
				p->games_today,
				p->new_accounts_today);
	}
	fclose(fd);
	free(pd);
	printf("Complete.\n");
	exit(0);

}
/**********************************************************************************
 Function NetPurchasedInLastNHours()
 date: 24/01/01 kriskoin Note: Grabbed from ecash2.cpp and modified for our purposes
***********************************************************************************/
int NetPurchasedInLastNHours(SDBRecord *r, int hours)
{
	time_t now = time(NULL);
	int total_charged = 0;
	int valid_tr_index = -1, i;
	// first step -- just find the last relevant transaction
	for (i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
		ClientTransaction *ct = &r->transaction[i];
		if (!ct->transaction_type) {
			continue;
		}
		if (difftime(now, ct->timestamp) < (int)(hours*3600)) {	// fits the time range
			valid_tr_index = i;	// set so we can count from here
		}
	}
	// now we do it for real, going backwards from this point towards the present
	for (i=valid_tr_index; i >= 0 ; i--) {
		ClientTransaction *ct = &r->transaction[i];
		if (!ct->transaction_type) {
			continue;
		}
		if (ct->transaction_type == CTT_PURCHASE) { // count it (+)
			total_charged += ct->transaction_amount;
		}
		if (ct->transaction_type == CTT_CREDIT) {	// count it (-)
			total_charged -= ct->transaction_amount;
		}
	  #if 0	// 20000228HK -- removed as it gets us no benefit, but could cause the
			// limit to be wrong if a check is improperly refunded
		if (ct->transaction_type == CTT_CHECK_REFUND) { // count it (+)
			total_charged += ct->transaction_amount;
		}
		if (ct->transaction_type == CTT_CHECK_ISSUED) { // count it (-)
			total_charged -= ct->transaction_amount;
		}
	  #endif
		// don't ever go negative!
		total_charged = max(total_charged, 0);
	}
	return max(0,total_charged);
}

/**********************************************************************************
 Function CheckMaxedOut(SDBRecord *r)
 date: 24/01/01 kriskoin Purpose: check if player is maxed out and log it
 NOTE: we only want players who are set to default lower limits and have no overrides
       and have logged in in the last 24 hours
***********************************************************************************/
void CheckMaxedOut(SDBRecord *r)
{
	// if he hasn't logged in in the last 24 hours, we don't care
	time_t now = time(NULL);
	if (difftime(now, r->last_login_times[0]) > 30*60*60) { // recent enough? (30h)
		return;
	}

	// if set to higher limits, don't bother
	if (r->flags & SDBRECORD_FLAG_HIGH_CC_LIMIT) {
		return;
	}
	// if there are any overrides set, forget it
	if (r->cc_override_limit1 || r->cc_override_limit2 || r->cc_override_limit3) {
		return;
	}

	potential_maxed_out_count++;
	static char curr_str[MAX_CURRENCY_STRING_LEN];
	zstruct(curr_str);
	// he's set to default lows which we'll copy over from the .ini file
	const int purchase_limits[3] = { 60000, 150000, 200000 } ;	// in cents
	const int purchase_days[3] = { 1, 7, 30 };
	for (int i=0 ; i < 3 ; i++) {
		int purchased = NetPurchasedInLastNHours(r, purchase_days[i]*24);
		if (purchased >= purchase_limits[i]) {	// maxed at this level
			char period[20];
			zstruct(period);
			if (purchase_days[i]==1) {
				strcpy(period, "24 hour");
			} else {
				sprintf(period, "%d day", purchase_days[i]);
			}
			maxed_count[i]++;
			AddToLog(MAXEDOUT_FNAME, NULL, "%s at %s limit (balance: %s)\n", 
				r->user_id, period,
				CurrencyString(curr_str, r->real_in_bank+r->real_in_play, CT_REAL));
			break;	// no need to know if same player is maxxed at a higher level
		}
	}
}

/**********************************************************************************
 Function *FileLastModifiedStr()
 date: 24/01/01 kriskoin Purpose: return a string (internal buffer ptr) with last-modified info for filename
***********************************************************************************/
char *FileLastModifiedStr(char *filename)
{
	#define FLM_BUF_SIZE	100
	static char output[FLM_BUF_SIZE];
	zstruct(output);
	struct stat buf;
	zstruct(buf);
	// get data associated with 'fh'
	int rc = stat(filename, &buf);
	/* Check if statistics are valid: */
	if(rc) {	// zero is good
		sprintf(output, "Bad filename (%s) trying to read info", filename);
	} else {
		char last_update_str[50];
		zstruct(last_update_str);
		ConvertSecondsToString(time(NULL)-buf.st_mtime, last_update_str, FALSE, FALSE, FALSE);
		sprintf(output, "%s ago", last_update_str);
		sprintf(output+strlen(output), " -- %s", ctime(&buf.st_mtime));
	}
	return output;
}

//*********************************************************
// https://github.com/kriskoin//
// Convert seconds_to_go to a string for the shot clock
//
void ConvertSecondsToString(int seconds_to_go, char *dest_str, int display_seconds_flag, int display_short_units_flag, int display_field_count)
{
	if (!display_seconds_flag || seconds_to_go >= 120) {
		seconds_to_go += 59;	// round up to next highest minute.
		display_seconds_flag = FALSE;
	}
	int days = seconds_to_go / (24*3600);
	seconds_to_go -= days*24*3600;
	int hours = seconds_to_go / 3600;
	seconds_to_go -= hours * 3600;
	int minutes = seconds_to_go / 60;
	seconds_to_go -= minutes * 60;
	dest_str[0] = 0;
	if (days) {
		if (display_short_units_flag) {
			sprintf(dest_str+strlen(dest_str), "%dd ", days);
		} else {
			sprintf(dest_str+strlen(dest_str), "%d day%s ", days, days==1 ? "" : "s");
		}
	}
	if (days || hours) {
		if (display_short_units_flag) {
			sprintf(dest_str+strlen(dest_str), "%dh ", hours);
		} else {
			sprintf(dest_str+strlen(dest_str), "%d hour%s ", hours, hours==1 ? "" : "s");
		}
	}
	if (!display_seconds_flag || minutes) {
		if (display_short_units_flag) {
			sprintf(dest_str+strlen(dest_str), "%dm", minutes);
		} else {
			sprintf(dest_str+strlen(dest_str), "%d minute%s", minutes, minutes==1 ? "" : "s");
		}
		if (display_seconds_flag) {
			strcat(dest_str, " ");
		}
	}
	if (display_seconds_flag) {
		if (display_short_units_flag) {
			sprintf(dest_str+strlen(dest_str), "%ds", seconds_to_go);
		} else {
			sprintf(dest_str+strlen(dest_str), "%d second%s", seconds_to_go, seconds_to_go==1 ? "" : "s");
		}
	}
	NOTUSED(display_field_count);	// !!! compiler unreferenced param warning 20:::}
