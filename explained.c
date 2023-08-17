int main(int argc, char *argv[]) {
  FILE *input = stdin;                   // Initialize input to stdin
  char *input_fn = "(stdin)";            // Initialize input filename string

  if (argc == 2) {
    input_fn = argv[1];                  // If argument count is 2, use the provided input filename
    input = fopen(input_fn, "re");       // Attempt to open the file
    if (!input) err(1, "%s", input_fn);  // Handle file open error
  } else if (argc > 2) {
    errx(1, "too many arguments");       // Print an error message if too many arguments are provided
  }

  char *line = NULL;                     // Line buffer for getline
  size_t n = 0;                          // Size of the line buffer

  // Save original sigactions to restore for child process
  struct sigaction SIGINT_oldact;
  struct sigaction SIGTSTP_oldact;

  // Creating sigaction structs for SIGINT and SIGTSTP handling
  struct sigaction SIGINT_action = {0}, ignore_action = {0};
  ignore_action.sa_handler = SIG_IGN;                  // Ignore SIGSTP in interactive mode
  sigfillset(&ignore_action.sa_mask);
  ignore_action.sa_flags = 0;                          // No flags set
  SIGINT_action.sa_handler = sigint_handler;           // SIGINT uses sigint_handler
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;                          // No flags set

  for (;;) {

prompt:;

    // Handles the background processes
    int bgFlag = 0;
    pid_t bgChildPid;
    int bgChildStatus;
    int corrBgStatus;

    // Wait for and handle any finished background child processes
    while ((bgChildPid = waitpid(0, &bgChildStatus, WNOHANG | WUNTRACED)) > 0) {
      // Handle child process exit
      if (WIFEXITED(bgChildStatus)) {
        corrBgStatus = WEXITSTATUS(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Exit status %d.\n",
                (intmax_t)bgChildPid, corrBgStatus);
      }
      // Handle child process termination by a signal
      else if (WIFSIGNALED(bgChildStatus)) {
        corrBgStatus = WTERMSIG(bgChildStatus);
        fprintf(stderr, "Child process %jd done. Signaled %d.\n",
                (intmax_t)bgChildPid, corrBgStatus);
      }
      // Handle child process being stopped
      else if (WIFSTOPPED(bgChildStatus)) {
        fprintf(stderr, "Child process %jd stopped. Continuing.\n",
                (intmax_t)bgChildPid);
        // Send SIGCONT signal to that process to resume
        kill(bgChildPid, 18);
      }
    }

    if (input == stdin) {
      // Write code that we're in interactive mode
      char *prompt = getenv("PS1");                     // Only print prompt in interactive mode
      if (prompt == NULL) {
        prompt = "";
      }
      fprintf(stderr, "%s", prompt);

      // Setting handlers to handle signals
      sigaction(SIGTSTP, &ignore_action, &SIGTSTP_oldact);
      sigaction(SIGINT, &SIGINT_action, &SIGINT_oldact);
    }
    ssize_t line_len = getline(&line, &n, input);
    if (feof(input) != 0) {
      exit(0);  // Exit if end of file is reached
    }

    if (line_len < 0) {
      // Handle getline interruption (e.g., ^C)
      if (errno == EINTR) {
        clearerr(input);
        fprintf(stderr, "\n");
        goto prompt;
      }
      err(1, "%s", input_fn);  // Handle other errors
    }
    if (line_len == 1 && strcmp(line, "\n") == 0) {
      continue;  // Skip empty lines
    }

    size_t nwords = wordsplit(line);  // Split the line into words
    for (size_t i = 0; i < nwords; ++i) {
      char *exp_word = expand(words[i]);  // Expand special variables in each word
      free(words[i]);
      words[i] = exp_word;
    }

    if (input == stdin) {
      // Set SIGINT to ignore for interactive mode
      sigaction(SIGINT, &ignore_action, NULL);
    }

    // Run this command in the background
    if (strcmp(words[nwords - 1], "&") == 0) {
      nwords--;  // Remove the last word
      bgFlag = 1; // Set the background flag
    }

    // CD Function
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

    // Exit Function
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
    pid_t childPid = fork();  // Create a child process

    switch (childPid) {
      case -1:
        perror("fork() failed!");
        continue;
      case 0: {
        // Child process
        if (input == stdin) {
          // Restore original signal actions
          sigaction(SIGINT, &SIGINT_oldact, NULL);
          sigaction(SIGTSTP, &SIGTSTP_oldact, NULL);
        }

        // Parsing the words list
        char* args[nwords + 1];
        size_t args_len = 0;
        int childInput;
        int childOutput;

        for (size_t i = 0; i < nwords; ++i) {
          // Redirected input file
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
              }  // Skip both the < and the input file
              i++;
              continue;
            }
          }
          // Redirected output file truncated
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
          // Redirected output file appended
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
              }  // Skip both the >> and the output file
              i++;
              continue;
            }
          } else {
            args_len++;
            args[args_len] = words[i];
          }
        }
        args[args_len] = NULL;

        execvp(args[0], args);  // Execute the command in the child process
        perror("smallsh");      // Handle execution error
        _exit(EXIT_FAILURE);    // Exit the child process
        break;
      }
      default: {
        if (!bgFlag) {
          // Parent process: Wait for the child process to finish
          waitpid(childPid, &childStatus, WUNTRACED);

          int corrStatus;
          if (WIFSIGNALED(childStatus)) {
            corrStatus = WTERMSIG(childStatus) + 128;  // Set exit status to include signal number
          }
          else if (WIFSTOPPED(childStatus)) {
            // Child process was stopped, handle accordingly
            char* bgPid;
            asprintf(&bgPid, "%jd", (intmax_t) childPid);
            setenv("BG_PID", bgPid, 1);
            fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)childPid);
            // Send SIGCONT signal to the child process to continue
            kill(childPid, 18);  // Signal 18 = SIGCONT
            goto prompt;  // Go back to prompting
          }
          else if (WIFEXITED(childStatus)) {
            corrStatus = WEXITSTATUS(childStatus);  // Get the exit status of the child process
          }
          else {
            corrStatus = WTERMSIG(childStatus);  // Set exit status to signal number
          }

          // Convert exit status to string and set environment variable
          char* childStatusStr;
          asprintf(&childStatusStr, "%d", corrStatus);
          setenv("LATEST_FG", childStatusStr, 1);
        }
        else {
          // Background process handling: Wait for the child process to finish without blocking
          waitpid(childPid, &childStatus, WNOHANG | WUNTRACED);
          char* bgPid;
          asprintf(&bgPid, "%jd", (intmax_t)childPid);
          setenv("BG_PID", bgPid, 1);
        }
      }
    }  
  } 
}

#define MAX_WORDS 100   // Define a maximum number of words

char *words[MAX_WORDS] = {0};  // Initialize an array of word pointers

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;  // Initialize word length counter
  size_t wind = 0;  // Initialize word index counter

  char const *c = line;  // Pointer to iterate through the input line
  /* discard leading space */
  for (; *c && isspace(*c); ++c);  // Skip leading spaces

  for (; *c;) {
    if (wind == MAX_WORDS) break;  // Stop if the maximum number of words is reached
    /* read a word */
    if (*c == '#') break;  // Stop at comments ('#')
    for (; *c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;  // Skip backslash escapes
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));  // Reallocate memory for the word
      if (!tmp) err(1, "realloc");  // Handle memory allocation error
      words[wind] = tmp;
      words[wind][wlen++] = *c;  // Store character in the word
      words[wind][wlen] = '\0';  // Add null-terminator at the end of the word
    }
    ++wind;  // Move to the next word
    wlen = 0;  // Reset word length counter
    for (; *c && isspace(*c); ++c);  // Skip spaces after a word
  }
  return wind;  // Return the number of words parsed
}

/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char param_scan(char const *word, char const **start, char const **end) {
  static char const *prev;  // Store previous position for scanning

  if (!word) word = prev;

  char ret = 0;  // Initialize return value
  *start = 0;    // Initialize start pointer
  *end = 0;      // Initialize end pointer

  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');  // Find the next occurrence of '$'
    if (!s) break;       // Stop if no '$' found
    switch (s[1]) {      // Check the character after '$'
    case '$':
    case '!':
    case '?':
      ret = s[1];       // Set the return value to the character
      *start = s;       // Set the start pointer to the '$' character
      *end = s + 2;     // Set the end pointer to the character after '$'
      break;
    case '{':
      char *e = strchr(s + 2, '}');  // Find the closing '}'
      if (e) {
        ret = s[1];       // Set the return value to the character after '$'
        *start = s;       // Set the start pointer to the '$' character
        *end = e + 1;     // Set the end pointer to the closing '}'
      }
      break;
    }
  }
  prev = *end;  // Store the new position for the next scan
  return ret;   // Return the scanned character
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *build_str(char const *start, char const *end) {
  static size_t base_len = 0;  // Maintain length of the base string
  static char *base = 0;       // Pointer to the base string

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;       // Store the current base string
    base = NULL;            // Reset base pointer
    base_len = 0;           // Reset base length
    return ret;             // Return the old base string
  }

  /* Append [start, end) to base string
   * If end is NULL, append the whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);  // Calculate the length to append
  size_t newsize = sizeof *base * (base_len + n + 1);  // Calculate new size for the base string
  void *tmp = realloc(base, newsize);  // Reallocate memory for the base string
  if (!tmp) err(1, "realloc");          // Handle memory allocation error
  base = tmp;
  memcpy(base + base_len, start, n);   // Copy the data to the base string
  base_len += n;                        // Update the base length
  base[base_len] = '\0';                // Null-terminate the base string

  return base;                          // Return the updated base string
}

/* Expands all instances of $! $$ $? and ${param} in a string
 * Returns a newly allocated string that the caller must free
 */
char *expand(char const *word) {
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);  // Find parameter tokens in the word
  build_str(NULL, NULL);                   // Reset the base string
  build_str(pos, start);                   // Append data before the first parameter

  while (c) {
    if (c == '!') {
      char *bg_pid = getenv("BG_PID");     // Get background process ID
      if (!bg_pid) build_str("", NULL);    // Append empty string if not available
      else build_str(bg_pid, NULL);        // Append background process ID
    }
    else if (c == '$') {
      char *pid;
      asprintf(&pid, "%jd", (intmax_t)getpid());  // Convert process ID to string
      if (pid < 0) err(1, "getpid() or asprintf() failure");
      build_str(pid, NULL);                 // Append process ID
    }
    else if (c == '?') {
      char *fg_status = getenv("LATEST_FG");     // Get latest foreground process status
      if (!fg_status) build_str("0", NULL);      // Append '0' if not available
      else build_str(fg_status, NULL);           // Append foreground status
    }
    else if (c == '{') {
      // Extract parameter name from the word
      size_t length = end - 1 - (start + 2);
      char temp[length + 1];
      memcpy(temp, start + 2, length);
      temp[length] = '\0';  // Null-terminate the parameter name

      char *value = getenv(temp);  // Get parameter value from environment
      if (!value) build_str("", NULL);  // Append empty string if not available
      else build_str(value, NULL);       // Append parameter value
    }
    pos = end;                 // Move position to the end of the parameter
    c = param_scan(pos, &start, &end);  // Find the next parameter token
    build_str(pos, start);      // Append data between parameters
  }
  return build_str(start, NULL);  // Append data after the last parameter and return the expanded string
}

/*
 * SIGINT Handler that doesn't do anything
 */
void sigint_handler(int sig) {
  return;
}
