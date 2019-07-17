



#define DBG_OUTPUT_PORT Serial
#define FS_NO_GLOBALS
#include <FS.h>
#ifdef ARDUINO_ARCH_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <SD.h>
#else
#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include "SdFat.h"
SdFat SD;
#endif


#include <SPI.h>
#include <ArduinoJson.h>



const char* ssid = "SSID";
const char* password = "PASS";

const int pin_CS_SDcard = 4;
const char* host = "SDAsync";
AsyncWebServer server(80);
DNSServer dns;

void returnOK(AsyncWebServerRequest *request) {
  request->send(200, "text/plain", "");
}

void returnFail(AsyncWebServerRequest *request, String msg) {
  request->send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(AsyncWebServerRequest *request) {

  String path = request->url();
  String dataType = "text/plain";
  Serial.print("Seite anfordern: "); Serial.println(path);
  struct fileBlk {
    File dataFile;
  };
  fileBlk *fileObj = new fileBlk;

  if (path.endsWith("/")) path += "index.htm";
  if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  else if (path.endsWith(".htm")) dataType = "text/html";
  else if (path.endsWith(".html")) dataType = "text/html";
  else if (path.endsWith(".css")) dataType = "text/css";
  else if (path.endsWith(".js")) dataType = "application/javascript";
  else if (path.endsWith(".png")) dataType = "image/png";
  else if (path.endsWith(".gif")) dataType = "image/gif";
  else if (path.endsWith(".jpg")) dataType = "image/jpeg";
  else if (path.endsWith(".ico")) dataType = "image/x-icon";
  else if (path.endsWith(".xml")) dataType = "text/xml";
  else if (path.endsWith(".pdf")) dataType = "application/pdf";
  else if (path.endsWith(".zip")) dataType = "application/zip";

  fileObj->dataFile  = SD.open(path.c_str());
  if (fileObj->dataFile.isDirectory()) {
    path += "/index.htm";
    dataType = "text/html";
    fileObj->dataFile = SD.open(path.c_str());
  }

  if (!fileObj->dataFile) {
    delete fileObj;
    return false;
  }

  if (request->hasParam("download")) dataType = "application/octet-stream";

  // Here is the context problem.  If there are multiple downloads active,
  // we don't have the File handles. So we only allow one active download request
  // at a time and keep the file handle in static.  I'm open to a solution.

  request->_tempObject = (void*)fileObj;
  request->send(dataType, fileObj->dataFile.size(), [request](uint8_t *buffer, size_t maxlen, size_t index) -> size_t {
    fileBlk *fileObj = (fileBlk*)request->_tempObject;
    size_t thisSize = fileObj->dataFile.read(buffer, maxlen);
    if ((index + thisSize) >= fileObj->dataFile.size()) {
      fileObj->dataFile.close();
      request->_tempObject = NULL;
      delete fileObj;
    }
    return thisSize;
  });
  return true;
}

void handleSDUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  struct uploadRequest {
    uploadRequest* next;
    AsyncWebServerRequest *request;
    File uploadFile;
    uint32_t fileSize;
    uploadRequest() {
      next = NULL;
      request = NULL;
      fileSize = 0;
    }
  };
  static uploadRequest uploadRequestHead;
  uploadRequest* thisUploadRequest = NULL;

  if ( ! index) {
    if (SD.exists((char *)filename.c_str())) SD.remove((char *)filename.c_str());
    thisUploadRequest = new uploadRequest;
    thisUploadRequest->request = request;
    thisUploadRequest->next = uploadRequestHead.next;
    uploadRequestHead.next = thisUploadRequest;
    thisUploadRequest->uploadFile = SD.open(filename.c_str(), FILE_WRITE);
    DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(filename);
  }
  else {
    thisUploadRequest = uploadRequestHead.next;
    while (thisUploadRequest->request != request) thisUploadRequest = thisUploadRequest->next;
  }

  if (thisUploadRequest->uploadFile) {
    for (size_t i = 0; i < len; i++) {
      thisUploadRequest->uploadFile.write(data[i]);
    }
    thisUploadRequest->fileSize += len;
  }

  if (final) {
    thisUploadRequest->uploadFile.close();
    DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(thisUploadRequest->fileSize);
    uploadRequest* linkUploadRequest = &uploadRequestHead;
    while (linkUploadRequest->next != thisUploadRequest) linkUploadRequest = linkUploadRequest->next;
    linkUploadRequest->next = thisUploadRequest->next;
    delete thisUploadRequest;
  }
}

void deleteRecursive(String path) {
  File file = SD.open((char *)path.c_str());
  if (!file.isDirectory()) {
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }
  file.rewindDirectory();
  File entry;
  while (entry = file.openNextFile()) {
    String entryPath = path + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
  }
  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete(AsyncWebServerRequest *request) {
  if ( ! request->params()) returnFail(request, "No Path");
  String path = request->arg("path");
  if (path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail(request, "Bad Path");
  }
  deleteRecursive(path);
  returnOK(request);
}

void handleCreate(AsyncWebServerRequest *request) {
  if ( ! request->params()) returnFail(request, "No Path");
  String path = request->arg("path");
  if (path == "/" || SD.exists((char *)path.c_str())) {
    returnFail(request, "Bad Path");
    return;
  }

  if (path.indexOf('.') > 0) {
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if (file) {
      file.print((char*)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK(request);
}

void printDirectory(AsyncWebServerRequest *request) {

  char name[260];

  if ( ! request->hasParam("dir")) return returnFail(request, "BAD ARGS");
  String path = request->arg("dir");
  if (path != "/" && !SD.exists((char *)path.c_str())) return returnFail(request, "BAD PATH");

  File dir = SD.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    return returnFail(request, "NOT DIR");
  }
  StaticJsonDocument <1024> doc;
  JsonArray array = doc.to<JsonArray>();
  dir.rewindDirectory();
  File entry;
  while (entry = dir.openNextFile()) {

    JsonObject object = doc.createNestedObject();
    entry.getName(name, sizeof(name));

    object["type"] = (entry.isDirectory()) ? "dir" : "file";
    object["name"] = name;


    array.add( object );
    entry.close();

  }
  String response = "";
  serializeJson(array, response);
  Serial.println(response);
  request->send(200, "application/json", response);
  dir.close();
}

void handleNotFound(AsyncWebServerRequest *request) {
  String path = request->url();
  if (path.endsWith("/")) path += "index.htm";
  if (loadFromSdCard(request)) {
    return;
  }
  String message = "\nNo Handler\r\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET) ? "GET" : "POST";
  message += "\nParameters: ";
  message += request->params();
  message += "\n";
  for (uint8_t i = 0; i < request->params(); i++) {
    AsyncWebParameter* p = request->getParam(i);
    message += String(p->name().c_str()) + " : " + String(p->value().c_str()) + "\r\n";
  }
  request->send(404, "text/plain", message);
  DBG_OUTPUT_PORT.print(message);
}

void setup(void) {

  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");
  uint8_t i = 0;
  WiFi.begin(ssid, password);
  Serial.println("");
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
    Serial.print(".");
  }
  if (i == 11) {
    DBG_OUTPUT_PORT.print("Could not connect to");
    DBG_OUTPUT_PORT.println(ssid);


    while (1) {
      delay(500);
    }

  }
  delay(100);
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  delay(100);

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
    DBG_OUTPUT_PORT.println("MDNS responder started");
    DBG_OUTPUT_PORT.print("You can now connect to http://");
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(".local");
  }

  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on("/edit", HTTP_POST, returnOK, handleSDUpload);
  server.onNotFound(handleNotFound);
  delay(100);

  DBG_OUTPUT_PORT.println("HTTP server started");

#ifdef ARDUINO_ARCH_ESP8266
  if ( ! SD.begin(pin_CS_SDcard, SPI_FULL_SPEED)) {
    DBG_OUTPUT_PORT.println("SD initiatization failed. Retrying.");
    while (!SD.begin(pin_CS_SDcard, SPI_FULL_SPEED)) {
      delay(250);
    }
  }
#else
  // If having Problems with the Init of the SD Card choose a lower SD_SCK_MHZ
  if ( ! SD.begin(pin_CS_SDcard, SD_SCK_MHZ(19))) {
    DBG_OUTPUT_PORT.println("SD initiatization failed. Retrying.");
    while (!SD.begin(pin_CS_SDcard, SD_SCK_MHZ(19))) {
      delay(250);
    }
  }
#endif
  DBG_OUTPUT_PORT.println("SD Initialized.");
  delay(200);
  server.begin();
}

void loop(void) {

}
