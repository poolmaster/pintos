#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
#include "devices/shutdown.h"

/* mem access helper functions */
static void check_user (const uint8_t *);
static int get_user (const uint8_t *);
static bool put_user (uint8_t *udst, uint8_t byte);
static int memread_user (void *src, void *dst, size_t bytes); 
static void fail_invalid_access (void);

/* syscall helper functions */
static void syscall_handler (struct intr_frame *);
static void sys_halt (void);
/*void sys_exit (int); */ /* extern funtion */
static pid_t sys_exec (const char *cmdline);
static int sys_wait (pid_t pid);
static struct file_desc *sys_find_fd (int fd);
static struct file *sys_find_file (int fd);
static bool sys_create(const char* filename, unsigned initial_size);
static bool sys_remove(const char* filename);
static int sys_open(const char* filename);
static int sys_filesize(int fd);
static int sys_read(int fd, void *buffer, unsigned size);
static int sys_write(int fd, const void *buffer, unsigned size);
static void sys_seek(int fd, unsigned position);
static unsigned sys_tell(int fd);
static void sys_close(int fd);

struct lock filesys_lock; /* file system has no internal synch for now */
  
/* memeory access helper functions */
/* Reads a byte at user virtual address UADDR.
 * UADDR must be below PHYS_BASE.
 * Returns the byte value if successful, -1 if a segfault
 * occurred. */

static void
check_user (const uint8_t *uaddr) {/*{{{*/
  if ((void *)uaddr < PHYS_BASE) {
    fail_invalid_access ();
  }
}/*}}}*/

static int
get_user (const uint8_t *uaddr) {/*{{{*/
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
       : "=&a" (result) : "m" (*uaddr));
  return result;
}/*}}}*/
 
/* Writes BYTE to user address UDST.
 * UDST must be below PHYS_BASE.
 * Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte) {/*{{{*/
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
  return error_code != -1;
}/*}}}*/

static int 
memread_user (void *src, void *dst, size_t bytes) {/*{{{*/
  int32_t val;
  for (size_t i = 0; i < bytes; i++) {
    check_user (src + i);
    val = get_user (src + i);  
    *( (char *)(dst + i) ) = val & 0xff;
  }
  return bytes;
}/*}}}*/

static void 
fail_invalid_access (void) {/*{{{*/
  if (lock_held_by_current_thread (&filesys_lock)) {
    lock_release (&filesys_lock);
  }
  sys_exit (-1);
  NOT_REACHED ();
}/*}}}*/

/* syscall functions */
static void
sys_halt (void) {/*{{{*/
  shutdown_power_off ();
}/*}}}*/

void 
sys_exit (int status) {/*{{{*/
  /* cleaning up are through process_exit () in thread_exit ()*/
  /* including close files, handle child processes, semaphore up */
  struct thread *cur_t = thread_current ();      
  /* user program should have proc not freed at this point */
  ASSERT (cur_t->proc != NULL);
  (cur_t->proc)->exitcode = status;
  
  thread_exit ();  
}/*}}}*/

static pid_t 
sys_exec (const char *cmdline) {/*{{{*/
  /*cmdline passed in is an address in user memory
   *need to check */
  check_user ((const uint8_t *) cmdline);

  lock_acquire (&filesys_lock); /* load uses file system */
  pid_t pid = process_execute (cmdline);  
  lock_release (&filesys_lock);

  return pid;
}/*}}}*/

static int 
sys_wait (pid_t pid) {/*{{{*/
 return process_wait (pid);
}/*}}}*/

static bool 
sys_create(const char* filename, unsigned initial_size) {/*{{{*/
  check_user ((const uint8_t *) filename);

  lock_acquire (&filesys_lock);
  bool success = filesys_create (filename, initial_size);
  lock_release (&filesys_lock);

  return success;
}/*}}}*/

static bool 
sys_remove(const char* filename) {/*{{{*/
  check_user ((const uint8_t *) filename);

  lock_acquire (&filesys_lock);
  bool success = filesys_remove (filename);
  lock_release (&filesys_lock);

  return success;
}/*}}}*/

static int 
sys_open(const char* filename) {/*{{{*/
  check_user ((const uint8_t *) filename);

  struct file *fp = NULL;
  struct file_desc *fd = palloc_get_page (0);

  if (!fd) {
    return -1;
  }

  lock_acquire (&filesys_lock);
  fp = filesys_open (filename);
  if (!fp) {
    palloc_free_page (fd);
    lock_release (&filesys_lock);
    return -1;
  }

  fd->file = fp;
  fd->dir = NULL; /*TODO: file system improvement */

  struct list *fd_list = &(thread_current ()->fd_list);
  if (list_empty (fd_list)) {
    fd->id = 3; /* 0, 1, 2 are reserved */
  } else {
    fd->id = (list_entry (list_back (fd_list), struct file_desc, elem)->id) + 1;
  }
  list_push_back (fd_list, &fd->elem);

  lock_release (&filesys_lock);
  return fd->id;  
}/*}}}*/

static struct file_desc *
sys_find_fd (int fd) {/*{{{*/
  struct list *fd_list = &(thread_current ()->fd_list);
  for (struct list_elem *e = list_begin (fd_list);
       e != list_end (fd_list); e = list_next (e)) {
    struct file_desc *descriptor = list_entry (e, struct file_desc, elem);
    if (fd != descriptor->id) continue;
    return descriptor;  
  }
  return NULL;
}/*}}}*/

static struct file *
sys_find_file (int fd) {/*{{{*/
  struct file_desc *descriptor = sys_find_fd (fd);
  if (!descriptor) {
    return descriptor->file;
  }
  return NULL;
}/*}}}*/

static int 
sys_filesize(int fd) {/*{{{*/
  struct file *file = sys_find_file (fd);
  
  if (!file) {
    return -1;
  }
  lock_acquire (&filesys_lock); 
  int size = file_length (file);
  lock_release (&filesys_lock); 

  return size;
}/*}}}*/

static int 
sys_read(int fd, void *buffer, unsigned size) {/*{{{*/
  check_user ((const uint8_t *) buffer);
  check_user ((const uint8_t *) buffer + size - 1);

  lock_acquire (&filesys_lock);
  if (fd == 0) {
    for (unsigned i = 0; i < size; i++) {
      if (!put_user (buffer + i, input_getc ())) {
        lock_release (&filesys_lock);
        sys_exit (-1); //fault
      }
    }
    return size;
  }
  struct file *file = sys_find_file (fd);
  int size_read = -1; 
  if (file) {
    size_read = file_read (file, buffer, size);
  }
  lock_release (&filesys_lock);
  return size_read;
}/*}}}*/

static int 
sys_write(int fd, const void *buffer, unsigned size) {/*{{{*/
  check_user ((const uint8_t *) buffer);
  check_user ((const uint8_t *) buffer + size - 1);

  lock_acquire (&filesys_lock);
  if (fd == 1) {
    putbuf (buffer, size);  
    return size;
  }
  struct file *file = sys_find_file (fd);
  int size_write = -1; 
  if (file) {
    size_write = file_write (file, buffer, size);
  }
  lock_release (&filesys_lock);
  return size_write;
}/*}}}*/

static void 
sys_seek(int fd, unsigned position) {/*{{{*/
  lock_acquire (&filesys_lock);  
  struct file *file = sys_find_file (fd); 

  if (file) {
    file_seek (file, position);
  }
  /* else error handling ? */
  lock_release (&filesys_lock);  
}/*}}}*/

static unsigned 
sys_tell(int fd) {/*{{{*/
  lock_acquire (&filesys_lock);  
  struct file *file = sys_find_file (fd); 
  unsigned pos = 0;

  if (file) {
    pos = file_tell (file);
  }
  /* else error handling ? */
  lock_release (&filesys_lock);  
  return pos;
}/*}}}*/

static void 
sys_close(int fd) {/*{{{*/
  lock_acquire (&filesys_lock);  
  struct file_desc *desc = sys_find_fd (fd); 
  
  if (desc && desc->file) {
    file_close (desc->file);
    /*TODO: handle directory */
    list_remove (&desc->elem);
    palloc_free_page (desc);
  }
  lock_release (&filesys_lock);
}/*}}}*/

void
syscall_init (void) 
{/*{{{*/
  lock_init (&filesys_lock);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}/*}}}*/

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  printf ("system call!\n");
  int syscall_num;
  ASSERT (sizeof(syscall_num) == 4);

  // The system call number is in the 32-bit word at the caller's stack pointer.
  memread_user(f->esp, &syscall_num, sizeof(syscall_num));

  switch (syscall_num) {
  case SYS_HALT:                   /* Halt the operating system. */
  {
    sys_halt ();
    NOT_REACHED ();
    break;
  }
  case SYS_EXIT:                   /* Terminate this process. */
  {
    int status;
    memread_user (f->esp + 4, &status, sizeof(status));
    sys_exit (status);
    NOT_REACHED ();
    break;
  }
  case SYS_EXEC:                   /* Start another process. */
  {
    char *cmdline;
    memread_user (f->esp + 4, &cmdline, sizeof(cmdline));
    pid_t ret = sys_exec ((const char *)cmdline);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_WAIT:                   /* Wait for a child process to die. */
  {
    pid_t pid;
    memread_user (f->esp + 4, &pid, sizeof(pid));
    int ret = sys_wait (pid);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_CREATE:                 /* Create a file. */
  {
    char *fn;
    unsigned size;
    memread_user (f->esp + 4, &fn, sizeof(fn));
    memread_user (f->esp + 8, &size, sizeof(size));
    bool ret = sys_create (fn, size);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_REMOVE:                 /* Delete a file. */
  {
    char *fn;
    memread_user (f->esp + 4, &fn, sizeof(fn));
    bool ret = sys_remove (fn);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_OPEN:                   /* Open a file. */
  {
    char *fn;
    memread_user (f->esp + 4, &fn, sizeof(fn));
    int ret = sys_open (fn);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_FILESIZE:               /* Obtain a file's size. */
  {
    int fd;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    int ret = sys_filesize (fd);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_READ:                   /* Read from a file. */
  {
    int fd;
    void *buffer;
    unsigned size;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    memread_user (f->esp + 8, &buffer, sizeof(buffer));
    memread_user (f->esp + 12, &size, sizeof(size));
    int ret = sys_read (fd, buffer, size);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_WRITE:                  /* Write to a file. */
  {
    int fd;
    void *buffer;
    unsigned size;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    memread_user (f->esp + 8, &buffer, sizeof(buffer));
    memread_user (f->esp + 12, &size, sizeof(size));
    int ret = sys_write (fd, buffer, size);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_SEEK:                   /* Change position in a file. */
  {
    int fd;
    unsigned pos;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    memread_user (f->esp + 8, &pos, sizeof(pos));
    sys_seek (fd, pos);
    break;
  }
  case SYS_TELL:                   /* Report current position in a file. */
  {
    int fd;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    unsigned ret = sys_tell (fd);
    f->eax = (uint32_t) ret;
    break;
  }
  case SYS_CLOSE:                  /* Close a file. */
  {
    int fd;
    memread_user (f->esp + 4, &fd, sizeof(fd));
    sys_close (fd);
    break;
  }
  default:
    printf ("[ERROR]: unimplemented system call: syscall_num=%0d\n", syscall_num);
    sys_exit (-1);
    break;
  }
}
