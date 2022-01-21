/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
/*----------------3. virtual memory : memory management----------------------*/
#include "threads/mmu.h"
#include "userprog/process.h"
/*----------------3. virtual memory : memory management----------------------*/

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void vm_init(void)
{
	vm_anon_init();
	vm_file_init();
#ifdef EFILESYS /* For project 4 */
	pagecache_init();
#endif
	register_inspect_intr();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type(struct page *page)
{
	int ty = VM_TYPE(page->operations->type);
	switch (ty)
	{
	case VM_UNINIT:
		return VM_TYPE(page->uninit.type);
	default:
		return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim(void);
static bool vm_do_claim_page(struct page *page);
static struct frame *vm_evict_frame(void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool vm_alloc_page_with_initializer(enum vm_type type, void *upage, bool writable,
									vm_initializer *init, void *aux)
{

	ASSERT(VM_TYPE(type) != VM_UNINIT) // 인자로 들어오는 TYPE은 ANON 아니면 FILE-BACKED

	struct supplemental_page_table *spt = &thread_current()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page(spt, upage) == NULL)
	{ /* spt안에 upage에 해당하는 페이지가 없으면 */
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */

		/* 함수 포인터를 사용하여 TYPE에 맞는 페이지 초기화 함수를 사용한다. */
		typedef bool (*initializeFunc)(struct page *, enum vm_type, void *);
		initializeFunc initializer = NULL;

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			// case VM_ANON|VM_MARKER_0:
			initializer = anon_initializer;
			break;
		case VM_FILE:
			initializer = file_backed_initializer;
			break;
		}

		/* 새 페이지를 만들어서 page 구조체의 멤버를 초기화한다. */
		struct page *new_page = malloc(sizeof(struct page));
		uninit_new(new_page, upage, init, type, aux, initializer);

		new_page->writable = writable;
		new_page->page_cnt = -1; // file-mapped page가 아니므로 -1.

		/* TODO: Insert the page into the spt. */
		/* 새로 만든 UNINIT 페이지를 프로세스의 spt에 넣는다. 
		   아직 물리 메모리랑 매핑이 된 것은 아니다. */
		spt_insert_page(spt, new_page);

		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page(struct supplemental_page_table *spt UNUSED, void *va UNUSED)
{
	struct page *page = (struct page *)malloc(sizeof(struct page));
	struct hash_elem *e;

	/* 해당 va가 속해 있는 페이지 시작 주소를 가지는 page 만든다.
       해당 페이지가 spt에 있는지 확인할 것. */
	page->va = pg_round_down(va);

	/* e와 같은 해시값(va)을 가지는 원소를 e에 해당하는 bucket list 내에서 
       찾아 리턴한다. 만약 못 찾으면 NULL을 리턴한다. */
	e = hash_find(&spt->pages, &page->hash_elem);

	free(page);

	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
// bool
// spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
// 	int succ = false;

// 	/* 반환값이 NULL이면 삽입 성공 */
// 	if (!hash_insert(&spt->pages, &page->hash_elem))
// 		succ = true;

// 	return succ;
// }

bool spt_insert_page(struct supplemental_page_table *spt UNUSED, struct page *page UNUSED)
{
	// int succ = false;
	/* TODO: Fill this function. */
	return insert_page(&spt->pages, page);
}

bool insert_page(struct hash *pages, struct page *p)
{
	/* 반환값이 NULL이면 삽입 성공! */
	if (!hash_insert(pages, &p->hash_elem))
		return true;
	else
		return false;
}

bool delete_page(struct hash *pages, struct page *p)
{
	if (!hash_delete(pages, &p->hash_elem))
		return true;
	else
		return false;
}

void spt_remove_page(struct supplemental_page_table *spt, struct page *page)
{
	vm_dealloc_page(page);
	return true;
}

/* Get the struct frame, that will be evicted. */
struct list_elem *start;

static struct frame *
vm_get_victim(void)
{
	struct frame *victim = NULL;
	/* FIFO eviction policy */
	// victim = list_entry(list_pop_front (&frame_table), struct frame, frame_elem);
	struct thread *curr = thread_current();
	struct list_elem *e = start;

	for (start = e; start != list_end(&frame_table); start = list_next(start))
	{
		victim = list_entry(start, struct frame, frame_elem);
		if (pml4_is_accessed(curr->pml4, victim->page->va))
			pml4_set_accessed(curr->pml4, victim->page->va, 0);
		else
			return victim;
	}

	for (start = list_begin(&frame_table); start != e; start = list_next(start))
	{
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
vm_evict_frame(void)
{
	struct frame *victim UNUSED = vm_get_victim();
	/* TODO: swap out the victim and return the evicted frame. */
	if (swap_out(victim->page))
		return victim;
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame(void)
{
	// struct frame *frame = NULL;
	/* TODO: Fill this function. */
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	frame->kva = palloc_get_page(PAL_USER); /* USER POOL에서 커널 가상 주소 공간으로 1page 할당 */

	/* if 프레임이 꽉 차서 할당받을 수 없다면 페이지 교체 실시
	   else 성공했다면 frame 구조체 커널 주소 멤버에 위에서 할당받은 메모리 커널 주소 넣기 */
	if (frame->kva == NULL)
	{
		// frame = vm_evict_frame();
		frame->page = NULL;

		return frame;
	}
	// list_push_back (&frame_table, &frame->frame_elem);

	frame->page = NULL;

	ASSERT(frame != NULL);
	ASSERT(frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth(void *addr UNUSED)
{
	/* stack에 해당하는 ANON 페이지를 UNINIT으로 만들고 SPT에 넣어준다. 
	   그 후 바로 claim해서 물리 메모리와 매핑해준다.*/
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, addr, 1)){
		vm_claim_page(addr);
		thread_current()->stack_bottom -= PGSIZE;
	}
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp(struct page *page UNUSED)
{
}

/* Return true on success */
bool vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
						 bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
{
	struct supplemental_page_table *spt UNUSED = &thread_current()->spt;
	struct page *page = NULL;

	if (is_kernel_vaddr(addr))
	{ // 유저 공간 페이지 폴트여야 한다.
		return false;
	}
	/* 1. 스택 포인터를 어떻게 가져올 것인지
	   -> 페이지 폴트가 커널 영역에서 났는지, 유저 영역에서 났는지 */
	void *rsp_stack = is_kernel_vaddr(f->rsp) ? thread_current()->rsp_stack : f->rsp;

	if (not_present)
	{
		/* 페이지의 Present bit이 0이면 -> 메모리 상에 존재하지 않으면 
		메모리에 프레임을 올리고 프레임과 페이지를 매핑시켜준다. */
		if (!vm_claim_page(addr))
		{
			if (rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr && addr <= USER_STACK)
			{
				/* 2. Page fault 발생 주소가 유저 스택 내에 있고, 스택 포인터보다 8바이트 밑에 있지 않으면 */
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
void vm_dealloc_page(struct page *page)
{
	destroy(page);
	free(page);
}

/* Claim the page that allocate on VA. */
bool vm_claim_page(void *va UNUSED)
{
	ASSERT(is_user_vaddr(va));

	struct page *page;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if (page == NULL)
		return false;

	return vm_do_claim_page(page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page(struct page *page)
{
	struct frame *frame = vm_get_frame();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* 프로세스의 pml4에 유저 메모리 page->va에서 
	   커널 주소 공간에 있는 프레임의 주소 frame->kva로 매핑한다. 
	   이제 pml4_get_page(plm4, page->va)를 통해 page가 매핑된 커널 가상 주소를 알 수 있다. */
	if (install_page(page->va, frame->kva, page->writable))
	{ // 유저페이지가 이미 매핑되었거나 메모리 할당 실패 시 false
		return swap_in(page, frame->kva);
	}
	return false;
}

/* Initialize new supplemental page table */
void supplemental_page_table_init(struct supplemental_page_table *spt UNUSED)
{
	hash_init(&spt->pages, hash_func, less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool supplemental_page_table_copy(struct supplemental_page_table *dst UNUSED,
								  struct supplemental_page_table *src UNUSED)
{

	struct hash_iterator i;

	/* 1. SRC의 해시 테이블의 각 bucket 내 elem들을 모두 복사한다. */
	hash_first(&i, &src->pages);
	while (hash_next(&i))
	{																				 // src의 각각의 페이지를 반복문을 통해 복사
		struct page *parent_page = hash_entry(hash_cur(&i), struct page, hash_elem); // 현재 해시 테이블의 element 리턴
		enum vm_type type = page_get_type(parent_page);								 // 부모 페이지의 type
		void *upage = parent_page->va;												 // 부모 페이지의 가상 주소
		bool writable = parent_page->writable;										 // 부모 페이지의 쓰기 가능 여부
		vm_initializer *init = parent_page->uninit.init;							 // 부모의 초기화되지 않은 페이지들 할당 위해
		void *aux = parent_page->uninit.aux;

		// 부모 페이지가 STACK이라면 setup_stack()
		if (parent_page->uninit.type & VM_MARKER_0)
		{
			setup_stack(&thread_current()->tf);
		}
		// 부모 타입이 uninit인 경우
		else if (parent_page->operations->type == VM_UNINIT)
		{
			if (!vm_alloc_page_with_initializer(type, upage, writable, init, aux))
				// 자식 프로세스의 유저 메모리에 UNINIT 페이지를 하나 만들고 SPT 삽입.
				return false;
		}
		// STACK도 아니고 UNINIT도 아니면 vm_init 함수를 넣지 않은 상태에서
		else
		{
			if (!vm_alloc_page(type, upage, writable)) // uninit 페이지 만들고 SPT 삽입.
				return false;
			if (!vm_claim_page(upage)) // 바로 물리 메모리와 매핑하고 Initialize한다.
				return false;
		}

		// UNIT이 아닌 모든 페이지(stack 포함)에 대응하는 물리 메모리 데이터를 부모로부터 memcpy
		if (parent_page->operations->type != VM_UNINIT)
		{
			struct page *child_page = spt_find_page(dst, upage);
			memcpy(child_page->frame->kva, parent_page->frame->kva, PGSIZE);
		}
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void supplemental_page_table_kill(struct supplemental_page_table *spt UNUSED)
{
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	struct hash_iterator i;

	hash_first(&i, &spt->pages);
	while (hash_next(&i))
	{
		struct page *page = hash_entry(hash_cur(&i), struct page, hash_elem);

		if (page->operations->type == VM_FILE) {
			do_munmap(page->va);
		}
	}
	hash_destroy(&spt->pages, spt_destructor);
}

void spt_destructor(struct hash_elem *e, void *aux)
{
	const struct page *p = hash_entry(e, struct page, hash_elem);
	free(p);
}

unsigned hash_func(const struct hash_elem *e, void *aux UNUSED)
{
	const struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux)
{
	const struct page *p_a = hash_entry(a, struct page, hash_elem);
	const struct page *p_b = hash_entry(b, struct page, hash_elem);
	return p_a->va < p_b->va;
}
