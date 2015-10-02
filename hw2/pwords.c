#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "errors.h"

// a dictionary node
typedef struct dict {
  char *word;
  int count;
  struct dict *next;
} dict_t;

// a dictionary node protected by a mutex
typedef struct syncdict {
  pthread_mutex_t lock;
  dict_t *wd;
} syncdict_t;

// arguments to pass to each thread
typedef struct targ {
  long tid;          // thread 'id'
  FILE *infile;      // file to read from
  syncdict_t *swd;   // dictionary to insert into
} targ_t;

// mutex to protect allocation of memory
pthread_mutex_t mm_mutex = PTHREAD_MUTEX_INITIALIZER;

// mutex to protect non-threadsafe file I/O
pthread_mutex_t gw_mutex = PTHREAD_MUTEX_INITIALIZER;


syncdict_t *new_syncdict( void );
void *mymalloc( size_t size );
void *words( void *t );
int get_word( char *buf, int n, FILE *infile);
void insert_word( syncdict_t *sd, char *word );
dict_t *make_dict(char *word);
char *make_word( char *word );
void print_dict(dict_t *d);

#define NTHREADS 4

int
main( int argc, char *argv[] ) {
  
  pthread_t threads[NTHREADS]; // thread ids
  targ_t targs[NTHREADS];      // arguments to pass to threads
  void *ret = NULL;            // return value from threads
  int status = 0;
  
  FILE *infile = stdin;        // input file
  
  syncdict_t *swd = new_syncdict( );
  
  if( argc >= 2 ) 
    infile = fopen( argv[1], "r" );
  if( !infile ) {
    printf( "Unable to open %s\n", argv[1] );
    exit( EXIT_FAILURE );
  }

  // create threads
  for( long tid = 0; tid < NTHREADS; ++tid ) {
    targs[tid].tid = tid;
    targs[tid].infile = infile;
    targs[tid].swd = swd;
    if( (status = pthread_create( &threads[tid], NULL, words, (void *) &targs[tid] )) != 0 )
      err_abort( status, "create thread" );
    printf( "created thread %ld\n", tid );
  } // for
  
  for( long tid = 0; tid < NTHREADS; ++tid) {
    if( (status = pthread_join( threads[tid], &ret )) != 0 )
      err_abort( status, "join thread" );
    printf("joined thread %ld (%d)\n", tid, *((int *) ret));
  }
  
  print_dict( swd->wd );
  fclose( infile );
  pthread_exit( NULL );
}

// thread-safe allocation of memory from the heap
void *
mymalloc( size_t size ) {
  void *r;
  pthread_mutex_lock( &mm_mutex );
  r = malloc( size );
  pthread_mutex_unlock( &mm_mutex );
  return r;
}

// initialize a dictionary
syncdict_t *
new_syncdict( void ) {
  syncdict_t *r = (syncdict_t *) mymalloc( sizeof( syncdict_t ) );
  pthread_mutex_init( &r->lock, NULL ); 
  r->wd = NULL;
  return r;
}

#define MAXWORD 1024
void *
words( void *t ) {
  targ_t *tp = (targ_t *) t;
  long tid = tp->tid;
  FILE *infile = tp->infile;
  syncdict_t *sd = tp->swd;
  int *ret = mymalloc( sizeof(int) );
  char wordbuf[MAXWORD+1];
  printf("Words %ld\n", tid);
  while( get_word( wordbuf, MAXWORD, infile ) ) {
    printf( "T%ld: got %s\n", tid, wordbuf );
    insert_word( sd, wordbuf ); // add to dictionary
  }
  pthread_exit( ret );
}

int
get_word( char *buf, int n, FILE *infile) {
  int inword = 0;
  int c;
  pthread_mutex_lock( &gw_mutex );
  while( (c = fgetc( infile )) != EOF && inword < n) {
    if( inword && !isalpha(c) ) {
      buf[inword] = '\0';	// terminate the word string
      pthread_mutex_unlock( &gw_mutex );
      return 1;                 // we've read a word
    } 
    if( isalpha(c) ) 
      buf[inword++] = c;
  }

  if( inword > 0 ){ // did we exit the loop because of overflow?
    buf[inword] = '\0';	// terminate the word string
    pthread_mutex_unlock( &gw_mutex );
    return 1;           // we've read a (truncated) word
  }
  pthread_mutex_unlock( &gw_mutex );
  return 0;			// no more words
}

void
insert_word( syncdict_t *sd, char *word ) {

  //   Insert word into dict or increment count if already there
  //   return pointer to the updated dict
  
  dict_t *nd = NULL;
  dict_t *pd = NULL;		// prior to insertion point
  dict_t *di = NULL;		// following insertion point

  pthread_mutex_lock( &sd->lock );
  di = sd->wd;
  // Search down the list to find if the word is present or point of insertion
  while( di && ( strcmp(word, di->word ) >= 0 ) ) { 
    if( strcmp( word, di->word ) == 0) { 
      di->count++;		// increment count
      pthread_mutex_unlock( &sd->lock );
      return;			// head remains unchanged
    }
    pd = di;			// advance ptr pair
    di = di->next;
  }
  nd = make_dict( word );	// not found, make entry 
  nd->next = di;		
  if( pd ) {
    pd->next = nd;
    pthread_mutex_unlock( &sd->lock );
    return;			// insert beyond head
  }
  sd->wd = nd;			// update head
  pthread_mutex_unlock( &sd->lock );
  return;
}

dict_t *
make_dict( char *word ) {
  dict_t *nd = (dict_t *) mymalloc( sizeof(dict_t) );
  nd->word = make_word( word );
  nd->count = 1;
  nd->next = NULL;
  return nd;
}

char *
make_word( char *word ) {
  return strcpy( mymalloc( strlen( word ) + 1 ), word );
}

void
print_dict( dict_t *d ) {
  while( d ) {
    printf("[%d] %s\n", d->count, d->word);
    d = d->next;
  }
}
