#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <glob.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARGS 64
#define MAX_PIPES 20

int execute_builtin(char **args);
int execute_command(char **args, int input_fd, int output_fd, int background, int is_last_command);
void parse_and_execute(char *input);
char** expand_wildcards(char** args);
char** tokenize(char* str, const char* delim);

int main() {
    char input[MAX_INPUT_SIZE];

    while (1) {
        printf("miell> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "exit") == 0) {
            break;
        }

        parse_and_execute(input);
    }

    return 0;
}

char** tokenize(char* str, const char* delim) {
    char** result = malloc(MAX_ARGS * sizeof(char*));
    char* token;
    int i = 0;

    token = strtok(str, delim);
    while(token != NULL && i < MAX_ARGS - 1) {
        // Remove surrounding quotes if present
        if (token[0] == '"' && token[strlen(token)-1] == '"') {
            token[strlen(token)-1] = '\0';
            token++;
        }
        result[i] = strdup(token);
        token = strtok(NULL, delim);
        i++;
    }
    result[i] = NULL;
    return result;
}

int execute_builtin(char **args) {
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: expected argument\n");
        } else {
            if (chdir(args[1]) != 0) {
                perror("cd");
            }
        }
        return 1;
    } else if (strcmp(args[0], "pwd") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("%s\n", cwd);
        } else {
            perror("getcwd() error");
        }
        return 1;
    }
    return 0;
}

char** expand_wildcards(char** args) {
       char** expanded_args = NULL;
       int expanded_size = 0;
       int expanded_capacity = 10;
       expanded_args = malloc(expanded_capacity * sizeof(char*));

       for (int i = 0; args[i] != NULL; i++) {
           if (strchr(args[i], '*') != NULL || strchr(args[i], '?') != NULL) {
               glob_t globbuf;
               int glob_result = glob(args[i], GLOB_NOCHECK | GLOB_TILDE, NULL, &globbuf);
               if (glob_result == 0) {
                   for (size_t j = 0; j < globbuf.gl_pathc; j++) {
                       if (expanded_size >= expanded_capacity - 1) {
                           expanded_capacity *= 2;
                           expanded_args = realloc(expanded_args, expanded_capacity * sizeof(char*));
                       }
                       expanded_args[expanded_size++] = strdup(globbuf.gl_pathv[j]);
                   }
               } else {
                   expanded_args[expanded_size++] = strdup(args[i]);
               }
               globfree(&globbuf);
           } else {
               expanded_args[expanded_size++] = strdup(args[i]);
           }
       }
       expanded_args[expanded_size] = NULL;
       return expanded_args;
}

void parse_and_execute(char *input) {
    // Trim leading and trailing whitespace
    while (*input == ' ' || *input == '\t') input++;
    char *end = input + strlen(input) - 1;
    while (end > input && (*end == ' ' || *end == '\t' || *end == '\n')) end--;
    *(end + 1) = '\0';

    if (strlen(input) == 0) {
        // Empty command, just return
        return;
    }

    fprintf(stderr, "DEBUG: Parsing command: %s\n", input);

    char **commands = tokenize(input, "|");
    int num_commands = 0;
    while (commands[num_commands] != NULL) num_commands++;

    fprintf(stderr, "DEBUG: Number of commands in pipeline: %d\n", num_commands);

    int pipes[MAX_PIPES][2];
    for (int i = 0; i < num_commands - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return;
        }
    }

    int background = 0;
    if (num_commands > 0 && strlen(commands[num_commands-1]) > 0 &&
        strcmp(commands[num_commands-1] + strlen(commands[num_commands-1]) - 1, "&") == 0) {
        background = 1;
        commands[num_commands-1][strlen(commands[num_commands-1]) - 1] = '\0';
    }

    fprintf(stderr, "DEBUG: Background execution: %s\n", background ? "yes" : "no");

    int last_command_status = 0;

    for (int i = 0; i < num_commands; i++) {
        char **args = tokenize(commands[i], " \t");
        
        // Apply wildcard expansion
        char **expanded_args = expand_wildcards(args);
        
        // Free the original args
        for (int j = 0; args[j] != NULL; j++) {
            free(args[j]);
        }
        free(args);
        args = expanded_args;

        fprintf(stderr, "DEBUG: Command %d: ", i);
        for (int j = 0; args[j] != NULL; j++) {
            fprintf(stderr, "%s ", args[j]);
        }
        fprintf(stderr, "\n");

        if (args[0] == NULL) {
            fprintf(stderr, "Error: Empty command\n");
            // Free memory and continue to next command
            free(args);
            continue;
        }

        if (i == 0 && execute_builtin(args)) {
            // Free memory and continue to next command if it was a built-in command
            for (int j = 0; args[j] != NULL; j++) {
                free(args[j]);
            }
            free(args);
            continue;
        }

        int input_fd = STDIN_FILENO;
        int output_fd = STDOUT_FILENO;

        // Check for input redirection
        for (int j = 0; args[j] != NULL; j++) {
            if (strcmp(args[j], "<") == 0) {
                if (args[j + 1] != NULL) {
                    input_fd = open(args[j + 1], O_RDONLY);
                    if (input_fd == -1) {
                        perror("open");
                        return;
                    }
                    args[j] = NULL;
                    j++;
                } else {
                    fprintf(stderr, "Error: Missing input file\n");
                    return;
                }
            }
        }

        // Check for output redirection
        for (int j = 0; args[j] != NULL; j++) {
            if (strcmp(args[j], ">") == 0) {
                if (args[j + 1] != NULL) {
                    output_fd = open(args[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
                    if (output_fd == -1) {
                        perror("open");
                        return;
                    }
                    args[j] = NULL;
                    j++;
                } else {
                    fprintf(stderr, "Error: Missing output file\n");
                    return;
                }
            }
        }

        if (i > 0) {
            input_fd = pipes[i - 1][0];
        }

        if (i < num_commands - 1) {
            output_fd = pipes[i][1];
        }

        fprintf(stderr, "DEBUG: Command %d - input_fd: %d, output_fd: %d\n", i, input_fd, output_fd);

        int is_last_command = (i == num_commands - 1);
        int command_status = execute_command(args, input_fd, output_fd, (is_last_command && background), is_last_command);

        // For pipelines, we only care about the status of the last command
        if (is_last_command) {
            last_command_status = command_status;
        }

        // Free the memory allocated for args
        for (int j = 0; args[j] != NULL; j++) {
            free(args[j]);
        }
        free(args);

        // Close pipe ends that are no longer needed
        if (i > 0) {
            close(pipes[i - 1][0]);
        }
        if (i < num_commands - 1) {
            close(pipes[i][1]);
        }
    }

    // Free the memory allocated for commands
    for (int i = 0; commands[i] != NULL; i++) {
        free(commands[i]);
    }
    free(commands);

    // Wait for all non-background processes to finish
    if (!background) {
        while (wait(NULL) > 0);
    }

    // Only report non-zero status for the last command in the pipeline
    if (last_command_status != 0 && !background) {
        fprintf(stderr, "Pipeline exited with non-zero status %d\n", last_command_status);
    }

    fprintf(stderr, "DEBUG: Command execution completed\n");
}

int execute_command(char **args, int input_fd, int output_fd, int background, int is_last_command) {
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        return -1;
    } else if (pid == 0) {
        // Child process
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        // Close all other pipe file descriptors
        for (int i = 3; i < 20; i++) {  // Assuming max file descriptor is less than 20
            if (i != input_fd && i != output_fd) {
                close(i);
            }
        }

        fprintf(stderr, "DEBUG: Executing command: %s\n", args[0]);
        for (int i = 0; args[i] != NULL; i++) {
            fprintf(stderr, "DEBUG: arg[%d] = %s\n", i, args[i]);
        }

        execvp(args[0], args);
        // If execvp returns, it must have failed
        fprintf(stderr, "Error: Command not found or failed to execute: %s\n", args[0]);
        exit(1);
    } else {
        // Parent process
        if (!background) {
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                int exit_status = WEXITSTATUS(status);
                fprintf(stderr, "DEBUG: Command '%s' exited with status %d\n", args[0], exit_status);
                return exit_status;
            } else if (WIFSIGNALED(status)) {
                fprintf(stderr, "Command '%s' killed by signal %d\n", args[0], WTERMSIG(status));
                return -1;
            }
        } else {
            printf("[1] %d\n", pid);
        }
    }
    return 0;
}