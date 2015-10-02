#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "parse.h"

/*          Get tokens from a line of characters */
/* Return:  new array of pointers to tokens */
/* Effects: token separators in line are replaced with NULL */
/* Storage: Resulting token array points into original line */


// consider space, newline, and ':' to be separators
#define TOKseparators " \n:><&"

// extract tokens from 'line';
// return an array of tokens; the array is allocated on the heap
tok_t *
getToks( char *line ) {
  char *c = NULL;

  // create an 'empty' array of tokens (set all to NULL)
  tok_t *toks = malloc( MAXTOKS * sizeof(tok_t) );
  for (int i = 0; i < MAXTOKS; ++i)
    toks[i] = NULL;

  c = strtok( line, TOKseparators );	 // start a tokenizer on 'line'
  for (int i = 0; c && (i < MAXTOKS); ++i) {
    toks[i] = c;
    c = strtok( NULL, TOKseparators );	// scan for the next token
  }
  
  return toks;
}

// deallocate the array used for tokens
void
freeToks( tok_t *toks ) {
  free( toks );
}

void
fprintTok( FILE *ofile, tok_t *t ) {
  for (int i = 0; i<MAXTOKS && t[i]; i++) {
    fprintf( ofile, "%s ", t[i]);
  }
  fprintf(ofile,"\n");
}

/* Locate special processing character */
int
isDirectTok( tok_t *t, char *R ) {
  for (int i = 0; i < MAXTOKS-1 && t[i]; i++) {
    if (strncmp(t[i],R,1) == 0) return i;
  }
  return 0;
}
