/**
 * @brief watchdog
 * @author lan
 */

#include "service/watchdog/watchdog.h"

#include <stdio.h>
#include <sys/unistd.h>

#include "FreeRTOS.h"
#include "task.h"
#include "utility.h"

void WatchdogMain([[maybe_unused]] void *p) {
  auto tick = xTaskGetTickCount();
  while (true) {
    // auto start = GetHighResolutionTick64();
    // printf("watchdog is running! Freertos uptime: %u.%03us. System uptime: %u.%06us\r\n", xTaskGetTickCount() / 1000,
    //        xTaskGetTickCount() % 1000, uint32_t(GetHighResolutionTick64() / 1000000),
    //        uint32_t(GetHighResolutionTick64() % 1000000));
    // auto end = GetHighResolutionTick64();
    // printf("watchdog printf: start %uus, end %uus, delay %uus\r\n", (uint32_t)start, (uint32_t)end,
    //        (uint32_t)(end - start));
    // auto start2 = GetHighResolutionTick64();
    // auto end2 = GetHighResolutionTick64();
    // printf("GetHighResolutionTick64(): start %uus, end %uus, delay %uus\r\n", (uint32_t)start2, (uint32_t)end2,
    //        (uint32_t)(end2 - start2));
    // auto start1 = GetHighResolutionTick64();
    // HighResolutionDelay(5);
    // auto end1 = GetHighResolutionTick64();
    // printf("high resolution delay: start %uus, end %uus, delay %uus\r\n", (uint32_t)start1, (uint32_t)end1,
    //        (uint32_t)(end1 - start1));
    // auto start3 = GetHighResolutionTick64();
    // taskYIELD();  // 3-4us
    // auto end3 = GetHighResolutionTick64();
    // printf("context switch: start %uus, end %uus, delay %uus\r\n", (uint32_t)start3, (uint32_t)end3,
    //        (uint32_t)(end3 - start3));
    // auto start4 = GetHighResolutionTick64();
    // write(stdout->_file, "\r\n", 2);
    // auto end4 = GetHighResolutionTick64();
    // printf("null uart write: start %uus, end %uus, delay %uus\r\n", (uint32_t)start4, (uint32_t)end4,
    //        (uint32_t)(end4 - start4));
    vTaskDelayUntil(&tick, 5000);
  }
}