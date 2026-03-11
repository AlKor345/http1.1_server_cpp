#include "server.h"

int main() {
    struct addrinfo hints{};
    struct sockaddr_storage their_addr{};
    socklen_t sin_size;
    struct sigaction sa{};
    SSL_CTX* ctx = nullptr;
    bool ssl_ready = false;
    char s[INET6_ADDRSTRLEN];
    int rv;

    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;

    struct addrinfo* result = nullptr;
    if ((rv = getaddrinfo(nullptr, PORT, &hints, &result)) != 0) {
        std::cerr << "getaddrinfo: " << gai_strerror(rv) << "\n";
        return 1;
    }

    int lfd = open("server_log.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);

    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> servinfo(result, freeaddrinfo);

    Socket sockfd;
    for (auto p = servinfo.get(); p != nullptr; p = p->ai_next) {
        int tmp_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (tmp_fd == -1) {
            std::cerr << "server: socket: " << std::strerror(errno) << "\n";
            continue;
        }
        sockfd = Socket(tmp_fd);

        int yes = 1;
        if (setsockopt(sockfd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
            throw std::system_error(errno, std::generic_category(), "setsockopt");

        if (bind(sockfd.get(), p->ai_addr, p->ai_addrlen) == -1) {
            sockfd.close();
            std::cerr << "server: bind: " << std::strerror(errno) << "\n";
            continue;
        }
        break;
    }

    if (!sockfd.is_valid()) {
        std::cerr << "server: failed to bind\n";
        return 2;
    }

    if (listen(sockfd.get(), BACKLOG) == -1)
        throw std::system_error(errno, std::generic_category(), "listen");

    sa.sa_handler = sigchld_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, nullptr) == -1)
        throw std::system_error(errno, std::generic_category(), "sigaction");

    if (set_context(&ctx, "server.crt", "server.key") == -1) {
        std::cerr << errno << " ssl_ctx\n";
    } else {
        ssl_ready = true;
    }
    signal(SIGPIPE, SIG_IGN);

    std::cout << "server: waiting for connections...\n";

    while (true) {
        sin_size = sizeof their_addr;
        int new_fd = accept(sockfd.get(), (struct sockaddr*)&their_addr, &sin_size);
        if (new_fd == -1) {
            std::cerr << "accept: " << std::strerror(errno) << "\n";
            continue;
        }

        inet_ntop(their_addr.ss_family,
                  get_in_addr((struct sockaddr*)&their_addr),
                  s, sizeof s);
        std::cout << "server: got connection from " << s << "\n";

        std::string client_ip(s);

        pid_t pid = fork();
        if (pid == -1) {
            std::cerr << "fork: " << std::strerror(errno) << "\n";
            ::close(new_fd);
            continue;
        }

        if (pid == 0) {
            sockfd.close();
            Socket new_socket(new_fd);

            struct timeval tv;
            tv.tv_sec  = 30;
            tv.tv_usec = 0;
            setsockopt(new_socket.get(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

            char buffer[4096];
            Request  request;
            Response response;

            while (true) {
                std::string raw;
                raw.reserve(8192);
                response  = Response{};
                request   = Request{};
                int fd_key = new_socket.get();

                ssize_t bytes_read = recv(new_socket.get(), buffer, sizeof(buffer) - 1, 0);

                if (bytes_read <= 0) {
                    response.status_code = 400;
                    response.body        = "Bad Request";
                    goto send_response;
                }

                if ((unsigned char)buffer[0] == 0x16) {
                    if (!ssl_ready) {
                        response.status_code = 400;
                        response.body        = "Bad Request";
                        goto send_response;
                    }
                    request.ssl = true;
                    if (!((size_t)fd_key < ssl_connections.size() &&
                          ssl_connections[fd_key] != nullptr))
                    {
                        if (accept_ssl(fd_key, ctx) < 0) {
                            response.status_code = 400;
                            response.body        = "Bad Request";
                            goto send_response;
                        }
                    }
                    if (read_ssl(ssl_connections[fd_key], buffer, sizeof(buffer) - 1) < 0) {
                        response.status_code = 400;
                        response.body        = "Bad Request";
                        goto send_response;
                    }
                    bytes_read = sizeof(buffer) - 1;
                }

                raw.append(buffer, bytes_read);
                read_long_req(raw, fd_key,
                              (size_t)fd_key < ssl_connections.size()
                                  ? ssl_connections[fd_key] : nullptr,
                              request.ssl);

                request = request_parse(raw.c_str(), (ssize_t)raw.size(), client_ip);

                if (check_100(request, fd_key) == -1) {
                    response.status_code = 413;
                    response.body        = "Payload Too Large";
                    goto send_response;
                }

                proxy_to_backend(new_socket, request, response, ctx);

                send_response:
                std::string str_response = build_resp(response);

                if (response.total_sent == 0) {
                    if (response.chunked) {
                        ssize_t res_len    = (ssize_t)str_response.size();
                        size_t  total_sent = 0;
                        while (total_sent < (size_t)res_len) {
                            ssize_t bytes_sent = send(new_socket.get(),
                                str_response.substr(total_sent).c_str(),
                                res_len - total_sent, 0);
                            if (bytes_sent == -1) {
                                std::cerr << "send error: " << std::strerror(errno) << "\n";
                                break;
                            }
                            total_sent += bytes_sent;
                        }
                    } else {
                        if (request.ssl &&
                            (size_t)fd_key < ssl_connections.size() &&
                            ssl_connections[fd_key])
                        {
                            SSL_write(ssl_connections[fd_key],
                                      str_response.c_str(),
                                      (int)str_response.size());
                        } else {
                            send(new_socket.get(),
                                 str_response.c_str(), str_response.size(), 0);
                        }
                    }
                }

                logMessage(request, response, lfd);

                auto it = request.headers.find("Connection");
                if (it == request.headers.end() || it->second != "keep-alive") {
                    if (request.ssl &&
                        (size_t)fd_key < ssl_connections.size() &&
                        ssl_connections[fd_key])
                    {
                        SSL_shutdown(ssl_connections[fd_key]);
                        SSL_free(ssl_connections[fd_key]);
                        ssl_connections[fd_key] = nullptr;
                    }
                    break;
                }
            }

            exit(0);
        } else {
            ::close(new_fd);
        }
    }

    SSL_CTX_free(ctx);
    return 0;
}