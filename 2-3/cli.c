//////////////////////////////////////////////////////////////////////////////////
// File Name  : cli.c                                                           //
// Date       : 2026/05/13                                                      //
// OS         : Ubuntu 20.04.6 LTS 64bits                                       //
// Author     : Jo Young Hoon                                                   //
// Student ID : 2022202078                                                      //
// -----------------------------------------------------------------------------//
// Title      : System Programming Assignment 2-3 Client                        //
// Description:                                                                 //
//   This client connects to the FTP server through a TCP socket. It converts   //
//   user commands into FTP commands, sends them to the server, receives the    //
//   execution result, and prints the result. If the user enters quit or        //
//   presses Ctrl+C, the client sends QUIT before terminating.                  //
//////////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_BUFF 4096
#define MAX_ARGC 32

static int sockfd = -1;
static volatile sig_atomic_t g_sigint = 0;

static void sigint_handler(int signo);
static ssize_t sender(int fd, const void *buf, size_t len);
static ssize_t receiver(int fd, void *buf, size_t len);
static int send_message(int fd, const char *msg);
static int recv_message(int fd, char **msg);
static int split_args(char *line, char *args[], int max_args);
static int append_token(char *dst, size_t dst_size, const char *token);
static int append_raw(char *dst, size_t dst_size, const char *src);
static int conv_cmd(char *buff, char *cmd_buff, size_t cmd_size);
static void send_quit_if_connected(void);

///////////////////////////////////////////////////////////////////////////////
// sigint_handler                                                            //
// Input   : int signo - received signal number                              //
// Output  : void                                                            //
// Purpose : Records Ctrl+C so the main loop can send QUIT safely.           //
///////////////////////////////////////////////////////////////////////////////
static void sigint_handler(int signo)
{
    // Ignore the signal number and just set the flag to indicate SIGINT was received.
    (void)signo;
    g_sigint = 1;
}

///////////////////////////////////////////////////////////////////////////////
// sender                                                                    //
// Input   : int fd          - socket descriptor                             //
//           const void *buf - data buffer                                   //
//           size_t len      - bytes to send                                 //
// Output  : ssize_t         - sent byte count, -1 on error                  //
// Purpose : Sends all bytes in the buffer to the server.                    //
///////////////////////////////////////////////////////////////////////////////
static ssize_t sender(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    const char *ptr = (const char *)buf;

    // Loop until all bytes are sent
    while (total < len)
    {
        // Write the remaining bytes to the socket
        ssize_t n = write(fd, ptr + total, len - total);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                // If interrupted by a signal, check if it's SIGINT and return -1 to allow graceful shutdown.
                if (g_sigint)
                {
                    return -1;
                }
                continue;
            }
            return -1;
        }
        // If write returns 0, it means the connection was closed by the server.
        if (n == 0)
        {
            break;
        }
        // Update the total bytes sent
        total += (size_t)n;
    }
    return (ssize_t)total;
}

///////////////////////////////////////////////////////////////////////////////
// receiver                                                                  //
// Input   : int fd     - socket descriptor                                  //
//           void *buf  - receive buffer                                     //
//           size_t len - bytes to receive                                   //
// Output  : ssize_t    - received byte count, -1 on error                   //
// Purpose : Receives the exact number of bytes from the server.             //
///////////////////////////////////////////////////////////////////////////////
static ssize_t receiver(int fd, void *buf, size_t len)
{
    size_t total = 0;
    char *ptr = (char *)buf;
    // Loop until the requested number of bytes is received
    while (total < len)
    {
        // Read from the socket into the buffer
        ssize_t n = read(fd, ptr + total, len - total);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        // If read returns 0, it means the connection was closed by the server.
        if (n == 0)
        {
            return 0;
        }
        // Update the total bytes received
        total += (size_t)n;
    }
    return (ssize_t)total;
}

///////////////////////////////////////////////////////////////////////////////
// send_message                                                              //
// Input   : int fd          - socket descriptor                             //
//           const char *msg - FTP command                                   //
// Output  : int             - 0 success, -1 fail                            //
// Purpose : Sends a length-prefixed command to the server.                  //
///////////////////////////////////////////////////////////////////////////////
static int send_message(int fd, const char *msg)
{
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);
    // Send the length prefix first
    if (sender(fd, &net_len, sizeof(net_len)) != (ssize_t)sizeof(net_len))
    {
        return -1;
    }
    // Then send the actual message
    if (sender(fd, msg, len) != (ssize_t)len)
    {
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// recv_message                                                              //
// Input   : int fd     - socket descriptor                                  //
//           char **msg - server response buffer                             //
// Output  : int        - 0 success, -1 fail                                 //
// Purpose : Receives a length-prefixed response from the server.            //
///////////////////////////////////////////////////////////////////////////////
static int recv_message(int fd, char **msg)
{
    uint32_t net_len;
    uint32_t len;
    char *buffer;
    *msg = NULL;

    // First receive the length prefix
    if (receiver(fd, &net_len, sizeof(net_len)) != (ssize_t)sizeof(net_len))
    {
        return -1;
    }
    // Convert the length from network byte order to host byte order
    len = ntohl(net_len);
    buffer = (char *)malloc((size_t)len + 1U);
    if (buffer == NULL)
    {
        return -1;
    }
    // Then receive the actual message
    if (receiver(fd, buffer, len) != (ssize_t)len)
    {
        free(buffer);
        return -1;
    }
    // Null-terminate the received message and assign it to the output parameter
    buffer[len] = '\0';
    *msg = buffer;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// split_args                                                                //
// Input   : char *line   - command line to split                            //
//           char *args[] - token array                                      //
//           int max_args - maximum token count                              //
// Output  : int          - parsed token count                               //
// Purpose : Splits user input into blank-separated tokens.                  //
///////////////////////////////////////////////////////////////////////////////
static int split_args(char *line, char *args[], int max_args)
{
    int count = 0;
    char *saveptr;
    char *token;
    // Use strtok_r to split the line into tokens based on whitespace characters
    token = strtok_r(line, " \t\r\n", &saveptr);
    // Loop through the tokens and store them in the args array until max_args is reached
    while (token != NULL && count < max_args - 1)
    {
        args[count++] = token;
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    // Null-terminate the args array and return the token count
    args[count] = NULL;

    return count;
}

///////////////////////////////////////////////////////////////////////////////
// append_token                                                              //
// Input   : char *dst          - destination command buffer                 //
//           size_t dst_size    - destination buffer size                    //
//           const char *token  - token to append                            //
// Output  : int                - 0 success, -1 fail                         //
// Purpose : Appends one blank and one token to a command string.            //
///////////////////////////////////////////////////////////////////////////////
static int append_token(char *dst, size_t dst_size, const char *token)
{
    size_t used = strlen(dst);
    int written;
    // Check if there is enough space to append a blank and the token
    if (used >= dst_size)
    {
        return -1;
    }
    // Append a blank and the token to the destination buffer
    written = snprintf(dst + used, dst_size - used, " %s", token);
    if (written < 0 || (size_t)written >= dst_size - used)
    {
        return -1;
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// append_raw                                                                //
// Input   : char *dst       - destination command buffer                    //
//           size_t dst_size - destination buffer size                       //
//           const char *src - string to append                              //
// Output  : int             - 0 success, -1 fail                            //
// Purpose : Appends a raw string while checking the destination size.       //
///////////////////////////////////////////////////////////////////////////////
static int append_raw(char *dst, size_t dst_size, const char *src)
{
    // Calculate the current length of the destination string and the length of the source string
    size_t used = strlen(dst);
    size_t add = strlen(src);
    // Check if there is enough space to append the raw string
    if (used + add >= dst_size)
    {
        return -1;
    }
    // Append the raw string to the destination buffer
    memcpy(dst + used, src, add + 1U);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// conv_cmd                                                                  //
// Input   : char *buff      - user command                                  //
//           char *cmd_buff  - converted FTP command                         //
//           size_t cmd_size - converted command buffer size                 //
// Output  : int             - 0 success, -1 fail                            //
// Purpose : Converts user commands into FTP commands required by the server.//
///////////////////////////////////////////////////////////////////////////////
static int conv_cmd(char *buff, char *cmd_buff, size_t cmd_size)
{
    char *args[MAX_ARGC];
    int argc;
    int i;
    int a_flag;
    int l_flag;

    // Split the user input into arguments
    cmd_buff[0] = '\0';
    argc = split_args(buff, args, MAX_ARGC);
    // Check if at least a command is provided
    if (argc == 0)
    {
        return -1;
    }
    // ls -> NLST
    if (strcmp(args[0], "ls") == 0)
    {
        a_flag = 0;
        l_flag = 0;
        strcpy(cmd_buff, "NLST");
        // Parse options '-a' and '-l'
        for (i = 1; i < argc; i++)
        {
            // If the argument starts with a hyphen, it is treated as an option
            if (args[i][0] == '-')
            {
                int j;
                // If the option is just a hyphen with no characters, it's invalid
                if (args[i][1] == '\0')
                {
                    write(STDERR_FILENO, "Error: invalid option\n",
                          sizeof("Error: invalid option\n") - 1);
                    return -1;
                }
                // Check each character in the option string for valid options 'a' and 'l'
                for (j = 1; args[i][j] != '\0'; j++)
                {
                    if (args[i][j] == 'a')
                    {
                        a_flag = 1;
                    }
                    else if (args[i][j] == 'l')
                    {
                        l_flag = 1;
                    }
                    else
                    {
                        write(STDERR_FILENO, "Error: invalid option\n",
                              sizeof("Error: invalid option\n") - 1);
                        return -1;
                    }
                }
            }
        }
        // Append options to the command if they were specified
        if (a_flag && l_flag)
        {
            if (append_token(cmd_buff, cmd_size, "-al") < 0)
            {
                return -1;
            }
        }
        // If only one of the options was specified, append it to the command
        else if (a_flag)
        {
            if (append_token(cmd_buff, cmd_size, "-a") < 0)
            {
                return -1;
            }
        }
        // If only one of the options was specified, append it to the command
        else if (l_flag)
        {
            if (append_token(cmd_buff, cmd_size, "-l") < 0)
            {
                return -1;
            }
        }
        // Append any remaining arguments (e.g., target path) to the command
        for (i = 1; i < argc; i++)
        {
            if (args[i][0] != '-')
            {
                if (append_token(cmd_buff, cmd_size, args[i]) < 0)
                {
                    return -1;
                }
            }
        }
        return 0;
    }
    // dir -> LIST
    if (strcmp(args[0], "dir") == 0)
    {
        strcpy(cmd_buff, "LIST");
        // 'dir' does not take any options, so any option flag is an error
        for (i = 1; i < argc; i++)
        {
            if (args[i][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
                return -1;
            }
            if (append_token(cmd_buff, cmd_size, args[i]) < 0)
            {
                return -1;
            }
        }
        return 0;
    }
    // pwd -> PWD
    if (strcmp(args[0], "pwd") == 0)
    {
        // 'pwd' takes absolutely no arguments or options, so any additional token is an error
        if (argc > 1)
        {
            if (args[1][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
            }
            else
            {
                write(STDERR_FILENO, "Error: argument is not required\n",
                      sizeof("Error: argument is not required\n") - 1);
            }
            return -1;
        }
        strcpy(cmd_buff, "PWD");
        return 0;
    }
    // cd -> CWD or CDUP
    if (strcmp(args[0], "cd") == 0)
    {
        // Require exactly one target directory, which cannot be an option
        if (argc < 2)
        {
            write(STDERR_FILENO, "Error: argument is required\n",
                  sizeof("Error: argument is required\n") - 1);
            return -1;
        }
        // If there are more than 2 tokens, it's an error because 'cd' only takes one argument
        if (argc > 2)
        {
            write(STDERR_FILENO, "Error: only one argument is valid\n",
                  sizeof("Error: only one argument is valid\n") - 1);
            return -1;
        }
        // Prevent options passed to 'cd' since it only takes a directory argument
        if (args[1][0] == '-')
        {
            write(STDERR_FILENO, "Error: invalid option\n",
                  sizeof("Error: invalid option\n") - 1);
            return -1;
        }
        // If the argument is "..", translate to CDUP, otherwise translate to CWD
        if (strcmp(args[1], "..") == 0)
        {
            strcpy(cmd_buff, "CDUP");
            return 0;
        }
        // For any other directory argument, translate to CWD followed by the directory name
        snprintf(cmd_buff, cmd_size, "CWD %s", args[1]);
        return 0;
    }
    // mkdir -> MKD
    if (strcmp(args[0], "mkdir") == 0)
    {
        // 'mkdir' requires at least one argument (the directory name)
        if (argc < 2)
        {
            write(STDERR_FILENO, "Error: argument is required\n",
                  sizeof("Error: argument is required\n") - 1);
            return -1;
        }
        // Process each directory name argument sequentially, ensuring they are not options
        for (i = 1; i < argc; i++)
        {
            // If the argument starts with a hyphen, it is treated as an invalid option
            if (args[i][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
                return -1;
            }
            // Append a newline before each MKD command if there are multiple directories, then append the MKD command with the directory name
            if (i > 1 && append_raw(cmd_buff, cmd_size, "\n") < 0)
            {
                return -1;
            }
            // Append the MKD command followed by the directory name to the command buffer
            if (append_raw(cmd_buff, cmd_size, "MKD ") < 0 ||
                append_raw(cmd_buff, cmd_size, args[i]) < 0)
            {
                return -1;
            }
        }
        return 0;
    }
    // delete -> DELE
    if (strcmp(args[0], "delete") == 0)
    {
        // 'delete' requires at least one argument (the file name)
        if (argc < 2)
        {
            write(STDERR_FILENO, "Error: argument is required\n",
                  sizeof("Error: argument is required\n") - 1);
            return -1;
        }
        // Process each file name argument sequentially, ensuring they are not options
        for (i = 1; i < argc; i++)
        {
            // If the argument starts with a hyphen, it is treated as an invalid option
            if (args[i][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
                return -1;
            }
            // Append a newline before each DELE command if there are multiple files, then append the DELE command with the file name
            if (i > 1 && append_raw(cmd_buff, cmd_size, "\n") < 0)
            {
                return -1;
            }
            // Append the DELE command followed by the file name to the command buffer
            if (append_raw(cmd_buff, cmd_size, "DELE ") < 0 ||
                append_raw(cmd_buff, cmd_size, args[i]) < 0)
            {
                return -1;
            }
        }
        return 0;
    }
    // rmdir -> RMD
    if (strcmp(args[0], "rmdir") == 0)
    {
        // 'rmdir' requires at least one argument (the directory name)
        if (argc < 2)
        {
            write(STDERR_FILENO, "Error: argument is required\n",
                  sizeof("Error: argument is required\n") - 1);
            return -1;
        }
        // Process each directory name argument sequentially, ensuring they are not options
        for (i = 1; i < argc; i++)
        {
            // If the argument starts with a hyphen, it is treated as an invalid option
            if (args[i][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
                return -1;
            }
            // Append a newline before each RMD command if there are multiple directories, then append the RMD command with the directory name
            if (i > 1 && append_raw(cmd_buff, cmd_size, "\n") < 0)
            {
                return -1;
            }
            // Append the RMD command followed by the directory name to the command buffer
            if (append_raw(cmd_buff, cmd_size, "RMD ") < 0 ||
                append_raw(cmd_buff, cmd_size, args[i]) < 0)
            {
                return -1;
            }
        }
        return 0;
    }
    // rename -> RNFR + RNTO
    if (strcmp(args[0], "rename") == 0)
    {
        // 'rename' requires exactly two arguments: the old name and the new name, which cannot be options
        if (argc != 3)
        {
            write(STDERR_FILENO, "Error: two argument are required\n",
                  sizeof("Error: two argument are required\n") - 1);
            return -1;
        }
        //  Check both arguments to ensure they aren't options
        if (args[1][0] == '-' || args[2][0] == '-')
        {
            write(STDERR_FILENO, "Error: invalid option\n",
                  sizeof("Error: invalid option\n") - 1);
            return -1;
        }
        // Format the command buffer with the RNFR and RNTO commands for renaming the file or directory
        snprintf(cmd_buff, cmd_size, "RNFR %s RNTO %s", args[1], args[2]);
        return 0;
    }
    // quit -> QUIT
    if (strcmp(args[0], "quit") == 0)
    {
        // 'quit' should not have any trailing arguments, so if there are any additional tokens, it's an error
        if (argc > 1)
        {
            // If the first additional token starts with a hyphen, print an error message indicating that it's an invalid option. Otherwise, print an error message indicating that no argument is required.
            if (args[1][0] == '-')
            {
                write(STDERR_FILENO, "Error: invalid option\n",
                      sizeof("Error: invalid option\n") - 1);
            }
            else
            {
                write(STDERR_FILENO, "Error: argument is not required\n",
                      sizeof("Error: argument is not required\n") - 1);
            }
            return -1;
        }
        // Format the command buffer with the QUIT command to signal the server that the client is disconnecting
        strcpy(cmd_buff, "QUIT");
        return 0;
    }
    // If the command does not match any of the recognized commands, print an error message indicating that the command is invalid
    write(STDERR_FILENO, "Invalid command\n", sizeof("Invalid command\n") - 1);
    return -1;
}

///////////////////////////////////////////////////////////////////////////////
// send_quit_if_connected                                                    //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Sends QUIT to the server before client termination.             //
///////////////////////////////////////////////////////////////////////////////
static void send_quit_if_connected(void)
{
    // If the socket is valid, send a QUIT command to the server to allow for graceful disconnection before the client terminates.
    if (sockfd >= 0)
    {
        send_message(sockfd, "QUIT");
    }
}

///////////////////////////////////////////////////////////////////////////////
// main                                                                      //
// Input   : int argc      - argument count                                  //
//           char **argv   - server IP address and port number               //
// Output  : int           - process exit status                             //
// Purpose : Connects to the server and processes user commands.             //
///////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    char buff[MAX_BUFF];
    char cmd_buff[MAX_BUFF];
    char *rcv_buff;
    struct sockaddr_in server;
    struct sigaction act;
    fd_set readfds;
    int maxfd;
    int ready;
    char probe;
    ssize_t peeked;

    // Check command-line arguments
    if (argc != 3)
    {
        write(STDERR_FILENO, "Usage: ./cli <server_ip> <port>\n",
              sizeof("Usage: ./cli <server_ip> <port>\n") - 1);
        return 1;
    }

    // Set up signal handler for SIGINT
    memset(&act, 0, sizeof(act));
    act.sa_handler = sigint_handler;
    sigemptyset(&act.sa_mask);
    if (sigaction(SIGINT, &act, NULL) < 0)
    {
        perror("sigaction");
        return 1;
    }
    // Create TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }
    // Set up server address structure
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((uint16_t)atoi(argv[2]));

    // Convert the server IP address from text to binary form
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        sockfd = -1;
        return 1;
    }
    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("connect");
        close(sockfd);
        sockfd = -1;
        return 1;
    }
    // Main loop to process user commands and server responses
    for (;;)
    {
        // Check for SIGINT before waiting for input
        if (g_sigint)
        {
            write(STDOUT_FILENO, "\n", 1);
            send_quit_if_connected();
            break;
        }
        // Prompt the user for a command
        write(STDOUT_FILENO, "> ", 2);

        // Use select to wait for user input, SIGINT, or server disconnect.
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        FD_SET(sockfd, &readfds);
        maxfd = (sockfd > STDIN_FILENO) ? sockfd : STDIN_FILENO;

        // Wait for input
        ready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (ready < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("select");
            break;
        }

        // If the server closed the socket, exit even while waiting for input.
        if (FD_ISSET(sockfd, &readfds))
        {
            // Use recv with MSG_PEEK to check if the server has disconnected without consuming any data from the socket.
            peeked = recv(sockfd, &probe, 1, MSG_PEEK);
            if (peeked == 0)
            {
                write(STDOUT_FILENO, "\nServer disconnected\n",
                      sizeof("\nServer disconnected\n") - 1);
                break;
            }
            // If recv returns -1, check if it was due to an interrupt. If it was an interrupt caused by SIGINT, allow the loop to handle it and exit gracefully. Otherwise, print the error and break the loop.
            if (peeked < 0)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                perror("recv");
                break;
            }
            // If the server socket is ready but recv did not indicate a disconnect, it means there is unexpected data from the server that we are not prepared to handle at this point, so print an error message and break the loop.
            if (!FD_ISSET(STDIN_FILENO, &readfds))
            {
                write(STDOUT_FILENO, "\nUnexpected server data\n",
                      sizeof("\nUnexpected server data\n") - 1);
                break;
            }
        }

        // Check if the user entered a command
        if (FD_ISSET(STDIN_FILENO, &readfds))
        {
            // Read user input
            if (fgets(buff, sizeof(buff), stdin) == NULL)
            {
                // Check for EOF or error
                if (g_sigint)
                {
                    write(STDOUT_FILENO, "\n", 1);
                    send_quit_if_connected();
                }
                break;
            }
            // Convert the user command to an FTP command
            if (conv_cmd(buff, cmd_buff, sizeof(cmd_buff)) < 0)
            {
                continue;
            }
            // Send the command to the server
            if (send_message(sockfd, cmd_buff) < 0)
            {
                perror("write");
                close(sockfd);
                sockfd = -1;
                return 1;
            }
            // Check for QUIT command to exit the program
            if (strcmp(cmd_buff, "QUIT") == 0)
            {
                break;
            }
            // Receive the message from the server
            if (recv_message(sockfd, &rcv_buff) < 0)
            {
                if (g_sigint)
                {
                    write(STDOUT_FILENO, "\n", 1);
                    send_quit_if_connected();
                    break;
                }
                write(STDOUT_FILENO, "Server disconnected\n",
                      sizeof("Server disconnected\n") - 1);
                break;
            }
            // Print the server response
            write(STDOUT_FILENO, rcv_buff, strlen(rcv_buff));
            free(rcv_buff);
        }
    }
    // Clean up and exit
    close(sockfd);
    return 0;
}
