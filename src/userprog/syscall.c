#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "userprog/pagedir.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include <list.h>
#include "devices/input.h"
#include "userprog/process.h"

static void syscall_handler (struct intr_frame *);

static void halt ();
static void exit (struct intr_frame *f);

static void wait (struct intr_frame *f);
static void create (struct intr_frame *f);
static void removee (struct intr_frame *f);
static void open (struct intr_frame *f);
static void exec(struct intr_frame *f);
static void read (struct intr_frame *f);
static void write (struct intr_frame *f);
static void close (struct intr_frame *f);
static void filesize (struct intr_frame *f);
static void tell (struct intr_frame *f);
 static void seek (struct intr_frame *f);

/* pointer validity */
static bool check_invalid_ptr (const void *ptr);

/* find file pointer from file descriptor */
static struct file *find_file (int fd); 
//------------------------------------------------------------------------------------
static struct lock create_remove_lock;
  
 static struct lock fileD_lock;
static struct lock read_lock;
static struct lock write_lock;

//-----------------------------------------------------------------------------------
  
void
exitAndClose (struct intr_frame *f, int status)
{
printf("%s: exit(%d)\n", thread_name (), status);
// printf("\n\n1\n");
/* close file descriptors */
struct thread *t = thread_current ();
 
struct list_elem *e;
 
while (!list_empty (&t->file_list))
{
//printf("\nbefore remove\n\n");
e = list_pop_back (&t->file_list);
//printf("\nafter remove\n\n");
struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
file_close (f_elem->file);
free (f_elem);
}
  // printf("\n\n2\n");

  
  
  /* free waited_children_list and children_list */
  while (!list_empty (&t->children_list))
    {
      e = list_pop_back (&t->children_list);
      struct child_elem *c_elem = list_entry (e, struct child_elem, elem);
      // free children from the global exit_list
       free_thread_from_exit_list (c_elem->pid);
      
       free (c_elem);
    } 
  
   //printf("\n\n3\n");

  
    add_thread_to_exited_list (t->tid, status);

  
   if (t->exec_file) 
    {
      file_allow_write (t->exec_file);
      file_close (t->exec_file);
    }

     
   /* release all the locks that have not already been released */
  while (!list_empty (&t->acquired_locks))
    {
      struct list_elem *e = list_front (&t->acquired_locks);
      struct lock *l = list_entry (e, struct lock, elem);
      lock_release (l);
    }

  // printf("\n\n5\n");

  thread_exit ();
  f->eax = status;
  //printf("\n\nEnd of exitAndClose\n\n");
 
}
 
 
static bool
check_invalid_ptr (const void *ptr)
{
if (!is_user_vaddr (ptr) || !pagedir_get_page (thread_current ()->pagedir, ptr) || ptr == NULL) // I used the first method of checking, functions in userprog/pagedir.c and in threads/vaddr.h.
return true;
return false;
 
}
 
 
void
syscall_init (void)
{
	lock_init (&fileD_lock);
  lock_init(&create_remove_lock);
 
  lock_init (&write_lock);
  lock_init (&read_lock);
  
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
 
static void
syscall_handler (struct intr_frame *f UNUSED)
{

 
if (check_invalid_ptr (f->esp))
{
exitAndClose (f,-1);
return;
}
 
int num = *((int *)f->esp);
 
switch (num)
{
case SYS_HALT:
halt ();
break;
 
case SYS_EXIT:
exit(f);
break;
 
case SYS_CREATE:
create(f);
break;
 
case SYS_OPEN:
open(f);
break;
 
case SYS_READ:
read(f);
break;
 
case SYS_CLOSE:
close(f);
break;
 
case SYS_WRITE:
write(f);
break;
 
case SYS_REMOVE:
removee(f);
break;
 
case SYS_EXEC:
 exec(f);
break;
case SYS_WAIT:
 wait(f);
break;
 
case SYS_FILESIZE:
   filesize(f);
break;
 
case SYS_SEEK:
  seek(f);
break;
case SYS_TELL:
   tell(f);
break;
 
default:
printf ("unknown system call!\n");
exitAndClose(f,-1);
break;
 
}
 
// f->eax = res;
 
}
 
//******************************************//
static void
halt ()
{
shutdown_power_off ();
}
//*************************************//
 
static void
exit (struct intr_frame *f)
{
 void *current_esp =f->esp+sizeof(void*); 
  
int status;
if (check_invalid_ptr (current_esp)) // check the first arg
status = -1; // non zero indicate error
else
status = *(int *)current_esp;
 
exitAndClose(f, status);
}
//******************************************************************//
static void wait (struct intr_frame *f)
{
 
     void *current_esp = f->esp + sizeof (void *);
     if(check_invalid_ptr(current_esp))
     {
       exitAndClose(f, -1);
    
        return;
      }
  //-------------------------------------------------
  // get pid of the thread 
  tid_t pid ; 
  pid = *(typeof (pid)*)current_esp; 
  //-----------------------------------------------
   struct thread *cur=thread_current();
 
    struct list_elem *e;
  
    for (e = list_begin (&cur->children_list); 
       e != list_end (&cur->children_list);
       e = list_next (e))
    {
       struct child_elem *c_elem = list_entry(e , struct child_elem , elem);
       
      if(c_elem->pid==pid)
       {
           if(c_elem->already_waited)
           {
              f->eax=-1;
             return ;
           
           }
        
           c_elem->already_waited=true;
           f->eax=process_wait(pid);
           return; 
                 
       }//end if 
    
    }// end children for loop 
   //here there is no child with this pid //
    f->eax=-1;
     
}// end function 

//*********************************************************//
static void
create (struct intr_frame *f) // takes two arguments (char *file name,initial size)
{
  void*  current_esp=f->esp+sizeof(void*);
if (check_invalid_ptr (current_esp)) 
{
exitAndClose(f, -1);
return;
}
  const char* filename;
  filename = *(typeof (filename)*) (current_esp); 
 if (check_invalid_ptr (filename)) 
{
exitAndClose(f, -1);
return;
}
//----------------------------------------------------------------------------------------------------------------------  
 
 current_esp+=sizeof(void*); 
if (check_invalid_ptr (current_esp)) 
{
exitAndClose(f, -1);
return;
}
 

unsigned initial_size = *(unsigned*)(current_esp);
  
  
  
  
  lock_acquire (&create_remove_lock);
f->eax = filesys_create (filename, initial_size);
 lock_release (&create_remove_lock);
}
 
//**********************************************//
 
static void
open (struct intr_frame *f) // takes one arg (file name)
{ // return fd if successful Otherwise -1
// fd=0 & fd=1 are reserved

static int next_fd = 2;
int fd;
 void *current_esp = f->esp + sizeof (void *);
if (check_invalid_ptr (current_esp))
{
exitAndClose(f, -1);
return;
}
 const char*filename;
    
 filename = * (typeof(filename) *) current_esp;
  
  if (check_invalid_ptr (filename))
    {
      exitAndClose (f, -1);
      return;
    }
  
  
struct file *file = filesys_open (filename);
 

 if (!file)
fd = -1;
else{
  lock_acquire (&fileD_lock);
   fd = next_fd++;
  lock_release (&fileD_lock);
}
struct thread *t = thread_current (); // when we open file we add it to the current process opened files
struct file_elem *f_elem = (struct file_elem *) malloc (sizeof (struct file_elem));
if (f_elem==NULL)
    {
      exitAndClose (f, -1);
      return;
    }
f_elem->fd =fd;
f_elem->file = file;
 
list_push_back (&t->file_list, &f_elem->elem);
 
f->eax = fd;
 
}
 
//*********************************************//
 
static void
read (struct intr_frame *f) // (fd,buffer,size) read size bytes from open fd into buffer
{ // return acually read or -1
 
 void *current_esp = f->esp + sizeof (void *);

if (check_invalid_ptr (current_esp))
{
exitAndClose(f, -1);
return;
}
 
int fd = *(int *)current_esp;
//---------------------------------------------------------------------------------------------------------------------------
 current_esp+=sizeof(void*); 

if (check_invalid_ptr (current_esp))
{
exitAndClose(f, -1);
return;
} 

 const void *buffer;
   buffer= *(typeof(buffer) *)current_esp;


 //--------------------------------------------------------------------------------------------
  
  current_esp+=sizeof(void*);
 if (check_invalid_ptr (current_esp))
 {
   exitAndClose(f, -1); 
   return;
 }  
   
  unsigned sizee = *(unsigned*)current_esp;
// ----------------------------------------------------------------------------------------------------------------------
 
if (fd == STDOUT_FILENO || fd < -1||check_invalid_ptr (buffer) ||
      check_invalid_ptr (buffer + sizee) )
{
exitAndClose(f, -1);
return;
}
 
ASSERT (fd >= 0);
 
if (fd == STDIN_FILENO)
{
f->eax = input_getc ();
return;
}
 
struct file *file = find_file (fd); // find which file corresponds to this fd
 
if (file != NULL)
{
  lock_acquire (&read_lock);
f->eax = file_read(file, buffer, sizee);
   lock_release (&read_lock);
}
else
exitAndClose(f, -1);
 
 
}
 
//******************************************//
 
 
static struct file *
find_file (int fd)
{
struct thread *t = thread_current ();
struct list_elem *e;
 
for (e = list_begin (&t->file_list); e != list_end (&t->file_list); e = list_next (e))
{
struct file_elem *f_elem = list_entry (e, struct file_elem, elem);
if (f_elem->fd == fd)
return f_elem->file;
}
return NULL;
}
 
//****************************************************//
 
static void
write (struct intr_frame *f) //(int fd, const void *buffer, unsigned size)
{ //write size bytes from buffer to open file fd return actually written
 
 
 void *current_esp = f->esp + sizeof (void *);

if (check_invalid_ptr (current_esp))
{
exitAndClose(f, -1);
return;
}
 
int fd = *(int *)current_esp;
//---------------------------------------------------------------------------------------------------------------------------
 current_esp+=sizeof(void*); 

if (check_invalid_ptr (current_esp))
{
exitAndClose(f, -1);
return;
} 

 const void *buffer;
   buffer= *(typeof(buffer) *)current_esp;

 //--------------------------------------------------------------------------------------------
  
  current_esp+=sizeof(void*);
 if (check_invalid_ptr (current_esp))
 {
   exitAndClose(f, -1); 
   return;
 }  
   
  unsigned sizee = *(unsigned*)current_esp;
// ----------------------------------------------------------------------------------------------------------------------
if (fd == STDIN_FILENO || fd < -1 ||check_invalid_ptr (buffer)|| check_invalid_ptr (buffer + sizee))
{
exitAndClose(f, -1);
return;
}
 
if (fd == STDOUT_FILENO){ // write to the console
  
putbuf(buffer, sizee);
f->eax = sizee;

return;
}
 
struct file *file=find_file(fd);
if(file!=NULL){

lock_acquire(&read_lock); 
  lock_acquire(&write_lock);
f->eax =file_write(file,buffer,sizee);
 lock_release (&write_lock);
      lock_release (&read_lock);
  
} else{
exitAndClose(f,-1);
}
 
 
}
 
//********************************************************//
 
static void
close (struct intr_frame *f) //(int fd)
{
void *cur_sp = f->esp + sizeof (void *);

  if (check_invalid_ptr (cur_sp) )// validate them
{
exitAndClose(f, -1);
return;
}
 int fd ;
      
 fd = *(int *)cur_sp;
 
struct thread *t = thread_current ();
 
struct list_elem *e;
 
for (e = list_begin (&t->file_list); e != list_end (&t->file_list);e = list_next (e))
{
struct file_elem *elem = list_entry (e, struct file_elem, elem);
if (elem->fd == fd)
{
file_close (elem->file);
list_remove (e);
free (elem);
return;
}
}
 
exitAndClose(f, -1);
}
 
 
//*************************************************************//
 
static void
removee (struct intr_frame *f)
{
  
void *current_esp = f->esp + sizeof (void *);

  if (check_invalid_ptr (current_esp)) // validate them
{
exitAndClose(f, -1);
return;
}
 
 char *filename = *(char *) current_esp;
 
   if (check_invalid_ptr (filename))
    {
      exitAndClose (f, -1);
      return;
    }
  lock_acquire(&create_remove_lock);
f->eax = filesys_remove (filename);
  lock_release(&create_remove_lock);
}
 
//******************************************************************//


static void exec (struct intr_frame *f)
{
  
  void *cur_sp = f->esp + sizeof (void *);

  if (check_invalid_ptr (cur_sp)) // validate them
{
exitAndClose(f, -1);
return;
}
  const char* cmd_line ;
  
   cmd_line = *(typeof(cmd_line)*)cur_sp;
  
  if(check_invalid_ptr(cmd_line)){
  exitAndClose(f,-1);
  return;
  }
  
   
   //printf("\nHere\n");
   tid_t pid = process_execute (cmd_line);

   //printf("\nBefore IFFF\n\n"); 
   if(pid == TID_ERROR)
    {
     f->eax = -1;
      return;
    
    }
       
      f->eax = pid; 
      
 // printf("\nFinishExec\n\n");  


}

//-------------------------------------------------------------------------------------------------------------------------------------------------


static void
filesize (struct intr_frame *f)   // takes one param (int fd)
{
  int fd;
  
  void *current_esp = f->esp + sizeof (void *);

  if (check_invalid_ptr (current_esp)) // validate them
{
exitAndClose(f, -1);
return;
}
  
  fd=*(int*)current_esp;
  
  struct file *file = find_file (fd);
  if (file != NULL)
    {      
      lock_acquire (&write_lock);
      f->eax = file_length (file);
      lock_release(&write_lock); 
    }
  else
    exitAndClose (f, -1);
}
      
      //*******************************************/
      
      
      static void
seek (struct intr_frame *f)       // takes  (int fd, unsigned position)
{
  int fd;
  unsigned position;
  void *current_esp = f->esp + sizeof (void *);
 
  if (check_invalid_ptr (current_esp)) // validate them
  {
  exitAndClose(f, -1);
  return;
  }
  fd=*(int*)current_esp;
      
   //----------------------------------------------------------------------------   
  current_esp += sizeof (void *);
   if (check_invalid_ptr (current_esp)) // validate them
  {
  exitAndClose(f, -1);
  return;
  }
       
    position=*(unsigned*)current_esp;   
  //----------------------------------------------------------------------------------------

  struct file *file = find_file (fd);
  if (file != NULL)
    {
      
      lock_acquire (&read_lock);
      lock_acquire (&write_lock);
      file_seek (file, position);
      lock_release(&write_lock);
      lock_release(&read_lock);
     
    }
  else
    exitAndClose (f, -1);
}
      
      //**********************************************************//
       
static void 
tell (struct intr_frame *f) 				// takes (int fd)
{ 
  int fd;
  void *current_esp = f->esp + sizeof (void *);
   if (check_invalid_ptr (current_esp)) // validate them
  {
  exitAndClose(f, -1);
  return;
  }
    fd=*(int*)current_esp;     
   
  struct file *file = find_file (fd);
  if (file != NULL)
    {
      lock_acquire (&read_lock);
      lock_acquire (&write_lock);
      f->eax = file_tell (file);
     lock_release (&write_lock);
      lock_release (&read_lock);
    }
  else
     exitAndClose(f, -1);

       }

       //*********************************************//
