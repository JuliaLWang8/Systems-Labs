#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "common.h"
#include "wc.h"
#include <string.h>
#include <ctype.h>

// some function declarations for helper functions
struct bucket; //each bucket of the table
char *wordfromstr(char *array, long size, int *wordnum); //gets word from array
struct bucket *checksame(struct bucket **top, int index, char* word); //check if word is in table
long hashfn(char *word, long numbuckets); //hash function djb2 

//STRUCT DEFINITIONS
struct wc {
	long numbuckets; //total amount of buckets, should be static at 2*(words)
	struct bucket **top; //first bucket pointer
};

struct bucket {
	int count; //counts number of occurances of word
	char *word; //the word itself
};

//HELPER FUNCTIONS
//
char *wordfromstr(char *array, long size, int *wordnum){
	//outputs word at wordnum index in the hash table
	//updates wordnum to the start of the next word
	
	//printf("check word from str\n");

	if ((array == NULL)){
		return NULL;
	}
	int wordlen = 0; //length of word
	
	for (int i=*wordnum;i<size;i++){
		//printf("array[i] is %c, ", array[i]);
		//printf("looking until space found\n");
		// if we reach the end of the array or hit a space
		//printf("space is %d\n", isspace(array[i]));
		if (isspace(array[i])!=0){
		//	printf("end of array or space\n");
			break;
		} else if (!(array[i])){
		//	printf("array dne\n");
		 	break;
		}else{
			wordlen+=1; //otherwise still part of word
		}
	}
	int i= *wordnum;

	//allocate new space for pointer
	//len+1 so we can have a null terminator, therefore use calloc not malloc
	char *word = (char*)calloc((wordlen+1),sizeof(char));
	
	//put word into the pointer char by char
	for (int j=0;j<wordlen;j++){
		word[j]=array[i+j];
	}
	*wordnum = i+wordlen+1; //set index of next word to check

	if (wordlen == 0){
		free(word);
		return wordfromstr(array, size, wordnum);
	}
	return word;
}

struct bucket *checksame(struct bucket **top, int index, char *word){
	//checks if the words are the same starting at bucket start
	//returns the bucket the word goes to if it matches
	//otherwise returns null
	//printf("checksame\n");
	struct bucket *curr = top[index]; 
	int i=index;
	//LINEAR PROBING
	//look for a bucket until we find a matching word or an empty NULL bucket
	while (curr!= NULL){
		if (strcmp(curr->word, word) == 0){ 
			// case where the current word is the same 
			return curr;
		}
		//if not return, go to the next bucket
		i+=1;
		curr= top[i];
	}
	return NULL;
}

long hashfn(char *word, long numbuckets){
	//maps word to bucket
	//uses djb2 hashing for strings
	//printf("hashfn\n");
	long hash = 5381; //initial hash
	int c;
	while ((c = *word++)){
		hash = ((hash <<5) + hash) + c; //djb2
	}
	if (hash < 0){
		hash *= -1; //multiply if negative to make pos
	}
	//hash is the final bucket
	//guarantee that its within the indices of buckets
	return hash%numbuckets;
}

//MAIN FUNCTIONS
struct wc *wc_init(char *word_array, long size)
{
	/*
	 	create hash table from array of characters
		counting occurances of words
	 */
	//printf("Lets get started\n");
	struct wc *wc;

	wc = (struct wc *)malloc(sizeof(struct wc));
	assert(wc);
	
	wc->numbuckets = 2*size/5; //set the num buckets
		//since size is # characters: approximate avg word length of 5
		//we want 2x the number of total words, so 2/5*size buckets
	//allocate space for all buckets
	wc->top = (struct bucket**)malloc(wc->numbuckets*sizeof(struct bucket*));

	//initialize all buckets to be NULL 
	for(int i=0;i<(wc->numbuckets);i++){
		wc->top[i] = NULL;
	}
	int i=0;
	//printf("finished initializing");
	while(i<size){
		//gets a word from the array
		char* word = wordfromstr(word_array,size,&i); //wordfromstr updates i
		//printf("word: %s\n", word);
		//int charcount = strlen(word);
		long ind = hashfn(word, wc->numbuckets);
		struct bucket *newbucket = NULL;
		struct bucket *currbucket = NULL;
		//input into hash table
		//
		//first check the first available spot the function hashes to (index ind)
		if (((wc->top)[ind]) == NULL){
			//first bucket empty
			//printf("first spot empty\n");
			newbucket = (struct bucket*)malloc(sizeof(struct bucket)); //TODO check this
			(wc->top)[ind] = newbucket; //assigning the pointer at that spot to the created bucket
			
			//newbucket->word = (char*)calloc(charcount+1, sizeof(char)); //+1 for null terminator
				//don't need this since already allocated space for the word in wordfromstr
			newbucket->word = word; 
			newbucket->count = 1; //new word so 1 encounter
			/*
			if ((wc->top)[ind+1]){ //set to be the next bucket
				newbucket->next = (wc->top)[ind+1];
			}else{
				newbucket->next=NULL;
			}*/
		} else {
			//not empty case
			//check if the same word -> increase the counter
			//if different word, linear probe to the next available bucket
		//	printf("first bucket not empty\n");
			if (checksame(wc->top,ind, word)==NULL){
				//if checksame is null, the word at the bucket is different :( 
				currbucket = (wc->top)[ind];
				long temp = ind;
				int breakout = 0;
				//find next available bucket space
				while (currbucket){
					if (checksame(wc->top, temp, word)==NULL){
						//still not the same word but still a word
						//keep probing for the next available
					temp+=1;
					currbucket=(wc->top)[temp];
				
					} else {
						//if we actually find a match
						currbucket->count+=1;
						free(word);
						breakout = 1; //to go to the next word
						break; //don't need to find next empty null ptr
					}
				}
				if (breakout == 1){
					//if we found the word, we continue to the next word
					continue;
				}
				//create a new bucket and put the word in
				newbucket = (struct bucket*)malloc(sizeof(struct bucket));
				(wc->top)[temp] = newbucket;

				//newbucket->word = (char*)calloc(charcount+1, sizeof(char)); //for null terminator
				newbucket->word = word;
				newbucket->count=1;
				/*if ((wc->top)[temp]){
					newbucket->next = (wc->top)[temp]; //set to the next bucket in wc
				} else {
					newbucket->next = NULL;
				}*/
			} else {
				//word at the bucket is the same
				currbucket = checksame((wc->top), ind, word);
				free(word);
				currbucket->count+=1; //when we found the word in the table, only have to update count
			}
		}
	}
	return wc;
}

void wc_output(struct wc *wc){
	/*
		prints words and their counts
	 */
	//printf("output is:\n");
	long i=0;
	long numbuck = wc->numbuckets; //setting number of buckets
	struct bucket *curr = NULL;
	struct bucket **top = wc->top;
	while (i < numbuck) {
		curr = top[i];
		if (curr != NULL){
			//print
			if (curr->word == NULL){ //word dne
				//printf("word dne");
				continue;
			} else if (strlen(curr->word) == 0) {
				
				continue;
			}
			printf("%s", curr->word);
			printf(":%d\n", curr->count);
		}
		i+=1;
	}
}

void wc_destroy(struct wc *wc)
{
	/*
	 	frees all memory
	 */
	long i=0;
	long numbuck = wc->numbuckets;
	struct bucket* curr = NULL;
	struct bucket **top = wc->top;
	while (i<numbuck){
		curr = top[i];
		//iterate through all buckets to free them
		if (curr){
			free(curr->word);
			free(curr);
		}
		i++;
	}
	free(wc->top);
	free(wc);
}
