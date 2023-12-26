#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int sh_cd(char **args);
int sh_help(char **args);
int sh_exit(char **args);

char *builtin_strs[] = {"cd", "help", "exit"};

int (*builtin_func[])(char **) = {&sh_cd, &sh_help, &sh_exit};

int sh_num_builtins() { return sizeof(builtin_strs) / sizeof(char *); }

int sh_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "sh: expected argument to \"cd\"\n");
} else {
    if (chdir(args[1]) != 0) {
      perror("sh");
}
}
  return 1;
}

char *read_line() {
  char *line = NULL;
  size_t bufsize = 0;

  if (getline(&line, &bufsize, stdin) == -1) {
    if (feof(stdin)) {
      exit(EXIT_SUCCESS); // EOF ctrl-D
    } else {
      perror("readline");
      exit(EXIT_FAILURE);
    }
  }

  return line;
}

char **split_pipes(char *line) {
  char **commands = calloc(8, 16);
  if (!commands) {
    exit(1);
  }
  char *command;
  int pos = 0;

  command = strtok(line, "|");
  while (command) {
    commands[pos++] = command;
    command = strtok(NULL, "|");
  }

  // Trim whitespace
  for (int i = 0; commands[i]; i++) {
    while (*commands[i] == ' ' || *commands[i] == '\n') {
      commands[i]++;
    }
    char *last_spc = strrchr(commands[i], '\0');
    --last_spc;
    while (*last_spc == ' ' || *last_spc == '\n') {
      *last_spc = '\0';
      --last_spc;
    }
  }
  return commands;
}

char ***split_args(char **commands, int *status) {
  int outer_pos = 0;
  char ***command_args = calloc(8, 16);
  if (!command_args) {
    exit(1);
  }

  for (int i = 0; commands[i]; i++) {
    int pos = 0;
    char **args = calloc(8, 16);
    if (!args) {
      exit(1);
    }

    bool quotes = false;
    char *cur_tok = &commands[i][0];
    for (int j = 0; commands[i][j]; j++) {
      if (commands[i][j] == '"' && !quotes) {
        // Starting quote mark
        commands[i][j++] = '\0';
        cur_tok = &commands[i][j];
        quotes = true;
      } else if (quotes) {
        if (commands[i][j] == '"') {
          // Ending quote mark
          quotes = false;
          commands[i][j] = '\0';
          args[pos++] = cur_tok;
          cur_tok = NULL;
        } else {
          // Regular char in string literal
          continue;
        }
      } else if (commands[i][j] == ' ') {
        // Space delim
        if (cur_tok && *cur_tok != ' ') {
          args[pos++] = cur_tok;
        }
        cur_tok = &commands[i][j + 1];
        commands[i][j] = '\0';
      } else {
        // Regular char
        continue;
      }
    }
    // String literal was never finished
    if (quotes) {
      *status = -1;
    } else if (cur_tok && *cur_tok != ' ') {
      args[pos++] = cur_tok;
    }

    // Good to go
    command_args[outer_pos++] = args;
  }

  *status = outer_pos;
  return command_args;
}

void execute_command(char **args, int input_fd, int output_fd) {
  // Create child
  pid_t pid;
  pid_t wpid;
  int status;

  if ((pid = fork()) == 0) {
    // Child

    // Redirect input and output fd's if needed
    if (input_fd != STDIN_FILENO) {
      dup2(input_fd, STDIN_FILENO);
      close(input_fd);
    }

    if (output_fd != STDOUT_FILENO) {
      dup2(output_fd, STDOUT_FILENO);
      close(output_fd);
    }

    // Execute
    if (execvp(args[0], args) == -1) {
      perror("execvp");
      exit(EXIT_FAILURE);
    }
  } else if (pid < 0) {
    perror("fork");
    exit(EXIT_FAILURE);
  } else {
    // Parent
    do {
      wpid = waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }
}

void execute_pipeline(char ***commands, int num_commands) {
  int **pipes = malloc((num_commands - 1) * sizeof(int *));

  // Create pipes
  for (int i = 0; i < num_commands - 1; i++) {
    pipes[i] = malloc(2 * sizeof(int));
    if (pipe(pipes[i]) == -1) {
      perror("pipe");
      exit(EXIT_FAILURE);
    }
  }

  // Execute commands in the pipeline
  for (int i = 0; i < num_commands - 1; i++) {
    int input_fd = (i == 0) ? STDIN_FILENO : pipes[i - 1][0];
    int output_fd = pipes[i][1];

    execute_command(commands[i], input_fd, output_fd);
    close(output_fd);
  }

  // Execute last command
  int input_fd = (num_commands > 1) ? pipes[num_commands - 2][0] : STDIN_FILENO;
  int output_fd = STDOUT_FILENO;
  execute_command(commands[num_commands - 1], input_fd, output_fd);

  for (int i = 0; i < num_commands - 1; i++) {
    close(pipes[i][0]);
    close(pipes[i][1]);
    free(pipes[i]);
  }
  free(pipes);
}

int main() {
  while (true) {
    char *cwd = getcwd(NULL, 0);
    char *last_slash = strrchr(cwd, '/');
    *last_slash = 0;
    ++last_slash;
    printf("%s\e[1;31m/%s \e[0m\e[1m=>\e[0m ", cwd, last_slash);
    int status = 0;
    char *test = read_line();
    if (strlen(test) == 1) {
      free(test);
      continue;
    } else if (strncmp(test, "exit", 4) == 0) {
      free(test);
      return 0;
    }
    char **commands = split_pipes(test);
    char ***args = split_args(commands, &status);
    if (status == -1) {
      fprintf(stderr, "Expected end of quoted string\n");
    }
    execute_pipeline(args, status);

    for (int i = 0; args[i]; i++) {
      free(args[i]);
    }
    free(args);
    free(commands);
    free(test);
    free(cwd);
  }
}
