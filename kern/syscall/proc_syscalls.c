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
#include <cpu.h>
#include <vm.h>
#include <vfs.h>
#include <stat.h>
#include <syscall.h>
#include <proc_syscalls.h>
#include <uio.h>
#include <synch.h>
#include <vnode.h>
#include <copyinout.h>
#include <kern/wait.h>
#include <mips/trapframe.h>
#include <kern/fcntl.h> //MACROS like O_RDONLY
#include <limits.h>	//MACROS like ARG_MAX

#define ALIGNMENT 4u;

struct proc_table *p_table;

void
enter_forked_process(struct trapframe *tf, unsigned long nothing)
{	
	(void)nothing;
	struct trapframe trap = *tf;
	trap.tf_v0 = 0;
	trap.tf_a3 = 0;
	trap.tf_epc += 4;

	// Potential memory leak
	mips_usermode(&trap);
}

pid_t 
sys_fork(struct trapframe *parent_tf, int32_t *retval)
{
	int err;
	struct trapframe *child_tf;
	struct proc *newproc;

	newproc = proc_create_wrapper("child proc");
	if(newproc == NULL){
		*retval = 1;
		return 1;
	}

	newproc->ppid = curproc->pid;

	err = filetable_copy(curproc, newproc);
	if(err){
		// proc_destroy(newproc);
		*retval = -1;
		return err;
	}

	err = as_copy(curproc->p_addrspace, &newproc->p_addrspace);
	if(err){
		// proc_destroy(newproc); 
		*retval = err;
		return err;
	}

	*retval = newproc->pid;


	child_tf = trapframe_copy(parent_tf);
	

	err = thread_fork("child", newproc, (void*)enter_forked_process, child_tf, (unsigned long)newproc->pid);
	if(err) {
		kfree(child_tf);
		proc_destroy(newproc);
		*retval = err;
		return err;
	}

	return 0;
}

pid_t
sys_waitpid(pid_t pid, userptr_t status_ptr, int options, int32_t *retval)
{
	int res;
	int ch_status;

	// Options not supported
	if(options) {
		*retval = EINVAL;
		return EINVAL;
	}

	lock_acquire(p_table->pt_lock);

	KASSERT(p_table->table[pid] != NULL);

	if(pid < PID_MIN || p_table->table[pid] == NULL) {
		lock_release(p_table->pt_lock);
		*retval = ESRCH;
		return ESRCH;
	}

	if(p_table->table[pid]->ppid != curproc->pid) {
		lock_release(p_table->pt_lock);
		*retval = ECHILD;
		return ECHILD;
	}

	struct proc *childproc = p_table->table[pid];

	KASSERT(childproc->pid == pid);
	
	lock_release(p_table->pt_lock);

	/* Ensure the status ptr is valid (if provided) before continuing */
	/* We won't do anything with the value, this just is a memcheck */
	if(status_ptr != NULL) {
		res = copyin(status_ptr, (void*)&res, 4);
		if(res) {
			*retval = res;
			return res;
		}
	}

	if(!childproc->exited) {
		P(&childproc->exit_sem);
	}

	KASSERT(childproc->exited);

	ch_status = childproc->exit_status;

	if(status_ptr != NULL) {
		res = copyout((void *)&ch_status, status_ptr, 4);
		if(res) {
			*retval = res;
			return res;
		}
	}

	lock_acquire(p_table->pt_lock);
	// proc_destroy(childproc); // leak memory for now, fix in asst3
	p_table->table[pid] = NULL;
	lock_release(p_table->pt_lock);

	*retval = pid;
	return 0;
}

void
sys_exit(int exitcode) 
{

	int pid = curproc->pid;
	int ppid = curproc->ppid;

	if(ppid >= 0 && ppid < 256 && p_table->table[ppid] == NULL) {
		return;
	}

	if(ppid >= 0 && ppid <= 255 && p_table->table[ppid]->exited) {
		lock_acquire(p_table->pt_lock);
		p_table->table[pid] = NULL;
		// proc_destroy(p_table->table[pid]); // leak memory for now, fix in asst3 
		lock_release(p_table->pt_lock);
		return;
	}
	curproc->exited = true;
	curproc->exit_status = _MKWAIT_EXIT(exitcode);
	V(&curproc->exit_sem);
	thread_exit();
}

void
sys_getpid(int32_t *retval)
{
	KASSERT(curproc->pid >= PID_MIN);
	*retval = curproc->pid;
}

struct trapframe *
trapframe_copy(struct trapframe *parent_tf)
{
	if(parent_tf == NULL){
		return NULL;
	}

	struct trapframe *child_tf;
	child_tf = kmalloc(sizeof(*child_tf));

	memcpy(child_tf, parent_tf, sizeof(*parent_tf));

	return child_tf;
}

int
load_arg_pointers(vaddr_t * addresses, vaddr_t * stkptr, int arrsize)
{
	*stkptr -= arrsize*4;
	int result = copyout((const void *)addresses, (void*)stkptr, arrsize*4);
	if(result) {
		return result;
	}
	return 0;
}

int
build_user_stack(char strings[][ARG_MAX], size_t * lengths, int arrsize, vaddr_t * stkptr)
{
	vaddr_t addresses[arrsize];
	int i, result;
	char nothing = '\0';

	for(i = 0; i < arrsize; i++) {

		int nulls = lengths[i] % 4;
		size_t offset = lengths[i] + nulls;
		*stkptr -= offset;
		addresses[i] = *stkptr;
		
		size_t ch;
		for(ch = 0; ch < offset; ch++) {

			if(ch < lengths[i]) {

				result = copyout((const void *)&strings[i][ch], (void *)stkptr, 1);
				if(result) {
					return result;
				}
				*stkptr += 1;

			} else {

				result = copyout((const void *)&nothing, (void *)stkptr, 1);
				if(result) {
					return result;
				}
				*stkptr += 1;
			}
		}

		*stkptr = addresses[i]; // does stkptr point to the next available spot, or the top used address?
	}

	result = load_arg_pointers(addresses, stkptr, arrsize);
	if(result) {
		return result;
	}
	return 0;
}

int
sys_execv(const char *program, char **args, int32_t *retval)
{
	int result;
	int num_bytes = 0;
	char kprogram[PATH_MAX];
	size_t prog_len = 0;
	
	result = copyinstr((const_userptr_t) program, kprogram, PATH_MAX, &prog_len); 
	if(result){
		*retval = result;
		return result;
	}
	num_bytes += prog_len + 4;

	userptr_t kargs[ARG_MAX];
	int index = 0;
	userptr_t argv = 0;

	//Copy in 0 index of args (should be program pointer)
	//Check if null
	//If no, start while loop which checks to see that the last arg copied in is not null
	result = copyin((const_userptr_t) args, argv, 4);
	if(result){
		*retval = result;
		return result;
	}

	result = copyin((const_userptr_t) argv, kargs[index], 4);
	if(result){
		*retval = result;
		return result;
	}

	//Need to loop here until a null pointer is copied in
	while(kargs[index] != NULL){
		
		index++;
		argv += 4;
		num_bytes =+ 4;
		
		//copy in pointer
		result = copyin((const_userptr_t) argv, kargs[index], 4);
		if(result){
			*retval = result;
			return result;
		}

		//check to see that total number of copied bytes does not exceed ARG_MAX
		// if(num_bytes > ARG_MAX){
		// 	*retval = E2BIG;
		// 	return E2BIG;
		// }
	}

	/*By here we have a buffer kargs that holds all of the arg pointers. 
	How do we copy in the areas of user memory that the pointers originally pointed to? */
	char argbuf[index][ARG_MAX];
	size_t lengths[index];

	int i;
	for(i = 0; i < index; i++) {
		result = copyinstr((const_userptr_t)kargs[i], argbuf[i], ARG_MAX, &lengths[i]); 
		if(result){
			*retval = result;
			return result;
		}
	}

	//Operations to load the executable into mem
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;


	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result) {
		*retval = result;
		return result;
	}

	/* We should be a new process. */
	KASSERT(proc_getas() == NULL);

	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(v);
		*retval = ENOMEM;
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		vfs_close(v);
		*retval = result;
		return result;
	}

	/* Done with the file now. */
	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		/* p_addrspace will go away when curproc is destroyed */
		*retval = result;
		return result;
	}
	kprintf("Stkptr after as_define_stack: %x", stackptr);

	//Copy arguments from kernel space to userpsace
	result = build_user_stack(argbuf, lengths, index, &stackptr);
	if(result) {
		*retval = 1;
		return 1;
	}

	vaddr_t array_start = stackptr;

	//Return to userspace using enter_new_process (in kern/arch/mips/locore/trap.c)
	enter_new_process(index, (userptr_t)array_start, NULL, stackptr, entrypoint);

	//SHOULD NOT REACH HERE ON SUCCESS
	*retval = EINVAL;
	return -1;
}