#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "threads/palloc.h"
#include "threads/flags.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

/*----------------3. virtual memory : memory management----------------------*/
#include "include/vm/vm.h"
#include "include/vm/file.h"
/*----------------3. virtual memory : memory management----------------------*/

const int STDIN = 1;
const int STDOUT = 2;
void syscall_entry (void);
void syscall_handler (struct intr_frame *);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
static struct file *find_file_by_fd(int fd);

/*----------------3. virtual memory : memory management----------------------*/
struct page* check_address(void *addr);
void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write);

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap(void *addr);
/*----------------3. virtual memory : memory management----------------------*/

void halt(void);
void exit(int status);
tid_t fork(const char *thread_name, struct intr_frame *f);
bool create(const char *file, unsigned initial_size);
bool remove(const char *file);
int open(const char *file);
int filesize(int fd);
int read(int fd, void *buffer, unsigned size);
int write(int fd, const void *buffer, unsigned size);
void seek(int fd, unsigned position);
unsigned tell(int fd);
void close(int fd);
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

struct lock filesys_lock;

void
syscall_init (void) {
	lock_init (&filesys_lock);
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
#ifdef VM
	thread_current()->rsp_stack = f->rsp;
#endif
	switch (f->R.rax)
	{
		case SYS_HALT:
		{
			halt();
			NOT_REACHED();
			break;
		}

		case SYS_EXIT:
		{
			exit(f->R.rdi);
			NOT_REACHED();
			break;
		}

		case SYS_FORK:
		{
			f->R.rax = fork(f->R.rdi, f);
			break;
		}
			
		case SYS_EXEC:
		{
			if (exec(f->R.rdi) == -1) {
				exit(-1);
			}
			break;
		}
			
		case SYS_WAIT:
		{
			f->R.rax = process_wait(f->R.rdi);
			break;
		}
			
		case SYS_CREATE:
		{
			f->R.rax = create(f->R.rdi, f->R.rsi);
			break;
		}
			
		case SYS_REMOVE:
		{
			f->R.rax = remove(f->R.rdi);
			break;
		}
			
		case SYS_OPEN:
		{
			f->R.rax = open(f->R.rdi);
			break;
		}
			
		case SYS_FILESIZE:
		{
			f->R.rax = filesize(f->R.rdi);
			break;
		}
			
		case SYS_READ:
		{
			check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 1);
			f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		}
			
		case SYS_WRITE:
		{
			check_valid_buffer(f->R.rsi, f->R.rdx, f->rsp, 0);
			f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
			break;
		}

		case SYS_SEEK:
		{
			seek(f->R.rdi, f->R.rsi);
			break;
		}
			
		case SYS_TELL:
		{
			f->R.rax = tell(f->R.rdi);
			break;
		}
			
		case SYS_CLOSE:
		{
			close(f->R.rdi);
			break;
		}

		/* ---------------Project 3. Virtual Memory -------------- */
		case SYS_MMAP:
		{
			f->R.rax = mmap(f->R.rdi, f->R.rsi, f->R.rdx, f->R.r10, f->R.r8);
			break;
		}
		case SYS_MUNMAP:
		{
			munmap(f->R.rdi);
			break;
		}
		/* ---------------Project 3. Virtual Memory -------------- */
			
		default:
			exit(-1);
			break;
			
	}
	// printf ("system call!\n");
	// thread_exit ();
}

/* ---------------Project 3. Virtual Memory -------------- */

void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	// if (offset % PGSIZE != 0)  // 이게 맞는 건지 잘 모르겠음
	// 	return NULL;
	struct file *file = process_get_file(fd);

	if (file == NULL)
		return NULL;
	
	/* 파일의 시작점도 페이지 정렬 */
	if (offset % PGSIZE != 0) {
        return NULL;
    }

	/*  It must fail if addr is not page-aligned */
	if (pg_round_down(addr) != addr || is_kernel_vaddr(addr))
		return NULL;

	/*  if the range of pages mapped overlaps any existing set of mapped pages */
	if (spt_find_page(&thread_current()->spt, addr))
		return NULL;

	/* addr가 NULL(0), 파일의 길이가 0*/
	if (addr == NULL || (long long)length == 0)
		return NULL;
	
	/* file descriptors representing console input and output are not mappable */
	if (fd == 0 || fd == 1)
		exit(-1);
	
	return do_mmap(addr, length, writable, file, offset);
}

void munmap(void *addr){
	do_munmap(addr);
}

/* ---------------Project 3. Virtual Memory -------------- */


/*-------------project 2 syscall --------------------*/

void exit(int status)
{
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

void halt(void)
{
	power_off();
}

bool create(const char *filename, unsigned initial_size) 
{
	bool return_code;
	check_address(filename);

	lock_acquire(&filesys_lock); 
	return_code = filesys_create(filename, initial_size);
	lock_release (&filesys_lock);
	return return_code;
}

bool remove(const char *filename)
{
	bool return_code;
	check_address(filename);
	lock_acquire(&filesys_lock);
	return_code = filesys_remove(filename);
	lock_release(&filesys_lock);
	return return_code;
}

// 깊게 생각해보기. 
// syscall 에서 전달되는건 exec(f->R.rdi) . 이걸 cmdline이라고 할 수 있나? 
// 함수의 리턴값도 int 가 아닌 pid_t 임. 고민 해야함. 
int exec(char *cmdline)
{
	check_address(cmdline);

	// process_exec 의 process_cleanup때문에 f->R.rdi 가 날아간다는데 
	// process cleanup의 대상은 f (intr_frame) 가 아니지 않나?
	// 현재 진행중인 프로세스에서 context switching을 하는 역할인데. 
	int file_size = strlen(cmdline) + 1;
	char *cmd_copy = palloc_get_page(PAL_ZERO);
	if (cmd_copy == NULL) {
		exit(-1);
	}
	strlcpy(cmd_copy, cmdline, file_size);

	if (process_exec(cmd_copy) == -1) {
		return -1;
	}
	NOT_REACHED();
	return 0;
}

// 보류
tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
} 

// 인자로 넣어주는 fd가 내가 쓰고싶은 파일. buffer에 쓸 내용 넣어서 전달.
int write(int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	int write_result;
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL) {
		return -1;
	}
	struct thread *cur = thread_current();
	if (fd == 1) {
		putbuf(buffer, size);  // 표준출력을 처리하는 함수 putbuf()
		write_result = size;
	} else if (fd == 0) {
		write_result = -1;
	} else {
		lock_acquire(&filesys_lock);
		write_result = file_write(fileobj, buffer, size);
		lock_release(&filesys_lock);
	}
	return write_result;
}

int read(int fd, void *buffer, unsigned size) {
	check_address(buffer);
	int read_result;
	struct thread *cur = thread_current();
	struct file *file_fd = find_file_by_fd(fd);
	if (file_fd == NULL) {
		return -1;
	}
	if (fd == 0) {
		int i;
		unsigned char *buf = buffer;
		for (i = 0; i < size; i++) {
			char c = input_getc();
			*buf++ = c;
			if (c == '\0') {
				break;
			}
		}
		read_result = i;
	} else if (fd == 1) {
		read_result = -1;
	} else {
		lock_acquire(&filesys_lock);
		read_result = file_read(file_fd, buffer, size);
		lock_release(&filesys_lock);
	}
	return read_result;
}
// fd 인자를 받아서 파일 크기를 리턴
int filesize(int fd) {
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

// fd 값 리턴, 실패시 -1 리턴. 파일 여는 함수
int open(const char *file) {
	check_address(file);
	struct file *open_file = filesys_open(file);
	if (open_file == NULL) {
		return -1;
	}
	int fd = add_file_to_fdt(open_file);

	//fd talbe 이 가득 찼다면 
	if (fd == -1) {
		file_close(open_file);
	}
	return fd;
}

// 파일 위치 (offset)으로 이동하는 함수
// read 는 전체를 읽는 함수이고 seek은 그 파일의 어디를 읽고있는지 초점을 정한다.
void seek(int fd, unsigned position) {
	if (fd <= 1) {   // 0: 표준입력, 1: 표준 출력
		return;
	}
	struct file *seek_file = find_file_by_fd(fd);
	seek_file->pos = position;
}
// tell 함수는 내가 읽고있는 파일의 seek 초점이 어디인지 알려준다. 
unsigned tell(int fd) {
	if (fd <= 1) {
		return;
	} 
	struct file *tell_file = find_file_by_fd(fd);
	return file_tell(tell_file);
}

void close(int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL) {
		return;
	}
	if (fileobj <= 2)     // stdin 하고 stdout 은 지우면 안된다.
		return;
	remove_file_from_fdt(fd);
}



/*-------------project 2 syscall --------------------*/



/*------------- project 2 helper function -------------- */
struct page* check_address(void *addr)
{
	/* 주소 addr이 유저 가상 주소가 아니거나 pml4에 없으면 프로세스 종료 */
	if (addr == NULL || !is_user_vaddr(addr))
	{
		exit(-1);
	}
	/* 유저 가상 주소면 SPT에서 페이지 찾아서 리턴 */
	return spt_find_page(&thread_current()->spt, addr);
}
// fd 로 파일 찾는 함수
static struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return cur->fd_table[fd];
}

// 현재 프로세스의 fd 테이블에 파일 추가.
int add_file_to_fdt(struct file *file) 
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;
	// fd의 위치가 제한 범위를 넘지 않고, fd table의 인덱스 위치와 일치한다면
	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx]) {
		cur->fd_idx++;
	}	
	// error. fd table full
	if (cur->fd_idx >= FDCOUNT_LIMIT) {
		return -1;
	}
	fdt[cur->fd_idx] = file;
	return cur->fd_idx;
}

// fd 테이블에 현재 스레드 제거
void remove_file_from_fdt(int fd)
{
	struct thread *cur = thread_current();

	// if invalid fd
	if (fd < 0 || fd >= FDCOUNT_LIMIT){
		return;
	}
	cur->fd_table[fd] = NULL;
}

/* -----------------project 2 helper functions -----------*/

/*----------------3. virtual memory : memory management----------------------*/
/* read와 write 시스템 콜의 경우 파일 이름의 주소를 받는 대신 buffer의 주소를 받는다. */
void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write){
	/*  */
	for (int i = 0; i < size; i++){
		/* buffer의 시작 주소에서 size만큼 떨어진 주소까지
		   각 주소가 포함된 page가 유저 가상 메모리 공간에 있는지,
		   만약 있다면 spt 테이블에 들어있는지를 확인한다.  */
		struct page* page = check_address(buffer + i); 

		/* 해당 주소가 포함된 페이지가 spt에 없다면 */
		if(page == NULL)
			exit(-1);

		/* write 시스템 콜을 호출했는데 이 페이지가 쓰기가 허용된 페이지가 아닌 경우 */
		if(to_write == true && page->writable == false)
			exit(-1);
	}

}
/*----------------3. virtual memory : memory management----------------------*/