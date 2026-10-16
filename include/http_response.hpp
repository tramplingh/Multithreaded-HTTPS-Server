#pragma once

#include <map>
#include <string>
using namespace std;

const char* reason_phrase(int status);

struct HttpResponse{
	int status = 200;
	string content_type = "text/plain";
	string body;
	map<string, string> headers;
	
	string serialize() const;
	
	static HttpResponse make(int status, string body, string content_type = "text/plain");
};
