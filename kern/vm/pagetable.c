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
#include <pagetable.h>

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

int32_t 
pt_destroy(struct pagetable * pt)
{
	kfree(pt);
	return 0;
}

//Create new PTE
int32_t 
pt_add(struct pagetable *pt, vaddr_t vaddr)
{
	if(pt == NULL){			//Consider doing error checking for vaddr
		return EINVAL;
	}

	struct pt_entry *pte = pte_create();
	if(pte == NULL){
		return ENOMEM;
	}
	
	//Check for tail of linked list
	if(pt->tail == NULL){
		KASSERT(pt->head == NULL);
		pt->head = pte;
		pt->tail = pte;
	}
	else{
		KASSERT(pt->head != NULL);
		pt->tail->next_entry = pte;
		pt->tail = pte;
	}
	pte->vpn = (uint32_t)vaddr >> 12;
	return 0;
}

int32_t 
pt_remove(struct pagetable *pt, vaddr_t vaddr)
{
	if(pt == NULL){			//Consider doing error checking for vaddr
		return EINVAL;
	}

	if(pt->head == NULL){
		return EPTEMPTY;
	}
	KASSERT(pt->tail != NULL);
	
	bool found = false;
	struct pt_entry *pte_current = pt->head;
	struct pt_entry *pte_prev = NULL;
	uint32_t vpn = vaddr >> 12;

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
	uint32_t vpn = vaddr >> 12;

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

