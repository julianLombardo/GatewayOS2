#pragma once
#include "../lib/types.h"
#include "../net/net.h"

struct TlsConnection {
    TcpSocket* tcp;

    // Session keys
    uint8_t client_write_key[16]; // AES-128 key
    uint8_t server_write_key[16];
    uint8_t client_write_iv[16];
    uint8_t server_write_iv[16];
    uint8_t client_write_mac_key[32]; // HMAC-SHA256 key
    uint8_t server_write_mac_key[32];

    // Sequence numbers
    uint64_t client_seq;
    uint64_t server_seq;

    // State
    bool handshake_done;
    bool error;
    char error_msg[48];

    // Handshake hash (for Finished message)
    uint8_t handshake_hash[32];

    // Receive buffer for decrypted data
    uint8_t rx_buf[2048];
    int rx_len;
    int rx_pos;
};

// Connect and perform TLS handshake
TlsConnection* tls_connect(uint32_t ip, uint16_t port);

// Send application data over TLS
bool tls_send(TlsConnection* conn, const void* data, int len);

// Receive decrypted data (returns bytes read, 0 if nothing)
int tls_recv(TlsConnection* conn, void* buf, int buf_size);

// Close TLS connection
void tls_close(TlsConnection* conn);

// Check if connection is ready
bool tls_is_ready(TlsConnection* conn);
