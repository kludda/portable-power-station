/*

*/

#define PIN_BMS_RX 10
#define PIN_BMS_TX 11

#define PIN_BMS_LOAD_ACTIVE 8
#define PIN_BMS_LOAD_ACTIVE_INVERT false

#define BMS_POLL_TIME_MS (1300l)
#define BMS_POLL_TIMEOUT_MS (60*1000l)
#define COM_TIMOUT_MS (250)

#define VERBOSE_LEVEL -1 // -1 for off

#include <SoftwareSerial.h>

struct packet_t {
    enum state_t {
        NONE,
        START,
        STATUS,
        CMD,
        PAYLOAD_LENGTH,
        PAYLOAD,
        CHECK_0,
        CHECK_1,
        STOP,
        DONE,
        ERR,
    };
    enum {
        PAYLOAD_OFFSET = 4,
    };
    enum readStatus_t {
        READ_DONE,
        READ_RECV,
        READ_ERROR,
    };
    state_t nextState;
    int cur;
    uint8_t stat;
    uint8_t cmd;
    uint8_t len;
    uint8_t lenRead;
    uint16_t check;
    uint8_t data[256 + 5];
};

void resetPackage(packet_t& pkg)
{
    pkg.nextState = packet_t::NONE;
    pkg.cur = 0;
    pkg.stat = 0;
    pkg.cmd = 0;
    pkg.len = 0;
    pkg.lenRead = 0;
    pkg.check = 0;
    memset(pkg.data, 0, sizeof(pkg.data));
}

const char* packetStateToStr(packet_t::state_t state)
{
    switch (state) {
    case packet_t::NONE:
        return "NONE";
    case packet_t::START:
        return "START";
    case packet_t::STATUS:
        return "STATUS";
    case packet_t::CMD:
        return "CMD";
    case packet_t::PAYLOAD_LENGTH:
        return "PAYLOAD_LENGTH";
    case packet_t::PAYLOAD:
        return "PAYLOAD";
    case packet_t::CHECK_0:
        return "CHECK_0";
    case packet_t::CHECK_1:
        return "CHECK_1";
    case packet_t::STOP:
        return "STOP";
    case packet_t::DONE:
        return "DONE";
    case packet_t::ERR:
        return "ERR";
    }
    return "uknown";
}

const char* packetReadStatusToStr(packet_t::readStatus_t status)
{
    switch (status) {
    case packet_t::READ_DONE:
        return "READ_DONE";
    case packet_t::READ_RECV:
        return "READ_RECV";
    case packet_t::READ_ERROR:
        return "READ_ERROR";
    }
    return "uknown";
}

packet_t::readStatus_t readPacket(int data, packet_t& pkg, bool fromBms)
{
    switch (pkg.nextState) {
    case packet_t::NONE:
        if (data != 0xDD) {
            pkg.nextState = packet_t::ERR;
            return packet_t::READ_ERROR;
        }
        pkg.nextState = fromBms ? packet_t::CMD : packet_t::STATUS;
        break;
    case packet_t::STATUS:
        pkg.nextState = fromBms ? packet_t::PAYLOAD_LENGTH : packet_t::CMD;
        pkg.stat = data;
        break;
    case packet_t::CMD:
        pkg.nextState = fromBms ? packet_t::STATUS : packet_t::PAYLOAD_LENGTH;
        pkg.cmd = data;
        break;
    case packet_t::PAYLOAD_LENGTH:
        pkg.len = data;
        pkg.lenRead = 0;
        if (pkg.len > 0) {
            pkg.nextState = packet_t::PAYLOAD;
        } else {
            pkg.nextState = packet_t::CHECK_0;
        }

        break;
    case packet_t::PAYLOAD:
        pkg.lenRead++;
        if (pkg.lenRead == pkg.len) {
            pkg.nextState = packet_t::CHECK_0;
        }
        break;
    case packet_t::CHECK_0:
        pkg.check = (data << 8);
        pkg.nextState = packet_t::CHECK_1;
        break;
    case packet_t::CHECK_1:
        pkg.check = pkg.check | data;
        pkg.nextState = packet_t::STOP;
        break;
    case packet_t::STOP:
        if (data != 0x77) {
            pkg.nextState = packet_t::ERR;
            return packet_t::READ_ERROR;
        }
        pkg.nextState = packet_t::DONE;
        break;
    }

    pkg.data[pkg.cur++] = data;

    if (pkg.nextState == packet_t::DONE) {
        return packet_t::READ_DONE;
    } else if (pkg.nextState != packet_t::NONE) {
        return packet_t::READ_RECV;
    } else {
        return packet_t::READ_ERROR;
    }
}

uint16_t packetCalcChecksum(const packet_t& pkg)
{
    uint16_t sum = 0;
    for (int iB = 0; iB < pkg.len + 2; iB++) {
        sum += pkg.data[iB + 2];
    }
    return 0xFFFF - sum + 1;
}

void printPackage(const packet_t& pkg)
{
    Serial.print("package:\n");
    Serial.print("state:");
    Serial.print(packetStateToStr(pkg.nextState));

    Serial.print("\nstatus:");
    Serial.print(pkg.stat, HEX);

    Serial.print("\ncmd:");
    Serial.print(pkg.cmd, HEX);

    Serial.print("\nlen:");
    Serial.print(pkg.len, HEX);

    Serial.print("\ncheck:");
    Serial.print(pkg.check, HEX);

    Serial.print("\n");

    if (pkg.len > 0) {
        Serial.print("payload: ");
        for (int iP = 0; iP < pkg.len; iP++) {
            Serial.print(" ");
            Serial.print(pkg.data[iP + packet_t::PAYLOAD_OFFSET], HEX);
        }
        Serial.print("\n");
    }

    Serial.print("full pkg: ");
    for (int iP = 0; iP < pkg.cur; iP++) {
        Serial.print(" ");
        Serial.print(pkg.data[iP], HEX);
    }
    Serial.print("\n");
}

void generateRequestPackage(packet_t& pkg, int command)
{
    pkg.data[0] = 0xdd;
    pkg.data[1] = 0xa5;
    pkg.data[2] = command;
    pkg.data[3] = 0x00;
    pkg.len = 0;
    uint16_t checksum = packetCalcChecksum(pkg);
    pkg.data[4] = checksum >> 8;
    pkg.data[5] = checksum & 0xff;
    pkg.data[6] = 0x77;
    pkg.cur = 7;
}

void setLoadState( bool state ) {
  digitalWrite(LED_BUILTIN, state ? HIGH : LOW);

  if( PIN_BMS_LOAD_ACTIVE_INVERT ) {
    digitalWrite(PIN_BMS_LOAD_ACTIVE, state ? LOW : HIGH);
  } else  {
    digitalWrite(PIN_BMS_LOAD_ACTIVE, state ? HIGH : LOW);
  }
  
}

enum sysState_t {
    SYS_STATE_NONE,
    SYS_STATE_WAITING,
    SYS_STATE_BT_RECV,
    SYS_STATE_BT_SEND,
    SYS_STATE_BMS_RECV,
    SYS_STATE_BMS_SEND,
    SYS_STATE_POLL_SEND,
    SYS_STATE_POLL_RECV
};

const char* sysStateToStr(int state)
{
    switch (state) {
    case SYS_STATE_NONE:
        return "SYS_STATE_NONE";
    case SYS_STATE_WAITING:
        return "SYS_STATE_WAITING";
    case SYS_STATE_BT_RECV:
        return "SYS_STATE_BT_RECV";
    case SYS_STATE_BT_SEND:
        return "SYS_STATE_BT_SEND";
    case SYS_STATE_BMS_RECV:
        return "SYS_STATE_BMS_RECV";
    case SYS_STATE_BMS_SEND:
        return "SYS_STATE_BMS_SEND";
    case SYS_STATE_POLL_SEND:
        return "SYS_STATE_POLL_SEND";
    case SYS_STATE_POLL_RECV:
        return "SYS_STATE_POLL_RECV";
    }
    return "unknown";
}


SoftwareSerial SoftSerial(PIN_BMS_RX, PIN_BMS_TX); // RX, TX

packet_t passPacket;
packet_t passPacket2;

bool g_blinker = false;
sysState_t g_sysState = SYS_STATE_WAITING;
sysState_t g_sysLastState = SYS_STATE_NONE;

unsigned long g_lastStateChange = 0;
unsigned long g_lastPollRequest = 0;
unsigned long g_lastPollReply = 0;

#if VERBOSE_LEVEL >= 2
#define PRINT_LEVEL_2( print_arg ) print_arg
#else
#define PRINT_LEVEL_2( print_arg )
#endif
#if VERBOSE_LEVEL >= 1
#define PRINT_LEVEL_1( print_arg ) print_arg
#else
#define PRINT_LEVEL_1( print_arg )
#endif
#if VERBOSE_LEVEL >= 0
#define PRINT_LEVEL_0( print_arg ) print_arg
#else
#define PRINT_LEVEL_0( print_arg )
#endif


void setup()
{
    // initialize digital pin LED_BUILTIN as an output.
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PIN_BMS_LOAD_ACTIVE, OUTPUT);
    setLoadState( false );

    Serial.begin(38400);
    Serial1.begin(9600);
    SoftSerial.begin(9600);
    resetPackage(passPacket);
    resetPackage(passPacket2);
    g_sysState = SYS_STATE_WAITING;

    while (!Serial) {
        ; // wait for serial port to connect. Needed for native USB port only
    }

    g_lastStateChange = millis();
}

void loop()
{
    long loopStartTime = millis();

    if (g_sysState != g_sysLastState) {
        PRINT_LEVEL_1( Serial.print("sys state: ") );
        PRINT_LEVEL_1( Serial.print(sysStateToStr(g_sysLastState)) );
        PRINT_LEVEL_1( Serial.print(" -> ") );
        PRINT_LEVEL_1( Serial.print(sysStateToStr(g_sysState)) );
        PRINT_LEVEL_1( Serial.print("\n") );
        g_lastStateChange = loopStartTime;
        g_sysLastState = g_sysState;
    }

    if (g_sysState != SYS_STATE_WAITING
            && loopStartTime - g_lastStateChange > 250
        || g_lastStateChange > loopStartTime) {
        PRINT_LEVEL_0( Serial.print("timeout at  ") );
        PRINT_LEVEL_0( Serial.print(sysStateToStr(g_sysLastState)) );
        PRINT_LEVEL_0( Serial.print(" at:") );
        PRINT_LEVEL_0( Serial.print(loopStartTime) );
        PRINT_LEVEL_0( Serial.print("\n") );
        g_sysState = SYS_STATE_WAITING;
        resetPackage(passPacket);
        return;
    }

    if (g_sysState == SYS_STATE_WAITING && loopStartTime - g_lastPollReply > BMS_POLL_TIME_MS) {
        g_sysState = SYS_STATE_POLL_SEND;
    }

    if ( loopStartTime - g_lastPollReply > BMS_POLL_TIMEOUT_MS ) {
      PRINT_LEVEL_0( Serial.print("No poll reply in 60 seconds, disable load\n") );
      setLoadState( false );
    }

    // Serial.print("loop\n");
    if (g_sysState == SYS_STATE_WAITING) {
        if (Serial1.available()) {
            resetPackage(passPacket);
            g_sysState = SYS_STATE_BT_RECV;
        }
    } else if (g_sysState == SYS_STATE_BT_RECV) {
        while (Serial1.available()) {
            int inByte = Serial1.read();
            //  SoftSerial.write(inByte);
            packet_t::readStatus_t readStatus = readPacket(inByte, passPacket, false);
            if (readStatus == packet_t::READ_ERROR) {
                PRINT_LEVEL_0( Serial.print("bt: error\n") );
                PRINT_LEVEL_1( printPackage(passPacket) );
                PRINT_LEVEL_0( Serial.print("\n") );
                resetPackage(passPacket);
                g_sysState = SYS_STATE_WAITING;
            } else if (readStatus == packet_t::READ_DONE) {
                PRINT_LEVEL_1( Serial.print("bt: done\n"));
                uint16_t pkgCheck = packetCalcChecksum(passPacket);
                if (pkgCheck != passPacket.check) {
                    PRINT_LEVEL_0( Serial.print("bt: check err:\n") );
                    PRINT_LEVEL_0( Serial.print(pkgCheck, HEX) );
                    PRINT_LEVEL_0( Serial.print(" - ") );
                    PRINT_LEVEL_0( Serial.print(passPacket.check, HEX) );
                    PRINT_LEVEL_0( Serial.print("\n") );
                    g_sysState = SYS_STATE_WAITING;
                }
                PRINT_LEVEL_2( printPackage(passPacket) );
                PRINT_LEVEL_1( Serial.print("\n") );
                g_sysState = SYS_STATE_BMS_SEND;
            }
        }
    } else if (g_sysState == SYS_STATE_BT_SEND) {

        for (int iB = 0; iB < passPacket.cur; iB++) {
            Serial1.write(passPacket.data[iB]);
        }
        resetPackage(passPacket);
        g_sysState = SYS_STATE_WAITING;
    } else if (g_sysState == SYS_STATE_BMS_RECV || g_sysState == SYS_STATE_POLL_RECV) {
        while (SoftSerial.available()) {
            int inByte = SoftSerial.read();
            //      Serial.print("bms:");
            //  Serial.print(inByte, HEX);
            //  Serial.print("\n");
            packet_t::readStatus_t readStatus = readPacket(inByte, passPacket, true);
            if (readStatus == packet_t::READ_ERROR) {
                PRINT_LEVEL_0(Serial.print("bms: error\n"));
                PRINT_LEVEL_1(printPackage(passPacket));
                PRINT_LEVEL_0(Serial.print("\n"));
                resetPackage(passPacket);
                g_sysState = SYS_STATE_WAITING;
            } else if (readStatus == packet_t::READ_DONE) {
                PRINT_LEVEL_1(Serial.print("bms: done\n"));
                uint16_t pkgCheck = packetCalcChecksum(passPacket);
                if (pkgCheck != passPacket.check) {
                    PRINT_LEVEL_0(Serial.print("bms: check err:\n"));
                    PRINT_LEVEL_0(Serial.print(pkgCheck, HEX));
                    PRINT_LEVEL_0(Serial.print(" - "));
                    PRINT_LEVEL_0(Serial.print(passPacket.check, HEX));
                    PRINT_LEVEL_0(Serial.print("\n"));
                    resetPackage(passPacket);
                    g_sysState = SYS_STATE_WAITING;
                }
                PRINT_LEVEL_2(printPackage(passPacket));
                PRINT_LEVEL_1(Serial.print("\n"));
                if (g_sysState == SYS_STATE_BMS_RECV) {
                    g_sysState = SYS_STATE_BT_SEND;
                } else {
                    PRINT_LEVEL_1(Serial.print("bms poll: done\n"));
                    if (passPacket.stat == 0 && passPacket.cmd == 3
                        && passPacket.len >= 20) {
                        g_lastPollReply = loopStartTime;
                        uint8_t fetState = passPacket.data[packet_t::PAYLOAD_OFFSET + 20];
                        bool chargeFet = (fetState & 0x1) != 0;
                        bool loadFet = (fetState & 0x2) != 0;
                        PRINT_LEVEL_0(Serial.print("bms poll: chargeFet: "));
                        PRINT_LEVEL_0(Serial.print(chargeFet ? "on " : "off "));
                        PRINT_LEVEL_0(Serial.print(" loadFet: "));
                        PRINT_LEVEL_0(Serial.print(loadFet ? "on " : "off "));
                        PRINT_LEVEL_0(Serial.print("\n"));
                        
                        setLoadState( loadFet );
                    } else {
                       PRINT_LEVEL_0( Serial.print("bms poll: uknown response: register:"));
                        PRINT_LEVEL_0(Serial.print(passPacket.stat));
                        PRINT_LEVEL_0(Serial.print("status: "));
                        PRINT_LEVEL_0(Serial.print(passPacket.cmd));
                        PRINT_LEVEL_0(Serial.print("\n"));
                    }
                    g_sysState = SYS_STATE_WAITING;
                }
            }
        }
    } else if (g_sysState == SYS_STATE_BMS_SEND) {
        for (int iB = 0; iB < passPacket.cur; iB++) {
            //   Serial.println( passPacket.data[iB], HEX );
            SoftSerial.write(passPacket.data[iB]);
        }
        resetPackage(passPacket);
        g_sysState = SYS_STATE_BMS_RECV;
    } else if (g_sysState == SYS_STATE_POLL_SEND) {
        g_lastPollRequest = loopStartTime;
        PRINT_LEVEL_1(Serial.println("send poll"));
        resetPackage(passPacket);
        generateRequestPackage(passPacket, 0x3);

        for (int iB = 0; iB < passPacket.cur; iB++) {
            // Serial.println( passPacket.data[iB], HEX );
            SoftSerial.write(passPacket.data[iB]);
        }
        resetPackage(passPacket);
        g_sysState = SYS_STATE_POLL_RECV;
    }
}
