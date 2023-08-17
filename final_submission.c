/*
Sources Used: 
Explorations, Skeleton Code on ED, OSU Tutoring Servcies for help debugging/reorganizing code
https://stackoverflow.com/questions/19461744/how-to-make-parent-wait-for-all-child-processes-to-finish
https://stackoverflow.com/questions/42546478/scanf-ignoring-new-line-character-when-scanning-from-input-file
https://stackoverflow.com/questions/2245193/why-does-open-create-my-file-with-the-wrong-permissions
https://brennan.io/2015/01/16/write-a-shell-in-c/
https://blog.ehoneahobed.com/building-a-simple-shell-in-c-part-1
https://blog.ehoneahobed.com/building-a-simple-shell-in-c-part-2
https://www.geeksforgeeks.org/making-linux-shell-c/
https://www.geeksforgeeks.org/signals-c-language/
https://linuxhint.com/signal_handlers_c_programming_language/
*/

#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <inttypes.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

/*
 * SIGINT Handler that doesn't do anything
 * ( Allows getline() to be interrupted without errors)
 */
void sigint_handler(int sig);

int main(int argc, char *argv[])
{
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

  /* Save original sigactions to restore for child process */
  struct sigaction SIGINT_oldact;
  struct sigaction SIGTSTP_oldact;

  /* Creating sigaction structs for SIGINT and SIGTSTP handling */
  struct sigaction SIGINT_action = {0}, ignore_action = {0};
  ignore_action.sa_handler = SIG_IGN;                         // ignore SIGSTP in interactive mode
  sigfillset(&ignore_action.sa_mask);
  ignore_action.sa_flags = 0;                                 // No flags set
  SIGINT_action.sa_handler = sigint_handler;                  // SIGINT uses sigint_handler
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;                                 // No flags set

  for (;;) {

prompt:;

    /* Handles the Background processes */
    int bgFlag = 0;
    pid_t bgChildPid;
    int bgChildStatus;
    int corrBgStatus;
    while ((bgChildPid = waitpid(0, &bgChildStatus, WNOHANG | WUNTRACED)) > 0) {
      if (WIFEXITED(bgChildStatus)) {
        corrBgStatus = WEXITSTATUS(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Exit status %d.\n",
            (intmax_t)bgChildPid, corrBgStatus);
      }
      else if (WIFSIGNALED(bgChildStatus)) {
        corrBgStatus = WTERMSIG(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Signaled %d.\n",
            (intmax_t)bgChildPid, corrBgStatus);
      }
      else if (WIFSTOPPED(bgChildStatus)) {                                // Send SIGCONT signal to that process
          fprintf(stderr, "Child process %jd stopped. Continuing.\n",
              (intmax_t)bgChildPid);
          kill(bgChildPid, 18);
      }
    }

    if (input == stdin) {
      /* Write code that we're interactive */
      char *prompt = getenv("PS1");                                       // Only print prompt in interactive mode
      if (prompt == NULL) {
        prompt = "";
      }
      fprintf(stderr, "%s", prompt);

      /* Setting handlers to handle signals */
      sigaction(SIGTSTP, &ignore_action, &SIGTSTP_oldact);
      sigaction(SIGINT, &SIGINT_action, &SIGINT_oldact);
    }
    ssize_t line_len = getline(&line, &n, input);
    if (feof(input) != 0) {
      exit(0);
    }

    if (line_len < 0) {                                                   // got interrupted with ^C
      if (errno == EINTR) {
        clearerr(input);
        fprintf(stderr, "\n");
        goto prompt;
      }
      err(1, "%s", input_fn);
    }
    if (line_len == 1 && strcmp(line, "\n") == 0) {
      continue;
    }

    size_t nwords = wordsplit(line);
    for (size_t i = 0; i < nwords; ++i) {
      char *exp_word = expand(words[i]);
      free(words[i]);
      words[i] = exp_word;
    }

    if (input == stdin) {                                            // set SIGINT to ignore for interactive mode
      sigaction(SIGINT, &ignore_action, NULL);
    }

    /* Run this command in background */
    if (strcmp(words[nwords - 1], "&") == 0) {
      nwords--;                                                     // Remove the last word
      bgFlag = 1;
    }

    /* CD Function */
    if (strcmp(words[0], "cd") == 0) {
      if (nwords > 1) {
        if (chdir(words[1]) == -1) {
          fprintf(stderr, "directory not found\n");
        }
      }
      else if (chdir(getenv("HOME")) == -1) {
        fprintf(stderr, "directory not found\n");
      }
      continue;
    }

    /* Exit Function */
    if (strcmp(words[0], "exit") == 0) {
      if (nwords > 2) {
        fprintf(stderr, "too many arguments\n");
        goto prompt;
      }
      else if (nwords == 2) {
        char *next;
        long value;
        value = strtol(words[1], &next, 10);
        if ((next == words[1]) || (*next != '\0')) {
          fprintf(stderr, "that is not a number\n");
          goto prompt;
        }
        else {
          exit(value);
        }
      }
      else {
        char *exp_word = expand("$?");
        for (;;) exit(atoi(exp_word));
      }
    }

    int childStatus;
    pid_t childPid = fork();

    switch(childPid) {
      case -1:
        perror("fork() failed!");
        continue;
      case 0: {                                                            // child process
        /* change the sigactions */
        if (input == stdin) {
          sigaction(SIGINT, &SIGINT_oldact, NULL);
          sigaction(SIGTSTP, &SIGTSTP_oldact, NULL);
        }

        /* Parsing the words list */
        char* args[nwords + 1];
        size_t args_len = 0;
        int childInput;
        int childOutput;
        for (size_t i = 0; i < nwords; ++i) {
          /* Redirected input file */
          if (strcmp(words[i], "<") == 0) {
            if (strcmp(words[i + 1], "")) {
              childInput = open(words[i + 1], O_RDONLY | O_CLOEXEC);
              if (childInput == -1) {
                perror("failed to open file");
                exit(1);
              }
              int result = dup2(childInput, 0);
              if (result == -1) {
                perror("failed to assign new input file");
                exit(2);
              }                                                           // skip both the < and the input file
              i++;
              continue;
            }
          }
          /* Redirected output file truncated */
          else if (strcmp(words[i], ">") == 0) {
            if (strcmp(words[i + 1], "")) {
              childOutput = open(words[i + 1],
                  O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0777);
              if (childOutput == -1) {
                perror("failed to create new or open old output file");
                exit(1);
              }
              int result = dup2(childOutput, 1);
              if (result == -1) {
                perror("failed to assign new output file");
                exit(2);
              }
              i++;
              continue;
            }
          }

          /* Redirected output file appended */
          else if (strcmp(words[i], ">>") == 0) {
            if (strcmp(words[i + 1], "")) {
              childOutput = open(words[i + 1],
                  O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0777);
              if (childOutput == -1) {
                perror("failed to create new or open old output file");
                exit(1);
              }
              int result = dup2(childOutput, 1);
              if (result == -1) {
                perror("failed to assign new output file");
                exit(2);
              }                                                           // skip both the >> and the output file
              i++;
              continue;
            }
          }

          else {
            args_len++;
            args[i] = words[i];
          }
        }
        args[args_len] = NULL;

        execvp(args[0], args);
        perror("smallsh");
        _exit(EXIT_FAILURE);
        break;
      }
      default: {
        if (!bgFlag) {
          waitpid(childPid, &childStatus, WUNTRACED);
          int corrStatus;
          if (WIFSIGNALED(childStatus)) {
            corrStatus = WTERMSIG(childStatus) + 128;
          }
          else if (WIFSTOPPED(childStatus)) {                                  // Stop signal was given
            char* bgPid;
            asprintf(&bgPid, "%jd", (intmax_t) childPid);
            setenv("BG_PID", bgPid, 1);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)childPid);
            kill(childPid, 18);                                               // Signal 18 = SIGCONT
            goto prompt;
          }
          else if (WIFEXITED(childStatus)) corrStatus = WEXITSTATUS(childStatus);
          else corrStatus = WTERMSIG(childStatus);
          char* childStatusStr;
          asprintf(&childStatusStr, "%d", corrStatus);
          setenv("LATEST_FG", childStatusStr, 1);

        }
        else {
          waitpid(childPid, &childStatus, WNOHANG | WUNTRACED);
          char* bgPid;
          asprintf(&bgPid, "%jd", (intmax_t)childPid);
          setenv("BG_PID", bgPid, 1);
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
  /* discard leading space */
  for (;*c && isspace(*c); ++c); 

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
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;

  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
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
  char const *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!') {
      char* bg_pid = getenv("BG_PID");
      if (!bg_pid) build_str("", NULL);
      else build_str(bg_pid, NULL);
    }
    else if (c == '$') {
      char* pid;
      asprintf(&pid, "%jd", (intmax_t)getpid());
      if (pid < 0) err(1, "getpid() or asprintf() failure");
      build_str(pid, NULL);
    }
    else if (c == '?') {
      char* fg_status = getenv("LATEST_FG");
      if (!fg_status) build_str("0", NULL);
      else build_str(fg_status, NULL);
    }
    else if (c == '{') {                                        // using end -1 and start + 2 as the pointers for the parameter
      size_t length = end - 1 - (start + 2);
      char temp[length + 1];
      memcpy(temp, start + 2, length);
      temp[length] = '\0';                                      // add null char to end string

      char* value = getenv(temp);
      if (!value) build_str("", NULL);
      else build_str(value, NULL);
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}

/*
 * SIGINT Handler that doesn't do anything
 */

void
sigint_handler(int sig) {
  return;
}
