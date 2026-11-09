//////////////////////////////////////////////////////////////////////////////////////
// File Name : cli.c                                                                //
// Date      : 2026/05/05                                                           //
// OS        : Ubuntu 20.04.6 LTS 64bits                                            //
// Author    : Jo Yeong Hoon                                                        //
// Student ID: 2022202078                                                           //
// ---------------------------------------------------------------------------      //
// Title     : System Programming Assignment #2-2 ( FTP server )                    //
// Description:                                                                     //
//   This client receives the server IP address and port number as command line     //
//   arguments. After connecting to the server, it repeatedly reads a string from   //
//   standard input, sends it to the server, receives the echoed string, and        //
//   prints the received string.                                                    //
//////////////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUF_SIZE 256

static ssize_t write_all(int fd, const void *buf, size_t count);

//////////////////////////////////////////////////////////////////////////////////
// main                                                                         //
// =============================================================================//
// Input : int argc     -> argument count                                       //
//         char *argv[] -> server IP address and port number                    //
// Output: int          -> process exit status                                  //
// Purpose:                                                                     //
//   Connect to the server and repeat input, send, receive,                     //
//   and print operations.                                                      //
//////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    char buff[BUF_SIZE];
    int n;
    int sockfd;
    struct sockaddr_in serv_addr;

    // Check for the correct number of command line arguments.
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server_ip> <port>\n", argv[0]);
        return 1;
    }

    // Create a TCP socket.
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }

    // Set up the server address structure.
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    // if the server IP address is invalid, print an error message and exit.
    if (serv_addr.sin_addr.s_addr == INADDR_NONE)
    {
        fprintf(stderr, "Invalid server IP address\n");
        close(sockfd);
        return 1;
    }

    // Connect to the server using the specified IP address and port number.
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // Repeatedly read a string from standard input, send it to the server, receive the echoed string, and print the received string.
    while (1)
    {
        write(STDOUT_FILENO, "> ", 2);

        // Read a string from standard input.
        n = read(STDIN_FILENO, buff, BUF_SIZE - 1);
        if (n <= 0)
        {
            break;
        }
        buff[n] = '\0';

        // Send the string to the server using write_all().
        if (write_all(sockfd, buff, (size_t)n) != n)
        {
            perror("write");
            break;
        }

        // Receive the echoed string from the server and print it.
        n = read(sockfd, buff, BUF_SIZE - 1);
        if (n <= 0)
        {
            break;
        }
        buff[n] = '\0';

        // Print the received string to standard output.
        printf("from server: %s", buff);
    }
    // Close the socket and exit the program.
    close(sockfd);
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////
// write_all                                                                    //
// =============================================================================//
// Input : int fd       -> file descriptor                                      //
//         const void * -> buffer address                                       //
//         size_t       -> byte count                                           //
// Output: ssize_t      -> written byte count on success, -1 on failure         //
// Purpose:                                                                     //
//   Repeatedly call write() until all bytes are sent.                          //
//////////////////////////////////////////////////////////////////////////////////
static ssize_t write_all(int fd, const void *buf, size_t count)
{
    const char *ptr = (const char *)buf;
    size_t left = count;

    // Repeatedly call write() until all bytes are sent.
    while (left > 0)
    {
        ssize_t written = write(fd, ptr, left);

        if (written < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }

        if (written == 0)
        {
            break;
        }

        ptr += written;
        left -= (size_t)written;
    }
    // Return the total written byte count on success, or -1 on failure.
    return (ssize_t)(count - left);
}
