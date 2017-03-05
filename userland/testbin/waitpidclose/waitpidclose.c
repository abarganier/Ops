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
 * waitpidclose.c
 *
 * 	Tests whether the waitpid and close syscalls work properly together.
 *	Print statements will be made that should indicate the expected order
 *	of events. This is to make sure the process calling waitpid(pid) is
 *	properly blocked by the child process.
 *
 * This should run correctly when the fork, waitpid, and exit syscalls 
 * are implemented.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <err.h>
#include <test161/test161.h>

int
main(int argc, char **argv)
{

	// Assume argument passing is *not* supported.
	(void) argc;
	(void) argv;

	int pid;
	int proc_status;
	int res;
	int i;
	int dummy = 0;


	printf("Calling fork()\n");

	pid = fork();
	if(pid == 0) {
		printf("I am the child.\n");
		printf("Child now performing some operations to kill time.\n");
		for(i = 0; i < 100001; i++) {
			if(i % 2 == 0) {
				dummy += i;
			} else {
				dummy -= i;
			}
		}
		printf("Child operations over.\n");
		printf("Child now calling exit().\n");
		_exit(0); // Signal success;

		
	} else if(pid > 0) {
		printf("I am the parent. Child's PID is: %d\n", pid);
		printf("Parent is calling waitpid(%d)\n", pid);
		res = waitpid(pid, &proc_status, 0);
		if(res < 0) {
			err(-1, "waitpid() failed, error returned: %d\n", res);	
		} else {
			printf("waitpid returned, return value: %d\n", res);
			printf("You should not see this unless the child has exited!\n");
			printf("Status value returned is %d, expected %d\n", proc_status, 0);
		}
	} else {
		err(-1, "fork() failed, error returned: %d\n", pid);
	}
	
	return 0;
}