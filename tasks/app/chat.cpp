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
#define TEST_DEST_ADDR 0x02

#define TEST_RECEIVE

LoraPacket test_packet;
DataLinkError send_state;
bool receive_packet;
int cnt = 0;
int seq;

void GenerateTestNumber(uint8_t *content) {
  content[0]++;
  for (uint8_t i = 0; i < TEST_BUFFER_LENGTH; i++) {
    content[i] = content[0] + i;
  }
}

void sender_callback(const LoraPacket *pak, DataLinkError error) { send_state = error; }

void receiver_callback(const LoraPacket *pak, DataLinkError error) {
  memset(&test_packet, 0, sizeof(test_packet));
  memcpy(&test_packet, pak, pak->header.length);
  receive_packet = true;
}
void ChatMain([[maybe_unused]] void *p) {
  while (true) {
    printf("[ChatMain] running\r\n", 0);
    LoraService lora_service = LORA_SERVICE_LINK_STATE;
    send_state = DataLink_Unknow;
    cnt = 0;
    seq = 0;
#ifdef TEST_RECEIVE
    DataLinkRegisterService(false, LORA_SERVICE_LINK_STATE, receiver_callback);
    DataLinkReceivePacketBegin();
#endif
#ifdef TEST_SEND
    DataLinkRegisterService(true, LORA_SERVICE_LINK_STATE, sender_callback);
    DataLinkReceivePacketBegin();
#endif
    while (true) {
      cnt++;
#ifdef TEST_RECEIVE
      if (receive_packet) {
        receive_packet = false;
        printf("Receive Packet, Src = %d, Seq = %d\n\r", test_packet.header.src_addr, test_packet.header.settings.seq);
      }
#endif

#ifdef TEST_SEND
      // vTaskDelay(5);
      seq = seq ^ 1;
      test_packet.header.settings.seq = seq;
      test_packet.header.dest_addr = TEST_DEST_ADDR;
      test_packet.header.src_addr = LORA_ADDR;
      test_packet.header.length = MAX_LORA_PACKET_SIZE;
      GenerateTestNumber(test_packet.content);
      assert(DataLinkDeclareTransmitBuffer() != 0);
      memcpy(&datalink_transmit_buffer, &test_packet, MAX_LORA_PACKET_SIZE);
      DataLinkError error_number = DataLinkSendPacket(lora_service, &datalink_transmit_buffer);
      if (error_number != DataLink_OK) {
        printf("cnt = %d, Error Code = %d\r\n", cnt, error_number);
      } else {
        printf("cnt = %d, convoke done\r\n", cnt);
        while (send_state == DataLink_Unknow) {
        }
        if (send_state == DataLink_OK) {
          printf("cnt = %d, send success!\r\n", cnt);
        } else {
          printf("cnt = %d. send failed, Error Code = %d\r\n", cnt, send_state);
        }
        send_state = DataLink_Unknow;
      }
      DataLinkReleaseTransmitBuffer();
#endif
    }
  }
}
