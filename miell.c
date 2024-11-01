#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>
#include <glob.h>

#define MAX_INPUT_SIZE 1024
#define MAX_ARG_COUNT 64
#define MAX_PIPE_COUNT 10
#define DEBUG 0  // Set to 0 to disable debug logging

// Function prototypes
char** parse_input(char* input, int* arg_count);
int execute_builtin(char** args);
void execute_command(char** args, int input_fd, int output_fd, int is_background);
void handle_pipes(char*** commands, int command_count, int is_background);
void handle_redirection(char** args, int* arg_count, int* input_fd, int* output_fd);
void free_commands(char*** commands, int command_count);
void debug_log(const char* format, ...);
char** expand_wildcards(char** args, int* arg_count);
void execute_background_commands(char* input);
void display_prompt(void);

int main(void) {
    char input[MAX_INPUT_SIZE];
    char*** commands = NULL;
    int command_count;

    debug_log("Shell started\n");

    while (1) {
        display_prompt();

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        input[strcspn(input, "\n")] = 0;  // Remove trailing newline
        debug_log("Received input: %s\n", input);

        if (strcmp(input, "exit") == 0) {
            debug_log("Exit command received\n");
            break;
        }

        // Handle background commands
        if (strchr(input, '&') != NULL) {
            execute_background_commands(input);
            continue;
        }

        // Split input into commands (for pipes)
        commands = malloc(MAX_PIPE_COUNT * sizeof(char**));
        command_count = 0;
        char* saveptr;
        char* command = strtok_r(input, "|", &saveptr);
        while (command != NULL && command_count < MAX_PIPE_COUNT) {
            commands[command_count] = parse_input(command, NULL);
            debug_log("Parsed command %d: %s\n", command_count, command);
            command_count++;
            command = strtok_r(NULL, "|", &saveptr);
        }
        debug_log("Total commands: %d\n", command_count);

        // Handle built-in commands
        if (command_count == 1 && execute_builtin(commands[0])) {
            debug_log("Executed built-in command\n");
            free_commands(commands, command_count);
            continue;
        }

        // Handle pipes and execution
        handle_pipes(commands, command_count, 0);

        // Free allocated memory
        free_commands(commands, command_count);

        // Wait for a short time to allow output to be displayed
        usleep(10000);  // Wait for 10ms
    }

    debug_log("Shell exiting\n");
    return 0;
}

void display_prompt(void) {
    printf("\nmiell> ");
    fflush(stdout);
}

void execute_background_commands(char* input) {
    char* saveptr;
    char* token = strtok_r(input, "&", &saveptr);
    int job_number = 1;

    while (token != NULL) {
        // Trim leading and trailing whitespace
        while (*token == ' ' || *token == '\t') token++;
        char* end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) end--;
        *(end + 1) = '\0';

        if (strlen(token) > 0) {
            // Parse the command into pipes
            char*** commands = malloc(MAX_PIPE_COUNT * sizeof(char**));
            int command_count = 0;
            char* pipe_saveptr;
            char* pipe_token = strtok_r(token, "|", &pipe_saveptr);
            while (pipe_token != NULL && command_count < MAX_PIPE_COUNT) {
                commands[command_count] = parse_input(pipe_token, NULL);
                command_count++;
                pipe_token = strtok_r(NULL, "|", &pipe_saveptr);
            }

            pid_t pid = fork();
            if (pid == 0) {
                // Child process
                handle_pipes(commands, command_count, 1);
                exit(0);
            } else if (pid > 0) {
                // Parent process
                printf("[%d] %d\n", job_number++, pid);
                debug_log("Started background job %d (PID: %d): %s\n", job_number - 1, pid, token);
            } else {
                perror("fork");
            }

            // Free allocated memory
            free_commands(commands, command_count);
        }

        token = strtok_r(NULL, "&", &saveptr);
    }
}

char** parse_input(char* input, int* arg_count) {
    char** args = malloc(MAX_ARG_COUNT * sizeof(char*));
    int count = 0;
    char* token;
    char* saveptr;

    token = strtok_r(input, " \t", &saveptr);
    while (token != NULL && count < MAX_ARG_COUNT - 1) {
        args[count] = strdup(token);
        count++;
        token = strtok_r(NULL, " \t", &saveptr);
    }
    args[count] = NULL;
    if (arg_count) {
        *arg_count = count;
    }
    debug_log("Parsed %d arguments\n", count);

    // Expand wildcards
    args = expand_wildcards(args, &count);
    if (arg_count) {
        *arg_count = count;
    }
    debug_log("After wildcard expansion: %d arguments\n", count);

    return args;
}

char** expand_wildcards(char** args, int* arg_count) {
    char** new_args = malloc(MAX_ARG_COUNT * sizeof(char*));
    int new_count = 0;
    glob_t glob_result;

    for (int i = 0; i < *arg_count; i++) {
        if (strchr(args[i], '*') || strchr(args[i], '?')) {
            // Perform wildcard expansion
            int glob_flags = GLOB_NOCHECK | GLOB_TILDE;
            if (glob(args[i], glob_flags, NULL, &glob_result) == 0) {
                for (size_t j = 0; j < glob_result.gl_pathc && new_count < MAX_ARG_COUNT - 1; j++) {
                    new_args[new_count] = strdup(glob_result.gl_pathv[j]);
                    new_count++;
                }
                globfree(&glob_result);
            }
        } else {
            // No wildcard, just copy the argument
            new_args[new_count] = strdup(args[i]);
            new_count++;
        }
    }

    new_args[new_count] = NULL;
    *arg_count = new_count;

    // Free the old args
    for (int i = 0; args[i] != NULL; i++) {
        free(args[i]);
    }
    free(args);

    return new_args;
}

int execute_builtin(char** args) {
    if (strcmp(args[0], "cd") == 0) {
        if (args[1] == NULL) {
            fprintf(stderr, "cd: missing argument\n");
            debug_log("cd: missing argument\n");
        } else if (chdir(args[1]) != 0) {
            perror("cd");
            debug_log("cd failed: %s\n", strerror(errno));
        } else {
            debug_log("Changed directory to %s\n", args[1]);
        }
        return 1;
    }
    return 0;
}

void execute_command(char** args, int input_fd, int output_fd, int is_background) {
    debug_log("Executing command: %s (background: %d)\n", args[0], is_background);
    pid_t pid = fork();

    if (pid == 0) {  // Child process
        debug_log("Child process started (PID: %d)\n", getpid());
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
            debug_log("Redirected input (fd: %d)\n", input_fd);
        }
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
            debug_log("Redirected output (fd: %d)\n", output_fd);
        }
        
        // Remove redirection to /dev/null for background processes
        /*
        if (is_background) {
            int devnull = open("/dev/null", O_RDONLY);
            if (devnull == -1) {
                perror("open");
                debug_log("Failed to open /dev/null: %s\n", strerror(errno));
                exit(1);
            }
            dup2(devnull, STDIN_FILENO);
            close(devnull);
            debug_log("Redirected background process input to /dev/null\n");
        }
        */

        execvp(args[0], args);
        fprintf(stderr, "Error: command not found: %s\n", args[0]);
        debug_log("execvp failed: %s\n", strerror(errno));
        exit(1);
    } else if (pid > 0) {  // Parent process
        debug_log("Parent process: child PID is %d\n", pid);
        if (is_background) {
            printf("[1] %d\n", pid);
            debug_log("Background process started (PID: %d)\n", pid);
        } else {
            int status;
            waitpid(pid, &status, 0);
            debug_log("Waited for child process (PID: %d, Status: %d)\n", pid, status);
        }
    } else {
        perror("fork");
        debug_log("Fork failed: %s\n", strerror(errno));
        exit(1);
    }
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
void handle_pipes(char*** commands, int command_count, int is_background) {
    debug_log("Handling pipes (command_count: %d, background: %d)\n", command_count, is_background);
    int pipes[MAX_PIPE_COUNT-1][2];
    int i;
    pid_t last_pid = 0;

    for (i = 0; i < command_count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            debug_log("Pipe creation failed: %s\n", strerror(errno));
            exit(1);
        }
        debug_log("Created pipe %d: read_fd=%d, write_fd=%d\n", i, pipes[i][0], pipes[i][1]);
    }

    for (i = 0; i < command_count; i++) {
        int input_fd = (i == 0) ? STDIN_FILENO : pipes[i-1][0];
        int output_fd = (i == command_count-1) ? STDOUT_FILENO : pipes[i][1];

        int arg_count;
        for (arg_count = 0; commands[i][arg_count] != NULL; arg_count++);
        debug_log("Command %d has %d arguments\n", i, arg_count);
        
        handle_redirection(commands[i], &arg_count, &input_fd, &output_fd);

        pid_t pid = fork();
        if (pid == 0) {  // Child process
            // Close all unused pipe ends
            for (int j = 0; j < command_count - 1; j++) {
                if (j != i - 1) close(pipes[j][0]);
                if (j != i) close(pipes[j][1]);
            }

            if (input_fd != STDIN_FILENO) {
                dup2(input_fd, STDIN_FILENO);
                close(input_fd);
            }
            if (output_fd != STDOUT_FILENO) {
                dup2(output_fd, STDOUT_FILENO);
                close(output_fd);
            }

            execvp(commands[i][0], commands[i]);
            fprintf(stderr, "Error: command not found: %s\n", commands[i][0]);
            exit(1);
        } else if (pid > 0) {  // Parent process
            debug_log("Started process for command %d (PID: %d)\n", i, pid);
            if (i == command_count - 1) {
                last_pid = pid;
            }
        } else {
            perror("fork");
            exit(1);
        }
    }

    // Close all pipe ends in the parent process
    for (i = 0; i < command_count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // Wait for all child processes
    if (!is_background) {
        for (i = 0; i < command_count; i++) {
            int status;
            waitpid(-1, &status, 0);
            debug_log("Child process exited with status: %d\n", status);
        }
    }
}
#pragma GCC diagnostic pop

void handle_redirection(char** args, int* arg_count, int* input_fd, int* output_fd) {
    for (int i = 0; i < *arg_count; i++) {
        if (strcmp(args[i], "<") == 0) {
            *input_fd = open(args[i+1], O_RDONLY);
            if (*input_fd == -1) {
                perror("open");
                debug_log("Input redirection failed: %s\n", strerror(errno));
                exit(1);
            }
            debug_log("Input redirected from file: %s (fd: %d)\n", args[i+1], *input_fd);
            free(args[i]);
            free(args[i+1]);
            for (int j = i; j < *arg_count - 2; j++) {
                args[j] = args[j+2];
            }
            *arg_count -= 2;
            i--;
        } else if (strcmp(args[i], ">") == 0) {
            *output_fd = open(args[i+1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*output_fd == -1) {
                perror("open");
                debug_log("Output redirection failed: %s\n", strerror(errno));
                exit(1);
            }
            debug_log("Output redirected to file: %s (fd: %d)\n", args[i+1], *output_fd);
            free(args[i]);
            free(args[i+1]);
            for (int j = i; j < *arg_count - 2; j++) {
                args[j] = args[j+2];
            }
            *arg_count -= 2;
            i--;
        } else if (strcmp(args[i], ">>") == 0) {
            *output_fd = open(args[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (*output_fd == -1) {
                perror("open");
                debug_log("Output redirection (append) failed: %s\n", strerror(errno));
                exit(1);
            }
            debug_log("Output redirected (append) to file: %s (fd: %d)\n", args[i+1], *output_fd);
            free(args[i]);
            free(args[i+1]);
            for (int j = i; j < *arg_count - 2; j++) {
                args[j] = args[j+2];
            }
            *arg_count -= 2;
            i--;
        }
    }
    args[*arg_count] = NULL;
}

void free_commands(char*** commands, int command_count) {
    for (int i = 0; i < command_count; i++) {
        for (int j = 0; commands[i][j] != NULL; j++) {
            free(commands[i][j]);
        }
        free(commands[i]);
    }
    free(commands);
    debug_log("Freed memory for %d commands\n", command_count);
}

void debug_log(const char* format, ...) {
    if (!DEBUG) return;

    va_list args;
    va_start(args, format);

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);

    fprintf(stderr, "[DEBUG %s] ", timestamp);
    vfprintf(stderr, format, args);
    va_end(args);
}
