///////////////////////////////////////////////////////////////////////////////
// File Name: srv.c                                                          //
// Date :2026/04/15                                                          //
// OS   : Ubuntu 20.04.6 LTS 64bits                                          //
// Author : jo yeong-hoon                                                    //
// Student ID : 2022202078                                                   //
// ------------------------------------------------------------------------- //
// Title : System Programming Assignment #1-3 ( ftp server )                 //
// Description : "This program is the server side of a simple                //
// FTP implementation. It executes client commands, outputs results, and     //
// handles exceptions by returning error messages for invalid operations     //
// like accessing missing files or directories."                             //
///////////////////////////////////////////////////////////////////////////////
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <errno.h>

#define B_size 4096

///////////////////////////////////////////////////////////////////////////
// wprint                                                                //
// ===================================================================== //
// Input: const char* -> Message string to be printed                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Prints a given message to the standard output (STDOUT)       //
///////////////////////////////////////////////////////////////////////////
void wprint(const char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}
///////////////////////////////////////////////////////////////////////////
// wprint_err                                                            //
// ===================================================================== //
// Input: const char* -> Error message string to be printed              //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Prints an error message to STDOUT with an "Error: " prefix   //
///////////////////////////////////////////////////////////////////////////
void wprint_err(const char *msg)
{
    wprint("Error: ");
    wprint(msg);
    wprint("\n");
}
///////////////////////////////////////////////////////////////////////////
// cmp                                                                   //
// ===================================================================== //
// Input: const void* -> Pointer to the first string pointer             //
//        const void* -> Pointer to the second string pointer            //
//        (Input parameter Description)                                  //
// Output: int -  0 if strings are equal                                 //
//               >0 if first string is greater than second               //
//               <0 if first string is less than second                  //
//         (Out parameter Description)                                   //
// Purpose: Compare function used by qsort to sort string arrays         //
///////////////////////////////////////////////////////////////////////////
int cmp(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}
///////////////////////////////////////////////////////////////////////////
// int_to_array                                                          //
// ===================================================================== //
// Input: int   -> Integer number to be converted                        //
//        char* -> Buffer to store the converted string                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Converts an integer to a string without using sprintf        //
///////////////////////////////////////////////////////////////////////////
void int_to_array(int num, char *str)
{
    int i = 0;
    int isNegative = 0;

    // Handle 0 explicitly
    if (num == 0)
    {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (num < 0)
    {
        isNegative = 1;
        num = -num;
    }

    // Extract digits from right to left (modulo 10)
    while (num != 0)
    {
        int rem = num % 10;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        num = num / 10;
    }

    if (isNegative)
        str[i++] = '-';
    str[i] = '\0';

    // Reverse the string since digits were extracted backwards
    int start = 0, end = i - 1;
    while (start < end)
    {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}
///////////////////////////////////////////////////////////////////////////
// parse_command                                                         //
// ===================================================================== //
// Input: char* -> Command string to be parsed                           //
//        char*[] -> Array to store the parsed tokens                    //
//        (Input parameter Description)                                  //
// Output: int - Number of parsed tokens (arguments count)               //
//         (Out parameter Description)                                   //
// Purpose: Tokenizes a string by spaces and newlines into an array      //
///////////////////////////////////////////////////////////////////////////
int parse_command(char *cmd, char *args[])
{
    int count = 0;
    char *ptr;

    // Use strtok_r for thread-safe tokenization
    char *token = strtok_r(cmd, " \n", &ptr);

    // Loop until no more tokens are found
    while (token != NULL)
    {
        args[count++] = token;
        token = strtok_r(NULL, " \n", &ptr);
    }

    args[count] = NULL; // NULL-terminate the arguments array
    return count;
}

///////////////////////////////////////////////////////////////////////////
// NLST                                                                  //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes NLST/LIST command to show directory/file contents   //
///////////////////////////////////////////////////////////////////////////
void NLST(char **args, int arg_cnt)
{
    int a_flag = 0, l_flag = 0;
    char *cur_path = "./";

    // Loop to separate options (-a, -l) from the target path
    for (int i = 1; i < arg_cnt; i++)
    {
        if (args[i][0] == '-')
        {
            if (strchr(args[i], 'a'))
                a_flag = 1;
            if (strchr(args[i], 'l'))
                l_flag = 1;
        }
        else
        {
            cur_path = args[i]; // Treat non-option argument as the target path
        }
    }

    // Output headers based on the command type (LIST vs NLST)
    if (strcmp(args[0], "LIST") == 0)
    {
        wprint("LIST\n");
        a_flag = 1;
        l_flag = 1;
    }
    else
    {
        wprint("NLST\t");
        if (a_flag || l_flag)
        {
            wprint("-");
            if (a_flag)
                wprint("a");
            if (l_flag)
                wprint("l");
        }
        wprint("\n");
    }

    struct stat st;
    char name_buf[256];

    // Check if the target path exists and retrieve its metadata
    if (stat(cur_path, &st) < 0)
    {
        wprint_err("No such file or directory");
        return;
    }

    // Branch 1: If the target is a regular file (not a directory)
    if (S_ISREG(st.st_mode))
    {
        strcpy(name_buf, cur_path);

        // Detailed format (-l) for a single file
        if (l_flag && !a_flag)
        {
            // Build the permission string using bitwise AND operations
            char perms[11] = "----------";
            if (S_ISDIR(st.st_mode))
                perms[0] = 'd';
            if (st.st_mode & S_IRUSR)
                perms[1] = 'r';
            if (st.st_mode & S_IWUSR)
                perms[2] = 'w';
            if (st.st_mode & S_IXUSR)
                perms[3] = 'x';
            if (st.st_mode & S_IRGRP)
                perms[4] = 'r';
            if (st.st_mode & S_IWGRP)
                perms[5] = 'w';
            if (st.st_mode & S_IXGRP)
                perms[6] = 'x';
            if (st.st_mode & S_IROTH)
                perms[7] = 'r';
            if (st.st_mode & S_IWOTH)
                perms[8] = 'w';
            if (st.st_mode & S_IXOTH)
                perms[9] = 'x';
            wprint(perms);
            wprint(" ");

            // Convert and print link count
            char num_buf[32];
            int_to_array(st.st_nlink, num_buf);
            wprint(num_buf);
            wprint(" ");

            // Retrieve and print user and group names using IDs
            struct passwd *Password = getpwuid(st.st_uid);
            struct group *gr = getgrgid(st.st_gid);
            wprint(Password->pw_name);
            wprint(" ");
            wprint(gr->gr_name);
            wprint(" ");

            // Convert and print file size
            int_to_array(st.st_size, num_buf);
            wprint(num_buf);
            wprint(" ");

            // Format and print the last modification time
            char time_buf[20];
            struct tm *tm_info = localtime(&st.st_mtime);
            strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm_info);
            wprint(time_buf);

            wprint(" ");
            wprint(name_buf);
            wprint("\n");
        }
        // Basic format for a single file (no -l flag)
        else if (!a_flag && !l_flag)
        {
            wprint(name_buf);
            wprint("\n");
        }
        else
        {
            wprint_err("invalid option");
            return;
        }
    }
    // Branch 2: If the target is a directory
    else
    {
        DIR *dir = opendir(cur_path);

        // Handle directory open errors based on errno
        if (dir == NULL)
        {
            if (errno == ENOENT)
                wprint_err("No such file or directory");
            else if (errno == EACCES)
                wprint_err("cannot access"); // Permission denied
            else
                wprint_err("Failed to open directory");
            return;
        }

        struct dirent *info;
        char *filename[1024];
        int file_cnt = 0;

        // Read all entries in the directory
        while ((info = readdir(dir)) != NULL)
        {
            // Skip hidden files (starting with '.') if -a flag is not set
            if (!a_flag && info->d_name[0] == '.')
                continue;
            filename[file_cnt] = strdup(info->d_name);
            file_cnt++;
        }
        closedir(dir);

        // Sort the filenames alphabetically
        qsort(filename, file_cnt, sizeof(char *), cmp);
        int col_cnt = 0;

        // Iterate through sorted files to print them
        for (int i = 0; i < file_cnt; i++)
        {
            // Construct the full path to run stat() correctly
            char full_path[1024];
            strcpy(full_path, cur_path);
            if (strcmp(cur_path, "./") != 0)
                strcat(full_path, "/");
            strcat(full_path, filename[i]);

            // Skip if stat fails (e.g., broken symlink)
            if (stat(full_path, &st) < 0)
                continue;

            char name_buf[256];
            strcpy(name_buf, filename[i]);

            // Append '/' to directory names for visual distinction
            if (S_ISDIR(st.st_mode))
            {
                strcat(name_buf, "/");
            }

            // Detailed list format (-l)
            if (l_flag)
            {
                char perms[11] = "----------";
                if (S_ISDIR(st.st_mode))
                    perms[0] = 'd';
                if (st.st_mode & S_IRUSR)
                    perms[1] = 'r';
                if (st.st_mode & S_IWUSR)
                    perms[2] = 'w';
                if (st.st_mode & S_IXUSR)
                    perms[3] = 'x';
                if (st.st_mode & S_IRGRP)
                    perms[4] = 'r';
                if (st.st_mode & S_IWGRP)
                    perms[5] = 'w';
                if (st.st_mode & S_IXGRP)
                    perms[6] = 'x';
                if (st.st_mode & S_IROTH)
                    perms[7] = 'r';
                if (st.st_mode & S_IWOTH)
                    perms[8] = 'w';
                if (st.st_mode & S_IXOTH)
                    perms[9] = 'x';
                wprint(perms);
                wprint(" ");

                // link count
                char num_buf[32];
                int_to_array(st.st_nlink, num_buf);
                wprint(num_buf);
                wprint(" ");

                // uid, gid
                struct passwd *Password = getpwuid(st.st_uid);
                struct group *gr = getgrgid(st.st_gid);
                wprint(Password->pw_name);
                wprint(" ");
                wprint(gr->gr_name);
                wprint(" ");

                // size of file
                int_to_array(st.st_size, num_buf);
                wprint(num_buf);
                wprint(" ");

                // time information
                char time_buf[20];
                struct tm *tm_info = localtime(&st.st_mtime);
                strftime(time_buf, sizeof(time_buf), "%b %d %H:%M", tm_info);
                wprint(time_buf);

                // directory name
                wprint(" ");
                wprint(name_buf);
                wprint("\n");
            }
            // Basic format (Grid layout)
            else
            {
                wprint(name_buf);
                int len = strlen(name_buf);

                // Pad with spaces to align columns (15 characters width)
                for (int j = len; j < 15; j++)
                {
                    wprint(" ");
                }

                col_cnt++;
                // Print newline after 5 items to form a grid
                if (col_cnt == 5)
                {
                    wprint("\n");
                    col_cnt = 0;
                }
            }
            free(filename[i]); // Free duplicated string memory
        }

        // Print final newline if the grid row didn't finish exactly at 5
        if (!l_flag && col_cnt > 0)
            wprint("\n");
    }
}
///////////////////////////////////////////////////////////////////////////
// PWD                                                                   //
// ===================================================================== //
// Input: void - None                                                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes PWD command to print the current working directory  //
///////////////////////////////////////////////////////////////////////////
void PWD()
{
    char cwd[B_size];
    // Retrieve the absolute path of the current working directory
    if (getcwd(cwd, sizeof(cwd)) != NULL)
    {
        wprint("'");
        wprint(cwd);
        wprint("' is current directory\n");
    }
    else
    {
        wprint_err("failed to get current directory");
    }
}
///////////////////////////////////////////////////////////////////////////
// CWD                                                                   //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes CWD command to change the current working directory //
///////////////////////////////////////////////////////////////////////////
void CWD(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("directory not found");
        return;
    }

    char cwd[B_size];
    // Attempt to change directory to the specified path
    if (chdir(args[1]) == 0)
    {
        wprint("CWD ");
        wprint(args[1]);
        wprint("\n");

        getcwd(cwd, sizeof(cwd));
        wprint("'");
        wprint(cwd);
        wprint("' is current directory\n");
    }
    else
    {
        wprint_err("directory not found");
    }
}
///////////////////////////////////////////////////////////////////////////
// CDUP                                                                  //
// ===================================================================== //
// Input: void - None                                                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes CDUP command to move to the parent directory        //
///////////////////////////////////////////////////////////////////////////
void CDUP()
{
    char cwd[B_size];
    // Move up one level in the directory hierarchy
    if (chdir("..") == 0)
    {
        wprint("CDUP\n");
        getcwd(cwd, sizeof(cwd));
        wprint("'");
        wprint(cwd);
        wprint("' is current directory\n");
    }
    else
    {
        wprint_err("directory not found");
    }
}
///////////////////////////////////////////////////////////////////////////
// Mkdir                                                                 //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes MKD command to create new directories               //
///////////////////////////////////////////////////////////////////////////
void Mkdir(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("argument is required");
        return;
    }

    // Loop through arguments to support creating multiple directories at once
    for (int i = 1; i < arg_cnt; i++)
    {
        // Try to create the directory with 0775 permissions (rwxrwxr-x)
        if (mkdir(args[i], 0775) == 0)
        {
            wprint("MKD ");
            wprint(args[i]);
            wprint("\n");
        }
        else // Fails if file/dir already exists or permission denied
        {
            wprint("Error: cannot create directory '");
            wprint(args[i]);
            wprint("': File exists\n");
        }
    }
}
///////////////////////////////////////////////////////////////////////////
// DELE                                                                  //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes DELE command to delete specified files              //
///////////////////////////////////////////////////////////////////////////
void DELE(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("argument is required");
        return;
    }

    // Process multiple file deletion arguments
    for (int i = 1; i < arg_cnt; i++)
    {
        // Use unlink() to delete regular files
        if (unlink(args[i]) == 0)
        {
            wprint("DELE ");
            wprint(args[i]);
            wprint("\n");
        }
        else
        {
            wprint_err("failed to stat the file or directory");
        }
    }
}
///////////////////////////////////////////////////////////////////////////
// RMD                                                                   //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes RMD command to remove specified directories         //
///////////////////////////////////////////////////////////////////////////
void RMD(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("argument is required");
        return;
    }

    // Process multiple directory removal arguments
    for (int i = 1; i < arg_cnt; i++)
    {
        // Use rmdir() to delete empty directories
        if (rmdir(args[i]) == 0)
        {
            wprint("RMD ");
            wprint(args[i]);
            wprint("\n");
        }
        else
        {
            wprint("Error: failed to remove '");
            wprint(args[i]);
            wprint("'\n");
        }
    }
}
///////////////////////////////////////////////////////////////////////////
// RNFR_RNTO                                                             //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes RNFR and RNTO commands sequentially to rename a file//
///////////////////////////////////////////////////////////////////////////
void RNFR_RNTO(char **args, int arg_cnt)
{
    // Ensure strict format: RNFR [old_name] RNTO [new_name]
    if (arg_cnt != 4 || strcmp(args[2], "RNTO") != 0)
    {
        wprint_err("two arguments are required");
        return;
    }
    char *prev = args[1];
    char *cur = args[3];
    struct stat st;

    // Prevent overwriting: Check if the new name already exists
    if (stat(cur, &st) == 0)
    {
        wprint_err("name to change already exists");
        return;
    }

    // Perform the atomic rename operation
    if (rename(prev, cur) == 0)
    {
        wprint("RNFR ");
        wprint(prev);
        wprint("\nRNTO ");
        wprint(cur);
        wprint("\n");
    }
    else
    {
        wprint("failed to rename");
    }
}
///////////////////////////////////////////////////////////////////////////
// RETR                                                                  //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes RETR command to read and send a file to the client  //
///////////////////////////////////////////////////////////////////////////
void RETR(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("argument is required");
        return;
    }

    char src_path[256] = "server_root/";
    strcat(src_path, args[1]);
    char dest_path[256] = "client_root/";
    strcat(dest_path, args[1]);

    // 1. Open the source file in the server directory for reading only
    int fd_src = open(src_path, O_RDONLY);
    if (fd_src < 0)
    {
        wprint("Error: '");
        wprint(args[1]);
        wprint("' does not exist in server_root\n");
        return;
    }

    // 2. Open/Create the destination file in the client directory for writing
    // O_TRUNC clears the file if it exists; 0644 sets standard rw-r--r-- permissions
    int fd_dest = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dest < 0)
    {
        wprint_err("failed to create file in client_root");
        close(fd_src);
        return;
    }

    // 3. Copy file data using a buffer loop
    char file_buf[B_size];
    ssize_t n;
    int total_bytes = 0;

    // Read chunks of data until EOF (read returns 0)
    while ((n = read(fd_src, file_buf, sizeof(file_buf))) > 0)
    {
        write(fd_dest, file_buf, n); // Write the exact number of bytes read
        total_bytes += n;
    }

    close(fd_src);
    close(fd_dest);

    // 4. Output success message with total bytes transferred
    char num_buf[32];
    int_to_array(total_bytes, num_buf);

    wprint("RETR ");
    wprint(args[1]);
    wprint("\nOK: ");
    wprint(num_buf);
    wprint(" bytes copied to client_root\n");
}
///////////////////////////////////////////////////////////////////////////
// STOR                                                                  //
// ===================================================================== //
// Input: char** -> Array of parsed arguments                            //
//        int    -> Number of arguments                                  //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes STOR command to receive and save a file from client //
///////////////////////////////////////////////////////////////////////////
void STOR(char **args, int arg_cnt)
{
    if (arg_cnt < 2)
    {
        wprint_err("argument is required");
        return;
    }

    // Direction reversed for STOR: Read from client, write to server
    char src_path[256] = "client_root/";
    strcat(src_path, args[1]);
    char dest_path[256] = "server_root/";
    strcat(dest_path, args[1]);

    // 1. Open the source file from the client side
    int fd_src = open(src_path, O_RDONLY);
    if (fd_src < 0)
    {
        wprint("Error: '");
        wprint(args[1]);
        wprint("' does not exist in client_root\n");
        return;
    }

    // 2. Open/Create the destination file in the server side
    int fd_dest = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd_dest < 0)
    {
        wprint_err("failed to create file in server_root");
        close(fd_src);
        return;
    }

    char file_buf[B_size];
    ssize_t n;
    int total_bytes = 0;

    // 3. Perform chunked data transfer loop
    while ((n = read(fd_src, file_buf, sizeof(file_buf))) > 0)
    {
        write(fd_dest, file_buf, n);
        total_bytes += n;
    }

    close(fd_src);
    close(fd_dest);

    // Convert the actual copied bytes (total_bytes) to a string
    char num_buf[32];
    int_to_array(total_bytes, num_buf);

    // Retrieve the original file size (Expected bytes) using stat()
    struct stat st;
    stat(src_path, &st); // Get file information using the source path
    char expected_buf[32];
    int_to_array(st.st_size, expected_buf); // Convert the original file size to a string

    wprint("STOR ");
    wprint(args[1]);
    wprint("\nOK: ");
    wprint(num_buf);
    wprint(" bytes copied to server_root (expected ");
    wprint(expected_buf);
    wprint(" bytes)\n");
}
///////////////////////////////////////////////////////////////////////////
// QUIT                                                                  //
// ===================================================================== //
// Input: void - None                                                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Executes QUIT command to terminate the server process        //
///////////////////////////////////////////////////////////////////////////
void QUIT()
{
    wprint("Quit success\n");
    exit(0);
}
///////////////////////////////////////////////////////////////////////////
// main                                                                  //
// ===================================================================== //
// Input: void - None                                                    //
//        (Input parameter Description)                                  //
// Output: int - 0 success                                               //
//         (Out parameter Description)                                   //
// Purpose: Main loop that reads data from STDIN (pipe), parses commands,//
//          and routes them to the appropriate handler functions         //
///////////////////////////////////////////////////////////////////////////
int main()
{
    char buf[B_size] = {0};
    int len = 0;

    // Infinite loop: continuously read data coming from the standard input (pipe)
    // The loop breaks when read returns 0 (EOF, meaning the pipe is closed by client)
    while ((len = read(STDIN_FILENO, buf, B_size - 1)) > 0)
    {
        buf[len] = '\0'; // Null-terminate the raw buffer read from the pipe
        char *Line_ptr;

        // Split the buffer by newline in case multiple commands arrived together
        char *line = strtok_r(buf, "\n", &Line_ptr);

        // Process each command line extracted from the buffer
        while (line != NULL)
        {
            char *args[20];
            // Parse the line into executable arguments
            int arg_cnt = parse_command(line, args);

            if (arg_cnt > 0)
            {
                char *cmd = args[0];

                // Command Dispatcher: Route to the correct handler based on the FTP command
                if (strcmp(cmd, "NLST") == 0 || strcmp(cmd, "LIST") == 0)
                {
                    NLST(args, arg_cnt);
                }
                else if (strcmp(cmd, "PWD") == 0)
                {
                    PWD();
                }
                else if (strcmp(cmd, "CWD") == 0)
                {
                    CWD(args, arg_cnt);
                }
                else if (strcmp(cmd, "CDUP") == 0)
                {
                    CDUP();
                }
                else if (strcmp(cmd, "MKD") == 0)
                {
                    Mkdir(args, arg_cnt);
                }
                else if (strcmp(cmd, "RMD") == 0)
                {
                    RMD(args, arg_cnt);
                }
                else if (strcmp(cmd, "DELE") == 0)
                {
                    DELE(args, arg_cnt);
                }
                else if (strcmp(cmd, "RNFR") == 0)
                {
                    RNFR_RNTO(args, arg_cnt);
                }
                else if (strcmp(cmd, "RETR") == 0)
                {
                    RETR(args, arg_cnt);
                }
                else if (strcmp(cmd, "STOR") == 0)
                {
                    STOR(args, arg_cnt);
                }
                else if (strcmp(cmd, "QUIT") == 0)
                {
                    QUIT();
                }
            }
            // Fetch the next command line from the buffer
            line = strtok_r(NULL, "\n", &Line_ptr);
        }
    }

    return 0;
}