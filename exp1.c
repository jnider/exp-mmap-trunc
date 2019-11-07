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

#define MAX_THREADS 20
#define ITERATIONS 1000

static unsigned long filestart;

struct thread_params
{
	int thread_id; // Index of this thread
	int input_file; // file handle to 'large file'
	void *input_mem; // address of memory-mapped file
	unsigned int input_length; // length of the memory mapped file
	unsigned int pagesize; // page size used by the OS
	unsigned int iterations; // how many times to read through the file
};

// Thread B: truncates the file without synchronizing with main thread
void *read_thread_main(void *arg)
{
	unsigned long count = 0;
	struct thread_params *params = (struct thread_params*)arg;
	int input_file = params->input_file;
	int thread_id = params->thread_id;
	void *input_mem = params->input_mem;
	unsigned int length = params->input_length;
	unsigned int pagesize = params->pagesize;
	unsigned int iterations = params->iterations;

	printf("Thread %i starting\n", thread_id);

	// read data from the file
	while (iterations--)
	{
		printf("#%i: Iteration %u\n", thread_id, iterations);
		for(void* addr = input_mem; addr < (input_mem + length); addr+=sizeof(unsigned int))
		{
			unsigned int data = *(unsigned int*)addr;
			if (((unsigned long)addr % pagesize) == 0)
				printf("Thread %i: %p = %u\n", thread_id, addr, data);
			*(unsigned int*)addr++;
			count++;
		}
	}

	printf("Thread %i done (count=%lu)\n", thread_id, count);
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
	int input_file;
	void *input_mem;
	struct stat input_stat;
	unsigned int length;
	unsigned int thread_id;
	void *retval;
	int pagesize = sysconf(_SC_PAGESIZE);
	struct sigaction sigbus_action;
	struct timespec start_tm, stop_tm;

	// install signal handler
	sigbus_action.sa_flags = SA_SIGINFO;
	sigbus_action.sa_sigaction = sigbus_handler;
	sigaction(SIGBUS, &sigbus_action, NULL);

	printf("App PID: %i\n", getpid());

	// main thread opens a file, and gets the length
	input_file = open(filename, O_RDWR);
	if (input_file == -1)
	{
		printf("Couldn't open the file %s for read-write\n", filename);
		return errno;
	}
	fstat(input_file, &input_stat);
	length = input_stat.st_size;
	printf("File is %u bytes long\n", length);

	// main thread mmaps the entire file
	input_mem = mmap(NULL, length, PROT_READ, MAP_PRIVATE, input_file, 0);
	printf("File mapped from %p to %p\n", input_mem, input_mem + length);
	filestart = (unsigned long)input_mem;

	// read value near the end of the file
	unsigned int data = *(unsigned int*)(input_mem + length - 8);
	printf("Data value is: %u\n", data);

	clock_gettime(CLOCK_REALTIME, &start_tm);

	// create threads which will read the file asynchronously
	for (thread_id=0; thread_id < MAX_THREADS; thread_id++)
	{
		struct thread_params *params = malloc(sizeof(struct thread_params));
		params->thread_id = thread_id;
		params->input_file = input_file;
		params->input_length = length;
		params->input_mem = input_mem;
		params->pagesize = pagesize;
		params->iterations = ITERATIONS;
		pthread_create(&threads[thread_id], NULL, read_thread_main, params);
	}

	// start shortening the file, one page at a time
	while (length > pagesize)
	{
		length = 500;
		printf("Shortening file to %u\n", length);
		if (ftruncate(input_file, length) == -1)
			printf("Problem truncating file\n");
		sleep(1);
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
