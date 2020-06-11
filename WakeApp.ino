/*****************************************************
 *  Engineering Team Project - PJWSTK Gdańsk
 *  Project name: "WakeApp"
 *  
 *  Michał Borkowski
 *  Kamil Szczepanik
 *  Łukasz Waleński
 *  Tomasz Fałdrowicz
 *  
 *  Date June 2020.
 *  
 *   The code below refers to a physical alarm clock 
 *   built on the controller ESP32
 * 
 *****************************************************/

//Used libraries

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans40pt7b.h>
#include <Wire.h>
#include <RTClib.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <WiFi.h>
#include <NTPClient.h>
#include "MQ7.h"
#include <MySQL_Connection.h>
#include <MySQL_Cursor.h>
#include "BluetoothSerial.h"
#include <WiFi.h>
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <WebServer.h>


// Define constants

#define ALARM_SIG 25
#define TFT_DC 2
#define TFT_CS 5
#define TFT_RST 4
#define TFT_WIDTH 320
#define SWITCH 33
#define LED 13
#define COSENSOR 34
#define GASSENSOR A0
#define LED_SENSOR 32
#define BUZER 27
#define DF_VOLUME 30
#define TRIGGER 0

// Define variables

BluetoothSerial SerialBT;
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST); // LCD object 
RTC_DS3231 rtc;                            // module RTC object
HardwareSerial hs(1);                      // Hardware serial for Wiring
DFRobotDFPlayerMini myDFPlayer;            // module DFPlayer object
WiFiUDP ntpUDP;                            // WiFiUDP object needed to NTP server
MQ7 mq7(COSENSOR,5.0);                     // set CO sensor
IPAddress server_addr(194,88,154,134);     // IP of the MySQL server
char user[] = "19697_adm";                 // MySQL user login username
char password[] = "budzik1!";              // MySQL user login password


WiFiClient client;                         // WiFi client needed to connect MySQL server         
MySQL_Connection conn((Client *)&client);  // Set MySQL connection
MySQL_Cursor cur = MySQL_Cursor(&conn);
DateTime alarm_date;

//Define Variables

byte mac[6];                              // Table with mac address ESP32
char query[1500];                         // Table with query to MySQL database
String closest_data;
String time_alarm;
bool alarmOFF = false;
const char* adjust_time = "04:00:00";     // Set hour when time will be adjust with NTP server
char old_date[15];
char old_time[9];
char old_alarm[15];
char alarm_time[9];
char wdays[7][4] = { "Nd", "Pon", "Wt", "Sr", "Czw", "Pt", "Sob" }; // List of days of week
bool is_alarm_playing = false;
bool alarm_on = false;
bool ntp_adjusted = false;
int alarm_ctr;
const long utcOffsetInSeconds = 7200L;   // Set UTC offset (for Poland +2h - summer time)
const char* ssid;
const char* pass;
char ssid_address = 0;
char pass_address = 100;
String ssid_str = "";
String pass_str = "";
bool force_check_alarms = true;
bool is_downloading_alarms = false;
bool to_change_alarm = true;
char dayOfWeek[4];
unsigned long currentTime = 0;

NTPClient timeClient(ntpUDP, "time.google.com", utcOffsetInSeconds); // Set NTP client - instead of "time.google.com" you can use "pool.ntp.org"

///////////////////////////////////////////////////////////////
//Function writing SSID and password to internal memory(EEPROM)
///////////////////////////////////////////////////////////////
void write_string(char add,String data)
{
  int _size = data.length();
  int i;
  for(i=0;i<_size;i++)
  {
    EEPROM.write(add+i,data[i]);
  }
  EEPROM.write(add+_size,'\0');      // Add termination null character for String Data
  EEPROM.commit();
}
 
/////////////////////////////////////////////////////////////////
//Function reading SSID and password from internal memory(EEPROM)
/////////////////////////////////////////////////////////////////
String read_string(char add)
{
  int i;
  char data[100];                    // Max 100 Bytes
  int len=0;
  unsigned char k;
  k=EEPROM.read(add);
  if (k == 255) return String("");
  while(k != '\0' && len<500)        // Read until null character
  {   
    k=EEPROM.read(add+len);
    data[len]=k;
    len++;
  }
  data[len]='\0';
  return String(data);
}

///////////////////////////////////////////////////////////////
//Function starting WiFi connection
///////////////////////////////////////////////////////////////
void startWiFi(){
 
  WiFi.begin(ssid_str.c_str(), pass_str.c_str());
  Serial.println("Laczenie WIFI ...");
  
  int max_retry = 20;
  int current_retry = 0;
  
  while (WiFi.status() != WL_CONNECTED ) {    // Wait for the Wi-Fi to connect
    delay(500);
    Serial.print('.');    
    current_retry++;
    if(current_retry == max_retry){           // If we enter incorrect SSID or password we see message and restart ESP
       tft.println("Wproawadzono bledne dane.\nSprobuj ponownie." );
       delay(4000);
       write_string(ssid_address, "");        // Clear SSID
       write_string(pass_address, "");        // Clear WiFi password
       ESP.restart();                         // Restart ESP
      }
      
  }
  Serial.println('\n');
  Serial.print("Polaczono do ");
  Serial.println(WiFi.SSID());                // Tell us what network we're connected to
  Serial.print("IP address:\t");
  Serial.println(WiFi.localIP());             // Send the IP address of the ESP32 to the computer

  tft.println("WiFi Polaczono.");             // Show message to LCD display
  delay(1000);
}

///////////////////////////////////////////////////////////////
//Function to enter WiFi data (SSID, password) using Bluetooth
///////////////////////////////////////////////////////////////
void BluetoothConfig() {

  char buffer[60];                            // Set buffer
  tft.setFont(&FreeSans9pt7b);                // Set font to LCD display
  tft.println("W aplikacji WakeApp wpisz nazwe\n oraz haslo sieci WiFi.\n (Pamietaj o uruchomieniu bluetooth\n oraz sparowaniu telefonu z budzikiem)\n"); // Show message to LCD display
  while (!SerialBT.available());              // Wait for config
  ssid_str = SerialBT.readString();           // Enter SSID your WiFi
  while (!SerialBT.available());              // Wait for config
  pass_str = SerialBT.readString();           // Enter password to your WiFi

  ssid_str.trim();                            // Delete whitespace character in SSID
  pass_str.trim();                            // Delete whitespace character in password
  Serial.println("Nazwa Sieci");
  Serial.println(ssid_str);

  write_string(ssid_address, ssid_str);       // Write your new SSID
  write_string(pass_address, pass_str);       // Write your new password

}

///////////////////////////////////////////////////////////////
//Function responsible for launching DFPlayer module - sound(MP3)
///////////////////////////////////////////////////////////////
void DFPlayer(){
  hs.begin(9600, SERIAL_8N1, 16, 17);
  int count = 0;
  while (count < 10) {                           // Wait for launching DFPlayer module
    if (!myDFPlayer.begin(hs)) {
      count++;
      Serial.print("Laczenie DFPlayer ...");
      Serial.println(count);
    }
    else {
      break;
    }
  }
  if (count < 10) {                              // If module started...
    Serial.println("DFPlayer Polaczono.");
    tft.println("DFPlayer Polaczono.");          // Show message to LCD display
    myDFPlayer.pause();                          // Start with no sound
    myDFPlayer.volume(DF_VOLUME);                // Set default volume 
    delay(1000);
  }
  else {
    Serial.println("DFPlayer Error.");
    tft.println("DFPlayer Error.");              // Show message to LCD display
    while(1);
  }  
}

///////////////////////////////////////////////////////////////
//Function responsible for launching DS3231 (RTC) module
///////////////////////////////////////////////////////////////

void RTCModule(){
       
   if (!rtc.begin()) {                           // Wait for launching RTC module
    Serial.println("Bład RTC. Wymień baterie");
    tft.println("Bład RTC. Wymień baterie");     // If is a problem with RTC show message to LCD display
    while (1);
  }
  Serial.println("RTC Poloczono.");
  tft.println("RTC Poloczono.");                 // If is ok show message to LCD display
  delay(1000);
}
///////////////////////////////////////////////////////////////
//Function responsible for initial settings LCD display and show date and time
///////////////////////////////////////////////////////////////
void showMessage(char* s_new, char* s_old, int y0, int height) {
  int16_t x1, y1;
  uint16_t w, w2, h;
  int x, y;

  if (strcmp(s_new, s_old) != 0) {
    tft.getTextBounds(s_old, 0, 0, &x1, &y1, &w, &h);          // (string, a starting cursor X&Y position, addresses of two signed and two unsigned 16-bit integers. These last four values will then contain the upper-left corner and the width & height of the area)
    w2 = w * 11 / 10;
    tft.fillRect((TFT_WIDTH - w2) / 2 , y0 - (height / 2) + 1, w2, height, ILI9341_BLACK), //(x, y, length, width , color)
    tft.getTextBounds(s_new, 0, 0, &x1, &y1, &w, &h);
    tft.setCursor((TFT_WIDTH - w) / 2, y0 + (h / 2) - 1);      // Set position of cursor (x,y)
    tft.print(s_new);
    strcpy(s_old, s_new);

  }
}

///////////////////////////////////////////////////////////////
//Function responsible for setting time from NTP server
///////////////////////////////////////////////////////////////

void setTimeByNTP() {

  RTCModule();                                 // Launching RTC function
  timeClient.begin();                          // Set ntp client
  Serial.println("TimeByNTP Initialized");
  tft.println("NTP Poloczono.");               // Show message to LCD display
  delay(1000);
  timeClient.update();                         // Update time by NTP
  long actualTime = timeClient.getEpochTime(); // Get epoch time
  rtc.adjust(DateTime(actualTime));            // Set time in RTC by date from NTP
  
}

///////////////////////////////////////////////////////////////
//Function responsible for setting time from NTP server
///////////////////////////////////////////////////////////////

void getAndSetAlarm() {
    if(is_downloading_alarms == false) {
      is_downloading_alarms = true;
      MySQL_Cursor *cur_mem = new MySQL_Cursor(&conn);  
      
      cur_mem->execute(query);                               // Execute the query    
      column_names *cols = cur_mem->get_columns();           // Fetch the columns
           
      row_values *row = NULL;                       
      closest_data.clear();
      time_alarm.clear();   
      do {                                                   // Read the rows and print them
        row = cur_mem->get_next_row();
        if (row != NULL) { 
          closest_data = row->values[0];
          time_alarm = row->values[1]; 
          closest_data.replace(".", "/");                    // Replace "." to "/"
          int year, month, day;
          day = closest_data.substring(0, 2).toInt();        // Convert day to int
          month = closest_data.substring(4, 6).toInt();      // Convert month to int
          year = closest_data.substring(8, closest_data.length()).toInt(); // Convert year to int
          sprintf(dayOfWeek, wdays[day_of_week(day-1, month, year)]); // Get day of week                              
        }    
      } while (row != NULL);
    
      if (closest_data.length() == 0 && time_alarm.length() == 0){  // If in database are not time alarm then print "Brak alarmu"
        alarmOFF=true;
        time_alarm = "Brak Alarmu";
      } else {
        alarmOFF=false;
      }                  
      
      delete cur_mem;                             // Deleting the cursor also frees up memory used
      is_downloading_alarms = false;
      to_change_alarm = true;
    }
}

///////////////////////////////////////////////////////////////
//Function responsible for stopping alarm 
///////////////////////////////////////////////////////////////

void IRAM_ATTR handleStopAlarm() {
  myDFPlayer.pause();                            // Stop playing alarm (MP3)
  if(alarm_on) {
    force_check_alarms = true;                   // Variable protect to check alarm and play alarm the same time
  }
  alarm_on = false;
  tft.drawRect(30, 180, 260, 40, ILI9341_BLACK); // Set display rectangle
  digitalWrite(LED, LOW);                        // Turn off LED 
  is_alarm_playing = false;
}

///////////////////////////////////////////////////////////////
//Function responsible for write MAC address properly
///////////////////////////////////////////////////////////////
String mac2String(byte ar[])
{
  String s;
  for (byte i = 0; i < 6; ++i)
  {
    char buf[3];
    sprintf(buf, "%2X", mac[i]);
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

///////////////////////////////////////////////////////////////
//Necessary function to calculate the day of the week(including leap year)
///////////////////////////////////////////////////////////////

int fm(int date, int month, int year) {
  int fmonth, leap;
  if ((year % 100 == 0) && (year % 400 != 0))
  leap = 0;
     else if (year % 4 == 0)
    leap = 1;
  else
    leap = 0;
  
    fmonth = 3 + (2 - leap) * ((month + 2) / (2 * month))+ (5 * month + month / 9) / 2;
  
   fmonth = fmonth % 7;
  
    return fmonth;
}

///////////////////////////////////////////////////////////////
//Function responsible for determining the day of the week
///////////////////////////////////////////////////////////////

int day_of_week(int date, int month, int year) {

   int dayOfWeek;
   int YY = year % 100;
   int century = year / 100;

   dayOfWeek = 1.25 * YY + fm(date, month, year) + date - 2 * (century % 4);

   return dayOfWeek % 7;            // Remainder on division by 7
}

///////////////////////////////////////////////////////////////
// setup() function starts first
///////////////////////////////////////////////////////////////
void setup() {
  
  int16_t x1, y1;
  uint16_t w, h;
  
  Serial.begin(115200);
  EEPROM.begin(512);                     // Internal memory. Non-volatile semiconductor memory
  delay(10);

  ssid_str = read_string(ssid_address);  // Get WiFi SSID
  pass_str = read_string(pass_address);  // Get WiFi password 

  ssid_str.trim();                       // Delete whitespace character in SSID
  pass_str.trim();                       // Delete whitespace character in password
 
  strcpy(old_date, "00000000000000");    // Assign old_date
  strcpy(old_time, "00000000");          // Assign old_time
  strcpy(old_alarm, "00000000000000");   // Assign old_alarm
  time_alarm = "Brak Alarmu";           

  // Wyświetl inicjalizację
  tft.begin();                      // Start display
  tft.setRotation(1);               // Set rotation.Rotation 1 is landscape (wide) mode
  tft.fillScreen(ILI9341_BLACK);    // Set black background
  tft.setTextColor(ILI9341_WHITE);  // Set white text color
  tft.setFont(&FreeSans12pt7b);     // Set text font
  String s = "Initializing...";
  tft.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);   //(string, a starting cursor X&Y position, addresses of two signed and two unsigned 16-bit integers. These last four values will then contain the upper-left corner and the width & height of the area)
  tft.setCursor(0, h);              // Set position of cursor (x,y)

  if(ssid_str == ""){               // If SSID is null
    SerialBT.begin("WakeApp_BT");   // Start bluetooth
    BluetoothConfig();              // Run function BluetoothConfig()
  }

  startWiFi();                      // Run function
  DFPlayer();                       // Run function 
  setTimeByNTP();                   // Run function
  tft.fillScreen(ILI9341_BLACK);    // Set black background
    
  // GPIO pins to be set to specific modes
  pinMode(TRIGGER,INPUT);
  pinMode(SWITCH, INPUT_PULLDOWN);
  pinMode(LED, OUTPUT);
  pinMode(COSENSOR, INPUT);
  pinMode(GASSENSOR, INPUT);
  digitalWrite(LED, LOW);
  pinMode(BUZER, OUTPUT);
  pinMode(LED_SENSOR, OUTPUT);

  
  attachInterrupt(digitalPinToInterrupt(SWITCH), handleStopAlarm, RISING); // Set an interrupt on the button "switch" (turn off alarm)
  WiFi.macAddress(mac);                       //Gets the MAC Address of WiFi 
  
  
  if (conn.connect(server_addr, 3306, user, password)) {        // Connect to DataBase-MySQL
    delay(1000);
   }
  else
    Serial.println("Connection failed.");

  String macAdress = mac2String(mac);
  int str_len = macAdress.length() + 1;                         // Set length of macAdress
  char char_array[str_len];                                     // Prepare the character array (the buffer)  
  macAdress.toCharArray(char_array, str_len);                   // Copies the characters in this instance to the Unicode character table. 
  
  // Query to DataBase for closest time alarm
   sprintf(query, "Select DATE_FORMAT(Closed_date,GET_FORMAT(Date,'EUR')), Time from (SELECT `DateStart`, `DateEnd`, `Sequence`, `Time`, ( CASE WHEN `Sequence` is NOT NULL and `Sequence` != '0' then (SELECT  DATE_ADD(`DateStart`, INTERVAL n4.num*1000+n3.num*100+n2.num*10+n1.num DAY ) AS DATE FROM ( SELECT 0 AS num UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) AS n1,( SELECT 0 AS num UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) AS n2,( SELECT 0 AS num UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) AS n3,(SELECT 0 AS num UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4 UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) AS n4 HAVING DATE >= `DateStart` AND DATE <= `DateEnd` AND  LOCATE(WEEKDAY(DATE)+1, `Sequence`) > 0 and (CASE WHEN `Time` > CURRENT_TIME then DATE>=CURRENT_DATE() else DATE>CURRENT_DATE() end) ORDER BY DATE Limit 0, 1) else `DateStart` END ) as Closed_date FROM 19697_wakeapp.alarm as t1 LEFT JOIN 19697_wakeapp.device as t2 on t1.DeviceId = t2.`DeviceId` where t2.`Mac` = '%s') as All_data where Closed_date > CURRENT_DATE() or (Closed_date = CURRENT_DATE() and Time>CURRENT_TIME()) order by Closed_date, Time Limit 0, 1;", char_array);

}

///////////////////////////////////////////////////////////////
// loop() function works continuously
///////////////////////////////////////////////////////////////

void loop() {  
  char new_time[9], new_date[15], new_alarm[15];
  int sw;
  int reset;
  currentTime = millis();
  reset = digitalRead(TRIGGER);
  
  if(reset == 0){                               // If reset button will be pushed
   write_string(ssid_address, "");              // set SSID as an empty string
   write_string(pass_address, "");              // set password as an empty string

   ESP.restart();                               // Reset alarm clock
  }
  DateTime now = rtc.now();                     // Assign current time from RTC to variable 
  int currentSeconds = now.second();            // Assign current second to variable 

  if(currentSeconds == 30) {                    // Check if the current second =30
    getAndSetAlarm();                           // Then run function
  } else {
    if(force_check_alarms) {
      delay(1500);
      getAndSetAlarm();
      force_check_alarms = false;
    }
  }

  //Sensor czadu i gazu MQ-2 i MQ-7
  int sensorValue = analogRead(GASSENSOR);
  
    // part of the code to calculate the data from the chart
//  float sensor_volt=(float)sensorValue/1024*5.0;
//  float RS_gas = (5.0-sensor_volt)/sensor_volt;
//  float RS_air = (5.0-sensor_volt)/sensor_volt;
//  float R0 = RS_air/9.8;
//  float ratio = RS_gas/R0;
//  Serial.print("sensor_volt = ");
//  Serial.println(sensor_volt);
//  Serial.print("RS_ratio = ");
//  Serial.println(RS_gas);
//  Serial.print("Rs/R0 = ");
//  Serial.println(ratio);
  
  if(currentTime >= 300000UL){                      // Wait 3 minutes and start reading sensors
    if(mq7.getPPM() >= 220 || sensorValue > 1400){
    digitalWrite(BUZER, HIGH);
    digitalWrite(LED_SENSOR, HIGH);
    delay(500);
    digitalWrite(BUZER, LOW);
    digitalWrite(LED_SENSOR, LOW);
    delay(500);
    digitalWrite(BUZER, HIGH);
    digitalWrite(LED_SENSOR, HIGH);
    delay(500);
    digitalWrite(BUZER, LOW);
    digitalWrite(LED_SENSOR, LOW);
    delay(500);
  
   }else{
    digitalWrite(BUZER, LOW);
 }
  }
  // Display the current date and time on the LCD
  sprintf(new_date, "%02d/%02d/%04d ", now.day(), now.month(), now.year());
  strcat(new_date, wdays[now.dayOfTheWeek()]);
  sprintf(new_time, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

  //if an alarm is active in the database set alarm without seconds but with a day of the week
  if (time_alarm.compareTo("Brak Alarmu") != 0) {
   sprintf(new_alarm, time_alarm.substring(0, time_alarm.length()-3).c_str());
   strcat(new_alarm, " ");
   strcat(new_alarm, dayOfWeek);
  } else {
    sprintf(new_alarm, time_alarm.c_str());
  }

  tft.setFont(&FreeSans18pt7b);               // Set font
  tft.setTextColor(ILI9341_RED);              // Set red color
  showMessage(new_alarm, old_alarm, 200, 28); // Show alarm to display 
  
  tft.setFont(&FreeSans18pt7b);               // Set font
  tft.setTextColor(ILI9341_WHITE);            // Set white color
  showMessage(new_date, old_date, 40, 28);    // Show date to display
  
  tft.setFont(&FreeSans40pt7b);               // Set font
  tft.setTextColor(ILI9341_WHITE);            // Set white color
  showMessage(new_time, old_time, 120, 64);   // Show time to display
 
  
  // Check if the current time is the alarm time. If true run yhe music and turn on LED
  if(currentSeconds == 0) {
    if (strstr(new_time, time_alarm.c_str()) != NULL) {
      if (!is_alarm_playing) {
        myDFPlayer.loopFolder(2);
        is_alarm_playing = true;   
        alarm_on = true;
        digitalWrite(LED, HIGH);
        alarm_ctr = 0;
      }
    }
  }
  
  // When the alarm is on, flashes a red frame around the alarm time on the LCD
  if (alarm_on) {
    if (alarm_ctr == 0) {
      tft.drawRect(30, 180, 260, 40, ILI9341_RED); 
    }
    else if (alarm_ctr == ALARM_SIG) {
      tft.drawRect(30, 180, 260, 40, ILI9341_BLACK); 
    }
    alarm_ctr++;
    alarm_ctr %= ALARM_SIG * 2;
  }
  
  // Set the time via NTP at the same time every day
  if (strstr(new_time, adjust_time) != NULL) {
    if (!ntp_adjusted) {
      setTimeByNTP();
      ntp_adjusted = true;
    }
  }
  else {
    ntp_adjusted = false;
  }
}
