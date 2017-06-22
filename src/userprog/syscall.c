#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

/* mem access helper functions */
static int get_user (const uint8_t *);
static bool put_user (uint8_t *udst, uint8_t byte);

/* syscall helper functions */
static void syscall_handler (struct intr_frame *);
static void sys_halt (void);
/*static int sys_exit (int); */ /* extern funtion */
static pid_t sys_exec (const char *cmdline);
static int sys_wait (pid_t pid);
static bool sys_create(const char* filename, unsigned initial_size);
static bool sys_remove(const char* filename);
static int sys_open(const char* file);
static int sys_filesize(int fd);
static int sys_read(int fd, void *buffer, unsigned size);
static int sys_write(int fd, const void *buffer, unsigned size);
static void sys_seek(int fd, unsigned position);
static unsigned sys_tell(int fd);
static void sys_close(int fd);
  
/* memeory access helper functions */
/* Reads a byte at user virtual address UADDR.
 * UADDR must be below PHYS_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
 * UDST must be below PHYS_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}




void
syscall_init (void) 
{
  /*TODO need to init lock as well here*/
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");

  switch (syscall_num)
  case SYS_HALT:                   /* Halt the operating system. */
    sys_halt
  case SYS_EXIT:                   /* Terminate this process. */
    sys_exit
  case SYS_EXEC:                   /* Start another process. */
    sys_exec
  case SYS_WAIT:                   /* Wait for a child process to die. */
    sys_wait
  case SYS_CREATE:                 /* Create a file. */
    sys_create
  case SYS_REMOVE:                 /* Delete a file. */
    sys_remove
  case SYS_OPEN:                   /* Open a file. */
    sys_open
  case SYS_FILESIZE:               /* Obtain a file's size. */
    sys_filesize
  case SYS_READ:                   /* Read from a file. */
    sys_read
  case SYS_WRITE:                  /* Write to a file. */
    sys_write
  case SYS_SEEK:                   /* Change position in a file. */
    sys_seek
  case SYS_TELL:                   /* Report current position in a file. */
    sys_tell
  case SYS_CLOSE:                  /* Close a file. */
    sys_close
  default:


}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  thread_exit ();
}
