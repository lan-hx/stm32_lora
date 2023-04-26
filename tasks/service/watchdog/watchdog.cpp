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
    printf("watchdog is running! Freertos uptime: %u.%03us. System uptime: %u.%06us\r\n", xTaskGetTickCount() / 1000,
           xTaskGetTickCount() % 1000, uint32_t(GetHighResolutionTick64() / 1000000),
           uint32_t(GetHighResolutionTick64() % 1000000));
    vTaskDelayUntil(&tick, 10000);
  }
}