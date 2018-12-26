#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void *user_address_validity(const void*);

#endif /* userprog/syscall.h */
