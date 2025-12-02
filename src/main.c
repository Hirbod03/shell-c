#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  printf("$ ");

  // Captures the user's command in the "command" variable
  char command[1024];
  fgets(command, sizeof(command), stdin);

  printf("\n %c: command not found", command);

  return 0;
}
