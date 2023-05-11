//NodeMCU-32S
//ESP32 Dev Module
//
//03.09.22 MQTT добавлен batt_capacity_m 
//19.10.22 Изменен json, добавлены PV
//01.03.23 Добалены диоды диагностики
//???????? Остаток заряда батарей
//12.04.23 uptime


//???????? изменить версию программы
String DevVer="110523";

#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <PubSubClient.h>
#include <FS.h>                                             // Библиотека для работы с файловой системой
#include "SPIFFS.h"
#define FORMAT_SPIFFS_IF_FAILED true
#include <SimpleFTPServer.h>                                //FTP сервер

#define LED1 5  //PWR Blue                                  //Тусклый при включении,светит(10) при setup, светит (5) в работе
#define LED2 18 //WF Green                                  //Моргает(10) подк.WiFi, светит(1) - подкл.,MQTT отправ.(30) 
#define LED3 19 //Ser Yellow                                //Светит(1) при работе UART
#define LED4 21 //Vol Red                                   //Вспыхивает(30) при работе АЦП напряжение батарей, светит (10) если батареи не подключены

const char* host = "Voltronic ESP32";
const char* ssid = "WiFi";                                  //WiFi SSID
const char* password = "11111111";                          //WiFi Password
int an1, an2, an3, an4;
int ao1, ao2, ao3, ao4;
float an1_, an2_, an3_, an4_, a;

const char* mqtt_server = "192.168.1.50";                   //MQTT Server IP
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (256)
char msg[MSG_BUFFER_SIZE];
String con = "";
bool rest = false;
unsigned int upt;
unsigned long uptl;
String uptime="0d 0h 1m";

WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);
FtpServer ftpSrv;

/*
   Server Index Page
*/

const char* serverIndex =
  "<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
  "<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
  "<input type='file' name='update'>"
  "<input type='submit' value='Update'>"
  "</form>"
  "<div id='prg'>progress: 0%</div>"
  "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!');"
  "alert('Update Success. Rebooting...');"
  "open('/')"
  "},"
  "error: function (a, b, c) {"
  "}"
  "});"
  "});"
  "</script>";

/*
   setup function
*/
// Reply1 QPIGS
float voltage_grid;
float freq_grid;
float voltage_out;
float freq_out;
int load_va;
int load_watt;
int load_percent;
int voltage_bus;
float voltage_batt;
int batt_charge_current;
int batt_capacity;
int temp_heatsink;
int pv_input_current;
float pv_input_voltage;
int pv_input_watts;
float pv_input_watthour;
float load_watthour = 0;
float scc_voltage;
int batt_discharge_current;
char device_status[7];
char device_status2[2];

// Reply2 QPIRI
float grid_voltage_rating;
float grid_current_rating;
float out_voltage_rating;
float out_freq_rating;
float out_current_rating;
int out_va_rating;
int out_watt_rating;
float batt_rating;
float batt_recharge_voltage;
float batt_under_voltage;
float batt_bulk_voltage;
float batt_float_voltage;
int batt_type;
int max_grid_charge_current;
int max_charge_current;
int in_voltage_range;
int out_source_priority;
int charger_source_priority;
int machine_type;
int topology;
int out_mode;
float batt_redischarge_voltage;

int batt_capacity_m;

//String out="";
String devicename;
int runinterval = 120;
float ampfactor = 1.0;
float wattfactor = 1.0;
char status1[1024];

String QPIGS = "\x51\x50\x49\x47\x53\xB7\xA9\x0D";
String QPIRI = "\x51\x50\x49\x52\x49\xF8\x54\x0D";

String POP_USB = "\x50\x4F\x50\x30\x30\xC2\x48\x0D";        //Setting device output source priority  POP00
String POP_SUB = "\x50\x4F\x50\x30\x31\xD2\x69\x0D";        //POP01
String POP_SBU = "\x50\x4F\x50\x30\x32\xE2\x0A\x0D";        //POP02
String PCP_SBLe = "\x50\x43\x50\x30\x30\x8D\x7A\x0D";       //Setting device charger priority  PCP00
String PCP_SLBe = "\x50\x43\x50\x30\x32\xAD\x38\x0D";       //PCP02

//String QPI = "\x51\x50\x49\x82\x61\x0D";
//String QGMN = "\x51\x47\x4d\x4e\x49\x28\x0D";
//String QID = "\x51\x49\x44\xD6\xEA\x0D";
//String QVFW = "\x51\x56\x46\x57\x62\x99\x0D";

String string1, string2, string3, pop, pcp;
int string1L, string2L, string3L, popL, pcpL;
unsigned long start;
int count;

void setup() {
  // put your setup code here, to run once:
  //Serial.begin(115200);
  Serial2.begin(2400);
  Serial2.setTimeout(2000);
  
  ledcSetup(1, 12000, 10);
  ledcSetup(2, 12000, 10);
  ledcSetup(3, 12000, 10);
  ledcSetup(4, 12000, 10);
  ledcAttachPin(LED1, 1);
  ledcAttachPin(LED2, 2);
  ledcAttachPin(LED3, 3);
  ledcAttachPin(LED4, 4);
  ledcWrite(1, 10);
  ledcWrite(2, 0);
  ledcWrite(3, 0);
  ledcWrite(4, 0);

  WiFi_con();

  server.on("/data", HTTP_GET, []() {
    if (server.arg("mode")) {
      if (server.arg("mode") == "10") POP(0);
      if (server.arg("mode") == "11") POP(1);
      if (server.arg("mode") == "12") POP(2);
      if (server.arg("mode") == "20") PCP(0);
      if (server.arg("mode") == "21") PCP(1);
      if (server.arg("mode") == "22") PCP(2);
      if (server.arg("mode") == "23") PCP(3);
    }
    
    String js = out();
    server.send(200, "text/json", js);
  });
  server.on("/json", HTTP_GET, []() {
    String js = out();
    server.send(200, "text/plain", js);
  });
  server.onNotFound([]() {                                  // Описываем действия при событии "Не найдено"
    if (!handleFileRead(server.uri())) {                    // Если функция handleFileRead (описана ниже) возвращает значение false в ответ на поиск файла в файловой системе
      String message;
      message.reserve(100);
      message = F("Error: File not found\n\nURI: ");
      message += server.uri();
      message += F("\nMethod: ");
      message += (server.method() == HTTP_GET) ? "GET" : "POST";
      message += F("\nArguments: ");
      message += server.args();
      message += '\n';
      for (uint8_t i = 0; i < server.args(); i++) {
        message += F(" NAME:");
        message += server.argName(i);
        message += F("\n VALUE:");
        message += server.arg(i);
        message += '\n';
      }
      message += "path=";
      message += server.arg("path");
      message += '\n';
      server.send(404, "text/plain", message);                        // возвращаем на запрос текстовое сообщение "File isn't found" с кодом 404 (не найдено)
    }
  });
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });

  server.begin();
  //Serial.println("Start");
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED);                              // Инициализируем работу с файловой системой
  ftpSrv.begin("admin", "1234");                                      // Поднимаем FTP-сервер для удобства отладки работы HTML (логин: relay, пароль: relay)

}

void loop() {
  // put your main code here, to run repeatedly:
  server.handleClient();
  ftpSrv.handleFTP();
  ledcWrite(1, 5);

  con = "";
  if (WiFi.status() == WL_CONNECTED) con += String("W"); else WiFi_con();//con+=String("w"); //Подключение WiFi 
  if (client.connected()) con += String("M"); else {
    con += String("m");                                               //Подключение MQTT
    rest = true;
  }
  if (client.connected() && rest) {
    PubConfig();
    rest = false;
  }

  if(upt>1 && millis()<300000) uptl=millis();
  if(uptl+300000 < millis()){
    uptl=millis();
    upt++;
    uptime=int(upt/288);
    uptime+="d ";
    uptime+=int(upt%288/12);
    uptime+="h ";
    uptime+=upt%288%12*5;
    uptime+="m";
  }

  if (start + 200 < millis()) {
    if(an1_>3 || an1_==0) ledcWrite(4, 30);
    an1 += analogRead(A6); //A6 G34
    an2 += analogRead(A7); //A7 G35
    an3 += analogRead(A4); //A4 G32
    an4 += analogRead(A5); //A5 G33
    count++;
    start = millis();
    if(an1_>3 || an1_==0) ledcWrite(4, 0); else ledcWrite(4, 10);
  }

  if (count >= 20) {
    an1 /= count;
    an2 /= count;
    an3 /= count;
    an4 /= count;
    filter();

    a = 4096 / 3.3;
    an1_ = (an1 / a + 0.1) * 32.850; //32.90; //*22.56;
    an2_ = (an2 / a + 0.1) * 23.267; //23.26; //*16.02;
    an3_ = (an3 / a + 0.1) * 16.038; //15.96; //*11.11;
    an4_ = (an4 / a + 0.1) * 7.758;  //7.82; //*5.63;
    an1_ = an1_ - an2_;
    an2_ = an2_ - an3_;
    an3_ = an3_ - an4_;
    count = 0; an1 = 0; an2 = 0; an3 = 0; an4 = 0;
    
    ledcWrite(3, 1);
    string1 = "";
    string2 = "";

    char c[4];
    char str[130];
    Serial2.print(QPIRI);
    string1 = Serial2.readStringUntil('\r');
    string1L = string1.length();
    string1.toCharArray(str, string1L - 2);
    sscanf(str, "%1c %f %f %f %f %f %d %d %f %f %f %f %f %d %d %d %d %d %d %1c %d %d %d %f %d %d",
           &c, &grid_voltage_rating, &grid_current_rating, &out_voltage_rating, &out_freq_rating, &out_current_rating,
           &out_va_rating, &out_watt_rating, &batt_rating, &batt_recharge_voltage, &batt_under_voltage,
           &batt_bulk_voltage, &batt_float_voltage, &batt_type, &max_grid_charge_current, &max_charge_current,
           &in_voltage_range, &out_source_priority, &charger_source_priority, &c, &machine_type, &topology,
           &out_mode, &batt_redischarge_voltage, &c, &c);
    //(230.0 21.7 230.0 50.0 21.7 5000 5000 48.0 44.0 43.6 56.8 54.5 2 20 010 1 1 0 9 01 0 0 54.0 0 1 000 1

    Serial2.print(QPIGS);
    string2 = Serial2.readStringUntil('\r');
    string2L = string2.length();
    string2.toCharArray(str, string2L - 2);
    sscanf(str, "%1c %f %f %f %f %4d %4d %d %d %f %d %d %d %d %f %f %d %8c %2c %2c %d %3c",
           &c, &voltage_grid, &freq_grid, &voltage_out, &freq_out, &load_va, &load_watt, &load_percent,
           &voltage_bus, &voltage_batt, &batt_charge_current, &batt_capacity, &temp_heatsink,
           &pv_input_current, &pv_input_voltage, &scc_voltage, &batt_discharge_current, &device_status, &c, &c, 
           &pv_input_watts, &device_status2);
    //(239.5 49.9 230.0 49.9 0690 0625 013 379 54.50 000 100 0030 0004 075.6 54.49 00000 00010111 00 00 00217 110
    
    ledcWrite(3, 0);

    if (!client.connected()) reconnect();
    Publish();
  }
  client.loop();
}

void WiFi_con() {
  int nn=0;
  // Connect to WiFi network
  WiFi.begin(ssid, password);
  //Serial.println("WiFi connect ");
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    nn++;
    ledcWrite(2, 10);
    delay(100);
    ledcWrite(2, 0);
    delay(400);
    Serial.print(".");
    if (nn > 20) ESP.restart(); //break;
  }
  if (WiFi.status() == WL_CONNECTED) reconnect();
  ledcWrite(2, 1);
}

void callback(char* topic, byte* payload, unsigned int length) {
  if (client.connected()) {
    // Действие при получении комманды
    String messageTemp;
    for (int i = 0; i < length; i++)
      messageTemp += (char)payload[i];

    if (String(topic) == "ha/ESP_Voltronic/com"){ 
      if (messageTemp == "USB") POP(0);                               //USB, SUB, SBU
      if (messageTemp == "SUB") POP(1);  
      if (messageTemp == "SBU") POP(2); 
      if (messageTemp == "SBL+") PCP(0);                              //SBL+, SBL-, SLB+, SLB-
      if (messageTemp == "SBL-") PCP(1); 
      if (messageTemp == "SLB+") PCP(2); 
      if (messageTemp == "SLB-") PCP(3); 
    }
    if (String(topic) == "homeassistant/status" && messageTemp == "online") PubConfig();
  }
}

void Publish() {
  ledcWrite(2, 30);
  if (an1_ >3) {
    snprintf (msg, MSG_BUFFER_SIZE, "%.2f", an1_);
    client.publish("ha/ESP_Voltronic/Battery1", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%.2f", an2_);
    client.publish("ha/ESP_Voltronic/Battery2", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%.2f", an3_);
    client.publish("ha/ESP_Voltronic/Battery3", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%.2f", an4_);
    client.publish("ha/ESP_Voltronic/Battery4", msg);
  }

  if (voltage_batt != 0) {
    snprintf (msg, MSG_BUFFER_SIZE, "%.1f", voltage_grid);
    client.publish("ha/ESP_Voltronic/Inv_AC_voltage", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%i", load_watt);
    client.publish("ha/ESP_Voltronic/Inv_Output_power", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%.1f", voltage_batt);
    client.publish("ha/ESP_Voltronic/Inv_Battery_voltage", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%i", batt_capacity);
    client.publish("ha/ESP_Voltronic/Inv_Battery_capacity", msg);
    if(batt_capacity_m>0){
      snprintf (msg, MSG_BUFFER_SIZE, "%i", batt_capacity_m);
      client.publish("ha/ESP_Voltronic/Inv_Battery_capacity_M", msg);
    }
    snprintf (msg, MSG_BUFFER_SIZE, "%i", batt_charge_current);
    client.publish("ha/ESP_Voltronic/Inv_Battery_charging", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%i", batt_discharge_current);
    client.publish("ha/ESP_Voltronic/Inv_Battery_discharging", msg);

    snprintf (msg, MSG_BUFFER_SIZE, "%i", pv_input_current);
    client.publish("ha/ESP_Voltronic/PV_Input_current", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%.1f", pv_input_voltage);
    client.publish("ha/ESP_Voltronic/PV_Input_voltage", msg);
    snprintf (msg, MSG_BUFFER_SIZE, "%i", pv_input_watts);
    client.publish("ha/ESP_Voltronic/PV_Charging_power", msg);

    if (out_source_priority==0) snprintf (msg, MSG_BUFFER_SIZE, "USB");
      else if(out_source_priority==1)   snprintf (msg, MSG_BUFFER_SIZE, "SUB");
        else snprintf (msg, MSG_BUFFER_SIZE, "SBU");
    client.publish("ha/ESP_Voltronic/Out_priority", msg);  
    if(charger_source_priority==0) snprintf (msg, MSG_BUFFER_SIZE, "SBL+");
      else if(charger_source_priority==1)   snprintf (msg, MSG_BUFFER_SIZE, "SBL-");
        else if(charger_source_priority==2)   snprintf (msg, MSG_BUFFER_SIZE, "SLB+");
          else snprintf (msg, MSG_BUFFER_SIZE, "SLB-");
    client.publish("ha/ESP_Voltronic/Charger_priority", msg);  
  }

  snprintf (msg, MSG_BUFFER_SIZE, "%s",uptime);
  client.publish("ha/ESP_Voltronic/Uptime", msg);  
  snprintf (msg, MSG_BUFFER_SIZE, "%s", WiFi.SSID());
  client.publish("ha/ESP_Voltronic/SSID", msg);
  snprintf (msg, MSG_BUFFER_SIZE, "%i", WiFi.RSSI());
  client.publish("ha/ESP_Voltronic/RSSI", msg);
  ledcWrite(2, 1);
}

void reconnect() {
  // Loop until we're reconnected
  if (!client.connected()) {
    // Attempt to connect
    ledcWrite(2, 5);
    if (client.connect("ESP_Voltronic", "mqtt", "mqtt")) {
      //ledcWrite(2, 5);
      // Публикации при подключении
      PubConfig();
      ledcWrite(2, 0);
      // ... и подписки
      client.subscribe("ha/ESP_Voltronic/com");
      client.subscribe("homeassistant/status");
    }
  }
}

void PubConfig() {
  String jsonDev, jsonSen;

  //Device
  jsonDev = "\"device\": {\"connections\": [[\"mac\", \"cc:50:e3:b6:34:3c\"]],\"identifiers\":[\"MQTT_ESP_Voltronic\"],\"name\": \"ESP_Voltronic\",\"sw_version\": \"" + DevVer +"\",\"model\": \"ESP32\",\"manufacturer\":\"NodeMCU-32S\"}}";

  //Setting device output source priority
  jsonSen = "{\"icon\": \"mdi:solar-power-variant\", \"name\": \"Voltronic_Out_priority\",\"state_topic\": \"ha/ESP_Voltronic/Out_priority\",\"command_topic\": \"ha/ESP_Voltronic/com\",\"unique_id\": \"ESP_Voltronic_Out_priority\",\"options\": [\"USB\", \"SUB\", \"SBU\"]," + jsonDev;
  client.beginPublish("homeassistant/select/ESP_Voltronic/Out_priority/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Setting device charger priority
  jsonSen = "{\"icon\": \"mdi:solar-power-variant\", \"name\": \"Voltronic_Charger_priority\",\"state_topic\": \"ha/ESP_Voltronic/Charger_priority\",\"command_topic\": \"ha/ESP_Voltronic/com\",\"unique_id\": \"ESP_Voltronic_Charger_priority\",\"options\": [\"SBL+\", \"SBL-\", \"SLB+\", \"SLB-\"]," + jsonDev;
  client.beginPublish("homeassistant/select/ESP_Voltronic/Charger_priority/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();
  
  //Battery1
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\",\"icon\": \"mdi:car-battery\", \"name\": \"Voltronic_Battery1\",\"state_topic\": \"ha/ESP_Voltronic/Battery1\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Battery1\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Battery1/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery2
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\",\"icon\": \"mdi:car-battery\", \"name\": \"Voltronic_Battery2\",\"state_topic\": \"ha/ESP_Voltronic/Battery2\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Battery2\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Battery2/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery3
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\",\"icon\": \"mdi:car-battery\", \"name\": \"Voltronic_Battery3\",\"state_topic\": \"ha/ESP_Voltronic/Battery3\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Battery3\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Battery3/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery4
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\",\"icon\": \"mdi:car-battery\", \"name\": \"Voltronic_Battery4\",\"state_topic\": \"ha/ESP_Voltronic/Battery4\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Battery4\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Battery4/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //SSID
  jsonSen = "{\"device_class\": \"signal_strength\",\"name\": \"Voltronic_SSID\",\"state_topic\": \"ha/ESP_Voltronic/SSID\",\"unique_id\": \"ESP_Voltronic_SSID\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/SSID/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //RSSI
  jsonSen = "{\"device_class\": \"signal_strength\",\"state_class\": \"measurement\",\"name\": \"Voltronic_RSSI\",\"state_topic\": \"ha/ESP_Voltronic/RSSI\",\"unit_of_measurement\": \"dBm\",\"unique_id\": \"ESP_Voltronic_RSSI\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/RSSI/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Grid voltage Напряжение сети
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_AC_voltage\",\"state_topic\": \"ha/ESP_Voltronic/Inv_AC_voltage\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Inv_AC_voltage\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_AC_voltage/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //AC output active power Нагрузка
  jsonSen = "{\"device_class\": \"power\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_Output_power\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Output_power\",\"unit_of_measurement\": \"W\",\"unique_id\": \"ESP_Voltronic_Inv_Output_power\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Output_power/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery voltage Напряжение батареи
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\",\"icon\": \"mdi:car-battery\", \"name\": \"Voltronic_Inv_Battery_voltage\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Battery_voltage\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_Inv_Battery_voltage\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Battery_voltage/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery capacity Заряд батареи
  jsonSen = "{\"device_class\": \"battery\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_Battery_capacity\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Battery_capacity\",\"unit_of_measurement\": \"%\",\"unique_id\": \"ESP_Voltronic_Inv_Battery_capacity\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Battery_capacity/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery capacity Заряд батареи модифицированный
  jsonSen = "{\"device_class\": \"battery\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_Battery_capacity_M\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Battery_capacity_M\",\"unit_of_measurement\": \"%\",\"unique_id\": \"ESP_Voltronic_Inv_Battery_capacity_M\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Battery_capacity_M/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery charging current Ток заряда батареи
  jsonSen = "{\"device_class\": \"current\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_Battery_charging\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Battery_charging\",\"unit_of_measurement\": \"A\",\"unique_id\": \"ESP_Voltronic_Inv_Battery_charging\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Battery_charging/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //Battery discharge current Ток разряда батареи
  jsonSen = "{\"device_class\": \"current\",\"state_class\": \"measurement\", \"name\": \"Voltronic_Inv_Battery_discharging\",\"state_topic\": \"ha/ESP_Voltronic/Inv_Battery_discharging\",\"unit_of_measurement\": \"A\",\"unique_id\": \"ESP_Voltronic_Inv_Battery_discharging\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Inv_Battery_discharging/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //PV Input voltage Напряжение PV
  jsonSen = "{\"device_class\": \"voltage\",\"state_class\": \"measurement\", \"name\": \"Voltronic_PV_Input_voltage\",\"state_topic\": \"ha/ESP_Voltronic/PV_Input_voltage\",\"unit_of_measurement\": \"V\",\"unique_id\": \"ESP_Voltronic_PV_Input_voltage\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/PV_Input_voltage/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //PV Input current Ток PV
  jsonSen = "{\"device_class\": \"current\",\"state_class\": \"measurement\", \"name\": \"Voltronic_PV_Input_current\",\"state_topic\": \"ha/ESP_Voltronic/PV_Input_current\",\"unit_of_measurement\": \"A\",\"unique_id\": \"ESP_Voltronic_PV_Input_current\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/PV_Input_current/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();

  //PV Charging power Мощность PV
  jsonSen = "{\"device_class\": \"power\",\"state_class\": \"measurement\", \"name\": \"Voltronic_PV_Charging_power\",\"state_topic\": \"ha/ESP_Voltronic/PV_Charging_power\",\"unit_of_measurement\": \"W\",\"unique_id\": \"ESP_Voltronic_PV_Charging_power\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/PV_Charging_power/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();
  
  //Uptime
  jsonSen = "{\"icon\": \"mdi:timer-outline\", \"name\": \"Voltronic_Uptime\",\"state_topic\": \"ha/ESP_Voltronic/Uptime\",\"unique_id\": \"ESP_Voltronic_Uptime\"," + jsonDev;
  client.beginPublish("homeassistant/sensor/ESP_Voltronic/Uptime/config", jsonSen.length(), false);
  client.print(jsonSen); client.endPublish();
}

void filter() {
  const int sr = 7;

  if (ao1 == 0) {
    ao1 = an1; ao2 = an2; ao3 = an3; ao4 = an4;
    return;
  }
  if (ao1 + sr < an1) ao1 = an1 - sr;
  if (ao1 - sr > an1) ao1 = an1 + sr;
  if (ao2 + sr < an2) ao2 = an2 - sr;
  if (ao2 - sr > an2) ao2 = an2 + sr;
  if (ao3 + sr < an3) ao3 = an3 - sr;
  if (ao3 - sr > an3) ao3 = an3 + sr;
  if (ao4 + sr < an4) ao4 = an4 - sr;
  if (ao4 - sr > an4) ao4 = an4 + sr;

  an1 = ao1;
  an2 = ao2;
  an3 = ao3;
  an4 = ao4;
}

bool handleFileRead(String path) {                                      // Функция работы с файловой системой
  if (path.endsWith("/")) {                                             // Если устройство вызывается по корневому адресу, то должен вызываться файл index.html (добавляем его в конец адреса)
    path += "index.html";
    PubConfig();
  }
  String contentType = getContentType(path);                            // С помощью функции getContentType (описана ниже) определяем по типу файла (в адресе обращения) какой заголовок необходимо возвращать по его вызову
  if (SPIFFS.exists(path)) {                                            // Если в файловой системе существует файл по адресу обращения
    File file = SPIFFS.open(path, "r");                                 //  Открываем файл для чтения
    size_t sent = server.streamFile(file, contentType);                 //  Выводим содержимое файла по HTTP, указывая заголовок типа содержимого contentType
    file.close();                                                       //  Закрываем файл
    return true;                                                        //  Завершаем выполнение функции, возвращая результатом ее исполнения true (истина)
  }
  return false;                                                         // Завершаем выполнение функции, возвращая результатом ее исполнения false (если не обработалось предыдущее условие)
}

String getContentType(String filename) {                                // Функция, возвращающая необходимый заголовок типа содержимого в зависимости от расширения файла
  if (filename.endsWith(".html")) return "text/html";                   // Если файл заканчивается на ".html", то возвращаем заголовок "text/html" и завершаем выполнение функции
  else if (filename.endsWith(".css")) return "text/css";                // Если файл заканчивается на ".css", то возвращаем заголовок "text/css" и завершаем выполнение функции
  else if (filename.endsWith(".js")) return "application/javascript";   // Если файл заканчивается на ".js", то возвращаем заголовок "application/javascript" и завершаем выполнение функции
  else if (filename.endsWith(".png")) return "image/png";               // Если файл заканчивается на ".png", то возвращаем заголовок "image/png" и завершаем выполнение функции
  else if (filename.endsWith(".jpg")) return "image/jpeg";              // Если файл заканчивается на ".jpg", то возвращаем заголовок "image/jpg" и завершаем выполнение функции
  else if (filename.endsWith(".gif")) return "image/gif";               // Если файл заканчивается на ".gif", то возвращаем заголовок "image/gif" и завершаем выполнение функции
  else if (filename.endsWith(".ico")) return "image/x-icon";            // Если файл заканчивается на ".ico", то возвращаем заголовок "image/x-icon" и завершаем выполнение функции
  return "text/plain";                                                  // Если ни один из типов файла не совпал, то считаем что содержимое файла текстовое, отдаем соответствующий заголовок и завершаем выполнение функции
}

String out() {
  String json;
  //json.reserve(140);
    json = "{\n\"batt1\":";
    json += an1_;
    json += ", \"batt2\":";
    json += an2_;
    json += ", \"batt3\":";
    json += an3_;
    json += ", \"batt4\":";
    json += an4_;
    json += ", \n\"voltage_batt\":";
    json += voltage_batt;
    json += ", \"batt_capacity_m\":";

    //==================== batt_capacity_m
    /*if(batt_discharge_current>0){
      if(voltage_batt>54) batt_capacity_m=100;
      if(voltage_batt<=54) batt_capacity_m=99;
      if(voltage_batt<52) batt_capacity_m=98;
      if(voltage_batt<51.3){
        batt_capacity_m=sqrt(float(batt_discharge_current)/12);
        batt_capacity_m=float(voltage_batt+batt_capacity_m-batt_under_voltage)/(batt_float_voltage-3-batt_under_voltage)*100;
      }
    }
    if(batt_charge_current>0){
      batt_capacity_m=sqrt(float(batt_charge_current)/12)-3;
      batt_capacity_m=float(voltage_batt-batt_capacity_m-batt_under_voltage)/(batt_bulk_voltage+3-batt_under_voltage)*100;
    }*/
    //==================== batt_capacity_m
    
    if(voltage_batt<54 && batt_discharge_current>0) batt_capacity_m=(voltage_batt-batt_under_voltage+batt_discharge_current/20)/(51.5-batt_under_voltage)*100;
      else batt_capacity_m=batt_capacity;
    if(batt_capacity_m>100) batt_capacity_m=100;
    json += batt_capacity_m;

    json += ", \n\n\"batt_under_voltage\":";
    json += batt_under_voltage;
    json += ", \"batt_recharge_voltage\":";
    json += batt_recharge_voltage;
    json += ", \"batt_redischarge_voltage\":";
    json += batt_redischarge_voltage;
    json += ", \"batt_float_voltage\":";
    json += batt_float_voltage;
    json += ", \"batt_bulk_voltage\":";
    json += batt_bulk_voltage;
    json += ", \n\"max_grid_charge_current\":";
    json += max_grid_charge_current;
    json += ", \"max_charge_current\":";
    json += max_charge_current;
    json += ", \"batt_charge_current\":";
    json += batt_charge_current;
    json += ", \"batt_discharge_current\":";
    json += batt_discharge_current;

    json += ", \n\n\"pv_input_voltage\":";
    json += pv_input_voltage;
    json += ", \"pv_input_current\":";
    json += pv_input_current;
    json += ", \"pv_input_watts\":";
    json += pv_input_watts;
    
    json += ", \n\n\"voltage_grid\":";
    json += voltage_grid;
    json += ", \"freq_grid\":";
    json += freq_grid;
    json += ", \"voltage_out\":";
    json += voltage_out;
    json += ", \"freq_out\":";
    json += freq_out;
    json += ", \n\"load_va\":";
    json += load_va;
    json += ", \"load_watt\":";
    json += load_watt;
    json += ", \"load_percent\":";
    json += load_percent;
    json += ", \"batt_capacity\":";
    json += batt_capacity;

    json += ", \n\"batt_type\":\"";
    if(batt_type==0) json += "AGM";
      else if(batt_type==1) json += "Flooded";
        else json += "User";
    json += "\", \"out_source_priority\":\"";
    if(out_source_priority==0) json += "USB";
      else if(out_source_priority==1) json += "SUB";
        else json += "SBU";
    json += "\", \"charger_source_priority\":\"";
    if(charger_source_priority==0) json += "SBL+";
      else if(charger_source_priority==1) json += "SBL-";
        else if(charger_source_priority==2) json += "SLB+";
          else json += "SLB-";
    json += "\", \n\"voltage_bus\":";
    json += voltage_bus;
    json += ", \"temp_heatsink\":";
    json += temp_heatsink;
    json += ", \"scc_voltage\":";
    json += scc_voltage;
    json += ", \"device_status\":\"";
    json += device_status;
    json += "\", \"device_status2\":\"";
    json += device_status2;

    json += "\", \n\n\"QPIRI\":\"";
    json += string1.substring(0,string1L-2);
    json += "\", \"str1L\":";
    json += string1L;
    json += ", \n\"QPIGS\":\"";
    json += string2.substring(0,string2L-2);
    json += "\", \"str2L\":";
    json += string2L;
    json += ", \n\"POP\":\"";
    json += pop.substring(0,popL-2);
    json += "\", \"popL\":";
    json += popL;
    json += ", \n\"PCP\":\"";
    json += pcp.substring(0,pcpL-2);
    json += "\", \"pcpL\":";
    json += pcpL;
    
    json += ", \n\n\"Uptime\":\"";
    json += uptime;
    json += "\", \n\"time\":";
    json += millis()/1000;
    
    json += "\n}";

  return json;
}

void POP(int ar){
  pop = "";
  if(ar==0) Serial2.print(POP_USB);
  if(ar==1) Serial2.print(POP_SUB);  
  if(ar==2) Serial2.print(POP_SBU);  
  pop = Serial2.readStringUntil('\r');
  popL = pop.length();
}

void PCP(int ar){
  pcp = "";
  if(ar==0) Serial2.print(PCP_SBLe);
  if(ar==1) Serial2.print(PCP_SBLe);
  if(ar==2) Serial2.print(PCP_SLBe);  
  if(ar==3) Serial2.print(PCP_SLBe);  
  pcp = Serial2.readStringUntil('\r');
  pcpL = pcp.length();
}
