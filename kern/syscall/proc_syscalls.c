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
#include <proc_syscalls.h>
#include <uio.h>
#include <synch.h>
#include <vnode.h>
#include <copyinout.h>
#include <mips/trapframe.h>


pid_t 
sys_fork(struct trapframe *parent_tf, struct trapframe **child_tf, int32_t *retval){

	int err;

	//Make new proc
	struct proc *newproc;
	newproc = proc_create_wrapper("child proc");
	if(newproc == NULL){
		*retval = 1;
		return 1;
	}

	//Set child ppid
	newproc->ppid = curproc->pid;

	//Copy parent's filetable
	err = filetable_copy(curproc, newproc);
	if(err){
		proc_destroy(newproc);
		*retval = 1;
		return 1;
	}

	//Copy parent's address space
	err = as_copy(curproc->p_addrspace, &newproc->p_addrspace);
	if(err){
		proc_destroy(newproc);
		*retval = err;
		return err;
	}

	//Copy parent's trap frame
	*child_tf = trapframe_copy(parent_tf);


	//Call thread_fork


	return 0;
}


struct trapframe *
trapframe_copy(struct trapframe *parent_tf){

	//Error checking
	if(parent_tf == NULL){
		return NULL;
	}

	struct trapframe *child_tf;
	child_tf = kmalloc(sizeof(child_tf));


	child_tf->tf_vaddr = parent_tf->tf_vaddr;	/* coprocessor 0 vaddr register */
	child_tf->tf_status = parent_tf->tf_status;	/* coprocessor 0 status register */
	child_tf->tf_cause = parent_tf->tf_cause;	/* coprocessor 0 cause register */
	child_tf->tf_lo = parent_tf->tf_lo;
	child_tf->tf_hi = parent_tf->tf_hi;
	child_tf->tf_ra = parent_tf->tf_ra;		/* Saved register 31 */
	child_tf->tf_at = parent_tf->tf_at;		/* Saved register 1 (AT) */
	child_tf->tf_v0 = parent_tf->tf_v0;		/* Saved register 2 (v0) */
	child_tf->tf_v1 = parent_tf->tf_v1;
	child_tf->tf_a0 = parent_tf->tf_a0;
	child_tf->tf_a1 = parent_tf->tf_a1;
	child_tf->tf_a2 = parent_tf->tf_a2;
	child_tf->tf_a3 = parent_tf->tf_a3;
	child_tf->tf_t0 = parent_tf->tf_t0;
	child_tf->tf_t1 = parent_tf->tf_t1;
	child_tf->tf_t2 = parent_tf->tf_t2;
	child_tf->tf_t3 = parent_tf->tf_t3;
	child_tf->tf_t4 = parent_tf->tf_t4;
	child_tf->tf_t5 = parent_tf->tf_t5;
	child_tf->tf_t6 = parent_tf->tf_t6;
	child_tf->tf_t7 = parent_tf->tf_t7;
	child_tf->tf_s0 = parent_tf->tf_s0;
	child_tf->tf_s1 = parent_tf->tf_s1;
	child_tf->tf_s2 = parent_tf->tf_s2;
	child_tf->tf_s3 = parent_tf->tf_s3;
	child_tf->tf_s4 = parent_tf->tf_s4;
	child_tf->tf_s5 = parent_tf->tf_s5;
	child_tf->tf_s6 = parent_tf->tf_s6;
	child_tf->tf_s7 = parent_tf->tf_s7;
	child_tf->tf_t8 = parent_tf->tf_t8;
	child_tf->tf_t9 = parent_tf->tf_t9;
	child_tf->tf_gp = parent_tf->tf_gp;
	child_tf->tf_sp = parent_tf->tf_sp;
	child_tf->tf_s8 = parent_tf->tf_s8;
	child_tf->tf_epc = parent_tf->tf_epc;

	//Need error checking for incorrect/failed copy?

	return child_tf;

}