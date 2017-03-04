#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

#include <proc.h>

/* routine for thread_fork() to begin executing */
void enter_forked_process(struct trapframe *, unsigned long);
pid_t sys_fork(struct trapframe *, int32_t *);
struct trapframe *trapframe_copy(struct trapframe *);

#endif /* _PROC_SYSCALLS_H_ */
