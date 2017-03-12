#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

#include <proc.h>

/* routine for thread_fork() to begin executing */
void enter_forked_process(struct trapframe *, unsigned long);
pid_t sys_fork(struct trapframe *, int32_t *);
pid_t sys_waitpid(pid_t, userptr_t, int, int32_t *);
void sys_exit(int);
struct trapframe *trapframe_copy(struct trapframe *);
void sys_getpid(int32_t *);
int sys_execv(const char *, char **, int32_t *);
int build_user_stack(char**, size_t *, size_t, userptr_t, size_t *);
int count_str_size(char *, size_t *);
int check_arg_size(size_t *);
void cleanup_double_ptr(char**, size_t);

/* Supporting method that ensures a pointer's address 
   is aligned by a given byte size */
bool ptr_is_aligned(void *, int);

#endif /* _PROC_SYSCALLS_H_ */
