#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  printf("$ ");

  // Captures the user's command in the "command" variable
  char command[1024];
  fgets(command, sizeof(command), stdin);

  // Remove the trailing newline
  command[strcspn(command, "\n")] = '\0';

  // Prints "<command>: command not found" msg
  printf("%s: command not found\n", command);

  return 0;
}
