#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <termios.h>
#include <string.h>
#include "process.h"
#include "shell.h"

#define NUL '\0'

extern char **environ;

static void mark_process_status( process* p, int status );
static bool process_is_stopped( process* p );
static bool process_is_completed( process* p );

static int exec( char *file, char **argv );
static int index( char *str, char ch);

// 'exec' a new process
void
launch_process( process *p ) {

  // re-enable signals for the process
  SET_SIGNALS(SIG_DFL);
  dup2( p->stdin, STDIN_FILENO ); // set up input
  dup2( p->stdout, STDOUT_FILENO ); // set up output
  exec( p->argv[0], p->argv); // our own function
  // if we're here, execve failed
  perror ( "exec" ); // print an error message
  exit( EXIT_FAILURE ); // exit the shell

}

// function to execute the command 'file' using the current
// environment list
static int
exec( char *file, char **argv ){

  // If the file name specifies a path, don't search for it on the search path;
  // just try to execute it
  if( index(file, '/') != -1 ) {
    execve( file, argv, environ );
    return -1;
  }

  // the name doesn't specify a path; do a search on the search path
  
  char *path = getenv("PATH"); // get the search path
  int nameLength = strlen(file);

  // maximum size of the fully qualified name of the executable file
  #define MAX_NAME_SIZE 1000
  char fullName[MAX_NAME_SIZE+1]; // fully qualified name;
                                  // +1 will take care of the terminating NUL byte
  int size;  // size of a portion of the path
  bool noAccess = false; // flags whether we have permission to execute 'file'
  char *first, *last;

  // generate the next file name to try
  for( first = path; ; first = last+1 ) {

    // extract the next portion of the path ... 
    for( last = first; (*last != NUL) && (*last != ':'); ++last ) 
      ; 
    size = last - first; // ... and compute its size

    // check that the fully qualified name of the file
    // is not goint to exceed MAX_NAME_SIZE
    // need '+1' as we might need to append '\' to the portion of the path
    if( (size + nameLength + 1) > MAX_NAME_SIZE )
      // the fully qualified name would exceed the maxaimal length allowed
      // so, it can't be it
      continue;
    
    strncpy( fullName, first, size );
    // if the portion didn't end with '/', append it
    if( last[-1] != '/' )
      fullName[size++] = '/';

    // append the name 'file' to the portion of the path we have extracted
    // as 'file' is NUL-terminated, the result is NUL-terminated
    strcpy( fullName + size, file );

    // try to execute ...
    execve(fullName, argv, environ);
    // ... and check for errors
    if (errno == EACCES) // we don't have permission to execute 'fullName'
      // set the flag, but don't give up:
      // we might find the file with right permissions elsewhere on the path
      noAccess = true; 
                      
    else 
      if( errno != ENOENT )  // if the execution failed for any reason other than
	// the non-existence of the fully qualified name we cooked up
	// or the wrong permissions (see above), stop and return 
	break;

    if (*last == NUL) {
      // We've hit the end of the path: we're done.  If there existed
      // a file by the right name along the search path, but its
      // permissions were wrong, set errno accordingly and
      // return. Otherwise, return whatever we just got back.
      
      if( noAccess )
	errno = EACCES;
      break;
      
    } // if (*last == NUL)
  }
  return -1;
} // exec


// return an index of 'ch' in 'str'
// if not found, return -1
int
index( char *str, char ch ){
  for( int i = 0;  *str != NUL ; ++i)
    if( *str++ == ch )
      return i;
  return -1;
}

// put the job consiting of 'p' in the foreground
// if 'resume' is true, the process has to resume
// after having been stopped by a signal
void
put_in_foreground( process *p, bool resume ){
  // give the control of the terminal to 'p'
  if( tcsetpgrp( shell_terminal, p->pid ) < 0 )
    printf( "Failed to set the process group of the terminal" );

  // if necessary, send the job a continue signal
  if( resume ){
    if( !p->completed )
      p->stopped = false;

    if( kill ( - p->pid, SIGCONT ) < 0 )
      perror ("kill (SIGCONT)");
  }

  // wait for the process to finish
  wait_for_process( p );
  
  // give the control of the terminal back to shell
  
  if( tcsetpgrp (shell_terminal, shell_pgid) < 0 )
    printf( "Failed to set the process group of the terminal to shell\n" );
  // restore shell's terminal modes
  if( tcgetattr (shell_terminal, &p->tmodes)  < 0 )
    printf( "Failed to retrieve terminal attributes" );
  if( tcsetattr (shell_terminal, TCSADRAIN, &shell_tmodes) < 0 )
    printf( "Failed to set terminal attributes" );
}

void
put_in_background( process *p, bool resume ){

  // if necessary, send the job a continue signal
  if( resume ){
    if( !p->completed )
      p->stopped = false;

    if( kill ( - p->pid, SIGCONT ) < 0 )
      perror ("kill (SIGCONT)");
  }

}

// wait for a process to complete or be killed by a signal
// and record its return status
void
wait_for_process( process *p ){
  int status;
  do {
    // want to know when a child terminates or is stopped by a signal
    waitpid( p->pid, &status, WUNTRACED );
    mark_process_status( p, status );
  } while( !process_is_stopped( p ) && !process_is_completed( p ) );
}

// has 'p' completed?
bool
process_is_completed( process* p ){
  if( p->completed )
    return true;
  else
    return false;
}

// has 'p' completed or been stopped?
bool
process_is_stopped( process* p ){
  if( p->completed || p->stopped )
    return true;
  else
    return false;
}

void
mark_process_status( process* p, int status ){
  p->status = status;
  if( WIFSTOPPED( status ) != 0 ) // was 'p' stopped by a signal?
    p->stopped = true;
  else {
    p->completed = true;
    if( WIFSIGNALED(status) ) // was 'p' killed by a signal?
      // print a massage infoming us what signall killed the process
      fprintf( stderr, "[%ld] Terminated by signal %d. \n", (long) p->pid, WTERMSIG (p->status));
  } // else
}

process*
get_process( pid_t pid ){
  process* t = first_process;
  while( t->next != first_process ){
    if( t->pid == pid ){
      return t;
    }
  }
  if( t->pid == pid ){
    return t;
  }
  return NULL;
}
