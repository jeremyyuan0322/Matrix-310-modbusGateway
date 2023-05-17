#include <WiFi.h>
#include "inc/Artila-Matrix310.h"
#include "esp_wifi.h"
// #include "rom/ets_sys.h"
// Modbus RTU structs
struct modbusRtuWrite {
  uint8_t address;
  uint8_t function;
  uint8_t startAddress[2];
  uint8_t dataLength[2];
  uint8_t crc[2];
};

struct modbusRtuRead {
  uint8_t address;
  uint8_t function;
  uint8_t dataLength;
  uint8_t *data;
  uint8_t crc[2];
};

// Modbus TCP structs
struct modbusTcpRequest {
  uint8_t transactionId[2];
  uint8_t protocolId[2];
  uint8_t length[2];
  modbusRtuWrite rtuPart; // no crc
};//000100000006020300440003
// 00 01 00 00 00 06   

struct modbusTcpResponse {
  uint8_t transactionId[2];
  uint8_t protocolId[2];
  uint8_t length[2];
  modbusRtuRead rtuPart; // no crc
};

// WiFi and server settings
const int serverPort = 502;
WiFiServer  server(serverPort); // 99, 81

// Helper functions
// uint16_t calculateCRC(uint8_t *buffer, uint8_t len);
// void parseModbusTCPRequest(uint8_t *tcpBuffer, uint8_t tcpLength, modbusTcpRequest &tcpRequest);
// bool processModbusrtuWrite(modbusRtuWrite &rtuWrite, modbusRtuRead &rtuRead, 
//                           uint16_t &tcpResponseLength, uint16_t &rtuReadDataLength, 
//                           uint8_t *rtuReadBuffer);
// void createModbusTCPResponse(modbusTcpRequest &tcpRequest, modbusRtuRead &rtuRead, 
//                               modbusTcpResponse &tcpResponse, uint16_t tcpResponseLength);
// void printModbusRtuWrite(modbusRtuWrite &rtu, bool isTcp);
// void printModbusRtuRead(modbusRtuRead &rtu, bool isTcp);
// void printModbusTcpRequest(modbusTcpRequest &tcp);
// void printModbusTcpResponse(modbusTcpResponse &tcp);
WiFiClient client;
unsigned int wifiConnectCount = 0;

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  delay(100);

  // Initialize RS485 communication
  Serial2.begin(9600);
  pinMode(COM1_RTS, OUTPUT);
  delay(100);

  // Connect to Wi-Fi
  wifiConnect();

  // Start the server
  server.begin();
  Serial.print("Server started at ");
  Serial.println(WiFi.localIP());
  Serial.print("port ");
  Serial.println(serverPort);
  delay(100);
}

void loop()
{
  if(WiFi.status() != WL_CONNECTED){
    Serial.printf("WiFi.status(): %d\n", WiFi.status());
    Serial.printf("wifiConnectCount: %d\n", wifiConnectCount);
    WiFi.disconnect(true);
    delay(1000);
    // WiFi.mode(WIFI_OFF);
    Serial.println("WiFi disconnected");
    Serial.println("try to reconnect to WiFi");
    // WiFi.mode(WIFI_STA);
    wifiConnect();
    wifiConnectCount++;
    
  }
  // Check if a client has connected
  // server.available() return a new WiFiClient object, return empty object if no client connected
  client = server.available();
  if (client)
  {
    client.setNoDelay(true);
    Serial.println("New client connected");
    modbusTcpRequest tcpRequest;
    modbusTcpResponse tcpResponse;
    // modbusRtuRead rtuRead;
    
    initStructs(tcpRequest, tcpResponse);
    
    unsigned long clientTimeout = millis();
    while (client.connected())
    {
      if (client.available())
      {
        Serial.println("received tcp request");
        // Read Modbus TCP request
        // uint8_t tcpBuffer[256];
        uint8_t bytesRead = client.read((byte *)&tcpRequest, 12);
        // Parse Modbus TCP request
        // parseModbusTCPRequest(tcpBuffer, bytesRead, tcpRequest);
        printModbusTcpRequest(tcpRequest);

        // Process Modbus RTU request
        uint16_t tcpResponseLength = 0;
        uint16_t rtuReadDataLength = swap_uint16(*(uint16_t *)(&tcpRequest.rtuPart.dataLength[0]));
        Serial.printf("rtuReadDataLength: %i\n", rtuReadDataLength); // 3
        uint8_t rtuReadBuffer[rtuReadDataLength * 2 + 5];
        initBuffer(rtuReadBuffer, sizeof(rtuReadBuffer));
        Serial.printf("rtuReadBufferSize: %d\n", sizeof(rtuReadBuffer)); // 11
        if (processModbusrtuWrite(tcpRequest.rtuPart, tcpResponse.rtuPart, tcpResponseLength,
                                  rtuReadDataLength, rtuReadBuffer) == false)
        {
          Serial.println("processModbusrtuWrite failed");
          break;
        }
        Serial.printf("tcpResponseLength: %X\n", tcpResponseLength); // 9
        Serial.println("after:");
        printModbusRtuRead(tcpResponse.rtuPart, false);

        // Create Modbus TCP response
        createModbusTCPResponse(tcpRequest, tcpResponse, tcpResponseLength);
        printModbusTcpResponse(tcpResponse);

        // Send Modbus TCP response
        uint8_t tcpResponseBuffer[tcpResponseLength + 6]; // 9 + 6 = 15
        initBuffer(tcpResponseBuffer, sizeof(tcpResponseBuffer));
        Serial.printf("tcpResponseLength+6: %d\n", tcpResponseLength + 6); // 15
        memcpy(tcpResponseBuffer, &tcpResponse.transactionId[0], 6);
        memcpy(tcpResponseBuffer + 6, &tcpResponse.rtuPart.address, 3);
        memcpy(tcpResponseBuffer + 9, tcpResponse.rtuPart.data, tcpResponseLength - 3);
        delay(10);
        if(tcpResponse.rtuPart.data != NULL){
          Serial.printf("Heap Size before free: %d\n", ESP.getHeapSize());
          int usedHeap = ESP.getHeapSize() - ESP.getFreeHeap();
          Serial.printf("Used Heap Size before free: %d\n", usedHeap);
          free(tcpResponse.rtuPart.data); // free memory
          tcpResponse.rtuPart.data = NULL;
          Serial.println("freed rtuRead.data memory");
        }
        Serial.printf("Heap Size: %d\n", ESP.getHeapSize());
        int usedHeap = ESP.getHeapSize() - ESP.getFreeHeap();
        Serial.printf("Used Heap Size: %d\n", usedHeap);
        // no crc
        Serial.print("tcpResponseBuffer:");
        printBuffer(tcpResponseBuffer, sizeof(tcpResponseBuffer));
        Serial.printf("rtuReadDataLength: %d\n", rtuReadDataLength);
        int clientWrite = client.write(tcpResponseBuffer, 9 + rtuReadDataLength * 2); // 9+3*2=15
        Serial.printf("clientWrite: %d\n", clientWrite);
        Serial.println("sent tcp response");
        
        Serial.println();
        client.flush();
        delay(10);
        clientTimeout = millis();
      }
      if(millis() - clientTimeout > 5000){
        client.stop();
        Serial.println("client timeout");
        Serial.println("client disconnected");
        break;
      }
      else if(client.connected() != true){
        client.stop();
        Serial.println("client disconnected");
        break;
      }
      else if(WiFi.status() != WL_CONNECTED){
        client.stop();
        Serial.println("WiFi disconnected");
        Serial.println("client disconnected");
        break;
      }
    }
    
  }
}

void wifiConnect()
{
  // Replace with your network credentials
  const char *ssid = "ROOM2";
  const char *password = "12481248";
  // wifi_ps_type_t current_ps_mode;
  // esp_wifi_get_ps(&current_ps_mode);
  // esp_sleep_enable_wifi_wakeup();
  // if(current_ps_mode == WIFI_PS_NONE) {
  //   Serial.println("WiFi is not in power save mode");
  // } 
  // else if(current_ps_mode == WIFI_PS_MIN_MODEM) {
  //   Serial.println("WiFi is in minimum modem power save mode");
  // } 
  // else if(current_ps_mode == WIFI_PS_MAX_MODEM) {
  //   Serial.println("WiFi is in maximum modem power save mode");
  // }
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  // WiFi.setAutoReconnect(false);
  delay(100);
  WiFi.mode(WIFI_STA);
  // WiFi.setSleep(false);
  // WiFi.setSleep(WIFI_PS_NONE);
  // wifiScan();
  // esp_wifi_set_max_tx_power(20);
  WiFi.begin(ssid, password);
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    vTaskDelay(500 / portTICK_PERIOD_MS);
    Serial.print(".");
    if (WiFi.status() == WL_CONNECTED) {
      // wifi_config_t conf;
      // esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &conf);
      /*
      - ESP_OK: succeed
  *    - ESP_ERR_WIFI_NOT_INIT: WiFi is not initialized by esp_wifi_init
  *    - ESP_ERR_INVALID_ARG: invalid argument
  *    - ESP_ERR_WIFI_MODE: WiFi mode is wrong
  *    - ESP_ERR_WIFI_CONN: WiFi internal error, the station/soft-AP control block is invalid*/
      // if (err == ESP_OK)
      // {
      //   // 打印 SSID
      //   Serial.printf("SSID: %s\n", conf.sta.ssid);
      // }
      // else
      // {
      //   // 處理錯誤
      //   if(err == ESP_ERR_WIFI_NOT_INIT){
      //     Serial.println("WiFi is not initialized by esp_wifi_init");
      //   }
      //   else if(err == ESP_ERR_INVALID_ARG){
      //     Serial.println("invalid argument");
      //   }
      //   else if(err == ESP_ERR_WIFI_MODE){
      //     Serial.println("WiFi mode is wrong");
      //   }
      //   else if(err == ESP_ERR_WIFI_CONN){
      //     Serial.println("WiFi internal error, the station/soft-AP control block is invalid");
      //   }
      //   else{
      //     Serial.printf("unknown error: %d\n", err);
      //   }
      // }

      //RSSI
      Serial.printf("WiFi.RSSI(): %d\n", WiFi.RSSI());
      Serial.println("");
      Serial.println("WiFi connected.");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
      break;
    }
    if (millis() - startTime > 5000) {
      // WiFi connection timeout
      Serial.println("\nwifi connect timeout");
      break;
    }
  }
}

void wifiScan(){
  Serial.println("Scan start");
    // WiFi.scanNetworks will return the number of networks found.
    int n = WiFi.scanNetworks();
    Serial.println("Scan done");
    if (n == 0) {
        Serial.println("no networks found");
    } else {
        Serial.print(n);
        Serial.println(" networks found");
        Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
        for (int i = 0; i < n; ++i) {
            // Print SSID and RSSI for each network found
            Serial.printf("%2d",i + 1);
            Serial.print(" | ");
            Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
            Serial.print(" | ");
            Serial.printf("%4d", WiFi.RSSI(i));
            Serial.print(" | ");
            Serial.printf("%2d", WiFi.channel(i));
            Serial.print(" | ");
            switch (WiFi.encryptionType(i))
            {
            case WIFI_AUTH_OPEN:
                Serial.print("open");
                break;
            case WIFI_AUTH_WEP:
                Serial.print("WEP");
                break;
            case WIFI_AUTH_WPA_PSK:
                Serial.print("WPA");
                break;
            case WIFI_AUTH_WPA2_PSK:
                Serial.print("WPA2");
                break;
            case WIFI_AUTH_WPA_WPA2_PSK:
                Serial.print("WPA+WPA2");
                break;
            case WIFI_AUTH_WPA2_ENTERPRISE:
                Serial.print("WPA2-EAP");
                break;
            case WIFI_AUTH_WPA3_PSK:
                Serial.print("WPA3");
                break;
            case WIFI_AUTH_WPA2_WPA3_PSK:
                Serial.print("WPA2+WPA3");
                break;
            case WIFI_AUTH_WAPI_PSK:
                Serial.print("WAPI");
                break;
            default:
                Serial.print("unknown");
            }
            Serial.println();
            delay(10);
        }
    }
    Serial.println("");
    // Delete the scan result to free memory for code below.
    WiFi.scanDelete();
    // Wait a bit before next step.
    delay(1000);
}

void initStructs(modbusTcpRequest &tcpRequest, modbusTcpResponse &tcpResponse) {
  memset(&tcpRequest, 0, sizeof(modbusTcpRequest));
  memset(&tcpResponse, 0, sizeof(modbusTcpResponse));
  tcpResponse.rtuPart.data = NULL;
}
void initBuffer(uint8_t *buffer, uint8_t length) {
  memset(buffer, 0, length);
}

uint16_t swap_uint16(uint16_t val)
{
    return (val << 8) | (val >> 8);
}

// Function to calculate CRC-16
unsigned short calculateCRC(uint8_t *data, uint8_t length)
{
    unsigned int i;
    unsigned short crc = 0xFFFF;
    while (length--)
    {
        crc ^= *data++;
        for (i = 0; i < 8; ++i)
        {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc = (crc >> 1);
        }
    }
    return crc;
}

// void parseModbusTCPRequest(uint8_t *tcpBuffer, uint8_t tcpLength, modbusTcpRequest &tcpRequest) {
//   memcpy(&tcpRequest, tcpBuffer, sizeof(modbusTcpRequest));
// }

bool processModbusrtuWrite(modbusRtuWrite &rtuWrite, modbusRtuRead &rtuRead, 
                          uint16_t &tcpResponseLength, uint16_t &rtuReadDataLength 
                          ,uint8_t *rtuReadBuffer) {
  // calculate CRC
  uint16_t crc = calculateCRC((uint8_t *)&rtuWrite, 6);
  rtuWrite.crc[0] = crc & 0xFF;
  rtuWrite.crc[1] = (crc >> 8) & 0xFF;
  rtuRead.data = (byte *)malloc(sizeof(byte) * rtuReadDataLength * 2); // 3*2=6
  // rtuRead.data = new byte[sizeof(byte) * rtuReadDataLength * 2];
  if (rtuRead.data == NULL)
  {
        Serial.println("malloc failed");
        client.stop();
        return false;
  }
  else
  {
        Serial.println("malloc success");
  }
  initBuffer(rtuRead.data, rtuReadDataLength * 2);
  digitalWrite(COM1_RTS, HIGH); // Set RS485 to transmit mode
  delay(0.01);
  // Prepare RTU request for sending
  Serial.println("data send: ");
  printModbusRtuWrite(rtuWrite, false);
  int writeLen = Serial2.write((byte *)&rtuWrite, sizeof(modbusRtuWrite)); 
  Serial.print("data send: ");
  Serial.println(writeLen);
  Serial2.flush();
  delay(1);
  digitalWrite(COM1_RTS, LOW); // Set RS485 to receive mode
  delay(0.01);
  // Read RTU response
  unsigned long RS485Timeout = millis();
  uint16_t bytesRead = 0;
  while (1)
  {
        if (Serial2.available() > 0)
        {
            bytesRead = Serial2.readBytes(rtuReadBuffer, 5 + rtuReadDataLength * 2);
            tcpResponseLength = bytesRead - 2;
            Serial2.flush();
            if (bytesRead > 0)
            {
                Serial.print("rtuRead data: ");
                for (int i = 0; i < bytesRead; i++)
                {
                  Serial.print(*(byte *)(rtuReadBuffer + i), HEX);
                  Serial.print(" ");
                }
                Serial.println("");
                Serial.print("read length: ");
                Serial.println(bytesRead);
            }
            break;
        }
        if (millis() - RS485Timeout > 1000)
        {
            Serial.println("read nothing!");
            client.stop();
            return false;
        }
  }
  // Process and populate modbusRtuRead struct
  memcpy(&rtuRead.address, rtuReadBuffer, 3);
  rtuRead.data = rtuReadBuffer + 3; // Set pointer to data
  Serial.printf("rtuReadBufferSize: %d\n", sizeof(rtuReadBuffer));
  unsigned int rtuCrcLocation = 3 + rtuReadDataLength * 2;
  Serial.printf("rtuCrcLocation: %i\n", rtuCrcLocation); // 9
  memcpy(&rtuRead.crc, rtuReadBuffer + rtuCrcLocation, 2);
  Serial.println("in processModbusrtuWrite function: ");
  printModbusRtuRead(rtuRead, false);
  return true;
}

void createModbusTCPResponse(modbusTcpRequest &tcpRequest, 
                            modbusTcpResponse &tcpResponse, uint16_t tcpResponseLength) {
  memcpy(&tcpResponse.transactionId, &tcpRequest.transactionId, 4);
  *(uint16_t *)&tcpResponse.length[0] = swap_uint16(tcpResponseLength);
  // memcpy(&tcpResponse.rtuPart.address, &rtuRead.address, 3);
  // tcpResponse.rtuPart.data = rtuRead.data;
}

void printBuffer(uint8_t *buffer, uint16_t length) {
  for (int i = 0; i < length; i++) {
    Serial.print(buffer[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void printModbusRtuWrite(modbusRtuWrite &rtu, bool isTcp) {
  Serial.println("Modbus RTU Write:");
  Serial.print("Address: ");
  Serial.println(rtu.address, HEX);
  Serial.print("Function: ");
  Serial.println(rtu.function, HEX);
  Serial.print("Start Address: ");
  Serial.print(rtu.startAddress[0], HEX);
  Serial.println(rtu.startAddress[1], HEX);
  Serial.print("Data Length: ");
  Serial.print(rtu.dataLength[0], HEX);
  Serial.println(rtu.dataLength[1], HEX);
  if(!isTcp) {
    Serial.print("CRC: ");
    Serial.print(rtu.crc[0], HEX);
    Serial.println(rtu.crc[1], HEX);
    Serial.println();
  }  
}

void printModbusRtuRead(modbusRtuRead &rtu, bool isTcp) {
  Serial.println("Modbus RTU Read:");
  Serial.print("Address: ");
  Serial.println(rtu.address, HEX);
  Serial.print("Function: ");
  Serial.println(rtu.function, HEX);
  Serial.print("Data Length: ");
  Serial.println(rtu.dataLength, HEX);
  Serial.print("Data: ");
  for (int i = 0; i < rtu.dataLength; i++)
  {
        Serial.print(*(byte *)(rtu.data + i), HEX);
        Serial.print(" ");
  }
  Serial.println();
  if(!isTcp) {
    Serial.print("CRC: ");
    Serial.print(rtu.crc[0], HEX);
    Serial.println(rtu.crc[1], HEX);
    Serial.println();
  }
}

void printModbusTcpRequest(modbusTcpRequest &tcp) {
  bool isTcp = true;
  Serial.println("Modbus TCP Request:");
  Serial.print("Transaction ID: ");
  Serial.print(tcp.transactionId[0], HEX);
  Serial.println(tcp.transactionId[1], HEX);
  Serial.print("Protocol ID: ");
  Serial.print(tcp.protocolId[0], HEX);
  Serial.println(tcp.protocolId[1], HEX);
  Serial.print("Length: ");
  Serial.print(tcp.length[0], HEX);
  Serial.println(tcp.length[1], HEX);
  Serial.println("Modbus RTU Write Part(no crc):");
  printModbusRtuWrite(tcp.rtuPart, isTcp);
}

void printModbusTcpResponse(modbusTcpResponse &tcp) {
  bool isTcp = true;
  Serial.println("Modbus TCP Response:");
  Serial.print("Transaction ID: ");
  Serial.print(tcp.transactionId[0], HEX);
  Serial.println(tcp.transactionId[1], HEX);
  Serial.print("Protocol ID: ");
  Serial.print(tcp.protocolId[0], HEX);
  Serial.println(tcp.protocolId[1], HEX);
  Serial.print("Length: ");
  Serial.print(tcp.length[0], HEX);
  Serial.println(tcp.length[1], HEX);
  Serial.println("Modbus RTU Read Part(no crc):");
  printModbusRtuRead(tcp.rtuPart, isTcp);
}

