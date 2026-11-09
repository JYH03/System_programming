///////////////////////////////////////////////////////////////////////////////
// File Name: srv.c                        					                 //
// Date	:2026/04/15							                                 //
// OS	: Ubuntu 20.04.6 LTS 64bits     					                 //
// Author : jo yeong-hoon   							                     //
// Student ID : 2022202078    						                         //
// ------------------------------------------------------------------------- //
// Title : System Programming Assignment #1-3 ( ftp server )  		         //
// Description : This program reads and displays the contents of a directory //
//		  specified by the input argument.				                     //
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
///////////////////////////////////////////////////////////////////////////////
// main            								//
// ========================================================================= //
// Input: argc, argv    							//
// 	argc : argument count							//
//   	argv : array of argument strings					//
//           									//
// Output: int  - 0 success         						//
//		- 1 on failure							//
//             								//
// Purpose : Outputs the information of the input directory. If a file name	//
// 	     or more than one directory is provided as an argument, it prints//
// 	     an error message and terminates with an error.       		//
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])

{
    //////////////////////////////Row insert ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    DIR *dp;                   // pointer to the directory stream
    struct dirent *dirp;       // pointer to the structure containing file information
    char *dir_path;            // pointer to store the target directory path
    char *exec_name = argv[0]; // store the executed program name

    if (argc == 1)
    {                   // argument == 1
        dir_path = "."; // default to current directory if no argument is provided
    }
    else if (argc == 2)
    {                       // argument == 2
        dir_path = argv[1]; // use the provided directory path
    }
    else
    {                                                                  // more than one directory path is provided
        fprintf(stderr, "only one directory path can be processed\n"); // print error message
        return 1;                                                      // return 1
    }

    char *prog_name = strrchr(exec_name, '/'); // find '/'
    if (prog_name != NULL)
    {                // there is '/'
        prog_name++; // Skip the '/' character
    }
    else
    {                          // no '/'
        prog_name = exec_name; // If '/' is not used, proceed as is.
    }

    dp = opendir(dir_path); // open directory stream
    if (dp == NULL)
    { // if opening the directory fails
        if (errno == EACCES)
        {                                                                                    // failed to open due to lack of permission (Access denied)
            fprintf(stderr, "%s: cannot access '%s': Access denied\n", prog_name, dir_path); // print error message
        }
        else if (errno == ENOENT || errno == ENOTDIR)
        {                                                                                        // ENOENT: No such file or directory, ENOTDIR: Not a directory
            fprintf(stderr, "%s: cannot access '%s': No such directory\n", prog_name, dir_path); // print error message
        }
        else
        {                                                                                          // print standard error message for other unknown errors
            fprintf(stderr, "%s: cannot access '%s': %s\n", prog_name, dir_path, strerror(errno)); // print error message
        }
        return 1; // return 1
    }

    while ((dirp = readdir(dp)) != NULL)
    {                                 // read directory contents
        printf("%s\n", dirp->d_name); // print file names
    }

    closedir(dp); // close directory stream

    return 0; // return 0
    ////////////////////////////////// End of row insert /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
}
