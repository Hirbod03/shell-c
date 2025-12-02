#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // Captures the user's command in the "command" variable
  char command[1024];
  const char *commands[] = { "exit" , "echo"};

  // Infinite read-execute loop
  while (1) {
    // Prompt
    printf("$ ");

    // storing user input
    char *input = fgets(command, sizeof(command), stdin);

    // Read input; exit loop on EOF
    if (input == NULL) {
      break;
    }

    // Remove the trailing newline before comparing
    command[strcspn(command, "\n")] = '\0';

    // check for empty input
    if (strlen(command)<1){
      continue;
    }
    // check for 'exit' command
    if (strcmp(command, commands[0]) == 0) {
      break;
    }
    // check for echo command
    if (strncmp(command, commands[1], 5) == 0) {
      // printing everything after 'echo'
      printf("%s\n", command + 5);
      continue;
    }
    else if (strcmp(command, "echo")==0){
      printf("\n");
      continue;
    }

    // Prints "<command>: command not found" msg
    printf("%s: command not found\n", command);
  }

  return 0;
}
