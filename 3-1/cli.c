///////////////////////////////////////////////////////////////////////////////
// File Name   : cli.c                                                       //
// Date        : 2026/05/19                                                  //
// OS          : Ubuntu 20.04.6 LTS 64bits                                   //
// Author      : jo yeong-hoon                                               //
// Student ID  : 2022202078                                                  //
// ------------------------------------------------------------------------- //
// Title       : System Programming Assignment 3-1 (FTP Server)              //
// Description : Connects to the server and performs IP-checked login.       //
///////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_BUF 20

void log_in(int sockfd);
int read_msg(int sockfd, char *buf);
int write_msg(int sockfd, const char *msg);

////////////////////////////////////////////////////////////////////////////////
// main                                                                       //
// ========================================================================== //
// Input  : argc, argv - server IP address and server port                    //
// Output : int - 0 success, 1 fail                                           //
// Purpose: Creates a client socket and connects to the FTP login server.     //
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    int sockfd;
    struct sockaddr_in servaddr;
    // Check command-line arguments
    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server IP> <server port>\n", argv[0]);
        return 1;
    }
    // Create socket
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return 1;
    }
    // Set up server address structure
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons((unsigned short)atoi(argv[2]));
    if (inet_pton(AF_INET, argv[1], &servaddr.sin_addr) <= 0)
    {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    // Connect to server
    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("connect");
        close(sockfd);
        return 1;
    }

    // Perform login procedure
    log_in(sockfd);
    close(sockfd);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// log_in                                                                     //
// ========================================================================== //
// Input  : sockfd - connected server socket descriptor                       //
// Output : None                                                              //
// Purpose: Checks server access result and sends login information to server.//
////////////////////////////////////////////////////////////////////////////////
void log_in(int sockfd)
{
    char user[MAX_BUF];
    char passwd[MAX_BUF];
    char buf[MAX_BUF];

    if (read_msg(sockfd, buf) <= 0)
    {
        return;
    }
    // Check server response for connection status
    if (!strcmp(buf, "REJECTION"))
    {
        printf("** Connection refused **\n");
        return;
    }
    // If not rejected, check if accepted
    if (strcmp(buf, "ACCEPTED"))
    {
        return;
    }
    printf("** It is connected to Server **\n");

    // Loop for login attempts
    while (1)
    {
        // Prompt user for ID and password
        printf("Input ID : ");
        fflush(stdout);
        if (scanf("%19s", user) != 1)
        {
            return;
        }
        printf("Input Password : ");
        fflush(stdout);
        if (scanf("%19s", passwd) != 1)
        {
            return;
        }

        // Send login information to server
        if (write_msg(sockfd, user) < 0 || write_msg(sockfd, passwd) < 0)
        {
            return;
        }
        // Check server response for login result
        if (read_msg(sockfd, buf) <= 0)
        {
            return;
        }
        // If response is not "OK", exit
        if (strcmp(buf, "OK"))
        {
            return;
        }
        // If login is successful, check for further server response
        if (read_msg(sockfd, buf) <= 0)
        {
            return;
        }
        // Handle server response for login result
        if (!strcmp(buf, "OK"))
        {
            printf("** User '%s' logged in **\n", user);
            return;
        }
        // If login failed, check if it's a failure or disconnection
        if (!strcmp(buf, "FAIL"))
        {
            printf("** Log-in failed **\n");
            continue;
        }
        // If server indicates disconnection, print message and exit
        if (!strcmp(buf, "DISCONNECTION"))
        {
            printf("** Connection closed **\n");
            return;
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// read_msg                                                                   //
// ========================================================================== //
// Input  : sockfd - socket descriptor, buf - message buffer                  //
// Output : int - 1 success, 0 disconnected, -1 fail                          //
// Purpose: Receives one fixed-size protocol message and terminates it.       //
////////////////////////////////////////////////////////////////////////////////
int read_msg(int sockfd, char *buf)
{
    int received = 0;
    int n;
    // Clear the buffer before reading
    memset(buf, 0, MAX_BUF);

    // Read until the entire message is received
    while (received < MAX_BUF)
    {
        n = read(sockfd, buf + received, MAX_BUF - received);
        if (n == 0)
        {
            return 0;
        }
        if (n < 0)
        {
            perror("read");
            return -1;
        }
        received += n;
    }
    buf[MAX_BUF - 1] = '\0';
    return 1;
}

////////////////////////////////////////////////////////////////////////////////
// write_msg                                                                  //
// ========================================================================== //
// Input  : sockfd - socket descriptor, msg - string message                  //
// Output : int - 1 success, -1 fail                                          //
// Purpose: Sends one fixed-size protocol message to match skeleton protocol. //
////////////////////////////////////////////////////////////////////////////////
int write_msg(int sockfd, const char *msg)
{
    char buf[MAX_BUF];
    int sent = 0;
    int n;

    // Prepare the message buffer by copying the input message and padding with zeros
    memset(buf, 0, MAX_BUF);
    strncpy(buf, msg, MAX_BUF - 1);

    // Send the message in chunks until the entire message is sent
    while (sent < MAX_BUF)
    {
        n = write(sockfd, buf + sent, MAX_BUF - sent);
        if (n <= 0)
        {
            perror("write");
            return -1;
        }
        sent += n;
    }
    return 1;
}
