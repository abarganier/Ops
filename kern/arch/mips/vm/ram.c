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
#include <lib.h>
#include <vm.h>
#include <mainbus.h>


vaddr_t firstfree;   /* first free virtual address; set by start.S */

paddr_t firstpaddr;  /* address of first free physical page */
static paddr_t lastpaddr;   /* one past end of last free physical page */
// static paddr_t kernaddr_start = 0x200;
static paddr_t kernaddr_end;

paddr_t coremap_paddr;		//Marks starting address of coremap. Should never change after first assignment.
uint32_t coremap_size;
/*
 * Called very early in system boot to figure out how much physical
 * RAM is available.
 */
void
ram_bootstrap(void)
{
	// kprintf("in ram_bootstrap\n");
	size_t ramsize;

	/* Get size of RAM. */
	ramsize = mainbus_ramsize();

	/*
	 * This is the same as the last physical address, as long as
	 * we have less than 512 megabytes of memory. If we had more,
	 * we wouldn't be able to access it all through kseg0 and
	 * everything would get a lot more complicated. This is not a
	 * case we are going to worry about.
	 */
	if (ramsize > 512*1024*1024) {
		ramsize = 512*1024*1024;
	}

	lastpaddr = ramsize;

	/*
	 * Get first free virtual address from where start.S saved it.
	 * Convert to physical address.
	 */
	firstpaddr = firstfree - MIPS_KSEG0;

	kernaddr_end = firstpaddr - 1;

	coremap_paddr = firstpaddr;
	coremap_size = ramsize / 4096;

	firstpaddr = firstpaddr + ((ramsize / 4096)*8);

	bzero((void*) PADDR_TO_KVADDR(coremap_paddr), coremap_size);
	
	/*
	 *	Add coremap entries for pages used by exception handler/kernel
	 */
	uint64_t num_kern_pages = kernaddr_end / 4096;
	if(kernaddr_end % 4096 > 0) {
		num_kern_pages += 1; 
	}

	uint64_t first_entry = build_page_entry(num_kern_pages, 0, false, false, true, true, PADDR_TO_KVADDR(0));
	uint64_t mid_entry = build_page_entry(num_kern_pages, 0, false, false, false, true, PADDR_TO_KVADDR(0));

	uint64_t *cm_addr = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr);
	cm_addr[0] = first_entry;

	for(uint64_t entry = 1; entry < num_kern_pages; entry++) {
		cm_addr[entry] = mid_entry;
	}

	/*
	 *	Add coremap entries for pages used by coremap itself
	 */
	uint64_t num_cm_pages = (coremap_size * 8) / 4096;	
	if(((coremap_size * 8) % 4096) > 0) {
		num_cm_pages += 1;
	}

	first_entry = build_page_entry(num_kern_pages, 0, false, false, true, true, PADDR_TO_KVADDR(coremap_paddr));
	mid_entry = build_page_entry(num_kern_pages, 0, false, false, false, true, PADDR_TO_KVADDR(coremap_paddr));

	cm_addr = (uint64_t *) PADDR_TO_KVADDR(coremap_paddr + ((uint32_t)num_kern_pages) * 8);
	cm_addr[0] = first_entry;

	for(uint64_t entry = 1; entry < num_cm_pages; entry++) {
		cm_addr[entry] = mid_entry;
	}

	kprintf("%uk physical memory available\n",
		(lastpaddr-firstpaddr)/1024);
}

/*
 * This function is for allocating physical memory prior to VM
 * initialization.
 *
 * The pages it hands back will not be reported to the VM system when
 * the VM system calls ram_getsize(). If it's desired to free up these
 * pages later on after bootup is complete, some mechanism for adding
 * them to the VM system's page management must be implemented.
 * Alternatively, one can do enough VM initialization early so that
 * this function is never needed.
 *
 * Note: while the error return value of 0 is a legal physical address,
 * it's not a legal *allocatable* physical address, because it's the
 * page with the exception handlers on it.
 *
 * This function should not be called once the VM system is initialized,
 * so it is not synchronized.
 */
paddr_t
ram_stealmem(unsigned long npages)
{
	size_t size;
	paddr_t paddr;

	size = npages * PAGE_SIZE;

	if (firstpaddr + size > lastpaddr) {
		return 0;
	}

	paddr = firstpaddr;
	firstpaddr += size;

	return paddr;
}

/*
 * This function is intended to be called by the VM system when it
 * initializes in order to find out what memory it has available to
 * manage. Physical memory begins at physical address 0 and ends with
 * the address returned by this function. We assume that physical
 * memory is contiguous. This is not universally true, but is true on
 * the MIPS platforms we intend to run on.
 *
 * lastpaddr is constant once set by ram_bootstrap(), so this function
 * need not be synchronized.
 *
 * It is recommended, however, that this function be used only to
 * initialize the VM system, after which the VM system should take
 * charge of knowing what memory exists.
 */
paddr_t
ram_getsize(void)
{
	return lastpaddr;
}

/*
 * This function is intended to be called by the VM system when it
 * initializes in order to find out what memory it has available to
 * manage.
 *
 * It can only be called once, and once called ram_stealmem() will
 * no longer work, as that would invalidate the result it returned
 * and lead to multiple things using the same memory.
 *
 * This function should not be called once the VM system is initialized,
 * so it is not synchronized.
 */
paddr_t
ram_getfirstfree(void)
{
	paddr_t ret;

	ret = firstpaddr;
	firstpaddr = lastpaddr = 0;
	return ret;
}
