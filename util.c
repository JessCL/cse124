#include "util.h"
#include "DieWithMessage.h"

mime_map meme_types [] = {
  {".css", "text/css"},
  {".html", "text/html"},
  {".jpeg", "image/jpeg"},
  {".jpg", "image/jpeg"},
  {".js", "application/javascript"},
  {".png", "image/png"},
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
