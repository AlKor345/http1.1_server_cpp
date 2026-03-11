#ifndef BUILDING
#define BUILDING
#include "type.h"
std::string build_resp(Response& response) {
    std::ostringstream resp_stream;

    auto it = HTTP_STATUS_PHRASES.find(response.status_code);
    std::string phrase = (it != HTTP_STATUS_PHRASES.end()) ? it->second : "Unknown";

    resp_stream << "HTTP/1.1 " << response.status_code << " " << phrase << "\r\n";

    for (const auto& header : response.headers)
        resp_stream << header.first << ": " << header.second << "\r\n";

    if (response.headers.count("Connection") == 0)
        resp_stream << "Connection: " << (response.keep_alive ? "keep-alive" : "close") << "\r\n";

    auto enc_it = response.headers.find("Transfer-Encoding");
    if (enc_it != response.headers.end() && enc_it->second == "chunked") {
        response.chunked = true;
        size_t body_len  = response.body.size();
        std::ostringstream chunk_header;
        chunk_header << std::hex << body_len << "\r\n";
        resp_stream << "\r\n" << chunk_header.str() << response.body << "\r\n0\r\n\r\n";
    } else {
        // if (response.headers.count("Content-Length") == 0)
        //     resp_stream << "Content-Length: " << response.body.size() << "\r\n";
        resp_stream << "\r\n" << response.body;
    }
    return resp_stream.str();
}

std::string build_req(const Request& request, Response& response) {
    std::ostringstream req_stream;

    auto host_it = request.headers.find("Host");
    if (host_it == request.headers.end()) {
        response.status_code = 400;
        response.body        = "HTTP/1.1 requests must include the 'Host:' header.\r\n";
        return "";
    }
    // if (host_it->second != "127.0.0.1") {
    //     response.status_code = 400;
    //     response.body        = "False host address\r\n";
    //     return "";
    // }

    req_stream << request.method << " " << request.url << " " << request.version << "\r\n";
    for (const auto& header : request.headers)
        req_stream << header.first << ": " << header.second << "\r\n";
    req_stream << "X-Forwarded-For: " << request.ip << "\r\n";
    req_stream << "\r\n" << request.body;
    return req_stream.str();
}
#endif