// Basic data logger for Sensirion SEN54, designed to work in-circuit with VINDSTYRKA
#include <Wire.h>

#define STANDALONE_MODE false       // Log SEN54 values without a VINDSTYRKA in circuit

#define CHECK_DATA_READY_FLAG true  // Check the data ready flag
#define WAIT_DATA_READY_STATE 0     // Wait for the DR flag to be either set (1) or cleared (0)

#define LOG_ERRORS true
#define LOG_WARNINGS true
#define LOG_CSV true

#define I2C_ADDR 0x69
#define SCL_MONITOR_PIN 2  // Monitor the SCL line to time I2C communication after VINDSTYRKA.

uint32_t logCount = 0;

// Calculate the CRC
uint8_t calcCRC(uint16_t word) {
  uint8_t data[2] = {word >> 8, word & 0xFF};
  uint8_t crc = 0xFF;

  for (int i = 0; i < 2; i++) {
    crc ^= data[i];
    for (uint8_t bit = 8; bit > 0; --bit) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x31u;
      }
      else {
        crc = (crc << 1);
      }
    }
  }
  return crc;
}

// Send a command to the SEN54
void txCommand(uint16_t cmd) {
  uint8_t command[2] = {cmd >> 8, cmd & 0xFF};
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(command, 2);
  uint8_t result = Wire.endTransmission(true);

  #if LOG_ERRORS
  if (result) {
    Serial.print("! Transmit error: ");
    Serial.println(result);
  }
  #endif
}

// Request data from the SEN54
bool requestData(uint16_t command, uint8_t messageLength, uint16_t *msgBuffer)
{
  uint8_t byteCount = messageLength*3;

  // Send the command and await the response
  txCommand(command);
  delay(20);

  // Read back the response
  uint8_t bytesAvailable = Wire.requestFrom(I2C_ADDR, byteCount, true);

  if (bytesAvailable != byteCount) {
    #if LOG_ERRORS
    Serial.println("! Incorrect response length");
    #endif
    return false;
  }

  // Read each word + CRC
  uint8_t messagesRead = 0;
  uint8_t bytesRead = 0;
  uint8_t buffer[3];

  while (Wire.available() && bytesRead < byteCount) {
    buffer [bytesRead] = Wire.read();
    bytesRead++;

    if (bytesRead == 3) {
      bytesRead = 0;
      uint16_t word = buffer[0] << 8 | buffer[1];
      if(buffer[2] == calcCRC(word)) {
        // CRC is valid
        msgBuffer[messagesRead] = word;
        messagesRead++;
      }
      else {
        // CRC is not valid
        #if LOG_ERRORS
        Serial.println("! CRC error");
        #endif
        return false;
      }
    }
  }
  if (messagesRead != messageLength) {
    // Incomplete message received
    #if LOG_ERRORS
    Serial.println("! Message incomplete");
    #endif
    return false;
  }
  return true;
}

// Request and log all values from the SEN45
void logAllValues() {
  uint16_t measuredValues[8];
  uint16_t rawValues[4];
  uint16_t mystValues[3];

  if (requestData(0x03C4, 8, measuredValues) && requestData(0x03D2, 4, rawValues) && requestData(0x03F5, 3, mystValues)) {
    logPrint("PRO", 8, measuredValues, true);
    logPrint("RAW", 4, rawValues, false);
    logPrint("MYS", 3, mystValues, false);
    Serial.println();
    logCount++;
  }
  else {
    #if LOG_ERRORS
    Serial.println("! Data error");
    #endif
  }
}


#if LOG_CSV

// Log values as CSV (millis,count;prefix,length,byte_0,..byte_n,)
void logPrint(String pfx, uint8_t length, uint16_t *bytes, bool header) {
  if (header) {
    Serial.print(millis());
    Serial.print(",");
    Serial.print(logCount);
    Serial.print(";");
  }
  Serial.print(pfx);
  Serial.print(",");
  Serial.print(length);
  Serial.print(",");
  for (uint8_t i = 0; i < length; i++) {
    Serial.print(bytes[i]);
    Serial.print(",");
  }
}

#else

// Log values pretty
void logPrint(String pfx, uint8_t length, uint16_t *bytes, bool header) {
  char buffer[16];
  sprintf(buffer, "%08u ", logCount);
  Serial.print(buffer);

  Serial.print(pfx);
  Serial.print(": ");
  for (uint8_t i = 0; i < length; i++) {
    char buffer[16];
    sprintf(buffer, "%.4X (%05u) ", bytes[i], bytes[i]);
    Serial.print(buffer);
  }
  Serial.println();
}

#endif


void setup() {
  Wire.begin();
  pinMode(SCL_MONITOR_PIN, INPUT);
  // Setup SEN45
  Serial.begin(230400);
  Serial.println("# Startup.");

  #if STANDALONE_MODE
  // Send the start measurements command
  Serial.println("# Start measurements.");
  txCommand(0x0021);
  #endif

  delay(2000);
  Serial.println("# Logging.");
}

void loop() {
  #if !STANDALONE_MODE
  // Wait for VINDSTYRKA to take SCL low and start communication
  while (digitalRead(SCL_MONITOR_PIN)) {}

  // Wait for SCL to be high for at least 40 ms (i.e. VINDSTYRKA idling between Sensor reads)
  unsigned long lastSCLLowTransition = millis();

  while (millis() - lastSCLLowTransition < 40) {
    if (!digitalRead(SCL_MONITOR_PIN)) {
      lastSCLLowTransition = millis();
    }
  }
  #endif

  // Check the data ready flag.
  #if CHECK_DATA_READY_FLAG
  uint16_t dataReady[1];
  if (requestData(0x0202, 1, dataReady)) {
    if  (dataReady[0] == WAIT_DATA_READY_STATE){
      // Request and log all Sensor values
      logAllValues();
    }
    #if LOG_WARNINGS
    else {
      Serial.print("! Data ready state: ");
      Serial.println(dataReady[0]);
    }
    #endif
  }
  #else
  logAllValues();
  delay(1000);
  #endif
}
