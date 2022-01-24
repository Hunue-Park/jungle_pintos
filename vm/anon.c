/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "bitmap.h"
#include "lib/string.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* 
스왑 디스크에서 사용 가능한 영역과 사용된 영역을 관리하기 위한 자료구조로 bitmap 사용
스왑 영역은 PGSIZE 단위로 관리 => 기본적으로 스왑 영역은 디스크이니 섹터로 관리하는데
이를 페이지 단위로 관리하려면 섹터 단위를 페이지 단위로 바꿔줄 필요가 있음.
이 단위가 SECTORS_PER_PAGE! (8섹터 당 1페이지 관리)
*/
struct bitmap *swap_table;
int bitcnt;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;



/* Initialize the data for anonymous pages
anon page를 위한 디스크 내 스왑 공간을 생성 & 스왑 테이블을 함께 생성해 해당 공간을 관리
 */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	//struct anon_page anonymous_page;
	swap_disk = disk_get(1, 1); // 스왑 디스크를 반환받아 swap_disk 변수에 저장
    size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE; // 스왑 사이즈: 한 페이지 당 8개의 섹터가 필요하니 
    swap_table = bitmap_create(swap_size);
}

/*프로세스가 uninit 페이지에 접근해 page fault가 발생했을 때 
page fault handler에 의해
호출되는 함수*/

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;

	struct anon_page *anon_page = &page->anon;
}

/* Swap in the page by read contents from the swap disk. */
static bool anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon; // 해당 페이지를 anon_page로 변경

	int page_no = anon_page->swap_index; // anon_page에 들어있는 swap_index


    if (bitmap_test(swap_table, page_no) == false) {
        return false;
    }

    for (int i = 0; i < SECTORS_PER_PAGE; ++i) {
        disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
    }

    bitmap_set(swap_table, page_no, false);
    
    return true;
}


/* Swap out the page by writing contents to the swap disk. */
static bool anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

    /* 비트맵을 처음부터 순회해 false 값을 갖는 비트를 하나 찾는다.
    즉, 페이지를 할당받을 수 있는 swap slot을 하나 찾는다. */
	int page_no = bitmap_scan(swap_table, 0, 1, false);

    if (page_no == BITMAP_ERROR) {
        return false;
    }

    /* 
    한 페이지를 디스크에 써주기 위해 SECTORS_PER_PAGE 개의 섹터에 저장해야 한다.
    이때 디스크에 각 섹터 크기의 DISK_SECTOR_SIZE만큼 써준다.
    */
    for (int i = 0; i < SECTORS_PER_PAGE; ++i) {
        disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
    }

    /*
    swap table의 해당 페이지에 대한 swap slot의 비트를 true로 바꿔주고
    해당 페이지의 PTE에서 present bit을 0으로 바꿔준다.
    이제 프로세스가 이 페이지에 접근하면 page fault가 뜬다.
    */
    bitmap_set(swap_table, page_no, true);
    pml4_clear_page(thread_current()->pml4, page->va);

    /* 페이지의 swap_index 값을 이 페이지가 저장된 swap slot의 번호로 써준다.*/
    anon_page->swap_index = page_no;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
