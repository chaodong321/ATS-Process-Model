#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/file.h>
#include <errno.h>
#include <signal.h>

#include "lock_and_kill.h"


#define PROC_BASE "/proc"
#define INITIAL_PIDVSIZE 32
#define LOCKFILE_BUF_LEN 16
#define LINE_MAX 1024 //may be hava problem with it
int
ink_killall(const char *pname, int sig)
{
	int err;
	pid_t *pidv;
	int pidvcnt;

	if (ink_killall_get_pidv_xmalloc(pname, &pidv, &pidvcnt) < 0) {
		return -1;
	}

	if (pidvcnt == 0) {
		free(pidv);
		return 0;
	}

	err = ink_killall_kill_pidv(pidv, pidvcnt, sig);
	free(pidv);

	return err;
}

int
ink_killall_get_pidv_xmalloc(const char *pname, pid_t ** pidv, int *pidvcnt)
{
  DIR *dir;
  FILE *fp;
  struct dirent *de;
  pid_t pid, self;
  char buf[LINE_MAX], *p, *comm;
  int pidvsize = INITIAL_PIDVSIZE;

  if (!pname || !pidv || !pidvcnt)
	goto l_error;

  self = getpid();
  if (!(dir = opendir(PROC_BASE)))
	goto l_error;

  *pidvcnt = 0;
  *pidv = (pid_t *)malloc(pidvsize * sizeof(pid_t));

  while ((de = readdir(dir))) {
	if (!(pid = (pid_t) atoi(de->d_name)) || pid == self)
	  continue;
	snprintf(buf, sizeof(buf), PROC_BASE "/%d/stat", pid);
	if ((fp = fopen(buf, "r"))) {
	  if (fgets(buf, sizeof buf, fp) == 0)
		goto l_close;
	  if ((p = strchr(buf, '('))) {
		comm = p + 1;
		if ((p = strchr(comm, ')')))
		  *p = '\0';
		else
		  goto l_close;
		if (strcmp(comm, pname) == 0) {
		  if (*pidvcnt >= pidvsize) {
			pid_t *pidv_realloc;
			pidvsize *= 2;
			if (!(pidv_realloc = (pid_t *)realloc(*pidv, pidvsize * sizeof(pid_t)))) {
			  free(*pidv);
			  goto l_error;
			} else {
			  *pidv = pidv_realloc;
			}
		  }
		  (*pidv)[*pidvcnt] = pid;
		  (*pidvcnt)++;
		}
	  }
	l_close:
	  fclose(fp);
	}
  }
  closedir(dir);

  if (*pidvcnt == 0) {
	free(*pidv);
	*pidv = 0;
  }
  return 0;
l_error:
  *pidv = NULL;
  *pidvcnt = 0;
  return -1;
}

int
ink_killall_kill_pidv(pid_t * pidv, int pidvcnt, int sig)
{
	int err = 0;
	if (!pidv || (pidvcnt <= 0))
		return -1;

	while (pidvcnt > 0) {
		pidvcnt--;
		if (kill(pidv[pidvcnt], sig) < 0)
			err = -1;
	}
	return err;
}


////////////////////类函数的实现在下面//////////////////////////////////
////////////////////////////////////////////////////////////////////////
int
Lockfile::Open(pid_t * holding_pid)
{
  char buf[LOCKFILE_BUF_LEN];
  pid_t val;
  int err;
  *holding_pid = 0;

#define FAIL(x) \
{ \
  if (fd > 0) \
	close (fd); \
  return (x); \
}

  struct flock lock;
  char *t;
  int size;//开始的时候设置成无效的一个值

  // Try and open the Lockfile. Create it if it does not already
  // exist.
  do {
	fd = open(fname, O_RDWR | O_CREAT, 0644);
  } while ((fd < 0) && (errno == EINTR));

  if (fd < 0)
	return (-errno);

  // Lock it. Note that if we can't get the lock EAGAIN will be the
  // error we receive.
  lock.l_type = F_WRLCK;
  lock.l_start = 0;
  lock.l_whence = SEEK_SET;
  lock.l_len = 0;

  do {
	err = fcntl(fd, F_SETLK, &lock);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
	// We couldn't get the lock. Try and read the process id of the
	// process holding the lock from the lockfile.
	t = buf;

	for (size = 15; size > 0;) {
	  do {
		err = read(fd, t, size);
	  } while ((err < 0) && (errno == EINTR));

	  if (err < 0)
		FAIL(-errno);
	  if (err == 0)
		break;

	  size -= err;
	  t += err;
	}
	*t = '\0';

	// coverity[secure_coding]
	if (sscanf(buf, "%d\n", (int*)&val) != 1) {
	  *holding_pid = 0;
	} else {
	  *holding_pid = val;
	}
	FAIL(0);

  }
  // If we did get the lock, then set the close on exec flag so that
  // we don't accidently pass the file descriptor to a child process
  // when we do a fork/exec.
  do {
	err = fcntl(fd, F_GETFD, 0);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0)
	FAIL(-errno);

  val = err | FD_CLOEXEC;

  do {
	err = fcntl(fd, F_SETFD, val);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0)
	FAIL(-errno);

  // Return the file descriptor of the opened lockfile. When this file
  // descriptor is closed the lock will be released.
  return (1);					// success
#undef FAIL
}

int
Lockfile::Get(pid_t * holding_pid)
{
  char buf[LOCKFILE_BUF_LEN];
  int err;
  *holding_pid = 0;

  fd = -1;

  // Open the Lockfile and get the lock. If we are successful, the
  // return value will be the file descriptor of the opened Lockfile.
  err = Open(holding_pid);
  if (err != 1)
	return err;

  if (fd < 0) {
	return -1;
  }

  // Truncate the Lockfile effectively erasing it.
  do {
	err = ftruncate(fd, 0);
  } while ((err < 0) && (errno == EINTR));

  if (err < 0) {
	close(fd);
	return (-errno);
  }

  // Write our process id to the Lockfile.
  snprintf(buf, sizeof(buf), "%d\n", (int) getpid());

  do {
	err = write(fd, buf, strlen(buf));
  } while ((err < 0) && (errno == EINTR));

  if (err != (int) strlen(buf)) {
	close(fd);
	return (-errno);
  }
  return (1);					// success
}

void
Lockfile::Close(void)
{
  if (fd != -1) {
	close(fd);
  }
}

//-------------------------------------------------------------------------
// Lockfile::Kill() and Lockfile::KillAll()
//
// Open the lockfile. If we succeed it means there was no process
// holding the lock. We'll just close the file and release the lock
// in that case. If we don't succeed in getting the lock, the
// process id of the process holding the lock is returned. We
// repeatedly send the KILL signal to that process until doing so
// fails. That is, until kill says that the process id is no longer
// valid (we killed the process), or that we don't have permission
// to send a signal to that process id (the process holding the lock
// is dead and a new process has replaced it).
//
// INKqa11325 (Kevlar: linux machine hosed up if specific threads
// killed): Unfortunately, it's possible on Linux that the main PID of
// the process has been successfully killed (and is waiting to be
// reaped while in a defunct state), while some of the other threads
// of the process just don't want to go away.  Integrate ink_killall
// into Kill() and KillAll() just to make sure we really kill
// everything and so that we don't spin hard while trying to kill a
// defunct process.
//-------------------------------------------------------------------------


static void
lockfile_kill_internal(pid_t init_pid, int init_sig, pid_t pid, const char *pname, int sig)
{
  int err;

#if defined(linux)

  pid_t *pidv;
  int pidvcnt;

  // Need to grab pname's pid vector before we issue any kill signals.
  // Specifically, this prevents the race-condition in which
  // traffic_manager spawns a new traffic_server while we still think
  // we're killall'ing the old traffic_server.
  if (pname) {
	  //这函数的功能是什么，将程序名为pname的进程都不给杀死，pidv是pid的数组指针，pidvcnt是进程个数
	ink_killall_get_pidv_xmalloc(pname, &pidv, &pidvcnt);
  }

  if (init_sig > 0) {
	kill(init_pid, init_sig);
	// sleep for a bit and give time for the first signal to be
	// delivered
	sleep(1);
  }

  do {
	if ((err = kill(pid, sig)) == 0) {
	  sleep(1);
	}
	if (pname && (pidvcnt > 0)) {
	  ink_killall_kill_pidv(pidv, pidvcnt, sig);
	  sleep(1);
	}
  } while ((err == 0) || ((err < 0) && (errno == EINTR)));

  free(pidv);

#else

  if (init_sig > 0) {
	kill(init_pid, init_sig);
	// sleep for a bit and give time for the first signal to be
	// delivered
	sleep(1);
  }

  do {
	err = kill(pid, sig);
  } while ((err == 0) || ((err < 0) && (errno == EINTR)));

#endif	// linux check

}

/////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////
void
Lockfile::Kill(int sig, int initial_sig, const char *pname)
{
  int err;
  int pid;
  pid_t holding_pid;

  err = Open(&holding_pid);
  if (err == 1) 				// success getting the lock file,说明没有对应的server进程存在
  {
	Close();					//因此不需要处理，关闭就行了
  } else if (err == 0)			// someone else has the lock
  {
	pid = holding_pid;			//获取持有锁进程的pid
	if (pid != 0) { 			//当进程pid有效的时候，就去杀死这个进程

	  lockfile_kill_internal(pid, initial_sig, pid, pname, sig);
	}
  }
}


/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
//没怎么明白这个函数!!
void
Lockfile::KillGroup(int sig, int initial_sig, const char *pname)
{
  int err;
  pid_t pid;
  pid_t holding_pid;

  err = Open(&holding_pid);
  if (err == 1) 				// success getting the lock file
  {
	Close();
  } else if (err == 0)			// someone else has the lock
  {
	do {
	  pid = getpgid(holding_pid);//获得进程组识别码
	} while ((pid < 0) && (errno == EINTR));

	if ((pid < 0) || (pid == getpid()))
	  pid = holding_pid;
	else
	  pid = -pid;

	if (pid != 0) {
	  // We kill the holding_pid instead of the process_group
	  // initially since there is no point trying to get core files
	  // from a group since the core file of one overwrites the core
	  // file of another one
	  lockfile_kill_internal(holding_pid, initial_sig, pid, pname, sig);
	}
  }
}
