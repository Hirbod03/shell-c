#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // Captures the user's command in the "command" variable
  char command[1024];

  // Infinite read-execute loop
  while (1) {
    // Prompt
    printf("$ ");

    // Read input; exit loop on EOF
    if (fgets(command, sizeof(command), stdin) == NULL) {
      break;
    }

    // Remove the trailing newline
    command[strcspn(command, "\n")] = '\0';

    // Prints "<command>: command not found" msg
    printf("%s: command not found\n", command);
  }

  return 0;
}
