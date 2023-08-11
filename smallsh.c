#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h> 
#include <sys/wait.h> 
#include <sys/stat.h> 
#include <sys/types.h>
#include <signal.h>
#include <ctype.h>
#include <stdint.h>


#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);
char pid_str[16];

pid_t last_bg_pid = -1;                                 // Initialize with an invalid value
pid_t get_last_bg_pid() {
    return last_bg_pid;
}

int last_exit_status = 0;                               // Initialize with a default value
int get_last_exit_status() {
    return last_exit_status;
}

void set_last_exit_status(int status) {
    last_exit_status = status;
}

void sigint_handler(int sig) {
    // do nothing 
}

void check_background_processes() {
    pid_t pid;
    int status;

    // Wait for any background process without blocking
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (WIFEXITED(status)) {
            fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)pid, WTERMSIG(status));
        }
    }
}

int main(int argc, char *argv[])
{
    // Signal handling for SIGINT
    struct sigaction sa;
    sa.sa_handler = sigint_handler;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

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
    int exit_status = 0;
    
    for (;;) {
    //prompt:;
    /* TODO: Manage background processes */
    check_background_processes();

    /* TODO: prompt */
    if (input == stdin) {
        char *ps1 = getenv("PS1");
        if (ps1){
            fprintf(stderr, "%s\n", expand(ps1));
        }
    }

    continue_loop:
        continue;
    
    end_of_input:
        exit_status = get_status();
        break;

    prompt:
        prompt();

    // Read line of input
    ssize_t line_len; 
    do {
        line_len = getline(&line, &n, input);
        if (line_len < 0){
            if (feof(input)) {
                exit_status = get_status();                     // end of input
                goto end_of_input;
            } else {
                fprintf(stderr, "\n");
                prompt();
                goto prompt;
            }
        }
    } while (line_len < 0 && errno == EINTR);

    // Handle errors
    if (line_len < 0){
        err(1, "%s", input_fn);
    }

    if (ferror(input)) {
        clearerr(input);
        errno = 0;
    }

    if (line[0] == '\0' || isspace(line[0])) {
        goto continue_loop;                                               // If empty or only whitespace, continue loop
    }
    
    size_t nwords = wordsplit(line);

/* 
   Parsing Part: Check for background operator and redirection
    operators 
*/
    // Check for background operator
    bool run_in_background = false;
    if (nwords > 0 && strcmp(words[nwords - 1], "&") == 0) {
        run_in_background = true;
        free(words[nwords - 1]);
        nwords--;
    }

    // Process redirection operators 
    bool append_output = false;
    for (size_t i = 0; i < nwords; ++i) {
        if (strcmp(words[i], ">") == 0) {
            if (i + 1 < words) {
                freopen(words[i + 1], "w", stdout);
                free(words[i]);
                words[i] = NULL;
                i++;
            }
        } else if (strcmp(words[i], ">>") == 0) {
            if (i + 1 < nwords) {
                append_output = true;
                freopen(words[i + 1], "a", stdout);
                free(words[i]);
                words[i] = NULL;
                i++;
            }
        } else if (strcmp(words[i], "<") == 0) {
            if (i + 1 < nwords) {
                freopen(words[i + 1], "r", stdin);
                free(words[i]);
                words[i] = NULL;
                i++;
            }
        }
    }

    // Clear redirections, and reset stdout 
    if (append_output){
        freopen("/dev/tty", "a", stdout);
    }

/*
    Part 5: Execution -- Built-in Commands
    Exit and CD
*/
    if (nwords > 0) {
        if (strcmp(words[0], "exit") == 0) {
            if (nwords == 1) {                                      // Get exit status from last foreground command
                int exit_status = get_last_exit_status();
                goto continue_loop;
            } else if (nwords == 2) {                               // Convert to integer
                int exit_status = atoi(words[1]);
                goto continue_loop;
            } else {
                fprintf(stderr, "exit: too many arguments\n");
                goto prompt;
            }
        } else if (strcmp(words[0], "cd") == 0) {
            if (nwords == 1) {
                char *home = getenv("HOME");
                if (home) {
                    if (chdir(home) != 0) {
                        perror("cd");
                    }
                } else {
                    fprintf(stderr, "cd: HOME environment variable not set\n");
                }
            } else if (nwords == 2) {
                if (chdir(words[1]) != 0) {
                    perror("cd");
                }
            } else {
                fprintf(stderr, "cd: too many arguments\n");
            }
            continue;
        }
    }

/*
    Part 5: Execution -- Non-Built-in Commands
    Part 6: Waiting
*/
    pid_t child_pid = fork();
    if (child_pid < 0) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (child_pid == 0) {
        // child processes 

        // Restore original signal dispositions
        struct sigaction oldact;
        if (sigaction(SIGINT, NULL, &oldact) == -1) {
            perror("sigaction");
            exit(EXIT_FAILURE);
        }

        if (oldact.sa_handler != SIG_DFL) {
            if (sigaction(SIGINT, &oldact, NULL) == -1) {
                perror("sigaction");
                exit(EXIT_FAILURE);
            }
        }

        for (size_t i = 0; i < nwords; ++i) {
            if (strcmp(words[i], "<") == 0) {
                if (i + 1 < nwords) {
                    freopen(words[i + 1], "r", stdin);
                    free(words[i]);
                    free(words[i + 1]);
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    i++;
                } 
            } else if (strcmp(words[i], ">") == 0) {
                if (i + 1 < nwords) {
                    freopen(words[i + 1], "w", stdout); // Redirect stdout to the specified file (create/truncate)
                    free(words[i]);
                    free(words[i + 1]);
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    i++;
                }
            } else if (strcmp(words[i], ">>") == 0) {
                if (i + 1 < nwords) {
                    freopen(words[i + 1], "a", stdout); // Redirect stdout to the specified file (append)
                    free(words[i]);
                    free(words[i + 1]);
                    words[i] = NULL;
                    words[i + 1] = NULL;
                    i++;
                }
            }
        }

        // execute non built-in commands
        execvp(words[0], words);
        
        // Exec failed - error message 
        perror("execvp");
        exit(EXIT_FAILURE);

    } else {
        if(!run_in_background) {
            int status;
            pid_t waited_pid = waitpid(child_pid, &status, 0);
            if (WIFEXITED(status)) {
                set_last_exit_status(WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                set_last_exit_status(128 + WTERMSIG(status));
            }
        } else {
            fprintf("Background process started with PID %d\n", child_pid);
            set_last_bg_pid(child_pid);
        }
    }


// OTHER PARTB OF CODE
    for (size_t i = 0; i < nwords; ++i) {
      fprintf(stderr, "Word %zu: %s  -->  ", i, words[i]);
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
      fprintf(stderr, "%s\n", words[i]);
    }

    struct command *cmd = parse_command(words, nwords);         // parse

    exit_status = execute_command(cmd);

    // Free memory 
    for (size_t i = 0; i < nwords; ++i){
        free(words[i]);
    }
    free(cmd);
  }

    free(line);  // Free memory allocated by getline

    if(exit_status){
        fprintf(stderr, "Exiting with status %d\n", exit_status);
    }

  return 0;
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
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char **start, char **end)
{
  static char *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = NULL;
  *end = NULL;
  char *s = strchr(word, '$');
  if (s) {
    char *c = strchr("$!?", s[1]);
    if (c) {
      ret = *c;
      *start = s;
      *end = s + 2;
    }
    else if (s[1] == '{') {
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = '{';
        *start = s;
        *end = e + 1;
      }
    }
  }
  prev = *end;
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
char *
expand(char const *word)
{
  char const *pos = word;
  char *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);

  while (c) {
    if (c == '!') {
        char bgpid_str[16];                                       // Buffer so background process ID is string
        snprintf(bgpid_str, sizeof(bgpid_str), "%d", get_last_bg_pid());
        build_str(bgpid_str, NULL);
    }
    else if (c == '$') {
        char bgpid_str[16];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());
        build_str(pid_str, NULL);                                 // Replace with smallsh process ID
    }
    else if (c == '?') {
        char exit_status_str[16];                                 // Buffer to hold the exit status
        snprintf(exit_status_str, sizeof(exit_status_str), "%d", get_last_exit_status());
        build_str(exit_status_str, NULL);
    }
    else if (c == '{') {                                        // Extract parameter name from ${parameter}
        char param_name[end - start - 2];
        strncpy(param_name, start + 2, end - start - 3);
        param_name[end - start - 3] = '\0';

        char *param_value = getenv(param_name);                   // Get the environment variable value
        if (!param_value) {
            param_value = "";                                       // Default to empty string if variable is unset
        }
        build_str(param_value, NULL);
    } 

    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
