#include "static_file_handler.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>
using namespace std;
namespace fs = std::filesystem;

namespace{
	int hex_value(char c){
		if(c >= '0' && c <= '9') return c - '0';
		if(c >= 'a' && c <= 'f') return c - 'a' + 10;
		if(c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}
}

StaticFileHandler::StaticFileHandler(const string& document_root){
	std::error_code ec;
	root_ = fs::canonical(document_root, ec);
	if(ec) return;
	ok_ = fs::is_directory(root_, ec);
}

bool StaticFileHandler::url_decode(const string& in, string& out){
	out.clear();
	for(size_t i = 0; i < in.size(); ++i){
		if(in[i] == '%'){
			if(i+2 >= in.size()) return false;
			int hi = hex_value(in[i+1]);
			int lo = hex_value(in[i+2]);
			if(hi < 0 || lo < 0) return false;
			out.push_back(static_cast<char>(hi * 16 + lo));
			i += 2;
		} else{
			out.push_back(in[i]);
		}
	}
	return true;
}

string StaticFileHandler::mime_type(const fs::path& p){
	static const unordered_map<string, string> types = {
		{".html", "text/html; charset=utf-8"},
		{".htm", "text/html; charset=utf-8"},
		{".css", "text/css; charset=utf-8"},
		{".js", "application/javascript; charset=utf-8"},
		{".json", "application/json"},
		{".txt", "text/plain; charset=utf-8"},
		{".png", "image/png"},
		{".jpg", "image/jpeg"},
		{".jpeg", "image/jpeg"},
		{".svg", "image/jpeg"},
		{".ico", "image/x-icon"},
	};
	
	string ext = p.extension().string();
	transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { 
		return static_cast<char>(tolower(c));
	});
	auto it = types.find(ext);
	return it == types.end() ? "application/octet-stream" : it->second;
}

HttpResponse StaticFileHandler::handle(const HttpRequest & req) const{
	if(!ok_) return HttpResponse::make(500, "500 Internal Server Error\n");
	
	string target = req.path;
	size_t cut = target.find_first_of("?#");
	if(cut != string::npos) target.resize(cut);
	
	string decoded;
	if(!url_decode(target, decoded)) return HttpResponse::make(400, "400 Bad Request\n");
	if(decoded.find('\0') != string::npos) return HttpResponse::make(400, "400 Bad Request\n");
	
	size_t start = decoded.find_first_not_of('/');
	string rel_str = (start == string::npos) ? "" : decoded.substr(start);
	
	if (rel_str.empty() || rel_str.back() == '/') rel_str += "index.html";
	
	error_code ec;
	fs::path candidate = fs::weakly_canonical(root_ / rel_str, ec);
	if(ec) return HttpResponse::make(404, "404 Not Found\n");
	
	fs::path rel = candidate.lexically_relative(root_);
	if(rel.empty() || *rel.begin() == ".."){
		return HttpResponse::make(404, "404 Not Found\n");
	}
	if(!fs::is_regular_file(candidate, ec) || ec){
		return HttpResponse::make(404, "404 Not Found\n");
	}
	
	ifstream file(candidate, ios::binary);
	if(!file) return HttpResponse::make(500, "500 Internal Server Error");
	
	uintmax_t size = fs::file_size(candidate, ec);
	if(ec) return HttpResponse::make(500, "500 Internal Server Error\n");
	
	string body(static_cast<size_t>(size),'\0');
	file.read(body.data(), static_cast<streamsize>(size));
	body.resize(static_cast<size_t>(file.gcount()));
	
	HttpResponse resp = HttpResponse::make(200, move(body), mime_type(candidate));
	resp.headers["X-Content-Type-Options"] = "nosniff";
	return resp;
}
