/**
 * @brief 网络相关设置
 * @author lan
 */

#ifndef LORA_TASKS_INCLUDE_SERVICE_CONFIG_H_
#define LORA_TASKS_INCLUDE_SERVICE_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_LORA_PACKET_SIZE 255  // 物理层包最大长度
#define LORA_TIMEOUT_IN_MS 1000   // 物理层超时时间
#define LORA_TIMEOUT_RETRY 2      // 物理层失败尝试次数

#define DATA_LINK_HEARD_LIST_TICK 1000           // heard list定时器精度
#define DATA_LINK_DETECT_TIME 30000              // 数据链路层探测包广播间隔
#define DATA_LINK_LINK_STATE_COOLDOWN_TIME 1000  // link state包的最小间隔
#define DATA_LINK_HEARD_LIST_REFRESH 30000       // heard list更新间隔
#define DATA_LINK_HEARD_LIST_TIMEOUT 120000      // heard list项超时时间

#define DATA_LINK_TIMEOUT_IN_MS 1000  // 数据链路层超时时间
#define DATA_LINK_RETRY 5             // 数据链路层重传次数

// m3新增
#define NETWORK_ROUTE_CYCLE_IN_MS 5000       // 网络层发送路由包的周期
#define NORMAL_PACKET_SEND_CYCLE_IN_MS 3000  // 发普通包的周期

enum NetworkType {
  LoraType = 0,
  WiFiType = 1,
};

/**
 * @brief 本机MAC地址
 * @note
 * 地址分配如下：
 * 0x00: loopback
 * 0x01-MAX_VALID_LORA_ADDR: valid address
 * MAX_VALID_LORA_ADDR+1-0xFE: reserved
 * 0xFF: broadcast
 */
#ifndef LORA_ADDR
#define LORA_ADDR 0x01
// #define TEST_DEST_ADDR 0x3
#define REJECTED_LORA_ADDR 0x03  // m3新增

#endif  // LORA_ADDR
#define MAX_VALID_LORA_ADDR 0x7F

#define LORA_MAGIC_NUMBER 0x55
#define LORA_TRANSFER_NUM 2  // Lora最大中转站数量

// 服务类型 LoraPacket.settings.service
#define LORA_SERVICE_NUM 5
#define MIN_LORA_TRANSPORT_SERVICE 4  // 大于等于这个服务号需要走传输层认证，可能会变，不要偷懒
enum LoraService {
  LORA_SERVICE_LINK_STATE = 0,
  LORA_SERVICE_UNAVALIABLE,
  // LORA_SERVICE_NUM,
};

/**
 * @brief Lora packet header structure
 * @note
 * if src/dst addr is loopback, we should consider it as `LORA_ADDR`
 * seq: should be kept for every peer
 * nak: may be removed in future
 * crc: will be REMOVED after M2
 */
typedef struct LoraPacketHeader {
  uint8_t dest_addr = 0;
  // if addr is 0, it should be ignored.
  uint8_t transfer_addr[LORA_TRANSFER_NUM] = {0};
  uint8_t curr_addr = {0};
  uint8_t src_addr = {0};
  uint8_t magic_number = LORA_MAGIC_NUMBER;
  struct {
    uint8_t seq : 1;
    bool ack : 1;
    bool use_wifi : 1;
    bool nak : 1;
    uint8_t reserved : 1;
    uint8_t service : 3;
  } settings = {0, 0, 0, 0, 0, 0};
  uint8_t length = 0;
  uint8_t crc = 0;  // xor of crc16 high and low bytes
                    // uint8_t reserved = 0;
} LoraPacketHeader;

#define MAX_LORA_CONTENT_LENGTH (MAX_LORA_PACKET_SIZE - sizeof(LoraPacketHeader))

typedef struct LoraPacket {
  LoraPacketHeader header;
  uint8_t content[MAX_LORA_CONTENT_LENGTH];
} LoraPacket;

static_assert(sizeof(LoraPacket) == MAX_LORA_PACKET_SIZE);

typedef struct NetworkLinkStatePacket {
  uint8_t num;
  uint8_t sender_addr;
  // uint8_t registered_service;
  struct NeighborState {
    uint8_t next_addr;
    uint8_t next_next_addr;
    uint8_t registered_service;
  } neighbor[0];
} NetworkLinkStatePacket;

// TODO: to be validated
#define MAX_LINK_STATE_PACKET_NUM                                              \
  ((MAX_LORA_CONTENT_LENGTH - offsetof(NetworkLinkStatePacket, neighbor[0])) / \
   sizeof(NetworkLinkStatePacket::NeighborState))

#define MAX_NETWORK_PACKET (1 * (MAX_LORA_CONTENT_LENGTH))

#ifdef __cplusplus
}
#endif

#endif  // LORA_TASKS_INCLUDE_SERVICE_CONFIG_H_
