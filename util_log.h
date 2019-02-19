#ifndef __UTIL_LOG_H_
#define __UTIL_LOG_H_
 
#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>
#include <sys/prctl.h>
#include "util_blockqueue.h"

using namespace std;

#define FLUSH_LOG_THREAD_NAME "flush_log"

class Log
{
    public:
        static Log* get_instance()
        {
            static Log instance;
            return &instance;
        }

        static void *flush_log_thread(void* args)
        {
            prctl(PR_SET_NAME, FLUSH_LOG_THREAD_NAME);
            Log::get_instance()->async_write_log();
			return NULL;
        }
 
        bool init(const char* file_name, int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
 
        void write_log(int level,    const char* file, const char* func, int line, const char* format, ...);
 
        void flush(void);
 
    private:
        Log();
        virtual ~Log();
        void async_write_log()
        {
            string single_log;
            while(m_log_queue->pop(single_log))
            {
                pthread_mutex_lock(m_mutex);
                printf("%s", single_log.c_str());
#if 1
                if (fprintf(m_fp, "%s", single_log.c_str()) < 0)
                    printf("Error writing to stream using fprintf\n");

                fflush(m_fp);
#else
                fputs(single_log.c_str(), m_fp);
#endif
                pthread_mutex_unlock(m_mutex);
            }
        }
 
    private:
        pthread_mutex_t *m_mutex;
        char dir_name[256];
        char log_name[256];
        int m_split_lines;
        int m_log_buf_size;
        long long  m_count;
        int m_today;
        FILE *m_fp;
        char *m_buf;
        block_queue<string> *m_log_queue;
        bool m_is_async;
};

#define LOG_INIT(file_name, log_buf_size, split_lines, max_queue_size) Log::get_instance()->init(file_name, log_buf_size, split_lines, max_queue_size)
#define LOG_FLUSH() Log::get_instance()->flush()

#define LOG(level, format, ...) Log::get_instance()->write_log(level, __FILE__, __FUNCTION__, __LINE__, format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...) LOG(0, format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) LOG(1, format, ##__VA_ARGS__)
#define LOG_WARN(format, ...) LOG(2, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG(3, format, ##__VA_ARGS__)

#endif

