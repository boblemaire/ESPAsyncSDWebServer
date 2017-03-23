/*
  Adaptation of SDWebServer (synchronous).

  Copyright (c)2017 Bob Lemaire. All rights reserved.

  This is a derivative work that uses ESPAsyncWebServer instead of ESP8266WebServer.
  While I didn't benchmark it against the synchronous version, it is
  noticeably faster.

  There was a slight problem with no context in the callback
  while downloading an SD file.  It was resolved by serializing the
  requests with a FiFoqueue. While this may have a slight impact on performance,
  there may be merit in limiting the concurrent downloads in the interest 
  keeping heap consumption down.  Once again, no real data, just a hunch.

  The best part about this program is the ACE editor and file manager.

  The original author's copyright follows:

  ********************************************************************************* 
 
  SDWebServer - Example WebServer with SD Card backend for esp8266

  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the ESP8266WebServer library for Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Have a FAT Formatted SD Card connected to the SPI port of the ESP8266
  The web root is the SD Card root folder
  File extensions with more than 3 charecters are not supported by the SD Library
  File Names longer than 8 charecters will be truncated by the SD library, so keep filenames shorter
  index.htm is the default index (works on subfolders as well)

  upload the contents of SdRoot to the root of the SDcard and access the editor by going to http://esp8266sd.local/edit

*/

#define DBG_OUTPUT_PORT Serial
#define FS_NO_GLOBALS
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266mDNS.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <MYWIFI.h>

const int pin_CS_SDcard = 15;
const char* ssid = MY_WIFI_SSID;
const char* password = MY_WIFI_PASSWORD;
const char* host = "SDAsync";
AsyncWebServer server(80);

/**************************************************************************************************
 *   Simple class to manage request FiFo queue.  Used to serialize downloads.
 **************************************************************************************************/
class AWSreqQueue { 
   
  struct reqQueue {
    reqQueue* next;
    AsyncWebServerRequest *request;
    reqQueue(){next=NULL; request=NULL;};
  };

  public:
    void queue(AsyncWebServerRequest *request){
      reqQueue* newQueue = new reqQueue;
      newQueue->request = request;
      reqQueue* curQueue = &queueHead;
      while(curQueue->next != NULL) curQueue = curQueue->next;
      curQueue->next = newQueue;
    }
    
    AsyncWebServerRequest* dequeue(){
      if(queueHead.next == NULL) return NULL;
      reqQueue* curQueue = queueHead.next;
      queueHead.next = curQueue->next;
      AsyncWebServerRequest* request = curQueue->request;
      delete curQueue;
      return request;
    }

    bool isEmpty(){return (queueHead.next == NULL);}
    
  private:
    reqQueue queueHead;
};



AWSreqQueue WsDownloadQueue;                  // Instance of above FiFo
bool WSDownloadActive = false;                // Semaphore to control serialization


void returnOK(AsyncWebServerRequest *request) {request->send(200, "text/plain", "");}
  
void returnFail(AsyncWebServerRequest *request, String msg) {request->send(500, "text/plain", msg + "\r\n");}

bool loadFromSdCard(AsyncWebServerRequest *request) {
      static File dataFile;
      String path = request->url();
      String dataType = "text/plain";
      if(path.endsWith("/")) path += "index.htm";
      if(path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
      else if(path.endsWith(".htm")) dataType = "text/html";
      else if(path.endsWith(".css")) dataType = "text/css";
      else if(path.endsWith(".js")) dataType = "application/javascript";
      else if(path.endsWith(".png")) dataType = "image/png";
      else if(path.endsWith(".gif")) dataType = "image/gif";
      else if(path.endsWith(".jpg")) dataType = "image/jpeg";
      else if(path.endsWith(".ico")) dataType = "image/x-icon";
      else if(path.endsWith(".xml")) dataType = "text/xml";
      else if(path.endsWith(".pdf")) dataType = "application/pdf";
      else if(path.endsWith(".zip")) dataType = "application/zip";
     
      dataFile  = SD.open(path.c_str());
      if(dataFile.isDirectory()){
        path += "/index.htm";
        dataType = "text/html";
        dataFile = SD.open(path.c_str());
      }
    
      if (!dataFile){
        return false;
      }
    
      if (request->hasParam("download")) dataType = "application/octet-stream";

            // Here is the context problem.  If there are multiple downloads active, 
            // we don't have the File handles. So we only allow one active download request
            // at a time and keep the file handle in static.  I'm open to a solution.
    
      request->send(dataType, dataFile.size(), [](uint8_t *buffer, size_t maxlen, size_t index) -> size_t {
                                                  size_t thisSize = dataFile.read(buffer, maxlen);
                                                  if((index + thisSize) >= dataFile.size()){
                                                    dataFile.close();
                                                    WSDownloadActive = false;
                                                  }
                                                  return thisSize;
                                                });
      return true;
}

void handleSDUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  struct uploadRequest {
    uploadRequest* next;
    AsyncWebServerRequest *request;
    File uploadFile;
    uint32_t fileSize;
    uploadRequest(){next = NULL; request = NULL; fileSize = 0;}
  };
  static uploadRequest uploadRequestHead;
  uploadRequest* thisUploadRequest = NULL;

  if( ! index){
    if(SD.exists((char *)filename.c_str())) SD.remove((char *)filename.c_str());
    thisUploadRequest = new uploadRequest;
    thisUploadRequest->request = request;
    thisUploadRequest->next = uploadRequestHead.next;
    uploadRequestHead.next = thisUploadRequest;
    thisUploadRequest->uploadFile = SD.open(filename.c_str(), FILE_WRITE);
    DBG_OUTPUT_PORT.print("Upload: START, filename: "); DBG_OUTPUT_PORT.println(filename);
  }
  else{
    thisUploadRequest = uploadRequestHead.next;
    while(thisUploadRequest->request != request) thisUploadRequest = thisUploadRequest->next;
  }
  
  if(thisUploadRequest->uploadFile){
    for(size_t i=0; i<len; i++){
      thisUploadRequest->uploadFile.write(data[i]);
    }
    thisUploadRequest->fileSize += len;
  }
  
  if(final){
    thisUploadRequest->uploadFile.close();
    DBG_OUTPUT_PORT.print("Upload: END, Size: "); DBG_OUTPUT_PORT.println(thisUploadRequest->fileSize);
    uploadRequest* linkUploadRequest = &uploadRequestHead;
    while(linkUploadRequest->next != thisUploadRequest) linkUploadRequest = linkUploadRequest->next;
    linkUploadRequest->next = thisUploadRequest->next;
    delete thisUploadRequest;
  }
}

void deleteRecursive(String path){
  File file = SD.open((char *)path.c_str());
  if(!file.isDirectory()){
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }
  file.rewindDirectory();
  File entry;
  while(entry = file.openNextFile()) {
    String entryPath = path + "/" +entry.name();
    if(entry.isDirectory()){
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

void handleDelete(AsyncWebServerRequest *request){
  if( ! request->params()) returnFail(request, "No Path");
  String path = request->arg("path");
  if(path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail(request, "Bad Path");
  }
  deleteRecursive(path);
  returnOK(request);
}

void handleCreate(AsyncWebServerRequest *request){
  if( ! request->params()) returnFail(request, "No Path");
  String path = request->arg("path");
  if(path == "/" || SD.exists((char *)path.c_str())) {
    returnFail(request, "Bad Path");
    return;
  }

  if(path.indexOf('.') > 0){
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if(file){
      file.write((const char *)0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK(request);
}

void printDirectory(AsyncWebServerRequest *request) {
  if( ! request->hasParam("dir")) return returnFail(request, "BAD ARGS");
  String path = request->arg("dir");
  if(path != "/" && !SD.exists((char *)path.c_str())) return returnFail(request, "BAD PATH");
  File dir = SD.open((char *)path.c_str());
  path = String();
  if(!dir.isDirectory()){
    dir.close();
    return returnFail(request, "NOT DIR");
  }
  DynamicJsonBuffer jsonBuffer;
  JsonArray& array = jsonBuffer.createArray();
  dir.rewindDirectory();
  File entry;
  while(entry = dir.openNextFile()){
    JsonObject& object = jsonBuffer.createObject();
    object["type"] = (entry.isDirectory()) ? "dir" : "file";
    object["name"] = String(entry.name());
    array.add(object);
    entry.close();
  }  
  String response = "";
  array.printTo(response);
  request->send(200, "application/json", response);
  dir.close();
}

void handleNotFound(AsyncWebServerRequest *request){
  String path = request->url();
  if(path.endsWith("/")) path += "index.htm";
  if(File testFile = SD.open(path.c_str())){
    testFile.close();
    WsDownloadQueue.queue(request);
    return;
  }
  String message = "\nNo Handler\r\n";
  message += "URI: ";
  message += request->url();
  message += "\nMethod: ";
  message += (request->method() == HTTP_GET)?"GET":"POST";
  message += "\nParameters: ";
  message += request->params();
  message += "\n";
  for (uint8_t i=0; i<request->params(); i++){
    AsyncWebParameter* p = request->getParam(i);
    message += String(p->name().c_str()) + " : " + String(p->value().c_str()) + "\r\n";
  }
  request->send(404, "text/plain", message);
  DBG_OUTPUT_PORT.print(message);
}

void AwsCheckQueues(){
  if( ! WSDownloadActive &&
      ! WsDownloadQueue.isEmpty()){
    WSDownloadActive = true;  
    loadFromSdCard(WsDownloadQueue.dequeue());
  }
}

void setup(void){
  
  DBG_OUTPUT_PORT.begin(115200);
  DBG_OUTPUT_PORT.setDebugOutput(true);
  DBG_OUTPUT_PORT.print("\n");

  WiFi.begin(ssid, password);
  DBG_OUTPUT_PORT.print("Connecting to ");
  DBG_OUTPUT_PORT.println(ssid);

  // Wait for connection
  
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 20) {//wait 10 seconds
    delay(500);
  }
  if(i == 21){
    DBG_OUTPUT_PORT.print("Could not connect to");
    DBG_OUTPUT_PORT.println(ssid);
    while(1) delay(500);
  }
  DBG_OUTPUT_PORT.print("Connected! IP address: ");
  DBG_OUTPUT_PORT.println(WiFi.localIP());
  
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

  server.begin();
  DBG_OUTPUT_PORT.println("HTTP server started");

  if( ! SD.begin(pin_CS_SDcard, SPI_FULL_SPEED)){
    DBG_OUTPUT_PORT.println("SD initiatization failed. Retrying.");
    while(!SD.begin(pin_CS_SDcard, SPI_FULL_SPEED)){
      delay(250); 
    } 
  }
  DBG_OUTPUT_PORT.println("SD Initialized.");
 
}

void loop(void){
   AwsCheckQueues();
}
