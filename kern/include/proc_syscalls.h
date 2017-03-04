#ifndef _PROC_SYSCALLS_H_
#define _PROC_SYSCALLS_H_

#include <proc.h>


pid_t sys_fork(struct trapframe *, struct trapframe **, int32_t *);
struct trapframe *trapframe_copy(struct trapframe *);



#endif /* _PROC_SYSCALLS_H_ */
