#include "http_parser.hpp"

#include <algorithm>
#include <cctype>
#include <utility>
using namespace std;

namespace{
	ParseResult fail(int code, string msg){
		ParseResult r;
		r.status = ParseStatus::Error;
		r.error_code = code;
		r.error_msg = move(msg);
		return r;
	}
	ParseResult incomplete() {
		return ParseResult{};
	}
	
	string to_lower(string s){
		transform(s.begin(), s.end(), s.begin(), 
			[](unsigned char c) {
				return static_cast<char>(tolower(c));});
		return s;
	}
	string trim(const string& s){
		size_t b = s.find_first_not_of(" \t");
		if(b == string::npos) return "";
		size_t e = s.find_last_not_of(" \t");
		return s.substr(b, e - b + 1);
	}
	
	bool is_token(const string& s){
		if(s.empty()) return false;
		static const string extra = "!#$%&'*+-.^_`|~";
		for(unsigned char c : s){
			if(!isalnum(c) && extra.find(static_cast<char>(c)) == string::npos){
				return false;
			}
		}
		return true;
	}
	
	bool parse_content_length(const string& v, size_t& out){
		if(v.empty() || v.size() > 9) return false;
		for(unsigned char c : v){
			if(!isdigit(c)) return false;
		}
		out = static_cast<size_t>(stoull(v));
		return true;
	}
	
	}
	
ParseResult parse_http_request(const string& buf, HttpRequest& out){
    	size_t header_end = buf.find("\r\n\r\n");
    	if(header_end == string::npos){
        if(buf.size() > MAX_HEADERS_BYTES) return fail(431, "headers too large");
        return incomplete();
    }
    if(header_end > MAX_HEADERS_BYTES) return fail(431, "headers too large");
 
    const string head = buf.substr(0, header_end);
 
    size_t line_end = head.find("\r\n");
    const string request_line = head.substr(0, line_end); 
 
    size_t sp1 = request_line.find(' ');
    size_t sp2 = (sp1 == string::npos) ? string::npos
                                                 : request_line.find(' ', sp1 + 1);
    if(sp1 == string::npos || sp2 == string::npos) {
        return fail(400, "malformed request line");
    }
 
    HttpRequest req;
    req.method = request_line.substr(0, sp1);
    req.path = request_line.substr(sp1 + 1, sp2 - sp1 - 1);
    req.version = request_line.substr(sp2 + 1);
 
    if(!is_token(req.method)) return fail(400, "invalid method");
    if(req.path.empty() || req.path[0] != '/' || req.path.find(' ') != std::string::npos) {
        return fail(400, "invalid request target");
    }
    if(req.version.rfind("HTTP/", 0) != 0) return fail(400, "invalid HTTP version");
    if(req.version != "HTTP/1.1" && req.version != "HTTP/1.0") {
        return fail(505, "HTTP version not supported");
    }
 
    size_t pos = (line_end == string::npos) ? head.size() : line_end + 2;
    while(pos < head.size()) {
        size_t next = head.find("\r\n", pos);
        string line = (next == string::npos) ? head.substr(pos)
                                                       : head.substr(pos, next - pos);
        pos = (next == string::npos) ? head.size() : next + 2;
 
        size_t colon = line.find(':');
        if(colon == string::npos || colon == 0) return fail(400, "malformed header");
 
        string name = line.substr(0, colon);
        if(!is_token(name)) return fail(400, "invalid header name");  
        name = to_lower(std::move(name));
        string value = trim(line.substr(colon + 1));
 
        auto existing = req.headers.find(name);
        if(name == "content-length" && existing != req.headers.end() && existing->second != value) {
            return fail(400, "conflicting Content-Length");
        }
 
        req.headers[name] = std::move(value);
        if(req.headers.size() > MAX_HEADER_COUNT) return fail(431, "too many headers");
    }
 
    if(req.version == "HTTP/1.1" && req.headers.find("host") == req.headers.end()) {
        return fail(400, "missing Host header");
    }
 
    if(req.headers.count("transfer-encoding")) {
        return fail(501, "Transfer-Encoding not supported");
    }
 
    size_t content_length = 0;
    auto cl = req.headers.find("content-length");
    if(cl != req.headers.end()) {
        if(!parse_content_length(cl->second, content_length)) {
            return fail(400, "invalid Content-Length");
        }
        if(content_length > MAX_BODY_BYTES) return fail(413, "body too large");
    }
 
    const size_t body_start = header_end + 4;
    if(buf.size() < body_start + content_length) return incomplete(); 
 
    req.body = buf.substr(body_start, content_length);
 
    out = move(req);
    ParseResult done;
    done.status = ParseStatus::Complete;
    done.consumed = body_start + content_length;
    return done;
}

