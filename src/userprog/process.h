#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

typedef int pid_t;

#define PID_ERROR ((pid_t) -1)
#define PID_INIT ((pid_t) -2)

/* pcb struct */
struct process {
  pid_t pid;                      /* process id */
  const char* cmdline;            /* cmdline of process bein executed */

  struct thread *parent_thread;   /* parent thread */    
  bool waiting;                                   
  bool exited;
  bool orphan;                    /* parent thread has terminated before */
  int32_t exitcode;               /* exitcode passed through exit ()*/
  
  struct semaphore sema_init;     /* sema between start_process () and process_execute ()*/
  struct semaphore sema_wait;     /* sema for wait () */
};

struct file_desc {
  struct list_elem elem;
  int id;
  struct file* file;
  struct dir* dir;
};

tid_t process_execute (const char *file_name);
int process_wait (pid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
