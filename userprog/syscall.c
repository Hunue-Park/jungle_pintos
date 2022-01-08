#include "userprog/syscall.h"
#include <stdio.h>
#include <string.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "threads/synch.h"
#include "filesys/filesys.h"
#include "filesys/file.h"

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

	lock_init(filesys_lock);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.

	// printf("syscall num: %d\n",f->R.rax);
	switch (f->R.rax) {
	
	case (SYS_HALT):
		syscall_halt();
		break;

	case (SYS_EXIT):
		syscall_exit(f->R.rdi);
		break;

	case (SYS_FORK):
		f->R.rax = syscall_fork(f->R.rdi, f);
		break;
	
	case (SYS_EXEC):
		break;
	
	case (SYS_WAIT):
		break;
	
	case (SYS_CREATE):
		f->R.rax = syscall_create(f->R.rdi, f->R.rsi);
		break;

	case (SYS_REMOVE):
		f->R.rax = syscall_remove(f->R.rdi);
		break;

	case (SYS_OPEN):
		f->R.rax = syscall_open(f->R.rdi);
		break;

	case (SYS_FILESIZE):
		f->R.rax = syscall_filesize(f->R.rdi);
		break;

	case (SYS_READ):
		f->R.rax = syscall_read(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case (SYS_WRITE):
		f->R.rax = syscall_write(f->R.rdi, f->R.rsi, f->R.rdx);
		break;

	case (SYS_SEEK):
		syscall_seek(f->R.rdi, f->R.rsi);
		break;

	case (SYS_TELL):
		f->R.rax = syscall_tell(f->R.rdi);
		break;

	case (SYS_CLOSE):
		syscall_close(f->R.rdi);
		break;

	default:
		printf ("unvalid system call!\n");
		thread_exit ();
		break;
	}
}

// static bool
// validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
// {
//   /* p_offset and p_vaddr must have the same page offset. */
//   if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
//     return false; 

//   /* p_offset must point within FILE. */
//   if (phdr->p_offset > (Elf32_Off) file_length (file)) 
//     return false;

//   /* p_memsz must be at least as big as p_filesz. */
//   if (phdr->p_memsz < phdr->p_filesz) 
//     return false; 

//   /* The segment must not be empty. */
//   if (phdr->p_memsz == 0)
//     return false;

//   /* The virtual memory region must both start and end within the
//      user address space range. */
//   if (!is_user_vaddr ((void *) phdr->p_vaddr))
//     return false;
//   if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
//     return false;

//   /* The region cannot "wrap around" across the kernel virtual
//      address space. */
//   if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
//     return false;

//   /* Disallow mapping page 0.
//      Not only is it a bad idea to map page 0, but if we allowed
//      it then user code that passed a null pointer to system calls
//      could quite likely panic the kernel by way of null pointer
//      assertions in memcpy(), etc. */
//   if (phdr->p_vaddr < PGSIZE)
//     return false;

//   /* It's okay. */
//   return true;
// }

void
syscall_halt (void) {
	power_off();
}

void
syscall_exit (int status) {
	thread_current()->exit_status = status; 
	thread_exit();
}

int
syscall_fork (const char *thread_name, struct intr_frame *user_context) {
	
	return process_fork(thread_name, user_context);
}

int
syscall_open (const char *file) {
	check_address(file);
	struct file *opened_file = filesys_open(file);
	int fd = process_add_file(opened_file);
	return fd;
}

bool
syscall_create (const char *file_name, unsigned size) {
	check_address(file_name);
	return filesys_create (file_name, size);
}

bool 
syscall_remove (const char *file_name) {
	check_address(file_name);
	return filesys_remove(file_name);
}

int
syscall_filesize (int fd) {
	
	if (fd < 0){
		return -1;
	} else {
		struct file *f;
		f = process_get_file(fd);
		check_address(f);
		return file_length(f);
	}

}

int
syscall_read (int fd, const void *buffer, unsigned size) {
	check_address(buffer);

	struct file *f;
	lock_acquire (&filesys_lock);

	if (fd == STDIN_FILENO){
    // 표준 입력
		unsigned count = size;
		while (count--) *((char *)buffer++) = input_getc();
		lock_release (&filesys_lock);
		return size;
	}
	if ((f = process_get_file (fd)) == NULL){
		lock_release (&filesys_lock);
		return -1;
    }
	size = file_read (f, buffer, size);
	lock_release (&filesys_lock);
	return size;
}

int
syscall_write (int fd, const void *buffer, unsigned size) {
	check_address(buffer);

	struct file *f;
	lock_acquire (&filesys_lock);
	if (fd == STDOUT_FILENO){
		putbuf (buffer, size);
		lock_release (&filesys_lock);
		return size;  
    }
	if ((f = process_get_file (fd)) == NULL){
		lock_release (&filesys_lock);
		return 0;
    }
	size = file_write (f, buffer, size);
	lock_release (&filesys_lock);
	return size;
}

void
syscall_seek (int fd, unsigned position) {
	
	if (fd < 2 || position < 0) {
		return;
	} else {
		struct file *f;
		f = process_get_file(fd);
		f->pos = position;
	}
}

unsigned
syscall_tell (int fd) {

	if (fd < 2) {
		return;
		//? 오류 시에 -1을 반환하면 안 될 것 같은데 그냥 비워둬도 될까
	} else {
		struct file *f;
		f = process_get_file(fd);
		return file_tell(f);
	}
}

void
syscall_close (int fd) {
	struct thread *t = thread_current();
	if (fd < 0 || t->next_fd <= fd)
		return;
	file_close(t->fd_table[fd]);
	t->fd_table[fd] = NULL;
}

void
check_address (int *addr) {
	if (is_kernel_vaddr(addr))
		syscall_exit(-1);
		NOT_REACHED();
}

int
process_add_file (struct file *f) {
	if (f == NULL)
		return -1;
	
	int fd;
	struct thread *t;

	t = thread_current ();

	lock_acquire(filesys_lock);
	fd = t->next_fd++;
	t->fd_table[fd] = f;
	lock_release(filesys_lock);
	
	return fd;
}

struct file
* process_get_file (int fd) {
	if (fd == NULL)
		return -1;
	
	struct thread *t;
	t = thread_current();

	return t->fd_table[fd];
}