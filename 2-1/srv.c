///////////////////////////////////////////////////////////////////////////////
// File Name  : srv.c
// Date       : 2026/04/30
// OS         : Ubuntu 20.04.6 LTS 64bits
// Author     : Jo Young Hoon
// Student ID : 2022202078
// ---------------------------------------------------------------------------
// Title      : System Programming Assignment 2-1 Server
// Description: Receives FTP commands from the client and returns processed
//              command results using TCP sockets.
///////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <dirent.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFF 1024
#define SEND_BUFF 4096
#define LISTEN_BACKLOG 5
#define MAX_ARGC 16

///////////////////////////////////////////////////////////////////////////////
// client_info
// Input   : struct sockaddr_in *client - connected client address
// Output  : void
// Purpose : Prints the client's IP address and port number.
///////////////////////////////////////////////////////////////////////////////
static void client_info(struct sockaddr_in *client)
{
    char info[MAX_BUFF];
    int len;

    // Print client information header
    write(STDOUT_FILENO, "==========Client info===========\n",
          sizeof("==========Client info===========\n") - 1);

    // write the client's IP address and port number to the info buffer and print it
    len = snprintf(info, sizeof(info), "client IP: %s\n",
                   inet_ntoa(client->sin_addr));
    if (len > 0)
    {
        write(STDOUT_FILENO, info, strlen(info));
    }
    len = snprintf(info, sizeof(info), "client port: %d\n",
                   ntohs(client->sin_port));
    if (len > 0)
    {
        write(STDOUT_FILENO, info, strlen(info));
    }
    write(STDOUT_FILENO, "================================\n",
          sizeof("================================\n") - 1);
}

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
// cmd_process
// Input   : char *buff        - received FTP command
//           char *result_buff - command result buffer
// Output  : int               - 0 success, -1 fail
// Purpose : Processes NLST and QUIT commands.
///////////////////////////////////////////////////////////////////////////////
static int cmd_process(char *buff, char *result_buff)
{
    DIR *dirp;
    struct dirent *dir;
    size_t used = 0;
    int a_flag = 0;
    int l_flag = 0;
    char temp[MAX_BUFF];
    char *argv[MAX_ARGC + 1];
    int argc;
    int option;

    // if buff is "QUIT", set result_buff to "QUIT" and return 0
    if (strcmp(buff, "QUIT") == 0)
    {
        strcpy(result_buff, "QUIT");
        return 0;
    }

    strcpy(temp, buff);
    argc = split_command(temp, argv, MAX_ARGC);

    // if buff is not "NLST", set result_buff to "Invalid command\n" and return -1
    if (argc <= 0 || strcmp(argv[0], "NLST") != 0)
    {
        strcpy(result_buff, "Invalid command\n");
        return -1;
    }

    // Parse options using getopt and set flags accordingly
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
            strcpy(result_buff, "Invalid option\n");
            return -1;
        }
    }

    if (optind != argc)
    {
        strcpy(result_buff, "Invalid option\n");
        return -1;
    }

    // Open the current directory and read its contents
    dirp = opendir(".");
    if (dirp == NULL)
    {
        strcpy(result_buff, "opendir() error\n");
        return -1;
    }

    // Read the directory entries and build the result string
    result_buff[0] = '\0';
    while ((dir = readdir(dirp)) != NULL)
    {
        int w;

        // Skip hidden files
        if (!a_flag && dir->d_name[0] == '.')
        {
            continue;
        }

        // Append the directory entry name to the result buffer
        if (l_flag)
        {
            struct stat st;
            struct passwd *pw;
            struct group *gr;
            struct tm *tm_info;
            char perm[11];
            char time_buff[32];

            if (lstat(dir->d_name, &st) < 0)
            {
                continue;
            }

            perm[0] = S_ISDIR(st.st_mode) ? 'd' : '-';
            perm[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
            perm[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
            perm[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
            perm[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
            perm[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
            perm[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
            perm[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
            perm[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
            perm[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
            perm[10] = '\0';

            pw = getpwuid(st.st_uid);
            gr = getgrgid(st.st_gid);
            tm_info = localtime(&st.st_mtime);
            if (tm_info == NULL)
            {
                strcpy(time_buff, "unknown");
            }
            else
            {
                strftime(time_buff, sizeof(time_buff), "%b %d %H:%M", tm_info);
            }

            w = snprintf(result_buff + used, SEND_BUFF - used,
                         "%s %2ld %s %s %8ld %s %s\n",
                         perm,
                         (long)st.st_nlink,
                         pw == NULL ? "unknown" : pw->pw_name,
                         gr == NULL ? "unknown" : gr->gr_name,
                         (long)st.st_size,
                         time_buff,
                         dir->d_name);
        }
        else
        {
            w = snprintf(result_buff + used, SEND_BUFF - used,
                         "%s\n", dir->d_name);
        }

        if (w < 0 || (size_t)w >= SEND_BUFF - used)
        {
            break;
        }

        used += (size_t)w;
    }

    // Close the directory stream and finalize the result string
    closedir(dirp);
    if (used == 0)
    {
        strcpy(result_buff, "\n");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// main
// Input   : int argc    - number of command-line arguments
//           char **argv - server port number
// Output  : int         - 0 success, 1 fail
// Purpose : Opens a server socket, accepts a client, processes FTP commands,
//           and sends the results back to the client.
///////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    char buff[MAX_BUFF];
    char result_buff[SEND_BUFF];
    int n;
    int listen_fd;
    int connect_fd;
    int optval = 1;
    int server_quit = 0;
    struct sockaddr_in server;
    struct sockaddr_in client;
    socklen_t clientlen;

    // Check command-line arguments
    if (argc != 2)
    {
        write(STDERR_FILENO, "Usage: ./srv <port>\n",
              sizeof("Usage: ./srv <port>\n") - 1);
        return 1;
    }

    // Create a TCP socket
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
    {
        perror("socket");
        return 1;
    }

    // Set socket options to allow address reuse
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval)) < 0)
    {
        perror("setsockopt");
        close(listen_fd);
        return 1;
    }

    // Set up the server address structure and bind the socket
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons((unsigned short)atoi(argv[1]));

    // Bind the socket to the specified port
    if (bind(listen_fd, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("bind");
        close(listen_fd);
        return 1;
    }

    // Listen for incoming connections
    if (listen(listen_fd, LISTEN_BACKLOG) < 0)
    {
        perror("listen");
        close(listen_fd);
        return 1;
    }

    // Main loop to accept and handle client connections
    while (!server_quit)
    {
        clientlen = sizeof(client);
        connect_fd = accept(listen_fd, (struct sockaddr *)&client, &clientlen);
        if (connect_fd < 0)
        {
            perror("accept");
            continue;
        }
        client_info(&client);

        // Loop to read commands from the client, process them, and send responses
        for (;;)
        {
            // Read a command from the client
            n = read(connect_fd, buff, MAX_BUFF - 1);
            if (n < 0)
            {
                write(STDERR_FILENO, "read() error\n",
                      sizeof("read() error\n") - 1);
                break;
            }
            if (n == 0)
            {
                break;
            }
            buff[n] = '\0';
            write(STDOUT_FILENO, buff, strlen(buff));
            write(STDOUT_FILENO, "\n", 1);
            // Process the command and prepare the result
            if (cmd_process(buff, result_buff) < 0)
            {
                write(STDERR_FILENO, "cmd_process() error\n",
                      sizeof("cmd_process() error\n") - 1);
            }
            // Send the result back to the client
            if (write(connect_fd, result_buff, strlen(result_buff)) < 0)
            {
                write(STDERR_FILENO, "write() error\n",
                      sizeof("write() error\n") - 1);
                break;
            }
            // Check if the result is "QUIT" and exit the loop if it is
            if (strcmp(result_buff, "QUIT") == 0)
            {
                server_quit = 1;
                break;
            }
        }
        // Close the connection socket after handling the client
        close(connect_fd);
    }
    // Close the listening socket and exit
    close(listen_fd);
    return 0;
}
