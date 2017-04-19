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

static char *kargs;
struct lock *exec_lock;
static char *kprogram;
static bool first_exec = true;

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
	newproc->p_cwd = curproc->p_cwd;

	VOP_INCREF(newproc->p_cwd);

	err = filetable_copy(curproc, newproc);
	if(err){
		proc_destroy(newproc);
		*retval = -1;
		return err;
	}

	err = as_copy(curproc->p_addrspace, &newproc->p_addrspace, newproc->pid);
	if(err){
		proc_destroy(newproc); 
		*retval = err;
		return err;
	}

	newproc->p_addrspace->as_pid = newproc->pid;
	*retval = newproc->pid;


	child_tf = trapframe_copy(parent_tf);
	if(child_tf == NULL){
		kprintf("Child trap frame is null \n");
		kfree(child_tf);
		proc_destroy(newproc);
		*retval = 1;
		return 1;
	}
	

	err = thread_fork("child", newproc, (void*)enter_forked_process, child_tf, (unsigned long)newproc->pid);
	if(err) {
		kfree(child_tf);
		kprintf("Failed thread_fork\n");
		proc_destroy(newproc);
		*retval = err;
		return err;
	}

	return 0;
}

static
bool
heap_overlaps_stack(struct addrspace *as, intptr_t heap_increase)
{
	return (vaddr_t)(as->stack_start - as->stack_size) < (vaddr_t)(as->heap_start + as->heap_size + heap_increase);
}

int
sys_sbrk(intptr_t amount, int32_t *retval)
{
	int err;

	// kprintf("In sys_sbrk with amount = %d\n", (int)amount);
	struct addrspace *as = proc_getas();
	if(amount % PAGE_SIZE > 0) {
		*retval = EINVAL;
		return EINVAL;
	}

	if(amount < 0 && (size_t)(amount*-1) > as->heap_size) {
		*retval = EINVAL;
		return EINVAL;
	}

	if(amount > 0 && heap_overlaps_stack(as, amount)) {
		*retval = ENOMEM;
		return ENOMEM;
	}

	*retval = as->heap_start + as->heap_size;

	// kprintf("Leaving sys_sbrk\n");
	// kprintf("as->heap_size = %u\n", as->heap_size);
	// kprintf("as->heap_start = %x\n", as->heap_start);
	// kprintf("heap ending address = %x\n", as->heap_start + as->heap_size);

	as->heap_size += amount;

	err = as_clean_segments(as);
	if(err) {
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

	//KASSERT(p_table->table[pid] != NULL);

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
	p_table->table[pid] = NULL;
	lock_release(p_table->pt_lock);

	proc_destroy(childproc); // leak memory for now, fix in asst3

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
		kprintf("SYS_EXIT: The parent has already exited! Cleaning myself up\n");
		lock_acquire(p_table->pt_lock);
		thread_exit();
		
		proc_destroy(p_table->table[pid]);
		p_table->table[pid] = NULL;
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
build_user_stack(char *kargs, size_t *lengths, size_t num_ptrs, userptr_t stkptr, size_t karg_size)
{
	int result;

	userptr_t og_stkptr = stkptr;

	stkptr -= karg_size;

	// copy out big spicy string
	result = copyout(kargs, stkptr, karg_size);
	if(result) {
		kprintf("Copyout of string values to user stack failed! Error: %d\n", result);
		return result;
	}

	stkptr = og_stkptr - karg_size;
	userptr_t argv_ptr = og_stkptr - karg_size - (4*(num_ptrs+1));

	char **argv = kmalloc(sizeof(char *) * num_ptrs+1);
	argv[num_ptrs] = NULL;

	for(size_t arg_number = 0; arg_number < num_ptrs; arg_number++) {
		argv[arg_number] = (char *)stkptr;
		stkptr += lengths[arg_number];
	}

	stkptr = og_stkptr - karg_size;

	result = copyout(argv, argv_ptr, 4*(num_ptrs+1));
	if(result) {
		kprintf("Copyout of argv failed!\n");
		return 1;
	}

	kfree(argv);
	return 0;

}

int
sys_execv(const char *program, char **args, int32_t *retval)
{
	if(first_exec) {
		kargs = kmalloc(ARG_MAX);
		KASSERT(kargs != NULL);
		kprogram = kmalloc(PATH_MAX);
		KASSERT(kprogram != NULL);
		first_exec = false;
	}
	lock_acquire(exec_lock);
	bzero(kargs, ARG_MAX);
	bzero(kprogram, PATH_MAX);
	size_t result;

	/* Use program pointer as src to copy in the program string to a kernel string space. Use only for address space call*/
	result = copyinstr((const_userptr_t) program, kprogram, PATH_MAX, &result); 
	if(result){
		lock_release(exec_lock);
		*retval = result;
		return result;
	}

	size_t index = 0;

	char *cur_head_of_args;

	/*Copy in 4 bytes of arg pointer to check validity*/
	result = copyin((const_userptr_t)args, &cur_head_of_args, 4);
	if(result){
		lock_release(exec_lock);
		*retval = result;
		return result;
	}
	
	while(cur_head_of_args != NULL){

		index++;
		result = copyin((const_userptr_t) args+(index*4), &cur_head_of_args, 4);
		if(result){
			lock_release(exec_lock);
			*retval = result;
			return result;
		}
	}

	char ** karg_ptrs = kmalloc(sizeof(char *) * index);
	if(karg_ptrs == NULL) {
		*retval = ENOMEM;
		return ENOMEM;
	}

	for(size_t j=0; j<index; j++){
		result = copyin((const_userptr_t) (args+j), &karg_ptrs[j], 4);
		if(result){
			lock_release(exec_lock);
			kprintf("Copying pointers failed \n");
			kfree(karg_ptrs);
			*retval = result;
			return result;
		}
	}

 	size_t karg_size = 0;
 	size_t ret_length = 0;
 	size_t rem_space = ARG_MAX-(4*index);
 	size_t *lengths = kmalloc(sizeof(size_t) * index); 

	for(size_t arg_num=0; arg_num<index; arg_num++){

		result = copyinstr((const_userptr_t) karg_ptrs[arg_num], (char *)&kargs[karg_size], rem_space, &ret_length);
		if(result){
			lock_release(exec_lock);
			*retval = result;
			kprintf("Copying in argument string number %d failed!\n", arg_num);
			return result;
		}

		karg_size += ret_length;
		rem_space -= ret_length;

		size_t num_nulls = (ret_length == 0 || ret_length%4 == 0) ? 0 : 4-(ret_length%4);

		lengths[arg_num] = ret_length+num_nulls;
		for(size_t k=0; k<num_nulls; k++){
			kargs[karg_size++] = '\0';
		}
	}

	// kprintf("Printing string buffer\n\n");
	// size_t i;
	// kprintf("\"");
	// for(i = 0; i < karg_size; i++) {
	// 	if(kargs[i] == '\0') {
	// 		kprintf("\\0");
	// 	} else {
	// 		kprintf("%c", kargs[i]);
	// 	}
	// }
	// kprintf("\"\n\n");
	// kprintf("string buffer size including null terminators is %d\n\n", karg_size);

	//Operations to load the executable into mem
	struct addrspace *as;
	struct vnode *v;
	vaddr_t entrypoint, stackptr;

	/* Open the file. */
	result = vfs_open(kprogram, O_RDONLY, 0, &v);
	if (result) {
		lock_release(exec_lock);
		kfree(karg_ptrs);
		kfree(lengths);
		*retval = result;
		return result;
	}

 	/* Create a new address space. */
	as = as_create();
	if (as == NULL) {
		lock_release(exec_lock);
		vfs_close(v);
		kfree(karg_ptrs);
		kfree(lengths);
		*retval = ENOMEM;
		return ENOMEM;
	}

	/* Switch to it and activate it. */
	struct addrspace *old_as;
	old_as = proc_setas(as);
	as_activate();

 	/* Load the executable. */
	result = load_elf(v, &entrypoint);
	if (result) {
		lock_release(exec_lock);
		vfs_close(v);
		proc_setas(old_as);
		as_activate();
		kfree(karg_ptrs);
		kfree(lengths);
		*retval = result;
		return result;
	}

	/* Done with the file now. */
 	vfs_close(v);

	/* Define the user stack in the address space */
	result = as_define_stack(as, &stackptr);
	if (result) {
		lock_release(exec_lock);
		proc_setas(old_as);
		as_activate();
		kfree(karg_ptrs);
		kfree(lengths);
		*retval = result;
		return result;
	}

 	//Copy arguments from kernel space to userpsace
	result = build_user_stack(kargs, lengths, index, (userptr_t)stackptr, karg_size);
	if(result) {
		lock_release(exec_lock);
		proc_setas(old_as);
		as_activate();
		kfree(karg_ptrs);
		kfree(lengths);
		*retval = result;
		return result;
	}

	stackptr -= (karg_size + ((index+1)*4));
	userptr_t argv_ptr_copy = (userptr_t)stackptr;

	kfree(karg_ptrs);
	kfree(lengths);

	lock_release(exec_lock);

	//Return to userspace using enter_new_process (in kern/arch/mips/locore/trap.c)
	enter_new_process(index, argv_ptr_copy, NULL, stackptr, entrypoint);

	proc_setas(old_as);
	as_activate();

 	//SHOULD NOT REACH HERE ON SUCCESS
	*retval = EINVAL;
	return EINVAL;
}