#include "common.h"
//for open
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h> //for read and write, close
#include <string.h> //for strerror
#include <errno.h> //for strerror(errno) in syserror
#include <dirent.h> //for opendir

//function declarations
int copysinglefile(char *intial, char *destin);
int copydirectory(char *initial, char *destin);

//helper functions
int copysinglefile(char *initial, char *destin){
	/*	copies a single file
		initial = path/filename
		destin = path/filename
	*/
	//printf("Running file copy \n");
	int orig = open(initial, O_RDONLY); //file descriptor of original
	int copied = creat(destin, O_WRONLY); //create a new file
	if (orig == -1){
		//negative upon error in opening file or creating new one
		syserror(open, initial);
		return -1;
	} else if (copied == -1){
		syserror(creat, destin);
		return -1;
	}
	char buff[4096]; //create buffer
	int readret = 1; //can't initialize to 0 because returns 0 upon EOF
	unsigned int writeret;
	while (readret != 0){ //loop until eof
		//printf("Reading old file \n");
		readret = read(orig, buff, 4096); //reads 4096 bytes of the file
		//printf("Writing new file \n");
		writeret = write(copied, buff, readret); //writes to destination 
		if (readret != writeret){
			//num bytes read != num bytes written
			syserror(write, destin);
			return -1;
		} else if (readret == -1){
			syserror(read, initial);
		} else if (writeret == -1){
			syserror(write, destin);
		}
	}
		
	//close the original and the new file
	//printf("Closing both files \n");
	int closeorig = close(orig);
	int closecp = close(copied);
	//return error if failed
	if(closeorig == -1) {
		syserror(close, initial);
		return -1;
	} else if (closecp == -1){
		syserror(close, destin);
		return -1;
	}
	
	//check that the files are the same in terminal:
	//diff firstfilename.txt secondfile.txt
	//OR
	//ls -lR /cad2/ece353s/tester > theirout.txt
	//ls -lR ./tester > myout.txt
	//vimdiff myout.txt theirout.txt
	return 0; //upon succcess
	
}

int copydirectory(char *initial, char *destin){
	/* 	copies all files in a directory 
		initial = path/dir
		destin = path (without the final destination directory)
	*/
	//open directory 
	//printf("open original dir\n");
	DIR *orig = opendir(initial);
	if (orig == NULL){
		syserror(opendir, initial);
		return -1;
	}
	//make a new one for output 
	//printf("make new one\n");
	int copied = mkdir(destin, S_IRWXU); 
	//https://www.gnu.org/software/libc/manual/html_node/Permission-Bits.html for mkdir mode info
	if (copied != 0){ //0 on success
		syserror(mkdir, destin);
		return -1;
	}
	//printf("Open new dir \n");
	DIR *cpied = opendir(destin); 
	if (cpied == NULL){
		syserror(opendir, destin);
		return -1;
	}

	//read the directory one by one
	//printf("Start reading stream \n");
	struct dirent *stream = readdir(orig);
		//stream->d_name is the filename
	char pathforstat[256];
	char unknowntype[256];
	char destinfilepath[256];
	int statout;
	//struct stat *buff = NULL; see below about bad address error
	struct stat buff;
	int prelimout = 0;
	int modout = 0;
	
	//printf("Finished creating variables \n");

	while (stream != NULL){
		//printf("in loop\n");	

		//pathforstat = stream->d_name; CANNOT DO THIS, USE STRCPY INSTEAD
		strcpy(unknowntype, stream->d_name); //unknowntype is the file/dir in the sequence
		if (strcmp(unknowntype, ".") == 0){
			//printf("case 1\n");
			stream = readdir(orig);
			continue; //seg fault on next line if it's .
		} else if (strcmp(unknowntype, "..") == 0){
			//also gives seg fault
			//printf("case 2\n");
			stream = readdir(orig);
			continue;
		}
		//printf("stream name is %s\n", stream->d_name);
		strcpy(pathforstat, initial);
		strcat(pathforstat, "/"); //pathfortat = /julia
		//printf("Adding slash onto path: %s \n", pathforstat);
		strcat(pathforstat, unknowntype); //path + the file name; pathforstat = initial/julia.txt
		printf("final pfs: %s \n", pathforstat);
		//pathforstat = path and name of source

		//printf("Check stats \n");
		//running stat to check if its a directory
		statout = stat(pathforstat, &buff); //file info to be stored in buff
			//buff->st_mode is file type and mode https://man7.org/linux/man-pages/man2/lstat.2.html
			//S_IFDIR for directory https://man7.org/linux/man-pages/man7/inode.7.html
			//Bad address error if using struct stat * buff and buff as second param
				//use & instead bc can't declare pointer to nothing then use it
			
		if (statout == -1){ 
			syserror(stat, pathforstat);
			return -1;
		}

		//destinfilepath = stream->d_name CANT DO THIS, STRCPY
		strcpy(destinfilepath, destin);
		strcat(destinfilepath, "/");	
		strcat(destinfilepath, unknowntype); //destinfilepath = destin/julia.txt

		if (S_ISDIR(buff.st_mode)){
			//printf("Found a directory \n");
		//another directory found, call this function again (recursion)
			prelimout = copydirectory(pathforstat, destinfilepath); // ./tester
			if (prelimout == -1){
				return -1;
			}
		} else if (S_ISREG(buff.st_mode)){ 
			printf("Found a file \n");
		// its a regular file not directory, run the copy
			prelimout = copysinglefile(pathforstat, destinfilepath); 
			if (prelimout == -1){
				return -1;
			}
		}
		//chmod changes the files mode bit, use to set the mode of new file?
		//https://man7.org/linux/man-pages/man2/chmod.2.html
		//printf("Changing the mode \n");
		modout = chmod(destinfilepath, buff.st_mode);
		if (modout == -1){
			syserror(chmod, destinfilepath);
			return -1;
		}
		//printf("Next stream! \n");
		//update the stream to read the next thing in current dir
		stream = readdir(orig);
	}
	//printf("Closing directories \n");
	//close directories
	copied = 0;
	copied = closedir(orig);
	if (copied == -1){
		syserror(closedir, initial);
		return -1;
	}
	copied = closedir(cpied);
	if (copied == -1){
		syserror(closedir, destin);
		return -1;
	}
	return 0;
}

void usage()
{
	fprintf(stderr, "Usage: cpr srcdir dstdir\n");
	exit(1);
}



int
main(int argc, char *argv[])
{
	//printf("let's roll\n");
	if (argc != 3) {
		usage();
	}
	//printf("first arg: %s\n", argv[1]);
	//printf("second: %s\n", argv[2]);
	
	int out;
	out = copydirectory(argv[1], argv[2]);
	if (out == -1){
		return -1;
	}

	struct stat buff;

	//use stat and chmod to check the mode and set the mode of copied
	out = stat(argv[1], &buff); //original
	if (out == -1){
		syserror(stat, argv[1]);
		return -1;
	}
	//set mode bits of the output
	out = chmod(argv[2], buff.st_mode);
	
	//printf("out is %d", out);
	
	return 0;
}
