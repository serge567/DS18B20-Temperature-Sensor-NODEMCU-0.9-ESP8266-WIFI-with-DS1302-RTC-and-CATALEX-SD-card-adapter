////////////////////////////////////////////////////////////////////////////////////
// Temperature sensor webserver, storing temperature value to SD card
// 2019-09-23
// Ver 0.2 
////////////////////////////////////////////////////////////////////////////////////

// Clock
// DS1302 RTC Module CLK ----- NODEMCU D4 (GPIO2)
// DS1302 RTC Module DAT ----- NODEMCU D3 (GPIO0)
// DS1302 RTC Module RST ----- NODEMCU D0 (GPIO16)
#include <DS1302RTC.h>
#include <Time.h>
DS1302RTC RTC(16, 0, 2);// Set pins:  RST,DAT,CLK
String sYear     = "0000";
String sMonth    = "00";
String sDay      = "00";
String ssWeekDay =   "";
String sHours    = "00";
String sMinutes  = "00";
String sSeconds  = "00";
const String sWeekDay[8] = {"", 
							"Sunday", 
							"Monday", 
							"Tuesday", 
							"Wednesday", 
							"Thursday", 
							"Friday", 
							"Saturday"
							};
 
// Web server
#include <ESP8266WiFi.h>
WiFiServer server(80);
const char sSSID[] = "TemperatureServer";
const char sPass[] = "12345temp";

// SD card
// SD SCK ------ NODEMCU D5 (GPIO14)
// SD MOSI ----- NODEMCU D6 (GPIO12)
// SD MISO ----- NODEMCU D7 (GPIO13)
// SD CS ------- NODEMCU D8 (GPIO15)
// SD card must be formated as MS-DOS FAT32(not LBA) WIN95 - tested!
// New VFAT or others are not supported (operation to remove file does not work with VFAT)
// Writing by byte too fast with a lot of data can be reason that buffer 
// get overfill!! -> esp8266 automatic restart. Use delay().
#include <SPI.h>
#include <SD.h>
File myFile;
const int chipSelect = 15; //GPIO15 is CS (SPI) for SD card
byte fileByte;
char* strWebHomePage = "INDEX.TXT";// 8 chars max is supported.
static const String indextxt = "<!DOCTYPE html>\r\n<html><head>\r\n<title>Temperature logger</title>\r\n<h2>Temperature measurements</h2>\r\n<style>table, th, td {border: 1px solid black;border-collapse: collapse;}</style>\r\n<table>\r\n<th>&nbsp;&nbsp;Date and time&nbsp;&nbsp;</th><th>&nbsp;&nbsp;Temperature value&nbsp;&nbsp;</th>\r\n<!-- data -->\r\n<!-- <tr><td>&nbsp;&nbsp;AA&nbsp;&nbsp;</td><td>&nbsp;&nbsp;BB&nbsp;&nbsp;</td></tr> -->\r\n</table>\r\n<br>\r\n<b>\r\n<!-- YYYY-MM-DD -->                                   \r\n&nbsp;&nbsp;\r\n<!-- WD -->                                           \r\n&nbsp;&nbsp;\r\n<!-- hh:mm:ss -->                                     \r\n</b>\r\n<br>\r\n<b>\r\nTemperature, now:&nbsp;\r\n<!-- Temp -->                  \r\n</b>\r\n</head></html>\r\n";
// Temperature sensor
#include <OneWire.h>
#include <DallasTemperature.h>
#define ONE_WIRE_BUS 5 //DS18B20 S --- NODEMCU D1 (GPIO5)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float fActualTemper = 0;

// Green LED
const int LED_PIN = 4; //NODEMCU D2 (GPIO4)
const long LEDinterval = 1000; // LED blinking
long LEDpreviousMillis = 0;

// Timer interval
long currentMillis = 0;
const long interval = 10000;   // interval at which to read analog value (milliseconds), 1800000 - 30 min
long previousMillis = 0; 

// NTP
#include <WiFiUdp.h>
const char sSSIDNTP[] = "SSIDInternetRouter";  //  your network SSID (name)
const char sPassNTP[]  = "password";  // your network password
static const char ntpServerName[] = "ntp.nict.jp";
const int timeZone = 9;     // Tokyo
WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets
bool bNTPSyncFinished;


void setup() {
  
    DebugTool();
	iNTP(); //Connect to Internet WIFI to get date and time 
    iWiFi();
    SDinit();
}

void loop() {
   WiFiClient client = server.available();
   if (client) {
	 Serial.println("Client..P1"); 
	 Serial.print("Free heap:");
	 Serial.println(ESP.getFreeHeap(),DEC); 
	 TemperSenRead();
     ClockRead();
     client.print(" ");    
     InsertStringToTextFile(strWebHomePage, sYear+"-"+sMonth+"-"+sDay , "<!-- YYYY-MM-DD -->", 20);
     client.print(" ");  
     InsertStringToTextFile(strWebHomePage, " "+ssWeekDay+" ", "<!-- WD -->", 13);
	 client.print(" ");  
	 InsertStringToTextFile(strWebHomePage, sHours+":"+sMinutes+":"+sSeconds, "<!-- hh:mm:ss -->", 19);
	 client.print(" ");  
	 InsertStringToTextFile(strWebHomePage, String(fActualTemper) + " "+ char(176) +"C", "<!-- Temp -->", 15);     
	 myFile = SD.open(strWebHomePage); 
	 if (myFile) {
		Serial.println("Client..P2"); 
		while (myFile.available()) {
			yield(); //to prevent Soft WDT reset in esp8266 while long loop
			fileByte = myFile.read();
			client.print(String(char(fileByte)));
		//	Serial.write(fileByte);
		}
		myFile.close();
	 }
   }
   currentMillis = millis();  //Tick for timer
   LEDRun();
   TempMeas(); 
}


////////////////////////////////////////////////////////////////////////

void DebugTool(){
  //LED configuring
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);
}

void iWiFi(){
  
  // WIFI AP configuring 
    WiFi.softAP(sSSID, sPass);
    IPAddress myIP = WiFi.softAPIP();

    // Web server configuring 
    server.begin();
}

void SDinit(){
  
    if (!SD.begin(chipSelect)) {
        Serial.println("SD initialization failed!");
    return;
   }
  Serial.println("SD initialization done.");
	delay(3000);
	if(SD.exists(strWebHomePage))
      {
		   Serial.println("Removing file ...");
           SD.remove(strWebHomePage);
           Serial.println("index.txt file removed");
      }
    Serial.println("Creating a new one.");
	myFile = SD.open(strWebHomePage, FILE_WRITE);
	if (myFile) {
		Serial.println("Writing data to file.");
		myFile.print(indextxt); 
	    myFile.close();
		Serial.println("SD file index.txt re-created.");
	}	 	 	
}

void InsertStringToTextFile (char* strFilename, String strInsert, String strSearch, int inPosOffset) {

  String strTemp 		= "";
  int intPointer 		= -1;
  byte TempFileByte;
  myFile = SD.open(strFilename, FILE_WRITE);
  if (myFile) {
	  for (int i = 1; i <= (int)myFile.size(); i++) { // i is position of first symbol number to search string - strSearch
		strTemp 		= "";
		for (int k = 0; k < strSearch.length(); k++) { // k-loop to combine strTemp string from file, strTemp will be compared with strSearch
		  yield(); //to prevent Soft WDT reset in esp8266 while long loop
		  if ((i - 1 + k) <= (int)myFile.size()) {		 
			myFile.seek(i - 1 + k);		
			TempFileByte = myFile.read();		
			strTemp += char(TempFileByte); // combining string to compare with strSearch
			if (strTemp != strSearch.substring(0, k + 1)) { // break k-loop to combine strTemp if first symbols of strTemp are not much to first symbols of strSearch
				break;
			}
		  }
		}
		if (strSearch == strTemp) {
			intPointer = i - 1;
			myFile.seek(myFile.size()); // setting file pointer to end of file
			break;
		}
	  }
	  if (intPointer != -1) {
		if (strInsert.length() > 0) {
			if (inPosOffset == 0) {
				myFile.print(strInsert); // inreasing file size: + strInsert symbols amount
				delay(20); //to prevent buffer overfill while writing to SD
				for (int i = (int)myFile.size(); i > intPointer; i--) { // i-loop to shift symbols from intPointer to end of file, making space to insert strInsert string
				  yield(); //to prevent Soft WDT reset in esp8266 while long loop
				  myFile.seek(i - strInsert.length() - 1);
				  TempFileByte = myFile.read();
				  myFile.seek(i);
				  myFile.write(char(TempFileByte));
				  delay(20); //to prevent buffer overfill while writing to SD
				}
			}	
			myFile.seek(intPointer + inPosOffset); // printing strInsert string to file with (intPointer + inPosOffset) symbol position
			myFile.print(strInsert);
			delay(20); //to prevent buffer overfill while writing to SD
		}	
	  }
	  myFile.close();
  } 
}

void TempMeas() {

	if (currentMillis - previousMillis >= interval) {
       previousMillis = currentMillis;
       TemperSenRead();
       ClockRead(); 
       Serial.println("Saving measurement to file :" + sYear+"-"+sMonth+"-"+sDay+" "+ssWeekDay+" "+sHours+":"+sMinutes+":"+sSeconds + "   Temperature: " +  String(fActualTemper));
       Serial.print("Free heap:");
	   Serial.println(ESP.getFreeHeap(),DEC);
       InsertStringToTextFile(strWebHomePage, 
			"<tr><td>&nbsp;&nbsp;"
			+sYear+"-"+sMonth+"-"+sDay+" "+ssWeekDay+" "+sHours+":"+sMinutes+":"+sSeconds
			+"&nbsp;&nbsp;</td><td>&nbsp;&nbsp;"
			+String(fActualTemper) + " "+ char(176) +"C"
			+"&nbsp;&nbsp;</td></tr>", 
			"<!-- data -->", 0);
    }
}

void TemperSenRead(){
  
  // Start up the library for Temperature sensor
  sensors.begin();
  sensors.requestTemperatures();  
  fActualTemper = sensors.getTempCByIndex(0);
  
}

void LEDRun(){
  
  if (currentMillis - LEDpreviousMillis >= LEDinterval) {
     LEDpreviousMillis = currentMillis;
     if (digitalRead(LED_PIN) == LOW) {
        digitalWrite(LED_PIN, HIGH);
     } 
     else {
        digitalWrite(LED_PIN, LOW);
     }  
  }
  
}

void ClockRead(){
    
      sYear     = String(year());
      sMonth    = LeadZero(String(month()));
      sDay      = LeadZero(String(day()));
      ssWeekDay = sWeekDay[weekday()];
      sHours    = LeadZero(String(hour()));
      sMinutes  = LeadZero(String(minute()));
      sSeconds  = LeadZero(String(second()));
    
}

String LeadZero(String sStr) {
	
	if (sStr.length() < 2) {
       return "0" + sStr;
	}
	return sStr;
}


//--------------------- NTP -------------------------------------------

//// Date and time provider settings, date and time synchronization ////
void iNTP(){
	WiFi.begin(sSSIDNTP, sPassNTP);
	delay (15000);
	if (WiFi.status() == WL_CONNECTED) {
		Udp.begin(localPort);
		setSyncProvider(getNtpTime);
		setSyncInterval(300); 
 
		if (timeStatus() != timeNotSet) {
			RTC.set(now());
			Serial.println("NTP Date Time -> Actual clock");
			Serial.println("NTP Date Time -> RTC done");
		}
		else {  
			setSyncProvider(RTC.get);
			Serial.println("RTC Date Time (not NTP) -> Actual clock");
		}
		
		Udp.stop();
	}
	else {
		Serial.println("It couldn't connect Wifi AP to get NTP synchronization");
		setSyncProvider(RTC.get);
		Serial.println("RTC Date Time (not NTP) -> Actual clock");
	}
	bNTPSyncFinished = true;
	WiFi.disconnect();
}
//END Date and time provider settings, date and time synchronization ///

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
	if (!bNTPSyncFinished) {

	  IPAddress ntpServerIP; // NTP server's ip address

	  while (Udp.parsePacket() > 0) ; // discard any previously received packets
	  Serial.println("Transmit NTP Request");
	  // get a random server from the pool
	  WiFi.hostByName(ntpServerName, ntpServerIP);
	  Serial.print(ntpServerName);
	  Serial.print(": ");
	  Serial.println(ntpServerIP);
	  sendNTPpacket(ntpServerIP);
	  uint32_t beginWait = millis();
	  while (millis() - beginWait < 1500) {
		int size = Udp.parsePacket();
		if (size >= NTP_PACKET_SIZE) {
		  Serial.println("Receive NTP Response");
		  Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
		  unsigned long secsSince1900;
		  // convert four bytes starting at location 40 to a long integer
		  secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
		  secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
		  secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
		  secsSince1900 |= (unsigned long)packetBuffer[43];
		  return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
		}
	  }
	  Serial.println("No NTP Response :-(");
	  return 0; // return 0 if unable to get the time
	}
	return 0; // NTP synchronization finished, just exit.
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}

//--------------------- NTP END ----------------------------------------
