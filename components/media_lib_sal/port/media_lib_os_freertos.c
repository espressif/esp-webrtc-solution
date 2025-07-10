
/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2021 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in
 * which case, it is free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the
 * Software without restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "media_lib_adapter.h"
#include "media_lib_os_reg.h"
#include "esp_idf_version.h"

#if CONFIG_FREERTOS_ENABLE_TASK_SNAPSHOT
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 2, 0))
#include "esp_private/freertos_debug.h"
#else
#include "freertos/task_snapshot.h"
#endif
#endif

#ifdef __XTENSA__
#include "esp_debug_helpers.h"
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#include "esp_cpu_utils.h"
#endif
#endif
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
#include "esp_memory_utils.h"
#else
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(4, 4, 0))
#include "soc/soc_memory_types.h"
#else
#include "soc/soc_memory_layout.h"
#endif
#endif

#define RETURN_ON_NULL_HANDLE(h)                                               \
    if (h == NULL) {                                                           \
        return ESP_ERR_INVALID_ARG;                                            \
    }

#define TAG "MEDIA_OS"
#define MAX_STACK_SIZE      (100*1024)
#define MAX_SEARCH_CODE_LEN (1024)
#define RISC_V_RET_CODE     (0x8082)

#if CONFIG_SPIRAM_BOOT_INIT

static void *_malloc_in_heap(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void _free_in_heap(void *buf)
{ 
    heap_caps_free(buf);
}

static void *_calloc_in_heap(size_t elm, size_t size)
{
    size_t total = elm * size;
    void *buf = _malloc_in_heap(total);
    if (buf) {
        memset(buf, 0, total);
    }
    return buf;
}

static void *_realloc_in_heap(void *buf, size_t size)
{
    return heap_caps_realloc(buf, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void *_malloc_align_in_heap(size_t size, uint8_t align)
{
    return heap_caps_aligned_alloc(align, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static char *_strdup_in_heap(const char *str)
{
    if (str) {
        int len = strlen(str) + 1;
        void *buf = _malloc_in_heap(len);
        if (buf) {
            memcpy(buf, str, len);
            return buf;
        }
    }
    return NULL;
}

#else

static void *_malloc_align(size_t size, uint8_t align)
{
    if(!align || ((align & (align - 1)) != 0)) {
        return NULL;
    }
    size += align;
    uint8_t *buf = (uint8_t*) malloc(size);
    if (buf == NULL) {
        return NULL;
    }
    uint8_t *aligned = (uint8_t*)(((size_t)buf + align) & (~(align-1)));
    aligned[-1] = (uint8_t)(size_t)(aligned - buf);
    return aligned;
}

static void _free_align(void *addr)
{
    if (addr == NULL) {
        return;
    }
    uint8_t *aligned = (uint8_t*)addr;
    free(aligned - aligned[-1]);
}

#endif

#if defined(CONFIG_SPIRAM_BOOT_INIT) &&                                        \
    (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
BaseType_t __attribute__((weak)) xTaskCreateRestrictedPinnedToCore(
    const TaskParameters_t *const pxTaskDefinition, TaskHandle_t *pxCreatedTask,
    const BaseType_t xCoreID)
{
    ESP_LOGE(TAG,
             "Not found right %s.\r\nPlease enter IDF-PATH with \"cd "
             "$IDF_PATH\" and apply the IDF patch with \"git apply "
             "$ADF_PATH/idf_patches/idf_v3.3_freertos.patch\" first\r\n",
             __func__);
    return pdFALSE;
}
static int _thread_create(media_lib_thread_handle_t *handle, const char *name,
                          void(*body)(void *arg), void *arg, uint32_t stack_size,
                          int prio, int core)
{
    StackType_t *task_stack = NULL;
    do {
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
        BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(body, name, stack_size, arg, prio, (TaskHandle_t *)handle,
                                                         core, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Error creating RestrictedPinnedToCore %s", name);
            break;
        }
#else
        task_stack = (StackType_t *) calloc_in_heap(1, stack_size);
        TaskParameters_t xRegParameters = {.pvTaskCode = body,
                                           .pcName = name,
                                           .usStackDepth = stack_size,
                                           .pvParameters = arg,
                                           .uxPriority =
                                               prio | portPRIVILEGE_BIT,
                                           .puxStackBuffer = task_stack,
                                           .xRegions = {{
                                               .pvBaseAddress = 0x00,
                                               .ulLengthInBytes = 0x00,
                                               .ulParameters = 0x00,
                                           }}};
        if (xTaskCreateRestrictedPinnedToCore(
                &xRegParameters, (TaskHandle_t *)handle, core) != pdPASS) {
            ESP_LOGE(TAG, "Error creating RestrictedPinnedToCore %s", name);
            break;
        }
#endif
        return ESP_OK;
    } while (0);
    if (task_stack) {
        _free_in_heap(task_stack);
    }
    return ESP_FAIL;
}
#else
static int _thread_create(media_lib_thread_handle_t *handle, const char *name,
                          void(*body)(void *arg), void *arg, uint32_t stack_size,
                          int prio, int core)
{
    if (xTaskCreatePinnedToCore(body, name, stack_size, arg, prio,
                                (TaskHandle_t *)handle, core) != pdPASS) {
        ESP_LOGE(TAG, "Fail to create thread %s\n", name);
        return ESP_FAIL;
    }
    return ESP_OK;
}
#endif

static void _thread_destroy(media_lib_thread_handle_t handle)
{
#if defined(CONFIG_SPIRAM_BOOT_INIT) &&              \
    (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)  &&  \
    (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
    vTaskDeleteWithCaps((TaskHandle_t)handle);
#else
    // allow NULL to destroy self
    vTaskDelete((TaskHandle_t)handle);
#endif
}

static bool _thread_set_priority(media_lib_thread_handle_t handle, int prio)
{
    vTaskPrioritySet((TaskHandle_t)handle, prio);
    return true;
}

static void _thread_sleep(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}

static int _sema_create(media_lib_sema_handle_t *sema)
{
    if (sema) {
        *sema = (media_lib_sema_handle_t)xSemaphoreCreateCounting(1, 0);
        if (*sema != NULL) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static int _mutex_create(media_lib_mutex_handle_t *mutex)
{
    if (mutex) {
        *mutex = (media_lib_mutex_handle_t)xSemaphoreCreateRecursiveMutex();
        if (*mutex != NULL) {
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

static int _os_lock_timeout(void *lock, uint32_t timeout)
{
    RETURN_ON_NULL_HANDLE(lock);
    if (timeout != portMAX_DELAY) {
        timeout /= portTICK_PERIOD_MS;
    }
    return xSemaphoreTake((QueueHandle_t)lock, timeout) ? ESP_OK : ESP_FAIL;
}

static int _mutex_lock_timeout(media_lib_mutex_handle_t mutex, uint32_t timeout)
{
    RETURN_ON_NULL_HANDLE(mutex);
    if (timeout != portMAX_DELAY) {
        timeout /= portTICK_PERIOD_MS;
    }
    return xSemaphoreTakeRecursive(mutex, timeout);
}

static int _sema_lock_timeout(media_lib_sema_handle_t sema, uint32_t timeout)
{
    return _os_lock_timeout(sema, timeout);
}

static int _os_unlock(void *lock)
{
    RETURN_ON_NULL_HANDLE(lock);
    xSemaphoreGive((QueueHandle_t)lock);
    return ESP_OK;
}

static int _mutex_unlock(media_lib_mutex_handle_t mutex)
{
    RETURN_ON_NULL_HANDLE(mutex);
    return xSemaphoreGiveRecursive(mutex);
}

static int _sema_unlock(media_lib_sema_handle_t sema)
{
    return _os_unlock(sema);
}

static int _os_lock_destroy(void *lock)
{
    RETURN_ON_NULL_HANDLE(lock);
    vSemaphoreDelete((QueueHandle_t)lock);
    return ESP_OK;
}

static int _mutex_destroy(media_lib_mutex_handle_t mutex)
{
    return _os_lock_destroy(mutex);
}

static int _sema_destroy(media_lib_sema_handle_t sema)
{
    return _os_lock_destroy(sema);
}

static int _enter_critical(void)
{
    return ESP_OK;
}

static int _leave_critical(void)
{ 
    return ESP_OK;
}

static int _event_group_create(media_lib_event_grp_handle_t *group)
{
    RETURN_ON_NULL_HANDLE(group);
    *group = (media_lib_event_grp_handle_t)xEventGroupCreate();
    return ESP_OK;
}

static uint32_t _event_group_set_bits(media_lib_event_grp_handle_t group, uint32_t bits)
{
    RETURN_ON_NULL_HANDLE(group);
    return (uint32_t)xEventGroupSetBits((EventGroupHandle_t)group, bits);
}

static uint32_t _event_group_clr_bits(media_lib_event_grp_handle_t group, uint32_t bits)
{
    RETURN_ON_NULL_HANDLE(group);
    return (uint32_t)xEventGroupClearBits((EventGroupHandle_t)group, bits);
}

static uint32_t _event_group_wait_bits(media_lib_event_grp_handle_t group,
                                       uint32_t bits, uint32_t timeout)
{
    RETURN_ON_NULL_HANDLE(group);
    if (timeout != portMAX_DELAY) {
        timeout /= portTICK_PERIOD_MS;
    }
    return (uint32_t)xEventGroupWaitBits((EventGroupHandle_t)group, bits, false,
                                         true, timeout);
}

static int _event_group_destroy(media_lib_event_grp_handle_t group)
{
    RETURN_ON_NULL_HANDLE(group);
    vEventGroupDelete((EventGroupHandle_t)group);
    return ESP_OK;
}

#ifdef __XTENSA__

static int _get_stack_frame(void** addr, int n)
{
    int filled = 0;;
#if CONFIG_FREERTOS_ENABLE_TASK_SNAPSHOT
    esp_backtrace_frame_t frame = {};
    TaskSnapshot_t snap_shot;
    TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
    vTaskGetSnapshot(cur_task, &snap_shot);
    snap_shot.pxTopOfStack = pxTaskGetStackStart(cur_task);;
    esp_backtrace_get_start(&(frame.pc), &(frame.sp), &(frame.next_pc));

    for (int i = 0; i < n; i++) {
        esp_backtrace_get_next_frame(&frame);
        if (!((uint32_t)frame.sp >= (uint32_t)snap_shot.pxTopOfStack &&
            ((uint32_t)frame.sp <= (uint32_t)snap_shot.pxEndOfStack))) {
            break;
        }
        addr[filled] = (void*)esp_cpu_process_stack_pc(frame.pc);
        if (!esp_ptr_executable((void*)addr[filled])) {
            break;
        }
        filled++;
    }
#endif
    return filled;
}

#else

/**
 * @brief   Get reserved stack size of a function from `addi` instruction before `ret`
 *          Limitation: not support dynamic array on stack
 *              Instruction: addi sp, sp, imm
 *              Instruction size is 16 or 32 based on imm size
 *                  sp(4:0): 2
 *              When size is 16:
 *                000|imm(5)|sp(4:0)|imm(4:0)|01
 *                011|imm(9)|00010|imm(4|6|8:7|5)|01
 *              When size is 32:
 *                 imm(11:0)|sp(4:0)|000|sp(4:0)|0010011
 */
static int get_addi_size(uint32_t addi)
{
    if ((addi & 0x10113) == 0x10113) {
        return addi >> 20;
    }
    addi >>= 16;
    if ((addi & 0x6103) == 0x101) {
        return (addi & 0xfc) >> 2;
    }
    if ((addi & 0x6103) == 0x6101) {
        uint32_t s = 0;
        if (addi & 0x40) {
            s += 16;
        }
        if (addi & 0x20) {
            s += 64;
        }
        if (addi & 0x10) {
            s += 256;
        }
        if (addi & 0x08) {
            s += 128;
        }
        if (addi & 0x04) {
            s += 32;
        }
        return s;
    }
    return 0;
}

static int _get_stack_frame(void** addr, int n)
{
    int fill = 0;
#if CONFIG_FREERTOS_ENABLE_TASK_SNAPSHOT
    uint32_t pc, sp;
    TaskSnapshot_t snap_shot;
    TaskHandle_t cur_task = xTaskGetCurrentTaskHandle();
    vTaskGetSnapshot(cur_task, &snap_shot);
    snap_shot.pxTopOfStack = pxTaskGetStackStart(cur_task);
    asm volatile ("addi %0, sp, 0\n"
                  "auipc %1, 0\n"
                  "addi %1, %1, 0\n"
                  : "=r" (sp), "=r" (pc));
    uint16_t* pc_addr = (uint16_t*)pc;
    uint8_t* sp_addr = (uint8_t*)sp;
    int depth = 0;
    for (int i = 0; i < MAX_SEARCH_CODE_LEN; i++) {
        if (pc_addr[i] != RISC_V_RET_CODE) {
            continue;
        }
        uint32_t v = (pc_addr[i-1] << 16) + pc_addr[i-2];
        int s = get_addi_size(v);
        if (s == 0) {
            break;
        }
        sp_addr += s;
        if (sp_addr >= snap_shot.pxEndOfStack) {
            break;
        }
        uint32_t lr = *((int*)sp_addr - 1) - 4;
        if (esp_ptr_executable((void*)lr)) {
            depth++;
            if (depth >= 2) {
                addr[fill++] = (void*) lr;
                if (fill >= n) {
                    break;
                }
            }
            pc_addr = (uint16_t*) lr;
            i = 0;
            continue;
        }
        break;
    }
#endif
    return fill;
}
#endif

esp_err_t media_lib_add_default_os_adapter(void)
{
    media_lib_os_t os_lib = {
#if CONFIG_SPIRAM_BOOT_INIT
        .malloc = _malloc_in_heap,
        .free = _free_in_heap,
        .malloc_align = _malloc_align_in_heap,
        .free_align = _free_in_heap,
        .calloc = _calloc_in_heap,
        .realloc = _realloc_in_heap,
        .strdup = _strdup_in_heap,
#else
        .malloc = malloc,
        .free = free,
        .calloc = calloc,
        .realloc = realloc,
        .malloc_align = _malloc_align,
        .free_align = _free_align,
        .strdup = strdup,
#endif
        .get_stack_frame = _get_stack_frame,

        .thread_create = _thread_create,
        .thread_destroy = _thread_destroy,
        .thread_set_prio = _thread_set_priority,
        .thread_sleep = _thread_sleep,

        .sema_create = _sema_create,
        .sema_lock   = _sema_lock_timeout,
        .sema_unlock = _sema_unlock,
        .sema_destroy = _sema_destroy,

        .mutex_create = _mutex_create,
        .mutex_lock =   _mutex_lock_timeout,
        .mutex_unlock = _mutex_unlock,
        .mutex_destroy = _mutex_destroy,

        .enter_critical = _enter_critical,
        .leave_critical = _leave_critical,

        .group_create = _event_group_create,
        .group_set_bits = _event_group_set_bits,
        .group_clr_bits = _event_group_clr_bits,
        .group_wait_bits = _event_group_wait_bits,
        .group_destroy = _event_group_destroy,
    };
    return media_lib_os_register(&os_lib);
}
