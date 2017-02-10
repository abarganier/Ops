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
 * Driver code is in kern/tests/synchprobs.c We will
 * replace that file. This file is yours to modify as you see fit.
 *
 * You should implement your solution to the whalemating problem below.
 */

#include <types.h>
#include <lib.h>
#include <thread.h>
#include <test.h>
#include <synch.h>

struct semaphore *male_sem;
struct semaphore *female_sem;
struct semaphore *mm_sem;

/*
 * Called by the driver during initialization.
 */

void whalemating_init() {
	male_sem = sem_create("male_sem", 0);
	if(male_sem == NULL) {
		panic("whalemating_init(): male_sem creation failed.\n");
	}
	female_sem = sem_create("female_sem", 0);
	if(female_sem == NULL) {
		panic("whalemating_init(): female_sem creation failed.\n");
	}
	mm_sem = sem_create("mm_sem", 1);
	if(mm_sem == NULL) {
		panic("whalemating_init(): mm_sem creation failed.\n");
	}

}

/*
 * Called by the driver during teardown.
 */

void
whalemating_cleanup() {
	sem_destroy(male_sem);
	sem_destroy(female_sem);
	sem_destroy(mm_sem);
}

void
male(uint32_t index)
{
	male_start(index);
	P(male_sem);
	male_end(index);
}

void
female(uint32_t index)
{
	female_start(index);
	P(female_sem);
	female_end(index);
}

void
matchmaker(uint32_t index)
{
	matchmaker_start(index);
	P(mm_sem);
	V(male_sem);
	V(female_sem);
	matchmaker_end(index);
	V(mm_sem);
}
