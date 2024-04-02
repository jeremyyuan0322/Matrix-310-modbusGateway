// #define ESP32
#include "inc/Artila-Matrix310.h"
#include "inc/version.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h> //https://github.com/me-no-dev/ESPAsyncWebServer
#include <AsyncTCP.h>
#include <SD.h>
#include <SPIFFS.h> //https://github.com/me-no-dev/AsyncTCP
#include <FS.h>
// #include <ESPmDNS.h>
#include "HardwareSerial.h"
// #include <Ethernet.h>
#include "Ethernet.h"
// Modbus bridge include
#include "ModbusBridgeEthernet.h"
#include "ModbusBridgeWiFi.h"
// Modbus RTU client include
#include "ModbusClientRTU.h"

// #ifndef LOCAL_LOG_LEVEL
// #define LOCAL_LOG_LEVEL LOG_LEVEL_VERBOSE
// #endif

// #define LOG_TERM_NOCOLOR // coloring terminal output doesn't work with ArduinoIDE and PlatformIO
// #include "Logging.h"

// userName,userPwd,deviceName,netInterface,wifiSetFin,ssid,staPwd,wifiMode,wifiIp,wifiNetmask,wifiGateway,ethSetFin,ethMode,ethIp,ethNetmask,ethGateway,serialSetFin,baud,parityBit,dataBit,stopBit,rtuTimeout,bridgeSetFin,port,uidMin,uidMax,tcpTimeout
// admin,admin,Matrix-310,0,0,,,0,192.168.4.1,255.255.255.0,192.168.4.1,0,1,192.168.2.127,255.255.255.0,192.168.2.1,0,9600,0,8,1,3000,0,502,1,247,0
String userName = "admin";
String userPwd = "admin";
String deviceName = "Matrix-310";
int netInterface = 0; // 0: wifi, 1: ethernet
bool wifiSetFin = 0;  // 0: haven't set wifi, 1: wifi setting is finished
char ssid[20] = "";
char staPwd[20] = "";
bool wifiMode = 0; // 0:dhcp, 1:static
IPAddress wifiIp(192, 168, 4, 127);
IPAddress wifiNetmask(255, 255, 255, 0);
IPAddress wifiGateway(192, 168, 4, 1); // 指定網關
bool ethSetFin = 0;                    // 0: haven't set ethernet, 1: ethernet setting is finished
bool ethMode = 1;                      // 0:dhcp, 1:static
IPAddress ethIp(192, 168, 2, 127);
IPAddress ethNetmask(255, 255, 255, 0);
IPAddress ethGateway(192, 168, 2, 1); // 指定網關
int serialSetFin = 0;
int baud = 9600;
int parityBit = 0; // 0: none, 1: odd, 2: even
int dataBit = 8;
int stopBit = 1;
int rtuTimeout = 1000;
int bridgeSetFin = 0;
int port = 502;
int uidMin = 1;
int uidMax = 247;
int tcpTimeout = 0;
bool hasEthMac = false;

enum INTERFACE
{
  WIFI_DHCP = 0,
  WIFI_STATIC = 1,
  ETH_DHCP = 2,
  ETH_STATIC = 3,
  NO_NETWORK = 4,
} CURRENT_NETINTERFACE = NO_NETWORK;
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
// Create a ModbusRTU client instance
ModbusClientRTU MB(COM1_RTS); // what is the second parareter: queue limit?
Error MB_ERROR1;
// Create bridge
ModbusBridgeWiFi MBbridgeWifi;
ModbusBridgeEthernet MBbridgeEth;

TaskHandle_t handleSetting, handleWifiGateway, handleEthGateway, handlePrintHeap, handleReadyLed;

int settingStackSize = 3000;
int gatewayStackSize = 3000;
int printHeapStackSize = 2000;
int readyLedStackSize = 1000;

SPIClass spi = SPIClass();
void multiTaskCreate()
{
  if (hasEthMac == true)
  {
    Serial.println("System ready");
    digitalWrite(LED_READY, HIGH);
  }
  else
  {
    Serial.println("Error: No Ethernet MAC address");
    xTaskCreate(readyLedBlink, "Ready led task", readyLedStackSize, NULL, 1, &handleReadyLed);
  }
  xTaskCreate(settingMode, "Setting mode task", settingStackSize, NULL, 2, &handleSetting);
  if (CURRENT_NETINTERFACE == ETH_DHCP || CURRENT_NETINTERFACE == ETH_STATIC)
  {
    Serial.println("start eth gateway");
    xTaskCreate(ethGatewayMode, "Eth Gateway task", gatewayStackSize, NULL, 2, &handleEthGateway);
  }
  else
  {
    Serial.println("start wifi gateway");
    digitalWrite(LED_WIFI, HIGH);
    xTaskCreate(wifiGatewayMode, "Wifi Gateway task", gatewayStackSize, NULL, 2, &handleWifiGateway);
  }
  // xTaskCreate(printHeapSize, "Heap size task", printHeapStackSize, NULL, 1, &handlePrintHeap);
}
void readyLedBlink(void *pvParam)
{
  while (1)
  {
    digitalWrite(LED_READY, HIGH);
    delay(1000);
    digitalWrite(LED_READY, LOW);
    delay(1000);
    yield();
  }
}
void printHeapSize(void *pvParam)
{
  int heapSizeCount = 0;
  while (1)
  {
    Serial.printf("\n%d. Free heap size: %d\n", heapSizeCount, ESP.getFreeHeap());
    int usedHeap = ESP.getHeapSize() - ESP.getFreeHeap();
    Serial.printf("%d. Used Heap Size: %d\n", heapSizeCount, usedHeap);
    UBaseType_t settingUsedStack = settingStackSize - uxTaskGetStackHighWaterMark(handleSetting);
    UBaseType_t wifiGatewayUsedStack = gatewayStackSize - uxTaskGetStackHighWaterMark(handleWifiGateway);
    UBaseType_t ethGatewayUsedStack = gatewayStackSize - uxTaskGetStackHighWaterMark(handleEthGateway);
    UBaseType_t heapUsedStack = printHeapStackSize - uxTaskGetStackHighWaterMark(handlePrintHeap);
    Serial.printf("%d. Setting mode used stack: %d\n", heapSizeCount, settingUsedStack);
    Serial.printf("%d. WiFi Gateway mode used stack: %d\n", heapSizeCount, wifiGatewayUsedStack);
    Serial.printf("%d. Eth Gateway mode used stack: %d\n", heapSizeCount, ethGatewayUsedStack);
    Serial.printf("%d. Heap size used stack: %d\n", heapSizeCount, heapUsedStack);
    heapSizeCount++;
    vTaskDelay(10000 / portTICK_PERIOD_MS);
    yield();
  }
}
void settingMode(void *pvParam)
{
  // Add service to MDNS-SD
  // MDNS.addService("http", "tcp", 80);
  setupRouting();
  while (1)
  {
    vTaskDelay(1 / portTICK_PERIOD_MS);
    yield();
  }
}
/*
void handleData(ModbusMessage response, uint32_t token)
{
  MBUlogLvl = LOG_LEVEL_VERBOSE; // Set log level for ModbusClient
  Serial.printf("Response: serverID=%d, FC=%d, Token=%08X, length=%d:\n", response.getServerID(), response.getFunctionCode(), token, response.size());
  for (auto &byte : response)
  {
    Serial.printf("%02X ", byte);
  }
  Serial.println("");
}

// Define an onError handler function to receive error responses
// Arguments are the error code returned and a user-supplied token to identify the causing request
void handleError(Error error, uint32_t token)
{
  MBUlogLvl = LOG_LEVEL_VERBOSE; // Set log level for ModbusClient
  // ModbusError wraps the error code and provides a readable error message for it
  ModbusError me(error);
  Serial.printf("Error response: %02X - %s\n", (int)me, (const char *)me);
}
*/
void wifiGatewayMode(void *pvParam)
{
  MBUlogLvl = LOG_LEVEL_VERBOSE; // Set log level for ModbusClient
  // MDNS.addService("http", "tcp", port);
  RTUutils::prepareHardwareSerial(Serial2);
  // ModbusClientRTU already set pinMode(MR_rtsPin, OUTPUT);
  setSerial();
  // MB.onDataHandler(&handleData);
  // MB.onErrorHandler(&handleError);
  // Set RTU Modbus message timeout to 2000ms
  MB.setTimeout(rtuTimeout);
  // Start ModbusRTU background task on core 1
  MB.begin(Serial2, 1);
  // Define and start WiFi bridge: Modbus slave ID, 1-247, any Modbus function code
  for (uint8_t i = uidMin; i <= uidMax; i++)
  {
    MBbridgeWifi.attachServer(i, i, ANY_FUNCTION_CODE, &MB);
  }
  // Check: print out all combinations served to Serial
  // MBbridgeWifi.listServer();

  // Start the bridge. Port 502, 4 simultaneous clients allowed, 600ms inactivity to disconnect client, 0ms means never disconnect
  MBbridgeWifi.start(port, 4, tcpTimeout);

  Serial.printf("Use the shown IP and port %d to send requests!\n", port);

  // Your output on the Serial monitor should start with:
  //      __ OK __
  //      .IP address: 192.168.178.74
  //      [N] 1324| ModbusServer.cpp     [ 127] listServer: Server   4:  00 06
  //      [N] 1324| ModbusServer.cpp     [ 127] listServer: Server   5:  03 04
  //      Use the shown IP and port 502 to send requests!
  while (1)
  {
    vTaskDelay(1 / portTICK_PERIOD_MS);
    yield();
  }
}

void ethGatewayMode(void *pvParam)
{
  MBUlogLvl = LOG_LEVEL_VERBOSE; // Set log level for ModbusClient
  // MDNS.addService("http", "tcp", port);
  RTUutils::prepareHardwareSerial(Serial2);
  // ModbusClientRTU already set pinMode(MR_rtsPin, OUTPUT);
  setSerial();
  // Set RTU Modbus message timeout to 2000ms
  MB.setTimeout(rtuTimeout);
  // Start ModbusRTU background task on core 1
  MB.begin(Serial2, 1);
  // Define and start WiFi bridge: Modbus slave ID, 1-247, any Modbus function code
  for (uint8_t i = uidMin; i <= uidMax; i++)
  {
    MBbridgeEth.attachServer(i, i, ANY_FUNCTION_CODE, &MB);
  }
  // Check: print out all combinations served to Serial
  // MBbridgeEth.listServer();

  // Start the bridge. Port 502, 4 simultaneous clients allowed, 600ms inactivity to disconnect client, 0ms means never disconnect
  MBbridgeEth.start(port, 4, tcpTimeout);

  Serial.printf("Use the shown IP and port %d to send requests!\n", port);

  // Your output on the Serial monitor should start with:
  //      __ OK __
  //      .IP address: 192.168.178.74
  //      [N] 1324| ModbusServer.cpp     [ 127] listServer: Server   4:  00 06
  //      [N] 1324| ModbusServer.cpp     [ 127] listServer: Server   5:  03 04
  //      Use the shown IP and port 502 to send requests!
  while (1)
  {
    vTaskDelay(1 / portTICK_PERIOD_MS);
    yield();
  }
}

void setupRouting()
{
  server.serveStatic("/", SPIFFS, "/www/").setDefaultFile("index.html");
  server.serveStatic("/settings", SPIFFS, "/www/setting.html");

  server.on("/login", HTTP_GET, [](AsyncWebServerRequest *request){
    if(!request->authenticate(userName.c_str(), userPwd.c_str())){
      Serial.println("login fail");
      // return request->requestAuthentication();
      // request->redirect("/");
      request->send(401, "text/plain", "Unauthorized");
    }
    Serial.println("login Success");
    request->send(200, "text/plain", "Login Success!"); });

  server.on("/wifi", HTTP_POST, [](AsyncWebServerRequest *request){
    String response;
    String ssid_str;
    String staPwd_str;
    Serial.println("try to set wifi");
    if (request->hasParam("ssid", true))
    {
      ssid_str = request->getParam("ssid", true)->value();
      Serial.printf("ssid length: %d\n", ssid_str.length());
      strcpy(ssid, ssid_str.c_str());
      Serial.print("SSID: ");
      Serial.println(ssid);
    }
    if (request->hasParam("staPwd", true))
    {
      staPwd_str = request->getParam("staPwd", true)->value();
      Serial.printf("staPwd length: %d\n", staPwd_str.length());
      strcpy(staPwd, staPwd_str.c_str());
      Serial.print("staPwd: ");
      Serial.println(staPwd);
    }
    if (request->hasParam("wifiMode", true))
    {
      wifiMode = request->getParam("wifiMode", true)->value().toInt();
      Serial.printf("wifiMode: %d\n", wifiMode);
      if (wifiMode == 1)
      {
        if (request->hasParam("wifiIp", true))
        {
          wifiIp.fromString(request->getParam("wifiIp", true)->value());
          Serial.printf("wifiIp: %s\n", wifiIp.toString().c_str());
        }
        if (request->hasParam("wifiNetmask", true))
        {
          wifiNetmask.fromString(request->getParam("wifiNetmask", true)->value());
          Serial.printf("wifiNetmask: %s\n", wifiNetmask.toString().c_str());
        }
        if (request->hasParam("wifiGateway", true))
        {
          wifiGateway.fromString(request->getParam("wifiGateway", true)->value());
          Serial.printf("wifiGateway: %s\n", wifiGateway.toString().c_str());
        }
      }
    }
    if (wifiMode == 0){
      response = "The DHCP setup for the WiFi has been completed.\n";
    }
    else{
      response = "The static IP setup for the Ethernet has been completed.\n";
    }
    response += "SSID: " + String(ssid) + "\n" + "Please click on 'reboot' to apply the settings.";
    Serial.println(response);
    request->send(200, "text/plain", response);
    wifiSetFin = 1;
    netInterface = 0;
    ethSetFin = 0;
    writeToCSV(SPIFFS, "/setting.csv"); });

  server.on("/eth", HTTP_POST, [](AsyncWebServerRequest *request){
    String response;
    Serial.println("try to set ethernet");
    if(request->hasParam("ethMode"), true){
      Serial.printf("ethMode str\n");
      ethMode = request->getParam("ethMode", true)->value().toInt();
      Serial.printf("ethMode: %d\n", ethMode);
      if(ethMode == 1){
        if(request->hasParam("ethIp"), true){
          ethIp.fromString(request->getParam("ethIp", true)->value());
          Serial.printf("ethIp: %s\n", ethIp.toString().c_str());
        }
        if(request->hasParam("ethNetmask"), true){
          ethNetmask.fromString(request->getParam("ethNetmask", true)->value());
          Serial.printf("ethNetmask: %s\n", ethNetmask.toString().c_str());
        }
        if(request->hasParam("ethGateway"), true){
          ethGateway.fromString(request->getParam("ethGateway", true)->value());
          Serial.printf("ethGateway: %s\n", ethGateway.toString().c_str());
        }
      }
    }
    if (ethMode == 0){
      response = "The DHCP setup for the Ethernet has been completed.\n";
    }
    else{
      response = "The static IP setup for the Ethernet has been completed.\n";
    }
    response += "Please click on 'reboot' to apply the settings.";
    Serial.println(response);
    request->send(200, "text/plain", response);
    ethSetFin = 1;
    netInterface = 1;
    wifiSetFin = 0;
    writeToCSV(SPIFFS, "/setting.csv"); });

  // Serial setting route
  server.on("/serial", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // 取得表單的值並將他們設為相對應的變數
        // 檢查每一個參數並儲存他們
        if(p->name() == "baud"){
          baud = p->value().toInt();
          Serial.printf("baud: %d\n", baud);
        }else if(p->name() == "parityBit"){
          parityBit = p->value().toInt();
          Serial.printf("parityBit: %d\n", parityBit);
        }else if(p->name() == "dataBit"){
          dataBit = p->value().toInt();
          Serial.printf("dataBit: %d\n", dataBit);
        }else if(p->name() == "stopBit"){
          stopBit = p->value().toInt();
          Serial.printf("stopBit: %d\n", stopBit);
        }
        else if(p->name() == "rtuTimeout"){
          rtuTimeout = p->value().toInt();
          Serial.printf("rtuTimeout: %d\n", rtuTimeout);
        }
      }
    }
    request->send(200, "text/plain", "Serial settings updated.\nPlease click on 'reboot' to apply the settings.");
    // 設定成功後將變數儲存到flash裡面的setting.csv
    serialSetFin = 1;
    writeToCSV(SPIFFS, "/setting.csv"); });

  // Modbus Gateway setting route
  server.on("/bridge", HTTP_POST, [](AsyncWebServerRequest *request){
    int params = request->params();
    for(int i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isPost()){
        // 取得表單的值並將他們設為相對應的變數
        // 檢查每一個參數並儲存他們
        if(p->name() == "port"){
          port = p->value().toInt();
          Serial.printf("port: %d\n", port);
        }
        // uidMin,uidMax,tcpTimeout
        else if(p->name() == "uidMin"){
          uidMin = p->value().toInt();
          Serial.printf("uidMin: %d\n", uidMin);
        }
        else if(p->name() == "uidMax"){
          uidMax = p->value().toInt();
          Serial.printf("uidMax: %d\n", uidMax);
        }
        else if(p->name() == "tcpTimeout"){
          tcpTimeout = p->value().toInt();
          Serial.printf("tcpTimeout: %d\n", tcpTimeout);
        }
      }
    }
    // 設定成功後將變數儲存到flash裡面的setting.csv
    request->send(200, "text/plain", "Modbus Bridge settings updated.\nPlease click on 'reboot' to apply the settings.");
    bridgeSetFin = 1;
    writeToCSV(SPIFFS, "/setting.csv"); });

  // server.on("/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
  // request->send(200, "text/plain", "upload setting.csv");
  //   }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  //     static File uploadFile;

  //     if (!index) {
  //       uploadFile = SPIFFS.open("/setting.csv", "w");
  //       if (!uploadFile) {
  //         Serial.println("Failed to open file for writing");
  //         return;
  //       }
  //     }

  //     for (size_t i = 0; i < len; i++) {
  //       uploadFile.write(data[i]);
  //     }

  //     if (final) {
  //       uploadFile.close();
  //     }
  //     Serial.printf("upload file: %s\n", filename.c_str());
  //   }
  // );

  // server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request){
  //   Serial.println("download");
  //   request->send(SPIFFS, "/setting.csv", "text/csv", true);
  // });

  server.on("/getSetting", HTTP_GET, [](AsyncWebServerRequest *request){
    String setting = readSetting(SPIFFS, "/setting.csv");
    if (setting == "failed to open file for reading")
    {
      setting = "";
    }
    Serial.println("getSetting!!!");
    Serial.println(setting);
    request->send(200, "text/plain", setting); });
  server.on("/getSystemInfo", HTTP_GET, [](AsyncWebServerRequest *request){
    // software version in version.h
    // software version, wifiStaMac, wifiApMac, ethMac, CURRENT_NETINTERFACE, dhcpIp
    String systemInfo = "";
    String wifiStaMac = WiFi.macAddress();
    String wifiApMac = WiFi.softAPmacAddress();
    String ethMac = getEthMac(SPIFFS, "/mac.txt");
    String dhcpIp = "";
    if (ethMac == "failed to open file for reading")
    {
      ethMac = "ethMacError";
    }
    systemInfo = "v" + version + "," + wifiStaMac + "," + wifiApMac + "," + ethMac + "," + String(CURRENT_NETINTERFACE);
    if (CURRENT_NETINTERFACE == WIFI_DHCP){
      dhcpIp = WiFi.localIP().toString();
      if(dhcpIp == "0.0.0.0"){
        dhcpIp = "Failed to obtain IP address from DHCP Server";
      }
      systemInfo = systemInfo + "," + dhcpIp;
    }
    else if (CURRENT_NETINTERFACE == ETH_DHCP){
      dhcpIp = Ethernet.localIP().toString();
      if(dhcpIp == "0.0.0.0"){
        dhcpIp = "Failed to obtain IP address from DHCP Server";
      }
      systemInfo = systemInfo + "," + dhcpIp;
    }
    // other
    else if (CURRENT_NETINTERFACE == NO_NETWORK)
    {
      Serial.println("Please complete the network settings first.");
    }
    else
    {
      Serial.println("using static IP.");
    }
    Serial.println(systemInfo);
    request->send(200, "text/plain", systemInfo); });

  server.on("/changePassword", HTTP_POST, [](AsyncWebServerRequest *request){
    String currentPassword = request->getParam("currentPassword", true)->value();
    String newPassword = request->getParam("newPassword", true)->value();
    String response;
    // Optional: Check if the current password is correct
    // This depends on how you store passwords
    if(currentPassword != userPwd){
      response = "Forbidden: Incorrect password.";
      request->send(403, "text/plain", response);
      return;
    }
    response = "Password changed successfully.\nPlease click on 'reboot' to apply the settings.";
    Serial.println(response);
    request->send(200, "text/plain", response);
    userPwd = newPassword;
    writeToCSV(SPIFFS, "/setting.csv"); });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("reboot now!");
    request->send(200, "text/plain", "Rebooting...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP.restart(); });

  server.onNotFound(notFound);
  server.begin();
}
void notFound(AsyncWebServerRequest *request)
{
  request->send(404, "text/plain", "Not found");
}

bool wifiSta()
{
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  if (wifiMode == 1)
  {
    if (!WiFi.config(wifiIp, wifiGateway, wifiNetmask))
    {
      Serial.println("STA Failed to configure");
      return false;
    }
  }
  staPwd == "" ? WiFi.begin(ssid) : WiFi.begin(ssid, staPwd);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
    if (WiFi.status() == WL_CONNECTED)
    {
      break;
    }
    if (millis() - startTime > 12000)
    {
      Serial.println("\nwifi connect fail");
      return false;
    }
  }
  vTaskDelay(1 / portTICK_PERIOD_MS);
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  if (wifiMode == 1)
  {
    Serial.println("Gateway: ");
    Serial.println(WiFi.gatewayIP());
    Serial.println("Netmask: ");
    Serial.println(WiFi.subnetMask());
  }
  return true;
}
String wifiAP()
{
  const char *apName = deviceName.c_str();
  const char *apPwd = userPwd.c_str();
  // IPAddress ip(192, 168, 4, 2);
  // IPAddress gateway(192, 168, 1, 254);  // 指定網關
  // IPAddress subnet(255, 255, 255, 0);
  String apStatus;
  Serial.println();
  Serial.print("AP name: ");
  Serial.println(apName);
  // WiFi.softAPConfig(ip, ip, subnet);
  // using default AP IP 192.168.4.1
  Serial.print("Setting soft-AP ... ");
  if (apPwd && (strlen(apPwd) > 0 && strlen(apPwd) < 8))
  {
    Serial.println("AP password too short, use deflult password!");
    apPwd = "00000000";
  }
  Serial.println(WiFi.softAP(apName, apPwd) ? "AP Ready" : "AP Failed!");
  Serial.printf("AP Password: %s\n", apPwd);
  apStatus = "AP IP address: " + WiFi.softAPIP().toString();
  Serial.println("AP started");
  return apStatus;
}
bool connectEth()
{
  Ethernet.init(LAN_CS); // pin 5
  vTaskDelay(100 / portTICK_PERIOD_MS);
  byte ethMac[6];
  String ethMacStr = getEthMac(SPIFFS, "/mac.txt");
  if (ethMacStr == "get mac failed")
  {
    Serial.println("get eth mac failed");
    return false;
  }
  sscanf(ethMacStr.c_str(), "%2hhx:%2hhx:%2hhx:%2hhx:%2hhx:%2hhx",
         &ethMac[0], &ethMac[1], &ethMac[2], &ethMac[3], &ethMac[4], &ethMac[5]);
  Serial.print("MAC: ");
  for (int i = 0; i < 6; i++)
  {
    Serial.print(ethMac[i], HEX);
    if (i < 5)
      Serial.print(":");
  }
  Serial.println();
  Ethernet.init(5); // pin 5
  vTaskDelay(100 / portTICK_PERIOD_MS);
  if (ethMode == 0)
  {
    // start the Ethernet connection:
    Serial.println("Initialize Ethernet with DHCP:");
    // Matrix-310 tries connecting the internet with DHCP
    unsigned long dhcpTimeout = 5000;
    // begin(uint8_t *mac, unsigned long timeout, unsigned long responseTimeout)
    if (Ethernet.begin(ethMac, dhcpTimeout) == 0)
    {
      // Fail to use DHCP
      Serial.println("Failed to configure Ethernet using DHCP");
      // Check for Ethernet hardware present
      if (Ethernet.hardwareStatus() == EthernetNoHardware)
      {
        Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      }
      if (Ethernet.linkStatus() == LinkOFF)
      {
        Serial.println("Ethernet cable is not connected.");
      }
      return false;
    }
    else
    { // Matrix310 already connect to the internet
      Serial.print("  DHCP assigned IP ");
      Serial.println(Ethernet.localIP());
    }
  }
  else
  {
    Serial.println("Initialize Ethernet with Static IP:");
    // Matrix-310 tries connecting the internet with Static IP
    // begin(uint8_t *mac, IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet)
    Ethernet.begin(ethMac, ethIp, ethIp, ethGateway, ethNetmask);
    Serial.print("  Static IP ");
    Serial.println(Ethernet.localIP());
  }
  // give the Ethernet shield a second to initialize:
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  return true;
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels)
      {
        listDir(fs, file.path(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}
String getEthMac(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);
  String mac;
  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    mac = "get mac failed";
    return mac;
  }
  while (file.available())
  {
    // read ethernet mac address
    mac = file.readStringUntil('\n');
  }
  file.close();
  return mac;
}

String readSetting(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);
  String setting;
  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    setting = "failed to open file for reading";
    return setting;
  }
  while (file.available())
  {
    file.readStringUntil('\n');
    // 讀取第二行
    setting = file.readStringUntil('\n');
  }
  file.close();
  return setting;
}

void readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available())
  {
    Serial.write(file.read());
  }
  Serial.println();
  file.close();
}

void loadSetting(fs::FS &fs, const char *path)
{
  File file = fs.open(path);

  if (!file || file.isDirectory())
  {
    Serial.println("Failed to open setting.csv for reading");
    return;
  }

  while (file.available())
  {
    // 跳过第一行（标题行）
    file.readStringUntil('\n');
    // 读取第二行
    String line = file.readStringUntil('\n');
    int start = 0;
    int end = line.indexOf(',');
    // userName,userPwd,deviceName,netInterface,wifiSetFin,ssid,staPwd,wifiMode,wifiIp,wifiNetmask,wifiGateway,ethSetFin,ethMode,ethIp,ethNetmask,ethGateway,serialSetFin,baud,parityBit,dataBit,stopBit,rtuTimeout,bridgeSetFin,port,uidMin,uidMax,tcpTimeout
    // For each setting
    userName = line.substring(start, end);
    start = end + 1;
    end = line.indexOf(',', start);

    userPwd = line.substring(start, end);
    start = end + 1;
    end = line.indexOf(',', start);

    deviceName = line.substring(start, end);
    start = end + 1;
    end = line.indexOf(',', start);

    netInterface = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    wifiSetFin = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    strncpy(ssid, line.substring(start, end).c_str(), sizeof(ssid));
    start = end + 1;
    end = line.indexOf(',', start);

    strncpy(staPwd, line.substring(start, end).c_str(), sizeof(staPwd));
    start = end + 1;
    end = line.indexOf(',', start);

    wifiMode = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    if (!wifiIp.fromString(line.substring(start, end)))
    {
      Serial.println("WiFi IP address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);

    if (!wifiNetmask.fromString(line.substring(start, end)))
    {
      Serial.println("WiFi Netmask address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);

    if (!wifiGateway.fromString(line.substring(start, end)))
    {
      Serial.println("WiFi Gateway address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);

    ethSetFin = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    ethMode = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    if (!ethIp.fromString(line.substring(start, end)))
    {
      Serial.println("Ethernet IP address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);

    if (!ethNetmask.fromString(line.substring(start, end)))
    {
      Serial.println("Ethernet Netmask address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);

    if (!ethGateway.fromString(line.substring(start, end)))
    {
      Serial.println("Ethernet Gateway address error");
    }
    start = end + 1;
    end = line.indexOf(',', start);
    // serialSetFin,baud,parityBit,dataBit,stopBit,rtuTimeout,bridgeSetFin,port,uidMin,uidMax,tcpTimeout
    serialSetFin = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    baud = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    parityBit = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    dataBit = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    stopBit = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    rtuTimeout = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    bridgeSetFin = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    port = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    uidMin = line.substring(start, end).toInt();
    start = end + 1;
    end = line.indexOf(',', start);

    uidMax = line.substring(start, end).toInt();
    start = end + 1;

    tcpTimeout = line.substring(start).toInt(); // until the end of the line
  }
  file.close();
  // errorDetect();
}

// void errorDetect(){
//   // 讀取檔案後，檢查是否有錯誤，如果錯設回default.csv

// }

void writeToCSV(fs::FS &fs, const char *path)
{
  Serial.printf("Writing file: %s\r\n", path);

  // Open file, create if it doesn't exist
  File file = fs.open(path, FILE_WRITE);

  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }

  String title = "userName,userPwd,deviceName,netInterface,wifiSetFin,ssid,staPwd,wifiMode,wifiIp,wifiNetmask,wifiGateway,ethSetFin,ethMode,ethIp,ethNetmask,ethGateway,serialSetFin,baud,parityBit,dataBit,stopBit,rtuTimeout,bridgeSetFin,port,uidMin,uidMax,tcpTimeout\n";

  String csv = userName + "," + userPwd + "," + deviceName + "," +
               String(netInterface) + "," + String(wifiSetFin) + "," + ssid + "," +
               staPwd + "," + String(wifiMode) + "," + wifiIp.toString() + "," +
               wifiNetmask.toString() + "," + wifiGateway.toString() + "," + String(ethSetFin) + "," +
               String(ethMode) + "," + ethIp.toString() + "," + ethNetmask.toString() + "," + ethGateway.toString() + "," +
               String(serialSetFin) + "," + String(baud) + "," + String(parityBit) + "," +
               String(dataBit) + "," + String(stopBit) + "," + String(rtuTimeout) + "," +
               String(bridgeSetFin) + "," + String(port) + "," + String(uidMin) + "," +
               String(uidMax) + "," + String(tcpTimeout);

  if (file.print(title + csv))
  {
    Serial.println("File was written");
  }
  else
  {
    Serial.println("Write failed");
  }

  file.close();
}

void setSerial()
{
  SerialConfig config;
  switch (dataBit)
  {
  case 8:
    if (parityBit == 0)
    {
      if (stopBit == 1)
        config = SERIAL_8N1;
      else if (stopBit == 2)
        config = SERIAL_8N2;
    }
    else if (parityBit == 1)
    {
      if (stopBit == 1)
        config = SERIAL_8O1;
      else if (stopBit == 2)
        config = SERIAL_8O2;
    }
    else if (parityBit == 2)
    {
      if (stopBit == 1)
        config = SERIAL_8E1;
      else if (stopBit == 2)
        config = SERIAL_8E2;
    }
    break;
  case 5:
    if (parityBit == 0)
    {
      if (stopBit == 1)
        config = SERIAL_5N1;
      else if (stopBit == 2)
        config = SERIAL_5N2;
    }
    else if (parityBit == 1)
    {
      if (stopBit == 1)
        config = SERIAL_5O1;
      else if (stopBit == 2)
        config = SERIAL_5O2;
    }
    else if (parityBit == 2)
    {
      if (stopBit == 1)
        config = SERIAL_5E1;
      else if (stopBit == 2)
        config = SERIAL_5E2;
    }
    break;

  case 6:
    if (parityBit == 0)
    {
      if (stopBit == 1)
        config = SERIAL_6N1;
      else if (stopBit == 2)
        config = SERIAL_6N2;
    }
    else if (parityBit == 1)
    {
      if (stopBit == 1)
        config = SERIAL_6O1;
      else if (stopBit == 2)
        config = SERIAL_6O2;
    }
    else if (parityBit == 2)
    {
      if (stopBit == 1)
        config = SERIAL_6E1;
      else if (stopBit == 2)
        config = SERIAL_6E2;
    }
    break;

  case 7:
    if (parityBit == 0)
    {
      if (stopBit == 1)
        config = SERIAL_7N1;
      else if (stopBit == 2)
        config = SERIAL_7N2;
    }
    else if (parityBit == 1)
    {
      if (stopBit == 1)
        config = SERIAL_7O1;
      else if (stopBit == 2)
        config = SERIAL_7O2;
    }
    else if (parityBit == 2)
    {
      if (stopBit == 1)
        config = SERIAL_7E1;
      else if (stopBit == 2)
        config = SERIAL_7E2;
    }
    break;
  }
  Serial.printf("baud: %d\nconfig: %X\n", baud, config);
  Serial2.begin(baud, config);
  return; // 錯誤判斷
}

// bool setSerial() {
// paritybit計算有錯
//   // SERIAL_5N1
//   uint32_t serialConfig[] = {
//     0x8000010, // 5N1
//     0x8000012, // 5E1
//     0x8000013, // 5O1
//     0x8000014, // 6N1
//     0x8000016, // 6E1
//     0x8000017, // 6O1
//     0x8000018, // 7N1
//     0x800001a, // 7E1
//     0x800001b, // 7O1
//     0x800001c, // 8N1
//     0x800001e, // 8E1
//     0x800001f, // 8O1
//     0x8000030, // 5N2
//     0x8000032, // 5E2
//     0x8000033, // 5O2
//     0x8000034, // 6N2
//     0x8000036, // 6E2
//     0x8000037, // 6O2
//     0x8000038, // 7N2
//     0x800003a, // 7E2
//     0x800003b, // 7O2
//     0x800003c, // 8N2
//     0x800003e, // 8E2
//     0x800003f  // 8O2
//   };
//   // For the index computation
//   const uint8_t dataBitBase = 5;
//   // none: 0, odd: 1, even: 2
//   const uint8_t parityBitBase = 0;
//   const uint8_t stopBitBase = 1;

//   uint8_t dataBitIndex = dataBit - dataBitBase;
//   uint8_t parityBitIndex = parityBit - parityBitBase;
//   uint8_t stopBitIndex = stopBit - stopBitBase;

//   if(dataBitIndex > 3 || parityBitIndex > 2 || stopBitIndex > 1) {
//     Serial.println("Configuration error");
//     return false;
//   }

//   uint32_t configIndex = dataBitIndex * 6 + parityBitIndex * 2 + stopBitIndex;
//   uint32_t config = serialConfig[configIndex];
//   Serial.printf("baud: %d\nconfig: %X\n", baud, config);
//   Serial2.begin(baud, config);
//   return true;
// }

// Try to connect to the network according to the settings, CURRENT_NETINTERFACE is the current connection status.
void applyOldNetSet()
{
  Serial.println("Apply old settings");
  if (wifiSetFin == 1)
  {
    if (!wifiSta()) // wifi connect fail
    {
      // CURRENT_NETINTERFACE default is NO_NETWORK
      Serial.println("connect wifiSta timeout");
      return;
    }
    if (wifiMode == 0)
    {
      CURRENT_NETINTERFACE = WIFI_DHCP;
    }
    else
    {
      CURRENT_NETINTERFACE = WIFI_STATIC;
    }
    return;
  }
  else if (ethSetFin == 1)
  {
    if (!connectEth()) // ethernet connect fail
    {
      // CURRENT_NETINTERFACE default is NO_NETWORK
      Serial.println("connect ethernet timeout");
      return;
    }
    if (ethMode == 0)
    {
      CURRENT_NETINTERFACE = ETH_DHCP;
    }
    else
    {
      CURRENT_NETINTERFACE = ETH_STATIC;
    }
    return;
  }
  else
  {
    Serial.println("No old settings");
  }
}
int readRotarySwitch()
{
  int total=0;
  if (digitalRead(SW_1) == LOW) { total+=1; }
  if (digitalRead(SW_2) == LOW) { total+=2; }
  if (digitalRead(SW_4) == LOW) { total+=4; }
  return total;
}
void initRotarySwitch()
{
  pinMode(SW_1, INPUT_PULLUP);
  pinMode(SW_2, INPUT_PULLUP);
  pinMode(SW_4, INPUT_PULLUP);
}
void printAllGV()
{
  Serial.printf("userName: %s\n", userName);
  Serial.printf("userPwd: %s\n", userPwd);
  Serial.print("deviceName: ");
  Serial.println(deviceName);
  Serial.printf("netInterface: %d\n", netInterface);
  Serial.printf("wifiSetFin: %d\n", wifiSetFin);
  Serial.printf("ssid: %s\n", ssid);
  Serial.printf("staPwd: %s\n", staPwd);
  Serial.printf("wifiMode: %d\n", wifiMode);
  Serial.printf("wifiIp: %s\n", wifiIp.toString().c_str());
  Serial.printf("wifiNetmask: %s\n", wifiNetmask.toString().c_str());
  Serial.printf("wifiGateway: %s\n", wifiGateway.toString().c_str());
  Serial.printf("ethSetFin: %d\n", ethSetFin);
  Serial.printf("ethMode: %d\n", ethMode);
  Serial.printf("ethIp: %s\n", ethIp.toString().c_str());
  Serial.printf("ethNetmask: %s\n", ethNetmask.toString().c_str());
  Serial.printf("ethGateway: %s\n", ethGateway.toString().c_str());
  Serial.printf("serialSetFin: %d\n", serialSetFin);
  Serial.printf("baud: %d\n", baud);
  Serial.printf("parityBit: %d\n", parityBit);
  Serial.printf("dataBit: %d\n", dataBit);
  Serial.printf("stopBit: %d\n", stopBit);
  Serial.printf("rtuTimeout: %d\n", rtuTimeout);
  Serial.printf("bridgeSetFin: %d\n", bridgeSetFin);
  Serial.printf("port: %d\n", port);
  Serial.printf("uidMin: %d\n", uidMin);
  Serial.printf("uidMax: %d\n", uidMax);
  Serial.printf("tcpTimeout: %d\n", tcpTimeout);
}
void initSd()
{
  pinMode(SD_CS, OUTPUT);
  // You need to light up LED_READY first, otherwise the SDcard can't be mounted.
  digitalWrite(LED_READY, HIGH);

  Serial.println("Turning the LED_READY ON");
  vTaskDelay(1000 / portTICK_PERIOD_MS);
  // Initialize SD card
  Serial.println("\n------------------------------");
  Serial.print("Initializing SD card...\n");
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spi))
  {
    Serial.println("Card Mount Failed");
    return;
  }
  Serial.println("SD card mount successfully!");
  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE)
  {
    Serial.println("No SD card attached");
    return;
  }
  // Show SD card type
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }
  // Show SD card size
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.println("\n------------------------------");
}
void writeEthMac(fs::FS &fs, const char *path, String newEthMac)
{
  Serial.printf("Writing file: %s\r\n", path);

  // Open file, create if it doesn't exist
  File file = fs.open(path, FILE_WRITE);

  if (!file)
  {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Write data to file
  if (file.print(newEthMac))
  {
    Serial.println("File was written");
  }
  else
  {
    Serial.println("Write failed");
  }
  file.close();
}
void renewEthMac(fs::FS &fs, const char *path, String baseMac)
{
  File file = fs.open(path);
  if (!file)
  {
    Serial.println("Failed to open file for reading");
    return;
  }

  while (file.available())
  {
    String line = file.readStringUntil('\n');
    int commaIndex = line.indexOf(',');

    // Ignore lines without a comma
    if (commaIndex == -1)
    {
      continue;
    }

    String mac = line.substring(0, commaIndex);
    if (mac == baseMac)
    {
      String newEthMac = line.substring(commaIndex + 1);
      Serial.println("Matched Eth MAC found: " + newEthMac);

      // Call your function here to write newEthMac to flash.
      writeEthMac(SPIFFS, "/mac.txt", newEthMac);
      Serial.println("Renewed Eth MAC: " + newEthMac);
      hasEthMac = true;
    }
  }
  if (!hasEthMac)
  {
    Serial.println("No matched Eth MAC found");
  }
  file.close();
  SD.end();
  spi.end();
}
void initLed()
{
  pinMode(LED_READY, OUTPUT);
  pinMode(LED_WIFI, OUTPUT);
  digitalWrite(LED_READY, LOW);
  digitalWrite(LED_WIFI, LOW);
}
void setup()
{
  initLed();
  initRotarySwitch();
  String baseMac = WiFi.macAddress();
  Serial.begin(115200);
  vTaskDelay(3000 / portTICK_PERIOD_MS);
  Serial.print("ESP Board MAC Address:  ");
  Serial.println(baseMac);
  if (!SPIFFS.begin(true))
  {
    Serial.println("SPIFFS ERROR");
    return;
  }
  String ethMac = getEthMac(SPIFFS, "/mac.txt");

  if (ethMac == "")
  {
    Serial.println("No Ethernet MAC Address");
    initSd();
    renewEthMac(SD, "/mac.csv", baseMac);
  }
  else
  {
    Serial.print("Ethernet MAC Address:  ");
    Serial.println(ethMac);
    hasEthMac = true;
  }

  int RSValue = readRotarySwitch();
  Serial.print("Rotary switch value is: ");
  Serial.println(RSValue);
  if (RSValue == 7)
  {
    Serial.println("Reset to default");
    loadSetting(SPIFFS, "/default.csv");
    writeToCSV(SPIFFS, "/setting.csv");
  }
  listDir(SPIFFS, "/", 0);
  readFile(SPIFFS, "/setting.csv");
  loadSetting(SPIFFS, "/setting.csv");
  Serial.println();
  if (deviceName == "Matrix-310")
  {
    Serial.println("update deviceName");
    deviceName += "-" + baseMac.substring(9, 11) + baseMac.substring(12, 14) + baseMac.substring(15, 17);
    writeToCSV(SPIFFS, "/setting.csv");
  }
  printAllGV();
  WiFi.mode(WIFI_AP_STA);
  Serial.println(wifiAP()); // open AP
  vTaskDelay(2000 / portTICK_PERIOD_MS);
  applyOldNetSet(); // if wifiSetFin == 1, try to connect to wifi; if ethSetFin == 1, try to connect to eth

  // if (!MDNS.begin("esp32"))
  // {
  //   Serial.println("Error setting up MDNS responder!");
  //   delay(1000);
  // }
  // Serial.println("mDNS responder started at esp32.local");
  Serial.println("multiTaskCreate");
  multiTaskCreate();
}

void loop()
{
}