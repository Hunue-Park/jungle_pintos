/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/string.h"
#include "include/threads/mmu.h"

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


struct bitmap *swap_table;
int bitcnt;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE;


/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1);
    size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE;
    swap_table = bitmap_create(swap_size);
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* page struct 안의 Union 영역은 현재 uninit page이다.
	   ANON page를 초기화해주기 위해 해당 데이터를 모두 0으로 초기화해준다. 
	   Q. 이렇게 하면 Union 영역은 모두 다 0으로 초기화되나?*/
	struct uninit_page *uninit = &page->uninit;
	memset(uninit, 0, sizeof(struct uninit_page));

	/* Set up the handler */
	/* 이제 해당 페이지는 ANON이므로 operations도 anon으로 지정한다. */
	page->operations = &anon_ops;

	/* 해당 페이지는 아직 물리 메모리 위에 있으므로 swap_index의 값을 -1로 설정해준다. */
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_index = -1;

	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
	
	/* swap out된 페이지가 디스크 스왑 영역 어디에 저장되었는지는
	   anon_page 구조체 안에 저장되어 있다. */
	int page_no = anon_page->swap_index;

	/* 스왑 테이블에서 해당 스왑 슬롯이 진짜 사용 중인지 체크  */
    if (bitmap_test(swap_table, page_no) == false) {
        return false;
    }

	/* 해당 스왑 영역의 데이터를 가상 주소 공간 kva에 써 준다. */
    for (int i = 0; i < SECTORS_PER_PAGE; ++i) {
        disk_read(swap_disk, page_no * SECTORS_PER_PAGE + i, kva + DISK_SECTOR_SIZE * i);
    }

	/* 다시 해당 스왑 슬롯을 false로 만들어준다. */
    bitmap_set(swap_table, page_no, false);
    
    return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* 비트맵을 처음부터 순회해 false 값을 가진 비트를 하나 찾는다.
	   즉, 페이지를 할당받을 수 있는 swap slot을 하나 찾는다. */
	int page_no = bitmap_scan(swap_table, 0, 1, false);

    if (page_no == BITMAP_ERROR) {
        return false; 
    }

	/* 한 페이지를 디스크에 써 주기 위해 SECTORS_PER_PAGE개의 섹터에 저장해야 한다.
	   이 때 디스크에 각 섹터의 크기 DISK_SECTOR_SIZE만큼 써 준다. */
	/*  
		현재 디스크 섹터와 그 안의 페이지는 다음과 같음
		disk sector            :   1 2 3 4 5 6 7 8   910111213141516  1718192021222324
		each page              : | 1 2 3 4 5 6 7 8 | 1 2 3 4 5 6 7 8 | 1 2 3 4 5 6 7 8 | 
		page_no & swap slot    :   1                 2                 3 
	*/
    for (int i = 0; i < SECTORS_PER_PAGE; ++i) {
        disk_write(swap_disk, page_no * SECTORS_PER_PAGE + i, page->va + DISK_SECTOR_SIZE * i);
    }

	/* swap table의 해당 페이지에 대한 swap slot의 비트를 TRUE로 바꿔주고,
	   해당 페이지의 PTE에서 Present Bit을 0으로 바꿔준다.
	   이제 프로세스가 이 페이지에 접근하면 Page Fault가 뜬다.  */
    bitmap_set(swap_table, page_no, true);
    pml4_clear_page(thread_current()->pml4, page->va);

	/* 페이지의 swap_index 값을 이 페이지가 저장된 swap slot의 번호로 써 준다. */
    anon_page->swap_index = page_no;

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
