#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

#include <proc.h>

/*
 * Syscall method to write to a file
 */
ssize_t sys_write(int, const void *, size_t, int32_t *);
ssize_t sys_read(int, void *, size_t, int32_t *);
int sys_open(const char *, int, int32_t *);
int sys_close(int, int32_t *);
int sys_dup2(int, int, int32_t *);
void sys_close_helper(struct filehandle *, int);
off_t sys_lseek(int, off_t, const void *, off_t *);

int sys___getcwd(char *, size_t , int32_t *);


#endif /* _FILE_SYSCALLS_H_ */
