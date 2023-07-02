#include "Module.h"

class SysModSystem:public Module {

public:

  unsigned long loopCounter = 0;

  SysModSystem() :Module("System") {};

  void setup() {
    Module::setup();
    print->print("%s %s\n", __PRETTY_FUNCTION__, name);

    parentObject = ui->initGroup(parentObject, name);

    ui->initDisplay(parentObject, "UpTime", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", "Uptime");
      web->addResponse(object, "comment", "Uptime of board");
    });
    ui->initDisplay(parentObject, "Loops");
    ui->initDisplay(parentObject, "Heap", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", object["id"]);
      web->addResponse(object, "comment", "Free / Total (largest free)");
    });
    ui->initDisplay(parentObject, "Stack");

    ui->initButton(parentObject, "restart", "Restart", [](JsonObject object) {
      ws.closeAll(1012);
      ESP.restart();
    });

    //should be in SysModFiles...
    files->parentObject = ui->initGroup(files->parentObject, "files");

    JsonObject dirObject = ui->initMany(files->parentObject, "flist", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", "Files");
      web->addResponse(object, "comment", "List of files");
      JsonArray rows = web->addResponseArray(object, "many");
      files->dirToJson(rows);
    });
    ui->initDisplay(dirObject, "fName", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", "Name");
    });
    ui->initDisplay(dirObject, "fSize", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", "Size (B)");
    });

    ui->initDisplay(files->parentObject, "dsize", nullptr, nullptr, [](JsonObject object) {
      web->addResponse(object, "label", "Total FS size");
    });

    print->print("%s %s %s\n", __PRETTY_FUNCTION__, name, success?"success":"failed");
  }

  void loop() {
    // Module::loop();

    loopCounter++;
    if (millis() - secondMillis >= 1000 || !secondMillis) {
      secondMillis = millis();
      ui->setValueV("UpTime", "%u s", millis()/1000);
      ui->setValueV("Loops", "%lu /s", loopCounter);
      ui->setValueV("Heap", "%d / %d (%d) B", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMaxAllocHeap());
      ui->setValueV("Stack", "%d B", uxTaskGetStackHighWaterMark(NULL));

      //should be in SysModFiles...
      ui->setValueV("dsize", "%d / %d B", files->usedBytes(), files->totalBytes());

      loopCounter = 0;
    }
  }

};

static SysModSystem *sys;