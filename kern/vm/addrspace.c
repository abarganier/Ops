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
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	as->pt = pt_create();
	if(as->pt == NULL) {
		kfree(as);
		return NULL;
	}

	as->regions = region_list_create();
	if(as->regions == NULL) {
		kfree(as->pt);
		kfree(as);
		return NULL;
	}

	as->heap_start = 0;
	as->heap_size = 0;

	as->stack_start = 0;
	as->stack_size = 0;

	as->the_num = 10;
	return as;
}

static
int
as_copy_regions(struct addrspace *old, struct addrspace *new)
{
	if(old == NULL || new == NULL) {
		return EINVAL;
	}

	struct mem_region *current_old = old->regions->head;
	bool success = true;

	while(current_old != NULL) {
		success = add_region(new->regions, current_old->start_addr, current_old->size, 1, 1, 1);
		if(!success) {
			region_list_destroy(new->regions);
			return 1;
		}
		current_old = current_old->next;
	}
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	int err = 0;

	err = as_copy_regions(old, newas);
	if(err) {
		as_destroy(newas);
		return 1;
	}
	newas->heap_start = old->heap_start;
	newas->heap_size = old->heap_size;
	newas->stack_start = old->stack_start;
	newas->stack_size = old->stack_size;

	err = pt_copy(old, newas);
	if(err) {
		as_destroy(newas);
		return 1;
	}

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	pt_destroy(as->pt);
	region_list_destroy(as->regions);
	kfree(as);
}


void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

static
vaddr_t
get_heap_start(struct addrspace *as)
{
	vaddr_t max = 0;
	vaddr_t region_end = 0;
	struct mem_region *current = as->regions->head;
	while(current != NULL) {
		region_end = current->start_addr + current->size;
		if(region_end > max) {
			max = region_end;
		}
		current = current->next;
	}
	return max;
}

static
int32_t
as_define_heap(struct addrspace *as)
{
	int32_t err = 0;
	if(as->pt->head != NULL) {
		vaddr_t heap_start = 0;
		heap_start = get_heap_start(as);
		as->heap_start = heap_start;
		as->heap_size = 0;
	}
	return err;
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	(void)readable;
	(void)writeable;
	(void)executable;
	if(as == NULL) {
		return EINVAL;
	}

	if(memsize <= 0) {
		kprintf("WARNING: as_define_region called with memsize <= 0!\n");
		return 0;
	} 

	bool success = false;
	if(region_available(as->regions, vaddr, memsize)) {
		// PERMISSIONS LOGIC NOT IMPLEMENTED!
		success = add_region(as->regions, vaddr, memsize, readable, writeable, executable);
		if(success) {
			as_define_heap(as);
		}
	}

	return success ? 0 : MEMOVLP;
}

int
as_prepare_load(struct addrspace *as)
{
	// NO NEED TO IMPLEMENT.
	// MAKE VIRTUAL PAGES ON VM FAULTS - Carl
	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	// NO NEED TO IMPLEMENT.
	// MAKE VIRTUAL PAGES ON VM FAULTS - Carl
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;
	as->stack_start = *stackptr;
	as->stack_size = 2048 * 2048; // MAY NEED TO CHANGE
	return 0;
}

static
bool
as_in_stack(struct addrspace *as, vaddr_t vaddr)
{
	return (vaddr < as->stack_start) && (vaddr >= as->stack_start - as->stack_size);
}

static
bool
as_in_heap(struct addrspace *as, vaddr_t vaddr)
{
	return (vaddr >= as->heap_start) && (vaddr < as->heap_start + as->heap_size);
}

bool
vaddr_in_segment(struct addrspace *as, vaddr_t vaddr)
{
	return is_valid_region(as->regions, vaddr, 0) || as_in_stack(as, vaddr) || as_in_heap(as, vaddr);
}