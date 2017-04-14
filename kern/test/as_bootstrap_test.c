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
#include <addrspace.h>
#include <test.h>


/*
 *	Adds "num_regions" regions of size "rsize" starting at 0x00040000
 */
static
int 
add_regions(struct addrspace *as, size_t num_regions, size_t rsize, vaddr_t start_addr)
{
	vaddr_t curr_addr = start_addr;
	size_t i;
	int err = 0;
	for(i = 0; i < num_regions; i++) {
		err = as_define_region(as, curr_addr, rsize, 1, 1, 1);
		if(err) {
			kprintf("ERROR: as_define_region returned an error in add_regions!\n");
			return err;
		}
		curr_addr += rsize;
	}
	return 0;
}

/*
 *	Ensures mem_regions were added properly to the addrspace
 */
static
int
verify_regions(struct addrspace *as, size_t num_regions, size_t rsize, vaddr_t start_addr)
{
	struct region_list *rlist = as->regions;
	struct mem_region *current = rlist->head;

	vaddr_t curr_addr = start_addr;
	size_t count = 0;
	bool error = false;
	
	while(current != NULL) {
		count += 1;
		if(count > num_regions) {
			kprintf("ERROR: verify_regions - found more mem_regions than expected\n");
			error = true;
		}
		if(current->start_addr != curr_addr) {
			kprintf("ERROR: verify_regions - start_addr for mem_region different than expected. Found %u, Expected %u\n", current->start_addr, curr_addr);
			error = true;
		}
		if(current->size != rsize) {
			kprintf("ERROR: verify_regions - size for mem_region different than expected. Found %u, Expected %u\n", current->size, rsize);
			error = true;
		}
		if(error) {
			return 1;
		}
		current = current->next;
		curr_addr += rsize;
	}
	return 0;
}

// Define 2 memory regions taking up 1 page, expect 3 virtual pages to be allocated
static
int
as_test1(void) 
{
	struct addrspace *as = as_create();
	if(as == NULL) {
		panic("as_create() returned NULL, ENOMEM error!\n");
	}

	int err = 0;
	vaddr_t start_addr = 0x00040000;
	size_t region_size = PAGE_SIZE/2;
	size_t num_regions = 2;

	// Add two mem_regions of 2048 bytes
	add_regions(as, num_regions, region_size, start_addr);
	err = verify_regions(as, num_regions, region_size, start_addr);
	if(err) {
		kprintf("ERROR: as_test1 failed the verify_regions check\n");
		return 1;
	}

	return 0;
}

static
int
overlap_region_test(void)
{
	struct addrspace *as = as_create();
	if(as == NULL) {
		panic("as_create() returned NULL, ENOMEM error!\n");
	}

	int err = 0;
	vaddr_t start_addr = 0x00040000;
	size_t region_size = PAGE_SIZE + (PAGE_SIZE/2);
	size_t num_regions = 1;
	
	kprintf("Ready to add overlapping memory regions (this should fail!)\n");
	// Add first mem_region
	err = add_regions(as, num_regions, region_size, start_addr);
	if(err) {
		kprintf("An error occurred, but not when expected! We can't overlap regions on an empty region list\n");
	}

	err = add_regions(as, num_regions, region_size, start_addr);
	if(err) {
		kprintf("SUCCESS: An error was returned when trying to define overlapping memory regions!\n");
		return 0;
	} else {
		return 1;
	}
}

int
as_bootstrap_test(int nargs, char **args)
{
	(void)nargs;
	(void)args;

	int err = 0;
	err = as_test1();
	if(err) {
		panic("as_test1 failed!\n");
	}

	err = overlap_region_test();
	if(err) {
		panic("overlap_region_test failed!\n");
	}

	kprintf("as_bootstrap_test: SUCCESS\n");	
	kprintf("DDDDDDDDDDDDD             OOOOOOOOO     PPPPPPPPPPPPPPPPP   EEEEEEEEEEEEEEEEEEEEEE        SSSSSSSSSSSSSSS      OOOOOOOOO     NNNNNNNN        NNNNNNNN\nD::::::::::::DDD        OO:::::::::OO   P::::::::::::::::P  E::::::::::::::::::::E      SS:::::::::::::::S   OO:::::::::OO   N:::::::N       N::::::N\nD:::::::::::::::DD    OO:::::::::::::OO P::::::PPPPPP:::::P E::::::::::::::::::::E     S:::::SSSSSS::::::S OO:::::::::::::OO N::::::::N      N::::::N\nDDD:::::DDDDD:::::D  O:::::::OOO:::::::OPP:::::P     P:::::PEE::::::EEEEEEEEE::::E     S:::::S     SSSSSSSO:::::::OOO:::::::ON:::::::::N     N::::::N\n  D:::::D    D:::::D O::::::O   O::::::O  P::::P     P:::::P  E:::::E       EEEEEE     S:::::S            O::::::O   O::::::ON::::::::::N    N::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P::::P     P:::::P  E:::::E                  S:::::S            O:::::O     O:::::ON:::::::::::N   N::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P::::PPPPPP:::::P   E::::::EEEEEEEEEE         S::::SSSS         O:::::O     O:::::ON:::::::N::::N  N::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P:::::::::::::PP    E:::::::::::::::E          SS::::::SSSSS    O:::::O     O:::::ON::::::N N::::N N::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P::::PPPPPPPPP      E:::::::::::::::E            SSS::::::::SS  O:::::O     O:::::ON::::::N  N::::N:::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P::::P              E::::::EEEEEEEEEE               SSSSSS::::S O:::::O     O:::::ON::::::N   N:::::::::::N\n  D:::::D     D:::::DO:::::O     O:::::O  P::::P              E:::::E                              S:::::SO:::::O     O:::::ON::::::N    N::::::::::N\n  D:::::D    D:::::D O::::::O   O::::::O  P::::P              E:::::E       EEEEEE                 S:::::SO::::::O   O::::::ON::::::N     N:::::::::N\nDDD:::::DDDDD:::::D  O:::::::OOO:::::::OPP::::::PP          EE::::::EEEEEEEE:::::E     SSSSSSS     S:::::SO:::::::OOO:::::::ON::::::N      N::::::::N\nD:::::::::::::::DD    OO:::::::::::::OO P::::::::P          E::::::::::::::::::::E     S::::::SSSSSS:::::S OO:::::::::::::OO N::::::N       N:::::::N\nD::::::::::::DDD        OO:::::::::OO   P::::::::P          E::::::::::::::::::::E     S:::::::::::::::SS    OO:::::::::OO   N::::::N        N::::::N\nDDDDDDDDDDDDD             OOOOOOOOO     PPPPPPPPPP          EEEEEEEEEEEEEEEEEEEEEE      SSSSSSSSSSSSSSS        OOOOOOOOO     NNNNNNNN         NNNNNNN\n");

	return 0;
}


