/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "threads/mmu.h"
#include "userprog/process.h"

/* Project 3-2: 전역 변수 추가 */
struct list frame_table;
struct list_elem *start;

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
	/* --- Project 3-2: Anonymous page --- */
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
    e = hash_find(&spt->spt_hash, &page->hash_elem);	// hash_elem 구조체 얻음

    free(page);

    return e != NULL ? hash_entry (e, struct page, hash_elem) : NULL;	// 존재하지 않는다면 NULL 리턴
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	//int succ = false;

	// if (!hash_insert(&spt->spt_hash, &page->hash_elem))
	// 	succ = true;
	/* TODO: Fill this function. */
	
	return page_insert(&spt->spt_hash, page);
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

	// victim = list_entry(list_pop_front (&frame_table), struct frame, frame_elem);
	// return victim; 
	struct thread *curr = thread_current();
	struct list_elem *e =start;

	for (start = e; start != list_end(&frame_table); start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va))
			pml4_set_accessed(curr->pml4, victim->page->va, 0);
		else
			return victim;
	}

	for (start = list_begin(&frame_table); start != e; start = list_next(start)) {
		victim = list_entry(start, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va))
			pml4_set_accessed(curr->pml4, victim->page->va, 0);
		else
			return victim;
	}
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	swap_out(victim->page);
	return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	
	
	frame->kva = palloc_get_page(PAL_USER); // user pool에서 커널 가상 주소 공간으로 1page 할당
	if (frame->kva == NULL) { // 유저 풀 공간이 하나도 없다면
		frame = vm_evict_frame(); // frame에서 공간 내리고 새로 할당받아온다.
		frame->page = NULL;
		return frame;
	}
	list_push_back(&frame_table, &frame->frame_elem);
	
	frame->page = NULL;
	
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
	/* stack에 해당하는 ANON 페이지를 UNINIT으로 만들고 SPT에 넣어준다.
	이후, 바로 claim해서 물리 메모리와 맵핑해준다. */
	if (vm_alloc_page(VM_ANON| VM_MARKER_0, addr, 1)) {
		vm_claim_page(addr);
		thread_current()->stack_bottom -= PGSIZE;
	}
}


/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	//struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */

	/* 유저 가상 메모리 안의 페이지가 아니라면 여기서 끝내기*/
	if (is_kernel_vaddr(addr)) {
		return false;
	}
	/* 1. 유저 스택 포인터 가져오는 방법 => 이때 반드시 유저 스택 포인터여야 함! 
	모종의 이유로 인터럽트 프레임 내 rsp 주소가 커널 영역이라면 얘를 갖고 오는 게 아니라 thread 내에 우리가 이전에 저장해뒀던 rsp_stack(유저 스택 포인터)를 가져온다.
	그게 아니라 유저 주소를 가리키고 있다면 f->rsp를 갖고 온다.
	*/
	void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;
	/* 페이지의 present bit이 0이면, 즉 메모리 상에 존재하지 않으면
	프레임에 메모리를 올리고 프레임과 페이지를 매핑시킨다.
	*/
	if (not_present) {
		if(!vm_claim_page(addr)) {
			// 여기서 해당 주소가 유저 스택 내에 존재하는지를 체크한다.
			if (rsp_stack-8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK) {
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

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
			struct hash_iterator i;
			hash_first (&i, &src->spt_hash);
			while (hash_next (&i)) { // src 각 페이지를 반복문 통해 복사
				struct page *parent_page = hash_entry(hash_cur (&i), struct page, hash_elem); // 현재 해시 테이블 element 리턴
				enum vm_type type = page_get_type(parent_page); // 부모 페이지 type
				void *upage = parent_page->va; // 부모 페이지 가상 주소
				bool writable = parent_page->writable; // 부모 페이지 쓰기 가능 여부
				vm_initializer *init = parent_page->uninit.init; // 부모 초기화되지 않은 페이지들 할당!
				void* aux = parent_page->uninit.aux;

				if (parent_page->uninit.type & VM_MARKER_0) {
					setup_stack(&thread_current()->tf);
				}
				else if (parent_page->operations->type == VM_UNINIT) {
					if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
					return false;
				}
				else {
					if (!vm_alloc_page(type, upage, writable))
						return false;
					if (!vm_claim_page(upage))
						return false;
				}

				if (parent_page->operations->type != VM_UNINIT) {
					struct page * child_page = spt_find_page(dst, upage);
					memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
				}
			}
			return true;
}


/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */

	struct hash_iterator i;
	
	hash_first (&i, &spt->spt_hash);
	while (hash_next (&i)) {
		struct page *page = hash_entry (hash_cur (&i), struct page, hash_elem);

		if (page->operations->type == VM_FILE) {
			//do_munmap(page->va); pjt 4
		}
		destroy(page);
	}
	hash_destroy(&spt->spt_hash, spt_destructor);
}

/* Initialize new supplemental page table */
/* --- Project 3: VM --- */
unsigned page_hash (const struct hash_elem *e, void *aux UNUSED) {
	const struct page *p = hash_entry(e, struct page, hash_elem); // 해당 페이지가 들어있는 해시 테이블 시작 주소를 가져온다. 우리는 hash_elem만 알고 있으니 
	return hash_bytes(&p->va, sizeof(p->va));
}

/* --- Project 3: VM --- */
// 가상 주소 값 비교해서 더 큰 주소값 찾아주는 -> hash table과 주소값은 1대1 매핑이니 크기 비교 -> hash
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED) {
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);

	return p_a->va < p_b->va;
}

/* --- Project 3: VM --- */
bool page_insert(struct hash *h, struct page *p) {
	if(!hash_insert(h, &p->hash_elem)) {
		return true;
	}
	else
		return false;

}

/* --- Project 3: VM --- */
bool page_delete(struct hash *h, struct page *p) {
	if(!hash_delete(h, &p->hash_elem)) {
		return true;
	}
	else
		return false;

}


void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	
	/* --- Project 3: VM --- */
	hash_init(&spt->spt_hash, page_hash, page_less, NULL);
}








void spt_destructor(struct hash_elem *e, void* aux) {
	const struct page *p = hash_entry(e, struct page, hash_elem);
	free(p);
}