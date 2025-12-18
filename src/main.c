#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <termios.h>  // Required for raw mode

#define MAX_PATH_ENTRIES 100
#define MAX_ARGS 100

// Forward declarations
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
void parse_path(char *path_string);
void execute_external_program(char *full_path, int argc, char *argv[], char *redirect_out, char *redirect_err, int redirect_out_append, int redirect_err_append);
void restore_fd(int saved_fd, int target_fd);

// Terminal mode handling
struct termios original_termios;

void disable_raw_mode() {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
}

void enable_raw_mode() {
  tcgetattr(STDIN_FILENO, &original_termios);
  atexit(disable_raw_mode);
  
  struct termios raw = original_termios;
  // Disable ICANON (canonical mode) and ECHO (automatic echoing)
  raw.c_lflag &= ~(ICANON | ECHO);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// function signature for a built-in command
typedef int (*builtin_func)(int argc, char *argv[]);

// structure linking a name to a function
struct builtin {
  char *name;
  builtin_func func;
};

// dispatch table
struct builtin builtins[] = {
  {"exit", shell_exit},
  {"echo", shell_echo},
  {"help", shell_help},
  {"type", shell_type},
  {"pwd", shell_pwd},
  {"cd", shell_cd},
};

// storing path directories
char *path_dirs[MAX_PATH_ENTRIES];
int path_count = 0;

// exit function
int shell_exit(int argc, char *argv[]) {
  exit(0);
  return 0; 
}

int shell_echo(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    printf("%s", argv[i]);
    if (i < argc - 1) {
      printf(" ");
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

  for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
    char *token = argv[arg_idx];
    int found = 0;

    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(token, builtins[i].name) == 0) {
        printf("%s is a shell builtin\n", token);
        found = 1;
        break;
      }
    }

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
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
    return 0;
  } 
  else {
    perror("getcwd");
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

  if (arg[0] == '~') {
    char *home = getenv("HOME");
    if (home == NULL) {
      fprintf(stderr, "cd: HOME not set\n");
      return 1;
    }

    if (arg[1] == '\0') {
      target_dir = home;
    } else if (arg[1] == '/') {
      static char expanded_path[1024];
      snprintf(expanded_path, sizeof(expanded_path), "%s%s", home, arg + 1);
      target_dir = expanded_path;
    }
  }

  if (chdir(target_dir) != 0) {
    fprintf(stderr, "cd: %s: No such file or directory\n", arg);
  }

  return 1; 
}

int num_builtins() {
  return sizeof(builtins) / sizeof(struct builtin);
}

void parse_path(char *path_string) {
    if (path_string == NULL) return;
    
    char *path_copy = strdup(path_string);
    if (!path_copy) {
        perror("strdup");
        return;
    }
    
    char *dir = strtok(path_copy, ":");
    while (dir != NULL && path_count < MAX_PATH_ENTRIES) {
        path_dirs[path_count] = strdup(dir);  
        path_count++;
        dir = strtok(NULL, ":");
    }
    
    free(path_copy);
}

char* ext_check(char *program_name){
  for (int i = 0; i < path_count; i++){
    static char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], program_name);
    if (access(full_path, F_OK) == 0 && access(full_path, X_OK) == 0){
      return full_path;
    }
  }
  return NULL;
}

const char* complete_builtin(const char *prefix) {
  if (prefix == NULL) return NULL;
  size_t n = strlen(prefix);
  if (n == 0) return NULL;
  if (strncmp("echo", prefix, n) == 0) return "echo";
  if (strncmp("exit", prefix, n) == 0) return "exit";
  if (strncmp("type", prefix, n) == 0) return "type";
  return NULL;
}

int setup_redirect_fd(const char *path, int target_fd, int should_exit_on_error, int append_mode) {
  int flags = O_WRONLY | O_CREAT | (append_mode ? O_APPEND : O_TRUNC);
  int fd = open(path, flags, 0666);
  if (fd < 0) {
    perror("open");
    if (should_exit_on_error) exit(1);
    return -1;
  }
  if (dup2(fd, target_fd) < 0) {
    perror("dup2");
    close(fd);
    if (should_exit_on_error) exit(1);
    return -1;
  }
  close(fd);
  return 0;
}

int save_and_redirect_fd(const char *path, int target_fd, int append_mode) {
  int saved_fd = dup(target_fd);
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
    dup2(saved_fd, target_fd);
    close(saved_fd);
  }
}

void execute_external_program(char *full_path, int argc, char *argv[], char *redirect_out, char *redirect_err, int redirect_out_append, int redirect_err_append) {
    argv[argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    } else if (pid == 0) {
      if (redirect_out != NULL) {
        setup_redirect_fd(redirect_out, STDOUT_FILENO, 1, redirect_out_append);
      }
      if (redirect_err != NULL) {
        setup_redirect_fd(redirect_err, STDERR_FILENO, 1, redirect_err_append);
      }
      execv(full_path, argv);
        perror("execv");
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);
    }
}

int parse_command(const char *line, char *argv[], int max_args) {
  int argc = 0;
  int in_single_quote = 0;
  int in_double_quote = 0;
  char token[1024];
  int len = 0;

  for (const char *p = line;; p++) {
    char c = *p;

    if (!in_double_quote && c == '\'') {
      in_single_quote = !in_single_quote;
      continue;
    }

    if (!in_single_quote && c == '"') {
      in_double_quote = !in_double_quote;
      continue;
    }

    if ((c == '\0') || (!in_single_quote && !in_double_quote && isspace((unsigned char)c))) {
      if (len > 0) {
        token[len] = '\0';
        if (argc < max_args - 1) {
          argv[argc++] = strdup(token);
        }
        len = 0;
      }
      if (c == '\0') break;
      continue;
    }

    if (in_double_quote && c == '\\') {
      char next = *(++p);
      if (next == '\0') break; 
      if (next == '"' || next == '\\') {
        c = next; 
      } else {
        if (len < (int)sizeof(token) - 1) {
          token[len++] = '\\';
        }
        c = next;
      }
    }

    if (!in_single_quote && !in_double_quote && c == '\\') {
      char next = *(++p);
      if (next == '\0') break;
      c = next;
    }

    if (len < (int)sizeof(token) - 1) {
      token[len++] = c;
    }
  }

  argv[argc] = NULL;
  return argc;
}

int main(int argc, char *argv[]) {
  // 1. Enable Raw Mode for handling TAB keys
  if (isatty(STDIN_FILENO)) {
      enable_raw_mode();
  }
  
  setbuf(stdout, NULL);

  char *shell_path = getenv("PATH");
  if (shell_path == NULL) {
    fprintf(stderr, "Warning: PATH not set\n");
  } else {
    parse_path(shell_path); 
  }

  char command[1024];

  while (1) {
    printf("$ ");
    
    // read_input_line now handles raw input and returns 1 if a command was entered, 0 on EOF
    if (read_input_line(command, sizeof(command)) == 0) break;
    
    char *argv[MAX_ARGS];
    int argc = parse_command(command, argv, MAX_ARGS);

    if (argc == 0) continue;

    char *redirect_out = NULL;
    char *redirect_err = NULL;
    int redirect_out_append = 0;
    int redirect_err_append = 0;
    
    for (int i = 0; i < argc; i++) {
      if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], "1>") == 0) {
        if (i + 1 < argc) {
          redirect_out = argv[i + 1];
          redirect_out_append = 0;
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } else if (strcmp(argv[i], ">>") == 0 || strcmp(argv[i], "1>>") == 0) {
        if (i + 1 < argc) {
          redirect_out = argv[i + 1];
          redirect_out_append = 1;
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } else if (strcmp(argv[i], "2>") == 0) {
        if (i + 1 < argc) {
          redirect_err = argv[i + 1];
          redirect_err_append = 0;
        }
        for (int j = i; j + 2 <= argc; j++) {
          argv[j] = argv[j + 2];
        }
        argc -= 2;
        i -= 1; 
      } else if (strcmp(argv[i], "2>>") == 0) {
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

    if (!found) {
      char *full_path = ext_check(cmd_name);
      if (full_path != NULL){
        execute_external_program(full_path, argc, argv, redirect_out, redirect_err, redirect_out_append, redirect_err_append);
      } else {
        printf("%s: command not found\n", cmd_name);
      }
    }

    for (int i = 0; i < argc; i++) {
      free(argv[i]);
    }
  }
  return 0;
}

// Updated function to handle Raw Mode input
int read_input_line(char *buffer, size_t size) {
  size_t len = 0;
  memset(buffer, 0, size);

  while (1) {
    char c;
    // Read 1 byte from standard input
    if (read(STDIN_FILENO, &c, 1) != 1) break; 

    // Handle Ctrl+D (EOF)
    if (c == 4) { 
        if (len == 0) return 0; 
        break; 
    }
    
    // Handle Enter/Return
    if (c == '\n' || c == '\r') {
      printf("\n"); // Move to new line visually
      buffer[len] = '\0';
      return 1;
    }

    // Handle TAB for Autocomplete
    if (c == '\t') {
      // 1. Extract the current token/prefix from the buffer
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

      // 2. Check for match
      const char *comp = complete_builtin(prefix);
      
      if (comp != NULL) {
        size_t prefix_len = strlen(prefix);
        size_t comp_len = strlen(comp);
        
        if (len + (comp_len - prefix_len) + 1 < size) {
            // Print only the suffix and a space
            printf("%s ", comp + prefix_len);
            fflush(stdout);

            // Update buffer
            strcpy(buffer + len, comp + prefix_len);
            len += (comp_len - prefix_len);
            buffer[len++] = ' ';
            buffer[len] = '\0';
        }
      } else {
        printf("\a"); // Bell
        fflush(stdout);
      }
      continue;
    }

    // Handle Backspace (127 is standard DEL, \b is sometimes used)
    if (c == 127 || c == '\b') {
      if (len > 0) {
        len--;
        buffer[len] = '\0';
        // Erase character from terminal: Move back, print space, move back
        printf("\b \b");
        fflush(stdout);
      }
      continue;
    }

    // Handle Normal Characters
    if (len < size - 1) {
      // Ignore other control characters
      if (iscntrl(c)) continue; 
      
      buffer[len++] = c;
      printf("%c", c); // Echo character back to user
      fflush(stdout);
    }
  }
  return 1;
}