#include "http_response.hpp"

#include <utility>
using namespace std;

const char* reason_phrase(int status){
	switch(status){
		case 200: return "OK";
		case 400: return "Bad Request";
		case 404: return "Not Found";
		case 405: return "Method Not Allowed";
		case 408: return "Request Timeout";
		case 413: return "Payload Too Large";
		case 431: return "Request header Fields Too Large";
		case 500: return "Internal Server Error";
		case 501: return "Not Implemented";
		case 505: return "HTTP Version Not Supported";
		default: return "Unknown";
	}
}

string HttpResponse::serialize() const{
	string out = "HTTP/1.1 " + to_string(status) + " " + reason_phrase(status) + "\r\n";
	out += "Content-Type: " + content_type + "\r\n";
	out += "Content-Length: " + to_string(body.size()) + "\r\n";
	out += "Connection: close\r\n";
	for(const auto& [name, value] : headers){
		out += name + ": "+value+"\r\n";
	}
	out += "\r\n";
	out += body;
	return out;
}

HttpResponse HttpResponse::make(int status, string body, string content_type){
	HttpResponse r;
	r.status = status;
	r.body = move(body);
	r.content_type = move(content_type);
	return r;
}
