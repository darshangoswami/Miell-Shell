# Miell Shell

Miell is a simple, custom shell implementation in C. It provides basic shell functionality including command execution, piping, input/output redirection, and background process handling.

## Features

- Command execution
- Piping (`|`)
- Input redirection (`<`)
- Output redirection (`>` and `>>`)
- Background process execution (`&`)
- Built-in `cd` command
- Wildcard expansion

## Building the Shell

To build the Miell shell, follow these steps:

1. Ensure you have GCC and Make installed on your system.
2. Download the source files.
3. Navigate to the project directory in your terminal.
4. Run the following command to compile the shell:

   ```
   make
   ```

This will create an executable named `miell`.

## Running the Shell

After building the shell, you can run it using the following command:

```
./miell
```

You should see the Miell prompt:

```
miell>
```

## Usage

Here are some examples of how to use the Miell shell:

1. Execute a simple command:

   ```
   miell> ls -l
   ```

2. Use pipes:

   ```
   miell> ls -l | grep .txt
   ```

3. Input/Output redirection:

   ```
   miell> cat < input.txt > output.txt
   ```

4. Run a command in the background:

   ```
   miell> long_running_command &
   ```

5. Change directory:

   ```
   miell> cd /path/to/directory
   ```

6. Use wildcards:

   ```
   miell> ls *.txt
   ```

7. Exit the shell:
   ```
   miell> exit
   ```

## Debugging

If you need to debug the shell, you can enable debug logging by changing the `DEBUG` macro in `miell.c` to 1:

```c
#define DEBUG 1
```

Then recompile the shell using `make`.

## Cleaning Up

To remove the compiled files and start fresh, use:

```
make clean
```
