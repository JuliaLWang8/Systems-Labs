#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "thread.h"
#include "interrupt.h"

//#define DEBUG_USE_VALGRIND

#ifdef DEBUG_USE_VALGRIND
#include <valgrind.h>
#endif

//DEFINITIONS
//thread states and id states:
enum{ 
	READY = 0,
	BLOCKED = 1,
	RUNNING = 2,
	EXITED = 3,
	DEAD = 4,
	WAITING = 5,
	ID_USED = 0,
	ID_FREE = 1, 
	LOCKED = 0,
	UNLOCKED = 1
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
struct wait_queue{
	struct node *top; //points to the top node of the queue
	struct node *tail; //points to tail of the queue
	int num_threads; //total amount of threads in the queue
};

struct node{
	struct thread *thread; //thread in the queue
	struct node *next; //next thread in the sewait_queuece
	struct node *prev;
};

struct thread *pop(struct wait_queue *q){
	/* function to pop the top of the queue (FIFO)*/
	int i = interrupts_off();
	if (q == NULL){
		interrupts_set(i);
		return NULL;
	}
	struct thread *thread = q->top->thread;
	struct node *temp = q->top;

	q->top = q->top->next;
	free(temp); //free the queue node
	q->num_threads -=1; //decrease number of threads
	interrupts_set(i);
	return thread;
}

struct thread *popbyid(struct wait_queue *q, Tid w_id){
	int i = interrupts_off();
	if (q==NULL){
		interrupts_set(i);
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
			interrupts_set(i);
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
	interrupts_set(i);
	return final;
}

int enqueue(struct wait_queue*q, struct thread *curr){
	int i = interrupts_off();
	//allocate space for node
	if ((q==NULL) || (curr==NULL)){
		interrupts_set(i);
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
	interrupts_set(i);
	return 1;
}

// for thread ID array to keep track of which id's are available
// uopn thread creation
struct id_node{
	Tid id; //threads id
	int status; //status = ID_FREE or ID_USED
	int state; //thread state
	struct wait_queue *wq;
};


//INITIALIZING
struct wait_queue *readyq = NULL; //queue with all ready threads
struct wait_queue *exitedq = NULL; //queue with all exited threads
struct wait_queue *deadq = NULL;
struct wait_queue *waitq = NULL;
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
	int i = interrupts_off();
	//allocate space for id array, set ids
	idarray = (struct id_node*)malloc(THREAD_MAX_THREADS*sizeof(struct id_node));
	//since all possible threads are THREADS_MAX_THREADS
	for (int i=0; i<=THREAD_MAX_THREADS-1; i++){
		//set each thread id and status to be ID_FREE
		struct id_node curr_id; //new id struct
		curr_id.id = i;
		curr_id.status = ID_FREE;
		curr_id.state = READY;
		curr_id.wq = NULL;
		idarray[i]= curr_id;
		
	}
	//allocate space for queues
	readyq = (struct wait_queue *)malloc(sizeof(struct wait_queue)); //ready queue
	readyq->top = NULL;
	readyq->tail = NULL;
	readyq->num_threads = 0;

	exitedq = (struct wait_queue *)malloc(sizeof(struct wait_queue)); //exited queue
	exitedq->top = NULL;
	exitedq->tail = NULL;
	exitedq->num_threads = 0;
	
	//create first thread
	struct thread *first = (struct thread*)malloc(sizeof(struct thread));
	first->id = 0;
	idarray[0].status = ID_USED; //set the first id to be occupied
	first->stack = NULL; //set stack to be NULL
	
	//set first thread as the currently running one
	runningrn = first;
	interrupts_set(i);
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
	int f = interrupts_off();
	//check for next available thread id
	
	struct id_node tid = idarray[0];
	int i =0;
	while(tid.status == ID_USED){
		//used already so check next id
		if (i== THREAD_MAX_THREADS-1){
			//reached the end without finding available id
			//return THREAD_NOMORE if max threads have been reached
			interrupts_set(f); //enable interrupts again
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
		interrupts_set(f); //enable again
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
		free(curr); //free the thread
		interrupts_set(f); //turn em back on
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

	#ifdef DEBUG_USE_VALGRIND
		unsigned valgrind_register_retval = VALGRIND_STACK_REGISTER(s - THREAD_MIN_STACK, s);
		assert(valgrind_register_retval);
	#endif

	curr->state = READY; //sets current thread to READY
	//adds thread to ready queue
	if (enqueue(readyq, curr)== -1){
		interrupts_set(f); //enable interrupts before return
		return THREAD_FAILED;
	} //otherwise, enqueue worked
	
	//set id as used
	(idarray[curr->id]).status = ID_USED;
	int ret = curr->id;
	interrupts_set(f);
	return ret;
}

Tid
thread_yield(Tid want_tid)
{
	int i = interrupts_off(); //critical section time
	volatile int sw = 0;	
	volatile int retid = 0;
	/* runs thread with want_tid, yields currently running thread*/
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
		if (readyq->top == NULL){
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
		idarray[runningrn->id].state=READY;
		enqueue(readyq, runningrn);

		if (want_tid == THREAD_ANY){
			struct node *test = readyq->top;
			while (test != readyq->tail) {
				test = test->next;
			}
			runningrn = pop(readyq); //works bc already checked if readyq empty

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
			volatile int thread_killed = temp->id;
			if (readyq->num_threads > 0 ) {
				runningrn = popbyid(readyq, readyq->top->thread->id);
			}
			if (idarray[thread_killed].wq != NULL){
				//if there is an associated wait q with the thread
				// run thread wakeup on all of the wq threads
				thread_wakeup(idarray[thread_killed].wq, 1);
				wait_queue_destroy(idarray[thread_killed].wq);
			}
			free(temp->stack);
			free(temp);
		}
		runningrn->state = RUNNING;
		idarray[runningrn->id].state = RUNNING;
		
		//update previously running thread

		sw = 1;
		setcontext(&runningrn->context);
		//Should never reach here
		assert(0);
		interrupts_set(i);
		return retid;	
}

void
thread_exit()
{
	//turn off interrupts - critical section
	int i = interrupts_off();
	//check waitq
	//if the currently running thread has associated wq
	volatile int thread_to_exit = runningrn->id;
	if (idarray[runningrn->id].wq != NULL){
		//if there is an associated wait q with the thread
		// run thread wakeup on all of the wq threads
		thread_wakeup(idarray[runningrn->id].wq, 1);
		wait_queue_destroy(idarray[runningrn->id].wq);
	}
	//check current running thread runningrn
	//set thread status to exited
	runningrn->state = EXITED;
	idarray[runningrn->id].state= READY;
	
	if (exitedq->num_threads > 0){
		struct thread *curr = popbyid(exitedq, exitedq->top->thread->id);
			#ifdef DEBUG_USE_VALGRIND
				VALGRIND_STACK_DEREGISTER(curr->context.uc_mcontext.gregs[REG_RSP] - THREAD_MIN_STACK);
			#endif
		free(curr->stack);
		free(curr);
	}


	enqueue(exitedq, runningrn);//add the thread to exited queue
	int id = runningrn->id;
	runningrn->id = -1; //set it to not have an id
	
	runningrn = NULL;
	//add id to available thread id list
	(idarray[id]).status = ID_FREE;
	
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
			volatile int thread_killed = temp->id;
			if (readyq->num_threads > 0 ) {
				runningrn = popbyid(readyq, readyq->top->thread->id);
			}
			if (idarray[thread_killed].wq != NULL){
				//if there is an associated wait q with the thread
				// run thread wakeup on all of the wq threads
				thread_wakeup(idarray[thread_killed].wq, 1);
				wait_queue_destroy(idarray[thread_killed].wq);
			}
			free(temp->stack);
			free(temp);
			if ((readyq->num_threads == 0 && thread_to_exit == thread_id()) || (readyq->num_threads == 0 && thread_killed == thread_id())){
				free(readyq); 
				interrupts_set(i);
				exit(0);
			}
		}
		
		runningrn->state = READY;
		setcontext(&runningrn->context);	
	}
}

Tid
thread_kill(Tid tid)
{
	//free memory allocated for thread
	int i = interrupts_off(); //critical section time yay
	int retid = -1; //id to be returned
	if (tid == runningrn->id || tid<0 || tid>THREAD_MAX_THREADS-1){
		interrupts_set(i);
		return THREAD_INVALID;
	}
	struct node *curr = readyq->top;
	while (curr){
		
		if (curr->thread->id == tid){
			curr->thread->state = DEAD;
			idarray[curr->thread->id].state = DEAD;
			retid = curr->thread->id;
			interrupts_set(i);
			return retid;
		} else {
			curr = curr->next;
		}
	}
	if (curr == NULL){
		if ((idarray[tid].status == ID_USED)&& (idarray[tid].state == WAITING)){
			idarray[tid].state = DEAD;
			retid = tid;
		} else {
			interrupts_set(i);
			return THREAD_INVALID;
		}
	}
	interrupts_set(i);
	return retid;
	
}

/*******************************************************************
 * Important: The rest of the code should be implemented in Lab 3. *
 *******************************************************************/

/* make sure to fill the wait_queue structure defined above */
struct wait_queue *
wait_queue_create()
{
	int i = interrupts_off(); //interrupts off
	struct wait_queue *wq;

	wq = malloc(sizeof(struct wait_queue));
	assert(wq);

	wq->top = NULL; //assign top and tail DNE
	wq->tail = NULL;
	wq->num_threads = 0; //0 threads in the q
	
	interrupts_set(i); //interrupts back on
	return wq;
}

void
wait_queue_destroy(struct wait_queue *wq)
{
	int i = interrupts_off();
	if (wq->num_threads == 0){
		//if the queue is empty, can just free her
		free(wq);
		interrupts_set(i);
		return;
	}
	interrupts_set(i);
}

Tid
thread_sleep(struct wait_queue *queue)
{
	
	int i = interrupts_off();
	if (queue == NULL){
		//THREAD_INVALID if the wq dne
		interrupts_set(i);
		return THREAD_INVALID;
	}

	//if no threads in readyq, return THREAD_NONE
	if (readyq->num_threads == 0){
		interrupts_set(i);
		return THREAD_NONE;
	}

	//TODO: context switch
	volatile int sw = 0;
	volatile int ret = 0;
	getcontext(&runningrn->context);
	
	if (sw == 1){
		interrupts_set(i);
		return ret;
	}
	//run the next thread in the ready queue
	//pop thread from readyq to run now
	struct thread *next = pop(readyq);

	while (next->state == DEAD){
		idarray[runningrn->id].status = ID_FREE;
		struct thread *temp = next;
		volatile int thread_killed = temp->id;
		if (readyq->num_threads > 0 ) {
			next = popbyid(readyq, readyq->top->thread->id);
		}
		if (idarray[thread_killed].wq != NULL){
			//if there is an associated wait q with the thread
			// run thread wakeup on all of the wq threads
			thread_wakeup(idarray[thread_killed].wq, 1);
			wait_queue_destroy(idarray[thread_killed].wq);
		}
		free(temp->stack);
		free(temp);
	}
	//change state of runningrn to WAITING
	runningrn->state = WAITING;
	idarray[runningrn->id].state = WAITING;

	//set thread id to be output
	ret = next->id;
		//set runningrn = thread


	//add current thread to the wait queue	
	enqueue(queue, runningrn);
	runningrn = next;
	runningrn->state = RUNNING;
	idarray[runningrn->id].state = RUNNING;
	
	sw = 1;
	setcontext(&runningrn->context);
	//Should never reach here
	interrupts_set(i);
	return ret; //return threadid of now running thread
}

/* when the 'all' parameter is 1, wakeup all threads waiting in the queue.
 * returns whether a thread was woken up on not. */
int
thread_wakeup(struct wait_queue *queue, int all)
{
	int i = interrupts_off();
	int count = 0;
	//check if wq is NULL
	if (queue == NULL){
		interrupts_set(i);
		return count; 
	}
	if (queue->top == NULL){
		//no suspended threads in the wq case
		interrupts_set(i);
		return count;
	}
	if (all == 0){
	// all = 0 : 1 thread woken up
		//get thread to wakeup from waitq
		struct thread *t = pop(queue);

		//set state of thread to ready if not killed
		if (idarray[t->id].state == WAITING){
			t->state = READY;
			idarray[t->id].state= READY;
		} else {
			//otherwise it's killed
			t->state = DEAD;
		}
		//add thread to readyq
		enqueue(readyq, t);
		count += 1;

	} else if (all == 1){
	// all = 1 : all threads woken up
		while(queue->num_threads > 0){
			count += thread_wakeup(queue, 0);
		}
		//push all threads from wq in a loop
	}
	interrupts_set(i);
	return count;
}

/* suspend current thread until Thread tid exits */
Tid
thread_wait(Tid tid)
{
	int i = interrupts_off();
	//check idarray for tid && current thread-> return THREAD_INVALID
	if (tid < 0 || tid>THREAD_MAX_THREADS-1){
		//tid out of bounds when negative or too big
		interrupts_set(i);
		return THREAD_INVALID;
	} 
	if (idarray[tid].status == ID_FREE){
		//tid isn't being used 
		interrupts_set(i);
		return THREAD_INVALID;
	}
	if (tid == thread_id()){
		interrupts_set(i);
		return THREAD_INVALID;
	}
	//set state of runningrn to WAITING
	runningrn->state = WAITING;
	//int ret = runningrn->id; //return value 
	idarray[runningrn->id].state = WAITING;

	if (idarray[tid].status == ID_USED){

	
		//set wq of thread tid
		if (idarray[tid].wq == NULL){	
			//if wq DNE
			//initialize queue, allocate space for it
			
			idarray[tid].wq = wait_queue_create();
		}
		//otherwise wq does exist
		//add runningrn (curr thread) to waitq of thread with tid
		//enqueue(idarray[tid].wq, runningrn);
		thread_sleep(idarray[tid].wq);
	}
	interrupts_set(i);
	//return tid
	return tid;
}

struct lock {
	/* ... Fill this in ... */
	int state; //LOCKED or UNLOCKED
	Tid id; //id of the thread holding it
	struct wait_queue *wq;
};

struct lock *
lock_create()
{
	int i = interrupts_off();
	struct lock *lock;

	lock = malloc(sizeof(struct lock));
	assert(lock);

	lock->wq = wait_queue_create();
	lock->state = UNLOCKED;
	lock->id= -328;

	interrupts_set(i);
	return lock;
}

void
lock_destroy(struct lock *lock)
{

	int i = interrupts_off();
	assert(lock != NULL);

	if (lock->wq->num_threads>0 || lock->state==LOCKED ) goto BLAH;
	
	wait_queue_destroy(lock->wq);
	free(lock);
BLAH:
	interrupts_set(i);
}

void
lock_acquire(struct lock *lock)
{
	int i = interrupts_off();
	assert(lock != NULL);

	while (lock->state == LOCKED && lock->id != runningrn->id){
		//lock is being held
		thread_sleep(lock->wq);
	}

	lock->state = LOCKED; //hold the lock
	lock->id = runningrn->id; //current thread holds lock

	interrupts_set(i);
}

void
lock_release(struct lock *lock)
{
	int i = interrupts_off();
	assert(lock != NULL);

	//check lock held by running thread
	if (lock->id == runningrn->id && lock->state == LOCKED){
		lock->state = UNLOCKED;
		lock->id = -328;
		thread_wakeup(lock->wq, 1);
	}
	//wakeup threads in wq
	interrupts_set(i);
}

struct cv {
	/* ... Fill this in ... */
	struct wait_queue *wq;
};

struct cv *
cv_create()
{
	int i = interrupts_off();
	struct cv *cv;

	cv = malloc(sizeof(struct cv));
	assert(cv);

	cv->wq = wait_queue_create();

	interrupts_set(i);
	return cv;
}

void
cv_destroy(struct cv *cv)
{
	int i = interrupts_off();
	assert(cv != NULL);

	if (cv->wq->num_threads >0) goto BLAH;
	
	wait_queue_destroy(cv->wq);
	free(cv);
BLAH:
	interrupts_set(i);
}

void
cv_wait(struct cv *cv, struct lock *lock)
{
	int i = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->state == LOCKED && lock->id == runningrn->id){
		lock_release(lock);
		thread_sleep(cv->wq);
		lock_acquire(lock);
	}

	interrupts_set(i);
}

void
cv_signal(struct cv *cv, struct lock *lock)
{
	int i = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->state == LOCKED && lock->id == runningrn->id){
		thread_wakeup(cv->wq, 0);
	}
	interrupts_set(i);
}

void
cv_broadcast(struct cv *cv, struct lock *lock)
{
	int i = interrupts_off();
	assert(cv != NULL);
	assert(lock != NULL);

	if (lock->state == LOCKED && lock->id == runningrn->id){
		thread_wakeup(cv->wq, 1);
	}
	interrupts_set(i);
}
