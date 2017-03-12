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
  	
  	char * parmList[] = {"/bin/ls", "-l", "-s", "-h", "i", "j", "thisshitiscrazyannoying",  NULL};
  	execv("/bin/ls", parmList);
    
  	// int proc_status = 0;
    // int res = 0;
    // pid_t i = fork();
    // if (i == 0)
    // {
    //     execv("/bin/ls", (char * const *)parmList);
    //     _exit(1);
    // }
    // else if (i > 0)
    // {
    //     res = waitpid(i, &proc_status, 0);
    //     if(res) {
    //     	printf("waitpid failed\n");
    //     }
    // }
    // else
    // {
    // 	printf("Fork failed\n");
    // }
    return 0;
}