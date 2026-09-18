#ifndef LIGHTING_TYPES_H_
#define LIGHTING_TYPES_H_

#include <stdbool.h>
#include <stdint.h>

#define ZONE_COUNT 4U

/* Lighting state of each street-light zone. */
typedef enum {
    LIGHT_DAY_OFF = 0,
    LIGHT_ECO,
    LIGHT_PREDICTIVE,
    LIGHT_OCCUPIED,
    LIGHT_MANUAL,
    LIGHT_FAILSAFE,
    LIGHT_EMERGENCY
} LightMode;

/* Events exchanged between the QNX tasks. */
typedef enum {
    EVENT_SENSOR = 1,
    EVENT_PREDICTION,
    EVENT_EMERGENCY,
    EVENT_COMMAND,
    EVENT_TIMER_TICK,
    EVENT_SHUTDOWN
} EventType;

/* Commands supported by the mandatory CLI. */
typedef enum {
    COMMAND_NONE = 0,
    COMMAND_STATUS,
    COMMAND_SET_DAYLIGHT,
    COMMAND_MOTION,
    COMMAND_EMERGENCY_ON,
    COMMAND_EMERGENCY_OFF,
    COMMAND_MANUAL_BRIGHTNESS,
    COMMAND_AUTO_MODE,
    COMMAND_QUIT
} CommandType;

typedef struct {
    uint8_t zone_id;

    /* 0 = completely dark, 100 = bright daylight. */
    uint8_t daylight_percent;

    bool motion_detected;
    bool sensor_healthy;
    uint64_t timestamp_ms;
} SensorSample;

typedef struct {
    uint8_t zone_id;

    /* Expected traffic demand from 0 to 100. */
    uint8_t demand_percent;

    uint64_t timestamp_ms;
} PredictionUpdate;

typedef struct {
    bool active;
    uint32_t timeout_seconds;
    uint64_t timestamp_ms;
} EmergencyUpdate;

typedef struct {
    CommandType command;
    uint8_t zone_id;
    int32_t value;
} ManualCommand;

typedef struct {
    EventType type;

    union {
        SensorSample sensor;
        PredictionUpdate prediction;
        EmergencyUpdate emergency;
        ManualCommand command;
    } data;
} LightingEvent;

typedef struct {
    uint8_t zone_id;
    LightMode mode;
    uint8_t brightness_percent;

    uint8_t daylight_percent;
    uint8_t predicted_demand_percent;
    bool motion_detected;
    bool sensor_healthy;

    bool manual_override;
    uint8_t manual_brightness_percent;

    uint64_t last_motion_ms;
} ZoneState;

#endif /* LIGHTING_TYPES_H_ */
