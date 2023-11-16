// Includes
#include "secrets.h"

#include <WiFi.h>
#include <DHT.h>
#include <SPIFFS.h>
#include <time.h>
#include <ESPAsyncWebSrv.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <esp_sntp.h>

#define TS_ENABLE_SSL
#include <ThingSpeak.h>


// Pins
const int DHT_PIN = 33;
const int PHOTO_PIN = 36;
const int SOIL_PIN = 39;
const int LED_PIN = 2;
const int WATER_PIN = 32;


// Conf
const int SERIAL_BAUDRATE = 115200;
const int SENS_READING_DELAY = 1000;
#define CHECK_LOW_SOIL_HUMI_SAMPLES (5)
const int CHECK_LOW_SOIL_HUMI_SAMPLE_INTERVAL = 100;  //ms




// WiFi conf
const char *WIFI_SSID = SECRET_SSID;
const char *WIFI_PASSWD = SECRET_PASSWD;


// NTP conf
const char *NTP_SERVER1 = "a.st1.ntp.br";  //https://ntp.br/
const char *NTP_SERVER2 = "b.st1.ntp.br";
const char *NTP_SERVER3 = "c.st1.ntp.br";
const char *TZ = "UTC+3";  // Horário de Brazília (o + é invertido por algúm motivo)


// DHT11
DHT dht(DHT_PIN, DHT11);


// Thingspeak
WiFiClient client;
unsigned long SENSOR_CHANNEL_ID = SECRET_SENSOR_CHANNEL_ID;
const char *SENSOR_CHANNEL_READ_KEY = SECRET_SENSOR_CHANNEL_READ_KEY;
const char *SENSOR_CHANNEL_WRITE_KEY = SECRET_SENSOR_CHANNEL_WRITE_KEY;

unsigned long WATERING_CHANNEL_ID = SECRET_WATERING_CHANNEL_ID;
const char *WATERING_CHANNEL_READ_KEY = SECRET_WATERING_CHANNEL_READ_KEY;
const char *WATERING_CHANNEL_WRITE_KEY = SECRET_WATERING_CHANNEL_WRITE_KEY;

#define FIELD_TEMP (1)
#define FIELD_HUM (2)
#define FIELD_LUMI (3)
#define FIELD_SOIL (4)
#define FIELD_TIMESTAMP (5)

#define FIELD_WATERING_TIMESTAMP (1)
#define FIELD_WATERING_DURATION (2)
#define FIELD_WATERING_TYPE (3)

#define WATERING_MUTEX_TAKE_TIME (500)  //ms


typedef enum {
  WATERING_LOW_SOIL_HUMI,
  WATERING_WATER_NOW,
  WATERING_ALARM,
} WateringType_t;




// Configurações globais modificáveis
xSemaphoreHandle xConfigMutex;  //Mutex para utilizar a variável config.
typedef struct {
  int sens_log_delay;
  int watering_time;
  int min_watering_interval;

  bool check_low_soil_humi;
  int check_low_soil_humi_interval;
  float min_soil_humi;

  bool activate_alarm;
  int alarm_hours;
  int alarm_minutes;


  TaskHandle_t log_task_handle;
  TaskHandle_t watering_task_handle;
  TaskHandle_t check_low_soil_humi_task_handle;

} configs_t;


configs_t configs = {
  .sens_log_delay = 20,  //min, atraso para enviar para o servidor

  .watering_time = 6,           //s, tempo cada rega
  .min_watering_interval = 60,  //s, espera antes da próxima rega

  .check_low_soil_humi = false,       //se deve molhar a planta quando a humidade estiver baixa
  .check_low_soil_humi_interval = 4,  //h, intervado de verificação
  .min_soil_humi = 60.f,              //%

  .activate_alarm = false,  //Regar períodica
  .alarm_hours = 12,
  .alarm_minutes = 0,

  //task handlers
  NULL,
  NULL,
  NULL,
};

configs_t def_configs = configs;



xSemaphoreHandle xWateringMutex;  //Regar planta


// AsyncWebServer
AsyncWebServer webserver(80);
AsyncWebSocket websocket("/websocket");
AsyncEventSource serverevents("/events");


// Arquivos
const char *INDEX_FILE = "/index.html";


/*
 * Declarações
 */
//Leitura
float readTemp();
float readHum();
float readSoil();
float readLumi();

//Servidor
String templ_processor(const String &var);
void onWsEvent(AsyncWebSocket *srv, AsyncWebSocketClient *cli, AwsEventType type, void *arg, uint8_t *data, size_t len);
void wsReloadConfigs();

//ThingSpeak
void disable_log_sensor_data();
void enable_log_sensor_data();
void log_sensor_data_task(void *task_data);

//Regas
void check_low_soil_humi_on(void);
void check_low_soil_humi_off(void);
void check_low_soil_humi_task(void *task_data);
float check_soil(void);
void water_now(int type);
void watering_task(void *ptx_int_type);
void enable_water_now_btn(void);
void enable_water_now_btn_cli(uint32_t cli_id);
void disable_water_now_btn(void);
void btn_water_now(void);
void reload_humi_t(void *task_data);
void reload_humi();


//Utilidades
long h_to_ms(long h);
long min_to_ms(long ms);
long s_to_ms(long s);
String time_to_iso8061(time_t t);
void iso8061_to_time(String iso, time_t *t);
String float_to_string(float fl);
void log_ws(String message);

//Testes
void test_sensors();
void test_time();




void setup() {
  Serial.begin(SERIAL_BAUDRATE);

  dht.begin();

  // Pins
  pinMode(PHOTO_PIN, INPUT);
  pinMode(SOIL_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(WATER_PIN, OUTPUT);

  digitalWrite(LED_PIN, LOW);
  digitalWrite(WATER_PIN, LOW);

  //SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("Erro para iniciar o sistema de arquivos SPIFFS.");
    return;
  }


  // Conecta com WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  Serial.println("Conectando com a rede");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }
  Serial.printf("\nConectado à rede “%s”.\n", WiFi.SSID().c_str());
  Serial.printf("IP local: ");
  Serial.println(WiFi.localIP());


  // Time
  configTzTime(TZ, NTP_SERVER1, NTP_SERVER2, NTP_SERVER3);
  Serial.println("Sincronizando relógio");
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println("OK");


  // ThingSpeak
  ThingSpeak.begin(client);


  // Mutexes
  xConfigMutex = xSemaphoreCreateMutex();
  xWateringMutex = xSemaphoreCreateMutex();


  // WebServer
  // WebSockets
  websocket.onEvent(onWsEvent);
  webserver.addHandler(&websocket);

  // Rotas
  webserver.on("/", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send(SPIFFS, "/index.html", String(), false, templ_processor);
  });

  webserver.on("/histo.html", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send(SPIFFS, "/histo.html", String(), false);
  });

  webserver.on("/graphs.html", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send(SPIFFS, "/graphs.html", String(), false);
  });

  // Rotas de leitura de sensor
  webserver.on("/readsensors", HTTP_GET, [](AsyncWebServerRequest *requ) {
    AsyncResponseStream *res = requ->beginResponseStream("application/json");

    DynamicJsonDocument json(1024);

    json["event"] = "read-sensors";

    time_t now;
    time(&now);
    json["timestamp"] = now * 1000.0;  //JS é em millis
    json["type"] = "read-sensors";

    json["data"]["temp"] = readTemp();
    json["data"]["hum"] = readHum();
    json["data"]["soil"] = readSoil();
    json["data"]["lumi"] = readLumi();

    serializeJson(json, *res);
    requ->send(res);
  });

  // Configuração
  webserver.on("/configs", HTTP_GET, [](AsyncWebServerRequest *requ) {
    if (xSemaphoreTake(xConfigMutex, 2000 / portTICK_PERIOD_MS)) {
      AsyncResponseStream *res = requ->beginResponseStream("application/json");

      DynamicJsonDocument json(2048);

      json["type"] = "read-configs";

      json["configs"]["sens_log_delay"] = configs.sens_log_delay;
      json["configs"]["watering_time"] = configs.watering_time;

      json["configs"]["check_low_soil_humi"] = configs.check_low_soil_humi;
      json["configs"]["check_low_soil_humi_interval"] = configs.check_low_soil_humi_interval;
      json["configs"]["min_soil_humi"] = configs.min_soil_humi;

      json["configs"]["activate_alarm"] = configs.activate_alarm;
      json["configs"]["alarm_hours"] = (int)configs.alarm_hours;
      json["configs"]["alarm_minutes"] = (int)configs.alarm_minutes;

      serializeJson(json, *res);

      requ->send(res);

      xSemaphoreGive(xConfigMutex);
    } else {
      Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
      requ->send(503);
    }
  });

  webserver.on("/temp", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send_P(200, "text/plain", float_to_string(readTemp()).c_str());
  });

  webserver.on("/hum", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send_P(200, "text/plain", float_to_string(readHum()).c_str());
  });

  webserver.on("/soil", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send_P(200, "text/plain", float_to_string(readSoil()).c_str());
  });

  webserver.on("/lumi", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->send_P(200, "text/plain", float_to_string(readLumi()).c_str());
  });


  webserver.on("/on", HTTP_GET, [](AsyncWebServerRequest *requ) {
    digitalWrite(LED_PIN, HIGH);
    requ->redirect("/");
  });

  webserver.on("/off", HTTP_GET, [](AsyncWebServerRequest *requ) {
    digitalWrite(LED_PIN, LOW);
    requ->redirect("/");
  });

  // Arquivos Estáticos
  webserver.serveStatic("/static/", SPIFFS, "/static/");

  // Configuração
  webserver.on("/update-configs", HTTP_GET, [](AsyncWebServerRequest *requ) {
    requ->redirect("/?conf=true");
  });

  webserver.on("/update-configs", HTTP_POST, [](AsyncWebServerRequest *requ) {
    int n_params = requ->params();
    for (int i = 0; i < n_params; i++) {
      AsyncWebParameter *p = requ->getParam(i);
      if (p->isPost()) {
        Serial.printf("POST[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      }
    }

    if (xSemaphoreTake(xConfigMutex, portMAX_DELAY)) {
      AsyncWebParameter *p = NULL;

      int sens_log_delay = configs.sens_log_delay;
      if (requ->hasParam("sens_log_delay", true)) {
        p = requ->getParam("sens_log_delay", true);
        sens_log_delay = p->value().toInt();
      }

      int watering_time = configs.watering_time;
      if (requ->hasParam("watering_time", true)) {
        p = requ->getParam("watering_time", true);
        watering_time = p->value().toInt();
      }

      bool check_low_soil_humi = requ->hasParam("check_low_soil_humi", true);

      int check_low_soil_humi_interval;
      float min_soil_humi;
      if (check_low_soil_humi) {

        if (requ->hasParam("check_low_soil_humi_interval", true)) {
          p = requ->getParam("check_low_soil_humi_interval", true);
          check_low_soil_humi_interval = p->value().toInt();
        } else {
          check_low_soil_humi_interval = configs.check_low_soil_humi_interval;
        }

        if (requ->hasParam("min_soil_humi", true)) {
          p = requ->getParam("min_soil_humi", true);
          min_soil_humi = p->value().toFloat();
        } else {
          min_soil_humi = configs.min_soil_humi;
        }

      } else {  //!check_low_soil_humi (volta pra padrão)
        check_low_soil_humi_interval = def_configs.check_low_soil_humi_interval;
        min_soil_humi = def_configs.min_soil_humi;
      }

      bool activate_alarm = requ->hasParam("activate_alarm", true);
      int alarm_hours;
      int alarm_minutes;
      if (activate_alarm) {

        if (requ->hasParam("alarm_hours_alarm_minutes", true)) {
          p = requ->getParam("alarm_hours_alarm_minutes", true);
          alarm_hours = p->value().substring(0, 2).toInt();
          alarm_minutes = p->value().substring(3).toInt();
          Serial.printf("%02d:%02d\n", alarm_hours, alarm_minutes);
        } else {
          alarm_hours = configs.alarm_hours;
          alarm_minutes = configs.alarm_minutes;
        }

      } else {  //!activate_alarm (volta par padrão)
        alarm_hours = def_configs.alarm_hours;
        alarm_minutes = def_configs.alarm_minutes;
      }

      bool reload_log = configs.sens_log_delay != sens_log_delay;
      // bool changed_check_soil_humi =
      //   configs.check_low_soil_humi_interval != check_low_soil_humi_interval || configs.min_soil_humi != min_soil_humi;
      // bool changed_alarm =
      //   configs.alarm_hours != alarm_hours || configs.alarm_minutes != alarm_minutes;


      configs.sens_log_delay = sens_log_delay;
      configs.watering_time = watering_time;

      configs.check_low_soil_humi = check_low_soil_humi;
      configs.check_low_soil_humi_interval = check_low_soil_humi_interval;
      configs.min_soil_humi = min_soil_humi;

      configs.activate_alarm = activate_alarm;
      configs.alarm_hours = alarm_hours;
      configs.alarm_minutes = alarm_minutes;
      Serial.printf("%02d:%02d\n", configs.alarm_hours, configs.alarm_minutes);

      xSemaphoreGive(xConfigMutex);

      //recarrega



      if (reload_log) {
        enable_log_sensor_data();
        Serial.println("exit_enabme_log");
      }

      Serial.println("321");
      if (check_low_soil_humi) {
        void reload_humi();
      }

      // //[!!!]

      // Serial.println("321");
      // if (check_low_soil_humi && changed_check_soil_humi) {
      //   Serial.println("enter_check_off");
      //   check_low_soil_humi_off();
      //   Serial.println("enter_check_on");
      //   check_low_soil_humi_on();
      // } else {
      //   Serial.println("only_check_off");
      //   check_low_soil_humi_off();
      // }
      // Serial.println("bye.");

      // TODO
      // if (activate_alarm && changed_alarm) {

      // } else {
      // }

      //TODO Reinicia alarme e logging, Fazer a função de log


    } else {
      Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
      requ->send(503);
    }

    requ->redirect("/?conf=true");
  });

  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  webserver.begin();



  enable_log_sensor_data();


  if (def_configs.check_low_soil_humi) {
    check_low_soil_humi_on();
  }

  enable_water_now_btn();
}


void loop() {
  delay(10000);
}

void enable_log_sensor_data() {
  disable_log_sensor_data();

  xTaskCreatePinnedToCore(
    log_sensor_data_task,
    "logging_task",
    2048,
    NULL,
    tskIDLE_PRIORITY,
    &(configs.log_task_handle),
    tskNO_AFFINITY);
}


void disable_log_sensor_data() {
  if (configs.log_task_handle != NULL) {
    vTaskDelete(configs.log_task_handle);
    configs.log_task_handle = NULL;

    log_ws("Logging desativado.");
  }
}





void log_sensor_data_task(void *task_data) {
  static long delay = min_to_ms(def_configs.sens_log_delay);

  if (xSemaphoreTake(xConfigMutex, portMAX_DELAY)) {
    delay = min_to_ms(configs.sens_log_delay);

    xSemaphoreGive(xConfigMutex);
  } else {
    Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
  }

  log_ws("Logging ativado.");

  for (;;) {

    log_ws("Logando para o ThingSpeak... ");

    ThingSpeak.setField(FIELD_TEMP, readTemp());
    ThingSpeak.setField(FIELD_HUM, readHum());
    ThingSpeak.setField(FIELD_LUMI, readLumi());
    ThingSpeak.setField(FIELD_SOIL, readSoil());
    time_t now;
    time(&now);
    ThingSpeak.setField(FIELD_TIMESTAMP, time_to_iso8061(now));

    int ret_write = ThingSpeak.writeFields(SENSOR_CHANNEL_ID, SENSOR_CHANNEL_WRITE_KEY);
    if (ret_write == 200) {
      log_ws("OK!");
    } else {
      log_ws(String("ERRO, COD: ") + String(ret_write));
    }

    vTaskDelay(delay / portTICK_PERIOD_MS);
  }
}

void check_low_soil_humi_task(void *task_data) {
  long check_interval;
  float min_soil_humi;

  if (xSemaphoreTake(xConfigMutex, portMAX_DELAY)) {
    check_interval = h_to_ms(configs.check_low_soil_humi_interval);
    min_soil_humi = configs.min_soil_humi;
    xSemaphoreGive(xConfigMutex);
  } else {
    Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
    Serial.printf("Fechando “%s”...\n", __FUNCTION__);
    vTaskDelete(NULL);
  }

  for (;;) {
    vTaskDelay(check_interval / portTICK_PERIOD_MS);

    float current_soil_humi = check_soil();
    Serial.printf("current_soil_humi: %g\n", current_soil_humi);
    if (current_soil_humi < min_soil_humi) {
      water_now(WATERING_WATER_NOW);
    }
  }
}


float check_soil(void) {
  float samples_sum = 0.f;
  int samples = 0;
  for (int i = 0; i < CHECK_LOW_SOIL_HUMI_SAMPLES; i++) {
    float sample = readSoil();

    if (isnan(sample)) {  //Erro de leitura
      continue;
    }
    samples_sum += sample;
    samples++;
    vTaskDelay(CHECK_LOW_SOIL_HUMI_SAMPLE_INTERVAL / portTICK_PERIOD_MS);
  }

  if (samples == 0) {
    return NAN;
  } else {
    return samples_sum / samples;
  }
}

void enable_water_now_btn(void) {
  websocket.printfAll(R"EOF({"type":"enable-water-now","message":null})EOF");
}

void enable_water_now_btn_cli(uint32_t cli_id) {
  websocket.printf(cli_id, R"EOF({"type":"enable-water-now","message":null})EOF");
}

void disable_water_now_btn(void) {
  websocket.printfAll(R"EOF({"type":"disable-water-now","message":null})EOF");
}

void btn_water_now(void) {
  water_now(WATERING_WATER_NOW);
}


void water_now(int type) {
  if (xSemaphoreTake(xWateringMutex, WATERING_MUTEX_TAKE_TIME / portTICK_PERIOD_MS)) {
    xTaskCreatePinnedToCore(
      watering_task,
      "watering_task",
      2048,
      (void *)type,
      tskIDLE_PRIORITY,
      NULL,
      tskNO_AFFINITY);

    xSemaphoreGive(xWateringMutex);
  } else {
    log_ws("Prazo mínimo para nova rega não atingido");
  }
}


void watering_task(void *type) {
  long watering_time;
  long next_watering_interval;

  if (xSemaphoreTake(xConfigMutex, portMAX_DELAY)) {
    watering_time = configs.watering_time * 1000l;
    next_watering_interval = s_to_ms(configs.min_watering_interval);

    xSemaphoreGive(xConfigMutex);
  } else {
    Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
    vTaskDelete(NULL);
  }

  if (xSemaphoreTake(xWateringMutex, WATERING_MUTEX_TAKE_TIME / portTICK_PERIOD_MS)) {
    disable_water_now_btn();

    log_ws("Regando agora...");

    digitalWrite(WATER_PIN, HIGH);

    time_t now;
    time(&now);
    // Serial.println(time_to_iso8061(now));

    //ThingSpeak LOG
    ThingSpeak.setField(FIELD_WATERING_TIMESTAMP, time_to_iso8061(now));
    ThingSpeak.setField(FIELD_WATERING_DURATION, watering_time);

    char *watering_type;
    switch ((int)type) {
      case WATERING_WATER_NOW:
        watering_type = "water-now";
        break;
      case WATERING_LOW_SOIL_HUMI:
        watering_type = "low-soil-humi";
        break;
      case WATERING_ALARM:
        watering_type = "alarm";
        break;
      default:
        watering_type = "none";
        break;
    }
    ThingSpeak.setField(FIELD_WATERING_TYPE, watering_type);

    log_ws("\tLogando para o ThingSpeak");

    int ret_write = ThingSpeak.writeFields(SENSOR_CHANNEL_ID, WATERING_CHANNEL_WRITE_KEY);
    if (ret_write == 200) {
      log_ws("\tOK!");
    } else {
      log_ws(String("\tErro ao logar rega, COD:") + String(ret_write));
    }


    vTaskDelay(watering_time / portTICK_PERIOD_MS);

    digitalWrite(WATER_PIN, LOW);

    log_ws("Regado :)");


    vTaskDelay(next_watering_interval / portTICK_PERIOD_MS);
    enable_water_now_btn();

    xSemaphoreGive(xWateringMutex);
  } else {
    log_ws("Prazo mínimo para próxima rega não atingido.");
    return;
  }

  vTaskDelete(NULL);
}

String float_to_string(float fl) {
  return isnan(fl) ? String("---") : String(fl);
}



String templ_processor(const String &var) {
  if (var == "TEMP") {
    return float_to_string(readTemp());
  } else if (var == "HUM") {
    return float_to_string(readHum());
  } else if (var == "SOIL") {
    return float_to_string(readSoil());
  } else if (var == "LUMI") {
    return float_to_string(readLumi());
  } else {
    return String();
  }
}


void check_low_soil_humi_on(void) {
  if (xSemaphoreTake(xConfigMutex, portMAX_DELAY)) {
    if (configs.check_low_soil_humi) {
      log_ws("Regar por humidade já ativada.");
      return;
    }

    configs.check_low_soil_humi = true;
    xTaskCreatePinnedToCore(
      check_low_soil_humi_task,
      "check_low_soil_humi",
      2048,
      NULL,
      tskIDLE_PRIORITY,
      &(configs.check_low_soil_humi_task_handle),
      tskNO_AFFINITY);

    log_ws("Regar por humidade ativada.");
    Serial.printf("\tUminade mínima: %.2g%%. Intervado de %dh.\n", configs.min_soil_humi, configs.check_low_soil_humi_interval);

    xSemaphoreGive(xConfigMutex);
  } else {
    Serial.printf("Erro ao adquirir mutex em “%s” linha %d...\n", __FUNCTION__, __LINE__);
  }
}

void check_low_soil_humi_off(void) {
  Serial.println("!");
  if (!configs.check_low_soil_humi) {
    return;
  }

  Serial.println("!!");
  vTaskDelete(configs.check_low_soil_humi_task_handle);
  Serial.println("!!!");

  configs.check_low_soil_humi_task_handle = NULL;
}

void wsReloadConfigs() {
  websocket.printfAll(R"EOF({"type":"reload-config","message":null})EOF");
}

long h_to_ms(long h) {
  return min_to_ms(h * 60l);
}
long min_to_ms(long ms) {
  return ms * 60l * 1000l;
}

long s_to_ms(long s) {
  return s * 60l;
}

void test_time() {
  time_t now;
  char strftime_buf[64];
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(strftime_buf, sizeof(strftime_buf), "%d/%m/%y %T %Z%z", &timeinfo);
  Serial.println(strftime_buf);
  Serial.println(time_to_iso8061(now));
}

void test_sensors() {
  // Leitura DHT
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();

  if (isnan(temp) || isnan(hum)) {
    Serial.println("Erro de leitura de temperatura e humidade.");
  } else {
    Serial.printf("Temperatura: %6g°C \tHumidade: %6g%%\n", temp, hum);
  }

  // Leitura Fotoresistor
  float lumi = analogRead(PHOTO_PIN) / 4095.f * 100.f;  //Porcento
  Serial.printf("Luminosidade: %6g%%\n", lumi);

  // Leitura de Humidade no solo
  float soil_hum = 100.f - analogRead(SOIL_PIN) / 4095.f * 100.f;  //Porcento, invertido
  Serial.printf("Umidade do solo: %6g%%\n", soil_hum);


  delay(SENS_READING_DELAY);
}

String time_to_iso8061(time_t t) {
  struct tm gm;
  gmtime_r(&t, &gm);
  char str_buf[32];
  strftime(str_buf, sizeof(str_buf), "%FT%TZ", &gm);
  return String(str_buf);
}

time_t iso8061_to_time(String iso) {
  struct tm tm;
  strptime(iso.c_str(), "%FT%TZ", &tm);
  return mktime(&tm);
}

float readTemp() {
  float temp = dht.readTemperature();

  if (isnan(temp)) {
    Serial.println("Erro ao ler temperatura.");
    return NAN;
  } else {
    return temp;
  }
}

//static portMUX_TYPE readHumLock = portMUX_INITIALIZER_UNLOCKED;
float readHum() {
  float hum = dht.readHumidity();

  if (isnan(hum)) {
    Serial.println("Erro ao ler umidade.");
    return NAN;
  } else {
    return hum;
  }
}


float readSoil() {
  return 100.f - analogRead(SOIL_PIN) / 4095.f * 100.f;
}


float readLumi() {
  return analogRead(PHOTO_PIN) / 4095.f * 100.f;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("ws[%s][%u] conectado.\n", server->url(), client->id());
    client->printf("Olá, client %u! :)", client->id());
    wsReloadConfigs();
    client->ping();

    enable_water_now_btn_cli(client->id());

  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("ws[%s][%u] desconectado: %u\n", server->url(), client->id());

  } else if (type == WS_EVT_ERROR) {
    //error was received from the other end
    Serial.printf("ws[%s][%u] erro(%u): %s\n", server->url(), client->id(), *((uint16_t *)arg), (char *)data);

  } else if (type == WS_EVT_PONG) {
    //pong message was received (in response to a ping request maybeSerial)
    Serial.printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len) ? (char *)data : "");

  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    if (info->final && info->index == 0 && info->len == len) {
      //the whole message is in a single frame and we got all of it's data
      Serial.printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT) ? "text" : "binary", info->len);

      if (info->opcode == WS_TEXT) {
        data[len] = 0;
        Serial.printf("%s\n", (char *)data);

        DynamicJsonDocument json(1024);
        deserializeJson(json, (char *)data);

        if (json["type"] == "water-now-btn") {
          log_ws("Recebido regar agora.");
          btn_water_now();
        } else {
          Serial.printf("Tipo de mensagem “%s” desconhecida.\n", (char *)data);
          log_ws(String("Tipo de mensagem desconhecida") + String((char *)data));
        }

      } else {
        for (size_t i = 0; i < info->len; i++) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.printf("\n");
      }
      if (info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if (info->index == 0) {
        if (info->num == 0)
          Serial.printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
        Serial.printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      Serial.printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT) ? "text" : "binary", info->index, info->index + len);
      if (info->message_opcode == WS_TEXT) {
        data[len] = 0;
        Serial.printf("%s\n", (char *)data);
      } else {
        for (size_t i = 0; i < len; i++) {
          Serial.printf("%02x ", data[i]);
        }
        Serial.printf("\n");
      }

      if ((info->index + len) == info->len) {
        Serial.printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if (info->final) {
          Serial.printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT) ? "text" : "binary");
          if (info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}

void reload_humi() {
  xTaskCreatePinnedToCore(
    reload_humi_t,
    "reload_humi_t",
    1024,
    NULL,
    1,
    NULL,
    tskNO_AFFINITY);
}

void reload_humi_t(void *task_data) {
  check_low_soil_humi_off();
  check_low_soil_humi_on();
  vTaskDelete(NULL);
}

void log_ws(String message) {
  Serial.print("Log: ");
  Serial.println(message);
  websocket.printfAll(R"EOF({"type":"log","message":"%s"})EOF", message.c_str());
}