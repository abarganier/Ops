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

vaddr_t
get_vpn(vaddr_t vaddr) {
	vaddr_t vpn = vaddr >> 12;
	vpn = vpn << 12;
	return vpn;
}

struct pagetable *
pt_create(void)
{
	struct pagetable *pt;
	pt = kmalloc(sizeof(*pt));
	if(pt == NULL){
		return NULL;
	}

	pt->head = NULL;
	pt->tail = NULL;
	return pt;
}

static
void
pt_cleanup_entries(struct addrspace *as)
{
	struct pagetable *pt = as->pt;

	if(pt == NULL || pt->head == NULL){
		return;
	}

	struct pt_entry *current = pt->head;

	while(current != NULL){
		struct pt_entry *to_destroy = current;
		// kprintf("pt_cleanup_entries - Freeing coremap page with vpn: %x, owner: %u\n", to_destroy->vpn, as->as_pid);

		current = current->next_entry;
		pte_destroy(to_destroy, as->as_pid);
	}
}

int32_t 
pt_destroy(struct addrspace *as)
{
	pt_cleanup_entries(as);
	kfree(as->pt);
	return 0;
}

int32_t
pt_copy(struct addrspace *old, struct addrspace *newas)
{
	if(old == NULL || newas == NULL) {
		return EINVAL;
	}

	paddr_t new_ppn = 0;
	int32_t ret = 0;

	struct pt_entry *old_curr = old->pt->head;
	while(old_curr != NULL) {
		ret = pt_add(newas, old_curr->vpn, &new_ppn);
		if(ret) {
			return ret;
		}

		memcpy((void *)PADDR_TO_KVADDR(new_ppn), (void *)PADDR_TO_KVADDR(old_curr->ppn), PAGE_SIZE);

		old_curr = old_curr->next_entry;
	}

	return 0;
}

static
int32_t
pte_set_ppn(struct pt_entry *pte, struct addrspace *as)
{
	if(pte == NULL) {
		kprintf("ERROR: NULL pointer passed to pte_set_paddr\n");
		return EINVAL;
	}
	
	size_t npages = 1;
	paddr_t ppn; 


	ppn = alloc_upages(npages, pte->vpn, as->as_pid);
	if(ppn <= 0) {
		return NOPPN;
	}

	bzero((void *)PADDR_TO_KVADDR(ppn), PAGE_SIZE);

	pte->ppn = ppn;
	return 0;
}

int32_t 
pt_add(struct addrspace *as, vaddr_t vaddr, paddr_t *ppn_ret)
{

	if(as == NULL || as->pt == NULL){
		return EINVAL;
	}

	struct pagetable *pt = as->pt;
	struct pt_entry *old_pte = pt_get_pte(pt, vaddr);
	// If existing page doesn't exist, allocate it.
	// If it does exist, return vpn of existing page
	if(old_pte == NULL) {

		struct pt_entry *pte = pte_create();
		if(pte == NULL){
			return ENOMEM;
		}
		
		pte->vpn = get_vpn(vaddr);
		
		int32_t err;
		err = pte_set_ppn(pte, as);
		if(err) {
			pte_destroy(pte, as->as_pid);
			return err;
		}

		*ppn_ret = pte->ppn;

		if(pt->tail == NULL){
			KASSERT(pt->head == NULL);
			pt->head = pte;
			pt->tail = pte;
		} else {
			KASSERT(pt->head != NULL);
			pt->tail->next_entry = pte;
			pt->tail = pte;
		}

	} else {
		*ppn_ret = old_pte->ppn;
	}

	return 0;
}

int32_t 
pt_remove(struct addrspace *as, vaddr_t vaddr)
{

	if(as == NULL || as->pt == NULL){
		return EINVAL;
	}

	struct pagetable *pt = as->pt;

	if(pt->head == NULL){
		return EPTEMPTY;
	}

	KASSERT(pt->tail != NULL);
	
	bool found = false;
	struct pt_entry *pte_current = pt->head;
	struct pt_entry *pte_prev = NULL;
	vaddr_t vpn = get_vpn(vaddr);

	while(pte_current != NULL){
		//Check current pte for vpn
		if(pte_current->vpn == vpn){
			if(pte_prev != NULL){
				pte_prev->next_entry = pte_current->next_entry;				
			}

			found = true;
			break;
		}
		pte_prev = pte_current;
		pte_current = pte_current->next_entry;
	}

	if(!found){
		return EBADVPN;
	}

	if(pte_current == pt->head && pte_current == pt->tail){
		pt->head = NULL;
		pt->tail = NULL;
	}
	else if(pte_current == pt->tail){
		pt->tail = pte_prev;
	}
	else if(pte_current == pt->head){
		pt->head = pte_current->next_entry;
	}

	pte_destroy(pte_current, as->as_pid);

	return 0;
}

struct pt_entry *
pt_get_pte(struct pagetable *pt, vaddr_t vaddr)
{
	
	if(pt == NULL || pt->head == NULL) {
		return NULL;
	}

	bool found = false;
	struct pt_entry *pte_current = pt->head;
	vaddr_t vpn = get_vpn(vaddr);

	while(pte_current != NULL){
		//Check current pte for vpn
		if(pte_current->vpn == vpn){
			found = true;
			break;
		}
		pte_current = pte_current->next_entry;
	}

	if(!found){
		return NULL;
	}

	return pte_current;
}

struct pt_entry *
pte_create(void)
{
	struct pt_entry *pte;
	pte = kmalloc(sizeof(*pte));
	if(pte == NULL){
		return NULL;
	}
	pte->next_entry = NULL;
	pte->vpn = 0;
	pte->ppn = 0;

	return pte;
}

int32_t 
pte_destroy(struct pt_entry *pte, pid_t owner_pid)
{
	if(pte->ppn > 0) {
		KASSERT(pte->ppn % PAGE_SIZE == 0);	
		uint32_t cm_index = pte->ppn / PAGE_SIZE;
		free_page_at_index(cm_index, owner_pid, pte->vpn);
	} else {
		kprintf("pte_destroy: NOTE - pte_destroy called on page with no assigned ppn\n");
	}
	tlb_null_entry(pte->vpn);
	kfree(pte);
	return 0;
}

