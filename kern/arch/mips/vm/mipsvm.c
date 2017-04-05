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
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>


/*
 * Wrap ram_stealmem in a spinlock.
 */
// static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;


void
vm_bootstrap(void)
{
	/* Do nothing. */
}

// static
// paddr_t
// getppages(unsigned long npages)
// {
// 	(void)npages;
// 	return 0;
// }

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	uint64_t cm_entry;
	bool found_pages = false;

	for(uint32_t offset = 1; offset < coremap_size; offset++) {

		paddr_t address = coremap_paddr + ( sizeof(uint64_t) * offset );
		cm_entry = (uint64_t) address;

		if( get_page_is_free(cm_entry) ) {

			uint64_t next_entry;

			for(uint32_t nextpage = 1; nextpage < npages; nextpage++) {

				paddr_t next_address = address + ( sizeof(uint64_t) * nextpage );
				next_entry = (uint64_t) next_address;
				if( !get_page_is_free(next_entry) ) {
					found_pages = false;
					break;
				}
				if( nextpage == npages-1) { // if we reach the end without breaking
					found_pages = true;
				}
			}
		}

		if(found_pages) {
			break;
		}
	}

	if(!found_pages) {
		panic("Unable to find %d contiguous pages in coremap!\n", npages);
	}

	// Now ready to set state of n coremap entries

	for(uint64_t entry = 0; entry < npages; entry++) {
		// Set each coremap entry with proper values
	}

	//Convert PADDR to VADDR

	//Update [all?] reserved entries with owner VADDR

	// Return virtual address of the first page.

	return 0;
}

void
free_kpages(vaddr_t addr)
{
	/* nothing - leak the memory. */
	(void)addr;
}

unsigned
int
coremap_used_bytes() {

	/* dumbvm doesn't track page allocations. Return 0 so that khu works. */
	return 0;
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("dumbvm tried to do tlb shootdown?!\n");
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	(void)faulttype;
	(void)faultaddress;
	return 0;
}