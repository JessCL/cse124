#include <arpa/inet.h>          
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define LISTENQ  1024  /* second argument to listen() */
#define MAXLINE 1024

//To be removed
void handle_directory_request(int out_fd, int dir_fd, char *filename){
    char buf[MAXLINE], m_time[32], size[16];
    struct stat statbuf;
    sprintf(buf, "HTTP/1.1 200 OK\r\n%s%s%s%s%s",
            "Content-Type: text/html\r\n\r\n",
            "<html><head><style>",
            "body{font-family: monospace; font-size: 13px;}",
            "td {padding: 1.5px 6px;}",
            "</style></head><body><table>\n");
    write_to_client(out_fd, buf, strlen(buf));
    DIR *d = fdopendir(dir_fd);
    struct dirent *dp;
    int ffd;
    while ((dp = readdir(d)) != NULL){
        if(!strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..")){
            continue;
        }
        if ((ffd = openat(dir_fd, dp->d_name, O_RDONLY)) == -1){
            perror(dp->d_name);
            continue;
        }
        fstat(ffd, &statbuf);
        strftime(m_time, sizeof(m_time),
                 "%Y-%m-%d %H:%M", localtime(&statbuf.st_mtime));
        format_size(size, &statbuf);
        if(S_ISREG(statbuf.st_mode) || S_ISDIR(statbuf.st_mode)){
            char *d = S_ISDIR(statbuf.st_mode) ? "/" : "";
            sprintf(buf, "<tr><td><a href=\"%s%s\">%s%s</a></td><td>%s</td><td>%s</td></tr>\n",
                    dp->d_name, d, dp->d_name, d, m_time, size);
            write_to_client(out_fd, buf, strlen(buf));
        }
        close(ffd);
    }
    sprintf(buf, "</table></body></html>");
    write_to_client(out_fd, buf, strlen(buf));
    closedir(d);
}

/* Error notcie from the server-side
 * Parameters: client socket file descriptor, requested file descriptor, http request struct, size of the requested file
 * Returns: Null
 */
void client_error(int fd, int status, char *msg, char *longmsg){
    char buf[MAXLINE];
    sprintf(buf, "HTTP/1.1 %d %s\r\n", status, msg);
    sprintf(buf + strlen(buf),
            "Content-length: %lu\r\n\r\n", strlen(longmsg));
    sprintf(buf + strlen(buf), "%s", longmsg);
    write_to_client(fd, buf, strlen(buf));
}

/* Read the requested file on disk, and send it back to the remote side
 * Parameters: client socket file descriptor, requested file descriptor, http request struct, size of the requested file
 * Returns: Null
 */

void serve_static(int out_fd, int in_fd, http_request *req, size_t total_size){
  char buf[256];
  if (req->offset > 0){
    sprintf(buf, "HTTP/1.1 206 Partial\r\n");
    sprintf(buf + strlen(buf), "Content-Range: bytes %lu-%lu/%lu\r\n",
            req->offset, req->end, total_size);
  } else {
    sprintf(buf, "HTTP/1.1 200 OK\r\nAccept-Ranges: bytes\r\n");
  }
  sprintf(buf + strlen(buf), "Cache-Control: no-cache\r\n");
  // sprintf(buf + strlen(buf), "Cache-Control: public, max-age=315360000\r\nExpires: Thu, 31 Dec 2037 23:55:55 GMT\r\n");

  sprintf(buf + strlen(buf), "Content-length: %lu\r\n",
          req->end - req->offset);
  sprintf(buf + strlen(buf), "Content-type: %s\r\n\r\n",
          get_mime_type(req->filename));

  write_to_client(out_fd, buf, strlen(buf));
  off_t offset = req->offset; /* copy */
  while(offset < req->end){
    if(sendfile_to(out_fd, in_fd, &offset, req->end - req->offset) <= 0) {
        break;
    }
    printf("offset: %d \n\n", offset);
    close(out_fd);
    break;
  }
}

/* Parse the client request, acquire the requested file name
 * Parameters: client socket file descriptor, http request struct that we want to fill and return
 * Returns: Null
 */
void parse_request(int fd, http_request *req){
  rio_t rio;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE];
  req->offset = 0;
  req->end = 0;              

  rio_readinitb(&rio, fd);
  rio_readlineb(&rio, buf, MAXLINE);
  // TODO, parse the http version, and set flag
  sscanf(buf, "%s %s", method, uri); 

  char* filename = uri;
  if(uri[0] == '/'){
    filename = uri + 1;
    int length = strlen(filename);
    if (length == 0){
      filename = ".";
    } else {
      for (int i = 0; i < length; ++ i) {
        if (filename[i] == '?') {
          filename[i] = '\0';
          break;
        }
      }
    }
  }

  url_decode(filename, req->filename, MAXLINE);
}

/* Parse the http request, process it and generate response
 * Parameters: Client socket file descripton, client socket address info
 * Returns: None
 */

void process(int fd, struct sockaddr_in *clientaddr){
  http_request req;
  //Parse the http request, retrive the file name etc.
  parse_request(fd, &req);

  struct stat sbuf;
  int status = 200
  int ffd = open(req.filename, O_RDONLY, 0);

  if(ffd <= 0){
    //File not found
    status = 404;
    char *msg = "File not found";
    client_error(fd, status, "Not found", msg);
  } else {
    fstat(ffd, &sbuf);
    //Check if is a regular file?
    if(S_ISREG(sbuf.st_mode)){
      if (req.end == 0){
        req.end = sbuf.st_size;
      }
      // if is requesting partial content, then set the status code to 206
      if (req.offset > 0){
        status = 206;
      }
      serve_static(fd, ffd, &req, sbuf.st_size);
    } else if(S_ISDIR(sbuf.st_mode)){
      status = 200;
      //TODO Append index.html to file path and try to render, if not, then return 404
      handle_directory_request(fd, ffd, req.filename);
    } else {
      //Malfored request, http 400 error
      status = 400;
      char *msg = "Unknow Error";
      client_error(fd, status, "Error", msg);
    }
    close(ffd);
  }
}

/* Socket initialization, listen for web connections
 * on a specified port. 
 * Parameters: pointer to variable containing the port to connect on
 * Returns: the socket 
 */

int startup(int port){
  int httpd = 0;
  int optval = 1;
  struct sockaddr_in name;
  //Following code snippets from TCP/IP Sockets in C
  httpd = socket(AF_INET, SOCK_STREAM, 0);

  if (httpd == -1)
    DieWithSystemMessage("socket");

  if (setsockopt(httpd, SOL_SOCKET, SO_REUSEADDR,
                 (const void *)&optval , sizeof(int)) < 0)
    return -1;


  memset(&name, 0, sizeof(name));
  name.sin_family = AF_INET;
  name.sin_port = htons(port);
  name.sin_addr.s_addr = htonl(INADDR_ANY);

  if (bind(httpd, (struct sockaddr *)&name, sizeof(name)) < 0)
    DieWithSystemMessage("bind");

  if (listen(httpd, 5) < 0)
    DieWithSystemMessage("listen");

  return(httpd);
}

int main(int argc, char *argv[]){
  if(argc != 3)
    DieWithUserMessage("<port>", "<path/to/document/root>");

  struct sockaddr_in clientaddr;
  
  int default_port = 9999,
      server_sock,
      client_sock;

  char buf[256];
  char *path = getcwd(buf, 256);
  socklen_t clientlen = sizeof clientaddr;

  //Setup port number and root directory path
  default_port = atoi(argv[1]);
  path = argv[2];

  //Initialize the socket
  server_sock = startup(default_port);

  signal(SIGPIPE, SIG_IGN);

  // Concurrency handling, multi-process approach
  for(int i = 0; i < 10; i++) {
    int pid = fork();
    if (pid == 0) {         //  child
      while(1){
        client_sock = accept(server_sock, (struct sockaddr *)&clientaddr, &clientlen);

        if (client_sock == -1)
          DieWithSystemMessage("accept");

        process(client_sock, &clientaddr);
        close(client_sock);
      }
    } else if(pid < 0) {
      perror("fork");
    }
  }

  while(1){
    client_sock = accept(server_sock, (struct sockaddr *)&clientaddr, &clientlen);
    
    if (client_sock == -1)
      DieWithSystemMessage("accept");

    process(client_sock, &clientaddr);
    close(client_sock);
  }

  return 0;
}
