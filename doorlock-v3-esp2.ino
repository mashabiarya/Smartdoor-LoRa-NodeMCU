// Define the interface type
#if 0
#include <SPI.h>
#include <PN532_SPI.h>
#include "PN532.h"
PN532_SPI pn532spi(SPI, 10);
PN532 nfc(pn532spi);

#elif 0
#include <PN532_HSU.h>
#include <PN532.h>
PN532_HSU pn532hsu(Serial1);
PN532 nfc(pn532hsu);

#else
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <NfcAdapter.h>

PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);
#endif

#include <Wire.h> // must be included here so that Arduino library object file references work
#include <RtcDS3231.h>
RtcDS3231<TwoWire> Rtc(Wire);

#define relay1 2
#define relay2 4

#include <LiquidCrystal_I2C.h>
LiquidCrystal_I2C lcd(0x27, 16, 2);
unsigned long previousMillis = 0;
const long interval = 100; // interval in ms

#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>

#define SCK 5
#define MISO 19
#define MOSI 27
#define SS 18
#define RST 23
#define DIO0 14

//433E6 for Asia
//866E6 for Europe
//915E6 for North America
#define BAND 915E6

DynamicJsonDocument doc(1024);

unsigned long lastConnectionTime = 0;        
const unsigned long postingInterval = 5 * 1000;
char datestring[20];
String dtime;

void GenerateKeyA(uint8_t *uid, uint8_t uidLength, uint8_t *static_key, uint8_t *result);
void Reverse(const char *original, char *reverse, int size);
::byte nibble(char c);
void dumpByteArray(const ::byte *byteArray, const ::byte arraySize);
void hexCharacterStringToBytes(::byte *byteArray, const char *hexString);

void setup()
{
  Serial.begin(115200);

  pinMode(relay1, OUTPUT);
  pinMode(relay2, OUTPUT);

  digitalWrite(relay1, HIGH);
  digitalWrite(relay2, HIGH);
  
  lcd.init();
  lcd.backlight();
  doc["macAddr"] = "58:BF:25:8B:EA:2C";

  Serial.println("Initialize!");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Initialize...");
  delay(500);

  SPI.begin(SCK, MISO, MOSI, SS);
  LoRa.setPins(SS, RST, DIO0);
  if (!LoRa.begin(BAND))
  {
    Serial.println("Starting LoRa failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("LoRa failed!");
    while (1)
      ;
  }
  Serial.println("LoRa Initializing OK!");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("LoRa OK!");
  delay(500);
  
  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata)
  {
    Serial.print("Didn't find PN53x board");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("PN53x board");
    lcd.setCursor(0, 1);
    lcd.print("Not found!");
    while (1)
      delay(100); // halt
  }
  
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("PN53x Found!");
    
  // Got ok data, print it out!
  Serial.print("Found chip PN5");
  Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. ");
  Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.');
  Serial.println((versiondata >> 8) & 0xFF, DEC);

  // configure board to read RFID tags
  nfc.SAMConfig();

  Serial.println("Waiting for an ISO14443A Card ...");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tap your card!");
  delay(500);

  Rtc.Begin();

  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  printDateTime(compiled);
  Serial.println();

  if (!Rtc.IsDateTimeValid()) 
  {
      if (Rtc.LastError() != 0)
      {
          // we have a communications error
          // see https://www.arduino.cc/en/Reference/WireEndTransmission for 
          // what the number means
          Serial.print("RTC communications error = ");
          Serial.println(Rtc.LastError());
      }else{
          // Common Causes:
          //    1) first time you ran and the device wasn't running yet
          //    2) the battery on the device is low or even missing

          Serial.println("RTC lost confidence in the DateTime!");

          // following line sets the RTC to the date & time this sketch was compiled
          // it will also reset the valid flag internally unless the Rtc device is
          // having an issue

          Rtc.SetDateTime(compiled);
      }
  }

  if (!Rtc.GetIsRunning())
  {
      Serial.println("RTC was not actively running, starting now");
      Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled) 
  {
      Serial.println("RTC is older than compile time!  (Updating DateTime)");
      Rtc.SetDateTime(compiled);
  }
  else if (now > compiled) 
  {
      Serial.println("RTC is newer than compile time. (this is expected)");
  }
  else if (now == compiled) 
  {
      Serial.println("RTC is the same as compile time! (not expected but all is fine)");
  }

  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeNone); 
  
}

void loop()
{
  RtcDateTime now = Rtc.GetDateTime();
  printDateTime(now);
  
  lcd.setCursor(0, 0);
  lcd.print("Tap your card!");
            
  uint8_t success;
  uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
  uint8_t uidLength;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 10);

  if (success)
  {
    String uidStr = "";
    // Display some basic information about the card
    Serial.println("Found an ISO14443A card");
    Serial.print("  UID Length: ");
    Serial.print(uidLength, DEC);
    Serial.println(" bytes");
    Serial.print("  UID Value: ");
    nfc.PrintHex(uid, uidLength);
    
    for (uint8_t i = 0; i < uidLength; i++)
    {
      uidStr += String(uid[i], HEX);
    }
    
    
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Card Detected!");
    delay(500);

    if (uidLength == 4)
    {
      // We probably have a Mifare Classic card ...
      Serial.println("Seems to be a Mifare Classic card (4 byte UID)");

      // Now we need to try to authenticate it for read/write access
      // Try with the factory default KeyA: 0xFF 0xFF 0xFF 0xFF 0xFF 0xFF
      Serial.println("Trying to authenticate block 4 with default KEYA value");
      // uint8_t keya[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      uint8_t keya[6];
      uint8_t static_key[2] = {20, 21};
      GenerateKeyA(uid, uidLength, static_key, keya);
      nfc.PrintHex(keya, 6);
      Serial.println();

      // Start with block 4 (the first block of sector 1) since sector 0
      // contains the manufacturer data and it's probably better just
      // to leave it alone unless you know what you're doing
      success = nfc.mifareclassic_AuthenticateBlock(uid, uidLength, 4, 0, keya);

      if (success)
      {
        Serial.println("Sector 1 (Blocks 4..7) has been authenticated");
        uint8_t data[16];
        uint8_t data_card[32];

        // If you want to write something to block 4 to test with, uncomment
        // the following line and this text should be read back in a minute
        // data = { 'a', 'd', 'a', 'f', 'r', 'u', 'i', 't', '.', 'c', 'o', 'm', 0, 0, 0, 0};
        // success = nfc.mifareclassic_WriteDataBlock (4, data);

        // Try to read the contents of block 4
        success = nfc.mifareclassic_ReadDataBlock(5, data);

        if (success)
        {
          for (size_t i = 0; i < 16; i++)
            data_card[i] = data[i];

          // Data seems to have been read ... spit it out
          Serial.println("Reading Block 5:");
          nfc.PrintHexChar(data, 16);
          Serial.println("");

          // Wait a bit before reading the card again
          // delay(500);
          success = nfc.mifareclassic_ReadDataBlock(6, data);
          
          if (success)
          {
            for (size_t i = 16; i < 32; i++)
              data_card[i] = data[i - 16];

            Serial.println("Reading Block 6:");
            nfc.PrintHexChar(data, 16);
            Serial.println("");
            nfc.PrintHexChar(data_card, 32);
            Serial.println("");

            uint8_t nip[18];
            String strNip;
            for (size_t i = 0; i < 18; i++)
            {
              nip[i] = data_card[i + 1];
              strNip += String((char)nip[i]);
            }
            nfc.PrintHexChar(nip, 18);
            Serial.println("");
            Serial.println(strNip);

            doc["uidCard"] = uidStr;
            doc["nip"] = strNip;
            doc["type"] = 0;
            doc["datetime"] = datestring;

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("NIP");
            lcd.setCursor(0, 1);
            lcd.print(strNip);

            delay(1000);

            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Open doorlock!");
            lcd.setCursor(0, 1);
            lcd.print("3 seconds");
        
            digitalWrite(relay1, LOW);
            delay(3000);
            digitalWrite(relay1, HIGH);
            
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print("Close doorlock!");

            char buffer[256];
            size_t n = serializeJson(doc, buffer);
            Serial.print("json data: ");
            Serial.println(buffer);
          
            //Send LoRa packet to receiver
            LoRa.beginPacket();
            LoRa.print(buffer);
            LoRa.endPacket();
          }
          else
          {
            Serial.println("Ooops ... unable to read the requested block.  Try another key?");
            digitalWrite(relay1, HIGH);
            return;
          }
        }
        else
        {
          Serial.println("Ooops ... unable to read the requested block.  Try another key?");
          digitalWrite(relay1, HIGH);
          return;
        }
      }
      else
      {
        Serial.println("Ooops ... authentication failed: Try another key?");
        digitalWrite(relay1, HIGH);
        return;
      }
    }
  }

  if (millis() - lastConnectionTime > postingInterval) {   
    doc["type"] = 99;
    doc["datetime"] = datestring;
    
    char buffer[256];
    size_t n = serializeJson(doc, buffer);
    Serial.print("json data: ");
    Serial.println(buffer);
  
    //Send LoRa packet to receiver
    LoRa.beginPacket();
    LoRa.print(buffer);
    LoRa.endPacket();
    lastConnectionTime = millis();
  }
}

void Reverse(const char *original, char *reverse, int size)
{
  if (size > 0 && original != NULL && reverse != NULL)
  {
    for (int i = 0; i < size; ++i)
    {
      reverse[i] = original[size - i - 2];
    }

    reverse[size - 1] = '\0';
  }
}

void hexCharacterStringToBytes(::byte *byteArray, const char *hexString)
{
  bool oddLength = strlen(hexString) & 1;

  ::byte currentByte = 0;
  ::byte byteIndex = 0;

  for (::byte charIndex = 0; charIndex < strlen(hexString); charIndex++)
  {
    bool oddCharIndex = charIndex & 1;

    if (oddLength)
    {
      // If the length is odd
      if (oddCharIndex)
      {
        // odd characters go in high nibble
        currentByte = nibble(hexString[charIndex]) << 4;
      }
      else
      {
        // Even characters go into low nibble
        currentByte |= nibble(hexString[charIndex]);
        byteArray[byteIndex++] = currentByte;
        currentByte = 0;
      }
    }
    else
    {
      // If the length is even
      if (!oddCharIndex)
      {
        // Odd characters go into the high nibble
        currentByte = nibble(hexString[charIndex]) << 4;
      }
      else
      {
        // Odd characters go into low nibble
        currentByte |= nibble(hexString[charIndex]);
        byteArray[byteIndex++] = currentByte;
        currentByte = 0;
      }
    }
  }
}

void dumpByteArray(const ::byte *byteArray, const ::byte arraySize)
{

  for (int i = 0; i < arraySize; i++)
  {
    Serial.print("0x");
    if (byteArray[i] < 0x10)
      Serial.print("0");
    Serial.print(byteArray[i], HEX);
    Serial.print(", ");
  }
  Serial.println();
}

::byte nibble(char c)
{
  if (c >= '0' && c <= '9')
    return c - '0';

  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;

  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;

  return 0; // Not a valid hexadecimal character
}

void GenerateKeyA(uint8_t *uid, uint8_t uidLength, uint8_t *static_key, uint8_t *result)
{
  Serial.println("GenreateKey A start");
  nfc.PrintHex(uid, uidLength);
  memcpy(result, uid, uidLength);
  String CardID = "";
  for (::byte i = 0; i < uidLength; i++)
  {
    String rs = String(uid[i], HEX);
    if (rs.length() == 1)
    {
      rs = "0" + rs;
    }
    CardID += rs;
  }
  Serial.println(CardID);
  char reverse[CardID.length()];
  Reverse(CardID.c_str(), reverse, CardID.length() + 1);
  String rev = String(reverse);
  Serial.println(rev);
  Serial.println("GenreateKey A end");
  lcd.clear();

  ::byte arr[rev.length() / 2];
  hexCharacterStringToBytes(arr, rev.c_str());
  dumpByteArray(arr, rev.length() / 2);

  ::byte key[6];
  key[0] = static_key[0];
  key[1] = arr[0];
  key[2] = arr[1];
  key[3] = arr[2];
  key[4] = arr[3];
  key[5] = static_key[1];
  dumpByteArray(key, 6);

  for (size_t i = 0; i < 6; i++)
    result[i] = key[i];
}

#define countof(a) (sizeof(a) / sizeof(a[0]))

void printDateTime(const RtcDateTime& dt)
{
    snprintf_P(datestring, 
            countof(datestring),
            PSTR("%04u-%02u-%02u %02u:%02u:%02u"),
            dt.Year(),
            dt.Month(),
            dt.Day(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.println(datestring);
}
