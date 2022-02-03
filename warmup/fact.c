#include "common.h"
#include <string.h>
#include <ctype.h>

//program that takes an input<12 and returns its factorial

int factorial(int num);

int factorial(int num){
	//recursive factorial function
	if (num == 1){
		return 1;	

	} else {
		return num*factorial(num-1);
	}
}

int
main(int argc, char **argv)
{
	int total = 1;
	char *num = argv[1]; //input string
	int n = strlen(num); //n = length of the input string

	for (int i=0;i<n;i++){
		if( !isdigit(num[i])){
			//if there is a character that isnt a digit
			printf("Huh?\n");
			return 0;
		}
	}
	
	int numm = atoi(num); //convert into integer
	if (numm == 0){
		//0 case
		//the negative case is already covered by isdigit since neg sign will return false
		printf("Huh?\n");
		return 0;
	}else if (numm > 12){
		//overflow factorial over 12
		printf("Overflow\n");
		return 0;
	}
	total = factorial(numm); //call factorial function
	printf("%d\n", total);
	return 0;
}
