#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <fcntl.h> 
#include <sys/wait.h> 
#include <sys/stat.h> 
#include <sys/types.h>
#include <stdint.h>


// Global variables
char *prompt = "$ ";


// Function prototypes
int parse_line(char *line);
int execute_command(char **args, int *status);
void handle_signal(int signal);

// Signal handling
void handle_signal(int signal) {
  if (signal == SIGINT) {
    // Ignore this signal
    return;
  }

  // If any other signal is received, print a message and continue
  fprintf(stderr, "Ignoring signal %d\n", signal);
}

// Main
int main() {
  char line[1024];
  int status;

  while (1) {
    printf("$ ");
    fgets(line, sizeof(line), stdin);

    // Parse the line
    char **args = parse_line(line);

    // Execute the command
    int ret = execute_command(args, &status);

    // Handle the exit status
    if (ret == -1) {
      perror("Failed to execute command");
    } else if (status != 0) {
      printf("Exited with status %d\n", status);
    }
  }

  return 0;
}

int parse_line(char *line) {
  char *token;
  char **args = malloc(sizeof(char *) * 10);
  int i = 0;

  token = strtok(line, " ");
  while (token != NULL) {
    args[i] = token;
    i++;
    token = strtok(NULL, " ");
  }

  args[i] = NULL;

  return i;
}

int execute_command(char **args, int *status) {
  int pid = fork();
  if (pid == -1) {
    return -1;
  } else if (pid == 0) {
    // Child process
    execvp(args[0], args);
    exit(1);
  } else {
    // Parent process
    if (args[i] != NULL && args[i][0] != '&') {
      waitpid(pid, status, 0);
    }
  }

  return 0;
}

void print_prompt() {
  printf("%s", prompt);
}

char **parse_line(char *line) {
  char **args = malloc(sizeof(char *) * 10);
  int i = 0;

  for (char *token = strtok(line, " "); token != NULL; token = strtok(NULL, " ")) {
    args[i++] = token;
  }

  args[i] = NULL;
  return args;
}

// Word splitting
char **parse_line(char *line) {
  char **args = malloc(sizeof(char *) * 512);
  int i = 0;

  for (char *token = strtok(line, " "); token != NULL; token = strtok(NULL, " ")) {
    // Handle backslashes
    while (strchr(token, '\\') != NULL) {
      int j = 0;
      char *new_token = malloc(sizeof(char) * strlen(token) + 1);
      for (int i = 0; i < strlen(token); i++) {
        if (token[i] == '\\') {
          i++;
        }
        new_token[j++] = token[i];
      }
      token = new_token;
    }

    // Handle comments
    if (token[0] == '#') {
      break;
    }
    args[i++] = token;
  }
  args[i] = NULL;
  return args;
}

// Expansion
void expand_parameters(char **args) {
  for (int i = 0; args[i] != NULL; i++) {
    if (args[i][0] == '$') {
      // Check for special parameters
      if (args[i][1] == '?') {
        // $? expands to the exit status of the last foreground command
        args[i] = itoa(get_exit_status());
      } else if (args[i][1] == '!') {
        // $! expands to the pid of the most recent background process
        args[i] = itoa(get_pid_of_last_background_process());
      } else if (args[i][1] == '$') {
        // $$ expands to the pid of the current shell process
        args[i] = itoa(getpid());
      } else {
        // Otherwise, expand the parameter as a variable
        args[i] = getenv(args[i] + 2);
        if (args[i] == NULL) {
          args[i] = "";
        }
      }
    }
  }
}

// Waiting
void handle_signal(int signal) {
  // Handle SIGINT (Ctrl-C)
  if (signal == SIGINT) {
    printf("\n");
  }
}

// Parsing
int parse_line(char *line) {
  char **args = malloc(sizeof(char *) * 512);
  int i = 0;

  // Split the line into words
  for (char *token = strtok(line, " "); token != NULL; token = strtok(NULL, " ")) {
    // Handle backslashes
    while (strchr(token, '\\') != NULL) {
      int j = 0;
      char *new_token = malloc(sizeof(char) * strlen(token) + 1);
      for (int i = 0; i < strlen(token); i++) {
        if (token[i] == '\\') {
          i++;
        }
        new_token[j++] = token[i];
      }
      token = new_token;
    }
    // Handle comments
    if (token[0] == '#') {
      break;
    }
    args[i++] = token;
  }
  args[i] = NULL;
  return i;
}

// Input and Output redirection
int execute_command(char **args, int *status) {
  int i = 0;

  // Fork a child process
  pid_t pid = fork();
  if (pid == -1) {
    // Error forking
    fprintf(stderr, "Failed to fork\n");
    return 1;
  }

  if (pid == 0) {
    // Child process
    // Reset all signals to their original dispositions
    sigset_t set;
    sigfillset(&set);
    sigprocmask(SIG_SETMASK, &set, NULL);

    // Handle redirection
    for (i = 0; args[i] != NULL; i++) {
      if (args[i][0] == '<') {
        // Input redirection
        int fd = open(args[i + 1], O_RDONLY);
        if (fd == -1) {
          perror("Failed to open input file");
          exit(1);
        }

        // Redirect stdin
        dup2(fd, 0);
        close(fd);

        // Remove the redirection operator from the arguments
        args[i] = args[i + 1];
        args[i + 1] = NULL;
      } else if (args[i][0] == '>') {
        // Output redirection
        int fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0777);
        if (fd == -1) {
          perror("Failed to open output file");
          exit(1);
        }

        // Redirect stdout
        dup2(fd, 1);
        close(fd);

        // Remove the redirection operator from the arguments
        args[i] = args[i + 1];
        args[i + 1] = NULL;
      }
    }

    // Execute the command
    if (execvp(args[0], args) == -1) {
      // Command not found
      perror("Failed to execute command");
      exit(1);
    }
  } else {
    // Parent process
    // If the command is not in the background, wait for it to finish
    if (args[i] != NULL && args[i][0] != '&') {
      waitpid(pid, status, 0);
      set_exit_status(*status);
    } else {
      // The command is in the background
      // Set the $! variable
      set_pid_of_last_background_process(pid);
    }
  }

  return *status;
}
