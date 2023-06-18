// Mock Arduino implementation of a Sensirion SEN54 to learn how VINDSTYRKA processes the values
// Implemented commands:
// - 0x0021 - Start measurements
// - 0x0202 - Read data ready
// - 0x03C4 - Read measured values
// - 0x03D2 - Read raw values (documented in application note)
// - 0x03F5 - Read raw mystery values (completely undocumented but used by VINDSTYRKA; contains the same raw temp and humidity data as CMD 0x03D2, along with a mystery value)

#include <Wire.h>
#define I2C_ADDR 0x69

// Processed measurements, influences pm2.5 display.
struct MeasuredValues {
  uint16_t pm1p0;        // Scale: x10   # Not used
  uint16_t pm2p5;        // Scale: x10   # Directly used for the PM2.5 display
  uint16_t pm4p0;        // Scale: x10   # Not used
  uint16_t pm10p0;       // Scale: x10   # Not Used
  uint16_t humidity;     // Scale: x100  # Not used
  uint16_t temperature;  // Scale: x200  # Not used
  uint16_t vocIndex;     // Scale: x10   # Not used
  uint16_t noxIndex;     // Scale: x10   # Not supported
};

// Raw values, influences tVOC trend indicator
struct RawValues {
  uint16_t humidity;     // Scale: x100  # Not used
  uint16_t temperature;  // Scale: x200  # Not used
  uint16_t vocRaw;       // Scale: x1    # Processed for tVOC display & Zigbee cluster 0xfc7e
  uint16_t noxRaw;       // Scale: x1    # Not supported
};

// Mystery values, influences temperature/humidity displays (changes in temperature also influence the humidity reading)
struct RawMysteryValues {
  uint16_t humidity;     // Scale: x100     # Processed for temp display (identical to "raw" value)
  uint16_t temperature;  // Scale: x200     # Processed for humidity display (identical to "raw" value)
  uint16_t mystery1;     // Mystery word 1  # Influences humidity and temperature readings in a mysterious way
  uint16_t mystery2;     // Mystery word 2  # Not used
};

// Initial mock values
uint16_t pm2p5 = 0;               // pm2.5 * 10
uint16_t rawHumidity = 4000;      // rh% * 100
uint16_t rawTemperature = 5000;   // temp. c * 500
uint16_t rawVOC = 30000;          // Raw VOC Reading
uint16_t rawMysteryWord = 0xFB0C; // Mystery word

MeasuredValues MockMeasurements = {0, pm2p5, 0, 0, 0, 0, 0, 0xFFFF};
RawValues MockRawMeasurements = {rawHumidity, rawTemperature, rawVOC, 0xFFFF};
RawMysteryValues MockRawMysteryMeasurements = {rawHumidity, rawTemperature, rawMysteryWord, 0xFFFF};

// Some flags and buffers
bool dataReadyFlag = true;
bool tickFlag = false;

uint8_t TXBuffer[255];
uint8_t TXLength = 0;
bool TXReady = false;

// Calculate CRC checksum (taken from Sensirion datasheet)
void AddCrc(uint8_t *buffer, uint8_t offset) {
  uint8_t crc = 0xFF;
  for (int i = 0; i < 2; i++) {
    crc ^= buffer[offset + i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31u;
      }
      else {
        crc = (crc << 1);
      }
    }
  }
  buffer[offset + 2] = crc;
}

// Add word + checksum to the TX buffer
uint8_t addTXWord(uint16_t payload, uint8_t *buffer, uint8_t offset) {
  buffer[offset + 0] = payload >> 8;
  buffer[offset + 1] = payload & 0xFF;
  AddCrc(buffer, offset);
  return 3;
}

// Respond to the "data ready" command
void respondDataReady(bool ready) {
  TXLength += addTXWord(0x0000 | ready, TXBuffer, TXLength);
  TXReady = true;
}

// Respond to the "read measured values" command
void respondMeasuredValues(MeasuredValues mock) {
  TXLength += addTXWord(mock.pm1p0, TXBuffer, TXLength);
  TXLength += addTXWord(mock.pm2p5, TXBuffer, TXLength);
  TXLength += addTXWord(mock.pm4p0, TXBuffer, TXLength);
  TXLength += addTXWord(mock.pm10p0, TXBuffer, TXLength);
  TXLength += addTXWord(mock.humidity, TXBuffer, TXLength);
  TXLength += addTXWord(mock.temperature, TXBuffer, TXLength);
  TXLength += addTXWord(mock.vocIndex, TXBuffer, TXLength);
  TXLength += addTXWord(mock.noxIndex, TXBuffer, TXLength);
  TXReady = true;
}

// Respond to the "read raw values" command
void respondRawValues(RawValues mock) {
  TXLength += addTXWord(mock.humidity, TXBuffer, TXLength);
  TXLength += addTXWord(mock.temperature, TXBuffer, TXLength);
  TXLength += addTXWord(mock.vocRaw, TXBuffer, TXLength);
  TXLength += addTXWord(mock.noxRaw, TXBuffer, TXLength);
  TXReady = true;
}

// Respond to the "read raw mystery values" command
void respondRawMysteryValues(RawMysteryValues mock) {
  TXLength += addTXWord(mock.humidity, TXBuffer, TXLength);
  TXLength += addTXWord(mock.temperature, TXBuffer, TXLength);
  TXLength += addTXWord(mock.mystery1, TXBuffer, TXLength);
  TXLength += addTXWord(mock.mystery2, TXBuffer, TXLength);
  TXReady = true;
}

// Print the response payload
void printTXBuffer() {
  Serial.print("RX: | ");
  for (uint8_t i = 0; i < TXLength; i++) {
    char buffer[4];
    sprintf(buffer, "%.2X ", TXBuffer[i]);
    Serial.print(buffer);
    if((i+1) % 3 == 0) {
      Serial.print("| ");
    }
  }
  char buffer[6];
  sprintf(buffer, "(%u)", TXLength);
  Serial.print(buffer);
  Serial.print("\r\n");
}

// Handle incoming commands
void RxHandler(uint8_t numbytes) {
  uint8_t data[2];
  uint8_t i = 0;
  while(Wire.available()){
    if (i < 2) {
      data[i] = Wire.read();
      i++;
    }
    else {
      Serial.print(Wire.read(), HEX);  // Print any extra bytes received
    }
  }

  uint16_t command = data[0] << 8 | data[1];

  // Process the command
  if (command == 0x0021){
    Serial.println("CMD 0x0021: Start measurements");
    MockMeasurements.pm2p5 = pm2p5;  // Reset counter when device inits
  }
  else if (command == 0x0202){
    respondDataReady(dataReadyFlag);
    Serial.println("CMD 0x0202: Data ready");
  }
  else if (command == 0x03C4) {
    respondMeasuredValues(MockMeasurements);
    Serial.println("CMD 0x03C4: Read measured values");
  }
  else if (command == 0x03D2) {
    respondRawValues(MockRawMeasurements);
    Serial.println("CMD 0x03D2: Read raw values");
  }
  else if (command == 0x03F5) {
    respondRawMysteryValues(MockRawMysteryMeasurements);
    Serial.println("CMD 0x03F5: Read raw mystery values");
    tickFlag = true;  // Last command in the sequence, trigger the tick
  }
  else {
    Serial.print("Unknown command: 0x");
    Serial.println(command, HEX);
  }
  printTXBuffer();
}

// Write response buffer to I2C
void TxHandler() {
  if (TXLength && TXReady) {
    for (uint8_t i = 0; i < TXLength; i++) {
      Wire.write(TXBuffer[i]);
    }
    TXLength = 0;
    TXReady = false;
  }
  else {
    Serial.println("Data requested prematurely!");
  }
}

// Setup serial and I2C
void setup() {
  Serial.begin(230400);
  Wire.begin(I2C_ADDR);
  Wire.onReceive(RxHandler);
  Wire.onRequest(TxHandler);
}

// Control the mock values
void loop() {
  // Every time the device sends the "mystery" command this gets triggered
  if (tickFlag) {
    tickFlag = false;

    // The PM2.5 display is setup as a 0-100 counter
    MockMeasurements.pm2p5 += 10;
    if (MockMeasurements.pm2p5 > 1000) {
      MockMeasurements.pm2p5 = pm2p5;
    }
  }
}
