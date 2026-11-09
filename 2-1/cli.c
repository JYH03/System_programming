///////////////////////////////////////////////////////////////////////////////
// File Name  : cli.c
// Date       : 2026/04/30
// OS         : Ubuntu 20.04.6 LTS 64bits
// Author     : Jo Young Hoon
// Student ID : 2022202078
// ---------------------------------------------------------------------------
// Title      : System Programming Assignment 2-1 Client
// Description: Converts user commands to FTP commands and communicates with
//              the FTP server using TCP sockets.
///////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_BUFF 1024
#define RCV_BUFF 4096
#define MAX_ARGC 16

///////////////////////////////////////////////////////////////////////////////
// split_command
// Input   : char *line    - command line
//           char **argv   - argument vector
//           int max_argc  - maximum argument count
// Output  : int           - argument count, -1 fail
// Purpose : Splits a command line into argv form for getopt().
///////////////////////////////////////////////////////////////////////////////
static int split_command(char *line, char **argv, int max_argc)
{
    int argc = 0;
    char *token;

    // Use strtok to split the line into tokens based on space and tab delimiters
    token = strtok(line, " \t");
    while (token != NULL)
    {
        if (argc >= max_argc)
        {
            return -1;
        }
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    // Null-terminate the argv array
    argv[argc] = NULL;
    return argc;
}

///////////////////////////////////////////////////////////////////////////////
// conv_cmd
// Input   : char *buff     - user command
//           char *cmd_buff - converted FTP command
// Output  : int            - 0 success, -1 fail
// Purpose : Converts ls, ls options, and quit to NLST and QUIT.
///////////////////////////////////////////////////////////////////////////////
static int conv_cmd(char *buff, char *cmd_buff)
{
    char temp[MAX_BUFF];
    char *argv[MAX_ARGC + 1];
    int argc;
    int option;
    int a_flag = 0;
    int l_flag = 0;

    // Remove the newline character from the end of the input command
    buff[strcspn(buff, "\n")] = '\0';
    strcpy(temp, buff);

    // Split the command line into arguments and count them
    argc = split_command(temp, argv, MAX_ARGC);
    if (argc <= 0)
    {
        return -1;
    }

    // Check if the command is "quit" with no options
    if (argc == 1 && strcmp(argv[0], "quit") == 0)
    {
        strcpy(cmd_buff, "QUIT");
        return 0;
    }
    // Check if the command is "ls" and parse options
    if (strcmp(argv[0], "ls") != 0)
    {
        return -1;
    }

    opterr = 0;
    optind = 0;
    while ((option = getopt(argc, argv, "al")) != -1)
    {
        switch (option)
        {
        case 'a':
            a_flag = 1;
            break;
        case 'l':
            l_flag = 1;
            break;
        default:
            return -1;
        }
    }
    // Check if there are any non-option arguments left after parsing options
    if (optind != argc)
    {
        return -1;
    }

    // Construct the FTP command based on the options provided
    if (a_flag && l_flag)
    {
        strcpy(cmd_buff, "NLST -al");
    }
    else if (a_flag)
    {
        strcpy(cmd_buff, "NLST -a");
    }
    else if (l_flag)
    {
        strcpy(cmd_buff, "NLST -l");
    }
    else
    {
        strcpy(cmd_buff, "NLST");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// main
// Input   : int argc    - number of command-line arguments
//           char **argv - server IP address and port number
// Output  : int         - 0 success, 1 fail
// Purpose : Connects to the server, sends converted FTP commands, and displays
//           the server response.
///////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    char buff[MAX_BUFF];
    char cmd_buff[MAX_BUFF];
    char rcv_buff[RCV_BUFF];
    int n;
    int sockfd;
    struct sockaddr_in server;

    // Check command-line arguments
    if (argc != 3)
    {
        write(STDERR_FILENO, "Usage: ./cli <server_ip> <port>\n",
              sizeof("Usage: ./cli <server_ip> <port>\n") - 1);
        return 1;
    }

    // Create a TCP socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    // Set up the server address structure
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons((unsigned short)atoi(argv[2]));

    // Convert the server IP address from text to binary form
    if (inet_pton(AF_INET, argv[1], &server.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    // Connect to the server
    if (connect(sockfd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // Main loop to read user commands, convert them, and communicate with the server
    for (;;)
    {
        // Prompt the user for input
        write(STDOUT_FILENO, "> ", 2);
        if (fgets(buff, sizeof(buff), stdin) == NULL)
        {
            break;
        }

        // Convert the user command to an FTP command
        if (conv_cmd(buff, cmd_buff) < 0)
        {
            write(STDERR_FILENO, "conv_cmd() error\n",
                  sizeof("conv_cmd() error\n") - 1);
            continue;
        }

        // Send the converted command to the server
        n = strlen(cmd_buff);
        if (write(sockfd, cmd_buff, n) != n)
        {
            write(STDERR_FILENO, "write() error\n",
                  sizeof("write() error\n") - 1);
            close(sockfd);
            return 1;
        }

        // Read the response from the server
        if ((n = read(sockfd, rcv_buff, RCV_BUFF - 1)) < 0)
        {
            write(STDERR_FILENO, "read() error\n",
                  sizeof("read() error\n") - 1);
            close(sockfd);
            return 1;
        }
        if (n == 0)
        {
            break;
        }
        rcv_buff[n] = '\0';

        // Check if the server response is "QUIT" and exit if it is
        if (strcmp(rcv_buff, "QUIT") == 0)
        {
            write(STDOUT_FILENO, "Program quit!!\n",
                  sizeof("Program quit!!\n") - 1);
            break;
        }

        // Print the server response to the user
        write(STDOUT_FILENO, rcv_buff, strlen(rcv_buff));
    }

    // Close the socket and exit
    close(sockfd);
    return 0;
}
