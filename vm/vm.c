/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"

// global variables
struct list frame_table;
struct list_elem* start;

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	list_init(&frame_table);
	start = list_begin(&frame_table);
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		
		struct page* page = (struct page*)malloc(sizeof(struct page));

        typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
        initializerFunc initializer = NULL;

        switch(VM_TYPE(type)) {
            case VM_ANON:
                initializer = anon_initializer;
                break;
            case VM_FILE:
                initializer = file_backed_initializer;
                break;
		}

        uninit_new(page, upage, init, type, aux, initializer);

        // page member 초기화
        page->writable = writable;
        // hex_dump(page->va, page->va, PGSIZE, true);

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// struct page  *page = NULL;
	/* TODO: Fill this function. */
    struct page* page = (struct page*)malloc(sizeof(struct page));
    struct hash_elem *e;

    page->va = pg_round_down(va);  // va가 가리키는 가상 페이지의 시작 포인트(오프셋이 0으로 설정된 va) 반환
    e = hash_find(&spt->pages, &page->hash_elem);	// hash_elem 구조체 얻음

    free(page);

    return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;	// 존재하지 않는다면 NULL 리턴
}


/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	// int succ = false;
	/* TODO: Fill this function. */

	return insert_page(&spt->pages, page);
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
    /* TODO: The policy for eviction is up to you. */
    struct thread *curr = thread_current();
    struct list_elem *e = start;

    for (start = e; start != list_end(&frame_table); start = list_next(start)) {
        victim = list_entry(start, struct frame, frame_elem);
        if (pml4_is_accessed(curr->pml4, victim->page->va))
            pml4_set_accessed (curr->pml4, victim->page->va, 0);
        else
            return victim;
    }

    for (start = list_begin(&frame_table); start != e; start = list_next(start)) {
        victim = list_entry(start, struct frame, frame_elem);
        if (pml4_is_accessed(curr->pml4, victim->page->va))
            pml4_set_accessed (curr->pml4, victim->page->va, 0);
        else
            return victim;
    }

    return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim();
    /* TODO: swap out the victim and return the evicted frame. */

    swap_out(victim->page);

    return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame * vm_get_frame (void) {
	// struct frame *frame = NULL;
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	
	frame->kva = palloc_get_page(PAL_USER);
    if(frame->kva == NULL)
    {
        frame = vm_evict_frame();
        frame->page = NULL;

        return frame;
    }
    list_push_back (&frame_table, &frame->frame_elem);

    frame->page = NULL;

	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void vm_stack_growth (void *addr UNUSED) {
    if(vm_alloc_page(VM_ANON | VM_MARKER_0, addr, 1))
    {
        vm_claim_page(addr);
        thread_current()->stack_bottom -= PGSIZE;   // 스택은 위에서부터 쌓기 때문에 주소값 위치를 페이지 사이즈씩 마이너스함
    }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	// struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (is_kernel_vaddr(addr)) {
        return false;
	}

    void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;
    if (not_present){
        if (!vm_claim_page(addr)) {
            if (rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
                vm_stack_growth(thread_current()->stack_bottom - PGSIZE);
                return true;
            }
            return false;
        }
        else
            return true;
    }
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page (void *va UNUSED) {
	struct page *page;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
// 가상 주소와 물리주소 매핑( 성공, 실패 여부 리턴해줌 )
static bool vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	if (install_page(page->va, frame->kva, page->writable)) {	// 유저페이지가 이미 매핑되었거나 메모리 할당 실패 시 false
        return swap_in(page, frame->kva);
    }
    return false;
}


/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	hash_init(&spt->pages, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	struct hash_iterator i;
	struct hash * src_hash = &src->pages;
	hash_first (&i, src_hash);
	while (hash_next (&i)) {
    	struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
		enum vm_type type = page_get_type(p);
		void *upage = p->va;
		bool writable = p->writable;
		bool success = false;
		vm_initializer *init = p->uninit.init;
		void *aux = p->uninit.aux;
		if(p->operations->type == VM_UNINIT){
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
		}
		else if(type == VM_ANON) {
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
			if (!vm_claim_page(upage))
				return false;
			struct page* newpage = spt_find_page(dst, upage);
			memcpy(newpage->frame->kva, p->frame->kva, PGSIZE);
		}
        else if (type == VM_FILE) {
            if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				return false;
			if (!vm_claim_page(upage))
				return false;
			struct page* newpage = spt_find_page(dst, upage);
			memcpy(newpage->frame->kva, p->frame->kva, PGSIZE);
        }
	}
	return true;
}
/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;

    hash_first (&i, &spt->pages);
    while (hash_next (&i)) {
        struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);

        if (page->operations->type == VM_FILE) {
            do_munmap(page->va);
            // destroy(page);
        }
    }
    hash_destroy(&spt->pages, spt_destructor);
}

// 해시 테블 초기화 시 해시 값을 구해주는 함수의 포인터
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED) {
    const struct page *p = hash_entry(p_, struct page, hash_elem);
    return hash_bytes(&p->va, sizeof p->va);
}

// 해시 테이블 초기화할 때 해시 요소들 비교하는 함수의 포인터
// a가 b보다 작으면 true, 반대면 false
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);

    return a->va < b->va;
}

bool insert_page(struct hash *pages, struct page *p) {
    if (!hash_insert(pages, &p->hash_elem))
        return true;
    else
        return false;
}

bool delete_page(struct hash *pages, struct page *p) {
    if (!hash_delete(pages, &p->hash_elem))
        return true;
    else
        return false;
}

void spt_destructor(struct hash_elem *e, void* aux) {
    const struct page *p = hash_entry(e, struct page, hash_elem);
    free(p);
}

