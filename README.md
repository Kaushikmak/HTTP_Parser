# HTTP Parser & Server

A super simple, RFC-compliant HTTP/1.1 server in C.

## How to compile

Just run gcc to compile the server:
```bash
gcc -Wall -Wextra -o test_server test_server.c http_server.c
```
Then start it up:
```bash
./test_server
```

## How to see the response

Make sure the server is running, then you can test it in two ways:

1. In your browser
Just pop this into your address bar:
http://localhost:8080

2. Using curl
Open up another terminal tab and run:
```bash
curl -v http://localhost:8080
```
(The -v flag lets you see the raw headers, including that sweet empty line b/w header and body delimited by \r\n\r\n)

## Benchmark Performance

I put this simple parser up against the production-grade picohttpparser for a 1,000,000 iteration benchmark (with equivalent overhead):

- Custom parser: ~0.28 seconds
- picohttpparser: ~0.04 seconds
