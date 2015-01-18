#include "util.h"

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


const char* get_mime_type(char *filename){
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
  while(*p && --max) {
    *dest++ = *p++;
  }
  *dest = '\0';
}

