#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <sys/resource.h>

// constants
#define MAX_ARGS 16
#define MAX_BUFFER 25
#define DELIMS " \t\r\n\a"

// forward declaration
void removePidShiftList(pid_t pid);
bool isPidInList(int pid);
int findIndexToRemove(int pid);

// globals
pid_t pid_list[1000];
int pid_list_index = -1;

void CtrlCHandler() 
{
	// ignore
	printf("\n");
	fflush(stdout);
	return;
}

void procFinishHandler(int sig)
{
	int pid;
	pid = wait(NULL);
	if (pid != -1) {
		// a process already finished
		// update for listjobs()
		int temp = findIndexToRemove(pid);
		pid_list[temp] = -1;
		return;
	}
}

int runListJobs() 
{
	if (pid_list_index == -1) {
		printf("No background processes running\n");
		return 0;
	}

	printf("List of backgrounded processes:\n");

	int i = 0;
	int status;
	int toRemove[20]; // if exit at same time

	for (int i = 0; i <= pid_list_index; i++) 
	{
		printf("Command %d with PID %d", i+1, pid_list[i]);

		// check if already finished
		if (pid_list[i] == -1) {
			printf(" Status:FINISHED\n");
			removePidShiftList(pid_list[i]);
			return 0;
		}

		pid_t ret = waitpid(pid_list[i], &status, WNOHANG);
		
		if (ret == -1) {
			printf(" Status:ERROR\n");
		} else if (ret == 0) {
			printf(" Status:RUNNING\n");
		} else {
			// remove from list
			printf(" Status:FINISHED\n");
			toRemove[i] = ret;
			i++;
		}
	}

	while (toRemove[i] != 0) 
	{
		removePidShiftList(toRemove[i]);
		i++;
	}

	return 0;
}

int findIndexToRemove(int pid)
{
	int remove = -1;
	for (int i = 0; i <= pid_list_index; i++) {
		if (pid == pid_list[i]) {
			remove = i;
		}
	}
	return remove;
}

void removePidShiftList(pid_t pid)
{
	int remove = -1;

	// find index of process to remove
	for (int i = 0; i <= pid_list_index; i++) {
		if (pid == pid_list[i]) {
			remove = i;
		}
	}
	// iterate from index and shift remaining
	for (int i = remove; i <= pid_list_index; i++) {
		pid_list[i] = pid_list[i+1];
	}

	pid_list_index--;
}

void cleanupProcesses() 
{
	int temp_pid;
	for (int i = 0; i <= pid_list_index; i++) {
		temp_pid = pid_list[i];
		printf("KILL PID: %d\n", temp_pid);
		kill(temp_pid, SIGKILL);
	}
}

void runExec(char **args, bool background) 
{
	if (args[0] == NULL) return;

	int pid;
	int pstatus, cstatus;

	pid = fork();

	switch(pid) 
	{ 	
		case -1: // error
			perror("fork() failed in runExec()\n");
			exit(-1);

		case 0: // child	
			if (execvp(args[0], args) == -1) {
				printf("Failed to execute command\n");
				exit(-1);
			}	// execvp shouldn't return unless error

		default: // parent
			if (background) {
				pid_list_index++;
				pid_list[pid_list_index] = pid; 
			} else { 
				waitpid(pid, &pstatus, 0);
			}
	}
	return;
}

// Referenced http://brennan.io/2015/01/16/write-a-shell-in-c/ 
char ** parseLine(char *line) 
{
	int buffer = MAX_BUFFER;
	char **args = malloc(buffer * sizeof(char*));
	char *arg;
	int pos = 0;

	if (!args) {
		perror("Mem alloc error - args\n");
		exit(1);
	}

	arg = strtok(line, DELIMS);
	
	while (arg != NULL) {
		args[pos] = arg;
		pos++;

		// check if need more mem
		if (pos >= buffer) {
			buffer += MAX_BUFFER;
			args = realloc(args, buffer * sizeof(char *));
			if (!args) {
				perror("Mem realloc error - args");
				exit(2);
			}
		}
		// get next
		arg = strtok(NULL, DELIMS); 
	}
	// end arr
	args[pos] = NULL; 
	return args;
}

int runShellSpecificCmd(char **args)
{
	if (strcmp(args[0], "cd") == 0) {
		if (chdir(args[1]) != 0) {
			fprintf(stderr, "Error: chdir() to path %s failed\n", args[1]);
		}
	} else {
		char cwd[1024];
		if (getcwd(cwd, sizeof(cwd)) != NULL) {
			printf("Current Directory: %s\n", cwd);
		} else {
			perror("Error: getcwd() failed");
		}
	}
	return 0;
}

void makeFg(char** args)
{
	// convert pid string to int
	int pid = (int)strtol(args[1], (char **)NULL, 10);

	if (!isPidInList(pid)) {
		fprintf(stderr, "PID is not in background\n");
		return;
	}	

	waitpid(pid, NULL, 0);
	removePidShiftList(pid);
	return;
}

bool isPidInList(int pid) {
	for (int i = 0; i <= pid_list_index; i++) {
		if (pid == (int) pid_list[i]) {
			return true;
		}
	}
	return false;
}

bool isBg(char** args)
{
	int i = 0;
	while(args[i] != NULL) {
		if (strcmp(args[i], "&") == 0) {
			args[i] = NULL;
			return true;
		}
		i++;
	}
	return false;
}

int main(int argc, char** argv)
{
	char **args;
	char *line = NULL;
	char *shellSpecificCmd[] = {"cd", "pwd"};
	pid_t pid;
	size_t buffer = 1024;
	int i = 0;

	for(;;)
	{
		printf("shell> ");

		signal(SIGCHLD, procFinishHandler);

		signal(SIGINT, CtrlCHandler);

		getline(&line, &buffer, stdin);
		args = parseLine(line);

		if (*args[0] == '\r' || *args[0] == '\n') {
			continue;
		}
		else if (strcmp(args[0], "exit") == 0 || strcmp(args[0], "quit") == 0) { 
			// remove all zombie/orphan processes
			cleanupProcesses();
			printf("Exiting Shell\n");
			exit(0);
		}
		else if (strcmp(args[0], "cd") == 0 || strcmp(args[0], "pwd") == 0) {
			runShellSpecificCmd(args);
			continue;
		}
		else if (strcmp(args[0], "listjobs") == 0) {
			runListJobs();
			continue;
		} 
		else if ((strcmp(args[0], "fg") == 0)) {
			if (args[2] != NULL) {
				printf("fg command takes one argument - pid number\n");
				continue;
			} else {
				makeFg(args);
			}	
		}
		else if (args[0] != NULL) {
			runExec(args, isBg(args));
		}

		free(line);
		free(args);
		line = NULL;
		args = NULL;
	}

	return 0;
}
