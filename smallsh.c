#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_WORDS 512

// Global Variables
int backgroundFlag = 0;
int childStatus = 0; 
int backgroundChild = 0;
char *words[MAX_WORDS]; 

// Function for checking termination of background processes
void backgroundCheck() {
    pid_t childPid;
    int childStatus;

    while ((childPid = waitpid(-1, &childStatus, WNOHANG)) > 0) {
        if (WIFEXITED(childStatus)) {
            printf("Background process %d terminated. Exit status: %d\n", (int)childPid, WEXITSTATUS(childStatus));
        } else if (WIFSIGNALED(childStatus)) {
            printf("Background process %d terminated by signal: %d\n", (int)childPid, WTERMSIG(childStatus));
        }
    }
}

int main(int argc, char *argv[]) {
    struct sigaction SIGINT_default = {0};
    struct sigaction SIGTSTP_default = {0};
    struct sigaction SIGINT_action = {0};
    struct sigaction SIGTSTP_action = {0};
    
    SIGINT_action.sa_handler = SIG_IGN; 
    SIGTSTP_action.sa_handler = SIG_IGN; 
    
    sigaction(SIGINT, &SIGINT_action, &SIGINT_default); 
    sigaction(SIGTSTP, &SIGTSTP_action, &SIGTSTP_default); 

    // Infinite loop for the shell
    while (1) {
        backgroundFlag = 0;
        backgroundChild = 0;

        // Manage background processes
        backgroundCheck();

        // Display prompt and read input
        const char *PS1 = getenv("PS1");
        if (PS1 == NULL) {
            PS1 = ""; 
        }
        printf("%s", PS1);

        // Read input line
        char *line = NULL;
        size_t lineSize = 0;
        ssize_t lineLength = getline(&line, &lineSize, stdin);
        if (lineLength < 0) {
            if (feof(stdin) != 0) {
                printf("\n"); 
                break; 
            }
            perror("Error reading input");
            exit(EXIT_FAILURE);
        }

        // Split input line into words
        size_t numWords = wordsplit(line, words);

        // Expand parameters in words
        for (size_t i = 0; i < numWords; ++i) {
            char *expandedWord = expand(words[i]);
            free(words[i]);
            words[i] = expandedWord;
        }

        // Execute built-in commands or fork and execute external commands
                for (size_t i = 0; i < numWords; ++i) {
            char *expandedWord = expand(words[i]);
            free(words[i]);
            words[i] = expandedWord;
        }

        // Execute built-in commands or fork and execute external commands
        if (numWords > 0 && strcmp(words[0], "exit") == 0) {
            // ... (exit handling)
        } else if (numWords > 0 && strcmp(words[0], "cd") == 0) {
            // ... (cd handling)
        } else {
            // Fork and execute external command
            pid_t childPid = fork();
            if (childPid == -1) {
                perror("fork() error");
                exit(EXIT_FAILURE);
            } else if (childPid == 0) {
                // ... (child process handling)
            } else {
                // Parent process
                if (!backgroundFlag) {
                    int status;
                    waitpid(childPid, &status, 0); // Wait for the child process to finish

                    if (WIFSIGNALED(status)) {
                        printf("Foreground process exited with signal %d\n", WTERMSIG(status));
                    } else if (WIFEXITED(status)) {
                        printf("Foreground process exited with status %d\n", WEXITSTATUS(status));
                    }
                } else {
                    printf("Background process started with PID %d\n", childPid);
                }
            }
        }

        // Handle background process execution
        while ((backgroundChild = waitpid(-1, &childStatus, WNOHANG)) > 0) {
            if (WIFEXITED(childStatus)) {
                printf("Background process %d exited with status %d\n", backgroundChild, WEXITSTATUS(childStatus));
            } else if (WIFSIGNALED(childStatus)) {
                printf("Background process %d exited due to signal %d\n", backgroundChild, WTERMSIG(childStatus));
            }
        }
        free(line);
    }
    return 0;
}

size_t wordsplit(char const *line, char *words[]) {
    size_t wordCount = 0;
    const char *delimiters = " \t\n";                       
    char *lineCopy = strdup(line);                          
    char *token = strtok(lineCopy, delimiters);

    while (token != NULL && wordCount < MAX_WORDS) {
        words[wordCount] = strdup(token);
        token = strtok(NULL, delimiters);
        wordCount++;
    }
    free(lineCopy);                                         
    return wordCount;
}

char *expand(char const *word) {
    const char *paramChars = "$"; 
    char *expandedWord = malloc(strlen(word) + 1); 
    char *destination = expandedWord;

    while (*word != '\0') {
        if (*word == '$') {
            word++;
            if (*word == '\0') {
                *destination = '$';
                destination++;
                break;
            } else if (*word == '$') {
                *destination = '$';
                destination++;
            } else if (*word == '{') {
                word++;
                char *paramEnd = strchr(word, '}');
                if (paramEnd != NULL) {
                    char paramName[paramEnd - word + 1];
                    strncpy(paramName, word, paramEnd - word);
                    paramName[paramEnd - word] = '\0';
                    word = paramEnd + 1;
                    continue;
                }
            } else {
                if (*word == '!') {
                    pid_t backgroundPid = getpid(); 
                    destination += sprintf(destination, "%d", (int)backgroundPid);
                    word++;
                } else if (*word == '$') {
                    pid_t shellPid = getpid(); 
                    destination += sprintf(destination, "%d", (int)shellPid);
                    word++;
                } else if (*word == '?') {
                    int exitStatus = 0; 
                    destination += sprintf(destination, "%d", exitStatus);
                    word++;
                } else {
                    *destination = '$';
                    destination++;
                    *destination = *word;
                    destination++;
                    word++;
                }
            }
        } else {
            *destination = *word;
            destination++;
        }
        word++;
    }
    *destination = '\0'; 
    return expandedWord;
}
