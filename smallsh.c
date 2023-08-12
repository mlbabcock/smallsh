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
void print_prompt();
char **parse_line(char *line);
void expand_parameters(char **args);
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
int main(int argc, char **argv) {
  // Set up signal handlers
  signal(SIGINT, handle_signal);
  signal(SIGTSTP, SIG_IGN);

  // If no arguments are given, then run in interactive mode
  if (argc == 1) {
    while (1) {
      // Print the prompt
      print_prompt();

      // Get a line of input
      char *line = NULL;
      size_t n = 0;
      ssize_t line_length = getline(&line, &n, stdin);
      if (line_length == -1) {
        if (feof(stdin)) {
          // Exit the shell
          break;
        } else {
          // Handle error
          perror("Failed to read line");
          continue;
        }
      }

      // Parse the line
      char **args = parse_line(line);

      // Execute the command
      int status = execute_command(args, status);

      // Free the line of input
      free(line);

      // Free the arguments
      for (int i = 0; args[i] != NULL; i++) {
        free(args[i]);
      }
      free(args);
    }
  } else if (argc == 2) {
    // Otherwise, run in non-interactive mode
    // Execute the specified command
    int status = execute_command(argv, status);

    // Exit the shell
    exit(status);
  } else {
    // Incorrect usage
    fprintf(stderr, "Usage: smallsh [script_file]\n");
    exit(1);
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

// Execution
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


// Waiting
void handle_signal(int signal) {
  if (signal == SIGINT || signal == SIGTSTP) {
    // Ignore these signals
    return;
  }

  // If any other signal is received, print a message and continue
  fprintf(stderr, "Ignoring signal %d\n", signal);
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
