#include <FS.h>          // this needs to be first, or it all crashes and burns..
#include <SPI.h>
#include <Wire.h>

//TFT display
#include "Adafruit_GFX.h"
#include <Adafruit_ST7735.h>

//wifi module
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>
#include "WeatherData.h"
#include <ArduinoOTA.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

// Color definitions
#define BLACK    0x0000
#define BLUE     0x001F
#define RED      0xF800
#define GREEN    0x07E0
#define CYAN     0x07FF
#define MAGENTA  0xF81F
#define YELLOW   0xFFE0
#define WHITE    0xFFFF
#define GREY     0xC618

// Define pins that will be used as parameters in the TFT constructor
#define cs   D8
#define dc   D3
#define rst  D4
//#define dc   D4
//#define rst  -1

// select which pin will trigger the configuration portal when set to LOW
#define TRIGGER_PIN 0


//flag for saving data
bool shouldSaveConfig = false;

//define your default values here, if there are different values in config.json, they are overwritten.
char api_user[20] = ""; //add your own geonames username
char api_key[33] = ""; //add your own accuweather API KEY
char city[40] = "Frankfurt "; // add your prefered city
char units [40]= "metric"; // (options: metric/imperial )

int timeout = 120; // seconds to run for
WiFiManager wm; // global wm instance
// setup custom parameters
// 
// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
WiFiManagerParameter custom_api_user("api_user", "api user", api_user, 20);
WiFiManagerParameter custom_api_key("api_key", "api key", api_key, 32);
WiFiManagerParameter custom_city("city", "city", city, 40);
WiFiManagerParameter custom_units("units", "units", units, 40);

bool portalRunning      = false;

WeatherData weatherData = WeatherData();

// Instantiate the TFT constructor with the pin values defined above
Adafruit_ST7735 tft = Adafruit_ST7735(cs, dc, rst);

long weatherDataTimer = 0;

float coord_lon = NULL; 
float coord_lat = NULL;

char hostString[16] = {0};

void checkButton(){
  // check for button press
  if ( digitalRead(TRIGGER_PIN) == LOW && (!portalRunning)) {
    // poor mans debounce/press-hold, code not ideal for production
    delay(50);
    if( digitalRead(TRIGGER_PIN) == LOW ){
      Serial.println("Button Pressed");
      // still holding button for 3000 ms, reset settings, code not ideaa for production
      delay(3000); // reset delay hold
      if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Button Held");
        Serial.println("Erasing Config, restarting");
        wm.resetSettings();
        ESP.restart();
      }
      
      // start portal w delay
      Serial.println("Starting config portal");
      wm.setConfigPortalTimeout(timeout);
      
      if (!wm.startConfigPortal(String(hostString).c_str())) {
        Serial.println("failed to connect or hit timeout");
        delay(3000);
        // ESP.restart();
      } else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
      }
    } 
  } else if (!portalRunning) {
      Serial.println("Button Pressed, Starting Web Portal");
      wm.startWebPortal();
  }
  portalRunning = true;
  
  if (portalRunning){
    wm.process();
  }
}

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setupSpiffs(){
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(api_user, json["api_user"]);
          strcpy(api_key, json["api_key"]);
          strcpy(city, json["city"]);
          strcpy(units, json["units"]);

          // if(json["ip"]) {
          //   Serial.println("setting custom ip from config");
          //   strcpy(static_ip, json["ip"]);
          //   strcpy(static_gw, json["gateway"]);
          //   strcpy(static_sn, json["subnet"]);
          //   Serial.println(static_ip);
          // } else {
          //   Serial.println("no custom ip in config");
          // }

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
}

void setup () {

  Serial.begin(115200);
  Serial.println("setup()");
  pinMode(TRIGGER_PIN, INPUT_PULLUP);

  delay(500);

  sprintf(hostString, "ESPWEATHER_%06X", ESP.getChipId());
  Serial.println(hostString);

  setupSpiffs();
  
  //set config save notify callback
  wm.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  wm.addParameter(&custom_api_user);
  wm.addParameter(&custom_api_key);
  wm.addParameter(&custom_city);
  wm.addParameter(&custom_units);

  Wire.begin();

  tft.initR(INITR_BLACKTAB);   // initialize a ST7735S chip, black tab
  tft.fillScreen(BLACK);
  tft.setCursor(30, 80);
  tft.setTextColor(WHITE);
  tft.setTextSize(1);
  tft.print("Connecting...");

  WiFi.hostname(hostString);
  WiFi.mode(WIFI_STA);
  
  wm.setAPCallback([&](WiFiManager* wifiManager) {
      Serial.println("Configportal running");
      tft.fillScreen(BLACK);
      tft.setCursor(30, 80);
      tft.setTextColor(WHITE);
      tft.setTextSize(1);
      tft.print(wifiManager->getConfigPortalSSID() + "/" + WiFi.softAPIP().toString());
      Serial.printf("Entered config mode:ip=%s, ssid='%s'\n", 
                        WiFi.softAPIP().toString().c_str(), 
                        wifiManager->getConfigPortalSSID().c_str());
  });

  if(!wm.autoConnect(hostString)){
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      //reset and try again, or maybe put it to deep sleep
      ESP.restart();
      delay(5000);
  } else {

  }

  Serial.println("connected...yeey :)");
  tft.fillScreen(BLACK);
  tft.setCursor(30, 80);
  tft.setTextColor(WHITE);
  tft.setTextSize(1);
  //tft.print("Connected");
  
  ArduinoOTA.setHostname(hostString);
  ArduinoOTA.begin();



  initWeather();
}

void loop() {
  ArduinoOTA.handle();
  checkButton();
  
  if (millis() - weatherDataTimer > 60000) {
    initWeather();
    weatherDataTimer = millis();
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    //read updated parameters
    strcpy(api_user, custom_api_user.getValue());
    strcpy(api_key, custom_api_key.getValue());
    strcpy(city, custom_city.getValue());
    strcpy(units, custom_units.getValue());

    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["api_user"]   = api_user;
    json["api_key"]   = api_key;
    json["city"] = city;
    json["units"]   = units;

    // json["ip"]          = WiFi.localIP().toString();
    // json["gateway"]     = WiFi.gatewayIP().toString();
    // json["subnet"]      = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.prettyPrintTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    shouldSaveConfig = false;
  }
}

void initWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    tft.fillScreen(BLACK);
    tft.setCursor(30, 80);
    tft.setTextColor(WHITE);
    tft.setTextSize(1);
    tft.print("Disconnected");
  } else {
	tft.fillRect(0, 70, tft.width(), 30, BLACK);
    getWeatherData();
	if (coord_lat != NULL && coord_lon != NULL)
       getCurrentTimeRequest(coord_lat, coord_lon);

  }
}


void getWeatherData() {
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&units=" + String(units) + "&APPID=" + String(api_key);

  HTTPClient http;  //Declare an object of class HTTPClient
  http.begin(url);  //Specify request destination
  int httpCode = http.GET();//Send the request
  Serial.println("\ngetWeatherData: "+ String(httpCode) + " "+ url);
  if (httpCode == 200) { //Check t he returning code
    String payload = http.getString();   //Get the request response payload
	Serial.println(payload);
    //parse data
    parseWeatherData(payload);
  }
  http.end();//Close connection
}

/**
 * Comverted json data using 
 * https://arduinojson.org/v5/assistant/
 */
void parseWeatherData(String payload) {
  const size_t capacity = JSON_ARRAY_SIZE(1) + JSON_OBJECT_SIZE(1) + 2 * JSON_OBJECT_SIZE(2) + 2 * JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(11) + 500;
  DynamicJsonBuffer jsonBuffer(capacity);

  JsonObject& root = jsonBuffer.parseObject(payload);

  coord_lon = root["coord"]["lon"]; // 25.61
  coord_lat = root["coord"]["lat"]; // 45.65

  JsonObject& weather_0 = root["weather"][0];
  int weather_0_id = weather_0["id"]; // 803
  const char* weather_0_main = weather_0["main"]; // "Clouds"
  const char* weather_0_description = weather_0["description"]; // "broken clouds"
  const char* weather_0_icon = weather_0["icon"]; // "04d"

  const char* base = root["base"]; // "stations"

  JsonObject& main = root["main"];
  float main_temp = main["temp"]; // -6.04
  float main_pressure = main["pressure"]; // 1036.21
  int main_humidity = main["humidity"]; // 65
  float main_temp_min = main["temp_min"]; // -6.04
  float main_temp_max = main["temp_max"]; // -6.04
  float main_sea_level = main["sea_level"]; // 1036.21
  float main_grnd_level = main["grnd_level"]; // 922.42

  float wind_speed = root["wind"]["speed"]; // 1.21
  float wind_deg = root["wind"]["deg"]; // 344.501

  int clouds_all = root["clouds"]["all"]; // 68

  long dt = root["dt"]; // 1551023165

  JsonObject& sys = root["sys"];
  float sys_message = sys["message"]; // 0.0077
  const char* sys_country = sys["country"]; // COUNTRY
  long sys_sunrise = sys["sunrise"]; // 1550984672
  long sys_sunset = sys["sunset"]; // 1551023855

  long id = root["id"]; // 683844
  const char* cityName = root["name"]; // CITY
  int cod = root["cod"]; // 200

  //tft.fillScreen(BLACK);
  tft.fillRect(0, 0 , tft.width(), 58, BLACK);

  displayTemperature(main_temp);
  displayIcon(weather_0_icon);
  displayDescription(weather_0_description);
  
  tft.fillRect(0, 105 , tft.width(), tft.height() - 105, BLACK);
  displaySunriseTime(sys_sunrise);  
  displaySunsetTime(sys_sunset);
  displayLocation(cityName);

  delay(5000);
}

void getCurrentTimeRequest(float latitude, float longitude) {
  String url = "http://api.geonames.org/timezoneJSON?lat=" + String(latitude) + "&lng=" + String(longitude) + "&username=" + String(api_user);

  HTTPClient http;  //Declare an object of class HTTPClient
  http.begin(url);  //Specify request destination
  int httpCode = http.GET();//Send the request
  Serial.println("\ngetCurrentTimeRequest: "+ String(httpCode) + " "+ url);
  if (httpCode == 200) { //Check the returning code
    String payload = http.getString();   //Get the request response payload
	Serial.println(payload);
    //parse data
    parseTimeData(payload);
  }
  Serial.println("getCurrentTimeRequest: "+String(httpCode) );
  http.end();   //Close connection
}

void parseTimeData(String payload) {
  const size_t capacity = JSON_OBJECT_SIZE(11) + 220;
  DynamicJsonBuffer jsonBuffer(capacity);

  JsonObject& root = jsonBuffer.parseObject(payload);

  const char* sunrise = root["sunrise"]; // "2019-03-08 06:44"
  float lng = root["lng"]; // 25.61
  const char* countryCode = root["countryCode"]; // "RO"
  int gmtOffset = root["gmtOffset"]; // 2
  int rawOffset = root["rawOffset"]; // 2
  const char* sunset = root["sunset"]; // "2019-03-08 18:13"
  const char* timezoneId = root["timezoneId"]; // "Europe/Bucharest"
  int dstOffset = root["dstOffset"]; // 3
  const char* countryName = root["countryName"]; // "Romania"
  const char* currentTime = root["time"]; // "2019-03-08 01:11"
  float lat = root["lat"]; // 45.65

  Serial.println(currentTime);

  displayCurrentTime(currentTime);
}

// TIME
void displayCurrentTime(String currentTime) {
  tft.fillRect(0, 70, tft.width(), 30, BLACK);
  tft.setTextSize(3);
  tft.setCursor(1, 70);

  String timeOnly = currentTime.substring(10);
  tft.print(timeOnly);
}

// TEMPERATURE
void displayTemperature(float main_temp) {
  tft.setTextColor(WHITE);
  tft.setTextSize(2.5);

  tft.setCursor(5, 20);
  String temperatureValue = String((int)main_temp) + (char)247 + "C";
  tft.print(temperatureValue);
}

// ICON
void displayIcon(String weatherIcon) {
  tft.setTextSize(1);
  tft.drawBitmap(75, 5, weatherData.GetIcon(weatherIcon) , 50, 50, WHITE);
}

// DESCRIPTION
void displayDescription(String weatherDescription) {
  tft.setTextSize(1);
  tft.setCursor(5, 50);
  if(weatherDescription.length() > 18){
   weatherDescription = weatherDescription.substring(0, 15) + "...";
  }
  String description = String(weatherDescription);
  tft.print(description);
}

// SUNRISE
void displaySunriseTime(long sys_sunrise) {

  sys_sunrise = sys_sunrise + 7200 + 3600;//ADD 2 Hours (For GMT+2)

  int hr = (sys_sunrise  % 86400L) / 3600;
  int minute = (sys_sunrise % 3600) / 60;
  int sec = (sys_sunrise % 60);

  String sunriseHour;
  String sunriseMinute;

  if (hr < 10) {
    sunriseHour = "0" + String(hr);
  } else {
    sunriseHour = String(hr);
  }

  if (minute < 10) {
    sunriseMinute = "0" + String(minute);
  } else {
    sunriseMinute = String(minute);
  }

  tft.setTextSize(1);
  tft.setCursor(5, 105);

  String sunrise = "Sunrise: " + sunriseHour + " : " + sunriseMinute;
  tft.print(sunrise);
}


// SUNSET
void displaySunsetTime(long sys_sunset) {

  sys_sunset = sys_sunset + 7200 + 3600;//ADD 2 Hours (For GMT+2)

  int sunset_hr = (sys_sunset  % 86400L) / 3600;
  int sunset_minute = (sys_sunset % 3600) / 60;
  int sunset_sec = (sys_sunset % 60);

  String sunsetHour;
  String sunsetMinute;

  if (sunset_hr < 10) {
    sunsetHour = "0" + String(sunset_hr);
  } else {
    sunsetHour = String(sunset_hr);
  }

  if (sunset_minute < 10) {
    sunsetMinute = "0" + String(sunset_minute);
  } else {
    sunsetMinute = String(sunset_minute);
  }

  tft.setTextSize(1);
  tft.setCursor(5, 115);

  String sunset = "Sunset: " + sunsetHour + " : " + sunsetMinute;
  tft.print(sunset);
}

// LOCATION
void displayLocation(String cityName) {
  tft.setTextSize(1);
  tft.setCursor(5, 140);
  String loc = "City: " + String(cityName);
  tft.print(loc);
}
