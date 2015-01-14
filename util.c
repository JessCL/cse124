#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
  char filename[512];
  off_t offset;             
  size_t end;
} http_request;

typedef struct {
  const char *extension;
  const char *mime_type;
} mime_map;

mime_map meme_types [] = {
  {".css", "text/css"},
  {".gif", "image/gif"},
  {".htm", "text/html"},
  {".html", "text/html"},
  {".jpeg", "image/jpeg"},
  {".jpg", "image/jpeg"},
  {".ico", "image/x-icon"},
  {".js", "application/javascript"},
  {".pdf", "application/pdf"},
  {".mp4", "video/mp4"},
  {".png", "image/png"},
  {".svg", "image/svg+xml"},
  {".xml", "text/xml"},
  {NULL, NULL},
};

char *default_mime_type = "text/plain";

void format_size(char* buf, struct stat *stat){
  if(S_ISDIR(stat->st_mode)){
    sprintf(buf, "%s", "[DIR]");
  } else {
    off_t size = stat->st_size;
    if(size < 1024){
      sprintf(buf, "%lu", size);
    } else if (size < 1024 * 1024){
      sprintf(buf, "%.1fK", (double)size / 1024);
    } else if (size < 1024 * 1024 * 1024){
      sprintf(buf, "%.1fM", (double)size / 1024 / 1024);
    } else {
      sprintf(buf, "%.1fG", (double)size / 1024 / 1024 / 1024);
    }
  }
}

static const char* get_mime_type(char *filename){
  char *dot = strrchr(filename, '.');
  if(dot){ // strrchar Locate last occurrence of character in string
    mime_map *map = meme_types;
    while(map->extension){
      if(strcmp(map->extension, dot) == 0){
        return map->mime_type;
      }
      map++;
    }
  }
  return default_mime_type;
}

void url_decode(char* src, char* dest, int max) {
  char *p = src;
  char code[3] = { 0 };
  while(*p && --max) {
    if(*p == '%') {
      memcpy(code, ++p, 2);
      *dest++ = (char)strtoul(code, NULL, 16);
      p += 2;
    } else {
      *dest++ = *p++;
    }
  }
  *dest = '\0';
}

