#pragma once 

#include <cstddef>
#include <string>

#include "http_request.hpp"
using namespace std;

constexpr size_t MAX_HEADERS_BYTES = 8 * 1024;
constexpr size_t MAX_HEADER_COUNT = 100;
constexpr size_t MAX_BODY_BYTES = 1024 * 1024;

enum class ParseStatus{
	Complete,
	Incomplete,
	Error
};

struct ParseResult{
	ParseStatus status = ParseStatus::Incomplete;
	int error_code = 400;
	string error_msg;
	size_t consumed = 0;
};

ParseResult parse_http_request(const string& buf, HttpRequest& out);
