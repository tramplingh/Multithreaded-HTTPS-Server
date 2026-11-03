#pragma once

#include <openssl/ssl.h>

#include <cstddef>
#include <string>

using namespace std;

string tls_error_string();

class TlsContext{
	public:
		TlsContext(const string& cert_file, const string& key_file);
		~TlsContext();
		
		TlsContext(const TlsContext&) = delete;
		TlsContext& operator = (const TlsContext&) = delete;
		
		SSL_CTX* get() const{
			return ctx_;
		}
		
	private:
		SSL_CTX* ctx_ = nullptr;
	};
	
enum class TlsIo{
	Ok,      //Bytes were read
	Closed,  //Peer closed connection cleanly
	Timeout, //socket receive timeout fired
	Error    //protocol or I/O error; Connection is unusable
};


class TlsConnection{
	public:
		TlsConnection(SSL_CTX* ctx, int fd);
		~TlsConnection();
		
		TlsConnection(const TlsConnection&) = delete;
		TlsConnection& operator = (const TlsConnection&) = delete;
		
		bool ok() const{
			return ssl_ != nullptr;
		}
		
		bool handshake();
		
		TlsIo read(char* buf, size_t len, size_t& n);
		
		bool write_all(const string& data);
		
		void shutdown();
		
	private:
		SSL* ssl_ = nullptr;
		bool handshaken_ = false;
		bool fatal_ = fakse;
	};
	
		
		
		
