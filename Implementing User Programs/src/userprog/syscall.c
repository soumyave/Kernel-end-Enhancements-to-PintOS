#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "list.h"
#include "process.h"

static void syscall_handler (struct intr_frame *);
struct file_descriptor_struct* list_search(struct list* files, int file_id);
void *check_validity(int arg_1,int arg_2);
void read_write(struct intr_frame *f UNUSED,char type);
struct file_descriptor_struct *file_descriptor_pointer;
int syscall_type;
int *stack_pointer;
int index;
uint8_t *buffer;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}
/* The arg for system calls with 1 argument will be in the stack at 
   the location esp+1(where esp is the current stack pointer), args for 
   system calls with 2 argument will be in the stack at the location 
   esp+4 and esp+5 and args for system calls with 3 argument will be 
   in the stack at the location esp+5,esp+6 and esp+7.
   The system call arguments will be validated before execution of 
   system calls. Incase of failure during validation, the process will 
   be killed.
   The values returned by the system calls will be stored in the eax register. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
    stack_pointer = f->esp;
    user_address_validity(stack_pointer);
    syscall_type = *stack_pointer;
    switch (syscall_type)
    {
	case SYS_HALT:
	    shutdown_power_off();
	    break;

	case SYS_EXIT:
	    user_address_validity(stack_pointer+1);
	    exit_process(*(stack_pointer+1));
	    break;

	case SYS_WAIT:
	    user_address_validity(stack_pointer+1);
	    f->eax = process_wait(*(stack_pointer+1));
	    break;
        
	case SYS_EXEC:
	    user_address_validity(stack_pointer+1);
	    user_address_validity(*(stack_pointer+1));
	    f->eax = exec_process(*(stack_pointer+1));
	    break;

	case SYS_CREATE:
	    user_address_validity(stack_pointer+5);
	    user_address_validity(*(stack_pointer+4));
	    file_lock_acquire();
	    f->eax = filesys_create(*(stack_pointer+4),*(stack_pointer+5));
	    file_lock_release();
	    break;

	case SYS_REMOVE:
	    user_address_validity(stack_pointer+1);
	    user_address_validity(*(stack_pointer+1));
	    file_lock_acquire();
	    file_remove(f,*(stack_pointer+1));
	    file_lock_release();
	    break;

	case SYS_OPEN:
	    user_address_validity(stack_pointer+1);
	    user_address_validity(*(stack_pointer+1));
	    file_lock_acquire();
            struct file_descriptor_struct *file_ptr = filesys_open (*(stack_pointer+1));
	    file_lock_release();
	    file_open_this(f,file_ptr);
	    break;

        case SYS_CLOSE:
	    user_address_validity(stack_pointer+1);
	    file_lock_acquire();
	    file_close_this(&thread_current()->file_descriptor,*(stack_pointer+1));
	    file_lock_release();
	    break;

	case SYS_FILESIZE:
	    user_address_validity(stack_pointer+1);
	    file_lock_acquire();
	    f->eax = file_length (list_search(&thread_current()->file_descriptor, *(stack_pointer+1))->file_pointer);
	    file_lock_release();
	    break;

	case SYS_READ:
	    user_address_validity(stack_pointer+7);
	    user_address_validity(*(stack_pointer+6));
	    if(*(stack_pointer+5)!=0)
	    {
 		read_write(f,'r');
	    }
            else if(*(stack_pointer+5)==0)
	    {
		buffer = *(stack_pointer+6);
                f->eax = *(stack_pointer+7);
		for(index=0;index<*(stack_pointer+7);index++)
			buffer[index] = input_getc();
	    }
	    break;

	case SYS_WRITE:
	    user_address_validity(stack_pointer+7);
	    user_address_validity(*(stack_pointer+6));
	    if(*(stack_pointer+5)!=1)
	    {
                read_write(f,'w');
	    }
            else if(*(stack_pointer+5)==1)
	    {
		putbuf(*(stack_pointer+6),*(stack_pointer+7));
		f->eax = *(stack_pointer+7);
	    }
	    break;

	case SYS_SEEK:
	    user_address_validity(stack_pointer+5);
	    file_lock_acquire();
	    file_seek(list_search(&thread_current()->file_descriptor, *(stack_pointer+4))->file_pointer,*(stack_pointer+5));
	    file_lock_release();
	    break;

	case SYS_TELL:
	    user_address_validity(stack_pointer+1);
	    file_lock_acquire();
	    f->eax = file_tell(list_search(&thread_current()->file_descriptor, *(stack_pointer+1))->file_pointer);
	    file_lock_release();
	    break;
	default:
	    printf("Default %d\n",*stack_pointer);
    }
}
/* Checks the user address is below the PHY_BASE and if it is 
   registered in the page directory */
void *user_address_validity(const void *vaddr)
{
    if (is_user_vaddr(vaddr) && pagedir_get_page(thread_current()->pagedir, vaddr) != NULL)
    {
        return pagedir_get_page(thread_current()->pagedir, vaddr);
    }
    else 
    {
        exit_process(-1);
	return 0;
    }
}
/* Function to terminate the process and return the status to the kernel 
   If the process to be terminated is a child, the parent process needs to 
   woken up and the corresponding wait_for_child value should be updated */
void exit_process(int exit_status)
{
    struct list_elem *element;
    struct child_structure *child_elem;
    struct list_elem *start = list_begin (&thread_current()->thread_parent->child_process);
    struct list_elem *end = list_end (&thread_current()->thread_parent->child_process);
    for (element = start; element != end; element = list_next (element))
    {
        child_elem = list_entry (element, struct child_structure, child_list_elem);
        if(child_elem->child_id != thread_current()->tid)
		continue;
        else if(child_elem->child_id == thread_current()->tid)
        {
            child_elem->child_exit = exit_status;
            child_elem->child_load = true;
        }
    }
    thread_current()->thread_exit = exit_status;
    struct thread *thread_waiting = thread_current()->thread_parent->wait_for_child;
    struct thread *current_id = thread_current()->tid;
    if(thread_waiting != current_id)	
        thread_exit();
    else if(thread_waiting == current_id)
    {	
        sema_up(&thread_current()->thread_parent->semaphore_child);
        thread_exit();
    }
}
/* Function to run the executable with the name given and return process id 
   if the program loads or return -1 if couldn't load */
int exec_process(char *file_name)
{
    char *file_new;
    struct file* file;
    file_lock_acquire();
    file_new = make_file_copy(file_name);
    file = filesys_open (file_new);
    if(file!=NULL)
    {
        file_close(file);
	file_lock_release();
	return process_execute(file_name);
        
    }
    else if(file == NULL)
    {
        file_lock_release();
	return -1;
    }
}
/* Function to read bytes from open file into buffer if type is read ('r') or 
   write bytes from buffer to open file if type is write ('w') 
   Returns -1 if the file couldn't be read or written */
void read_write(struct intr_frame *f UNUSED,char type)
{
    file_descriptor_pointer = list_search(&thread_current()->file_descriptor, *(stack_pointer+5));
    if (file_descriptor_pointer)
    {
        file_lock_acquire();
        if(type == 'r')
            f->eax = file_read (file_descriptor_pointer->file_pointer, *(stack_pointer+6), *(stack_pointer+7));
        else if(type == 'w')
            f->eax = file_write (file_descriptor_pointer->file_pointer, *(stack_pointer+6), *(stack_pointer+7));
        file_lock_release();
    }
    else if(!file_descriptor_pointer)
	f->eax=-1;
}
/* Function to check the list of files for the required file id and return the file element 
   if the given file id is present or return NULL */
struct file_descriptor_struct* list_search(struct list* file, int file_id)
{
    struct list_elem *element;
    struct file_descriptor_struct *file_element;
    int id;
    for (element = list_begin (file); element != list_end (file);element = list_next (element))
    {
        file_element = list_entry (element, struct file_descriptor_struct, file_descriptor_elem);
        id = file_element->file_descriptor_id;
        if(id != file_id)
            return NULL;
        if(id == file_id)	
            return file_element;
    }
}
/* Function to open the file based on the given file pointer and return the 
   file descriptor id if the file could be opened and -1 if the file couldn't be opened */
void file_open_this(struct intr_frame *f UNUSED,struct file_descriptor_struct *file_ptr)
{
    struct file_descriptor_struct *file;
    file = malloc(sizeof(*file));
    if(file_ptr != NULL)
    {
	file->file_descriptor_id = thread_current()->file_descriptor_count;
        file->file_pointer = file_ptr;
	thread_current()->file_descriptor_count++;
	list_push_back (&thread_current()->file_descriptor, &file->file_descriptor_elem);
	f->eax = file->file_descriptor_id;
    }
    else if(file_ptr == NULL)
        f->eax = -1;
}
/* Function to find file descriptor id to be closed
   and close the file descriptor */
void file_close_this(struct list* file, int file_descriptor_id)
{
    struct list_elem *element;
    struct file_descriptor_struct *file_element;
    int id;
    for (element = list_begin (file); element != list_end (file); element = list_next (element))
    {
        file_element = list_entry (element, struct file_descriptor_struct, file_descriptor_elem);
        id = file_element->file_descriptor_id;
        if(id == file_descriptor_id)
        {
	    list_remove(element);
            file_close(file_element->file_pointer);
        }
    }
    free(file_element);
}
/* Function to delete the file with the given file descriptor id and 
   return true if successful or false otherwise */
void file_remove(struct intr_frame *f UNUSED,int file_descriptor_id)
{
    bool remove = filesys_remove(file_descriptor_id);
    if(remove != NULL)
        f->eax = true;
    else if(remove == NULL)
	f->eax = false;
}
/* Function to close all the files to deallocate the memory used by the 
   file descriptor elements when process_exit() is called */
void file_close_all(struct list* file)
{
    struct list_elem *element;
    struct file_descriptor_struct *file_element;
    struct file_descriptor_struct *file_to_close;
    while(list_empty(file)!=1)
    {
        element = list_pop_front(file);
        file_element = list_entry (element, struct file_descriptor_struct, file_descriptor_elem);
        file_to_close = file_element->file_pointer;
        list_remove(element);
 	file_close(file_to_close);     	
	free(file_element);
    }     
}
