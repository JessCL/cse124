A simple HTTP web server in C, UCSD CSE 124 Winter 2015 project

Yu Xia: A53041213
MengQi Yu: A53077101

Features
--------

1. Basic MIME mapping
2. Low resource usage
3. [sendfile(2)](http://kernel.org/doc/man-pages/online/pages/man2/sendfile.2.html)
4. Concurrency by pre-fork
5. Basic permisison checking
6. .htaccess checking
7. HTTP 1.1 pipelined request handling.

Missing features
------------
1. POST request handling.
2. Directory listing

Usage
-----

`httpd <port> <path/to/document/root>`, opens a server in the give directory.
