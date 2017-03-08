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
sys_execv(const char *program, char **args, int32_t *retval)
{
	//Copy in arguments from address space to kernel space
	int result;
	int num_bytes = 0;			//Note: Can't do sizeof() on program or args because they come from userspace
	char *kprogram = NULL;		//Kernel destination address for program arg
	
	result = copyin((const_userptr_t) program, kprogram, 4); //len 4 because pointer?
	if(result){
		*retval = result;
		return result;		 	//May have to return -1, but I don't think it matters
	}
	num_bytes += 4;		 



	char *kargs[ARG_MAX];		//Kernel destination address for args argument
	int index = 0;


	//Copy in 0 index of args (should be program pointer)
	//Check if null
		//If yes, done. Do not return error. NULL could be a user-desired argument
		//If no, start while loop which checks to see that the last arg copied in is not null
	result = copyin((const_userptr_t) args[index], kargs[index], 4);
	if(result){
		*retval = result;
		return result;
	}
	num_bytes =+ 4;


	//Need to loop here until a null pointer is copied in
	while(kargs[index] != NULL){
		
		//incremement index
		index++;

		//copy in pointer
		result = copyin((const_userptr_t) args[index], kargs[index], 4);
		//error check copyin
		if(result){
			*retval = result;
			return result;
		}
		num_bytes =+ 4;

		//check to see that total number of copied bytes does not exceed ARG_MAX
		if(num_bytes > ARG_MAX){
			*retval = E2BIG;
			return E2BIG;
		}
	}

	/*By here we have a buffer kargs that holds all of the arg pointers. 
	How do we copy in the areas of user memory that the pointers originally pointed to? */



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


	//Copy arguments from kernel space to userpsace

	//Return to userspace using enter_new_process (in kern/arch/mips/locore/trap.c)



	//SHOULD NOT REACH HERE ON SUCCESS
	*retval = EINVAL;
	return -1;
}
