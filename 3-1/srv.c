///////////////////////////////////////////////////////////////////////////////
// File Name   : srv.c                                                       //
// Date        : 2026/05/19                                                  //
// OS          : Ubuntu 20.04.6 LTS 64bits                                   //
// Author      : jo yeong-hoon                                               //
// Student ID  : 2022202078                                                  //
// ------------------------------------------------------------------------- //
// Title       : System Programming Assignment 3-1 (FTP Server)              //
// Description : Accepts clients, checks access.txt,                         //
//               and authenticates passwd.                                   //
///////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define MAX_BUF 20
#define LINE_BUF 256

int check_access(const char *client_ip);
int ip_match(const char *pattern, const char *client_ip);
int log_auth(int connfd);
int read_msg(int sockfd, char *buf);
int user_match(char *user, char *passwd);
int write_msg(int sockfd, const char *msg);

////////////////////////////////////////////////////////////////////////////////
// main                                                                       //
// ========================================================================== //
// Input  : argc, argv - server port                                          //
// Output : int - 0 success, 1 fail                                           //
// Purpose: Opens a listening socket and handles one client session at a time.//
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char *argv[])
{
    int listenfd;
    int connfd;
    int opt = 1;
    socklen_t clilen;
    struct sockaddr_in servaddr;
    struct sockaddr_in cliaddr;
    char client_ip[INET_ADDRSTRLEN];

    // Check command-line arguments
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <server port>\n", argv[0]);
        return 1;
    }

    // Create a TCP socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket");
        return 1;
    }

    // Set socket options to reuse the address
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(listenfd);
        return 1;
    }

    // Initialize server address structure
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons((unsigned short)atoi(argv[1]));

    // Bind the socket to the specified port
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind");
        close(listenfd);
        return 1;
    }

    // Start listening for incoming connections
    if (listen(listenfd, 5) < 0)
    {
        perror("listen");
        close(listenfd);
        return 1;
    }

    // Main loop to accept and handle client connections
    while (1)
    {
        // Accept a client connection
        clilen = sizeof(cliaddr);
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0)
        {
            perror("accept");
            continue;
        }

        // Convert client IP address to string format
        inet_ntop(AF_INET, &cliaddr.sin_addr, client_ip, sizeof(client_ip));
        printf("** Client is trying to connect **\n");
        printf(" - IP:   %s\n", client_ip);
        printf(" - Port: %d\n", ntohs(cliaddr.sin_port));

        // Check if the client IP is allowed to connect
        if (!check_access(client_ip))
        {
            write_msg(connfd, "REJECTION");
            printf("** It is NOT authenticated client **\n");
            close(connfd);
            continue;
        }

        // Send acceptance message to the client
        write_msg(connfd, "ACCEPTED");
        printf("** Client is connected **\n");

        // Authenticate the client with user ID and password
        if (log_auth(connfd) == 0)
        {
            printf("** Fail to log-in **\n");
            close(connfd);
            continue;
        }
        printf("** Success to log-in **\n");
        close(connfd);
    }
}

////////////////////////////////////////////////////////////////////////////////
// check_access                                                               //
// ========================================================================== //
// Input  : client_ip - connected client's IPv4 address string                //
// Output : int - 1 accepted, 0 rejected                                      //
// Purpose: Checks whether the client IP exists in access.txt.                //
////////////////////////////////////////////////////////////////////////////////
int check_access(const char *client_ip)
{
    FILE *fp_checkIP;
    char line[LINE_BUF];
    char *newline;

    // Open the access.txt file for reading
    fp_checkIP = fopen("access.txt", "r");
    if (fp_checkIP == NULL)
    {
        return 0;
    }

    // Read each line from the access.txt file and check for a match with the client IP
    while (fgets(line, sizeof(line), fp_checkIP) != NULL)
    {
        // Remove newline characters from the line
        newline = strchr(line, '\n');
        if (newline != NULL)
        {
            *newline = '\0';
        }
        // Remove carriage return characters from the line
        newline = strchr(line, '\r');
        if (newline != NULL)
        {
            *newline = '\0';
        }
        // Check if the line matches the client IP using the ip_match function
        if (ip_match(line, client_ip))
        {
            fclose(fp_checkIP);
            return 1;
        }
    }

    fclose(fp_checkIP);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// ip_match                                                                   //
// ========================================================================== //
// Input  : pattern - access.txt IP pattern, client_ip - client's IPv4 address//
// Output : int - 1 matched, 0 not matched                                    //
// Purpose: Compares exact IPv4 strings and wildcard patterns like *.*.*.*.   //
////////////////////////////////////////////////////////////////////////////////
int ip_match(const char *pattern, const char *client_ip)
{
    char pattern_part[4][MAX_BUF];
    char ip_part[4][MAX_BUF];
    int i;

    // Parse the pattern and client IP into their respective parts
    if (sscanf(pattern, "%19[^.].%19[^.].%19[^.].%19s",
               pattern_part[0], pattern_part[1],
               pattern_part[2], pattern_part[3]) != 4)
    {
        return 0;
    }
    // Parse the client IP into its respective parts
    if (sscanf(client_ip, "%19[^.].%19[^.].%19[^.].%19s",
               ip_part[0], ip_part[1], ip_part[2], ip_part[3]) != 4)
    {
        return 0;
    }
    // Compare each part of the pattern with the corresponding part of the client IP
    for (i = 0; i < 4; i++)
    {
        // If the pattern part is not a wildcard and does not match the client IP part, return 0
        if (strcmp(pattern_part[i], "*") && strcmp(pattern_part[i], ip_part[i]))
        {
            return 0;
        }
    }

    return 1;
}

////////////////////////////////////////////////////////////////////////////////
// log_auth                                                                   //
// ========================================================================== //
// Input  : connfd - connected client socket descriptor                       //
// Output : int - 1 login success, 0 login failed three times                 //
// Purpose: Receives user ID and password and controls three login attempts.  //
////////////////////////////////////////////////////////////////////////////////
int log_auth(int connfd)
{
    char user[MAX_BUF];
    char passwd[MAX_BUF];
    int count = 1;

    // Loop to allow up to three login attempts
    while (count <= 3)
    {
        printf("** User is trying to log-in (%d/3) **\n", count);

        // Read the user ID and password from the client
        if (read_msg(connfd, user) <= 0 || read_msg(connfd, passwd) <= 0)
        {
            return 0;
        }
        // Send an acknowledgment message to the client
        if (write_msg(connfd, "OK") < 0)
        {
            return 0;
        }
        // Check if the user ID and password match an entry in the passwd file
        if (user_match(user, passwd) == 1)
        {
            write_msg(connfd, "OK");
            return 1;
        }

        // If the login attempt fails, send a failure message to the client and increment the count
        printf("** Log-in failed **\n");
        if (count >= 3)
        {
            write_msg(connfd, "DISCONNECTION");
            return 0;
        }
        // Send a failure message to the client for the current login attempt
        write_msg(connfd, "FAIL");
        count++;
    }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// user_match                                                                 //
// ========================================================================== //
// Input  : user - client ID, passwd - client password                        //
// Output : int - 1 success, 0 fail                                           //
// Purpose: Searches the passwd file for a matching ID and password pair.     //
////////////////////////////////////////////////////////////////////////////////
int user_match(char *user, char *passwd)
{
    FILE *fp;
    char line[LINE_BUF];
    char saved_user[MAX_BUF];
    char saved_passwd[MAX_BUF];

    // Open the passwd file for reading
    fp = fopen("passwd", "r");
    if (fp == NULL)
    {
        return 0;
    }

    // Read each line from the passwd file and check for a matching user ID and password
    while (fgets(line, sizeof(line), fp) != NULL)
    {
        // Split the line into user ID and password fields
        if (sscanf(line, "%19[^:]:%19[^:]", saved_user, saved_passwd) == 2)
        {
            // Compare the user ID and password with the saved values from the passwd file
            if (!strcmp(user, saved_user) && !strcmp(passwd, saved_passwd))
            {
                fclose(fp);
                return 1;
            }
        }
    }

    fclose(fp);
    return 0;
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

    // Initialize the buffer to zero and read data from the socket until the entire message is received
    memset(buf, 0, MAX_BUF);
    while (received < MAX_BUF)
    {
        // Read data from the socket and handle disconnection or read errors
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

    // Initialize the buffer to zero and copy the message into the buffer, ensuring it does not exceed the maximum size
    memset(buf, 0, MAX_BUF);
    strncpy(buf, msg, MAX_BUF - 1);

    // Write the message to the socket until the entire message is sent, handling write errors
    while (sent < MAX_BUF)
    {
        // Write data to the socket and handle write errors
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
