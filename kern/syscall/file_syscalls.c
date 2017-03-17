/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <proc.h>
#include <kern/seek.h>
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
#include <stat.h>
#include <syscall.h>
#include <file_syscalls.h>
#include <uio.h>
#include <synch.h>
#include <vnode.h>
#include <copyinout.h>
#include <kern/fcntl.h>

ssize_t
sys_write(int fd, const void *buf, size_t buflen, int32_t *retval)
{
	// kprintf("sys_write: fd = %d\n", fd);
	if(buf == NULL){
		*retval = -1;
		return EFAULT;
	}

	//Error checking on buffer
	if(buflen < 1) {
		*retval = -1;
		return EFAULT;
	}

	char *kbuf = kmalloc(buflen);
	if(kbuf == NULL) {
		*retval = ENOMEM;
		return ENOMEM;
	} 

	int result = copyin((const_userptr_t) buf, (void *)kbuf, buflen);
	if(result){
		kfree(kbuf);		
		*retval = result;
		return result;
	}

	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = EBADF;
		return EBADF;
	}
	
	int fh_flags = curproc->filetable[fd]->fh_perm;
	if(fh_flags == O_RDONLY || 
		fh_flags == O_RDONLY+O_CREAT || 
		fh_flags == O_RDONLY+O_EXCL || 
		fh_flags == O_RDONLY+O_TRUNC || 
		fh_flags == O_RDONLY+O_APPEND){
		*retval = EBADF;
		return EBADF;
	}
	
	//Need this? This assumes flags should not be 3 (both WRONLY AND RDRW) or >=NOCTTY
	if(fh_flags==3 || fh_flags >= O_NOCTTY){
		*retval = EINVAL;
		return EINVAL;
	}

	struct filehandle *fh = curproc->filetable[fd];	
	struct iovec iov;
	struct uio u;
	
	lock_acquire(fh->fh_lock);
	
	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;
	u.uio_iov = &iov;	
	u.uio_iovcnt = 1; 
	u.uio_resid = buflen;
	u.uio_offset = fh->fh_offset_value;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_WRITE;
	u.uio_space = curproc->p_addrspace;

	result = VOP_WRITE(fh->fh_vnode, &u);
	if(result) {
		*retval = result;
		lock_release(fh->fh_lock);
		return result;
	}
	fh->fh_offset_value = u.uio_offset;

	*retval = buflen - u.uio_resid;
	lock_release(fh->fh_lock);

	return 0;
}

ssize_t
sys_read(int fd, void *buf, size_t buflen, int32_t *retval)
{
	if(buf == NULL){
		*retval = -1;
		return EFAULT;
	}

	//Error checking on buffer
	if(buflen < 1) {
		*retval = -1;
		return EFAULT;
	}

	char *kbuf = kmalloc(buflen);
	if(kbuf == NULL) {
		*retval = ENOMEM;
		return ENOMEM;
	}

	int result = copyin((const_userptr_t)buf, (void *)kbuf, buflen);
	if(result){
		kfree(kbuf);
		*retval = result;
		return result;
	}

	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = -1;
		return EBADF;
	}

	int fh_flags = curproc->filetable[fd]->fh_perm;
	if(fh_flags == O_WRONLY || 
		fh_flags == O_WRONLY+O_CREAT || 
		fh_flags == O_WRONLY+O_EXCL || 
		fh_flags == O_WRONLY+O_TRUNC || 
		fh_flags == O_WRONLY+O_APPEND){
		*retval = -1;
		return EBADF;
	}

	//Need this? This assumes flags should not be 3 (both WRONLY AND RDRW) or >=NOCTTY
	if(fh_flags==3 || fh_flags >= O_NOCTTY){
		*retval = -1;
		return EINVAL;
	}

	struct filehandle *fh = curproc->filetable[fd];
	struct iovec iov;
	struct uio u;

	lock_acquire(fh->fh_lock);

	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;
	u.uio_iov = &iov;	
	u.uio_iovcnt = 1; 
	u.uio_resid = buflen;
	u.uio_offset = fh->fh_offset_value;
	u.uio_segflg = UIO_SYSSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = NULL;

	result = VOP_READ(fh->fh_vnode, &u);
	if(result) {
		lock_release(fh->fh_lock);
		*retval = result;
		return result;
	}

	fh->fh_offset_value = u.uio_offset;
	lock_release(fh->fh_lock);

	*retval = buflen - u.uio_resid;
	return 0;
}

int
sys_open(const char *filename, int flags, int32_t * retval)
{


	if(flags==3 || flags >= O_NOCTTY){
		*retval = EINVAL;
		return EINVAL;
	}

	struct filehandle *new_fh;
	int result;
	char *k_filename = kmalloc(sizeof(filename));
	size_t size = 0;

	/* Handles EFAULT. Add 1 to strlen for null terminator */
	result = copyinstr((const_userptr_t)filename, (void*)k_filename, (size_t)100, &size);
	if (result) {
		*retval = result;
		return result;
	}

	//filename_cpy[0] == '\0'
	if(size<2){
//		kprintf("Empty string arg \n");
		*retval = EINVAL;
		return EINVAL;
	}

	/* Still need to call vfs_open after filehandle_create() */
	new_fh = filehandle_create(k_filename, flags);
	if(new_fh == NULL) {
		*retval = -1;
		return 1;
	}

	//lock_acquire(new_fh->fh_lock); //THIS IS SHIT?

	//No one should be able to mess with the file handles while I'm checking for a free space
	
	int i;
	int free_index = 63;

	for(i=3; i<64;i++){
		if(curproc->filetable[i] == NULL) {
			free_index = i < free_index ? i : free_index;
			break;
		}
	}

	//Make copy of pathname here and provide as argument to open
	char *k_filename_copy = kstrdup(k_filename);

	/* Handles EINVAL, ENXIO, ENODEV */
	result = vfs_open(k_filename_copy, new_fh->fh_perm, 0, &new_fh->fh_vnode);
	if(result){
		*retval = result;
		// kprintf("Open: Calling filehandle destory on fd: %d\n", free_index);
		filehandle_destroy(new_fh);
		return result;
	}

	new_fh->num_open_proc++;
	curproc->filetable[free_index] = new_fh;
	*retval = free_index;

//	lock_release(new_fh->fh_lock);
	return 0;
}

int 
sys_close(int fd, int32_t * retval)
{
	// kprintf("In sys_close\n");
	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		// kprintf("In sys_close, filetable[%d] is null, returning\n", fd);
		*retval = EBADF;
		return EBADF;
	}

	struct filehandle *fh = curproc->filetable[fd];
	lock_acquire(fh->fh_lock);
	// kprintf("In sys_close, deleting file handle for fd = %d\n", fd);
	sys_close_helper(fh, fd); // releases lock
	*retval = 0;
	return 0;
}

void
sys_close_helper(struct filehandle * fh, int fd) {
	/* Must acquire fh_lock before calling! */
	KASSERT(fh != NULL);
	fh->num_open_proc--;
	if(fh->num_open_proc < 1) {
		// vfs_close cannot fail. See vfspath.c:119 for details.
		vfs_close(fh->fh_vnode);
		
		curproc->filetable[fd] = NULL;
		lock_release(fh->fh_lock);
		// kprintf("Close: Calling filehandle destory on fd: %d\n", fd);
		filehandle_destroy(fh);

	} else {
		curproc->filetable[fd] = NULL;
		lock_release(fh->fh_lock);
	}
}

int
sys_dup2(int fdold, int fdnew, int32_t * retval)
{
// 	kprintf("Entering dup2 with fdold = %d, fdnew = %d\n", fdold, fdnew);
// 	kprintf("refcount for STDIN = %d\n", curproc->filetable[0]->num_open_proc);
// 	kprintf("refcount for STDOUT = %d\n", curproc->filetable[1]->num_open_proc);
	if(fdold < 0 || fdold > 63 || 
		fdnew < 0 || fdnew > 63 ||
		fdold == fdnew || 
		curproc->filetable[fdold] == NULL) 
	{
		*retval = EBADF;
		return EBADF;
	}

	lock_acquire(curproc->filetable[fdold]->fh_lock);

	if(curproc->filetable[fdnew] != NULL) {

		struct filehandle *fh_new = curproc->filetable[fdnew];

		lock_acquire(fh_new->fh_lock);

		KASSERT(fh_new != NULL);

		fh_new->num_open_proc--;

		if(fh_new->num_open_proc < 1) {
			// vfs_close cannot fail. See vfspath.c:119 for details.
			vfs_close(fh_new->fh_vnode);
			curproc->filetable[fdnew] = NULL;
			lock_release(fh_new->fh_lock);
			// kprintf("Dup2: Calling filehandle destory on fd: %d\n", fdnew);
			filehandle_destroy(fh_new);
		} else {
			curproc->filetable[fdnew] = NULL;
			lock_release(fh_new->fh_lock);
		}
	}

	curproc->filetable[fdold]->num_open_proc++;
	curproc->filetable[fdnew] = curproc->filetable[fdold];
	lock_release(curproc->filetable[fdold]->fh_lock);
	*retval = fdnew;
	// kprintf("Leaving dup2\n");
	return 0;
}

int
sys_chdir(const char * pathname, int32_t * retval)
{
	if(pathname == NULL) {
		*retval = EFAULT;
		return EFAULT;
	}

	char kpathname[256];
	int result;
	size_t size = 0;

	result = copyinstr((const_userptr_t)pathname, (void*)&kpathname, (size_t)256, &size);
	if (result) {
		*retval = result;
		return result;
	}

	result = vfs_chdir(kpathname);
	if(result) {
		*retval = result;
		return result;
	}

	*retval = 0;
	return 0;
}

off_t 
sys_lseek(int fd, off_t pos, const void * whence, off_t * retval)
{

	//If fd is STDIN
	if(fd == 0){
		*retval = (off_t) ESPIPE;
		return (off_t)ESPIPE;
	}

	int whencebuf = 0;
	int err = copyin((const_userptr_t)whence, (void*)&whencebuf, 4);
	if(err) {
		*retval = (off_t)EINVAL;
		return (off_t)EINVAL;
	}

	if(whencebuf < 0 || whencebuf > 2) {
		*retval = (off_t)EINVAL;
		return (off_t)EINVAL;
	}



	if(fd < 1 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = (off_t)EBADF;
		return (off_t)EBADF;
	}


	struct filehandle * fh = curproc->filetable[fd];
	lock_acquire(fh->fh_lock);
	if(!VOP_ISSEEKABLE(fh->fh_vnode)) {
		lock_release(fh->fh_lock);
		*retval = (off_t)ESPIPE;
		return (off_t)ESPIPE;
	}

	off_t net_offset;
	if(whencebuf == SEEK_SET) {
		//fh->fh_offset_value = pos;
		net_offset = pos;
	} 
	else if(whencebuf == SEEK_CUR) {
		//fh->fh_offset_value += pos;	
		net_offset = fh->fh_offset_value + pos;
	} 
	else { // SEEK_END
		struct stat st;
		VOP_STAT(fh->fh_vnode, &st);
		//fh->fh_offset_value = st.st_size + pos;
		net_offset = st.st_size + pos;
	}

	if(net_offset<0){
		lock_release(fh->fh_lock);
		*retval = (off_t) EINVAL;
		return (off_t)EINVAL;
	}
	fh->fh_offset_value = net_offset;
	*retval = fh->fh_offset_value;
	lock_release(fh->fh_lock);

	return 0;
}

int
sys___getcwd(char *buf, size_t buflen, int32_t *retval){

	if(buflen<1 || buflen>PATH_MAX){
		*retval = EINVAL;
		return -1;
	}

	int result;

	char kbuf[buflen+1];
	result = copyin((const_userptr_t) buf, &kbuf, buflen+1);
	if(result){
		*retval = result;
		return -1;
	}
	
	struct uio u;
	struct iovec iov;
	uio_kinit(&iov, &u, kbuf, buflen, 0, UIO_READ); //Might have to be buflen-1


	result = vfs_getcwd(&u);
	if(result){
		*retval = result;
		return -1;
	}
	size_t size = 0;
	result = copyoutstr((const char *)&kbuf, (userptr_t) buf, buflen, &size);
	if(result){
		*retval = result;
		return -1;
	}

	*retval = size;
	return 0;


}