#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

struct lock *filesys_lock;
void check_address (int *addr);
void syscall_init (void);
void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void syscall_halt (void);
void syscall_exit (int status);
int syscall_fork (const char *thread_name);
bool syscall_create (const char *file_name, unsigned size);
bool syscall_remove(const char *file_name);
int syscall_filesize (int fd);
void syscall_seek(int fd, unsigned position);
unsigned syscall_tell(int fd);
int syscall_open (const char *file);
int syscall_read (int fd, const void *buffer, unsigned size);
int syscall_write (int fd, const void *buffer, unsigned size);
void syscall_close (int fd);

int process_add_file(struct file *f);
struct file *process_get_file (int fd);


#endif /* userprog/syscall.h */
