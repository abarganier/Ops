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

/*
 * Synchronization primitives.
 * The specifications of the functions are in synch.h.
 */

#include <types.h>
#include <lib.h>
#include <spinlock.h>
#include <wchan.h>
#include <thread.h>
#include <current.h>
#include <synch.h>

////////////////////////////////////////////////////////////
//
// Semaphore.

struct semaphore *
sem_create(const char *name, unsigned initial_count)
{
	struct semaphore *sem;

	sem = kmalloc(sizeof(*sem));
	if (sem == NULL) {
		return NULL;
	}

	sem->sem_name = kstrdup(name);
	if (sem->sem_name == NULL) {
		kfree(sem);
		return NULL;
	}

	sem->sem_wchan = wchan_create(sem->sem_name);
	if (sem->sem_wchan == NULL) {
		kfree(sem->sem_name);
		kfree(sem);
		return NULL;
	}

	spinlock_init(&sem->sem_lock);
	sem->sem_count = initial_count;

	return sem;
}

void
sem_destroy(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/* wchan_cleanup will assert if anyone's waiting on it */
	spinlock_cleanup(&sem->sem_lock);
	wchan_destroy(sem->sem_wchan);
	kfree(sem->sem_name);
	kfree(sem);
}

// NOTE: P is analogous to "wait"
void
P(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * complete the P without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the semaphore spinlock to protect the wchan as well. */
	spinlock_acquire(&sem->sem_lock);
	while (sem->sem_count == 0) {
		/*
		 *
		 * Note that we don't maintain strict FIFO ordering of
		 * threads going through the semaphore; that is, we
		 * might "get" it on the first try even if other
		 * threads are waiting. Apparently according to some
		 * textbooks semaphores must for some reason have
		 * strict ordering. Too bad. :-)
		 *
		 * Exercise: how would you implement strict FIFO
		 * ordering?
		 */
		wchan_sleep(sem->sem_wchan, &sem->sem_lock);
	}
	KASSERT(sem->sem_count > 0);
	sem->sem_count--;
	spinlock_release(&sem->sem_lock);
}

// NOTE: V is analogous to "signal"
void
V(struct semaphore *sem)
{
	KASSERT(sem != NULL);

	spinlock_acquire(&sem->sem_lock);

	sem->sem_count++;
	KASSERT(sem->sem_count > 0);
	wchan_wakeone(sem->sem_wchan, &sem->sem_lock);

	spinlock_release(&sem->sem_lock);
}

////////////////////////////////////////////////////////////
//
// Lock.

struct lock *
lock_create(const char *name)
{
	struct lock *lock;

	lock = kmalloc(sizeof(*lock));
	if (lock == NULL) {
		return NULL;
	}

	lock->lk_name = kstrdup(name);
	if (lock->lk_name == NULL) {
		kfree(lock);
		return NULL;
	}

	spinlock_init(&lock->lk_spinlock);

	HANGMAN_LOCKABLEINIT(&lock->lk_hangman, lock->lk_name);
	
	lock->lk_wchan = wchan_create(lock->lk_name);
	if (lock->lk_wchan == NULL) {
		spinlock_cleanup(&lock->lk_spinlock);
		kfree(lock->lk_name);
		kfree(lock);
		return NULL;
	}
	
	lock->lk_thread = NULL;	

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock->lk_thread == NULL);
	
	spinlock_cleanup(&lock->lk_spinlock);
	wchan_destroy(lock->lk_wchan);
	kfree(lock->lk_name);
	lock->lk_thread = NULL;
	kfree(lock);
}

void
lock_acquire(struct lock *lock)
{
	KASSERT(lock != NULL);

	/*
	 * May not block in an interrupt handler.
	 *
	 * For robustness, always check, even if we can actually
	 * acquire the lock without blocking.
	 */
	KASSERT(curthread->t_in_interrupt == false);

	/* Use the lock's spinlock to protect the wchan as well. */
	spinlock_acquire(&lock->lk_spinlock);
	
	HANGMAN_WAIT(&curthread->t_hangman, &lock->lk_hangman);
	
	while (lock->lk_thread != NULL) {
		wchan_sleep(lock->lk_wchan, &lock->lk_spinlock);
	}

	KASSERT(lock->lk_thread == NULL);
	lock->lk_thread = curthread;
	
	HANGMAN_ACQUIRE(&curthread->t_hangman, &lock->lk_hangman);
	
	spinlock_release(&lock->lk_spinlock);
}

void
lock_release(struct lock *lock)
{
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));

	spinlock_acquire(&lock->lk_spinlock);
	
	KASSERT(lock->lk_thread != NULL);
	wchan_wakeone(lock->lk_wchan, &lock->lk_spinlock);
	lock->lk_thread = NULL;
	
	HANGMAN_RELEASE(&curthread->t_hangman, &lock->lk_hangman);
	
	spinlock_release(&lock->lk_spinlock);
}

bool
lock_do_i_hold(struct lock *lock)
{
	return (lock->lk_thread == curthread);
}

////////////////////////////////////////////////////////////
//
// CV

struct cv *
cv_create(const char *name)
{
	struct cv *cv;

	cv = kmalloc(sizeof(*cv));
	if (cv == NULL) {
		return NULL;
	}

	spinlock_init(&cv->cv_spinlock);

	cv->cv_name = kstrdup(name);
	if (cv->cv_name==NULL) {
		spinlock_cleanup(&cv->cv_spinlock);
		kfree(cv);
		return NULL;
	}
	
	cv->cv_wchan = wchan_create(cv->cv_name);
	if(cv->cv_wchan == NULL) {
		spinlock_cleanup(&cv->cv_spinlock);
		kfree(cv->cv_name);
		kfree(cv);
		return NULL;
	}
	
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	KASSERT(cv != NULL);

	wchan_destroy(cv->cv_wchan);
	spinlock_cleanup(&cv->cv_spinlock);

	kfree(cv->cv_name);
	kfree(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);

	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&cv->cv_spinlock);
	
	lock_release(lock);
	wchan_sleep(cv->cv_wchan, &(cv->cv_spinlock));
	spinlock_release(&(cv->cv_spinlock));

	lock_acquire(lock);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	
	spinlock_acquire(&(cv->cv_spinlock));
	wchan_wakeone(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);				
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	KASSERT(cv != NULL);
	KASSERT(lock != NULL);
	KASSERT(lock_do_i_hold(lock));
	spinlock_acquire(&(cv->cv_spinlock));
	wchan_wakeall(cv->cv_wchan, &cv->cv_spinlock);
	spinlock_release(&cv->cv_spinlock);				
}

////////////////////////////////////////////////////////////
//
// RW-Lock

struct rwlock *
rwlock_create(const char * name) {
	struct rwlock *rw;
	
	rw = kmalloc(sizeof(*rw));
	if(rw == NULL) {
		return NULL;
	}

	spinlock_init(&rw->rw_spinlock);

	rw->rwlock_name = kstrdup(name);
	if(rw->rwlock_name == NULL) {
		spinlock_cleanup(&rw->rw_spinlock);
		kfree(rw);
		return NULL;
	}

	rw->r_wchan = wchan_create("read channel");
	if(rw->r_wchan == NULL) {
		spinlock_cleanup(&rw->rw_spinlock);
		kfree(rw->rwlock_name);
		kfree(rw);
		return NULL;
	}	
	
	rw->w_wchan = wchan_create("write channel");
	if(rw->w_wchan == NULL) {
		spinlock_cleanup(&rw->rw_spinlock);
		kfree(rw->rwlock_name);
		wchan_destroy(rw->r_wchan);
		kfree(rw);
		return NULL;
	}

	rw->r_count = 0;
	rw->w_wait = false;
	rw->w_exec = false;	
	return rw;
}

void
rwlock_destroy(struct rwlock *rw) {
	wchan_destroy(rw->r_wchan);
	wchan_destroy(rw->w_wchan);
	spinlock_cleanup(&rw->rw_spinlock);
	kfree(rw->rwlock_name);
	kfree(rw);
}

void
rwlock_acquire_read(struct rwlock *rw) {
	KASSERT(rw != NULL);
	KASSERT(curthread->t_in_interrupt == false); 
	KASSERT(rw->r_count >= 0);
	spinlock_acquire(&rw->rw_spinlock);
	while(rw->w_exec || rw->w_wait) {
		wchan_sleep(rw->r_wchan, &rw->rw_spinlock);
	}
	rw->r_count++;
	KASSERT(rw->r_count > 0);
	spinlock_release(&rw->rw_spinlock);
}

void
rwlock_release_read(struct rwlock *rw) {
	KASSERT(rw != NULL);
	spinlock_acquire(&rw->rw_spinlock);
	KASSERT(rw->r_count > 0);
	rw->r_count--;
	if(rw->r_count == 0) {
		wchan_wakeone(rw->w_wchan, &rw->rw_spinlock);
	}
	spinlock_release(&rw->rw_spinlock);
}

void
rwlock_acquire_write(struct rwlock *rw) {
	KASSERT(rw != NULL);
	KASSERT(curthread->t_in_interrupt == false); 
	spinlock_acquire(&rw->rw_spinlock);
	while(rw->r_count > 0 || rw->w_exec) { 		
		rw->w_wait = true;
		wchan_sleep(rw->w_wchan, &rw->rw_spinlock);
	}
	rw->w_exec = true;
	if(wchan_isempty(rw->w_wchan, &rw->rw_spinlock)) {
		rw->w_wait = false;
	}
	spinlock_release(&rw->rw_spinlock);
}

void
rwlock_release_write(struct rwlock *rw) {
	KASSERT(rw != NULL);
	spinlock_acquire(&rw->rw_spinlock);
	KASSERT(rw->w_exec);
	rw->w_exec = false;
	if(rw->w_wait) {
		wchan_wakeone(rw->w_wchan, &rw->rw_spinlock);
	} else {
		wchan_wakeall(rw->r_wchan, &rw->rw_spinlock);
	}
	spinlock_release(&rw->rw_spinlock);
}
