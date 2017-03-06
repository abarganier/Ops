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

ssize_t
sys_write(int fd, const void *buf, size_t buflen, int32_t *retval)
{
	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = EBADF;
		return EBADF;
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

	int result = VOP_WRITE(fh->fh_vnode, &u);
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
	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = EBADF;
		return EBADF;
	}

	if(buf == NULL || buflen < 1) {
		*retval = -1;
		return 1;
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
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace;

	int result = VOP_READ(fh->fh_vnode, &u);
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
	struct filehandle *new_fh;
	int result;
	char *filename_cpy = kmalloc(sizeof(filename));
	size_t size = 0;
	/* Handles EFAULT. Add 1 to strlen for null terminator */
	result = copyinstr((const_userptr_t)filename, (void*)filename_cpy, (size_t)100, &size);
	if (result) {
		*retval = result;
		return result;
	}

	/* Still need to call vfs_open after filehandle_create() */
	new_fh = filehandle_create(filename_cpy, flags);
	if(new_fh == NULL) {
		*retval = -1;
		filehandle_destroy(new_fh);
		return 1;
	}

	lock_acquire(new_fh->fh_lock);

	int i;
	int free_index = 63;

	for(i=3; i<64;i++){
		if(curproc->filetable[i] == NULL) {
			free_index = i < free_index ? i : free_index;
			break;
		}
	}

	/* Handles EINVAL, ENXIO, ENODEV */
	result = vfs_open(new_fh->fh_name, new_fh->fh_perm, 0, &new_fh->fh_vnode);
	if(result){
		*retval = result;
		lock_release(new_fh->fh_lock);
		filehandle_destroy(new_fh);
		return 1;
	}

	new_fh->num_open_proc++;
	curproc->filetable[free_index] = new_fh;
	*retval = free_index;

	lock_release(new_fh->fh_lock);
	return 0;
}

int 
sys_close(int fd, int32_t * retval)
{
	if(fd < 0 || fd > 63 || curproc->filetable[fd] == NULL) {
		*retval = EBADF;
		return 1;
	}

	struct filehandle *fh = curproc->filetable[fd];
	lock_acquire(fh->fh_lock);
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
		filehandle_destroy(fh);

	} else {
		curproc->filetable[fd] = NULL;
		lock_release(fh->fh_lock);
	}
}

int
sys_dup2(int fdold, int fdnew, int32_t * retval)
{
	if(fdold < 0 || fdold > 63 || 
		fdnew < 0 || fdnew > 63 || 
		curproc->filetable[fdold] == NULL) 
	{
		*retval = EBADF;
		return EBADF;
	}

	if(curproc->filetable[fdnew] != NULL) {
		struct filehandle * fh = curproc->filetable[fdnew];
		lock_acquire(fh->fh_lock);
		sys_close_helper(fh, fdnew); // releases lock
	}

	curproc->filetable[fdnew] = curproc->filetable[fdold];
	*retval = fdnew;
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
	if(fd < 0 || fd > 63 || pos < 0 || curproc->filetable[fd] == NULL) {
		*retval = (off_t)EBADF;
		return (off_t)EBADF;
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

	struct filehandle * fh = curproc->filetable[fd];
	lock_acquire(fh->fh_lock);
	if(!VOP_ISSEEKABLE(fh->fh_vnode)) {
		lock_release(fh->fh_lock);
		*retval = (off_t)ESPIPE;
		return (off_t)ESPIPE;
	}

	if(whencebuf == SEEK_SET) {
		fh->fh_offset_value = pos;
	} else if(whencebuf == SEEK_CUR) {
		fh->fh_offset_value += pos;	
	} else { // SEEK_END
		struct stat st;
		VOP_STAT(fh->fh_vnode, &st);
		fh->fh_offset_value = st.st_size + pos;
	}

	*retval = fh->fh_offset_value;
	lock_release(fh->fh_lock);

	return 0;
}

int
sys___getcwd(char *buf, size_t buflen, int32_t *retval){

	//error check buff
	if(buf == NULL){
		*retval = EFAULT;
		return -1;
	}

	//Do I need to error check buflen?

	//vfs_getcwd needs a uio object and returns an int
	struct uio u;
	struct iovec iov;


	iov.iov_ubase = (userptr_t)buf;
	iov.iov_len = buflen;
	u.uio_iov = &iov;	
	u.uio_iovcnt = 1; 
	u.uio_resid = buflen;
	u.uio_offset = 0;
	u.uio_segflg = UIO_USERSPACE;
	u.uio_rw = UIO_READ;
	u.uio_space = curproc->p_addrspace;



	int result;
	result = vfs_getcwd(&u);
	if(result){

		*retval = result;
		return -1;
	}

	//copyout to *buf in userspace
	result = copyout(u.uio_iov->iov_ubase,(userptr_t) buf, buflen+1); //Need +1 for \0?
	if(result){
		*retval = result;
		return -1;
	}

	//proc struct has a pointer proc_cwd. What do we do with this? When do we set this?
	*retval = sizeof(buf);
	return 0;

}
