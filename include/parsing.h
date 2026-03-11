
#ifndef PARSING
#define PARSING
#include "type.h"

Request request_parse(const char* buffer, ssize_t bytes_read, const std::string& client_ip) {
    Request http_request;
    std::string str_buf(buffer, bytes_read);
    http_request.ip = client_ip;

    size_t first_crlf = str_buf.find("\r\n");
    if (first_crlf == std::string::npos) return http_request;

    std::vector<std::string> line = split(str_buf.substr(0, first_crlf), " ");
    if (line.size() >= 3) {
        http_request.method  = line[0];
        http_request.url     = normalize(line[1]);
        http_request.version = line[2];
    }

    size_t header_end = str_buf.find("\r\n\r\n");
    std::string headers_section = (header_end != std::string::npos)
        ? str_buf.substr(first_crlf + 2, header_end - (first_crlf + 2))
        : str_buf.substr(first_crlf + 2);
    
    for (const auto& h : split(headers_section, "\r\n")) {
        size_t colon = h.find(": ");
        if (colon != std::string::npos)
            http_request.headers[h.substr(0, colon)] = h.substr(colon + 2);
    }

    if (header_end != std::string::npos) {
        auto it = http_request.headers.find("Transfer-Encoding");
        if (it != http_request.headers.end() && it->second == "chunked") {
            read_chuncked(str_buf.substr(header_end), http_request);
        } else {
            http_request.body = str_buf.substr(header_end + 4);
        }
    }
    return http_request;
}

void read_chuncked(std::string &body, Request &req){
    ssize_t read_body_count = 0;
    std::ostringstream body_read;
    ssize_t start = 0;
    ssize_t end;
    int counter = 0;
    while(true)
    {
        start = body.find("\r\n", start + 2);
        end = body.find("\r\n", start + 2); 
        if(counter % 2 == 0){
            read_body_count += std::strtoul(body.substr(start + 2, end- (start + 2)).c_str(), nullptr, 16);
        }
        else{
            body_read << body.substr(start + 2, end- (start + 2)) << "\r\n";
        }
        counter++;

        if(end >= body.find("\r\n0\r\n\r\n")){
          break;
        }
    }
    body_read << "\r\n";
    req.body = body_read.str();
    req.headers["Content-Length"] = std::to_string(read_body_count);
}
#endif