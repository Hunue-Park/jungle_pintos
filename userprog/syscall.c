#include <stdio.h>
#include <syscall-nr.h>
#include "intrinsic.h"
#include "threads/thread.h"
#include "threads/interrupt.h"
#include "threads/loader.h"
#include "threads/flags.h"
#include "userprog/gdt.h"
#include "userprog/syscall.h"

/* project 2 added */
#include "threads/palloc.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "lib/kernel/console.h" // putbuf()

#include "vm/vm.h"

// #include "include/lib/stdio.h" //STDIN_FILENO, STDOUT_FILENO,

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

// for project 2
void check_address(const uint64_t*);
void halt(void);
void exit(int);
tid_t fork (const char *);
int exec(const char *);
int wait (tid_t);
bool create(const char*, unsigned);
bool remove(const char*);
int open(const char*);
int filesize(int);
int read(int, void*, unsigned);
int write(int, const void*, unsigned);
void seek(int, unsigned);
unsigned tell(int);
void close(int);

// int dup2(int, int); //project2 - extra work

int process_add_file (struct file*);
struct file* process_get_file (int);
void process_close_file (int);

int STDIN = 1;
int STDOUT = 2;


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

    //project2 lock_init
    lock_init(&filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	switch (f->R.rax){

        case SYS_HALT:
            halt();
            break;

        case SYS_EXIT:
            exit(f->R.rdi);
            break;

        case SYS_FORK: ;
            struct thread *cur = thread_current();
            memcpy(&cur->pif, f, sizeof(struct intr_frame));
            f->R.rax = fork(f->R.rdi);
            break;

        case SYS_EXEC:
            check_address(f->R.rdi);
            if (exec(f->R.rdi) == -1) exit(-1);
            break;

        case SYS_WAIT:
            f->R.rax = wait(f->R.rdi);
            break;

        case SYS_CREATE:
            check_address(f->R.rdi);
            // check_address(f->R.rdi + f->R.rsi);
            f->R.rax = create(f->R.rdi, f->R.rsi);
            break;

        case SYS_REMOVE:
            check_address(f->R.rdi);
            f->R.rax = remove(f->R.rdi);
            break;

        case SYS_OPEN:
            check_address(f->R.rdi);
            f->R.rax = open(f->R.rdi);
            break;

        case SYS_FILESIZE:
            f->R.rax = filesize(f->R.rdi);
            break;

        case SYS_READ:
            check_address(f->R.rsi); //check buffer size
            f->R.rax = read(f->R.rdi, f->R.rsi, f->R.rdx);
            break;

        case SYS_WRITE:
            check_address(f->R.rsi); //check buffer size
            f->R.rax = write(f->R.rdi, f->R.rsi, f->R.rdx);
            break;

        case SYS_SEEK:
            seek(f->R.rdi, f->R.rsi);
            break;

        case SYS_TELL:
            f->R.rax = tell(f->R.rdi);
            break;

        case SYS_CLOSE:
            close(f->R.rdi);
            break;

        /* Extra for Project 2 */
        // case SYS_DUP2:
        //     f->R.rax = dup2(f->R.rdi, f->R.rsi);
        //     break;


        /* Project 3 and optionally project 4. */
        // case SYS_MMAP:
        //     break;
        // case SYS_MUNMAP:
        //     break;

        default:
            exit(-1);
            break;
    }
}
void check_address(const uint64_t *addr){

    struct thread *cur = thread_current();
    if (!addr || is_kernel_vaddr(addr) || pml4_get_page(cur->pml4, addr) == NULL)
        exit(-1);
}

void halt(void){
    power_off();
}

void exit(int status){
    struct thread *cur = thread_current();
    cur->exit_status = status;
    printf ("%s: exit(%d)\n", cur->name, status);
    thread_exit();
}

tid_t fork(const char *thread_name){
    struct thread *cur = thread_current();
    return process_fork(thread_name, &cur->pif);
}

int exec(const char *cmd_line){
    int size = strlen(cmd_line) + 1;
    char *fn_copy = palloc_get_page(PAL_ZERO);

    if (!fn_copy) exit(-1);

    strlcpy(fn_copy, cmd_line, size);

	if (process_exec(fn_copy) == -1) return -1;

	NOT_REACHED();
	return 0;
}

int wait(tid_t tid){
    return process_wait(tid);
}

bool create(const char *file, unsigned initial_size){
        return filesys_create(file, initial_size);
}

bool remove(const char *file){
	    return filesys_remove(file);
}

int open(const char *file){
    // if (!file){
    //     return -1;
    // }
    struct file *file_obj = filesys_open(file);
    if (!file_obj){
        return -1;
    }
    int fd = process_add_file(file_obj);
    if (fd == -1)
        file_close(file_obj);
    return fd;
}

int filesize(int fd){
    struct file *file_obj = process_get_file(fd);
    if (!file_obj) return -1;

    return file_length(file_obj);
}

int read(int fd, void *buffer, unsigned size){
    int read_bytes;
    struct thread *cur = thread_current();
    struct file *file_obj = process_get_file(fd);

	if (!file_obj) return -1;

	if (file_obj == STDIN) {
		// if (!cur->stdin_count) {
		// 	NOT_REACHED(); //? 잘 모르겠다.
        //     // process_close_file(fd);
        //     // return -1;
		// }
        // else{
            int i;
            unsigned char *buf = buffer;

            // buffer에 1바이트씩 size 만큼 써줌.
            for (i = 0; i < size; i++) {
                // input buffer 로부터 key를 회수한다. 없으면 키가 들어올 때까지 기다림.
                char c = input_getc();
                *buf++ = c;
                if (c == '\0')
                    break;
            }
            return i;
        // }
	}
	// else
    if (file_obj == STDOUT) return -1;

    /* fild_read -> Returns the number of bytes actually read*/
    else{ //다른 파일에서 읽어올 때
        lock_acquire (&filesys_lock);
        read_bytes = file_read(file_obj, buffer, size);
        lock_release (&filesys_lock);
    }
	return read_bytes;
}

int write(int fd, const void *buffer, unsigned size){
	int written_bytes;
	struct file *file_obj = process_get_file(fd);
	struct thread *cur = thread_current ();

	if (!file_obj) return -1;
    if (file_obj == STDIN) return -1;

	if (file_obj == STDOUT) {
		// if (!cur->stdout_count){
		// 	process_close_file(fd);
		// 	NOT_REACHED();
		// 	return -1;
		// }
        // else{
            putbuf(buffer, size);
            return size;
        // }
	}


    // fild_obj > 2 다른 파일에 쓸때
	lock_acquire (&filesys_lock);
	written_bytes = file_write(file_obj, buffer, size);
	lock_release (&filesys_lock);

	return written_bytes;
}

void seek(int fd, unsigned position){ //cursor 를 position 으로 이동.
    struct file *file_obj = process_get_file(fd);

	if (file_obj > 2)
	    file_obj->pos = position;
}

unsigned tell(int fd){ //cursor의 위치를 return
    struct file *file_obj = process_get_file(fd);

	if (file_obj > 2)
	    return file_tell(file_obj);
}

void close(int fd){
    struct file *file_obj = process_get_file(fd);
	if (!file_obj) return ;

	struct thread *cur = thread_current ();

    /* for dup2 */

	// if (file_obj == STDIN || fd == 0)
	// 	cur->stdin_count --;

	// else if (file_obj == STDOUT || fd == 1)
	// 	cur->stdout_count --;

    /* for dup2 */

	if (fd <= 1 || file_obj <= 2) return;

	process_close_file(fd);


	// if (!file_obj->dup_count)
		file_close(file_obj);
	// else
		// file_obj->dup_count --;
}

//extra

// int dup2(int old_fd, int new_fd){
//     struct file *old_file = process_get_file(old_fd);
//     if (!old_fd) return -1;

//     struct file *new_file = process_get_file(new_fd);
//     if (old_fd == new_fd) return new_fd;

//     struct thread *cur = thread_current();
//     struct file **fdt = cur->fd_table;

//     if (old_file == 1){
//         cur->stdin_count++;
//     }

//     else if (old_file == 2){
//         cur->stdout_count++;
//     }

//     else{
//         old_file->dup_count++;
//     }
//     close(new_fd);
//     fdt[new_fd] = old_file;
//     return new_fd;
// }

int process_add_file (struct file *f){
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table; // file descriptor table

	while (cur->fd_idx < FDCOUNT_LIMIT && fdt[cur->fd_idx])
		cur->fd_idx++;

	if (cur->fd_idx >= FDCOUNT_LIMIT)
		return -1;

	fdt[cur->fd_idx] = f;
	return cur->fd_idx;
}

struct file *process_get_file (int fd){
	struct thread *cur = thread_current ();
	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return NULL;

	return cur->fd_table[fd];
}

void process_close_file (int fd){
	struct thread *cur = thread_current ();

	if (fd < 0 || fd >= FDCOUNT_LIMIT)
		return ;

	cur->fd_table[fd] = NULL;
}

