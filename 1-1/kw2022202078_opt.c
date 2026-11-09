///////////////////////////////////////////////////////////////////////////////
// File Name	: kw2022202078_opt.c						//
// Date	:2026/03/23							//
// OS	: Ubuntu 20.04.6 LTS 64bits     					//
// Author : jo yeong-hoon   							//
// Student ID : 2022202078    						//
// ------------------------------------------------------------------------- //   
// Title : System Programming Assignment #1-1 ( ftp server )  		//   
// Description : A program that parses command-line arguments using		//
// 		  getopt()							//
///////////////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////
// main            								//
// ========================================================================= //
// Input: argc, argv    							//
// 	argc : argument count							//
//   	argv : array of argument strings					//
//           									//
// Output: int  - 0 success          						//
//       									//
//             								//
// Purpose: Parses input options using getopt and prints the remaining 	//
// 	     arguments.       							//
///////////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv)
{
//////////////////////////////Row insert ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    int aflag = 0, bflag = 0;	//variables to count the a and b argument
    char *cvalue = NULL;	//A variable to store the string argument that follows c
    int index, c;		//“An index variable to indicate the position of the string in the getopt function, and a variable c to store the value returned by getopt
    opterr = 0;              //Set opterr so that it does not automatically print error messages
    while ((c = getopt(argc, argv, "abc:")) != -1)	//Repeat while getopt does not return -1
    {
        switch (c)	//When checking the value of c
        {
        case 'a': 	//For input a 
            aflag++;	//Increase the count by 1
            break;	//End of switch
        case 'b': 	//For input b
            bflag++;	//Increase the count by 1
            break;	//End of switch
        case 'c': 	//For input c
            cvalue = optarg;	// Store the string argument
            break;	//End of switch
        case '?':	//Ignore all other cases	
            break;	//End of switch
        }	//End of switch
    }		//End of while
    printf("aflag = %d, bflag = %d, cvalue = %s\n", aflag, bflag, cvalue); //Print the number of times a and b were entered, and the string provided for c
    for (index = optind; index < argc; index++)	//Starting from the option index (optind) to the end of the input
    {
        printf("Non-option argument %s\n", argv[index]);	//repeat and print messages for the remaining arguments
    }
    return 0;	//End of main function
////////////////////////////////// End of row insert /////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

}
