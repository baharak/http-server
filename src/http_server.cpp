/*
 * http_server.cpp
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <exception>
#include <sstream>
#include <string>

#include "http_server.hpp"

#define DEFAULT_PORT 8080
#define DEFAULT_TIMEOUT 300

using namespace std;

namespace {

struct HttpRequest {
  std::string method;
  std::string uri;
  std::string http_mode;
  bool bad;
  long trans_time;

  HttpRequest();

  bool Read(int fd);
  bool Respond(int fd, const std::string& http_root,
               const std::string& http_version);
};

bool EndsWithTwoNewLines(const std::string& s);

std::string PathFromUri(const std::string& uri);

ssize_t Write(int fd, const std::string& s) {
  ssize_t ret = 0;
  while (true) {
    ssize_t cnt = write(fd, s.c_str() + ret, s.length() - ret);
    if (cnt == 0)
      break;
    if (cnt == -1) {
      if (errno != EPIPE)
        perror("write");
      throw errno;
    }
    ret += cnt;
  }
  return ret;
}

void EndHeaders(int fd, const std::string& http_version,
                const std::string& http_mode) {
  if (http_mode == "HTTP/1.0" && http_version == "HTTP/1.1") {
    Write(fd, "Connection: close\r\n");
  }
  Write(fd, "\r\n");
}

bool EndResponse(int fd, const std::string& http_mode) {
  if (http_mode == "HTTP/1.0")
    close(fd);
  return true;
}

std::string ReplaceString(std::string subject, const std::string& search,
                          const std::string& replace) {
  size_t pos = 0;
  while ((pos = subject.find(search, pos)) != std::string::npos) {
    subject.replace(pos, search.length(), replace);
    pos += replace.length();
  }
  return subject;
}

HttpRequest::HttpRequest() : bad(true), trans_time(0) {
}

bool HttpRequest::Read(int fd) {
  const size_t buf_len = 10;
  char buf[buf_len];
  ssize_t numread;

  std::string request_string;
  do {
    numread = read(fd, &buf, buf_len);
    if (numread == -1) {
      if (errno != EPIPE)
        perror("read");
      throw errno;
    }

    for (int i = 0; i < numread; ++i) {
      request_string.push_back(buf[i]);
    }
  } while (numread && !EndsWithTwoNewLines(request_string));

  bad = false;

  std::istringstream iss(request_string);
  iss >> method >> uri >> http_mode;

  if (method != "GET" &&
      method != "OPTIONS" &&
      method != "HEAD" &&
      method != "POST" &&
      method != "PUT" &&
      method != "DELETE" &&
      method != "TRACE" &&
      method != "CONNECT") {
    bad = true;
  } else if (uri.empty()) {
    bad = true;
  } else if (uri[0] != '/' && uri[0] != '*' &&
             uri.find("http://") == std::string::npos &&
             uri.find("https://") == std::string::npos &&
             method != "CONNECT") {
    bad = true;
  } else if (http_mode != "HTTP/1.0" && http_mode != "HTTP/1.1")
    bad = true;

  return true;
}

bool HttpRequest::Respond(int fd, const std::string& http_root,
                          const std::string& http_mode) {
  if (bad) {
    Write(fd, http_mode + " 400 Bad Request\r\n");
    EndHeaders(fd, this->http_mode, http_mode);
    return EndResponse(fd, http_mode);
  }

  if (method != "GET") {
    Write(fd, http_mode + " 501 Not Implemented\r\n");
    EndHeaders(fd, this->http_mode, http_mode);
    return EndResponse(fd, http_mode);
  }


  std::string path = PathFromUri(uri);
  if (path == "/")
    path += "index.html";
  path = http_root + ReplaceString(path, std::string("%20"), " "); // create appr URI


  FILE* file = fopen(path.c_str(), "r");
  if (file == NULL) {
    switch (errno) {
      case ENOENT:
      case ENOTDIR:
        Write(fd, http_mode + " 404 Not Found\r\n");
        EndHeaders(fd, this->http_mode, http_mode);
        return EndResponse(fd, http_mode);
      case EACCES:
        Write(fd, http_mode + " 403 Forbidden\r\n");
        EndHeaders(fd, this->http_mode, http_mode);
        return EndResponse(fd, http_mode);
      default:
        perror("fopen");
        throw errno;
    }
  } // Open file


  struct stat statbuf;
  if (stat(path.c_str(), &statbuf) == -1) {
    fclose(file);
    perror("stat");
    throw errno;
  } // send 403
  if (S_ISDIR(statbuf.st_mode)) {
    fclose(file);
    Write(fd, http_mode + " 403 Forbidden\r\n");
    EndHeaders(fd, this->http_mode, http_mode);
    return EndResponse(fd, http_mode);
  }


  if (fseek(file, 0, SEEK_END) != 0) {
    fclose(file);
    Write(fd, http_mode + " 403 Forbidden\r\n");
    EndHeaders(fd, this->http_mode, http_mode);
    return EndResponse(fd, http_mode);
  } // len of file
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);


  Write(fd, http_mode + " 200 OK\r\n");

  timeval trans_start;
  gettimeofday(&trans_start, NULL);


  char content_length_str[32];
  sprintf(content_length_str, "Content-Length: %ld\r\n", file_size);
  Write(fd, content_length_str);

  EndHeaders(fd, this->http_mode, http_mode);


  try {
    while (!feof(file)) {
      size_t buf_len = 10;
      char buf[buf_len + 1];
      size_t numread = fread(&buf, 1, buf_len, file);
      write(fd, &buf, numread);
    }
  } catch (int error_num) {
    fclose(file);
    throw error_num;
  } // reading file for client : done

  timeval trans_end;
  gettimeofday(&trans_end, NULL);
  trans_time =  (long)(
      (trans_end.tv_sec - trans_start.tv_sec) * 1000000L +
      trans_end.tv_usec - trans_start.tv_usec);

  fclose(file);

  return EndResponse(fd, http_mode);
}

bool EndsWithTwoNewLines(const std::string& s) {
  static const std::string crlfcrlf = "\r\n\r\n";
  if (s.length() < crlfcrlf.length())
    return false;

  std::string lastFourChars(s.end() - crlfcrlf.length(), s.end());
  return lastFourChars == crlfcrlf;
}

std::string PathFromUri(const std::string& uri) {
  const std::string separator = "://";
  size_t index_from = uri.find(separator);
  if (index_from != std::string::npos) {
    index_from += separator.length();
  } else {
    index_from = 0;
  }

  size_t path_from = uri.find("/", index_from);
  if (path_from == std::string::npos)
    throw std::exception();

  return uri.substr(path_from);
}

}

namespace {

pthread_mutex_t trans_times_mutex;

}

void HttpServer::ProcessRequest(int fd) {
  while (true) {
    HttpRequest req;

    try {
      req.Read(fd);
    } catch(int error_num) {
      if (close(fd) == -1)
        perror("close");


      if (error_num == EAGAIN)
        return ;

      throw error_num;
    }

    try {
      req.Respond(fd, http_root, http_mode);
    } catch (int error_num) {
      if (close(fd) == -1)
        perror("close");

      throw error_num;
    }

    pthread_mutex_lock(&trans_times_mutex);
    printf("%ld\n", req.trans_time);
    fflush(stdout);
    pthread_mutex_unlock(&trans_times_mutex);

    if (http_mode == "HTTP/1.0")
      break;
  }
}

int HttpServer::AcceptConnection() {

  int fd = accept(this->sockfd, NULL, NULL);
  if (fd == -1) {
    perror("failed to accept a connection");
    throw exception();
  }


  if (http_mode == "HTTP/1.1") {
    struct timeval timeout;
    timeout.tv_sec = this->timeout;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                   sizeof(timeout)) == -1) {
      perror("setsockopt");
    }
  }

  return fd;
}

int ReturnPort(const char* portnumber) {
  int port;
  sscanf(portnumber, "%d", &port);
  return port;
}

bool RightPortRange(int portnumber) {
  return 1025 <= portnumber && portnumber <= 65535;
}

HttpServer::HttpServer(const char* http_root, int argc, char* argv[]) {
  pthread_mutex_init(&trans_times_mutex, NULL);

  this->http_root = http_root;



  this->http_mode = "1.0";
  if (argc > 1){
    this->http_mode = argv[1];
    if (this->http_mode ==  "1")
      this->http_mode = "1.0";
  }

  this->http_mode = "HTTP/" + this->http_mode;


  this->port = (argc > 2 ? ReturnPort(argv[2]) : DEFAULT_PORT);
  if (!RightPortRange(this->port)) {
    fprintf(stderr, " Input port number must be between 1024 and 65536 ");
    fprintf(stderr , "the port number %d is\n", this->port);
    throw exception();
  }


  int timeout;
  if (argc > 3)
    sscanf(argv[3], "%d", &timeout);
  else
    timeout = DEFAULT_TIMEOUT;

  if (timeout <= 0) {
    fprintf(stderr, "Invalid timeout: %d\n", timeout);
    throw exception();
  }
  this->timeout = timeout;
}

void HttpServer::Start() {

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror("creating socket failed");
    throw exception();
  }


  int on = 1;
  setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));


  sockaddr_in my_address;
  memset(&my_address, 0, sizeof(my_address));
  my_address.sin_family = PF_INET;
  my_address.sin_port = htons(port);
  my_address.sin_addr.s_addr = INADDR_ANY;

  if (bind(sockfd, (struct sockaddr*)&my_address, sizeof(my_address)) == -1) {
    perror("unable to bind");
    throw exception();
  }


  if (listen(sockfd, GetBacklog()) == -1) {
    perror("failed to listen");
    throw exception();
  }

  this->sockfd = sockfd;
}

void HttpServer::Stop() {
  close(this->sockfd);
}
