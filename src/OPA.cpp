#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "OPA.h"

//thread to process the messages into DataBase
void * ProcessMessages(void *arg) {
	int resultStatus = 0;
	OPA * theOPA = (OPA*)arg;	
	DBMessage * theMessage;	
	char errorMessage[100];
	theOPA->AddLog("Starting the process of the messages...!!!", false);
	while (true) { //forever
		while (theOPA->MessagesQueue->top != NULL) {
			theMessage = theOPA->MessagesQueue->top;
			resultStatus = theOPA->ExecuteCommand(theMessage->theMessage);
			if (resultStatus == 2) { //if success
				sprintf(errorMessage, "%s%s","SUCCESS processing the message ", theMessage->theMessage);
				theOPA->AddLog(errorMessage, true);	
			}
			else { //if error
				sprintf(errorMessage, "%s%s%s%d","ERROR processing the message ", theMessage->theMessage, " with error code -->", resultStatus);
				theOPA->AddLog(errorMessage, true);	
			}			
			pthread_mutex_lock(theOPA->MessagesQueue->queueLock); //block the mutex
			theOPA->MessagesQueue->top->previous = NULL;
			theOPA->MessagesQueue->top = theMessage->next;
			free(theMessage); //free memory
			pthread_mutex_unlock(theOPA->MessagesQueue->queueLock);	//free the mutex			
		}	
	}
	return NULL;
}
//end void * ProcessMessages(void *arg) 

void * PrintOPAInfo(void * arg) {
	OPA * theOPA = (OPA*)arg;	
	while (true) {
		theOPA->MessagesQueueSize(true);
		sleep(30);
	}
	return NULL;
}
//end void * PrintOPAInfo(void *arg) 

OPA::OPA() {
	conn = NULL;
	sprintf(conninfo1,"hostaddr = 192.168.0.52 user = postgres password = postgresdb dbname =  desert_poker"); //internal data base
	sprintf(conninfo2,"hostaddr = 200.9.37.163 user = postgres password = postgresdb dbname =  desert_poker"); //external database
	//cris 3-2-2004
	strcpy(fileName,"thread.txt");
	//end cris 3-2-2004
	DataBaseConnect();	
	pthread_mutexattr_t * attrib;
	pthread_mutexattr_settype(attrib, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&tempQueueLock, attrib);
	MessagesQueue = (DBMessages *)malloc(sizeof(DBMessages));
	MessagesQueue->queueLock = &tempQueueLock;
	pthread_mutex_lock(MessagesQueue->queueLock);
	MessagesQueue->queue = NULL;
	MessagesQueue->top = NULL;
	pthread_mutex_unlock(MessagesQueue->queueLock);
}


//cris 3-2-2004
OPA::OPA(char * file_Name) {
	conn = NULL;
	//sprintf(fileName, file_Name);
	sprintf(fileName,"Data/OPA/%s_%s.txt", file_Name,TimeStrWithYear2());
	/*sprintf(conninfo1,"hostaddr = 192.168.0.52 user = postgres password = postgresdb dbname =  desert_poker"); //internal data base
	sprintf(conninfo2,"hostaddr = 200.9.37.163 user = postgres password = postgresdb dbname =  dp_test"); //external database
	sprintf(conninfo2,"hostaddr = 192.168.2.1 user = postgres password = postgresdb dbname =  desert_poker"); //external database
	sprintf(conninfo2,"hostaddr = 192.168.0.44 user = postgres password = postgresdb dbname =  dp_test"); //internal data base */
//	sprintf(conninfo2,"hostaddr = 192.168.2.1 user = postgres password = postgresdb dbname =  desert_poker"); //external database
	sprintf(conninfo2,"hostaddr = 192.168.0.44 user = postgres password = postgresdb dbname =  dp_test_cris");
//	sprintf(conninfo2,"hostaddr = 200.9.37.163 user = postgres password = postgresdb dbname =  desert_poker"); //external database
	theFile = fopen(fileName, "w");	
	DataBaseConnect();	
	pthread_mutexattr_t * attrib;
	pthread_mutexattr_settype(attrib, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init(&tempQueueLock, attrib);
	MessagesQueue = (DBMessages *)malloc(sizeof(DBMessages));
	MessagesQueue->queueLock = &tempQueueLock;
	pthread_mutex_lock(MessagesQueue->queueLock);
	MessagesQueue->queue = NULL;
	MessagesQueue->top = NULL;
	pthread_mutex_unlock(MessagesQueue->queueLock);
	kp(("NEW Thread OPA %s Constructor Called\n",fileName));
	fprintf(theFile, "\n################################################ \n");
	fprintf(theFile, "# Session Started at %s\n", TimeStr());
	fprintf(theFile, "# Configuration:%s\n",conninfo2);
	fprintf(theFile, "################################################ \n\n");
	fflush(theFile);
}
//cris 3-2-2004

OPA::~OPA() {
	conn = NULL;
	DataBaseDisconnect();
	if (MessagesQueue->top != NULL) {
		kp(("ERROR...!! OPA Thread %s Terminated with %d messages in the database queue without processed at %s\n",fileName==NULL?"":fileName, MessagesQueueSize(true), TimeStr()));
	}
	else {
	    kp(("SUCCESS...!!! OPA Thread %s Terminated without messages in the database queue at %s\n",fileName==NULL?"":fileName, TimeStr()));
	}	
	fprintf(theFile, "\n################################################ \n");
	fprintf(theFile, "# Session Closed at %s\n", TimeStr());
	fprintf(theFile, "################################################ \n\n");
	fclose(theFile);
}

DBMessages * OPA::getMessagesQueue() {
	return MessagesQueue;	
}

bool OPA::DataBaseConnect() {
	conn = PQconnectdb(conninfo2);
	if (PQstatus(conn) == CONNECTION_BAD) {
		DataBaseDisconnect();
		return false;
	}	
	else {
		return true;	
	}
}

void OPA::DataBaseDisconnect() {
	if (conn)
		PQfinish(conn);
	conn = NULL;	
}

PGresult * OPA::ExecuteSelect(char * theQuery) {
	PGresult * result;
	if (PQstatus(conn) != CONNECTION_OK) {
		PQfinish(conn);
		DataBaseConnect();
	}
	if (PQstatus(conn) == CONNECTION_OK) {
		result = PQexec(conn, theQuery);
		if (PQresultStatus(result) != PGRES_COMMAND_OK) {
			PQclear(result);
			result = NULL;	
		}
	}
	return result;
}	

int OPA::ExecuteCommand(char * theCommand) {
	PGresult * result;
	int resultStatus = 0;
	bool commandResult = false;
	if (PQstatus(conn) != CONNECTION_OK) {
		PQfinish(conn);
		DataBaseConnect();
	}
	if (PQstatus(conn) == CONNECTION_OK) {
		result = PQexec(conn, theCommand);
		resultStatus = PQresultStatus(result);
		PQclear(result);
		result = NULL;	
	}		
	return resultStatus;
}

void OPA::BeginTransaction(int theIsolationLEvel, bool readWrite) {
	ExecuteCommand("BEGIN");
}

void OPA::RollBack() {
	ExecuteCommand("ROLLBACK");
}

void OPA::Commit() {
	ExecuteCommand("COMMIT");
}

bool OPA::AddMessage(char * theMessage, DBTypeMessage type) {
	DBMessage * tempMessage = (DBMessage *)malloc(sizeof(DBMessage));
	tempMessage->previous = NULL;
	tempMessage->next = NULL;
	sprintf(tempMessage->theMessage, theMessage);
	if (pthread_mutex_lock(MessagesQueue->queueLock) == 0) {
		if (MessagesQueue->top == NULL) {
			MessagesQueue->queue = tempMessage;
			MessagesQueue->top = tempMessage;	
		}
		else {			
			switch (type) {
				case DB_NORMAL_QUERY:
					MessagesQueue->queue->next = tempMessage;
					tempMessage->previous = MessagesQueue->queue;
					MessagesQueue->queue = tempMessage;
				break;
				case DB_HIGH_PRIORITY:
					MessagesQueue->top->previous = tempMessage;
					tempMessage->next = MessagesQueue->top;
					MessagesQueue->top = tempMessage;
				break;
				default:
					MessagesQueue->queue->next = tempMessage;
					tempMessage->previous = MessagesQueue->queue;
					MessagesQueue->queue = tempMessage;
				break;
			}
		}
		pthread_mutex_unlock(MessagesQueue->queueLock);
		return true;
	}
	else {
		return false;	
	}	
	return true;
}

void OPA::CreateThread() {
	pthread_create(&MessagesProcess, NULL, ProcessMessages, this);
	pthread_create(&OPAInfo, NULL, PrintOPAInfo, this);
	sleep(1);
}

void OPA::AddLog(char * theLog, bool flush) {
	fprintf(theFile, "%s\n", theLog);
	if (flush) {
		fflush(theFile);
	}
}

void OPA::ShutDown() {
	if (MessagesQueue->top != NULL) {
		kp(("Shuting Down the OPA with %d pending messages in the database queue at %s...!!! Wait while the messages are processed\n", MessagesQueueSize(false),TimeStr()));
		fprintf(theFile, "%s%d%s%s%s", "Shuting Down the OPA with ", MessagesQueueSize(false) ," pending messages in the database queue at ", TimeStr(), "...!!! Wait while the messages are processed\n");	
	}
	else {
		kp(("Shuting Down the OPA without messages in the database queue at %s\n", TimeStr()));
		fprintf(theFile, "%s%s\n", "Shuting Down the OPA without messages in the queue at ", TimeStr());	
	}	
	while (MessagesQueue->top != NULL) {
		sleep(60);	    
		if (MessagesQueue->top == NULL) {
			AddLog("The queue is empty. All the messages were processed...!", false);
		}
		else {
			MessagesQueueSize(true);
		}
	}	
}

int OPA::MessagesQueueSize(bool print) {
	DBMessage * tempMessage;	
	tempMessage = MessagesQueue->top;
	int tempSize = 0;
	if (MessagesQueue->top != NULL) {
		tempSize++;
	}
	while ((tempMessage != MessagesQueue->queue) && (MessagesQueue->top != NULL)) {
		tempMessage = tempMessage->next;
		tempSize++;
	}
	if (print) {
		fprintf(theFile, "%s%d%s%s\n", "There are ", tempSize, " messages in the queue at ", TimeStr());
		kp(("There are %d messages in the queue at %s\n", tempSize, TimeStr()));
		fflush(theFile);
	}
	return tempSize;
}
