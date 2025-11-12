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

//#define ENABLE_TEMPERATURE_SENSOR
//#define ENABLE_HUMIDITY_SENSOR
//#define ENABLE_POWER_SOURCE_BATTERY
//#define ENABLE_POWER_SOURCE_BATTERY_ENDPOINT0
#define ENABLE_AIRQUALITY_PM_SENSOR

#define SENSOR_UPDATE_PERIOD_SEC 30

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

#if defined(ENABLE_TEMPERATURE_SENSOR)
uint16_t temp_endpoint_id;
float temp = 25.0f;
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
#endif

#if defined(ENABLE_HUMIDITY_SENSOR)
uint16_t humi_endpoint_id;
float humi = 50.0f;
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
#endif

#if defined(ENABLE_POWER_SOURCE_BATTERY) || defined(ENABLE_POWER_SOURCE_BATTERY_ENDPOINT0)
uint16_t batt_endpoint_id;
float batt_voltage = 4.2f;
uint8_t batt_percentage = 100;
void battery_status_notification(uint16_t endpoint_id, float voltage, uint8_t percentage, void *user_data)
{
/* */
#if defined(ENABLE_POWER_SOURCE_BATTERY) 
  if (endpoint_id == 0) {
        ESP_LOGE(TAG, "Battery endpoint not initialized");
        return;
    }
#endif    
    
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
#endif

#if defined(ENABLE_AIRQUALITY_PM_SENSOR)
uint16_t pm_sensor_endpoint_id;
float pm25_value = 10.0f;
float pm10_value = 20.0f;
float pm1_value = 5.0f;
// PM2.5 값 업데이트 함수
void pm25_sensor_update(float pm25_value)
{
  chip::DeviceLayer::SystemLayer().ScheduleLambda([pm25_value]() {
    esp_matter_attr_val_t val = esp_matter_nullable_float(pm25_value);
    
    attribute::update(pm_sensor_endpoint_id,
                      Pm25ConcentrationMeasurement::Id,
                      Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                      &val);
  });
}

// PM10 값 업데이트 함수
void pm10_sensor_update(float pm10_value)
{
  chip::DeviceLayer::SystemLayer().ScheduleLambda([pm10_value]() {
    esp_matter_attr_val_t val = esp_matter_nullable_float(pm10_value);
    
    attribute::update(pm_sensor_endpoint_id,
                      Pm10ConcentrationMeasurement::Id,
                      Pm10ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                      &val);
  });
}

// PM1 값 업데이트 함수
void pm1_sensor_update(float pm1_value)
{
  chip::DeviceLayer::SystemLayer().ScheduleLambda([pm1_value]() {
    esp_matter_attr_val_t val = esp_matter_nullable_float(pm1_value); 
    attribute::update(pm_sensor_endpoint_id,
                      Pm1ConcentrationMeasurement::Id,
                      Pm1ConcentrationMeasurement::Attributes::MeasuredValue::Id,
                      &val);
  });
} 

#endif

void sensor_timer_callback(void *arg)
{

    #if defined(ENABLE_TEMPERATURE_SENSOR)
    temp += 0.1f;
    if( temp > 30 )
      temp = 25.0f;

    temp_sensor_update( temp_endpoint_id, temp );
    #endif

    #if defined(ENABLE_HUMIDITY_SENSOR)
    humi += 0.2f;
    if( humi > 60 )
      humi = 50.0f;

    humi_sensor_update( humi_endpoint_id, humi );
    #endif

    #if defined(ENABLE_POWER_SOURCE_BATTERY) || defined(ENABLE_POWER_SOURCE_BATTERY_ENDPOINT0)
    batt_voltage -= 0.1f;
    if( batt_voltage < 3.3f )
      batt_voltage = 4.2f;

    batt_percentage -= 2;
    if( batt_percentage < 20 )
      batt_percentage = 100;  

    battery_status_notification( batt_endpoint_id, batt_voltage , batt_percentage, NULL );
    //ESP_LOGI(TAG, "Sensor Timer Callback - Temp: %.2f, Humi: %.2f", temp, humi);
    #endif

    #if defined(ENABLE_AIRQUALITY_PM_SENSOR)
    pm25_value += 1.0f;
    if( pm25_value > 500.0f )
      pm25_value = 10.0f;
    pm25_sensor_update( pm25_value );

    pm10_value += 2.0f;
    if( pm10_value > 1000.0f )
      pm10_value = 20.0f;
    pm10_sensor_update( pm10_value );
    
    pm1_value += 0.5f;
    if( pm1_value > 250.0f )
      pm1_value = 5.0f;
    pm1_sensor_update( pm1_value );

    ESP_LOGI(TAG, "Sensor Timer Callback - PM2.5: %.2f, PM10: %.2f, PM1: %.2f", pm25_value, pm10_value, pm1_value);
    #endif
}

#if defined(ENABLE_POWER_SOURCE_BATTERY)
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
    battery_config.power_source.status = 1;
    battery_config.power_source.order = 0;
    strncpy(battery_config.power_source.description, "Battery", esp_matter::cluster::power_source::k_max_description_length);

  endpoint_t * battery_ep = endpoint::power_source::create(node, &battery_config, ENDPOINT_FLAG_NONE, NULL);  
  ABORT_APP_ON_FAILURE(battery_ep != nullptr, ESP_LOGE(TAG, "Failed to create battery endpoint"));

    // 배터리 속성 수동 추가 (필요한 경우)
    cluster_t *power_source_cluster = cluster::get(battery_ep, PowerSource::Id);
    if (power_source_cluster) {
    // BatPresent - 필수!
    esp_matter_attr_val_t bat_present_val = esp_matter_bool(true);
    attribute::create(power_source_cluster, 
                     PowerSource::Attributes::BatPresent::Id, 
                     ATTRIBUTE_FLAG_NONE, bat_present_val);
    
    // BatQuantity - 필수!
    esp_matter_attr_val_t bat_quantity_val = esp_matter_uint8(1);
    attribute::create(power_source_cluster, 
                     PowerSource::Attributes::BatQuantity::Id,
                     ATTRIBUTE_FLAG_NONE, bat_quantity_val);

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
#endif

#if defined(ENABLE_POWER_SOURCE_BATTERY_ENDPOINT0)
static void add_battery_to_root_node(node_t *node)
{
  // Endpoint 0 (Root Node) 가져오기
  endpoint_t *root_endpoint = endpoint::get(node, 0);
  if (!root_endpoint) {
    ESP_LOGE(TAG, "Failed to get root endpoint");
    return;
  }

  // Power Source 클러스터 직접 생성
  cluster_t *power_source_cluster = cluster::create(
    root_endpoint,
    PowerSource::Id,
    CLUSTER_FLAG_SERVER
  );
  
  if (!power_source_cluster) {
    ESP_LOGE(TAG, "Failed to create power source cluster");
    return;
  }

  // 필수 속성들 추가
  // Status
  esp_matter_attr_val_t status_val = esp_matter_uint8(1); // Active
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::Status::Id, 
                   ATTRIBUTE_FLAG_NONE, status_val);

  // Order
  esp_matter_attr_val_t order_val = esp_matter_uint8(0);
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::Order::Id,
                   ATTRIBUTE_FLAG_NONE, order_val);

  // Description
  esp_matter_attr_val_t desc_val = esp_matter_char_str("Battery", strlen("Battery"));
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::Description::Id,
                   ATTRIBUTE_FLAG_NONE, desc_val);

  // BatPresent
  esp_matter_attr_val_t bat_present_val = esp_matter_bool(true);
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::BatPresent::Id, 
                   ATTRIBUTE_FLAG_NONE, bat_present_val);
  
  // BatQuantity
  esp_matter_attr_val_t bat_quantity_val = esp_matter_uint8(1);
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::BatQuantity::Id,
                   ATTRIBUTE_FLAG_NONE, bat_quantity_val);

  // BatVoltage
  esp_matter_attr_val_t bat_voltage_val = esp_matter_nullable_uint32(4200);
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::BatVoltage::Id, 
                   ATTRIBUTE_FLAG_NULLABLE, bat_voltage_val);
  
  // BatPercentRemaining
  esp_matter_attr_val_t bat_percent_val = esp_matter_nullable_uint8(200);
  attribute::create(power_source_cluster, 
                   PowerSource::Attributes::BatPercentRemaining::Id,
                   ATTRIBUTE_FLAG_NULLABLE, bat_percent_val);

  // FeatureMap - Battery feature (0x02)
  esp_matter_attr_val_t feature_map_val = esp_matter_uint32(0x02);
  attribute::create(power_source_cluster,
                   PowerSource::Attributes::FeatureMap::Id,
                   ATTRIBUTE_FLAG_NONE, feature_map_val);

  batt_endpoint_id = 0;
  
  ESP_LOGI(TAG, "Power Source cluster added to endpoint 0");
}
#endif

#if defined(ENABLE_AIRQUALITY_PM_SENSOR)
// PM 센서 Endpoint 생성 함수 (Air Quality Sensor 기반)
static void create_pm_sensor_endpoint(node_t *node)
{
  /*
  feature_flags 설명:
0x01 = NumericMeasurement (실제 측정값 제공)
0x02 = LevelIndication (레벨만 제공)
0x04 = MediumLevel (중간 레벨)
0x08 = CriticalLevel (위험 레벨)
0x10 = PeakMeasurement (피크값 측정)
0x20 = AverageMeasurement (평균값 측정)  
  */

  // Air Quality Sensor config 설정
  air_quality_sensor::config_t config;
  
  // Air Quality Sensor endpoint 생성
  endpoint_t *endpoint = air_quality_sensor::create(node, &config, ENDPOINT_FLAG_NONE, NULL);
  ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "PM 센서 endpoint 생성 실패"));
  
  pm_sensor_endpoint_id = endpoint::get_id(endpoint);
  
  // ========== PM2.5 Concentration Measurement Cluster 추가 ==========
  cluster::pm25_concentration_measurement::config_t pm25_config;
  // NumericMeasurement feature 활성화 (Feature bit 0x01)
  pm25_config.feature_flags = 0x01;
    
  cluster_t *pm25_cluster = cluster::pm25_concentration_measurement::create(
    endpoint, &pm25_config, CLUSTER_FLAG_SERVER
  );
  
  if (pm25_cluster) {
    // MeasuredValue attribute 설정
    cluster::pm25_concentration_measurement::attribute::create_measured_value(
      pm25_cluster, nullable<float>(0.0f)
    );
    
    // MinMeasuredValue attribute 설정
    cluster::pm25_concentration_measurement::attribute::create_min_measured_value(
      pm25_cluster, nullable<float>(0.0f)
    );
    
    // MaxMeasuredValue attribute 설정
    cluster::pm25_concentration_measurement::attribute::create_max_measured_value(
      pm25_cluster, nullable<float>(1000.0f)
    );
    
    // MeasurementUnit attribute 설정 (4 = μg/m³)
    cluster::pm25_concentration_measurement::attribute::create_measurement_unit(
      pm25_cluster, static_cast<uint8_t>(4)
    );
    
    // MeasurementMedium attribute 설정 (0 = Air)
    cluster::pm25_concentration_measurement::attribute::create_measurement_medium(
      pm25_cluster, static_cast<uint8_t>(0)
    );
    
    ESP_LOGI(TAG, "PM2.5 클러스터 생성 완료");
  }
  
  // ========== PM10 Concentration Measurement Cluster 추가 ==========
  cluster::pm10_concentration_measurement::config_t pm10_config;
  // NumericMeasurement feature 활성화 (Feature bit 0x01)
  pm10_config.feature_flags = 0x01;
  cluster_t *pm10_cluster = cluster::pm10_concentration_measurement::create(
    endpoint, &pm10_config, CLUSTER_FLAG_SERVER
  );
  
  if (pm10_cluster) {
    // MeasuredValue attribute 설정
    cluster::pm10_concentration_measurement::attribute::create_measured_value(
      pm10_cluster, nullable<float>(0.0f)
    );
    
    // MinMeasuredValue attribute 설정
    cluster::pm10_concentration_measurement::attribute::create_min_measured_value(
      pm10_cluster, nullable<float>(0.0f)
    );
    
    // MaxMeasuredValue attribute 설정
    cluster::pm10_concentration_measurement::attribute::create_max_measured_value(
      pm10_cluster, nullable<float>(1000.0f)
    );
    
    // MeasurementUnit attribute 설정
    cluster::pm10_concentration_measurement::attribute::create_measurement_unit(
      pm10_cluster, static_cast<uint8_t>(4)
    );
    
    // MeasurementMedium attribute 설정
    cluster::pm10_concentration_measurement::attribute::create_measurement_medium(
      pm10_cluster, static_cast<uint8_t>(0)
    );
    
    ESP_LOGI(TAG, "PM10 클러스터 생성 완료");
  }
  
  #if 1
  // ========== PM1 Concentration Measurement Cluster 추가 (선택) ==========
  cluster::pm1_concentration_measurement::config_t pm1_config;
  // NumericMeasurement feature 활성화 (Feature bit 0x01)
  pm1_config.feature_flags = 0x01;
  cluster_t *pm1_cluster = cluster::pm1_concentration_measurement::create(
    endpoint, &pm1_config, CLUSTER_FLAG_SERVER
  );
  
  if (pm1_cluster) {
    cluster::pm1_concentration_measurement::attribute::create_measured_value(
      pm1_cluster, nullable<float>(0.0f)
    );
    
    cluster::pm1_concentration_measurement::attribute::create_min_measured_value(
      pm1_cluster, nullable<float>(0.0f)
    );
    
    cluster::pm1_concentration_measurement::attribute::create_max_measured_value(
      pm1_cluster, nullable<float>(1000.0f)
    );
    
    cluster::pm1_concentration_measurement::attribute::create_measurement_unit(
      pm1_cluster, static_cast<uint8_t>(4)
    );
    
    cluster::pm1_concentration_measurement::attribute::create_measurement_medium(
      pm1_cluster, static_cast<uint8_t>(0)
    );
    
    ESP_LOGI(TAG, "PM1 클러스터 생성 완료");
  }
  #endif
  
  ESP_LOGI(TAG, "PM 센서 Endpoint 생성 완료, ID: %d", pm_sensor_endpoint_id);
}
#endif

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

    #if defined(ENABLE_TEMPERATURE_SENSOR)
    // add temperature sensor device
    temperature_sensor::config_t temp_sensor_config;
    endpoint_t * temp_sensor_ep = temperature_sensor::create(node, &temp_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(temp_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create temperature_sensor endpoint"));
    temp_endpoint_id = esp_matter::endpoint::get_id(temp_sensor_ep);
    #endif

    #if defined(ENABLE_HUMIDITY_SENSOR)
    // add humidity sensor device
    humidity_sensor::config_t humidity_sensor_config;
    endpoint_t * humidity_sensor_ep = humidity_sensor::create(node, &humidity_sensor_config, ENDPOINT_FLAG_NONE, NULL);
    ABORT_APP_ON_FAILURE(humidity_sensor_ep != nullptr, ESP_LOGE(TAG, "Failed to create humidity_sensor endpoint"));
    humi_endpoint_id = esp_matter::endpoint::get_id(humidity_sensor_ep);
    #endif

    #if defined(ENABLE_POWER_SOURCE_BATTERY)
    // add battery powered device
    create_battery_endpoint( node );
    #endif

    #if defined(ENABLE_POWER_SOURCE_BATTERY_ENDPOINT0)
    add_battery_to_root_node(node);
    #endif

    #if defined(ENABLE_AIRQUALITY_PM_SENSOR)
    create_pm_sensor_endpoint( node );
    #endif


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
  ESP_ERROR_CHECK(esp_timer_start_periodic(sensor_timer, (SENSOR_UPDATE_PERIOD_SEC*1000*1000)));

}
