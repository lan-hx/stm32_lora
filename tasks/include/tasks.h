/**
 * @brief 任务入口、管理
 * @author lan
 */

#ifndef LORA_TASKS_TASK_H_
#define LORA_TASKS_TASK_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 分配任务资源
 */
void TaskResourceInit();

/**
 * @brief 分配任务并启动
 */
void TaskInit();

#ifdef __cplusplus
}
#endif

#endif  // LORA_TASKS_TASK_H_
