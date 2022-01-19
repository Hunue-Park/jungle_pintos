#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "filesys/off_t.h"
#include <stdbool.h>

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

// for VM
bool install_page (void *upage, void *kpage, bool writable);
bool lazy_load_segment (struct page *page, void *aux);
bool setup_stack (struct intr_frame *if_);
struct file *process_get_file(int fd);

// project3 | lazy load를 위해 필요한 정보 구조체
struct container {
    struct file *file;
    // bool writable;
    // size_t file_len;
    off_t offset;               // 읽어야 할 파일 오프셋
    size_t page_read_bytes;     // 가상 페이지에 쓰여져 있는 데이터 크기
};

#endif /* userprog/process.h */
