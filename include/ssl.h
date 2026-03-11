
#ifndef SSL
#define SSL
#include "type.h"
std::vector<SSL*> ssl_connections;

ssize_t set_context(SSL_CTX** ctx, const char* cert_file, const char* prv_file) {
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    *ctx = SSL_CTX_new(TLS_server_method());  // записываем результат по адресу
    if (!*ctx) return -1;

    SSL_CTX_set_options(*ctx, SSL_OP_ALL);

    if (SSL_CTX_use_certificate_file(*ctx, cert_file, SSL_FILETYPE_PEM) != 1) {
        std::cerr << "Certificate file not valid\n";
        SSL_CTX_free(*ctx);
        *ctx = nullptr;
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(*ctx, prv_file, SSL_FILETYPE_PEM) != 1) {
        std::cerr << "Private key file not valid\n";
        SSL_CTX_free(*ctx);
        *ctx = nullptr;
        return -1;
    }
    return 0;
}
ssize_t accept_ssl(int fd, SSL_CTX* ctx) {

    SSL* cSSL = SSL_new(ctx);
    SSL_set_fd(cSSL, fd);

    if ((size_t)fd >= ssl_connections.size())
        ssl_connections.resize(fd + 1, nullptr);
    ssl_connections[fd] = cSSL;

    if (SSL_accept(cSSL) <= 0) {
        ERR_print_errors_fp(stderr);
        SSL_free(cSSL);
        ssl_connections[fd] = nullptr;
        return -1;
        SSL_CTX_free(ctx);
    }
    SSL_CTX_free(ctx);
    return 1;
}

ssize_t read_ssl(SSL* ssl, char* buffer, size_t buf_size) {
    ssize_t bytes_read = SSL_read(ssl, buffer, (int)buf_size);
    return bytes_read;
}

ssize_t send_ssl(SSL* ssl, const std::string& data) {
    return SSL_write(ssl, data.c_str(), (int)data.size());
}

ssize_t connect_ssl(SSL* ssl, int sock_fd, const std::string& host, SSL_CTX* ctx) {

    SSL* ssl_ = SSL_new(ctx);
    SSL_CTX_free(ctx);
    if (!ssl_) return -1;

    SSL_set_fd(ssl_, sock_fd);
    SSL_set_tlsext_host_name(ssl_, host.c_str());

    if (SSL_connect(ssl_) != 1) {
        ERR_print_errors_fp(stderr);
        SSL_free(ssl_);
        return -1;
    }
    if (SSL_get_verify_result(ssl_) != X509_V_OK) {
        std::cerr << "Certificate verification failed\n";
        SSL_free(ssl_);
        return -1;
    }

    ssl = ssl_;
    return 1;
}
#endif