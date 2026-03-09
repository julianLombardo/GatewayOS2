#include "e1000.h"
#include "pci.h"
#include "serial.h"
#include "../memory/pmm.h"
#include "../memory/heap.h"
#include "../kernel/idt.h"
#include "../lib/string.h"
#include "../lib/printf.h"

// E1000 MMIO Register offsets
#define REG_CTRL    0x0000
#define REG_STATUS  0x0008
#define REG_EECD    0x0010
#define REG_EERD    0x0014
#define REG_ICR     0x00C0
#define REG_IMS     0x00D0
#define REG_IMC     0x00D8
#define REG_RCTL    0x0100
#define REG_TCTL    0x0400
#define REG_TIPG    0x0410
#define REG_RDBAL   0x2800
#define REG_RDBAH   0x2804
#define REG_RDLEN   0x2808
#define REG_RDH     0x2810
#define REG_RDT     0x2818
#define REG_TDBAL   0x3800
#define REG_TDBAH   0x3804
#define REG_TDLEN   0x3808
#define REG_TDH     0x3810
#define REG_TDT     0x3818
#define REG_MTA     0x5200
#define REG_RAL     0x5400
#define REG_RAH     0x5404

// CTRL bits
#define CTRL_SLU    (1 << 6)   // Set Link Up
#define CTRL_RST    (1 << 26)  // Device Reset

// RCTL bits
#define RCTL_EN     (1 << 1)
#define RCTL_SBP    (1 << 2)
#define RCTL_UPE    (1 << 3)   // Unicast promiscuous
#define RCTL_MPE    (1 << 4)   // Multicast promiscuous
#define RCTL_BAM    (1 << 15)  // Broadcast accept
#define RCTL_BSIZE_2048 (0 << 16)
#define RCTL_SECRC  (1 << 26)  // Strip CRC

// TCTL bits
#define TCTL_EN     (1 << 1)
#define TCTL_PSP    (1 << 3)

// TX command bits
#define TCMD_EOP    (1 << 0)
#define TCMD_IFCS   (1 << 1)
#define TCMD_RS     (1 << 3)

// Descriptor status bits
#define RDESC_STAT_DD  (1 << 0)
#define TDESC_STAT_DD  (1 << 0)

#define NUM_RX_DESC 32
#define NUM_TX_DESC 8
#define PKT_BUF_SIZE 2048

// RX descriptor
struct __attribute__((packed)) E1000RxDesc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
};

// TX descriptor
struct __attribute__((packed)) E1000TxDesc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
};

// Ring buffer for received packets
#define RX_RING_SIZE 16
#define RX_RING_PKT_SIZE 2048

struct RxPacket {
    uint8_t data[RX_RING_PKT_SIZE];
    uint16_t len;
    bool valid;
};

// E1000 state
static volatile uint32_t* mmio_base = nullptr;
static E1000RxDesc* rx_descs = nullptr;
static E1000TxDesc* tx_descs = nullptr;
static uint8_t* rx_buffers[NUM_RX_DESC];
static uint8_t* tx_buffers[NUM_TX_DESC];
static uint16_t rx_cur = 0;
static uint16_t tx_cur = 0;
static uint8_t mac_addr[6];
static bool initialized = false;

// Software RX ring
static RxPacket rx_ring[RX_RING_SIZE];
static int rx_ring_head = 0;
static int rx_ring_tail = 0;

static void e1000_write(uint32_t reg, uint32_t val) {
    mmio_base[reg / 4] = val;
}

static uint32_t e1000_read(uint32_t reg) {
    return mmio_base[reg / 4];
}

static uint16_t e1000_eeprom_read(uint8_t addr) {
    e1000_write(REG_EERD, (1) | ((uint32_t)addr << 8));
    uint32_t val;
    for (int i = 0; i < 10000; i++) {
        val = e1000_read(REG_EERD);
        if (val & (1 << 4)) break;
    }
    return (val >> 16) & 0xFFFF;
}

static bool e1000_detect_eeprom() {
    e1000_write(REG_EERD, 0x1);
    for (int i = 0; i < 1000; i++) {
        if (e1000_read(REG_EERD) & (1 << 4))
            return true;
    }
    return false;
}

static void e1000_read_mac() {
    bool has_eeprom = e1000_detect_eeprom();
    if (has_eeprom) {
        uint16_t w0 = e1000_eeprom_read(0);
        uint16_t w1 = e1000_eeprom_read(1);
        uint16_t w2 = e1000_eeprom_read(2);
        mac_addr[0] = w0 & 0xFF; mac_addr[1] = (w0 >> 8) & 0xFF;
        mac_addr[2] = w1 & 0xFF; mac_addr[3] = (w1 >> 8) & 0xFF;
        mac_addr[4] = w2 & 0xFF; mac_addr[5] = (w2 >> 8) & 0xFF;
    } else {
        // Read from RAL/RAH (MMIO mapped MAC)
        uint32_t ral = e1000_read(REG_RAL);
        uint32_t rah = e1000_read(REG_RAH);
        mac_addr[0] = ral & 0xFF;
        mac_addr[1] = (ral >> 8) & 0xFF;
        mac_addr[2] = (ral >> 16) & 0xFF;
        mac_addr[3] = (ral >> 24) & 0xFF;
        mac_addr[4] = rah & 0xFF;
        mac_addr[5] = (rah >> 8) & 0xFF;
    }
}

static void e1000_rx_init() {
    // Allocate RX descriptors (must be 16-byte aligned)
    rx_descs = (E1000RxDesc*)kmalloc_aligned(sizeof(E1000RxDesc) * NUM_RX_DESC, 16);
    memset(rx_descs, 0, sizeof(E1000RxDesc) * NUM_RX_DESC);

    for (int i = 0; i < NUM_RX_DESC; i++) {
        rx_buffers[i] = (uint8_t*)kmalloc_aligned(PKT_BUF_SIZE, 16);
        rx_descs[i].addr = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }

    e1000_write(REG_RDBAL, (uint32_t)rx_descs);
    e1000_write(REG_RDBAH, 0);
    e1000_write(REG_RDLEN, sizeof(E1000RxDesc) * NUM_RX_DESC);
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, NUM_RX_DESC - 1);

    // Enable receiver
    e1000_write(REG_RCTL, RCTL_EN | RCTL_BAM | RCTL_BSIZE_2048 | RCTL_SECRC);
}

static void e1000_tx_init() {
    tx_descs = (E1000TxDesc*)kmalloc_aligned(sizeof(E1000TxDesc) * NUM_TX_DESC, 16);
    memset(tx_descs, 0, sizeof(E1000TxDesc) * NUM_TX_DESC);

    for (int i = 0; i < NUM_TX_DESC; i++) {
        tx_buffers[i] = (uint8_t*)kmalloc_aligned(PKT_BUF_SIZE, 16);
        tx_descs[i].addr = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].status = TDESC_STAT_DD; // Mark as available
        tx_descs[i].cmd = 0;
    }

    e1000_write(REG_TDBAL, (uint32_t)tx_descs);
    e1000_write(REG_TDBAH, 0);
    e1000_write(REG_TDLEN, sizeof(E1000TxDesc) * NUM_TX_DESC);
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);

    // Inter-packet gap
    e1000_write(REG_TIPG, 0x0060200A);

    // Enable transmitter
    e1000_write(REG_TCTL, TCTL_EN | TCTL_PSP | (15 << 4) | (64 << 12));
}

static void e1000_irq_handler(Registers* regs) {
    (void)regs;
    uint32_t icr = e1000_read(REG_ICR); // Read clears interrupt
    if (icr & 0x80) { // RX packet
        e1000_poll();
    }
}

bool e1000_init() {
    PciDevice pci;
    if (!pci_find_device(E1000_VENDOR, E1000_DEVICE, &pci)) {
        serial_write("[E1000] NIC not found\n");
        return false;
    }

    char buf[64];
    ksprintf(buf, "[E1000] Found at PCI %d:%d BAR0=0x%08X IRQ=%d\n",
             pci.bus, pci.dev, pci.bar0, pci.irq);
    serial_write(buf);

    // Enable bus mastering for DMA
    pci_enable_bus_master(&pci);

    // Map MMIO
    mmio_base = (volatile uint32_t*)pci.bar0;

    // Reset device
    e1000_write(REG_IMC, 0xFFFFFFFF); // Disable all interrupts
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | CTRL_RST);
    // Wait for reset
    for (volatile int i = 0; i < 100000; i++);
    e1000_write(REG_IMC, 0xFFFFFFFF); // Disable interrupts again after reset

    // Set link up
    e1000_write(REG_CTRL, e1000_read(REG_CTRL) | CTRL_SLU);

    // Clear multicast table
    for (int i = 0; i < 128; i++)
        e1000_write(REG_MTA + i * 4, 0);

    // Read MAC address
    e1000_read_mac();
    ksprintf(buf, "[E1000] MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
             mac_addr[0], mac_addr[1], mac_addr[2],
             mac_addr[3], mac_addr[4], mac_addr[5]);
    serial_write(buf);

    // Setup RX/TX
    e1000_rx_init();
    e1000_tx_init();

    // Init software RX ring
    memset(rx_ring, 0, sizeof(rx_ring));
    rx_ring_head = 0;
    rx_ring_tail = 0;

    // Install IRQ handler and enable interrupts
    if (pci.irq < 16) {
        irq_install_handler(pci.irq, e1000_irq_handler);
    }
    e1000_write(REG_IMS, 0x1F6DC); // Enable useful interrupts

    initialized = true;
    serial_write("[E1000] Initialized\n");
    return true;
}

bool e1000_send(const void* data, uint16_t len) {
    if (!initialized || len > PKT_BUF_SIZE) return false;

    // Wait for descriptor to be available
    if (!(tx_descs[tx_cur].status & TDESC_STAT_DD)) return false;

    memcpy(tx_buffers[tx_cur], data, len);
    tx_descs[tx_cur].length = len;
    tx_descs[tx_cur].cmd = TCMD_EOP | TCMD_IFCS | TCMD_RS;
    tx_descs[tx_cur].status = 0;

    uint16_t old = tx_cur;
    tx_cur = (tx_cur + 1) % NUM_TX_DESC;
    e1000_write(REG_TDT, tx_cur);

    // Wait for send completion (polling, with timeout)
    for (int i = 0; i < 100000; i++) {
        if (tx_descs[old].status & TDESC_STAT_DD) return true;
    }
    return true; // Assume sent
}

void e1000_poll() {
    while (rx_descs[rx_cur].status & RDESC_STAT_DD) {
        uint16_t len = rx_descs[rx_cur].length;

        if (len > 0 && len <= RX_RING_PKT_SIZE) {
            // Copy to software ring buffer
            int next = (rx_ring_head + 1) % RX_RING_SIZE;
            if (next != rx_ring_tail) { // Not full
                memcpy(rx_ring[rx_ring_head].data, rx_buffers[rx_cur], len);
                rx_ring[rx_ring_head].len = len;
                rx_ring[rx_ring_head].valid = true;
                rx_ring_head = next;
            }
        }

        // Return descriptor to hardware
        rx_descs[rx_cur].status = 0;
        uint16_t old = rx_cur;
        rx_cur = (rx_cur + 1) % NUM_RX_DESC;
        e1000_write(REG_RDT, old);
    }
}

uint16_t e1000_recv(void* buf, uint16_t buf_size) {
    if (rx_ring_tail == rx_ring_head) return 0; // Empty

    RxPacket* pkt = &rx_ring[rx_ring_tail];
    if (!pkt->valid) return 0;

    uint16_t len = pkt->len;
    if (len > buf_size) len = buf_size;
    memcpy(buf, pkt->data, len);
    pkt->valid = false;
    rx_ring_tail = (rx_ring_tail + 1) % RX_RING_SIZE;
    return len;
}

void e1000_get_mac(uint8_t* out) {
    memcpy(out, mac_addr, 6);
}

bool e1000_link_up() {
    if (!initialized) return false;
    return (e1000_read(REG_STATUS) & 2) != 0; // Link up bit
}
