#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "devices/disk.h"
#include "bitmap.h"
#include "include/threads/vaddr.h"
struct page;
enum vm_type;

struct anon_page {
    int swap_index; // swap된 데이터들이 저장된 섹터 구역을 의미
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
