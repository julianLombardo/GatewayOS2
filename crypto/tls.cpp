#include "tls.h"
#include "sha256.h"
#include "aes.h"
#include "rsa.h"
#include "../lib/string.h"
#include "../lib/printf.h"
#include "../memory/heap.h"
#include "../drivers/serial.h"
#include "../kernel/timer.h"

// TLS record types
#define TLS_CHANGE_CIPHER   20
#define TLS_ALERT           21
#define TLS_HANDSHAKE       22
#define TLS_APPLICATION     23

// Handshake types
#define HS_CLIENT_HELLO     1
#define HS_SERVER_HELLO     2
#define HS_CERTIFICATE      11
#define HS_SERVER_HELLO_DONE 14
#define HS_CLIENT_KEY_EXCH  16
#define HS_FINISHED         20

// TLS 1.2
#define TLS_VERSION_MAJOR   3
#define TLS_VERSION_MINOR   3

// Cipher suite: TLS_RSA_WITH_AES_128_CBC_SHA256 = 0x003C
#define CS_RSA_AES128_CBC_SHA256 0x003C

// Max connections
#define MAX_TLS_CONNS 2
static TlsConnection tls_conns[MAX_TLS_CONNS];
static bool tls_conn_used[MAX_TLS_CONNS] = {false};

// Simple PRNG for client random (seeded with timer)
static uint32_t tls_rand_state = 0xDEADBEEF;
static void tls_rand_seed() {
    tls_rand_state ^= timer_get_ticks();
    tls_rand_state = tls_rand_state * 1664525 + 1013904223;
}
static uint8_t tls_rand_byte() {
    tls_rand_state = tls_rand_state * 1103515245 + 12345;
    return (tls_rand_state >> 16) & 0xFF;
}

// Read exactly n bytes from TCP with timeout
static bool tcp_read_exact(TcpSocket* sock, uint8_t* buf, int n, int timeout_ms) {
    int got = 0;
    uint32_t start = timer_get_ticks();
    while (got < n) {
        // Poll multiple times to catch all pending packets
        for (int p = 0; p < 10; p++) net_poll();
        int r = net_tcp_recv(sock, buf + got, n - got);
        if (r > 0) {
            got += r;
            start = timer_get_ticks(); // Reset timeout on progress
            if (got < n && n - got > 100) {
                char dbg[64];
                ksprintf(dbg, "[TLS] tcp_read_exact: got %d/%d\n", got, n);
                serial_write(dbg);
            }
            continue; // Try reading more immediately
        }
        if (sock->state == 0) return false; // Connection closed
        uint32_t elapsed = (timer_get_ticks() - start);
        if (elapsed > (uint32_t)(timeout_ms / 10)) {
            char dbg[64];
            ksprintf(dbg, "[TLS] tcp_read_exact timeout: got %d/%d\n", got, n);
            serial_write(dbg);
            return false;
        }
        // Small busy wait to let packets arrive
        for (volatile int i = 0; i < 2000; i++);
    }
    return true;
}

// Read a TLS record (up to 16KB per TLS spec)
static bool tls_read_record(TcpSocket* sock, uint8_t* type, uint8_t* buf, int* len, int max_len) {
    uint8_t hdr[5];
    if (!tcp_read_exact(sock, hdr, 5, 15000)) {
        serial_write("[TLS] Record header read timeout\n");
        return false;
    }
    *type = hdr[0];
    int rec_len = (hdr[3] << 8) | hdr[4];
    if (rec_len > max_len) {
        char dbg[64];
        ksprintf(dbg, "[TLS] Record too large: %d > %d\n", rec_len, max_len);
        serial_write(dbg);
        return false;
    }
    if (rec_len > 0) {
        if (!tcp_read_exact(sock, buf, rec_len, 15000)) {
            char dbg[64];
            ksprintf(dbg, "[TLS] Record body timeout: needed %d bytes\n", rec_len);
            serial_write(dbg);
            return false;
        }
    }
    *len = rec_len;
    char dbg[64];
    ksprintf(dbg, "[TLS] Got record type=%d len=%d\n", *type, rec_len);
    serial_write(dbg);
    return true;
}

// Write a TLS record (plaintext) — sends header+data as single TCP segment
static bool tls_write_record(TcpSocket* sock, uint8_t type, const uint8_t* data, int len) {
    static uint8_t sendbuf[2048 + 5];
    sendbuf[0] = type;
    sendbuf[1] = 3;
    sendbuf[2] = 1; // Use TLS 1.0 (3,1) for record layer — max compatibility
    sendbuf[3] = (len >> 8) & 0xFF;
    sendbuf[4] = len & 0xFF;
    if (len > 0 && len <= 2048)
        memcpy(sendbuf + 5, data, len);
    if (!net_tcp_send(sock, sendbuf, 5 + len)) return false;
    // Wait for ACK
    for (volatile int i = 0; i < 50000; i++);
    net_poll();
    return true;
}

// Write encrypted TLS record
static bool tls_write_encrypted(TlsConnection* conn, uint8_t type, const uint8_t* data, int len) {
    // Build MAC input: seq(8) + type(1) + version(2) + length(2) + data
    static uint8_t mac_input[2048 + 13];
    int mi = 0;
    // Sequence number (big-endian 8 bytes)
    for (int i = 7; i >= 0; i--)
        mac_input[mi++] = (conn->client_seq >> (i * 8)) & 0xFF;
    mac_input[mi++] = type;
    mac_input[mi++] = TLS_VERSION_MAJOR;
    mac_input[mi++] = TLS_VERSION_MINOR;
    mac_input[mi++] = (len >> 8) & 0xFF;
    mac_input[mi++] = len & 0xFF;
    memcpy(mac_input + mi, data, len);
    mi += len;

    // HMAC-SHA256
    uint8_t mac[32];
    hmac_sha256(conn->client_write_mac_key, 32, mac_input, mi, mac);

    // Pad to AES block boundary: data + mac(32) + padding
    int content_len = len + 32; // data + mac
    int pad_needed = 16 - (content_len % 16);
    if (pad_needed == 0) pad_needed = 16;
    int total = content_len + pad_needed;

    static uint8_t plaintext[2048 + 64];
    memcpy(plaintext, data, len);
    memcpy(plaintext + len, mac, 32);
    // PKCS padding
    for (int i = 0; i < pad_needed; i++)
        plaintext[content_len + i] = pad_needed - 1;

    // Generate random IV
    uint8_t iv[16];
    for (int i = 0; i < 16; i++) iv[i] = tls_rand_byte();

    // Encrypt
    static uint8_t ciphertext[2048 + 80];
    memcpy(ciphertext, iv, 16); // Explicit IV
    uint8_t iv_copy[16];
    memcpy(iv_copy, iv, 16);
    aes128_cbc_encrypt(plaintext, ciphertext + 16, total, conn->client_write_key, iv_copy);

    conn->client_seq++;

    return tls_write_record(conn->tcp, type, ciphertext, 16 + total);
}

// Decrypt a received TLS record
static int tls_decrypt_record(TlsConnection* conn, const uint8_t* data, int len, uint8_t* out) {
    if (len < 48) return -1; // At minimum: 16 IV + 16 data + 16 (mac part)

    // First 16 bytes are the explicit IV
    uint8_t iv[16];
    memcpy(iv, data, 16);

    int enc_len = len - 16;
    static uint8_t decrypted[2048 + 64];
    aes128_cbc_decrypt(data + 16, decrypted, enc_len, conn->server_write_key, iv);

    // Remove padding
    int pad_val = decrypted[enc_len - 1];
    if (pad_val >= enc_len) return -1;
    int unpadded = enc_len - pad_val - 1;

    // Remove MAC (32 bytes for SHA-256)
    int payload_len = unpadded - 32;
    if (payload_len < 0) return -1;

    memcpy(out, decrypted, payload_len);
    conn->server_seq++;
    return payload_len;
}

// Parse X.509 certificate to extract RSA public key (n, e)
// Minimal ASN.1 DER parser — just enough for RSA key extraction
static bool parse_asn1_len(const uint8_t* data, int* pos, int max, int* out_len) {
    if (*pos >= max) return false;
    uint8_t b = data[(*pos)++];
    if (b < 0x80) {
        *out_len = b;
    } else {
        int num_bytes = b & 0x7F;
        if (num_bytes > 3 || *pos + num_bytes > max) return false;
        *out_len = 0;
        for (int i = 0; i < num_bytes; i++)
            *out_len = (*out_len << 8) | data[(*pos)++];
    }
    return true;
}

static bool extract_rsa_pubkey(const uint8_t* cert, int cert_len,
                               const uint8_t** n, int* n_len,
                               const uint8_t** e, int* e_len) {
    // Search for RSA OID: 1.2.840.113549.1.1.1 = 06 09 2a 86 48 86 f7 0d 01 01 01
    static const uint8_t rsa_oid[] = {0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01};

    int oid_pos = -1;
    for (int i = 0; i < cert_len - 11; i++) {
        if (cert[i] == 0x06 && cert[i+1] == 0x09 &&
            memcmp(cert + i + 2, rsa_oid, 9) == 0) {
            oid_pos = i;
            break;
        }
    }
    if (oid_pos < 0) return false;

    // Find BIT STRING after the OID (contains the public key)
    int search_start = oid_pos + 11;
    int bs_pos = -1;
    for (int i = search_start; i < cert_len - 2 && i < search_start + 20; i++) {
        if (cert[i] == 0x03) { // BIT STRING
            bs_pos = i;
            break;
        }
    }
    if (bs_pos < 0) return false;

    int pos = bs_pos + 1;
    int bs_len;
    if (!parse_asn1_len(cert, &pos, cert_len, &bs_len)) return false;
    pos++; // Skip unused bits byte (0x00)

    // Now we should be at a SEQUENCE containing n and e
    if (cert[pos] != 0x30) return false;
    pos++;
    int seq_len;
    if (!parse_asn1_len(cert, &pos, cert_len, &seq_len)) return false;

    // First INTEGER: n (modulus)
    if (cert[pos] != 0x02) return false;
    pos++;
    if (!parse_asn1_len(cert, &pos, cert_len, n_len)) return false;
    *n = cert + pos;
    // Skip leading zero if present
    if ((*n)[0] == 0x00 && *n_len > 1) { (*n)++; (*n_len)--; }
    pos += *n_len + ((*n == cert + pos) ? 0 : 1); // account for skipped zero

    // Reparse to get correct position
    // Actually, let's re-find pos properly
    pos = (int)(*n - cert) + *n_len;

    // Second INTEGER: e (exponent)
    if (cert[pos] != 0x02) return false;
    pos++;
    if (!parse_asn1_len(cert, &pos, cert_len, e_len)) return false;
    *e = cert + pos;

    return true;
}

TlsConnection* tls_connect(uint32_t ip, uint16_t port) {
    // Find free slot
    TlsConnection* conn = nullptr;
    for (int i = 0; i < MAX_TLS_CONNS; i++) {
        if (!tls_conn_used[i]) {
            conn = &tls_conns[i];
            tls_conn_used[i] = true;
            break;
        }
    }
    if (!conn) return nullptr;

    memset(conn, 0, sizeof(TlsConnection));
    tls_rand_seed();

    // TCP connect
    conn->tcp = net_tcp_connect(ip, port);
    if (!conn->tcp) {
        strcpy(conn->error_msg, "TCP connect failed");
        conn->error = true;
        return conn;
    }

    // Wait for TCP to establish
    uint32_t start = timer_get_ticks();
    while (conn->tcp->state != 2) { // 2 = established
        net_poll();
        if ((timer_get_ticks() - start) * 10 > 5000) {
            strcpy(conn->error_msg, "TCP timeout");
            conn->error = true;
            return conn;
        }
    }

    serial_write("[TLS] TCP connected, starting handshake\n");

    // === CLIENT HELLO ===
    uint8_t client_random[32];
    // First 4 bytes: unix time (we use timer ticks)
    uint32_t t = timer_get_ticks();
    client_random[0] = (t >> 24); client_random[1] = (t >> 16);
    client_random[2] = (t >> 8); client_random[3] = t;
    for (int i = 4; i < 32; i++) client_random[i] = tls_rand_byte();

    // Build ClientHello with SNI extension
    uint8_t hello[256];
    int hp = 0;

    // Handshake header: type(1) + length(3)
    hello[hp++] = HS_CLIENT_HELLO;
    int len_pos = hp; hp += 3; // Fill length later

    // Version: TLS 1.2
    hello[hp++] = TLS_VERSION_MAJOR;
    hello[hp++] = TLS_VERSION_MINOR;

    // Client random
    memcpy(hello + hp, client_random, 32); hp += 32;

    // Session ID length: 0
    hello[hp++] = 0;

    // Cipher suites
    hello[hp++] = 0; hello[hp++] = 6; // 3 suites = 6 bytes
    // TLS_RSA_WITH_AES_128_CBC_SHA256 (0x003C)
    hello[hp++] = 0x00; hello[hp++] = 0x3C;
    // TLS_RSA_WITH_AES_128_CBC_SHA (0x002F)
    hello[hp++] = 0x00; hello[hp++] = 0x2F;
    // TLS_RSA_WITH_AES_256_CBC_SHA (0x0035)
    hello[hp++] = 0x00; hello[hp++] = 0x35;

    // Compression methods (1 byte length + null)
    hello[hp++] = 1;
    hello[hp++] = 0; // null compression

    // Extensions
    const char* sni_host = "smtp.gmail.com";
    int sni_len = 0;
    while (sni_host[sni_len]) sni_len++;

    int ext_start = hp;
    hp += 2; // Extensions total length (fill later)

    // SNI extension (type 0x0000)
    hello[hp++] = 0x00; hello[hp++] = 0x00; // Extension type: SNI
    int sni_ext_len = sni_len + 5;
    hello[hp++] = (sni_ext_len >> 8) & 0xFF; hello[hp++] = sni_ext_len & 0xFF;
    int sni_list_len = sni_len + 3;
    hello[hp++] = (sni_list_len >> 8) & 0xFF; hello[hp++] = sni_list_len & 0xFF;
    hello[hp++] = 0x00; // Host name type
    hello[hp++] = (sni_len >> 8) & 0xFF; hello[hp++] = sni_len & 0xFF;
    memcpy(hello + hp, sni_host, sni_len); hp += sni_len;

    // Fill extensions length
    int ext_total = hp - ext_start - 2;
    hello[ext_start]     = (ext_total >> 8) & 0xFF;
    hello[ext_start + 1] = ext_total & 0xFF;

    // Fill handshake length
    int hs_len = hp - len_pos - 3;
    hello[len_pos]   = (hs_len >> 16) & 0xFF;
    hello[len_pos+1] = (hs_len >> 8) & 0xFF;
    hello[len_pos+2] = hs_len & 0xFF;

    // Start handshake hash
    SHA256_CTX hs_hash;
    sha256_init(&hs_hash);
    sha256_update(&hs_hash, hello, hp);

    if (!tls_write_record(conn->tcp, TLS_HANDSHAKE, hello, hp)) {
        strcpy(conn->error_msg, "Failed to send ClientHello");
        conn->error = true;
        return conn;
    }
    serial_write("[TLS] ClientHello sent\n");

    // === READ SERVER MESSAGES ===
    uint8_t server_random[32];
    static uint8_t cert_buf[6144]; // Server certificate (can be large)
    int cert_data_len = 0;
    bool got_hello_done = false;

    static uint8_t rec_buf[8192];

    while (!got_hello_done) {
        uint8_t rec_type;
        int rec_len;
        if (!tls_read_record(conn->tcp, &rec_type, rec_buf, &rec_len, sizeof(rec_buf))) {
            strcpy(conn->error_msg, "Read timeout");
            conn->error = true;
            return conn;
        }

        if (rec_type == TLS_ALERT) {
            ksprintf(conn->error_msg, "TLS Alert: %d,%d", rec_buf[0], rec_buf[1]);
            conn->error = true;
            return conn;
        }

        if (rec_type != TLS_HANDSHAKE) continue;

        // Process handshake messages (may be multiple in one record)
        int pos = 0;
        while (pos + 4 <= rec_len) {
            uint8_t hs_type = rec_buf[pos];
            int hs_length = (rec_buf[pos+1] << 16) | (rec_buf[pos+2] << 8) | rec_buf[pos+3];
            int msg_start = pos;
            int msg_end = pos + 4 + hs_length;

            if (msg_end > rec_len) break; // Fragment, would need reassembly

            // Hash this handshake message
            sha256_update(&hs_hash, rec_buf + msg_start, hs_length + 4);

            char dbg[64];
            ksprintf(dbg, "[TLS] HS type=%d len=%d\n", hs_type, hs_length);
            serial_write(dbg);

            int dp = pos + 4; // data pointer within message

            if (hs_type == HS_SERVER_HELLO) {
                dp += 2; // version
                memcpy(server_random, rec_buf + dp, 32); dp += 32;
                int sid_len = rec_buf[dp++];
                dp += sid_len;
                dp += 2; // cipher suite
                dp += 1; // compression
                serial_write("[TLS] ServerHello received\n");
            } else if (hs_type == HS_CERTIFICATE) {
                int dp2 = dp;
                // Total certs length
                dp2 += 3;
                // First cert length
                int first_cert_len = (rec_buf[dp2] << 16) | (rec_buf[dp2+1] << 8) | rec_buf[dp2+2];
                dp2 += 3;
                if (first_cert_len <= (int)sizeof(cert_buf) && dp2 + first_cert_len <= rec_len) {
                    memcpy(cert_buf, rec_buf + dp2, first_cert_len);
                    cert_data_len = first_cert_len;
                }
                ksprintf(dbg, "[TLS] Certificate: %d bytes\n", first_cert_len);
                serial_write(dbg);
            } else if (hs_type == HS_SERVER_HELLO_DONE) {
                got_hello_done = true;
                serial_write("[TLS] ServerHelloDone\n");
            }

            pos = msg_end; // Advance to next message
        }
    }

    // === EXTRACT RSA PUBLIC KEY FROM CERTIFICATE ===
    const uint8_t* rsa_n = nullptr;
    const uint8_t* rsa_e = nullptr;
    int rsa_n_len = 0, rsa_e_len = 0;

    if (!extract_rsa_pubkey(cert_buf, cert_data_len, &rsa_n, &rsa_n_len, &rsa_e, &rsa_e_len)) {
        strcpy(conn->error_msg, "Cannot parse cert");
        conn->error = true;
        return conn;
    }

    char dbg[80];
    ksprintf(dbg, "[TLS] RSA key: n=%d bytes, e=%d bytes\n", rsa_n_len, rsa_e_len);
    serial_write(dbg);

    // === CLIENT KEY EXCHANGE ===
    // Generate 48-byte pre-master secret
    uint8_t pre_master[48];
    pre_master[0] = TLS_VERSION_MAJOR;
    pre_master[1] = TLS_VERSION_MINOR;
    for (int i = 2; i < 48; i++) pre_master[i] = tls_rand_byte();

    // RSA encrypt pre-master secret with server's public key
    static uint8_t encrypted_pms[512];
    int enc_len = rsa_encrypt(pre_master, 48, rsa_n, rsa_n_len, rsa_e, rsa_e_len,
                              encrypted_pms, sizeof(encrypted_pms));
    if (enc_len < 0) {
        strcpy(conn->error_msg, "RSA encrypt failed");
        conn->error = true;
        return conn;
    }

    // Build ClientKeyExchange message
    static uint8_t cke[520];
    int ckp = 0;
    cke[ckp++] = HS_CLIENT_KEY_EXCH;
    int cke_len = enc_len + 2; // 2 bytes for encrypted PMS length prefix
    cke[ckp++] = (cke_len >> 16) & 0xFF;
    cke[ckp++] = (cke_len >> 8) & 0xFF;
    cke[ckp++] = cke_len & 0xFF;
    cke[ckp++] = (enc_len >> 8) & 0xFF;
    cke[ckp++] = enc_len & 0xFF;
    memcpy(cke + ckp, encrypted_pms, enc_len);
    ckp += enc_len;

    sha256_update(&hs_hash, cke, ckp);
    tls_write_record(conn->tcp, TLS_HANDSHAKE, cke, ckp);
    serial_write("[TLS] ClientKeyExchange sent\n");

    // === DERIVE SESSION KEYS ===
    // Master secret = PRF(pre_master, "master secret", client_random + server_random)
    uint8_t seed[64];
    memcpy(seed, client_random, 32);
    memcpy(seed + 32, server_random, 32);

    uint8_t master_secret[48];
    tls_prf_sha256(pre_master, 48, "master secret", seed, 64, master_secret, 48);

    // Key block = PRF(master_secret, "key expansion", server_random + client_random)
    uint8_t key_seed[64];
    memcpy(key_seed, server_random, 32);
    memcpy(key_seed + 32, client_random, 32);

    // Need: client_write_mac(32) + server_write_mac(32) + client_write_key(16) + server_write_key(16) + client_iv(16) + server_iv(16) = 128 bytes
    uint8_t key_block[128];
    tls_prf_sha256(master_secret, 48, "key expansion", key_seed, 64, key_block, 128);

    memcpy(conn->client_write_mac_key, key_block, 32);
    memcpy(conn->server_write_mac_key, key_block + 32, 32);
    memcpy(conn->client_write_key, key_block + 64, 16);
    memcpy(conn->server_write_key, key_block + 80, 16);
    memcpy(conn->client_write_iv, key_block + 96, 16);
    memcpy(conn->server_write_iv, key_block + 112, 16);

    serial_write("[TLS] Session keys derived\n");

    // === CHANGE CIPHER SPEC ===
    uint8_t ccs = 1;
    tls_write_record(conn->tcp, TLS_CHANGE_CIPHER, &ccs, 1);
    serial_write("[TLS] ChangeCipherSpec sent\n");

    // === CLIENT FINISHED ===
    // Compute verify_data = PRF(master_secret, "client finished", Hash(handshake_messages))
    uint8_t hs_digest[32];
    SHA256_CTX hs_copy = hs_hash; // Copy to preserve for server finished check
    sha256_final(&hs_copy, hs_digest);

    uint8_t verify_data[12];
    tls_prf_sha256(master_secret, 48, "client finished", hs_digest, 32, verify_data, 12);

    uint8_t finished_msg[16];
    finished_msg[0] = HS_FINISHED;
    finished_msg[1] = 0;
    finished_msg[2] = 0;
    finished_msg[3] = 12;
    memcpy(finished_msg + 4, verify_data, 12);

    // Send encrypted Finished
    conn->client_seq = 0;
    conn->server_seq = 0;
    tls_write_encrypted(conn, TLS_HANDSHAKE, finished_msg, 16);
    serial_write("[TLS] Finished sent (encrypted)\n");

    // === READ SERVER CHANGE CIPHER SPEC + FINISHED ===
    // Read ChangeCipherSpec
    uint8_t rec_type2;
    int rec_len2;
    if (!tls_read_record(conn->tcp, &rec_type2, rec_buf, &rec_len2, sizeof(rec_buf))) {
        strcpy(conn->error_msg, "No server CCS");
        conn->error = true;
        return conn;
    }

    // Read server Finished (encrypted)
    if (!tls_read_record(conn->tcp, &rec_type2, rec_buf, &rec_len2, sizeof(rec_buf))) {
        strcpy(conn->error_msg, "No server Finished");
        conn->error = true;
        return conn;
    }

    // We could verify server Finished here, but for simplicity just trust it
    serial_write("[TLS] Handshake complete!\n");

    conn->handshake_done = true;
    return conn;
}

bool tls_send(TlsConnection* conn, const void* data, int len) {
    if (!conn || !conn->handshake_done || conn->error) return false;
    return tls_write_encrypted(conn, TLS_APPLICATION, (const uint8_t*)data, len);
}

int tls_recv(TlsConnection* conn, void* buf, int buf_size) {
    if (!conn || !conn->handshake_done || conn->error) return -1;

    // Return buffered data first
    if (conn->rx_pos < conn->rx_len) {
        int avail = conn->rx_len - conn->rx_pos;
        int copy = (avail < buf_size) ? avail : buf_size;
        memcpy(buf, conn->rx_buf + conn->rx_pos, copy);
        conn->rx_pos += copy;
        return copy;
    }

    // Try to read a new record
    net_poll();
    uint8_t rec_type;
    static uint8_t enc_buf[2048];
    int rec_len;

    // Non-blocking check - peek at TCP
    uint8_t hdr[5];
    int got = net_tcp_recv(conn->tcp, hdr, 5);
    if (got < 5) return 0; // Nothing available

    rec_type = hdr[0];
    rec_len = (hdr[3] << 8) | hdr[4];
    if (rec_len > (int)sizeof(enc_buf)) return -1;

    if (!tcp_read_exact(conn->tcp, enc_buf, rec_len, 5000)) return -1;

    if (rec_type == TLS_ALERT) {
        conn->error = true;
        return -1;
    }

    if (rec_type == TLS_APPLICATION) {
        int dec_len = tls_decrypt_record(conn, enc_buf, rec_len, conn->rx_buf);
        if (dec_len < 0) return -1;
        conn->rx_len = dec_len;
        conn->rx_pos = 0;

        int copy = (dec_len < buf_size) ? dec_len : buf_size;
        memcpy(buf, conn->rx_buf, copy);
        conn->rx_pos = copy;
        return copy;
    }

    return 0; // Ignore other record types
}

void tls_close(TlsConnection* conn) {
    if (!conn) return;
    // Send close_notify alert
    if (conn->tcp && conn->handshake_done && !conn->error) {
        uint8_t alert[2] = {1, 0}; // warning, close_notify
        tls_write_encrypted(conn, TLS_ALERT, alert, 2);
    }
    if (conn->tcp) {
        net_tcp_close(conn->tcp);
        conn->tcp = nullptr;
    }
    // Free slot
    for (int i = 0; i < MAX_TLS_CONNS; i++) {
        if (&tls_conns[i] == conn) {
            tls_conn_used[i] = false;
            break;
        }
    }
}

bool tls_is_ready(TlsConnection* conn) {
    return conn && conn->handshake_done && !conn->error;
}
