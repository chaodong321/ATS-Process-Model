#ifndef LOCK_AND_KILL_H
#define LOCK_AND_KILL_H
#include <sys/types.h>
#include <string.h>
#define PATH_NAME_MAX 128

/*-------------------------------------------------------------------------
   ink_killall
   - Sends signal 'sig' to all processes with the name 'pname'
   - Returns: -1 error
			   0 okay
  -------------------------------------------------------------------------*/
int ink_killall(const char *pname, int sig);

/*-------------------------------------------------------------------------
   ink_killall_get_pidv_xmalloc
   - Get all pid's named 'pname' and stores into ats_malloc'd
	 pid_t array, 'pidv'
   - Returns: -1 error (pidv: set to NULL; pidvcnt: set to 0)
			   0 okay (pidv: ats_malloc'd pid vector; pidvcnt: number of pid's;
				   if pidvcnt is set to 0, then pidv will be set to NULL)
  -------------------------------------------------------------------------*/
int ink_killall_get_pidv_xmalloc(const char *pname, pid_t ** pidv, int *pidvcnt);

/*-------------------------------------------------------------------------
   ink_killall_kill_pidv
   - Kills all pid's in 'pidv' with signal 'sig'
   - Returns: -1 error
			   0 okay
  -------------------------------------------------------------------------*/
int ink_killall_kill_pidv(pid_t * pidv, int pidvcnt, int sig);



class Lockfile
{
public:

	Lockfile(void):fd(0)
	{
		fname[0] = '\0';
	}


	// coverity[uninit_member]
	Lockfile(const char *filename):fd(0)
	{
		strcpy(fname, filename);
	}


	~Lockfile(void)
	{
	}

	void SetLockfileName(const char *filename)
	{
		strcpy(fname, filename);
	}

	const char *GetLockfileName(void)
	{
		return fname;
	}

	// Open() -----非常重要的函数
	//
	// Tries to open a lock file, returning:
	//	 -errno on error
	//	 0 if someone is holding the lock (with holding_pid set)
	//	 1 if we now have a writable lock file
	int Open(pid_t * holding_pid);

	// Get()
	//
	// Gets write access to a lock file, and if successful, truncates
	// file, and writes the current process ID.  Returns:
	//	 -errno on error
	//	 0 if someone is holding the lock (with holding_pid set)
	//	 1 if we now have a writable lock file
	int Get(pid_t * holding_pid);

	// Close()
	//
	// Closes the file handle on the opened Lockfile.
	void Close(void);

	// Kill()
	// KillGroup()
	//
	// Ensures no one is holding the lock. It tries to open the lock file
	// and if that does not succeed, it kills the process holding the lock.
	// If the lock file open succeeds, it closes the lock file releasing
	// the lock.
	//
	// The intial signal can be used to generate a core from the process while
	// still ensuring it dies.
	void Kill(int sig, int initial_sig = 0, const char *pname = NULL);
	void KillGroup(int sig, int initial_sig = 0, const char *pname = NULL);

private:
	char fname[PATH_NAME_MAX];
	int fd;
};

#endif
