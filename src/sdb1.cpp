/**********************************************************************************
 Member functions for SDB (SimpleDataBase)
 date: kriskoin 2019/01/01***********************************************************************************/

#ifdef HORATIO
	#define DISP 0
#endif

#define OPTIMIZE_FOR_LARGE_DATABASE	1	// disable anything that touches the whole file
#define TEST_QSORT_DEPTH			0	// test how deep QSort goes?

#ifdef WIN32
  #define WIN32_LEAN_AND_MEAN	// Exclude rarely-used stuff from Windows headers
  #include <windows.h>			// Needed for CritSec stuff
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#if WIN32
  #include <io.h>
#else
  #include <stdarg.h>
  #include <errno.h>
  #include <ctype.h>
  #include <sys/io.h>
  #include <unistd.h>
  #include <sys/mman.h>
#endif
#include "pokersrv.h"
#include "sdb.h"

#define SEQUENTIAL_SEARCH_STATUS_INTERVAL	30000	// how often to print status message to debwin

#define DATABASE_PATH	  "data//db//"	

/**********************************************************************************
 Function SimpleDataBase(char *database_name)
 date: kriskoin 2019/01/01 Purpose: constructor specifiying database_name
 NOTE: creating the SDB and then calling UseDataBase(name) is the same thing (for now)
***********************************************************************************/
SimpleDataBase::SimpleDataBase(char *database_name, int max_record_count)
{
	PPInitializeCriticalSection(&SDBCritSec, CRITSECPRI_SDB, "SDB");
	_clean_shutdown = FALSE;
	_sdb_name[0] = 0;	// blank the database name till it's initialized
	_txt_name[0] = 0;
	_bin_name[0] = 0;
	_SDB = NULL;
	index_player_id = NULL;
	index_player_id_count = 0;
	index_user_id = NULL;
	index_user_id_count = 0;
	hDatabaseFile = 0;	// file handle for binary database file
  #if WIN32
	hDatabaseMapping = 0;
  #endif
	RakeRec = NULL;
	MarketingRec = NULL;
	EcashRec = NULL;
	EcashRec_ID = 0;
	EcashFeeRec = NULL;
	MiscRec = NULL;
	ErrorRec = NULL;
	PendingRec = NULL;
	PendingRec_ID = 0;
	DraftsInRec = NULL;
	DraftsInRec_ID = 0;
	PrizesRec = NULL;
	PrizesRec_ID = 0;
	BadBeatRec = NULL;
	BadBeatRec_ID = 0;
	TournamentRec = NULL;
	TournamentRec_ID = 0;
	RealChipUniverse = 0;
	FakeChipUniverse = 0;
	TournamentChipUniverse = 0;
	RealChipChange = 0;
	FakeChipChange = 0;
	TournamentChipChange = 0;
	time_now = old_time = 0;
	iRecordCount = max_record_count;
	iRecordLength = sizeof(SDBRecord);
	number_of_accounts_purged = 0;
	stale_account_count = 0;
	//kp(("%s(%d) iRecordLength = %d\n", _FL, iRecordLength));
	iEmptyRecordSearchStart = 0;
	dwHighestPlayerID = 0;
	cash_in_play_at_startup = 0;
	cash_in_accounts_at_tables_at_startup = 0;
	indexes_need_sorting = 0;
	UseDataBase(database_name);
}

/**********************************************************************************
 Function ~SimpleDataBase
 date: kriskoin 2019/01/01 Purpose: destructor
***********************************************************************************/
SimpleDataBase::~SimpleDataBase(void)
{
	if (!_clean_shutdown) {
		ShutDown();
	}
	PPDeleteCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function ::UseDataBase(char *file_name_base)
 date: kriskoin 2019/01/01 Purpose: specifies which database we'll be using... do whatever startup is needed
 WARNING: this function touches every record of the database.
***********************************************************************************/
void SimpleDataBase::UseDataBase(char *file_name_base)
{
	if (DebugFilterLevel <= 0) {
		if (iRecordCount >= SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
			kp(("%s(%d) %s SDB::UseDataBase()...  This will also validate the database.\n",_FL,TimeStr()));
		}
	}
	EnterCriticalSection(&SDBCritSec);
	_clean_shutdown = FALSE;
	// in this case, we'll take a run through at reading the database, in case there's
	// a text version we need to update from

	strnncpy(_sdb_name, file_name_base, MAX_COMMON_STRING_LEN);

//	strcpy(_txt_name,_sdb_name) ;
//	strcat(_txt_name,".txt") ;
//	sprintf(_txt_name,"%s.txt", _sdb_name);
//	sprintf(_bin_name,"%s.bin", _sdb_name);
	sprintf(_bin_name,"%s%s.txt", DATABASE_PATH, _sdb_name);
	sprintf(_bin_name,"%s%s.bin", DATABASE_PATH, _sdb_name);

	RakeRec = NULL;
	RealChipUniverse = 0;		// we'll read it in as we get them
	FakeChipUniverse = 0;		// we'll read it in as we get them
	TournamentChipUniverse = 0;
	ReadDataBase();
	CountAllChips(0);	
	// RakeRec should've been set in ReadDataBase()

	if (!RakeRec) {
		Error(ERR_ERROR,"%s(%d) no rake account found in database", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
	if (DebugFilterLevel <= 0) {
		if (iRecordCount >= SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
			kp(("%s(%d) %s SDB::UseDataBase() complete.\n",_FL,TimeStr()));
		}
	}



}

//***** Country Codes *****
// Note: we've got up to 15 characters (16 less a nul) to store the
// the country name in the player record.
// The master copy of this list is in client\prefs.cpp
struct CountryCodes {
	char *abbrev15;	// 15 letter abbreviation
	char *abbrev2;	// 2 letter USPS abbreviation
	char *name;		// full name (for user display only)
} CountryCodes[] =  {
	"",					"",			"----- Select Your Country -----",
//	"Afghanistan",		"AF",		"Afghanistan",
	"Albania",			"AL",		"Albania",
	"Algeria",			"DZ",		"Algeria",
	"American Samoa",	"AS",		"American Samoa",
	"Andorra",			"AD",		"Andorra",
//	"Angola",			"AO",		"Angola",
	"Anguilla",			"AI",		"Anguilla",
	"Antarctica",		"AQ",		"Antarctica",
	"AG",				"AG",		"Antigua and Barbuda",
	"Argentina",		"AR",		"Argentina",
	"Armenia",			"AM",		"Armenia",
	"Aruba",			"AW",		"Aruba",
	"Australia",		"AU",		"Australia",
	"Austria",			"AT",		"Austria",
	"Azerbaijan",		"AZ",		"Azerbaijan",
	"Bahamas",			"BS",		"Bahamas",
	"Bahrain",			"BH",		"Bahrain",
	"Bangladesh",		"BD",		"Bangladesh",
	"Barbados",			"BB",		"Barbados",
	"Belarus",			"BY",		"Belarus",
	"Belgium",			"BE",		"Belgium",
	"Belize",			"BZ",		"Belize",
	"Benin",			"BJ",		"Benin",
	"Bermuda",			"BM",		"Bermuda",
	"Bhutan",			"BT",		"Bhutan",
	"Bolivia",			"BO",		"Bolivia",
	"BA",				"BA",		"Bosnia and Herzegovina",
	"Botswana",			"BW",		"Botswana",
	"Bouvet Island",	"BV",		"Bouvet Island",
	"Brazil",			"BR",		"Brazil",
	"IO",				"IO",		"British Indian Ocean Territory",
	"BN",				"BN",		"Brunei Darussalam",
	"Bulgaria",			"BG",		"Bulgaria",
	"Burkina Faso",		"BF",		"Burkina Faso",
	"Burundi",			"BI",		"Burundi",
	"Canada",			"CA",		"Canada",
	"Cambodia",			"KH",		"Cambodia",
	"Cameroon",			"CM",		"Cameroon",
	"Cape Verde",		"CV",		"Cape Verde",
	"Cayman Islands",	"KY",		"Cayman Islands",
	"CF",				"CF",		"Central African Republic",
	"Chad",				"TD",		"Chad",
	"Chile",			"CL",		"Chile",
	"China",			"CN",		"China",
	"Christmas Island",	"CX",		"Christmas Island",
	"Cocos",			"CC",		"Cocos (Keeling - Islands)",
	"Colombia",			"CO",		"Colombia",
	"Comoros",			"KM",		"Comoros",
	"Congo",			"CG",		"Congo",
	"Cook Islands",		"CK",		"Cook Islands",
//	"Costa Rica",		"CR",		"Costa Rica",
	"Cote D'Ivoire",	"CI",		"Cote D'Ivoire",
	"Croatia",			"HR",		"Croatia",
	"Cuba",				"CU",		"Cuba",
	"Cyprus",			"CY",		"Cyprus",
	"Czech Republic",	"CZ",		"Czech Republic",
	"Denmark",			"DK",		"Denmark",
	"Djibouti",			"DJ",		"Djibouti",
	"Dominica",			"DM",		"Dominica",
	"DO",				"DO",		"Dominican Republic",
	"East Timor",		"TP",		"East Timor",
	"Ecuador",			"EC",		"Ecuador",
	"Egypt",			"EG",		"Egypt",
	"El Salvador",		"SV",		"El Salvador",
	"GQ",				"GQ",		"Equatorial Guinea",
	"Eritrea",			"ER",		"Eritrea",
	"Estonia",			"EE",		"Estonia",
	"Ethiopia",			"ET",		"Ethiopia",
	"France",			"FR",		"France",
	"Finland",			"FI",		"Finland",
	"Falkland Islnds",	"FK",		"Falkland Islands",
	"Faroe Islands",	"FO",		"Faroe Islands",
	"Fiji",				"FJ",		"Fiji",
	"French Guiana",	"GF",		"French Guiana",
	"PF",				"PF",		"French Polynesia",
	"TF",				"TF",		"French Southern Territories",
	"Germany",			"DE",		"Germany",
	"Greece",			"GR",		"Greece",
	"Gabon",			"GA",		"Gabon",
	"Gambia",			"GM",		"Gambia",
	"Georgia",			"GE",		"Georgia",
	"Ghana",			"GH",		"Ghana",
	"Gibraltar",		"GI",		"Gibraltar",
	"Greenland",		"GL",		"Greenland",
	"Grenada",			"GD",		"Grenada",
	"Guadeloupe",		"GP",		"Guadeloupe",
	"Guam",				"GU",		"Guam",
	"Guatemala",		"GT",		"Guatemala",
	"Guinea",			"GN",		"Guinea",
	"Guinea-Bissau",	"GW",		"Guinea-Bissau",
	"Guyana",			"GY",		"Guyana",
	"Haiti",			"HT",		"Haiti",
	"HM",				"HM",		"Heard and Mc Donald Islands",
	"Honduras",			"HN",		"Honduras",
	"Hong Kong",		"HK",		"Hong Kong",
	"Hungary",			"HU",		"Hungary",
	"Italy",			"IT",		"Italy",
	"Iceland",			"IS",		"Iceland",
	"India",			"IN",		"India",
	"Indonesia",		"ID",		"Indonesia",
//	"Iran",				"IR",		"Iran (Islamic Republic of)",
//	"Iraq",				"IQ",		"Iraq",
	"Ireland",			"IE",		"Ireland",
	"Israel",			"IL",		"Israel",
	"Jamaica",			"JM",		"Jamaica",
	"Japan",			"JP",		"Japan",
	"Jordan",			"JO",		"Jordan",
	"Kazakhstan",		"KZ",		"Kazakhstan",
	"Kenya",			"KE",		"Kenya",
	"Kiribati",			"KI",		"Kiribati",
	"KP",				"KP",		"Korea, Democratic People's Republic of",
	"KR",				"KR",		"Korea, Republic of",
	"Kuwait",			"KW",		"Kuwait",
	"Kyrgyzstan",		"KG",		"Kyrgyzstan",
	"Lao PDR",			"LA",		"Lao People's Democratic Republic",
	"Latvia",			"LV",		"Latvia",
	"Lebanon",			"LB",		"Lebanon",
	"Lesotho",			"LS",		"Lesotho",
	"Liberia",			"LR",		"Liberia",
//	"LY",				"LY",		"Libyan Arab Jamahiriya",
	"Liechtenstein",	"LI",		"Liechtenstein",
	"Lithuania",		"LT",		"Lithuania",
	"Luxembourg",		"LU",		"Luxembourg",
	"Mexico",			"MX",		"Mexico",
	"Macau",			"MO",		"Macau",
	"MK",				"MK",		"Macedonia, the former Yugoslav Republic of",
	"Madagascar",		"MG",		"Madagascar",
	"Malawi",			"MW",		"Malawi",
	"Malaysia",			"MY",		"Malaysia",
	"Maldives",			"MV",		"Maldives",
	"Mali",				"ML",		"Mali",
	"Malta",			"MT",		"Malta",
	"MarshallIslands",	"MH",		"Marshall Islands",
	"Martinique",		"MQ",		"Martinique",
	"Mauritania",		"MR",		"Mauritania",
	"Mauritius",		"MU",		"Mauritius",
	"Mayotte",			"YT",		"Mayotte",
	"FX",				"FX",		"Metropolitan France",
	"FM",				"FM",		"Micronesia, Federated States of",
	"MD",				"MD",		"Moldova, Republic of",
	"Monaco",			"MC",		"Monaco",
	"Mongolia",			"MN",		"Mongolia",
	"Montserrat",		"MS",		"Montserrat",
	"Morocco",			"MA",		"Morocco",
	"Mozambique",		"MZ",		"Mozambique",
	"Myanmar",			"MM",		"Myanmar",
	"Namibia",			"NA",		"Namibia",
	"Nauru",			"NR",		"Nauru",
	"Nepal",			"NP",		"Nepal",
	"Netherlands",		"NL",		"Netherlands",
	"New Caledonia",	"NC",		"New Caledonia",
	"New Zealand",		"NZ",		"New Zealand",
	"Nicaragua",		"NI",		"Nicaragua",
	"Niger",			"NE",		"Niger",
	"Nigeria",			"NG",		"Nigeria",
	"Niue",				"NU",		"Niue",
	"Norfolk Island",	"NF",		"Norfolk Island",
	"MP",				"MP",		"Northern Mariana Islands",
	"Norway",			"NO",		"Norway",
	"Oman",				"OM",		"Oman",
	"Pakistan",			"PK",		"Pakistan",
	"Palau",			"PW",		"Palau",
	"Panama",			"PA",		"Panama",
	"Papua NewGuinea",	"PG",		"Papua New Guinea",
	"Paraguay",			"PY",		"Paraguay",
	"Peru",				"PE",		"Peru",
	"Philippines",		"PH",		"Philippines",
	"Pitcairn",			"PN",		"Pitcairn",
	"Poland",			"PL",		"Poland",
	"Portugal",			"PT",		"Portugal",
	"Puerto Rico",		"PR",		"Puerto Rico",
	"Qatar",			"QA",		"Qatar",
	"Reunion",			"RE",		"Reunion",
	"Romania",			"RO",		"Romania",
	"Russian Fed.",		"RU",		"Russian Federation",
	"Rwanda",			"RW",		"Rwanda",
	"Spain",			"ES",		"Spain",
	"Switzerland",		"CH",		"Switzerland",
	"Sweden",			"SE",		"Sweden",
	"KN",				"KN",		"Saint Kitts and Nevis",
	"Saint Lucia",		"LC",		"Saint Lucia",
	"VC",				"VC",		"Saint Vincent and the Grenadines",
	"Samoa",			"WS",		"Samoa",
	"San Marino",		"SM",		"San Marino",
	"ST",				"ST",		"Sao Tome and Principe",
	"Saudi Arabia",		"SA",		"Saudi Arabia",
	"Senegal",			"SN",		"Senegal",
	"Seychelles",		"SC",		"Seychelles",
	"Sierra Leone",		"SL",		"Sierra Leone",
	"Singapore",		"SG",		"Singapore",
	"Slovakia",			"SK",		"Slovakia (Slovak Republic)",
	"Slovenia",			"SI",		"Slovenia",
	"Solomon Islands",	"SB",		"Solomon Islands",
	"Somalia",			"SO",		"Somalia",
	"South Africa",		"ZA",		"South Africa",
	"Sri Lanka",		"LK",		"Sri Lanka",
	"St. Helena",		"SH",		"St. Helena",
	"PM",				"PM",		"St. Pierre and Miquelon",
	"Sudan",			"SD",		"Sudan",
	"Suriname",			"SR",		"Suriname",
	"SJ",				"SJ",		"Svalbard and Jan Mayen Islands",
	"Swaziland",		"SZ",		"Swaziland",
	"SY",				"SY",		"Syrian Arab Republic",
	"Taiwan",			"TW",		"Taiwan",
	"Tajikistan",		"TJ",		"Tajikistan",
	"TZ",				"TZ",		"Tanzania, United Republic of",
	"Thailand",			"TH",		"Thailand",
	"Netherlands",		"NL",		"The Netherlands",
	"Togo",				"TG",		"Togo",
	"Tokelau",			"TK",		"Tokelau",
	"Tonga",			"TO",		"Tonga",
	"TT",				"TT",		"Trinidad and Tobago",
	"Tunisia",			"TN",		"Tunisia",
	"Turkey",			"TR",		"Turkey",
	"Turkmenistan",		"TM",		"Turkmenistan",
	"TC",				"TC",		"Turks and Caicos Islands",
	"Tuvalu",			"TV",		"Tuvalu",
	"United States",	"US",		"United States",
	"United States",	"USA",		"United States",
	"United Kingdom",	"GB",		"United Kingdom",
	"Uganda",			"UG",		"Uganda",
	"Ukraine",			"UA",		"Ukraine",
	"AE",				"AE",		"United Arab Emirates",
//	"UM",				"UM",		"United States Minor Outlying Islands",
	"Uruguay",			"UY",		"Uruguay",
	"Uzbekistan",		"UZ",		"Uzbekistan",
	"Vanuatu",			"VU",		"Vanuatu",
	"VA",				"VA",		"Vatican City State",
	"Venezuela",		"VE",		"Venezuela",
	"Viet Nam",			"VN",		"Viet Nam",
	"VG",				"VG",		"Virgin Islands (British)",
	"VI",				"VI",		"Virgin Islands (U.S.)",
	"WF",				"WF",		"Wallis And Futuna Islands",
	"Western Sahara",	"EH",		"Western Sahara",
	"Yemen",			"YE",		"Yemen",
	"Yugoslavia",		"YU",		"Yugoslavia",
	"Zaire",			"ZR",		"Zaire",
	"Zambia",			"ZM",		"Zambia",
	"Zimbabwe",			"ZW",		"Zimbabwe",
	NULL, NULL, NULL
};

/**********************************************************************************
 Function ::ReadDataBase(void)
 date: kriskoin 2019/01/01 Purpose: read the database in, either from the .txt or from the .bin
 WARNING: this function touches every record of the database.



***********************************************************************************/
void SimpleDataBase::ReadDataBase(void)
{
	// we will read the txt version of the database if needed (if it's been modified)
	FILE *in = NULL;
	int read_text = CheckForNewTextFile();

//ricardoGANG
//read_text=0 ;
//ricardoGANG - TEST ONLY

	if (0/*read_text*/) {
		if ((in = fopen(_txt_name, "rt")) == NULL) {
			DIE("Couldn't read txt database, but we were told we should");
		}


		// Delete the binary file and start over.
		CreateNewDatabaseFile();

		// now read them all in
		#define MAX_INPUT_LINE	160
		char input_line[MAX_INPUT_LINE];
		int rec_index = 0;	// we'll set the indexes here -- in order as it's a new file
		SDBRecord sdb_rec;	// temp record storage
		while (!feof(in) && rec_index < iRecordCount) {
			fgets(input_line, MAX_INPUT_LINE, in);	// and see how to proceed from here
			if (input_line[0] != '#') {	// not a comment, so proccess it
				zstruct(sdb_rec);
				if (strlen(input_line) > 10 && ParseTextToRecord(input_line, &sdb_rec)) {// fill the record with data
					memcpy(&_SDB[rec_index], &sdb_rec, sizeof(SDBRecord));
					
					// get pointers to special accounts
					if (!stricmp(_SDB[rec_index].user_id, "Rake")) {	// rake account
						RakeRec = &_SDB[rec_index];
					}
					if (!stricmp(_SDB[rec_index].user_id, "Marketing")) {	// marketing account
						MarketingRec = &_SDB[rec_index];
					}
					if (!stricmp(_SDB[rec_index].user_id, "Ecash")) {	// ecash (SFC) account
						EcashRec = &_SDB[rec_index];
						EcashRec_ID = _SDB[rec_index].player_id;
					}
					if (!stricmp(_SDB[rec_index].user_id, "EcashFee")) {	// ecash fee account
						EcashFeeRec = &_SDB[rec_index];
					}
					if (!stricmp(_SDB[rec_index].user_id, "Error")) {	// error account
						ErrorRec = &_SDB[rec_index];
					}
					if (!stricmp(_SDB[rec_index].user_id, "Misc.")) {	// it's the misc account
						MiscRec = &_SDB[rec_index];
					}
					if (!stricmp(_SDB[rec_index].user_id, "Pending")) {	// pending credit account
						PendingRec = &_SDB[rec_index];
						PendingRec_ID = _SDB[rec_index].player_id;
					}
					if (!stricmp(_SDB[rec_index].user_id, "DraftsIn")) {	// DraftsIn account
						DraftsInRec = &_SDB[rec_index];
						DraftsInRec_ID = _SDB[rec_index].player_id;
					}
					if (!stricmp(_SDB[rec_index].user_id, "Prizes")) {	// prizes account
						PrizesRec = &_SDB[rec_index];
						PrizesRec_ID = _SDB[rec_index].player_id;
					}
					if (!stricmp(_SDB[rec_index].user_id, "BadBeats")) {	// bad beats
						BadBeatRec = &_SDB[rec_index];
						BadBeatRec_ID = _SDB[rec_index].player_id;
					}
					if (!stricmp(_SDB[rec_index].user_id, TOURNAMENT_CHIP_ACCOUNT_NAME)) {	// bad beats
						TournamentRec = &_SDB[rec_index];
						TournamentRec_ID = _SDB[rec_index].player_id;
					}
					rec_index++;
				}
			}
		}
		fclose(in);
	
		WriteDataBase(TRUE);

	}

	if (!_SDB) {
		OpenDatabaseFile();	// open binary file if necessary
	}

	// Allocate our index arrays and build them up as we go along.
	index_player_id = (SDBIndexEntry *)malloc(sizeof(SDBIndexEntry)*iRecordCount);
	index_player_id_count = 0;	// nothing in it yet
	if (!index_player_id) {
		DIE("Out of memory");
	}
	index_user_id = (SDBIndexEntry *)malloc(sizeof(SDBIndexEntry)*iRecordCount);
	index_user_id_count = 0;	// nothing in it yet
	if (!index_user_id) {
		DIE("Out of memory");
	}

	// Validate each record...
	SDBRecord *r = _SDB;
	iEmptyRecordSearchStart = -1;
	int never_logged_in_count = 0;
	number_of_accounts_purged = 0;
	stale_account_count = 0;
	int real_money_accounts = 0;
	int players_with_chips_in_play = 0;
	#define MIN_PURGEABLE_PLAYER_ID	(0x100000)
	WORD32 account_purge_time1 = time(NULL) - 10*24*60*60;	// 10 days ago.
	WORD32 account_purge_time2 = time(NULL) - 90*24*60*60;	// 90 days ago.

	// Open a file to append any stale user records to
	FILE *stale_fd = fopen("stale.bin", "ab");
	if (!stale_fd) {
		Error(ERR_ERROR, "%s(%d) stale.bin failed to open for append", _FL);
	}
	for (int j=0; j < iRecordCount; j++, r++) {
		if (DebugFilterLevel <= 0) {
			if (j && !(j%SEQUENTIAL_SEARCH_STATUS_INTERVAL)) {
				kp(("%s(%d) %s SDB::ReadDataBase: validating record %d of %d\n", _FL, TimeStr(), j, iRecordCount));
			}
		}
	  
	  #ifndef DBFILTER	// 24/01/01 kriskoin:
		//kriskoin: 		// into, erase it now.
		if (r->player_id >= MIN_PURGEABLE_PLAYER_ID &&
			stale_fd &&
			// adate: do not purge locked out accounts
			(!(r->flags & SDBRECORD_FLAG_LOCKED_OUT)) &&
			//kriskoin: 			!r->real_in_bank && !r->real_in_play &&
			r->priv <= ACCPRIV_PLAY_MONEY)	// only purge play money accounts
		{
			// step 1: allow purging anything that has never logged in
			if (r->account_creation_time <= account_purge_time1 &&
				!r->last_login_times[0])
			{
				// This account has never logged in.  Purge it.
				kp(("%s(%d) Purging account '%s' (%s) (never logged in)\n", _FL, r->user_id, r->email_address));
				fwrite(r, sizeof(*r), 1, stale_fd);
				zstruct(*r);
				number_of_accounts_purged++;
			}
			// step 2: allow purging play money accounts with low balances that
			// have not logged in for more than 60 days
			if (r->player_id && 
				r->last_login_times[0] &&
				r->last_login_times[0] <= account_purge_time2 &&
				r->fake_in_bank <= 1000*100)
			{
				// This account has not logged in for a long time has doesn't
				// have more than 1000 in it.
				kp(("%s(%d) Purging account '%s' (%s) (%d play chips) (%d days old)\n",
						_FL, r->user_id, r->email_address, r->fake_in_bank/100,
						(time(NULL) - r->last_login_times[0]) / (24*60*60)));
				fwrite(r, sizeof(*r), 1, stale_fd);
				zstruct(*r);
				number_of_accounts_purged++;
			}
		}
	  #else
		NOTUSED(account_purge_time1);
		NOTUSED(account_purge_time2);
	  #endif

		//kriskoin: 		if (r->player_id && r->gender != GENDER_MALE && r->gender != GENDER_FEMALE) {
			kp(("%s(%d) Player '%s' gender = $%02x.  Setting to GENDER_MALE\n",_FL, r->user_id, r->gender));
			r->gender = GENDER_MALE;
		}

		// If the country code is only two characters, try to replace it with
		// the 15 character version of the country name.
		if (strlen(r->mailing_address_country)==2 ||
			strlen(r->mailing_address_country)==3) {	// 24/01/01 kriskoin:
			struct CountryCodes *cc = CountryCodes;
			while (cc->abbrev2) {
				if (!strcmp(cc->abbrev2, r->mailing_address_country)) {
					// Found a match... replace it if different.
					if (strcmp(cc->abbrev15, r->mailing_address_country)) {
						kp(("%s(%d) Switching country for '%s' from '%s' to '%s'\n",_FL,r->user_id, r->mailing_address_country, cc->abbrev15));
						strnncpy(r->mailing_address_country, cc->abbrev15, MAX_COMMON_STRING_LEN);
					}
					break;	// all done.
				}
				cc++;
			}
		}

		if (r->player_id) {
			// Add player_id to index
			index_player_id[index_player_id_count].hash = r->player_id;
			index_player_id[index_player_id_count].index = j;
			index_player_id_count++;
			indexes_need_sorting = TRUE;
			if (r->player_id > dwHighestPlayerID) {	// keep track of the highest player_id in the database
				dwHighestPlayerID = r->player_id;
			}
		} else {
			// This record is empty... keep track of where to start
			// searching for an empty record when necessary.
			if (iEmptyRecordSearchStart==-1) {	// first empty record we found?
				iEmptyRecordSearchStart = j;	// yes, this is a good place to start.
			}
		}
		if (r->user_id[0]) {
			// Add user_name to index
			pr(("%s(%d) Found %s, valid_entry = %d, tournament_chips_in_play = %d\n",
					_FL, r->user_id, r->valid_entry, r->tournament_chips_in_play));
			index_user_id[index_user_id_count].hash = CalcStringHash(r->user_id);
			index_user_id[index_user_id_count].index = j;
			index_user_id_count++;
			indexes_need_sorting = TRUE;
			if (r->player_id >= MIN_PURGEABLE_PLAYER_ID && !r->last_login_times[0]) {
				never_logged_in_count++;	// another record that has never logged in
			}
		}

		// Make sure the account_creation_time is valid.  We didn't put the field
		// into the database until Oct 11, 1999, so earlier accounts are 0.
		if (!r->account_creation_time) {
			r->account_creation_time = time(NULL);	// just use now (it's better than zero).
		}

		RealChipUniverse += r->real_in_bank;
		RealChipUniverse += r->real_in_play;
		RealChipUniverse += r->pending_fee_refund;
		RealChipUniverse += r->pending_check;
		FakeChipUniverse += r->fake_in_bank;
		FakeChipUniverse += r->fake_in_play;
		TournamentChipUniverse += r->tournament_chips_in_play;

		// check for chips "in play" -- which there shouldn't be
		if (r->valid_entry) {
			// 24/01/01 kriskoin:
			int need_to_deal_with_tournaments = FALSE;
			// make sure it's not the tournament account itself
			if (r->tournament_chips_in_play > 0 &&	// if it's negative, ignore it...
				stricmp(r->user_id, TOURNAMENT_CHIP_ACCOUNT_NAME) &&	// ignore tournament account
				stricmp(r->user_id, BLACKHOLE_CHIP_ACCOUNT_NAME) ) {					// ignore BlackHole account
				if (!r->tournament_total_chips_in_play) {	// must be testing
					kp(("%s(%d) Found %5d.%02d tournament chips (but no total) in play for %s (%d prize pot, serial num %d)\n",
						_FL, r->tournament_chips_in_play / 100, r->tournament_chips_in_play % 100, r->user_id,
						r->tournament_creditable_pot, r->tournament_table_serial_number));
					r->tournament_chips_in_play = 0;	// just clear them
					r->tournament_table_serial_number = 0;
					r->tournament_fee_paid = 0;
					r->tournament_creditable_pot = 0;
				} else {
					need_to_deal_with_tournaments = TRUE;
					// nothing to do here now; all handled below
				}
				kp(("Found %5d.%02d tournament chips in play for %s\n",
						r->tournament_chips_in_play / 100, r->tournament_chips_in_play % 100, r->user_id));
			}
			// we can announce it now
			if (r->fake_in_play || r->real_in_play || need_to_deal_with_tournaments) {
				NotifyPlayerOfServerProblem(r);
				players_with_chips_in_play++;
			}
			// do the actual tournament refund at this point
			if (need_to_deal_with_tournaments) {	// if less than 0, tournaments account (to be ignored here)
				// do the actual refund				
				TournamentRefundStructure trs;
				zstruct(trs);
				FillTournamentRefundStructure(r, &trs);
				TournamentCashout(&trs);	// cash it out using the structure
			}			

			if (r->real_in_play) {
				cash_in_play_at_startup += r->real_in_play;
				cash_in_accounts_at_tables_at_startup += r->real_in_play + r->real_in_bank;
				kp(("Found %5d.%02d real chips in play for %s\n",
					r->real_in_play / 100, r->real_in_play % 100, r->user_id));
				MoveChipsFromPlayingToBank(r, CT_REAL);
			}
			if (r->fake_in_play) {
				kp(("Found %5d.%02d fake chips in play for %s\n",
					r->fake_in_play / 100, r->fake_in_play % 100, r->user_id));
				MoveChipsFromPlayingToBank(r, CT_PLAY);
			}
			if (r->tournament_chips_in_play && strcmp(r->user_id, TOURNAMENT_CHIP_ACCOUNT_NAME)) {
				kp(("%s %s(%d) Zeroing %d tournament chips for player %s\n",
							TimeStr(), _FL, r->tournament_chips_in_play, r->user_id));
				r->tournament_chips_in_play = 0;
			}
			if (r->priv==ACCPRIV_REAL_MONEY) {
				real_money_accounts++;
			}
		}

		// assign it as the rake account -- if that's what it is
		if (!stricmp(r->user_id, "Rake")) {	// it's the rake account
			RakeRec = r;
		}
		if (!stricmp(r->user_id, "Marketing")) {// marketing account
			MarketingRec = r;
		}
		if (!stricmp(r->user_id, "Ecash")) {	// ecash account
			EcashRec = r;
			EcashRec_ID = r->player_id;
		}
		if (!stricmp(r->user_id, "EcashFee")) {	// ecash fee account
			EcashFeeRec = r;
		}
		if (!stricmp(r->user_id, "Error")) {	// error account
			ErrorRec = r;
		}
		if (!stricmp(r->user_id, "Misc.")) {	// misc account
			MiscRec = r;
		}
		if (!stricmp(r->user_id, "Pending")) {	// pending credit account
			PendingRec = r;
			PendingRec_ID = r->player_id;
		}
		if (!stricmp(r->user_id, "DraftsIn")) {	// drafts received
			DraftsInRec = r;
			DraftsInRec_ID = r->player_id;
		}
		if (!stricmp(r->user_id, "Prizes")) {	// prizes account
			PrizesRec = r;
			PrizesRec_ID = r->player_id;
		}
		if (!stricmp(r->user_id, "BadBeats")) {	// bad beat account
			BadBeatRec = r;
			BadBeatRec_ID = r->player_id;
		}
		if (!stricmp(r->user_id, TOURNAMENT_CHIP_ACCOUNT_NAME)) {	// Tournament account
			TournamentRec = r;
			TournamentRec_ID = r->player_id;
		}
	}
	if (stale_fd) {
		stale_account_count = ftell(stale_fd)/sizeof(*r);
		if (number_of_accounts_purged) {
			kp(("%s(%d) stale.bin now contains %d stale accounts\n", _FL, stale_account_count));
		}
		fclose(stale_fd);
		stale_fd = 0;
	}
	if (iEmptyRecordSearchStart==-1) {
		Error(ERR_ERROR, "%s(%d) Database could not find any empty records for new accounts.", _FL);
		iEmptyRecordSearchStart = iRecordCount;	// put out of range.
	}
	if (players_with_chips_in_play) {
		kp(("%s(%d) **** There were %d players with chips in play ****\n", _FL, players_with_chips_in_play));
	}
	if (number_of_accounts_purged) {
		kp(("%s(%d) SDB: %d accounts were purged.  %d accounts (of %d) have never logged in\n",
				_FL, number_of_accounts_purged, never_logged_in_count, index_user_id_count));
	}
	if (DebugFilterLevel <= 0) {
		kp(("%s(%d) SDB: %d accounts are for real money.\n",_FL, real_money_accounts));
	}

	if (TournamentRec && TournamentRec->tournament_chips_in_play) {
		kp(("%s %s(%d) Zeroing %d tournament chips for player %s\n",
					TimeStr(), _FL, TournamentRec->tournament_chips_in_play, TournamentRec->user_id));
		TournamentRec->tournament_chips_in_play = 0;
	}
	// Sort the index arrays.
	SortIndexArrays();
  #if TEST_QSORT_DEPTH
	kp1(("%s(%d) ***** TESTING: Calling SortIndexArrays() again to test degenerate (pre-sorted) case.\n",_FL));
	SortIndexArrays();
  #endif
	if (!RakeRec) {
		kp1(("%s(%d) ***** No RakeRec found:  Create an account called 'Rake'\n",_FL));
	}
	if (!MarketingRec) {
		kp1(("%s(%d) ***** No MarketingRec found:  Create an account called 'Marketing'\n",_FL));
	}
	if (!EcashRec) {
		kp1(("%s(%d) ***** No EcashRec found:  Create an account called 'Ecash'\n",_FL));
	}
	if (!EcashFeeRec) {
		kp1(("%s(%d) ***** No EcashFeeRec found:  Create an account called 'EcashFee'\n",_FL));
	}
	if (!ErrorRec) {
		kp1(("%s(%d) ***** No Error found:  Create an account called 'Error'\n",_FL));
	}
	if (!MiscRec) {
		kp1(("%s(%d) ***** No MiscRec found:  Create an account called 'Misc.'\n",_FL));
	}
	if (!PendingRec) {
		kp1(("%s(%d) ***** No PendingRec found:  Create an account called 'Pending'\n",_FL));
	}
	if (!DraftsInRec) {
		kp1(("%s(%d) ***** No DraftsInRec found:  Create an account called 'DraftsIn'\n",_FL));
	}
	if (!PrizesRec) {
		kp1(("%s(%d) ***** No Prizes account found:  Create an account called 'Prizes'\n",_FL));
	}
	if (!BadBeatRec) {
		kp1(("%s(%d) ***** No BadBeats account found:  Create an account called 'BadBeats'\n",_FL));
	}
	if (!TournamentRec) {
		kp1(("%s(%d) ***** No Tournaments account found:  Create an account called 'Tournaments'\n",_FL));
	}
}

/**********************************************************************************
 Function ::WriteDataBase(void)
 date: kriskoin 2019/01/01 Purpose: write out the database (both bin and txt)
***********************************************************************************/
void SimpleDataBase::WriteDataBase(int force_write)
{
	// do this, at most, once per 5 seconds
	time_now = time(NULL);
	// sometimes, we want to force a write (within 5 seconds)
	if (!force_write) {	// if write is focred, we'll do it right away
		if (difftime(time_now, old_time) <  5) {	// wait 5 secs between writes unless urgent
			return;
		}
	}
	old_time = time_now;
	FlushDatabaseFile();	// tell OS to write out any dirty pages.

  #if 1	//kriskoin: 	EnterCriticalSection(&SDBCritSec);
	FILE *out_txt = NULL;

//	printf("\nAbout to write to %s\n",_txt_name);

	if ((out_txt = fopen(_txt_name, "wt")) == NULL) {
		Error(ERR_ERROR,"%s(%d) Couldn't open '%s' for write", _FL, _txt_name);
	}
	// write out the description/token line
	if (out_txt) {
		fprintf(out_txt,"# %s - modifications will be read in next time server is run\n", _txt_name);
		fprintf(out_txt,"# player_id,fullname_n,gendr,u_id,pw,city,email,real_bank,real_play,fake_bank,fake_play,hands,flops,rivs,valid_rec\n");
	}
	// write out the records
	SDBRecord *sdbr;

	for (int i=0; i < iRecordCount; i++) {
		sdbr = &_SDB[i];
		// write to txt (if there's a valid record there)
		if (out_txt/* ricardoGANG TEST && sdbr->valid_entry*/) {
			fprintf(out_txt, "%08d,%s,%d,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%s,%s,%d\n",
				sdbr->player_id, sdbr->full_name, sdbr->gender,
				sdbr->user_id, sdbr->password, sdbr->city,sdbr->email_address,
				sdbr->real_in_bank, sdbr->real_in_play, 
				sdbr->fake_in_bank, sdbr->fake_in_play,
				sdbr->hands_seen, sdbr->flops_seen, sdbr->rivers_seen, 
				sdbr->valid_entry,sdbr->idAffiliate,sdbr->last_name,sdbr->priv);
		}
	}
	if (out_txt) fclose(out_txt);
	LeaveCriticalSection(&SDBCritSec);
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Notify (by email) a player that there was a server problem
// but that his chips were taken care of properly.
//
void SimpleDataBase::NotifyPlayerOfServerProblem(SDBRecord *r)
{
  #if 1	//kriskoin: 	static int sent_email = 1;
  #else	// testing case... send a single test message when not running live
	static int sent_email = 0;
  #endif
	char curr_str1[MAX_CURRENCY_STRING_LEN];
	char curr_str2[MAX_CURRENCY_STRING_LEN];
	char curr_str3[MAX_CURRENCY_STRING_LEN];
	char curr_str4[MAX_CURRENCY_STRING_LEN];
	char curr_str5[MAX_CURRENCY_STRING_LEN];
	char curr_str6[MAX_CURRENCY_STRING_LEN];
	char curr_str7[MAX_CURRENCY_STRING_LEN];

	zstruct(curr_str1);
	zstruct(curr_str2);
	zstruct(curr_str3);
	zstruct(curr_str4);
	zstruct(curr_str5);
	zstruct(curr_str6);
	zstruct(curr_str7);

	if (r->tournament_chips_in_play) {
		TournamentRefundStructure trs;
		zstruct(trs);
		FillTournamentRefundStructure(r, &trs);
		kp(("%-10s %4d chips, %4d total, %6s pool, %6s share, %6s buy-in, %s fee, %7s total refund\n",
				r->user_id,
				trs.tournament_chips_in_play / 100,
				trs.tournament_total_chips_left_in_play / 100,
				CurrencyString(curr_str1, trs.tournament_creditable_pot, CT_REAL, TRUE),
				CurrencyString(curr_str2, trs.tournament_partial_payout, CT_REAL, TRUE),
				CurrencyString(curr_str3, trs.buyin_amount_paid, CT_REAL, TRUE),
				CurrencyString(curr_str4, trs.tournament_fee_paid, CT_REAL, TRUE),
				CurrencyString(curr_str5, trs.tournament_total_refund, CT_REAL, TRUE)));
	}

	if (!iRunningLiveFlag && sent_email) {
		// When not running live, we only send one email and it goes to support.
		// That's just for testing.
		// Since we've already sent one, just back out.
		return;
	}
	if (r->email_address[0] && !(r->flags & (SDBRECORD_FLAG_EMAIL_NOT_VALIDATED|SDBRECORD_FLAG_EMAIL_BOUNCES))) {	// they've got an email address.
		sent_email = TRUE;
		char fname[MAX_FNAME_LEN];
		MakeTempFName(fname, "e");	// create a temporary filename to write this to.

		FILE *fd = NULL;
		if ((fd = fopen(fname, "wt")) == NULL) {
			Error(ERR_ERROR,"%s(%d) Couldn't open tmp log file (%s) for write", _FL, fname);
			return;
		}

		// opened the file -- write a header
		fprintf(fd,"%s CST\n", TimeStr());
		fprintf(fd,"This e-mail was system generated emailed to %s\n", r->email_address);
		fprintf(fd,"-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-*-\n");
		fprintf(fd,"Player ID %s had chips at a table when there was a server event.\n", r->user_id);
		fprintf(fd,"The game you were participating in at the time of the event has been\n");
		fprintf(fd,"cancelled and all monies at the table in play or in your account\n");
		fprintf(fd,"have been returned to you.\n");
		fprintf(fd,"\n");
		fprintf(fd,"Last game number: %d\n", r->most_recent_games[0]);
		fprintf(fd,"(you may retrieve a hand history the Options menu in the main screen)\n");
		fprintf(fd,"\n");
		// tournament cancellation
		int total_refund = 0;
		int partial_refund = 0;
		int buyin_refund = 0;
		int fee_refund = 0;
		fprintf(fd,
			"---- Tournament Summary ----\n"
		);
		if (r->tournament_chips_in_play) {
			TournamentRefundStructure trs;
			zstruct(trs);
			FillTournamentRefundStructure(r, &trs);
			partial_refund = trs.tournament_partial_payout;		// used below in summaries
			fee_refund = trs.tournament_fee_paid;			// used below in summaries
			buyin_refund = trs.buyin_amount_paid;			// used below in summaries
			total_refund = trs.tournament_total_refund;		// used below in summaries
			fprintf(fd,
				//         10        20        30        40        50        60        70
				"You were playing in a tournament at the time and were holding\n"
				"%s chips out of the total %s chips in the tournament (%.2f%%).\n"
				"\n"
				"Any player still in the tournament who have chips will receive\n"
				"a complete refund of their buy-in and their entry fee.\n"
				"\n"
				"The remaining prize pool (%s from the players who busted out)\n"
				"will be paid out to the remaining players. The amount awarded\n"
				"to each player is directly proportional to the number of tournament\n"
				"chips the player had at the end of the last completed game.  \n"
				"\n"
				"Games which were cancelled before finishing, as per our policy, are\n"
				"counted. The division of the remaining pot is done in the interest \n"
				"of fairness."
				
				"We apologize for any inconvenience.\n"
				"any inconveniences.\n"
				
				"Your share of remaining prize pool: %s\n"
				"Buy-In Refund: %s\n"
				"Entry Fee Refund: %s\n"
				"TOTAL amount Refunded: %s\n"
				"\n",
				CurrencyString(curr_str1, trs.tournament_chips_in_play, CT_TOURNAMENT),
				CurrencyString(curr_str2, trs.tournament_total_chips_left_in_play, CT_TOURNAMENT),
				trs.percentage_held,
				CurrencyString(curr_str3, trs.tournament_creditable_pot, CT_REAL, TRUE),
				CurrencyString(curr_str4, partial_refund, CT_REAL, TRUE),
				CurrencyString(curr_str5, buyin_refund, CT_REAL, TRUE),
				CurrencyString(curr_str6, fee_refund, CT_REAL, TRUE),
				CurrencyString(curr_str7, partial_refund+buyin_refund+fee_refund, CT_REAL, TRUE)
			);
		} else {
			fprintf(fd,
				"You were not in a tournament at the time of this event.\n\n");
		}
		fprintf(fd,"****Real Money Account Summary*****\n");
		fprintf(fd,"%s   Available account balance at time of game cancellation\n", CurrencyString(curr_str1, r->real_in_bank, CT_REAL, TRUE));
		fprintf(fd,"%s   Total stake at all real money tables\n", CurrencyString(curr_str1, r->real_in_play, CT_REAL, TRUE));
		if (partial_refund) {
			fprintf(fd,"%s   Your share of the Tournament prize pool\n", CurrencyString(curr_str1, partial_refund, CT_REAL, TRUE));
		}
		if (buyin_refund) {
			fprintf(fd,"%s   Tournament Buy-In refund\n", CurrencyString(curr_str1, buyin_refund, CT_REAL, TRUE));
		}
		if (fee_refund) {
			fprintf(fd,"%s   Tournament Fee Refund\n", CurrencyString(curr_str1, fee_refund, CT_REAL, TRUE));
		}
		fprintf(fd,"------------\n");
		fprintf(fd,"%s  Current account balance\n", CurrencyString(curr_str1, r->real_in_bank+r->real_in_play+partial_refund+buyin_refund+fee_refund, CT_REAL, TRUE));
		fprintf(fd,"------------\n");
		fprintf(fd,"\n");

	  #if 0	// 2022 kriskoin
		fprintf(fd,"This is a rare occurrence and we apologize for any inconvenience to\n");
	  #else
		fprintf(fd,"This is a rare issue and we sincerely apologize for any inconvenience to\n");
	  #endif
		fprintf(fd,"the players that believe that they may have had winning hands. The game\n");
		fprintf(fd,"has been cancelled as per our policy.\n");
		fprintf(fd,"\n");
		fprintf(fd,"We are sorry for any inconvenience") ;
		fprintf(fd,"\n");
		fprintf(fd,"Please email support@kkrekop.io if you have any further questions.\n");
		fclose(fd);
		fd = NULL;
		char subject[200];
		zstruct(subject);
		if (!iRunningLiveFlag) {
			strcpy(subject, "TESTING ONLY - IGNORE: ");
		}
		strcat(subject, "Desert Poker Game Cancellation Notice - ");
		strcat(subject, r->user_id);

		if (iRunningLiveFlag) {
			// Send to the real person.
			Email(r->email_address,
					"Desert Poker",
					"support@kkrekop.io",
					subject,
					fname,
					"answers@kkrekop.io",	// Bcc:
					TRUE);							// delete file when done
		} else {
			// Send to support so we can test the game cancellation notice.
			Email(	"support@kkrekop.io",	// To:
					"Desert Poker",				// From "name"
					"support@kkrekop.io",	// From:
					subject,
					fname,
					"answers@kkrekop.io",	// Bcc:
					TRUE);							// delete file when done
		}
	}
}

/**********************************************************************************
 Function ::ParseTextToRecord(char *text_line, SDBRecord *sdbr)
 date: kriskoin 2019/01/01 Purpose: parse a text line into a SDB record
 Returns: T/F -- TRUE if the record was good, FALSE if it was bad
***********************************************************************************/
int SimpleDataBase::ParseTextToRecord(char *text_line, SDBRecord *sdbr)
{
	char *argv[SDB_RECORD_FIELD_NUM+51];
	int arg_count = GetArgs(SDB_RECORD_FIELD_NUM+51, argv, text_line, ',');
	if (arg_count != SDB_RECORD_FIELD_NUM) {	// bad text record
		if (arg_count) {
			Error(ERR_ERROR, "Bad text record (%s), count = %d", argv[0], arg_count);
		} else {
			Error(ERR_ERROR, "Bad text record (blank??)");
		}
		return FALSE;
	}
	// record is ok, let's fill the structure
	sdbr->player_id = atoi(argv[0]);
	strnncpy(sdbr->full_name, argv[1], MAX_PLAYER_FULLNAME_LEN);
	sdbr->gender = (BYTE8)(*argv[2]-'0');	// turn ASCII '2' into value 2, etc
	strnncpy(sdbr->user_id, argv[3], MAX_PLAYER_USERID_LEN);
	strnncpy(sdbr->password, argv[4], MAX_PLAYER_PASSWORD_LEN);
	strnncpy(sdbr->city, argv[5], MAX_COMMON_STRING_LEN);
	strnncpy(sdbr->email_address, argv[6], MAX_EMAIL_ADDRESS_LEN);
	sdbr->real_in_bank = atoi(argv[7]);
	sdbr->real_in_play = atoi(argv[8]);
	sdbr->fake_in_bank = atoi(argv[9]);
	sdbr->fake_in_play = atoi(argv[10]);
	sdbr->hands_seen = atoi(argv[11]);
	sdbr->flops_seen = atoi(argv[12]);
	sdbr->rivers_seen = atoi(argv[13]);
	sdbr->valid_entry = (BOOL8)atoi(argv[14]);
	strnncpy(sdbr->idAffiliate, argv[15], MAX_COMMON_STRING_LEN);
	strnncpy(sdbr->last_name, argv[16], MAX_PLAYER_LASTNAME_LEN);
	sdbr->priv = (BYTE8)atoi(argv[17]);

	return TRUE;
}

/**********************************************************************************
 Function ::GetArgs(int maxArgc, char *argv[], char *string, char seperator);
 date: kriskoin 2019/01/01 Purpose: parse a data line into arguments
***********************************************************************************/
int SimpleDataBase::GetArgs(int maxArgc, char *argv[], char *string, char seperator)
{
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

/**********************************************************************************
 Function ::SearchDataBaseByUserID(char *user_name, SDBRecord &result)
 date: kriskoin 2019/01/01 Purpose: search the database for this user id
 Return:  index value (offset) of this user, -1 if nothing found
***********************************************************************************/
int SimpleDataBase::SearchDataBaseByUserID(char *user_id)
{
	return SearchDataBaseByUserID(user_id, NULL);
}

int SimpleDataBase::SearchDataBaseByUserID(char *user_id, SDBRecord *result)
{
	// Do a binary search of the index to find the first entry
	// that matches.
	WORD32 search_hash = CalcStringHash(user_id);	// calc hash to search for

	if (indexes_need_sorting) {	// are the indexes out of date?
		SortIndexArrays();
	}

	int i = FindIndexEntry(index_user_id, index_user_id_count, search_hash);
	//kp(("%s(%d) FindIndexEntry() returned record index #%d\n", _FL, i));
	if (i>=0) {
		// Now we've got a hash match, step through linearly until we
		// find a string match.
		while (i < index_user_id_count) {
			int record = index_user_id[i].index;
			WORD32 new_hash = CalcStringHash(_SDB[record].user_id);
			//kp(("%s(%d) record %d: new_hash = $%08lx, search_hash = $%08lx\n",_FL, record, new_hash, search_hash));
			if (new_hash != search_hash) {
				// No chance of a match... record must not exist.
				return -1;	// no match.
			}
			if (!stricmp(_SDB[record].user_id, user_id)) {	// found it
				if (result) {
					memcpy(result, &_SDB[record], sizeof(SDBRecord));
				}
				//kp(("%s(%d) user_id '%s': player_id $%08lx\n", _FL, user_id, _SDB[record].player_id));
				return record;
			}
			i++;
		}
		//kp(("%s(%d) No match for '%s'\n", _FL, user_id));
	}
	return -1;	// not found
}

/**********************************************************************************
 Function SearchDataBaseByIndex(int index, SDBRecord *result);
 date: kriskoin 2019/01/01 Purpose: get a record via its index
***********************************************************************************/
ErrorType SimpleDataBase::SearchDataBaseByIndex(int index, SDBRecord *result)
{
	if (_SDB[index].valid_entry) {	// something useful there
		memcpy(result, &_SDB[index], sizeof(SDBRecord));
		return ERR_NONE;
	}
	// didn't find a valid record at this index
	return ERR_ERROR;
}

/**********************************************************************************
 Function ::SearchDataBaseByPlayerID(char *user_id, SDBRecord &result)
 date: kriskoin 2019/01/01 Purpose: search the database for this player id (unique WORD32)
 Return:  index value (offset) of this user, -1 if nothing found
***********************************************************************************/
int SimpleDataBase::SearchDataBaseByPlayerID(WORD32 player_id)
{
	return SearchDataBaseByPlayerID(player_id, NULL);
}

int SimpleDataBase::SearchDataBaseByPlayerID(WORD32 player_id, SDBRecord *result)
{
	// exception is for robots, player ID -1, who are always (all) record 0
	if (player_id == -1) return 0;
	if (ANONYMOUS_PLAYER(player_id)) return 0;

	EnterCriticalSection(&SDBCritSec);

	if (indexes_need_sorting) {	// are the indexes out of date?
		SortIndexArrays();
	}

	// Do a binary search of the index to find the first entry
	// that matches.
	int i = FindIndexEntry(index_player_id, index_player_id_count, player_id);
	if (i >= 0) {
		int record = index_player_id[i].index;
		if (_SDB[record].player_id==player_id) {	// found it.
			if (result) {
				memcpy(result, &_SDB[record], sizeof(SDBRecord));
			}
			LeaveCriticalSection(&SDBCritSec);
			return record;
		}
	}
	LeaveCriticalSection(&SDBCritSec);
	return -1;	// not found
}

/**********************************************************************************
 Function SimpleDataBase::GetCreditFeePoints
 Date: 2017/7/7 kriskoin Purpose: get current credit fee point balance
***********************************************************************************/
int SimpleDataBase::GetCreditFeePoints(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	int points = 0;
	if (index >= 0) { // found it
		// kriskoin : don't know why is this
		////points = _SDB[index].fee_credit_points;

		 //points = _SDB[index].pending_paypal;
		// end kriskoin 
		// kriskoin 
		points = _SDB[index].fee_credit_points;
		// end kriskoin 
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
//	WriteDataBase(FALSE);	// write out to disk, if needed
	return points;
}

// kriskoin 
/**********************************************************************************
 Function SimpleDataBase::GetTotalBonus
 Date: 2/22/2002
 Purpose: get total bonus earned within a period
***********************************************************************************/
int SimpleDataBase::GetTotalBonus(WORD32 player_id, WORD32 start_date, WORD32 end_date)
{
        EnterCriticalSection(&SDBCritSec);
        SDBRecord sdbr;
        int index = SearchDataBaseByPlayerID(player_id, &sdbr);
        int total_bonus = 0;
        if (index >= 0) { // found it
                for(int i=0;i<TRANS_TO_RECORD_PER_PLAYER;i++){
                        if(_SDB[index].transaction[i].timestamp < end_date && \
                                _SDB[index].transaction[i].timestamp > start_date){
                           if((strcmp(_SDB[index].transaction[i].str, "Bonus")==0 || \
				strcmp(_SDB[index].transaction[i].str, "Dep Bonus")==0) && \
                                _SDB[index].transaction[i].transaction_type == CTT_TRANSFER_IN){
                                total_bonus += _SDB[index].transaction[i].transaction_amount;
                           }
                        }
                }
        } else {
                Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts."
,
                        _FL, player_id);
        }
        LeaveCriticalSection(&SDBCritSec);
        return total_bonus;
}
// end kriskoin 


/**********************************************************************************
 Function SimpleDataBase::GetCreditFeePoints
 Date: 2017/7/7 kriskoin Purpose: get current credit fee point balance
***********************************************************************************/
int SimpleDataBase::GetPendingPaypalAmount(WORD32 player_id)
{
        EnterCriticalSection(&SDBCritSec);
        SDBRecord sdbr;
        int index = SearchDataBaseByPlayerID(player_id, &sdbr);
        int points = 0;
        if (index >= 0) { // found it
			//points = _SDB[index].fee_credit_points;
                 points = _SDB[index].pending_paypal;
        } else {
                Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts.",
                        _FL, player_id);
        }
        LeaveCriticalSection(&SDBCritSec);
//      WriteDataBase(FALSE);   // write out to disk, if needed
        return points;
}

/**********************************************************************************
 Function SimpleDataBase::AddToCreditFeePoints
 Date: 2017/7/7 kriskoin Purpose: add to the
***********************************************************************************/
void SimpleDataBase::AddToCreditFeePoints(WORD32 player_id, int points)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		_SDB[index].fee_credit_points += points;
	} else {

		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying add credit fee pts.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}


// kriskoin 
/**********************************************************************************
 Function SimpleDataBase::AddToGoodRakedGames
 Date: Robert 02/27/2002
 Purpose: add to the
***********************************************************************************/
void SimpleDataBase::AddToGoodRakedGames(WORD32 player_id, int points)
{
        EnterCriticalSection(&SDBCritSec);
        SDBRecord sdbr;
        int index = SearchDataBaseByPlayerID(player_id, &sdbr);
        if (index >= 0) { // found it
                _SDB[index].good_raked_games += points;
        } else {
                Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying add good_raked_games.",
                        _FL, player_id);
        }
        LeaveCriticalSection(&SDBCritSec);
        WriteDataBase(FALSE);   // write out to disk, if needed
}
// end kriskoin 

/**********************************************************************************
 Function SimpleDataBase::ClearCreditFeePoints
 Date: 2017/7/7 kriskoin Purpose:
***********************************************************************************/
void SimpleDataBase::ClearCreditFeePoints(WORD32 player_id)
{	
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it

		_SDB[index].fee_credit_points = 0;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying clear credit fee pts.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}

/**********************************************************************************
 Function SimpleDataBase::MoveEcashCreditToCash
 Date: 2017/7/7 kriskoin Purpose: move ecash pending credit into the cash account
***********************************************************************************/
void SimpleDataBase::MoveEcashCreditToCash(WORD32 player_id, int credit_amt)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		int amt_to_credit = min((int)_SDB[index].pending_fee_refund, credit_amt);
		_SDB[index].pending_fee_refund -= amt_to_credit;
		_SDB[index].real_in_bank += amt_to_credit;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%d) trying to move credit",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}

/**********************************************************************************
 Function SimpleDataBase::AddToEcashPendingCredit
 Date: 2017/7/7 kriskoin Purpose: add an amount to a player's pending credit
***********************************************************************************/
void SimpleDataBase::AddToEcashPendingCredit(WORD32 player_id, int credit_amt)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		RealChipChange += credit_amt;
		_SDB[index].pending_fee_refund += credit_amt;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%d) trying to add credit",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed

}

//*********************************************************
// https://github.com/kriskoin//
// Return the total amount of all pending checks for a player.
//
int SimpleDataBase::GetPendingCheckAmount(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int result = 0;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		result = sdbr.pending_check;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get pending check total.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	return result;
}


//*********************************************************
// 2002/3/15 - Robert
//
// Return the total amount of good_raked_games  for a player.
// * good_raked_games: $1+ raked games, use as a counter to auto-refund
//   the pending_credit_refund

int SimpleDataBase::GetGoodRakedGames(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int result = 0;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		result = sdbr.good_raked_games;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get good_raked_games.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	return result;
}



/**********************************************************************************
 Function SimpleDataBase::AddToChipsInBankForPlayerID()
 date: kriskoin 2019/01/01 Purpose: add/subtract to a players bank chips
***********************************************************************************/
void SimpleDataBase::AddToChipsInBankForPlayerID(WORD32 player_id, int chip_amount, ChipType chip_type)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		switch (chip_type) {
			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				FakeChipChange += chip_amount;

				_SDB[index].fake_in_bank += chip_amount;
				break;
			case CT_REAL:
				 RealChipChange += chip_amount;
				_SDB[index].real_in_bank += chip_amount;
				break;

			case CT_TOURNAMENT:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_TOURNAMENT (undefined behaviour)", _FL);
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
		}

	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%d) trying to +/- (%d) chip_type[%d] bank chips",

			player_id, chip_amount, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}

/**********************************************************************************
 Function SetChipsInBankForPlayerID()
 date: kriskoin 2019/01/01 Purpose: set the number of chips a player has in the bank
***********************************************************************************/
void SimpleDataBase::SetChipsInBankForPlayerID(WORD32 player_id, int chip_amount, ChipType chip_type)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		switch (chip_type) {
			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				FakeChipChange += chip_amount - _SDB[index].fake_in_bank;
				_SDB[index].fake_in_bank = chip_amount;
				break;
			case CT_REAL:
				RealChipChange += chip_amount - _SDB[index].real_in_bank;
				_SDB[index].real_in_bank = chip_amount;
				break;
			case CT_TOURNAMENT:
				Error(ERR_INTERNAL_ERROR,"%s(%d) SetChipsInBankForPlayerID(0x%08lx, %d) called with CT_TOURNAMENT (undefined behaviour)", _FL, player_id, chip_amount);
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) SetChipsInBankForPlayerID() called with unknown chip_type", _FL);
				break;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%d) trying to set (%d) chip_type[%d] bank chips",
			player_id, chip_amount, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}

/**********************************************************************************
 Function SetChipsInPlayForPlayerID()
 date: kriskoin 2019/01/01 Purpose: set the number of chips a player has in play
************************************************************************************/
void SimpleDataBase::SetChipsInPlayForPlayerID(WORD32 player_id, int chip_amount, ChipType chip_type)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		switch (chip_type) {

			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				FakeChipChange += chip_amount - _SDB[index].fake_in_play;
				_SDB[index].fake_in_play = chip_amount;
				break;
			case CT_REAL:
				RealChipChange += chip_amount - _SDB[index].real_in_play;
				_SDB[index].real_in_play = chip_amount;
				break;
			case CT_TOURNAMENT:
				TournamentChipChange += chip_amount - _SDB[index].tournament_chips_in_play;
				_SDB[index].tournament_chips_in_play = chip_amount;
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
				break;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%08lx) trying to set (%d) chip_type[%d] chips in play",
			player_id, chip_amount, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);	// write out to disk, if needed
}

/**********************************************************************************
 Function SimpleDataBase::GetPendingCreditForPlayerID
 Date: 2017/7/7 kriskoin Purpose: get pending ecash credit for this player id
***********************************************************************************/
int SimpleDataBase::GetPendingCreditForPlayerID(WORD32 player_id)
{
	SDBRecord sdbr;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		LeaveCriticalSection(&SDBCritSec);
		return _SDB[index].pending_fee_refund;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get pending credit",
			_FL, player_id);
		LeaveCriticalSection(&SDBCritSec);
		return 0;
	}
}

/**********************************************************************************
 Function SimpleDataBase::GetChipsInBankForPlayerID()
 date: kriskoin 2019/01/01 Purpose: get the number of chips in the bank for this particular player
***********************************************************************************/
int SimpleDataBase::GetChipsInBankForPlayerID(WORD32 player_id, ChipType chip_type)
{
	SDBRecord sdbr;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	int chips = 0;	// defaults to error condition
	if (index >= 0) { // found it
		switch (chip_type) {
			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				chips = _SDB[index].fake_in_bank;
				break;
			case CT_REAL:
				chips = _SDB[index].real_in_bank;
				break;
			case CT_TOURNAMENT:
				Error(ERR_INTERNAL_ERROR,"%s(%d) GetChipsInBankForPlayerID() called with CT_TOURNAMENT (should never be called!)", _FL);
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) GetChipsInBankForPlayerID() called with unknown chip_type", _FL);
				break;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get chip_type[%d] bank chips",
				_FL, player_id, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	return chips;
}

/**********************************************************************************
 Function SimpleDataBase::GetChipsInPlayForPlayerID()
 date: kriskoin 2019/01/01 Purpose: get the number of chips in the play for this particular player
***********************************************************************************/
int SimpleDataBase::GetChipsInPlayForPlayerID(WORD32 player_id, ChipType chip_type)
{
	SDBRecord sdbr;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	int chips = 0;
	if (index >= 0) { // found it
		switch (chip_type) {
			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				chips = _SDB[index].fake_in_play;
				break;
			case CT_REAL:
				chips = _SDB[index].real_in_play;

				break;
			case CT_TOURNAMENT:
				chips = _SDB[index].tournament_chips_in_play;
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
				break;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get chip_type[%d] chips in play",
				_FL, player_id, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	return chips;
}

/**********************************************************************************
 Function HandleEndGameChipChange(WORD32 player_id, WORD32 chips, int real_money_flag);
 date: kriskoin 2019/01/01 Purpose: do handling for chip net changes at end game
***********************************************************************************/
void SimpleDataBase::HandleEndGameChipChange(WORD32 player_id, WORD32 chips, ChipType chip_type)
{
	if (!player_id) {
		//kp(("%s(%d) Warning: HandleEndGameChipChange() has passed a zero player_id!\n", _FL));
		return;	// do nothing.
	}
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		switch (chip_type) {
			case CT_NONE:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
				break;
			case CT_PLAY:
				FakeChipChange += chips;
				_SDB[index].fake_in_play += chips;	// will be -ve if he lost
				if (_SDB[index].fake_in_play < 0) {
					Error(ERR_FATAL_ERROR,"SERIOUS %s(%d) fake chips in play for (%08lx) have gone to %d",
						_FL, player_id, _SDB[index].fake_in_play);
				}
				break;
			case CT_REAL:
				RealChipChange += chips;
				_SDB[index].real_in_play += chips;	// will be -ve if he lost
				if (_SDB[index].real_in_play < 0) {
					Error(ERR_FATAL_ERROR,"SERIOUS %s(%d) real chips in play for (%08lx) have gone to %d",
						_FL, player_id, _SDB[index].real_in_play);
				}
				break;
			case CT_TOURNAMENT:
				TournamentChipChange += chips;
				_SDB[index].tournament_chips_in_play += chips;
				if (_SDB[index].tournament_chips_in_play < 0) {
					Error(ERR_FATAL_ERROR,"SERIOUS %s(%d) tournament chips in play for (%08lx) have gone to %d",
						_FL, player_id, _SDB[index].tournament_chips_in_play);
				}
				break;
			default:
				Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id $%08lx (%d chips) in HandleEndGameChips (chip_type[%d])",
			_FL, player_id, chips, chip_type);
	}
	LeaveCriticalSection(&SDBCritSec);
	WriteDataBase(FALSE);
}

/**********************************************************************************
 Function SimpleDataBase::CheckForNewTextFile(void)
 date: kriskoin 2019/01/01 Purpose: check if we need to read in the .txt file
***********************************************************************************/
int SimpleDataBase::CheckForNewTextFile(void)
{
	struct stat txt_stat, bin_stat;
	zstruct(txt_stat);
	zstruct(bin_stat);
	if (stat(_txt_name, &txt_stat) !=0) {
		pr(("%s(%d) couldn't fstat %s\n", _FL, _txt_name));
		return FALSE;	// no txt file there at all
	}
	if (stat(_bin_name, &bin_stat) !=0) {
		pr(("%s(%d) couldn't fstat %s\n", _FL, _bin_name));
		return TRUE;	// no bin file... may as well try to read the text
	}
	pr(("timestamps: bin = %d, txt = %d\n", bin_stat.st_mtime, txt_stat.st_mtime));
	// return TRUE if txt is newer by 2 seconds or more, false if it isn't
	return (txt_stat.st_mtime > bin_stat.st_mtime+1 ? TRUE : FALSE);
}

//*********************************************************
// https://github.com/kriskoin//
// Buy into a tournament.  You always get STARTING_TOURNAMENT_CHIPS tournament chips.
// The money comes from the player's real in bank, the fee goes straight into rake.
// Player is left with a value in tournament_chips_in_play.
// If an error is returned, nothing was done.
// 24/01/01 kriskoin:
ErrorType SimpleDataBase::TournamentBuyIn(WORD32 player_id,
			WORD32 tournament_table_serial_number,
			int buyin_amount, int tournament_fee,
			WORD32 tournament_creditable_pot, WORD32 total_tournament_chips)
{
	EnterCriticalSection(&SDBCritSec);
	if (!TournamentRec) {
		// error was already printed by this point
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}

	int player_index = SearchDataBaseByPlayerID(player_id);
	if (player_index < 0) {
		Error(ERR_ERROR, "%s(%d) TournamentBuyIn(): Could not find player id 0x%08lx",
					_FL, player_id);
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}


	SDBRecord *p = _SDB + player_index;
	if (p->real_in_play < buyin_amount + tournament_fee) {
		// Not enough in bank to buy in.  This should never happen.
		Error(ERR_ERROR, "%s(%d) TournamentBuyIn(): Player 0x%08lx needs %d in bank but only has %d",
					_FL, player_id, buyin_amount + tournament_fee, p->real_in_bank);
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}

	// 24/01/01 kriskoin:
	// far, something previously has gone terribly wrong... we should have caught it by now.  It's
	// impossible (?) for the player to be sitting on two tournament tables at once, so we'll assume
	// he is OK to sit here and zero the chips.  Further below they will be set to the correct amount.
	if (p->tournament_chips_in_play) {
		// Player already has chips in play! He's only allowed one tourny at a time!
		Error(ERR_ERROR, "%s(%d) TournamentBuyIn(): Player 0x%08lx already has %d tournaments chips in play! zeroing them",
					_FL, player_id, p->tournament_chips_in_play);
		// set to zero and fall through to the buy-in below
		p->tournament_chips_in_play = 0;
	}

	// Everything looks clear... do the work.

	// to avoid accounting problems, be sure we're properly updating the RealChipChange
	
	// player's real_in_play goes to zero
	p->real_in_play -= (buyin_amount + tournament_fee);
	RealChipChange -= (buyin_amount + tournament_fee);
	// the real amount that will ultimately be paid back goes into the tournament account
	TournamentRec->real_in_bank += buyin_amount;
	RealChipChange += buyin_amount;
	// the fee goes to the rake account (AddToRake internally takes care of RealChipChange)
	AddToRakeAccount(tournament_fee, CT_REAL);
	// assign the player his tournament chips (taken from the tournament account)	
	TournamentRec->tournament_chips_in_play -= STARTING_TOURNAMENT_CHIPS;
	p->tournament_chips_in_play += STARTING_TOURNAMENT_CHIPS;
	
	p->tournament_table_serial_number = tournament_table_serial_number;
	p->tournament_fee_paid = (WORD16)tournament_fee;
	p->tournament_buyin_paid = buyin_amount;
	p->tournament_creditable_pot = tournament_creditable_pot;
	p->tournament_total_chips_in_play = total_tournament_chips;

	p->tournament_partial_payout = 0;
	LeaveCriticalSection(&SDBCritSec);
	return ERR_NONE;
}

/**********************************************************************************
 Function FillTournamentRefundStructure(WORD32 player_id, TournamentRefundStructure *trs);	
 date: 24/01/01 kriskoin Purpose: given a playerID, fill a TournamentRefundStructure
 NOTE:  this is where we figure out how much this player will get back...
		As per discussions, we will give back
***********************************************************************************/
ErrorType SimpleDataBase::FillTournamentRefundStructure(WORD32 player_id, TournamentRefundStructure *trs)	
{
	ErrorType rc = ERR_NONE;
	zstruct(*trs);
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
		rc = FillTournamentRefundStructure(&sdbr, trs);
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to fill TournamentResultStructure", _FL, player_id);
		rc = ERR_INTERNAL_ERROR;
	}
	LeaveCriticalSection(&SDBCritSec);
	return rc;
}

ErrorType SimpleDataBase::FillTournamentRefundStructure(SDBRecord *r, TournamentRefundStructure *trs)
{
	trs->player_id = r->player_id;
	trs->table_serial_number = r->tournament_table_serial_number;
	trs->tournament_creditable_pot = r->tournament_creditable_pot;
	trs->tournament_total_chips_left_in_play = r->tournament_total_chips_in_play;
	trs->tournament_chips_in_play = r->tournament_chips_in_play;
	trs->tournament_fee_paid = r->tournament_fee_paid;
	trs->buyin_amount_paid = r->tournament_buyin_paid;
	trs->tournament_partial_payout = r->tournament_partial_payout;
	// the following are calculated
	if (trs->tournament_chips_in_play && trs->tournament_total_chips_left_in_play) {
		// anyone playing gets their buy-in back plus their proportion of tournament chips left mapped
		// to the remaining tournament pot that's still unpaid
		trs->percentage_held = (float)((100.0 * trs->tournament_chips_in_play / trs->tournament_total_chips_left_in_play));
//		trs->tournament_partial_refund = (int)((trs->tournament_pot_left*trs->percentage_held+50) / 100);
		trs->tournament_total_refund = trs->tournament_partial_payout+trs->buyin_amount_paid+trs->tournament_fee_paid;
	}
	return ERR_NONE;
}


//*********************************************************
// https://github.com/kriskoin//
// Cash out of a tournament.
// The money comes from the player's real in bank, the fee goes straight into rake.
// Player is left with a value in tournament_chips_in_play.
// If an error is returned, nothing was done.
//

ErrorType SimpleDataBase::TournamentCashout(TournamentRefundStructure *trs)	// 20:::{
	// feed parameters that we'd be using if we were refunding the tournament
	return TournamentCashout(trs->player_id, trs->table_serial_number,
		trs->tournament_chips_in_play, trs->tournament_partial_payout+trs->buyin_amount_paid, trs->tournament_fee_paid);
}

ErrorType SimpleDataBase::TournamentCashout(WORD32 player_id,
			WORD32 tournament_table_serial_number,	// for error checking purposes
			int tournament_chips_to_cash_out,		// for error checking purposes
			int cashout_amount,						// real $'s to cash out (no error checking)
			int tournament_fee_to_refund)			// amount of fee to refund (usually zero)
{
	EnterCriticalSection(&SDBCritSec);
	if (!TournamentRec) {
		// An error message must already have been printed.  Don't print any more.
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}

	int player_index = SearchDataBaseByPlayerID(player_id);
	if (player_index < 0) {
		Error(ERR_ERROR, "%s(%d) TournamentCashout(): Could not find player id 0x%08lx",
					_FL, player_id);
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}

	SDBRecord *p = _SDB + player_index;
	if (p->tournament_table_serial_number != tournament_table_serial_number) {
		// Cashing out from wrong table!
		Error(ERR_ERROR, "%s(%d) TournamentCashout(): Player 0x%08lx is cashing out of table %d instead of %d!",
					_FL, player_id, tournament_table_serial_number, p->tournament_table_serial_number);
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}
	if (p->tournament_chips_in_play != tournament_chips_to_cash_out) {
		// Player already has chips in play! He's only allowed one tourny at a time!
		Error(ERR_ERROR, "%s(%d) TournamentCashout(): Player 0x%08lx is cashing out %d instead of %d!!",
					_FL, player_id, tournament_chips_to_cash_out, p->tournament_chips_in_play);
		LeaveCriticalSection(&SDBCritSec);
		return ERR_ERROR;
	}

  #if 0	// now supported below
	if (tournament_fee_to_refund) {
		Error(ERR_ERROR, "%s(%d) TournamentCashout(): Player 0x%08lx should get refunded %d of fee.  Not yet supported.",
					_FL, player_id, tournament_fee_to_refund);
	}
  #endif

	// Everything looks clear... do the work.
	TournamentRec->real_in_bank -= cashout_amount;
	p->real_in_bank += cashout_amount;
	if (tournament_fee_to_refund) {
		p->real_in_bank += tournament_fee_to_refund;
		RealChipChange += tournament_fee_to_refund;
		SubtractFromRakeAccount(tournament_fee_to_refund, CT_REAL);
	}
	TournamentRec->tournament_chips_in_play += tournament_chips_to_cash_out;
	p->tournament_chips_in_play -= tournament_chips_to_cash_out;
	p->tournament_total_chips_in_play -= tournament_chips_to_cash_out;
	p->tournament_table_serial_number = 0;
	p->tournament_fee_paid = 0;
	p->tournament_creditable_pot =0;
	p->tournament_partial_payout = 0;
	p->tournament_buyin_paid = 0;
	LeaveCriticalSection(&SDBCritSec);
	return ERR_NONE;
}

/**********************************************************************************
 Function SimpleDataBase::SetCreditableTournamentPot();
 date: 24/01/01 kriskoin Purpose: set the prize pool pot left in tournament for this player
************************************************************************************/
ErrorType SimpleDataBase::SetCreditableTournamentPot(WORD32 player_id, WORD32 tournament_creditable_pot)
{
	ErrorType rc = ERR_NONE;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		_SDB[index].tournament_creditable_pot = tournament_creditable_pot;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to set tournament_creditable_pot to %d",
			_FL, player_id, tournament_creditable_pot);
		rc = ERR_INTERNAL_ERROR;
	}
	LeaveCriticalSection(&SDBCritSec);
	return rc;
}

/**********************************************************************************
 Function SimpleDataBase::GetTotalTournamentChipsLeft();
 date: 24/01/01 kriskoin Purpose: tell us how many chips are left in local tournament universe for this player
***********************************************************************************/
WORD32 SimpleDataBase::GetTotalTournamentChipsLeft(WORD32 player_id)
{

	WORD32 chips_left = 0;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		chips_left = _SDB[index].tournament_total_chips_in_play;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to find total tourney chips left",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	return chips_left;
}

/**********************************************************************************
 Function SimpleDataBase::SetTotalTournamentChipsLeft();
 date: 24/01/01 kriskoin Purpose: set the local tournament chips universe for this player
***********************************************************************************/
ErrorType SimpleDataBase::SetTotalTournamentChipsLeft(WORD32 player_id, WORD32 chips_left)
{
	ErrorType rc = ERR_NONE;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		_SDB[index].tournament_total_chips_in_play = chips_left;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to set total tourney chips left to %d",
			_FL, player_id, chips_left);
		rc = ERR_INTERNAL_ERROR;
	}
	LeaveCriticalSection(&SDBCritSec);
	return rc;
}

/**********************************************************************************
 Function SimpleDataBase::SetTournamentPartialPayout();
 date: 24/01/01 kriskoin Purpose: set potential partial payout for this player's current tournament
***********************************************************************************/
ErrorType SimpleDataBase::SetTournamentPartialPayout(WORD32 player_id, WORD32 partial_payout)
{
	ErrorType rc = ERR_NONE;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		_SDB[index].tournament_partial_payout = partial_payout;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) in SetTournamentPartialPayout() to %d",
			_FL, player_id, partial_payout);
		rc = ERR_INTERNAL_ERROR;
	}
	LeaveCriticalSection(&SDBCritSec);
	return rc;
}

/**********************************************************************************
 Function TransferChips
 Date: 2017/7/7 kriskoin Purpose: transfer real money chips from one account to another
 **********************************************************************************/
void SimpleDataBase::TransferChips(WORD32 from_player_id, WORD32 to_player_id, WORD32 chips)
{
	EnterCriticalSection(&SDBCritSec);
	int from_index = SearchDataBaseByPlayerID(from_player_id);
	int to_index = SearchDataBaseByPlayerID(to_player_id);
	if (from_index >= 0 && to_index >= 0) { // found them
		_SDB[from_index].real_in_bank -= chips;
		_SDB[to_index].real_in_bank += chips;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id %08lx or %08lx trying to transfer %d chips",
			_FL, from_player_id, to_player_id, chips);
	}
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Generic transfer chips function.  Capable of transferring both
// real or play money chips from any account to any account.  Includes
// a variable to indicate which field of the account to transfer to/from:
// fields: 0=in bank, 1=pending CC refund field, 2=pending check field
// No logging is done by this function - that should be done at a higher level.
//
void SimpleDataBase::TransferChips(ChipType chip_type, WORD32 from_player_id,
		int from_account_field, WORD32 to_player_id, int to_account_field, int chips,
		char *calling_file, int calling_line)
{
	EnterCriticalSection(&SDBCritSec);
	int from_index = SearchDataBaseByPlayerID(from_player_id);
	int to_index = SearchDataBaseByPlayerID(to_player_id);
	if (from_index >= 0 && to_index >= 0) { // found them
		if (chip_type == CT_REAL) {
			int print_error = FALSE;
			int from_balance = 0;
			int to_balance = 0;
			switch (from_account_field) {
			case AF_PENDING_CC_REFUND:

				_SDB[from_index].pending_fee_refund -= chips;
				from_balance = _SDB[from_index].pending_fee_refund;
				if (from_balance < 0) {
					print_error = TRUE;
				}
				break;
			case AF_PENDING_CHECK:
				_SDB[from_index].pending_check -= chips;
				from_balance = _SDB[from_index].pending_check;
				if (from_balance < 0) {
					print_error = TRUE;
				}
				break;
		  	
			case AF_PENDING_PAYPAL:
				_SDB[from_index].pending_paypal -= chips;

				from_balance = _SDB[from_index].pending_paypal;

				if (from_balance < 0) {

					print_error = TRUE;

				}

				break;
			
			default:
				_SDB[from_index].real_in_bank -= chips;
				from_balance = _SDB[from_index].real_in_bank;

				break;
			}
			switch (to_account_field) {
			case AF_PENDING_CC_REFUND:
				_SDB[to_index].pending_fee_refund += chips;
				to_balance = _SDB[to_index].pending_fee_refund;
				if (to_balance < 0) {
					print_error = TRUE;
				}
				break;
			case AF_PENDING_CHECK:
				_SDB[to_index].pending_check += chips;
				to_balance = _SDB[to_index].pending_check;
				if (to_balance < 0) {
					print_error = TRUE;
				}
				break;
			
			case AF_PENDING_PAYPAL:
				_SDB[to_index].pending_paypal += chips;
				to_balance = _SDB[to_index].pending_paypal;
				if (to_balance < 0) {
					print_error = TRUE;

				}

			break;
			
			default:
				_SDB[to_index].real_in_bank += chips;
				to_balance = _SDB[to_index].real_in_bank;
				break;
			}
			// kriskoin  04/09/2002
			//if to_account's real_in_bank greater than $45, he is a real player
			if(_SDB[to_index].real_in_bank>4500) {
				_SDB[to_index].flags |= SDBRECORD_FLAG_REAL_PLAYER;
			}	
			// end kriskoin 
			if (print_error) {
				//kriskoin: 				// some sort of accounting bug.  Print some details to help make
				// tracking these problems down much easier.
				char curr_str1[MAX_CURRENCY_STRING_LEN];
				char curr_str2[MAX_CURRENCY_STRING_LEN];
				char curr_str3[MAX_CURRENCY_STRING_LEN];
				Error(ERR_ERROR, "TransferChips() detected an error: %s xferd: %s [%d] = %s, %s [%d] = %s, called from %s(%d)",
							CurrencyString(curr_str1, chips, CT_REAL),
							_SDB[from_index].user_id, from_account_field,
							CurrencyString(curr_str2, from_balance, CT_REAL),
							_SDB[to_index].user_id, to_account_field,
							CurrencyString(curr_str3, to_balance, CT_REAL),
							calling_file, calling_line);
			}
		} else if (chip_type == CT_PLAY) {
			// Ignore fields.. there's no pending for play money
			_SDB[from_index].fake_in_bank -= chips;
			_SDB[to_index].fake_in_bank += chips;
		} else if (chip_type == CT_TOURNAMENT) {
			Error(ERR_INTERNAL_ERROR,"%s(%d) Unsupported for CT_TOURNAMENT", _FL);
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id %08lx or %08lx trying to transfer %d chips",
			_FL, from_player_id, to_player_id, chips);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::MoveChipsFromPlayingToBank()
 date: kriskoin 2019/01/01 Purpose: upon startup, there shouldn't be any chips in the in_play field.  If there
          are, it implies improper shutdown -- ie, a player was left on the table
		  when the server shut down.  in this case, we'll move the chips back
***********************************************************************************/
void SimpleDataBase::MoveChipsFromPlayingToBank(SDBRecord *rec, ChipType chip_type)
{
	EnterCriticalSection(&SDBCritSec);
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			{
			WORD32 fake_chips_to_move = rec->fake_in_play;
			rec->fake_in_bank += fake_chips_to_move;
			rec->fake_in_play -= fake_chips_to_move;
			}
			break;
		case CT_REAL:
			{
			WORD32 real_chips_to_move = rec->real_in_play;
			rec->real_in_bank += real_chips_to_move;
			rec->real_in_play -= real_chips_to_move;
			}
			break;
		case CT_TOURNAMENT:

			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_TOURNAMENT (undefined behaviour)", _FL);
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function TransferChipsToPendingAccount
 Date: 2017/7/7 kriskoin Purpose: transfer from a player to the Pending account
***********************************************************************************/
void SimpleDataBase::TransferChipsToPendingAccount(WORD32 player_id, int chips)	
{
	if (!PendingRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find Pending account trying to transfer in %d chips", _FL, chips);
		return;
	}
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		_SDB[index].real_in_bank -= chips;
		PendingRec->real_in_bank += chips;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to transfer %d chips to Pending",
			_FL, player_id, chips);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function TransferChipsFromPendingAccount
 Date: 2017/7/7 kriskoin Purpose: transfer to a player from the Pending account
***********************************************************************************/
void SimpleDataBase::TransferChipsFromPendingAccount(WORD32 player_id, int chips)	
{
	if (!PendingRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find Pending account trying to transfer out %d chips", _FL, chips);
		return;
	}
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		_SDB[index].real_in_bank += chips;
		PendingRec->real_in_bank -= chips;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to transfer %d chips from Pending",
			_FL, player_id, chips);
	}
	LeaveCriticalSection(&SDBCritSec);


}
/**********************************************************************************
 Function SimpleDataBase::TransferChipsFromPendingToEcash
 Date: 2017/7/7 kriskoin Purpose: this used when a credit has successfully gone through
***********************************************************************************/
void SimpleDataBase::TransferChipsFromPendingToEcash(int chips)
{
	if (!PendingRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find Pending account trying to transfer %d chips to Ecash", _FL, chips);
		return;
	}
	if (!EcashRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find Ecash account trying to transfer %d chips to Ecash", _FL, chips);
		return;
	}
	EnterCriticalSection(&SDBCritSec);
	EcashRec->real_in_bank += chips;
	PendingRec->real_in_bank -= chips;
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::AddOrRemoveChipsInEcashFeeAccount(int chips)
 Date: 2017/7/7 kriskoin Purpose: the fees we pay to our ecash provider go here
***********************************************************************************/
void SimpleDataBase::AddOrRemoveChipsInEcashFeeAccount(int chips)
{
	if (!EcashFeeRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find EcashFee account trying to add %d chips", _FL, chips);
		return;
	}
	EnterCriticalSection(&SDBCritSec);
	RealChipChange += chips;
	EcashFeeRec->real_in_bank += chips;
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::SetCreditLeftForTransaction
 date: 24/01/01 kriskoin Purpose: modify how much can be credited back to a transaction (not knowing index)
***********************************************************************************/
void SimpleDataBase::SetCreditLeftForTransaction(WORD32 player_id, WORD32 ecash_id, BYTE8 tr_type, int cr_left)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		for (int tr_num=0; tr_num < TRANS_TO_RECORD_PER_PLAYER; tr_num++) {
			if (_SDB[index].transaction[tr_num].ecash_id == ecash_id &&
				_SDB[index].transaction[tr_num].transaction_type == tr_type) {	// found it
					_SDB[index].transaction[tr_num].credit_left = cr_left;
			}
		}
	} else {
		Error(ERR_INTERNAL_ERROR,
			"%s(%d) didn't find player_id (%08lx) trying to set credit left (%d) on ecash ID # %d",
			_FL, player_id, cr_left, ecash_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}



/**********************************************************************************
 void SimpleDataBase::ModifyCheckTransaction
 Date: 20180707 kriskoin :  Purpose: modify a check transaction entry
***********************************************************************************/
void SimpleDataBase::ModifyCheckTransaction(WORD32 player_id, WORD32 check_number,
	int check_amt, char *tracking_number, BYTE8 delivery_method)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// find corresponding check entry
		for (int tr_num=0; tr_num < TRANS_TO_RECORD_PER_PLAYER; tr_num++) {
			if (_SDB[index].transaction[tr_num].ecash_id == (WORD32)check_number &&
				_SDB[index].transaction[tr_num].transaction_type == CTT_CHECK_ISSUED) {	// found it
					// cast result into our version of this
					ClientCheckTransaction *cctp = (ClientCheckTransaction *)(&_SDB[index].transaction[tr_num]);
					cctp->transaction_amount = check_amt;
					cctp->delivery_method = (BYTE8)delivery_method;
					memcpy(cctp->first_eight, tracking_number, 8);
					memcpy(cctp->last_ten, tracking_number+8, 10);
			}
		}
	} else {
		Error(ERR_INTERNAL_ERROR,
			"%s(%d) didn't find player_id (%08lx) trying to modify check entry %d",
			_FL, player_id, check_number);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::SetCreditLeftForTransaction
 Date: 2017/7/7 kriskoin
 Purpose: modify how much can be credited back to a transaction
***********************************************************************************/
void SimpleDataBase::SetCreditLeftForTransaction(WORD32 player_id, int tr_num, int cr_left)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		_SDB[index].transaction[tr_num].credit_left = cr_left;
	} else {
		Error(ERR_INTERNAL_ERROR,
			"%s(%d) didn't find player_id (%08lx) trying to set credit left (%d) on transaction # %d",
			_FL, player_id, cr_left, tr_num);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::AddOrRemoveChipsInEcashAccount
 Date: 2017/7/7 kriskoin Purpose: the ecash providers account -- when someone buys chips, add here too
 Note: this is the other side of the transaction -- when player buys, this goes negative
***********************************************************************************/
void SimpleDataBase::AddOrRemoveChipsInEcashAccount(int chips)
{
	if (!EcashRec) {
		Error(ERR_ERROR, "%s(%d) Didn't find ecash account trying to add %d chips", _FL, chips);
		return;
	}
	EnterCriticalSection(&SDBCritSec);
	RealChipChange += chips;
	EcashRec->real_in_bank += chips;
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::SubtractFromRakeAccount
 date: 24/01/01 kriskoin Purpose: take money out of rake account (used for refunds)
***********************************************************************************/
void SimpleDataBase::SubtractFromRakeAccount(WORD32 chips, ChipType chip_type)
{
	if (!RakeRec) return;	// no Rake account to work with
	EnterCriticalSection(&SDBCritSec);
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			FakeChipChange -= chips;
			RakeRec->fake_in_bank -= chips;
			break;
		case CT_REAL:
			RealChipChange -= chips;
			RakeRec->real_in_bank -= chips;
			break;
		case CT_TOURNAMENT:
			// we don't rake tournament tables
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::AddToRakeAccount()
 date: kriskoin 2019/01/01 Purpose: add chips to the rake account
***********************************************************************************/
void SimpleDataBase::AddToRakeAccount(WORD32 chips, ChipType chip_type)
{
	if (!RakeRec) return;	// no Rake account to work with
	EnterCriticalSection(&SDBCritSec);
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			FakeChipChange += chips;
			RakeRec->fake_in_bank += chips;
			break;
		case CT_REAL:
			RealChipChange += chips;
			RakeRec->real_in_bank += chips;
			break;
		case CT_TOURNAMENT:
			// we don't rake tournament tables
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::GetRake(void)
 date: kriskoin 2019/01/01 Purpose: get the current quantity in the rake account
***********************************************************************************/
WORD32 SimpleDataBase::GetRake(ChipType chip_type)
{
	if (!RakeRec) return 0;	// no Rake account to work with
	// if it's not real money, just return 0
	return 	(chip_type == CT_REAL ? RakeRec->real_in_bank : 0);
}

/**********************************************************************************
 Function ClearRake(void);
 date: kriskoin 2019/01/01 Purpose: set the rake account to zero
***********************************************************************************/
void SimpleDataBase::ClearRake(ChipType chip_type)
{
	if (!RakeRec) return;	// no Rake account to work with
	EnterCriticalSection(&SDBCritSec);
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			FakeChipChange -= RakeRec->fake_in_bank;
			FakeChipUniverse -= RakeRec->fake_in_bank;
			RakeRec->fake_in_bank = 0;
			break;
		case CT_REAL:
			RealChipChange -= RakeRec->real_in_bank;
			RealChipUniverse -= RakeRec->real_in_bank;
			RakeRec->real_in_bank = 0;
			break;
		case CT_TOURNAMENT:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_TOURNAMENT (undefined behaviour)", _FL);
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::CountAllChips(WORD32 game_serial_number)
 date: kriskoin 2019/01/01 Purpose: total up all the chips out there, including rake
 Note:	  unless we're moving chips in or out of the game universe, this number
          shouldn't change -- it should remain constant with InitialChipCount
 WARNING: this function touches every record of the database.
***********************************************************************************/
WORD32 SimpleDataBase::CountAllChips(WORD32 game_serial_number)
{
	if (RealChipChange) {
		Error(ERR_FATAL_ERROR, "%s(%d) Accounting error (game %d): Real chip count is out by %d chips.", _FL,
			game_serial_number, RealChipChange);
		RealChipChange = 0;	// reset
	}
	if (FakeChipChange) {
		Error(ERR_FATAL_ERROR, "%s(%d) Accounting error (game %d): Fake chip count is out by %d chips.", _FL,
			game_serial_number, FakeChipChange);
		FakeChipChange = 0;	// reset
	}

	// The rest of this function touches every record in the database
	// and can take far too long when the database gets big.
	// We should change it so that this function only gets called
	// about once per day, perhaps when we're not busy or during
	// a short period of daily maintenance.  Suggestions are welcome.

  #if OPTIMIZE_FOR_LARGE_DATABASE	// 2022 kriskoin
	static int run_count = 0;
	if (run_count >= 5) {
		if (DebugFilterLevel <= 0) {
			kp1(("%s(%d) The thorough part of CountAllChips() has been disabled.\n", _FL));
		}
		return 0;
	}
	run_count++;
  #endif
	long real_count = 0, fake_count = 0, tournament_count = 0;
	EnterCriticalSection(&SDBCritSec);
	int start_ticks = GetTickCount();
	for (int i=0; i < iRecordCount; i++) {
		if (DebugFilterLevel <= 0) {
			if (i && !(i%SEQUENTIAL_SEARCH_STATUS_INTERVAL)) {
				kp(("%s(%d) %s SDB::CountAllChips: reading record %d of %d\n", _FL, TimeStr(), i, iRecordCount));
			}
		}
		if (_SDB[i].valid_entry) {
			real_count += _SDB[i].real_in_bank;
			real_count += _SDB[i].real_in_play;
			real_count += _SDB[i].pending_fee_refund;
			real_count += _SDB[i].pending_check;
			fake_count += _SDB[i].fake_in_bank;
			fake_count += _SDB[i].fake_in_play;

			tournament_count += _SDB[i].tournament_chips_in_play;
		}
	}
	int elapsed = GetTickCount() - start_ticks;

	if (DebugFilterLevel <= 0 && elapsed >= 100) {
		kp(("%s(%d) CountAllChips() took %dms to go through the database\n",_FL,elapsed));
	}
	if (real_count != RealChipUniverse) {
		static int printed_real_total_chips_error = 0;
		if (printed_real_total_chips_error < 100) {
			printed_real_total_chips_error++;	// only print it 100 times

			Error(ERR_FATAL_ERROR,"After game %d -- RealChipCount = %d, last was %d",
				game_serial_number, real_count, RealChipUniverse);
		}
		RealChipUniverse = real_count;
	}
	if (fake_count != FakeChipUniverse) {
		static int printed_fake_total_chips_error = 0;
		if (printed_fake_total_chips_error < 100) {
			printed_fake_total_chips_error++;	// only print it 100 times
			Error(ERR_FATAL_ERROR,"After game %d -- FakeChipCount = %d, last was %d",
				game_serial_number, fake_count, FakeChipUniverse);
		}
		FakeChipUniverse = fake_count;
	}

	if (tournament_count != TournamentChipUniverse) {
		static int printed_tournament_total_chips_error = 0;
		if (printed_tournament_total_chips_error < 100) {
			printed_tournament_total_chips_error++;	// only print it 100 times
			Error(ERR_FATAL_ERROR,"After game %d -- TournamentChipCount = %d, last was %d",
				game_serial_number, tournament_count, TournamentChipUniverse);
		}
		TournamentChipUniverse = tournament_count;
	}

	char str1[MAX_CURRENCY_STRING_LEN];

	char str2[MAX_CURRENCY_STRING_LEN];
	char str3[MAX_CURRENCY_STRING_LEN];
	kp1(("**** CHIP UNIVERSE: REAL: %s | PLAY: %s | TOURN: %s ****\n",
			CurrencyString(str1, real_count, CT_REAL),
			CurrencyString(str2, fake_count, CT_PLAY),
			CurrencyString(str3, tournament_count, CT_TOURNAMENT)));

	static int fixed_universe = FALSE;
	if (!fixed_universe && (RealChipUniverse /*|| FakeChipUniverse*/ || TournamentChipUniverse)) {
		fixed_universe = TRUE;
		if (iRunningLiveFlag) {
			EmailStr(
					"management@kkrekop.io",	// to:
					"Server - SDB",					// from_name
					"server@kkrekop.io",		// from_address
					"Chip Universe adjustments",	// subject
					NULL,							// bcc,
					"%s"
					"During server startup it was detected that the chip universe\n"
					"did not add up to zero.  The following adjustments were made\n"
					"to the BlackHole account to compensate:\n"
					"\n"
					"%s real money\n"
					"%s play money\n"
					"%s tournament chips\n"
					"\n"
					"The chip universe now sums to zero.\n"
					,
					iRunningLiveFlag ? "" : "*** THIS IS A TEST ***\n\n",
					CurrencyString(str1, RealChipUniverse, CT_REAL),
					CurrencyString(str2, FakeChipUniverse, CT_PLAY),
					CurrencyString(str3, TournamentChipUniverse, CT_TOURNAMENT)
			);
		}

		int black_hole_index = SearchDataBaseByUserID(BLACKHOLE_CHIP_ACCOUNT_NAME);
		if (black_hole_index >= 0) { // found them
			_SDB[black_hole_index].real_in_bank -= RealChipUniverse;
			_SDB[black_hole_index].fake_in_bank -= FakeChipUniverse;
			_SDB[black_hole_index].tournament_chips_in_play -= TournamentChipUniverse;
			RealChipUniverse = 0;
			FakeChipUniverse = 0;
			TournamentChipUniverse = 0;
		} else {
			Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find BlackHole account!  Create one.", _FL);
		}
	}

	LeaveCriticalSection(&SDBCritSec);
	return real_count+fake_count;
}

/**********************************************************************************
 Function SimpleDataBase::PlayerSawPocket(WORD32 player_id)
 date: kriskoin 2019/01/01 Purpose: we log players who saw their pocket cards
***********************************************************************************/
void SimpleDataBase::PlayerSawPocket(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		_SDB[index].hands_seen++;
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%08lx) trying to add to pocket count",
			player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::PlayerSawFlop(WORD32 player_id)
 date: kriskoin 2019/01/01 Purpose: we log players who saw the flop
***********************************************************************************/
void SimpleDataBase::PlayerSawFlop(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		_SDB[index].flops_seen++;
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%08lx) trying to add to flops count",
			player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::PlayerSawRiver(WORD32 player_id)
 date: kriskoin 2019/01/01 Purpose: we log players who saw the river card
***********************************************************************************/
void SimpleDataBase::PlayerSawRiver(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		_SDB[index].rivers_seen++;

	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%08lx) trying to add to rivers count",
			player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Add a game_serial_number to the list of recent games
// a player has played.

//
void SimpleDataBase::AddGameToPlayerHistory(WORD32 player_id, WORD32 game_serial_number)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// scroll history up one entry
		memmove(&_SDB[index].most_recent_games[1], &_SDB[index].most_recent_games[0], 
			sizeof(WORD32)*(GAMES_TO_RECORD_PER_PLAYER-1) );
		// set the new one
		_SDB[index].most_recent_games[0] = game_serial_number;
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id (%08lx) trying to add to game_serial_number", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Save login information for a player to his player record.
//  Allen Ko
void SimpleDataBase::SavePlayerLoginInfo(WORD32 player_id, time_t login_time, WORD32 ip_address, struct ClientPlatform *client_platform, struct VersionNumber *client_version)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// scroll history up one entry
		memmove(&_SDB[index].last_login_times[1], &_SDB[index].last_login_times[0], 
				sizeof(time_t)*(LOGINS_TO_RECORD_PER_PLAYER-1));
		memmove(&_SDB[index].last_login_ip[1], &_SDB[index].last_login_ip[0], 
				sizeof(WORD32)*(LOGINS_TO_RECORD_PER_PLAYER-1));
		memmove(&_SDB[index].last_login_computer_serial_nums[1], &_SDB[index].last_login_computer_serial_nums[0], 
				sizeof(WORD32)*(LOGINS_TO_RECORD_PER_PLAYER-1));
		// set the new one
		_SDB[index].last_login_times[0] = login_time;
		_SDB[index].last_login_ip[0] = ip_address;
		_SDB[index].last_login_computer_serial_nums[0] = client_platform->computer_serial_num;
		
		// If client_platform information is not empty, save it.
		struct ClientPlatform cp;
		zstruct(cp);
		if (memcmp(&cp, client_platform, sizeof(cp))) {
			// non-zero... save it.
			_SDB[index].client_platform = *client_platform;
		}
		// Save sequential client version number (32-bit field only)
		if (client_version->build) {
			_SDB[index].client_version = client_version->build;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx trying to add login info", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Set the client_version and client_platform fields without
// recording any additional info.
//
void SimpleDataBase::SetClientPlatformInfo(WORD32 player_id, struct ClientPlatform *client_platform, struct VersionNumber *client_version)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// If client_platform information is not empty, save it.
		struct ClientPlatform cp;
		zstruct(cp);
		if (memcmp(&cp, client_platform, sizeof(cp))) {
			// non-zero... save it.
			_SDB[index].client_platform = *client_platform;
		}
		// Save sequential client version number (32-bit field only)
		if (client_version->build) {
			_SDB[index].client_version = client_version->build;
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx trying to save client info", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::SavePlayerAutoAllIn(WORD32 player_id, time_t all_in_time)
 date: kriskoin 2019/01/01 Purpose: log a player going all in due to disconnect
***********************************************************************************/
void SimpleDataBase::SavePlayerAllInInfo(WORD32 player_id, time_t all_in_time, int worst_connection_state, WORD32 game_serial_number)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// scroll history up one entry
		memmove(&_SDB[index].all_in_times[1], &_SDB[index].all_in_times[0], 
				sizeof(time_t)*(ALLINS_TO_RECORD_PER_PLAYER-1));
		memmove(&_SDB[index].all_in_connection_state[1], &_SDB[index].all_in_connection_state[0], 
				sizeof(BYTE8)*(ALLINS_TO_RECORD_PER_PLAYER-1));
		memmove(&_SDB[index].all_in_game_numbers[1], &_SDB[index].all_in_game_numbers[0], 
				sizeof(WORD32)*(ALLINS_TO_RECORD_PER_PLAYER-1));
		// set the new one
		_SDB[index].all_in_times[0] = all_in_time;
		_SDB[index].all_in_connection_state[0] = (BYTE8)worst_connection_state;
		_SDB[index].all_in_game_numbers[0] = game_serial_number;
		if (all_in_time==0) {	// resetting their allins?
			_SDB[index].all_in_reset_time = time(NULL);	// save time_t when this was done.
		}
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx trying to add auto all-in info", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

/**********************************************************************************
 Function SimpleDataBase::GetNextTransactionNumber
 Date: 2017/7/7 kriskoin Purpose: find the next sequential transaction ID for a player
***********************************************************************************/
int SimpleDataBase::GetNextTransactionNumber(WORD32 player_id)
{
	int next_tn = -1;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		next_tn = _SDB[index].next_transaction_number;	
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx in GetNextTransactionNumber()", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	return next_tn;
}

//*********************************************************
// https://github.com/kriskoin//
// Return the next transaction number for a client (the exact same
// number as GetNextTransactionNumber() would return) and then increment
// the number (to keep the number returned unique).
//
int SimpleDataBase::GetNextTransactionNumberAndIncrement(WORD32 player_id)
{
	int next_tn = -1;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		next_tn = _SDB[index].next_transaction_number++;	
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx in GetNextTransactionNumberAndIncrement()", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	return next_tn;
}

/**********************************************************************************
 Function SimpleDataBase::LogPlayerTransaction
 Date: 2017/7/7 kriskoin Purpose: log ecash transactions
***********************************************************************************/
void SimpleDataBase::LogPlayerTransaction(WORD32 player_id, struct ClientTransaction *ct)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		// scroll history up one entry
		memmove(&_SDB[index].transaction[1], &_SDB[index].transaction[0],
					sizeof(ClientTransaction)*(TRANS_TO_RECORD_PER_PLAYER-1));
		// set the new one
		// memcpy(&_SDB[index].transaction[0], ct, sizeof(ClientTransaction));
		_SDB[index].transaction[0] = *ct;
	  #if 0	// 2022 kriskoin
		// update the next transaction number
		_SDB[index].next_transaction_number++;
	  #endif
	} else {
		Error(ERR_INTERNAL_ERROR,"didn't find player_id $%08lx trying to log transaction", player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Return the privilege level for a particular user (ACCPRIV_*)
// Returns 0 if not found (ACCPRIV_LOCKED_OUT)
//
BYTE8 SimpleDataBase::GetPrivLevel(WORD32 player_id)
{
	BYTE8 result = 0;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		result = _SDB[index].priv;
	}
	LeaveCriticalSection(&SDBCritSec);
	return result;
}

//*********************************************************
// https://github.com/kriskoin//
// Set the privilege level for a particular user (ACCPRIV_*)
//
void SimpleDataBase::SetPrivLevel(WORD32 player_id, BYTE8 priv_level, char *reason)
{
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found it
		if (priv_level != ACCPRIV_PLAY_MONEY) {	// do we care?
			//!!! THIS SHOULD BE LOGGED SOMEWHERE
			kp(("%s(%d) Changing privilege level from %d to %d for player id $%08lx.  Reason = '%s'\n",
					_FL,_SDB[index].priv,priv_level, player_id, reason));
		}
		_SDB[index].priv = priv_level;
	}
	LeaveCriticalSection(&SDBCritSec);
	NOTUSED(reason);
}

//*********************************************************
// https://github.com/kriskoin//
// Add (or remove) new chips to/from the chip universe.
//
void SimpleDataBase::AddChipsToUniverse(int amount, ChipType chip_type, char *reason)
{
	EnterCriticalSection(&SDBCritSec);
	long total = 0;
	switch (chip_type) {
		case CT_NONE:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_NONE", _FL);
			break;
		case CT_PLAY:
			FakeChipChange -= amount;
			FakeChipUniverse += amount;
			total = FakeChipUniverse;
			break;
		case CT_REAL:
			RealChipChange -= amount;
			RealChipUniverse += amount;
			total = RealChipUniverse;
			break;
		case CT_TOURNAMENT:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with CT_TOURNAMENT (undefined behaviour)", _FL);
			break;
		default:
			Error(ERR_INTERNAL_ERROR,"%s(%d) called with unknown chip_type", _FL);
	}
	LeaveCriticalSection(&SDBCritSec);
	pr(("%s %s(%d) Database: added %d chips to the %s money universe (%s) Total is now %d chips\n",
			TimeStr(), _FL, amount, real_money_flag ? "real" : "play", reason, total));
	NOTUSED(reason);
}

/**********************************************************************************
 Function SimpleDataBase::ShutDown(void)
 date: kriskoin 2019/01/01 Purpose: we're told we're shutting down -- do whatever cleanup is needed
 WARNING: this function touches every record of the database.
***********************************************************************************/
void SimpleDataBase::ShutDown(void)
{
	if (iRecordCount >= SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
		kp(("%s(%d) %s SDB::ShutDown has begun...\n",_FL,TimeStr()));
	}
	// move all player's chips from tables to the bank
	for (int i=0; i < iRecordCount; i++) {
		if (i && !(i%SEQUENTIAL_SEARCH_STATUS_INTERVAL)) {
			kp(("%s(%d) %s SDB::ShutDown: checking record %d of %d\n", _FL, TimeStr(), i, iRecordCount));
		}
		if (_SDB[i].valid_entry) {
			if (_SDB[i].real_in_play) {
				MoveChipsFromPlayingToBank(&_SDB[i], CT_REAL);
			}
			if (_SDB[i].fake_in_play) {
				MoveChipsFromPlayingToBank(&_SDB[i], CT_PLAY);
			}
		}
	}
	if (iRecordCount >=SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
		kp(("%s(%d) %s SDB::ShutDown: Flushing database file...\n",_FL,TimeStr()));
	}
	WriteDataBase(TRUE);	// write it out before shutting down

	if (iRecordCount >=SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
		kp(("%s(%d) %s SDB::ShutDown: Closing database file...\n",_FL,TimeStr()));
	}
	CloseDatabaseFile();	// close memory mapped file.
	_clean_shutdown = TRUE;
	if (iRecordCount >=SEQUENTIAL_SEARCH_STATUS_INTERVAL) {
		kp(("%s(%d) %s SDB::ShutDown complete.\n",_FL,TimeStr()));
	}
}

//*********************************************************
// https://github.com/kriskoin//
// Create a new binary database file
// WARNING: this function touches every record of the database.
//
void SimpleDataBase::CreateNewDatabaseFile(void)
{
	EnterCriticalSection(&SDBCritSec);
	CloseDatabaseFile();	// close old database file if it was open
	remove(_bin_name);		// delete it if it's there.
	// Create a new file suitable for memory mapping.
	int handle = creat(_bin_name,
			S_IREAD | S_IWRITE
		   #if !WIN32
			 | S_IRGRP | S_IWGRP
		   #endif
		);
	if (handle==-1) {
		Error(ERR_FATAL_ERROR, "%s(%d) Failed to create new binary database file ('%s')", _FL, _bin_name);
		exit(10);
	}
	close(handle);
	OpenDatabaseFile();		// now open it as a memory mapped file.
	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Open the database file as a memory mapped file.
// If the file fails to open successfully, DIE() is called.
//
void SimpleDataBase::OpenDatabaseFile(void)
{
	EnterCriticalSection(&SDBCritSec);
	if (_SDB) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) OpenDatabaseFile() called while database already mapped.",_FL);
		LeaveCriticalSection(&SDBCritSec);
		return;
	}

	if (hDatabaseFile) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) OpenDatabaseFile() called while database already open.",_FL);
		LeaveCriticalSection(&SDBCritSec);
		return;
	}

  #if WIN32
	hDatabaseFile = CreateFile(
			_bin_name,				// pointer to name of the file
			GENERIC_READ|GENERIC_WRITE, // access (read-write) mode
			0,						// share mode
			NULL,					// pointer to security attributes
			OPEN_ALWAYS,			// how to create
			FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_RANDOM_ACCESS,	// file attributes
			NULL					// handle to file with attributes to copy
		);
	if (hDatabaseFile==INVALID_HANDLE_VALUE) {
		Error(ERR_ERROR, "%s(%d) Database CreateFile() failed. GetLastError() = %d", _FL, GetLastError());
		DIE("Database failure");		
	}

	// It's open... create a file mapping for it...
	hDatabaseMapping = CreateFileMapping(
			hDatabaseFile,		// handle to file to map
			NULL,				// optional security attributes
			PAGE_READWRITE,		// protection for mapping object
			0,					// high-order 32 bits of object size
			iRecordLength*iRecordCount,	// low-order 32 bits of object size
			NULL				// name of file-mapping object
		);
	if (hDatabaseMapping==NULL) {
		Error(ERR_ERROR, "%s(%d) Database CreateFileMapping() failed. GetLastError() = %d", _FL, GetLastError());
		DIE("Database failure");		
	}

	// Now map it into our VM space...
	_SDB = (SDBRecord *)MapViewOfFile(
			hDatabaseMapping,	// file-mapping object to map into address space
			FILE_MAP_ALL_ACCESS,// access mode
			0,0,				// 64-bits of file offset
			iRecordLength*iRecordCount // number of bytes to map
		);
	if (_SDB==NULL) {
		Error(ERR_ERROR, "%s(%d) Database MapViewOfFile() failed. GetLastError() = %d", _FL, GetLastError());
		DIE("Database failure");		
	}
//	kp(("%s(%d) _SDB=$%08lx, first record user id = '%s'\n", _FL, _SDB, _SDB[0].user_id));
//	exit(0);
  #else	// !WIN32
	hDatabaseFile = open(_bin_name, O_CREAT|O_RDWR, S_IREAD|S_IWRITE);
	if (hDatabaseFile==-1) {
		Error(ERR_ERROR, "%s(%d) Database CreateFile() failed. errno=%d", _FL, errno);
		DIE("Database failure");		
	}

	// Grow the file if necessary.
	long current_length = lseek(hDatabaseFile, 0, SEEK_END);
	if (current_length < iRecordCount * iRecordLength) {
		kp(("%s(%d) Database needs growing: old size = %dK, new size = %dK\n",
					_FL, current_length >> 10, iRecordCount*iRecordLength >> 10));
		// Write enough zeroes to the file to occupy the entire space
		// we'll try to map.
		int bytes_left = iRecordCount * iRecordLength - current_length;
		#define BYTES_PER_WRITE	1000
		char buffer[BYTES_PER_WRITE];
		memset(buffer, 0, BYTES_PER_WRITE);
		while (bytes_left > 0) {
			int bytes_to_write = min(bytes_left, BYTES_PER_WRITE);
			int bytes_written = write(hDatabaseFile, buffer, bytes_to_write);
			if (bytes_written != bytes_to_write) {
				Error(ERR_ERROR, "%s(%d) Fail to allocate all space for binary database file. Disk full?", _FL);
				DIE("Database create failed.");
			}
			bytes_left -= bytes_written;
		}
		kp(("%s(%d) Finished growing database.\n",_FL));
	}

	// It's open... map it into our VM space
	_SDB = (SDBRecord *)mmap(NULL, iRecordLength*iRecordCount,
				PROT_READ|PROT_WRITE, MAP_SHARED, hDatabaseFile, 0);
	if (_SDB==(SDBRecord *)MAP_FAILED) {
		Error(ERR_ERROR, "%s(%d) Database mmap() failed. errno=%d", _FL, errno);
		DIE("Database failure");		
	}
	//kp(("%s(%d) mmap() returned address $%08lx\n", _FL, _SDB));
  #endif

	// The file is now open and ready for access through _SDB.
	LeaveCriticalSection(&SDBCritSec);
	
}

//*********************************************************
// https://github.com/kriskoin//
// Close the database file if it is open.
//
void SimpleDataBase::CloseDatabaseFile(void)
{
	EnterCriticalSection(&SDBCritSec);
  #if WIN32
	if (_SDB) {
		FlushDatabaseFile();
		BOOL success = UnmapViewOfFile(_SDB);
 		if (!success) {
			Error(ERR_ERROR, "%s(%d) UnmapViewOfFile() failed.  GetLastError() = %d", _FL, GetLastError());
 		}
		_SDB = NULL;
	}
	if (hDatabaseMapping) {
		BOOL success = CloseHandle(hDatabaseMapping);
 		if (!success) {
			Error(ERR_ERROR, "%s(%d) database CloseHandle() failed.  GetLastError() = %d", _FL, GetLastError());
 		}
		hDatabaseMapping = NULL;
	}	
	if (hDatabaseFile) {
		BOOL success = CloseHandle(hDatabaseFile);
 		if (!success) {
			Error(ERR_ERROR, "%s(%d) database CloseHandle() failed.  GetLastError() = %d", _FL, GetLastError());
 		}
		hDatabaseFile = NULL;
	}	
  #else	// !WIN32
	if (_SDB) {
		FlushDatabaseFile();
		int result = munmap((caddr_t)_SDB, iRecordLength*iRecordCount);
 		if (result) {
			Error(ERR_ERROR, "%s(%d) database munmap() failed. errno=%d", _FL, errno);
 		}
		_SDB = NULL;
	}
	if (hDatabaseFile) {
		close(hDatabaseFile);
		hDatabaseFile = 0;
	}	
  #endif
	if (index_player_id) {

		free(index_player_id);
		index_player_id = NULL;
	}  
	index_player_id_count = 0;

	if (index_user_id) {
		free(index_user_id);
		index_user_id = NULL;
	}  
	index_user_id_count = 0;

	LeaveCriticalSection(&SDBCritSec);
}

//*********************************************************
// https://github.com/kriskoin//
// Flush the database (or part of it) to disk.  It may not
// get written immediately, but it will get queued immediately.

//
void SimpleDataBase::FlushDatabaseFile(void)
{
	FlushDatabaseFile(0,iRecordLength*iRecordCount);	// flush the entire file.
}
void SimpleDataBase::FlushDatabaseFile(WORD32 start_offset, WORD32 length)
{
	if (!_SDB) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) FlushDatabaseFile() called with no current file mapping",_FL);
	  #if INCL_STACK_CRAWL
		DisplayStackCrawl();
	  #endif
		return;
	}
  #if WIN32
	BOOL success = FlushViewOfFile((void *)((byte *)_SDB+start_offset), length);
	if (!success) {

		Error(ERR_ERROR, "%s(%d) FlushViewOfFile($%08lx+%u, %u) failed. GetLastError() = %d",_FL,_SDB,start_offset, length, GetLastError());
	}
  #else

	int result = msync((caddr_t)((byte *)_SDB+start_offset), length, MS_ASYNC | MS_INVALIDATE);
	if (result) {
		Error(ERR_ERROR, "%s(%d) database msync() failed. errno=%d", _FL, errno);
	}
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Compare and swap functions for sorting an SDBIndexEntry array.
//
static int CompareSDBIndexEntry(int n1, int n2, void *base)
{
	WORD32 h1 = ((SDBIndexEntry *)base)[n1].hash;
	WORD32 h2 = ((SDBIndexEntry *)base)[n2].hash;
	if (h1==h2) {
		return 0;
	} else if (h1 > h2) {
		return 1;
	}
	return -1;
}
static void SwapSDBIndexEntry(int n1, int n2, void *base)
{
	SDBIndexEntry x				= ((SDBIndexEntry *)base)[n1];
	((SDBIndexEntry *)base)[n1] = ((SDBIndexEntry *)base)[n2];
	((SDBIndexEntry *)base)[n2] = x;
}

//*********************************************************
// https://github.com/kriskoin//
// Sort the index arrays so they can be searched quickly.
//
void SimpleDataBase::SortIndexArrays(void)
{
	EnterCriticalSection(&SDBCritSec);
	QSort(index_user_id_count, CompareSDBIndexEntry, 
					SwapSDBIndexEntry, (void *)index_user_id);
  #if TEST_QSORT_DEPTH
	static int max_depth = 0;
	if (iQSortMaxDepth > max_depth) {
		kp(("%s(%d) Max depth reached during QSort of userid's = %d (for %d records)\n", _FL, iQSortMaxDepth, index_user_id_count));
		max_depth = iQSortMaxDepth;
	}
  #endif
	QSort(index_player_id_count, CompareSDBIndexEntry, 
					SwapSDBIndexEntry, (void *)index_player_id);
  #if TEST_QSORT_DEPTH
	if (iQSortMaxDepth > max_depth) {
		kp(("%s(%d) Max depth reached during QSort of player_id's = %d (for %d records)\n", _FL, iQSortMaxDepth, index_player_id_count));
		max_depth = iQSortMaxDepth;
	}
  #endif
	LeaveCriticalSection(&SDBCritSec);
  #if 0	// 2022 kriskoin
	kp(("%s(%d) Done sorting index arrays.  Here's what we found:\n",_FL));
	for (int i=0 ; i<index_player_id_count ; i++) {
		kp(("%s(%d) index_player_id[%3d].hash = $%08lx, index = %d\n",_FL,i,index_player_id[i].hash,index_player_id[i].index));
	}

	for (i=0 ; i<index_user_id_count ; i++) {
		kp(("%s(%d) index_user_id[%3d].hash = $%08lx, index = %d\n",_FL,i,index_user_id[i].hash,index_user_id[i].index));
	}
  #endif
	int records_left = iRecordCount - index_user_id_count;
	int db_percentage = (index_user_id_count*100) / iRecordCount;
	if (db_percentage >= 96 || records_left < 1500) {
		Error(ERR_NOTE, "Warning: database is %d%% full (only %d of %d records free)",
					db_percentage, records_left, iRecordCount);
	}
	indexes_need_sorting = FALSE;
}

//*********************************************************
// https://github.com/kriskoin//
// Perform a binary search on an index array to find a particular hash.
// Returns -1 if not found.
// If found, it returns the FIRST entry that matches.  The caller can
// then step forward from that location to handle any hash collisions.
//
int SimpleDataBase::FindIndexEntry(SDBIndexEntry *index, int index_count, WORD32 hash_to_search_for)
{
  #if 0	// 2022 kriskoin
	// Temporarily just do a linear search.

	for (int i=0 ; i<index_count ; i++) {
		if (index[i].hash == hash_to_search_for) {
			return i;
		}
	}
  #endif
	int low = 0;
	int high = index_count - 1;
	while (low <= high) {
		int i = (high + low) / 2;
		//kp(("%s(%d) Searching for hash $%08lx.  low = %3d, high = %3d  [%d].hash=$%08lx\n", _FL, hash_to_search_for, low, high, i, index[i].hash));
		if (index[i].hash==hash_to_search_for) {
			// Found a match.  Step back to find the first match.
			//kp(("%s(%d) found match for $%08lx at index %d.\n",_FL, hash_to_search_for, i));
			while (i > 0 && index[i-1].hash==hash_to_search_for) {
				i--;
			}
			//kp(("%s(%d) stepped back to %d for $%08lx\n",_FL, i, hash_to_search_for));
			return i;	// found it.
		} else if (hash_to_search_for > index[i].hash) { // Move toward end
			low = i+1;
		} else {	// Move toward beginning

			high = i-1;
		}

	}
	//kp(("%s(%d) hash not found.\n",_FL));
	return -1;	// not found.
}

//*********************************************************
// https://github.com/kriskoin//
// Calculate a hash for a string to be used as an index.
// This function might produce collisions.
// A lowercase version of the string is hashed to make
// the searching case insensitive.
//
WORD32 SimpleDataBase::CalcStringHash(char *string)
{
  #if 1	// 2022 kriskoin
	char str[200], *s;
	strnncpy(str, string, 200);
	s = str;
	while (*s) {
		*s++ = (char)tolower(*s);
	}
	return CalcCRC32(str, strlen(str));
  #else
	kp1(("%s(%d) Warning: Using high-collision CalcStringHash() function\n",_FL));
  	// For testing hash collisions:
	return strlen(string)&15;
  #endif
}

//*********************************************************
// https://github.com/kriskoin//
// Find an empty record (player_id==0) and return a unique
// player ID to allow access to that record.
// Returns new player_id or 0 if no empty records are available.
// Allen Ko
//
WORD32 SimpleDataBase::CreateNewRecord(char *user_id_string)
{
	EnterCriticalSection(&SDBCritSec);

	// Make sure the user_id is not used already.
	char user_id_copy[MAX_PLAYER_USERID_LEN];
	strnncpy(user_id_copy, user_id_string, MAX_PLAYER_USERID_LEN);
	int i = SearchDataBaseByUserID(user_id_copy);
	if (i>=0) {
		Error(ERR_INTERNAL_ERROR, "%s(%d) Attempt to create a new record with a duplicate user_id ('%s')", _FL, user_id_string);
		LeaveCriticalSection(&SDBCritSec);

		return 0;
	}


	// loop until we find an empty one or run out of records.
	while (iEmptyRecordSearchStart < iRecordCount) {
		if (!_SDB[iEmptyRecordSearchStart].player_id) {
			// This one has an empty player_id... it's available.
			int record = iEmptyRecordSearchStart;

			// Allocate a new player_id
			WORD32 new_player_id = ++dwHighestPlayerID;	// allocate new player_id
			if (ANONYMOUS_PLAYER(new_player_id)) {
				Error(ERR_ERROR, "%s(%d) Ran out of unique player_id's creating a new account! (new=$%08lx)",_FL,new_player_id);
				LeaveCriticalSection(&SDBCritSec);
				return 0;	// no empty record found
			}

			// Fill in the new record.
			zstruct(_SDB[record]);	// zero it out.
			_SDB[record].player_id = new_player_id;	// allocate it.
			strnncpy(_SDB[record].user_id, user_id_copy, MAX_PLAYER_USERID_LEN);
			_SDB[record].account_creation_time = time(NULL);	// keep track of when the account was created.

			// Add it to the indexes and re-sort them
			index_player_id[index_player_id_count].hash = new_player_id;
			index_player_id[index_player_id_count].index = record;
			index_player_id_count++;
			indexes_need_sorting = TRUE;

			index_user_id[index_user_id_count].hash = CalcStringHash(user_id_copy);
			index_user_id[index_user_id_count].index = record;
			index_user_id_count++;
			indexes_need_sorting = TRUE;
			SortIndexArrays();		// re-sort them
			LeaveCriticalSection(&SDBCritSec);
			return new_player_id;	// all done.  return player_id to caller.
		}
		iEmptyRecordSearchStart++;	// try the next one.
	}
	LeaveCriticalSection(&SDBCritSec);
	return 0;	// no empty record found.
}

//*********************************************************
// https://github.com/kriskoin//
// Write most of the fields from an sdb record into the database
// Uses the player_id field to determine which database record to
// overwrite.
//
ErrorType SimpleDataBase::WriteRecord(SDBRecord *ir)
{
	// Make sure the user_id is not used already.
	EnterCriticalSection(&SDBCritSec);
	int i = SearchDataBaseByPlayerID(ir->player_id);
	if (i<0) {
		LeaveCriticalSection(&SDBCritSec);
		Error(ERR_ERROR, "%s(%d) Database::WriteRecord failed.  player_id $%08lx doesn't exist in database.",_FL,ir->player_id);
		return ERR_ERROR;
	}
	SDBRecord *r = &_SDB[i];
	// Fill in the fields we're allowed to copy...
	r->gender = ir->gender;
	r->valid_entry = ir->valid_entry;
	r->priv = ir->priv;
	r->dont_use_email1 = ir->dont_use_email1;
	r->dont_use_email2 = ir->dont_use_email2;
	r->flags = ir->flags;
	strnncpy(r->password, ir->password, MAX_PLAYER_PASSWORD_LEN);
  #if 0	// 2022 kriskoin
	strnncpy(r->password_rm, ir->password_rm, MAX_PLAYER_PASSWORD_LEN);
  #endif
	strnncpy(r->city, ir->city, MAX_COMMON_STRING_LEN);
	strnncpy(r->full_name,ir->full_name, MAX_PLAYER_FULLNAME_LEN);
	
	// ricardoGANG - 26/8/2003

	strncpy(r->last_name,ir->last_name,MAX_PLAYER_LASTNAME_LEN);
	strcpy(r->idAffiliate,ir->idAffiliate);//,MAX_COMMON_STRING_LEN);

	printf("\nln- %s idA - %s\n",r->last_name,r->idAffiliate);	
	// END - ricardoGANG - 26/8/2003
	
	strnncpy(r->email_address, ir->email_address, MAX_EMAIL_ADDRESS_LEN);

  strnncpy(r->mailing_address1, ir->mailing_address1, MAX_PLAYER_ADDRESS_LEN);
	strnncpy(r->mailing_address2, ir->mailing_address2, MAX_PLAYER_ADDRESS_LEN);
	strnncpy(r->mailing_address_state, ir->mailing_address_state, MAX_COMMON_STRING_LEN);
	strnncpy(r->mailing_address_country, ir->mailing_address_country, MAX_COMMON_STRING_LEN);
	strnncpy(r->mailing_address_postal_code, ir->mailing_address_postal_code, MAX_COMMON_STRING_LEN);
  #if 1	// 2022 kriskoin
	memcpy(r->admin_notes, ir->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN);
  #else
	strnncpy(r->admin_notes, ir->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN);
  #endif
	memcpy(r->phone_number, ir->phone_number, PHONE_NUM_LEN);	// 20:::	r->cc_override_limit1 = ir->cc_override_limit1;
	r->cc_override_limit2 = ir->cc_override_limit2;
	r->cc_override_limit3 = ir->cc_override_limit3;
	// kriskoin 
	r->fee_credit_points = ir->fee_credit_points;
	// end kriskoin 
	// If the user_id changed, deal with that here...
	if (strcmp(r->user_id, ir->user_id)) {
		// step 1: the new one must not be in the database already.
		int j = SearchDataBaseByUserID(ir->user_id);
		if (j >= 0 && j != i) {
			// error... it's already there (and it's not us)

			LeaveCriticalSection(&SDBCritSec);
			Error(ERR_ERROR, "%s(%d) Database::WriteRecord failed to change user_id ('%s' is already used)", _FL, ir->user_id); 
			return ERR_ERROR;
		}
		// step 2: fix the indexes.
		// find our existing user_id index entry, then store the new hash.
		for (j=0 ; j<index_user_id_count ; j++) {
			if (index_user_id[j].index == (WORD32)i) {
				// this one belongs to us.
				break;
			}
		}
		if (j >= index_user_id_count) {	// not found?
			LeaveCriticalSection(&SDBCritSec);
			Error(ERR_ERROR, "%s(%d) Database::WriteRecord failed to change user_id (old record not found)", _FL); 
			return ERR_ERROR;
		}

		// step 3: save it
		strnncpy(r->user_id, ir->user_id, MAX_PLAYER_USERID_LEN);
		index_user_id[j].hash = CalcStringHash(r->user_id);
		SortIndexArrays();		// re-sort them
	}
	LeaveCriticalSection(&SDBCritSec);

   printf("\n _SDB[i] idAff - %s | ln - %s",_SDB[i].idAffiliate,_SDB[i].last_name) ;
	return ERR_NONE;
}

//*********************************************************
// https://github.com/kriskoin//
// Retrieve the number of user records which currently exist.
//
int SimpleDataBase::GetUserRecordCount(void)
{
	return index_user_id_count;

}

/**********************************************************************************
 Function IncrementAddressChangeCount(WORD32 player_id)
 date: 24/01/01 kriskoin Purpose: increment and return the current count for this player
 Note: this is stored in a byte
***********************************************************************************/
int SimpleDataBase::IncrementAddressChangeCount(WORD32 player_id)
{
	int return_count = 0;
	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id);
	if (index >= 0) { // found him
		BYTE8 new_count = (BYTE8)(_SDB[index].address_change_count+1);
		_SDB[index].address_change_count = (BYTE8)(min(new_count, 255));
		return_count = _SDB[index].address_change_count;
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to increment address change count", _FL, player_id);

	}
	LeaveCriticalSection(&SDBCritSec);
	return return_count;
}

//*********************************************************
// https://github.com/kriskoin//
// Add a note to the comments section of an account record.
// Appends to end and clips if there's no more room.
//
#ifndef DBFILTER	// DO NOT include when building dbfilter.
void SimpleDataBase::AddAccountNote(WORD32 player_id, char *fmt_string, ...)
{
	char str[500];
	zstruct(str);
    va_list arg_ptr;
	va_start(arg_ptr, fmt_string);
	vsprintf(str, fmt_string, arg_ptr);
	va_end(arg_ptr);

	EnterCriticalSection(&SDBCritSec);
	int index = SearchDataBaseByPlayerID(player_id, NULL);
	if (index >= 0) { // found it
		SDBRecord *r = &_SDB[index];
		// First, make sure it ends in a newline.
		char uncomp_notes[MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED];
		zstruct(uncomp_notes);
		UncompressString(r->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN, uncomp_notes, MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED);
		MakeStringEndWithNewline(uncomp_notes, MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED);
		strnncat(uncomp_notes, str, MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED);
		CompressString(r->admin_notes, MAX_PLAYER_ADMIN_NOTES_LEN, uncomp_notes, MAX_PLAYER_ADMIN_NOTES_LEN_UNCOMPRESSED);
	} else {
		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to add account note",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
}
#endif

/**********************************************************************************
 Function ::SearchDataBaseByEmail(char *email_address, SDBRecord &result)
 Date: Allen Ko 2001-10-29
 Purpose: search the database for this Email Address 
 Return:  index value (offset) of this user, -1 if nothing found
***********************************************************************************/

int SimpleDataBase::SearchDataBaseByEmail(char *email_address)
{
	return SearchDataBaseByEmail(email_address, NULL);
}

int SimpleDataBase::SearchDataBaseByEmail(char* email_address, SDBRecord *result)
{  
	 int j=1;  
	 if (strlen(email_address)>0) {
		for (int i=0; i < index_user_id_count; i++) {
			//kp(("%s(%d) record %d: new_hash = $%08lx, search_hash = $%08lx\n",_FL, record, new_hash, search_hash));
			j = stricmp(_SDB[i].email_address, email_address );
			if (j == 0 ) {	// found it
				/// if(result) {
				//     memcpy(result, &_SDB[record], sizeof(SDBRecord));
				//  }
				//  kp(("%s(%d) user_id '%s': player_id $%08lx\n", _FL, user_id, _SDB[record].player_id));
			return 1;
			}
		
		}
	 }
    //kp(("%s(%d) No match for '%s'\n", _FL, user_id));

	return -1;	// not found
}

/**********************************************************************************
***********************************************************************************/
int SimpleDataBase::GetCreditPoints(WORD32 player_id)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int credit_guess , i;
        credit_guess=0;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	if (index >= 0) { // found it
			
         if ( _SDB[index].transaction[i].transaction_type == CTT_PURCHASE) {
		credit_guess +=  _SDB[index].transaction[i].transaction_amount;
	 }
		
	if ( _SDB[index].transaction[i].transaction_type == CTT_CREDIT) {
		credit_guess -=  _SDB[index].transaction[i].transaction_amount;
	} 
	/*
	for (i=0; i < TRANS_TO_RECORD_PER_PLAYER; i++) {
			if (_SDB[index].transaction[i].transaction_type == CTT_PURCHASE) {
				// 20010202: make sure it's not disabled before we add it in (credit amt > purchase means disabled)
				if (_SDB[index].transaction[i].credit_left <= _SDB[index].transaction[i].transaction_amount) {
					credit_guess += _SDB[index].transaction[i].credit_left;
				}
			}
			}		
	*/

	 if (credit_guess <=0) 
            credit_guess = 0;
	
	}
               
	 else {

		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
//	WriteDataBase(FALSE);	// write out to disk, if needed
	kp(("%s(%d) Credit Guess: '%d'\n", _FL, credit_guess));
	return credit_guess;
}


/***********************************************************************************/
void SimpleDataBase::SetCreditPoints(WORD32 player_id, INT32 left_to_credit_back)
{
	EnterCriticalSection(&SDBCritSec);
	SDBRecord sdbr;
	int index = SearchDataBaseByPlayerID(player_id, &sdbr);
	
	if (index >= 0) { // found it
			
        for ( int i=TRANS_TO_RECORD_PER_PLAYER-1; i >=0; i--) {
		//kp(("%s(%d) transaction credit left: %d\n", _FL, player_rec.transaction[i].credit_left ));  
		if ( _SDB[index].transaction[i].credit_left > left_to_credit_back) {
					  _SDB[index].transaction[i].credit_left 
						= _SDB[index].transaction[i].credit_left - left_to_credit_back;
						  left_to_credit_back = 0;	
					 }
		else {
					left_to_credit_back = left_to_credit_back - _SDB[index].transaction[i].credit_left;	
			  		_SDB[index].transaction[i].credit_left = 0;

			}
		
		if (left_to_credit_back == 0)
			break;
     	}			

	}
               
    else {

		Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts.",
			_FL, player_id);
	}
	LeaveCriticalSection(&SDBCritSec);
	//kp(("%s(%d) Credit Guess: '%d'\n", _FL, credit_guess));
	return;
}

void SimpleDataBase::RefundPaypalCredit(WORD32 player_id, INT32 left_to_credit_back)
{
        EnterCriticalSection(&SDBCritSec);
        SDBRecord sdbr;
        int index = SearchDataBaseByPlayerID(player_id, &sdbr);

        if (index >= 0) { // found it

        for ( int i=TRANS_TO_RECORD_PER_PLAYER-1; i >=0; i--) {
                //kp(("%s(%d) transaction credit left: %d\n", _FL, player_rec.transaction[i].credit_left ));
                if ( _SDB[index].transaction[i].transaction_type == CTT_PURCHASE ) {
		if ( _SDB[index].transaction[i].transaction_amount > left_to_credit_back) {
                                          _SDB[index].transaction[i].credit_left
                                                = _SDB[index].transaction[i].credit_left + left_to_credit_back;
                                                  left_to_credit_back = 0;
                                         }
                else {
                                        left_to_credit_back = left_to_credit_back - _SDB[index].transaction[i].transaction_amount;
                                        _SDB[index].transaction[i].credit_left += _SDB[index].transaction[i].transaction_amount ;

                        }

                if (left_to_credit_back == 0)
                        break;
               }      

         }

	}

    else {

                Error(ERR_INTERNAL_ERROR,"%s(%d) didn't find player_id (%08lx) trying to get credit fee pts.",
                        _FL, player_id);
        }
        LeaveCriticalSection(&SDBCritSec);
        //kp(("%s(%d) Credit Guess: '%d'\n", _FL, credit_guess));
        return;
}
