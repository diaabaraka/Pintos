#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
 
#include <list.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
 
void syscall_init (void);
 
/* File list element */
struct file_elem
{
int fd;
struct file *file;
struct list_elem elem;
};
void exitAndClose (struct intr_frame *f, int status); 

#endif /* userprog/syscall.h */

