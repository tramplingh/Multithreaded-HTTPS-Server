#pragma once

#include <string>
#include <unordered_map>
using namespace std;

struct HttpRequest{
	string method;
	string path;
	string version;
	unordered_map<string, string> headers;
	string body;
	
	string header(const string& name) const{
		auto it = headers.find(name);
		return it == headers.end() ? string{} : it->second;
	}
};
