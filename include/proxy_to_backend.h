
#ifndef P2B
#define P2B
#include "type.h"
void proxy_to_backend(Socket& client_socket,
                      const Request& request,
                      Response& response,
                    SSL_CTX *ctx)
{
    std::string req_str = build_req(request, response);
    if (req_str.empty())
        return;

    const char* BACKEND_HOST = "127.0.0.1";
    const char* BACKEND_PORT = "8080";

    if (request.ssl) {

        struct addrinfo hints_s{};
        hints_s.ai_family   = AF_UNSPEC;
        hints_s.ai_socktype = SOCK_STREAM;
        struct addrinfo* res_ssl = nullptr;
        if (getaddrinfo(BACKEND_HOST, BACKEND_PORT, &hints_s, &res_ssl) != 0) {
            response.status_code = 502;
            return;
        }
        std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> si(res_ssl, freeaddrinfo);

        int sock_fd = -1;
        for (auto p = si.get(); p; p = p->ai_next) {
            sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (sock_fd == -1) continue;
            if (connect(sock_fd, p->ai_addr, p->ai_addrlen) == 0) break;
            ::close(sock_fd);
            sock_fd = -1;
        }
        if (sock_fd == -1) { response.status_code = 502; return; }

        SSL* ssl = nullptr;
        if (connect_ssl(ssl, sock_fd, BACKEND_HOST, ctx) != 1) {
            response.status_code = 502;
            ::close(sock_fd);
            return;
        }

        send_ssl(ssl, req_str);

        char backend_buffer[4096];
        ssize_t backend_bytes;

        while ((backend_bytes = read_ssl(ssl, backend_buffer, sizeof(backend_buffer))) > 0) {

            int cfd = client_socket.get();
            if ((size_t)cfd < ssl_connections.size() && ssl_connections[cfd]) {
                if (send_ssl(ssl_connections[cfd],
                             std::string(backend_buffer, backend_bytes)) == -1) break;
            } else {
                if (send(cfd, backend_buffer, backend_bytes, 0) == -1) break;
            }
            response.total_sent += backend_bytes;
        }

        SSL_shutdown(ssl);
        SSL_free(ssl);
        ::close(sock_fd);
        return;
    }

    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int rv = getaddrinfo(BACKEND_HOST, BACKEND_PORT, &hints, &result);
    if (rv != 0) {
        std::cerr << "proxy: getaddrinfo error: " << gai_strerror(rv) << "\n";
        response.status_code = 502;
        return;
    }
    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> servinfo(result, freeaddrinfo);

    Socket backend_socket;
    for (auto p = servinfo.get(); p != nullptr; p = p->ai_next) {
        int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd == -1) { std::cerr << "proxy: socket: " << std::strerror(errno) << "\n"; continue; }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == -1) {
            std::cerr << "proxy: connect: " << std::strerror(errno) << "\n";
            ::close(fd); continue;
        }
        backend_socket = Socket(fd);
        break;
    }

    if (!backend_socket.is_valid()) {
        std::cerr << "proxy: failed to connect to backend\n";
        response.status_code = 502;
        return;
    }

    if (send(backend_socket.get(), req_str.c_str(), req_str.size(), 0) == -1) {
        std::cerr << "proxy: send to backend failed: " << std::strerror(errno) << "\n";
        response.status_code = 500;
        return;
    }

    char backend_buffer[4096];
    ssize_t backend_bytes;
    while ((backend_bytes = recv(backend_socket.get(), backend_buffer, sizeof(backend_buffer), 0)) > 0) {
        if (send(client_socket.get(), backend_buffer, backend_bytes, 0) == -1) {
            std::cerr << "proxy: send to client failed: " << std::strerror(errno) << "\n";
            break;
        }
        response.total_sent += backend_bytes;
    }
}

#endif