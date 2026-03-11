#ifndef P2B
#define P2B
#include "type.h"
#include "building.h"
#include "ssl.h"

void proxy_to_backend(Socket& client_socket,
                      const Request& request,
                      Response& response,
                      SSL_CTX* ctx)
{
    std::string req_str = build_req(request, response);
    if (req_str.empty()) return;

    const char* BACKEND_HOST = "127.0.0.1";
    const char* BACKEND_PORT = "8080";

    // TCP соединение — одно для обоих случаев
    struct addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(BACKEND_HOST, BACKEND_PORT, &hints, &res) != 0) {
        response.status_code = 502;
        return;
    }
    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> si(res, freeaddrinfo);

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
    if (request.ssl) {
        if (connect_ssl(ssl, sock_fd, BACKEND_HOST, ctx) != 1) {
            response.status_code = 502;
            ::close(sock_fd);
            return;
        }
    }

    auto sendBackend = [&](const std::string& data) -> ssize_t {
        if (ssl) return send_ssl(ssl, data);
        return send(sock_fd, data.c_str(), data.size(), 0);
    };

    auto recvBackend = [&](char* buf, size_t size) -> ssize_t {
        if (ssl) return read_ssl(ssl, buf, size);
        return recv(sock_fd, buf, size, 0);
    };

    auto sendClient = [&](const char* buf, ssize_t n) -> ssize_t {
        int cfd = client_socket.get();
        if ((size_t)cfd < ssl_connections.size() && ssl_connections[cfd])
            return send_ssl(ssl_connections[cfd], std::string(buf, n));
        return send(cfd, buf, n, 0);
    };

    if (sendBackend(req_str) == -1) {
        response.status_code = 500;
    } else {
        char buf[4096];
        ssize_t n;
        while ((n = recvBackend(buf, sizeof(buf))) > 0) {
            if (sendClient(buf, n) == -1) break;
            response.total_sent += n;
        }
    }

    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); }
    ::close(sock_fd);
}
#endif