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
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	uint64_t cm_entry;
	uint64_t first_index = 0;
	bool found_pages = false;
	// kprintf("alloc_kpages called, requested %d pages\n", npages);

	for(uint32_t offset = 0; offset < coremap_size; offset++) {
		// kprintf("looking for first free page, offset = %d\n", offset);
		// paddr_t address = coremap_paddr + ( sizeof(uint64_t) * offset );
		// cm_entry = (uint64_t) address;
		cm_entry = coremap[offset];

		if(get_page_is_free(cm_entry) ) {

			if(npages > 1) {

				uint64_t next_entry;

				for(uint32_t nextpage = 1; nextpage < npages; nextpage++) {

					// paddr_t next_address = address + ( sizeof(uint64_t) * nextpage );
					// next_entry = (uint64_t) next_address;
					next_entry = coremap[offset + nextpage];

					if( !get_page_is_free(next_entry) ) {
						found_pages = false;
						break;
					}
					if( nextpage == npages-1 ) { // if we reach the end without breaking
						first_index = offset;
						found_pages = true;
					}
				}
			}  else {
				first_index = offset;
				found_pages = true;
			}
		} 

		if(found_pages) {
			break;
		}
	}

	if(!found_pages) {
		// panic("Unable to find %d contiguous pages in coremap!\n", npages);
		panic("Unable to find enough contiguous pages in the coremap!\n");
		return (vaddr_t) 0;
	}

	vaddr_t virtual_address = PADDR_TO_KVADDR(first_index*4096);
	/**
	 *	CURPROC DOESN'T EXIST YET, SET TO 0 FOR NOW
	 **/
	// pid_t owner = curproc->pid; 
	uint64_t first_entry;
	if(npages == 1) {
		first_entry = build_page_entry(npages, 0, false, false, true, true, virtual_address);
	} else {
		first_entry = build_page_entry(npages, 0, false, false, true, false, virtual_address);
	}
	coremap[first_index] = first_entry;


	uint64_t last_entry = build_page_entry(npages, 0, false, false, false, true, virtual_address);
	if(npages == 2) {

		coremap[first_index+1] = first_entry;
	
	} else if(npages > 2) {
	
		uint64_t mid_entry = build_page_entry(npages, 0, false, false, false, false, virtual_address);
		for(uint64_t entry = 0; entry < npages-1; entry++) {
			coremap[entry+1] = mid_entry;
		}
		coremap[first_index+npages-1] = last_entry;
	}

	return virtual_address;
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