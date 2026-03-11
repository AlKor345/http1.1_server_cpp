#include <iostream>
#include <string>
#include <sstream>

int main(){
    std::string body = "\r\n\r\n5\r\nHello\r\n6\r\nWorld\r\n0\r\n\r\n";
    ssize_t read_body_count = 0;
    std::ostringstream body_read;
    ssize_t start = 0;
    ssize_t end;
    while(true)
    {
        start = body.find("\r\n"), start + 2;
        end = body.find("\r\n"), start + 2; 
        std::cout << body.substr(start, end) << '\n'<<"-----------------"<< '\n';
        body_read << body.substr(start, end);
    }
}