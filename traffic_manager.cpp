#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "lock_and_kill.h"
#include "util_log.h"


#define LOG_FILE_NAME "./log/traffic_manager.log"
#define LOG_BUF_SIZE 2048
#define LOG_SPLIT_SIZE 200000
#define LOG_MAX_QUEUE_SIZE 0

#define TRAFFIC_SERVER "traffic_server"

#define MANAGER_LOCKFILE "log/manager_lockfile"
#define SERVER_LOCKFILE "log/server_lockfile"

#define    NOWARN_UNUSED(x)    (void)(x)

static int killsig=SIGKILL;
static int coresig=0;
static const int sleep_time = 10;		// 10 sec
static const int manager_timeout = 3 * 60;		//	3 min
static const int server_timeout = 3 * 60;		//	3 min
static const int kill_timeout = 1 * 60; //	1 min

static void sig_alarm_warn(int signum=0)
{
	alarm(kill_timeout);
}


static void sig_fatal(int signum)
{
	abort();
}


static void set_alarm_warn()
{
	struct sigaction action;
	action.sa_handler = sig_alarm_warn;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGALRM, &action, NULL);
}

static void set_alarm_death()
{
	struct sigaction action;
	action.sa_handler = sig_fatal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGALRM, &action, NULL);
}

static void safe_kill(const char* lockfile_name,const char * pname,bool group)
{
	Lockfile lockfile(lockfile_name);
	LOG_INFO("Entering safe_kill");
	set_alarm_warn();
	alarm(kill_timeout);

	if (group == true) {
		lockfile.KillGroup(killsig, coresig, pname);
	} else {
		lockfile.Kill(killsig, coresig, pname);
	}
	alarm(0);
	set_alarm_death();
	LOG_INFO("Leaving safe_kill");

}

static void spawn_server()
{
	LOG_INFO("spwan traffic server");
	int err;
	err = fork();
	if (err == 0) {
		err = execl(TRAFFIC_SERVER, TRAFFIC_SERVER, NULL);
		if(err == -1){
			LOG_ERROR("error spawn, error info: %s", strerror(errno));
		}
		else{
			LOG_INFO("spawn traffic server successful!");
		}
		exit(0);

	} else if (err == -1) {
		LOG_ERROR("unable to fork !");
		exit(0);
	}
}


void check_server()
{
	int err;
	pid_t holding_pid;
	Lockfile server_lf(SERVER_LOCKFILE);
	err=server_lf.Get(&holding_pid);

	if(err==0){
		if(kill(holding_pid,0)==-1){
			ink_killall(TRAFFIC_SERVER, killsig);
			sleep(1);
			err=server_lf.Open(&holding_pid);
		}
	}

	if(err>0){
		server_lf.Close();
		safe_kill(SERVER_LOCKFILE, TRAFFIC_SERVER,false);
		spawn_server();
	}

}

int main()
{
	LOG_INIT(LOG_FILE_NAME, LOG_BUF_SIZE, LOG_SPLIT_SIZE, LOG_MAX_QUEUE_SIZE);
	
	pid_t holding_pid=0;
	Lockfile manager_lf(MANAGER_LOCKFILE);
	manager_lf.Get(&holding_pid);

	while(1){

		char buf[100];
		sprintf(buf,"----------------traffic_manager is running, pid:'%d'!",getpid());
		LOG_INFO(buf);


		sleep(5);
		int c=rand()%10;

		if(c==1){
			//模拟manager进程出现状况
			LOG_INFO("----------------traffic_manager has a expcetion and eixt!");
			exit(0);
		}else{
			//对server进程进行检查
			check_server();
		}
	}
	return 0;
}
