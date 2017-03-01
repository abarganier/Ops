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
 * getcwd.c
 *
 * 	Tests whether getcwd syscall works
 * 	This should run correctly when write is implemented correctly.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <test161/test161.h>

#define FILENAME_GETCWD "getcwdtest.dat"

int
main(int argc, char **argv)
{

	// 23 Mar 2012 : GWA : Assume argument passing is *not* supported.

	(void) argc;
	(void) argv;

//	char buf[100];
// 	char buf[100] = "";
// 	int ret = __getcwd(&buf, 100);


// 	printf("getcwd returned the buf %s\n", &buf);
// //	printf("retbuf is %s\n", retbuf);
// 	printf("getcwd ret value is %d\n",ret);




	// char * directory = malloc(sizeof(char)*101);
 //    int ret = __getcwd(directory, 100);

 //    free((void*)directory);
 //    printf("getcwd returned the buf %s\n", directory);
 //    printf("getcwd ret value is %d\n",ret);



    // char directory[100];
    // int ret = __getcwd(&directory, 101);

    // printf("getcwd returned the buf %s\n", directory);
    // printf("getcwd ret value is %d\n",ret);

   char directory[100];
   int ret = __getcwd((char*)&directory, 101);

   printf("getcwd returned the buf %s\n", directory);
   printf("getcwd ret value is %d\n",ret);

	return 0;

}