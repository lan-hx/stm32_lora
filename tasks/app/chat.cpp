/**
 * @brief 文本聊天app
 * @author lan
 */

#include "app/chat.h"

#include <string.h>

#include "main.h"
#include "service/lora/lora.h"
#include "service/web/config.h"
#include "service/web/data_link.h"
#include "service/web/web.h"
#include "utility.h"

#define TEST_BUFFER_LENGTH MAX_LORA_CONTENT_LENGTH
#define TEST_DEST_ADDR 01

// #define TEST_SEND
#define TEST_RECEIVE

DataLinkError send_state = DataLink_OK;
LoraPacket test_packet;
bool receive_packet = false;
int cnt = 0;

void GenerateTestNumber(uint8_t *content) {
  content[0]++;
  for (uint8_t i = 0; i < TEST_BUFFER_LENGTH; i++) {
    content[i] = content[0] + i;
  }
}
// send_state = DataLink_OK —— 发送包成功收到Ack
//              DataLink_Busy —— 上一个包还没有发完(不是Tx_Init)
//              DataLink_TxFailed —— 包重传超过最大次数(5次)
//              DataLink_Unknow —— 发送一个包之后短暂处于次状态
void sender_callback(const LoraPacket *pak, DataLinkError error) { send_state = error; }

void receiver_callback(const LoraPacket *pak, DataLinkError error) {
  memset(&test_packet, 0, sizeof(test_packet));
  memcpy(&test_packet, pak, pak->header.length);
  printf("RECEIVER_CALLBACK \r\n");
  receive_packet = true;
}

// m3新增
/*定时发送普通包，暂定3s一次*/

TimerHandle_t normal_packet_send_timer;
StaticTimer_t normal_packet_send_timer_buffer;

void NormalPacketSendTimerCallBack(TimerHandle_t xTimer) {
  LoraService lora_service = LORA_SERVICE_LINK_STATE;
  if (send_state != DataLink_Unknow) {
    if (send_state == DataLink_OK) {
      if (cnt == 0) {
        printf("begin send...\r\n");
      } else {
        printf("cnt = %d, send success!\r\n", cnt);
        DataLinkReleaseTransmitBuffer();
      }
    } else {
      printf("cnt = %d. send failed, Error Code = %d\r\n", cnt, send_state);
      DataLinkReleaseTransmitBuffer();
    }
    test_packet.header.dest_addr = TEST_DEST_ADDR;
    test_packet.header.src_addr = LORA_ADDR;
    test_packet.header.length = MAX_LORA_PACKET_SIZE;
    GenerateTestNumber(test_packet.content);
    assert(DataLinkDeclareTransmitBuffer() != 0);
    memcpy(datalink_transmit_buffer, &test_packet, test_packet.header.length);
    DataLinkError error_number = DataLinkSendPacket(lora_service, datalink_transmit_buffer);
    if (error_number == DataLink_OK) {
      printf("(cnt = %d)Upper Level Call DataLinkSendPacket\r\n", cnt);
      send_state = DataLink_Unknow;  // 目前状态unkown 收到Ack就会DataLink_OK
    } else {
      printf("(cnt = %d)DataLink is busy,it can't send a normal packet!\r\n");
    }
    cnt++;
  }
  xTimerStart(normal_packet_send_timer, 0);
  portYIELD();  // 让出CPU
}

void ChatMain([[maybe_unused]] void *p) {
  printf("[ChatMain] running\r\n", 0);

#ifdef TEST_RECEIVE
  DataLinkRegisterService(false, LORA_SERVICE_LINK_STATE, receiver_callback);
#endif
#ifdef TEST_SEND
  DataLinkRegisterService(true, LORA_SERVICE_LINK_STATE, sender_callback);
#endif
  // 开启收包
  DataLinkReceivePacketBegin();
  // 开启网络路由，每5s发送一个路由包
  NetworkBeginRoute();
#ifdef TEST_SEND
  // 每3s发送一个普通包
  xTimerStart(normal_packet_send_timer, 0);
#endif
#ifdef TEST_RECEIVE
  while (true) {
    if (receive_packet) {
      receive_packet = false;
      printf("Receive Packet, Src = %d, Seq = %d, first_number_in_content = %d\n\r", test_packet.header.src_addr,
             test_packet.header.settings.seq, test_packet.content[0]);
    }
#endif
  }
}