#define TYPES
#ifndef TYPES
#include <iostream>
#include <string>
#include <cstring>
#include <stdexcept>
#include <system_error>
#include <memory>
#include <cstdlib>
#include <vector>
#include <map>
#include <sstream>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <unordered_map>
#include <sys/epoll.h>
#include <fcntl.h>
#include <iomanip>
#include <openssl/ssl.h>
#include <openssl/err.h>
    struct Request {
        std::string method;
        std::string url;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
        std::string ip;
        bool ssl = false;
    };

    struct Response {
        int status_code = 200;
        std::map<std::string, std::string> headers;
        std::string body;
        bool keep_alive = false;
        bool chunked    = false;
        size_t total_sent = 0;
    };
    std::vector<SSL*> ssl_connections;

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}

    Socket(const Socket&)            = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) { close(); fd_ = other.fd_; other.fd_ = -1; }
        return *this;
    }

    ~Socket() { close(); }

    int  get()      const { return fd_; }
    bool is_valid() const { return fd_ != -1; }

    void close() {
        if (fd_ != -1) { ::close(fd_); fd_ = -1; }
    }

private:
    int fd_;
};

std::string normalize(std::string url) {
    auto parts  = split(url, "?");
    std::string path  = parts[0];
    std::string query = parts.size() > 1 ? "?" + parts[1] : "";

    std::vector<std::string> segments;
    for (const auto& seg : split(path, "/")) {
        if (seg.empty() || seg == ".") {
            continue;
        } else if (seg == "..") {
            if (!segments.empty()) segments.pop_back();
        } else {
            segments.push_back(seg);
        }
    }

    std::string result = "/";
    for (size_t i = 0; i < segments.size(); ++i) {
        if (i > 0) result += '/';
        result += segments[i];
    }
    return result + query;
}

std::vector<std::string> split(const std::string& str, const std::string& del) {
    std::vector<std::string> strings;
    size_t start = 0, end = str.find(del);
    while (end != std::string::npos) {
        strings.push_back(str.substr(start, end - start));
        start = end + del.length();
        end   = str.find(del, start);
    }
    strings.push_back(str.substr(start));
    return strings;
}
#endif
