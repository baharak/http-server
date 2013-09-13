/*
 * http_server.hpp
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

#ifndef MYHTTPD_HPP_
#define MYHTTPD_HPP_

#include <string>

#define DEFAULT_HTTP_ROOT "myhttpd-root"

class HttpServer {
 public:
  std::string http_mode;
  int port;
  int timeout;
  std::string http_root;
  int sockfd;

  HttpServer(const char* http_root, int argc, char* argv[]);
  void Start();
  virtual void Serve() = 0;
  virtual int GetBacklog() = 0;
  void Stop();
  int AcceptConnection();
  void ProcessRequest(int fd);
};


#endif
