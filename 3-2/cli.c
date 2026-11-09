////////////////////////////////////////////////////////////////////////////////
// File Name  : cli.c                                                         //
// Date       : 2026/05/22                                                    //
// OS         : Ubuntu 20.04.6 LTS 64bits                                     //
// Author     : Jo Young Hoon                                                 //
// Student ID : 2022202078                                                    //
// -------------------------------------------------------------------------- //
// Title      : System Programming Assignment #3-2 Client                     //
// Description: This client connects to the FTP server through the control    //
//              connection and receives directory list data through a         //
//              separate data connection created by the PORT command.         //
////////////////////////////////////////////////////////////////////////////////

#define _DEFAULT_SOURCE

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define MIN_DATA_PORT 10001
#define MAX_DATA_PORT 30000
#define BACKLOG 5
#define MAX_ARGC 32

char *convert_addr_to_str(unsigned long ip_addr, unsigned int port);

////////////////////////////////////////////////////////////////////////////////
// write_all                                                                  //
// ========================================================================== //
// Input   : int         - socket descriptor                                  //
//           const char* - data buffer                                        //
//           int         - data size                                          //
// Output  : void                                                             //
// Purpose : Sends all bytes in the buffer through the socket.                //
////////////////////////////////////////////////////////////////////////////////
static void write_all(int sock, const char *buf, int size)
{
    int sent;
    int total = 0;
    // Loop until all bytes are sent
    while (total < size)
    {
        // Send remaining bytes
        sent = write(sock, buf + total, size - total);
        if (sent <= 0)
        {
            perror("write");
            exit(1);
        }
        total += sent;
    }
}

////////////////////////////////////////////////////////////////////////////////
// split_args                                                                 //
// ========================================================================== //
// Input   : char*  - user command line                                       //
//           char** - parsed argument array                                   //
//           int    - maximum argument count                                  //
// Output  : int    - parsed argument count                                   //
// Purpose : Splits a command line into blank-separated tokens.               //
////////////////////////////////////////////////////////////////////////////////
static int split_args(char *line, char *args[], int max_args)
{
    int count = 0;
    char *saveptr;
    char *token;
    // Use strtok_r to split the line into tokens based on whitespace
    token = strtok_r(line, " \t\r\n", &saveptr);
    while (token != NULL && count < max_args - 1)
    {
        args[count++] = token;
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    args[count] = NULL;

    return count;
}

////////////////////////////////////////////////////////////////////////////////
// make_ftp_command                                                           //
// ========================================================================== //
// Input   : char* - user command line                                        //
//           char* - converted FTP command buffer                             //
//           int   - converted command buffer size                            //
// Output  : int   - 0 success, -1 fail                                       //
// Purpose : Converts ls with -a/-l options into an NLST command.             //
////////////////////////////////////////////////////////////////////////////////
static int make_ftp_command(char *input, char *cmd, int cmd_size)
{
    int argc;
    int i;
    int j;
    int a_flag = 0;
    int l_flag = 0;
    char *args[MAX_ARGC];
    // Split the input command into arguments
    argc = split_args(input, args, MAX_ARGC);
    if (argc == 0)
    {
        return -1;
    }
    // Check if the command is "ls"
    if (strcmp(args[0], "ls") != 0)
    {
        printf("Only ls command is supported.\n");
        return -1;
    }
    // Parse options and set flags
    for (i = 1; i < argc; i++)
    {
        if (args[i][0] == '-')
        {
            if (args[i][1] == '\0')
            {
                printf("Error: invalid option\n");
                return -1;
            }
            // Check each character in the option string
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
                    printf("Error: invalid option\n");
                    return -1;
                }
            }
        }
    }
    // Build the NLST command based on the flags and arguments
    snprintf(cmd, cmd_size, "NLST");
    if (a_flag && l_flag)
    {
        snprintf(cmd + strlen(cmd), cmd_size - strlen(cmd), " -al");
    }
    else if (a_flag)
    {
        snprintf(cmd + strlen(cmd), cmd_size - strlen(cmd), " -a");
    }
    else if (l_flag)
    {
        snprintf(cmd + strlen(cmd), cmd_size - strlen(cmd), " -l");
    }
    // Append non-option arguments to the command
    for (i = 1; i < argc; i++)
    {
        if (args[i][0] != '-')
        {
            snprintf(cmd + strlen(cmd), cmd_size - strlen(cmd), " %s", args[i]);
        }
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// make_data_listen_socket                                                    //
// ========================================================================== //
// Input   : int* - selected data connection port                             //
// Output  : int  - listening socket descriptor                               //
// Purpose : Creates a listening socket using a random port from 10001 to     //
//           30000 for the data connection.                                   //
////////////////////////////////////////////////////////////////////////////////
static int make_data_listen_socket(int *data_port)
{
    int listen_sock;
    int opt = 1;
    int retry;
    struct sockaddr_in data_addr;
    // Try up to 100 times to find an available port for the data connection
    for (retry = 0; retry < 100; retry++)
    {
        // Create a new socket for listening
        listen_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_sock < 0)
        {
            perror("socket");
            exit(1);
        }
        // Set socket options to allow address reuse
        setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        // Select a random port for the data connection
        *data_port = rand() % (MAX_DATA_PORT - MIN_DATA_PORT + 1) + MIN_DATA_PORT;
        // Set up the address structure for binding the socket
        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        data_addr.sin_port = htons(*data_port);
        // Attempt to bind the socket to the selected port
        if (bind(listen_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) == 0)
        {
            if (listen(listen_sock, BACKLOG) < 0)
            {
                perror("listen");
                close(listen_sock);
                exit(1);
            }
            return listen_sock;
        }
        // If bind fails, close the socket and check if the error is EADDRINUSE (port already in use)
        close(listen_sock);
        if (errno != EADDRINUSE)
        {
            perror("bind");
            exit(1);
        }
    }

    fprintf(stderr, "Failed to bind data connection port.\n");
    exit(1);
}

////////////////////////////////////////////////////////////////////////////////
// read_control_message                                                       //
// ========================================================================== //
// Input   : int   - control socket descriptor                                //
//           char* - receive buffer                                           //
//           int   - buffer size                                              //
// Output  : int   - received byte count                                      //
// Purpose : Receives one newline-terminated response from control connection.//
////////////////////////////////////////////////////////////////////////////////
static int read_control_message(int sock, char *buf, int size)
{
    int n;
    int total = 0;
    char ch;

    memset(buf, 0, size);
    // Read bytes one by one until a newline is encountered or the buffer is full
    while (total < size - 1)
    {
        n = read(sock, &ch, 1);
        if (n < 0)
        {
            perror("read");
            exit(1);
        }
        if (n == 0)
        {
            break;
        }
        if (ch == '\n')
        {
            break;
        }

        buf[total++] = ch;
    }
    buf[total] = '\0';
    return total;
}

////////////////////////////////////////////////////////////////////////////////
// main                                                                       //
// ========================================================================== //
// Input   : int argc    - argument count                                     //
//           char **argv - server IP address and control port number          //
// Output  : int         - process exit status                                //
// Purpose : Makes the control connection, creates data connection for ls,    //
//           and prints the directory list received from the server.          //
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    int control_sock;
    int data_listen_sock;
    int data_sock;
    int server_port;
    int data_port;
    int n;
    int total_bytes;
    socklen_t addr_len;
    char input[BUF_SIZE];
    char input_copy[BUF_SIZE];
    char nlst_cmd[BUF_SIZE];
    char buf[BUF_SIZE];
    char *hostport;
    struct sockaddr_in server_addr;
    struct sockaddr_in local_addr;
    struct sockaddr_in temp;
    struct sockaddr_in data_client_addr;

    // Check command line arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server ip> <server port>\n", argv[0]);
        exit(1);
    }

    // Seed the random number generator for data port selection
    srand((unsigned int)time(NULL));
    server_port = atoi(argv[2]);

    // Create control connection
    control_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (control_sock < 0)
    {
        perror("socket");
        exit(1);
    }
    // Set up server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid server IP address.\n");
        close(control_sock);
        exit(1);
    }
    // Connect to the server
    if (connect(control_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("connect");
        close(control_sock);
        exit(1);
    }

    // Main command loop
    while (1)
    {
        // Prompt user for command
        printf("> ");
        fflush(stdout);

        // Read user input
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        // Remove trailing newline
        input[strcspn(input, "\n")] = '\0';

        if (strcmp(input, "quit") == 0 || strcmp(input, "exit") == 0)
        {
            break;
        }

        // Convert user input to NLST command
        snprintf(input_copy, sizeof(input_copy), "%s", input);
        if (make_ftp_command(input_copy, nlst_cmd, sizeof(nlst_cmd)) < 0)
        {
            continue;
        }

        // Send PORT command and set up data connection
        data_listen_sock = make_data_listen_socket(&data_port);
        addr_len = sizeof(local_addr);
        if (getsockname(control_sock, (struct sockaddr *)&local_addr, &addr_len) < 0)
        {
            perror("getsockname");
            close(data_listen_sock);
            close(control_sock);
            exit(1);
        }

        // Prepare PORT command with local IP and selected data port
        memset(&temp, 0, sizeof(temp));
        temp.sin_addr.s_addr = local_addr.sin_addr.s_addr;
        temp.sin_port = htons(data_port);

        // Convert address and port to PORT command format and send to server
        hostport = convert_addr_to_str(temp.sin_addr.s_addr, temp.sin_port);
        write_all(control_sock, hostport, strlen(hostport));
        free(hostport);

        // Read server response to PORT command
        read_control_message(control_sock, buf, sizeof(buf));
        printf("%s\n", buf);

        // Wait for incoming data connection from server
        addr_len = sizeof(data_client_addr);
        data_sock = accept(data_listen_sock, (struct sockaddr *)&data_client_addr, &addr_len);
        if (data_sock < 0)
        {
            perror("accept");
            close(data_listen_sock);
            close(control_sock);
            exit(1);
        }

        // Send NLST command to server
        write_all(control_sock, nlst_cmd, strlen(nlst_cmd));
        // Read server response to NLST command
        read_control_message(control_sock, buf, sizeof(buf));
        printf("%s\n", buf);
        // Read directory list data from data connection and print to stdout
        total_bytes = 0;
        while ((n = read(data_sock, buf, sizeof(buf) - 1)) > 0)
        {
            buf[n] = '\0';
            printf("%s", buf);
            total_bytes += n;
        }
        // Check for read error
        if (n < 0)
        {
            perror("read");
            close(data_sock);
            close(data_listen_sock);
            close(control_sock);
            exit(1);
        }
        // Read final response from control connection after data transfer is complete
        read_control_message(control_sock, buf, sizeof(buf));
        printf("%s\n", buf);
        printf("OK. %d bytes is received.\n", total_bytes);

        close(data_sock);
        close(data_listen_sock);
    }

    close(control_sock);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// convert_addr_to_str                                                       //
// ========================================================================== //
// Input   : unsigned long - IP address in network byte order                 //
//           unsigned int  - port number in network byte order                //
// Output  : char*         - PORT command string                              //
// Purpose : Converts IP address and port to PORT h1,h2,h3,h4,p1,p2 format.  //
////////////////////////////////////////////////////////////////////////////////
char *convert_addr_to_str(unsigned long ip_addr, unsigned int port)
{
    unsigned char *ip;
    unsigned int host_port;
    unsigned int p1;
    unsigned int p2;
    char *addr;
    // Extract IP address bytes and port number
    ip = (unsigned char *)&ip_addr;
    host_port = ntohs((unsigned short)port);
    p1 = host_port / 256;
    p2 = host_port % 256;
    // Allocate buffer for PORT command string
    addr = (char *)malloc(BUF_SIZE);
    if (addr == NULL)
    {
        perror("malloc");
        exit(1);
    }
    // Format the PORT command string
    snprintf(addr, BUF_SIZE, "PORT %u,%u,%u,%u,%u,%u",
             ip[0], ip[1], ip[2], ip[3], p1, p2);

    return addr;
}
