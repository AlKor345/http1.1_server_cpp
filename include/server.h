#ifndef SERVER
#define SERVER

#include "type.h"
#include "parsing.h"
#include "proxy_to_backend.h"
#include "ssl.h"
#include "building.h"

const std::unordered_map<int, std::string> HTTP_STATUS_PHRASES = {
    {100, "Continue"}, {101, "Switching Protocols"}, {102, "Processing"}, {103, "Early Hints"},
    {200, "OK"}, {201, "Created"}, {202, "Accepted"}, {203, "Non-Authoritative Information"},
    {204, "No Content"}, {205, "Reset Content"}, {206, "Partial Content"},
    {300, "Multiple Choices"}, {301, "Moved Permanently"}, {302, "Found"},
    {303, "See Other"}, {304, "Not Modified"}, {307, "Temporary Redirect"}, {308, "Permanent Redirect"},
    {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"}, {404, "Not Found"},
    {405, "Method Not Allowed"}, {408, "Request Timeout"}, {409, "Conflict"}, {410, "Gone"},
    {413, "Payload Too Large"}, {418, "I'm a teapot"}, {422, "Unprocessable Entity"},
    {429, "Too Many Requests"},
    {500, "Internal Server Error"}, {501, "Not Implemented"}, {502, "Bad Gateway"},
    {503, "Service Unavailable"}, {504, "Gateway Timeout"}, {505, "HTTP Version Not Supported"}
};
constexpr size_t MAX_REQUEST_SIZE = 10 * 1024 * 1024;
constexpr const char* PORT     = "3490";
constexpr int         BACKLOG  = 10;


void logMessage(const Request& req, const Response& resp, int& lfd) {
    if (lfd == -1) return;

    std::ostringstream oss;
    time_t t         = time(nullptr);
    tm*    local_time = localtime(&t);
    std::ostringstream time_oss;
    time_oss << std::put_time(local_time, "[%Y-%m-%d %H:%M:%S]");
    oss << time_oss.str();

    if (resp.status_code != 200) {
        oss << " [ERROR] ";
        if (!resp.body.empty()) oss << "[" << req.ip << "]";
        oss << std::endl << req.body << "\n\n";
    } else {
        auto ua_it   = req.headers.find("User-Agent");
        auto host_it = req.headers.find("Host");
        std::string ua   = (ua_it   != req.headers.end()) ? ua_it->second   : "-";
        std::string host = (host_it != req.headers.end()) ? host_it->second : "-";
        oss << " [INFO] [" << req.ip << "]\n";
        oss << '"' << req.method << ' ' << req.url << ' ' << req.version << '"';
        oss << ' ' << '"' << ua << '"' << " - " << host << " -\n\n";
    }

    std::string log_msg = oss.str();
    write(lfd, log_msg.c_str(), log_msg.length());
}

void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

ssize_t check_100(Request& http_request, int fd) {
    auto expect_it = http_request.headers.find("Expect");
    if (expect_it == http_request.headers.end() ||
        expect_it->second != "100-continue")
        return 0;

    auto cl_it = http_request.headers.find("Content-Length");
    if (cl_it != http_request.headers.end()) {
        size_t content_length = std::stoul(cl_it->second);
        if (content_length > MAX_REQUEST_SIZE) {
            std::string reject = "HTTP/1.1 413 Payload Too Large\r\n\r\n";
            send(fd, reject.c_str(), reject.size(), 0);
            return -1;
        }
    }

    std::string cont = "HTTP/1.1 100 Continue\r\n\r\n";
    send(fd, cont.c_str(), cont.size(), 0);
    return 0;
}

void sigchld_handler(int s) {
    (void)s;
    int saved_errno = errno;
    while (waitpid(-1, nullptr, WNOHANG) > 0);
    errno = saved_errno;
}

void* get_in_addr(struct sockaddr* sa) {
    if (sa->sa_family == AF_INET)
        return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void read_long_req(std::string& raw, int fd, SSL* ssl, bool use_ssl) {
    while (true) {
        char chunk[4096];
        ssize_t n;
        if (use_ssl && ssl) {
            n = SSL_read(ssl, chunk, sizeof(chunk));
        } else {
            n = recv(fd, chunk, sizeof(chunk), 0);
        }
        if (n <= 0) break;
        raw.append(chunk, n);
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }

    size_t header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) return;

    size_t body_start     = header_end + 4;
    size_t content_length = 0;

    auto cl_pos = raw.find("Content-Length: ");
    if (cl_pos != std::string::npos && cl_pos < header_end) {
        size_t val_start = cl_pos + 16;
        size_t val_end   = raw.find("\r\n", val_start);
        content_length   = std::stoul(raw.substr(val_start, val_end - val_start));
    }

    while (raw.size() - body_start < content_length) {
        if (raw.size() > MAX_REQUEST_SIZE) break;
        char chunk[4096];
        ssize_t n;
        if (use_ssl && ssl) {
            n = SSL_read(ssl, chunk, sizeof(chunk));
        } else {
            n = recv(fd, chunk, sizeof(chunk), 0);
        }
        if (n <= 0) break;
        raw.append(chunk, n);
    }
}


#endif