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
#include <addrspace.h>
#include <vm.h>
#include <proc.h>

/*
 * This file contains the implementation of methods supporting the 
 * region_list struct and the mem_region struct (a dependency of 
 * region_list).
 */
static bool debug_regions = false;

/*
 *	region_list methods
 */ 
struct region_list *
region_list_create(void)
{
	struct region_list *new_list;
	new_list = kmalloc(sizeof(*new_list));
	if(new_list == NULL) {
		return NULL;
	}
	new_list->head = NULL;
	new_list->tail = NULL;
	return new_list;
}

int 
region_copy(struct mem_region *old, struct mem_region **new_ptr)
{
	struct mem_region *new_region = mem_region_create();
	if(new_region == NULL) {
		return ENOMEM;
	}

	new_region->start_addr = old->start_addr;
	new_region->size = old->size;
	*new_ptr = new_region;
	return 0;
}

static
void
clear_region_entries(struct region_list *list)
{
	if(list == NULL || list->head == NULL){
		return;
	}

	struct mem_region *current = list->head;

	while(current != NULL){
		struct mem_region *to_destroy = current;
		current = current->next;
		mem_region_destroy(to_destroy);
	}
}

void 
region_list_destroy(struct region_list *list)
{
	if(list == NULL) {
		panic("Tried to free a NULL region_list!\n");
	}

	if(list->head == NULL) {
		KASSERT(list->tail == NULL);
		kfree(list);
		return;
	} 
	
	clear_region_entries(list);
	kfree(list);
}

bool 
add_region(struct region_list *list, vaddr_t vaddr, size_t size, int readable, int writeable, int executable)
{
	(void)readable;
	(void)writeable;
	(void)executable;
	if(list == NULL) {
		panic("Tried to add an mem_region to a NULL region_list!\n");
		return false;
	}

	struct mem_region *new_region = mem_region_create();
	if(new_region == NULL) {
		return false;
	}

	new_region->next = NULL;
	new_region->start_addr = vaddr;
	new_region->size = size;

	// new_region->readable = readable > 0 ? true : false;
	// new_region->writeable = writeable > 0 ? true : false;
	// new_region->readable = executable > 0 ? true : false;

	if(list->head == NULL) {
		KASSERT(list->tail == NULL);
		list->head = new_region;
		list->tail = new_region;
	} else {
		list->tail->next = new_region;
		list->tail = new_region;
	}

	return true;
}	

static
bool
falls_in_region(struct mem_region *region, vaddr_t vaddr)
{
	return (vaddr >= region->start_addr) && (vaddr < region->start_addr + region->size);
}

// static
// bool
// valid_perms(struct mem_region *region, int permissions)
// {
// 	if(permissions == VM_FAULT_READONLY) {
// 		kprintf("WARNING: valid_perms called with a VM_FAULT_READONLY!\n");
// 	}

// 	if(permissions == VM_FAULT_READ) {
// 		return region->readable;
// 	}

// 	if(permissions == VM_FAULT_WRITE) {
// 		return region->writeable;
// 	}

// 	kprintf("WARNING: valid_perms called with unknown permissions code: %d\n", permissions);

// 	return false;
// }

static
bool
no_region_overlap(struct mem_region *region, vaddr_t vaddr, size_t size)
{
	if(debug_regions) {
		kprintf("--- In no_region_overlap ---\n");
		kprintf("Condition is...\n");
		kprintf("vaddr + size <= region->start_addr || vaddr >= region->start_addr + region->size\n");
		kprintf("%x <= %x || %x >= %x\n", vaddr + size, region->start_addr, vaddr, region->start_addr + region->size);
		kprintf("region->start_addr = %u\n", region->start_addr);
		kprintf("region->start_addr + region->size = %u\n", region->start_addr + region->size);
		kprintf("vaddr + size = %u\n", vaddr + size);
		kprintf("vaddr = %u\n", vaddr);
	}

	return vaddr + size <= region->start_addr || vaddr >= region->start_addr + region->size;
}


bool
is_valid_region(struct region_list *list, vaddr_t vaddr, int permissions)
{
	(void)permissions;
	if(list == NULL || list->head == NULL) {
		return false;
	}

	struct mem_region *current = list->head;
	bool found_valid_region = false;
	
	while(current != NULL) {
		if(falls_in_region(current, vaddr) /*&& valid_perms(current, permissions)*/) {
			found_valid_region = true;
			break;
		}
		current = current->next;
	}
	
	return found_valid_region;
}

bool
region_available(struct region_list *list, vaddr_t vaddr, size_t size)
{
	if(list == NULL) {
		return EINVAL;
	}

	bool valid_region = true;

	struct mem_region *current = list->head;
	while(current != NULL) {
		if(!no_region_overlap(current, vaddr, size)) {

			if(debug_regions) {
				kprintf("overlaps_region returned true for vaddr = %u, size = %u\n", vaddr, size);
			}

			valid_region = false;
			break;
		}
		current = current->next;
	}

	if(debug_regions) {
		kprintf("region_available return value: %s\n", valid_region ? "true" : "false");
	}

	return valid_region;
}

/*
 *	mem_region methods;
 */

struct mem_region *
mem_region_create(void)
{
	struct mem_region *new_region;
	new_region = kmalloc(sizeof(*new_region));
	if(new_region == NULL) {
		return NULL;
	}

	new_region->next = NULL;
	new_region->start_addr = 0;
	new_region->size = 0;
	// new_region->writeable = false;
	// new_region->readable = false;
	// new_region->executable = false;

	return new_region;
}

void 
mem_region_destroy(struct mem_region *region)
{
	if(region == NULL) {
		panic("Tried to free a NULL mem_region!\n");
	}
	kfree(region);
}
