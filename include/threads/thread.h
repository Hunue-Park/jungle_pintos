#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/interrupt.h"
#include "threads/synch.h"
#ifdef VM
#include "vm/vm.h"
#endif


/* States in a thread's life cycle. */
enum thread_status {
	THREAD_RUNNING,     /* Running thread. */
	THREAD_READY,       /* Not running but ready to run. */
	THREAD_BLOCKED,     /* Waiting for an event to trigger. */
	THREAD_DYING        /* About to be destroyed. */
};

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

/* A kernel thread or user process.
 *
 * Each thread structure is stored in its own 4 kB page.  The
 * thread structure itself sits at the very bottom of the page
 * (at offset 0).  The rest of the page is reserved for the
 * thread's kernel stack, which grows downward from the top of
 * the page (at offset 4 kB).  Here's an illustration:
 *
 *      4 kB +---------------------------------+
 *           |          kernel stack           |
 *           |                |                |
 *           |                |                |
 *           |                V                |
 *           |         grows downward          |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           |                                 |
 *           +---------------------------------+
 *           |              magic              |
 *           |            intr_frame           |
 *           |                :                |
 *           |                :                |
 *           |               name              |
 *           |              status             |
 *      0 kB +---------------------------------+
 *
 * The upshot of this is twofold:
 *
 *    1. First, `struct thread' must not be allowed to grow too
 *       big.  If it does, then there will not be enough room for
 *       the kernel stack.  Our base `struct thread' is only a
 *       few bytes in size.  It probably should stay well under 1
 *       kB.
 *
 *    2. Second, kernel stacks must not be allowed to grow too
 *       large.  If a stack overflows, it will corrupt the thread
 *       state.  Thus, kernel functions should not allocate large
 *       structures or arrays as non-static local variables.  Use
 *       dynamic allocation with malloc() or palloc_get_page()
 *       instead.
 *
 * The first symptom of either of these problems will probably be
 * an assertion failure in thread_current(), which checks that
 * the `magic' member of the running thread's `struct thread' is
 * set to THREAD_MAGIC.  Stack overflow will normally change this
 * value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
 * the run queue (thread.c), or it can be an element in a
 * semaphore wait list (synch.c).  It can be used these two ways
 * only because they are mutually exclusive: only a thread in the
 * ready state is on the run queue, whereas only a thread in the
 * blocked state is on a semaphore wait list. */
struct thread {
	/* Owned by thread.c. */
	tid_t tid;                          /* Thread identifier. */
	enum thread_status status;          /* Thread state. */
	char name[16];                      /* Name (for debugging purposes). */
	int priority;                       /* Priority. */

	/* Shared between thread.c and synch.c. */
	struct list_elem elem;              /* List element. */

	// 알람시계 일어날 시간. 
	int64_t wakeup;

	// priority donation
	int init_priority;  // 최초 스레드 우선순위 저장.

	struct lock *wait_on_lock;  // 현재 스레드가 요청했는데 받지못한 lock. 기다리는중
	struct list donations; // 자신에게 priority 를 나누어준 '쓰레드'의 리스트
	struct list_elem donation_elem; // 위의 스레드 리스트를 관리하기위한 element. thread 구조체의 elem과 구분.

	// Project 2
	int exit_status;    // child 프로세스의 exit status를 parent에게 전달하기 위함.
	struct file **fd_table;  
	struct file *running;
	struct semaphore wait_sema;  // parent 프로세스가 child 프로세스를 기다리기 위함.
	struct semaphore free_sema;  // parent 프로세스의 종료를 child 프로세스의 종료 시그널을 받을때까지로 미룸
	struct semaphore fork_sema;  // child 의 fork 가 완료될때까지 parent는 기다린다. (__do_fork)
	struct list child_list;  // children 리스트
	struct list_elem child_elem;  // 현재 스레드를 children list에 집어넣기 위함.
	int fd_idx;   // fdTable의 인덱스
	struct intr_frame parent_if;  // 자신의 intr_frame을 보존하고 fork시에 child 프로세스에 전달.
	int stdin_count;
	int stdout_count;
#ifdef USERPROG
	/* Owned by userprog/process.c. */
	uint64_t *pml4;                     /* Page map level 4 */
#endif
#ifdef VM
	/* Table for whole virtual memory owned by thread. */
	struct supplemental_page_table spt;
	void *stack_bottom;
	void *rsp_stack;
#endif

	/* Owned by thread.c. */
	struct intr_frame tf;               /* Information for switching */
	unsigned magic;                     /* Detects stack overflow. */
};

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

int thread_get_priority (void);
void thread_set_priority (int);

// priority donation
bool
thread_compare_donate_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);
void 
donate_priority (void);
void
remove_with_lock (struct lock *lock);
void
refresh_priority (void);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void do_iret (struct intr_frame *tf);

// 알람시계 함수 선언
void thread_sleep (int64_t ticks);
void thread_awake (int64_t ticks);

// priority scheduling 함수
void thread_test_preemption (void);
bool thread_compare_priority (const struct list_elem *a, const struct list_elem *b, void *aux UNUSED);

// project2
#define FDT_PAGES 3
#define FDCOUNT_LIMIT FDT_PAGES *(1 << 9) 


#endif /* threads/thread.h */

