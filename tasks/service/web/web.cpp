/**
 * @brief 网络进程
 * @author lan
 */

#include "service/web/web.h"

#include <errno.h>
#include <stdio.h>
#include <stm32f1xx_hal.h>
#include <string.h>

#include "service/web/lora.h"
#include "utility.h"

[[maybe_unused]] static int message = 0;
[[maybe_unused]] static int ret = 0;
[[maybe_unused]] static int ret1 = 0;
[[maybe_unused]] static char buf[256];

void WebMain([[maybe_unused]] void *p) {
  while (true) {
    printf("Slave ...\r\n");
    // HAL_Delay(800);
    printf("Receiving package...\r\n");

    // int sz = scanf("%s", buffer);
    int sz = LoraRead(buf, 256);
    printf("Content (%d): %s\r\n", sz, buf);

    printf("Package received ...\r\n");

    // printf("Master ...\r\n");
    //// HAL_Delay(1000);
    // printf("Sending package...\r\n");
    //
    // ret = fprintf(flora, "Hello from 02 %0241d", message);
    // if (ret == -1) {
    //   printf("fprintf error: %s\r\n", strerror(errno));
    // }
    // auto start = GetHighResolutionTick();
    // ret1 = fflush(flora);
    // auto end = GetHighResolutionTick();
    // printf("fflush return %d, time: %uus\r\n", ret1, end - start);
    // message += 1;
    //
    // printf("Transmission: %d\r\n", ret);
    // printf("Package sent...\r\n\r\n");
    // if (message % 100 == 0) {
    //   printf("time now: %u\r\n", HAL_GetTick());
    // }
  }
}
