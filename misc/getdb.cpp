/**********************
 * 05/09/03
 * A very simple TropicanaPoker.bin handler
 * ------------------------------------
 *****************/
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "/poker/pplib/gamedata.h"
char *filename = "/poker/server/src/TropicanaPoker.bin";
int dbm_fd;

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

int main()
{

	SDBRecord dbr;

	if((dbm_fd = open(filename,O_RDWR)) == -1){
	    printf("can't open %s\n",filename);
	    exit(1);
	}
	else SetFileLock(dbm_fd,F_WRLCK);

	printf("player_id\tcreat_time\thands\tuser_id\temail_address\tfull_name\tpriv\tdaily_deposit\ttotal_deposit\n");

	off_t dbsize = lseek(dbm_fd, 0, SEEK_END);
	int dbrno = dbsize/sizeof(dbr);
	
	lseek(dbm_fd, 0, SEEK_SET);
	for(int i=0; i<dbrno; i++) {
		read(dbm_fd, &dbr, sizeof(dbr));
		if(dbr.player_id) {
			int total_deposit = 0;
			int daily_deposit = 0;
			printf("%08x", dbr.player_id);
			printf("\t%d", dbr.account_creation_time);
			printf("\t%d", dbr.hands_seen);
			printf("\t%s", dbr.user_id);
			printf("\t%s", dbr.email_address);
			printf("\t%s", dbr.full_name);
			printf("\t%d", dbr.priv);
		//	printf("\t%s", dbr.password);

			tm dt;
			time_t tt;
			char time_str[20];
		        char yesterday_str[20];

		        tt = time(NULL);
		        tt-=24*60*60;
		        memcpy(&dt, localtime(&tt), sizeof(tm));
		        sprintf(yesterday_str,"%d-%02d-%02d",
		                dt.tm_year+1900,
		                dt.tm_mon+1,
        		        dt.tm_mday);

			for(int j=0; j<TRANS_TO_RECORD_PER_PLAYER; j++) {
				if (dbr.transaction[j].transaction_type==CTT_PURCHASE || \
				    dbr.transaction[j].transaction_type==CTT_WIRE_IN){		
				   tt=dbr.transaction[j].timestamp;
				   memcpy(&dt, localtime(&tt), sizeof(tm));
                           	   sprintf(time_str, "%d-%02d-%02d",
                                	dt.tm_year+1900,
                                	dt.tm_mon+1,
                                	dt.tm_mday);
				   if (strcmp(time_str, yesterday_str)==0){
				   	daily_deposit += dbr.transaction[j].transaction_amount;
				   }
				   total_deposit += dbr.transaction[j].transaction_amount;
				}
			}
			printf("\t$%d", daily_deposit);
			printf("\t$%d\n", total_deposit);
		//	printf("\t%s", dbr.password);
			printf("\n");
		}
		fprintf(stderr, "\r%d ...", i+1);
	}
	fprintf(stderr, "done\n");

	close(dbm_fd);

}
 
}
