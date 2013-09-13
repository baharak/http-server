/*
 * http_client.cpp
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
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <exception>

#include "http_client.hpp"

#define DOWNLOAD_DIR "Downloads"

namespace {

pthread_mutex_t connect_times_mutex;
std::vector<long> connect_times;

pthread_mutex_t throughputs_mutex;
std::vector<long long> throughputs;

ssize_t Write(int fd, const std::string& s, size_t* numwritten) {
  ssize_t ret = 0;
  while (true) {
    ssize_t cnt = write(fd, s.c_str() + ret, s.length() - ret);
    if (cnt == 0)
      break;
    if (cnt == -1)
      throw errno;
    ret += cnt;
  }
  if (numwritten != NULL)
    *numwritten = ret;
  return ret;
}

bool Search(
    size_t read_count,
    std::vector<char>& bytes,
    std::string str,
    std::vector<char>::iterator* match_end) {
  size_t from = 0;
  if (bytes.size() > read_count + str.length() - 1)
    from = bytes.size() - (read_count + str.length() - 1);

  std::vector<char>::iterator it = std::search(
      bytes.begin() + from, bytes.end(),
      str.begin(), str.end());
  if (it != bytes.end()) {
    *match_end = it + str.size();
    return true;
  }
  return false;
}

}

HttpClient::HttpClient(int argc, char* argv[]) {
  if (argc < 6) {
    fprintf(stderr,
            "Usage: %s <http_mode> <hostname> <port> <URI> <numrequests>"
            " [<numthreads>]",
            argv[0]);
    throw std::exception();
  }

  http_mode = argv[1];
  if (http_mode == "1" || http_mode == "1.0" || http_mode == "HTTP/1.0")
    http_mode = "HTTP/1.0";
  else if (http_mode == "1.1" || http_mode == "HTTP/1.1")
    http_mode = "HTTP/1.1";
  else {
    fprintf(stderr, "Invalid HTTP mode: %s\n", http_mode.c_str());
    throw std::exception();
  }
  hostname = argv[2];
  sscanf(argv[3], "%d", &port);
  uri = argv[4];
  sscanf(argv[5], "%d", &numrequests);
  if (argc > 6)
    sscanf(argv[6], "%d", &numthreads);
  else
    numthreads = 1;
}

int HttpClient::Connect() {
  struct addrinfo hints, *servinfo, *p;
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  char port_str[10];
  sprintf(port_str, "%d", this->port);

  pthread_mutex_lock(&connect_times_mutex);
  int rv = getaddrinfo(hostname.c_str(), port_str, &hints, &servinfo);
  pthread_mutex_unlock(&connect_times_mutex);
  if (rv != 0) {
    fprintf(stderr, "getaddrinfo: %s (%d)\n", gai_strerror(rv), rv);
    throw std::exception();
  }

  int sockfd;


  for(p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype,
                         p->ai_protocol)) == -1) {
      perror("client: socket");
      continue;
    }

    timeval connect_start, connect_end;
    gettimeofday(&connect_start, NULL);
    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      perror("client: connect");
      continue;
    }
    gettimeofday(&connect_end, NULL);
    long connect_time =  (long)(
        (connect_end.tv_sec - connect_start.tv_sec) * 1000000L +
        connect_end.tv_usec - connect_start.tv_usec);
    pthread_mutex_lock(&connect_times_mutex);
    connect_times.push_back(connect_time);
    pthread_mutex_unlock(&connect_times_mutex);
    break;
  }
  freeaddrinfo(servinfo);

  if (p == NULL) {
    fprintf(stderr, "client: failed to connect\n");
    throw std::exception();
  }

  return sockfd;
}

void HttpClient::Request(int fd, const std::string& uri,
                         const std::string& http_mode,
                         const std::string& hostname,
                         size_t* numwritten) {
  Write(fd, "GET " + uri + " " + http_mode + "\r\n", numwritten);
  if (http_mode == "HTTP/1.1") {
    Write(fd, "Host: " + hostname + "\r\n", numwritten);
  }
  Write(fd, "\r\n", numwritten);
}

bool HttpClient::Receive(int fd, const std::string& http_mode, FILE* file,
                         size_t* numread_total) {

  std::string crlfcrlf = "\r\n\r\n";
  std::vector<char> bytes;
  size_t body_offset;
  bool body = false;
  size_t content_length;
  bool content_length_read = false;
  while (true) {
    char buf[10];
    size_t count = sizeof(buf);
    if (content_length_read) {
      size_t remaining = content_length - (bytes.size() - body_offset);
      count = std::min(count, remaining);
    }
    ssize_t numread = read(fd, buf, count);
    if (numread == -1)
      throw errno;
    if (numread == 0)
      break;

    if (numread_total != NULL)
      *numread_total += numread;
    for (int i = 0; i < numread; ++i) {
      bytes.push_back(buf[i]);
    }

    if (body)
      continue;

    std::vector<char>::iterator body_begin;
    if (Search(count, bytes, crlfcrlf, &body_begin)) {
      body_offset = body_begin - bytes.begin();
      body = true;
      std::vector<char>::iterator content_length_begin;
      if (!Search(bytes.size(), bytes, "Content-Length: ",
                  &content_length_begin)) {
        fprintf(stderr, "Error: Missing Content-Length");
        break;
      }

      std::vector<char>::iterator content_length_end = std::search(
          content_length_begin, bytes.end(),
          crlfcrlf.begin(), crlfcrlf.begin() + 2);
      std::string content_length_str(content_length_begin, content_length_end);
      int content_length_int;
      if (sscanf(content_length_str.c_str(), "%d", &content_length_int) == 1) {
        content_length = content_length_int;
        content_length_read = true;
      }
    }
  }

  if (body) {
    std::string header(bytes.begin(), bytes.begin() + body_offset);
    //pthread_mutex_lock(&connect_times_mutex);
    //fprintf(stderr, "%ld %s", (long)pthread_self(), header.c_str());
    //pthread_mutex_unlock(&connect_times_mutex);

    fwrite(&*(bytes.begin() + body_offset), 1, bytes.size() - body_offset,
           file);
  }


  std::vector<char>::iterator it;
  if (http_mode == "HTTP/1.0" ||
      (Search(bytes.size(), bytes, "Connection: close\r\n", &it) &&
       (size_t)(it - bytes.begin()) <= body_offset)) {
    close(fd);
    return true;
  } // close for http1.0

  return false;
}

void* Download(void* arg) {
  HttpClient* http_client = (HttpClient*)arg;
  const std::string& hostname = http_client->hostname;
  const std::string& http_mode = http_client->http_mode;
  const std::string& uri = http_client->uri;
  int numrequests = http_client->numrequests;

  if (mkdir(DOWNLOAD_DIR, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) &&
      errno != EEXIST) {
    perror("mkdir");
    return NULL;
  }

  int sockfd;
  bool connection_closed = true;
  for (int i = 0; i < numrequests; ++i) {
    if (connection_closed)
      sockfd = http_client->Connect();

    timeval download_start;
    gettimeofday(&download_start, NULL);
    size_t traffic = 0;

    std::string path = uri;
    std::replace(path.begin() + 1, path.end(), '/', '_');
    path = DOWNLOAD_DIR + path;
    FILE* file = fopen(path.c_str(), "w");
    http_client->Request(sockfd, uri, http_mode, hostname, &traffic);
    connection_closed = http_client->Receive(sockfd, http_mode, file,
                                             &traffic);
    fclose(file);

    timeval download_end;
    gettimeofday(&download_end, NULL);
    long download_time =  (long)(
        (download_end.tv_sec - download_start.tv_sec) * 1000000L +
        download_end.tv_usec - download_start.tv_usec);
    long long throughput = traffic * 1000000LL / download_time;

    pthread_mutex_lock(&throughputs_mutex);
    throughputs.push_back(throughput);
    pthread_mutex_unlock(&throughputs_mutex);
  }

  if (!connection_closed)
    close(sockfd);

  return NULL;
}

void HttpClient::DownloadAll() {
  pthread_mutex_init(&connect_times_mutex, NULL);
  pthread_mutex_init(&throughputs_mutex, NULL);

  pthread_t threads[numthreads];

  for (int i = 0; i < numthreads; ++i) {
    if (pthread_create(&threads[i], NULL, Download, this) != 0) {
      perror("pthread_create");
      throw std::exception();
    }
  }

  for (int i = 0; i < numthreads; ++i) {
    if (pthread_join(threads[i], NULL) != 0) {
      perror("pthread_join");
      throw std::exception();
    }
  }


  long connect_time = std::accumulate(
      connect_times.begin(), connect_times.end(), 0L) / connect_times.size();
  long long throughput = std::accumulate(
      throughputs.begin(), throughputs.end(), 0LL) / throughputs.size();
  printf("%10ld\t%19lld\n", connect_time, throughput);



  connect_times.clear();
  throughputs.clear();
  pthread_mutex_destroy(&connect_times_mutex);
  pthread_mutex_destroy(&throughputs_mutex);
}
