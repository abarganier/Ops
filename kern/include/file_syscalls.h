#ifndef _FILE_SYSCALLS_H_
#define _FILE_SYSCALLS_H_

/*
 * Syscall method to write to a file
 */
ssize_t sys_write(int, const void *, size_t, int32_t *);
int sys_open(const char *, int);

#endif /* _FILE_SYSCALLS_H_ */
