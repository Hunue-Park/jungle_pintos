#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/flags.h"
#include "threads/vaddr.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include <stdio.h>
#include <syscall-nr.h>
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* project 2 - system call*/
void check_address(const uint64_t *addr);
void halt(void);
void exit(int status);
tid_t fork(const char *, struct intr_frame *);
int exec(const char *file);
int wait(tid_t);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned length);
int write(int fd, const void *buffer, unsigned length);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);

int dup2(int oldfd, int newfd);

int process_add_file(struct file *);
struct file *process_get_file(int);
void process_close_file(int);

const int STDIN = 1;
const int STDOUT = 2;


/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);

	lock_init(&filesys_lock);
}

/* project 2 - system call*/
/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch(f->R.rax)
	{
		case SYS_HALT:
			halt();
			break;

		case SYS_EXIT:
			exit(f->R.rdi);
			break;

		case SYS_FORK:
			f->R.rax = fork(f->R.rdi, f);
			break;

		case SYS_EXEC:
			if (exec(f->R.rdi) == -1)
			{
				exit(-1);
			}
			break;

		case SYS_WAIT:
			f->R.rax = wait(f->R.rdi);
			break;

		case SYS_CREATE:
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;

		case SYS_REMOVE:
			f->R.rax = remove(f->R.rdi);
			break;

		case SYS_OPEN:
			f->R.rax = open(f->R.rdi);
			break;

		case SYS_FILESIZE:
			f->R.rax = filesize(f->R.rdi);
			break;

		case SYS_READ:
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_WRITE:
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;

		case SYS_SEEK:
			seek(f->R.rdi, f->R.rsi);
			break;
			
		case SYS_TELL:
			f->R.rax = tell(f->R.rdi);
			break;

		case SYS_DUP2:
			f->R.rax = dup2(f->R.rdi, f->R.rsi);
			break;

		case SYS_CLOSE:
			close(f->R.rdi);
			break;

		default:
			exit(-1);
			break;
	}
	thread_exit ();
}


/* project 2 - system call*/
void check_address(const uint64_t *uaddr)
{
	if(uaddr == NULL || !(is_user_vaddr(uaddr)) ||  pml4_get_page(thread_current()->pml4, uaddr) == NULL)
	{
		exit(-1);
	}
}

void halt(void)
{
	power_off();
}

void exit(int status)
{
	struct thread *t = thread_current();
	t->exit_status = status;
	// printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

tid_t fork(const char *thread_name, struct intr_frame *if_)
{
	return process_fork(thread_name, if_);
}

int exec(const char *cmd_line)
{
	struct thread *curr = thread_current();
	check_address(cmd_line);

	int size = strlen(cmd_line) + 1;
	char *fn_copy = palloc_get_page(PAL_ZERO);

	if (fn_copy == NULL)
	{
		exit(-1);
	}
	strlcpy(fn_copy, cmd_line, size);

	if(process_exec(fn_copy) == -1)
	{
		return -1;
	}

	NOT_REACHED();
	return 0;
}

int wait(tid_t pid)
{
	process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
	check_address(file);
	return filesys_create(file, initial_size);
}

bool remove(const char *file)
{
	check_address(file);
	return filesys_remove(file);
}

int open(const char *file)
{
	check_address(file);
	struct file *file_obj = filesys_open(file);

	if (file_obj == NULL)
	{
		return -1;
	}

	int fd = process_add_file(file_obj);
	if (fd == -1)
	{
		file_close(file_obj);
	}

	return fd;
}

int filesize(int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{
		return -1;
	}

	return file_length(file_obj);
}

int read(int fd, void *buffer, unsigned size)
{
	check_address(buffer);
	int ret;
	struct thread *curr = thread_current();

	struct file *file_obj = process_get_file(fd);
	if (file_obj == NULL)
	{
		return -1;
	}

	if (file_obj == 1)
	{
		// stdin, 왜 1이지?
		// fd table 초기화할떄, fd[0]= 1(stdin), fd[1]= 2(stdout)
		if(curr->stdin_count == 0)
		{
			NOT_REACHED();
		}

		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++)
		{
			char c = input_getc();
			*buf++ = c;
			if (c == '\0')
			{
				break;
			}
		}
		return i;
	}
	else
	{
		if (file_obj == 2)
		{
			return -1;
		}

		lock_acquire(&filesys_lock);
		ret = file_read(file_obj, buffer, size);
		lock_release(&filesys_lock);

		return ret;
	}
}

int write(int fd, const void *buffer, unsigned size)
{
	check_address(buffer);
	int ret;
	struct thread *curr = thread_current();

	struct file *file_obj = process_get_file(fd);
	if(file_obj == NULL)
	{
		return -1;
	}

	if (file_obj == 2)
	{
		// stdout
		if(curr->stdout_count == 0)
		{
			NOT_REACHED();
			process_close_file(fd);
			return -1;
		}

		putbuf(buffer, size);
		return size;
	}
	else
	{
		if(file_obj == 1)
		{
			return -1;
		}

		lock_acquire(&filesys_lock);
		ret = file_write(file_obj, buffer, size);
		lock_release(&filesys_lock);
		
		return ret;
	}
}

void seek(int fd, unsigned position)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj <= 2)
	{
		return ;
	}

	file_obj->pos = position;
}

unsigned tell(int fd)
{
	struct file *file_obj = process_get_file(fd);
	if (file_obj<=2)
	{
		return ;
	}

	return file_tell(file_obj);
}

void close(int fd)
{
	struct file *file_obj = process_get_file(fd);
	if(file_obj == NULL)
	{
		return ;
	}

	struct thread *curr = thread_current();

	if (file_obj == 1 || fd == 0)
	{
		curr->stdin_count --;
	}
	else if (file_obj == 2 || fd == 1)
	{
		curr->stdout_count --;
	}

	if (fd <=1 || file_obj <= 2)
	{
		return;
	}

	process_close_file(fd);

	if (file_obj->dup_count == 0)
	{
		file_close(file_obj);
	}
	else
	{
		file_obj->dup_count --;
	}
}

int dup2(int oldfd, int newfd)
{
	struct file *old_file = process_get_file(oldfd);
	if (old_file == NULL)
	{
		return -1;
	}

	struct file *new_file = process_get_file(newfd);
	if(oldfd == newfd)
	{
		return newfd;
	}

	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	if(old_file == 1)
	{
		curr->stdin_count ++;
	}
	else if(old_file == 2)
	{
		curr->stdout_count ++;
	}
	else
	{
		old_file->dup_count ++;
	}

	close(newfd);
	fdt[newfd] = old_file;
	return newfd;
}

int process_add_file(struct file *f)
{
	struct thread *curr = thread_current();
	struct file **fdt = curr->fd_table;

	while (curr->fd_idx < FDCOUNT_LIMIT && fdt[curr->fd_idx])
	{
		curr->fd_idx++;
	}

	// full case
	if (curr->fd_idx >= FDCOUNT_LIMIT)
	{
		return -1;
	}

	fdt[curr->fd_idx] = f;
	return curr->fd_idx;
}

struct file *process_get_file(int fd)
{
	struct thread *curr = thread_current();
	if(fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return NULL;
	}

	return curr->fd_table[fd];
}

void process_close_file(int fd)
{
	struct thread *curr = thread_current();
		if(fd < 0 || fd >= FDCOUNT_LIMIT)
	{
		return ;
	}

	curr->fd_table[fd] = NULL;
}