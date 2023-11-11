/*
   @title     StarMod
   @file      SysModWeb.cpp
   @date      20231016
   @repo      https://github.com/ewowi/StarMod
   @Authors   https://github.com/ewowi/StarMod/commits/main
   @Copyright (c) 2023 Github StarMod Commit Authors
   @license   GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

#include "SysModWeb.h"
#include "SysModModel.h"
#include "SysModUI.h"
#include "SysModPrint.h"
#include "SysModFiles.h"
#include "SysModules.h"

#include "User/UserModMDNS.h"

#include "AsyncJson.h"

#include <ArduinoOTA.h>

//https://techtutorialsx.com/2018/08/24/esp32-web-server-serving-html-from-file-system/
//https://randomnerdtutorials.com/esp32-async-web-server-espasyncwebserver-library/


AsyncWebServer * SysModWeb::server = nullptr;
AsyncWebSocket * SysModWeb::ws = nullptr;

const char * (*SysModWeb::processWSFunc)(JsonVariant &) = nullptr;

DynamicJsonDocument * SysModWeb::responseDocLoopTask = nullptr;
DynamicJsonDocument * SysModWeb::responseDocAsyncTCP = nullptr;
bool SysModWeb::clientsChanged = false;

unsigned long SysModWeb::wsSendBytesCounter = 0;
unsigned long SysModWeb::wsSendJsonCounter = 0;
unsigned long SysModWeb::wsSendDataWsCounter = 0;

SysModWeb::SysModWeb() :SysModule("Web") {
  ws = new AsyncWebSocket("/ws");
  server = new AsyncWebServer(80);

  //CORS compatiblity
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Origin"), "*");
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Methods"), "*");
  DefaultHeaders::Instance().addHeader(F("Access-Control-Allow-Headers"), "*");

  responseDocLoopTask = new DynamicJsonDocument(2048);
  responseDocAsyncTCP = new DynamicJsonDocument(3072);
};

void SysModWeb::setup() {
  SysModule::setup();
  USER_PRINT_FUNCTION("%s %s\n", __PRETTY_FUNCTION__, name);

  parentVar = ui->initModule(parentVar, name);

  JsonObject tableVar = ui->initTable(parentVar, "clTbl", nullptr, false, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "Clients");
    web->addResponse(var["id"], "comment", "List of clients");
    JsonArray rows = web->addResponseA(var["id"], "table");
    web->clientsToJson(rows);
  });
  ui->initNumber(tableVar, "clNr", 0, 0, 999, true, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "Nr");
  });
  ui->initText(tableVar, "clIp", nullptr, 16, true, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "IP");
  });
  ui->initCheckBox(tableVar, "clIsFull", false, true, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "Is full");
  }, [](JsonObject var, uint8_t) { //chFun
    print->printJson("clIsFull.chFun", var);
  });
  ui->initSelect(tableVar, "clStatus", -1, true, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "Status");
    //tbd: not working yet in ui
    JsonArray select = web->addResponseA(var["id"], "select");
    select.add("Disconnected"); //0
    select.add("Connected"); //1
    select.add("Disconnecting"); //2
  });
  ui->initNumber(tableVar, "clLength", 0, 0, WS_MAX_QUEUED_MESSAGES, true, [](JsonObject var) { //uiFun
    web->addResponse(var["id"], "label", "Length");
  });

  ui->initText(parentVar, "wsSendBytes", nullptr, 16, true);
  ui->initText(parentVar, "wsSendJson", nullptr, 16, true);
  ui->initNumber(parentVar, "queueLength", WS_MAX_QUEUED_MESSAGES, 0, WS_MAX_QUEUED_MESSAGES, true);

  USER_PRINT_FUNCTION("%s %s %s\n", __PRETTY_FUNCTION__, name, success?"success":"failed");
}

void SysModWeb::loop() {
  // SysModule::loop();

  //currently not used as each variable is send individually
  if (this->modelUpdated) {
    sendDataWs(*mdl->model); //send new data, all clients, no def

    this->modelUpdated = false;
  }

}

void SysModWeb::loop1s() {
  // if something changed in clTbl
  if (clientsChanged) {
    clientsChanged = false;
    ui->processUiFun("clTbl"); //every second (temp)
  }

  uint8_t rowNr = 0;
  for (auto client:SysModWeb::ws->getClients()) {
    // print->printClient("up", client);
    mdl->setValueB("clIsFull", client->queueIsFull(), rowNr);
    mdl->setValueI("clStatus", client->status(), rowNr);
    mdl->setValueI("clLength", client->queueLength(), rowNr);
    rowNr++;
  }

  mdl->setValueLossy("wsSendBytes", "%lu /s", wsSendBytesCounter);
  wsSendBytesCounter = 0;
  mdl->setValueLossy("wsSendJson", "%lu /s", wsSendJsonCounter);
  wsSendJsonCounter = 0;
}

void SysModWeb::connectedChanged() {
  if (SysModules::isConnected) {
    ws->onEvent(wsEvent);
    // ws->onEvent(wsEvent2);
    server->addHandler(ws);

    server->begin();

    // USER_PRINTF("%s server (re)started\n", name); //causes crash for some reason...
    USER_PRINTF("server (re)started\n"); //and this not causes crash ??? whats with name?
  }
  //else remove handlers...
}

//WebSocket connection to 'ws://192.168.8.152/ws' failed: The operation couldn’t be completed. Protocol error
//WS error 192.168.8.126 9 (2)
//WS event data 0.0.0.0 (1) 0 0 0=0? 34 0
//WS pong 0.0.0.0 9 (1)
//wsEvent deserializeJson failed with code EmptyInput

// https://github.com/me-no-dev/ESPAsyncWebServer/blob/master/examples/ESP_AsyncFSBrowser/ESP_AsyncFSBrowser.ino
void SysModWeb::wsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  // if (!ws->count()) {
  //   USER_PRINT_Async("wsEvent no clients\n");
  //   return;
  // }
  if (type == WS_EVT_CONNECT) {
    print->printClient("WS client connected", client);
    sendDataWs(*mdl->model, client); //send definition to client
    clientsChanged = true;
  } else if (type == WS_EVT_DISCONNECT) {
    print->printClient("WS Client disconnected", client);
    clientsChanged = true;
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    // USER_PRINT_Async("  info %d %d %d=%d? %d %d\n", info->final, info->index, info->len, len, info->opcode, data[0]);
    if (info->final && info->index == 0 && info->len == len) { //not multipart
      print->printClient("WS event data", client);
      // the whole message is in a single frame and we got all of its data (max. 1450 bytes)
      if (info->opcode == WS_TEXT)
      {
        if (len > 0 && len < 10 && data[0] == 'p') {
          // application layer ping/pong heartbeat.
          // client-side socket layer ping packets are unresponded (investigate)
          // print->printClient("WS client pong", client); //crash?
          USER_PRINT_Async("pong\n");
          client->text("pong");
          return;
        }

        if (processWSFunc) { //processJson defined
          JsonDocument *responseDoc = web->getResponseDoc();
          responseDoc->clear(); //needed for deserializeJson?
          JsonVariant responseVariant = responseDoc->as<JsonVariant>();

          DeserializationError error = deserializeJson(*responseDoc, data, len); //data to responseDoc

          if (error || responseVariant.isNull()) {
            USER_PRINT_Async("wsEvent deserializeJson failed with code %s\n", error.c_str());
            client->text("{\"success\":true}"); // we have to send something back otherwise WS connection closes
          } else {
            const char * error = processWSFunc(responseVariant); //processJson, adds to responseDoc / responseVariant

            if (responseVariant.size()) {
              print->printJson("WS_EVT_DATA json", responseVariant);
              print->printJDocInfo("WS_EVT_DATA info", *responseDoc);

              //uiFun only send to requesting client
              if (responseVariant["uiFun"].isNull())
                sendDataWs(responseVariant);
              else
                sendDataWs(responseVariant, client);
            }
            else {
              USER_PRINT_Async("WS_EVT_DATA no responseDoc\n");
              client->text("{\"success\":true}"); // we have to send something back otherwise WS connection closes
            }
          }
        }
        else {
          USER_PRINT_Async("WS_EVT_DATA no processWSFunc\n");
          client->text("{\"success\":true}"); // we have to send something back otherwise WS connection closes
        }
        }
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          USER_PRINT_Async("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        USER_PRINT_Async("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      USER_PRINT_Async("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      }
      // else {
      //   char buff[3];
      //   for(size_t i=0; i < len; i++) {
      //     sprintf(buff, "%02x ", (uint8_t) data[i]);
      //     msg += buff ;
      //   }
      // }
      USER_PRINT_Async("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        USER_PRINT_Async("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          USER_PRINT_Async("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }

      //message is comprised of multiple frames or the frame is split into multiple packets
      //if(info->index == 0){
        //if (!wsFrameBuffer && len < 4096) wsFrameBuffer = new uint8_t[4096];
      //}

      //if (wsFrameBuffer && len < 4096 && info->index + info->)
      //{

      //}

      // if((info->index + len) == info->len){
      //   if(info->final){
      //     if(info->message_opcode == WS_TEXT) {
      //       client->text("{\"error\":9}"); //we do not handle split packets right now
      //       USER_PRINT_Async("WS multipart message: we do not handle split packets right now\n");
      //     }
      //   }
      // }
      // USER_PRINT_Async("WS multipart message f:%d i:%d len:%d == %d\n", info->final, info->index, info->len, len);
    }
  } else if (type == WS_EVT_ERROR){
    //error was received from the other end
    // print->printClient("WS error", client); //crashes
    // USER_PRINT_Async("WS error\n");
    USER_PRINT_Async("ws[%s][%u] error(): \n", server->url(), client->id());//, *((uint16_t*)arg));//, (char*)data);

  } else if (type == WS_EVT_PONG){
    //pong message was received (in response to a ping request maybe)
    // print->printClient("WS pong", client); //crashes!
    // USER_PRINT_Async("WS pong\n");
    // USER_PRINT_Async("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
    USER_PRINT_Async("ws[%s][%u] pong[%u]: \n", server->url(), client->id(), len);//, (len)?(char*)data:"");
  }
}

void SysModWeb::wsEvent2(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    USER_PRINT_Async("ws[%s][%u] connect\n", server->url(), client->id());
    // client->printf("Hello Client %u :)", client->id());
    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    USER_PRINT_Async("ws[%s][%u] disconnect\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    USER_PRINT_Async("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    USER_PRINT_Async("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      USER_PRINT_Async("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } 
      // else {
      //   char buff[3];
      //   for(size_t i=0; i < info->len; i++) {
      //     sprintf(buff, "%02x ", (uint8_t) data[i]);
      //     msg += buff ;
      //   }
      // }
      USER_PRINT_Async("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT)
        client->text("I got your text message");
      else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          USER_PRINT_Async("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        USER_PRINT_Async("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      USER_PRINT_Async("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < len; i++) {
          msg += (char) data[i];
        }
      }
      // else {
      //   char buff[3];
      //   for(size_t i=0; i < len; i++) {
      //     sprintf(buff, "%02x ", (uint8_t) data[i]);
      //     msg += buff ;
      //   }
      // }
      USER_PRINT_Async("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        USER_PRINT_Async("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          USER_PRINT_Async("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT)
            client->text("I got your text message");
          else
            client->binary("I got your binary message");
        }
      }
    }
  }
}


void SysModWeb::sendDataWs(JsonVariant json, AsyncWebSocketClient * client) {
  if (!ws) {
    USER_PRINT_Async("sendDataWs no ws\n");
    return;
  }

  wsSendDataWsCounter++;
  if (wsSendDataWsCounter > 1) {
    USER_PRINT_Async("sendDataWs parallel %d %s\n", wsSendDataWsCounter, pcTaskGetTaskName(NULL));
    wsSendDataWsCounter--;
    return;
  }

  wsSendJsonCounter++;
  ws->cleanupClients();

  if (ws->count()) {
    size_t len = measureJson(json);
    AsyncWebSocketMessageBuffer *wsBuf = ws->makeBuffer(len);

    if (wsBuf) {
      wsBuf->lock();
      serializeJson(json, (char *)wsBuf->get(), len);
      if (client) {
        if (client->status() == WS_CONNECTED && !client->queueIsFull())
          client->text(wsBuf);
        else 
          print->printClient("sendDataWs client full or not connected", client);

        // DEBUG_PRINTLN("to a single client.");
      } else {
        for (auto client:ws->getClients()) {
          if (client->status() == WS_CONNECTED && !client->queueIsFull())
            client->text(wsBuf);
          else 
            // printClient("sendDataWs client(s) full or not connected", client); //crash!!
            USER_PRINT_Async("sendDataWs client full or not connected\n");
        }
        // DEBUG_PRINTLN("to multiple clients.");
      }
      wsBuf->unlock();
      ws->_cleanBuffers();
    }
    else {
      USER_PRINT_Async("sendDataWs WS buffer allocation failed\n");
      ws->closeAll(1013); //code 1013 = temporary overload, try again later
      ws->cleanupClients(); //disconnect all clients to release memory
      ws->_cleanBuffers();
    }
  }
  wsSendDataWsCounter--;
}

//add an url to the webserver to listen to
bool SysModWeb::addURL(const char * uri, const char * contentType, const char * path, const uint8_t * content, size_t len) {
  server->on(uri, HTTP_GET, [uri, contentType, path, content, len](AsyncWebServerRequest *request) {
    if (path) {
      USER_PRINT_Async("Webserver: addUrl %s %s file: %s", uri, contentType, path);
      request->send(LittleFS, path, contentType);
    }
    else {
      USER_PRINT_Async("Webserver: addUrl %s %s csdata %d-%d (%s)", uri, contentType, content, len, request->url().c_str());

      // if (handleIfNoneMatchCacheHeader(request)) return;

      AsyncWebServerResponse *response;
      response = request->beginResponse_P(200, contentType, content, len);
      response->addHeader(FPSTR("Content-Encoding"),"gzip");
      // setStaticContentCacheHeaders(response);
      request->send(response);

      USER_PRINT_Async("!\n");
    }
  });

  return true;
}

//not used at the moment
bool SysModWeb::processURL(const char * uri, void (*func)(AsyncWebServerRequest *)) {
  server->on(uri, HTTP_GET, [uri, func](AsyncWebServerRequest *request) {
    func(request);
  });
  return true;
}

bool SysModWeb::addUpload(const char * uri) {

  // curl -F 'data=@fixture1.json' 192.168.8.213/upload
  server->on(uri, HTTP_POST, [](AsyncWebServerRequest *request) {},
  [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                size_t len, bool final) {
    USER_PRINT_Async("handleUpload r:%s f:%s i:%d l:%d f:%d\n", request->url().c_str(), filename.c_str(), index, len, final);
    if (!index) {
      String finalname = filename;
      if (finalname.charAt(0) != '/') {
        finalname = '/' + finalname; // prepend slash if missing
      }

      request->_tempFile = files->open(finalname.c_str(), "w");
      // DEBUG_PRINT("Uploading ");
      // DEBUG_PRINTLN(finalname);
      // if (finalname.equals("/presets.json")) presetsModifiedTime = toki.second();
    }
    if (len) {
      request->_tempFile.write(data,len);
    }
    if (final) {
      request->_tempFile.close();
      // USER_PRINT("File uploaded: ");  // WLEDMM
      // USER_PRINTLN(filename);            // WLEDMM
      // if (filename.equalsIgnoreCase("/cfg.json") || filename.equalsIgnoreCase("cfg.json")) { // WLEDMM
      //   request->send(200, "text/plain", F("Configuration restore successful.\nRebooting..."));
      //   doReboot = true;
      // } else {
      //   if (filename.equals("/presets.json") || filename.equals("presets.json")) {  // WLEDMM
      //     request->send(200, "text/plain", F("Presets File Uploaded!"));
      //   } else
          request->send(200, "text/plain", F("File Uploaded!"));
      // }
      // cacheInvalidate++;
     files->filesChange();
    }
  });
  return true;
}

bool SysModWeb::addUpdate(const char * uri) {

  // curl -F 'data=@fixture1.json' 192.168.8.213/upload
  server->on(uri, HTTP_POST, [](AsyncWebServerRequest *request) {},
  [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data,
                size_t len, bool final) {
    USER_PRINT_Async("handleUpdate r:%s f:%s i:%d l:%d f:%d\n", request->url().c_str(), filename.c_str(), index, len, final);
    if(!index){
      USER_PRINTF("OTA Update Start\n");
      // WLED::instance().disableWatchdog();
      // usermods.onUpdateBegin(true); // notify usermods that update is about to begin (some may require task de-init)
      // lastEditTime = millis(); // make sure PIN does not lock during update
      Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000);
    }
    if (!Update.hasError()) Update.write(data, len);
    if (final) {
      char message[64];
      const char * serverName = mdl->getValue("serverName");

      print->fFormat(message, sizeof(message)-1, "Update of %s (...%d) %s", serverName, WiFi.localIP()[3], Update.end(true)?"Successful":"Failed");

      USER_PRINTF("%s\n", message);
      request->send(200, "text/plain", message);

      // usermods.onUpdateBegin(false); // notify usermods that update has failed (some may require task init)
      // WLED::instance().enableWatchdog();
    }
  });
  return true;
}

bool SysModWeb::addFileServer(const char * uri) {

  server->on(uri, HTTP_GET, [uri](AsyncWebServerRequest *request){
    const char * ddd = request->url().c_str();
    const char * path = ddd + strlen(uri); //remove the uri from the path (skip their positions)
    USER_PRINT_Async("fileServer request %s %s %s\n", uri, request->url().c_str(), path);
    if(LittleFS.exists(path)) {
      request->send(LittleFS, path, "text/plain");//"application/json");
    }
  });
  return true;
}

bool SysModWeb::setupJsonHandlers(const char * uri, const char * (*processFunc)(JsonVariant &)) {
  processWSFunc = processFunc; //for WebSocket requests

  //URL handler, e.g. for curl calls
  AsyncCallbackJsonWebHandler *handler = new AsyncCallbackJsonWebHandler(uri, [processFunc](AsyncWebServerRequest *request, JsonVariant &json) {
    JsonDocument *responseDoc = web->getResponseDoc();
    responseDoc->clear(); //needed for deserializeJson?
    JsonVariant responseVariant = responseDoc->as<JsonVariant>();

    print->printJson("AsyncCallbackJsonWebHandler", json);
    const char * pErr = processFunc(json); //processJson

    //WLED compatibility
    if (json["v"]) { //WLED compatibility: verbose response
      serveJson (request);
    }
    else
    if (responseVariant.size()) { //responseVariant set by processFunc
      char resStr[200]; 
      serializeJson(responseVariant, resStr, 200);
      USER_PRINT_Async("processJsonUrl response %s\n", resStr);
      request->send(200, "application/json", resStr);
    }
    else
      // request->send(200, "text/plain", "OK");
      request->send(200, "application/json", F("{\"success\":true}"));
  });
  server->addHandler(handler);
  return true;
}

void SysModWeb::addResponse(const char * id, const char * key, const char * value) {
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  responseVariant[id][key] = (char *)value; //create a copy!!
}

void SysModWeb::addResponseArray(const char * id, const char * key, JsonArray value) {
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  responseVariant[id][key] = value;
}

void SysModWeb::addResponseV(const char * id, const char * key, const char * format, ...) {
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  va_list args;
  va_start(args, format);

  // size_t len = vprintf(format, args);
  char value[128];
  vsnprintf(value, sizeof(value)-1, format, args);

  va_end(args);

  responseVariant[id][key] = value;
}

void SysModWeb::addResponseI(const char * id, const char * key, int value) { //temporary, use overloading
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  responseVariant[id][key] = value;
}
void SysModWeb::addResponseB(const char * id, const char * key, bool value) { //temporary, use overloading
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  responseVariant[id][key] = value;
}
JsonArray SysModWeb::addResponseA(const char * id, const char * key) {
  JsonVariant responseVariant = getResponseDoc()->as<JsonVariant>();
  if (responseVariant[id].isNull()) responseVariant.createNestedObject(id);
  return responseVariant[id].createNestedArray(key);
}

void SysModWeb::clientsToJson(JsonArray array, bool nameOnly, const char * filter) {
  for (auto client:ws->getClients()) {
    if (nameOnly) {
      array.add((char *)client->remoteIP().toString().c_str()); //create a copy!
    } else {
      // USER_PRINTF("Client %d %d ...%d\n", client->id(), client->queueIsFull(), client->remoteIP()[3]);
      JsonArray row = array.createNestedArray();
      row.add(client->id());
      row.add((char *)client->remoteIP().toString().c_str()); //create a copy!
      row.add(client->queueIsFull());
      row.add(client->status());
      row.add(client->queueLength());
    }
  }
}

JsonDocument * SysModWeb::getResponseDoc() {
  // USER_PRINTF("response wsevent core %d %s\n", xPortGetCoreID(), pcTaskGetTaskName(NULL));

  return strncmp(pcTaskGetTaskName(NULL), "loopTask", 8) == 0?web->responseDocLoopTask:web->responseDocAsyncTCP;
}

void SysModWeb::serveJson(AsyncWebServerRequest *request) {

  AsyncJsonResponse * response;

  // return model.json
  if (request->url().indexOf("mdl") > 0) {
    JsonArray model = mdl->model->as<JsonArray>();
    USER_PRINTF("serveJson model ...%d, %s %d %d %d %d\n", request->client()->remoteIP()[3], request->url().c_str(), model.size(),  measureJson(model), model.memoryUsage(), mdl->model->capacity());

    response = new AsyncJsonResponse(true,  model.memoryUsage()); //array tbd: here copy is mode, see WLED for using reference
    JsonArray root = response->getRoot();

    // root = module does not work? so add each element individually
    for (JsonObject module: model)
      root.add(module);
  }
  // nodes crashes!
  // else if (request->url().indexOf("nodes") > 0) {
  //   JsonArray model = mdl->model->as<JsonArray>();
  //   USER_PRINTF("serveJson model ...%d, %s %d %d %d %d\n", request->client()->remoteIP()[3], request->url().c_str(), model.size(),  measureJson(model), model.memoryUsage(), mdl->model->capacity());

  //   response = new AsyncJsonResponse(true, 5000); //array
  //   JsonArray root = response->getRoot();

  //   for (auto node=instances->nodes.begin(); node!=instances->nodes.end(); ++node) {
  //     root.add(node->name);
  //   }
  // }
  else { //WLED compatible
    USER_PRINTF("serveJson ...%d, %s\n", request->client()->remoteIP()[3], request->url().c_str());
    response = new AsyncJsonResponse(false, 5000); //object
    JsonObject root = response->getRoot();

    //temporary set all WLED variables (as otherwise WLED-native does not show the instance): tbd: clean up (state still needed, info not)
    const char* jsonState = "{\"transition\":7,\"ps\":9,\"pl\":-1,\"nl\":{\"on\":false,\"dur\":60,\"mode\":1,\"tbri\":0,\"rem\":-1},\"udpn\":{\"send\":false,\"recv\":true},\"lor\":0,\"mainseg\":0,\"seg\":[{\"id\":0,\"start\":0,\"stop\":144,\"len\":144,\"grp\":1,\"spc\":0,\"of\":0,\"on\":true,\"frz\":false,\"bri\":255,\"cct\":127,\"col\":[[182,15,98,0],[0,0,0,0],[255,224,160,0]],\"fx\":0,\"sx\":128,\"ix\":128,\"pal\":11,\"c1\":8,\"c2\":20,\"c3\":31,\"sel\":true,\"rev\":false,\"mi\":false,\"o1\":false,\"o2\":false,\"o3\":false,\"ssim\":0,\"mp12\":1}]}";
    StaticJsonDocument<5000> docState;
    deserializeJson(docState, jsonState);
    root["state"] = docState;

    root["state"]["bri"] = mdl->getValue("bri");
    root["state"]["on"] = mdl->getValue("on").as<bool>();
    root["info"]["name"] = mdl->getValue("serverName");
    root["info"]["arch"] = "esp32"; //platformName

    root["info"]["rel"] = "StarMod";
    root["info"]["ver"] = "0.0.1";
    root["info"]["vid"] = mdl->getValue("version").as<uint32_t>(); //WLED-native needs int otherwise status offline!!!
    root["info"]["leds"]["count"] = 999;
    root["info"]["leds"]["countP"] = 998;
    root["info"]["leds"]["fps"] = mdl->getValue("fps"); //tbd: should be realFps but is ro var
    root["info"]["wifi"]["rssi"] = WiFi.RSSI();// mdl->getValue("rssi"); (ro)

    String escapedMac;
    escapedMac = WiFi.macAddress();
    escapedMac.replace(":", "");
    escapedMac.toLowerCase();
    root["info"]["mac"] = (char *)escapedMac.c_str(); //copy mdns->escapedMac gives LoadProhibited crash, tbd: find out why
    root["info"]["ip"] = (char *)WiFi.localIP().toString().c_str();
    // print->printJson("serveJson", root);
  }

  response->setLength();
  request->send(response);
} //serveJson