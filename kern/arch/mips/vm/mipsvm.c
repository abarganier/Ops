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

static struct spinlock coremap_lock = SPINLOCK_INITIALIZER;
uint32_t coremap_used_pages; // Also protected from coremap_lock

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

// static
// void
// print_coremap(void) 
// {
// 	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
// 	kprintf("\nPrinting coremap, num_pages used = %ull :\n", coremap_used_pages);
// 	spinlock_acquire(&coremap_lock);

// 	for(uint32_t offset = 0; offset < coremap_size; offset++) {
// 		kprintf("%d: ", offset);
// 		if(get_page_is_free(coremap[offset])) {
// 			kprintf("free, ");
// 		} else {
// 			kprintf("not_free, ");
// 		}

// 		if(get_is_fixed(coremap[offset])) {
// 			kprintf("fixed, ");
// 		} else {
// 			kprintf("not_fixed, ");
// 		}

// 		kprintf("chunk size: %llu\n", get_chunk_size(coremap[offset]));
// 	}	
// 	spinlock_release(&coremap_lock);
// }

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	uint64_t cm_entry;
	uint64_t first_index = 0;
	bool found_pages = false;
	spinlock_acquire(&coremap_lock);
	for(uint32_t offset = 0; offset < coremap_size; offset++) {

		cm_entry = coremap[offset];

		if(get_page_is_free(cm_entry)) {

			// If more than 1 page, make sure next chunks are free as well, else set index
			if(npages > 1) {

				uint64_t next_entry;

				for(uint32_t nextpage = 1; nextpage < npages; nextpage++) {

					next_entry = coremap[offset + nextpage];

					if(!get_page_is_free(next_entry)) {
						found_pages = false;
						break;
					}

					if(nextpage == npages - 1) { 
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
		spinlock_release(&coremap_lock);
		return 0;
	}

	vaddr_t virtual_address = PADDR_TO_KVADDR(first_index*4096);
	
	/*
	 *	NOTE: AT BOOT, CURPROC DOESN'T EXIST YET, SET OWNER TO 0 FOR NOW. The kernel probably owns this stuff, anyway? 
	 */

	/*
	 *	Set first coremap entry
	 */
	uint64_t first_entry = build_page_entry(npages, 0, false, false, true, false, virtual_address);
	coremap[first_index] = first_entry;
	coremap_used_pages++;

	/*
	 *	Set additional coremap entries (if more than one)
	 */
	uint64_t mid_entry = build_page_entry(npages, 0, false, false, false, false, virtual_address);
	for(uint64_t entry = 1; entry < npages; entry++) {
		coremap[first_index + entry] = mid_entry;
		coremap_used_pages++;
	}
	spinlock_release(&coremap_lock);

	return virtual_address;
}

void
free_kpages(vaddr_t addr)
{
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	bool not_found = false;

	spinlock_acquire(&coremap_lock);
	for(uint32_t entry = 0; entry < coremap_size; entry++) {

		if(get_vaddr(coremap[entry]) == addr) {

			if(get_is_fixed(coremap[entry])) {
				panic("Unable to free fixed coremap entries!\n");
			}

			// should be first chunk of set
			KASSERT(get_is_first_chunk(coremap[entry]));

			uint64_t chunk_size = get_chunk_size(coremap[entry]);

			for(uint32_t chunk = 0; chunk < chunk_size; chunk++) {
				coremap[entry + chunk] = 0;
				coremap_used_pages--;
			}

			break;
		}

		if(entry == coremap_size-1) {
			not_found = true;
		}
	}
	spinlock_release(&coremap_lock);
	
	if(not_found) {
		panic("free_kpages was unable to find the address passed!\n");
	}
}

unsigned
int
coremap_used_bytes() {
	spinlock_acquire(&coremap_lock);
	unsigned int bytes = coremap_used_pages * 4096;
	spinlock_release(&coremap_lock);
	return bytes;
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