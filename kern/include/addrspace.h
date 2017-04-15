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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include "opt-dumbvm.h"

struct vnode;

/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */

struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t as_npages2;
        paddr_t as_stackpbase;
#else
        struct pagetable *pt;
        struct region_list *regions;
        vaddr_t heap_start;
        size_t heap_size;
        vaddr_t stack_start;
        size_t stack_size;
#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);
bool              vaddr_in_segment(struct addrspace *, vaddr_t);

/*
 *  Supporting structure for addrspace struct. Essentially a LinkedList to
 *  keep track of memory regions defined in as_define_region.
 */
struct region_list {
  struct mem_region *head;
  struct mem_region *tail;
};

struct region_list *region_list_create(void);
void region_list_destroy(struct region_list *);
bool add_region(struct region_list *, vaddr_t, size_t, int, int, int);
bool is_valid_region(struct region_list *, vaddr_t, int);
bool region_available(struct region_list *, vaddr_t, size_t);

/*
 *  Node struct for the region_list 
 */
struct mem_region {
  struct mem_region *next;
  vaddr_t start_addr;
  size_t size;
  // bool writeable;
  // bool readable;
  // bool executable;
}; 

struct mem_region *mem_region_create(void);
void mem_region_destroy(struct mem_region *);
int region_copy(struct mem_region *, struct mem_region **);

/*
 *  Supporting virtual memory structure for addrspace struct. Essentially a LinkedList to
 *  keep track of virtual pages allocated.
 */ 
struct pagetable 
{
  struct pt_entry *head;
  struct pt_entry *tail;
};

struct pagetable *pt_create(void);
int32_t pt_destroy(struct pagetable *);
int32_t pt_copy(struct addrspace *, struct addrspace *);
int32_t pt_add(struct pagetable *, vaddr_t, paddr_t *);
int32_t pt_create_region(struct addrspace *, struct mem_region *);
int32_t pt_remove(struct pagetable *, vaddr_t);
struct pt_entry *pt_get_pte(struct pagetable *, vaddr_t);

/*
 *  Node struct for the pagetable
 */
struct pt_entry
{
  struct pt_entry *next_entry;
  vaddr_t vpn;
  paddr_t ppn;
};

struct pt_entry *pte_create(void);
int32_t pte_destroy(struct pt_entry *);
vaddr_t get_vpn(vaddr_t);

/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);


#endif /* _ADDRSPACE_H_ */
