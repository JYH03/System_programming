///////////////////////////////////////////////////////////////////////////////
// File Name  : srv.c                                                        //
// Date       : 2026/05/13                                                   //
// OS         : Ubuntu 20.04.6 LTS 64bits                                    //
// Author     : Jo Young Hoon                                                //
// Student ID : 2022202078                                                   //
// --------------------------------------------------------------------------//
// Title      : System Programming Assignment 2-3 Server                     //
// Description:                                                              //
//   This server accepts TCP clients using fork(). The parent process        //
//   manages child process information and prints it every 10 seconds.       //
//   Each child process receives FTP commands from one client, executes them,//
//   and sends the execution result back to the client.                      //
///////////////////////////////////////////////////////////////////////////////

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <grp.h>
#include <netinet/in.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_BUFF 8192
#define MAX_ARGC 32
#define MAX_CLIENTS 128
#define LISTENQ 10

typedef struct
{
    pid_t pid;
    int port;
    time_t start_time;
} ClientInfo;

static ClientInfo g_clients[MAX_CLIENTS];
static int g_client_count = 0;
static int g_listenfd = -1;
static volatile sig_atomic_t g_sigchld = 0;
static volatile sig_atomic_t g_sigalrm = 0;
static volatile sig_atomic_t g_sigint = 0;

static char g_response[MAX_BUFF * 4];

static void sigchld_handler(int signo);
static void sigalrm_handler(int signo);
static void sigint_handler(int signo);
static ssize_t sender(int fd, const void *buf, size_t len);
static ssize_t receiver(int fd, void *buf, size_t len);
static int send_message(int fd, const char *msg);
static int recv_message(int fd, char **msg);
static void response_clear(void);
static void response_write(const char *msg);
static void response_error(const char *msg);
static char *copy_string(const char *src);
static int parse_command(char *cmd, char *args[]);
static int name_cmp(const void *a, const void *b);
static void mode_to_string(mode_t mode, char *out);
static void print_long_entry(const char *path, const char *name);
static void command_nlst(char **args, int argc);
static void command_pwd(void);
static void command_cwd(char **args, int argc);
static void command_cdup(void);
static void command_mkd(char **args, int argc);
static void command_dele(char **args, int argc);
static void command_rmd(char **args, int argc);
static void command_rename(char **args, int argc);
static int execute_ftp_command(char *cmdline);
static void service_client(int connfd);
static void add_client(pid_t pid, int port);
static void remove_client(pid_t pid);
static void reap_children(void);
static void print_client_table(void);
static void shutdown_server(void);
static int make_listen_socket(const char *port);

///////////////////////////////////////////////////////////////////////////////
// sigchld_handler                                                           //
// Input   : int signo - received signal number                              //
// Output  : void                                                            //
// Purpose : Records that a child process has terminated.                    //
///////////////////////////////////////////////////////////////////////////////
static void sigchld_handler(int signo)
{
    (void)signo;
    g_sigchld = 1;
}

///////////////////////////////////////////////////////////////////////////////
// sigalrm_handler                                                           //
// Input   : int signo - received signal number                              //
// Output  : void                                                            //
// Purpose : Records that the 10-second timer has expired.                   //
///////////////////////////////////////////////////////////////////////////////
static void sigalrm_handler(int signo)
{
    (void)signo;
    g_sigalrm = 1;
}

///////////////////////////////////////////////////////////////////////////////
// sigint_handler                                                            //
// Input   : int signo - received signal number                              //
// Output  : void                                                            //
// Purpose : Records that the server should terminate.                       //
///////////////////////////////////////////////////////////////////////////////
static void sigint_handler(int signo)
{
    (void)signo;
    g_sigint = 1;
}

///////////////////////////////////////////////////////////////////////////////
// sender                                                                    //
// Input   : int fd          - socket descriptor                             //
//           const void *buf - data buffer                                   //
//           size_t len      - byte count                                    //
// Output  : ssize_t         - sent byte count, -1 on error                  //
// Purpose : Sends every byte in the buffer.                                 //
///////////////////////////////////////////////////////////////////////////////
static ssize_t sender(int fd, const void *buf, size_t len)
{
    size_t total = 0;
    const char *ptr = (const char *)buf;

    while (total < len)
    {
        ssize_t n = write(fd, ptr + total, len - total);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        total += (size_t)n;
    }

    return (ssize_t)total;
}

///////////////////////////////////////////////////////////////////////////////
// receiver                                                                  //
// Input   : int fd     - socket descriptor                                  //
//           void *buf  - receive buffer                                     //
//           size_t len - byte count                                         //
// Output  : ssize_t    - received byte count, -1 on error                   //
// Purpose : Receives the requested number of bytes.                         //
///////////////////////////////////////////////////////////////////////////////
static ssize_t receiver(int fd, void *buf, size_t len)
{
    size_t total = 0;
    char *ptr = (char *)buf;

    while (total < len)
    {
        ssize_t n = read(fd, ptr + total, len - total);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (n == 0)
        {
            return 0;
        }
        total += (size_t)n;
    }

    return (ssize_t)total;
}

///////////////////////////////////////////////////////////////////////////////
// send_message                                                              //
// Input   : int fd          - socket descriptor                             //
//           const char *msg - message string                                //
// Output  : int             - 0 success, -1 fail                            //
// Purpose : Sends a length-prefixed message to the client.                  //
///////////////////////////////////////////////////////////////////////////////
static int send_message(int fd, const char *msg)
{
    uint32_t len = (uint32_t)strlen(msg);
    uint32_t net_len = htonl(len);

    if (sender(fd, &net_len, sizeof(net_len)) != (ssize_t)sizeof(net_len))
    {
        return -1;
    }
    if (sender(fd, msg, len) != (ssize_t)len)
    {
        return -1;
    }
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// recv_message                                                              //
// Input   : int fd     - socket descriptor                                  //
//           char **msg - received message buffer                            //
// Output  : int        - 0 success, -1 fail                                 //
// Purpose : Receives a length-prefixed message from the client.             //
///////////////////////////////////////////////////////////////////////////////
static int recv_message(int fd, char **msg)
{
    uint32_t net_len;
    uint32_t len;
    char *buffer;

    *msg = NULL;
    if (receiver(fd, &net_len, sizeof(net_len)) != (ssize_t)sizeof(net_len))
    {
        return -1;
    }

    len = ntohl(net_len);
    if (len > MAX_BUFF)
    {
        return -1;
    }

    buffer = (char *)malloc((size_t)len + 1U);
    if (buffer == NULL)
    {
        return -1;
    }

    if (receiver(fd, buffer, len) != (ssize_t)len)
    {
        free(buffer);
        return -1;
    }

    buffer[len] = '\0';
    *msg = buffer;
    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// response_clear                                                            //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Clears the per-command response buffer.                         //
///////////////////////////////////////////////////////////////////////////////
static void response_clear(void)
{
    g_response[0] = '\0';
}

///////////////////////////////////////////////////////////////////////////////
// response_write                                                            //
// Input   : const char *msg - message to append                             //
// Output  : void                                                            //
// Purpose : Appends a message to the response buffer.                       //
///////////////////////////////////////////////////////////////////////////////
static void response_write(const char *msg)
{
    size_t used = strlen(g_response);
    size_t left = sizeof(g_response) - used - 1U;

    if (left > 0)
    {
        strncat(g_response, msg, left);
    }
}

///////////////////////////////////////////////////////////////////////////////
// response_error                                                            //
// Input   : const char *msg - error message                                 //
// Output  : void                                                            //
// Purpose : Appends an FTP command error message.                           //
///////////////////////////////////////////////////////////////////////////////
static void response_error(const char *msg)
{
    response_write("Error: ");
    response_write(msg);
    response_write("\n");
}

///////////////////////////////////////////////////////////////////////////////
// copy_string                                                               //
// Input   : const char *src - source string                                 //
// Output  : char *          - duplicated string, NULL on failure            //
// Purpose : Allocates and copies a string without relying on strdup().      //
///////////////////////////////////////////////////////////////////////////////
static char *copy_string(const char *src)
{
    size_t len = strlen(src) + 1U;
    char *dst = (char *)malloc(len);

    if (dst != NULL)
    {
        memcpy(dst, src, len);
    }

    return dst;
}

///////////////////////////////////////////////////////////////////////////////
// parse_command                                                             //
// Input   : char *cmd   - FTP command line                                  //
//           char *args[]- token array                                       //
// Output  : int         - token count                                       //
// Purpose : Splits a command line into blank-separated tokens.              //
///////////////////////////////////////////////////////////////////////////////
static int parse_command(char *cmd, char *args[])
{
    int count = 0;
    char *saveptr;
    char *token;

    token = strtok_r(cmd, " \t\r\n", &saveptr);
    while (token != NULL && count < MAX_ARGC - 1)
    {
        args[count++] = token;
        token = strtok_r(NULL, " \t\r\n", &saveptr);
    }
    args[count] = NULL;
    return count;
}

///////////////////////////////////////////////////////////////////////////////
// name_cmp                                                                  //
// Input   : const void *a - first string pointer                            //
//           const void *b - second string pointer                           //
// Output  : int           - strcmp result                                   //
// Purpose : Compares directory entry names for sorting.                     //
///////////////////////////////////////////////////////////////////////////////
static int name_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

///////////////////////////////////////////////////////////////////////////////
// mode_to_string                                                            //
// Input   : mode_t mode - file mode                                         //
//           char *out   - permission string buffer                          //
// Output  : void                                                            //
// Purpose : Converts file mode bits to ls -l style text.                    //
///////////////////////////////////////////////////////////////////////////////
static void mode_to_string(mode_t mode, char *out)
{
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

///////////////////////////////////////////////////////////////////////////////
// print_long_entry                                                          //
// Input   : const char *path - file path used for stat                      //
//           const char *name - display name                                 //
// Output  : void                                                            //
// Purpose : Appends one long-format directory entry to the response buffer. //
///////////////////////////////////////////////////////////////////////////////
static void print_long_entry(const char *path, const char *name)
{
    struct stat st;
    struct passwd *pw;
    struct group *gr;
    char perm[11];
    char timebuf[64];
    char line[2048];
    char display_name[1024];
    struct tm *tm_info;

    if (lstat(path, &st) < 0)
    {
        return;
    }

    mode_to_string(st.st_mode, perm);
    pw = getpwuid(st.st_uid);
    gr = getgrgid(st.st_gid);
    tm_info = localtime(&st.st_mtime);
    if (tm_info != NULL)
    {
        strftime(timebuf, sizeof(timebuf), "%b %d %H:%M", tm_info);
    }
    else
    {
        strcpy(timebuf, "??? ?? ??:??");
    }

    snprintf(display_name, sizeof(display_name), "%s%s", name,
             S_ISDIR(st.st_mode) ? "/" : "");
    snprintf(line, sizeof(line), "%s %3ld %-8s %-8s %8ld %s %s\n",
             perm, (long)st.st_nlink,
             pw ? pw->pw_name : "unknown",
             gr ? gr->gr_name : "unknown",
             (long)st.st_size, timebuf, display_name);
    response_write(line);
}

///////////////////////////////////////////////////////////////////////////////
// command_nlst                                                              //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes NLST or LIST.                                          //
///////////////////////////////////////////////////////////////////////////////
static void command_nlst(char **args, int argc)
{
    int a_flag = 0;
    int l_flag = 0;
    int i;
    int count = 0;
    int path_count = 0;
    int col_count = 0;
    char *target = ".";
    struct stat st;
    char **names = NULL;
    DIR *dirp;
    struct dirent *entry;

    // Check if the command is LIST, which implies both -a and -l options.
    if (strcmp(args[0], "LIST") == 0)
    {
        a_flag = 1;
        l_flag = 1;
    }

    // Parse options and target path from arguments.
    for (i = 1; i < argc; i++)
    {
        // If the argument starts with '-', it's an option string.
        if (args[i][0] == '-')
        {
            int j;

            // Validate options and set flags.
            if (args[i][1] == '\0')
            {
                response_error("invalid option");
                return;
            }
            // Check each character in the option string.
            for (j = 1; args[i][j] != '\0'; j++)
            {
                if (args[i][j] != 'a' && args[i][j] != 'l')
                {
                    response_error("invalid option");
                    return;
                }
            }
            // Set flags based on options.
            if (strchr(args[i], 'a') != NULL)
            {
                a_flag = 1;
            }
            // 'l' option is valid for both LIST and NLST, but only LIST implies it by default.
            if (strchr(args[i], 'l') != NULL)
            {
                l_flag = 1;
            }
        }
        // If it's not an option, treat it as a target path.
        else
        {
            path_count++;
            target = args[i];
        }
    }

    // Validate that at most one target path is specified.
    if (path_count > 1)
    {
        response_error("too many arguments");
        return;
    }
    // If the command is NLST and no options are specified, default to no options.
    if (strcmp(args[0], "LIST") == 0)
    {
        response_write("LIST\n");
    }
    // For NLST, include options in the response header if specified.
    else
    {
        // For NLST, we include the options in the response header if specified.
        response_write("NLST\t");
        if (a_flag || l_flag)
        {
            response_write("-");
            if (a_flag)
            {
                response_write("a");
            }
            if (l_flag)
            {
                response_write("l");
            }
        }
        response_write("\n");
    }
    // Check if the target path exists and get its status.
    if (stat(target, &st) < 0)
    {
        response_error("No such file or directory");
        return;
    }
    //
    if (!S_ISDIR(st.st_mode))
    {
        // If the target is a file, only print it if it's not hidden or if -a is specified.
        if (a_flag)
        {
            response_error("invalid option");
            return;
        }
        // For a file, we only print it if it's not hidden or if -a is specified.
        if (l_flag)
        {
            print_long_entry(target, target);
        }
        // For NLST, we just print the file name without details.
        else
        {
            response_write(target);
            response_write("\n");
        }
        return;
    }
    // If the target is a directory, open it and read entries.
    dirp = opendir(target);
    if (dirp == NULL)
    {
        response_error("Failed to open directory");
        return;
    }
    // Allocate an array to hold entry names for sorting.
    names = (char **)calloc(1024, sizeof(char *));
    if (names == NULL)
    {
        closedir(dirp);
        response_error("Memory allocation failed");
        return;
    }
    // Read directory entries and store their names, applying the -a option filter.
    while ((entry = readdir(dirp)) != NULL && count < 1024)
    {
        if (!a_flag && entry->d_name[0] == '.')
        {
            continue;
        }
        names[count] = copy_string(entry->d_name);
        if (names[count] == NULL)
        {
            break;
        }
        count++;
    }
    closedir(dirp);
    // Sort the entry names alphabetically.
    qsort(names, (size_t)count, sizeof(char *), name_cmp);
    for (i = 0; i < count; i++)
    {
        // For each entry, print it in long format if -l is specified, otherwise just the name.
        if (l_flag)
        {
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", target, names[i]);
            print_long_entry(path, names[i]);
        }
        else
        {
            char path[1024];
            char item[512];
            // For NLST, we just print the file name, appending '/' if it's a directory.
            snprintf(path, sizeof(path), "%s/%s", target, names[i]);
            if (stat(path, &st) == 0 && S_ISDIR(st.st_mode))
            {
                snprintf(item, sizeof(item), "%s/", names[i]);
            }
            else
            {
                snprintf(item, sizeof(item), "%s", names[i]);
            }
            // For NLST, we just print the file name, appending '/' if it's a directory.
            response_write(item);
            for (int j = (int)strlen(item); j < 15; j++)
            {
                response_write(" ");
            }
            col_count++;
            if (col_count == 5)
            {
                response_write("\n");
                col_count = 0;
            }
        }
        free(names[i]);
    }
    // If we ended with a partial line in NLST, add a newline at the end.
    if (!l_flag && col_count > 0)
    {
        response_write("\n");
    }
    free(names);
}

///////////////////////////////////////////////////////////////////////////////
// command_pwd                                                               //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Executes PWD.                                                   //
///////////////////////////////////////////////////////////////////////////////
static void command_pwd(void)
{
    char cwd[1024];
    // Get the current working directory and include it in the response.
    if (getcwd(cwd, sizeof(cwd)) == NULL)
    {
        response_error("getcwd failed");
        return;
    }
    // Include the current directory in the response.
    response_write("'");
    response_write(cwd);
    response_write("' is current directory\n");
}

///////////////////////////////////////////////////////////////////////////////
// command_cwd                                                               //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes CWD.                                                   //
///////////////////////////////////////////////////////////////////////////////
static void command_cwd(char **args, int argc)
{
    // Validate that exactly one argument is provided for the target directory.
    if (argc < 2)
    {
        response_error("directory not found");
        return;
    }
    // Validate that no extra arguments are provided.
    if (argc > 2)
    {
        response_error("too many arguments");
        return;
    }
    // Attempt to change the current working directory to the specified path.
    if (chdir(args[1]) < 0)
    {
        response_error("directory not found");
        return;
    }
    // Include the new current directory in the response.
    response_write("CWD ");
    response_write(args[1]);
    response_write("\n");
    command_pwd();
}

///////////////////////////////////////////////////////////////////////////////
// command_cdup                                                              //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Executes CDUP.                                                  //
///////////////////////////////////////////////////////////////////////////////
static void command_cdup(void)
{
    // Attempt to change the current working directory to the parent directory.
    if (chdir("..") < 0)
    {
        response_error("directory not found");
        return;
    }
    // Include the new current directory in the response.
    response_write("CDUP\n");
    command_pwd();
}

///////////////////////////////////////////////////////////////////////////////
// command_mkd                                                               //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes MKD.                                                   //
///////////////////////////////////////////////////////////////////////////////
static void command_mkd(char **args, int argc)
{
    int i;
    // Validate that at least one argument is provided for the directory name.
    if (argc < 2)
    {
        response_error("argument is required");
        return;
    }
    // Attempt to create directories for each specified name and include the result in the response.
    for (i = 1; i < argc; i++)
    {
        if (mkdir(args[i], 0755) < 0)
        {
            response_write("Error: cannot create directory '");
            response_write(args[i]);
            response_write("': File exists\n");
        }
        else
        {
            response_write("MKD ");
            response_write(args[i]);
            response_write("\n");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// command_dele                                                              //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes DELE.                                                  //
///////////////////////////////////////////////////////////////////////////////
static void command_dele(char **args, int argc)
{
    int i;
    // Validate that at least one argument is provided for the file name.
    if (argc < 2)
    {
        response_error("argument is required");
        return;
    }
    // Attempt to delete files for each specified name and include the result in the response.
    for (i = 1; i < argc; i++)
    {
        if (unlink(args[i]) < 0)
        {
            response_error("failed to stat the file or directory");
        }
        else
        {
            response_write("DELE ");
            response_write(args[i]);
            response_write("\n");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// command_rmd                                                               //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes RMD.                                                   //
///////////////////////////////////////////////////////////////////////////////
static void command_rmd(char **args, int argc)
{
    int i;
    // Validate that at least one argument is provided for the directory name.
    if (argc < 2)
    {
        response_error("argument is required");
        return;
    }
    // Attempt to remove directories for each specified name and include the result in the response.
    for (i = 1; i < argc; i++)
    {
        if (rmdir(args[i]) < 0)
        {
            response_write("Error: failed to remove ");
            response_write(args[i]);
            response_write("\n");
        }
        else
        {
            response_write("RMD ");
            response_write(args[i]);
            response_write("\n");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// command_rename                                                            //
// Input   : char **args - parsed FTP command                                //
//           int argc    - argument count                                    //
// Output  : void                                                            //
// Purpose : Executes RNFR/RNTO.                                             //
///////////////////////////////////////////////////////////////////////////////
static void command_rename(char **args, int argc)
{
    // Validate that the command is RNFR and that exactly two arguments are provided for the source and target names.
    if (argc != 4 || strcmp(args[2], "RNTO") != 0)
    {
        response_error("two arguments are required");
        return;
    }
    // Validate that the source file or directory exists.
    if (access(args[3], F_OK) == 0)
    {
        response_error("name to change already exists");
        return;
    }
    // Attempt to rename the source file or directory to the target name and include the result in the response.
    if (rename(args[1], args[3]) < 0)
    {
        response_error("Failed to rename file");
        return;
    }
    // Include the rename operation in the response.
    response_write("RNFR ");
    response_write(args[1]);
    response_write("\nRNTO ");
    response_write(args[3]);
    response_write("\n");
}

///////////////////////////////////////////////////////////////////////////////
// execute_ftp_command                                                       //
// Input   : char *cmdline - FTP command line                                //
// Output  : int           - 1 if QUIT, otherwise 0                          //
// Purpose : Dispatches one FTP command to the proper handler.               //
///////////////////////////////////////////////////////////////////////////////
static int execute_ftp_command(char *cmdline)
{
    char *args[MAX_ARGC];
    int argc;
    // Parse the command line into arguments and determine which command to execute.
    argc = parse_command(cmdline, args);
    if (argc <= 0)
    {
        response_error("Invalid command");
        return 0;
    }
    // Dispatch the command to the appropriate handler based on the first argument.
    if (strcmp(args[0], "NLST") == 0 || strcmp(args[0], "LIST") == 0)
    {
        command_nlst(args, argc);
    }
    else if (strcmp(args[0], "PWD") == 0)
    {
        command_pwd();
    }
    else if (strcmp(args[0], "CWD") == 0)
    {
        command_cwd(args, argc);
    }
    else if (strcmp(args[0], "CDUP") == 0)
    {
        command_cdup();
    }
    else if (strcmp(args[0], "MKD") == 0)
    {
        command_mkd(args, argc);
    }
    else if (strcmp(args[0], "DELE") == 0)
    {
        command_dele(args, argc);
    }
    else if (strcmp(args[0], "RMD") == 0)
    {
        command_rmd(args, argc);
    }
    else if (strcmp(args[0], "RNFR") == 0)
    {
        command_rename(args, argc);
    }
    else if (strcmp(args[0], "QUIT") == 0)
    {
        response_write("QUIT");
        return 1;
    }
    // If the command is not recognized, return an error message.
    else
    {
        response_error("Unknown command");
    }

    return 0;
}

///////////////////////////////////////////////////////////////////////////////
// service_client                                                            //
// Input   : int connfd - connected socket descriptor                        //
// Output  : void                                                            //
// Purpose : Handles one client in a child process.                          //
///////////////////////////////////////////////////////////////////////////////
static void service_client(int connfd)
{
    char *cmd;
    // Loop to receive commands from the client, execute them, and send responses until the client disconnects or sends QUIT.
    while (recv_message(connfd, &cmd) == 0)
    {
        int quit = 0;
        char *line;
        char *saveptr;
        // Clear the response buffer before processing the new command.
        response_clear();
        line = strtok_r(cmd, "\n", &saveptr);
        // Process each line of the received command, which may contain multiple commands separated by newlines.
        while (line != NULL)
        {
            char logbuf[MAX_BUFF + 64];
            snprintf(logbuf, sizeof(logbuf), "> %s [%d]\n", line, (int)getpid());
            write(STDOUT_FILENO, logbuf, strlen(logbuf));

            quit = execute_ftp_command(line);
            if (quit)
            {
                break;
            }
            line = strtok_r(NULL, "\n", &saveptr);
        }
        free(cmd);
        // Send the accumulated response back to the client. If sending fails, break the loop to terminate the connection.
        if (send_message(connfd, g_response) < 0)
        {
            break;
        }
        //  If the client sent a QUIT command, break the loop to terminate the connection.
        if (quit)
        {
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// add_client                                                                //
// Input   : pid_t pid - child process ID                                    //
//           int port  - client port number                                  //
// Output  : void                                                            //
// Purpose : Adds a child process to the parent's client table.              //
///////////////////////////////////////////////////////////////////////////////
static void add_client(pid_t pid, int port)
{
    if (g_client_count >= MAX_CLIENTS)
    {
        return;
    }
    // Add a new client to the client table with its PID, port, and connection start time.
    g_clients[g_client_count].pid = pid;
    g_clients[g_client_count].port = port;
    g_clients[g_client_count].start_time = time(NULL);
    g_client_count++;
}

///////////////////////////////////////////////////////////////////////////////
// remove_client                                                             //
// Input   : pid_t pid - child process ID                                    //
// Output  : void                                                            //
// Purpose : Removes a child process from the parent's client table.         //
///////////////////////////////////////////////////////////////////////////////
static void remove_client(pid_t pid)
{
    int i;
    // Search for the client with the specified PID in the client table and remove it by replacing it with the last entry.
    for (i = 0; i < g_client_count; i++)
    {
        if (g_clients[i].pid == pid)
        {
            printf("Client(%d)'s Release\n", (int)pid);
            g_clients[i] = g_clients[g_client_count - 1];
            g_client_count--;
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// reap_children                                                             //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Reaps terminated children and updates the client table.         //
///////////////////////////////////////////////////////////////////////////////
static void reap_children(void)
{
    int status;
    pid_t pid;
    // Reap all terminated child processes without blocking and remove them from the client table.
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        (void)status;
        remove_client(pid);
    }
}

///////////////////////////////////////////////////////////////////////////////
// print_client_table                                                        //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Prints current child process information.                       //
///////////////////////////////////////////////////////////////////////////////
static void print_client_table(void)
{
    int i;
    time_t now = time(NULL);
    // Print the number of currently connected clients and their details (PID, port, and connection duration).
    printf("Current Number Of Client : %d\n", g_client_count);
    printf("    PID     PORT     TIME\n");
    for (i = 0; i < g_client_count; i++)
    {
        printf("%7d %8d %8ld\n",
               (int)g_clients[i].pid,
               g_clients[i].port,
               (long)(now - g_clients[i].start_time));
    }
    fflush(stdout);
}

///////////////////////////////////////////////////////////////////////////////
// shutdown_server                                                           //
// Input   : void                                                            //
// Output  : void                                                            //
// Purpose : Terminates all child processes and closes the listening socket. //
///////////////////////////////////////////////////////////////////////////////
static void shutdown_server(void)
{
    int i;
    // Send SIGINT to all child processes to request them to terminate.
    for (i = 0; i < g_client_count; i++)
    {
        kill(g_clients[i].pid, SIGINT);
    }
    // Wait for all child processes to terminate.
    while (waitpid(-1, NULL, 0) > 0)
    {
    }
    // Close the listening socket if it's open.
    if (g_listenfd >= 0)
    {
        close(g_listenfd);
        g_listenfd = -1;
    }

    printf("Server terminated\n");
    fflush(stdout);
}

///////////////////////////////////////////////////////////////////////////////
// make_listen_socket                                                        //
// Input   : const char *port - server port text                             //
// Output  : int              - listen socket descriptor, -1 on error        //
// Purpose : Creates, binds, and listens on a TCP socket.                    //
///////////////////////////////////////////////////////////////////////////////
static int make_listen_socket(const char *port)
{
    int listenfd;
    int opt = 1;
    struct sockaddr_in servaddr;
    // Create a TCP socket for listening.
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0)
    {
        perror("socket");
        return -1;
    }
    // Set the SO_REUSEADDR option to allow quick reuse of the port after the server is restarted.
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons((uint16_t)atoi(port));
    // Bind the socket to the specified port on all interfaces.
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
    {
        perror("bind");
        close(listenfd);
        return -1;
    }
    // Start listening for incoming connections with a backlog of LISTENQ.
    if (listen(listenfd, LISTENQ) < 0)
    {
        perror("listen");
        close(listenfd);
        return -1;
    }

    return listenfd;
}

///////////////////////////////////////////////////////////////////////////////
// main                                                                      //
// Input   : int argc    - argument count                                    //
//           char **argv - port number                                       //
// Output  : int         - process exit status                               //
// Purpose : Runs the concurrent FTP server.                                 //
///////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    struct sigaction act_chld;
    struct sigaction act_alrm;
    struct sigaction act_int;
    struct sockaddr_in cliaddr;
    socklen_t clilen;
    // Validate that the port number is provided as a command-line argument.
    if (argc != 2)
    {
        fprintf(stderr, "Usage: ./srv <port>\n");
        return 1;
    }
    // SIGCHLD handler
    memset(&act_chld, 0, sizeof(act_chld));
    act_chld.sa_handler = sigchld_handler;
    sigemptyset(&act_chld.sa_mask);
    act_chld.sa_flags = 0;
    sigaction(SIGCHLD, &act_chld, NULL);
    // SIGALRM handler
    memset(&act_alrm, 0, sizeof(act_alrm));
    act_alrm.sa_handler = sigalrm_handler;
    sigemptyset(&act_alrm.sa_mask);
    act_alrm.sa_flags = 0;
    sigaction(SIGALRM, &act_alrm, NULL);
    // SIGINT handler
    memset(&act_int, 0, sizeof(act_int));
    act_int.sa_handler = sigint_handler;
    sigemptyset(&act_int.sa_mask);
    act_int.sa_flags = 0;
    sigaction(SIGINT, &act_int, NULL);

    // Create the listening socket on the specified port.
    g_listenfd = make_listen_socket(argv[1]);
    if (g_listenfd < 0)
    {
        return 1;
    }

    // Set an alarm to trigger every 10 seconds for periodic client table printing.
    alarm(10);

    // Main server loop to accept and handle client connections until SIGINT is received.
    while (!g_sigint)
    {
        int connfd;
        pid_t pid;

        // Check if a SIGCHLD signal was received, indicating that a child process has terminated.
        if (g_sigchld)
        {
            g_sigchld = 0;
            reap_children();
        }
        // Check if a SIGALRM signal was received, indicating that it's time to print the client table.
        if (g_sigalrm)
        {
            g_sigalrm = 0;
            print_client_table();
            alarm(10);
        }

        // Accept a new client connection. If interrupted by a signal, retry the accept call.
        clilen = sizeof(cliaddr);
        connfd = accept(g_listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            perror("accept");
            continue;
        }

        // Fork a child process to handle the client connection. The parent process will continue to accept new connections.
        pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(connfd);
            continue;
        }

        // In the child process, close the listening socket, reset signal handlers to default, and service the client.
        if (pid == 0)
        {
            close(g_listenfd);
            signal(SIGALRM, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            signal(SIGINT, SIG_DFL);
            service_client(connfd);
            close(connfd);
            exit(0);
        }

        add_client(pid, ntohs(cliaddr.sin_port));
        printf("========== Client info ==========\n");
        printf("client IP   : %s\n", inet_ntoa(cliaddr.sin_addr));
        printf("client port : %d\n", ntohs(cliaddr.sin_port));
        printf("=================================\n");
        printf("Child Process ID : %d\n", (int)pid);
        print_client_table();
        alarm(10);
        close(connfd);
    }
    shutdown_server();
    return 0;
}
