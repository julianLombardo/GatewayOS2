#include "net.h"
#include "../drivers/e1000.h"
#include "../drivers/serial.h"
#include "../kernel/timer.h"
#include "../memory/heap.h"
#include "../lib/string.h"
#include "../lib/printf.h"

static NetConfig config;
static uint8_t our_mac[6];
static uint8_t pkt_buf[2048];
static bool net_up = false;

// ARP cache (simple, 16 entries)
#define ARP_CACHE_SIZE 16
struct ArpEntry {
    uint32_t ip;
    uint8_t  mac[6];
    bool     valid;
};
static ArpEntry arp_cache[ARP_CACHE_SIZE];

// TCP sockets
static TcpSocket tcp_sockets[MAX_TCP_SOCKETS];
static uint16_t next_ephemeral_port = 49152;

// DNS state
static uint32_t dns_result_ip = 0;
static uint16_t dns_xid = 0;
static bool dns_resolved = false;

// ============================================================
// CHECKSUMS
// ============================================================
static uint16_t ip_checksum(const void* data, int len) {
    const uint16_t* p = (const uint16_t*)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len == 1) sum += *(const uint8_t*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(uint16_t)sum;
}

// ============================================================
// ETHERNET
// ============================================================
static void eth_send(const uint8_t* dst, uint16_t type, const void* payload, uint16_t len) {
    uint8_t frame[1536];
    EthHeader* eth = (EthHeader*)frame;
    memcpy(eth->dst, dst, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = htons(type);
    memcpy(frame + 14, payload, len);
    e1000_send(frame, 14 + len);
}

static const uint8_t BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// ============================================================
// ARP
// ============================================================
static void arp_cache_add(uint32_t ip, const uint8_t* mac) {
    // Update existing
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(arp_cache[i].mac, mac, 6);
            return;
        }
    }
    // Add new
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = ip;
            memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].valid = true;
            return;
        }
    }
    // Overwrite first
    arp_cache[0].ip = ip;
    memcpy(arp_cache[0].mac, mac, 6);
    arp_cache[0].valid = true;
}

static bool arp_cache_lookup(uint32_t ip, uint8_t* mac_out) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            memcpy(mac_out, arp_cache[i].mac, 6);
            return true;
        }
    }
    return false;
}

void net_send_arp_request(uint32_t ip) {
    ArpPacket arp;
    arp.hw_type = htons(1);        // Ethernet
    arp.proto_type = htons(0x0800); // IPv4
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.operation = htons(1);      // Request
    memcpy(arp.sender_mac, our_mac, 6);
    arp.sender_ip = config.ip;
    memset(arp.target_mac, 0, 6);
    arp.target_ip = ip;
    eth_send(BROADCAST_MAC, ETH_TYPE_ARP, &arp, sizeof(arp));
}

static void handle_arp(const uint8_t* data, uint16_t len) {
    if (len < sizeof(ArpPacket)) return;
    const ArpPacket* arp = (const ArpPacket*)data;

    // Cache sender
    arp_cache_add(arp->sender_ip, arp->sender_mac);

    // Reply to requests for our IP
    if (ntohs(arp->operation) == 1 && arp->target_ip == config.ip) {
        ArpPacket reply;
        reply.hw_type = htons(1);
        reply.proto_type = htons(0x0800);
        reply.hw_len = 6;
        reply.proto_len = 4;
        reply.operation = htons(2); // Reply
        memcpy(reply.sender_mac, our_mac, 6);
        reply.sender_ip = config.ip;
        memcpy(reply.target_mac, arp->sender_mac, 6);
        reply.target_ip = arp->sender_ip;
        eth_send(arp->sender_mac, ETH_TYPE_ARP, &reply, sizeof(reply));
    }
}

// ============================================================
// IP
// ============================================================
static bool resolve_mac(uint32_t ip, uint8_t* mac_out) {
    // Same subnet? ARP directly. Different subnet? Use gateway.
    uint32_t target = ip;
    if ((ip & config.subnet) != (config.ip & config.subnet))
        target = config.gateway;

    if (arp_cache_lookup(target, mac_out)) return true;

    // Send ARP and wait
    net_send_arp_request(target);
    for (int i = 0; i < 500; i++) {
        e1000_poll();
        // Check for ARP replies in received packets
        uint16_t plen = e1000_recv(pkt_buf, sizeof(pkt_buf));
        if (plen > 0) {
            EthHeader* eth = (EthHeader*)pkt_buf;
            if (ntohs(eth->type) == ETH_TYPE_ARP)
                handle_arp(pkt_buf + 14, plen - 14);
        }
        if (arp_cache_lookup(target, mac_out)) return true;
        for (volatile int j = 0; j < 10000; j++);
    }
    return false;
}

static void ip_send(uint32_t dst_ip, uint8_t protocol, const void* payload, uint16_t len) {
    uint8_t mac[6];
    if (!resolve_mac(dst_ip, mac)) {
        serial_write("[IP] ARP failed, cannot send\n");
        return;
    }

    uint8_t pkt[1500];
    IpHeader* ip = (IpHeader*)pkt;
    ip->ver_ihl = 0x45;  // IPv4, 20-byte header
    ip->tos = 0;
    ip->total_len = htons(20 + len);
    ip->ident = htons(0);
    ip->flags_frag = 0;
    ip->ttl = 64;
    ip->protocol = protocol;
    ip->checksum = 0;
    ip->src_ip = config.ip;
    ip->dst_ip = dst_ip;
    ip->checksum = ip_checksum(ip, 20);

    memcpy(pkt + 20, payload, len);
    eth_send(mac, ETH_TYPE_IP, pkt, 20 + len);
}

// ============================================================
// ICMP
// ============================================================
static void handle_icmp(const IpHeader* ip_hdr, const uint8_t* data, uint16_t len) {
    if (len < 8) return;
    const IcmpHeader* icmp = (const IcmpHeader*)data;

    if (icmp->type == 8) { // Echo request -> reply
        uint8_t reply[1500];
        IcmpHeader* r = (IcmpHeader*)reply;
        r->type = 0; // Echo reply
        r->code = 0;
        r->ident = icmp->ident;
        r->seq = icmp->seq;
        r->checksum = 0;
        if (len > 8) memcpy(reply + 8, data + 8, len - 8);
        r->checksum = ip_checksum(reply, len);
        ip_send(ip_hdr->src_ip, IP_PROTO_ICMP, reply, len);
    }
}

bool net_send_ping(uint32_t dst_ip) {
    uint8_t pkt[64];
    IcmpHeader* icmp = (IcmpHeader*)pkt;
    icmp->type = 8; // Echo request
    icmp->code = 0;
    icmp->ident = htons(1);
    icmp->seq = htons(1);
    icmp->checksum = 0;
    // Payload
    for (int i = 0; i < 32; i++) pkt[8 + i] = i;
    icmp->checksum = ip_checksum(pkt, 40);
    ip_send(dst_ip, IP_PROTO_ICMP, pkt, 40);
    return true;
}

// ============================================================
// UDP
// ============================================================
static void handle_udp(const IpHeader* ip_hdr, const uint8_t* data, uint16_t len) {
    if (len < 8) return;
    const UdpHeader* udp = (const UdpHeader*)data;
    uint16_t dst_port = ntohs(udp->dst_port);
    uint16_t udp_len = ntohs(udp->length);
    const uint8_t* payload = data + 8;
    uint16_t payload_len = udp_len - 8;

    // DHCP response (port 68)
    if (dst_port == 68 && payload_len >= sizeof(DhcpMessage) - 312) {
        const DhcpMessage* dhcp = (const DhcpMessage*)payload;
        if (ntohl(dhcp->magic_cookie) == 0x63825363) {
            config.ip = dhcp->yiaddr;

            // Parse DHCP options
            const uint8_t* opt = dhcp->options;
            int opt_len = payload_len - (sizeof(DhcpMessage) - 312);
            for (int i = 0; i < opt_len && opt[i] != 0xFF;) {
                uint8_t type = opt[i++];
                if (type == 0) continue;
                uint8_t olen = opt[i++];
                if (type == 1 && olen == 4) memcpy(&config.subnet, &opt[i], 4);
                if (type == 3 && olen >= 4) memcpy(&config.gateway, &opt[i], 4);
                if (type == 6 && olen >= 4) memcpy(&config.dns, &opt[i], 4);
                i += olen;
            }
            config.configured = true;

            char buf[80];
            ksprintf(buf, "[NET] DHCP: IP=%d.%d.%d.%d\n",
                     config.ip & 0xFF, (config.ip >> 8) & 0xFF,
                     (config.ip >> 16) & 0xFF, (config.ip >> 24) & 0xFF);
            serial_write(buf);
        }
    }

    // DNS response (src port 53)
    if (ntohs(udp->src_port) == 53 && payload_len >= 12) {
        uint16_t txid = (payload[0] << 8) | payload[1];
        if (txid == dns_xid) {
            uint16_t flags = (payload[2] << 8) | payload[3];
            uint16_t ancount = (payload[6] << 8) | payload[7];
            if ((flags & 0x8000) && ancount > 0) {
                // Skip question section
                int pos = 12;
                while (pos < (int)payload_len && payload[pos] != 0) pos += payload[pos] + 1;
                pos += 5; // null + qtype(2) + qclass(2)
                // Read first answer
                if (pos + 12 <= (int)payload_len) {
                    // Skip name (could be pointer)
                    if ((payload[pos] & 0xC0) == 0xC0) pos += 2;
                    else { while (pos < (int)payload_len && payload[pos] != 0) pos += payload[pos] + 1; pos++; }
                    // type(2) + class(2) + ttl(4) + rdlength(2)
                    uint16_t rdlen = (payload[pos + 8] << 8) | payload[pos + 9];
                    pos += 10;
                    if (rdlen == 4 && pos + 4 <= (int)payload_len) {
                        memcpy(&dns_result_ip, &payload[pos], 4);
                        dns_resolved = true;
                    }
                }
            }
        }
    }

    (void)ip_hdr;
}

bool net_send_udp(uint32_t dst_ip, uint16_t src_port, uint16_t dst_port,
                  const void* data, uint16_t len) {
    if (!config.configured) return false;

    uint8_t pkt[1500];
    UdpHeader* udp = (UdpHeader*)pkt;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->length = htons(8 + len);
    udp->checksum = 0; // Optional for UDP over IPv4
    memcpy(pkt + 8, data, len);
    ip_send(dst_ip, IP_PROTO_UDP, pkt, 8 + len);
    return true;
}

// ============================================================
// TCP
// ============================================================
static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, const void* tcp_data, uint16_t len) {
    uint32_t sum = 0;
    // Pseudo header
    sum += (src_ip & 0xFFFF) + (src_ip >> 16);
    sum += (dst_ip & 0xFFFF) + (dst_ip >> 16);
    sum += htons(IP_PROTO_TCP);
    sum += htons(len);
    // TCP segment
    const uint16_t* p = (const uint16_t*)tcp_data;
    int remaining = len;
    while (remaining > 1) { sum += *p++; remaining -= 2; }
    if (remaining == 1) sum += *(const uint8_t*)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~(uint16_t)sum;
}

static void tcp_send_packet(TcpSocket* sock, uint8_t flags, const void* data, uint16_t len) {
    uint8_t pkt[1500];
    TcpHeader* tcp = (TcpHeader*)pkt;
    memset(tcp, 0, sizeof(TcpHeader));
    tcp->src_port = htons(sock->local_port);
    tcp->dst_port = htons(sock->remote_port);
    tcp->seq_num = htonl(sock->seq);
    tcp->ack_num = htonl(sock->ack);
    tcp->flags = flags;
    // Advertise available buffer space as window
    uint16_t win = (uint16_t)(sizeof(sock->rx_buf) - sock->rx_len);
    if (win < 1460) win = 1460; // Always allow at least one segment
    tcp->window = htons(win);
    tcp->checksum = 0;
    tcp->urgent = 0;

    int hdr_len = 20;
    // Add MSS option to SYN packets (required by many stacks including QEMU SLiRP)
    if (flags & TCP_SYN) {
        pkt[20] = 0x02; // MSS option kind
        pkt[21] = 0x04; // MSS option length
        pkt[22] = 0x05; // MSS = 1460 (0x05B4)
        pkt[23] = 0xB4;
        hdr_len = 24;
        tcp->data_offset = (6 << 4); // 24 bytes with options
    } else {
        tcp->data_offset = (5 << 4); // 20 bytes, no options
    }

    if (len > 0 && data) memcpy(pkt + hdr_len, data, len);
    tcp->checksum = tcp_checksum(config.ip, sock->remote_ip, pkt, hdr_len + len);
    ip_send(sock->remote_ip, IP_PROTO_TCP, pkt, hdr_len + len);
}

static void handle_tcp(const IpHeader* ip_hdr, const uint8_t* data, uint16_t len) {
    if (len < 20) return;
    const TcpHeader* tcp = (const TcpHeader*)data;
    uint16_t dst_port = ntohs(tcp->dst_port);
    uint16_t src_port = ntohs(tcp->src_port);
    uint32_t seq = ntohl(tcp->seq_num);
    uint32_t ack = ntohl(tcp->ack_num);
    uint8_t flags = tcp->flags;
    int hdr_len = (tcp->data_offset >> 4) * 4;
    int payload_len = len - hdr_len;

    // Find matching socket
    TcpSocket* sock = nullptr;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state > 0 &&
            tcp_sockets[i].local_port == dst_port &&
            tcp_sockets[i].remote_port == src_port &&
            tcp_sockets[i].remote_ip == ip_hdr->src_ip) {
            sock = &tcp_sockets[i];
            break;
        }
    }
    if (!sock) {
        char dbg[80];
        ksprintf(dbg, "[TCP] No socket for %d->%d flags=0x%02X data=%d\n", src_port, dst_port, flags, payload_len);
        serial_write(dbg);
        return;
    }

    if (sock->state == 1 && (flags & TCP_SYN) && (flags & TCP_ACK)) {
        // SYN-ACK received
        sock->ack = seq + 1;
        sock->seq = ack;
        sock->state = 2; // Established
        tcp_send_packet(sock, TCP_ACK, nullptr, 0);
        char buf[48];
        ksprintf(buf, "[TCP] Connected to port %d\n", sock->remote_port);
        serial_write(buf);
    } else if (sock->state == 2) {
        if (payload_len > 0) {
            // Data received
            int space = (int)sizeof(sock->rx_buf) - sock->rx_len;
            int copy = payload_len < space ? payload_len : space;
            if (copy > 0) {
                memcpy(sock->rx_buf + sock->rx_len, data + hdr_len, copy);
                sock->rx_len += copy;
                sock->rx_ready = true;
            }
            char dbg[64];
            ksprintf(dbg, "[TCP] RX %d bytes (buf=%d/%d)\n", payload_len, sock->rx_len, (int)sizeof(sock->rx_buf));
            serial_write(dbg);
            sock->ack = seq + payload_len;
            tcp_send_packet(sock, TCP_ACK, nullptr, 0);
        }
        if (flags & TCP_FIN) {
            sock->ack = seq + 1;
            tcp_send_packet(sock, TCP_ACK | TCP_FIN, nullptr, 0);
            sock->state = 0; // Closed
        }
        if (flags & TCP_RST) {
            sock->state = 0;
        }
    } else if (sock->state == 3) {
        if (flags & TCP_ACK) {
            sock->state = 0; // Fully closed
        }
    }
}

TcpSocket* net_tcp_connect(uint32_t ip, uint16_t port) {
    if (!config.configured) {
        serial_write("[TCP] Network not configured\n");
        return nullptr;
    }

    // Find free socket
    TcpSocket* sock = nullptr;
    for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
        if (tcp_sockets[i].state == 0) {
            sock = &tcp_sockets[i];
            break;
        }
    }
    if (!sock) {
        serial_write("[TCP] No free sockets!\n");
        // Force-close any stale sockets
        for (int i = 0; i < MAX_TCP_SOCKETS; i++) {
            if (tcp_sockets[i].state != 0 && tcp_sockets[i].state != 2) {
                tcp_sockets[i].state = 0;
                sock = &tcp_sockets[i];
                serial_write("[TCP] Recycled stale socket\n");
                break;
            }
        }
        if (!sock) return nullptr;
    }

    memset(sock, 0, sizeof(TcpSocket));
    sock->remote_ip = ip;
    sock->local_port = next_ephemeral_port++;
    sock->remote_port = port;
    sock->seq = timer_get_ticks() * 12345 + 67890; // Simple ISN
    sock->ack = 0;
    sock->state = 1; // SYN_SENT

    serial_write("[TCP] Sending SYN...\n");

    // Send SYN with retries
    tcp_send_packet(sock, TCP_SYN, nullptr, 0);
    sock->seq++; // SYN consumes a sequence number

    // Wait for SYN-ACK with SYN retransmit
    for (int retry = 0; retry < 3; retry++) {
        for (int i = 0; i < 2000; i++) {
            net_poll();
            if (sock->state == 2) {
                serial_write("[TCP] Connected!\n");
                return sock;
            }
            for (volatile int j = 0; j < 5000; j++);
        }
        if (retry < 2) {
            // Retransmit SYN
            sock->seq--; // Undo the increment to resend same SYN
            serial_write("[TCP] Retransmitting SYN...\n");
            tcp_send_packet(sock, TCP_SYN, nullptr, 0);
            sock->seq++;
        }
    }

    // Timeout
    sock->state = 0;
    char dbg[80];
    ksprintf(dbg, "[TCP] Connect timeout to %d.%d.%d.%d:%d\n",
             ip & 0xFF, (ip>>8)&0xFF, (ip>>16)&0xFF, (ip>>24)&0xFF, port);
    serial_write(dbg);
    return nullptr;
}

bool net_tcp_send(TcpSocket* sock, const void* data, uint16_t len) {
    if (!sock || sock->state != 2) return false;
    // Send in chunks of up to 1400 bytes
    const uint8_t* p = (const uint8_t*)data;
    while (len > 0) {
        uint16_t chunk = len > 1400 ? 1400 : len;
        tcp_send_packet(sock, TCP_PSH | TCP_ACK, p, chunk);
        sock->seq += chunk;
        p += chunk;
        len -= chunk;
        // Brief pause between chunks
        for (volatile int i = 0; i < 10000; i++);
    }
    return true;
}

uint16_t net_tcp_recv(TcpSocket* sock, void* buf, uint16_t buf_size) {
    if (!sock || sock->state < 2) return 0;
    if (sock->rx_len == 0) return 0;
    uint16_t copy = sock->rx_len < buf_size ? sock->rx_len : buf_size;
    memcpy(buf, sock->rx_buf, copy);
    // Shift remaining
    if (copy < sock->rx_len)
        memcpy(sock->rx_buf, sock->rx_buf + copy, sock->rx_len - copy);
    sock->rx_len -= copy;
    if (sock->rx_len == 0) sock->rx_ready = false;
    return copy;
}

void net_tcp_close(TcpSocket* sock) {
    if (!sock || sock->state == 0) return;
    if (sock->state == 2) {
        tcp_send_packet(sock, TCP_FIN | TCP_ACK, nullptr, 0);
        sock->seq++;
        sock->state = 3;
        // Wait briefly for ACK
        for (int i = 0; i < 500; i++) {
            net_poll();
            if (sock->state == 0) return;
            for (volatile int j = 0; j < 5000; j++);
        }
    }
    sock->state = 0;
}

// ============================================================
// DHCP
// ============================================================
void net_dhcp_discover() {
    uint8_t frame[600];
    memset(frame, 0, sizeof(frame));

    // Build DHCP Discover inside UDP inside IP inside Ethernet
    EthHeader* eth = (EthHeader*)frame;
    memcpy(eth->dst, BROADCAST_MAC, 6);
    memcpy(eth->src, our_mac, 6);
    eth->type = htons(ETH_TYPE_IP);

    IpHeader* ip = (IpHeader*)(frame + 14);
    ip->ver_ihl = 0x45;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_UDP;
    ip->src_ip = 0;
    ip->dst_ip = 0xFFFFFFFF;

    UdpHeader* udp = (UdpHeader*)(frame + 34);
    udp->src_port = htons(68);
    udp->dst_port = htons(67);

    DhcpMessage* dhcp = (DhcpMessage*)(frame + 42);
    dhcp->op = 1; // Boot request
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->xid = htonl(0xDEAD0001);
    dhcp->flags = htons(0x8000); // Broadcast
    memcpy(dhcp->chaddr, our_mac, 6);
    dhcp->magic_cookie = htonl(0x63825363);

    // Options: DHCP Discover
    dhcp->options[0] = 53; dhcp->options[1] = 1; dhcp->options[2] = 1; // DHCP Discover
    dhcp->options[3] = 55; dhcp->options[4] = 3; // Parameter request
    dhcp->options[5] = 1;  // Subnet mask
    dhcp->options[6] = 3;  // Router
    dhcp->options[7] = 6;  // DNS
    dhcp->options[8] = 0xFF; // End

    uint16_t dhcp_len = sizeof(DhcpMessage);
    udp->length = htons(8 + dhcp_len);
    ip->total_len = htons(20 + 8 + dhcp_len);
    ip->checksum = 0;
    ip->checksum = ip_checksum(ip, 20);

    e1000_send(frame, 14 + 20 + 8 + dhcp_len);
    serial_write("[NET] DHCP Discover sent\n");
}

// ============================================================
// DNS
// ============================================================
uint32_t net_dns_resolve(const char* hostname) {
    if (!config.configured || config.dns == 0) return 0;

    uint8_t query[256];
    memset(query, 0, sizeof(query));

    // DNS header
    dns_xid = (uint16_t)(timer_get_ticks() & 0xFFFF);
    query[0] = dns_xid >> 8; query[1] = dns_xid & 0xFF; // Transaction ID
    query[2] = 0x01; query[3] = 0x00; // Standard query, recursion desired
    query[4] = 0x00; query[5] = 0x01; // 1 question
    // Answer/auth/additional = 0

    // Encode hostname as DNS labels
    int pos = 12;
    const char* p = hostname;
    while (*p) {
        const char* dot = p;
        while (*dot && *dot != '.') dot++;
        int label_len = dot - p;
        query[pos++] = label_len;
        for (int i = 0; i < label_len; i++) query[pos++] = p[i];
        p = *dot ? dot + 1 : dot;
    }
    query[pos++] = 0; // Root label
    query[pos++] = 0; query[pos++] = 1;  // Type A
    query[pos++] = 0; query[pos++] = 1;  // Class IN

    dns_resolved = false;
    dns_result_ip = 0;

    net_send_udp(config.dns, 53, 53, query, pos);

    // Wait for response
    for (int i = 0; i < 3000; i++) {
        net_poll();
        if (dns_resolved) return dns_result_ip;
        for (volatile int j = 0; j < 5000; j++);
    }
    return 0;
}

// ============================================================
// PACKET HANDLING
// ============================================================
static void handle_ip(const uint8_t* data, uint16_t len) {
    if (len < 20) return;
    const IpHeader* ip = (const IpHeader*)data;
    int hdr_len = (ip->ver_ihl & 0x0F) * 4;
    uint16_t total = ntohs(ip->total_len);
    if (total > len) total = len;
    uint16_t payload_len = total - hdr_len;
    const uint8_t* payload = data + hdr_len;

    switch (ip->protocol) {
        case IP_PROTO_ICMP: handle_icmp(ip, payload, payload_len); break;
        case IP_PROTO_UDP:  handle_udp(ip, payload, payload_len);  break;
        case IP_PROTO_TCP:  handle_tcp(ip, payload, payload_len);  break;
    }
}

// ============================================================
// INIT / POLL
// ============================================================
void net_init() {
    memset(&config, 0, sizeof(config));
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(tcp_sockets, 0, sizeof(tcp_sockets));
    e1000_get_mac(our_mac);
    net_up = true;
}

static int poll_pkt_count = 0;
static int poll_tcp_count = 0;

void net_poll() {
    if (!net_up) return;
    e1000_poll();

    uint16_t len;
    while ((len = e1000_recv(pkt_buf, sizeof(pkt_buf))) > 0) {
        if (len < 14) continue;
        poll_pkt_count++;
        EthHeader* eth = (EthHeader*)pkt_buf;
        uint16_t type = ntohs(eth->type);
        switch (type) {
            case ETH_TYPE_ARP: handle_arp(pkt_buf + 14, len - 14); break;
            case ETH_TYPE_IP: {
                const IpHeader* ip = (const IpHeader*)(pkt_buf + 14);
                if (ip->protocol == IP_PROTO_TCP) {
                    poll_tcp_count++;
                    if (poll_tcp_count <= 20) {
                        char dbg[80];
                        uint16_t ip_total = ntohs(ip->total_len);
                        ksprintf(dbg, "[NET] TCP pkt #%d len=%d ip_total=%d\n", poll_tcp_count, len, ip_total);
                        serial_write(dbg);
                    }
                }
                handle_ip(pkt_buf + 14, len - 14);
                break;
            }
        }
    }
}

NetConfig* net_get_config() {
    return &config;
}

bool net_is_up() {
    return net_up && e1000_link_up();
}
