/*
 * Copyright (c) 2001, 2002, 2009
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
 * Driver code is in kern/tests/synchprobs.c We will replace that file. This
 * file is yours to modify as you see fit.
 *
 * You should implement your solution to the stoplight problem below. The
 * quadrant and direction mappings for reference: (although the problem is, of
 * course, stable under rotation)
 *
 *   |0 |
 * -     --
 *    01  1
 * 3  32
 * --    --
 *   | 2|
 *
 * As way to think about it, assuming cars drive on the right: a car entering
 * the intersection from direction X will enter intersection quadrant X first.
 * The semantics of the problem are that once a car enters any quadrant it has
 * to be somewhere in the intersection until it call leaveIntersection(),
 * which it should call while in the final quadrant.
 *
 * As an example, let's say a car approaches the intersection and needs to
 * pass through quadrants 0, 3 and 2. Once you call inQuadrant(0), the car is
 * considered in quadrant 0 until you call inQuadrant(3). After you call
 * inQuadrant(2), the car is considered in quadrant 2 until you call
 * leaveIntersection().
 *
 * You will probably want to write some helper functions to assist with the
 * mappings. Modular arithmetic can help, e.g. a car passing straight through
 * the intersection entering from direction X will leave to direction (X + 2)
 * % 4 and pass through quadrants X and (X + 3) % 4.  Boo-yah.
 *
 * Your solutions below should call the inQuadrant() and leaveIntersection()
 * functions in synchprobs.c to record their progress.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

/*
 * Each quadrant gets its own semaphore
 */
struct semaphore* sem0; 
struct semaphore* sem1; 
struct semaphore* sem2; 
struct semaphore* sem3;

/*
 * Each method gets its own lock to prevent overlapping calls from interfering with each other 
 */
struct lock* l1;
struct lock* l2;
struct lock* l3;
 
/*
 * Called by the driver during initialization.
 */
void
stoplight_init() {
	sem0 = sem_create("sem0", 1);
	if (sem0 == NULL) {
		panic("Stoplight.c: Failed to create sem0!\n");
	}
	
	sem1 = sem_create("sem1", 1);
	if (sem1 == NULL) {
		panic("Stoplight.c: failed to create sem1!\n");
	}		
	
	sem2 = sem_create("sem2", 1);
	if (sem2 == NULL) {
		panic("Stoplight.c: Failed to create sem2!\n");
	}
	
	sem3 = sem_create("sem3", 1);
	if (sem3 == NULL) {
		panic("Stoplight.c: Failed to create sem3!\n");
	}		
	l1 = lock_create("lock1");
	if (l1 == NULL) {
		panic("Stoplight.c: Failed to create lock l1!\n");
	}
	l2 = lock_create("lock2");
	if (l2 == NULL) { 
		panic("Stoplight.c: Failed to create lock l2!\n");
	}	
	l3 = lock_create("lock3");
	if (l3 == NULL) {
		panic("Stoplight.c: Failed to create lock l3!\n");
	}	
}

/*
 * Called by the driver during teardown.
 */
void stoplight_cleanup() {
	sem_destroy(sem0);
	sem_destroy(sem1);
	sem_destroy(sem2);
	sem_destroy(sem3);
	lock_destroy(l1);
	lock_destroy(l2);
	lock_destroy(l3);
}

/*
 * Used to get a reference to a semaphore given a corresponding quadrant ID
 */
struct semaphore* get_sem(uint32_t quadrant) {
	switch (quadrant) {
		case 0:
			return sem0;
			break;
		case 1:
			return sem1;
			break;
		case 2: 
			return sem2;
			break;
		case 3:
			return sem3;
			break;
		default:
			panic("stoplight.c:getSem(): Invalid quadrant ID passed!\n");
			return NULL;
	}
}

/*
 * Enters: X
 * Leaves: X
 */
void
turnright(uint32_t direction, uint32_t index)
{
	struct semaphore* sem = get_sem(direction);
	lock_acquire(l1);
	P(sem);
	inQuadrant(direction, index);
	leaveIntersection(index);
	V(sem);
	lock_release(l1);
}

/*
 * Enters: X
 * Leaves: (X+3)%4
 */
void
gostraight(uint32_t direction, uint32_t index)
{
	struct semaphore* sem_enter = get_sem(direction);
	uint32_t leave_direction = (direction + 3) % 4;
	struct semaphore* sem_leave = get_sem(leave_direction);
	lock_acquire(l2);
	P(sem_enter);
	P(sem_leave);
	inQuadrant(direction, index);
	inQuadrant(leave_direction, index);
	leaveIntersection(index);
	V(sem_enter);
	V(sem_leave);
	lock_release(l2);
}

/*
 * Enters: X
 * Passes through: (X+3)%4
 * Leaves: (X+2)%4
 */
void
turnleft(uint32_t direction, uint32_t index)
{
	struct semaphore* sem_enter = get_sem(direction);
	uint32_t direction_passthrough = (direction + 3) % 4;
	uint32_t direction_leave = (direction + 2) % 4;
	struct semaphore* sem_passthrough = get_sem(direction_passthrough);
	struct semaphore* sem_leave = get_sem(direction_leave);
	lock_acquire(l3);
	P(sem_enter);
	P(sem_passthrough);
	P(sem_leave);
	inQuadrant(direction, index);
	inQuadrant(direction_passthrough, index);	
	inQuadrant(direction_leave, index);	
	leaveIntersection(index);
	V(sem_enter);
	V(sem_passthrough);
	V(sem_leave);
	lock_release(l3);
}
