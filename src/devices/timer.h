#ifndef DEVICES_TIMER_H
#define DEVICES_TIMER_H

#include <round.h>
#include <stdint.h>
#include <list.h>
#include "threads/synch.h"



/* Number of timer interrupts per second. */
#define TIMER_FREQ 100

void timer_init (void);
void timer_calibrate (void);

int64_t timer_ticks (void);
int64_t timer_elapsed (int64_t);

/* Sleep and yield the CPU to other threads. */
void timer_sleep (int64_t sleep_ticks);
void timer_msleep (int64_t milliseconds);
void timer_usleep (int64_t microseconds);
void timer_nsleep (int64_t nanoseconds);

/* Busy waits. */
void timer_mdelay (int64_t milliseconds);
void timer_udelay (int64_t microseconds);
void timer_ndelay (int64_t nanoseconds);

void timer_print_stats (void);

/* Comparable function for sorting list elements. */
bool comparable(struct list_elem *e1,struct list_elem *e2,void*aux);


/* struct for sleep element */
struct sleep_elem{

  int64_t wakeup_time;		/* wakeup time*/
  struct list_elem elem;	/* List element. */
  struct semaphore sleep_sema; /* semaphore count for specific wakeup time*/
  

};
/*end of struct */


#endif /* devices/timer.h */












