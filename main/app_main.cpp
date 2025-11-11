/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_log_level.h"
#include "freertos/projdefs.h"
#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>
#if CONFIG_PM_ENABLE
#include <esp_pm.h>
#endif

#include <esp_matter.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <app_priv.h>
#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        ESP_LOGI(TAG, "Interface IP Address changed");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricRemoved:
        {
            ESP_LOGI(TAG, "Fabric removed successfully");
            if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            {
                chip::CommissioningWindowManager & commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
                constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
                if (!commissionMgr.IsCommissioningWindowOpen())
                {
                    /* After removing last fabric, this example does not remove the Wi-Fi credentials
                     * and still has IP connectivity so, only advertising on DNS-SD.
                     */
                    CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                    chip::CommissioningWindowAdvertisement::kDnssdOnly);
                    if (err != CHIP_NO_ERROR)
                    {
                        ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                    }
                }
            }
        break;
        }

    case chip::DeviceLayer::DeviceEventType::kFabricWillBeRemoved:
        ESP_LOGI(TAG, "Fabric will be removed");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricUpdated:
        ESP_LOGI(TAG, "Fabric is updated");
        break;

    case chip::DeviceLayer::DeviceEventType::kFabricCommitted:
        ESP_LOGI(TAG, "Fabric is committed");
        break;
    default:
        break;
    }
}

static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);
    return ESP_OK;
}

static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;

    if (type == PRE_UPDATE) {
        /* Driver update */
    }

    return err;
}


esp_timer_handle_t sensor_timer;
uint16_t temp_endpoint_id;
float temp = 25.0f;
uint16_t humi_endpoint_id;
float humi = 50.0f;
uint16_t batt_endpoint_id;
float batt_voltage = 4.2f;
uint8_t batt_percentage = 100;

void temp_sensor_update( uint16_t temp_ep_id, float temp )
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([temp_ep_id, temp]() {
        attribute_t * attribute = attribute::get(temp_ep_id,
                                                 TemperatureMeasurement::Id,
                                                 TemperatureMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        attribute::get_val(attribute, &val);
        val.val.i16 = static_cast<int16_t>(temp * 100);

        attribute::update(temp_ep_id, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}

void humi_sensor_update( uint16_t humi_ep_id, float humi )
{
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([humi_ep_id, humi]() {
        attribute_t * attribute = attribute::get(humi_ep_id,
                                                 RelativeHumidityMeasurement::Id,
                                                 RelativeHumidityMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        attribute::get_val(attribute, &val);
        val.val.u16 = static_cast<uint16_t>(humi * 100);

        attribute::update(humi_ep_id, RelativeHumidityMeasurement::Id, RelativeHumidityMeasurement::Attributes::MeasuredValue::Id, &val);
    });
}


void battery_status_notification(uint16_t endpoint_id, float voltage, uint8_t percentage, void *user_data)
{
    if (endpoint_id == 0) {
        ESP_LOGE(TAG, "Battery endpoint not initialized");
        return;
    }
    
    uint32_t voltage_mv = (uint32_t)(voltage * 1000);
    
    chip::DeviceLayer::SystemLayer().ScheduleLambda([endpoint_id, voltage_mv, percentage]() {
        // BatPercentRemaining 업데이트
        esp_matter_attr_val_t percent_val = esp_matter_nullable_uint8(percentage * 2);
        attribute::update(endpoint_id, PowerSource::Id, 
                         PowerSource::Attributes::BatPercentRemaining::Id, &percent_val);
        
        // BatVoltage 업데이트
        esp_matter_attr_val_t voltage_val = esp_matter_nullable_uint32(voltage_mv);
        attribute::update(endpoint_id, PowerSource::Id,
                         PowerSource::Attributes::BatVoltage::Id, &voltage_val);
        
        ESP_LOGI(TAG, "Battery updated: %d mV, %d %%", (int)(voltage_mv), percentage);
    });
  }


void sensor_timer_callback(void *arg)
{
    temp += 0.1f;
    if( temp > 30 )
      temp = 25.0f;

    humi += 0.2f;
    if( humi > 60 )
      humi = 50.0f;

    batt_voltage -= 0.1f;
    if( batt_voltage < 3.6f )
      batt_voltage = 3.7f;

    batt_percentage -= 2;
    if( batt_percentage < 20 )
      batt_percentage = 100;  

    temp_sensor_update( temp_endpoint_id, temp );
    humi_sensor_update( humi_endpoint_id, humi );
    battery_status_notification( batt_endpoint_id, batt_voltage , batt_percentage, NULL );
    ESP_LOGI(TAG, "Sensor Timer Callback - Temp: %.2f, Humi: %.2f", temp, humi);
}

static void create_battery_endpoint(node_t *node)
{
  power_source::config_t battery_config = {};

  // Battery feature flag 설정 (0x02 = Battery feature)
  battery_config.power_source.feature_flags = 0x02;

    // Battery feature config
    battery_config.power_source.features.battery.bat_charge_level = 0; // Ok
    battery_config.power_source.features.battery.bat_replacement_needed = false;
    battery_config.power_source.features.battery.bat_replaceability = 1; // NotReplaceable
    
    // 기본 설정
    battery_config.power_source.status = 0;
    battery_config.power_source.order = 1;
    strncpy(battery_config.power_source.description, "Battery", esp_matter::cluster::power_source::k_max_description_length);

  endpoint_t * battery_ep = endpoint::power_source::create(node, &battery_config, ENDPOINT_FLAG_NONE, NULL);  
  ABORT_APP_ON_FAILURE(battery_ep != nullptr, ESP_LOGE(TAG, "Failed to create battery endpoint"));

    // 배터리 속성 수동 추가 (필요한 경우)
    cluster_t *power_source_cluster = cluster::get(battery_ep, PowerSource::Id);
    if (power_source_cluster) {
        // BatVoltage attribute 추가
        esp_matter_attr_val_t bat_voltage_val = esp_matter_nullable_uint32(4200); // 4.2V
        attribute::create(power_source_cluster, PowerSource::Attributes::BatVoltage::Id, 
                         ATTRIBUTE_FLAG_NULLABLE, bat_voltage_val);
        
        // BatPercentRemaining attribute 추가  
        esp_matter_attr_val_t bat_percent_val = esp_matter_nullable_uint8(200); // 100%
        attribute::create(power_source_cluster, PowerSource::Attributes::BatPercentRemaining::Id,
                         ATTRIBUTE_FLAG_NULLABLE, bat_percent_val);
    }

  //batt_endpoint_id = esp_matter::endpoint::get_id(battery_ep);
  //s_ctx.config.battery.cb = battery_status_notification;
  //s_ctx.config.battery.endpoint_id = endpoint::get_id(battery_ep);
  batt_endpoint_id = endpoint::get_id(battery_ep);
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    /* Initialize the ESP NVS layer */
    nvs_flash_init();

#if CONFIG_PM_ENABLE
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
#if CONFIG_FREERTOS_USE_TICKLESS_IDLE
        .light_sleep_enable = true
#endif
    };
    err = esp_pm_configure(&pm_config);
#endif
#ifdef CONFIG_ENABLE_USER_ACTIVE_MODE_TRIGGER_BUTTON
    app_driver_button_init();
#endif
    /* Create a Matter node and add the mandatory Root Node device type on endpoint 0 */
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    // add temperature sensor device
    temperature_sensor::config_t temp_sensor_config;
    endpoint_t * temp_sensor_ep = temperature_sensor::create(node, &temp_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));
    temp_endpoint_id = esp_matter::endpoint::get_id(temp_sensor_ep);

    // add humidity sensor device
    humidity_sensor::config_t humidity_sensor_config;
    endpoint_t * humidity_sensor_ep = humidity_sensor::create(node, &humidity_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(humidity_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity_sensor endpoint"));
    humi_endpoint_id = esp_matter::endpoint::get_id(humidity_sensor_ep);
  
    // add battery powered device
    create_battery_endpoint( node );
    


#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    /* Set OpenThread platform config */
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    set_openthread_platform_config(&config);
#endif

    /* Matter start */
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));


    /* esp timer create */
    esp_timer_create_args_t timer_args = {
      .callback = &sensor_timer_callback,
        .name = "sensor_timer",
    };

  ESP_ERROR_CHECK(esp_timer_create(&timer_args, &sensor_timer));
  ESP_ERROR_CHECK(esp_timer_start_periodic(sensor_timer, (10*1000*1000)));


#if 0
    uint16_t temp_ep_id = esp_matter::endpoint::get_id(temp_sensor_ep);
    float temp = 25.0f;
    while(1)
    {
        temp += 0.1f;
        if( temp > 30 )
          temp = 25.0f;
          
    // schedule the attribute update so that we can report it from matter thread
    chip::DeviceLayer::SystemLayer().ScheduleLambda([temp_ep_id, temp]() {
        attribute_t * attribute = attribute::get(temp_ep_id,
                                                 TemperatureMeasurement::Id,
                                                 TemperatureMeasurement::Attributes::MeasuredValue::Id);

        esp_matter_attr_val_t val = esp_matter_invalid(NULL);
        attribute::get_val(attribute, &val);
        val.val.i16 = static_cast<int16_t>(temp * 100);

        attribute::update(temp_ep_id, TemperatureMeasurement::Id, TemperatureMeasurement::Attributes::MeasuredValue::Id, &val);
    });

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
#endif    

}
