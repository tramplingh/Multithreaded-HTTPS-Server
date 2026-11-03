// TCP server + TLS (OpenSSL) + HTTP parser + static file serving + thread pool

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/time.h>

#include <chrono>
#include <csignal>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>
#include <mutex>
#include <thread>
#include <memory>
#include <sstream>
#include <stdexcept>


#include <http_parser.hpp>
#include <http_request.hpp>
#include <http_response.hpp>
#include "static_file_handler.hpp"
#include "thread_pool.hpp"
#include "tls.hpp"

using namespace std;

constexpr int PORT = 8080;
constexpr int BACKLOG = 128;
constexpr int RECV_TIMEOUT_SEC = 5;
constexpr const char* DOCUMENT_ROOT = "./www";
constexpr const char* CERT_FILE = "./cert/server.crt";
constexpr const char* KEY_FILE = "./cert/server.key";
constexpr size_t NUM_THREADS = 4;
constexpr size_t MAX_QUEUE = 256;

volatile sig_atomic_t g_running = 1;
void on_signal(int){ g_running = 0; }

mutex g_log_mutex;
void log_line(const string& line){
	lock_guard<mutex> lock(g_log_mutex);
	cout<<line<<"\n";
}

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
	if(req.path == "/slow"){
		this_thread::sleep_for(chrono::seconds(2));
		return HttpResponse::make(200, "slow response done\n");
	}
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
		log_line(" -> parse error " +  to_string(result.error_code) + ": " + result.error_msg);
		resp = HttpResponse::make(result.error_code, result.error_msg + "\n");
	} else{
		resp = handle_request(req, files);
		ostringstream line;
		line<<" -> "<<req.method<<" "<<req.path<<" "<<req.version<<" => "<<resp.status<<" )"<<resp.body.size()<<"B)";
		log_line(line.str());
	}
	
	send_all(client_fd, resp.serialize());
	close(client_fd);
}

// main function
int main(){
	struct sigaction sa{};
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, nullptr);
	sigaction(SIGTERM, &sa, nullptr);
	
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
	
	ThreadPool pool(NUM_THREADS, MAX_QUEUE);
	
	cout<<"Serving "<<DOCUMENT_ROOT<<" on port "<<PORT<<" with "<<NUM_THREADS<<" worker threads...\n";
	
	while(g_running){
		sockaddr_in client_addr{};
		socklen_t client_len = sizeof(client_addr);
		int client_fd = accept(sfd, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
		
		if(client_fd < 0){
			if(errno == EINTR) continue;
			cerr<<"accept() failed: "<<strerror(errno)<<"\n";
			break;
		}
		
		bool queued = pool.enqueue([client_fd, &files] {
			handle_client(client_fd, files);
		});
		
		if(!queued){
			send_all(client_fd, HttpResponse::make(503, "503 Service Unavailable\n").serialize());
			close(client_fd);
		}
	}
	
	cout<<"\nShutting down...\n";
	close(sfd);
	return 0;
}

