#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <signal.h>
#include <sys/param.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include "lock_and_kill.h"
#include "util_log.h"


#define LOG_FILE_NAME "./log/traffic_cop.log"
#define LOG_BUF_SIZE 2048
#define LOG_SPLIT_SIZE 200000
#define LOG_MAX_QUEUE_SIZE 0

#define TRAFFIC_MANAGER "traffic_manager"
#define TRAFFIC_SERVER "traffic_server"

#define COP_LOCKFILE "log/cop_lockfile"
#define MANAGER_LOCKFILE "log/manager_lockfile"
#define SERVER_LOCKFILE "log/server_lockfile"

#define    NOWARN_UNUSED(x)    (void)(x)


static int killsig=SIGKILL;
static int coresig=0;
static int server_not_found = 0;

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

static void sig_child(int signum)
{
	NOWARN_UNUSED(signum);
	pid_t pid = 0;
	int status = 0;
	for (;;) {
		pid = waitpid(WAIT_ANY, &status, WNOHANG);

		if (pid <= 0) {
			break;
		}
		// TSqa03086 - We can not log the child status signal from
		//	 the signal handler since syslog can deadlock.	Record
		//	 the pid and the status in a global for logging
		//	 next time through the event loop.	We will occasionally
		//	 lose some information if we get two sig childs in rapid
		//	 succession
		// child_pid = pid;
		//child_status = status;
  }
}


static void init_signals()
{
	struct sigaction action;
	LOG_INFO("Entering init_signals()");
	action.sa_handler = sig_child;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	sigaction(SIGCHLD, &action, NULL);
	action.sa_handler = sig_fatal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	LOG_INFO("leaving init_signals()");
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


//为了简单化，直接返回0
static int server_up()
{
	return 1;
}


static int heartbeat_manager()
{
	//safe_kill(manager_lockfile, manager_binary, true);
	return 1;
}

static int heartbeat_server()
{
	//safe_kill(server_lockfile, server_binary, false);
	//server_failures = 0;
	return 1;
}



static void spawn_manager()
{
	LOG_INFO("spwan traffic manager");
	int err;
	err = fork();
	if (err == 0) {

		err = execl(TRAFFIC_MANAGER, TRAFFIC_MANAGER, NULL);
		if(err == -1){
			LOG_ERROR("error spawn, error info: %s", strerror(errno));
		}
		else{
			LOG_INFO("spawn traffic manager successful!");
		}
		exit(0);
	} else if (err == -1) {
		LOG_ERROR("unable to fork !");
		exit(0);
	}
}

static void check_lockfile()
{
	int err;
	pid_t holding_pid;
	Lockfile cop_lf(COP_LOCKFILE);
	err = cop_lf.Get(&holding_pid);

	if (err < 0) {
		LOG_INFO("leaving check_lockfile(),and err<0");
		exit(0);
	} else if (err == 0) {
		LOG_INFO("leaving check_lockfile(),and err==0");
		exit(0);
	}
	
	LOG_INFO("lock file is true");
}



static void check_programs()
{
	int err;
	pid_t holding_pid;

	LOG_INFO("Entering check_programs()");
	//尝试去获取 manager的lockfile，如果成功，说明没有manager进程在运行
	Lockfile manager_lf(MANAGER_LOCKFILE);
	err = manager_lf.Open(&holding_pid);

	//通过检测err的值来判断manager进程的运行情况
	if(err==0){
		LOG_INFO("in check_programs(),manager_lockfile,err==0");

		if(kill(holding_pid,0)==-1){

			LOG_INFO("holding_pid is %d,and invalid",holding_pid);

			ink_killall(TRAFFIC_MANAGER, killsig);
			sleep(1);				  // give signals a chance to be received
			err = manager_lf.Open(&holding_pid);
		}
	}

	if(err>0){//说明可以获得manager lockfile
		// 'lockfile_open' returns the file descriptor of the opened
		// lockfile.  We need to close this before spawning the
		// manager so that the manager can grab the lock.
		manager_lf.Close();
		// Make sure we don't have a stray traffic server running.

		LOG_INFO("traffic_manager not running, making sure traffic_server is dead");
		safe_kill(SERVER_LOCKFILE,TRAFFIC_SERVER,false);
		spawn_manager();
	}
	else
	{
		//err<0,Open中返回负值，说明可能是加锁成功，但是设置lockfile的文件信息失败
		// If there is a manager running we want to heartbeat it to
		// make sure it hasn't wedged. If the manager test succeeds we
		// check to see if the server is up. (That is, it hasn't been
		// brought down via the UI).  If the manager thinks the server
		// is up, we make sure there is actually a server process
		// running. If there is we test it.

		alarm(2*manager_timeout);
		err=heartbeat_manager();//?
		alarm(0);

		if(err<0){//???what case
			return ;
		}


		if(server_up()<=0){//???what case
			return;//err>0 ,manager is running ,if server is down  we think manager can create a new server ,so return
		}

		Lockfile server_lf(TRAFFIC_SERVER);
		err=server_lf.Open(&holding_pid);

		if(err==0){
			if(kill(holding_pid,0)==-1){
				ink_killall(TRAFFIC_SERVER,killsig);
				sleep(1);// give signals a chance to be received
				err=server_lf.Open(&holding_pid);
			}
		}

		if(err>0){
			server_lf.Close();
			server_not_found += 1;

			if(server_not_found>1){
				server_not_found=0;
				safe_kill(MANAGER_LOCKFILE, TRAFFIC_MANAGER, true);
			}
		}else{
			alarm(2 * server_timeout);
			heartbeat_server();//?
			alarm(0);
		}

	}
	LOG_INFO("Leaving check_programs");
}


static void init()
{
	LOG_INFO("Entering init()");
	init_signals();
	check_lockfile();
	LOG_INFO("Leaving init()");
}

static void millisleep(int ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms - ts.tv_sec * 1000) * 1000 * 1000;
	nanosleep(&ts, NULL);
}

// Changed function from taking no argument and returning void
// to taking a void* and returning a void*. The change was made
// so that we can call ink_thread_create() on this function
// in the case of running cop as a win32 service.

static void* check(void* arg)
{
	LOG_INFO("check...");
	for(;;){
		// problems with the ownership of this file as root Make sure it is
		// owned by the admin user

		alarm(2 * (sleep_time + manager_timeout * 2 + server_timeout));

		check_programs();
		millisleep(sleep_time * 1000);
	}
	LOG_INFO("exit check");
	return arg;
}

void init_daemon(void)
{
	unsigned int i;
	pid_t pid;
	struct rlimit rl;
	struct sigaction sa;
	//printf("------------------------------\n");
	//umask(0);
	if(getrlimit(RLIMIT_NOFILE,&rl)<0){
		exit(1);
	}


	if((pid=fork())<0){
		exit(1);//fork失败，退出
	}else if(pid> 0){
		exit(0);//是父进程，结束父进程
	}

	//是第一子进程，后台继续执行
	setsid();//第一子进程成为新的会话组长和进程组长
	//并与控制终端分离
	sa.sa_handler=SIG_IGN;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags=0;

	if(sigaction(SIGHUP,&sa,NULL)<0){
		exit(1);
	}

	if((pid=fork())<0){
		exit(1);//fork失败，退出
	}else if(pid> 0){
		exit(0);//是父进程，结束父进程
	}

	//是第二子进程，继续
	//第二子进程不再是会话组长
	umask(0);
	if (rl.rlim_max==RLIM_INFINITY){
		rl.rlim_max=1024;
	}

	for(i=0;i< rl.rlim_max;++i)//关闭打开的文件描述符
	{
		close(i);
	}

	//chdir("/tmp");//改变工作目录到/tmp
	return;
}


int main()
{
	//init_daemon();//守护进程初始化函数
	
	LOG_INIT(LOG_FILE_NAME, LOG_BUF_SIZE, LOG_SPLIT_SIZE, LOG_MAX_QUEUE_SIZE);
	
	signal(SIGHUP, SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);
	signal(SIGTTIN, SIG_IGN);
	//setsid();
	init();
	check(NULL);
	LOG_INFO("leaving main()");
	return 0;
}
