/***********************************************************
 * Author:          Kelsey Helms
 * Date Created:    February 25, 2017
 * Filename:        smallsh.c
 *
 * Overview:
 * This program implements a small shell that with exit,
 * cd, and status commands built in.
 ************************************************************/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <fcntl.h>
#include <signal.h>

#define CHILD 0
#define TRUE 1
#define FALSE 0
#define MAX_PROCS 50
#define MAX_LENGTH 2048


/* ************************************************************************
	                    Global Variables
 ************************************************************************ */

pid_t forePID = -1;    //keeps track of PID in the foreground
struct backProcess *backProcs[MAX_PROCS];    //array of structs of background PIDs
int exitStatus = 0;    //keeps track of exit status of most recently terminated process
int backgroundDisabled = FALSE;    //keeps track of if background is disabled.

struct command {    //keeps track of commands
    char **args;    //array of commands entered
    char *inputFile;    //input file name
    char *outputFile;    //output file name
    int background;    //background process indicator
};

struct backProcess {    //keeps track of PIDs in background
    pid_t backPID;    //background PID
    int active;    //keeps tracks of whether or not the process is running
};


/* ************************************************************************
	                    Function Prototypes
 ************************************************************************ */

void initializeShell();    //initializes shell with signal handlers
void runShell();    //runs the shell
void getCommand(char *command, struct command *curCommand); //parses the user input command
void exitShell();    //exits the shell
void changeDir(char *path);    //changes the current directory
void printStatus();    //prints the exit status of the most recently terminated process
int runCommand(struct command *curCommand);    //runs the user command
void interruptSignal(int sigNum);    //catches SIGINT signals sent to foreground processes
void childTerminates(int sigNum);    //catches SIGCHLD signals sent by background processes
void disableBackground(int sigNum);   //catches SIGTSTP signals to prevent background processes
void saveProcess(int spawnpid);    //saves information about a background process


/* ************************************************************************
	                         Functions
 ************************************************************************ */

/***********************************************************
 * main: calls functions to run shell.
 *
 * parameters: none.
 * returns: none.
 ***********************************************************/

int main() {
    initializeShell();    //initialize shell
    runShell();    //run shell
    return 0;
}


/***********************************************************
 * initializeShell: initializes the shell.
 *
 * parameters: none.
 * returns: none.
 ***********************************************************/

void initializeShell() {
    struct sigaction sigint_action;    //SIGINT struct
    sigint_action.sa_handler = interruptSignal;    //SIGINT handler function
    sigint_action.sa_flags = SA_RESTART;    //make sure call can restart
    sigfillset(&(sigint_action.sa_mask));    //block other signals
    sigaction(SIGINT, &sigint_action, NULL);    //identify SIGINT as signal

    struct sigaction sigchld_action;    //SIGCHLD struct
    sigchld_action.sa_handler = childTerminates;    //SIGCHLD handler function
    sigchld_action.sa_flags = SA_RESTART;    //make sure call can restart
    sigfillset(&(sigchld_action.sa_mask));    //block other signals
    sigaction(SIGCHLD, &sigchld_action, NULL);    //identify SIGCHLD as signal

    struct sigaction sigtstp_action;    //SIGTSTP struct
    sigtstp_action.sa_handler = disableBackground;    //SIGTSTP handler function
    sigtstp_action.sa_flags = SA_RESTART;    //make sure call can restart
    sigfillset(&(sigtstp_action.sa_mask));    //block other signals
    sigaction(SIGTSTP, &sigtstp_action, NULL);    //identify SIGTSTP as signal
}


/***********************************************************
 * runShell: runs the shell and acts as manager of shell
 * operations.
 *
 * parameters: none.
 * returns: none.
 ***********************************************************/

void runShell() {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {
        backProcs[i] = NULL;    //make sure array of baground PIDs is not full of garbage
    }

    while (1) {    //run always until exited manually through user command
        struct command *curCommand = malloc(sizeof(struct command));
        assert(curCommand != NULL);    //make sure struct exists
        curCommand->args = NULL;    //reset argument array
        curCommand->inputFile = NULL;    //reset input file
        curCommand->outputFile = NULL;    //reset output file
        curCommand->background = FALSE;    //reset background process indicator

        char buffer[MAX_LENGTH];    //char array to get command

        fprintf(stdout, ": ");    //print command prompt
        fflush(stdout);    //flush output

        memset(buffer, '\0', sizeof(buffer));    //make sure buffer isn't full of garbage before using for fgets
        fgets(buffer, sizeof(buffer), stdin);    //get user command

        curCommand->args = malloc(strlen(buffer) * sizeof(char *));    //allocate enough memory for arguments
        assert(curCommand->args != NULL);    //make sure array exists
        for (i = 0; i < strlen(buffer); i++) {
            curCommand->args[i] = NULL;    //make sure array isn't full of garbage before using to store commands
        }
        getCommand(buffer, curCommand);    //get information from command

        if (curCommand->args[0] == '\0') {    //if command was empty, restart loop
            continue;
        } else if (strcmp("exit", curCommand->args[0]) == 0) {    //if exit command, free argument and exit shell
            free(curCommand->args);
            curCommand->args = NULL;
            exitShell();    //call to exit shell
        } else if (strcmp("cd", curCommand->args[0]) == 0) {    //if cd command, change to indicated directory
            changeDir(curCommand->args[1]);
        } else if (strcmp("status", curCommand->args[0]) == 0) {    //if status command, call print function
            printStatus();
        } else {    //deal with any commands not built-in
            exitStatus = runCommand(curCommand);    //run the user command
            //although exitStatus is global, log status here so forced exits [exit(1)] can be utilized and saved
        }

        if (curCommand->args != NULL) {    //free the argument array for next iteration of while loop
            free(curCommand->args);
            curCommand->args = NULL;
        }
    }
}


/***********************************************************
 * getCommand: parses input to get command.
 *
 * parameters: user command, command struct.
 * returns: none.
 ***********************************************************/

void getCommand(char *command, struct command *curCommand) {
    int i = 0;    //argument i
    char *delimiter = " \n";    //command string delimiters
    int outRedirect = FALSE;    //indicates whether output redirection is necessary
    int inRedirect = FALSE;    //indicates whether input redirection is necessary

    char *token = strtok(command, delimiter);    //get the first token in the command string

    while (token != NULL) {        //while there are still tokens in the command
        regex_t regex;        //set up regular expression
        regcomp(&regex, "^#", 0);        //check if there is a comment symbol
        int isComment = regexec(&regex, token, 0, NULL, 0);        //save if command is a comment

        if (outRedirect == TRUE) {        //if it's previously been determined there is output redirection
            outRedirect = FALSE;             //reset output redirection indicator
            curCommand->outputFile = token;        //current token is now name of output file
        } else if (inRedirect == TRUE) {        //if it's previously been determined there is input redirection
            inRedirect = FALSE;             //reset input redirection indicator
            curCommand->inputFile = token;        //current token is now name of input file
        } else if (isComment == 0) {        //if there's a comment in the command
            curCommand->args[i] = '\0';    //ignore all characters in the command and leave
            break;
        } else if (strcmp(token, "<") == 0) {    //if there's an input redirection symbol
            inRedirect = TRUE;             //set the input redirection indicator
        } else if (strcmp(token, ">") == 0) {    //if there's an output redirection indicator
            outRedirect = TRUE;             //set the output redirection indicator
        } else if (strcmp(token, "&") == 0) {   //if there's a background symbol
            if (backgroundDisabled == FALSE) {    //and if the background isn't disabled
                curCommand->background = TRUE;    //set the background process indicator
            }
        } else if (strstr(token, "$$")) {    //if there's a PID symbol
            int pid = getpid();    //get PID
            char pre[MAX_LENGTH];    //chars before the $$
            char res[MAX_LENGTH];    //end result
            char post[MAX_LENGTH];    //chars after the $$
            memset(pre, '\0', MAX_LENGTH);    //make sure all arrays aren't full of garbage
            memset(res, '\0', MAX_LENGTH);
            memset(post, '\0', MAX_LENGTH);
            int replace = strstr(token, "$$") - token;    //find placement of $$ in string
            strncpy(pre, token, replace);    //copy chars before $$ to pre array
            strncpy(post, &token[replace + 2], strlen(token) - (replace + 2));    //copy chars after $$ to post array
            if (pre == NULL && post == NULL) {    //if only $$, just use PID
                sprintf(res, "%d", pid);
            } else if (pre == NULL) {    //if chars after $$, use PID and post
                sprintf(res, "%d%s", pid, post);
            } else if (post == NULL) {    //if chars before $$, use pre and PID
                sprintf(res, "%s%d", pre, pid);
            } else {    //otherwise use all three for resulting string
                sprintf(res, "%s%d%s", pre, pid, post);
            }
            curCommand->args[i++] = res;    //add resulting string to argument array
        } else {    //if argument isn't redirection, filename, comment, or background process
            curCommand->args[i++] = token;    //save command in argument array
        }

        token = strtok(NULL, delimiter);    //get the next token
    }
}


/***********************************************************
 * exitShell: kills all processes runningand exits shell
 *
 * parameters: none.
 * returns: none.
 ***********************************************************/

void exitShell() {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {    //loop through background PIDs
        if (backProcs[i] != NULL) {    //if any aren't null
            kill(backProcs[i]->backPID, SIGKILL);    //kill the process
            free(backProcs[i]);    //free the memory
            backProcs[i] = NULL;   //and make it null
        }
    }
    exit(EXIT_SUCCESS);    //then exit the shell
}


/***********************************************************
 * changeDir: changes current directory.
 *
 * parameters: directory path.
 * returns: none.
 ***********************************************************/

void changeDir(char *path) {
    int dir;    //check if successful

    if (path == NULL) {    //if path is null, command was just cd
        dir = chdir(getenv("HOME"));    //cd goes to home directory

        if (dir == -1) {     //make sure chdir was successful
            perror("Error:");    //if not, print error
            return;
        }
    } else {    //if path wasn't null
        dir = chdir(path);    //change directory to path indicated

        if (dir == -1) {    //make sure chdir was successful
            perror("Error:");    //if not, print error
            return;
        }
    }
}


/***********************************************************
 * printStatus: prints exit status of most recent process.
 *
 * parameters: none.
 * returns: none.
 ***********************************************************/

void printStatus() {
    fprintf(stdout, "exit value %d\n", exitStatus);    //print exit status from most recently terminated process
    fflush(stdout);    //flush output
}


/***********************************************************
 * runCommand: creates child processes to run commands.
 *
 * parameters: command struct.
 * returns: exit status int.
 ***********************************************************/

int runCommand(struct command *curCommand) {
    int stdOut = dup(STDOUT_FILENO);    //saves a copy of the stdout file descriptor
    int stdIn = dup(STDIN_FILENO);    //saves a copy of the stdin file descriptor

    if (stdOut == -1 || stdIn == -1) {    //make sure the dup function worked
        perror("Error");    //if not, print error
        exit(EXIT_FAILURE);
    }

    pid_t spawnpid = fork();    //fork the process

    if (spawnpid == CHILD) {    //if in the child process
        int inputFD = -1;    //set input and output file descriptors
        int outputFD = -1;

        if (curCommand->background == TRUE) {    //if process is in the background
            if (curCommand->outputFile == NULL) {    //if there is no output redirection
                outputFD = open("/dev/null", O_WRONLY);    //write output to null device
                if (outputFD == -1) {    //if it can't open
                    printf("error: cannot complete command\n");    //print error message
                    exit(1);    //exit with status 1
                }
                dup2(outputFD, STDOUT_FILENO);    //copy file descriptor to stdout
                close(outputFD);    //close output file descriptor
            }
            if (curCommand->inputFile == NULL) {    //if there is no input redirection
                inputFD = open("/dev/null", O_RDONLY);    //read input to null device
                if (inputFD == -1) {    //if it can't open
                    printf("error: cannot complete command\n");    //print error message
                    exit(1);    //exit with status 1
                }
                dup2(inputFD, STDIN_FILENO);    //copy file descriptor to stdin
                close(inputFD);    //close input file descriptor
            }
        } else {    //if process is in the foreground
            if (curCommand->outputFile != NULL) { //if there's output redirection
                //open an existing file,truncate it, or create a new one to serve as a temporary stdout
                outputFD = open(curCommand->outputFile, O_WRONLY | O_TRUNC | O_CREAT, 0777);
                if (outputFD == -1) {    //if it can't open
                    printf("cannot open %s for output\n", curCommand->outputFile);    //print error message
                    exit(1);    //exit with status 1
                }
                dup2(outputFD, STDOUT_FILENO);    //copy file descriptor to stdout
                close(outputFD);    //close output file descriptor
            }
            if (curCommand->inputFile != NULL) {    //if there's input redirection
                inputFD = open(curCommand->inputFile, O_RDONLY, 0777);    //open an existing file to serve as a temporary stdin stream
                if (inputFD == -1) {    //if it can't open
                    printf("cannot open %s for input\n", curCommand->inputFile);    //print error message
                    exit(1);    //exit status 1
                }
                dup2(inputFD, STDIN_FILENO);    //copy file descriptor to stdin
                close(inputFD);    //exit with status 1
            }
        }
        execvp(curCommand->args[0], curCommand->args);    //execute the command

        printf("%s: no such file or directory\n", curCommand->args[0]);    //print error message if not a file
        exit(1);    //exit with status 1

    } else if (spawnpid > CHILD) {    //if in the parent process
        pid_t exitpid;

        if (curCommand->background == TRUE) {    //if child is a background process
            saveProcess(spawnpid);    //save the child's PID to array of background PIDs
            fprintf(stdout, "background pid is %d\n", spawnpid);    //print that the process has begun executing and PID
            fflush(stdout);   //flush output
        } else {    //if child is a foreground process
            forePID = spawnpid;    //save the child's PID
            exitpid = waitpid(spawnpid, &exitStatus, 0);    //wait for child to end before the parent resumes
        }

        if (curCommand->inputFile != NULL) {
            dup2(stdIn, STDIN_FILENO);    //reset stdin to default
        }
        if (curCommand->outputFile != NULL) {
            dup2(stdOut, STDOUT_FILENO);    //reset stdout to default
        }
        fflush(stdout);    //flush output
        return WEXITSTATUS(exitStatus);     //return the child's exit status

    } else {    //if not in child or parent, something went wrong!
        perror("Error");    //print error message
        exit(EXIT_FAILURE);
    }
}


/***********************************************************
 * interruptSignal: kills foreground process and outputs signal
 * that interrupted.
 *
 * parameters: signal number int.
 * returns: none.
 ***********************************************************/

void interruptSignal(int sigNum) {
    kill(forePID, SIGKILL);    //kill process
    fprintf(stdout, "terminated by signal %d\n", sigNum);    //print what signal terminated process
    fflush(stdout);    //flush output
}


/***********************************************************
 * childTerminates: executes when child process terminates.
 * prints what child exit status was and frees the memory.
 *
 * parameters: signal number int.
 * returns: none.
 ***********************************************************/

void childTerminates(int sigNum) {
    int i;
    for (i = 0; i < MAX_PROCS; i++) {   //look through all PIDs in background array
        if (backProcs[i] != NULL) {    //if not null
            pid_t pid = waitpid(backProcs[i]->backPID, &exitStatus, WNOHANG);    //get the child's PID

            if (pid > 0) {    //if PID is valid, then child just terminated
                backProcs[i]->active = FALSE;    //indicate that the process is no longer running
                kill(backProcs[i]->backPID, SIGKILL);    //kill the zombie
                if (exitStatus != 0  && exitStatus != 1) {    //if exit status was a signal, print signal
                    fprintf(stdout, "background pid %d is done: terminated by signal %d\n", pid, exitStatus);
                } else {    //if exit status wasn't signal, print exit status
                    fprintf(stdout, "background pid %d is done: exit value %d\n", pid, exitStatus);
                }
                fflush(stdout);    //flush output
                free(backProcs[i]);    //free the memory
                backProcs[i] = NULL;    //set to null for use by another
            }
        }
    }

}


/***********************************************************
 * disableBackground: keeps track of whether background
 * processes are enabled or not
 *
 * parameters: signal number int.
 * returns: none.
 ***********************************************************/

void disableBackground(int sigNum) {
    if (backgroundDisabled) {    //if background is disabled
        fprintf(stdout, "Exiting foreground-only mode\n");    //let user know background is enabled
        fflush(stdout);    //flush output
        backgroundDisabled = FALSE;    //set background as enabled
    } else {    //if background is enabled
        fprintf(stdout, "Entering foreground-only mode (& is now ignored)\n");    //let user know background is disabled
        fflush(stdout);    //flush output
        backgroundDisabled = TRUE;    //set background as disabled
    }
}


/***********************************************************
 * saveProcess: saves background process in array to keep
 * track of what has completed.
 *
 * parameters: background process pid.
 * returns: none.
 ***********************************************************/

void saveProcess(pid_t spawnpid) {
    int saved = FALSE;    //indicator to make sure process was saved
    int i = 0;

    struct backProcess *proc = malloc(sizeof(struct backProcess));    //malloc background process struct
    proc->backPID = spawnpid;    //set PID to process' PID
    proc->active = TRUE;    //set process as active

    while (saved == FALSE) {    //while the process still hasn't been saved
        if (backProcs[i] == NULL) {    //if an empty spot is found
            backProcs[i] = proc;    //save the process
            saved = TRUE;    //indicate process has been saved
        }
        i++;    //iterate through array
    }
}