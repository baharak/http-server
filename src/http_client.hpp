/*
 * http_client.hpp
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

#ifndef HTTP_CLIENT_HPP_
#define HTTP_CLIENT_HPP_

#include <string>

class HttpClient {
 public:
  std::string http_mode;
  std::string hostname;
  int port;
  std::string uri;
  int numrequests;
  int numthreads;

  HttpClient(int argc, char* argv[]);
  int Connect();
  void DownloadAll();
  static void Request(int fd, const std::string& uri,
                      const std::string& http_mode,
                      const std::string& hostname,
                      size_t* numwritten = NULL);
  static bool Receive(int fd, const std::string& http_mode, FILE *file,
                      size_t* numread = NULL);
};

#endif
