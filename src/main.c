#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

# define MAX_PATH_ENTRIES 100

// Forward declarations
int shell_exit(char *args);
int shell_echo(char *args);
int shell_help(char *args);
int shell_type(char *args);
int num_builtins();
void parse_path(char *path_string);

// function signature for a built-in command (returns int so we can signal errors or exit status)
typedef int (*builtin_func)(char *args);

// structure linking a name to a function
struct builtin {
  char *name;
  builtin_func func;
};

// dispastch table (best practice allegedly)
struct builtin builtins[] = {
  {"exit", shell_exit},
  {"echo", shell_echo},
  {"help", shell_help},
  {"type", shell_type},
};

// storing path directories
char *path_dirs[MAX_PATH_ENTRIES];
int path_count = 0;

void parse_path(char *path_string) {
    if (path_string == NULL) return;
    
    // IMPORTANT: duplicate the string because strtok modifies it
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

// exit function
int shell_exit(char *args) {
  exit(0);
  return 0; // unreachable, but satisfies compiler
}

int shell_echo(char *args) {
  // print the args if they exist
  if (args != NULL) {
      printf("%s\n", args);
  } else {
      printf("\n");
  }
  return 1; // 1 to signal "continue running"
}

int shell_type(char *args) {
  // if no arguments provided, print usage error
  if (args == NULL) { 
    printf("type: expected argument\n");
    return 1;
  }

  // duplicate the args so we can tokenize safely (strtok modifies the string)
  char *copy = strdup(args);
  if (!copy) {
    perror("strdup");
    return 1;
  }

  // get the first token (separated by spaces)
  char *token = strtok(copy, " ");
  while (token != NULL) {
    int found = 0;
    // iterate over builtins to see if token matches a builtin name
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(token, builtins[i].name) == 0) {
        // if matched, report that it's a shell builtin
        printf("%s is a shell builtin\n", token);
        found = 1;
        break;
      }
    }
    // if no match was found, report accordingly
    if (!found) {
      for (int i = 0; i < path_count; i++){
        // appending program name to dir path
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path_dirs[i], token);
        // printf("%s\n", full_path);
        if (access(full_path, F_OK) == 0){ // if the file exists
          if (access(full_path, X_OK) == 0){ // if the file is executable
            printf("%s is %s\n", token, full_path);
            found = 1;
            break;
          }
          continue; // if file is not executable, we just skip
        }
      }
      if (!found){
        printf("%s: not found\n", token);
      }
    }
    // advance to the next space-separated token
    token = strtok(NULL, " ");
  }

  // free the duplicated string to avoid leaks
  free(copy);
  return 1; // continue running the shell
}

int shell_help(char *args) {
  printf("Hirbod's Shell. Built-ins available:\n");
  printf("  cd\n  help\n  exit\n  echo\n");
  return 1;
}

// returns the number of built-in commands in the shell
int num_builtins() {
  return sizeof(builtins) / sizeof(struct builtin);
}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);

  // read PATH at statup
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
    
    // clean newline
    command[strcspn(command, "\n")] = '\0';
    
    // parsing logic
    char *cmd_name = strtok(command, " "); // storing command
    char *args = strtok(NULL, ""); // storing command arguments

    if (cmd_name == NULL) continue;

    int found = 0;
    
    // nice generic loop that I won't have to tinker with after adding more commands
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(cmd_name, builtins[i].name) == 0) {
        builtins[i].func(args);
        found = 1;
        break;
      }
    }

    if (!found) {
      // where non-builtins are handled
      printf("%s: command not found\n", cmd_name);
    }
  }
  return 0;
}