#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <wait.h>
#include <fcntl.h>
#include <errno.h>

int terminal_handler(void);
int foreground_child_handler(void);
int background_child_handler(void);

int command_in_the_background(char **);
int single_piping(int, char ** , int);
int execute_command(int, char **);
int output_redirection(int, char **, int);

int get_index_of_pipe(int, char **);
int get_index_of_redirection(int, char ** arglist);



int prepare(){
	terminal_handler();
	return 0;
}


int process_arglist(int count, char ** arglist) {
	int pipe_index;
	int redirection_index;
	int return_value = 1;

	if (strcmp(arglist[count - 1], "&") == 0){ //In case of Executing commands in the background
		arglist[count - 1] = NULL; //We don't want to include the '&' on the execvp
		return_value = command_in_the_background(arglist);
	}
	else {	
		pipe_index = get_index_of_pipe(count, arglist);
		if (pipe_index != -1) { //In case of single piping
			return_value = single_piping(count, arglist, pipe_index);
		}
		else {
			redirection_index = get_index_of_redirection(count, arglist);
			if (redirection_index != -1) { //In case of single piping
				return_value = output_redirection(count, arglist, redirection_index);
			}
			else {
				return_value = execute_command(count, arglist);
			}
		}
	}
	return return_value;
}


int finalize(){
	return 0;
}
//*****************************
//Signals
int terminal_handler() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR || signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		perror("signal");
		exit(1);
	}
	return 1;
}

int foreground_child_handler() {
	if (signal(SIGINT, SIG_DFL) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
		perror("signal");
		exit(1);
	}
	return 1;
}

int background_child_handler() {
	if (signal(SIGINT, SIG_IGN) == SIG_ERR || signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
		perror("signal");
		exit(1);
	}
	return 1;
}



//Executing commands in the background.
int command_in_the_background(char ** arglist){
	int status_code;

	pid_t pid = fork();
	if (pid == -1) { 
		perror("fork");
		return 0;
	}
	if (pid == 0) {	
		background_child_handler();
		status_code = execvp(arglist[0], arglist);
		if(status_code == -1) { 
			perror("exec");
			return 0;
		}
		exit(1);
	}
	return 1;
}

//Single piping.
int get_index_of_pipe(int count, char ** arglist) {
	for (int i = 0; i < count; i++) {
		if(strcmp(arglist[i], "|") == 0) {
			return i;
		}
	}
	return -1;
}
	

int single_piping(int count, char ** arglist, int pipe_index) {
	
	int pipefd[2]; 
	int status_code;
	pid_t first_pid;
	pid_t second_pid;
	char ** first_command;
	char ** second_command;


	arglist[pipe_index] = NULL; // remove the |
	first_command = arglist;
	second_command = arglist + pipe_index + 1;

	status_code = pipe(pipefd);
	if (status_code == -1) {
		perror("exec");
		return 0;
	}
	first_pid = fork();
	if (first_pid == -1) { 
		perror("fork");
		return 0;
	}
	if (first_pid == 0) {
		foreground_child_handler();
		close(pipefd[0]);
		if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
			perror("dup2");
			return 0;
		}
		status_code = execvp(first_command[0], first_command);
		if (status_code == -1) {
			perror("exec");
			return 0;
		}
	}
	if (first_pid > 0) {
		second_pid = fork();
		if (second_pid == -1) {
			perror("fork");
			return 0;
		}
		if (second_pid == 0) {
			foreground_child_handler();
			close(pipefd[1]);
			if (dup2(pipefd[0], STDIN_FILENO) == -1) {
				perror("dup2");
				return 0;
			}
			status_code = execvp(second_command[0], second_command);
			if (status_code == -1) {
				perror("exec");
				return 0;
			}
		}
		if (second_pid > 0) {
			if (waitpid(first_pid, NULL, 0) == -1 && errno != EINTR && errno != ECHILD) {
				perror("waitpid");
				return 0;
			}
			close(pipefd[1]);
			if (waitpid(second_pid, NULL, 0) == -1 && errno != EINTR && errno != ECHILD) {
				perror("waitpid");
				return 0;
			}
			close(pipefd[0]);
		}
	}
	return 1;	
}

//Outout redirection
int get_index_of_redirection(int count, char ** arglist) {
	for (int i = 0; i < count; i++) {
		if(strcmp(arglist[i], ">>") == 0) {
			return i;
		}
	}
	return -1;
}

int output_redirection(int count, char ** arglist, int redirection_index) {
	int status_code, fd;
	pid_t pid;

	arglist[redirection_index] = NULL;
	fd = open(arglist[redirection_index + 1], O_WRONLY | O_APPEND | O_CREAT, 0644);
	if (fd == -1) {
		perror("open");
		return 0;
	}
	pid = fork();
	if (pid == -1) {
		perror("fork");
		return 0;
	}
	if (pid == 0) {
		foreground_child_handler();
		if (dup2(fd, STDOUT_FILENO) == -1) {
			perror("dup2");
			return 0;
		}
		status_code = execvp(arglist[0], arglist);
		if (status_code == -1) {
			perror("exec");
			return 0;
		}
	}
	if (waitpid(pid, NULL, 0) == -1 && errno != EINTR && errno != ECHILD) {
			perror("waitpid");
			return 0;
	}
	return 1;
}


//Executing commands
int execute_command(int count, char ** arglist) {
	char * command = arglist[0];
	pid_t pid = fork();
	if (pid == -1) {
		perror("fork");
		return 0;
	}
	if (pid == 0) {
		foreground_child_handler();
		if (-1 == execvp(command, arglist)) {
			perror("exec");
			return 0;
		}
	}
	if (pid > 0) {
		if (waitpid(pid, NULL, 0) == -1 && errno != EINTR && errno != ECHILD) {
			perror("waitpid");
			return 0;
		}
	}
	return 1;
}
//******************************
