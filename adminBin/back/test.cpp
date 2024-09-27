/*****************************************************************/
/* https://github.com/kriskoin                                   */
/* Lee el archivo DesertPoker.Bin y envia a escribir a Postgres	 */
/*****************************************************************/

#include <libpq-fe.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "gamedata.h"
#include "stdlib.h"
#include <math.h>




char sql[512];
char conninfo[100];
char nameBIN[50] ;    //name of the bin file
PGconn *conn;
PGresult *result;

#define conninfo  "hostaddr = 192.168.0.44 user = postgres password = postgresdb dbname =  dp_test_cris"


int main( int argc,char **argv){
   int numReg=0;
   FILE* fileBINformat;
   SDBRecord registro;
 	 strcpy(nameBIN,"//poker//src//Data//DB//DesertPoker.bin");    //name of the bin file

   if((fileBINformat=fopen(nameBIN,"r"))==NULL){
       printf ("%s%s%s","\n File: ",nameBIN,"  not found !!! \n");
       exit(0);
   };//if

//	 FILE *B;
//	 B = fopen("Backup.txt", "w+");	
//   fprintf(B,"# player_id,user_id,passw,fisrt_name,last_name,gender,email,city,real_bank,real_play,fake_bank,fake_play,hands,flops,rivs,valid_rec\n");		

 	 conn = PQconnectdb(conninfo);
	 if (PQstatus(conn) == CONNECTION_OK){	                    	
	   while(!(feof(fileBINformat))){
  	    fread(&registro,sizeof(registro),1,fileBINformat);
    	  if (!(feof(fileBINformat))){				 		
      	   if(registro.player_id){
//			  	   fprintf(B,"%d,",registro.player_id);   //player id
//				     fprintf(B,"%s,",registro.user_id);     //user id
//				     fprintf(B,"%s,",registro.password);    //password
//				     fprintf(B,"%s,",registro.full_name);   //first name
//				     fprintf(B,"%s,",registro.last_name);
//					   fprintf(B,"%s,",registro.gender1);
//				     fprintf(B,"%s,",registro.email_address);
//				     fprintf(B,"%s,",registro.city);		
//				     fprintf(B,"%s,",registro.birth_date);		
//				     fprintf(B,"%d,",registro.real_in_bank / 100);
//			  	   fprintf(B,"%d,",registro.real_in_play / 100);
//			    	 fprintf(B,"%d,",registro.fake_in_bank / 100);
//				     fprintf(B,"%d",registro.fake_in_play / 100);
		
				   	 sprintf(sql," update players_account set real_in_bank=%d, real_in_play=%d, fake_in_bank=%d, fake_in_play=%d, is_logged=FALSE where login_name='%s'",	(registro.real_in_bank / 100), (registro.real_in_play / 100),	(registro.fake_in_bank / 100), (registro.fake_in_play / 100),registro.user_id);
						 result = PQexec(conn,sql);
						 PQclear(result);
//				     fprintf(B,"\n");
	           numReg++;
  	       };//if
    	  };//if
   	};//while
	 };//CONNECTION_OK	

   PQfinish(conn);	
//	 fclose(B);
   fclose(fileBINformat);
   printf("Total Reg: %d\n",numReg);

   return(0) ;
};//main




