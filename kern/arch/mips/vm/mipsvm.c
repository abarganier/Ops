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
static bool debug_mode = false;

uint32_t coremap_used_pages; // Also protected from coremap_lock
uint32_t num_fixed_pages;

void
vm_bootstrap(void)
{
	/* Do nothing. */
}

static
paddr_t
get_ppn(struct addrspace *as, vaddr_t vaddr, paddr_t *ppn) {
	
	int32_t err;
	
	err = pt_add(as->pt, vaddr, ppn);
	if(err) {
		panic("pt_add in get_ppn failed! Error: %d\n", err);
	}
	return 0;
}


int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	(void)faulttype;
	struct addrspace *as = proc_getas();
	if(as == NULL) {
		panic("ERROR: Current process addrspace undefined in vm_fault!\n");
	}
	kprintf("Fault address: %x\n", faultaddress);
	if(vaddr_in_segment(as, faultaddress)) {
		paddr_t ppn;
		int32_t err;
		err = get_ppn(as, faultaddress, &ppn);
		if(err) {
			kprintf("ERROR: get_ppn failed in vm_fault!\n");
		}
	} else {
		panic("SEGFAULT\n");
	}

	return 0;
}

/*
 * Debugging method to print the coremap. Be sure to call within the crit. section.
 */
static
void
print_coremap(void) 
{
	kprintf("\nPrinting coremap, num_pages used = %u :\n", coremap_used_pages);
	
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	for(uint32_t offset = 0; offset < coremap_size; offset++) {
		kprintf("%d: ", offset);
		if(get_page_is_free(coremap[offset])) {
			kprintf("free, ");
		} else {
			kprintf("not_free, ");
		}


		if(get_is_fixed(coremap[offset])) {
			kprintf("fixed, ");
		} else {
			kprintf("not_fixed, ");
		}

		kprintf("chunk size: %llu\n", get_chunk_size(coremap[offset]));
	}	
}

static
bool
find_pages(uint32_t *index_ptr, uint64_t *coremap, unsigned npages)
{
	uint64_t cm_entry;
	bool found_pages = false;;
	for(*index_ptr = num_fixed_pages; *index_ptr < coremap_size; (*index_ptr)++) {

		cm_entry = coremap[*index_ptr];
		if(get_page_is_free(cm_entry)) {

			// If more than 1 page, make sure next chunks are free as well, else set index
			if(npages > 1) {

				// Make sure we don't move past the coremap bounds
				if(*index_ptr + npages >= coremap_size) {
					break;
				}
				uint64_t next_entry;
				for(uint32_t nextpage = 1; nextpage < npages; nextpage++) {
					next_entry = coremap[*index_ptr + nextpage];
					if(!get_page_is_free(next_entry)) {
						// Minus one due to loop increment
						*index_ptr += nextpage - 1;
						KASSERT(*index_ptr+1 < coremap_size);
						break;
					}
					if(nextpage == npages - 1) { 
						found_pages = true;
						break;
					}
				}

			}  else {
				found_pages = true;
				break;
			}
		} 

		if(found_pages) {
			break;
		}
	}
	return found_pages;
}

static
vaddr_t
alloc_pages(unsigned npages, bool is_fixed, paddr_t *ppn)
{
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	uint32_t first_index = 0;
	bool found_pages = false;

	spinlock_acquire(&coremap_lock);
	if(debug_mode && coremap_used_pages > 75) {
		kprintf("Entering alloc_kpages.\n");
		print_coremap();
	}

	found_pages = find_pages(&first_index, coremap, npages);

	if(!found_pages) {
		spinlock_release(&coremap_lock);
		return 0;
	}

	*ppn = first_index*PAGE_SIZE;

	vaddr_t virtual_address = PADDR_TO_KVADDR(*ppn);

	// Set first coremap entry. Set owner to 0 for now (revisit this later)
	uint64_t first_entry = build_page_entry(npages, 0, false, false, true, is_fixed, virtual_address);
	coremap[first_index] = first_entry;
	coremap_used_pages++;

	// Set additional coremap entries (if more than one)
	uint64_t mid_entry = build_page_entry(npages, 0, false, false, false, is_fixed, virtual_address);
	for(uint64_t entry = 1; entry < npages; entry++) {
		coremap[first_index + entry] = mid_entry;
		coremap_used_pages++;
	}

	if(debug_mode && coremap_used_pages > 75) {
		kprintf("\nLeaving alloc_kpages\n");
	}
	
	spinlock_release(&coremap_lock);

	return virtual_address;
}

vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t dummy = 0;
	vaddr_t ret = alloc_pages(npages, true, &dummy);
	(void)dummy;
	return ret;
}

paddr_t
alloc_upages(unsigned npages)
{
	paddr_t ppn = 0;
	vaddr_t ret = alloc_pages(npages, true, &ppn);
	(void)ret;
	return ppn;
}

void
free_kpages(vaddr_t addr)
{
	uint64_t *coremap = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	bool not_found = false;

	spinlock_acquire(&coremap_lock);

	if(debug_mode && coremap_used_pages > 75) {
		kprintf("Entering free_kpages.\ncoremap_used_pages: %u\n", coremap_used_pages);
	}

	for(uint32_t entry = num_fixed_pages; entry < coremap_size; entry++) {

		if(get_vaddr(coremap[entry]) == addr) {

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

	if(debug_mode && coremap_used_pages > 75) {
		kprintf("Leaving free_kpages.\ncoremap_used_pages: %u\n", coremap_used_pages);
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