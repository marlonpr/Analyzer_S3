#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

namespace {

constexpr uart_port_t kHostUart = UART_NUM_0;
constexpr std::size_t kChannelCount = 5;
constexpr std::size_t kCommandBufferLength = 128;
constexpr std::size_t kOutputBufferLength = 192;
constexpr std::size_t kEdgeQueueLength = 64;

constexpr std::array<gpio_num_t, kChannelCount> kInputPins = {
    static_cast<gpio_num_t>(CONFIG_START_ANALYZER_GPIO_ESP01),
    static_cast<gpio_num_t>(CONFIG_START_ANALYZER_GPIO_ESP02),
    static_cast<gpio_num_t>(CONFIG_START_ANALYZER_GPIO_ESP03),
    static_cast<gpio_num_t>(CONFIG_START_ANALYZER_GPIO_ESP04),
    static_cast<gpio_num_t>(CONFIG_START_ANALYZER_GPIO_ESP05),
};

constexpr std::array<const char*, kChannelCount> kDeviceIds = {
    "ESP01", "ESP02", "ESP03", "ESP04", "ESP05"
};

struct EdgeEvent {
    uint64_t run_id{};
    uint32_t trial_id{};
    uint8_t channel{};
    int64_t timestamp_us{};
};

struct CaptureState {
    bool armed{};
    uint64_t run_id{};
    uint32_t trial_id{};
    uint8_t edge_mask{};
};

QueueHandle_t edge_queue = nullptr;
SemaphoreHandle_t output_mutex = nullptr;
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
CaptureState capture_state{};

void WriteProtocolLine(const char* format, ...) {
    char buffer[kOutputBufferLength]{};
    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer) - 3, format, args);
    va_end(args);
    if (length < 0) return;

    std::size_t used = static_cast<std::size_t>(length);
    if (used > sizeof(buffer) - 3) used = sizeof(buffer) - 3;
    buffer[used++] = '\r';
    buffer[used++] = '\n';

    if (output_mutex != nullptr) {
        xSemaphoreTake(output_mutex, portMAX_DELAY);
    }
    uart_write_bytes(kHostUart, buffer, used);
    if (output_mutex != nullptr) {
        xSemaphoreGive(output_mutex);
    }
}

uint8_t PopCount5(uint8_t value) {
    value &= 0x1F;
    uint8_t count = 0;
    while (value != 0) {
        count += static_cast<uint8_t>(value & 1U);
        value >>= 1U;
    }
    return count;
}

void IRAM_ATTR EdgeIsr(void* arg) {
    // Capture time immediately on ISR entry. Five ISR callbacks are serviced
    // sequentially, so same-pulse channel skew must be calibrated once before
    // the experiment. For the 100+ us effects under test this ISR latency is
    // expected to be negligible, but the calibration makes that measurable.
    const int64_t timestamp_us = esp_timer_get_time();
    const uint32_t channel = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    if (channel >= kChannelCount) return;

    EdgeEvent event{};
    bool should_queue = false;

    portENTER_CRITICAL_ISR(&state_mux);
    const uint8_t bit = static_cast<uint8_t>(1U << channel);
    if (capture_state.armed && (capture_state.edge_mask & bit) == 0) {
        capture_state.edge_mask = static_cast<uint8_t>(capture_state.edge_mask | bit);
        event.run_id = capture_state.run_id;
        event.trial_id = capture_state.trial_id;
        event.channel = static_cast<uint8_t>(channel);
        event.timestamp_us = timestamp_us;
        should_queue = true;
    }
    portEXIT_CRITICAL_ISR(&state_mux);

    if (!should_queue || edge_queue == nullptr) return;

    BaseType_t higher_priority_woken = pdFALSE;
    xQueueSendFromISR(edge_queue, &event, &higher_priority_woken);
    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void EdgeOutputTask(void*) {
    EdgeEvent event{};
    while (true) {
        if (xQueueReceive(edge_queue, &event, portMAX_DELAY) == pdTRUE) {
            WriteProtocolLine(
                "ANZ|EDGE|%" PRIu64 "|%" PRIu32 "|%u|%s|%" PRId64,
                event.run_id,
                event.trial_id,
                static_cast<unsigned>(event.channel + 1U),
                kDeviceIds[event.channel],
                event.timestamp_us);
        }
    }
}

bool ParseUnsigned64(const char* text, uint64_t& value) {
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') return false;
    value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseUnsigned32(const char* text, uint32_t& value) {
    uint64_t parsed = 0;
    if (!ParseUnsigned64(text, parsed) || parsed == 0 || parsed > UINT32_MAX) return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

void BeginTrial(uint64_t run_id, uint32_t trial_id) {
    // Queue entries carry their own run/trial IDs, so never discard old queued
    // edges here. If the output task is briefly behind, it can still serialize
    // those old records correctly while the new trial is already armed.
    portENTER_CRITICAL(&state_mux);
    capture_state.armed = false;
    capture_state.run_id = run_id;
    capture_state.trial_id = trial_id;
    capture_state.edge_mask = 0;
    capture_state.armed = true;
    portEXIT_CRITICAL(&state_mux);

    WriteProtocolLine("ANZ|ACK|BEGIN|%" PRIu64 "|%" PRIu32, run_id, trial_id);
}

void EndTrial(uint64_t run_id, uint32_t trial_id) {
    uint8_t mask = 0;
    bool matched = false;

    portENTER_CRITICAL(&state_mux);
    if (capture_state.run_id == run_id && capture_state.trial_id == trial_id) {
        mask = capture_state.edge_mask;
        capture_state.armed = false;
        matched = true;
    }
    portEXIT_CRITICAL(&state_mux);

    if (!matched) {
        WriteProtocolLine("ANZ|ERR|END_MISMATCH|%" PRIu64 "|%" PRIu32, run_id, trial_id);
        return;
    }

    // Give EdgeOutputTask a short opportunity to drain events already queued by
    // the ISRs so SUMMARY normally follows all EDGE records for this trial.
    vTaskDelay(pdMS_TO_TICKS(2));
    WriteProtocolLine(
        "ANZ|SUMMARY|%" PRIu64 "|%" PRIu32 "|0x%02X|%u",
        run_id,
        trial_id,
        static_cast<unsigned>(mask),
        static_cast<unsigned>(PopCount5(mask)));
}

void ReportStatus() {
    CaptureState snapshot{};
    portENTER_CRITICAL(&state_mux);
    snapshot = capture_state;
    portEXIT_CRITICAL(&state_mux);

    WriteProtocolLine(
        "ANZ|STATUS|armed=%u|run=%" PRIu64 "|trial=%" PRIu32 "|mask=0x%02X|count=%u",
        snapshot.armed ? 1U : 0U,
        snapshot.run_id,
        snapshot.trial_id,
        static_cast<unsigned>(snapshot.edge_mask),
        static_cast<unsigned>(PopCount5(snapshot.edge_mask)));
}

void ProcessCommand(char* line) {
    if (std::strcmp(line, "PING") == 0) {
        WriteProtocolLine("ANZ|PONG");
        return;
    }
    if (std::strcmp(line, "STATUS") == 0) {
        ReportStatus();
        return;
    }
    if (std::strcmp(line, "CLEAR") == 0) {
        portENTER_CRITICAL(&state_mux);
        capture_state = {};
        portEXIT_CRITICAL(&state_mux);
        EdgeEvent stale{};
        while (xQueueReceive(edge_queue, &stale, 0) == pdTRUE) {
        }
        WriteProtocolLine("ANZ|ACK|CLEAR");
        return;
    }

    char* save = nullptr;
    char* command = strtok_r(line, "|", &save);
    char* run_text = strtok_r(nullptr, "|", &save);
    char* trial_text = strtok_r(nullptr, "|", &save);
    char* extra = strtok_r(nullptr, "|", &save);

    if (command == nullptr || run_text == nullptr || trial_text == nullptr || extra != nullptr) {
        WriteProtocolLine("ANZ|ERR|BAD_COMMAND");
        return;
    }

    uint64_t run_id = 0;
    uint32_t trial_id = 0;
    if (!ParseUnsigned64(run_text, run_id) || run_id == 0 ||
        !ParseUnsigned32(trial_text, trial_id)) {
        WriteProtocolLine("ANZ|ERR|BAD_ID");
        return;
    }

    if (std::strcmp(command, "BEGIN") == 0) {
        BeginTrial(run_id, trial_id);
    } else if (std::strcmp(command, "END") == 0) {
        EndTrial(run_id, trial_id);
    } else {
        WriteProtocolLine("ANZ|ERR|UNKNOWN_COMMAND");
    }
}

void SerialCommandTask(void*) {
    char line[kCommandBufferLength]{};
    std::size_t length = 0;

    while (true) {
        uint8_t byte = 0;
        const int received = uart_read_bytes(kHostUart, &byte, 1, pdMS_TO_TICKS(100));
        if (received <= 0) continue;

        if (byte == '\r') continue;
        if (byte == '\n') {
            if (length == 0) continue;
            line[length] = '\0';
            ProcessCommand(line);
            length = 0;
            continue;
        }

        if (byte < 0x20 || byte > 0x7E) continue;
        if (length + 1 < sizeof(line)) {
            line[length++] = static_cast<char>(byte);
        } else {
            length = 0;
            WriteProtocolLine("ANZ|ERR|COMMAND_TOO_LONG");
        }
    }
}

void InitialiseSerial() {
    uart_config_t config{};
    config.baud_rate = CONFIG_START_ANALYZER_UART_BAUD;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk = UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(uart_driver_install(kHostUart, 2048, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(kHostUart, &config));
    ESP_ERROR_CHECK(uart_set_pin(
        kHostUart,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE));
}

void InitialiseInputs() {
    uint64_t pin_mask = 0;
    for (gpio_num_t pin : kInputPins) {
        pin_mask |= 1ULL << static_cast<unsigned>(pin);
    }

    gpio_config_t config{};
    config.pin_bit_mask = pin_mask;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_DISABLE;
    config.pull_down_en = GPIO_PULLDOWN_ENABLE;
    config.intr_type = GPIO_INTR_POSEDGE;
    ESP_ERROR_CHECK(gpio_config(&config));

    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_IRAM));
    for (std::size_t channel = 0; channel < kChannelCount; ++channel) {
        ESP_ERROR_CHECK(gpio_isr_handler_add(
            kInputPins[channel],
            &EdgeIsr,
            reinterpret_cast<void*>(static_cast<uintptr_t>(channel))));
    }
}

}  // namespace

extern "C" void app_main() {
    esp_log_level_set("*", ESP_LOG_WARN);

    edge_queue = xQueueCreate(kEdgeQueueLength, sizeof(EdgeEvent));
    output_mutex = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(edge_queue == nullptr || output_mutex == nullptr ? ESP_ERR_NO_MEM : ESP_OK);

    InitialiseSerial();
    InitialiseInputs();

    if (xTaskCreate(&EdgeOutputTask, "edge_output", 4096, nullptr, 10, nullptr) != pdPASS ||
        xTaskCreate(&SerialCommandTask, "serial_cmd", 4096, nullptr, 8, nullptr) != pdPASS) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    WriteProtocolLine(
        "ANZ|READY|baud=%d|ESP01=%d|ESP02=%d|ESP03=%d|ESP04=%d|ESP05=%d",
        CONFIG_START_ANALYZER_UART_BAUD,
        static_cast<int>(kInputPins[0]),
        static_cast<int>(kInputPins[1]),
        static_cast<int>(kInputPins[2]),
        static_cast<int>(kInputPins[3]),
        static_cast<int>(kInputPins[4]));
}
