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
#include "vm/vm.h"

#include "filesys/directory.h"
#include "filesys/fat.h"
#include "filesys/inode.h"
const int STDIN = 1;
const int STDOUT = 2;
void syscall_entry (void);
void syscall_handler (struct intr_frame *);
struct page * check_address(void *addr);
void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write);
int add_file_to_fdt(struct file *file);
void remove_file_from_fdt(int fd);
static struct file *find_file_by_fd(int fd);


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

void* mmap (void *addr, size_t length, int writable, int fd, off_t offset);
void munmap (void *addr);

// Project 4-2. Subdirectory
bool chdir (const char *dir_input);
bool mkdir (const char *dir_input);
bool readdir (int fd, char* name);
bool isdir (int fd);
int inumber (int fd);
int symlink (const char* target, const char* linkpath);

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

struct lock file_rw_lock;

void
syscall_init (void) {
	lock_init (&file_rw_lock);
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

		case SYS_CHDIR:
		{
			f->R.rax = chdir(f->R.rdi);
			break;
		}

		case SYS_MKDIR:
		{
			f->R.rax = mkdir(f->R.rdi);
			break;
		}

		case SYS_READDIR:
		{
			f->R.rax = readdir(f->R.rdi, f->R.rsi);
			break;
		}

		case SYS_ISDIR:
		{
			f->R.rax = isdir(f->R.rdi);
			break;
		}

		case SYS_INUMBER:
		{
			f->R.rax = inumber(f->R.rdi);
			break;
		}

		case SYS_SYMLINK:
		{
			f->R.rax = symlink(f->R.rdi, f->R.rsi);
			break;
		}

			
		default:
			printf("(syscall_handler) Not implemented syscall! :)");
			exit(-1);
			break;
			
	}
	// printf ("system call!\n");
	// thread_exit ();
}

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

	// lock_acquire(&filesys_lock); 
	return_code = filesys_create(filename, initial_size);
	// lock_release (&filesys_lock);
	return return_code;
}

bool remove(const char *filename)
{
	bool return_code;
	check_address(filename);
	// lock_acquire(&filesys_lock);
	return_code = filesys_remove(filename);
	// lock_release(&filesys_lock);
	return return_code;
}

// ?????? ???????????????. 
// syscall ?????? ??????????????? exec(f->R.rdi) . ?????? cmdline????????? ??? ??? ??????? 
// ????????? ???????????? int ??? ?????? pid_t ???. ?????? ?????????. 
int exec(char *cmdline)
{
	check_address(cmdline);

	// process_exec ??? process_cleanup????????? f->R.rdi ??? ?????????????????? 
	// process cleanup??? ????????? f (intr_frame) ??? ????????? ???????
	// ?????? ???????????? ?????????????????? context switching??? ?????? ????????????. 
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

// ??????
tid_t fork(const char *thread_name, struct intr_frame *f) {
	return process_fork(thread_name, f);
} 

// ????????? ???????????? fd??? ?????? ???????????? ??????. buffer??? ??? ?????? ????????? ??????.
int write(int fd, const void *buffer, unsigned size) {
	check_address(buffer);
	int write_result;
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL) {
		return -1;
	}
	struct thread *cur = thread_current();
	if (fd == 1) {
		putbuf(buffer, size);  // ??????????????? ???????????? ?????? putbuf()
		write_result = size;
	} else if (fd == 0) {
		write_result = -1;
	} else {
		if (inode_isdir(fileobj->inode)) {
			return -1;
		}
		lock_acquire(&file_rw_lock);
		write_result = file_write(fileobj, buffer, size);
		lock_release(&file_rw_lock);
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
		lock_acquire(&file_rw_lock);
		read_result = file_read(file_fd, buffer, size);
		lock_release(&file_rw_lock);
	}
	return read_result;
} 
// fd ????????? ????????? ?????? ????????? ??????
int filesize(int fd) {
	struct file *open_file = find_file_by_fd(fd);
	if (open_file == NULL) {
		return -1;
	}
	return file_length(open_file);
}

// fd ??? ??????, ????????? -1 ??????. ?????? ?????? ??????
int open(const char *file) {
	check_address(file);
	struct file *open_file = filesys_open(file);
	if (open_file == NULL) {
		return -1;
	}
	int fd = add_file_to_fdt(open_file);

	//fd talbe ??? ?????? ????????? 
	if (fd == -1) {
		file_close(open_file);
	}
	return fd;
}

// ?????? ?????? (offset)?????? ???????????? ??????
// read ??? ????????? ?????? ???????????? seek??? ??? ????????? ????????? ??????????????? ????????? ?????????.
void seek(int fd, unsigned position) {
	if (fd <= 1) {   // 0: ????????????, 1: ?????? ??????
		return;
	}
	struct file *seek_file = find_file_by_fd(fd);
	seek_file->pos = position;
}
// tell ????????? ?????? ???????????? ????????? seek ????????? ???????????? ????????????. 
unsigned tell(int fd) {
	if (fd <= 1) {
		return;
	} 
	struct file *tell_file = find_file_by_fd(fd);
	return file_tell(tell_file);
}

void close(int fd)
{
	struct file *fileobj = find_file_by_fd(fd);
	if (fileobj == NULL)
		return;

	struct thread *cur = thread_current();

	// fd = 0 : stdin, 1 : stdout - thread.c/thread_create ??????
	if (fd == 0 || fileobj == STDIN)
	{
		cur->stdin_count--;
	}
	else if (fd == 1 || fileobj == STDOUT)
	{
		cur->stdout_count--;
	}

	remove_file_from_fdt(fd);
	if (fd <= 1 || fileobj <= 2)
		return;

	if (inode_isdir(fileobj->inode)){
		remove_file_from_fdt(fd);
		dir_close((struct dir *)fileobj);
	}
	else if (fileobj->dupCount == 0)
		file_close(fileobj);
	else
		fileobj->dupCount--;
}


// for VM
void *mmap (void *addr, size_t length, int writable, int fd, off_t offset) {

    if (offset % PGSIZE != 0) {
        return NULL;
    }

    if (pg_round_down(addr) != addr || is_kernel_vaddr(addr) || addr == NULL || (long long)length <= 0)
        return NULL;
    
    if (fd == 0 || fd == 1)
        exit(-1);
    
    // vm_overlap
    if (spt_find_page(&thread_current()->spt, addr))
        return NULL;

    struct file *target = find_file_by_fd(fd);
	// struct file *target = find_file_by_fd(fd);

    if (target == NULL)
        return NULL;

    void * ret = do_mmap(addr, length, writable, target, offset);

    return ret;
}

void munmap (void *addr) {
    do_munmap(addr);
}

// Project 4-2. Subdirectory
bool
chdir (const char *dir_input) {
	struct path* path = parse_filepath(dir_input);
	if(path->dircount==-1) {
		return false;
	}
	struct dir* subdir = find_subdir(path->dirnames, path->dircount);
	if(subdir == NULL) {
		dir_close (subdir);
		free_path(path);
		return false;
	}

	if(subdir == NULL) return false;	

	if (!strcmp(path->filename, "root")){
		set_current_directory(dir_open_root());
		dir_close(subdir);
		free_path(path);
		return true;
	}

	struct inode *inode = NULL; // inode of subdirectory or file
	dir_lookup(subdir, path->filename, &inode);

	if (inode == NULL) return false;
	set_current_directory(dir_open(inode));

	dir_close (subdir);
	free_path(path);

	return true;
}

bool mkdir (const char *dir_input){
	bool success = false;

	if(strlen(dir_input) == 0) return false;

	struct path* path = parse_filepath(dir_input);
	if(path->dircount==-1) {
		return false;
	}
	struct dir* subdir = find_subdir(path->dirnames, path->dircount);
	if(subdir == NULL) {
		goto done;
	}

	// create new directory named 'path->filename'
	cluster_t clst = fat_create_chain(0);
	if(clst == 0){ // FAT is full (= disk is full)
		goto done;
	}
	disk_sector_t sect = cluster_to_sector(clst);

	// dir-vine) must fail if disk is full
	// cluster_t isAvailable = fat_create_chain(0);
	// if(sect >= filesys_disk->capacity)
	// 	return false;

	dir_create(sect, DISK_SECTOR_SIZE/sizeof(struct dir_entry)); //?????? directory obj ??????

	struct dir *dir = dir_open(inode_open(sect));
	dir_add(dir, ".", sect);
	dir_add(dir, "..", inode_get_inumber(dir_get_inode(subdir)));
	dir_close(dir);

	success = dir_add(subdir, path->filename, cluster_to_sector(clst));
	// #ifdef DBG if fail?

done: 
	dir_close (subdir); //?????? ???????????? ????????? ??????????????? ?????? close???????????????
	free_path(path);

	return success;
}

bool
readdir (int fd, char* name) {
	struct file *fileobj = find_file_by_fd(fd);
	if (inode_isdir(fileobj->inode)){
		return dir_readdir((struct dir *)fileobj, name);
	}
	else return false;
}

bool
isdir (int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	return inode_isdir(fileobj->inode);
}

int
inumber (int fd) {
	struct file *fileobj = find_file_by_fd(fd);
	// return ((struct inode*)fileobj->inode)->sector;
	return fileobj->inode->sector;
}

int
symlink (const char* target, const char* linkpath) {
	bool lazy = false;
	//parse link path
	struct path* path_link = parse_filepath(linkpath);
	if(path_link->dircount==-1) {
		return -1;
	}
	struct dir* subdir_link = find_subdir(path_link->dirnames, path_link->dircount);
	if(subdir_link == NULL) {
		dir_close (subdir_link);
		free_path(path_link);
		return -1;
	}

	//parse target path
	struct path* path_tar = parse_filepath(target);
	if(path_tar->dircount==-1) {
		return -1;
	}
	struct dir* subdir_tar = find_subdir(path_tar->dirnames, path_tar->dircount);
	if(subdir_tar == NULL) {
		dir_close (subdir_tar);
		free_path(path_tar);
		return -1;
	}

	//find target inode
	struct inode* inode = NULL;
	dir_lookup(subdir_tar, path_tar->filename, &inode);
	if(inode == NULL) {
		//lazy symlink(target not created yet)
		inode = dir_get_inode(subdir_tar);
		lazy = true;
	}

	//add to link path
	dir_add(subdir_link, path_link->filename, inode_get_inumber(inode));
	set_entry_symlink(subdir_link, path_link->filename, true);
	if (lazy){ // create a lazy link to some file
		set_entry_lazytar(subdir_link, path_link->filename, path_tar->filename);
	}
	else{ // if target is a lazy link to some file; propagate lazy link
		struct dir_entry target_entry;
		off_t ofs;
		lookup(subdir_tar, path_tar->filename, &target_entry, &ofs);
		if(strcmp("lazy", target_entry.lazy)){
			set_entry_lazytar(subdir_link, path_link->filename, target_entry.lazy);
		}
	}

	dir_close (subdir_link);
	free_path(path_link);
	dir_close (subdir_tar);
	free_path(path_tar);
	return 0;
}



/*-------------project 2 syscall --------------------*/



/*------------- project 2 helper function -------------- */
// page??? ?????? check_address ??????
struct page * check_address(void *addr) {
    if (is_kernel_vaddr(addr) || addr == NULL) {
        exit(-1);
    }
    return spt_find_page(&thread_current()->spt, addr);
}

void check_valid_buffer(void* buffer, unsigned size, void* rsp, bool to_write) {
    for (int i = 0; i < size; i++) {
        struct page* page = check_address(buffer + i);    // ????????? ?????? buffer?????? buffer + size????????? ????????? ??? ???????????? ????????? ???????????? ??????
        if(page == NULL)
            exit(-1);
        if(to_write == true && page->writable == false)
            exit(-1);
    }
}

/* -----------Project 3 change -----------------*/
// fd ??? ?????? ?????? ??????
static struct file *find_file_by_fd(int fd) {
	struct thread *cur = thread_current();
	if (fd < 0 || fd >= FDCOUNT_LIMIT) {
		return NULL;
	}
	return cur->fd_table[fd];
}

// ?????? ??????????????? fd ???????????? ?????? ??????.
int add_file_to_fdt(struct file *file) 
{
	struct thread *cur = thread_current();
	struct file **fdt = cur->fd_table;
	// fd??? ????????? ?????? ????????? ?????? ??????, fd table??? ????????? ????????? ???????????????
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

// fd ???????????? ?????? ????????? ??????
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