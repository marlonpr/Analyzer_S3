#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

extern int64_t host_now_us;
extern uint64_t host_gptimer_count;
extern uint64_t host_next_alarm;
extern char host_output[2000000];
extern size_t host_output_used;
static inline void host_append(const char* data, size_t n) {
    if (host_output_used+n+1>=2000000) abort();
    memcpy(host_output+host_output_used,data,n);
    host_output_used+=n;
    host_output[host_output_used]=0;
}
static inline void host_log(const char* tag, const char* fmt, ...) {
    (void)tag;
    char buf[2048]; va_list args;va_start(args,fmt);
    int n=vsnprintf(buf,sizeof(buf),fmt,args);va_end(args);
    if(n>0) host_append(buf,(size_t)n<sizeof(buf)?(size_t)n:sizeof(buf)-1);
    host_append("\n",1);
}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGD(...) ((void)0)
#define IRAM_ATTR
#define DRAM_ATTR
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERROR_CHECK(x) do { if((x)!=ESP_OK) abort(); } while(0)
static inline const char* esp_err_to_name(esp_err_t e) {return e==ESP_OK?"ESP_OK":"HOST_ERROR";}
static inline int64_t esp_timer_get_time(void) {return host_now_us;}
static inline void esp_rom_delay_us(uint32_t d) {host_now_us+=d;host_gptimer_count+=d;}
typedef int BaseType_t;
typedef unsigned UBaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef void* SemaphoreHandle_t;
typedef int portMUX_TYPE;
typedef uint32_t configRUN_TIME_COUNTER_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define configMAX_TASK_NAME_LEN 16
#define configMAX_PRIORITIES 25
#define tskNO_AFFINITY -1
#define portTICK_PERIOD_MS 10
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define taskENTER_CRITICAL(x) ((void)(x))
#define taskEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define portYIELD_FROM_ISR(...) ((void)0)
static inline BaseType_t xPortGetCoreID(void) {return 0;}
static inline BaseType_t xTaskGetCoreID(TaskHandle_t t) {(void)t;return 0;}
extern TaskHandle_t host_current_task;
static inline TaskHandle_t xTaskGetCurrentTaskHandleForCore(int c) {(void)c;return host_current_task;}
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) {return host_current_task;}
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) {return (void*)1;}
static inline SemaphoreHandle_t xSemaphoreCreateBinary(void) {return (void*)1;}
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t h,TickType_t t) {(void)h;(void)t;return pdTRUE;}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t h) {(void)h;return pdTRUE;}
static inline void vSemaphoreDelete(SemaphoreHandle_t h) {(void)h;}
static inline QueueHandle_t xQueueCreate(unsigned n,unsigned sz) {(void)n;(void)sz;return (void*)1;}
static inline BaseType_t xQueueReceive(QueueHandle_t q,void* o,TickType_t t) {(void)q;(void)o;(void)t;return pdFALSE;}
static inline BaseType_t xQueueSendFromISR(QueueHandle_t q,const void* x,BaseType_t* w) {(void)q;(void)x;*w=pdFALSE;return pdTRUE;}
static inline unsigned uxQueueMessagesWaiting(QueueHandle_t q) {(void)q;return 0;}
static inline BaseType_t xQueueReset(QueueHandle_t q) {(void)q;return pdTRUE;}
static inline BaseType_t xQueueOverwrite(QueueHandle_t q,const void* v) {(void)q;(void)v;return pdTRUE;}
static inline void vTaskDelay(TickType_t t) {host_now_us+=(int64_t)t*1000;}
static inline void taskYIELD(void) {}
static inline void vTaskDelete(TaskHandle_t t) {(void)t;}
static inline void xTaskNotifyGive(TaskHandle_t t) {(void)t;}
static inline unsigned ulTaskNotifyTake(BaseType_t clear,TickType_t t) {(void)clear;(void)t;return 0;}
static inline BaseType_t xTaskNotifyGiveFromISR(TaskHandle_t t,BaseType_t* w) {(void)t;*w=pdFALSE;return pdTRUE;}
static inline BaseType_t xTaskCreate(void(*f)(void*),const char* n,unsigned stack,void* a,unsigned p,TaskHandle_t* t) {(void)f;(void)n;(void)stack;(void)a;(void)p;if(t)*t=(void*)1;return pdPASS;}
static inline BaseType_t xTaskCreatePinnedToCore(void(*f)(void*),const char* n,unsigned stack,void* a,unsigned p,TaskHandle_t* t,int core) {(void)core;return xTaskCreate(f,n,stack,a,p,t);}
typedef struct {TaskHandle_t xHandle;const char* pcTaskName;unsigned uxCurrentPriority;uint32_t ulRunTimeCounter;} TaskStatus_t;
static inline unsigned uxTaskGetSystemState(TaskStatus_t* out,unsigned capacity,configRUN_TIME_COUNTER_TYPE* total) {
    (void)total;if(capacity<3)return 0;
    out[0]=(TaskStatus_t){(void*)1,"rtc_discipline",2,0};
    out[1]=(TaskStatus_t){(void*)2,"wifi",23,0};
    out[2]=(TaskStatus_t){(void*)3,"udp_command",5,0};return 3;
}
typedef void* gptimer_handle_t;
typedef struct {int clk_src;int direction;unsigned resolution_hz;int intr_priority;} gptimer_config_t;
typedef struct {uint64_t alarm_count;uint64_t reload_count;struct {unsigned auto_reload_on_alarm:1;} flags;} gptimer_alarm_config_t;
typedef struct {uint64_t count_value;uint64_t alarm_value;} gptimer_alarm_event_data_t;
typedef struct {bool(*on_alarm)(gptimer_handle_t,const gptimer_alarm_event_data_t*,void*);} gptimer_event_callbacks_t;
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
static inline esp_err_t gptimer_new_timer(const gptimer_config_t* c,gptimer_handle_t* h) {(void)c;*h=(void*)1;return ESP_OK;}
static inline esp_err_t gptimer_register_event_callbacks(gptimer_handle_t h,const gptimer_event_callbacks_t* c,void* a) {(void)h;(void)c;(void)a;return ESP_OK;}
static inline esp_err_t gptimer_enable(gptimer_handle_t h) {(void)h;return ESP_OK;}
static inline esp_err_t gptimer_start(gptimer_handle_t h) {(void)h;return ESP_OK;}
static inline esp_err_t gptimer_set_raw_count(gptimer_handle_t h,uint64_t n) {(void)h;host_gptimer_count=n;return ESP_OK;}
static inline esp_err_t gptimer_get_raw_count(gptimer_handle_t h,uint64_t* n) {(void)h;*n=host_gptimer_count;return ESP_OK;}
static inline esp_err_t gptimer_set_alarm_action(gptimer_handle_t h,const gptimer_alarm_config_t* c) {(void)h;host_next_alarm=c->alarm_count;return ESP_OK;}
typedef void* esp_timer_handle_t;
typedef struct {void(*callback)(void*);void* arg;int dispatch_method;const char* name;bool skip_unhandled_events;} esp_timer_create_args_t;
#define ESP_TIMER_ISR 1
static inline esp_err_t esp_timer_create(const esp_timer_create_args_t* a,esp_timer_handle_t* t) {(void)a;*t=(void*)1;return ESP_OK;}
static inline esp_err_t esp_timer_start_once(esp_timer_handle_t t,uint64_t d) {(void)t;(void)d;return ESP_OK;}
static inline esp_err_t esp_timer_stop(esp_timer_handle_t t) {(void)t;return ESP_OK;}
static inline bool esp_timer_is_active(esp_timer_handle_t t) {(void)t;return false;}
typedef int gpio_num_t;
typedef struct {uint64_t pin_bit_mask;int mode;int pull_up_en;int pull_down_en;int intr_type;} gpio_config_t;
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_POSEDGE 1
#define GPIO_INTR_NEGEDGE 2
#define GPIO_INTR_ANYEDGE 3
#define GPIO_IS_VALID_GPIO(g) ((g)>=0&&(g)<49)
#define ESP_INTR_FLAG_IRAM 1
#define ESP_INTR_FLAG_LEVEL1 2
static inline esp_err_t gpio_config(const gpio_config_t* c) {(void)c;return ESP_OK;}
static inline int gpio_get_level(gpio_num_t g) {(void)g;return 0;}
static inline esp_err_t gpio_set_level(gpio_num_t g,uint32_t v) {(void)g;(void)v;return ESP_OK;}
static inline esp_err_t gpio_install_isr_service(int flags) {(void)flags;return ESP_OK;}
static inline esp_err_t gpio_isr_handler_add(gpio_num_t g,void(*f)(void*),void* a) {(void)g;(void)f;(void)a;return ESP_OK;}
static inline esp_err_t gpio_isr_handler_remove(gpio_num_t g) {(void)g;return ESP_OK;}
static inline esp_err_t gpio_set_intr_type(gpio_num_t g,int t) {(void)g;(void)t;return ESP_OK;}
static inline esp_err_t gpio_intr_enable(gpio_num_t g) {(void)g;return ESP_OK;}
typedef void* i2c_master_bus_handle_t;
typedef void* i2c_master_dev_handle_t;
typedef int i2c_port_t;
typedef int uart_port_t;
#define UART_NUM_0 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
typedef struct {int baud_rate;int data_bits;int parity;int stop_bits;int flow_ctrl;int source_clk;int rx_flow_ctrl_thresh;} uart_config_t;
static inline int uart_write_bytes(uart_port_t u,const void* data,size_t n) {(void)u;host_append((const char*)data,n);return (int)n;}
static inline int uart_read_bytes(uart_port_t u,void* data,uint32_t n,TickType_t t) {(void)u;(void)data;(void)n;(void)t;return 0;}
static inline esp_err_t uart_driver_install(uart_port_t u,int rx,int tx,int q,void* h,int flags) {(void)u;(void)rx;(void)tx;(void)q;(void)h;(void)flags;return ESP_OK;}
static inline esp_err_t uart_param_config(uart_port_t u,const uart_config_t* c) {(void)u;(void)c;return ESP_OK;}
static inline esp_err_t uart_set_pin(uart_port_t u,int tx,int rx,int rts,int cts) {(void)u;(void)tx;(void)rx;(void)rts;(void)cts;return ESP_OK;}

static inline void gpio_uninstall_isr_service(void) {}
#define ESP_LOG_WARN 2
static inline void esp_log_level_set(const char* tag,int level) {(void)tag;(void)level;}
