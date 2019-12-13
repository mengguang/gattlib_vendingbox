#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>

#include "gattlib.h"
#include <glib.h>

//https://developer.gnome.org/glib/stable/glib-The-Main-Event-Loop.html

#define NUS_CHARACTERISTIC_TX_UUID    "FEC7"
#define NUS_CHARACTERISTIC_RX_UUID    "FEC8"

#define PROTOCOL_REQUEST_SIZE 20
#define PROTOCOL_RESPONSE_SIZE 20

uint8_t request_buffer[PROTOCOL_REQUEST_SIZE];
uint8_t response_buffer[PROTOCOL_RESPONSE_SIZE];

const uint8_t protocol_header = 0xAA;
const uint8_t protocol_tailor = 0x55;

const uint8_t protocol_command_battery_request = 0x01;
const uint8_t protocol_command_battery_response = 0x02;
const uint8_t protocol_command_nonce_request = 0x03;
const uint8_t protocol_command_nonce_response = 0x04;
const uint8_t protocol_command_motor_request = 0x05;
const uint8_t protocol_command_motor_response = 0x06;
const uint8_t protocol_command_lock_request = 0x07;
const uint8_t protocol_command_lock_response = 0x08;

GMainLoop *main_loop = nullptr;
gatt_connection_t *m_connection = nullptr;

static int char_to_int(char input) {
    if (input >= '0' && input <= '9')
        return input - '0';
    if (input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if (input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    return 0;
}

// This function assumes src to be a zero terminated sanitized string with
// an even number of [0-9a-f] characters, and target to be sufficiently large
void hex_load(const char *src, uint8_t *target) {
    //skip 0x header
    if ((src[0] == '0') && (tolower(src[1]) == 'x')) {
        src += 2;
    }
    while (*src && src[1]) {
        *(target++) = char_to_int(*src) * 16 + char_to_int(src[1]);
        src += 2;
    }
}

void hex_dump(const char *label, const uint8_t *data, uint32_t length) {
    printf("%s",label);
    for (uint32_t i = 0; i < length; i++) {
        printf("%02X", data[i]);
    }
    printf("\n");
}

uint8_t ble_server_fill_check_sum(uint8_t *data) {
    //sum of byte 1 to 17, save result to byte 18
    uint8_t check_sum = 0;
    for (int i = 1; i <= 17; i++) {
        check_sum += data[i];
    }
    data[18] = check_sum;
    return check_sum;
}

bool make_request_of_battery(uint8_t *request) {
    memset(request, 0, PROTOCOL_REQUEST_SIZE);
    request[0] = protocol_header;
    request[19] = protocol_tailor;
    request[1] = protocol_command_battery_request;
    ble_server_fill_check_sum(request);
    return true;
}

void notification_cb(const uuid_t *uuid, const uint8_t *data, size_t data_length, void *user_data) {
    hex_dump("response:\n", data, data_length);
    g_main_loop_quit(main_loop);
}

static void usage(char *argv[]) {
    printf("%s <device_address>\n", argv[0]);
}

void int_handler(int dummy) {
    gattlib_disconnect(m_connection);
    exit(0);
}

int main(int argc, char *argv[]) {
    char input[256];
    char *input_ptr;
    int i, ret, total_length, length = 0;
    uuid_t nus_characteristic_tx_uuid;
    uuid_t nus_characteristic_rx_uuid;

    if (argc != 2) {
        usage(argv);
        return 1;
    }

    m_connection = gattlib_connect(nullptr, argv[1],
                                   GATTLIB_CONNECTION_OPTIONS_LEGACY_BDADDR_LE_RANDOM |
                                   GATTLIB_CONNECTION_OPTIONS_LEGACY_BT_SEC_LOW);
    if (m_connection == nullptr) {
        fprintf(stderr, "Fail to connect to the bluetooth device.\n");
        return 1;
    }

    // Convert characteristics to their respective UUIDs
    ret = gattlib_string_to_uuid(NUS_CHARACTERISTIC_TX_UUID, strlen(NUS_CHARACTERISTIC_TX_UUID) + 1,
                                 &nus_characteristic_tx_uuid);
    if (ret) {
        fprintf(stderr, "Fail to convert characteristic TX to UUID.\n");
        return 1;
    }
    ret = gattlib_string_to_uuid(NUS_CHARACTERISTIC_RX_UUID, strlen(NUS_CHARACTERISTIC_RX_UUID) + 1,
                                 &nus_characteristic_rx_uuid);
    if (ret) {
        fprintf(stderr, "Fail to convert characteristic RX to UUID.\n");
        return 1;
    }

    // Look for handle for NUS_CHARACTERISTIC_TX_UUID
    gattlib_characteristic_t *characteristics;
    int characteristic_count;
    ret = gattlib_discover_char(m_connection, &characteristics, &characteristic_count);
    if (ret) {
        fprintf(stderr, "Fail to discover characteristic.\n");
        return 1;
    }

    uint16_t tx_handle = 0, rx_handle = 0;
    for (i = 0; i < characteristic_count; i++) {
        printf("value handle: 0x%04X\n", characteristics[i].value_handle);
        printf("handle: 0x%04X\n", characteristics[i].handle);
        if (gattlib_uuid_cmp(&characteristics[i].uuid, &nus_characteristic_tx_uuid) == 0) {
            tx_handle = characteristics[i].value_handle;
        } else if (gattlib_uuid_cmp(&characteristics[i].uuid, &nus_characteristic_rx_uuid) == 0) {
            rx_handle = characteristics[i].value_handle;
        }
    }
    if (tx_handle == 0) {
        fprintf(stderr, "Fail to find VendingBox TX characteristic.\n");
        return 1;
    } else if (rx_handle == 0) {
        fprintf(stderr, "Fail to find VendingBox RX characteristic.\n");
        return 1;
    }
    free(characteristics);
    // Register notification handler
    //gattlib_register_indication(m_connection, notification_cb, nullptr);
    gattlib_register_notification(m_connection, notification_cb, nullptr);

    ret = gattlib_notification_start(m_connection, &nus_characteristic_rx_uuid);
    if (ret) {
        fprintf(stderr, "Fail to start notification\n.");
        goto DISCONNECT;
    }
    // Register handler to catch Ctrl+C
    signal(SIGINT, int_handler);

    while (true) {
        make_request_of_battery(request_buffer);
        hex_dump("request:\n", request_buffer, sizeof(request_buffer));

        ret = gattlib_write_without_response_char_by_handle(m_connection, tx_handle, request_buffer,
                                                            sizeof(request_buffer));
        if (ret) {
            fprintf(stderr, "Fail to send data to NUS TX characteristic.\n");
            return 1;
        }
        main_loop = g_main_loop_new(nullptr, 0);
        g_main_loop_run(main_loop);
        g_main_loop_unref(main_loop);
        main_loop = nullptr;
        sleep(1);
    }
    DISCONNECT:
    gattlib_disconnect(m_connection);
    puts("Done");
    return ret;

}
