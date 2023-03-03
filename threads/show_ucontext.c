#include <sys/types.h>
#include <unistd.h>
#include <assert.h>
#include <stdlib.h>
#include <ucontext.h>
#include "interrupt.h"

#ifndef __x86_64__
#error "Do this project on a 64-bit x86-64 linux machine"
#endif /* __x86_64__ */

#if __WORDSIZE != 64
#error "word size should be 64 bits"
#endif

static void call_setcontext(ucontext_t * context);
static void show_interrupt(void);

/* zero out the context */
ucontext_t mycontext = { 0 };

void
get_start_end(long *startp, long *endp)
{
	char filename[128];
	char line[256];
	FILE *f;
	
	sprintf(filename, "/proc/%d/maps", getpid());
	f = fopen(filename, "r");
	assert(f);
	/* read first line */
	fgets(line, sizeof(line), f);
	/* read second line, which has the executable region. */
	fgets(line, sizeof(line), f);	
	sscanf(line, "%lx-%lx ", startp, endp);
}

int
main(int argc, char **argv)
{
	long start, end;
	/* we declare this variable to be volatile so that the compiler will
	 * make sure to store it on the stack and not in a register. This is
	 * ESSENTIAL, or else the code in this function may run in an infinite
	 * loop (you can try this by removing the volatile keyword, and
	 * compiling this file with the "-O2" flag to the gcc compiler, by
	 * changing CFLAGS in the Makefile).
	 *
	 * QUESTION: why must setcontext_called be stored on the stack, and not
	 * in a register? You will need to look at the code below, and
	 * understand how getcontext and setcontext work to answer this
	 * question. */
	volatile int setcontext_called = 0;
	int err;

	/*
	 * DO NOT CHANGE/ADD ANY CODE UNTIL BELOW TBD(). SEE BELOW.
	 */

	/* Get context: make sure to read the man page of getcontext in detail,
	 * or else you will not be able to answer the questions below. */
	err = getcontext(&mycontext);
	assert(!err);

	/* QUESTION: which of the fields of mycontext changed due to the call
	 * above? Hint: It will help to run the program using gdb and put a
	 * breakpoint at entry to main and before and after the calls to
	 * getcontext().
	 * - Use "info registers" to see the values of the registers.
	 * - Use "next"/"step" to advance to the next line of code.
	 * - Use "print mycontext" to see the values stored in mycontext.
	 *   Compare them with the output of "info registers".
	 * - Use "ptype mycontext" so see the type/fields of mycontext */

	// ucontext_t struct info - https://pubs.opengroup.org/onlinepubs/7908799/xsh/ucontext.h.html 
	// getcontext: initializes mycontext to the currently active context

	printf("%s: setcontext_called = %d\n", __FUNCTION__, setcontext_called);
	if (setcontext_called == 1) {
		/* QUESTION: will be get here? why or why not? */
		//TODO: check- setcontext_called will be 1 after setcontext() is called
		show_interrupt();
		exit(0);
	}

	get_start_end(&start, &end);
	printf("start = 0x%lx\n", start);
	printf("end = 0x%lx\n", end);

	/* show size of ucontext_t structure. Hint: use sizeof(). */
	printf("ucontext_t size = %ld bytes\n", (long int) sizeof(mycontext));

	/* now, look inside of the context you just saved. */

	/* first, think about code */
	/* the program counter is called rip in x86-64 */
	printf("memory address of main() = 0x%lx\n", (unsigned long) &main);
	printf("memory address of the program counter (RIP) saved "
	       "in mycontext = 0x%lx\n",
	       (unsigned long) mycontext.uc_mcontext.gregs[REG_RIP]);
	// getting rip: https://stackoverflow.com/questions/4067524/how-to-get-the-pc-register-for-any-thread-on-linux 

	/* now, think about parameters */
	//  %rdi, %rsi first 2 registers for arguments
	printf("argc = %d\n", (int) mycontext.uc_mcontext.gregs[REG_RDI]); // is a lld, not d
	printf("argv = %p\n", (void *) mycontext.uc_mcontext.gregs[REG_RSI]);

	/* QUESTIONS: how are these parameters passed into the main function? 
	 * are there any saved registers in mycontext that store the parameter
	 * values above. why or why not? Hint: Use gdb, and then run
	 * "disassemble main" in gdb, and then scroll up to see the beginning of
	 * the main function. */ 

	/* now, think about the stack */
	/* QUESTIONS: Is setcontext_called and err stored on the stack? does the
	 * stack grow up or down? What are the stack related data in
	 * mycontext.uc_mcontext.gregs[]? */

	// local variables stored below rbp on the stack
	// base pointer rbp -8 amd -16 for the first 2 variables
	printf("memory address of the variable setcontext_called = %p\n",
	       (void *) mycontext.uc_mcontext.gregs[REG_RBP]-8);
	// setcontext_called is first local variable we come across =>directly below rbp
	// next variable is err, located below setcontext_called
	printf("memory address of the variable err = %p\n",
	       (void *) mycontext.uc_mcontext.gregs[REG_RBP]-16);
	
	//TODO: check if we can just subtract their addresses
	printf("number of bytes pushed to the stack between setcontext_called "
	       "and err = %ld\n", (unsigned long) ((&setcontext_called)-(&err)));

	// register REG_RSP
	printf("stack pointer register (RSP) stored in mycontext = 0x%lx\n",
	       (unsigned long) mycontext.uc_mcontext.gregs[REG_RSP]);

	// gregs[REG_RSP] is a lld, &err is int* => so make lld into int*
	// number of bytes appears as -6, make it positive (gives suspicious error message) 
	printf("number of bytes between err and the saved stack in mycontext "
	       "= %ld\n", (unsigned long) -1*(((int*)mycontext.uc_mcontext.gregs[REG_RSP])-(&err)));

	/* QUESTION: what is the uc_stack field in mycontext? Note that this
	 * field is used to store an alternate stack for use during signal
	 * handling, and is NOT the stack of the running thread. */

	// uc_stack is stack used for the context, stack pointer value:
	printf("value of uc_stack.ss_sp = 0x%lx\n",
	       (unsigned long) mycontext.uc_stack.ss_sp);

	/* Don't move on to the next part of the lab until you know how to
	 * change the stack in a context when you manipulate a context to create
	 * a new thread. */

	/* now we will try to understand how setcontext works */
	setcontext_called = 1;
	call_setcontext(&mycontext);
	/* QUESTION: why does the program not fail at the assert below? */
	assert(0);
}

static void
call_setcontext(ucontext_t * context)
{
	int err = setcontext(context);
	assert(!err);
}

static void
show_interrupt(void)
{
	int err;

	/* QUESTION: how did we get here if there was an assert above? */

	/* now think about interrupts. you will need to understand how they
	 * work, and how they interact with get/setcontext for implementing
	 * preemptive threading. */

	/* QUESTION: what does interrupts_on() do? see interrupt.c */
	interrupts_on();

	/* getcontext stores the process's signal mask */
	err = getcontext(&mycontext);
	assert(!err);

	/* QUESTION: Are interrupts masked (i.e., disabled) in mycontext?
	 * HINT: use sigismember below. */
	// checks if signo is in the set
	printf("interrupt is disabled = %d\n",
	       (unsigned int) sigismember(&mycontext.uc_sigmask, SIG_TYPE));

	interrupts_off();

	err = getcontext(&mycontext);
	assert(!err);

	/* QUESTION: which fields of mycontext changed as a result of the
	 * getcontext call above? */
	printf("interrupt is disabled = %d\n",
	       (unsigned int) sigismember(&mycontext.uc_sigmask, SIG_TYPE));
}
