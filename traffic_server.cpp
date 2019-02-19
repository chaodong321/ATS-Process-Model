#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include "lock_and_kill.h"
#include "util_log.h"


#define LOG_FILE_NAME "./log/traffic_server.log"
#define LOG_BUF_SIZE 2048
#define LOG_SPLIT_SIZE 200000
#define LOG_MAX_QUEUE_SIZE 0


#define SERVER_LOCKFILE "log/server_lockfile"

int main()
{
	LOG_INIT(LOG_FILE_NAME, LOG_BUF_SIZE, LOG_SPLIT_SIZE, LOG_MAX_QUEUE_SIZE);
	
	pid_t holding_pid=0;
	Lockfile server_lf(SERVER_LOCKFILE);
	server_lf.Get(&holding_pid);

	while(1){

		char buf[100];
		sprintf(buf,"==============traffic_server is running, pid:'%d'!",getpid());
		LOG_INFO(buf);
		sleep(5);
		int c=rand()%100;

		if(c<30){//模拟server进程出现状况
			LOG_INFO("=================traffic_server has a expcetion and exit!");
			exit(1);
		}

	}
	return 0;

}
