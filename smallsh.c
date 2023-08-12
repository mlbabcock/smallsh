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

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

#define MAX_LINE_LENGTH 1024
#define MAX_WORD_COUNT 512

// Global Functions
char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

void manage_background_processes();
void print_prompt();
char** parse_line(char* line);
int execute_command(char** words);
void handle_signal(int signal);


size_t wordsplit(char const *line);
char param_scan(char const *word, char **start, char **end);
char *build_str(char const *start, char const *end);
char *expand(char const *word);

// Global Variables
char *line;
char** words;
int word_count;
int last_exit_status;
pid_t last_background_pid;

int is_background_operator(char* word){
    return strcmp(word, "&") == 0;
}

int is_redirection_operator(char* word){
    return strcmp(word, ">") == 0 || strcmp(word, "<") == 0 || strcmp(word, ">>") == 0;
}

int main(int argc, char *argv[])
{
    // Signal Handlers
    signal(SIGINT, handle_signal);
    signal(SIGTSTP, handle_signal);

    // Initialize last exit status and background PID
    last_exit_status = 0;
    last_background_pid = -1;

    line = malloc(sizeof(char) * MAX_LINE_LENGTH);                      // Buffer to store user's input

    FILE *input = stdin;
    char *input_fn = "(stdin)";

    if (argc == 2) {
        input_fn = argv[1];
        input = fopen(input_fn, "re");
        if (!input) err(1, "%s", input_fn);
    } else if (argc > 2) {
        errx(1, "too many arguments");
    }

    while (1) {
    //prompt:;
        /* TODO: Manage background processes */
        manage_background_processes();

        /* TODO: prompt */
        print_prompt();

        // Read line of input
        int length = MAX_LINE_LENGTH;
        int line_len = getline(&line, &length, stdin);

        if (line_len == -1 && errno == EINTR) {
            errno = 0;
            printf("\n");
            print_prompt();
            continue;
        }

        char* words;
        words = parse_line(line);
        expand_words(words);
        parse_words(words);
        execute_command(words);

        for (int i = 0; i < word_count; i++) {
            free(words[i]);
        }
        free(words);
    }
    return 0;
}

void print_prompt(){
    char* user = getenv("USER");
    char* ps1 = getenv("PS1");
    fprintf("%s%s$ ", user, ps1);
}

char** parse_line(char* line) {
    char* word = strtok(line, " ");
    char** words = malloc(sizeof(char*) * MAX_WORD_COUNT);

    // iterate over words
    int word_count = 0;
    while (word != NULL) {
        word = expand_word(word);

        words[word_count] = word;
        word = strtok(NULL, " ");
        word_count++;
    }
    return words;
}

void handle_signal(int signal) {
    if (signal == SIGINT) {
        return;
    }

    if (signal == SIGTSTP) {
        kill(getpid(), SIGSTOP);
    }
    errno = 0;
    printf("\n");
}

void parse_words(char** words){
    for (int i = 0; i < word_count; i++) {
        if (is_background_operator(words[i])) {
            words[i] = NULL;
            words[word_count++] = words[i + 1];
            i++;
        } else if (is_redirection_operator(words[i])) {
            if (words[i + 1] != NULL) {
                words[i] = words[i] + 1;
                words[word_count++] = words[i + 1];
                i++;
            }
        }
    }
}

int execute_command(char** words) {
    if (strcmp(words[0], "exit") == 0) {
        int status;
        if (words[1] != NULL) {
            status = atoi(words[1]);
        } else {
            status = last_exit_status;
        }
        exit(status);
    } else if (strcmp(words[0], "cd") == 0) {
        if (words[1] == NULL) {
            chdir("/");
        } else {
            chdir(words[1]);
        }
    } else {
        int pid, status;
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork");
            return -1;
        } else if (pid == 0) {
            // Ignore SIGINT and SIGTSTP
            if (isatty(STDIN_FILENO)) {
                signal(SIGINT, SIG_IGN);
                signal(SIGTSTP, SIG_IGN);           
            }

            // Handle Redirection
            for (int i = 0; i < word_count; i++) {
                if (is_redirection_operator(words[i])) {
                    char* filename = words[i + 1];
                    int fd;
                    if (strcmp(words[i], "<") == 0) {
                        fd = open(filename, O_RDONLY);
                    } else if (strcmp(words[i], ">") == 0) {
                        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0777);
                    } else {
                        fd = open(filename, O_WRONLY | O_APPEND | O_CREAT, 0777);
                    }
                    if (fd == -1) {
                        perror("open");
                        exit(1);
                    }
                    close(0);
                    dup2(fd, 0);
                    close(fd);
                }
            }
            int status = execvp(words[0], words);
            if (status == -1) {
                perror("execvp");
                exit(1);
            }
        } else {                                                                // Parent Process
            if (!is_background_operator(words[word_count - 1])) {
                waitpid(pid, &status, 0);
                last_exit_status = status;
            } else {
                last_background_pid = pid;
            }
        }
    }
    return 0;
}

void manage_background_processes() {
  pid_t pgid = getpgid(getpid());

  for (pid_t pid = 0; pid < sysconf(_SC_CHILD_MAX); pid++) {
    if (getpgid(pid) == pgid) {                                                   // Check the status of the process
      int status;
      if (waitpid(pid, &status, WNOHANG) == pid) {
        if (WIFEXITED(status)) {
          fprintf(stderr, "Child process %jd done. Exit status %d\n", (intmax_t) pid, status);
        } else if (WIFSIGNALED(status)) {
          fprintf(stderr, "Child process %jd done. Signaled %d\n", (intmax_t) pid, WTERMSIG(status));
        }
      } else if (WIFSTOPPED(status)) {
        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) pid);
        kill(pid, SIGCONT);
      }
    }
  }
}
