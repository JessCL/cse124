#ifndef _util_h
#define _util_h

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "rio.h"

#define LISTENQ  1024  /* second argument to listen() */
#define MAXLINE 1024

typedef struct {
  char filename[MAXLINE];
  off_t offset;             
  size_t end;
  char type[MAXLINE];
  char method[MAXLINE];
  int assigned;
} http_request;

typedef struct {
  const char *extension;
  const char *mime_type;
} mime_map;

const char* get_mime_type(char *filename);

#endif
