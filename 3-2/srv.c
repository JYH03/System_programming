////////////////////////////////////////////////////////////////////////////////
// File Name  : srv.c                                                         //
// Date       : 2026/05/22                                                    //
// OS         : Ubuntu 20.04.6 LTS 64bits                                     //
// Author     : Jo Young Hoon                                                 //
// Student ID : 2022202078                                                    //
// -------------------------------------------------------------------------- //
// Title      : System Programming Assignment #3-2 Server                     //
// Description: This server receives PORT and NLST through the control        //
//              connection, connects to the client's data port, and sends     //
//              the directory list through the data connection.               //
////////////////////////////////////////////////////////////////////////////////

#define _DEFAULT_SOURCE

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

#define BUF_SIZE 1024
#define BACKLOG 5
#define MAX_ARGC 32

////////////////////////////////////////////////////////////////////////////////
// split_args                                                                //
// ========================================================================== //
// Input   : char*  - command line                                            //
//           char** - parsed argument array                                   //
// Output  : int    - parsed argument count                                   //
// Purpose : Splits an NLST command into blank-separated tokens.              //
////////////////////////////////////////////////////////////////////////////////
static int split_args(char *line, char *args[])
{
    int count = 0;
    char *saveptr;
    char *token;
    // Use strtok_r for thread safety, although this server is single-threaded.
    token = strtok_r(line, " \t\r\n", &saveptr);
    while (token != NULL && count < MAX_ARGC - 1)
    {
        args[count++] = token;
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    args[count] = NULL;

    return count;
}

////////////////////////////////////////////////////////////////////////////////
// copy_string                                                               //
// ========================================================================== //
// Input   : const char* - source string                                      //
// Output  : char*       - copied string                                      //
// Purpose : Allocates memory and copies a directory entry name.              //
////////////////////////////////////////////////////////////////////////////////
static char *copy_string(const char *src)
{
    size_t len = strlen(src) + 1;
    char *dst = (char *)malloc(len);
    // Check for malloc failure
    if (dst != NULL)
    {
        memcpy(dst, src, len);
    }

    return dst;
}

////////////////////////////////////////////////////////////////////////////////
// name_cmp                                                                  //
// ========================================================================== //
// Input   : const void* - first string pointer                               //
//           const void* - second string pointer                              //
// Output  : int         - strcmp result                                      //
// Purpose : Compares directory entry names for qsort.                        //
////////////////////////////////////////////////////////////////////////////////
static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

////////////////////////////////////////////////////////////////////////////////
// mode_to_string                                                            //
// ========================================================================== //
// Input   : mode_t - file mode                                               //
//           char*  - permission string buffer                                //
// Output  : void                                                            //
// Purpose : Converts file mode bits to ls -l style permission text.          //
////////////////////////////////////////////////////////////////////////////////
static void mode_to_string(mode_t mode, char *out)
{
    // Fill the out buffer with permission string based on the mode bits
    out[0] = S_ISDIR(mode) ? 'd' : '-';
    out[1] = (mode & S_IRUSR) ? 'r' : '-';
    out[2] = (mode & S_IWUSR) ? 'w' : '-';
    out[3] = (mode & S_IXUSR) ? 'x' : '-';
    out[4] = (mode & S_IRGRP) ? 'r' : '-';
    out[5] = (mode & S_IWGRP) ? 'w' : '-';
    out[6] = (mode & S_IXGRP) ? 'x' : '-';
    out[7] = (mode & S_IROTH) ? 'r' : '-';
    out[8] = (mode & S_IWOTH) ? 'w' : '-';
    out[9] = (mode & S_IXOTH) ? 'x' : '-';
    out[10] = '\0';
}

////////////////////////////////////////////////////////////////////////////////
// write_all                                                                 //
// ========================================================================== //
// Input   : int         - socket descriptor                                  //
//           const char* - data buffer                                        //
//           int         - data size                                          //
// Output  : void                                                            //
// Purpose : Sends all bytes in the buffer through the socket.                //
////////////////////////////////////////////////////////////////////////////////
static void write_all(int sock, const char *buf, int size)
{
    int sent;
    int total = 0;
    // Loop until all bytes are sent
    while (total < size)
    {
        // Send the remaining bytes
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
// send_long_entry                                                           //
// ========================================================================== //
// Input   : int         - data socket descriptor                             //
//           const char* - file path used for lstat                           //
//           const char* - display name                                       //
// Output  : void                                                            //
// Purpose : Sends one ls -l style directory entry through data connection.   //
////////////////////////////////////////////////////////////////////////////////
static void send_long_entry(int data_sock, const char *path, const char *name)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;
    struct tm *tm_info;
    char perm[11];
    char timebuf[64];
    char line[BUF_SIZE * 2];
    char display_name[BUF_SIZE];

    // Get file information using lstat
    if (lstat(path, &st) < 0)
    {
        return;
    }
    // Convert mode to permission string
    mode_to_string(st.st_mode, perm);
    pw = getpwuid(st.st_uid);
    gr = getgrgid(st.st_gid);
    tm_info = localtime(&st.st_mtime);
    // Format time string, handle potential NULL from localtime
    if (tm_info != NULL)
    {
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm_info);
    }
    else
    {
        snprintf(timebuf, sizeof(timebuf), "??? ?? ??:??");
    }
    // Prepare display name, append '/' if it's a directory
    snprintf(display_name, sizeof(display_name), "%s%s", name,
             S_ISDIR(st.st_mode) ? "/" : "");
    // Format the long entry line
    snprintf(line, sizeof(line), "%s %3ld %-8s %-8s %8ld %s %s\n",
             perm, (long)st.st_nlink,
             pw ? pw->pw_name : "unknown",
             gr ? gr->gr_name : "unknown",
             (long)st.st_size, timebuf, display_name);
    write_all(data_sock, line, strlen(line));
}

////////////////////////////////////////////////////////////////////////////////
// make_control_socket                                                       //
// ========================================================================== //
// Input   : int - server control port                                        //
// Output  : int - listening socket descriptor                                //
// Purpose : Creates, binds, and listens on the server control socket.        //
////////////////////////////////////////////////////////////////////////////////
static int make_control_socket(int port)
{
    int listen_sock;
    int opt = 1;
    struct sockaddr_in server_addr;
    // Create a TCP socket for the control connection
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_sock < 0)
    {
        perror("socket");
        exit(1);
    }
    // Set SO_REUSEADDR to allow quick restart of the server
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    // Initialize server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);
    // Bind the socket to the specified port
    if (bind(listen_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(listen_sock);
        exit(1);
    }
    // Start listening for incoming connections
    if (listen(listen_sock, BACKLOG) < 0)
    {
        perror("listen");
        close(listen_sock);
        exit(1);
    }

    return listen_sock;
}

////////////////////////////////////////////////////////////////////////////////
// make_data_connection                                                      //
// ========================================================================== //
// Input   : char*        - client IP address                                 //
//           unsigned int - client data port                                  //
// Output  : int          - connected data socket descriptor                  //
// Purpose : Connects to the client data port received from the PORT command. //
////////////////////////////////////////////////////////////////////////////////
static int make_data_connection(char *host_ip, unsigned int port_num)
{
    int data_sock;
    struct sockaddr_in data_addr;
    // Create a TCP socket for the data connection
    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0)
    {
        perror("socket");
        exit(1);
    }
    // Initialize client address structure
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_port = htons(port_num);
    if (inet_pton(AF_INET, host_ip, &data_addr.sin_addr) <= 0)
    {
        fprintf(stderr, "Invalid client IP address.\n");
        close(data_sock);
        exit(1);
    }
    // Connect to the client's data port
    if (connect(data_sock, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
    {
        perror("connect");
        close(data_sock);
        exit(1);
    }

    return data_sock;
}

////////////////////////////////////////////////////////////////////////////////
// send_directory_list                                                       //
// ========================================================================== //
// Input   : int   - data socket descriptor                                   //
//           char* - NLST command line                                        //
// Output  : void                                                            //
// Purpose : Sends the directory list to the client through data connection.  //
////////////////////////////////////////////////////////////////////////////////
static void send_directory_list(int data_sock, char *cmdline)
{
    int i;
    int count;
    int capacity;
    int argc;
    int a_flag = 0;
    int l_flag = 0;
    int path_count = 0;
    char line[BUF_SIZE * 2];
    char path[BUF_SIZE];
    char fullpath[BUF_SIZE * 2];
    char *args[MAX_ARGC];
    char **names;
    DIR *dirp;
    struct dirent *entry;
    struct stat st;
    // Default to current directory if no path is specified
    snprintf(path, sizeof(path), ".");
    argc = split_args(cmdline, args);
    // Parse options and path from the command line arguments
    for (i = 1; i < argc; i++)
    {
        if (args[i][0] == '-')
        {
            int j;

            if (args[i][1] == '\0')
            {
                snprintf(line, sizeof(line), "Error: invalid option\n");
                write_all(data_sock, line, strlen(line));
                return;
            }

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
                    snprintf(line, sizeof(line), "Error: invalid option\n");
                    write_all(data_sock, line, strlen(line));
                    return;
                }
            }
        }
        else
        {
            path_count++;
            snprintf(path, sizeof(path), "%s", args[i]);
        }
    }
    // Check for too many arguments
    if (path_count > 1)
    {
        snprintf(line, sizeof(line), "Error: too many arguments\n");
        write_all(data_sock, line, strlen(line));
        return;
    }
    // Get file information for the specified path
    if (lstat(path, &st) < 0)
    {
        snprintf(line, sizeof(line), "Error: No such file or directory\n");
        write_all(data_sock, line, strlen(line));
        return;
    }
    // If it's not a directory, send a single entry and return
    if (!S_ISDIR(st.st_mode))
    {
        if (l_flag)
        {
            send_long_entry(data_sock, path, path);
        }
        else if (a_flag || path[0] != '.')
        {
            snprintf(line, sizeof(line), "%s\n", path);
            write_all(data_sock, line, strlen(line));
        }
        return;
    }
    // It's a directory, read entries and send the list
    count = 0;
    capacity = 16;
    names = (char **)malloc(sizeof(char *) * capacity);
    if (names == NULL)
    {
        perror("malloc");
        exit(1);
    }
    // Open the directory
    dirp = opendir(path);
    if (dirp == NULL)
    {
        perror("opendir");
        free(names);
        exit(1);
    }
    // Read directory entries
    while ((entry = readdir(dirp)) != NULL)
    {
        // Skip hidden files if -a is not specified
        if (!a_flag && entry->d_name[0] == '.')
        {
            continue;
        }
        // Resize the names array if needed
        if (count >= capacity)
        {
            char **temp;
            capacity *= 2;
            temp = (char **)realloc(names, sizeof(char *) * capacity);
            // Check for realloc failure
            if (temp == NULL)
            {
                perror("realloc");
                closedir(dirp);
                // Free previously allocated names before exiting
                for (i = 0; i < count; i++)
                {
                    free(names[i]);
                }
                free(names);
                exit(1);
            }
            names = temp;
        }
        // Copy the directory entry name into the names array
        names[count] = copy_string(entry->d_name);
        if (names[count] == NULL)
        {
            perror("malloc");
            closedir(dirp);
            // Free previously allocated names before exiting
            for (i = 0; i < count; i++)
            {
                free(names[i]);
            }
            free(names);
            exit(1);
        }
        count++;
    }
    closedir(dirp);
    // Sort the names alphabetically
    qsort(names, count, sizeof(char *), name_cmp);
    // Send the directory entries to the client
    for (i = 0; i < count; i++)
    {
        // Construct the full path for lstat if -l is specified
        if (l_flag)
        {
            // Handle the case where path is "." to avoid "./filename"
            if (strcmp(path, ".") == 0)
            {
                snprintf(fullpath, sizeof(fullpath), "%s", names[i]);
            }
            else
            {
                snprintf(fullpath, sizeof(fullpath), "%s/%s", path, names[i]);
            }
            send_long_entry(data_sock, fullpath, names[i]);
        }
        else
        {
            snprintf(line, sizeof(line), "%s\n", names[i]);
            write_all(data_sock, line, strlen(line));
        }
        free(names[i]);
    }
    free(names);
}
////////////////////////////////////////////////////////////////////////////////
// convert_str_to_addr                                                       //
// ========================================================================== //
// Input   : char*         - PORT command string                              //
//           unsigned int* - converted port number                            //
// Output  : char*         - converted dotted-decimal IP address              //
// Purpose : Converts PORT h1,h2,h3,h4,p1,p2 to IP address and port number.  //
////////////////////////////////////////////////////////////////////////////////
char *convert_str_to_addr(char *str, unsigned int *port)
{
    unsigned int h1;
    unsigned int h2;
    unsigned int h3;
    unsigned int h4;
    unsigned int p1;
    unsigned int p2;
    char *addr;

    if (sscanf(str, "PORT %u,%u,%u,%u,%u,%u", &h1, &h2, &h3, &h4, &p1, &p2) != 6)
    {
        fprintf(stderr, "Invalid PORT command.\n");
        exit(1);
    }

    *port = p1 * 256 + p2;

    addr = (char *)malloc(BUF_SIZE);
    if (addr == NULL)
    {
        perror("malloc");
        exit(1);
    }

    snprintf(addr, BUF_SIZE, "%u.%u.%u.%u", h1, h2, h3, h4);

    return addr;
}
////////////////////////////////////////////////////////////////////////////////
// main                                                                      //
// ========================================================================== //
// Input   : int argc    - argument count                                    //
//           char **argv - server control port number                        //
// Output  : int         - process exit status                               //
// Purpose : Makes the control connection, handles PORT/NLST commands, and   //
//           sends directory list data through the data connection.           //
////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    int listen_sock;
    int control_sock;
    int data_sock;
    int control_port;
    int n;
    socklen_t addr_len;
    char *host_ip;
    char temp[BUF_SIZE];
    char buf[BUF_SIZE];
    char nlst_cmd[BUF_SIZE];
    unsigned int port_num;
    struct sockaddr_in client_addr;

    // Check for correct number of arguments
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <server port>\n", argv[0]);
        exit(1);
    }
    // Create and listen on the control socket
    control_port = atoi(argv[1]);
    listen_sock = make_control_socket(control_port);
    addr_len = sizeof(client_addr);
    control_sock = accept(listen_sock, (struct sockaddr *)&client_addr, &addr_len);
    // Check for accept failure
    if (control_sock < 0)
    {
        perror("accept");
        close(listen_sock);
        exit(1);
    }
    // Enter PORT/NLST handling loop
    while (1)
    {
        memset(temp, 0, sizeof(temp));
        n = read(control_sock, temp, sizeof(temp) - 1);
        // Check for read errors or client disconnect
        if (n == 0)
        {
            break;
        }
        // Handle read error
        if (n < 0)
        {
            perror("read");
            close(control_sock);
            close(listen_sock);
            exit(1);
        }
        temp[n] = '\0';
        printf("%s\n", temp);
        // Check if the command is PORT, if not, ignore and wait for the next command
        if (strncmp(temp, "PORT", 4) != 0)
        {
            continue;
        }
        // Convert the PORT command string to client IP address and port number
        host_ip = convert_str_to_addr(temp, &port_num);
        snprintf(buf, sizeof(buf), "200 Port command successful\n");
        write_all(control_sock, buf, strlen(buf));
        printf("%s", buf);

        // Establish the data connection to the client using the received IP and port
        data_sock = make_data_connection(host_ip, port_num);
        free(host_ip);
        // Wait for the next command (expected to be NLST) from the client through the control connection
        memset(buf, 0, sizeof(buf));
        n = read(control_sock, buf, sizeof(buf) - 1);
        // Check for read errors or client disconnect
        if (n <= 0)
        {
            if (n < 0)
            {
                perror("read");
            }
            close(data_sock);
            break;
        }
        // Null-terminate the received command and print it
        buf[n] = '\0';
        printf("%s\n", buf);
        snprintf(nlst_cmd, sizeof(nlst_cmd), "%s", buf);
        // Check if the command is NLST, if so, send the directory list through the data connection
        if (strncmp(buf, "NLST", 4) == 0)
        {
            // Send a preliminary reply to the client indicating that the directory list is being sent
            snprintf(buf, sizeof(buf), "150 Opening data connection for directory list.\n");
            write_all(control_sock, buf, strlen(buf));
            printf("%s", buf);
            // Send the directory list through the data connection
            send_directory_list(data_sock, nlst_cmd);
            close(data_sock);
            // Send a completion reply to the client after the directory list has been sent
            snprintf(buf, sizeof(buf), "226 Result is sent successfully.\n");
            write_all(control_sock, buf, strlen(buf));
            printf("%s", buf);
        }
        else
        {
            close(data_sock);
        }
    }
    // Clean up and close sockets before exiting
    close(control_sock);
    close(listen_sock);
    return 0;
}
