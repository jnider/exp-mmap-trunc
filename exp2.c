#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

//#define URCU_INLINE_SMALL_FUNCTIONS
#include <urcu/urcu-mb.h>	/* Memory barrier RCU flavor */

#define MAX_THREADS 20
#define ITERATIONS 10000

static unsigned long filestart;

struct thread_params
{
	int thread_id; // Index of this thread
	void *input_mem; // address of memory-mapped file
	struct file_attr *fattr; // contains information about open file
	unsigned int pagesize; // page size used by the OS
	unsigned int iterations; // how many times to read through the file
};

struct file_attr
{
	int handle; // file handle to 'large file'
	unsigned int length; // RCU protected length of the memory mapped file
};

// Thread B: truncates the file without synchronizing with main thread
void *read_thread_main(void *arg)
{
	struct file_attr *fattr;
	unsigned long count = 0;
	struct thread_params *params = (struct thread_params*)arg;
	int thread_id = params->thread_id;
	void *input_mem = params->input_mem;
	unsigned int pagesize = params->pagesize;
	unsigned int iterations = params->iterations;
	unsigned int length = params->fattr->length;

	printf("Thread %i starting\n", thread_id);
	rcu_register_thread_mb();

	// read data from the file
	while (iterations--)
	{
		// sync on the file length
		rcu_read_lock_mb();
		length = params->fattr->length;

		printf("#%i: Iteration %u (size=%i)\n", thread_id, iterations, length);
		for(void* addr = input_mem; addr < (input_mem + length); addr+=sizeof(unsigned int))
		{
			unsigned int data = *(unsigned int*)addr;
			if (((unsigned long)addr % pagesize) == 0)
				printf("Thread %i: %p = %u\n", thread_id, addr, data);
			*(unsigned int*)addr++;
			count++;
		}
		rcu_read_unlock_mb();
		//usleep(1);
	}

	printf("Thread %i done (count=%lu)\n", thread_id, count);
	rcu_unregister_thread_mb();
	free(params);
}

void sigbus_handler(int sig, siginfo_t *info, void *ucontext)
{
	switch(sig)
	{
	case 7:
		printf("Got SIGBUS access at %p\n", info->si_addr);
		printf("Thread %i\n", info->si_pid);
		printf("My PID: %i\n", getpid());
		printf("offset: %lu\n", (unsigned long)info->si_addr - filestart);
		break;
	default:
		printf("Unknown signal %i\n", sig);
	}
	exit(-sig);
}

int main(int argc, char **argv)
{
	char *filename = argv[1];
	pthread_t threads[MAX_THREADS];
	struct thread_params *params[MAX_THREADS];
	void *input_mem;
	struct stat input_stat;
	unsigned int thread_id;
	void *retval;
	int pagesize = sysconf(_SC_PAGESIZE);
	struct sigaction sigbus_action;
	struct file_attr myfile;
	struct timespec start_tm, stop_tm;

	// install signal handler
	sigbus_action.sa_flags = SA_SIGINFO;
	sigbus_action.sa_sigaction = sigbus_handler;
	sigaction(SIGBUS, &sigbus_action, NULL);

	printf("App PID: %i\n", getpid());

	// main thread opens a file, and gets the length
	myfile.handle = open(filename, O_RDWR);
	if (myfile.handle == -1)
	{
		printf("Couldn't open the file %s for read-write\n", filename);
		return errno;
	}
	fstat(myfile.handle, &input_stat);
	myfile.length = input_stat.st_size;
	printf("File is %u bytes long\n", myfile.length);

	// main thread mmaps the entire file
	input_mem = mmap(NULL, myfile.length, PROT_READ, MAP_PRIVATE, myfile.handle, 0);
	printf("File mapped from %p to %p\n", input_mem, input_mem + myfile.length);
	filestart = (unsigned long)input_mem;

	// read value near the end of the file
	unsigned int data = *(unsigned int*)(input_mem + myfile.length - 8);
	printf("Data value is: %u\n", data);

	clock_gettime(CLOCK_REALTIME, &start_tm);

	// create threads which will read the file asynchronously
	for (thread_id=0; thread_id < MAX_THREADS; thread_id++)
	{
		params[thread_id] = malloc(sizeof(struct thread_params));
		params[thread_id]->thread_id = thread_id;
		params[thread_id]->fattr = &myfile;
		params[thread_id]->input_mem = input_mem;
		params[thread_id]->pagesize = pagesize;
		params[thread_id]->iterations = ITERATIONS;
		pthread_create(&threads[thread_id], NULL, read_thread_main, params[thread_id]);
	}

	// start shortening the file, one page at a time
	while (myfile.length > pagesize)
	{
		// assign the new length & sync
		myfile.length = 4000;
		urcu_mb_synchronize_rcu();

		// now we are safe to shorten the file
		printf("Shortening file to %u\n", myfile.length);
		if (ftruncate(myfile.handle, myfile.length) == -1)
			printf("Problem truncating file\n");
		//usleep(10);
	}

	// wait for all threads to finish
	for (thread_id=0; thread_id < MAX_THREADS; thread_id++)
		pthread_join(threads[thread_id], &retval);

	clock_gettime(CLOCK_REALTIME, &stop_tm);
	if (stop_tm.tv_nsec < start_tm.tv_nsec)
	{
		stop_tm.tv_nsec += 1000000000;
		stop_tm.tv_sec -= 1;
	}
	printf("Time: %lu.%09lu\n", stop_tm.tv_sec - start_tm.tv_sec, stop_tm.tv_nsec - start_tm.tv_nsec);
	printf("File read correctly in all cases!\n");

	return 0;
}
