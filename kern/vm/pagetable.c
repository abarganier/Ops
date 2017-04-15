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

static
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
pt_cleanup_entries(struct pagetable *pt)
{
	if(pt == NULL || pt->head == NULL){
		return;
	}

	struct pt_entry *current = pt->head;

	while(current != NULL){
		struct pt_entry *to_destroy = current;
		current = current->next_entry;
		pte_destroy(to_destroy);
	}
}

int32_t 
pt_destroy(struct pagetable *pt)
{
	pt_cleanup_entries(pt);
	kfree(pt);
	return 0;
}

static
int32_t
pte_set_paddr(struct pt_entry *pte)
{
	if(pte == NULL) {
		kprintf("ERROR: NULL pointer passed to pte_set_paddr\n");
		return EINVAL;
	}

	

	return 0;
}

int32_t 
pt_add(struct pagetable *pt, vaddr_t vaddr, paddr_t *ppn_ret)
{

	if(pt == NULL){
		return EINVAL;
	}

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
		err = pte_set_paddr(pte);
		if(err) {
			pte_destroy(pte);
			return NOPPN;
		}

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
pt_remove(struct pagetable *pt, vaddr_t vaddr)
{
	if(pt == NULL){
		return EINVAL;
	}

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

	pte_destroy(pte_current);

	return 0;
}

struct pt_entry *
pt_get_pte(struct pagetable *pt, vaddr_t vaddr)
{
	
	if(pt == NULL || pt->head == NULL){			//Consider doing error checking for vaddr
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

	return pte;
}

int32_t 
pte_destroy(struct pt_entry *pte)
{
	kfree(pte);
	return 0;
}

