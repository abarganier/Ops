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


/*
Current structure of page-entry:

[Chunksize, owner(PID), free_bit, clean_bit, is_first_chunk_bit, is_last_chunk_bit, owner VADDR]

*/

/*Takes 64-bit page entry and returns chunk size*/
uint64_t
get_chunk_size(uint64_t page_entry)
{
	page_entry >>= CHUNK_SIZE_RIGHTBOUND -1; //rightshifts by num bits to the right of chunk_size
	return page_entry;
}

/*Sets chunk_size onto existing page_entry*/
uint64_t
set_chunk_size(uint64_t chunk_size, uint64_t page_entry)
{
	chunk_size <<= (CHUNK_SIZE_RIGHTBOUND-1);
	page_entry <<= (CHUNK_SIZE_LEFTBOUND - CHUNK_SIZE_RIGHTBOUND +1);
	page_entry >>= (CHUNK_SIZE_LEFTBOUND - CHUNK_SIZE_RIGHTBOUND +1);
	page_entry |= chunk_size;
	return page_entry;
}

/*Takes TYPE_SIZE-bit page entry and returns owner (PID)*/
uint64_t
get_owner(uint64_t page_entry){
	//Leftshift to get rid of all bits to the left of owner
	page_entry <<= TYPE_SIZE - OWNER_LEFTBOUND;
	//Rightshift to place owner bits in rightmost bit positions
	page_entry >>= TYPE_SIZE - (OWNER_LEFTBOUND - OWNER_RIGHTBOUND +1);
	return page_entry;
}

/*Sets owner onto existing page_entry*/
uint64_t
set_owner(uint64_t owner, uint64_t page_entry)
{
	//Leftshift owner to correct bit positions
	owner <<= OWNER_RIGHTBOUND-1;
	
	//Make copy of bits left of owner
	uint64_t left_bits = page_entry;
	left_bits >>= OWNER_LEFTBOUND;
	left_bits <<= OWNER_LEFTBOUND;

	//Remove original owner bits and all bits left of it from page_entry
	page_entry <<= TYPE_SIZE - (OWNER_RIGHTBOUND - 1);
	page_entry >>= TYPE_SIZE - (OWNER_RIGHTBOUND - 1);

	//OR left_bits back into page_entry
	page_entry |= left_bits;

	//OR new owner into page_entry
	page_entry |= owner;

	return page_entry;
}

/*Takes 64-bit page entry and returns free_bit*/
bool
get_page_is_free(uint64_t page_entry)
{
	//Leftshift to get rid of all bits to the left of free_bit
	page_entry <<= TYPE_SIZE - FREE_BIT_POS;
	
	//Rightshift to place free_bit at right-most bit position
	page_entry >>= TYPE_SIZE -1;
	
	//Check value of bit
	if((uint64_t)page_entry == 1){
		return true;
	}
	return false;
}

/*Sets free_bit onto existing page_entry*/
uint64_t
set_page_is_free(bool page_is_free, uint64_t page_entry)
{
	//Make copy of bits left of free_bit
	uint64_t left_bits = page_entry;
	left_bits >>= FREE_BIT_POS;
	left_bits <<= FREE_BIT_POS;

	//Remove original free_bit and all bits to its left
	page_entry <<= TYPE_SIZE - (FREE_BIT_POS - 1);
	page_entry >>= TYPE_SIZE - (FREE_BIT_POS - 1);

	//OR in left_bits
	page_entry |= left_bits;

	//If page is free, shift a 1 into the FREE_BIT_POS and OR into page_entry
	if(page_is_free){
		uint64_t free_bit = 1;
		free_bit <<= FREE_BIT_POS-1;
		page_entry |= free_bit;
	}
	
	return page_entry;
}

/*Takes 64-bit page entry and returns clean_bit*/
bool
get_page_is_clean(uint64_t page_entry)
{
	//Leftshift to get rid of all bits to the left of clean_bit
	page_entry <<= TYPE_SIZE - CLEAN_BIT_POS;
	
	//Rightshift to place clean_bit at right-most bit position
	page_entry >>= TYPE_SIZE - 1;
	
	//Check value of bit
	if((uint64_t)page_entry == 1){
		return true;
	}
	return false;
}

/*Sets clean_bit onto existing page_entry*/
uint64_t
set_page_is_clean(bool page_is_clean, uint64_t page_entry)
{
	//Make copy of bits left of clean_bit
	uint64_t left_bits = page_entry;
	left_bits >>= CLEAN_BIT_POS;
	left_bits <<= CLEAN_BIT_POS;

	//Remove original clean_bit and all bits to its left
	page_entry <<= TYPE_SIZE - (CLEAN_BIT_POS - 1);
	page_entry >>= TYPE_SIZE - (CLEAN_BIT_POS - 1);

	//OR in left_bits
	page_entry |= left_bits;

	//If page is clean, shift a 1 into the CLEAN_BIT_POS and OR into page_entry
	if(page_is_clean){
		uint64_t clean_bit = 1;
		clean_bit <<= CLEAN_BIT_POS-1;
		page_entry |= clean_bit;
	}
	
	return page_entry;
}


/*Takes 64-bit page entry and returns is_first_chunk_bit*/
bool
get_is_first_chunk(uint64_t page_entry)
{
	//Leftshift to get rid of all bits to the left of is_first_chunk_bit
	page_entry <<= TYPE_SIZE - IS_FIRST_CHUNK_BIT_POS;
	
	//Rightshift to place is_first_chunk_bit at right-most bit position
	page_entry >>= TYPE_SIZE - 1;
	
	//Check value of bit
	if((uint64_t)page_entry == 1){
		return true;
	}
	return false;
}

/*Sets is_first_chunk_bit onto existing page_entry*/
uint64_t
set_page_is_clean(bool is_first_chunk, uint64_t page_entry)
{
	//Make copy of bits left of is_first_chunk_bit
	uint64_t left_bits = page_entry;
	left_bits >>= IS_FIRST_CHUNK_BIT_POS;
	left_bits <<= IS_FIRST_CHUNK_BIT_POS;

	//Remove original is_first_chunk_bit and all bits to its left
	page_entry <<= TYPE_SIZE - (IS_FIRST_CHUNK_BIT_POS - 1);
	page_entry >>= TYPE_SIZE - (IS_FIRST_CHUNK_BIT_POS - 1);

	//OR in left_bits
	page_entry |= left_bits;

	//If page is first chunk, shift a 1 into the IS_FIRST_CHUNK_BIT_POS and OR into page_entry
	if(is_first_chunk){
		uint64_t is_first_chunk_bit = 1;
		is_first_chunk_bit <<= IS_FIRST_CHUNK_BIT_POS-1;
		page_entry |= is_first_chunk_bit;
	}
	
	return page_entry;
}

/*Takes 64-bit page entry and returns is_last_chunk_bit*/
bool
get_is_last_chunk(uint64_t page_entry)
{
	//Leftshift to get rid of all bits to the left of is_last_chunk_bit
	page_entry <<= TYPE_SIZE - IS_LAST_CHUNK_BIT_POS;
	
	//Rightshift to place is_last_chunk_bit at right-most bit position
	page_entry >>= TYPE_SIZE - 1;
	
	//Check value of bit
	if((uint64_t)page_entry == 1){
		return true;
	}
	return false;
}

/*Sets is_last_chunk_bit onto existing page_entry*/
uint64_t
set_page_is_clean(bool is_last_chunk, uint64_t page_entry)
{
	//Make copy of bits left of is_last_chunk_bit
	uint64_t left_bits = page_entry;
	left_bits >>= IS_LAST_CHUNK_BIT_POS;
	left_bits <<= IS_LAST_CHUNK_BIT_POS;

	//Remove original is_last_chunk_bit and all bits to its left
	page_entry <<= TYPE_SIZE - (IS_LAST_CHUNK_BIT_POS - 1);
	page_entry >>= TYPE_SIZE - (IS_LAST_CHUNK_BIT_POS - 1);

	//OR in left_bits
	page_entry |= left_bits;

	//If page is first chunk, shift a 1 into the IS_LAST_CHUNK_BIT_POS and OR into page_entry
	if(is_last_chunk){
		uint64_t is_last_chunk_bit = 1;
		is_last_chunk_bit <<= IS_LAST_CHUNK_BIT_POS-1;
		page_entry |= is_last_chunk_bit;
	}
	
	return page_entry;
}


/*Takes TYPE_SIZE-bit page entry and returns index of next_chunk*/
uint64_t
get_vaddr(uint64_t page_entry)
{
	//Leftshift to get rid of all bits to the left of vaddr
	page_entry <<= TYPE_SIZE - VADDR_LEFTBOUND;
	//Rightshift to place vaddr bits in rightmost bit positions
	page_entry >>= TYPE_SIZE - (VADDR_LEFTBOUND - VADDR_RIGHTBOUND +1);

	return page_entry;
}

/*Sets next_chunk index onto existing page_entry*/
uint64_t
set_vaddr(uint64_t vaddr, uint64_t page_entry)
{	
	//Leftshift vaddr to correct bit positions
	vaddr <<= VADDR_RIGHTBOUND-1;
	
	//Make copy of bits left of vaddr
	uint64_t left_bits = page_entry;
	left_bits >>= VADDR_LEFTBOUND;
	left_bits <<= VADDR_LEFTBOUND;

	//Remove original vaddr bits and all bits left of it from page_entry
	page_entry <<= TYPE_SIZE - (VADDR_RIGHTBOUND - 1);
	page_entry >>= TYPE_SIZE - (VADDR_RIGHTBOUND - 1);

	//OR left_bits back into page_entry
	page_entry |= left_bits;

	//OR new vaddr into page_entry
	page_entry |= vaddr;

	return page_entry;

}

/*One-run build of page_entry*/
uint64_t
build_page_entry(uint64_t chunk_size, uint64_t owner, bool is_free, bool is_clean, bool is_first_chunk, bool is_last_chunk, uint64_t vaddr)
{
	uint64_t page_entry = 0;

	//Move chunk_size and owner parameter values into correct bit positions
	chunk_size <<= CHUNK_SIZE_RIGHTBOUND-1;
	owner <<= OWNER_RIGHTBOUND-1;

	//OR in chunk_size and owner
	page_entry |= chunk_size;
	page_entry |= owner;

	//If page is free, shift a 1 into the FREE_BIT_POS and OR into page_entry
	if(is_free){
		uint64_t free_bit = 1;
		free_bit <<= FREE_BIT_POS-1;
		page_entry |= free_bit;
	}

	//If page is clean, shift a 1 into the CLEAN_BIT_POS and OR into page_entry
	if(is_clean){
		uint64_t clean_bit = 1;
		clean_bit <<= CLEAN_BIT_POS-1;
		page_entry |= clean_bit;
	}

	//If page is first chunk, shift a 1 into the IS_FIRST_CHUNK_BIT_POS and OR into page_entry
	if(is_first_chunk){
		uint64_t is_first_chunk_bit = 1;
		is_first_chunk_bit <<= IS_FIRST_CHUNK_BIT_POS-1;
		page_entry |= is_first_chunk_bit;
	}

	//If page is last chunk, shift a 1 into the IS_LAST_CHUNK_BIT_POS and OR into page_entry
	if(is_last_chunk){
		uint64_t is_last_chunk_bit = 1;
		is_last_chunk_bit <<= IS_LAST_CHUNK_BIT_POS-1;
		page_entry |= is_last_chunk_bit;
	}

	page_entry |= vaddr;

	return page_entry;
}



