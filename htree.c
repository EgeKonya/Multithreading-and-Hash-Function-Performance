#include <stdbool.h>
#include <stdio.h>
#include <string.h>     
#include <stdlib.h>   
#include <stdint.h>  
#include <inttypes.h>  
#include <errno.h>     // for EINTR
#include <fcntl.h>     
#include <unistd.h>    
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "common_threads.h"
#include "common.h"


// Print out the usage of the program and exit.
void *tree(void *arg);
void Usage(char*);
uint32_t jenkins_one_at_a_time_hash(const uint8_t* , uint64_t, uint64_t start);

// block size
#define BSIZE 4096
uint8_t *map;

typedef struct passThread {               //Struct to pass to threads
	int numThreads;                         //How many threads must be created
	int myNum;                              //What number thread current thread is
  uint64_t blockPerThread;                //How many blocks each thread receives
} pt;                                     //The name of the struct

int 
main(int argc, char** argv) 
{
  int32_t fd;
  uint32_t nblocks;

  // input checking 
  if (argc != 3)
    Usage(argv[0]);

  // open input file
  fd = open(argv[1], O_RDWR);
  if (fd == -1) {
    perror("open failed");
    exit(EXIT_FAILURE);
  }
  // use fstat to get file size
  // calculate nblocks
  int num_threads = atoi(argv[2]); 
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    perror("fstat");
    exit(EXIT_FAILURE);
  }
  nblocks = (sb.st_size / BSIZE) / num_threads;     //Finds how many blocks each thread must hash

  printf("num Threads = %d\n", num_threads);
  printf("Blocks per Thread = %u\n", nblocks);

  double start = GetTime();                         //Start timing

  // calculate hash value of the input file
  
  map = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);    //Map the entire file to memory
  if (map == MAP_FAILED) {                                        //Check if mmap has failed
    perror("map failed");
    exit(EXIT_FAILURE);
  }
  uint32_t *hash; 

  pt record;                                        //Create the first struct to pass to thread 0
	record.numThreads = num_threads;
	record.myNum = 0;
  record.blockPerThread = nblocks;
  
  pthread_t p1;
	Pthread_create(&p1, NULL, tree, &record);         //Create the root thread of the tree
	Pthread_join(p1, (void**)&hash);                  //Join with finished root and store return value into hash

  double end = GetTime();                           //End timing
  printf("hash = %u\n", *hash);                     //Print out all results
  printf("time taken = %f \n", (end - start));      
  close(fd);                                        //Close open file
  return EXIT_SUCCESS;
}

uint32_t 
jenkins_one_at_a_time_hash(const uint8_t* key, uint64_t length, uint64_t start)       //Hash function provided, 'start' argument added
{
  uint64_t i = 0;
  uint32_t hash = 0;

  while (i != length) {
    hash += key[i + start];     //start defines where on the key to begin hashing
    hash += hash << 10;
    hash ^= hash >> 6;
    i++;
  }
  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;
  return hash;
}

void* 
tree(void* arg) 
{
  uint32_t *hash  = malloc(sizeof(uint32_t));     //hash must be allocated on the heap so address remains when the thread closes
  uint32_t *hashReturnLeft;                       //Helper variables with hash process
  uint32_t *hashReturnRight;
  bool Rcat = false;
  bool Lcat = false;
  char catHash[100];
  char LcatHash[100];
  char RcatHash[100];
	pt threadRecord = *((pt*)arg);                          //Creates struct and defines it as value passed by Pthread_create 
	int childNumLeft = (2 * threadRecord.myNum) + 1;        //Calculates the thread number of my children
	int childNumRight = (2 * threadRecord.myNum) + 2;
	pthread_t ptest1;
	pthread_t ptest2;
  uint64_t start = threadRecord.myNum*threadRecord.blockPerThread*BSIZE;    //Where this threads assigned blocks to hash begin
  uint64_t len = threadRecord.blockPerThread*BSIZE;                         //How many bytes this thread must hash
  
  *hash = jenkins_one_at_a_time_hash(map, len, start);    //Hash assigned blocks
	if (childNumLeft < threadRecord.numThreads) {                 //If current thread needs a left child -->
		pt threadLeft;                                              //Create new struct to pass and assign appropriate values
		threadLeft.blockPerThread = threadRecord.blockPerThread;
    threadLeft.numThreads = threadRecord.numThreads;
		threadLeft.myNum = childNumLeft;
		Pthread_create(&ptest1, NULL, tree, &threadLeft);           //Create new thread and pass new struct
	}
	if (childNumRight < threadRecord.numThreads) {                //If current thread needs a right child -->
		pt threadRight;                                             //Create new struct to pass and assign appropriate values
		threadRight.blockPerThread = threadRecord.blockPerThread;
    threadRight.numThreads = threadRecord.numThreads;
		threadRight.myNum = childNumRight;
		Pthread_create(&ptest2, NULL, tree, &threadRight);          //Create new thread and pass new struct
	}
	if (childNumLeft < threadRecord.numThreads) {           //If current thread has a left child -->
    Lcat = true;                                          //Set flag for and wait for it to finish
		Pthread_join(ptest1, (void**)&hashReturnLeft);        //Store return value into hashReturnLeft
  }
	if (childNumRight < threadRecord.numThreads) {          //If current thread has a right child -->
		Rcat = true;                                          //Set flag for and wait for it to finish
    Pthread_join(ptest2, (void**)&hashReturnRight);       //Store return value into hashReturnRight
  } 

  if(Lcat) {                                      //If LCat flag is true -->
    sprintf(catHash, "%u", *hash);                //Convert hash values into strings
    sprintf(LcatHash, "%u", *hashReturnLeft);
    strcat(catHash, LcatHash);                    //Concatenate strings togethor
    free(hashReturnLeft);                         //Free unused memeory from heap
  }
  if(Rcat) {                                      //If RCat flag is true -->
    sprintf(RcatHash, "%u", *hashReturnRight);    //Convert hash value into string
    strcat(catHash, RcatHash);                    //Concatenate strings togethor
    free(hashReturnRight);                        //Free unused memeory from heap
  }
  if (Lcat || Rcat) {                                                                 //If either flag was set -->
    *hash = jenkins_one_at_a_time_hash((uint8_t*)catHash, strlen(catHash), 0);        //Hash the entire concatenated string
  } 

  return (void*) hash;                             //Return hash value calculated
}

void 
Usage(char* s) 
{
  fprintf(stderr, "Usage: %s filename num_threads \n", s);
  exit(EXIT_FAILURE);
}
