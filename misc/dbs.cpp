
/**********************
 * 05/09/03
 * A very simple TropicanaPoker.bin handler
 * ------------------------------------
 *****************/
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "gamedata.h"
char *filename = "../src/TropicanaPoker.bin";
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

int main(int argc, char **argv)
{

	SDBRecord dbr;
	int user_cnt = 0;
	int *user_list = NULL;
	int *raked_games =NULL;

	if((dbm_fd = open(filename,O_RDWR)) == -1)
	    printf("can't open %s\n",filename);
	else SetFileLock(dbm_fd,F_WRLCK);

	if(argc > 1) {
		// Read user table from file
		FILE *fd = fopen(argv[1], "r");
		if(fd == NULL) {
			fprintf(stderr, "can't open %s for reading\n", argv[1]);
			close(dbm_fd);
			exit(1);
		}
		while(!feof(fd)) {
			user_list = (int *)realloc(user_list,(user_cnt+1)*sizeof(int));
			raked_games = (int *)realloc(raked_games,(user_cnt+1)*sizeof(int));
			fscanf(fd,"%x\t",user_list+user_cnt);
			fscanf(fd,"%d\n",raked_games+user_cnt);
			user_cnt++;
		}
		fclose(fd);
	}

	char confirm[100];
	fprintf(stderr, "rewriting %s is an irreversible operation.\nthis program will reset all player's real_in_bank=0 and fake_in_bank=$1000.\ndo you really want to do this? (y/n)", filename);
	fgets(confirm, 100, stdin);
	if(*confirm != 'y') {
		close(dbm_fd);
		exit(2);
	}

	off_t dbsize = lseek(dbm_fd, 0, SEEK_END);
	int dbrno = dbsize/sizeof(dbr);
	
	lseek(dbm_fd, 0, SEEK_SET);
	for(int i=0; i<dbrno; i++) {
		read(dbm_fd, &dbr, sizeof(dbr));
		if(dbr.player_id) {
			int matched = 0;
			if(user_cnt == 0)
				matched = 0;
			else {
				for(int j=0; j<user_cnt; j++) {
					if(dbr.player_id == user_list[j]){
						matched = j+1;
						break;
					}
				}
			}
			if(matched) {
				printf("%08x,", dbr.player_id);
				printf("\t%s,", dbr.user_id);
				printf("\t%s,", dbr.client_platform.vendor_code);
				printf("\n");

				//dbr.fake_in_bank = 100000;
				//dbr.fake_in_play = 0;
				//dbr.real_in_bank = 100000;
				//dbr.real_in_play = 0;
				//dbr.fee_credit_points = 2;
				for (int k=0;k<TRANS_TO_RECORD_PER_PLAYER;k++){
					
					
					if (dbr.transaction[k].transaction_type>0) {
					/*
					    dbr.transaction[k].timestamp=dbr.transaction[k+2].timestamp;
					    dbr.transaction[k].ecash_id=dbr.transaction[k+2].ecash_id;
					    dbr.transaction[k].transaction_amount=dbr.transaction[k+2].transaction_amount;
					    dbr.transaction[k].partial_cc_number=dbr.transaction[k+2].partial_cc_number;
					    if (dbr.transaction[k].partial_cc_number!=1418817912)
					    dbr.transaction[k].credit_left=dbr.transaction[k].transaction_amount;
					    else
					    dbr.transaction[k].credit_left=0;

					    dbr.transaction[k].transaction_type=dbr.transaction[k+2].transaction_type;
					    strcpy(dbr.transaction[k].str, dbr.transaction[k+2].str);
					if (dbr.transaction[k].transaction_type==22)
					    dbr.transaction[k].credit_left=dbr.transaction[k].transaction_amount;
					*/
					    printf("(%d)type->%d, time->%d: %d\t%d\n", k, \
							dbr.transaction[k].timestamp, \
					    		dbr.transaction[k].transaction_type, \
					    		dbr.transaction[k].transaction_amount,\
					    		dbr.transaction[k].credit_left);
					}
					
				}
				//memset(dbr.transaction,0,sizeof(dbr.transaction));
				//dbr.pending_check = 0;
				//dbr.pending_fee_refund = 0;
				//dbr.pending_paypal = 0;
				//dbr.flags = 0;
				//lseek(dbm_fd, -sizeof(dbr), SEEK_CUR);
				//write(dbm_fd, &dbr, sizeof(dbr));
			}

		}

		fprintf(stderr, "\r%d ...", i+1);
	}
	fprintf(stderr, "done\n");

	close(dbm_fd);

}

}
