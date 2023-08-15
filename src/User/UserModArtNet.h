/*
   @title     StarMod
   @file      UserModArtNet.h
   @date      20230810
   @repo      https://github.com/ewowi/StarMod
   @Authors   https://github.com/ewowi/StarMod/commits/main
   @Copyright (c) 2023 Github StarMod Commit Authors
   @license   GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
*/

#define ARTNET_DEFAULT_PORT 6454

static const size_t ART_NET_HEADER_SIZE = 12;
static const byte   ART_NET_HEADER[] PROGMEM = {0x41,0x72,0x74,0x2d,0x4e,0x65,0x74,0x00,0x00,0x50,0x00,0x0e};

class UserModArtNet:public Module {

public:

  static IPAddress targetIp; //tbd: targetip also configurable from fixtures and artnet instead of pin output

  UserModArtNet() :Module("ArtNet") {
    print->print("%s %s\n", __PRETTY_FUNCTION__, name);

    isEnabled = false; //default off

    print->print("%s %s %s\n", __PRETTY_FUNCTION__, name, success?"success":"failed");
  };

  //setup filesystem
  void setup() {
    Module::setup();
    print->print("%s %s\n", __PRETTY_FUNCTION__, name);

    parentVar = ui->initModule(parentVar, name);

    ui->initSelect(parentVar, "artInst", -1, false, [](JsonObject var) { //uiFun
      web->addResponse(var["id"], "label", "Instance");
      web->addResponse(var["id"], "comment", "Instance to send data");
      JsonArray select = web->addResponseA(var["id"], "select");
      for (NodeInfo node: UserModInstances::nodes) {
        char option[32] = { 0 };
        strncpy(option, node.ip.toString().c_str(), sizeof(option)-1);
        strncat(option, " ", sizeof(option)-1);
        strncat(option, node.details, sizeof(option)-1);
        select.add(option);
      }
    }, [](JsonObject var) { //chFun
      size_t ddpInst = var["value"];
      if (ddpInst >=0 && ddpInst < UserModInstances::nodes.size()) {
        targetIp = UserModInstances::nodes[ddpInst].ip;
        print->print("Start ArtNet to %s\n", targetIp.toString().c_str());
      }
    }); //ddpInst

    print->print("%s %s %s\n", __PRETTY_FUNCTION__, name, success?"success":"failed");
  }

  void loop(){
    // Module::loop();

    if(!SysModModules::isConnected) return;

    if(!targetIp) return;

    if(!lds->newFrame) return;

    // calculate the number of UDP packets we need to send
    bool isRGBW = false;

    const size_t channelCount = ledsV.nrOfLedsP * (isRGBW?4:3); // 1 channel for every R,G,B,(W?) value
    const size_t ARTNET_CHANNELS_PER_PACKET = isRGBW?512:510; // 512/4=128 RGBW LEDs, 510/3=170 RGB LEDs
    const size_t packetCount = ((channelCount-1)/ARTNET_CHANNELS_PER_PACKET)+1;

    uint32_t channel = 0; 
    size_t bufferOffset = 0;

    sequenceNumber++;

    WiFiUDP ddpUdp;

    int bri = mdl->getValue("bri");

    for (size_t currentPacket = 0; currentPacket < packetCount; currentPacket++) {

      if (sequenceNumber > 255) sequenceNumber = 0;

      if (!ddpUdp.beginPacket(targetIp, ARTNET_DEFAULT_PORT)) {
        print->print("Art-Net WiFiUDP.beginPacket returned an error\n");
        return; // borked
      }

      size_t packetSize = ARTNET_CHANNELS_PER_PACKET;

      if (currentPacket == (packetCount - 1U)) {
        // last packet
        if (channelCount % ARTNET_CHANNELS_PER_PACKET) {
          packetSize = channelCount % ARTNET_CHANNELS_PER_PACKET;
        }
      }

      byte header_buffer[ART_NET_HEADER_SIZE];
      memcpy_P(header_buffer, ART_NET_HEADER, ART_NET_HEADER_SIZE);
      ddpUdp.write(header_buffer, ART_NET_HEADER_SIZE); // This doesn't change. Hard coded ID, OpCode, and protocol version.
      ddpUdp.write(sequenceNumber & 0xFF); // sequence number. 1..255
      ddpUdp.write(0x00); // physical - more an FYI, not really used for anything. 0..3
      ddpUdp.write((currentPacket) & 0xFF); // Universe LSB. 1 full packet == 1 full universe, so just use current packet number.
      ddpUdp.write(0x00); // Universe MSB, unused.
      ddpUdp.write(0xFF & (packetSize >> 8)); // 16-bit length of channel data, MSB
      ddpUdp.write(0xFF & (packetSize     )); // 16-bit length of channel data, LSB

      for (size_t i = 0; i < ledsV.nrOfLedsP; i++) {
        CRGB pixel = ledsP[i];
        ddpUdp.write(scale8(pixel.r, bri)); // R
        ddpUdp.write(scale8(pixel.g, bri)); // G
        ddpUdp.write(scale8(pixel.b, bri)); // B
        // if (isRGBW) ddpUdp.write(scale8(buffer[bufferOffset++], bri)); // W
      }

      if (!ddpUdp.endPacket()) {
        print->print("Art-Net WiFiUDP.endPacket returned an error\n");
        return; // borked
      }
      channel += packetSize;
    }

  }

  private:
    size_t sequenceNumber = 0;

};

static UserModArtNet *artnetmod;

IPAddress UserModArtNet::targetIp;