#pragma once

#include <filesystem>
#include <string>

#include "http_request.hpp"
#include "http_response.hpp"
using namespace std;

class StaticFileHandler{
public:
	explicit StaticFileHandler(const string& document_root);
	
	bool ok() const { return ok_; }
	
	HttpResponse handle(const HttpRequest& req) const;
	
	static string mime_type(const filesystem::path& p);
	
	static bool url_decode(const string& in, string& out);
	
private:
	filesystem::path root_;
	bool ok_ = false;
};
