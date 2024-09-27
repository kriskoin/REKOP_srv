#define DEBUG 0
#if DEBUG
#define Dprintf printf
#else
#define Dprintf
#endif
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "../pplib/gamedata.h"

#define MAX_LINE_LENGTH 520
#define MAX_PLAYERS 1000
#define MAX_GAMES 1000
//#define WORK_DIR "/var/local/poker/pokersrv"   /poker/server/src
#define WORK_DIR "/poker/server/src/Data"

FILE *fp;
FILE *fp_msg;

extern "C" {

void SetFileLock(int fd, short type)
{
	struct flock arg;
	extern int errno;

	arg.l_type = type;      /* lock type setting */
	arg.l_whence = 0;       /* from start of file */
	arg.l_start = 0;        /* byte offset to begining */
	arg.l_len = 0;          /* until end of file */
	if(fcntl(fd,F_SETLK,&arg) == -1 && errno == EACCES) {  /* busy */
	  printf("waiting for lock ...\n");
	  fcntl(fd,F_SETLKW,&arg);
	}

}

char *get_userid(WORD32 player_id)
{

	SDBRecord dbr;
	int dbm_fd;
	char filename[128];

	sprintf(filename,"%s/DB/DesertPoker.bin",WORK_DIR);
	if((dbm_fd = open(filename,O_RDWR)) == -1){
	    printf("can't open %s\n",filename);
	    exit(1);
	}
	else SetFileLock(dbm_fd,F_WRLCK);

	off_t dbsize = lseek(dbm_fd, 0, SEEK_END);
	int dbrno = dbsize/sizeof(dbr);
	
	lseek(dbm_fd, 0, SEEK_SET);
	for(int i=0; i<dbrno; i++) {
		read(dbm_fd, &dbr, sizeof(dbr));
		if(dbr.player_id==player_id) {
			break;
		}
	}
	close(dbm_fd);
	return dbr.user_id;

}
 
void line_escape_white(char *s, char *t)
{
	int i;
	int j=0;
	for (i=0;i<strlen(s);i++){
		switch(s[i]){
		   case ' ':
			t[j]=5;
			j++;
			break;
		   case '\t':
			if(t[j-1]=='\t'){
			   t[j]=5;
			   j++;
			}
			t[j]='\t';
			j++;
			break;
		   default:
			t[j]=s[i];
			j++;
			break;
		}
	}
	t[j]='\0';
			
}

void line_restore_white(char *s)
{
	int i;
	for (i=0;i<strlen(s);i++){
		if (s[i]==5)
			s[i]=' ';
	}
}

void get_date_end_str(char *date_end, char *date_start, int number_of_days)
{
	int yy;
	int mm;
	int dd;
	time_t mtime;
	tm dt;

	sscanf(date_start, "%4d%2d%2d", &yy, &mm, &dd);
        dt.tm_year=yy-1900;
        dt.tm_mon=mm;
        dt.tm_mday=dd;
        dt.tm_hour=0;
        dt.tm_min=0;
        dt.tm_sec=0;
        mtime=mktime(&dt)+60*60*24*number_of_days;
        memcpy(&dt, localtime(&mtime), sizeof(tm));

	sprintf(date_end, "%4d%02d%02d", dt.tm_year+1900, dt.tm_mon, dt.tm_mday);	
}

int game_no_cmp(int game_serial_number, int *game_no_ary, int count)
{
	int i;
	for (i=0;i<count;i++){
		if(game_serial_number==game_no_ary[i])
			return i;
	}
	return -1;
}

int main(int argc, char **argv)
{
	int i=0;
	char hal_filename[30];
	char filename[128] ;
	char cmd_line[256];
	char err_msg[256];
	int err_no=0;
	char arg_player_id[10];
	char arg_user_id[32];
	char arg_date_start[10];
	char arg_date_end[10];
	char tmp_date_str[10];
	int arg_number_of_days;
	char arg_email_str[64];
	char line[MAX_LINE_LENGTH];
	char line2[MAX_LINE_LENGTH];

	int full_datetime;
	char buf[500];
	char record[500];
	int num_line=0;
	static int log_type;
        char log_date[10];
        char log_time[10];
	char time_str[20];
        time_t last_log_time=0;
        time_t diff_sec;
        time_t tt;
        int yy;
        int mm;
        int dd;
        int hr;
        int mi;
        int se;
        tm dt;

	int login_cnt=0;
	int logout_cnt=0;
	int total_games=0;
	int real_games=0;
	int play_games=0;
	int total_win=0;
	int total_lose=0;
	int win_real_games=0;
	int lose_real_games=0;
	int win_real_game_no[MAX_GAMES];
	int win_real_game_chips[MAX_GAMES];
	int lose_real_game_no[MAX_GAMES];
	int lose_real_game_chips[MAX_GAMES];
	char user_id[32];
	char player_id[10];
	WORD32 hex_player_id;
	int chips;
	int chip_type;
	int game_serial_number;
	char ip_addr[20];
	int real_in_bank;
	int real_in_play;
	char table_name[32];
	int seating_position;
	int tmpint;


	int total_losers=0;
	int total_winners=0;
	int new_flag=1;

	struct {
		char player_id[12];
		int chips;
	} WinnerRec[MAX_PLAYERS], LoserRec[MAX_PLAYERS];

	int total1, total2;
	total1=0;
	total2=0;
	strcpy(cmd_line, argv[0]);
	for (i=1;i<argc;i++){
		strcat(cmd_line, " ");
		strcat(cmd_line, argv[i]);
	}
	if (argc<7) {
		sprintf(err_msg, "un-recognized format: %s\n", cmd_line);
		err_no=1;
		goto err;
	} else {
		strcpy(arg_player_id, argv[1]);
		strcpy(arg_date_start, argv[2]);
		sscanf(argv[3], "%d", &arg_number_of_days);
		strcpy(arg_email_str, argv[4]);
		strcat(arg_email_str, " ");
		strcat(arg_email_str, argv[5]);
		strcpy(arg_user_id, argv[6]);

	}

	//Since this program is quite time consuming, we have to make sure 
	//at any time only one proccess is doing the real job
	//and others are sleeping

	while(open("tmp_msg",O_CREAT | O_EXCL)<0){
		sleep(1);
	}	
	fp_msg = fopen("tmp_msg","a");

	//Prepare the working file
	//printf("Start generate the file...\n");
	char tmp_file[128];
	sprintf(tmp_file, "%s.hal", arg_date_start);
	strcpy(tmp_date_str, arg_date_start);
	strcpy(arg_date_end, arg_date_start);
	for(i=0;i<(arg_number_of_days-1);i++){
		get_date_end_str(arg_date_end, arg_date_start, 1);
		sprintf(buf, " %s.hal", arg_date_end);
		strcat(tmp_file, buf);
		strcpy(arg_date_start, arg_date_end);
	}
	strcpy(arg_date_start, tmp_date_str);
	sprintf(cmd_line,\
		"cd %s/History; cat %s > working.hal",\
		WORK_DIR, tmp_file);
	printf("\n\n  %s\n", cmd_line);
	system(cmd_line);


	//Start to parse the hal file
	sprintf(filename, "%s/History/working.hal",WORK_DIR);
	if ((fp=fopen(filename, "r"))==NULL){
		fprintf(stderr, "The hal file %s can not be opened. Error: %d.\n", filename,errno);
		exit(3);
	}

	while(fgets(line, MAX_LINE_LENGTH, fp)){
		sscanf(line, "%s\t", log_date);
                if(strlen(log_date)<8)
                        full_datetime=0;
                else
                        full_datetime=1;

                if(!full_datetime) {     //in case of not a full date-time format
                        sscanf(line, "%d\t\t%d\t", &diff_sec, &log_type);
                } else {
                        sscanf(line, "%s\t%s\t%d\t", log_date, log_time, &log_type);
                }

		//Start to proccess

		switch(log_type) {

		case 27: //LogPPGameResult
			
			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d", \
                                buf, buf, buf, player_id, \
				&game_serial_number, &chips, &chip_type);
			if (strcmp(player_id,arg_player_id)==0) {
				total_games++;
				if (chip_type>0){
			 	    real_games++;
				    if (chips > 0) {
					win_real_game_no[win_real_games]=game_serial_number;
					win_real_game_chips[win_real_games]=chips;
					win_real_games++;
					total_win+=chips;

					
				    } else {
					lose_real_game_no[lose_real_games]=game_serial_number;
					lose_real_game_chips[lose_real_games]=-chips;
					lose_real_games++;
					total_lose+=(-chips);
				    }
				} else {
				    play_games++;
				}
			}
				
			break;
		default:
			break;
		}
			
	}	
	//Start to re-parse the hal file...
	rewind(fp);


	fprintf(fp_msg,"Full play report for: %s (%s)\n", arg_user_id, arg_player_id);
	fprintf(fp_msg,"Date from %s to %s\n", arg_date_start, arg_date_end);
	fprintf(fp_msg, "\n------------------------------------------------\n");
	fprintf(fp_msg, "****** Real Money Play Information in Detail ******\n");
	fprintf(fp_msg, "------------------------------------------------\n");

	//printf("Start to re-parse the tmp hal file ......\n");
        while(fgets(line, MAX_LINE_LENGTH, fp)){
                sscanf(line, "%s\t", log_date);
                if(strlen(log_date)<8)
                        full_datetime=0;
                else
                        full_datetime=1;

                if(!full_datetime) {     //in case of not a full date-time format
                        sscanf(line, "%d\t\t%d\t", &diff_sec, &log_type);
                        tt=last_log_time+diff_sec;
                        memcpy(&dt, localtime(&tt), sizeof(tm));
                } else {
                        sscanf(line, "%s\t%s\t%d\t", log_date, log_time, &log_type);
                        sscanf(log_date,"%4d%2d%2d",&yy, &mm, &dd);
                        sscanf(log_time,"%2d:%2d:%2d",&hr, &mi, &se);
                        dt.tm_year=yy-1900;
                        dt.tm_mon=mm;
                        dt.tm_mday=dd;
                        dt.tm_hour=hr;
                        dt.tm_min=mi;
                        dt.tm_sec=se;
                        last_log_time=mktime(&dt);
                }

                //Generate the time_str
                //Format of time_str is like: "yyyy-mm-dd HH:MM:SS"

                sprintf(time_str,"%d-%02d-%02d %02d:%02d:%02d",
                                yy,mm,dd,
                                dt.tm_hour,
                                dt.tm_min,
                                dt.tm_sec);


                //Start to proccess


                switch(log_type) {

		case 2:  //LogLogin(login)
			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\%d",
                                buf, buf, buf, player_id, user_id, ip_addr, &real_in_bank,
                                &real_in_play, &tmpint, &tmpint);

			if (strcmp(player_id, arg_player_id)==0){
				fprintf(fp_msg, "Login @ %s (%s) - $%.2f total ($%.2f bank, $%.2f in play)\n",\
					time_str, ip_addr, 0.01*(real_in_bank + real_in_play), \
					0.01*real_in_bank, 0.01*real_in_play);
				login_cnt++;
			}
			break;
		case 3:  //LogLogin(logout)
			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\%d",
                                buf, buf, buf, player_id, user_id, ip_addr, &real_in_bank,
                                &real_in_play, &tmpint, &tmpint);
			if (strcmp(player_id, arg_player_id)==0){
				fprintf(fp_msg, "Logout @ %s (%s) - $%.2f total ($%.2f bank, $%.2f in play)\n",\
					time_str, ip_addr, 0.01*(real_in_bank + real_in_play), \
					0.01*real_in_bank, 0.01*real_in_play);
				logout_cnt++;
			}
			break;
		case 4:  //LogGameStart
                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%s\t%d\t%d\t%d\%d\t%d\t%d", \
                                buf, buf, buf, &game_serial_number, &tmpint, \
                                table_name, &tmpint, &chip_type, &tmpint, &tmpint, \
				&tmpint, &tmpint);
                        line_restore_white(table_name);
			if (chip_type>0) {
                           if ((i=game_no_cmp(game_serial_number,lose_real_game_no,lose_real_games))>=0) {
				fprintf(fp_msg, "#%d: %s (%s) -",game_serial_number, time_str, table_name);
			   } else {
                              if ((i=game_no_cmp(game_serial_number,win_real_game_no,win_real_games))>=0) {
				fprintf(fp_msg, "#%d: %s (%s) -",game_serial_number, time_str, table_name);
			      }
			   }
			}

			break;
		case 5: //LogGamePlayer
			char * sp;

                        line_escape_white(line, line2);

                        sscanf(line2, "%s\t%s\t%s\t%d\t%s", buf, buf, buf, \
                                &game_serial_number, buf);
                        if ((i=game_no_cmp(game_serial_number,lose_real_game_no,lose_real_games))>=0) {
				sp = strstr(line2, arg_player_id);
				sscanf(sp-3, "%d\t%s\t%s\t%d", \
					&seating_position, player_id, user_id, &chips);
				fprintf(fp_msg, " started with $%.2f (seat# %d)\n", \
					0.01*chips, seating_position);
			        fprintf(fp_msg, "#%d: Lost $%.2f\n", \
					game_serial_number, 0.01*lose_real_game_chips[i]);
			} else {
                            if ((i=game_no_cmp(game_serial_number,win_real_game_no,win_real_games))>=0) {
				sp = strstr(line2, arg_player_id);
				sscanf(sp-3, "%d\t%s\t%s\t%d", \
					&seating_position, player_id, user_id, &chips);
				fprintf(fp_msg, " started with $%.2f (seat# %d)\n", \
					0.01*chips, seating_position);
			        fprintf(fp_msg, "#%d: Won $%.2f\n", \
					game_serial_number, 0.01*win_real_game_chips[i]);
			    }
			}
			break;
                case 27: //LogPPGameResult

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d", \
                                buf, buf, buf, player_id, \
                                &game_serial_number, &chips, &chip_type);

			if(strcmp(player_id, arg_player_id)==0) break;

			if(chip_type>0 && chips>0) {
                           if ((i=game_no_cmp(game_serial_number,lose_real_game_no,lose_real_games))>=0) {
				sscanf(player_id,"%x",&hex_player_id);
				strcpy(user_id, get_userid(hex_player_id));
				fprintf(fp_msg, "#%d: %s won $%.2f\n", \
					game_serial_number, user_id, 0.01*chips);
				chips = lose_real_game_chips[i];
				total1+=chips;
				for(i=0;i<total_winners;i++){
				    if(strcmp(player_id, WinnerRec[i].player_id)==0){
					WinnerRec[i].chips+=chips;
					new_flag=0;
					break;
				    }
				}
				if(new_flag) {
					WinnerRec[total_winners].chips=chips;
					strcpy(WinnerRec[total_winners].player_id, player_id);
					total_winners++;
				} else
					new_flag=1;
			    } else break;
			} 
			if(chips<0 && chip_type>0) {
                           if (game_no_cmp(game_serial_number,win_real_game_no,win_real_games)>=0) {
                                for(i=0;i<total_losers;i++){
                                    if(strcmp(player_id, LoserRec[i].player_id)==0){
                                        LoserRec[i].chips+=-chips;
                                        new_flag=0;
                                        break;
                                    }
                                }
                                if(new_flag) {
                                        LoserRec[total_losers].chips=-chips;
                                        strcpy(LoserRec[total_losers].player_id, player_id);
                                        total_losers++;
                                } else  
                                        new_flag=1;
                            } else break;
			}
					
                        break;
                default:
                        break;
                }
		memset(line, 0, MAX_LINE_LENGTH);
		memset(line2, 0, MAX_LINE_LENGTH);
        }

	//Star to generate the message file ...

	fprintf(fp_msg,"\n--------------------------------------------------\n");
	fprintf(fp_msg,"******** Summary ********\n");
	fprintf(fp_msg,"--------------------------------------------------\n");
	fprintf(fp_msg,"Total login times: %d\tTotal logout times %d)\n", login_cnt, logout_cnt);
	fprintf(fp_msg,"Total real money games: %d (Total games: %d, total play money games %d)\n", 
		real_games, total_games, play_games);
	fprintf(fp_msg,"--------------------------------------------------\n");
	fprintf(fp_msg,"WON real money: \n");
	fprintf(fp_msg,"%d games, $%.2f chips\n", win_real_games, 0.01*total_win);
	fprintf(fp_msg,"\nWho lost to %s:\n", arg_user_id);
	total1=0;
	for(i=0;i<total_losers;i++){
		sscanf(LoserRec[i].player_id,"%x",&hex_player_id);
		strcpy(user_id, get_userid(hex_player_id));
		fprintf(fp_msg,"%s -> %.2f\n",\
			user_id, 0.01*LoserRec[i].chips);	
		total1+=LoserRec[i].chips;
	}
	fprintf(fp_msg, "(total = %.2f)\n",0.01*total1);
	fprintf(fp_msg,"--------------------------------------------------\n");

	fprintf(fp_msg,"LOST real money: \n");
	fprintf(fp_msg,"%d games, $%.2f chips\n", lose_real_games, 0.01*total_lose);

	fprintf(fp_msg,"\nWho won from %s:\n", arg_user_id);
	for(i=0;i<total_winners;i++){
		sscanf(WinnerRec[i].player_id,"%x",&hex_player_id);
		strcpy(user_id, get_userid(hex_player_id));
		fprintf(fp_msg,"%s -> %.2f\n",\
			user_id, 0.01*WinnerRec[i].chips);	
		total2+=WinnerRec[i].chips;
	}
	fprintf(fp_msg,"(total = %.2f)\n",0.01*total2);
	fclose(fp_msg);

	 Email( arg_email_str,
			"Desert Poker",
			arg_email_str,
			"Full player info",
			"tmp_msg",
			NULL,
			TRUE);
	
	sprintf(cmd_line, "cat tmp_msg | mail -s \"Full Player Info\" \
		%s &", arg_email_str);
	system(cmd_line);
	system("rm -f tmp_msg");

err:	if (err_no){
    printf("\nhay algun error \n");
		fprintf(fp_msg,"%s\n", err_msg);
	}

}

}
