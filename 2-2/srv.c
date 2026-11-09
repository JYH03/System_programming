//////////////////////////////////////////////////////////////////////////////////
// File Name : srv.c                                                            //
// Date      : 2026/05/05                                                       //
// OS        : Ubuntu 20.04.6 LTS 64bits                                        //
// Author    : Jo Yeong Hoon                                                    //
// Student ID: 2022202078                                                       //
// ---------------------------------------------------------------------------  //
// Title     : System Programming Assignment #2-2 ( FTP server)                 //
// Description:                                                                 //
//   This server receives a port number as a command line argument and waits    //
//   for client connections. Whenever a client connects, the server creates     //
//   a child process using fork(). The parent process prints client information //
//   and waits for the next client. The child process echoes received strings   //
//   until the client sends "QUIT" or the connection is closed.                 //
//////////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUF_SIZE 256

void sh_chld(int signum);
void sh_alrm(int signum);
static ssize_t write_all(int fd, const void *buf, size_t count);
static int is_quit(const char *buff, int n);

//////////////////////////////////////////////////////////////////////////////////
// main                                                                         //
// =============================================================================//
// Input : int argc     -> argument count                                       //
//         char *argv[] -> port number                                          //
// Output: int          -> process exit status                                  //
// Purpose:                                                                     //
//   Create a TCP server socket, accept clients, and handle each client in a    //
//   child process created by fork().                                           //
//////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    char buff[BUF_SIZE];
    int n;
    struct sockaddr_in server_addr, client_addr;
    int server_fd, client_fd;
    socklen_t len;
    pid_t pid;

    // Check for the correct number of command line arguments.
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    // Set up signal handlers for SIGALRM and SIGCHLD.
    signal(SIGALRM, sh_alrm);
    signal(SIGCHLD, sh_chld);

    // Create a TCP socket and bind it to the specified port number. Then, listen for incoming connections.
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket");
        return 1;
    }

    // Set up the server address structure and bind the socket to the specified port number.
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(argv[1]));

    // If the port number is invalid, print an error message and exit.
    if (bind(server_fd, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    // Listen for incoming connections on the server socket.
    if (listen(server_fd, 5) < 0)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    // Repeatedly accept client connections and handle each client in a child process created by fork().
    while (1)
    {
        // Accept a client connection.
        len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("accept");
            continue;
        }

        // Create a child process using fork() to handle the client connection.
        pid = fork();

        // If fork() fails, print an error message and close the client socket.
        if (pid < 0)
        {
            perror("fork");
            close(client_fd);
            continue;
        }

        // In the child process, handle the client connection.
        if (pid == 0)
        {
            // The child process does not need the server socket.
            close(server_fd);
            printf("Child Process ID : %d\n", getpid());
            fflush(stdout);

            // Echo received strings until the client sends "QUIT" or the connection is closed.
            while (1)
            {
                // Read a string from the client.
                n = read(client_fd, buff, BUF_SIZE - 1);
                if (n <= 0)
                {
                    break;
                }
                buff[n] = '\0';

                // Check whether the client requested connection termination.
                if (is_quit(buff, n))
                {
                    close(client_fd);
                    raise(SIGALRM);
                }

                // Send the string back to the client using write_all().
                if (write_all(client_fd, buff, (size_t)n) != n)
                {
                    break;
                }
            }
            // Close the client socket and exit the child process.
            close(client_fd);
            exit(0);
        }
        // In the parent process, print client information and close the client socket.
        printf("==========Client info==========\n");
        printf("client IP   : %s\n\n", inet_ntoa(client_addr.sin_addr));
        printf("client port : %d\n", ntohs(client_addr.sin_port));
        printf("===============================\n");
        fflush(stdout);
        close(client_fd);
    }
    // Close the server socket and exit the program.
    close(server_fd);
    return 0;
}

//////////////////////////////////////////////////////////////////////////////////
// sh_chld                                                                      //
// =============================================================================//
// Input : int signum -> signal number                                          //
// Output: void                                                                 //
// Purpose:                                                                     //
//   Handle SIGCHLD from a terminated child process and remove zombie processes.//
//////////////////////////////////////////////////////////////////////////////////
void sh_chld(int signum)
{
    int saved_errno = errno;

    (void)signum;
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        printf("Status of Child Process was changed.\n");
        fflush(stdout);
    }

    errno = saved_errno;
}

//////////////////////////////////////////////////////////////////////////////////
// sh_alrm                                                                      //
// =============================================================================//
// Input : int signum -> signal number                                          //
// Output: void                                                                 //
// Purpose:                                                                     //
//   Handle SIGALRM and terminate the child process.                            //
//////////////////////////////////////////////////////////////////////////////////
void sh_alrm(int signum)
{
    (void)signum;
    printf("Child Process(PID : %d) will be terminated.\n", getpid());
    fflush(stdout);
    exit(0);
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

    //
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

    return (ssize_t)(count - left);
}

//////////////////////////////////////////////////////////////////////////////////
// is_quit                                                                      //
// =============================================================================//
// Input : const char *buff -> received string                                  //
//         int n            -> received byte count                              //
// Output: int              -> 1 if the string is "QUIT", otherwise 0           //
// Purpose:                                                                     //
//   Check whether the client requested connection termination.                 //
//////////////////////////////////////////////////////////////////////////////////
static int is_quit(const char *buff, int n)
{
    while (n > 0 && (buff[n - 1] == '\n' || buff[n - 1] == '\r'))
    {
        n--;
    }

    if (n != 4)
    {
        return 0;
    }

    return strncmp(buff, "QUIT", 4) == 0;
}