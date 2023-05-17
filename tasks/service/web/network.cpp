/**
 * @brief 网络层
 * @author lan
 */

#define SERVICE_NETWORK_IMPL
#include "service/web/network.h"

#include "service/web/data_link.h"
// uint8_t network_buffer[MAX_NETWORK_PACKET];

void NetworkUpdateHeardList(NetworkLinkStatePacket *pack) {
  int HeardListOverstack = 1;
  int inlist = 0;
  for (int j = 0; j < MaxHeardListNum; j++) {
    if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->sender_addr) {
      HeardLists[j].tick = HeardListTicks;
      HeardLists[j].addr = pack->sender_addr;
      HeardLists[j].next_addr = pack->sender_addr;
      HeardLists[j].cost = 1;
      //   HeardLists[j].registered_service = pack->registered_service;
      inlist = 1;
      HeardListOverstack = 0;
      break;
    }
  }
  if (!inlist) {
    for (int j = 0; j < MaxHeardListNum; j++) {
      if (HeardLists[j].tick == 0) {
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->sender_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].cost = 1;
        // HeardLists[j].registered_service = pack->registered_service;
        HeardListOverstack = 0;
        break;
      }
    }
  }
  if (HeardListOverstack) {
    assert(HeardListOverstack < 1);
  }

  for (int i = 0; i < pack->num; i++) {
    inlist = 0;
    HeardListOverstack = 1;
    for (int j = 0; j < MaxHeardListNum; j++) {
      if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->neighbor[i].next_addr) {
        if (HeardLists[j].cost == 1) {
          inlist = 1;
          HeardListOverstack = 0;
          break;
        }
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->neighbor[i].next_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].cost = 2;
        HeardLists[j].registered_service = pack->neighbor[i].registered_service;
        inlist = 1;
        HeardListOverstack = 0;
        break;
      }
      if (HeardLists[j].tick > 0 && HeardLists[j].addr == pack->neighbor[i].next_next_addr) {
        if (HeardLists[j].cost < 3) {
          inlist = 1;
          HeardListOverstack = 0;
          break;
        }
        HeardLists[j].tick = HeardListTicks;
        HeardLists[j].addr = pack->neighbor[i].next_next_addr;
        HeardLists[j].next_addr = pack->sender_addr;
        HeardLists[j].next_next_addr = pack->neighbor[i].next_addr;
        HeardLists[j].cost = 3;
        HeardLists[j].registered_service = pack->neighbor[i].registered_service;
        inlist = 1;
        HeardListOverstack = 0;
        break;
      }
    }
    if (!inlist) {
      for (int j = 0; j < MaxHeardListNum; j++) {
        if (HeardLists[j].tick == 0) {
          if (pack->neighbor[i].next_next_addr != 0xFF) {
            HeardLists[j].tick = HeardListTicks;
            HeardLists[j].addr = pack->neighbor[i].next_addr;
            HeardLists[j].next_addr = pack->sender_addr;
            HeardLists[j].cost = 2;
            HeardLists[j].registered_service = pack->neighbor[i].registered_service;
            HeardListOverstack = 0;
            break;
          } else {
            HeardLists[j].tick = HeardListTicks;
            HeardLists[j].addr = pack->neighbor[i].next_next_addr;
            HeardLists[j].next_addr = pack->sender_addr;
            HeardLists[j].next_next_addr = pack->neighbor[i].next_addr;
            HeardLists[j].cost = 3;
            HeardLists[j].registered_service = pack->neighbor[i].registered_service;
            inlist = 1;
            HeardListOverstack = 0;
            break;
          }
        }
      }
    }
    if (HeardListOverstack) {
      assert(HeardListOverstack < 1);
    }
  }
}

/**
 * @brief 网络层事件处理
 */
void NetworkEventLoop() {}
