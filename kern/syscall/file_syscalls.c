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
#include <string.h>

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
sys_open(const char *filename, int flags, int32_t * retval){

	struct filehandle *newFH;

	int indexOfFilehandle;
	newFH = filehandle_create(filename, flags); 

	lock_acquire(newFH->fh_lock);

	int i;
	bool file_found = false;
	int free_index = 64;

	for(i=3; i<64;i++){

		if (curproc->filetable[i] != NULL) {

			if(strcmp(curproc->filetable[i]->fh_name,filename) == 0) {

				file_found = true;
				bool valid_perm = curproc->filetable[i]->fh_perm == flags;
				
				if(valid_perm) {
					*retval = i;
					curproc->filetable[i]->num_open_proc++;
					lock_release(newFH->fh_lock);
					filehandle_destroy(newFH);	
					return 0;
				}
			}
		} else {
			free_index = i < free_index ? i : free_index; 
		}
	}

	result = vfs_open(filename, flags, 0, &newFH->fh_vnode);
	if(result){
		*retval = result;
		lock_release(newFH->fh_lock);
		filehandle_destroy(newFH);
		return 1;
	}
	newFH->num_open_proc++;
	curproc->filetable[free_index] = newFH;
	*retval = free_index;
	/*
	 * TODO: Set filehandle offset to EOF, figure out if we can use a kernel space library for string compare
	 */
	lock_release(newFH->fh_lock);
	return 0;

}
