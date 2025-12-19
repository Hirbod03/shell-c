/*
 * ===============================================================================
 * HIRBOD'S SHELL - MAIN IMPLEMENTATION
 * ===============================================================================
 * * This program implements a POSIX-compliant shell in C.
 * * Key Features:
 * 1. REPL (Read-Eval-Print Loop): Continuously accepts user input.
 * 2. Raw Mode Input: Disables standard terminal line buffering to handle 
 * TAB completion and Backspace manually.
 * 3. Tokenizer: Custom parsing logic to handle spaces, quotes (' and "), and escapes (\).
 * 4. Built-ins: cd, echo, exit, type, pwd, help.
 * 5. External Commands: Uses fork() and exec() to run system programs (e.g., ls, grep).
 * 6. Redirection: Supports >, >>, 2>, 2>> by manipulating File Descriptors.
 * * ======================================================================================
 */

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>  // Required for raw mode (terminal settings)
#include <dirent.h>

#define MAX_PATH_ENTRIES 100
#define MAX_ARGS 100

// ================================================================================
// FORWARD DECLARATIONS
// ================================================================================
// C requires functions to be declared before they are used if they are defined later.
int shell_cd(int argc, char *argv[]);
int shell_pwd(int argc, char *argv[]);
int shell_exit(int argc, char *argv[]);
int shell_echo(int argc, char *argv[]);
int shell_help(int argc, char *argv[]);
int shell_type(int argc, char *argv[]);
int num_builtins();
int parse_command(const char *line, char *argv[], int max_args);
int setup_redirect_fd(const char *path, int target_fd, int should_exit_on_error, int append_mode);
int save_and_redirect_fd(const char *path, int target_fd, int append_mode);
int read_input_line(char *buffer, size_t size);
char* ext_check(char *program_name);
const char* complete_builtin(const char *prefix);
char* complete_executable(const char *prefix);
void parse_path(char *path_string);
void execute_external_program(char *full_path, int argc, char *argv[], char *redirect_out, char *redirect_err, int redirect_out_append, int redirect_err_append);
void restore_fd(int saved_fd, int target_fd);

// ================================================================================
// TERMINAL MODE HANDLING (Raw vs Canonical)
// ================================================================================
// Standard terminals operate in "Canonical Mode" (ICANON), meaning input is buffered
// line-by-line. The program doesn't see input until the user hits Enter.
// To support TAB completion, we need "Raw Mode", where we receive every keypress immediately.

struct termios original_termios; // Store original settings to restore them on exit

void disable_raw_mode() {
  // Restore the terminal to its original state (Canonical mode) so other programs behave normally.
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

void enable_raw_mode() {
  // Get current terminal attributes
  tcgetattr(STDIN_FILENO, &original_termios);
  
  // Register disable_raw_mode to run automatically when the program exits (even on crash/error)
  atexit(disable_raw_mode);
  
  struct termios raw = original_termios;
  
  // FLags:
  // ICANON: Canonical mode. We turn this OFF (using & ~) to read byte-by-byte.
  // ECHO:   Automatic printing. We turn this OFF because we will manually 'printf' characters
  //         back to the screen (allows us to handle backspace visually).
  raw.c_lflag &= ~(ICANON | ECHO);
  
  // Apply the new settings
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// ================================================================================
// BUILT-IN COMMAND REGISTRY
// ================================================================================
// We map string command names (like "cd") to actual C functions.

// function signature typedef: A function that takes argc/argv and returns an int.
typedef int (*builtin_func)(int argc, char *argv[]);

struct builtin {
  char *name;
  builtin_func func;
};

// Dispatch table: Used to lookup commands O(N) style.
struct builtin builtins[] = {
  {"exit", shell_exit},
  {"echo", shell_echo},
  {"help", shell_help},
  {"type", shell_type},
  {"pwd", shell_pwd},
  {"cd", shell_cd},
};

// Global cache for directories found in the PATH environment variable
char *path_dirs[MAX_PATH_ENTRIES];
int path_count = 0;

// ================================================================================
// BUILT-IN IMPLEMENTATIONS
// ================================================================================

int shell_exit(int argc, char *argv[]) {
  // exit(0) terminates the C program immediately. 
  // 'atexit' (registered earlier) will trigger here to fix terminal modes.
  exit(0);
  return 0; 
}

int shell_echo(int argc, char *argv[]) {
  // Simple loop starting at 1 (skipping the command name itself)
  for (int i = 1; i < argc; i++) {
    printf("%s", argv[i]);
    if (i < argc - 1) {
      printf(" "); // Add space between args, but not after the last one
    }
  }
  printf("\n");
  return 1; 
}

int shell_type(int argc, char *argv[]) {
  if (argc < 2) {
    printf("type: expected argument\n");
    return 1;
  }

  // Iterate over all arguments provided to 'type'
  for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
    char *token = argv[arg_idx];
    int found = 0;

    // Check if it's a built-in
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(token, builtins[i].name) == 0) {
        printf("%s is a shell builtin\n", token);
        found = 1;
        break;
      }
    }

    // If not built-in, check if it's an external executable in PATH
    if (!found) {
      char *full_path = ext_check(token);
      if (full_path != NULL) {
        printf("%s is %s\n", token, full_path);
        found = 1;
      }
      if (!found) {
        printf("%s: not found\n", token);
      }
    }
  }
  return 1; 
}

int shell_help(int argc, char *argv[]) {
  printf("Hirbod's Shell. Built-ins available:\n");
  printf("  cd\n  help\n  exit\n  echo\n");
  return 1;
}

int shell_pwd(int argc, char *argv[]){
  char cwd[1024];
  // getcwd fills the array with the current working directory path
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
    return 0;
  } 
  else {
    perror("getcwd"); // Prints standard error message based on errno
    return -1;
  }
}

int shell_cd(int argc, char *argv[]){
  if (argc < 2) {
    fprintf(stderr, "cd: missing argument\n");
    return 1;
  }

  char *arg = argv[1];
  char *target_dir = arg;

  // Handle Home Directory expansion (~)
  if (arg[0] == '~') {
    char *home = getenv("HOME");
    if (home == NULL) {
      fprintf(stderr, "cd: HOME not set\n");
      return 1;
    }

    if (arg[1] == '\0') {
      // Logic for just "~" -> go to home
      target_dir = home;
    } else if (arg[1] == '/') {
      // Logic for "~/Documents" -> combine home + rest of path
      // 'static' means this buffer persists in memory; safer for local large buffers
      static char expanded_path[1024]; 
      snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, arg + 1);
      target_dir = expanded_path;
    }
  }

  // chdir is the system call to change the process's working directory
  if (chdir(target_dir) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", arg);
  }

  return 1; 
}

int num_builtins() {
  return sizeof(builtins) / sizeof(struct builtin);
}

// ================================================================================
// PATH PARSING & EXECUTABLE FINDING
// ================================================================================

/*
 * parses the PATH env var (e.g., "/usr/bin:/bin:/usr/local/bin") 
 * and splits it into an array of strings for easy lookup later.
 */
void parse_path(char *path_string) {
    if (path_string == NULL) return;
    
    // Create a duplicate because 'strtok' modifies the string it parses
    char *path_copy = strdup(path_string);
    if (!path_copy) {
        perror("strdup");
        return;
    }
    
    // Split by colon delimiter
    char *dir = strtok(path_copy, ":");
    while (dir != NULL && path_count < MAX_PATH_ENTRIES) {
        path_dirs[path_count] = strdup(dir); // Store a copy of each path segment 
        path_count++;
        dir = strtok(NULL, ":"); // Continue to next token
    }
    
    free(path_copy); // Clean up the temporary copy
}

/*
 * Checks if a program exists in any of the directories stored in 'path_dirs'.
 * Returns the full path string if found and executable, NULL otherwise.
 */
char* ext_check(char *program_name){
  for (int i = 0; i < path_count; i++){
    static char full_path[1024]; // Static buffer avoids repeated stack allocation
    
    // Construct /usr/bin/ls
    snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], program_name);
    
    // access() checks file accessibility. F_OK = Exists, X_OK = Executable permissions.
    if (access(full_path, F_OK) == 0 && access(full_path, X_OK) == 0){
      return full_path;
    }
  }
  return NULL;
}

// Helper for Tab Completion
int compare_strings(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}

int get_completions(const char *prefix, char ***out_matches) {
    size_t prefix_len = strlen(prefix);
    if (prefix_len == 0) return 0;

    int capacity = 10;
    int count = 0;
    char **matches = malloc(capacity * sizeof(char *));

    // Builtins
    for (int i = 0; i < num_builtins(); i++) {
        if (strncmp(builtins[i].name, prefix, prefix_len) == 0) {
            if (count >= capacity) {
                capacity *= 2;
                matches = realloc(matches, capacity * sizeof(char *));
            }
            matches[count++] = strdup(builtins[i].name);
        }
    }

    // Executables
    for (int i = 0; i < path_count; i++) {
        DIR *d = opendir(path_dirs[i]);
        if (d == NULL) continue;

        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (strncmp(dir->d_name, prefix, prefix_len) == 0) {
                char full_path[1024];
                snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], dir->d_name);
                
                if (access(full_path, X_OK) == 0) {
                    // Check duplicates
                    int exists = 0;
                    for (int j = 0; j < count; j++) {
                        if (strcmp(matches[j], dir->d_name) == 0) {
                            exists = 1;
                            break;
                        }
                    }
                    if (!exists) {
                        if (count >= capacity) {
                            capacity *= 2;
                            matches = realloc(matches, capacity * sizeof(char *));
                        }
                        matches[count++] = strdup(dir->d_name);
                    }
                }
            }
        }
        closedir(d);
    }

    qsort(matches, count, sizeof(char *), compare_strings);
    *out_matches = matches;
    return count;
}

// ================================================================================
// FILE DESCRIPTOR MANIPULATION (REDIRECTION)
// ================================================================================

/*
 * Logic for replacing a standard FD (stdin/stdout/stderr) with a file.
 * Returns -1 on failure, 0 on success.
 */
int setup_redirect_fd(const char *path, int target_fd, int should_exit_on_error, int append_mode) {
  // O_WRONLY: Write only
  // O_CREAT: Create file if missing
  // O_APPEND vs O_TRUNC: Append adds to end, Truncate wipes file first (>> vs >)
  int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
  
  // 0666 gives read/write permissions to user/group/others
  int fd = open(path, flags, 0666);
  if (fd < 0) {
    perror("open");
    if (should_exit_on_error) exit(1);
    return -1;
  }
  
  // dup2(old, new) closes 'new' if open, and makes 'new' point to 'old' file table entry.
  // Effectively: "Make StdOut point to this file we just opened."
  if (dup2(fd, target_fd) < 0) {
    perror("dup2");
    close(fd);
    if (should_exit_on_error) exit(1);
    return -1;
  }
  
  close(fd); // We can close the original file handle now, the duplicated one remains.
  return 0;
}

/*
 * Saves the current state of a file descriptor (like stdout) before we redirect it,
 * so we can restore it after the command finishes.
 */
int save_and_redirect_fd(const char *path, int target_fd, int append_mode) {
  int saved_fd = dup(target_fd); // Create a backup of stdout (usually fd 1)
  if (saved_fd < 0) {
    perror("dup");
    return -1;
  }
  if (setup_redirect_fd(path, target_fd, 0, append_mode) < 0) {
    close(saved_fd);
    return -1;
  }
  return saved_fd;
}

void restore_fd(int saved_fd, int target_fd) {
  if (saved_fd != -1) {
    dup2(saved_fd, target_fd); // Restore stdout to point back to the terminal
    close(saved_fd); // Close the backup
  }
}

// ================================================================================
// EXTERNAL PROGRAM EXECUTION
// ================================================================================

void execute_external_program(char *full_path, int argc, char *argv[], char *redirect_out, char *redirect_err, int redirect_out_append, int redirect_err_append) {
    argv[argc] = NULL; // execv requires the array to be null-terminated

    // Fork creates a clone of the current process.
    // Parent process gets the child's PID. Child process gets 0.
    pid_t pid = fork();
    
    if (pid < 0) {
        perror("fork");
        return;
    } else if (pid == 0) {
      // === CHILD PROCESS ===
      // This is where we run the new program.
      
      // Handle redirections *inside* the child so the parent shell isn't affected
      if (redirect_out != NULL) {
        setup_redirect_fd(redirect_out, STDOUT_FILENO, 1, redirect_out_append);
      }
      if (redirect_err != NULL) {
        setup_redirect_fd(redirect_err, STDERR_FILENO, 1, redirect_err_append);
      }
      
      // execv replaces the current process memory with the new program.
      // If successful, this function never returns.
      execv(full_path, argv);
        
      // If we are here, execv failed (e.g., permission denied)
      perror("execv");
      exit(1);
    } else {
        // === PARENT PROCESS ===
        // Wait for the child (pid) to finish so the prompt doesn't appear prematurely.
        int status;
        waitpid(pid, &status, 0);
    }
}

// ================================================================================
// PARSING LOGIC
// ================================================================================

/*
 * Manual tokenizer that splits a string into arguments (argv).
 * Handles:
 * - Single quotes ('foo bar' is one arg)
 * - Double quotes ("foo bar" is one arg)
 * - Backslash escaping (\)
 */
int parse_command(const char *line, char *argv[], int max_args) {
  int argc = 0;
  int in_single_quote = 0;
  int in_double_quote = 0;
  char token[1024]; // Temporary buffer for building the current argument
  int len = 0;

  for (const char *p = line;; p++) {
    char c = *p;

    // Toggle single quote state (unless inside double quotes)
    if (!in_double_quote && c == '\'') {
      in_single_quote = !in_single_quote;
      continue; // Skip adding the quote char itself to the token
    }

    // Toggle double quote state (unless inside single quotes)
    if (!in_single_quote && c == '"') {
      in_double_quote = !in_double_quote;
      continue;
    }

    // Check for delimiter (Space or Null terminator)
    // Only treat space as delimiter if we are NOT inside quotes
    if ((c == '\0') || (!in_single_quote && !in_double_quote && isspace((unsigned char)c))) {
      if (len > 0) {
        token[len] = '\0'; // Terminate the string
        if (argc < max_args - 1) {
          argv[argc++] = strdup(token); // Allocate memory for the arg
        }
        len = 0; // Reset token builder
      }
      if (c == '\0') break; // End of line
      continue;
    }

    // Handle escapes inside double quotes (allows \" and \\)
    if (in_double_quote && c == '\\') {
      char next = *(++p);
      if (next == '\0') break; 
      if (next == '"' || next == '\\') {
        c = next; 
      } else {
        // If not a special escape, keep the backslash literal
        if (len < (int)sizeof(token) - 1) {
          token[len++] = '\\';
        }
        c = next;
      }
    }

    // Handle escapes outside quotes (generic escaping)
    if (!in_single_quote && !in_double_quote && c == '\\') {
      char next = *(++p);
      if (next == '\0') break;
      c = next;
    }

    // Append char to current token
    if (len < (int)sizeof(token) - 1) {
      token[len++] = c;
    }
  }

  argv[argc] = NULL;
  return argc;
}

// ================================================================================
// RAW INPUT HANDLER
// ================================================================================

/*
 * Finds the longest common prefix among a list of strings.
 * Returns a newly allocated string that must be freed by the caller.
 * 
 * Logic:
 * 1. Start with the first string as the candidate prefix.
 * 2. Iterate through the rest of the strings.
 * 3. For each string, shorten the candidate prefix until it matches the start of the string.
 */
char *find_lcp(char **matches, int count) {
    if (count == 0) return NULL;
    if (count == 1) return strdup(matches[0]);
    
    char *prefix = strdup(matches[0]);
    int len = strlen(prefix);
    
    for (int i = 1; i < count; i++) {
        int j = 0;
        // Compare characters until mismatch or end of string
        while (j < len && matches[i][j] != '\0' && prefix[j] == matches[i][j]) {
            j++;
        }
        len = j; // Update length of common prefix
        prefix[len] = '\0'; // Terminate string at new length
    }
    return prefix;
}

/* * Reads input byte-by-byte to handle specialized keys (TAB, Backspace).
 * Returns 1 if command entered, 0 on EOF (Ctrl+D).
 */
int read_input_line(char *buffer, size_t size) {
  size_t len = 0;
  int tab_count = 0; // Track consecutive tabs
  memset(buffer, 0, size);

  while (1) {
    char c;
    // read() from STDIN_FILENO returns 1 byte. In Raw Mode, this returns immediately.
    if (read(STDIN_FILENO, &c, 1) != 1) break; 

    // Handle Ctrl+D (EOF - Value 4)
    if (c == 4) { 
        if (len == 0) return 0; // If line is empty, signal exit
        break; 
    }
    
    // Handle Enter/Return (Carriage Return or Newline)
    if (c == '\n' || c == '\r') {
      printf("\n"); // Move to new line visually for the user
      buffer[len] = '\0';
      return 1;
    }

    // === TAB COMPLETION ===
    if (c == '\t') {
      // 1. Isolate the last word the user was typing
      int start = 0;
      while (start < len && (buffer[start] == ' ' || buffer[start] == '\t')) {
        start++;
      }
      
      char prefix[128];
      int i = 0;
      while (start + i < len && !isspace((unsigned char)buffer[start + i]) && i < (int)sizeof(prefix) - 1) {
        prefix[i] = buffer[start + i];
        i++;
      }
      prefix[i] = '\0';

      char **matches = NULL;
      int match_count = get_completions(prefix, &matches);

      if (match_count == 0) {
          printf("\a"); // Bell sound
          fflush(stdout);
          tab_count = 0;
      } else if (match_count == 1) {
          // Autocomplete
          size_t prefix_len = strlen(prefix);
          size_t comp_len = strlen(matches[0]);
          
          if (len + (comp_len - prefix_len) + 1 < size) {
              printf("%s ", matches[0] + prefix_len); // Visual update
              fflush(stdout);

              // Buffer update
              strcpy(buffer + len, matches[0] + prefix_len);
              len += (comp_len - prefix_len);
              buffer[len++] = ' ';
              buffer[len] = '\0';
          }
          tab_count = 0;
      } else {
          // Multiple matches found
          // Calculate the Longest Common Prefix (LCP) of all matches
          char *lcp = find_lcp(matches, match_count);
          size_t prefix_len = strlen(prefix);
          size_t lcp_len = strlen(lcp);

          // If the LCP is longer than what the user has typed so far,
          // we can auto-complete up to the LCP.
          if (lcp_len > prefix_len) {
              if (len + (lcp_len - prefix_len) < size) {
                  printf("%s", lcp + prefix_len); // Print only the new characters
                  fflush(stdout);
                  
                  // Append new characters to the buffer
                  strcpy(buffer + len, lcp + prefix_len);
                  len += (lcp_len - prefix_len);
                  buffer[len] = '\0';
              }
              tab_count = 0; // Reset tab count so next tab triggers list
          } else {
              // If we can't extend the prefix (LCP == current input),
              // behave like standard shell:
              // 1st Tab: Bell
              // 2nd Tab: List all matches
              if (tab_count == 0) {
                  printf("\a"); // Bell sound
                  fflush(stdout);
                  tab_count = 1;
              } else {
                  printf("\n");
                  for (int j = 0; j < match_count; j++) {
                      printf("%s  ", matches[j]);
                  }
                  printf("\n");
                  printf("$ %s", buffer); // Reprint prompt and buffer
                  fflush(stdout);
                  tab_count = 0;
              }
          }
          free(lcp);
      }

      // Free matches
      for (int j = 0; j < match_count; j++) {
          free(matches[j]);
      }
      free(matches);
      
      continue;
    }

    // Reset tab count for any other key
    tab_count = 0;

    // === BACKSPACE HANDLING ===
    // 127 is Standard DEL, \b is used in some terminals.
    if (c == 127 || c == '\b') {
      if (len > 0) {
        len--;
        buffer[len] = '\0';
        // Visual erase trick: Move cursor back (\b), print Space ( ), move cursor back again (\b)
        printf("\b \b");
        fflush(stdout);
      }
      continue;
    }

    // Handle Normal Characters
    if (len < size - 1) {
      if (iscntrl(c)) continue; // Ignore other weird control characters
      
      buffer[len++] = c;
      printf("%c", c); // We must manually ECHO the character back to the user
      fflush(stdout);
    }
  }
  return 1;
}

// ================================================================================
// PIPELINE & REDIRECTION HELPERS
// ================================================================================

/*
 * Scans the argument list for redirection operators (>, >>, 2>, 2>>).
 * Extracts the target filename and sets the appropriate flags.
 * Removes the operator and filename from argv by shifting remaining arguments.
 */
void parse_redirections(int *argc_ptr, char *argv[], char **redirect_out, char **redirect_err, int *redirect_out_append, int *redirect_err_append) {
    int argc = *argc_ptr;
    *redirect_out = NULL;
    *redirect_err = NULL;
    *redirect_out_append = 0;
    *redirect_err_append = 0;
    
    for (int i = 0; i < argc; i++) {
      int remove_count = 0;
      // Check for Standard Output Redirection
      if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "1>") == 0) {
        if (i + 1 < argc) {
          *redirect_out = argv[i + 1];
          *redirect_out_append = 0; // Truncate mode
          remove_count = 2;
        }
      } 
      // Check for Append Standard Output
      else if (strcmp(argv[i], ">>") == 0 || strcmp(argv[i], "1>>") == 0) {
        if (i + 1 < argc) {
          *redirect_out = argv[i + 1];
          *redirect_out_append = 1; // Append mode
          remove_count = 2;
        }
      } 
      // Check for Standard Error Redirection
      else if (strcmp(argv[i], "2>") == 0) {
        if (i + 1 < argc) {
          *redirect_err = argv[i + 1];
          *redirect_err_append = 0;
          remove_count = 2;
        }
      } 
      // Check for Append Standard Error
      else if (strcmp(argv[i], "2>>") == 0) {
        if (i + 1 < argc) {
          *redirect_err = argv[i + 1];
          *redirect_err_append = 1;
          remove_count = 2;
        }
      }

      // If a redirection was found, remove the operator and filename from argv
      if (remove_count > 0) {
          free(argv[i]); // Free the operator string
          // Shift remaining arguments left to fill the gap
          for (int j = i; j + remove_count <= argc; j++) {
              argv[j] = argv[j + remove_count];
          }
          argc -= remove_count;
          i -= 1; // Decrement i to re-check the new argument at this position
      }
    }
    *argc_ptr = argc; // Update the caller's argc
}

/*
 * Executes two commands connected by a pipe (|).
 * 1. Parses redirections for both commands.
 * 2. Creates a pipe.
 * 3. Forks two child processes.
 * 4. Connects stdout of Child 1 to the write end of the pipe.
 * 5. Connects stdin of Child 2 to the read end of the pipe.
 */
void run_pipeline(int argc1, char *argv1[], int argc2, char *argv2[]) {
    char *out1=NULL, *err1=NULL, *out2=NULL, *err2=NULL;
    int out1_app=0, err1_app=0, out2_app=0, err2_app=0;
    
    // Parse redirections for both commands independently
    parse_redirections(&argc1, argv1, &out1, &err1, &out1_app, &err1_app);
    parse_redirections(&argc2, argv2, &out2, &err2, &out2_app, &err2_app);

    // Resolve full paths for executables
    char *path1_static = ext_check(argv1[0]);
    char *path1 = path1_static ? strdup(path1_static) : NULL;
    
    char *path2_static = ext_check(argv2[0]);
    char *path2 = path2_static ? strdup(path2_static) : NULL;
    
    // Error handling if commands are not found
    if (!path1) { 
        printf("%s: command not found\n", argv1[0]); 
        if(path2) free(path2); 
        if(out1) free(out1); if(err1) free(err1);
        if(out2) free(out2); if(err2) free(err2);
        return; 
    }
    if (!path2) { 
        printf("%s: command not found\n", argv2[0]); 
        if(path1) free(path1); 
        if(out1) free(out1); if(err1) free(err1);
        if(out2) free(out2); if(err2) free(err2);
        return; 
    }

    // Create the pipe
    // pipefd[0] is for reading, pipefd[1] is for writing
    int pipefd[2];
    if (pipe(pipefd) < 0) { 
        perror("pipe"); 
        free(path1); free(path2); 
        if(out1) free(out1); if(err1) free(err1);
        if(out2) free(out2); if(err2) free(err2);
        return; 
    }

    // Fork first child (Command 1)
    pid_t pid1 = fork();
    if (pid1 == 0) {
        // === CHILD 1 ===
        close(pipefd[0]); // Close unused read end
        dup2(pipefd[1], STDOUT_FILENO); // Redirect stdout to pipe write end
        close(pipefd[1]); // Close original write end after dup
        
        // Handle other redirections (stderr)
        if (err1) setup_redirect_fd(err1, STDERR_FILENO, 1, err1_app);
        
        execv(path1, argv1);
        perror("execv");
        exit(1);
    }

    // Fork second child (Command 2)
    pid_t pid2 = fork();
    if (pid2 == 0) {
        // === CHILD 2 ===
        close(pipefd[1]); // Close unused write end
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to pipe read end
        close(pipefd[0]); // Close original read end after dup

        // Handle other redirections (stdout, stderr)
        if (out2) setup_redirect_fd(out2, STDOUT_FILENO, 1, out2_app);
        if (err2) setup_redirect_fd(err2, STDERR_FILENO, 1, err2_app);

        execv(path2, argv2);
        perror("execv");
        exit(1);
    }

    // === PARENT PROCESS ===
    // Close both ends of the pipe in the parent
    // If we don't close them, the children might hang waiting for EOF
    close(pipefd[0]);
    close(pipefd[1]);
    
    // Wait for both children to finish
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);
    
    // Cleanup
    free(path1);
    free(path2);
    if(out1) free(out1); if(err1) free(err1);
    if(out2) free(out2); if(err2) free(err2);
}

// ================================================================================
// MAIN ENTRY POINT
// ================================================================================

int main(int argc, char *argv[]) {
  // Check if we are in an interactive terminal. If so, enable custom raw mode.
  if (isatty(STDIN_FILENO)) {
      enable_raw_mode();
  }
  
  // Disable output buffering so prompts appear immediately
  setbuf(stdout, NULL);

  // Load PATH environment variable
  char *shell_path = getenv("PATH");
  if (shell_path == NULL) {
    fprintf(stderr, "Warning: PATH not set\n");
  } else {
    parse_path(shell_path); 
  }

  char command[1024];

  // MAIN LOOP
  while (1) {
    printf("$ ");
    
    // Get input (Raw mode aware)
    if (read_input_line(command, sizeof(command)) == 0) break;
    
    char *argv[MAX_ARGS];
    int argc = parse_command(command, argv, MAX_ARGS);

    if (argc == 0) continue; // Empty input

    // --- PIPELINE DETECTION ---
    // Scan for the pipe operator "|"
    int pipe_idx = -1;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "|") == 0) {
            pipe_idx = i;
            break;
        }
    }

    if (pipe_idx != -1) {
        // === PIPELINE EXECUTION ===
        // Split the argument list into two parts at the pipe operator
        
        free(argv[pipe_idx]); // Free the "|" string
        argv[pipe_idx] = NULL; // Terminate the first command's argument list
        
        // Command 1: argv[0] ... argv[pipe_idx-1]
        char **argv1 = argv;
        int argc1 = pipe_idx;
        
        // Command 2: argv[pipe_idx+1] ... argv[argc-1]
        char **argv2 = &argv[pipe_idx + 1];
        int argc2 = argc - (pipe_idx + 1);
        
        if (argc1 > 0 && argc2 > 0) {
            run_pipeline(argc1, argv1, argc2, argv2);
        } else {
            fprintf(stderr, "Invalid pipeline\n");
        }
    } else {
        // === NORMAL EXECUTION ===
        // Handle single command (Built-in or External) with optional redirection
        
        char *redirect_out = NULL;
        char *redirect_err = NULL;
        int redirect_out_append = 0;
        int redirect_err_append = 0;
        
        parse_redirections(&argc, argv, &redirect_out, &redirect_err, &redirect_out_append, &redirect_err_append);
        
        char *cmd_name = argv[0];
        int found = 0;

        // 1. Try to execute as Built-in
        for (int i = 0; i < num_builtins(); i++) {
          if (strcmp(cmd_name, builtins[i].name) == 0) {
            int saved_stdout = -1;
            int saved_stderr = -1;
            
            if (redirect_out != NULL) {
              saved_stdout = save_and_redirect_fd(redirect_out, STDOUT_FILENO, redirect_out_append);
            }
            if (redirect_err != NULL) {
              saved_stderr = save_and_redirect_fd(redirect_err, STDERR_FILENO, redirect_err_append);
            }

            builtins[i].func(argc, argv);

            restore_fd(saved_stdout, STDOUT_FILENO);
            restore_fd(saved_stderr, STDERR_FILENO);
            found = 1;
            break;
          }
        }

        // 2. Try to execute as External Program
        if (!found) {
          char *full_path = ext_check(cmd_name);
          if (full_path != NULL){
            execute_external_program(full_path, argc, argv, redirect_out, redirect_err, redirect_out_append, redirect_err_append);
          } else {
            printf("%s: command not found\n", cmd_name);
          }
        }
        
        if (redirect_out) free(redirect_out);
        if (redirect_err) free(redirect_err);
    }

    // Free memory allocated by strdup in parse_command
    for (int i = 0; i < argc; i++) {
      free(argv[i]);
    }
  }
  return 0;
}