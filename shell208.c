/*
    shell208.c

    Luke Wharton
    Partner Marc Eidelhoch

    A shell program that handles bash commands, command line arguments, file
    redirection, and help.
*/

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>

// The command buffer will need to have room to hold the
// command, the \n at the end of the command, and the \0.
// That's why the maximum command size is 2 less than the
// command buffer size.
#define COMMAND_BUFFER_SIZE     102
#define MAX_COMMAND_SIZE        COMMAND_BUFFER_SIZE - 2

// status values for get_command
#define COMMAND_INPUT_SUCCEEDED 0
#define COMMAND_INPUT_FAILED    1
#define COMMAND_END_OF_FILE     2
#define COMMAND_TOO_LONG        3


// function definitions
void signal_interrupt();
void interrupt_handler();
char ***get_command(char *command_buffer, int buffer_size, int *status, char *outputLocation, char *inputLocation);
int is_pipe(char *command_buffer);
int count_args(char *command_buffer, char *outputLocation, char *inputLocation, int index);
void set_redirection (char* command_buffer, char *outputLocation, char *inputLocation, int index);
void parse_args(char **my_argv, int number_of_args, char *command_buffer, int index);
void execute_command(char ***parsed_command, char *outputLocation, char *inputLocation);
void execute_single_command(char **command);
void redirect_stdout(char *outputLocation);
void redirect_stdin(char *inputLocation);
void display_help();
void free_array(char ***my_array);

int main()
{
    // variables for main infinite loop
    const char *prompt = "shell208> ";
    char command_line[COMMAND_BUFFER_SIZE];
    int status;
    char outputLocation[100];
    char inputLocation[100];
    char ***result;

    signal_interrupt();

     // The main infinite loop
    while (1)
    {   
        // sets initial values
        outputLocation[0] = '\0';
        inputLocation[0] = '\0';
        result = NULL;
        status = 0;
        printf("%s", prompt);
        fflush(stdout);

        result = get_command(command_line, COMMAND_BUFFER_SIZE, &status, outputLocation, inputLocation);
        if (status == COMMAND_END_OF_FILE)
        {
            // stdin has reached EOF, so it's time to be done; this often happens
            // when the user hits Ctrl-D
            break;

        }
        else if (status == COMMAND_INPUT_FAILED)
        {
            fprintf(stderr, "There was a problem reading your command. Please try again.\n");
            // we could try to analyze the error using ferror and respond in different
            // ways depending on the error, but instead, let's just bail
            break;

        }
        else if (status == COMMAND_TOO_LONG)
        {
            fprintf(stderr, "Commands are limited to length %d. Please try again.\n", MAX_COMMAND_SIZE);

        }
        else
        {
            execute_command(result, outputLocation, inputLocation);
        }
    }

    return 0;
}

// makes ctrl c not exit the sheell but instead stops application running in shell
void signal_interrupt()
{
    if (signal(SIGINT, interrupt_handler) != SIG_DFL) // register for CTRL-C
    {
        fprintf(stderr, "I'm confused.\n");
    }
}

// Handle registered interrupts
void interrupt_handler(int sig)
{
    fprintf(stderr, "\n");
    fflush(stderr);
}

/*
    Retrieves next line of input from command line and parses input to get 
    individual arguments

    Returns:
        a pointer to an array of strings containing parsed command line
        updates status to see if the command was inoutted correctly
        if standard output changes updates gets standard output location

    Preconditions:
        - buffer_size > 0
        - command_buffer != NULL
        - command_buffer points to a buffer large enough for at least buffer_size chars
*/
char ***get_command(char *command_buffer, int buffer_size, int *status, char *outputLocation, char *inputLocation)
{
    assert(buffer_size > 0);
    assert(command_buffer != NULL);

    // checking input
    if (fgets(command_buffer, buffer_size, stdin) == NULL)
    {
        if (feof(stdin))
        {
            *status = COMMAND_END_OF_FILE;
            return NULL;
        }
        else
        {
            *status = COMMAND_INPUT_FAILED;
            return NULL;
        }
    }

    int command_length = strlen(command_buffer);
    if (command_buffer[command_length - 1] != '\n')
    {
        // If we get here, the input line hasn't been fully read yet.
        // We need to read the rest of the input line so the unread portion
        // of the line doesn't corrupt the next command the user types.
        // Note that we won't store what we read, now, though, as the
        // user hasn't entered a valid command (because it's too long).
        char ch = getchar();
        while (ch != '\n' && ch != EOF)
        {
            ch = getchar();
        }

        *status = COMMAND_TOO_LONG;
    }

    // remove the newline character
    command_buffer[command_length - 1] = '\0';

    // checks if a pipe exists
    int pipe_index = is_pipe(command_buffer);

    // creates array of arrays containing parsed elements to be returned
    char ***parsed_elements = (char ***) malloc(sizeof(char **) * 2);

    // parses the first command
    int number_of_args = count_args(command_buffer, outputLocation, inputLocation, 0);
    char **my_argv = (char **) malloc(sizeof(char *) * (number_of_args + 1));
    parse_args(my_argv, number_of_args, command_buffer, 0);
    parsed_elements[0] = my_argv;

    // if there is a pipe, parses second command
    if (pipe_index)
    {
        number_of_args = count_args(command_buffer, outputLocation, inputLocation, pipe_index + 1);
        char **my_argv = (char **) malloc(sizeof(char *) * (number_of_args + 1));
        parse_args(my_argv, number_of_args, command_buffer, pipe_index + 1);
        parsed_elements[1] = my_argv;
    } 
    // If no pipe, sets second command to NULL
    else 
    {
        parsed_elements[1] = NULL;
    }

    return parsed_elements;
}


// Counts the number of arguments that exist within an input command, returns the number as
// an integer value
// while doing so, this function calls set_redirection to deal with redirection of stdin and stdout
int count_args (char *command_buffer, char *outputLocation, char *inputLocation, int index){
    int number_of_spaces = 0;
    int i = index;

    // removes leading spaces from command line input
    while (command_buffer[i] == ' ' && command_buffer[i] != '\0')
    {
        i++;
    }

    // finds the amount of arguments inputed in command line
    while (command_buffer[i] != '\0' && command_buffer[i] != '>' && command_buffer[i] != '<' && command_buffer[i] != '|')
    {
        if (command_buffer[i] == ' ')
        {
            while (command_buffer[i] == ' '){
                i++;
            }
            if (command_buffer[i] != '\0' && command_buffer[i] != '>' && command_buffer[i] != '<' && command_buffer[i] != '|'){
                number_of_spaces++;
            }
        } 
        else 
        {
            i++;
        }
    }

    // deals with redirection
    set_redirection (command_buffer, outputLocation, inputLocation, i);

    // Add 1 to number of spaces to get number of arguments
    // ex. "arg1 arg2" has 1 space but 2 arguments
    return number_of_spaces + 1;
}

// If needed, stores location of redirection of stdin and stdout to be updated later when executing the input commands
void set_redirection (char* command_buffer, char *outputLocation, char *inputLocation, int index){
    int i = index;
    while (command_buffer[i] != '\0' && command_buffer[i] != '|')
    {
        while (command_buffer[i] == ' ' )
            {
                i++;
            }
        // sees if stdout is redirected, stores updated stdout location
        // to update stdout later
        if (command_buffer[i] == '>')
        {
            int stringIndex = 0;
            i++;
            while (command_buffer[i] == ' ' )
            {
                i++;
            }
            while (command_buffer[i] != ' ' && command_buffer[i] != '\0')
            {
                outputLocation[stringIndex] = command_buffer[i];
                stringIndex++;
                i++;
            }
            outputLocation[stringIndex] = '\0';
        }
        // sees if stdin is redirected, stores updated stdin location
        // to update stdin later
        else if (command_buffer[i] == '<') 
        {
            int stringIndex = 0;
            i++;

          

            while (command_buffer[i] == ' ' ){
                i++;
            }

            

            while (command_buffer[i] != ' ' && command_buffer[i] != '\0'){
                inputLocation[stringIndex] = command_buffer[i];
                stringIndex++;
                i++;
            }
            inputLocation[stringIndex] = '\0';
            
        }
    }
}

// Reads through input in order to see if there is a pipe in the input command. If so,
// this function returns the index into the input string of the pipe, if not it returns 0
int is_pipe(char *command_buffer){
    int i = 0;
    while (command_buffer[i] != '\0'){
        if (command_buffer[i] == '|'){
            return i;
        }
        i++;
    }
    return 0;
}

// Parses an input command into its elements and puts each inside of an array that has been passed
// into this function
void parse_args(char **my_argv, int number_of_args, char *command_buffer, int index){
    int currentPosition = index;
    for (int k = 0; k < number_of_args; k++)
    {   
        // creates individual argument strings
        my_argv[k] = (char *) malloc(20*sizeof(char));
        if (my_argv[k] != NULL)
        {   
            
            // removes leading spaces
            while (command_buffer[currentPosition] == ' ' && command_buffer[currentPosition] != '\0'){
                currentPosition++;
            }
            
            // copies input argument into array until next space or end of input is reached
            int positionInArg = 0;
            while (command_buffer[currentPosition] != ' ' && command_buffer[currentPosition] != '\0'){
                my_argv[k][positionInArg] = command_buffer[currentPosition];
                positionInArg++;
                currentPosition++;
            }
            currentPosition++;
            positionInArg++;
            my_argv[k][positionInArg] = '\0';
        }
        else
        {
            fprintf(stderr, "Malloc failed\n");
            exit(1);
        }
    }
    // sets final element of array to NULL
    my_argv[number_of_args] = NULL;
}



// executes the parsed input command and deals with file redirection
// forks the process in order to do so
void execute_command(char ***parsed_command, char *outputLocation, char *inputLocation)
{
    if (fork() == 0)
    {
        /* Child */
        fflush(stdout);


        // If there is a pipe in the command, this will be executed
        if (parsed_command[1]){
            int fd[2];

            // Set up the file descriptors for the pipe
            if (pipe(fd) < 0)
            {
                perror("Trouble creating pipe");
                exit(1);
            }

            // Fork the child process for the pipe
            int pid = fork();
            if (pid < 0)
            {
                perror("Trouble forking");
                exit(1);
            }

            if (pid != 0)
            {
                /*Piped Parent */
                close(fd[0]);
                if (dup2(fd[1], STDOUT_FILENO) == -1) // set fd[1] to "out"
                {
                    perror("Trouble redirecting stdout");
                }
                close(fd[1]);

                // redirects stdin if needed
                if (inputLocation[0])
                    {
                        redirect_stdin(inputLocation);
                    }

                // executes command
                execute_single_command(parsed_command[0]);
            }
            else
            {
                /*Piped Child */
                close(fd[1]);
                if (dup2(fd[0], STDIN_FILENO ) == -1) // set fd[0] to "in"
                {
                    perror("Trouble redirecting stdin");
                }
                close(fd[0]);

                // redirects stdout if needed
                if (outputLocation[0])
                    {
                        redirect_stdout(outputLocation);
                    }

                // executes command
                execute_single_command(parsed_command[1]);
    
            }
        }

        // if there is not a pipe, this will be executed
        else {
            // changes stdout and stdin if necessary
            if (outputLocation[0])
            {
                redirect_stdout(outputLocation);
            }
            if (inputLocation[0])
            {
                redirect_stdin(inputLocation);
            }
            // executes the command
            execute_single_command(parsed_command[0]);
        }
    }

    else
    {
        /* Parent */

        fflush(stdout);

        // Wait for the child to finish execution
        int status;
        wait(&status);

        // if type exit, parent program ends
        if (!strcmp("exit", parsed_command[0][0]))
        {
            exit(0);
        } 

        // free stored input
        //free_array(parsed_command);
    
        // Note: this above bit of commented out code should be here and included
        // However, including it leads to some weird issue that breaks the functionality
        // of my shell. Therefore, I left it commented out to show that it should be there
        // but could not figure out the bug. Clearly, memory leaks are bad but I decided
        // to choose functionality of my shell over no memory leaks

        fflush(stdout);
    }
}

// Executesa single command with its arguments
void execute_single_command (char **command){

    // special cases help and exit
    if (!strcmp("help", command[0]))
    {
        display_help();
    } 
    else if (!strcmp("exit", command[0]))
    {
        exit(0);
    } 
    else 
    {
        // executes command
        execvp(command[0], command);

        perror("exec failed");

    }
    
    fflush(stdout);
    exit(0);
}

// Redirects stdout to specified output location
void redirect_stdout (char *outputLocation){
    int fd = open(outputLocation, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        perror("Trouble opening file");
        exit(1);
    }

    if (dup2(fd, STDOUT_FILENO) < 0)
    {
        perror("Trouble dup2-ing to stdout");
        close(fd);
        exit( 1 );
    }

    close(fd);
}

// Redirects stdin to specified input location
void redirect_stdin (char *inputLocation){
    int fd = open(inputLocation, O_RDONLY, 0644);
    if (fd < 0)
    {
        perror("Trouble opening file");
        exit(1);
    }

    if (dup2(fd, STDIN_FILENO) < 0)
    {
        perror("Trouble dup2-ing to stdin");
        close(fd);
        exit( 1 );
    }

    close(fd);
}

void display_help (){
    //output for help
    printf("\nHere is the help menu\n\nThis shell program supports the following functionality:\n");
    printf("this help command\ncommand line arguments\nredirection of stdout via >\n");
    printf("redirection of stdin via <\na single pipe\nsignal interupt using ctrl C\n\n");
    printf("Type exit to quit this program\n\n");
}

// function to free the array malloced to parse input
void free_array (char ***my_array){
    if (my_array)
    {
        int inner;
        for (int outer = 0; outer < 2; outer++)
        {
            if (my_array[outer])
            {
                inner = 0;
                while (my_array[outer][inner])
                {
                    free(my_array[outer][inner]);
                    my_array[outer][inner] = NULL;
                    inner++;
                }
                free(my_array[outer]);
                my_array[outer] = NULL;
            }

        }
   
        free(my_array);
        my_array = NULL;
    }
}


