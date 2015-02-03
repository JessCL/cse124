#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include "rio.h"
#include "util.h"

void DieWithUserMessage(const char *msg, const char *detail);
// Handle error with sys msg
void DieWithSystemMessage(const char *msg);

#define URI_SZ      1024
#define METHOD_SZ   5
#define VERSION_SZ  10
#define REQUEST_SZ 1024
#define TRUE 1
#define FALSE 0
#define DEBUG FALSE

pthread_cond_t timeout_cond;
pthread_mutex_t timeout_mutex;
int first_flag;

int getSO_ERROR(int fd) {
   int err = 1;
   socklen_t len = sizeof err;
   if (-1 == getsockopt(fd, SOL_SOCKET, SO_ERROR, (char *)&err, &len))
      //FatalError("getSO_ERROR");
   if (err)
      errno = err;              // set errno to the socket SO_ERROR
   return err;
}

void closeSocket(int fd) {      // *not* the Windows closesocket()
   if (fd >= 0) {
      getSO_ERROR(fd); // first clear any errors, which can cause close to fail
      shutdown(fd, SHUT_RDWR); // secondly, terminate the 'reliable' delivery
   }
}


char* getFileName(char * dirPath, char * fileType)
{
  DIR *dir=opendir(dirPath);
  if(dir==NULL)
    return NULL;
  chdir(dirPath);
  struct dirent *mydirent;
  char * filename;
  while((mydirent=readdir(dir)) != NULL)
  {
    int size = strlen(mydirent->d_name);
    if(strcmp( ( mydirent->d_name + (size - strlen(fileType)) ) , fileType) != 0)//ignore other files or dir 
      continue;
   
    struct stat st;
    stat(mydirent->d_name,&st);
    if(!S_ISDIR(st.st_mode))  //if it is not a child directory, return the file name
    {
      filename = (char*)malloc(256);
      strcpy(filename, mydirent->d_name);//make one copy for return
      //Here we assume that there is only one ".htaccess" file
      closedir(dir);
      return filename;
    }
  }
  return NULL;
}

/* Handle access permission according to .htaccess files
 * Parameters: client socket address structure, the file name(include path) the client want to access
 * Return:int {0,1}, 0 represents deny and 1 represents allow.
 */
int handle_htaccess(struct sockaddr_in *clientaddr, char* filename){
  char htaccess_filename[MAXLINE];
  strcpy(htaccess_filename, filename);
  char *slash = strrchr(htaccess_filename, '/');
  char *htaccessfilenamewodir;
  if(slash){
    slash[1] = '\0';
    htaccessfilenamewodir = getFileName( htaccess_filename, ".htaccess");
    if(htaccessfilenamewodir == NULL){
      
      return 1;//If there is no .htaccess file, allow access
    }

    strcat(htaccess_filename, htaccessfilenamewodir);
    free(htaccessfilenamewodir);
    int ffd = open(htaccess_filename, O_RDONLY, 0);
    if(ffd <= 0){
      //If there is no .htaccess file, allow access
      return 1;
    }
    else{
      if (DEBUG == TRUE) printf("Success open htaccess file\n");//////////////
      rio_t rio;
      char buf[MAXLINE], accPerm[MAXLINE], fromChar[MAXLINE], tmpbuf[MAXLINE];
      unsigned int subnet[5];
      rio_readinitb(&rio, ffd);
      ssize_t read_size;
      int addrFlag;
      do{
        read_size = rio_readlineb(&rio, buf, MAXLINE);
        sscanf(buf, "%s %s %s", accPerm, fromChar, tmpbuf);
        addrFlag = 1;
        int pos = 0;
        unsigned int IPaddr;
        while(tmpbuf[pos]){
          if(!((tmpbuf[pos]>='0' && tmpbuf[pos]<='9') || tmpbuf[pos]=='.' || tmpbuf[pos]=='/')) {
            addrFlag = 0;
            break;
          }
          ++pos;
        }
        if(addrFlag){//the entry contains an IP address
          if (DEBUG == TRUE) printf("Handling addrs in htaccess\n");
          sscanf(tmpbuf, "%u.%u.%u.%u/%u", 
            &subnet[0], &subnet[1], &subnet[2], &subnet[3], &subnet[4]); 
          IPaddr = (subnet[0]<<24)+(subnet[1]<<16)+(subnet[2]<<8)+subnet[3];
          IPaddr = (IPaddr>>(32-subnet[4]))<<(32-subnet[4]);
          /*printf("in file %u, client %u\n", IPaddr, 
            (ntohl(clientaddr->sin_addr.s_addr)>>(32-subnet[4])<<(32-subnet[4])));*/
        }
        else{//the entry contains a domain name
          if (DEBUG == TRUE) printf("Getting host by name\n");
          struct hostent host, *hostptr = &host;
          hostptr = gethostbyname(tmpbuf);
          if(hostptr){
            //TODO Dealing with the situation that there are multiple addresses in h_addr_list
            IPaddr = ntohl(*(int*)hostptr->h_addr_list[0]);//How could I know how many entries are in the list
            //printf("h_addr_list[0] is %u\n", ntohl(*(int*)hostptr->h_addr_list[0]));/////////////
          }
          else
            continue;
        }
        unsigned int clientAddrMatch;
        if(subnet[4]==0)
          clientAddrMatch = 0;
        else
          clientAddrMatch = (ntohl(clientaddr->sin_addr.s_addr)>>(32-subnet[4])<<(32-subnet[4]));
        if(IPaddr == clientAddrMatch) {
          if(strcmp("allow",accPerm)==0)
            return 1;
          if(strcmp("deny",accPerm)==0)
            return 0;
        }
      }
      while(read_size>0);

      //If there is no match in the file, allow access.
      return 1;

      close(ffd);
    }
  }
  else{
    DieWithUserMessage("<Malformed>", "<There should be a '/'>");
    return 1;
  }
}


/* Error notcie from the server-side
 * Parameters: client socket file descriptor, requested file descriptor, http request struct, size of the requested file
 * Returns: Null
 */
void send_error_msg(int fd, int status, char *msg, char *longmsg, char *type){
    char buf[MAXLINE];
    // HTTP/1.1 200 Not Found
    // Content-length xxx 
    // Message body
    sprintf(buf, "%s %d %s\r\n", type, status, msg);
    sprintf(buf + strlen(buf), "Content-length: %lu\r\n\r\n", strlen(longmsg));
    sprintf(buf + strlen(buf), "%s", longmsg);
    send(fd, buf, strlen(buf), 0);
}

/* Read the requested file on disk, and send it back to the remote side
 * Parameters: client socket file descriptor, requested file descriptor, http request struct, size of the requested file
 * Returns: Null
 */

void serve_static(int out_fd, int in_fd, http_request *req, size_t total_size, char *type){
  char buf[256];

  sprintf(buf, "%s 200 OK\r\n", type);
  
  sprintf(buf + strlen(buf), "Content-length: %llu\r\n", req->end - req->offset);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n", get_mime_type(req->filename));
  sprintf(buf + strlen(buf), "Connection: Keep-Alive\r\n\r\n");

  send(out_fd, buf, strlen(buf), 0);


  off_t offset = req->offset;
  sendfile_to(out_fd, in_fd, &offset, req->end - req->offset);
}

/* Parse the client request, acquire the requested file name
 * Parameters: client socket file descriptor, http request struct that we want to fill and return
 * Returns: Null
 */
int parse_request(int fd, http_request reqs[], char *addr){
  rio_t rio;
  char request_body[MAXLINE * 8], buf[MAXLINE], method[METHOD_SZ], uri[URI_SZ], version[VERSION_SZ];
  int i, j;
  struct stat sbuf;     

  // rio_readinitb(&rio, fd);
  // rio_readlineb(&rio, buf, MAXLINE);
  memset(request_body, 0, MAXLINE * 8);
  memset(buf, 0, MAXLINE);

  while( recv(fd, buf, MAXLINE - 1, 0) > 0){
    // for (i = 0; i < MAXLINE; ++i)
    // {
    //   printf("%d ", buf[i]);
    //   if(buf[i] == 0){
    //     break;
    //   }
    //   /* code */
    // }
    if (DEBUG == TRUE) printf("\n\n\n");
    strcat(request_body, buf);
    if (DEBUG == TRUE) printf("%s\n\n\n",request_body);

    int flag = 0;

    for (i = 0; i < sizeof(request_body) - 4; ++i)
    {
      if (request_body[i] == '\r' && request_body[i+1] == '\n' && request_body[i+2] == '\r' && request_body[i+3] == '\n')
      {
        if (DEBUG == TRUE) printf("captured CRLF CRLF\n");
        char * line = strtok(strdup(request_body), "\r\n");
        // while(line) {
          if( line && sscanf(line, "%s %s %s", method, uri, version) == 3 ){
            flag = 1;
            if(strcmp(method, "GET") == 0 || (strcmp(version, "HTTP/1.0") == 0 || strcmp(version, "HTTP/1.1") == 0)){
              if (DEBUG == TRUE) printf("%s\n", line);
              char prefix_addr[MAXLINE];
              strcpy(prefix_addr, addr);

              for(j = 0; j < REQUEST_SZ; j++){
                if(reqs[j].assigned == 0){
                  reqs[j].valid = 1;
                  reqs[j].offset = 0;
                  reqs[j].end = 0;         

                  reqs[j].assigned = 1;

                  char* filename = uri;
                  if(uri[0] == '/'){
                    filename = uri + 1;
                    int length = strlen(filename);
                    if (length == 0){
                      filename = "index.html";
                    }
                  }

                  if(prefix_addr[strlen(prefix_addr) - 1] != '/'){
                    strcat(prefix_addr, "/");
                  }

                  //printf("file name: %s\n", filename);

                  strcat(prefix_addr, filename);

                  int ffd = open(prefix_addr, O_RDONLY, 0);
                  if(ffd > 0){
                    fstat(ffd, &sbuf);
                    if(S_ISDIR(sbuf.st_mode)){
                      strcat(prefix_addr, "/index.html");
                    }
                  }
                  if (DEBUG == TRUE) printf("prefix %s\n",prefix_addr);
                  memcpy(reqs[j].filename, prefix_addr, MAXLINE); 
                  memcpy(reqs[j].type, version, VERSION_SZ); 
                  memcpy(reqs[j].method, method, METHOD_SZ);
                  memcpy(reqs[j].uri, uri, URI_SZ);
                  break;
                }
              }      
            }else{
              for(j = 0; j < REQUEST_SZ; j++){
                if(reqs[j].assigned == 0){
                  reqs[j].valid = 0;
                  reqs[j].assigned = 1;
                  break;
                }
              }

            }
            
          }else if(line && sscanf(line, "%s %s %s", method, uri, version) == 2){
            flag = 1;
            for(j = 0; j < REQUEST_SZ; j++){
              if(reqs[j].assigned == 0){
                reqs[j].valid = 0;
                reqs[j].assigned = 1;
                break;
              }
            }
          }

        //   line  = strtok(NULL, "\r\n");
        // }
      }
    }
    if(flag == 1){
      return 1;      
    }
    memset(buf, 0, MAXLINE);
  }


  // if( recv(fd, buf, MAXLINE, 0) > 0){
  //   if (DEBUG == TRUE) printf("inside recv loop %s\n", buf);

  //   return 1;
  // }else{
  //   return -1;
  // }
  // TODO, parse the http version, and set flag, if is not GET, 400 error
  // sscanf(buf, "%s %s %s", method, uri, version); 

  // printf("parsed results: %d        %s %s %s\n",fd, method, uri, version);
}


/* Parse the http request, process it and generate response
 * Parameters: Client socket file descripton, client socket address info
 * Returns: None
 */

void process(int fd, struct sockaddr_in *clientaddr, char *addr){
  http_request reqs[REQUEST_SZ];
  int i;
  //Parse the http request, retrive the file name etc.
  if( parse_request(fd, reqs, addr) == -1 ){
    int status = 400;
    char *msg = "Unknow Error";
    send_error_msg(fd, status, "Error", msg, "HTTP/1.1");
    close(fd);
    exit(fd);
    return;

  }


  for(i = 0; i < REQUEST_SZ; i++){
    if(reqs[i].assigned == 1){
      http_request req = reqs[i];
      if(reqs[i].valid != 1 && strcmp(req.method, "GET") != 0 || !(strcmp(req.type, "HTTP/1.0") == 0 || strcmp(req.type, "HTTP/1.1") == 0) || req.uri[0] != '/' ){
        if (DEBUG == TRUE) printf("400 response %s %s %s\n",req.method, req.type, req.uri);
        int status = 400;
        char *msg = "Unknow Error";
        reqs[i].assigned = 0;
        send_error_msg(fd, status, "Error", msg, "HTTP/1.1");
        close(fd);
        exit(fd);
        return;
      }

      struct stat sbuf;
      int status = 200;
      int file_access_flag;

      int accessStatus = handle_htaccess(clientaddr, req.filename);
      // printf("access status is %d\n", accessStatus);///////////////

      if(accessStatus){
          //if this directory is not allowed to access
        int ffd = open(req.filename, O_RDONLY, 0);

        if(ffd <= 0){
          //File not found
          status = 404;
          char *msg = "Page Not Found";
          send_error_msg(fd, status, "Not found", msg, req.type);
        } else {
          fstat(ffd, &sbuf);
          //Check if is a regular file?
          if(S_ISREG(sbuf.st_mode)){
            if (req.end == 0){
              req.end = sbuf.st_size;
            }
            //File permission handling, if can't read, return 403 msg
            file_access_flag = access(req.filename, R_OK);
            // printf("file access tag %d\n\n\n", file_access_flag);
            if(file_access_flag < 0){
              status = 403;
              char *msg = "Permission denied";
              send_error_msg(fd, status, "Forbidden", msg, req.type);
              reqs[i].assigned = 0;
              return;
            }
            serve_static(fd, ffd, &req, sbuf.st_size, req.type);
          } else {
            //Malfored request, http 400 error
            status = 400;
            char *msg = "Unknow Error";
            send_error_msg(fd, status, "Error", msg, req.type);
          }
          close(ffd);
        }
      }

      reqs[i].assigned = 0;

      if(strcmp(req.type, "HTTP/1.0") == 0){
        close(fd);
        exit(fd);
      }
      else if(strcmp(req.type, "HTTP/1.1") == 0 && first_flag == 0)
      {
        first_flag = 1;
        pthread_mutex_lock(&timeout_mutex);
        pthread_cond_signal(&timeout_cond);
        pthread_mutex_unlock(&timeout_mutex);
      }
    }
  }

  //If req.type == 1.0
  //  close(client_sock);
  // else if req.type == 1.1

}

/* Socket initialization, listen for web connections
 * on a specified port. 
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket 
 */

int socket_initilization(int port){
  int httpd = 0;
  struct sockaddr_in name;
  //Following code snippets from TCP/IP Sockets in C
  httpd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  if (httpd == -1)
    DieWithSystemMessage("socket");

  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    DieWithSystemMessage("bind");

  if (listen(httpd, 20) < 0)
    DieWithSystemMessage("listen");

  return(httpd);
}

void *timeout(void * client_sock_ptr){
  int client_sock = *(int*)client_sock_ptr;
  
  first_flag = 0;//only set as 0 here to make sure wait() is alway called before signal()
  pthread_mutex_lock(&timeout_mutex);
  pthread_cond_wait(&timeout_cond, &timeout_mutex);
  pthread_mutex_unlock(&timeout_mutex);

  sleep(1000);
  if (DEBUG == TRUE) printf("%d timeout\n",client_sock);
  closeSocket(client_sock);
  exit(client_sock);
}

void catch(int snum) {
  int pid;
  int client_sock;

  pid = wait(&client_sock);
  if (DEBUG == TRUE) printf("Closing client socket %d in parent process\n", WEXITSTATUS(client_sock));
  close(WEXITSTATUS(client_sock));
}

int main(int argc, char *argv[]){
  if(argc != 3)
    DieWithUserMessage("<port>", "<path/to/document/root>");

  struct sockaddr_in clientaddr;
  char *path;
  
  int default_port = 3000,
      server_sock,
      client_sock;

  socklen_t clientlen = sizeof clientaddr;

  signal(SIGCHLD, catch);

  //Setup port number and root directory path
  default_port = atoi(argv[1]);
  path = argv[2];

  //Initialize the socket
  server_sock = socket_initilization(default_port);

   /* Initialize mutex and condition variable objects */
  pthread_mutex_init(&timeout_mutex, NULL);
  pthread_cond_init(&timeout_cond, NULL);

  first_flag = -1;

  // new pthread, sleep TIME, wake up iterate through client_sock struct array, compare current system timestamp 
  // with struct's timestamp, if difference < TIME 

  // Concurrency handling, multi-process approach

  while(1){
    client_sock = accept(server_sock, (struct sockaddr *)&clientaddr, &clientlen);
    if (DEBUG == TRUE) printf("%d Accepted\n", client_sock);
    int * client_sock_ptr = &client_sock;
    //printf("Accept connection for %d\n", client_sock);
    if (client_sock == -1)
      DieWithSystemMessage("accept");

    int pid = fork();
    if(pid == 0){
      pthread_t thread = 0;
      pthread_create(&thread, NULL, timeout, (void *) client_sock_ptr );
      while(1){
        process(client_sock, &clientaddr, path);
        // close(client_sock);
        // exit(pid);
      }
    }else if (pid < 0){
      perror("fork");
    }
  }

  return 0;
}
