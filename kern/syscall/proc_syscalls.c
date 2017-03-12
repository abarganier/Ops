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
	kfree(tf);
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
build_user_stack(char strings[][100], size_t * lengths, int arrsize, vaddr_t * stkptr)
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
	kprintf("Entering execv\n");
	int result;
	// int num_bytes = 0;
	char kprogram[PATH_MAX];
	size_t prog_len = 0;
	
	result = copyinstr((const_userptr_t) program, kprogram, PATH_MAX, &prog_len); 
	if(result){
		*retval = result;
		return result;
	}

	kprintf("Copied in prog name: %s\n", kprogram);
	char *argv;

	// Copy in pointer value to get args address in userspace
	result = copyin((const_userptr_t) args, &argv, 4);
	if(result){
		kprintf("Copyin of argv failed\n");
		*retval = result;
		return result;
	}

	char *argv_temp = argv;
	char *ptr_val = argv_temp;

	int index = 0;
	// Count number of arguments
	while(ptr_val != NULL){
		index++;
		argv_temp += 4;

		result = copyin((const_userptr_t) argv_temp, &ptr_val, 4);
		if(result){
			kprintf("Copyin failed while counting args. index = %d\n", index);
			*retval = result;
			return result;
		}
	}


	int i;
	argv_temp = argv;
	char * arg_ptrs[index];
	// Get memory addresses for strings.
	// Since array is contiguous in memory, we can just keep adding 4 bytes for # of args.
	for(i = 0; i < index; i++) {
		arg_ptrs[i] = (char *)(argv_temp+4);
		argv_temp += 4;
	}

	char argstrbuf[index][100];
	size_t lengths[index];
	// Copy in string values that our argument pointers point to in userspace
	for(i = 0; i < index; i++) {
		result = copyinstr((const_userptr_t)arg_ptrs[i], argstrbuf[i], 100, &lengths[i]); 
		if(result){
			kprintf("copyinstr failed on try %d\n", i);
			kprintf("Error %d\n", result);
			*retval = result;
			return result;
		}
	}

	kprintf("Copied in arg string values.\n");
	for(i = 0; i < index; i++) {
		kprintf("Arg #%d value: %s\n", i+1, argstrbuf[i]);
	}

	struct addrspace *as;
	struct vnode *vn;
	vaddr_t entrypoint, stackptr;

	kprintf("Opening executable file.\n");
	result = vfs_open(kprogram, O_RDONLY, 0, &vn);
	if (result) {
		*retval = result;
		return result;
	}

	/* We should be a new process. */
	// KASSERT(proc_getas() == NULL);

	kprintf("Creating new address space.\n");
	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		vfs_close(vn);
		*retval = ENOMEM;
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	proc_setas(as);
	as_activate();

	kprintf("Loading executable.\n");
	/* Load the executable. */
	result = load_elf(vn, &entrypoint);
	if (result) {
		vfs_close(vn);
		*retval = result;
		return result;
	}

	kprintf("Closing executable file.\n");
	vfs_close(vn);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		*retval = result;
		return result;
	}
	kprintf("Stkptr after as_define_stack: %x", stackptr);
	stackptr -= 1; // 0x80000000 is not a valid portion of the user stack

	// //Copy arguments from kernel space to userpsace
	result = build_user_stack(argstrbuf, lengths, index, &stackptr);
	if(result) {
		*retval = 1;
		return 1;
	}

	// vaddr_t array_start = stackptr;

	// //Return to userspace using enter_new_process (in kern/arch/mips/locore/trap.c)
	enter_new_process(0, (userptr_t)stackptr, NULL, stackptr, entrypoint);

	//SHOULD NOT REACH HERE ON SUCCESS
	*retval = EINVAL;
	return -1;
}