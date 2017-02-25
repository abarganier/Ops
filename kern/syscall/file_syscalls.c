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
#include <current.h>
#include <addrspace.h>
#include <vm.h>
#include <vfs.h>
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
		*retval = -1;
		return EBADF;
	}	
	
	struct filehandle *fh = curproc->filetable[fd];	
	struct iovec iov;
	struct uio u;
	
	lock_acquire(fh->fh_lock);
	char * testbuf = kmalloc(sizeof(buf));		
	/*What if kmalloc fails for testbuf?*/


	int err = copyin(buf, testbuf, buflen);
	if(err) {
		*retval = -1;
		lock_release(fh->fh_lock);
		return 1;
	}

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
		*retval = -1;
		lock_release(fh->fh_lock);
		return result;
	}
	fh->fh_offset_value = u.uio_offset;
	*retval = u.uio_resid;

	lock_release(fh->fh_lock);
	return 0;
}

int
sys_open(const char *filename, int flags, int32_t * retval)
{
	struct filehandle *new_fh;

	new_fh = filehandle_create(filename, flags);
	if(new_fh == NULL) {
		*retval = -1;
		filehandle_destroy(new_fh);
		return 1;
	}

	lock_acquire(new_fh->fh_lock);

	char *filename_cpy = kmalloc(sizeof(filename));

	int result;
	
	/* Handles EFAULT*/
	result = copyin((userptr_t)filename, filename_cpy, (size_t)strlen(filename));
	if (result) {
		*retval = result;
		lock_release(new_fh->fh_lock);
		filehandle_destroy(new_fh);
		return 1;
	}

	int i;
	int free_index = 63;

	for(i=3; i<64;i++){
		if(curproc->filetable[i] == NULL) {
			free_index = i < free_index ? i : free_index;
			break;
		}
	}

	/*Handles EINVAL, ENXIO, ENODEV*/
	result = vfs_open(filename_cpy, flags, 0, &new_fh->fh_vnode);
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
	// Cannot close console files
	if(fd < 3 || fd > 63) {
		*retval = -1;
		return 1;
	}

	if(curproc->filetable[fd] == NULL) {
		*retval = EBADF;
		return 1;
	}
	struct filehandle *fh = curproc->filetable[fd];
	lock_acquire(fh->fh_lock);

	fh->num_open_proc--;
	if(fh->num_open_proc < 1) {
		// vfs_close cannot fail. See vfspath.c:119 for details.
		vfs_close(fh->fh_vnode);
		
		curproc->filetable[fd] = NULL;
		lock_release(fh->fh_lock);
		filehandle_destroy(fh);

		*retval = 0;
		return 0;
	} else {
		curproc->filetable[fd] = NULL;
		lock_release(fh->fh_lock);

		*retval = 0;
		return 0;
	}
}