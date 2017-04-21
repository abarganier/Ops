/*
 * Copyright (c) 2013
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

/*
 * Process support.
 *
 * There is (intentionally) not much here; you will need to add stuff
 * and maybe change around what's already present.
 *
 * p_lock is intended to be held when manipulating the pointers in the
 * proc structure, not while doing any significant work with the
 * things they point to. Rearrange this (and/or change it to be a
 * regular lock) as needed.
 *
 * Unless you're implementing multithreaded user processes, the only
 * process that will have more than one thread is the kernel process.
 */

#include <types.h>
#include <spl.h>
#include <proc.h>
#include <current.h>
#include <addrspace.h>
#include <vnode.h>
#include <synch.h>
#include <vfs.h>
#include <kern/unistd.h>
#include <proc_syscalls.h>
/*
 * The process for the kernel; this holds all the kernel-only threads.
 */
struct proc *kproc;

struct proc_table *p_table;

volatile pid_t pid_counter;

bool is_kproc = true;

/* NOTE: proc_table is a singleton struct and should only ever be init'd once */
struct proc_table *
proc_table_create(void) 
{
	struct proc_table *new_ptable;
	new_ptable = kmalloc(sizeof(*new_ptable));
	if(new_ptable == NULL) {
		return NULL;
	}

	for(int i = 0; i < 256; i++) {
		new_ptable->table[i] = NULL;
	}

	new_ptable->pt_lock = lock_create("ptable_lock");
	if(new_ptable->pt_lock == NULL) {
		kfree(new_ptable);
		return NULL;
	}

	return new_ptable;
}

void 
proc_table_destroy(struct proc_table *table) 
{	
	lock_destroy(table->pt_lock);
	kfree(table);
}

/*
 * Assign next available PID. Be sure the caller uses the p_table lock!
 */

int32_t
next_pid(void)
{
	// This makes sure the kproc is assigned PID of 1
	if(pid_counter < PID_MIN) {
		pid_counter = 1;
	}

	pid_t pid;
	bool pid_found = false;

	if(pid_counter > 255) {
		pid_counter = PID_MIN;
	}

	for(pid = pid_counter; pid < 256; pid++) {
		if(p_table->table[pid] == NULL) {
			pid_found = true;
			break;
		} else {
			pid_counter++;
		}
		if(pid == 255) {
			pid = PID_MIN;
		}
	}

	if(!pid_found) {
		kprintf("next_pid(): ERROR! No free PID was available in the process table!\n");
		return -1;
	}
	
	KASSERT(p_table->table[pid] == NULL);

	pid_counter++;

	return pid;
}
/*
 * Create a proc structure.
 */
static
struct proc *
proc_create(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	proc->exit_sem = NULL;
	proc->exit_sem = sem_create("process_exit_sem", 0);
	if(proc->exit_sem == NULL) {
		kfree(proc->p_name);
		spinlock_cleanup(&proc->p_lock);
		kfree(proc);
		return NULL;
	}

	proc->fork_lock = NULL;
	proc->fork_lock = lock_create("fork_lock");
	if(proc->fork_lock == NULL) {
		kfree(proc->p_name);
		spinlock_cleanup(&proc->p_lock);
		sem_destroy(proc->exit_sem);
		kfree(proc);
		return NULL;
	}

	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	if(!is_kproc) {
		lock_acquire(p_table->pt_lock);
	}

	int32_t new_pid = next_pid();	
	if(new_pid < 0) {
		kprintf("proc_create: Error! new_pid() returned -1\n");
		kfree(proc->p_name);
		spinlock_cleanup(&proc->p_lock);
		sem_destroy(proc->exit_sem);
		kfree(proc);
		return NULL;
	}

	proc->pid = (pid_t) new_pid;
	KASSERT(p_table->table[proc->pid] == NULL);
	p_table->table[proc->pid] = proc;
	
	if(!is_kproc) {
		lock_release(p_table->pt_lock);
	}


	/* First process has no parent, shouldn't 
	   be valid index into process table. 
	   Valid value set during sys_fork() */
	proc->ppid = 1;

	proc->exited = false;
	proc->exit_status = 0;

	for(int i = 0; i < 64; i++) {
		proc->filetable[i] = NULL;
	}

	return proc;
}

/* 	Wrapper of proc_create to be used in sys_fork(). The main difference
	is proc_create() assigns a PID whereas proc_create_child() leaves PID
	assignment responsibility to the caller 
*/
struct proc *
proc_create_child(const char *name)
{
	struct proc *proc;

	proc = kmalloc(sizeof(*proc));
	if (proc == NULL) {
		return NULL;
	}
	proc->p_name = kstrdup(name);
	if (proc->p_name == NULL) {
		kfree(proc);
		return NULL;
	}

	proc->p_numthreads = 0;
	spinlock_init(&proc->p_lock);

	proc->exit_sem = NULL;
	proc->exit_sem = sem_create("process_exit_sem", 0);
	if(proc->exit_sem == NULL) {
		kfree(proc->p_name);
		spinlock_cleanup(&proc->p_lock);
		kfree(proc);
		return NULL;
	}	

	proc->fork_lock = NULL;
	proc->fork_lock = lock_create("fork_lock");
	if(proc->fork_lock == NULL) {
		kfree(proc->p_name);
		spinlock_cleanup(&proc->p_lock);
		sem_destroy(proc->exit_sem);
		kfree(proc);
		return NULL;
	}


	/* VM fields */
	proc->p_addrspace = NULL;

	/* VFS fields */
	proc->p_cwd = NULL;

	/* First process has no parent, shouldn't 
	   be valid index into process table. 
	   Valid value set during sys_fork() */
	proc->ppid = 1;

	proc->exited = false;
	proc->exit_status = 0;

	for(int i = 0; i < 64; i++) {
		proc->filetable[i] = NULL;
	}

	return proc;
}

/*
 * Destroy a proc structure.
 *
 * Note: nothing currently calls this. Your wait/exit code will
 * probably want to do so.
 */
void
proc_destroy(struct proc *proc)
{
	/*
	 * You probably want to destroy and null out much of the
	 * process (particularly the address space) at exit time if
	 * your wait/exit design calls for the process structure to
	 * hang around beyond process exit. Some wait/exit designs
	 * do, some don't.
	 */

	KASSERT(proc != NULL);
	KASSERT(proc != kproc);

	/*
	 * We don't take p_lock in here because we must have the only
	 * reference to this structure. (Otherwise it would be
	 * incorrect to destroy it.)
	 */

	/* VFS fields */
	if (proc->p_cwd) {
		// kprintf("PROC %d wants to die\n", proc->pid);
		// kprintf("Address of proc's vnode: %p\n", proc->p_cwd);
		// kprintf("Vnode's refcount: %d \n", proc->p_cwd->vn_refcount);
		VOP_DECREF(proc->p_cwd);
		//vfs_close(proc->p_cwd);  //Closes vnode if ref_count is 0
		proc->p_cwd = NULL;
	}

	/* VM fields */
	if (proc->p_addrspace) {
		/*
		 * If p is the current process, remove it safely from
		 * p_addrspace before destroying it. This makes sure
		 * we don't try to activate the address space while
		 * it's being destroyed.
		 *
		 * Also explicitly deactivate, because setting the
		 * address space to NULL won't necessarily do that.
		 *
		 * (When the address space is NULL, it means the
		 * process is kernel-only; in that case it is normally
		 * ok if the MMU and MMU- related data structures
		 * still refer to the address space of the last
		 * process that had one. Then you save work if that
		 * process is the next one to run, which isn't
		 * uncommon. However, here we're going to destroy the
		 * address space, so we need to make sure that nothing
		 * in the VM system still refers to it.)
		 *
		 * The call to as_deactivate() must come after we
		 * clear the address space, or a timer interrupt might
		 * reactivate the old address space again behind our
		 * back.
		 *
		 * If p is not the current process, still remove it
		 * from p_addrspace before destroying it as a
		 * precaution. Note that if p is not the current
		 * process, in order to be here p must either have
		 * never run (e.g. cleaning up after fork failed) or
		 * have finished running and exited. It is quite
		 * incorrect to destroy the proc structure of some
		 * random other process while it's still running...
		 */
		struct addrspace *as;

		if (proc == curproc) {
			as = proc_setas(NULL);
			as_deactivate();
		}
		else {
			as = proc->p_addrspace;
			proc->p_addrspace = NULL;
		}
		as_destroy(as);
	}


	KASSERT(proc->p_numthreads == 0);
	spinlock_cleanup(&proc->p_lock);

	lock_destroy(proc->fork_lock);
	
	//Clean up semaphore, filehandles, name, thread
	sem_destroy(proc->exit_sem);
	

	/*
	 *Need to destroy all file handles with not in use
	 */

	for(size_t i=0; i<64; i++){
		if(proc->filetable[i] != NULL){
			filehandle_destroy(proc->filetable[i]);	
		}	
	}


	kfree(proc->p_name);
	kfree(proc);

}

/*
 * Create the process structure for the kernel.
 */
void
proc_bootstrap(void)
{
	p_table = proc_table_create();
	kproc = proc_create("[kernel]");
	if (kproc == NULL) {
		panic("proc_create for kproc failed\n");
	}
}

/*
 * Create a fresh proc for use by runprogram.
 *
 * It will have no address space and will inherit the current
 * process's (that is, the kernel menu's) current directory.
 */
struct proc *
proc_create_runprogram(const char *name)
{
	struct proc *newproc;

	newproc = proc_create(name);
	if (newproc == NULL) {
		return NULL;
	}

	/* VM fields */

	newproc->p_addrspace = NULL;

	/* VFS fields */

	/*
	 * Lock the current process to copy its current directory.
	 * (We don't need to lock the new process, though, as we have
	 * the only reference to it.)
	 */
	spinlock_acquire(&curproc->p_lock);
	if (curproc->p_cwd != NULL) {
		VOP_INCREF(curproc->p_cwd);
		newproc->p_cwd = curproc->p_cwd;
	}
	spinlock_release(&curproc->p_lock);

	exec_lock = lock_create("execv_lock");
	
	if(is_kproc) {
		is_kproc = false;
	}

	int result;
	/* Add elements to file table */
	newproc->filetable[0] = filehandle_create("con:", STDIN_FILENO); 
	if(newproc->filetable[0] == NULL){
		proc_destroy(newproc);	
		return NULL;
	}
	result = vfs_open(newproc->filetable[0]->fh_name, STDIN_FILENO, 0, &newproc->filetable[0]->fh_vnode);
	if(result) {
		filehandle_destroy(newproc->filetable[0]);
		proc_destroy(newproc);	 
		return NULL;
	}

	newproc->filetable[1] = filehandle_create("con:", STDOUT_FILENO); 
	if(newproc->filetable[1] == NULL){
		filehandle_destroy(newproc->filetable[0]);
		proc_destroy(newproc);	
		return NULL;
	}
	
	result = vfs_open(newproc->filetable[1]->fh_name, STDOUT_FILENO, 0, &newproc->filetable[1]->fh_vnode);
	if(result) {
		filehandle_destroy(newproc->filetable[0]);
		filehandle_destroy(newproc->filetable[1]);
		proc_destroy(newproc);	 
		return NULL;
	}

	newproc->filetable[2] = filehandle_create("con:", STDERR_FILENO); 
	if(newproc->filetable[2] == NULL){
		filehandle_destroy(newproc->filetable[0]);
		filehandle_destroy(newproc->filetable[1]);
		proc_destroy(newproc);		
		return NULL;
	}
	result = vfs_open(newproc->filetable[2]->fh_name, STDERR_FILENO, 0, &newproc->filetable[2]->fh_vnode);
	if(result) {
		filehandle_destroy(newproc->filetable[0]);
		filehandle_destroy(newproc->filetable[1]);
		filehandle_destroy(newproc->filetable[2]);
		proc_destroy(newproc);	 
		return NULL;
	}
	return newproc;
}

/*
 * Add a thread to a process. Either the thread or the process might
 * or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
int
proc_addthread(struct proc *proc, struct thread *t)
{
	int spl;

	KASSERT(t->t_proc == NULL);

	spinlock_acquire(&proc->p_lock);
	proc->p_numthreads++;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = proc;
	splx(spl);

	return 0;
}

/*
 * Remove a thread from its process. Either the thread or the process
 * might or might not be current.
 *
 * Turn off interrupts on the local cpu while changing t_proc, in
 * case it's current, to protect against the as_activate call in
 * the timer interrupt context switch, and any other implicit uses
 * of "curproc".
 */
void
proc_remthread(struct thread *t)
{
	struct proc *proc;
	int spl;

	proc = t->t_proc;
	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	KASSERT(proc->p_numthreads > 0);
	proc->p_numthreads--;
	spinlock_release(&proc->p_lock);

	spl = splhigh();
	t->t_proc = NULL;
	splx(spl);
}

/*
 * Fetch the address space of (the current) process.
 *
 * Caution: address spaces aren't refcounted. If you implement
 * multithreaded processes, make sure to set up a refcount scheme or
 * some other method to make this safe. Otherwise the returned address
 * space might disappear under you.
 */
struct addrspace *
proc_getas(void)
{
	struct addrspace *as;
	struct proc *proc = curproc;

	if (proc == NULL) {
		return NULL;
	}

	spinlock_acquire(&proc->p_lock);
	as = proc->p_addrspace;
	spinlock_release(&proc->p_lock);
	return as;
}

/*
 * Change the address space of (the current) process. Return the old
 * one for later restoration or disposal.
 */
struct addrspace *
proc_setas(struct addrspace *newas)
{
	struct addrspace *oldas;
	struct proc *proc = curproc;

	KASSERT(proc != NULL);

	spinlock_acquire(&proc->p_lock);
	oldas = proc->p_addrspace;

	proc->p_addrspace = newas;
	proc->p_addrspace->as_pid = proc->pid;

	spinlock_release(&proc->p_lock);
	return oldas;
}


struct filehandle *
filehandle_create(const char *name, int fh_perm)
{
	struct filehandle *filehandle;

	filehandle = kmalloc(sizeof(*filehandle));
	if (filehandle == NULL){
		return NULL;
	}

	filehandle->fh_name = kstrdup(name);
	if(filehandle->fh_name == NULL){
		kfree(filehandle);
		return NULL;
	}

	filehandle->fh_perm = fh_perm;
	filehandle->fh_offset_value = 0;
	filehandle->num_open_proc = 1;
	filehandle->fh_vnode = NULL;

	filehandle->fh_lock = lock_create("file_handle_lock");
	if(filehandle->fh_lock == NULL){
		kfree(filehandle->fh_name);
		kfree(filehandle);
		return NULL;
	}
	
	return filehandle;
}

void 
filehandle_destroy(struct filehandle *filehandle)
{
	KASSERT(filehandle != NULL);
	lock_acquire(filehandle->fh_lock);

	filehandle->num_open_proc--;
	if(filehandle->num_open_proc < 1) {
		lock_release(filehandle->fh_lock);
		lock_destroy(filehandle->fh_lock);
		VOP_DECREF(filehandle->fh_vnode);
		kfree(filehandle->fh_name);
		kfree(filehandle);
	} else {
		VOP_DECREF(filehandle->fh_vnode);
		lock_release(filehandle->fh_lock);
	}
}

/*	
 * Copy the filetable pointers from a src proc to a dest proc.
 * Error codes are simple, only flags if null pointers are passed.
 * Mainly used as a supporting method for the fork() syscall.
 */
int
filetable_copy(struct proc *src, struct proc *dest)
{
	if(src == NULL || dest == NULL) {
		return 1;
	}
	int size = 64;
	int i;
	for(i = 0; i < size; i++) {
		if(src->filetable[i] != NULL) { 
			lock_acquire(src->filetable[i]->fh_lock);
			dest->filetable[i] = src->filetable[i];
			dest->filetable[i]->num_open_proc++;
			lock_release(src->filetable[i]->fh_lock);
		} else {
			dest->filetable[i] = NULL;
		}
	}
	return 0;
}

