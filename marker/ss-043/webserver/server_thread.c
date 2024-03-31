#include "request.h"
#include "server_thread.h"
#include "common.h"

#define NUMBUCKETS 13500

struct server {
	int nr_threads;
	int max_requests;
	int max_cache_size;
	int exiting;
	/* add any other parameters you need */
	pthread_t *worker_threads;
	int *buffer;
	int in; //to keep track of place to write/read
	int out;
	int buffer_full; //This is for the case where max_requests = 1
	struct hashtable *c; //cache
};

//HASH TABLE IMPLEMENTATION
struct hashtable {
	long numbuckets; 	 //total buckets in the hash table
	int max_cache_size;	 //maximum size of the cache
	int cache_size;		 //current size of the cache (evict when trying to exeed)
	pthread_mutex_t hashlock;	 	 //lock on the hash table, 0 when free 1 when locked
	int hand;			//clock hand for eviction
	struct bucket *top;
};

struct bucket { 
	int num_files; //total number of files in the bucket
	struct llnode *head; //using chaining within each bucket'
	//struct llnode *tail;
	pthread_mutex_t bucketlock;
};

struct llnode{ //linked list node
	char *file_name;
	char *file_data;
	int file_size;
	struct llnode *next;
	struct llnode *prev;
};


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

int enqueue(struct bucket *b, struct llnode *node){
	if ((b == NULL) || (node == NULL)){
		return -1;
	}
	//pthread_mutex_lock(&(b->bucketlock));
	if ((b->num_files == 0) || (b->head == NULL)){
		//first one in the bucket
		b->head = node;
		b->head->next = NULL;
		b->head->prev = NULL;
	} else {
		struct llnode *curr = b->head;
		while (curr->next != NULL){
			curr = curr->next;
		}
		curr->next = node;
		curr->next->prev = curr;
		curr->next->next = NULL;	
	}
	b->num_files += 1;
	//pthread_mutex_unlock(&(b->bucketlock));
	return 1;
}

// int enqueueold(struct bucket *b, struct llnode *node){
// 	if ((b == NULL) || (node == NULL)){
// 		return -1;
// 	}
// 	pthread_mutex_lock(&(b->bucketlock));
// 	printf("Before - files %d, h %p, t%p\n", b->num_files, b->head, b->tail);
// 	if ((b->num_files == 0)||((b->head == NULL) && (b->tail == NULL))){
// 		//when no files in bucket (either no files, or both head and tail are null)
// 		b->head = node;
// 		b->tail = b->head;
// 		b->head->prev = NULL;
// 		b->head->next = NULL;
// 	} else {
// 		b->tail->next = node;
// 		b->tail->next->prev = b->tail;
// 		b->tail = b->tail->next;
// 		b->tail->next = NULL;
// 	}

// 	b->num_files += 1;
// 	printf("After - files %d, h %p, t%p\n", b->num_files, b->head, b->tail);
// 	pthread_mutex_unlock(&(b->bucketlock));
// 	return 1;
// }

// int enqueueoldold(struct bucket *b, struct llnode *node){
// 	//allocate space for node
// 	if ((b==NULL) || (node==NULL)){
// 		return -1;
// 	}
// 	//pthread_mutex_lock(&(b->bucketlock));
// 	printf("ENQUEUE: SIZE %d, %p\n", b->num_files, b->head);
// 	if (b->head == NULL){ 
// FIRST_ENQUEUE:
// 		//first file in the linked list
// 		assert(b->num_files == 0);
// 		printf("APPEDNIG NONE: %d\n", b->num_files);
// 		b->head = node;
// 		b->tail = b->head;
// 		b->head->next = NULL;
// 		b->head->prev = NULL;
// 		b->tail->next = NULL;
// 		b->tail->prev = NULL;
// 		//set top and tail
// 	} else {
// 		//existing files in the list
// 		printf("APPEDNIG: %d\n", b->num_files);
// 		if (b->num_files == 0) goto FIRST_ENQUEUE;
// 		b->tail->next = node;
// 		b->tail->next->prev = b->tail;
// 		b->tail = b->tail->next;

// 	}
// 	b->num_files++; //increment number of files in bucket
// 	//pthread_mutex_unlock(&(b->bucketlock));
// 	return 1;
// }

struct llnode *popbyfilename(char *filename, struct bucket *b){
	if ((b == NULL) || (filename == NULL) || (b->num_files == 0) || (b->head == NULL)){
		return NULL;
	}
	
	//pthread_mutex_lock(&(b->bucketlock));
	struct llnode *curr = b->head;
	//if (!curr) {
	//	pthread_mutex_unlock(&(b->bucketlock));
	//	return NULL;
	//}

	while ((strcmp(filename, curr->file_name) != 0)){
		//havent found the file
		if (curr->next){
			curr = curr->next;
			//printf("POINTER: %p\n", curr);
		} else {
		//	pthread_mutex_unlock(&(b->bucketlock));
			return NULL;
		}
	}
	//otherwise found the file
	if (curr == b->head){
		b->head = b->head->next;
		if (b->head) b->head->prev = NULL;

	} else if ((curr->prev != NULL) && (curr->next != NULL)){
		curr->prev->next = curr->next;
		curr->next->prev = curr->prev;
	} else if (curr->prev != NULL){
		//previous exists, next doesnt
		curr->prev->next = NULL;
	} else if (curr->next != NULL){
		//next exists, previous doesn't (should be the head case)
		b->head = curr->next;
		if (b->head) b->head->prev = NULL;
	} else {
		//theyre both null
		b->head = NULL; //reset bucket
	}
	curr->prev = NULL;
	curr->next = NULL;
	b->num_files -= 1;
	//pthread_mutex_unlock(&(b->bucketlock));
	return curr;
}

// struct llnode *popbyfilenameold(char *filename, struct bucket *b){
// 	if ((b == NULL) || (filename == NULL) || (b->num_files == 0)){
// 		return NULL;
// 	}
// 	pthread_mutex_lock(&(b->bucketlock));
// 	struct llnode *curr = b->head;
// 	int inc = 0;
// 	while ((inc < b->num_files) && (curr != NULL)){
// 		//check if filename match
// 		if (strcmp(filename, curr->file_name) == 0){
// 			//file name matches
// 			if ((curr == b->head) && (curr == b->tail)){
// 				printf("1 Pointers %p, %p, this should be 1: %d\n", b->head, b->tail, b->num_files);
// 				b->head = NULL;
// 				b->tail = NULL;
// 			} else if (curr == b->head){
// 				printf("num files %d | ", b->num_files);
// 				printf("2 Pointers %p, %p, %p\n", b->head, b->head->next, b->head->prev);
// 				b->head = b->head->next;
// 				b->head->prev = NULL;
// 			} else if (curr == b->tail){
// 				printf("3 Pointers %p, %p, %p\n", b->tail, b->tail->prev, b->tail->next);
// 				b->tail = b->tail->prev;
// 				b->tail->next = NULL;
// 			} else {
// 				printf("4 Pointers %p, %p\n", curr->next, curr->prev);
// 				curr->prev->next = curr->next;
// 				curr->next->prev = curr->prev;
// 			}
			
// 			curr->next = NULL;
// 			curr->prev = NULL;
// 			b->num_files -= 1;
// 			pthread_mutex_unlock(&(b->bucketlock));
// 			return curr;
// 		}
// 		curr = curr->next;
// 		inc +=1;
// 	}
// 	pthread_mutex_unlock(&(b->bucketlock));
// 	return NULL;
// }


// struct llnode *popbyfilenameoldold(char *filename, struct bucket *b){
// 	/* function to pop the top of the queue (FIFO)*/
// 	if (b == NULL){
// 		return NULL;
// 	}
// 	//pthread_mutex_lock(&(b->bucketlock));
// 	printf("POP: %d, %p\n",b->num_files, b->head);
// 	if (b->num_files == 0) return NULL;
// 	if (b->head == NULL){
// 		//pthread_mutex_unlock(&(b->bucketlock));
// 		return NULL;
// 	}
// 	struct llnode *curr = NULL;
// 	if (b->head && strcmp(b->head->file_name, filename) == 0){
// 		printf("CHECK 1\n");
// 		curr = b->head;
// 		if (b->num_files == 1) {
// 			b->head->next = NULL;
// 			b->head->prev = NULL;
// 			b->tail = NULL;
// 			b->head = NULL;
// 		}
// 		else {
// 			b->head = b->head->next;
// 			printf("POINTER TO NEW HEAD: %p\n", b->head);
// 			if (b->head) b->head->prev = NULL;
// 			else b->num_files = 0;
// 		}
// 		if (b->num_files > 0) b->num_files--;
// 		printf("CHECK 1 DONE\n");
// 	} else if (b->tail && strcmp(b->tail->file_name, filename) == 0 ){
// 		printf("CHECK 2\n");
// 		curr = b->tail;
// 		b->tail = b->tail->prev;
// 		if (b->tail) b->tail->next = NULL;
// 		b->num_files--;
// 		printf("CHECK 2 DONE\n");
// 	} else if (b->head){
// 		printf("CHECK 3\n");
// 		curr = b->head;
// 		while (curr){
// 			if (strcmp(curr->file_name, filename) == 0 ){
// 				if (curr->prev) curr->prev->next = curr->next;
// 				if (curr->next) curr->next->prev = curr->prev;
// 				b->num_files--;
// 				printf("CHECK 3 DONE\n");
// 				break;
// 			}
// 			else curr = curr->next;
// 		}
// 	}
// 	// struct llnode *curr = b->head;
// 	// if (curr == NULL){
// 	// 	printf("SHOULD BE 0: %d\n", b->num_files);
// 	// 	//pthread_mutex_unlock(&(b->bucketlock));
// 	// 	return NULL;
// 	// }
// 	// int iter = 0;
// 	// while(iter < b->num_files && strcmp(filename, curr->file_name) != 0){
// 	// 	if (curr->next){
// 	// 		curr = curr->next;
// 	// 		iter++;
// 	// 	}else{
// 	// 		//pthread_mutex_unlock(&(b->bucketlock));
// 	// 		return NULL;
// 	// 	}
// 	// }
// 	// //curr is now the llnode we want
// 	// if (curr == b->head){
// 	// 	printf("%d, %p, %p\n", b->num_files, b->head, b->head->next);
// 	// 	b->head = b->head->next;
// 	// } else if (curr == b->tail){
// 	// 	printf("WEHAT IS IT: %d\n", b->num_files);
// 	// 	b->tail = b->tail->prev;
// 	// 	b->tail->next = NULL;
// 	// } else {
// 	// 	if (curr->prev) curr->prev->next = curr->next;
// 	// 	if (curr->next) curr->next->prev = curr->prev;
// 	// }
// 	if (curr) curr->next = NULL;
// 	if (curr) curr->prev = NULL;
// 	//b->num_files --;
// 	//pthread_mutex_unlock(&(b->bucketlock));
// 	return curr;
// }

int is_in_bucket(char *filename, struct bucket *b){
	//returns 0 if file with filename not found in bucket 
	//returns 1 if file found in bucket
	if (b == NULL){
		return -1;
	}
	//pthread_mutex_lock(&(b->bucketlock));
	struct llnode *curr = b->head;
	int iter = 0;
	while (iter < b->num_files && curr != NULL){
		if (strcmp(filename, curr->file_name)==0){
			//pthread_mutex_unlock(&(b->bucketlock));
			return 1;
		}
		if (curr->next){
			curr = curr->next;
			iter++;
		} else {
			//pthread_mutex_unlock(&(b->bucketlock));
			return 0;
		}
	}
	//pthread_mutex_unlock(&(b->bucketlock));
	return 0;
}

//INITIALIZING
pthread_mutex_t lock;
pthread_cond_t full;
pthread_cond_t empty;
struct bucket *usingrn = NULL;

struct hashtable *cache_init(int max_size){
	// initializing the cache structure
	struct hashtable *cache = (struct hashtable *)malloc(sizeof(struct hashtable));

	cache->max_cache_size = max_size;
	cache->cache_size = 0;
	cache->top = NULL;
	cache->numbuckets = NUMBUCKETS; //number of buckets = number of files 
	cache->top = malloc(NUMBUCKETS*sizeof(struct bucket)); 
	// looping through all the buckets and initializing to NULL
	cache->hand = 0;
	for(int i=0;i<(cache->numbuckets);i++){
		struct bucket *curr = &(cache->top[i]);
		curr->num_files = 0;
		curr->head = NULL;
		//curr->tail = NULL;
		//pthread_mutex_init(&(curr->bucketlock), NULL);
	}
	pthread_mutex_init(&(cache->hashlock), NULL); //initialize lock

	return cache;
}

int cache_evict(int amount, struct hashtable *cache){
	//takes in amount and evicts that much memory using clock eviction

	// initialize evicted
	//printf("BY GOD WE ARE IN EVICT\n");
	//pthread_mutex_lock(&(cache->hashlock));
	int amount_evicted = 0;
	if (amount > cache->max_cache_size) return -1;

	while (amount_evicted < amount){
		//stop if evicted >= amount
		//loop through all of the buckets
		struct bucket *cb = &(cache->top[cache->hand]);
		//int count = cb->num_files;
	//	printf("%d, %p, EVICT\n", cb->num_files, cb->head);
		if (cb == NULL){
			//pthread_mutex_unlock(&(cache->hashlock));
			return -1;
		} else if (cb->head == NULL){
			//pthread_mutex_unlock(&(cache->hashlock));
			return -1;
		}
		struct llnode *curr;
		int files = cb->num_files;
		int out = 0;
		curr = popbyfilename(cb->head->file_name, cb); //curr is the head
		for (int i = 0; i < files; i++){
			out = is_in_bucket(curr->file_name, usingrn);
			if (out == 1){
				//if filename found in usingrn (file in use)
				enqueue(cb, curr);
			} else{
				//otherwise keep it popped
				amount_evicted += curr->file_size;
				free(curr->file_data);
				free(curr->file_name);
				free(curr);
				if (amount_evicted >= amount){
					//evicted the amount we needed
					cache->cache_size -= amount_evicted;
					//printf("SIZE %d\n", cache->cache_size);
					//pthread_mutex_unlock(&(cache->hashlock));
					return 1;
				}
			}
			if (i < files-1){
				curr = popbyfilename(cb->head->file_name, cb); //pop the head
			} else {
				break;
			}
		}

		//printf("EVICT: %d, %d\n", amount_evicted, amount);
		/*for (int i=0; i< count; i++){
			//printf("EVICT IN: %d, %d\n", amount_evicted, amount);
			if (cb->num_files == 0 || cb->head == NULL) {
				//cb->num_files = 0;
				break;
			}
			
			printf("EVICT: %p\n", cb->head);
			struct llnode *n = popbyfilename(cb->head->file_name, cb);
			//printf("EVICT: %p\n", n);
			int out = is_in_bucket(n->file_name, usingrn);
			if (out == 1){
				//found in usingrn, enqueue node back to bucket
				enqueue(cb, n);
				continue;
			} 
			//otherwise, evict
			cache->cache_size -= n->file_size;
			amount_evicted += n->file_size;
			//printf("EVICT out: %d, %d, %d\n", amount_evicted, amount, cache->cache_size);
			free(n);

			if (amount_evicted >= amount){
				//enough space was made
				break;
			}
		}*/
		cache->hand = (cache->hand+1) % NUMBUCKETS;
	}
	cache->cache_size -= amount_evicted;
	//printf("%d\n", cache->cache_size);
	//pthread_mutex_unlock(&(cache->hashlock));
	return -1;
}


int cache_insert(struct file_data *f, struct hashtable *cache){
	//takes file_data
	// returns -1 on failure
	// returns 0 on success
	// returns size needed if cache is too small
	if ((f == NULL) || (cache == NULL)){
		return -1;
	}
	pthread_mutex_lock(&(cache->hashlock));
	//printf("HELP\n");

	if (f->file_size > cache->max_cache_size){
		//check if file size larger than max_cache_size
		pthread_mutex_unlock(&(cache->hashlock));
		return -1;
	}

	//create llnode instance0
	struct llnode *node = (struct llnode*)malloc(sizeof(struct llnode));
	node->file_name = (char *)malloc(sizeof(char)*100);
	node->file_data = (char *)malloc(sizeof(char)*200000);
		//strcpy file info to the llnode
	strcpy(node->file_name, f->file_name);
	strcpy(node->file_data, f->file_buf);
	node->file_size = f->file_size;

	//checks if cache has enough space (max_cache_size)
	//diff is the amount of space left in the cache

	if (node->file_size + cache->cache_size > cache->max_cache_size){
		// if the file is larger than space in the cache
		//printf("I SHOULD BE HERE\n");
		//pthread_mutex_unlock(&(cache->hashlock));
		int retval = cache_evict(node->file_size + cache->cache_size - cache->max_cache_size, cache);
			//pass in the amount ofc space needed to add 
		if (retval == -1){
			pthread_mutex_unlock(&(cache->hashlock));
			return -1;
		}
		//return node->file_size + cache->cache_size - cache->max_cache_size;
		//diff = cache->max_cache_size - cache->cache_size;
//		return node->file_size - diff;
	}
	
	//hash function on filename
	long bucketnum = hashfn(node->file_name, cache->numbuckets);

	//insert into bucket linked list
	struct bucket *bucket = &(cache->top[bucketnum]);
	enqueue(bucket, node); //add the node to the bucket

	//update cache_size
	cache->cache_size += node->file_size;
	//printf("cache %d, %d\n", cache->cache_size, cache->max_cache_size);
	pthread_mutex_unlock(&(cache->hashlock));
	return 0;
}

struct llnode *cache_lookup(char *filename, struct hashtable *cache){
	//takes file name 
	pthread_mutex_lock(&(cache->hashlock));
	// hash function to find the bucket it should be
	long bucketnum = hashfn(filename, cache->numbuckets);

	//loop through linked list to figure out if file there
	struct bucket *b = &(cache->top[bucketnum]);
	if (b == NULL){
		pthread_mutex_unlock(&(cache->hashlock));
		return NULL;
	}
	//if bucket initialized, linked list in it
	struct llnode *curr = b->head;
//	printf(" HELLO %d, %p\n", b->num_files, b->head);
	int iter = 0;
	while((iter < b->num_files) && (curr != NULL)){
		//if name matches, return the node
		//printf("%s\n\n", filename);
		if (!curr) break;
		if (strcmp(filename, curr->file_name)==0){

			pthread_mutex_unlock(&(cache->hashlock));
			return curr;
		}
		//printf("CHECK FOR THE LOVE OF GOD: %p, %p\n", curr, curr->next);
		curr = curr->next;
		iter++;
	}
	//else returns null
	pthread_mutex_unlock(&(cache->hashlock));
	return NULL;
}

/* initialize file data */
static struct file_data *
file_data_init(void)
{
	struct file_data *data;

	data = Malloc(sizeof(struct file_data));
	data->file_name = NULL;
	data->file_buf = NULL;
	data->file_size = 0;
	return data;
}

/* free all file data */
static void
file_data_free(struct file_data *data)
{
	free(data->file_name);
	free(data->file_buf);
	free(data);
}

static void
do_server_request(struct server *sv, int connfd)
{
	int ret;
	struct request *rq;
	struct file_data *data;

	data = file_data_init();
	/* fill data->file_name with name of the file being requested */
	rq = request_init(connfd, data);
	if (!rq) {
		file_data_free(data);
		return;
	}
	/* read file, 
	 * fills data->file_buf with the file contents,
	 * data->file_size with file size. */
	//printf("-------CHECKPOINT 0-------\n");
	if (sv->max_cache_size == 0){
		//only when no cache
		ret = request_readfile(rq);
		if (ret == 0) { /* couldn't read file */
			goto out;
		}
		/* send file to client */
		request_sendfile(rq);
		//printf("-------CHECKPOINT 1-------\n");
	} else if (sv->max_cache_size > 0){
		//we have a cache
		//lock the hash table ALREADY DONE IN CACHE FUNCTIONS
		//cache lookup to see if file in cache-> hit or miss
		struct llnode *node;
		node = cache_lookup(data->file_name, sv->c);
		//if miss:
		if (node == NULL){
			//cache insert (which will evict if space needed)
			ret = request_readfile(rq); //TODO: check lock here
			if (ret == 0) { /* couldn't read file */
				goto out;
			}
			/* send file to client */
			request_sendfile(rq);
				
			struct llnode *idk = cache_lookup(data->file_name, sv->c);
			if (idk) goto out;
			while (1){

				cache_insert(data, sv->c);
				goto out;
//				if (check_val > 0) cache_evict(check_val, sv->c);
//				else goto out;
			}
			//printf("-------CHECKPOINT 5-------\n");
		} else {
		//if hit:
			//send file to be in use in usingrn
			pthread_mutex_lock(&(sv->c->hashlock));
			enqueue(usingrn, node);
			pthread_mutex_unlock(&(sv->c->hashlock));
			
			data->file_buf = (char *)malloc(sizeof(char)*300000);

			//get the file data from llnode returned
			strcpy(data->file_buf, node->file_data);
			data->file_size = node->file_size;

			request_set_data(rq, data);
			request_sendfile(rq);
			//file_data_free(rq_data);

			// remove file from usingrn
			pthread_mutex_lock(&(sv->c->hashlock));
			//printf("-------CHECKPOINT 8-------\n");
			popbyfilename(data->file_name, usingrn);
			//printf("-------CHECKPOINT 9-------\n");
			pthread_mutex_unlock(&(sv->c->hashlock));
			//printf("-------CHECKPOINT 10-------\n");
		}		
	}
	
out:
	request_destroy(rq);
	file_data_free(data);
}

/* entry point functions */
void thread_start_routine(struct server *sv){
	volatile int http_req;
	while(!sv->exiting){
		pthread_mutex_lock(&lock); //try to acquire lock
		if (sv->max_requests > 1){

			while (sv->in == sv->out && !sv->exiting){
				pthread_cond_wait(&empty, &lock);
			}
			if (sv->exiting){
				pthread_mutex_unlock(&lock);
				break;
			}
			http_req = sv->buffer[sv->out]; //consume req
			sv->out = (sv->out + 1) % sv->max_requests; //update out
		}
		else if (sv->max_requests == 1){
			while (sv->buffer_full == 0 && !sv->exiting){
				pthread_cond_wait(&empty, &lock);
			}
			if (sv->exiting){
				pthread_mutex_unlock(&lock);
				break;
			}
			http_req = sv->buffer[0];
			sv->buffer_full = 0;
		}
		pthread_cond_signal(&full); //signal
		pthread_mutex_unlock(&lock); //try to acquire lock
		do_server_request(sv, http_req);
	}
}

struct server *
server_init(int nr_threads, int max_requests, int max_cache_size)
{
	struct server *sv;

	sv = Malloc(sizeof(struct server));
	sv->nr_threads = nr_threads;
	sv->max_requests = max_requests;
	sv->max_cache_size = max_cache_size;
	sv->exiting = 0;

	/* Lab 4: create queue of max_request size when max_requests > 0 */
	/* Lab 4: create worker threads when nr_threads > 0 */

	if (nr_threads > 0 || max_requests > 0 || max_cache_size > 0) {
		if (max_requests > 0){
			sv->buffer = (int *)malloc(sizeof(int)*sv->max_requests); //create buffer
			if (max_requests > 1) {
				sv->in = sv->out = 0;
			}
			else if (max_requests == 1){
				sv->buffer_full = 0;
			}
			pthread_mutex_init(&lock, NULL); //initialize lock
			pthread_cond_init(&full, NULL); //init CVs
			pthread_cond_init(&empty, NULL);
								
		}
		
		if (nr_threads > 0){
			sv->worker_threads = (pthread_t *)malloc(sizeof(pthread_t)*sv->nr_threads); //init worker threads
			pthread_mutex_lock(&lock); //locking
			for (int i=0;i<sv->nr_threads;i++){
				//create threads
				pthread_create(&(sv->worker_threads[i]), NULL, (void *) thread_start_routine, (void *) sv);
			}

			pthread_mutex_unlock(&lock); //unlocking

		}
		
		/* Lab 5: init server cache and limit its size to max_cache_size */
		if (max_cache_size >= 0){
			sv->c = cache_init(max_cache_size);
			
			//TODO: check if right place for usingrn to be initialized
			usingrn = (struct bucket *)malloc(sizeof(struct bucket)); //for the files currently in use
			usingrn->num_files = 0;
			usingrn->head = NULL;
			//usingrn->tail = NULL;

		} else {
			sv->c = NULL;
			sv->max_cache_size = 0;
		}
	}

	return sv;
}

void
server_request(struct server *sv, int connfd)
{
	if (sv->nr_threads == 0) { /* no worker threads */
		do_server_request(sv, connfd);
	} else {
		/*  Save the relevant info in a buffer and have one of the
		 *  worker threads do the work. */
		pthread_mutex_lock(&lock); //lock her
		if (sv->max_requests > 1){

			while ((sv->in - sv->out + sv->max_requests) % sv->max_requests == sv->max_requests - 1){
				pthread_cond_wait(&full, &lock);
			}
			sv->buffer[sv->in] = connfd;
			sv->in = (sv->in + 1) % sv->max_requests;
		}
		else if (sv->max_requests == 1) {
			while (sv->buffer_full == 1){
				pthread_cond_wait(&full, &lock);
			}
			sv->buffer[0] = connfd;
				sv->buffer_full = 1;
		}
		pthread_cond_signal(&empty);
		pthread_mutex_unlock(&lock);
	}
}

void
server_exit(struct server *sv)
{
	/* when using one or more worker threads, use sv->exiting to indicate to
	 * these threads that the server is exiting. make sure to call
	 * pthread_join in this function so that the main server thread waits
	 * for all the worker threads to exit before exiting. */
	sv->exiting = 1;
	if (sv->nr_threads > 0){
		for (int i = 0; i<sv->nr_threads;i++){
			pthread_cond_broadcast(&empty); //wake em all up
			pthread_join(sv->worker_threads[i], NULL); //wait until exited
		}
		free(sv->worker_threads); //free array
		if (sv->max_requests > 0){
			free(sv->buffer);
		}
	}
	/* make sure to free any allocated resources */
	//free usingrn queue
	// struct llnode *curr = usingrn->head;
	// for (int i=0; i< usingrn->num_files; i++){
	// 	//freeing all the nodes
	// 	if (curr){
	// 		struct llnode *temp;
	// 		free(curr->file_data);
	// 		free(curr->file_name);
	// 		temp = curr;
	// 		curr = curr->next;
	// 		free(temp);
	// 	}	
	// }
	free(usingrn); //freeing the bucket itself

	//TODO: free cache stuff 
	// for (int i=0; i< sv->c->numbuckets; i++){
	//  	//nodes, buckets, hash table, usingrn list
	//  	//i index of bucket
	//  	struct bucket *b = &(sv->c->top[i]);
	// 	// struct llnode *curr = b->head;
	// 	// struct llnode *temp;
	//  	// while (curr != NULL){
	// 	// 	//free file stuff
	// 	// 	free(curr->file_name);
	// 	// 	free(curr->file_data);
	// 	// 	temp = curr;
	// 	// 	curr = curr->next;
	// 	// 	//free node
	// 	// 	free(temp);
	// 	// }
	// 	//free the bucket itself
	// 	free(b);
	// }
	free(sv->c->top);
	free(sv->c);
	free(sv);

}
