// TCP server + HTTP/1.1 parser + static file serving
// socket() -> bind() -> listen() -> accept() -> recv() -> send() -> close()

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <http_parser.hpp>
#include <http_request.hpp>
#include <http_response.hpp>
#include "static_file_handler.hpp"

using namespace std;

constexpr int PORT = 8080;
constexpr int BACKLOG = 128;
constexpr int RECV_TIMEOUT_SEC = 5;
constexpr const char* DOCUMENT_ROOT = "./www";

// Calls recv() until parser says request is complete or broken.
// Returns false if connection dies.
bool read_requests(int fd, HttpRequest& req, ParseResult& result){
	string buf;
	char chunk[4096];
	
	while(true){
		result = parse_http_request(buf, req);
		if(result.status != ParseStatus::Incomplete) return true;
		
		ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
		if(n < 0){
			if(errno == EINTR) continue;
			if(errno == EAGAIN || errno == EWOULDBLOCK){
				result.status = ParseStatus::Error;
				result.error_code = 408;
				result.error_msg = "request timed out";
				return true;
			}
			return false;
		}
		if(n == 0) return false;
		buf.append(chunk, static_cast<size_t>(n));
	}
}

HttpResponse handle_request(const HttpRequest& req, const StaticFileHandler& files){
	if(req.path == "/echo"){
		if(req.method != "POST"){
			HttpResponse r = HttpResponse::make(405, "405 Method Not Allowed\n");
			r.headers["Allow"] = "POST";
			return r;
		}
		return HttpResponse::make(200, req.body);
	}
	
	if(req.method != "GET"){
		HttpResponse r = HttpResponse::make(405, "405 Method Not Allowed\n");
		r.headers["Allow"] = "GET";
		return r;
	}
	return files.handle(req);
}

bool send_all(int fd, const string& data){
	size_t total = 0;
	while(total < data.size()){
		ssize_t sent = send(fd, data.data() + total, data.size() - total, MSG_NOSIGNAL);
		if(sent < 0){
			if(errno == EINTR) continue;
			return false;
		}
		total += static_cast<size_t>(sent);
	}
	return true;
}

// Creates socket for client; sets a timeout.
void handle_client(int client_fd, const StaticFileHandler& files){
	timeval tv{};
	tv.tv_sec = RECV_TIMEOUT_SEC;
	setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
	
	HttpRequest req;
	ParseResult result;
	
	if(!read_requests(client_fd, req, result)){
		close(client_fd);
		return;
	}
	
	HttpResponse resp;
	if(result.status == ParseStatus::Error){
		cout<<" -> parse error "<<result.error_code<<": "<<result.error_msg<<"\n";
		resp = HttpResponse::make(result.error_code, result.error_msg + "\n");
	} else{
		resp = handle_request(req, files);
		cout<<" -> "<<req.method<<" "<<req.path<<" "<<req.version<<" => "<<resp.status<<" ("<<resp.body.size()<<"B)\n";
	}
	
	send_all(client_fd, resp.serialize());
	close(client_fd);
}

// main function
int main(){
	StaticFileHandler files(DOCUMENT_ROOT);
	if(!files.ok()){
		std::cerr<<"Document root '"<<DOCUMENT_ROOT<<"' not found. Run the server from the project root.\n";
		return 1;
	}
	
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	if(sfd < 0){
		cerr<<"socket() failed: "<<strerror(errno)<<"\n";
		return 1;
	}
	int opt = 1;
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons(PORT);
	
	if(bind(sfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))<0){
		cerr<<"bind() failed: "<<strerror(errno)<<"\n";
		close(sfd);
		return 1;
	}
	if(listen(sfd, BACKLOG)<0){
		cerr<<"listen() failed: "<<strerror(errno)<<"\n";
		close(sfd);
		return 1;
	}
	
	cout<<"Serving "<<DOCUMENT_ROOT<<" on port "<<PORT<<"...\n";
	
	while(true){
		sockaddr_in client_addr{};
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(sfd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
		
		if(client_fd < 0){
			if(errno == EINTR) continue;
			cerr<<"accept() failed: "<<strerror(errno)<<"\n";
			break;
		}
		
		char ip[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, ip, sizeof(ip));
		cout<<"Connection from "<<ip<<":"<<ntohs(client_addr.sin_port)<<"\n";
		
		handle_client(client_fd, files);
	}
	
	close(sfd);
	return 0;
}

