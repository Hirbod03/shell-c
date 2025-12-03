#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// function signature for a built-in command (returns int so we can signal errors or exit status)
typedef int (*builtin_func)(char *args);

// structure linking a name to a function
struct builtin {
  char *name;
  builtin_func func;
};

// exit function
int shell_exit(char *args) {
  exit(0);
  return 0; // unreachable, but satisfies compiler
}

int shell_echo(char *args) {
  // Simple logic: print the args if they exist
  if (args != NULL) {
      printf("%s\n", args);
  } else {
      printf("\n");
  }
  return 1; // 1 means "continue running"
}

int shell_help(char *args) {
  printf("Hirbod's Shell. Built-ins available:\n");
  printf("  cd\n  help\n  exit\n  echo\n");
  return 1;
}

// 4. Create the Dispatch Table
struct builtin builtins[] = {
  {"exit", shell_exit},
  {"echo", shell_echo},
  {"help", shell_help},
  // Adding a new command is now just one line here.
  // {"cd", shell_cd}, 
};

int num_builtins() {
  return sizeof(builtins) / sizeof(struct builtin);
}

int main(int argc, char *argv[]) {
  setbuf(stdout, NULL);
  char command[1024];

  while (1) {
    printf("$ ");
    if (!fgets(command, sizeof(command), stdin)) break;
    
    // Clean newline
    command[strcspn(command, "\n")] = '\0';
    
    // Parsing logic (simplified)
    char *cmd_name = strtok(command, " ");
    char *args = strtok(NULL, ""); // Get the rest of the string

    if (cmd_name == NULL) continue;

    int found = 0;
    
    // 5. Generic Execution Loop
    // This loop NEVER changes, even if you add 100 commands.
    for (int i = 0; i < num_builtins(); i++) {
      if (strcmp(cmd_name, builtins[i].name) == 0) {
        builtins[i].func(args);
        found = 1;
        break;
      }
    }

    if (!found) {
      // In the future, this is where fork/exec happens for non-builtins
      printf("%s: command not found\n", cmd_name);
    }
  }
  return 0;
}