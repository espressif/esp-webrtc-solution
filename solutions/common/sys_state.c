/* Sys state

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#ifndef CONFIG_HEAP_TRACING_OFF
#include "esp_heap_trace.h"
#endif
#include "esp_heap_caps.h"
#include "sys_state.h"
#include "esp_idf_version.h"

#define TAG "SYS_STATE"

#define NUM_RECORDS 500

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
#define get_task_start xTaskGetStackStart
#else
#define get_task_start pxTaskGetStackStart
#endif

#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
#include "esp_memory_utils.h"
#ifndef configRUN_TIME_COUNTER_TYPE
#define configRUN_TIME_COUNTER_TYPE uint32_t
#endif
static int get_task_info(TaskStatus_t **array, int *array_size, configRUN_TIME_COUNTER_TYPE *run_time)
{
    int           size      = uxTaskGetNumberOfTasks() + 8;
    TaskStatus_t *new_array = realloc(*array, sizeof(TaskStatus_t) * size);
    if (new_array == NULL) {
        return -1;
    }
    size        = uxTaskGetSystemState(new_array, size, run_time);
    *array_size = size;
    *array      = new_array;
    return 0;
}

static void show_threads()
{
    static TaskStatus_t        *start_array  = NULL;
    static TaskStatus_t        *end_array    = NULL;
    const char                 *task_stack[] = { "Extr", "Intr" };
    const char                 *task_state[] = { "Running", "Ready", "Blocked", "Suspend", "Deleted" };
    int                         start_array_size, end_array_size;
    configRUN_TIME_COUNTER_TYPE start_run_time = 0, end_run_time = 0;
    if (get_task_info(&start_array, &start_array_size, &start_run_time) != 0) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (get_task_info(&end_array, &end_array_size, &end_run_time) != 0) {
        return;
    }
    uint32_t total_elapsed_time = (uint32_t)(end_run_time - start_run_time);
    if (total_elapsed_time) {
        ESP_LOGI(TAG, "| Task              | Run Time    | Per | Prio | HWM       | State   | CoreId   | Stack ");
        // Match each task in start_array to those in the end_array
        for (int i = 0; i < start_array_size; i++) {
            for (int j = 0; j < end_array_size; j++) {
                if (start_array[i].xHandle == end_array[j].xHandle) {
                    uint32_t task_elapsed_time = (uint32_t)(end_array[j].ulRunTimeCounter - start_array[i].ulRunTimeCounter);
                    uint32_t percentage_time   = (task_elapsed_time * 100UL) / (total_elapsed_time);
                    ESP_LOGI(TAG, "| %-17s | %-11d |%2d%%  | %-4u | %-9u | %-7s | %-8x | %s", start_array[i].pcTaskName,
                             (int)task_elapsed_time, (int)percentage_time, start_array[i].uxCurrentPriority,
                             (int)start_array[i].usStackHighWaterMark, task_state[(start_array[i].eCurrentState)],
                             start_array[i].xCoreID,
                             task_stack[esp_ptr_internal(get_task_start(start_array[i].xHandle))]);

                    start_array[i].xHandle = NULL;
                    end_array[j].xHandle   = NULL;
                    break;
                }
            }
        }
        for (int i = 0; i < start_array_size; i++) {
            if (start_array[i].xHandle != NULL) {
                ESP_LOGI(TAG, "| %s | Deleted", start_array[i].pcTaskName);
            }
        }
        for (int i = 0; i < end_array_size; i++) {
            if (end_array[i].xHandle != NULL) {
                ESP_LOGI(TAG, "| %s | Created", end_array[i].pcTaskName);
            }
        }
        printf("\n");
    }
}
#endif

void show_mem()
{
#ifdef CONFIG_SPIRAM_BOOT_INIT
    ESP_LOGI(TAG, "MEM Avail:%d, IRam:%d, PSRam:%d\n", (int)esp_get_free_heap_size(),
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#else
    ESP_LOGI(TAG, "MEM Avail:%d\n", (int)esp_get_free_heap_size());
#endif
}

void sys_state_show()
{
    show_mem();
#if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID && CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
    show_threads();
#endif
}

void sys_state_heap_trace(bool start)
{
#if (defined CONFIG_IDF_TARGET_ESP32S3) && (!defined CONFIG_HEAP_TRACING_OFF)
    static heap_trace_record_t *trace_record;
    if (trace_record == NULL) {
        trace_record = heap_caps_malloc(NUM_RECORDS * sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM);
        heap_trace_init_standalone(trace_record, NUM_RECORDS);
    }
    if (trace_record == NULL) {
        ESP_LOGE(TAG, "No memory to start trace");
        return;
    }
    static bool started = false;
    if (start) {
        ESP_LOGI(TAG, "Start to trace");
        if (started == false) {
            heap_trace_start(HEAP_TRACE_LEAKS);
            started = true;
        } else {
            heap_trace_resume();
        }
    } else {
        heap_trace_alloc_pause();
        heap_trace_dump();
    }
#endif
}
