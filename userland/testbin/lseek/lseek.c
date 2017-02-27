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

/*
 * lseek.c
 *
 * 	Tests whether lseek syscall works
 * 	This should run correctly when open and lseek are implemented correctly.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <test161/test161.h>

#define FILENAME_LSEEK "lseektest.dat"

int
main(int argc, char **argv)
{

	// 23 Mar 2012 : GWA : Assume argument passing is *not* supported.

	(void) argc;
	(void) argv;

	int fd;

	fd = open(FILENAME_LSEEK, O_WRONLY | O_CREAT | O_TRUNC);
	if(fd < 0) {
		err(1, "Failed to open file.\n");
	}

	// Bottom 32 bits is 2863311530 unsigned, -1431655766 signed (10101010101010101010101010101010)
	// Top 32 bits is 715827882 signed/unsigned (00101010101010101010101010101010)
	off_t res = lseek(fd, 3074457345618258602, 0);
	if(res < 0) {
		err(1, "lseek returned the error code %ld.\n", (long)res);
	}
	printf("lseek returned the new offset position %ld\n", (long)res);
	printf("test over\n");
	return 0;
}