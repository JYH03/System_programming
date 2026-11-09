///////////////////////////////////////////////////////////////////////////////
// File Name: cli.c                                                          //
// Date :2026/04/15                                                          //
// OS   : Ubuntu 20.04.6 LTS 64bits                                          //
// Author : jo yeong-hoon                                                    //
// Student ID : 2022202078                                                   //
// ------------------------------------------------------------------------- //
// Title : This program receives user input, parses it, and translates it    //
// into standard FTP commands before sending them to the server.             //
// It supports the following commands: ls, dir, pwd, cd, Mkdir, delete,      //
// rename, quit, put, and get. It also includes exception handling to        //
// process invalid options or arguments that do not match each command.      //
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

extern int optind;
extern int opterr;

///////////////////////////////////////////////////////////////////////////
// print                                                                 //
// ===================================================================== //
// Input: const char* -> Message string to be printed                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Prints a given message to the standard output (STDOUT)       //
///////////////////////////////////////////////////////////////////////////
void print(const char *msg)
{
    write(STDOUT_FILENO, msg, strlen(msg));
}

///////////////////////////////////////////////////////////////////////////
// printerr                                                              //
// ===================================================================== //
// Input: char* -> Error message string to be printed                    //
//        (Input parameter Description)                                  //
// Output: void - None                                                   //
//         (Out parameter Description)                                   //
// Purpose: Prints an error message to the standard error (STDERR)       //
///////////////////////////////////////////////////////////////////////////
void printerr(char *errmsg)
{
    write(STDERR_FILENO, errmsg, strlen(errmsg));
}

///////////////////////////////////////////////////////////////////////////
// main                                                                  //
// ===================================================================== //
// Input: int    -> Number of command-line arguments (argc)              //
//        char** -> Array of command-line argument strings (argv)        //
//        (Input parameter Description)                                  //
// Output: int -  0 success                                              //
//               -1 fail (invalid option or argument error)              //
//         (Out parameter Description)                                   //
// Purpose: Parses user input, handles exceptions, translates to FTP     //
//          commands, and transmits them to the server                   //
///////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    opterr = 0;               // Disable default getopt error messages
    char FTP_cmd[4096] = {0}; // Buffer to store the translated command to transfer to server

    // Check if the user entered at least a command
    if (argc < 2)
    {
        printerr("Error: argument is required\n");
        return -1;
    }

    char *command = argv[1];
    int c;

    // ---------------------------------------------------------
    // Command: ls -> translates to NLST
    // ---------------------------------------------------------
    if (strcmp(command, "ls") == 0)
    {
        int a = 0, l = 0, e = 0;

        // Parse options '-a' and '-l' using getopt
        while ((c = getopt(argc - 1, argv + 1, "al")) != -1)
        {
            switch (c)
            {
            case 'a':
                a++;
                break;
            case 'l':
                l++;
                break;
            case '?': // getopt returns '?' for unrecognized options
                e++;
                break;
            default:
                break;
            }
        }

        // Loop through remaining arguments to catch standalone hyphens ("-")
        for (int i = optind; i < argc; i++)
        {
            if (strcmp(argv[i], "-") == 0)
            {
                printerr("Error: invalid option\n");
                return -1;
            }
        }

        // If an invalid option was caught by getopt
        if (e)
        {
            printerr("Error: invalid option\n");
            return -1;
        }

        // Build the FTP command string based on parsed flags
        strcpy(FTP_cmd, "NLST");
        if (a && l)
        {
            strcat(FTP_cmd, " -al"); // option al
        }
        else if (a)
        {
            strcat(FTP_cmd, " -a"); // option a
        }
        else if (l)
        {
            strcat(FTP_cmd, " -l"); // option l
        }

        // Append any remaining arguments (e.g., target path) to the command
        for (int i = optind + 1; i < argc; i++)
        {
            strcat(FTP_cmd, " ");
            strcat(FTP_cmd, argv[i]);
        }
        strcat(FTP_cmd, "\n");
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: dir -> translates to LIST
    // ---------------------------------------------------------
    else if (strcmp(command, "dir") == 0)
    {
        // 'dir' does not take any options, so any option flag is an error
        while ((c = getopt(argc - 1, argv + 1, "")) != -1)
        {
            if (c == '?')
            {
                printerr("Error: invalid option\n");
                return -1;
            }
        }

        strcpy(FTP_cmd, "LIST");

        // Append paths if provided
        for (int i = optind + 1; i < argc; i++)
        {
            strcat(FTP_cmd, " ");
            strcat(FTP_cmd, argv[i]);
        }
        strcat(FTP_cmd, "\n");
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: pwd -> translates to PWD
    // ---------------------------------------------------------
    else if (strcmp(command, "pwd") == 0)
    {
        // 'pwd' takes absolutely no arguments or options
        if (argc > 2)
        {
            if (argv[2][0] == '-')
                printerr("Error: invalid option\n");
            else
                printerr("Error: argument is not required\n");
            return -1;
        }

        print("PWD\n");
    }
    // ---------------------------------------------------------
    // Command: cd -> translates to CWD or CDUP
    // ---------------------------------------------------------
    else if (strcmp(command, "cd") == 0)
    {
        // Require exactly one target directory
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }

        // Prevent options passed to 'cd'
        if (argv[2][0] == '-')
        {
            printerr("Error: invalid option\n");
            return -1;
        }

        // Translate "cd .." specifically to FTP command CDUP
        if (strcmp(argv[2], "..") == 0)
        {
            strcpy(FTP_cmd, "CDUP\n");
        }
        // Otherwise, translate to standard CWD
        else
        {
            strcpy(FTP_cmd, "CWD ");
            strcat(FTP_cmd, argv[2]);
            strcat(FTP_cmd, "\n");
        }
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: mkdir -> translates to MKD
    // ---------------------------------------------------------
    else if (strcmp(command, "mkdir") == 0)
    {
        // Require at least one directory name
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }

        // Loop through each argument to create multiple directories if requested
        for (int i = 2; i < argc; i++)
        {
            // Block options disguised as arguments
            if (argv[i][0] == '-')
            {
                printerr("Error: invalid option\n");
                return -1; // Abort entirely if any invalid option is found
            }
            strcpy(FTP_cmd, "MKD ");
            strcat(FTP_cmd, argv[i]);
            strcat(FTP_cmd, "\n");
            print(FTP_cmd);
        }
    }
    // ---------------------------------------------------------
    // Command: delete -> translates to DELE
    // ---------------------------------------------------------
    else if (strcmp(command, "delete") == 0)
    {
        // Require at least one file name
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }

        // Process each file sequentially
        for (int i = 2; i < argc; i++)
        {
            if (argv[i][0] == '-')
            {
                printerr("Error: invalid option\n");
                return -1;
            }
            strcpy(FTP_cmd, "DELE ");
            strcat(FTP_cmd, argv[i]);
            strcat(FTP_cmd, "\n");
            print(FTP_cmd);
        }
    }
    // ---------------------------------------------------------
    // Command: rmdir -> translates to RMD
    // ---------------------------------------------------------
    else if (strcmp(command, "rmdir") == 0)
    {
        // Require at least one directory name
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }

        // Process each directory sequentially
        for (int i = 2; i < argc; i++)
        {
            if (argv[i][0] == '-')
            {
                printerr("Error: invalid option\n");
                return -1;
            }
            strcpy(FTP_cmd, "RMD ");
            strcat(FTP_cmd, argv[i]);
            strcat(FTP_cmd, "\n");
            print(FTP_cmd);
        }
    }
    // ---------------------------------------------------------
    // Command: rename -> translates to RNFR & RNTO
    // ---------------------------------------------------------
    else if (strcmp(command, "rename") == 0)
    {
        // 'rename' strictly requires a source and a destination (argc must be 4)
        if (argc != 4)
        {
            if (argc > 2 && argv[2][0] == '-')
                printerr("Error: invalid option\n");
            else
                printerr("Error: two argument are required\n");
            return -1;
        }

        // Check both arguments to ensure they aren't options
        if (argv[2][0] == '-' || argv[3][0] == '-')
        {
            printerr("Error: invalid option\n");
            return -1;
        }

        // Combine RNFR (Rename From) and RNTO (Rename To) into one transaction
        strcpy(FTP_cmd, "RNFR ");
        strcat(FTP_cmd, argv[2]);
        strcat(FTP_cmd, " RNTO ");
        strcat(FTP_cmd, argv[3]);
        strcat(FTP_cmd, "\n");
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: get -> translates to RETR (Retrieve)
    // ---------------------------------------------------------
    else if (strcmp(command, "get") == 0)
    {
        // Strictly require exactly one file argument
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }
        else if (argc > 3)
        {
            printerr("Error: only one argument is valid\n");
            return -1;
        }

        if (argv[2][0] == '-')
        {
            printerr("Error: invalid option\n");
            return -1;
        }

        strcpy(FTP_cmd, "RETR ");
        strcat(FTP_cmd, argv[2]);
        strcat(FTP_cmd, "\n");
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: put -> translates to STOR (Store)
    // ---------------------------------------------------------
    else if (strcmp(command, "put") == 0)
    {
        // Strictly require exactly one file argument
        if (argc < 3)
        {
            printerr("Error: argument is required\n");
            return -1;
        }
        else if (argc > 3)
        {
            printerr("Error: only one argument is valid\n");
            return -1;
        }

        if (argv[2][0] == '-')
        {
            printerr("Error: invalid option\n");
            return -1;
        }

        strcpy(FTP_cmd, "STOR ");
        strcat(FTP_cmd, argv[2]);
        strcat(FTP_cmd, "\n");
        print(FTP_cmd);
    }
    // ---------------------------------------------------------
    // Command: quit -> exits the client gracefully
    // ---------------------------------------------------------
    else if (strcmp(command, "quit") == 0)
    {
        // 'quit' should not have any trailing arguments
        if (argc > 2)
        {
            if (argv[2][0] == '-')
                printerr("Error: invalid option\n");
            else
                printerr("Error: argument is not required\n");
            return -1;
        }
        print("QUIT\n");
    }
    // Catch-all for unrecognized commands
    else
    {
        printerr("Invalid command");
    }

    return 0;
}