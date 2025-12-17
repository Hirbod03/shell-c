#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define MAX_PATH_ENTRIES 100
#define MAX_ARGS 100

// Forward declarations
int shell_exit(int argc, char *argv[]);
int shell_echo(int argc, char *argv[]);
int shell_help(int argc, char *argv[]);
int shell_type(int argc, char *argv[]);
int shell_pwd(int argc, char *argv[]);
int shell_cd(int argc, char *argv[]);
int num_builtins();
void parse_path(char *path_string);
char* ext_check(char *program_name);
void execute_external_program(char *full_path, int argc, char *argv[]);
int parse_command(const char *line, char *argv[], int max_args);

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
  return 0; // unreachable, but satisfies compiler
}

int shell_echo(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {
    printf("%s", argv[i]);
    if (i < argc - 1) {
      printf(" ");
    }
  }
  printf("\n");
  return 1; // 1 to signal "continue running"
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
  return 1; // continue running the shell
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

  return 1; // continue running the shell
}

// returns the number of built-in commands in the shell
int num_builtins() {
  return sizeof(builtins) / sizeof(struct builtin);
}

void parse_path(char *path_string) {
    if (path_string == NULL) return;
    
  // duplicate the string because strtok modifies it
    char *path_copy = strdup(path_string);
    if (!path_copy) {
        perror("strdup");
        return;
    }
    
    // Tokenize by ':'
    char *dir = strtok(path_copy, ":");
    while (dir != NULL && path_count < MAX_PATH_ENTRIES) {
        path_dirs[path_count] = strdup(dir);  // Store each directory
        path_count++;
        dir = strtok(NULL, ":");
    }
    
    free(path_copy);
}

char* ext_check(char *program_name){
  for (int i = 0; i < path_count; i++){
    // appending program name to dir path
    static char full_path[1024];
    snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], program_name);
    if (access(full_path, F_OK) == 0 && access(full_path, X_OK) == 0){
      return full_path;
    }
  }
  return NULL;
}

void execute_external_program(char *full_path, int argc, char *argv[]) {
  // argv already prepared by parse_command; ensure NULL terminator
    argv[argc] = NULL;

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return;
    } else if (pid == 0) {
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

    // toggle quote modes; each quote type is ignored while inside the other
    if (!in_double_quote && c == '\'') {
      in_single_quote = !in_single_quote;
      continue;
    }

    if (!in_single_quote && c == '"') {
      in_double_quote = !in_double_quote;
      continue;
    }

    // outside quotes, whitespace ends the current token
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

    // backslash outside quotes escapes the next character (including whitespace)
    if (!in_single_quote && !in_double_quote && c == '\\') {
      char next = *(++p);
      if (next == '\0') break; // nothing to escape at end of line
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
  setbuf(stdout, NULL);

  // read PATH at startup
  char *shell_path = getenv("PATH");
  if (shell_path == NULL) {
    fprintf(stderr, "Warning: PATH not set\n");
  } else {
    parse_path(shell_path);  // Parse it into array
  }

  char command[1024];

  while (1) {
    printf("$ ");
    if (!fgets(command, sizeof(command), stdin)) break;
    
    command[strcspn(command, "\n")] = '\0';

    char *argv[MAX_ARGS];
    int argc = parse_command(command, argv, MAX_ARGS);

    if (argc == 0) continue;

    char *cmd_name = argv[0];
    int found = 0;

    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(cmd_name, builtins[i].name) == 0) {
        builtins[i].func(argc, argv);
        found = 1;
        break;
      }
    }

    if (!found) {
      char *full_path = ext_check(cmd_name);
      if (full_path != NULL){
        execute_external_program(full_path, argc, argv);
      } else {
        printf("%s: command not found\n", cmd_name);
      }
    }

    // free tokens allocated by parse_command
    for (int i = 0; i < argc; i++) {
      free(argv[i]);
    }
  }
  return 0;
}