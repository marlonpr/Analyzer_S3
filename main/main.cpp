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
constexpr std::size_t kChannelCount = 3;
constexpr uint8_t kAllChannelsMask = 0x07;

constexpr std::size_t kCommandBufferLength = 128;
constexpr std::size_t kOutputBufferLength = 512;
constexpr std::size_t kEdgeQueueLength = 256;
constexpr std::size_t kGroupCount = 16;
constexpr std::size_t kMaxTrains = 16;

constexpr std::array<gpio_num_t, kChannelCount> kInputPins = {
    static_cast<gpio_num_t>(CONFIG_ANALYZER_V10_GPIO_ESP01_COMMIT),
    static_cast<gpio_num_t>(CONFIG_ANALYZER_V10_GPIO_ESP02_COMMIT),
    static_cast<gpio_num_t>(CONFIG_ANALYZER_V10_GPIO_ESP03_COMMIT),
};

constexpr std::array<const char*, kChannelCount> kChannelNames = {
    "ESP01_COMMIT",
    "ESP02_COMMIT",
    "ESP03_COMMIT",
};

struct ChannelContext {
    uint8_t channel{};
    gpio_num_t pin{};
};

struct EdgeEvent {
    uint64_t run_id{};
    uint32_t trial_id{};
    uint32_t raw_index{};
    uint8_t channel{};
    uint8_t level{};
    int64_t timestamp_us{};
};

struct CaptureState {
    bool armed{};
    uint64_t run_id{};
    uint32_t trial_id{};
    uint8_t seen_mask{};
    std::array<uint32_t, kChannelCount> raw_counts{};
    uint32_t dropped_events{};
};

struct ChannelTrainState {
    bool have_previous{};
    bool previous_emitted{};
    EdgeEvent previous{};
    uint32_t qualified_count{};
};

struct MatchGroup {
    bool valid{};
    uint64_t run_id{};
    uint32_t trial_id{};
    uint8_t level{};
    uint8_t mask{};
    int64_t anchor_us{};
    int64_t min_us{};
    int64_t max_us{};
    std::array<int64_t, kChannelCount> timestamps{};
};

struct TrainSummary {
    bool used{};
    uint32_t train_id{};
    uint32_t boundaries{};
    uint32_t gated_boundaries{};
    uint32_t start_trips{};
    int64_t start_worst_range_us{};
    int64_t overall_worst_range_us{};
    int64_t first_group_us{};
    int64_t last_group_us{};
};

struct AnalysisState {
    uint64_t run_id{};
    uint32_t trial_id{};
    std::array<ChannelTrainState, kChannelCount> channels{};
    std::array<MatchGroup, kGroupCount> groups{};
    std::array<TrainSummary, kMaxTrains> trains{};
    uint32_t train_count{};
    int64_t last_completed_group_us{};
    uint32_t incomplete_groups{};
    uint32_t total_start_trips{};
    int64_t worst_start_range_us{};
};

QueueHandle_t edge_queue = nullptr;
SemaphoreHandle_t output_mutex = nullptr;
SemaphoreHandle_t analysis_mutex = nullptr;

portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;

CaptureState capture_state{};
AnalysisState analysis_state{};
std::array<ChannelContext, kChannelCount> channel_contexts{};

void WriteProtocolLine(const char* format, ...) {
    char buffer[kOutputBufferLength]{};

    va_list args;
    va_start(args, format);
    const int length = std::vsnprintf(buffer, sizeof(buffer) - 3, format, args);
    va_end(args);

    if (length < 0) {
        return;
    }

    std::size_t used = static_cast<std::size_t>(length);
    if (used > sizeof(buffer) - 3) {
        used = sizeof(buffer) - 3;
    }

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

uint8_t PopCount(uint8_t value) {
    uint8_t count = 0;
    while (value != 0U) {
        count += static_cast<uint8_t>(value & 1U);
        value >>= 1U;
    }
    return count;
}

void ResetAnalysis(uint64_t run_id, uint32_t trial_id) {
    xSemaphoreTake(analysis_mutex, portMAX_DELAY);
    analysis_state = {};
    analysis_state.run_id = run_id;
    analysis_state.trial_id = trial_id;
    xSemaphoreGive(analysis_mutex);
}

void IRAM_ATTR EdgeIsr(void* arg) {
    const int64_t timestamp_us = esp_timer_get_time();

    const auto* context = static_cast<const ChannelContext*>(arg);
    if (context == nullptr || context->channel >= kChannelCount) {
        return;
    }

    const uint8_t channel = context->channel;
    const uint8_t level =
        static_cast<uint8_t>(gpio_get_level(context->pin) != 0);

    EdgeEvent event{};
    bool should_queue = false;

    portENTER_CRITICAL_ISR(&state_mux);

    if (capture_state.armed) {
        capture_state.seen_mask =
            static_cast<uint8_t>(
                capture_state.seen_mask | (1U << channel));

        event.run_id = capture_state.run_id;
        event.trial_id = capture_state.trial_id;
        event.raw_index = ++capture_state.raw_counts[channel];
        event.channel = channel;
        event.level = level;
        event.timestamp_us = timestamp_us;
        should_queue = true;
    }

    portEXIT_CRITICAL_ISR(&state_mux);

    if (!should_queue || edge_queue == nullptr) {
        return;
    }

    BaseType_t higher_priority_woken = pdFALSE;

    if (xQueueSendFromISR(
            edge_queue,
            &event,
            &higher_priority_woken) != pdTRUE) {
        portENTER_CRITICAL_ISR(&state_mux);

        if (capture_state.run_id == event.run_id &&
            capture_state.trial_id == event.trial_id) {
            ++capture_state.dropped_events;
        }

        portEXIT_CRITICAL_ISR(&state_mux);
        return;
    }

    if (higher_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

void ReportMissingGroup(const MatchGroup& group) {
    WriteProtocolLine(
        "ANZ|MISSING|%" PRIu64 "|%" PRIu32
        "|level=%u|mask=0x%02X|anchor_us=%" PRId64,
        group.run_id,
        group.trial_id,
        static_cast<unsigned>(group.level),
        static_cast<unsigned>(group.mask),
        group.anchor_us);
}

void FlushExpiredGroupsLocked(int64_t now_us, bool force_all) {
    for (auto& group : analysis_state.groups) {
        if (!group.valid) {
            continue;
        }

        const bool expired =
            force_all ||
            (now_us > group.max_us &&
             now_us - group.max_us >
                 static_cast<int64_t>(
                     CONFIG_ANALYZER_V10_ASSOCIATION_WINDOW_US));

        if (!expired) {
            continue;
        }

        if (group.mask != kAllChannelsMask) {
            ++analysis_state.incomplete_groups;
            const MatchGroup copy = group;
            group = {};

            // analysis_mutex is recursive only by convention here; release
            // around UART output so serial writes never block analysis state.
            xSemaphoreGive(analysis_mutex);
            ReportMissingGroup(copy);
            xSemaphoreTake(analysis_mutex, portMAX_DELAY);
        } else {
            group = {};
        }
    }
}

TrainSummary* CurrentOrNewTrainLocked(int64_t group_time_us) {
    const bool new_train =
        analysis_state.train_count == 0 ||
        analysis_state.last_completed_group_us == 0 ||
        group_time_us - analysis_state.last_completed_group_us >
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_NEW_TRAIN_GAP_US);

    if (new_train) {
        if (analysis_state.train_count >= kMaxTrains) {
            return nullptr;
        }

        TrainSummary& train =
            analysis_state.trains[analysis_state.train_count];

        train = {};
        train.used = true;
        train.train_id = analysis_state.train_count + 1U;
        train.first_group_us = group_time_us;
        train.last_group_us = group_time_us;

        ++analysis_state.train_count;
        return &train;
    }

    TrainSummary& train =
        analysis_state.trains[analysis_state.train_count - 1U];

    train.last_group_us = group_time_us;
    return &train;
}

void CompleteGroupLocked(MatchGroup& group) {
    const int64_t range_us = group.max_us - group.min_us;
    const int64_t group_time_us = group.min_us;

    TrainSummary* train =
        CurrentOrNewTrainLocked(group_time_us);

    if (train == nullptr) {
        ++analysis_state.incomplete_groups;
        group = {};
        return;
    }

    const uint32_t boundary_index = train->boundaries++;
    train->last_group_us = group_time_us;

    if (range_us > train->overall_worst_range_us) {
        train->overall_worst_range_us = range_us;
    }

    const bool gated =
        boundary_index <
        static_cast<uint32_t>(
            CONFIG_ANALYZER_V10_START_GATED_BOUNDARIES);

    bool trip = false;

    if (gated) {
        ++train->gated_boundaries;

        if (range_us > train->start_worst_range_us) {
            train->start_worst_range_us = range_us;
        }

        if (range_us > analysis_state.worst_start_range_us) {
            analysis_state.worst_start_range_us = range_us;
        }

        trip =
            range_us >
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_START_TRIPWIRE_US);

        if (trip) {
            ++train->start_trips;
            ++analysis_state.total_start_trips;
        }
    }

    analysis_state.last_completed_group_us = group_time_us;

    const MatchGroup copy = group;
    const uint32_t train_id = train->train_id;
    group = {};

    xSemaphoreGive(analysis_mutex);

    WriteProtocolLine(
        "ANZ|SKEW|%" PRIu64 "|%" PRIu32
        "|train=%" PRIu32
        "|boundary=%" PRIu32
        "|ESP01=%" PRId64
        "|ESP02=%" PRId64
        "|ESP03=%" PRId64
        "|range_us=%" PRId64
        "|gated=%u"
        "|limit_us=%d"
        "|result=%s",
        copy.run_id,
        copy.trial_id,
        train_id,
        boundary_index,
        copy.timestamps[0],
        copy.timestamps[1],
        copy.timestamps[2],
        range_us,
        gated ? 1U : 0U,
        CONFIG_ANALYZER_V10_START_TRIPWIRE_US,
        gated ? (trip ? "TRIP" : "PASS") : "INFO");

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);
}

void AddQualifiedEdge(const EdgeEvent& event) {
    xSemaphoreTake(analysis_mutex, portMAX_DELAY);

    if (analysis_state.run_id != event.run_id ||
        analysis_state.trial_id != event.trial_id) {
        xSemaphoreGive(analysis_mutex);
        return;
    }

    ++analysis_state.channels[event.channel].qualified_count;

    FlushExpiredGroupsLocked(event.timestamp_us, false);

    MatchGroup* selected = nullptr;
    int64_t best_distance = INT64_MAX;

    for (auto& group : analysis_state.groups) {
        if (!group.valid ||
            group.run_id != event.run_id ||
            group.trial_id != event.trial_id ||
            group.level != event.level ||
            (group.mask & (1U << event.channel)) != 0U) {
            continue;
        }

        const int64_t distance =
            llabs(event.timestamp_us - group.anchor_us);

        if (distance >
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_ASSOCIATION_WINDOW_US)) {
            continue;
        }

        const int64_t new_min =
            event.timestamp_us < group.min_us
                ? event.timestamp_us
                : group.min_us;

        const int64_t new_max =
            event.timestamp_us > group.max_us
                ? event.timestamp_us
                : group.max_us;

        if (new_max - new_min >
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_ASSOCIATION_WINDOW_US)) {
            continue;
        }

        if (distance < best_distance) {
            best_distance = distance;
            selected = &group;
        }
    }

    if (selected == nullptr) {
        for (auto& group : analysis_state.groups) {
            if (!group.valid) {
                selected = &group;
                *selected = {};
                selected->valid = true;
                selected->run_id = event.run_id;
                selected->trial_id = event.trial_id;
                selected->level = event.level;
                selected->anchor_us = event.timestamp_us;
                selected->min_us = event.timestamp_us;
                selected->max_us = event.timestamp_us;
                break;
            }
        }
    }

    if (selected == nullptr) {
        ++analysis_state.incomplete_groups;
        xSemaphoreGive(analysis_mutex);

        WriteProtocolLine(
            "ANZ|ERR|GROUP_CAPACITY|%" PRIu64 "|%" PRIu32
            "|%s|%" PRId64,
            event.run_id,
            event.trial_id,
            kChannelNames[event.channel],
            event.timestamp_us);
        return;
    }

    selected->mask =
        static_cast<uint8_t>(
            selected->mask | (1U << event.channel));

    selected->timestamps[event.channel] =
        event.timestamp_us;

    if (event.timestamp_us < selected->min_us) {
        selected->min_us = event.timestamp_us;
    }

    if (event.timestamp_us > selected->max_us) {
        selected->max_us = event.timestamp_us;
    }

    const uint32_t qualified_index =
        analysis_state.channels[event.channel].qualified_count;

    xSemaphoreGive(analysis_mutex);

    WriteProtocolLine(
        "ANZ|QUALIFIED|%" PRIu64 "|%" PRIu32
        "|%u|%s|q=%" PRIu32 "|%" PRId64 "|%u",
        event.run_id,
        event.trial_id,
        static_cast<unsigned>(event.channel + 1U),
        kChannelNames[event.channel],
        qualified_index,
        event.timestamp_us,
        static_cast<unsigned>(event.level));

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);

    if (selected->valid &&
        selected->mask == kAllChannelsMask) {
        CompleteGroupLocked(*selected);
    }

    xSemaphoreGive(analysis_mutex);
}

void ProcessTrainQualification(const EdgeEvent& event) {
    EdgeEvent emit_previous{};
    bool should_emit_previous = false;
    bool should_emit_current = false;

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);

    ChannelTrainState& state =
        analysis_state.channels[event.channel];

    if (!state.have_previous) {
        state.have_previous = true;
        state.previous = event;
        state.previous_emitted = false;
        xSemaphoreGive(analysis_mutex);
        return;
    }

    const int64_t delta_us =
        event.timestamp_us - state.previous.timestamp_us;

    const bool approximately_one_second =
        delta_us >=
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_TRAIN_MIN_US) &&
        delta_us <=
            static_cast<int64_t>(
                CONFIG_ANALYZER_V10_TRAIN_MAX_US);

    const bool alternating_level =
        event.level != state.previous.level;

    if (approximately_one_second && alternating_level) {
        if (!state.previous_emitted) {
            emit_previous = state.previous;
            should_emit_previous = true;
        }

        should_emit_current = true;
    }

    state.previous = event;
    state.previous_emitted = should_emit_current;

    xSemaphoreGive(analysis_mutex);

    if (should_emit_previous) {
        AddQualifiedEdge(emit_previous);
    }

    if (should_emit_current) {
        AddQualifiedEdge(event);
    }
}

void EdgeOutputTask(void*) {
    EdgeEvent event{};

    while (true) {
        if (xQueueReceive(
                edge_queue,
                &event,
                portMAX_DELAY) != pdTRUE) {
            continue;
        }

        WriteProtocolLine(
            "ANZ|EDGE|%" PRIu64 "|%" PRIu32
            "|%u|%s|%" PRId64 "|%u",
            event.run_id,
            event.trial_id,
            static_cast<unsigned>(event.channel + 1U),
            kChannelNames[event.channel],
            event.timestamp_us,
            static_cast<unsigned>(event.level));

        ProcessTrainQualification(event);
    }
}

bool ParseUnsigned64(const char* text, uint64_t& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* end = nullptr;
    errno = 0;

    const unsigned long long parsed =
        std::strtoull(text, &end, 10);

    if (errno != 0 ||
        end == text ||
        *end != '\0') {
        return false;
    }

    value = static_cast<uint64_t>(parsed);
    return true;
}

bool ParseUnsigned32(const char* text, uint32_t& value) {
    uint64_t parsed = 0;

    if (!ParseUnsigned64(text, parsed) ||
        parsed == 0 ||
        parsed > UINT32_MAX) {
        return false;
    }

    value = static_cast<uint32_t>(parsed);
    return true;
}

void BeginTrial(uint64_t run_id, uint32_t trial_id) {
    ResetAnalysis(run_id, trial_id);

    portENTER_CRITICAL(&state_mux);

    capture_state = {};
    capture_state.run_id = run_id;
    capture_state.trial_id = trial_id;
    capture_state.armed = true;

    portEXIT_CRITICAL(&state_mux);

    WriteProtocolLine(
        "ANZ|ACK|BEGIN|%" PRIu64 "|%" PRIu32,
        run_id,
        trial_id);
}

void EndTrial(uint64_t run_id, uint32_t trial_id) {
    CaptureState capture{};
    bool matched = false;

    portENTER_CRITICAL(&state_mux);

    if (capture_state.run_id == run_id &&
        capture_state.trial_id == trial_id) {
        capture_state.armed = false;
        capture = capture_state;
        matched = true;
    }

    portEXIT_CRITICAL(&state_mux);

    if (!matched) {
        WriteProtocolLine(
            "ANZ|ERR|END_MISMATCH|%" PRIu64 "|%" PRIu32,
            run_id,
            trial_id);
        return;
    }

    for (uint32_t waited_ms = 0;
         waited_ms < 250;
         ++waited_ms) {
        if (uxQueueMessagesWaiting(edge_queue) == 0) {
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }

    // Let the output task finish any event already removed from the queue.
    vTaskDelay(pdMS_TO_TICKS(10));

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);

    FlushExpiredGroupsLocked(INT64_MAX / 2, true);

    AnalysisState snapshot = analysis_state;

    xSemaphoreGive(analysis_mutex);

    WriteProtocolLine(
        "ANZ|SUMMARY|%" PRIu64 "|%" PRIu32
        "|0x%02X|%u|trains=%" PRIu32,
        run_id,
        trial_id,
        static_cast<unsigned>(capture.seen_mask),
        static_cast<unsigned>(
            PopCount(capture.seen_mask)),
        snapshot.train_count);

    WriteProtocolLine(
        "ANZ|COUNTS|%" PRIu64 "|%" PRIu32
        "|ESP01_RAW=%" PRIu32
        "|ESP02_RAW=%" PRIu32
        "|ESP03_RAW=%" PRIu32
        "|ESP01_QUAL=%" PRIu32
        "|ESP02_QUAL=%" PRIu32
        "|ESP03_QUAL=%" PRIu32
        "|dropped=%" PRIu32,
        run_id,
        trial_id,
        capture.raw_counts[0],
        capture.raw_counts[1],
        capture.raw_counts[2],
        snapshot.channels[0].qualified_count,
        snapshot.channels[1].qualified_count,
        snapshot.channels[2].qualified_count,
        capture.dropped_events);

    for (uint32_t i = 0;
         i < snapshot.train_count &&
         i < kMaxTrains;
         ++i) {
        const TrainSummary& train =
            snapshot.trains[i];

        if (!train.used) {
            continue;
        }

        WriteProtocolLine(
            "ANZ|TRAIN|%" PRIu64 "|%" PRIu32
            "|train=%" PRIu32
            "|boundaries=%" PRIu32
            "|gated=%" PRIu32
            "|start_worst_range_us=%" PRId64
            "|overall_worst_range_us=%" PRId64
            "|start_trips=%" PRIu32
            "|result=%s",
            run_id,
            trial_id,
            train.train_id,
            train.boundaries,
            train.gated_boundaries,
            train.start_worst_range_us,
            train.overall_worst_range_us,
            train.start_trips,
            (train.gated_boundaries >=
                 static_cast<uint32_t>(
                     CONFIG_ANALYZER_V10_START_GATED_BOUNDARIES) &&
             train.start_trips == 0)
                ? "PASS"
                : "FAIL");
    }

    const bool equal_qualified =
        snapshot.channels[0].qualified_count ==
            snapshot.channels[1].qualified_count &&
        snapshot.channels[1].qualified_count ==
            snapshot.channels[2].qualified_count;

    bool every_train_has_start_gate = true;

    for (uint32_t i = 0;
         i < snapshot.train_count &&
         i < kMaxTrains;
         ++i) {
        if (!snapshot.trains[i].used) {
            continue;
        }

        if (snapshot.trains[i].gated_boundaries <
            static_cast<uint32_t>(
                CONFIG_ANALYZER_V10_START_GATED_BOUNDARIES)) {
            every_train_has_start_gate = false;
        }
    }

    const bool pass =
        capture.dropped_events == 0 &&
        snapshot.train_count > 0 &&
        equal_qualified &&
        snapshot.incomplete_groups == 0 &&
        snapshot.total_start_trips == 0 &&
        every_train_has_start_gate;

    WriteProtocolLine(
        "ANZ|TRIPWIRE|%" PRIu64 "|%" PRIu32
        "|mode=START_ONLY"
        "|limit_us=%d"
        "|first_boundaries=%d"
        "|trains=%" PRIu32
        "|worst_start_range_us=%" PRId64
        "|start_trips=%" PRIu32
        "|incomplete_groups=%" PRIu32
        "|result=%s",
        run_id,
        trial_id,
        CONFIG_ANALYZER_V10_START_TRIPWIRE_US,
        CONFIG_ANALYZER_V10_START_GATED_BOUNDARIES,
        snapshot.train_count,
        snapshot.worst_start_range_us,
        snapshot.total_start_trips,
        snapshot.incomplete_groups,
        pass ? "PASS" : "FAIL");
}

void ReportStatus() {
    CaptureState capture{};

    portENTER_CRITICAL(&state_mux);
    capture = capture_state;
    portEXIT_CRITICAL(&state_mux);

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);
    const AnalysisState analysis = analysis_state;
    xSemaphoreGive(analysis_mutex);

    WriteProtocolLine(
        "ANZ|STATUS"
        "|version=10"
        "|armed=%u"
        "|run=%" PRIu64
        "|trial=%" PRIu32
        "|raw=%" PRIu32 ",%" PRIu32 ",%" PRIu32
        "|qualified=%" PRIu32 ",%" PRIu32 ",%" PRIu32
        "|trains=%" PRIu32
        "|worst_start_range_us=%" PRId64
        "|start_trips=%" PRIu32
        "|incomplete=%" PRIu32
        "|dropped=%" PRIu32,
        capture.armed ? 1U : 0U,
        capture.run_id,
        capture.trial_id,
        capture.raw_counts[0],
        capture.raw_counts[1],
        capture.raw_counts[2],
        analysis.channels[0].qualified_count,
        analysis.channels[1].qualified_count,
        analysis.channels[2].qualified_count,
        analysis.train_count,
        analysis.worst_start_range_us,
        analysis.total_start_trips,
        analysis.incomplete_groups,
        capture.dropped_events);
}

void ClearState() {
    portENTER_CRITICAL(&state_mux);
    capture_state = {};
    portEXIT_CRITICAL(&state_mux);

    xSemaphoreTake(analysis_mutex, portMAX_DELAY);
    analysis_state = {};
    xSemaphoreGive(analysis_mutex);

    EdgeEvent stale{};
    while (xQueueReceive(
               edge_queue,
               &stale,
               0) == pdTRUE) {
    }

    WriteProtocolLine("ANZ|ACK|CLEAR");
}

void ProcessCommand(char* line) {
    if (std::strcmp(line, "PING") == 0) {
        WriteProtocolLine(
            "ANZ|PONG|version=10");
        return;
    }

    if (std::strcmp(line, "STATUS") == 0) {
        ReportStatus();
        return;
    }

    if (std::strcmp(line, "CLEAR") == 0) {
        ClearState();
        return;
    }

    char* save = nullptr;
    char* command =
        strtok_r(line, "|", &save);
    char* run_text =
        strtok_r(nullptr, "|", &save);
    char* trial_text =
        strtok_r(nullptr, "|", &save);
    char* extra =
        strtok_r(nullptr, "|", &save);

    if (command == nullptr ||
        run_text == nullptr ||
        trial_text == nullptr ||
        extra != nullptr) {
        WriteProtocolLine(
            "ANZ|ERR|BAD_COMMAND");
        return;
    }

    uint64_t run_id = 0;
    uint32_t trial_id = 0;

    if (!ParseUnsigned64(run_text, run_id) ||
        run_id == 0 ||
        !ParseUnsigned32(
            trial_text,
            trial_id)) {
        WriteProtocolLine(
            "ANZ|ERR|BAD_ID");
        return;
    }

    if (std::strcmp(
            command,
            "BEGIN") == 0) {
        BeginTrial(
            run_id,
            trial_id);
    } else if (std::strcmp(
                   command,
                   "END") == 0) {
        EndTrial(
            run_id,
            trial_id);
    } else {
        WriteProtocolLine(
            "ANZ|ERR|UNKNOWN_COMMAND");
    }
}

void SerialCommandTask(void*) {
    char line[kCommandBufferLength]{};
    std::size_t length = 0;

    while (true) {
        uint8_t byte = 0;

        const int received =
            uart_read_bytes(
                kHostUart,
                &byte,
                1,
                pdMS_TO_TICKS(100));

        if (received <= 0) {
            continue;
        }

        // Accept Hercules CR, LF, or CRLF.
        if (byte == '\r' ||
            byte == '\n') {
            if (length == 0) {
                continue;
            }

            line[length] = '\0';
            ProcessCommand(line);
            length = 0;
            continue;
        }

        if (byte < 0x20 ||
            byte > 0x7E) {
            continue;
        }

        if (length + 1 <
            sizeof(line)) {
            line[length++] =
                static_cast<char>(byte);
        } else {
            length = 0;
            WriteProtocolLine(
                "ANZ|ERR|COMMAND_TOO_LONG");
        }
    }
}

void InitialiseSerial() {
    uart_config_t config{};
    config.baud_rate =
        CONFIG_ANALYZER_V10_UART_BAUD;
    config.data_bits =
        UART_DATA_8_BITS;
    config.parity =
        UART_PARITY_DISABLE;
    config.stop_bits =
        UART_STOP_BITS_1;
    config.flow_ctrl =
        UART_HW_FLOWCTRL_DISABLE;
    config.rx_flow_ctrl_thresh = 0;
    config.source_clk =
        UART_SCLK_DEFAULT;

    ESP_ERROR_CHECK(
        uart_driver_install(
            kHostUart,
            2048,
            0,
            0,
            nullptr,
            0));

    ESP_ERROR_CHECK(
        uart_param_config(
            kHostUart,
            &config));

    ESP_ERROR_CHECK(
        uart_set_pin(
            kHostUart,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE));
}

void ValidatePins() {
    for (std::size_t i = 0;
         i < kChannelCount;
         ++i) {
        for (std::size_t j = i + 1;
             j < kChannelCount;
             ++j) {
            if (kInputPins[i] ==
                kInputPins[j]) {
                WriteProtocolLine(
                    "ANZ|FATAL|DUPLICATE_GPIO|%d",
                    static_cast<int>(
                        kInputPins[i]));

                ESP_ERROR_CHECK(
                    ESP_ERR_INVALID_ARG);
            }
        }
    }
}

void InitialiseInputs() {
    uint64_t pin_mask = 0;

    for (std::size_t channel = 0;
         channel < kChannelCount;
         ++channel) {
        channel_contexts[channel].channel =
            static_cast<uint8_t>(channel);

        channel_contexts[channel].pin =
            kInputPins[channel];

        pin_mask |=
            1ULL <<
            static_cast<unsigned>(
                kInputPins[channel]);
    }

    gpio_config_t config{};
    config.pin_bit_mask = pin_mask;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en =
        GPIO_PULLUP_DISABLE;
    config.pull_down_en =
        GPIO_PULLDOWN_ENABLE;
    config.intr_type =
        GPIO_INTR_ANYEDGE;

    ESP_ERROR_CHECK(
        gpio_config(&config));

    ESP_ERROR_CHECK(
        gpio_install_isr_service(
            ESP_INTR_FLAG_IRAM));

    for (std::size_t channel = 0;
         channel < kChannelCount;
         ++channel) {
        ESP_ERROR_CHECK(
            gpio_isr_handler_add(
                kInputPins[channel],
                &EdgeIsr,
                &channel_contexts[channel]));
    }
}

}  // namespace

extern "C" void app_main() {
    esp_log_level_set(
        "*",
        ESP_LOG_WARN);

    edge_queue =
        xQueueCreate(
            kEdgeQueueLength,
            sizeof(EdgeEvent));

    output_mutex =
        xSemaphoreCreateMutex();

    analysis_mutex =
        xSemaphoreCreateMutex();

    ESP_ERROR_CHECK(
        edge_queue == nullptr ||
        output_mutex == nullptr ||
        analysis_mutex == nullptr
            ? ESP_ERR_NO_MEM
            : ESP_OK);

    InitialiseSerial();
    ValidatePins();
    InitialiseInputs();

    if (xTaskCreate(
            &EdgeOutputTask,
            "edge_output",
            6144,
            nullptr,
            10,
            nullptr) != pdPASS ||
        xTaskCreate(
            &SerialCommandTask,
            "serial_cmd",
            4096,
            nullptr,
            8,
            nullptr) != pdPASS) {
        ESP_ERROR_CHECK(
            ESP_ERR_NO_MEM);
    }

    WriteProtocolLine(
        "ANZ|READY"
        "|version=10"
        "|mode=3DEVICE_1HZ"
        "|baud=%d"
        "|ESP01_COMMIT=%d"
        "|ESP02_COMMIT=%d"
        "|ESP03_COMMIT=%d"
        "|train_us=%d..%d"
        "|associate_us=%d"
        "|start_gate_boundaries=%d"
        "|start_trip_us=%d",
        CONFIG_ANALYZER_V10_UART_BAUD,
        static_cast<int>(
            kInputPins[0]),
        static_cast<int>(
            kInputPins[1]),
        static_cast<int>(
            kInputPins[2]),
        CONFIG_ANALYZER_V10_TRAIN_MIN_US,
        CONFIG_ANALYZER_V10_TRAIN_MAX_US,
        CONFIG_ANALYZER_V10_ASSOCIATION_WINDOW_US,
        CONFIG_ANALYZER_V10_START_GATED_BOUNDARIES,
        CONFIG_ANALYZER_V10_START_TRIPWIRE_US);

    WriteProtocolLine(
        "ANZ|MODE"
        "|ANYEDGE"
        "|physical_devices=3"
        "|controller_only_devices=ESP04,ESP05"
        "|queue=%u",
        static_cast<unsigned>(
            kEdgeQueueLength));
}
