#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"



tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/* --- Project 3 --- */
bool install_page (void *upage, void *kpage, bool writable);
bool lazy_load_segment (struct page *page, void *aux);
struct file *process_get_file(int fd);
//bool setup_stack (struct intr_frame *if_);
/* spt에 프로세스의 각 페이지와 관련된 실행 파일에 대한 정보를 저장해놓는 구조체. 즉, 파일 관련임.*/
struct container {
    struct file *file;
    off_t offset;            // 해당 파일의 오프셋
    size_t page_read_bytes;  // 읽어올 파일 데이터 크기(load_segment에서 1page보다는 작거나 같다.)
};


#endif /* userprog/process.h */
