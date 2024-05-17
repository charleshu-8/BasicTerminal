#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_INPUT_SIZE 1024
#define MAX_TOKEN_SIZE 64
#define MAX_NUM_TOKENS 64

// Splits string by space and returns array of tokens
char **tokenize(char *line) {
	char **tokens = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
	char *token = (char *)malloc(MAX_TOKEN_SIZE * sizeof(char));
	int i, tokenIndex = 0, tokenNo = 0;

	for(i = 0; i < strlen(line); i++) {
		char readChar = line[i];

	    if (readChar == ' ' || readChar == '\n' || readChar == '\t') {
            token[tokenIndex] = '\0';
            if (tokenIndex != 0) {
                tokens[tokenNo] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
                strcpy(tokens[tokenNo++], token);
                tokenIndex = 0;
            }
		} else {
		    token[tokenIndex++] = readChar;
		}
  	}

    free(token);
    tokens[tokenNo] = NULL;
    return tokens;
}

// User prompts 'cd' command
void changeDir(char **tokens, int tokenCnt) {
	// Are expecting 2 arguments ('cd' call and desired path)
	// If exceeds 2 arguments, throw argument count error
	// Otherwise, continue directory change
	if (tokenCnt > 2) {
		printf("Shell: Incorrect command\n");
	} else if (chdir(tokens[1]) != 0) {
		// Print error to user if `cd` command fails
		perror("cd");
	}
}

// Create child process for general user command
void genCmd(char **tokens) {
	// Find & pass command for child process to run
	execvp(tokens[0], tokens);
	// If can't find command, print error to user & kill child process
	printf("Shell: Incorrect command\n");
	exit(1);
}

volatile sig_atomic_t stop;

// Catches Ctrl+C input and notifies program to stop foreground processes
void sigHandle(int sig) {
	printf("\n");
	stop = 1;
}

int main(int argc, char* argv[]) {
	char line[MAX_INPUT_SIZE];
	char **tokens;
	// Track background & foreground process PIDs
	int *backgroundTracker = (int*) malloc(sizeof(int) * MAX_NUM_TOKENS);
	int *foregroundTracker = (int*) malloc(sizeof(int) * MAX_NUM_TOKENS);

	// File reading mode
	FILE* fp;
	if(argc == 2) {
		fp = fopen(argv[1],"r");
		if(fp < 0) {
			printf("File doesn't exists.");
			return -1;
		}
	}

	signal(SIGINT, sigHandle);

	// Command prompt loop
	int hasExited = 0;
	while(!hasExited) {
		stop = 0;

		// BEGIN: TAKING INPUT
		bzero(line, sizeof(line));
		if(argc == 2) {
            // Batch mode
			if (fgets(line, sizeof(line), fp) == NULL) {
                // File reading finished
				break;
			}
			line[strlen(line) - 1] = '\0';
		} else {
            // Interactive mode
			printf("$ ");
			scanf("%[^\n]", line);
			getchar();
		}
		// END: TAKING INPUT

		// Perform pre-check on input
		// If no input is detected (user just pressed return key), do nothing and re-display command prompt
		if (strcmp(line, "")) {
			// Terminate user input with new line
			line[strlen(line)] = '\n';
			// Tokenize user input
			tokens = tokenize(line);

			// If user prompts 'exit' command, go through background processes and kill them
			if (!strcmp(tokens[0], "exit")) {
				int status;
				for (int i = 0; i < MAX_NUM_TOKENS; i++) {
					if (backgroundTracker[i] != NULL) {
						kill(backgroundTracker[i], SIGTERM);
						waitpid(backgroundTracker[i], &status, 0);
					}
				}
				// Then leave prompt loop and end program
				hasExited = 1;
			}

			if (!hasExited) {
				// Parse passed argument
				// Determine how to run the given process
				// 0 -> Standard foreground
				// 1 -> Single command in background
				// 2 -> Sequential commands in foreground
				// 3 -> Parallel commands in foreground
				int tokenCnt = 0;
				int positionCheck = 0;
				int runMode = 0;
				for(int i=0; tokens[i] != NULL; i++){
					// Track token count
					tokenCnt++;
					// Track '&' usage
					if (!strcmp(tokens[i], "&")) {
						runMode = 1;
						positionCheck = tokenCnt;
					}
					// Track '&&' usage
					if (!strcmp(tokens[i], "&&")) {
						runMode = 2;
					}
					// Track '&&&' usage
					if (!strcmp(tokens[i], "&&&")) {
						runMode = 3;
					}
				}

				// Check if '&' is used incorrectly (not at end of command)
				if (runMode == 1 && positionCheck != tokenCnt) {
					runMode = 0;
				}

				switch (runMode) {
					case 1:
						// Background mode
						// Remove '&' token
						free(tokens[tokenCnt - 1]);
						tokens[tokenCnt - 1] = NULL;

						// Check if used command is 'cd'
						if (!strcmp(tokens[0], "cd")) {
							changeDir(tokens, tokenCnt);
						} else {
							// Generic commands
							int forkRet = fork();
							if (forkRet == 0) {
								// Swap process group so process runs in background
								setpgid(0, 0);
								genCmd(tokens);
							} else {
								// Put child process PID in tracker
								// Will reap background processes after every command
								int pid = forkRet;
								for (int i = 0; i < MAX_NUM_TOKENS; i++) {
									if (backgroundTracker[i] == NULL) {
										backgroundTracker[i] = pid;
										break;
									}
								}
							}
						}
						break;
					case 2:
						// Sequential foreground mode
						// Need to break up input into separate commands
						// Use loop that runs each subcommand in input while tracking overall progress through overall input tokens
						int tokenTracker = 0;
						while (tokenTracker < tokenCnt && !stop) {
							char **tokensBuffer = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
							int tokensBufferCnt = 0;
							// Walk through input and write to token buffer until we encounter '&&' token, denoting end of subcommand
							for (tokensBufferCnt; tokens[tokenTracker] != NULL; tokensBufferCnt++, tokenTracker++) {
								if (!strcmp(tokens[tokenTracker], "&&")) {
									tokenTracker++;
									break;
								}
								tokensBuffer[tokensBufferCnt] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
								strcpy(tokensBuffer[tokensBufferCnt], tokens[tokenTracker]);
							}

							// If the found subcommand is not empty string (i.e., some user input error), run command like in standard foreground mode
							if (tokensBufferCnt > 0) {
								// Check if used command is 'cd'
								if (!strcmp(tokensBuffer[0], "cd")) {
									changeDir(tokensBuffer, tokensBufferCnt);
								} else {
									// Generic commands
									int forkRet = fork();
									if (forkRet == 0) {
										genCmd(tokensBuffer);
									} else {
										int pid = forkRet;
										int status;

										// If user presses Ctrl+C during runtime, kill current process and stop command chain
										if (stop) {
											kill(pid, SIGTERM);
										}

										// Parent process waits on child process
										waitpid(pid, &status, 0);
									}
								}
							}

							// Free buffer
							for(int i = 0; tokensBuffer[i] != NULL; i++) {
								free(tokensBuffer[i]);
							}
							free(tokensBuffer);
						}
						break;
					case 3:
						// Parallel foreground mode
						// Need to break up subcommands to run them in parallel
						// Use loop that runs each subcommand in input while tracking overall progress through overall input tokens
						tokenTracker = 0;
						while (tokenTracker < tokenCnt && !stop) {
							char **tokensBuffer = (char **)malloc(MAX_NUM_TOKENS * sizeof(char *));
							int tokensBufferCnt = 0;
							// Walk through input and write to token buffer until we encounter '&&&' token, denoting end of command
							for (tokensBufferCnt; tokens[tokenTracker] != NULL; tokensBufferCnt++, tokenTracker++) {
								if (!strcmp(tokens[tokenTracker], "&&&")) {
									tokenTracker++;
									break;
								}
								tokensBuffer[tokensBufferCnt] = (char*)malloc(MAX_TOKEN_SIZE*sizeof(char));
								strcpy(tokensBuffer[tokensBufferCnt], tokens[tokenTracker]);
							}

							// If the found subcommand is not empty string (i.e., some user input error), run command in new process
							if (tokensBufferCnt > 0) {
								// Check if used command is 'cd'
								if (!strcmp(tokensBuffer[0], "cd")) {
									changeDir(tokensBuffer, tokensBufferCnt);
								} else {
									// Generic commands
									int forkRet = fork();
									if (forkRet == 0) {
										genCmd(tokensBuffer);
									} else {
										int pid = forkRet;

										// If user presses Ctrl+C during runtime, kill all foreground processes
										if (stop) {
											for (int i = 0; i < MAX_NUM_TOKENS; i++) {
												if (foregroundTracker[i] != NULL) {
													kill(foregroundTracker[i], SIGTERM);
												}
											}
										} else {
											// Else, put child process PID in tracker
											// Once PID is tracked, return to parsing input to start next subcommand process
											for (int i = 0; i < MAX_NUM_TOKENS; i++) {
												if (foregroundTracker[i] == NULL) {
													foregroundTracker[i] = pid;
													break;
												}
											}
										}
									}
								}
							}

							// Free buffer
							for(int i = 0; tokensBuffer[i] != NULL; i++) {
								free(tokensBuffer[i]);
							}
							free(tokensBuffer);
						}

						// After all subcommand processes have begun running, have parent process wait to reap each foreground process as they finish
						int foregroundCnt = 0;
						while (foregroundCnt < MAX_NUM_TOKENS) {
							for (foregroundCnt = 0; foregroundCnt < MAX_NUM_TOKENS; foregroundCnt++) {
								if (foregroundTracker[foregroundCnt] != NULL) {
									int status;
									waitpid(foregroundTracker[foregroundCnt], &status, 0);
									foregroundTracker[foregroundCnt] = NULL;
									break;
								}
							}
						}
						
						break;
					default:
						// Standard foreground mode
						// Check if used command is 'cd'
						if (!strcmp(tokens[0], "cd")) {
							changeDir(tokens, tokenCnt);
						} else {
							// Generic commands
							int forkRet = fork();
							if (forkRet == 0) {
								genCmd(tokens);
							} else {
								int pid = forkRet;
								int status;

								// If user presses Ctrl+C during runtime, kill current process
								if (stop) {
									kill(pid, SIGTERM);	
								}

								// Parent process waits on child process
								waitpid(pid, &status, 0);
							}
						}	
				}
			}
			
			// Free allocated memory
			for(int i = 0; tokens[i] != NULL; i++) {
				free(tokens[i]);
			}
			free(tokens);
		}

		// After every command input, check background processes to see if they can be reaped
		for (int i = 0; i < MAX_NUM_TOKENS; i++) {
			if (backgroundTracker[i] != NULL) {
				int status;
				int pid = waitpid(backgroundTracker[i], &status, WNOHANG);
				if (pid != 0) {
					backgroundTracker[i] = NULL;
					printf("Shell: Background process finished\n");
				}
			}
		}
	}
	// Free trackers
	free(backgroundTracker);
	free(foregroundTracker);

	return 0;
}
