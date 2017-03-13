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
build_user_stack(char* kargs, size_t * lengths, size_t num_ptrs, userptr_t stkptr, size_t karg_size)
{

	int result;

	userptr_t og_stkptr = stkptr;

	stkptr -= karg_size;

	kprintf("stkptr - karg_size = %x\n", (unsigned int)stkptr);

	result = copyout(kargs, stkptr, karg_size);
	if(result) {
		kprintf("Copyout of string values to user stack failed! Error: %d\n", result);
		return result;
	}

	stkptr = og_stkptr;

	char **argv = kmalloc(sizeof(char *)*(num_ptrs+1));

	kprintf("Stkptr value before array build: %x\n", (unsigned int)stkptr);
	
	stkptr = stkptr - karg_size;
	
	for(size_t i = 0; i < num_ptrs; i++) {
		argv[i] = (char *) stkptr;
		stkptr += lengths[i];
	}

	argv[num_ptrs] = NULL;
	
	for(size_t i = 0; i < num_ptrs+1; i++) {
		if(argv[i] == NULL) {
			kprintf("argv[%d] is: NULL\n", i);
		} else {
			kprintf("argv[%d] is: %s\n", i, argv[i]);
		}	
	}

	stkptr = og_stkptr - karg_size - (4*num_ptrs+1);

	result = copyout(argv, stkptr, (4*num_ptrs+1));
	if(result) {
		kprintf("Copyout of argv array failed. Error: %d\n", result);
		return result;
	}
	kprintf("Finished copying out values to user stack\n");

	return 0;
}

int
sys_execv(const char *program, char **args, int32_t *retval)
{


	int result;
	char kprogram[PATH_MAX];
	size_t prog_len = 0;

	/* Use program pointer as src to copy in the program string to a kernel string space. Use only for address space call*/
	result = copyinstr((const_userptr_t) program, kprogram, PATH_MAX, &prog_len); 
	if(result){
		*retval = result;
		return result;
	}

	size_t index = 0;
	char **cur_head_of_args = kmalloc(sizeof(char *)); //first string argument. This will also serve as the place where all copyin tests go.


	/*Copy in 4 bytes of arg pointer to check validity*/
	result = copyin((const_userptr_t) args,  cur_head_of_args, 4);
	if(result){
		*retval = result;
		cleanup_double_ptr(cur_head_of_args,1);
		return result;
	}
	

	/* Do a lookup to get the index count, reset the count, do again to actually copy data in*/
	while(*cur_head_of_args != NULL){
		index++;
		//copyin next 4 bytes of arg pointer to check for validity
		result = copyin((const_userptr_t) args+(index*4), cur_head_of_args, 4);
		if(result){
			*retval = result;
			cleanup_double_ptr(cur_head_of_args,1);
			return result;
		}
		//kprintf("Curhead value at index %u is: %s \n", index, cur_head_of_args[0]);
	}

	char ** karg_ptrs = kmalloc(sizeof(char *)*index);

	for(size_t j=0; j<index; j++){
		result = copyin((const_userptr_t) (args+j), &karg_ptrs[j], 4);
		if(result){
			kprintf("Copying pointers failed \n");
			*retval = result;
			//free stuff
			return result;
		}
		// kprintf("Karg value: %x \n", (unsigned int)karg_ptrs[j]);
	}



	//Set up *kargs to its max allowable size (ARG-MAX-(index*4)) && start counting num_bytes
	//size_t num_bytes = 0;
 	char *kargs = kmalloc(ARG_MAX-(4*index)); //ARG_MAX size minus num_bytes needed for pointers

 	size_t arg_num;
 	size_t karg_size = 0;
 	size_t ret_length = 0;
 	size_t rem_space = ARG_MAX-(4*index);
 	size_t lengths[index];

	kprintf("2nd value: %s \n", karg_ptrs[1]);

	for(arg_num=0; arg_num<index; arg_num++){

		result = copyinstr((const_userptr_t) karg_ptrs[arg_num], (char *)&kargs[karg_size], rem_space, &ret_length);
		if(result){
			*retval = result;
			//free shit
			kprintf("Fail!");
			return result;
		}
	

		karg_size += ret_length;
		rem_space -= ret_length;
		// kprintf("Length of arg %d copydin is: %d\n", arg_num, ret_length);
		size_t num_nulls = ret_length == 0 ? 0 : 4-(ret_length%4);
		// kprintf("num_nulls for arg %d: %d\n", arg_num, num_nulls);
		lengths[arg_num] = ret_length+num_nulls;
		for(size_t k=0; k<num_nulls; k++){
			kargs[karg_size++] = '\0';
		}
	}
	
	kprintf("Printing that spicy string\n");
	size_t i;
	for(i = 0; i < karg_size; i++) {
		kprintf("%c", kargs[i]);
	}
	kprintf("\n");
	kprintf("karg_size is %d\n", karg_size);

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
		vfs_close(v);
		*retval = result;
		return result;
	}

	/* Done with the file now. */
 	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		*retval = result;
		return result;
	}

 	//Copy arguments from kernel space to userpsace
	result = build_user_stack(kargs, lengths, index, (userptr_t)stackptr, karg_size);
	if(result) {
		*retval = result;
		return result;
	}

	stackptr -= (karg_size + (index*4+1));

	//Return to userspace using enter_new_process (in kern/arch/mips/locore/trap.c)
	enter_new_process(index, (userptr_t)stackptr, NULL, stackptr, entrypoint);

 	//SHOULD NOT REACH HERE ON SUCCESS
	*retval = EINVAL;
	return EINVAL;
}