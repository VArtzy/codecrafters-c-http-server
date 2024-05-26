# C http server

## Concept learned

- Binding port and server listening in C
- Creating handler and routing via http_handler func and if else
- Responding http response with it's parts (status line, headers, body)
- Extracting URL (routing) and http verbs discovering
- Response body and gzip'in it
- Read headers and common header like user-agent, host, etc
- Support concurrent connection (multithreading)
- Serve file in dir (web-server likely)
- Save/write file in dir 
- Stream file via fopen
- Content Encoding header, compression (gzip/zlib)

## Future TODO? (i think i will never see this code again...)

- Clean code (refactoring) -- to much buch copy/paste -- more modular
- More dynamic size buffers and static types (it is hard to implement growable data structure in C? idk...)


[![progress-banner](https://backend.codecrafters.io/progress/http-server/ecb889fd-0816-4461-adf3-d317f2591217)](https://app.codecrafters.io/users/codecrafters-bot?r=2qF)

This is a starting point for C solutions to the
["Build Your Own HTTP server" Challenge](https://app.codecrafters.io/courses/http-server/overview).

[HTTP](https://en.wikipedia.org/wiki/Hypertext_Transfer_Protocol) is the
protocol that powers the web. In this challenge, you'll build a HTTP/1.1 server
that is capable of serving multiple clients.

Along the way you'll learn about TCP servers,
[HTTP request syntax](https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html),
and more.

**Note**: If you're viewing this repo on GitHub, head over to
[codecrafters.io](https://codecrafters.io) to try the challenge.

# Passing the first stage

The entry point for your HTTP server implementation is in `app/server.c`. Study
and uncomment the relevant code, and push your changes to pass the first stage:

```sh
git add .
git commit -m "pass 1st stage" # any msg
git push origin master
```

Time to move on to the next stage!

# Stage 2 & beyond

Note: This section is for stages 2 and beyond.

1. Ensure you have `gcc` installed locally
1. Run `./your_server.sh` to run your program, which is implemented in
   `app/server.c`.
1. Commit your changes and run `git push origin master` to submit your solution
   to CodeCrafters. Test output will be streamed to your terminal.
