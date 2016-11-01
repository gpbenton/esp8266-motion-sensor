/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "mqtt_config.h"


static MQTT_Client mqttClient;
static char mqtt_topic[32];
static bool motion = false;

#define MOTION_GPIO 0
#define MOTION_PIN (GPIO_ID_PIN(MOTION_GPIO))
#define PERIPHS_MOTION PERIPHS_IO_MUX_GPIO0_U
#define FUNC_MOTION FUNC_GPIO0

void mqttConnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("USER: mqttConnectedCb\r\n");

    MQTT_Publish(client, mqtt_topic, motion?"ON":"OFF", motion?2:3, 0, 0);

    // After the first print, from first connect
    os_sprintf(&mqtt_topic, "sensor/%d/motion", system_get_chip_id());
}

/*
 * wakeup from timeout handler
 */
void fpm_wakup_cb_func1(void) {
    INFO("USER: fpm_wakup_cb_func1");

    wifi_fpm_close();
    if (GPIO_INPUT_GET(MOTION_GPIO)) {
        // GPIO high indicates motion
        motion = true;
    } else {
        motion = false;
    }
    wifi_set_opmode(STATION_MODE);
    wifi_station_connect();
}

#define FPM_SLEEP_MAX_TIME  	0xFFFFFFF

void mqttDisconnectedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("USER: mqttDisconnectedCb\r\n");

    // Disconnect and go to sleep until next interrupt
    //wifi_station_disconnect();

    wifi_set_opmode(NULL_MODE);

    wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);

    wifi_fpm_open();

    /* From non_os_sdk api reference */
    /* Set GPIO0 as input */
    GPIO_DIS_OUTPUT(MOTION_PIN);
    PIN_FUNC_SELECT(PERIPHS_MOTION, FUNC_MOTION);

    // ideally we would just set GPIO_PIN_INTR_ANYEDGE
    wifi_enable_gpio_wakeup(MOTION_PIN, motion?GPIO_PIN_INTR_LOLEVEL:GPIO_PIN_INTR_HILEVEL);

    wifi_fpm_set_wakeup_cb(fpm_wakup_cb_func1); // Set wakeup callback

    wifi_fpm_do_sleep(FPM_SLEEP_MAX_TIME);
    //wifi_fpm_do_sleep(1000000 * 10);  // Sleep for 10s, then publish again to test

}

void mqttPublishedCb(uint32_t *args)
{
    MQTT_Client* client = (MQTT_Client*)args;
    INFO("USER: mqttPublishedCb\r\n");

    MQTT_Disconnect(&mqttClient);

}



/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
 *******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    enum flash_size_map size_map = system_get_flash_size_map();
    uint32 rf_cal_sec = 0;
    
    switch (size_map) {
        case FLASH_SIZE_4M_MAP_256_256:
            rf_cal_sec = 128 - 5;
            break;
            
        case FLASH_SIZE_8M_MAP_512_512:
            rf_cal_sec = 256 - 5;
            break;
            
        case FLASH_SIZE_16M_MAP_512_512:
        case FLASH_SIZE_16M_MAP_1024_1024:
            rf_cal_sec = 512 - 5;
            break;
            
        case FLASH_SIZE_32M_MAP_512_512:
        case FLASH_SIZE_32M_MAP_1024_1024:
            rf_cal_sec = 1024 - 5;
            break;
            
        default:
            rf_cal_sec = 0;
            break;
    }
    
    return rf_cal_sec;
}

void wifi_handle_event_cb(System_Event_t *evt)
{
    INFO("USER: wifi_handle_event_cb event %x\n", evt->event);
    switch (evt->event) {
        case EVENT_STAMODE_CONNECTED:
            INFO("connect to ssid %s, channel %d\n", 
                      evt->event_info.connected.ssid, 
                      evt->event_info.connected.channel);
            break;
        case EVENT_STAMODE_DISCONNECTED:
            INFO("disconnect from ssid %s, reason %d\n", 
                      evt->event_info.disconnected.ssid, 
                      evt->event_info.disconnected.reason);

            break;
        case EVENT_STAMODE_AUTHMODE_CHANGE:
            INFO("mode: %d -> %d\n", 
                      evt->event_info.auth_change.old_mode, 
                      evt->event_info.auth_change.new_mode);
            break;
        case EVENT_STAMODE_GOT_IP:
            INFO("ip:" IPSTR ",mask:" IPSTR ",gw:" IPSTR,
                      IP2STR(&evt->event_info.got_ip.ip),
                      IP2STR(&evt->event_info.got_ip.mask),
                      IP2STR(&evt->event_info.got_ip.gw));
            INFO("\n");
            MQTT_Connect(&mqttClient);
            break;
            case EVENT_SOFTAPMODE_STACONNECTED:
            INFO("station: " MACSTR "join, AID = %d\n", 
                      MAC2STR(evt->event_info.sta_connected.mac), 
                      evt->event_info.sta_connected.aid);
            break;
        case EVENT_SOFTAPMODE_STADISCONNECTED:
            INFO("station: " MACSTR "leave, AID = %d\n", 
                      MAC2STR(evt->event_info.sta_disconnected.mac), 
                      evt->event_info.sta_disconnected.aid);
            break;
        default:
            break;
    }
}

void init_done_cb(void)
{
    /* Initalization done.  Go to sleep and wait for an interrupt */
    //wifi_station_disconnect();

}


void user_init(void)
{
    struct station_config stationConf; 
    char client_id[32]; 


    /* Set GPIO as input */
    GPIO_DIS_OUTPUT(MOTION_PIN);
    PIN_FUNC_SELECT(PERIPHS_MOTION, FUNC_MOTION);

    /* Set up the topic to report */
    os_sprintf(&mqtt_topic, "sensor/%d/init", system_get_chip_id());

    uart_init(BIT_RATE_115200, BIT_RATE_115200);
    os_delay_us(1000000);

    os_printf("\n\nmqtt motion sensor v0.4\n");
    os_printf("SDK version: %s \n", system_get_sdk_version());
    os_printf("Chip id %d\n", system_get_chip_id());
    os_printf("Publishing to topic %s\n", mqtt_topic);

    wifi_set_opmode(STATION_MODE);
    
    // static IP Address to speed up connection
    wifi_station_dhcpc_stop();
    struct ip_info info;
    IP4_ADDR(&info.ip, 192, 168, 0, 5);
    IP4_ADDR(&info.gw, 192, 168, 0, 1);
    IP4_ADDR(&info.netmask, 255, 255, 255, 0);
    wifi_set_ip_info(STATION_IF, &info);

    os_memcpy(&stationConf.ssid, STA_SSID, 32); 
    os_memcpy(&stationConf.password, STA_PASS, 64); 
    stationConf.bssid_set = 0;  //need not check MAC address of AP
    wifi_station_set_config(&stationConf); 

    wifi_set_event_handler_cb(wifi_handle_event_cb);

    MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);

    os_sprintf(&client_id, "PIR_%08X", system_get_chip_id());
    MQTT_InitClient(&mqttClient, client_id, NULL, NULL, 60, 1);
    MQTT_OnConnected(&mqttClient, mqttConnectedCb);
    MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
    MQTT_OnPublished(&mqttClient, mqttPublishedCb);

    system_init_done_cb(init_done_cb);

    INFO("\r\nSystem started ...\r\n");

#if defined(GLOBAL_DEBUG_ON)
#else
    system_set_os_print(0);
#endif    
}

/* vim: set ts=4 expandtab */
