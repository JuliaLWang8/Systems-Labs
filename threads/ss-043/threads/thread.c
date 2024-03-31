#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

//DEFINITIONS
//thread states and id states:
enum{ 
	READY = 0,
	BLOCKED = 1,
	RUNNING = 2,
	EXITED = 3,
	DEAD = 4,
	ID_USED = 0,
	ID_FREE = 1
};


/* This is the wait queue structure */
struct wait_queue {
	/* ... Fill this in Lab 3 ... */
};

/* This is the thread control block */
struct thread {
	Tid id; //thread id
	int state; //for thread states defined above
	void *stack; //each thread has individual stack, stack pointer
	ucontext_t context; //context 
};

// QUEUE STRUCTURE AND HELPER FUNCTIONS
// for the queue structures: ready queue, exited queue
struct que {
	struct node *top; //points to the top node of the queue
	struct node *tail; //points to tail of the queue
	int num_threads; //total amount of threads in the queue
};

struct node{
	struct thread *thread; //thread in the queue
	struct node *next; //next thread in the sequence
	struct node *prev;
};

struct thread *pop(struct que* q){
	/* function to pop the top of the queue (FIFO)
	 */
	if (q == NULL){
		return NULL;
	}
	struct thread *thread = q->top->thread;
	struct node *temp = q->top;

	q->top = q->top->next;
	free(temp); //free the queue node
	q->num_threads -=1; //decrease number of threads
	return thread;
}

struct thread *popbyid(struct que *q, Tid w_id){
	if (q==NULL){
		return NULL;
	}
	struct node *curr;
	curr = q->top;
	while(curr->thread->id != w_id){
		//loop until the thread id matches the wanted
		if (curr->next){
			curr = curr->next;
		}else{
			//looped through all of them with no avail
			//wanted id isn't in the queue
			return NULL;
		}
	}
	//curr is now the node* we want
	struct thread *final = curr->thread; //final is the thread to be popped
	struct node *temp;
	
	if (curr == q->top && curr== q->tail){
		q->top = NULL;
		q->tail = NULL;
		
	}else if (curr == q->top){
		q->top = q->top->next;
		q->top->prev = NULL;

	}else if (curr == q->tail){
		temp = curr->prev;
		q->tail = temp; //update the tail
		temp->next= NULL; //next node DNE anymore
	} else {
		temp = curr->prev; // the previous node
		temp->next = curr->next; //next of ^ is current next

		temp = curr->next; //the next node
		temp->prev = curr->prev; //prev of next = current previous
	}
	free(curr); //free the node, keep the thread
	q->num_threads -=1;
	return final;
}

int enqueue(struct que *q, struct thread *curr){
	//allocate space for node
	if ((q==NULL) || (curr==NULL)){
		return -1;
	}
	struct node *curr_node = (struct node*)malloc(sizeof(struct node));
	curr_node->thread = curr; //set thread
	curr_node->next = NULL; //next node in the queue is NULL
	curr_node->prev= NULL;
	if ((q->num_threads == 0) || (q->top == NULL && q->tail==NULL)){ 
		//first thread in the queue
		q->top = curr_node;
		q->tail = curr_node;
		q->num_threads = 1;
		//set top and tail
	} else {
		
		//existing threads on the queue
		(q->tail)->next = curr_node;
		curr_node->prev = q->tail;
		q->tail = curr_node; //update tail
		q->num_threads +=1; //increment number of threads

	}
	return 1;
}

// for thread ID array to keep track of which id's are available
// uopn thread creation
struct id_node{
	Tid id; //threads id
	int status; //status = ID_FREE or ID_USED
	//struct id_node* next;
};


//INITIALIZING
struct que *readyq = NULL; //queue with all ready threads
struct que *exitedq = NULL; //queue with all exited threads
struct que *deadq = NULL;
struct id_node *idarray = NULL; //queue with all ids, to keep track of which ones are used
struct thread *runningrn = NULL; //thread that is currently running

//THREAD FUNCTIONS
void
thread_stub(void (*thread_main)(void *), void *arg)
{
	//Tid ret;
	interrupts_on(); //turn interrupts on
	thread_main(arg); // call thread_main() function with arg
	thread_exit();
	exit(0);
}

void
thread_init(void)
{
	//allocate space for id array, set ids
	idarray = (struct id_node*)malloc(THREAD_MAX_THREADS*sizeof(struct id_node));
	//since all possible threads are THREADS_MAX_THREADS
	for (int i=0; i<=THREAD_MAX_THREADS-1; i++){
		//set each thread id and status to be ID_FREE
		struct id_node curr_id; //new id struct
		curr_id.id = i;
		curr_id.status = ID_FREE;
		idarray[i]= curr_id;
		
	}
	//allocate space for queues
	readyq = (struct que*)malloc(sizeof(struct que)); //ready queue
	readyq->top = NULL;
	readyq->tail = NULL;
	readyq->num_threads = 0;

	exitedq = (struct que*)malloc(sizeof(struct que)); //exited queue
	exitedq->top = NULL;
	exitedq->tail = NULL;
	exitedq->num_threads = 0;
	
	deadq = (struct que*)malloc(sizeof(struct que));
	deadq->top = NULL;
	deadq->tail = NULL;
	deadq->num_threads = 0;

	//create first thread
	struct thread *first = (struct thread*)malloc(sizeof(struct thread));
	first->id = 0;
	idarray[0].status = ID_USED; //set the first id to be occupied
	first->stack = NULL; //set stack to be NULL
	
	//set first thread as the currently running one
	runningrn = first;
}

Tid
thread_id()
{
	return (runningrn!=NULL ? runningrn->id: THREAD_FAILED);
}

Tid
thread_create(void (*fn) (void *), void *parg)
{
	//thread_create critical section
	interrupts_set(0);
	//check for next available thread id
	struct id_node tid = idarray[0];
	int i =0;
	while(tid.status == ID_USED){
		//used already so check next id
		if (i== THREAD_MAX_THREADS-1){
			//reached the end without finding available id
			//return THREAD_NOMORE if max threads have been reached
			interrupts_set(1); //enable interrupts again
			return THREAD_NOMORE;
		}
		i+=1;
		tid = idarray[i];
	}
	//otherwise the thread id will be tid->id
	
	//allocates thread structure
	struct thread *curr = (struct thread*)malloc(sizeof(struct thread));
	if (curr == NULL){
		//THREAD_NOMEMORY when no memory left to allocate
		interrupts_set(1); //enable again
		return THREAD_NOMEMORY;
	}
	curr->id = tid.id; //available thread id
	
	//update context
	getcontext(&curr->context);
	//update PC
	//program counter points to next instruction, stub function
	curr->context.uc_mcontext.gregs[REG_RIP] = (long long int) &thread_stub;
	curr->context.uc_mcontext.gregs[REG_RDI]= (long long int) fn; //first arg
	curr->context.uc_mcontext.gregs[REG_RSI] = (long long int) parg; //second
	
	//allocates stack memory
	//initializes SP
	void *s= malloc(THREAD_MIN_STACK);
	if (s ==NULL){
		//no memory for stack
		interrupts_set(1); //turn em back on
		free(curr); //free the thread
		return THREAD_NOMEMORY;
	}
	curr->stack = s; //init
	curr->context.uc_link = NULL; //for safety
	curr->context.uc_stack.ss_flags = 0; //TODO:check
	curr->context.uc_stack.ss_sp = curr->stack; //associating stack pointers
	curr->context.uc_stack.ss_size = THREAD_MIN_STACK; //set stack size

	//bring down to bottom of the stack, aligns it (mod 16 -8)
	s = curr->context.uc_stack.ss_sp + (THREAD_MIN_STACK - ((unsigned long long)s)%16 -8);
	//store final 
	curr->context.uc_mcontext.gregs[REG_RSP] = (long long int) s;

	curr->state = READY; //sets current thread to READY
	//adds thread to ready queue
	if (enqueue(readyq, curr)== -1){
		interrupts_set(1); //enable interrupts before return
		return THREAD_FAILED;
	} //otherwise, enqueue worked
	readyq->num_threads +=1;
	//set id as used
	(idarray[curr->id]).status = ID_USED;
	interrupts_set(1);
	return curr->id;
}

Tid
thread_yield(Tid want_tid)
{
	volatile int sw = 0;	
	volatile int retid = 0;

	/* runs thread with want_tid, yields currently running thread*/
	int i = interrupts_off(); //critical section time
	//check if THREAD_SELF or want_tid is current => continue running current thread
	if (want_tid == THREAD_SELF){
	
		interrupts_set(i); //turn back on when completed
		return runningrn->id;
	}else if(want_tid == runningrn->id){
		//continue running current, return its id
		interrupts_set(i); //turn back on when completed
		return runningrn->id;
	}
	//check if want_tid is out of bounds (0, THREAD_MAX_THREADS-1) => THREAD_INVALID
	if ((want_tid > THREAD_MAX_THREADS-1) || (want_tid<-2)){
		//no ids exist past thread_max_threads
		//want_tid should only be -1 and -2 for the ANY and SELF cases
		interrupts_set(i); 
		return THREAD_INVALID;
	}
	//if readyq has no threads, return THREAD_NONreadyq->top == NULL && readyq->tail == NULLE
	/*if (readyq->num_threads==0 || (readyq->top == NULL && readyq->tail == NULL)){
		printf("no threads in ready\n");
		//when there are no threads that are ready to run, exit
		interrupts_set(i); //enable again
		if (want_tid == THREAD_ANY){
			//no more threads other than caller are available
			printf("\n");
			return THREAD_NONE;
		}else{
			//no threads available
			return THREAD_INVALID;
		}
	}*/
		if (readyq->num_threads == 0){
			interrupts_set(i);
			if (want_tid == THREAD_ANY){

				interrupts_set(i);
				return THREAD_NONE;
			}else {
				interrupts_set(i);
				return THREAD_INVALID;
			}
		}

		getcontext(&runningrn->context);
	       	if (sw == 1){
				interrupts_set(i);
			return retid; 
		}
		struct node *curr = readyq->top;
		while (curr){
			curr = curr->next;
		}

		runningrn->state = READY;
		enqueue(readyq, runningrn);

		if (want_tid == THREAD_ANY){
			
			runningrn = popbyid(readyq, readyq->top->thread->id); //works bc already checked if readyq empty

		}else{
			
			runningrn = popbyid(readyq, want_tid); //works bc already checked if readyq empty
			if (runningrn == NULL){
				runningrn = popbyid(readyq, readyq->tail->thread->id);
				interrupts_set(i);
				return THREAD_INVALID;
			}
		}

		retid = runningrn->id;
		while (runningrn->state == DEAD){
			idarray[runningrn->id].status = ID_FREE;
			struct thread *temp = runningrn;
			free(temp->stack);
			free(temp);
				
			runningrn = popbyid(readyq, readyq->top->thread->id);
			readyq->num_threads -=1;
		}
		runningrn->state = RUNNING;
		
		//update previously running thread

		sw = 1;
		setcontext(&runningrn->context);

		interrupts_set(i);
		return retid;	

	//otherwise
	
	/*
	printf("1\n");
	volatile int sw = 0; //track context switch so we only run once, avoid inf loop
	struct ucontext_t con;	
	printf("out is %d\n", out);
	printf("second sw is %d\n", sw);
	

	//freeing exired queue
	printf("2\n");
	struct node *exitnode = exitedq->top;
	while (exitnode != NULL){
		struct node* temp = exitnode;
		exitnode = exitnode->next;
		free(temp->thread->stack);
		free(temp->thread);
		free(temp);
		exitedq->num_threads -=1; //update num threads
	}
	exitedq->top = NULL;
	exitedq->tail = NULL;

	//thread_exit if dead 
	if (runningrn->state == DEAD){
	//check if thread should exit, exit if so
		interrupts_set(i);
		thread_exit();
	}

	if (sw ==0 && out ==0){
		printf("3\n");
		sw = 1; //sets state so this only runs once 

		runningrn->context = con;	

	//runningrn->context = con; //resets context
		printf("4\n");
		// chooses thread with want_tid
		//check THREAD_ANY
		if (want_tid != THREAD_ANY){
			printf("4.2\n");
		//dequeue the thread with thread_id = want_tid
			runningrn = popbyid(readyq, want_tid);
			printf("4.3\n");
			if (runningrn == NULL){
			//thread with wanted id DNE in ready queue
				interrupts_set(i);
				return THREAD_INVALID;
			}
			runningrn->state = RUNNING;
		}

		con = runningrn->context;
	
		//context switch
		printf("6\n");
		//con = runningrn->context;
		out = setcontext(&con); //set context 
	}
	
	// turn on interrupts
	printf("id is %d\n", runningrn->id);
	printf("wanted id is %d\n", want_tid);
	interrupts_set(i);
	return runningrn->id;*/
}

void
thread_exit()
{
	//turn off interrupts - critical section
	int i = interrupts_off();
	
	//check current running thread runningrn
	//set thread status to exited
	runningrn->state = EXITED;
	
	if (exitedq->num_threads ==1){
		struct thread *curr = popbyid(exitedq, exitedq->top->thread->id);
		free(curr->stack);
		free(curr);
	}


	enqueue(exitedq, runningrn);//add the thread to exited queue
	int id = runningrn->id;
	runningrn->id = -1; //set it to not have an id
	
	runningrn = NULL;
	//add id to available thread id list
	(idarray[id]).status = ID_FREE;
	
	//turn on interrupts
	interrupts_set(i);

	//thread yield or THREAD_NONE if readyq is empty
	if ((readyq->num_threads == 0)){
		//exit if there are no ready threads
		free(readyq); 

		exit(0);
	} else {
		
		//otherwise yield
		runningrn = popbyid(readyq, readyq->top->thread->id);

		while (runningrn->state == DEAD){
			idarray[runningrn->id].status = ID_FREE;
			struct thread *temp = runningrn;
			free(temp->stack);
			free(temp);
				
			runningrn = popbyid(readyq, readyq->top->thread->id);
			readyq->num_threads -=1;
		}
		runningrn->state = READY;
		readyq->num_threads -=1;
		setcontext(&runningrn->context);	
	}
}

Tid
thread_kill(Tid tid)
{
	//free memory allocated for thread
	int i = interrupts_off(); //critical section time yay
	int retid = -1; //id to be returned
	if (tid == runningrn->id){
		interrupts_set(i);
		return THREAD_INVALID;
	}
	struct node *curr = readyq->top;
	while (curr){
		
		if (curr->thread->id == tid){
			curr->thread->state = DEAD;
			retid = curr->thread->id;
			interrupts_set(i);
			return retid;
		} else {
			curr = curr->next;
		}
	}
	if (curr == NULL){
		interrupts_set(i);
		return THREAD_INVALID;
	}
	interrupts_set(i);
	return retid;
	
	//check if thread id is in range
	/*if (tid < THREAD_MAX_THREADS && tid>=0){
		//make sure it's not the running thread
		//if (tid != runningrn->id){
			if (tid == runningrn->id){
				//is the currently running thread
				runningrn->state = DEAD; //set the new state
				retid = runningrn->id;
				runningrn->id = -1;
				enqueue(deadq, runningrn);
				//free(runningrn); //free the thread
				(idarray[retid]).status = ID_FREE; //set id to be free
				interrupts_set(i);
				return retid;
			}
			//if thread state is ready, kill and return id
			struct node *curr = readyq->top;
			while (curr->thread->id != tid){
			//iterate until we find node with tid
				if (curr->next){
					//next node
					curr = curr->next;
				} else {
					//next node not in ready queue
					//therefore thread with tid not ready
					interrupts_set(i);
					return THREAD_INVALID;
				}
			} 
			//kill the curr node
			if(curr == readyq->tail){
				//next DNE but previous does
	 			(curr->prev)->next = NULL; //set previous nodes next to null
				readyq->tail = curr->prev; //sets tail to previous
				retid = curr->thread->id;
				//free(curr);
				enqueue(deadq, curr->thread);
				(idarray[retid]).status = ID_FREE;
				interrupts_set(i);
				return retid;
			}else if (curr == readyq->top){
				//next exists, prev doesn't
				(curr->next)->prev = NULL; //sets second nodes prev to null
				readyq->top = curr->next;
				retid = curr->thread->id;
				//free(curr);
				enqueue(deadq, curr->thread);
				(idarray[retid]).status = ID_FREE;
				interrupts_set(i);
				return retid;
	                  		}else if (curr->next != NULL && curr->prev != NULL){
				//has a node before and after
				(curr->prev)->next = curr->next; 
				(curr->next)->prev = curr->prev;
				retid = curr->thread->id;
				//free(curr);
				enqueue(deadq, curr->thread);
				(idarray[retid]).status = ID_FREE;
				interrupts_set(i);
				return retid;
			} else {
				interrupts_set(i);
				return THREAD_INVALID;

			}
	//turn interrupts back on
		//}
	}
	interrupts_set(i);
	return THREAD_INVALID;      */
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	TBD();

	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	TBD();
	free(wq);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	TBD();
	return THREAD_FAILED;
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	TBD();
	return 0;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	TBD();
	return 0;
}

struct lock {
	/* ... Fill this in ... */
};

struct lock *
lock_create()
{
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	TBD();

	return lock;
}

void
lock_destroy(struct lock *lock)
{
	assert(lock != NULL);

	TBD();

	free(lock);
}

void
lock_acquire(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

void
lock_release(struct lock *lock)
{
	assert(lock != NULL);

	TBD();
}

struct cv {
	/* ... Fill this in ... */
};

struct cv *
cv_create()
{
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	TBD();

	return cv;
}

void
cv_destroy(struct cv *cv)
{
	assert(cv != NULL);

	TBD();

	free(cv);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	assert(cv != NULL);
	assert(lock != NULL);

	TBD();
}
