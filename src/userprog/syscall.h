#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

void sys_exit (int); /* needed by other handlers, e.g. page fault */

#endif /* userprog/syscall.h */
