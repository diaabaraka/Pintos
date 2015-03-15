#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif
/*mlfqs libs*/
#include <threads/real.h>
#include "devices/timer.h"

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

 /* Lock used for all_list */
static struct lock all_list_lock;

/* Lock used for exited_list */
static struct lock exited_list_lock; 
  
/* Stack frame for kernel_thread(). */
struct kernel_thread_frame
{
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
};

//----------------------------------------------------------------------------

struct exited_elem
{
  tid_t pid ;
  int status ;
  struct list_elem  elem;
  
};

//---------------------------------------------------------------------------
 
  static  struct list exited_list ;
//---------------------------------------------------------------------------
/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;



//-------------------------------------------

/*mlfqs load average value of the system*/
static int mlfqs_load_avg;


/*mlfqs ready list (queue for each priority)*/

static struct list mlfqs_ready_list[PRI_MAX+1];       /* there are 64 available priority */

//-------------------------------------------





static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);


//-----------------------------------------------------------------------
static struct thread * get_thread (tid_t tid);

  
//-----------------------------------------------------------------------


//------------------------------------------------------------------------------------------------------------------------

 static struct thread * get_thread (tid_t tid){

 // printf("\nstart gt thread\n\n");
 struct thread* return_thread =NULL;
  lock_acquire (&all_list_lock);
  struct list_elem *e ;
  
    for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
       struct thread *t=list_entry(e,struct thread , elem);
       if(t->tid==tid)
       {
          //printf("\n\nget the thread we search for \n\n");
         return_thread=t;
        break;
       }

    
    }

  lock_release(&all_list_lock);
  
   //printf("\nexitformgetThread\n\n");
  return return_thread;

}  



//-------------------------------------------------------------------------------------------------------------------------------------------

  
bool
priority_compare (const struct list_elem *a,
                  const struct list_elem *b, void *aux)
{
    ASSERT (aux==NULL);
    struct thread *t1 = list_entry (a, struct thread, elem);
    struct thread *t2 = list_entry (b, struct thread, elem);
    if (t1->priority > t2->priority)
        return true;
    return false;
}



/* One lock in a list */


/* MLFQS methods implementation */
/*Calculate the new priority based on the nice of the thread*/
int
mlfqs_priority_calc(const struct thread *t)
{
    int recent_CPU=t->mlfqs_recent_CPU;
    int nice=t->mlfqs_nice;

    /* priority =PRI_MAX-(recent_CPU/4) - (nice * 2) */

    int new_priority=div_fixed_int(recent_CPU,4);
    new_priority=-fixed_to_int_floor(sub_fixed_int(new_priority,(PRI_MAX-(2*nice))));
    /* Adjusting the priority to lie in the valid range PRI_MIN to PRI_MAX */

	--new_priority;
    if(new_priority>PRI_MAX)new_priority=PRI_MAX;

    else if(new_priority<PRI_MIN)new_priority=PRI_MIN;


return new_priority;
   
}
void mlfqs_priority_update(struct thread *t, void *aux UNUSED){

	 t->priority= mlfqs_priority_calc(t);
    	t->old_priority=  t->priority;


}
/* Calculate the new recent_CPU time */
void
mlfqs_recent_CPU_calc(struct thread *t, void *aux UNUSED)
{

    int recent_CPU=t->mlfqs_recent_CPU;
    int nice=t->mlfqs_nice;
    /** recent_cpu = (2*load_avg)/(2*load_avg + 1) * recent_cpu + nice */

    int new_recent_CPU=mul_fixed_int(mlfqs_load_avg,2);
    new_recent_CPU=div_two_fixed(new_recent_CPU,add_fixed_int(new_recent_CPU,1));
    new_recent_CPU=mul_two_fixed(new_recent_CPU,recent_CPU);
    new_recent_CPU=add_fixed_int(new_recent_CPU,nice);
    t->mlfqs_recent_CPU= new_recent_CPU;
}/* Calculate the new load average */
int
mlfqs_load_avg_calc(void)
{
    int ready_threads=mlfqs_ready_count();
    /*the number of threads that are either running or ready to run at
    time of update (not including the idle thread.)*/
    if(thread_current()!=idle_thread) ready_threads ++;

    /* load_avg = (59/60)*load_avg + (1/60)*ready_threads */

    int new_load_avg=mul_fixed_int(mlfqs_load_avg,59);
    new_load_avg=div_fixed_int(new_load_avg,60);
    int term=int_to_fixed(ready_threads);
    new_load_avg=add_two_fixed(new_load_avg,div_fixed_int(term,60));
    return new_load_avg;
}

/*Count the ready threads in the mlfqs ready list */
int
mlfqs_ready_count()
{
    int cnt=0;
    int i=0;
    for(; i<=PRI_MAX; i++)
    {
        cnt+=list_size (&mlfqs_ready_list[i]);


    }
    return cnt;




}
/* Sorting the mlfqs list after chaging priorities of the threads */
void
mlfqs_list_sort(void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    int i=0;
    for(; i<=PRI_MAX; i++)
    {

        struct list_elem *e = list_begin (&mlfqs_ready_list[i]);

        while (e != list_end (&mlfqs_ready_list[i]))
        {
            struct thread *t = list_entry (e, struct thread, elem);
            if (t->priority != i)
            {
                e = list_remove (e);
                list_push_back (&mlfqs_ready_list[t->priority], &t->elem);
            }
            else
            {
                e = list_next (e);
            }
        }


    }





}


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void)
{
    ASSERT (intr_get_level () == INTR_OFF);

    lock_init (&tid_lock);
  
    lock_init (&all_list_lock);
    
    lock_init (&exited_list_lock);
     
    if(thread_mlfqs)
    {
        /* Intialize the mlfqs ready list */
        int i=0;
        for(; i<=PRI_MAX; i++)
        {

            list_init(&mlfqs_ready_list[i]);

        }



    }
    else
    {

        list_init (&ready_list);
    }
    list_init(&lock_list);
    list_init (&all_list);
    list_init (&exited_list);

    /* Set up a thread structure for the running thread. */
    initial_thread = running_thread ();
    init_thread (initial_thread, "main", PRI_DEFAULT);
    initial_thread->status = THREAD_RUNNING;
    initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void)
{
    /* Create the idle thread. */
    struct semaphore idle_started;
    sema_init (&idle_started, 0);
    thread_create ("idle", PRI_MIN, idle, &idle_started);

    /* Start preemptive thread scheduling. */
    intr_enable ();

    /* Wait for the idle thread to initialize idle_thread. */
    sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void)
{
    struct thread *t = thread_current ();

    /* Update statistics. */
    if (t == idle_thread)
        idle_ticks++;
#ifdef USERPROG
    else if (t->pagedir != NULL)
        user_ticks++;
#endif
    else
        kernel_ticks++;

    /*Handling mlfqs*/
    if(thread_mlfqs)
    {
        /* recent_cpu is incremented by 1 for the running thread only*/
        if(t!=idle_thread)
            t->mlfqs_recent_CPU=add_fixed_int(t->mlfqs_recent_CPU,1);

        /* once per second the value of recent cpu is recalculated for every thread */
        if(timer_ticks ()%TIMER_FREQ==0)
        {
            /* Update mlfqs_load_avg */
            mlfqs_load_avg=mlfqs_load_avg_calc();

            /*Recalculate recent_CPU value for all threads */
            thread_foreach(mlfqs_recent_CPU_calc,NULL);

        }
        /*Thread priority is recalculated once every fourth clock tick*/
        if(timer_ticks()%4==0)
        {

            /* Update priority of all threads */
            thread_foreach(mlfqs_priority_update,NULL);
            /* Sorting the mlfqs list after chaging priorities of the threads */
            mlfqs_list_sort();

        }







    }

    /* Enforce preemption. */
    if (++thread_ticks >= TIME_SLICE)
        intr_yield_on_return ();
}


/* Prints thread statistics. */
void
thread_print_stats (void)
{
    printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
            idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux)
{
    struct thread *t;
    struct kernel_thread_frame *kf;
    struct switch_entry_frame *ef;
    struct switch_threads_frame *sf;
    tid_t tid;
    enum intr_level old_level;

    ASSERT (function != NULL);

    /* Allocate thread. */
    t = palloc_get_page (PAL_ZERO);
    if (t == NULL)
        return TID_ERROR;

    /* Initialize thread. */
    init_thread (t, name, priority);
    tid = t->tid = allocate_tid ();
   //---------------------------------------------------------
   
    t->parent=thread_current();
	
    //-------------------------------------------------------------------
if (thread_mlfqs) 
    {
    /* Intializing the nice value and recent_CPU of the thread t */
    t->mlfqs_nice=thread_current()->mlfqs_nice;
    t->mlfqs_recent_CPU=thread_current()->mlfqs_recent_CPU;
}
    //-------------------------------------------------------------------
    /* Prepare thread for first run by initializing its stack.
       Do this atomically so intermediate values for the 'stack'
       member cannot be observed. */
    old_level = intr_disable ();

    /* Stack frame for kernel_thread(). */
    kf = alloc_frame (t, sizeof *kf);
    kf->eip = NULL;
    kf->function = function;
    kf->aux = aux;

    /* Stack frame for switch_entry(). */
    ef = alloc_frame (t, sizeof *ef);
    ef->eip = (void (*) (void)) kernel_thread;

    /* Stack frame for switch_threads(). */
    sf = alloc_frame (t, sizeof *sf);
    sf->eip = switch_entry;
    sf->ebp = 0;

    intr_set_level (old_level);

    /* Add to run queue. */
    thread_unblock (t);

    return tid;
}

/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void)
{
    ASSERT (!intr_context ());
    ASSERT (intr_get_level () == INTR_OFF);
    
    thread_current ()->status = THREAD_BLOCKED;
    schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t)
{

    enum intr_level old_level;

    ASSERT (is_thread (t));
    old_level = intr_disable ();
    ASSERT (t->status == THREAD_BLOCKED);
    //--------------------------------------------------

    if(thread_mlfqs)
    {

        list_push_back (&mlfqs_ready_list[t->priority], &t->elem);


    }
    //--------------------------------------------------
    else
    {
        list_sort (&ready_list, priority_compare, NULL);
        list_insert_ordered (&ready_list, &t->elem, priority_compare, NULL);


    }
    t->status = THREAD_READY;
    if ((thread_current()->priority < t->priority) &&
            thread_current() != idle_thread)
    {
        if (intr_context ())
            intr_yield_on_return ();
        else
            thread_yield ();
    }
    intr_set_level (old_level);


}

/* Returns the name of the running thread. */
const char *
thread_name (void)
{
    return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void)
{
    struct thread *t = running_thread ();

    /* Make sure T is really a thread.
       If either of these assertions fire, then your thread may
       have overflowed its stack.  Each thread has less than 4 kB
       of stack, so a few big automatic arrays or moderate
       recursion can cause stack overflow. */
    ASSERT (is_thread (t));
    ASSERT (t->status == THREAD_RUNNING);

    return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void)
{
    return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void)
{
 // printf("\nStart of thread exit\n\n");
    ASSERT (!intr_context ());
    /* Release any locks held by the thread. */
    struct list_elem *e;
    struct thread *cur = thread_current ();
    for (e = list_begin (&lock_list); e != list_end (&lock_list);
            e = list_next (e))
    {
        struct lock *lock = list_entry (e, struct lock_elem, elem)->lock;
        struct thread *holder = lock->holder;
        if (holder == cur)
            lock_release (lock);
    }

#ifdef USERPROG
    process_exit ();
#endif
      //----------------------------------------------------
     
     
     struct thread *parent = thread_current()->parent;
     
//printf("\n%s\n\n",parent);
  if(parent)
     {
        sema_up(&parent->child_sema);
  //         printf("\nSema up parent\n\n");
     }
     
     //----------------------------------------------------
  
    /* Remove thread from all threads list, set our status to dying,
       and schedule another process.  That process will destroy us
       when it calls thread_schedule_tail(). */
    intr_disable ();
    lock_acquire(&all_list_lock);
    list_remove (&thread_current()->allelem);
    lock_release(&all_list_lock); 

    thread_current ()->status = THREAD_DYING;
    schedule ();
    //   printf("\nhere333333333\n\n");

    NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void)
{

    struct thread *cur = thread_current ();
    enum intr_level old_level;

    ASSERT (!intr_context ());
    old_level = intr_disable ();
    if (cur != idle_thread)
    {
        if(thread_mlfqs)
            list_push_back (&mlfqs_ready_list[cur->priority], &cur->elem);
        else
            list_insert_ordered (&ready_list, &cur->elem, priority_compare, NULL);
    }
    cur->status = THREAD_READY;
    schedule ();
    intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
    struct list_elem *e;

    ASSERT (intr_get_level () == INTR_OFF);

    for (e = list_begin (&all_list); e != list_end (&all_list);
            e = list_next (e))
    {
        struct thread *t = list_entry (e, struct thread, allelem);
        func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority)
{


    enum intr_level old_level = intr_disable ();


   struct thread *cur = thread_current ();
  if (cur->priority == cur->old_priority){
  // either donation finished or new thread and no donation occurs yet.
    cur->priority = new_priority;
  }

//   current->prirorty !=old prirorty >> (donation occured ) 
// we  will change the old priority and deny to change the current priorty untill the donation finished  
  
 cur->old_priority = new_priority;

  if (cur->priority < next_thread_to_run()->priority)
  thread_yield ();

  intr_set_level (old_level);

}

/* Returns the current thread's priority. */
int
thread_get_priority (void)
{
    enum intr_level old_level = intr_disable ();
    int pri = thread_current()->priority;
    intr_set_level (old_level);

    return pri;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int new_nice)
{
    if(thread_mlfqs)
    {
        enum intr_level old_level = intr_disable ();
        /* Set the new nice value */
        thread_current()->mlfqs_nice=new_nice;
       
        intr_set_level (old_level);
    }

}

/* Returns the current thread's nice value. */
int
thread_get_nice (void)
{
    enum intr_level old_level = intr_disable ();

    int nice= thread_current()->mlfqs_nice;

    intr_set_level (old_level);

    return nice;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void)
{
    enum intr_level old_level = intr_disable ();




    /* 100 * mlfqs_load_avg */
    int new_load= fixed_to_int_round(mul_fixed_int(mlfqs_load_avg,100));

    intr_set_level (old_level);
    return new_load;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void)
{
    enum intr_level old_level = intr_disable ();

    /* 100 * recent_cpu */
    int new_recent= fixed_to_int_round(mul_fixed_int(thread_current()->mlfqs_recent_CPU,100));


    intr_set_level (old_level);
    return new_recent;
}
/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED)
{
    struct semaphore *idle_started = idle_started_;
    idle_thread = thread_current ();
    sema_up (idle_started);

    for (;;)
    {
        /* Let someone else run. */
        intr_disable ();
        thread_block ();

        /* Re-enable interrupts and wait for the next one.

           The `sti' instruction disables interrupts until the
           completion of the next instruction, so these two
           instructions are executed atomically.  This atomicity is
           important; otherwise, an interrupt could be handled
           between re-enabling interrupts and waiting for the next
           one to occur, wasting as much as one clock tick worth of
           time.

           See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
           7.11.1 "HLT Instruction". */
        asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux)
{
    ASSERT (function != NULL);

    intr_enable ();       /* The scheduler runs with interrupts off. */
    function (aux);       /* Execute the thread function. */
    thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void)
{
    uint32_t *esp;

    /* Copy the CPU's stack pointer into `esp', and then round that
       down to the start of a page.  Because `struct thread' is
       always at the beginning of a page and the stack pointer is
       somewhere in the middle, this locates the curent thread. */
    asm ("mov %%esp, %0" : "=g" (esp));
    return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
    return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
    ASSERT (t != NULL);
    ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
    ASSERT (name != NULL);

    memset (t, 0, sizeof *t);
    t->status = THREAD_BLOCKED;
    strlcpy (t->name, name, sizeof t->name);
    t->stack = (uint8_t *) t + PGSIZE;
    t->blocking_lock = NULL;
    if(thread_mlfqs)
    {
        t->mlfqs_nice=0;
        t->mlfqs_recent_CPU=0;
	
	
    }
    else
    {
        t->priority = priority;
        t->old_priority = priority;
    }
    t->magic = THREAD_MAGIC;
    list_push_back (&all_list, &t->allelem);
    list_init (&t->acquired_locks);
    list_init(&t->children_list);
    
    list_init (&t->file_list);
    sema_init (&t->child_sema, 0);
   
    
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size)
{
    /* Stack data is always allocated in word-size units. */
    ASSERT (is_thread (t));
    ASSERT (size % sizeof (uint32_t) == 0);

    t->stack -= size;
    return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */



static struct thread *
next_thread_to_run (void)
{
    if(!thread_mlfqs)
    {
        if (list_empty (&ready_list))
            return idle_thread;
        else
        {

            list_sort (&ready_list, priority_compare, NULL);
            return  list_entry (list_begin(&ready_list), struct thread, elem);

        }
    }

    else
    {

        int i;
        for (i = PRI_MAX; i>=0; i--)
        {
            if (list_size (&mlfqs_ready_list[i]) > 0)
            {
                return list_entry (list_pop_front (&mlfqs_ready_list[i]),
                                   struct thread, elem);

            }
        }

    }
    return idle_thread;

}



/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
    struct thread *cur = running_thread ();

    ASSERT (intr_get_level () == INTR_OFF);

    /* Mark us as running. */
    cur->status = THREAD_RUNNING;

    /* Start new time slice. */
    thread_ticks = 0;

#ifdef USERPROG
    /* Activate the new address space. */
    process_activate ();
#endif

    /* If the thread we switched from is dying, destroy its struct
       thread.  This must happen late so that thread_exit() doesn't
       pull out the rug under itself.  (We don't free
       initial_thread because its memory was not obtained via
       palloc().) */
    if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread)
    {
        ASSERT (prev != cur);
        palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void)
{
    /* chcek first for rond robin sechudler >> thread_mlfqs=false*/
  

    struct thread *cur = running_thread ();
    struct thread *next = next_thread_to_run ();
    struct thread *prev = NULL;


    ASSERT (intr_get_level () == INTR_OFF);
    ASSERT (cur->status != THREAD_RUNNING);
    ASSERT (is_thread (next));

    list_remove(&next->elem);
    if (cur != next)
        prev = switch_threads (cur, next);     // prev=next's prev  we can make it prev=cur

    thread_schedule_tail (prev);
  
 //  printf("\nendshecule\n\n");
}


/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void)
{
    static tid_t next_tid = 1;
    tid_t tid;

    lock_acquire (&tid_lock);
    tid = next_tid++;
    lock_release (&tid_lock);

    return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);



//-----------------------------------------------------------------------------------------------------------------------
//add a exit thread with specific status 
void add_thread_to_exited_list (tid_t pid, int status){ // called from  exitAndclose method  

struct exited_elem *e_elem = (struct exited_elem*) malloc (sizeof
           (struct exited_elem));
  ASSERT(e_elem);

 e_elem->pid =pid ;
  
 e_elem->status =status; 
 lock_acquire (&exited_list_lock);
 list_push_back(&exited_list,&e_elem->elem);
 lock_release(&exited_list_lock);
}


void free_thread_from_exit_list (tid_t pid) //called from exitAndclose method
{
  struct list_elem *e;
  lock_acquire (&exited_list_lock);
  for (e = list_begin (&exited_list); e != list_end (&exited_list);
       e = list_next (e))
    
  {
       struct exited_elem *e_elem=list_entry(e,struct exited_elem, elem);
    
       if(e_elem->pid==pid)
       {
         list_remove(e);
         free(e_elem);
         
         break;
         
       }
 
  }
lock_release (&exited_list_lock);
  
  
}

int get_exit_status (tid_t pid){


int status =-1;
  
   struct list_elem *e;
  lock_acquire (&exited_list_lock);
   for (e = list_begin (&exited_list); e != list_end (&exited_list);
       e = list_next (e))
   {
         struct exited_elem *e_elem=list_entry(e,struct exited_elem,elem);
     
         if(e_elem->pid==pid)
         {
            status=e_elem->status;
            break;
         
         }
     
 
   
   }
  
  lock_release (&exited_list_lock);
  return status;
  
 
}









