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
#include <pagetable.h>
#include <test.h>

//#define TESTSIZE 533

int
pagetabletest(int nargs, char **args)
{
	(void)nargs;
	(void)args;
	
	vaddr_t v1 = 0x60000000;
	vaddr_t v2 = 0x50000000;
	vaddr_t v3 = 0x40000000;

	int32_t result;


	//Test pt_create functionality
	kprintf("Testing pt_create\n");
	struct pagetable *pt;
	pt = pt_create();

	if(pt == NULL){
		kprintf("PT_CREATE FAIL\n");
	}
	else{
		kprintf("PT_CREATE SUCCESS\n");
	}

	//Test pt_add functionality
	kprintf("Testing pt_add\n");
	result = pt_add(pt, v1);
	if(result){
		kprintf("pt_add failed when trying to add v1 for the first time \n");
	}
	result = pt_add(pt, v2);
	if(result){
		kprintf("pt_add failed when trying to add v2 for the first time \n");
	}

	struct pt_entry *thisPTE = pt->head;
	while(thisPTE != NULL){
		kprintf("Here's a PTE!\n");
		thisPTE = thisPTE->next_entry;
	}

	//Test pt_remove functionality
	kprintf("Testing pt_remove\n");
	result = pt_remove(pt, v1);
	if(result){
		kprintf("pt_remove failed to remove v1");
	}

	thisPTE = pt->head;
	while(thisPTE != NULL){
		kprintf("Here's a PTE!\n");
		thisPTE = thisPTE->next_entry;
	}

	result = pt_remove(pt, v1);
	if(result){
		kprintf("pt_remove failed to remove v1 a second time. Good!\n");
	}
	else{
		kprintf("pt_remove removed v1 twice???\n");
	}
	thisPTE = pt->head;
	while(thisPTE != NULL){
		kprintf("Here's a PTE!\n");
		thisPTE = thisPTE->next_entry;
	}

	result = pt_remove(pt, v3);
	if(result){
		kprintf("pt_remove failed to remove v3 because it was never in the pagetable. Good!\n");
	}

	result = pt_remove(pt, v2);
	if(result){
		kprintf("pt_remove failed to remove v2. Not good\n");
	}

	kprintf("Scan pagetable for PTEs. Should find none.\n");
	thisPTE = pt->head;
	while(thisPTE != NULL){
		kprintf("Here's a PTE!\n");
		thisPTE = thisPTE->next_entry;
	}
	kprintf("End search for PTEs.\n");


	//Test pte_create functionality
	kprintf("Testing pte_create\n");
	struct pt_entry *pte;
	pte = pte_create();
	if(pte->vpn == 0){
		kprintf("pte->vpn successfully initializes to 0\n");
	}
	else{
		kprintf("pte->vpn does not initialize to 0. Bad.\n");
	}
	if(pte->next_entry == NULL){
		kprintf("pte->next_entry successfully initializes to NULL\n");
	}
	else{
		kprintf("pte->next_entry does not initialize to NULL. Bad.\n");
	}

	//Test pt_get_pte functionality
	kprintf("Testing pt_get_pte\n");
	result = pt_add(pt, v1);
	if(result){
		kprintf("PT_ADD FAIL\n");
	}
	result = pt_add(pt, v3);
	if(result){
		kprintf("PT_ADD FAIL\n");
	}
	struct pt_entry *test_pte1 = NULL;
	test_pte1 = pt_get_pte(pt, v1);
	if(test_pte1 != NULL){
		if(test_pte1->vpn == v1 >>12){
			kprintf("PT_GET_PTE SUCCESS\n");
		}
		else{
			kprintf("PT_GET_PTE FAIL on vpn mismatch\n");
		}
	}
	else{
		kprintf("PT_GET_PTE FAIL on find\n");
	}

	struct pt_entry *test_pte2 = NULL;
	test_pte2 = pt_get_pte(pt, v2);
	if(test_pte2 != NULL){
		kprintf("PT_GET_PTE FAIL. Returned pointer to non-existant PTE\n");
	}

	
	//Test pte_destroy functionality (FAILING)
	kprintf("Testing pte_destroy\n");
	pte_destroy(pte);
	// if(pte == NULL){
	// 	kprintf("PTE was successfully destroyed\n");
	// }
	// else{
	// 	kprintf("PTE was not successfully destroyed\n");
	// }
	pte->vpn = v1 >> 12;
	if(pte->vpn == v1 >> 12){
		kprintf("PTE_DESTROY FAIL\n");
	}
	else{
		kprintf("PTE_DESTROY SUCCESS\n");
	}

	//Test pt_destroy functionality







	return 0;
}
