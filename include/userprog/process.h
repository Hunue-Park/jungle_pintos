#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

/*----------------3. virtual memory : memory management----------------------*/
bool install_page (void *upage, void *kpage, bool writable);
bool lazy_load_segment (struct page *page, void *aux);
/* lazy load를 위한 정보 구조체. 이 안에 해당 페이지에 대응되는 파일의 정보들이 들어가 있다.
   이 구조체를 통해서 페이지 폴트가 나고 디스크에서 필요한 파일을 불러올 때 필요한 파일 정보를
   알 수 있다. */
struct container {
    struct file *file;  
    off_t offset;           // 해당 파일의 오프셋
    size_t page_read_bytes; // 읽어올 파일의 데이터 크기(load_segment에서 1PAGE보다는 작거나 같다)
};
/*----------------3. virtual memory : memory management----------------------*/

#endif /* userprog/process.h */
