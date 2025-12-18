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
const char* complete_builtin(const char *prefix) {
  if (prefix == NULL) return NULL;
  size_t n = strlen(prefix);
  if (n == 0) return NULL;
  
  // Checks if input is a substring of known commands
  if (strncmp("echo", prefix, n) == 0) return "echo";
  if (strncmp("exit", prefix, n) == 0) return "exit";
  if (strncmp("type", prefix, n) == 0) return "type";
  return NULL;
}

char* complete_executable(const char *prefix) {
  if (prefix == NULL || strlen(prefix) == 0) return NULL;
  size_t prefix_len = strlen(prefix);
  static char match[256]; 

  for (int i = 0; i < path_count; i++) {
    DIR *d = opendir(path_dirs[i]);
    if (d == NULL) continue;

    struct dirent *dir;
    while ((dir = readdir(d)) != NULL) {
      if (strncmp(dir->d_name, prefix, prefix_len) == 0) {
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], dir->d_name);
        
        if (access(full_path, X_OK) == 0) {
          strncpy(match, dir->d_name, sizeof(match) - 1);
          match[sizeof(match) - 1] = '\0';
          closedir(d);
          return match;
        }
      }
    }
    closedir(d);
  }
  return NULL;
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

/* * Reads input byte-by-byte to handle specialized keys (TAB, Backspace).
 * Returns 1 if command entered, 0 on EOF (Ctrl+D).
 */
int read_input_line(char *buffer, size_t size) {
  size_t len = 0;
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

      // 2. Check for match in built-ins
      const char *comp = complete_builtin(prefix);
      if (comp == NULL) {
        comp = complete_executable(prefix);
      }
      
      if (comp != NULL) {
        size_t prefix_len = strlen(prefix);
        size_t comp_len = strlen(comp);
        
        // Append the missing part of the command
        if (len + (comp_len - prefix_len) + 1 < size) {
            printf("%s ", comp + prefix_len); // Visual update
            fflush(stdout);

            // Buffer update
            strcpy(buffer + len, comp + prefix_len);
            len += (comp_len - prefix_len);
            buffer[len++] = ' ';
            buffer[len] = '\0';
        }
      } else {
        printf("\a"); // Bell sound (System beep) if no match
        fflush(stdout);
      }
      continue;
    }

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

    // --- Redirection Parsing ---
    // We scan the arguments for >, >>, 2>, 2>>.
    // If found, we extract the filename, set flags, and remove those tokens from argv.
    char *redirect_out = NULL;
    char *redirect_err = NULL;
    int redirect_out_append = 0;
    int redirect_err_append = 0;
    
    for (int i = 0; i < argc; i++) {
      // Standard Output Redirection (> or 1>)
      if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "1>") == 0) {
        if (i + 1 < argc) {
          redirect_out = argv[i + 1];
          redirect_out_append = 0; // Truncate mode
        }
        // Shift remaining arguments left to remove ">" and "filename"
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } 
      // Append Standard Output (>> or 1>>)
      else if (strcmp(argv[i], ">>") == 0 || strcmp(argv[i], "1>>") == 0) {
        if (i + 1 < argc) {
          redirect_out = argv[i + 1];
          redirect_out_append = 1; // Append mode
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } 
      // Standard Error Redirection (2>)
      else if (strcmp(argv[i], "2>") == 0) {
        if (i + 1 < argc) {
          redirect_err = argv[i + 1];
          redirect_err_append = 0;
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } 
      // Append Standard Error (2>>)
      else if (strcmp(argv[i], "2>>") == 0) {
        if (i + 1 < argc) {
          redirect_err = argv[i + 1];
          redirect_err_append = 1;
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      }
    }

    char *cmd_name = argv[0];
    int found = 0;

    // 1. Try to execute as Built-in
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(cmd_name, builtins[i].name) == 0) {
        int saved_stdout = -1;
        int saved_stderr = -1;
        
        // If redirection was requested, swap FDs before running the function
        if (redirect_out != NULL) {
          saved_stdout = save_and_redirect_fd(redirect_out, STDOUT_FILENO, redirect_out_append);
        }
        if (redirect_err != NULL) {
          saved_stderr = save_and_redirect_fd(redirect_err, STDERR_FILENO, redirect_err_append);
        }

        builtins[i].func(argc, argv);

        // Swap FDs back to terminal so the prompt prints correctly
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
        // External programs handle redirection inside the child process (in execute_external_program)
        execute_external_program(full_path, argc, argv, redirect_out, redirect_err, redirect_out_append, redirect_err_append);
      } else {
        printf("%s: command not found\n", cmd_name);
      }
    }

    // Free memory allocated by strdup in parse_command
    for (int i = 0; i < argc; i++) {
      free(argv[i]);
    }
  }
  return 0;
}