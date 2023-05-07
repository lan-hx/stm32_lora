/**
 * @brief 网络进程
 * @author lan
 */

#define DATALINK_IMPL

#include "service/web/web.h"

#include "service/web/data_link.h"

void WebMain([[maybe_unused]] void *p) { DataLinkEventLoop(); }
