#include "cli.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app_tasks.h"
#include "event_queue.h"
#include "lighting_controller.h"

#define COMMAND_BUFFER_SIZE 128U
#define DEMO_LIGHT_RATED_WATTS 30.0

static char *trim_whitespace(char *text)
{
    char *end;

    while (isspace((unsigned char)*text)) {
        ++text;
    }

    end = text + strlen(text);

    while (end > text &&
           isspace((unsigned char)*(end - 1))) {
        --end;
    }

    *end = '\0';
    return text;
}

static void convert_to_lowercase(char *text)
{
    while (*text != '\0') {
        *text = (char)tolower((unsigned char)*text);
        ++text;
    }
}

static bool valid_zone_number(int zone_number)
{
    return zone_number >= 1 &&
           zone_number <= (int)ZONE_COUNT;
}

static bool valid_percent(int value)
{
    return value >= 0 && value <= 100;
}

static void print_help(void)
{
    printf("\nAvailable commands:\n");
    printf("  help                       Show this command list\n");
    printf("  status                     Show all lighting zones\n");
    printf("  daylight <zone> <0-100>    Set simulated daylight\n");
    printf("  motion <zone> on|off       Set simulated PIR motion\n");
    printf("  sensor <zone> ok|fail      Set sensor health\n");
    printf("  manual <zone> <0-100>      Set manual brightness\n");
    printf("  auto <zone>                Return zone to automatic mode\n");
    printf("  emergency on|off           Control emergency override\n");
    printf("  quit                       Stop the application\n");
    printf("\nZones are numbered 1 to %u.\n\n",
           (unsigned int)ZONE_COUNT);
}

static void print_status(AppContext *context)
{
    uint8_t zone_id;
    uint64_t current_time_ms = app_time_ms();
    double actual_kwh;
    double baseline_kwh;
    double savings_percent;

    pthread_mutex_lock(&context->state_lock);

    energy_monitor_update(
            &context->energy,
            context->zones,
            current_time_ms);

    actual_kwh = energy_monitor_actual_kwh(
            &context->energy,
            DEMO_LIGHT_RATED_WATTS);

    baseline_kwh = energy_monitor_baseline_kwh(
            &context->energy,
            DEMO_LIGHT_RATED_WATTS);

    savings_percent = energy_monitor_savings_percent(
            &context->energy);

    printf("\n");
    printf("+------+--------------+--------+----------+-----------+--------+--------+\n");
    printf("| Zone | Mode         | Light  | Daylight | Predicted | Motion | Sensor |\n");
    printf("+------+--------------+--------+----------+-----------+--------+--------+\n");

    for (zone_id = 0U; zone_id < ZONE_COUNT; ++zone_id) {
        const ZoneState *zone = &context->zones[zone_id];

        printf(
                "| %4u | %-12s | %5u%% | %7u%% | %8u%% | %-6s | %-6s |\n",
                (unsigned int)(zone_id + 1U),
                lighting_mode_name(zone->mode),
                (unsigned int)zone->brightness_percent,
                (unsigned int)zone->daylight_percent,
                (unsigned int)zone->predicted_demand_percent,
                zone->motion_detected ? "YES" : "NO",
                zone->sensor_healthy ? "OK" : "FAIL");
    }

    printf("+------+--------------+--------+----------+-----------+--------+--------+\n");

    if (context->emergency_active) {
        uint64_t remaining_seconds = 0U;

        if (context->emergency_deadline_ms >
                current_time_ms) {
            remaining_seconds =
                    (context->emergency_deadline_ms -
                     current_time_ms +
                     999U) /
                    1000U;
        }

        printf(
                "Emergency override: ACTIVE (%llu seconds remaining)\n",
                (unsigned long long)remaining_seconds);
    } else {
        printf("Emergency override: OFF\n");
    }

    printf("Estimated actual energy:   %.6f kWh\n",
           actual_kwh);
    printf("Always-ON baseline energy: %.6f kWh\n",
           baseline_kwh);
    printf("Estimated energy saving:   %.2f%%\n\n",
           savings_percent);

    pthread_mutex_unlock(&context->state_lock);
}

static void send_controller_command(
        AppContext *context,
        CommandType command,
        uint8_t zone_id,
        int32_t value)
{
    LightingEvent event = {0};

    event.type = EVENT_COMMAND;
    event.data.command.command = command;
    event.data.command.zone_id = zone_id;
    event.data.command.value = value;

    if (event_queue_send(
                context->event_queue,
                &event,
                PRIORITY_COMMAND) == -1) {
        perror("Failed to send controller command");
    }
}

void *cli_task(void *argument)
{
    AppContext *context = (AppContext *)argument;
    char input_buffer[COMMAND_BUFFER_SIZE];

    printf("\nSmart Street Lighting CLI started.\n");
    print_help();

    while (app_is_running(context)) {
        char *command;
        int zone_number;
        int value;
        char option[16];

        printf("lighting> ");
        fflush(stdout);

        if (fgets(
                    input_buffer,
                    sizeof(input_buffer),
                    stdin) == NULL) {
            printf("\nCLI input closed. Stopping application.\n");
            app_request_stop(context);
            break;
        }

        command = trim_whitespace(input_buffer);
        convert_to_lowercase(command);

        if (*command == '\0') {
            continue;
        }

        if (strcmp(command, "help") == 0) {
            print_help();
        } else if (strcmp(command, "status") == 0) {
            print_status(context);
        } else if (strcmp(command, "quit") == 0) {
            printf("Shutdown requested.\n");
            app_request_stop(context);
            break;
        } else if (sscanf(
                           command,
                           "daylight %d %d",
                           &zone_number,
                           &value) == 2) {

            if (!valid_zone_number(zone_number) ||
                !valid_percent(value)) {
                printf("Use: daylight <zone 1-%u> <0-100>\n",
                       (unsigned int)ZONE_COUNT);
                continue;
            }

            if (io_backend_set_daylight(
                        &context->io,
                        (uint8_t)(zone_number - 1),
                        (uint8_t)value) == -1) {
                perror("Unable to set daylight");
            } else {
                printf(
                        "Zone %d daylight set to %d%%.\n",
                        zone_number,
                        value);
            }
        } else if (sscanf(
                           command,
                           "motion %d %15s",
                           &zone_number,
                           option) == 2) {

            bool detected;

            if (!valid_zone_number(zone_number) ||
                (strcmp(option, "on") != 0 &&
                 strcmp(option, "off") != 0)) {
                printf("Use: motion <zone 1-%u> on|off\n",
                       (unsigned int)ZONE_COUNT);
                continue;
            }

            detected = strcmp(option, "on") == 0;

            if (io_backend_set_motion(
                        &context->io,
                        (uint8_t)(zone_number - 1),
                        detected) == -1) {
                perror("Unable to set motion");
            } else {
                printf(
                        "Zone %d motion set to %s.\n",
                        zone_number,
                        detected ? "ON" : "OFF");
            }
        } else if (sscanf(
                           command,
                           "sensor %d %15s",
                           &zone_number,
                           option) == 2) {

            bool healthy;

            if (!valid_zone_number(zone_number) ||
                (strcmp(option, "ok") != 0 &&
                 strcmp(option, "fail") != 0)) {
                printf("Use: sensor <zone 1-%u> ok|fail\n",
                       (unsigned int)ZONE_COUNT);
                continue;
            }

            healthy = strcmp(option, "ok") == 0;

            if (io_backend_set_sensor_health(
                        &context->io,
                        (uint8_t)(zone_number - 1),
                        healthy) == -1) {
                perror("Unable to set sensor health");
            } else {
                printf(
                        "Zone %d sensor health set to %s.\n",
                        zone_number,
                        healthy ? "OK" : "FAIL");
            }
        } else if (sscanf(
                           command,
                           "manual %d %d",
                           &zone_number,
                           &value) == 2) {

            if (!valid_zone_number(zone_number) ||
                !valid_percent(value)) {
                printf("Use: manual <zone 1-%u> <0-100>\n",
                       (unsigned int)ZONE_COUNT);
                continue;
            }

            send_controller_command(
                    context,
                    COMMAND_MANUAL_BRIGHTNESS,
                    (uint8_t)(zone_number - 1),
                    (int32_t)value);

            printf(
                    "Zone %d manual brightness requested: %d%%.\n",
                    zone_number,
                    value);
        } else if (sscanf(
                           command,
                           "auto %d",
                           &zone_number) == 1) {

            if (!valid_zone_number(zone_number)) {
                printf("Use: auto <zone 1-%u>\n",
                       (unsigned int)ZONE_COUNT);
                continue;
            }

            send_controller_command(
                    context,
                    COMMAND_AUTO_MODE,
                    (uint8_t)(zone_number - 1),
                    0);

            printf(
                    "Zone %d returned to automatic mode.\n",
                    zone_number);
        } else if (sscanf(
                           command,
                           "emergency %15s",
                           option) == 1) {

            if (strcmp(option, "on") == 0) {
                app_request_emergency(context, true);
            } else if (strcmp(option, "off") == 0) {
                app_request_emergency(context, false);
            } else {
                printf("Use: emergency on|off\n");
            }
        } else {
            printf(
                    "Unknown command. Type 'help' for the command list.\n");
        }
    }

    printf("[CLI] Command interface stopped.\n");
    fflush(stdout);

    return NULL;
}
