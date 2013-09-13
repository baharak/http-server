/*
 * myhttpdt.cpp
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

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <pthread.h>

#include <exception>
#include <utility>

#include "http_server.hpp"

#define MULTI_THREADED_BACKLOG 1000

using namespace std;

void* ProcessRequest(void* arg);

class MultiThreadedHttpServer : public HttpServer {
 public:
  MultiThreadedHttpServer(const char* http_root, int argc, char* argv[]): HttpServer(http_root, argc, argv) {}

  void Serve() {
    signal(SIGPIPE, SIG_IGN);

    while (true) {
      int fd = AcceptConnection();
      pthread_t thread;
      pair<MultiThreadedHttpServer*, int>* arg_pair =
          new pair<MultiThreadedHttpServer*, int>(this, fd);
      if (pthread_create(&thread, NULL, ::ProcessRequest, arg_pair) != 0) {
        perror("pthread_create");
        throw exception();
      }
    }
  }

  int GetBacklog() {
    return MULTI_THREADED_BACKLOG;
  }
};

void* ProcessRequest(void* arg) {
  pair<MultiThreadedHttpServer*, int>* arg_pair;
  arg_pair = (pair<MultiThreadedHttpServer*, int>*)arg;

  try {
    (arg_pair->first)->ProcessRequest(arg_pair->second);
  } catch (int error_num) {

    if (error_num != EPIPE && error_num != ECONNRESET) {
      perror("read/write");
      exit(1);
    }
  }

  delete arg_pair;
  return NULL;
}

int main(int argc, char* argv[]) {
  MultiThreadedHttpServer server(DEFAULT_HTTP_ROOT, argc, argv);
  server.Start();
  server.Serve();
  return 0;
}
