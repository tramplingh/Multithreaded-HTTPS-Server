#include "tls.hpp"

#include <openssl/err.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <stdexcept>

std::string tls_error_string() {
    unsigned long e = ERR_get_error();
    if (e == 0) return "no OpenSSL error queued (peer probably disconnected)";
    char buf[256];
    ERR_error_string_n(e, buf, sizeof(buf));
    return buf;
}

// ---------------- TlsContext ----------------

TlsContext::TlsContext(const std::string& cert_file, const std::string& key_file) {
    ctx_ = SSL_CTX_new(TLS_server_method());  // "negotiate the best TLS version both sides support"
    if (!ctx_) throw std::runtime_error("SSL_CTX_new failed: " + tls_error_string());

    // Refuse the old, broken protocol versions (SSLv3, TLS 1.0, TLS 1.1).
    SSL_CTX_set_min_proto_version(ctx_, TLS1_2_VERSION);

    if (SSL_CTX_use_certificate_file(ctx_, cert_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::string err = tls_error_string();
        SSL_CTX_free(ctx_);
        throw std::runtime_error("cannot load certificate '" + cert_file + "': " + err);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx_, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::string err = tls_error_string();
        SSL_CTX_free(ctx_);
        throw std::runtime_error("cannot load private key '" + key_file + "': " + err);
    }
    // Make sure the key actually belongs to the certificate.
    if (SSL_CTX_check_private_key(ctx_) != 1) {
        SSL_CTX_free(ctx_);
        throw std::runtime_error("private key does not match the certificate");
    }
}

TlsContext::~TlsContext() {
    if (ctx_) SSL_CTX_free(ctx_);
}

// ---------------- TlsConnection ----------------

TlsConnection::TlsConnection(SSL_CTX* ctx, int fd) {
    ssl_ = SSL_new(ctx);
    if (ssl_) SSL_set_fd(ssl_, fd);  // BIO_NOCLOSE semantics: we keep owning the fd
}

TlsConnection::~TlsConnection() {
    if (ssl_) SSL_free(ssl_);
}

bool TlsConnection::handshake() {
    if (!ssl_) return false;
    ERR_clear_error();
    int r = SSL_accept(ssl_);  // key exchange, certificate sent, session keys derived
    if (r == 1) {
        handshaken_ = true;
        return true;
    }
    fatal_ = true;
    return false;
}

TlsIo TlsConnection::read(char* buf, std::size_t len, std::size_t& n) {
    n = 0;
    ERR_clear_error();
    errno = 0;

    int r = SSL_read(ssl_, buf, static_cast<int>(std::min<std::size_t>(len, INT_MAX)));
    if (r > 0) {
        n = static_cast<std::size_t>(r);
        return TlsIo::Ok;
    }

    switch (SSL_get_error(ssl_, r)) {
        case SSL_ERROR_ZERO_RETURN:  // close_notify received
            return TlsIo::Closed;
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:   // can surface when SO_RCVTIMEO fires
            return TlsIo::Timeout;
        case SSL_ERROR_SYSCALL:
            // OpenSSL reports a socket timeout as a syscall error with EAGAIN.
            if (errno == EAGAIN || errno == EWOULDBLOCK) return TlsIo::Timeout;
            fatal_ = true;
            return TlsIo::Error;
        default:
            fatal_ = true;
            return TlsIo::Error;
    }
}

bool TlsConnection::write_all(const std::string& data) {
    std::size_t total = 0;
    while (total < data.size()) {
        ERR_clear_error();
        int chunk = static_cast<int>(std::min<std::size_t>(data.size() - total, 1 << 20));
        int w = SSL_write(ssl_, data.data() + total, chunk);
        if (w <= 0) {
            fatal_ = true;
            return false;
        }
        total += static_cast<std::size_t>(w);
    }
    return true;
}

void TlsConnection::shutdown() {
    if (ssl_ && handshaken_ && !fatal_) {
        ERR_clear_error();
        SSL_shutdown(ssl_);  // send close_notify; we don't wait for the peer's reply
    }
}
