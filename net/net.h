#pragma once

#include "../lib/types.h"

// Network byte order helpers
static inline uint16_t htons(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint16_t ntohs(uint16_t v) { return htons(v); }
static inline uint32_t htonl(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}
static inline uint32_t ntohl(uint32_t v) { return htonl(v); }

// Ethertypes
#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IP   0x0800

// IP protocols
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

// Ethernet header (14 bytes)
struct __attribute__((packed)) EthHeader {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type; // big-endian
};

// ARP packet
struct __attribute__((packed)) ArpPacket {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t operation;
    uint8_t  sender_mac[6];
    uint32_t sender_ip;
    uint8_t  target_mac[6];
    uint32_t target_ip;
};

// IPv4 header (20 bytes min)
struct __attribute__((packed)) IpHeader {
    uint8_t  ver_ihl;    // version (4) + IHL (4)
    uint8_t  tos;
    uint16_t total_len;
    uint16_t ident;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
};

// ICMP header
struct __attribute__((packed)) IcmpHeader {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
};

// UDP header
struct __attribute__((packed)) UdpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
};

// TCP header
struct __attribute__((packed)) TcpHeader {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset; // upper 4 bits = header length in 32-bit words
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
};

// TCP flags
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

// DHCP message
struct __attribute__((packed)) DhcpMessage {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic_cookie;
    uint8_t  options[312];
};

// Network configuration
struct NetConfig {
    uint32_t ip;
    uint32_t gateway;
    uint32_t subnet;
    uint32_t dns;
    bool     configured;
};

// Make an IP from 4 bytes
static inline uint32_t make_ip(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return a | (b << 8) | (c << 16) | (d << 24);
}

// Initialize networking (call after e1000_init)
void net_init();

// Process incoming packets (call from main loop)
void net_poll();

// Get current network config
NetConfig* net_get_config();

// Send an ARP request
void net_send_arp_request(uint32_t ip);

// Send DHCP discover to get an IP
void net_dhcp_discover();

// Send a UDP packet
bool net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                  const void* data, uint16_t len);

// Send ICMP echo (ping)
bool net_send_ping(uint32_t dst_ip);

// DNS resolve (blocking, simple)
uint32_t net_dns_resolve(const char* hostname);

// TCP connection (simple blocking)
struct TcpSocket {
    uint32_t remote_ip;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  state; // 0=closed, 1=syn_sent, 2=established, 3=fin_wait
    uint8_t  rx_buf[8192];
    uint16_t rx_len;
    bool     rx_ready;
};

#define MAX_TCP_SOCKETS 8

// Open a TCP connection
TcpSocket* net_tcp_connect(uint32_t ip, uint16_t port);

// Send data over TCP
bool net_tcp_send(TcpSocket* sock, const void* data, uint16_t len);

// Receive data from TCP (non-blocking, returns 0 if nothing)
uint16_t net_tcp_recv(TcpSocket* sock, void* buf, uint16_t buf_size);

// Close TCP connection
void net_tcp_close(TcpSocket* sock);

// Check network status
bool net_is_up();
