/*
 * myhttpdp.cpp
 *
 * Copyright (C) 2013 Baharak Saberidokht <baharak1364@gmail.com>
 *
 * This file is part of Http Server.
 *
 * Http Server is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, either version 3 of the License,
 * or (at your option) any later version.
 *
 * Http Server is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Http Server. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cerrno>
#include <csignal>
#include <cstdio>

#include <exception>

#include <sys/wait.h>
#include <unistd.h>

#include "http_server.hpp"

#define MULTI_PROCESS_BACKLOG 100

using namespace std;

class MultiProcessHttpServer : public HttpServer {
 public:
  MultiProcessHttpServer(const char* http_root, int argc, char* argv[])
    : HttpServer(http_root, argc, argv) {
  }

  void Serve() {
    int numprocesses = 5;
    for (int i = 0; i < numprocesses; ++i) {
      int pid = fork();
      if (pid == -1) {
        perror("fork");
        throw exception();
      } else if (pid == 0) {
        signal(SIGPIPE, SIG_IGN);

        while (true) {
          int fd = AcceptConnection();
          try {
            ProcessRequest(fd);
          } catch (int error_num) {
            if (error_num == EPIPE || error_num == ECONNRESET) {
              continue;
            }
            throw error_num;
          }
        }
      }
    }

    for (int i = 0; i < numprocesses; ++i)
      wait(NULL);
  }

  int GetBacklog() {
    return MULTI_PROCESS_BACKLOG;
  }
};

int main(int argc, char* argv[]) {
  MultiProcessHttpServer server(DEFAULT_HTTP_ROOT, argc, argv);
  server.Start();
  server.Serve();
  return 0;
}
