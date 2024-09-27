 
#include <libpq-fe.h> //Connect to Postgres
#include <pthread.h>
#include <string.h>
#include <stdio.h>

//end cris 4-2-2004
#ifndef _LLIP_H_INCLUDED
  #include "llip.h"
#endif
//end cris 4-2-2004

#ifndef _OPA
#define _OPA

enum DBTypeMessage { 
	DB_NORMAL_QUERY,
	DB_NORMAL_SELECT,
	DB_NORMAL_INSERT,
	DB_NORMAL_UPDATE,
	DB_NORMAL_DELETE,
	DB_HIGH_PRIORITY,
	DB_USER_INSERT,
};

struct DBMessage {
	char theMessage[200];
	DBTypeMessage type;
	DBMessage * previous;
	DBMessage * next;
};

struct DBMessages {
	DBMessage * queue;
	DBMessage * top;
	pthread_mutex_t * queueLock;
};

class OPA {
public:
	OPA(); //create and connect
    OPA(char * file_Name); //create and connect
    ~OPA(); //disconnect and delete

	//executes an DBQuery and returns the result and the type of the query (1 = rows affected, 2 = returned data, 3 = error)
	PGresult * ExecuteSelect(char * theQuery); //executes a Select from the queue
	int ExecuteCommand(char * theCommand); //executes a Command from the queue

	void BeginTransaction(int theIsolationLevel, bool readWrite);
	void RollBack();
	void Commit();

	void setMessagesQueue(DBMessages * theMessages);
	DBMessages * getMessagesQueue();

	bool AddMessage(char * theMessage, DBTypeMessage type); //Add message to the queue
	void CreateThread(); //creates the thread that process the messages
	void AddLog(char * theLog, bool flush); //add message to the log file
	void ShutDown();
	int MessagesQueueSize(bool print);
	
	//temp variables
	pthread_mutex_t tempQueueLock;
	DBMessages * MessagesQueue;
	
	PGconn *conn;
	char conninfo1[100];   //connection string 1
	char conninfo2[100];   //connection string 2
	
	char fileName[100];    //Log File Name
	
	FILE * theFile;		   //Log File pointer
	
private:
	pthread_t MessagesProcess;
	pthread_t OPAInfo;
	bool DataBaseConnect(); 
	void DataBaseDisconnect();
	
};

#endif
