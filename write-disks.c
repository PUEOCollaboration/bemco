#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>


#define blksiz 8*4096*4096
#define nblks 512

volatile int quit = 0;
static void * writer_thread(void * arg)
{

  char * where = (char*) arg;


  char * fname = NULL;
  asprintf(&fname,"%s/writer.dat", where);

  char * buf = malloc(blksiz);
  for (int i = 0; i < blksiz; i++)
  {
    buf[i] = (i + i*i ) & 0xff;
  }

  while (!quit)
  {
    FILE * f  = fopen(fname,"w");
    int fd = fileno(f);
    struct timespec begin;
    struct timespec end;

    if (!f)
    {
      fprintf(stderr,"Could not open %s for writing\n", fname);
      break;
    }

    for (int i  = 0; i < nblks; i++)
    {
      clock_gettime(CLOCK_MONOTONIC, &begin);
      write(fd,buf,blksiz);
      fflush(f);
      fsync(fd);
      clock_gettime(CLOCK_MONOTONIC, &end);
      double elapsed = end.tv_sec - begin.tv_sec + 1e-9 * (end.tv_nsec - begin.tv_nsec);
      printf("Wrote block to %s in %f seconds (%f MiB/s)\n", fname, elapsed, blksiz / elapsed / (1024.*1024));
      sleep(1);
      if (quit) break;
    }

    fclose(f);
  }

  free(fname);
  free(buf);
  return NULL;
}


void handler(int sig)
{
  quit = 1;
}


int main( int nargs, char ** args)
{


  printf("%d\n", nargs);
  pthread_t * threads = calloc(nargs-1,sizeof(pthread_t));

  sigset_t signal_set;
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGINT);
  sigaddset(&signal_set, SIGQUIT);
  pthread_sigmask(SIG_BLOCK, &signal_set, NULL);


  for (int i = 1; i < nargs; i++)
  {
    pthread_create(&threads[i-1], NULL, writer_thread, args[i]);
  }

  pthread_sigmask(SIG_UNBLOCK, &signal_set, NULL);
  signal(SIGINT,handler);

  for (int i = 0; i < nargs-1; i++)
  {
    pthread_join(threads[i], NULL);
  }
}

