#define DEBUG 0
#if DEBUG
#define Dprintf printf
#else
#define Dprintf
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "mysql.h"

#define MAX_LINE_LENGTH 320

FILE *fp;
MYSQL mysql;
MYSQL_RES *result;
MYSQL_ROW row;
char sql[512];

extern "C" {

int check_parse(char * table_name, int yy, int mm, int dd)
{ 
	//Check if the logfile has been parsed before
	int i=0;
	sprintf(sql, "select * from %s where YEAR(date_time)=%d and 
		MONTH(date_time)=%d and DAYOFMONTH(date_time)=%d",
		table_name, yy, mm, dd);
	mysql_real_query(&mysql, sql, strlen(sql));
	result=mysql_store_result(&mysql);
	while (row = mysql_fetch_row(result)){
		i++;
	}
	if (i>0){
		mysql_free_result(result);
		return(-1);
	}else {
		mysql_free_result(result);
		return 1;
	}
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

int main(int argc, char **argv)
{
	int i=0;
	char filename[30];
	char line[MAX_LINE_LENGTH];
	char line2[MAX_LINE_LENGTH];

	if (argc<2) {
		fprintf(stderr, "Usage: %s [hal_logfile]\n", argv[0]);
		exit(1);
	} else {
		strcpy(filename, argv[1]);
	}

	//Start to parse the hal file

	if ((fp=fopen(filename, "r"))==NULL){
		fprintf(stderr, "The hal file %s can not be opened. Error: %d.\n", filename,errno);
		exit(3);
	}
	printf("Start parsing %s ......\n", filename);

	//Connect to Mysql 

	mysql_init(&mysql);
	mysql_options(&mysql,MYSQL_READ_DEFAULT_GROUP,"mysqld");
	if (!mysql_real_connect(&mysql,"localhost","root",".barney","test",0,NULL,0))
	{
		fprintf(stderr, "Failed to connect: Error: %s\n", mysql_error(&mysql));
		fprintf(stderr, "Parsing file %s failed.\n", filename);
		exit(2);
	}
	
	int num_line=0;
	char log_date[10];
	char log_time[10];
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
	static int log_type;
	int full_datetime;
	char time_str[20];
	static int parse_checked[42];
	static int line_count[42];

	for(i=0;i<40;i++) parse_checked[i]=0;

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

		//printf("%s\t%d\n", time_str, log_type);
		

		//Start to insert into database tables

		char buf[100];
		char dbtable_name[64];

		switch(log_type) {
		case 1:  //LogComment
			strcpy(dbtable_name, "HAL_1_LogComment");
			if (parse_checked[1]==0){
			   parse_checked[1]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
			   if (parse_checked[1]==-1){
				printf("%s: The file has been parsed before\n", filename);
				exit(4);
			   }
			}
			sprintf(sql,"insert into %s(date_time, comment) values", dbtable_name);
			char comment[256];
			memset(comment,0,256);
			line_escape_white(line, line2);
			sscanf(line2, "%s\t%s\t%s\t%s", buf, buf, buf, comment);
			line_restore_white(comment);
			sprintf(buf, "('%s','%s')", time_str, comment);
			strcat(sql,buf);
			mysql_real_query(&mysql, sql, strlen(sql));

			comment[0]='\0';
			
			break;

		case 2:  //LogLogin(login)
			strcpy(dbtable_name, "HAL_2_3_LogLogin");
			if (parse_checked[2]==0){
			   parse_checked[2]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
			   if (parse_checked[2]==-1){
				printf("%s: The file has been parsed before\n", filename);
				exit(4);
			   }
			}

			sprintf(sql,"insert into %s(date_time, player_id, user_id, ip_address, \
				real_in_bank, real_in_play, fake_in_bank, fake_in_play, LoginOrOut) \
				values", dbtable_name);
                        char player_id[32];
			char user_id[32];
			char ip_addr[32];
			int real_in_bank;
			int real_in_play;
			int fake_in_bank;
			int fake_in_play;

			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\%d", 
				buf, buf, buf, player_id, user_id, ip_addr, &real_in_bank,
				&real_in_play, &fake_in_bank, &fake_in_play);
			line_restore_white(user_id);
                        sprintf(buf, "('%s','%s','%s','%s',%d,%d,%d,%d,'LOGIN')", time_str, 
				player_id, user_id, ip_addr,
				real_in_bank, real_in_play, fake_in_bank, fake_in_play);
                        strcat(sql,buf);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;
		case 3:  //LogLogin(logout)
                        strcpy(dbtable_name, "HAL_2_3_LogLogin");
                        if (parse_checked[2]==0){
                           parse_checked[2]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[2]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

			sprintf(sql,"insert into %s(date_time, player_id, user_id, ip_address, \
				real_in_bank, real_in_play, fake_in_bank, fake_in_play, LoginOrOut) \
				values", dbtable_name);

			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%s\t%d\t%d\t%d\%d", 
                                buf, buf, buf, player_id, user_id, ip_addr, &real_in_bank,
                                &real_in_play, &fake_in_bank, &fake_in_play);
			line_restore_white(user_id);
                        sprintf(buf, "('%s','%s','%s','%s',%d,%d,%d,%d,'LOGOUT')", time_str,
                                player_id, user_id, ip_addr,
                                real_in_bank, real_in_play, fake_in_bank, fake_in_play);
                        strcat(sql,buf);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;
		case 4:  //LogGameStart
			strcpy(dbtable_name, "HAL_4_LogGameStart");
			if (parse_checked[4]==0){
			   parse_checked[4]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
			   if (parse_checked[4]==-1){
				printf("%s: The file has been parsed before\n", filename);
				exit(4);
			   }
			}

			sprintf(sql,"insert into %s(date_time, game_serial_number, table_serial_number, \
				table_name, game_rules, chip_type, big_blind_amount, small_blind_amount, \
				button, tournament_buyin) values", dbtable_name);
                        int game_serial_number;
			int table_serial_number;
			char table_name[32];
			int game_rules;
			int chip_type;
			int big_blind_amount;
			int small_blind_amount;
			int button;
			int tournament_buyin;
                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%s\t%d\t%d\t%d\%d\t%d\t%d", \
                                buf, buf, buf, &game_serial_number, &table_serial_number, \
                                table_name, &game_rules, &chip_type, &big_blind_amount, &small_blind_amount, \
				&button, &tournament_buyin);
                        line_restore_white(table_name);
                        sprintf(buf, "('%s',%d,%d,'%s',%d,%d,%d,%d,%d,%d)", time_str,
                                game_serial_number, table_serial_number, table_name,
				game_rules, chip_type, big_blind_amount, small_blind_amount, 
                                button, tournament_buyin);
                        strcat(sql,buf);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;
		case 5:  //LogGamePlayer
                        strcpy(table_name, "HAL_5_LogGamePlayer");
                        if (parse_checked[5]==0){
                           parse_checked[5]=check_parse(table_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[5]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, sitting_out_flags, \
                                seating_position, player_id, user_id, chips) values", table_name);
                        char sitting_out_flags[32];
                        int seating_position;
                        int chips;
			char record[200];
			int count;
			int index[11];
			int j;
			int k;

			line_escape_white(line, line2);
			
                        sscanf(line2, "%s\t%s\t%s\t%d\t%s\t%200c", buf, buf, buf, \
				&game_serial_number, sitting_out_flags, record);
			//printf("len=%d,*** %s\n",strlen(record),record);
			index[0]=0;
			j=1;k=0;
			for(i=0;i<strlen(record);i++){
				if(record[i]=='\t'){
				   if(k==4){
					index[j]=i+1;
					j++;
					k=0;
				   }else {
					k++;
				   }
				}
			}				

			count=j-1;	
			for (i=0;i<count;i++){
				sscanf(&record[index[i]], "%d\t%s\t%s\t%d", &seating_position,
					player_id, user_id, &chips);
				line_restore_white(user_id);
				
                        	sprintf(buf, "('%s',%d,'%s',%d,'%s','%s',%d)", time_str,
                                	game_serial_number, sitting_out_flags, seating_position,
                                	player_id, user_id, chips);
				j=strlen(sql);
                        	strcat(sql,buf);
				//printf("%s\n",sql);
                        	mysql_real_query(&mysql, sql, strlen(sql));
				sql[j]='\0';
			}

			break;

		case 6:  //LogGameAction

                        strcpy(dbtable_name, "HAL_6_LogGameAction");
                        if (parse_checked[6]==0){
                           parse_checked[6]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[6]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, action_serial_number, \
                                seating_position, action, action_amount) values", dbtable_name);
                        int action_serial_number;
                        int action;
                        int action_amount;

			line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d", \
                                buf, buf, buf, &game_serial_number, &action_serial_number, \
                                &seating_position, &action, &action_amount);
                        sprintf(buf, "('%s',%d,%d,%d,%d,%d)", time_str,
                                game_serial_number, action_serial_number, 
                                seating_position, action, action_amount);
                        strcat(sql,buf);
			//printf("%s\n",sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;
		case 7:  //LogGameCardsDealt
                        strcpy(table_name, "HAL_7_LogGameCardsDealt");
                        if (parse_checked[7]==0){
                           parse_checked[7]=check_parse(table_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[7]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, index_no, \
                                card) values", table_name);
                        int fd_index;
                        int card;

                        line_escape_white(line, line2);

                        sscanf(line2, "%s\t%s\t%s\t%d\t%200c", buf, buf, buf, \
                                &game_serial_number, record);
                        //printf("len=%d,*** %s\n",strlen(record),record);
                        index[0]=0;
                        j=1;k=0;
                        for(i=0;i<strlen(record);i++){
                                if(record[i]=='\t'){
                                   if(k==2){
                                        index[j]=i+1;
                                        j++;
                                        k=0;
                                   }else {
                                        k++;
                                   }
                                }
                        }

                        count=j-1;
                        for (i=0;i<count;i++){
                                sscanf(&record[index[i]], "%d\t\%d", &fd_index, &card);

                                sprintf(buf, "('%s',%d,%d,%d)", time_str,
                                        game_serial_number, fd_index, card);
                                j=strlen(sql);
                                strcat(sql,buf);
                                //printf("%s\n",sql);
                                mysql_real_query(&mysql, sql, strlen(sql));
                                sql[j]='\0';
 			}
			break;

		case 8:  //LogGameEndPlayer
                        strcpy(dbtable_name, "HAL_8_LogGameEndPlayer");
                        if (parse_checked[8]==0){
                           parse_checked[8]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[8]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, seating_position, \
                                showed_hand, chips_net_change, hand_desc) values", dbtable_name);
                        int showed_hand;
                        int chips_net_change;
                        char hand_desc[80];

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%s", \
                                buf, buf, buf, &game_serial_number, &seating_position, \
                                &showed_hand, &chips_net_change, hand_desc);
			line_restore_white(hand_desc);
                        sprintf(buf, "('%s',%d,%d,%d,%d,'%s')", time_str,
                                game_serial_number, seating_position,
                                showed_hand, chips_net_change, hand_desc);
                        strcat(sql,buf);
			//printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

			hand_desc[0]='\0';

			break;
		case 9:  //LogGameEnd
                        strcpy(dbtable_name, "HAL_9_LogGameEnd");
                        if (parse_checked[9]==0){
                           parse_checked[9]=check_parse(dbtable_name, dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[9]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, rake, \
                                pot1,pot2,pot3,pot4,pot5,pot6,pot7,pot8,pot9,pot10) values", dbtable_name);

                        int rake;
                        int pot1;
                        int pot2;
                        int pot3;
                        int pot4;
                        int pot5;
                        int pot6;
                        int pot7;
                        int pot8;
                        int pot9;
                        int pot10;
                        
                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", \
                                buf, buf, buf, &game_serial_number, &rake, \
                                &pot1,&pot2,&pot3,&pot4,&pot5,&pot6,&pot7,&pot8,&pot9,&pot10);
                        sprintf(buf, "('%s',%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d)", time_str,
                                game_serial_number, rake,
                                pot1,pot2,pot3,pot4,pot5,pot6,pot7,pot8,pot9,pot10);
                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;

		case 10: //LogGameChatMsg
                        strcpy(dbtable_name, "HAL_10_LogGameChatMsg");
                        if (parse_checked[10]==0){
                           parse_checked[10]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[10]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
			char chat_msg[256];

                        sprintf(sql,"insert into %s(date_time, game_serial_number, \
                                user_id, chat_msg) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%s\t%s", \
                                buf, buf, buf, &game_serial_number, user_id,chat_msg); 
			line_restore_white(user_id);
			line_restore_white(chat_msg);
                        sprintf(buf, "('%s',%d,'%s','%s')", time_str, game_serial_number, user_id, chat_msg);

                        strcat(sql,buf);
                        //printf("len=%d %s\n", strlen(sql),sql);
                        mysql_real_query(&mysql, sql, strlen(sql));
			
			chat_msg[0]='\0';

			break;
		case 11: //LogNextLogfile
                       strcpy(dbtable_name, "HAL_11_LogNextLogfile");
			/*
                        if (parse_checked[11]==0){
                           parse_checked[11]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[11]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
			*/
                        char action_filename_inuse[32];

                        sprintf(sql,"insert into %s(date_time, action_filename_inuse) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s", \
                                buf, buf, buf, action_filename_inuse);
                        sprintf(buf, "('%s','%s')", time_str, action_filename_inuse);

                        strcat(sql,buf); 
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
		case 12: //LogAll7csCards
                        strcpy(dbtable_name, "HAL_12_LogAll7csCards");
                        if (parse_checked[12]==0){
                           parse_checked[12]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[12]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, game_serial_number, seating_position, \
                                card1,card2,card3,card4,card5,card6,card7) values", dbtable_name);

                        int card1;
                        int card2;
                        int card3;
                        int card4;
                        int card5;
                        int card6;
                        int card7;

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", \
                                buf, buf, buf, &game_serial_number, &seating_position, \
                                &card1,&card2,&card3,&card4,&card5,&card6,&card7);
                        sprintf(buf, "('%s',%d,%d,%d,%d,%d,%d,%d,%d,%d)", time_str,
                                game_serial_number, seating_position,
                                card1,card2,card3,card4,card5,card6,card7);
                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

			break;
		case 20: //LogEcashToPP
                        strcpy(dbtable_name, "HAL_20_LogEcashToPP");
                        if (parse_checked[20]==0){
                           parse_checked[20]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[20]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
                        char description[128];
			
			comment[0]='\0';

                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
				&chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
				game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf); 
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        comment[0]='\0';
			description[0]='\0';

			break;
		case 27: //LogPPGameResult
                        strcpy(dbtable_name, "HAL_27_LogPPGameResult");
                        if (parse_checked[27]==0){
                           parse_checked[27]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[27]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
                                &chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
                                game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        comment[0]='\0';
			description[0]='\0';

			break;
                case 29: //LogTransMCred
                        strcpy(dbtable_name, "HAL_29_LogTransMCred");
                        if (parse_checked[29]==0){
                           parse_checked[29]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[29]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
                                &chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
                                game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
                 case 30: //LogUrnPayout
                        strcpy(dbtable_name, "HAL_30_LogUrnPayout");
                        if (parse_checked[30]==0){
                           parse_checked[30]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[30]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
                 case 31: //LogATransPending
                        strcpy(dbtable_name, "HAL_31_LogTransPending");
                        if (parse_checked[31]==0){
                           parse_checked[31]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[31]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
                                &chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
                                game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
                 case 32: //LogTransCred
                        strcpy(dbtable_name, "HAL_32_LogTransCred");
                        if (parse_checked[32]==0){
                           parse_checked[32]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[32]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
                                &chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
                                game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
                 case 33: //LogTransChk
                        strcpy(dbtable_name, "HAL_33_LogTransChk");
                        if (parse_checked[33]==0){
                           parse_checked[33]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[33]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }
                        sprintf(sql,"insert into %s(date_time, player_id, game_serial_number, \
                                chips, chip_type, description, comment) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%d\t%d\t%d\t%s\t%s", \
                                buf, buf, buf, player_id, &game_serial_number, &chips, \
                                &chip_type, description, comment);
                        line_restore_white(description);
                        line_restore_white(comment);
                        sprintf(buf, "('%s','%s',%d,%d,%d,'%s','%s')", time_str, player_id, \
                                game_serial_number, chips, chip_type, description, comment);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        break;
 
		case 34: //LogTransfer
                        strcpy(dbtable_name, "HAL_34_LogTransfer");
                        if (parse_checked[34]==0){
                           parse_checked[34]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[34]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

			char from_id[32];
			char to_id[32];
			char from_account[64];
			char to_account[64];
			char reason[32];
			int amount;

                        sprintf(sql,"insert into %s(date_time, from_id, to_id, amount, \
                                from_account, to_account, chip_type, reason) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s", \
                                buf, buf, buf, from_id, to_id, &amount, \
                                from_account, to_account, &chip_type, reason);
                        line_restore_white(from_account);
                        line_restore_white(to_account);
                        line_restore_white(reason);
                        sprintf(buf, "('%s','%s','%s',%d,'%s','%s',%d,'%s')", time_str, from_id, to_id, \
                                amount, from_account, to_account, chip_type, reason);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        from_id[0]='\0';
                        to_id[0]='\0';
                        from_account[0]='\0';
                        to_account[0]='\0';
                        reason[0]='\0';

			break;
		case 35: //LogChipsToTable
                        strcpy(dbtable_name, "HAL_35_LogChipsToTable");
                        if (parse_checked[35]==0){
                           parse_checked[35]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[35]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, from_id, to_id, amount, \
                                from_account, to_account, chip_type, reason) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s", \
                                buf, buf, buf, from_id, to_id, &amount, \
                                from_account, to_account, &chip_type, reason);
                        line_restore_white(from_account);
                        line_restore_white(to_account);
                        line_restore_white(reason);
                        sprintf(buf, "('%s','%s','%s',%d,'%s','%s',%d,'%s')", time_str, from_id, to_id, \
                                amount, from_account, to_account, chip_type, reason);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        from_id[0]='\0';
                        to_id[0]='\0';
                        from_account[0]='\0';
                        to_account[0]='\0';
                        reason[0]='\0';

			break;
                case 40: //LogCCFeeRefund
                        strcpy(dbtable_name, "HAL_40_LogCCFeeRefund");
                        if (parse_checked[40]==0){
                           parse_checked[40]=check_parse(dbtable_name,dt.tm_year+1900, dt.tm_mon, dt.tm_mday);
                           if (parse_checked[40]==-1){
				printf("%s: The file has been parsed before\n", filename);
                                exit(4);
                           }
                        }

                        sprintf(sql,"insert into %s(date_time, from_id, to_id, amount, \
                                from_account, to_account, chip_type, reason) values", dbtable_name);

                        line_escape_white(line, line2);
                        sscanf(line2, "%s\t%s\t%s\t%s\t%s\t%d\t%s\t%s\t%d\t%s", \
                                buf, buf, buf, from_id, to_id, &amount, \
                                from_account, to_account, &chip_type, reason);
                        line_restore_white(from_account);
                        line_restore_white(to_account);
                        line_restore_white(reason);
                        sprintf(buf, "('%s','%s','%s',%d,'%s','%s',%d,'%s')", time_str, from_id, to_id, \
                                amount, from_account, to_account, chip_type, reason);

                        strcat(sql,buf);
                        //printf("%s\n", sql);
                        mysql_real_query(&mysql, sql, strlen(sql));

                        from_id[0]='\0';
                        to_id[0]='\0';
                        from_account[0]='\0';
                        to_account[0]='\0';
                        reason[0]='\0';

			break;
		default:
			printf("******* new logtype founded: %d\n",log_type); 
			break;
		}
		//printf("%s\n",line);
		num_line++;
		line_count[log_type]=line_count[log_type]+1;
			
	}	
	mysql_close(&mysql);

	int total_insert;
	total_insert=0;
	printf("Parse halfile %s finished. Totally %d lines been parsed\n", filename, num_line);
	for(i=1;i<41;i++){
	   if (line_count[i]!=0){
		printf("Log type %d: %d lines\n", i, line_count[i]);
		total_insert += line_count[i];
	   }
	}
	printf("Totally %d lines have been successfully parsed\n",total_insert);
	//fclose(fp);
}

}
