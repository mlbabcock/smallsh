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

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char *expand(char const *word, pid_t *background_pids, int background_count, int last_exit_status);

volatile sig_atomic_t interrupt_flag = 0;

void sigint_handler(int signo){
    // Custom behavior for SIGINT (CTRL-C) signal
    interrupt_flag = 1;
}

void sigtstp_handler(int signo){
  // Custom behavior for SIGTSTP (CTRL-Z) signal
  // Do nothing
}

void print_child_status(pid_t pid, int status){
    if (WIFEXITED(status)){
        fprintf(stderr, "Child process %jd done. Exit status %d\n", (intmax_t)pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)){
        fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)pid, WTERMSIG(status));
    }
}

int main(int argc, char *argv[])
{
    #define MAX_BACKGROUND_PROCESSES 100
    pid_t background_pids[MAX_BACKGROUND_PROCESSES];
    int background_count = 0;

    signal(SIGINT, sigint_handler);              // signal handlers
    signal(SIGTSTP, sigtstp_handler);

  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }

  char *line = NULL;
  size_t n = 0;
  int status;
  int last_exit_status = 0;

  for (;;) {
//prompt:;
    /* TODO: Manage background processes */
    int background = 0;

    /* Check for terminated background processes */
    for (int i = 0; i < background_count; ++i){
        pid_t pid = waitpid(background_pids[i], &status, WNOHANG | WUNTRACED | WCONTINUED);
        if (pid > 0) {
            if (WIFEXITED(status) || WIFSIGNALED(status)){
                print_child_status(pid, status);
            } else if (WIFSTOPPED(status)){
                fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)pid);
                kill(pid, SIGCONT);
            }
        }
    }

    /* TODO: prompt */
    if (input == stdin) {
        if (interrupt_flag){
            putchar('\n');
            interrupt_flag = 0;
            char *prompt = getenv("PS1");
            if (prompt == NULL) {
                prompt = "smallsh> ";                   // default prompt if no PS1
            }
            fprintf(stderr, "%s", prompt);
        }
    }

    ssize_t line_len; 
    do{
        errno = 0;
        line_len = getline(&line, &n, input);
    } while (line_len < 0 && errno == EINTR);

    if (line_len < 0){
         err(1, "%s", input_fn);
    }
    
    size_t nwords = wordsplit(line);
    int in_file = STDIN_FILENO;
    int out_file = STDOUT_FILENO;
    int append_flag = 0;

    for (size_t i = 0; i < nwords; ++i) {
        if (strcmp(words[i], "<") == 0){                           // Redirection handler for <
            if (i + 1 < nwords){
                in_file = open(words[i + 1], O_RDONLY);
                if (in_file < 0){
                    fprintf(stderr, "Error opening file %s: %s\n", words[i + 1], strerror(errno));
                    continue;
                }
                i++;
            } else {
                fprintf(stderr, "Missing file name for redirection\n");
            }
        } else if (strcmp(words[i], ">") == 0 || strcmp(words[i], ">>") == 0){      // Redirection handler for > and >>
            if (i + 1 < nwords){
                int flags = O_WRONLY | O_CREAT;
                if (strcmp(words[i], ">>") == 0){
                    flags |= O_APPEND; 
                    append_flag = 1;
                } else {
                    flags |= O_TRUNC;
                }
                out_file = open(words[i + 1], flags, S_IRUSR | S_IWUSR | S_IRGRP |S_IROTH);
                if (out_file < 0) {
                    fprintf(stderr, "Error opening file %s: %s\n", words[i+ 1], strerror(errno));
                    continue;
                }
                i++;
            } else {
                fprintf(stderr, "Missing file name for redirection\n");
            }
        } else if (strcmp(words[i], "&") == 0){
            background = 1;
        } else if (strcmp(words[i], "exit") == 0){                        // Exit function handler
            int exit_status = 0;
            if (i + 1 < nwords) {
                exit_status = atoi(words[i + 1]);
                i++;
            }
            for (size_t j = 0; j < nwords; j++){
                free(words[j]);                                         // Clean up for exit
            }
            free(line);
            fclose(input);
            exit(exit_status);
        } else if (strcmp(words[i], "cd") == 0) {                       // CD function handler
            if (i + 1 < nwords){
                int ret = chdir(words[i + 1]);
                if (ret != 0){
                    fprintf(stderr, "cd: %s: %s\n", words[i + 1], strerror(errno));
                }
                i++;
            } else {                                                    // CD function handler for no argument
                char *home_dir = getenv("HOME");
                if (home_dir) {
                    int ret = chdir(home_dir);
                    if (ret != 0) {
                        fprintf(stderr, "cd: %s: %s\n", home_dir, strerror(errno));
                    }
                } else {
                    fprintf(stderr, "cd: Home environmental variable has not been set.\n");
                }
            }
        } else {
            fprintf(stderr, "Word %zu: %s  -->  ", i, words[i]);
            char *exp_word = expand(words[i], background_pids, background_count, last_exit_status);

            free(words[i]);
            words[i] = exp_word;
            fprintf(stderr, "%s\n", words[i]);

            // Execute non-builtin commands using the appropriate EXEC(3) function
            pid_t pid = fork();
            if (pid == 0) {
                signal(SIGINT, SIG_DFL);     // Reset signal handlers in child
                signal(SIGTSTP, SIG_DFL);
                if (in_file != STDIN_FILENO) {
                    dup2(in_file, STDIN_FILENO);
                    close(in_file);
                }
                if (out_file != STDOUT_FILENO) {
                    dup2(out_file, STDOUT_FILENO);
                    close(out_file);
                }
                execvp(words[0], words);
                fprintf(stderr, "Error executing command: %s\n", strerror(errno));
                exit(1);
            } else if (pid < 0){
                fprintf(stderr, "Fork Error: %s\n", strerror(errno));
            } else {
                if (background) {
                    if (background_count < MAX_BACKGROUND_PROCESSES){
                        background_pids[background_count++] = pid;
                        fprintf(stderr, "Background process %d has started.\n", pid);
                    } else{
                        fprintf(stderr, "Maximum background process count reached.\n");
                    }
                } else {
                    waitpid(pid, &status, 0);
                    last_exit_status = WEXITSTATUS(status);             // Get exit status of last command
                }
            }

            if (in_file != STDIN_FILENO){
                close(in_file);
                in_file = STDIN_FILENO;
            }
            if (out_file != STDOUT_FILENO){
                close(out_file);
                out_file = STDOUT_FILENO;
            }
        }
    }
  }
}

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') {
        while (*c && *c != '\n') ++c;
        if (*c == '\n') ++c;
        continue;
    }

    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }

    // Check if word ends with # char 
    if (wlen > 0 && words[wind][wlen - 1] == '#'){
        words[wind][wlen - 1] = '\0';                           // Remove # char from word
    }
    ++wind;
    if (wlen > 0 && words[wind][wlen - 1] == '\\'){
        words[wind][wlen - 1] = ' ';                            // Remove the escape backslash
    }
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char param_scan(char const *word, char **start, char **end) {
    char ret = 0;
    *start = NULL;
    *end = NULL;
    for (char *s = word; *s; s++) {
        if (*s == '$' && s[1] == '{') {
            ret = '$';
            *start = s;
            s++;
            while (*s && *s != '}') {
                ++s;
            }
            *end = s + 1;
            break;
        }
    }
    return ret;
}


/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *expand(char const *word, pid_t *background_pids, int background_count, int last_exit_status)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(pos, start);
  while (c) {
    switch (c) {
        case '$':
            if (start[1] == '$') {
                char pid_str[20];
                snprintf(pid_str, sizeof(pid_str), "%d", getpid());
                return build_str(pid_str, NULL);
            } 
            break;
            if (start[1] == '?') {
                // Handle $? expansion
                if (start[1] == '?') {
                    char exit_status_str[20];
                    snprintf(exit_status_str, sizeof(exit_status_str), "%d", last_exit_status);
                    return build_str(exit_status_str, NULL);
                }
                break;
            if (start[1] == '!') {
                // Handle $! expansion
                if (start[1] == '!') {
                    if (background_count > 0) {
                        char bg_pid_str[20];
                        snprintf(bg_pid_str, sizeof(bg_pid_str), "%d", background_pids[background_count - 1]);
                        return build_str(bg_pid_str, NULL);
                    }
                    pos = end;
                }
            }
            break;
      case '#':
        while (*pos && *pos != '\n') {
            pos++;
        }
        break;
      default:
        break;
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
