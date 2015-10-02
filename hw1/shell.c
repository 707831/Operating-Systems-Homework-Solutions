#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <termios.h>
#include <pwd.h>
#include <sys/wait.h>
#include <stdbool.h>
#include <fcntl.h>
#include <sys/stat.h>

#define INPUT_STRING_SIZE 80
#define MAX_FILENAME_SIZE 30
#define MAX_PATHNAME_SIZE 512
#define USRNAME_LEN 100
#define BUFSIZE 100
#define NUL '\0'

#include "io.h"
#include "parse.h"
#include "process.h"
#include "shell.h"

// global variables
extern char **environ;
process* first_process = NULL; //pointer to the first process that is launched

// helper functions
static void print_welcome( char *s, pid_t pid, pid_t ppid );
static void print_prompt( void );
static void signal_handler( int signum );


// supported built-ins
int cmd_quit(tok_t arg[]);
int cmd_help(tok_t arg[]);
int cmd_cd(tok_t arg[]);
int cmd_fg(tok_t arg[]);
int cmd_bg(tok_t arg[]);
int cmd_wait(tok_t arg[]);

// Command Lookup table 
typedef int cmd_fun_t (tok_t arg[]); // cmd functions take token array and return int

typedef struct fun_desc {
  cmd_fun_t *fun; 
  char *cmd; // name of the command
  char *doc; // documentation for the command
} fun_desc_t;

fun_desc_t cmd_table[] = {
  {cmd_help, "?", "show this help menu"},
  {cmd_quit, "quit", "quit the command shell"},
  {cmd_cd, "cd", "change working directory"},
  {cmd_wait, "wait", "waiting for child processes to finish"},
  {cmd_fg, "fg", "putting process in foreground"},
  {cmd_bg, "bg", "putting process in background"},
};

// change working directory
int cmd_cd(tok_t arg[]){

  char *dir_name = NULL; // directory we're going to change into

  // check if there are any arguments to cd
  if( arg[0] == NULL )
    // if not, change to the current user's home directory
    dir_name = getenv( "HOME" );  // set 'dir_name' to the home directory of the current user

  // deal with users' home directories
  else if( arg[0][0] == '~' ){ // if the argument starts with '~'
    
    if( strlen( arg[0] ) == 1 ) // if the argument is exactly '~'
      dir_name = getenv( "HOME" );  // set 'dir_name' to the home directory of the current user
    
    else { // we're dealing with something like "~user", so have to do more work
      // extract the user name, put it into 'usr'
      char usr[USRNAME_LEN];
      char *str = arg[0];
      ++ str; // ignore the leading '~'
      int i = 0;
      while ( *str != NUL )
	usr[i++] = *str++;
      usr[i] = NUL; // terminate with NUL
      // now extract the home directory of 'usr'
      struct passwd pwd;
      struct passwd *result;
      char *buf[BUFSIZE];
      int s = getpwnam_r( usr, &pwd, buf, BUFSIZE, &result );
      if( result == NULL ){ // check that 'getpwnam_r' succeeded
	if( s == 0){
	  printf( "User %s not found.\n", usr );
	}
	else {
	  errno = s;
	  perror( "getpwnam_r" );
	}
	return 1;
      } // if( result == NULL )

      // set 'dir_name' to the home directory of 'usr'
      dir_name = pwd.pw_dir;
      
    } // else
  } // else if( arg[0][0] == '~' )
    
  else // there is an argument and there's no tilda: go ahead and simply use arg[0]
    dir_name = arg[0];
  
  if( chdir( dir_name ) < 0 ) // change directory to 'dir_name'
    printf( "Couldn't execute cd\n" );
  return 1;
}

// move a job into the foreground
int cmd_fg(tok_t arg[]){
  process* p;
  if( arg[0] != NULL )
    p = get_process( atoi( arg[0] ) );
  else if( first_process != NULL )
    p = first_process->prev;
  if( p != NULL ){
    put_in_foreground( p, true );
  }
  return 1;
}

// move a job into the background
int cmd_bg(tok_t arg[]){
  process* p;
  if( arg[0] != NULL ){
    p = get_process( atoi( arg[0] ) );
  }
  else if( first_process != NULL ){
    p = first_process->prev;
  }
  printf( "%d\n", p->pid );
  if( p != NULL ){
    put_in_background( p, true );
  }
  return 1;
}

int cmd_wait(tok_t arg[]) {
  pid_t pid;
  while( (pid = waitpid(-1, NULL, 0)) != 0 ) {
    if (errno == ECHILD) {
      break;
    }
  }
  return 1;
}

// quit
int cmd_quit(tok_t arg[]) {
  printf( "Bye\n" );
  exit(0);
  return 1;
}

// help
int cmd_help(tok_t arg[]) {
  for( unsigned int i = 0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); ++i ) {
    printf( "%s - %s\n",cmd_table[i].cmd, cmd_table[i].doc );
  }
  return 1;
}

// is 'cmd' in 'cmd_table'
// if yes, return its index
// if no, return -1
int lookup( char *cmd ) {
  for( unsigned int i = 0; i < (sizeof(cmd_table)/sizeof(fun_desc_t)); ++i ) 
    if( cmd && (strcmp( cmd_table[i].cmd, cmd ) == 0) )
      return i;

  return -1;
}

void
init_shell( void ) {

  // Check if we are running interactively
  shell_terminal = STDIN_FILENO;
  shell_is_interactive = isatty( shell_terminal );

  if( shell_is_interactive ){

    // force into foreground
    while( tcgetpgrp( shell_terminal ) != ( shell_pgid = getpgrp() ) )
      kill( - shell_pgid, SIGTTIN);

    // put shell in its own process group
    shell_pgid = getpid();
    if( setpgid( shell_pgid, shell_pgid ) < 0 ){
      perror( "Couldn't put the shell in its own process group" );
      exit( EXIT_FAILURE );
    }

    // take control of the terminal
    tcsetpgrp(shell_terminal, shell_pgid);
    tcgetattr(shell_terminal, &shell_tmodes);
  }

  // ignore signals
  SET_SIGNALS( SIG_IGN );
  
}

// add a process to our process list
void
add_process( process* p ) {
  // processes are arranged in a doulby-linked list;
  // 'fist_process' is the head of the list;
  // INVARIANT:
  // first_process->prev is the tail;
  // tail's 'next' is pointing to 'first_process' 
  if( first_process == NULL ){ // insertion into an empty list
    first_process = p;
    p->next = p;
    p->prev = p;
  }
  else {
    first_process->prev->next = p; // insert at the end of a non-empty list
    p->prev = first_process->prev;
    first_process->prev = p;
    p->next = first_process;
  }
}

// create a process given 'inputString' from stdin
// if a process is created, add the process to the list of processes
// and return a point to it
// if 'inputString' spesifies a built-in command,
// execute the command and return NULL
// if can't open file for input/output redirection, return NULL
process*
create_process( char* inputString ){

  // first, check if 'inputString' contains '>' or '<'
  // if so, redirect input/output
  // this has to be done before we parse the string;
  // parsing will treat these characters as separators

  int outfile = STDOUT_FILENO; // by default, output goes to STDOUT
  // check if we need to redirect output for this processs
  char *out_redirect = strstr( inputString, ">" );
  if( out_redirect != NULL ){
    // redirect output
    *out_redirect = NUL; // prune 'inputString'
    char *output = out_redirect + 1; // cut off the portion with the file name
    // open a file for writing
    char *file = getToks(output)[0];
    outfile = open( file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IROTH | S_IWOTH);
     // check that the file was opened
     if( outfile < 0 ) {
       perror( file );
       return NULL;
     }
  }

  int infile = STDIN_FILENO; // by default, input comes from STDIN
  // check if we need to redirect input for this processs
  char *in_redirect = strstr(inputString, "<");
  if( in_redirect != NULL ){
    // redirect input
    *in_redirect = NUL; // prune 'inputString'
    char *input = in_redirect + 1; // cut off the portion with the file name
    // open a file for reading
    char *file = getToks(input)[0];
    infile = open( file, O_RDONLY );
    // check that the file was opened
    if( infile < 0 ){
      perror( file );
      return NULL;
    }
  }

  // check for '&' at the end;
  // if it's present, the process should run in the background
  bool is_background = false;
  char* bg = strchr(inputString, '&');
  if( bg != NULL ){
    is_background = true;
    bg = NUL; // take '&' out
  }

  // now, we can tokenize (possibly, truncated) 'inputString'
  tok_t *t = getToks( inputString );   // an array of tokens parsed from input
  int fundex = lookup(t[0]);
  if (fundex >= 0) { // is the first token a shell literal?
    // execute a built-in command
    cmd_table[fundex].fun(&t[1]);
    return NULL;
  }
  else { // no, treat it as a process to be executed
    process* p = (process*) calloc( 1, sizeof(process) );
    p->stdin = infile;
    p->stdout = outfile;
    p->stderr = STDERR_FILENO;
    p->background = is_background;
    p->next = p->prev = NULL;
    
    int argc = 0;
    for( tok_t *tok = t; *tok != NUL; ++tok )
      ++argc;
    p->argc = argc;
    p->argv = t;
    
    add_process( p );
    return p;
  }
}

int
shell( int argc, char *argv[] ) {

  init_shell(); // initialize the shell
  
  pid_t pid = getpid();	       // get current processe's PID
  pid_t ppid = getppid();      // get parent's PID 
  pid_t cpid, tcpid, cpgid;

  print_welcome( argv[0], pid, ppid );
  print_prompt();
  char *s = malloc(INPUT_STRING_SIZE + 1); // user input string
  
  while( (s = freadln(stdin)) ){ // read in the input string
    process *p = create_process( s );
    if( p != NULL ){ // do we need to run a newly created process?
      cpid = fork(); // fork off a child
      if( cpid == 0 ) { // child
	p->pid = getpid();
	launch_process( p );
      } // if( cpid == 0 )
      else if( cpid > 0 ){ // parent
	p->pid = cpid;
	if( shell_is_interactive ){ // are we running shell from a terminal?
	  // put 'p' into its own process group
	  // since our shell is not supporting pipes, we treat
	  // each process by itself as a 'job' to be done
	  if( setpgid( cpid, cpid ) < 0 ) 
	    printf( "Failed to put the child into its own process group.\n" );
	  if( p->background )
	    put_in_background( p, false );
	  else
	    put_in_foreground( p, false );
	} // if( shell_is_interactive )
	else
	  wait_for_process( p );
      } // else if( cpid > 0 )
      else // fork() failed
	printf( "fork() failed\n" );
    } // if( p != NULL )
    print_prompt();
  } // while

  return EXIT_SUCCESS;
} // shell


// print a welome mesage
static void
print_welcome( char *s, pid_t pid, pid_t ppid ){
  printf("%s running as PID %ld under %ld\n", s, (long) pid, (long) ppid);
}

// print the prompt
static void
print_prompt( void ){
  // prompt has the form "lineNum pathname: "
  static int lineNum = 0; 
  static char pathname[MAX_PATHNAME_SIZE]; 
  getcwd( pathname, MAX_PATHNAME_SIZE );
  if( pathname == NULL ){
    perror( "getcwd error" );
    exit( EXIT_FAILURE );
  }
  fprintf(stdout, "%d %s: ", lineNum++, pathname);
}
