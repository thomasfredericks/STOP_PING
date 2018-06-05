// ESP8266-12E ArduCAM Mini Camera Server
//
// This program demonstrates using an ArduCAM Mini 2MP camera with an ESP8266-12E module.
// An OV2640 2MP ArduCAM Mini was used with this program.
//
// The program has a web interface that will allow:
//  - storing and viewing of captured jpg files
//  - viewing live streamed video
//  - changing the resolution
//
// Captured jpeg files are stored on the ESP8266-12E built in memory.
//
// The capture and streaming features can be reached directly via the browser by using the format:
//    http://IPaddress/capture - for still capture
//    http://IPaddress/stream  - for streaming video
//
// The captured stills or streamed video can be accessed as an HTML image:
//    <img src="http://IPaddress/capture">
//        or
//    <img src="http://IPaddress/stream">
//
//
//  Wiring diagram to connect ArduCAM to ESP8266-12E
//
//     ArduCAM mini   ->    ESP8266-12E
//         CS         ->        D0
//        MOSI        ->        D7
//        MISC        ->        D6
//         SCK        ->        D5
//         GND        ->       GND
//         VCC        ->       3V3
//         SDA        ->        D2
//         SCL        ->        D1


#include <FS.h> // FOR SPIFFS
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <ArduCAM.h>
#include <SPI.h>
#include <DNSServer.h>

#include "memorysaver.h"
#if !(defined ESP8266 )
#error Please select the ArduCAM ESP8266 UNO board in the Tools/Board
#endif

//This demo can only work on OV2640_MINI_2MP or ARDUCAM_SHIELD_V2 platform.
#if !(defined (OV2640_MINI_2MP)||(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM)))
#error Please select the hardware platform and camera module in the ../libraries/ArduCAM/memorysaver.h file
#endif

// set GPIO16 as the slave select :
const int CS = 16;

// if the video is chopped or distored, try using a lower value for the buffer
// lower values will have fewer frames per second while streaming
static const size_t bufferSize = 4096; // 4096; //2048; //1024;

static const int fileSpaceOffset = 700000;

static const int wifiType = 1; // 0:Station  1:AP

//AP mode configuration
const char *AP_ssid = "Adaptive Actions";
//Default is no password.If you want to set password,put your password here
const char *AP_password = "t0f5tuFF";

//Station mode you should put your ssid and password
const char *ssid = "yourWiFiSSID"; // Put your SSID here
const char *password = "yourWiFiPassword"; // Put your PASSWORD here

static IPAddress ip(192, 168, 1, 203); // static IP used for browser access: http://IPaddress
static IPAddress gateway(192, 168, 1, 1);
static IPAddress subnet(255, 255, 255, 0);



const String fName = "res.txt";

int fileTotalKB = 0;
int fileUsedKB = 0; int fileCount = 0;
String errMsg = "";
int imgMode = 1; // 0: stream  1: capture
int resolution = 4;
// resolutions:
// 0 = 160x120
// 1 = 176x144
// 2 = 320x240
// 3 = 352x288
// 4 = 640x480
// 5 = 800x600
// 6 = 1024x768
// 7 = 1280x1024
// 8 = 1600x1200

ESP8266WebServer server(80);

DNSServer dnsServer;

ArduCAM myCAM(OV2640, CS);

/////////////////////////////////////////////////
//   Updates Properties file with resolution  ///
/////////////////////////////////////////////////
void updateDataFile()
{

  File f = SPIFFS.open(fName, "w");
  if (!f) {
    Serial.println("prop file open failed");
  }
  else
  {
    Serial.println("====== Writing to prop file =========");

    f.println(resolution);
    Serial.println("Data file updated");
    f.close();
  }

}

///////////////////////////////////////////
//    Saves captured image to memory     //
///////////////////////////////////////////
void myCAMSaveToSPIFFS() {

  // as file space is used, capturing images will get slower. At a certain point, the images will become distored
  // or they will not save at all due to lack of space. To avoid this we set a limit and allow some free space to remain
  if ((fileTotalKB - fileUsedKB) < fileSpaceOffset)
  {
    String maxStr = "====== Maximum Data Storage Reached =========";
    Serial.println(maxStr);
    errMsg = maxStr;
    return;
  }

  String str;
  byte buf[256];
  static int i = 0;

  static int n = 0;
  uint8_t temp, temp_last;

  //  File file;
  //Flush the FIFO
  myCAM.flush_fifo();
  //Clear the capture done flag
  myCAM.clear_fifo_flag();
  //Start capture
  myCAM.start_capture();

   while ( !myCAM.get_bit(ARDUCHIP_TRIG , CAP_DONE_MASK) ) updateNetwork();
  Serial.println("File Capture Done!");

  fileCount++;
  if ( fileCount > 1 ) fileCount = 1;

  str = "/pics/" + String(fileCount)  + ".jpg";

  File f = SPIFFS.open(str, "w");
  if (!f) {
    Serial.println("prop file open failed");
  }
  else
  {
    Serial.println(str);
  }


  i = 0;
  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
#if !(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM))
  SPI.transfer(0xFF);
#endif
  //Read JPEG data from FIFO
  while ( (temp != 0xD9) | (temp_last != 0xFF)) {
    temp_last = temp;
    temp = SPI.transfer(0x00);

    //Write image data to buffer if not full
    if ( i < 256)
      buf[i++] = temp;
    else {
      //Write 256 bytes image data to file
      myCAM.CS_HIGH();
      f.write(buf , 256);
      i = 0;
      buf[i++] = temp;
      myCAM.CS_LOW();
      myCAM.set_fifo_burst();
    }

    updateNetwork();
    //delay(0);
  }

  //Write the remain bytes in the buffer
  if (i > 0) {
    myCAM.CS_HIGH();
    f.write(buf, i);
  }
  //Close the file
  f.close();
  Serial.println("CAM Save Done!");
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//    Retrieves a stored jpg file and sends to browser based on url: http://IPaddress/1.jpg     //
//////////////////////////////////////////////////////////////////////////////////////////////////
bool loadFromSpiffs(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) path += "index.htm";

  if (path.endsWith(".src")) path = path.substring(0, path.lastIndexOf("."));
  //  else if (path.endsWith(".htm")) dataType = "text/html";
  //  else if (path.endsWith(".css")) dataType = "text/css";
  //  else if (path.endsWith(".js")) dataType = "application/javascript";
  //  else if (path.endsWith(".png")) dataType = "image/png";
  //  else if (path.endsWith(".gif")) dataType = "image/gif";
  else if (path.endsWith(".jpg")) dataType = "image/jpeg";
  //  else if (path.endsWith(".ico")) dataType = "image/x-icon";
  //  else if (path.endsWith(".xml")) dataType = "text/xml";
  //  else if (path.endsWith(".pdf")) dataType = "application/pdf";
  //  else if (path.endsWith(".zip")) dataType = "application/zip";
  File dataFile = SPIFFS.open(path.c_str(), "r");
  if (server.hasArg("download")) dataType = "application/octet-stream";

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
  }

  dataFile.close();
  return true;
}

/////////////////////////////////////////////////////////////
//  sets the HTML used for the resoultions drop down       //
/////////////////////////////////////////////////////////////
String getDropDown()
{
  String webString = "";
  webString += "<select name=\"rez\">\n";
  webString += "   <option value=\"0\" ";
  if (resolution == 0)
    webString += " selected=\"seleted\" ";
  webString += ">160x120</option>\n";

  webString += "   <option value=\"1\" ";
  if (resolution == 1)
    webString += " selected=\"seleted\" ";
  webString += ">176x144</option>\n";

  webString += "   <option value=\"2\" ";
  if (resolution == 2)
    webString += " selected=\"seleted\" ";
  webString += ">320x240</option>\n";

  webString += "   <option value=\"3\" ";
  if (resolution == 3)
    webString += " selected=\"seleted\" ";
  webString += ">352x288</option>\n";

  webString += "   <option value=\"4\" ";
  if (resolution == 4)
    webString += " selected=\"seleted\" ";
  webString += ">640x480</option>\n";

  webString += "   <option value=\"5\" ";
  if (resolution == 5)
    webString += " selected=\"seleted\" ";
  webString += ">800x600</option>\n";

  webString += "   <option value=\"6\" ";
  if (resolution == 6)
    webString += " selected=\"seleted\" ";
  webString += ">1024x768</option>\n";

  webString += "   <option value=\"7\" ";
  if (resolution == 7)
    webString += " selected=\"seleted\" ";
  webString += ">1280x1024</option>\n";

  webString += "   <option value=\"8\" ";
  if (resolution == 8)
    webString += " selected=\"seleted\" ";
  webString += ">1600x1200</option>\n";

  webString += "  </select>\n";

  return webString;
}


////////////////////////////////////
// capture initialization         //
////////////////////////////////////
void start_capture() {
  myCAM.clear_fifo_flag();
  myCAM.start_capture();
}

/////////////////////////////////////////////
// capture still image and send to client  //
/////////////////////////////////////////////
void camCapture(ArduCAM myCAM) {

  WiFiClient client = server.client();

  size_t len = myCAM.read_fifo_length();
  if (len >= 0x07ffff) {
    Serial.println("Over size.");
    return;
  } else if (len == 0 ) {
    Serial.println("Size is 0.");
    return;
  }

  myCAM.CS_LOW();
  myCAM.set_fifo_burst();
#if !(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM))
  SPI.transfer(0xFF);
#endif

  if (!client.connected()) return;

  /*
    String response = "HTTP/1.1 200 OK\r\n";
    response += "Content-Type: image/jpeg\r\n";
    response += "Content-Length: " + String(len) + "\r\n\r\n";
    server.sendContent(response);
  */

  static uint8_t buffer[bufferSize] = {0xFF};

  while (len) {
    size_t will_copy = (len < bufferSize) ? len : bufferSize;
    SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
    if (!client.connected()) break;
    client.write(&buffer[0], will_copy);
    len -= will_copy;
  }

  myCAM.CS_HIGH();
}


/////////////////////////////////////////////
// initiate capture and record time used   //
/////////////////////////////////////////////
void serverCapture() {


  start_capture();
  Serial.println("CAM Capturing");

  int total_time = 0;

  total_time = millis();
  while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));
  total_time = millis() - total_time;
  Serial.print("capture total_time used (in miliseconds):");
  Serial.println(total_time, DEC);

  total_time = 0;

  Serial.println("CAM Capture Done!");
  total_time = millis();
  camCapture(myCAM);
  total_time = millis() - total_time;
  Serial.print("send total_time used (in miliseconds):");
  Serial.println(total_time, DEC);
  Serial.println("CAM send Done!");
}

/////////////////////////////
// stream video to client  //
/////////////////////////////
void serverStream() {
  WiFiClient client = server.client();

  String response = "HTTP/1.1 200 OK\r\n";
  response += "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  server.sendContent(response);

  while (client.connected()) {

    start_capture();

    while (!myCAM.get_bit(ARDUCHIP_TRIG, CAP_DONE_MASK));

    size_t len = myCAM.read_fifo_length();
    if (len >= 0x07ffff) {
      Serial.println("Over size.");
      continue;
    } else if (len == 0 ) {
      Serial.println("Size is 0.");
      continue;
    }

    myCAM.CS_LOW();
    myCAM.set_fifo_burst();
#if !(defined (ARDUCAM_SHIELD_V2) && defined (OV2640_CAM))
    SPI.transfer(0xFF);
#endif
    if (!client.connected()) break;
    response = "--frame\r\n";
    response += "Content-Type: image/jpeg\r\n\r\n";
    server.sendContent(response);

    static uint8_t buffer[bufferSize] = {0xFF};

    while (len) {
      size_t will_copy = (len < bufferSize) ? len : bufferSize;
      SPI.transferBytes(&buffer[0], &buffer[0], will_copy);
      if (!client.connected()) break;
      client.write(&buffer[0], will_copy);
      len -= will_copy;
    }
    myCAM.CS_HIGH();

    if (!client.connected()) break;
  }
}

////////////////////////////////////////////////////////////////
//  Default web handler used when client access web server    //
////////////////////////////////////////////////////////////////
void onRequest() {

  String ipStr = WiFi.localIP().toString();
  if (wifiType == 1) // AP mode = 1
  {
    ipStr = WiFi.softAPIP().toString();
  }

 

  Serial.print("server uri: " ); Serial.println(server.uri());

  
  // if url contains request for stored image: http://IPaddress/pics/2.jpg
  if (server.uri().indexOf(".jpg") != -1 )
  {
    loadFromSpiffs(server.uri());
    return;
  }

  //// default HTML
  /*
    String message = "<html><head>\n";
    message += "</head><body>\n";
    message += "<form action=\"http://" + ipStr + "/submit\" method=\"POST\">";
    message += "<h1>ESP8266-12E Arducam Mini</h1>\n";
    if (errMsg != "")
    message += "<div style=\"color:red\">" + errMsg + "</div>";


    if (imgMode == 0) // stream mode
    {
    message += "<div><h2>Video Streaming</h2></div> \n";
    message += "<div><img id=\"ArduCam\" src=\"http://" + ipStr + "/stream\" ></div>\n";
    imgMode = 1; // set back to capture mode so it doesn't get stuck in streaming mode

    }
    else
    {
    message += "<div>";
    message += getDropDown();

    message += " <input type=\"radio\" id=\"strm\" name=\"imgMode\" value=\"stream\" ";

    if (imgMode == 0)
      message += " checked ";
    message += "> Stream \n";

    message += " <input type=\"radio\" id=\"capt\" name=\"imgMode\" value=\"capture\"  ";
    if (imgMode == 1)
      message += " checked ";
    message += "> Capture \n";

    message += "&nbsp; <input type='submit' value='Submit'  >\n";
    message += " &nbsp;  <a style=\"font-size:12px; font-weight:bold;\" href=\"http://" + ipStr + "\">Refresh</a>";
    message += " &nbsp; &nbsp; <a style=\"font-size:12px; font-weight:bold;\" onclick=\"return confirm('Are you sure? This will delete all stored images.')\" ";
    message += " href=\"http://" + ipStr + "/clear\">Clear Data</a>\n";

    message += "</div>\n";


    ///////////////////////////////
    FSInfo fs_info;
    SPIFFS.info(fs_info);

    fileTotalKB = (int)fs_info.totalBytes;
    fileUsedKB = (int)fs_info.usedBytes;


    if (fileCount > 0)
    {
      int percentUsed = ((float)fileUsedKB / (float)(fileTotalKB - fileSpaceOffset)) * 100;
      String colorStr = "green";
      if (percentUsed > 90)
        colorStr = "red";

      message += "<div style=\"width:450px; background-color:darkgray; padding:1px;\">";
      message += "<div style=\"position:absolute; color:white; padding-top:2px; font-size:11px;\"> &nbsp; space used: " + String(percentUsed) + "%</div>";
      message += "<div style=\"width:" + String(percentUsed) + "%; height:16px; background-color: " + colorStr + ";\"></div></div>\n";

    }

    message += "<table><tr>";
    int colCnt = 0;
    for (int i = 1; i <= fileCount; i++)
    {
      message += "<td><a href=\"/pics/" + String(i) + ".jpg\">" + i + ".jpg</a></td>\n";

      colCnt++;
      if (colCnt >= 10) //  columns
      {
        message += "</tr><tr>";
        colCnt = 0;
      }
    }
    message += "</tr></table>\n";




    //useful for debugging max data storage

      message += "<table><tr><td>Total Bytes: </td><td style=\"text-align:right;\">";
      message += fileTotalKB;
      message += "</td></tr><tr><td>Used Bytes: </td><td style=\"text-align:right;\">";
      message += fileUsedKB;
      message += "</td></tr><tr><td>Remaing Bytes: </td><td style=\"text-align:right;\">";
      message += (fileTotalKB - fileUsedKB);
      message += "</td></tr></table>\n";

      float flashChipSize = (float)ESP.getFlashChipSize() / 1024.0 / 1024.0;
      message += "<br>chipSize: ";
      message += flashChipSize;


    message += "<div><img id=\"ArduCam\" src=\"http://" + ipStr + "/capture\" ></div>\n";


    }

    message += "</form> \n";
    message += "</body></html>\n";
  */

   if ( server.uri().startsWith("/www.html") ) {
      String message = "<!DOCTYPE html><html><head> <meta charset=\"UTF-8\"> <title>World Wide Web</title><style>body, html{height: 100%; margin: 0; background-color: #252525; border-color: #454545; color: #ffffff;}.bgimg-1, .bgimg-2, .bgimg-3{position: relative; opacity: 0.65; background-position: center; background-repeat: no-repeat; background-size: cover;}.bgimg-1{background-image: url(\"pics/1.jpg\"); height: 100%;}.caption{position: absolute; height: 100%; width: 100%; text-align: center; color: #fff; text-transform: uppercase; font-family: \"Arial\",sans-serif; font-size: 200px; font-size: 25vh; font-weight: 900; color: #ff0000 ; vertical-align: middle; display: table;}</style></head><body ><div id=\"reloader\" class=\"bgimg-1\"><div id=\"datext\" class=\"caption\"><div id=\"inner\" style=\" display: table-cell; vertical-align: middle;\"> STOP<BR> PING </div></div></div><script type=\"text/javascript\">function replaceImage(){if ( update==true ){var newImage='pics/1.jpg?'+new Date().getMilliseconds(); document.getElementById(\"reloader\").style.backgroundImage=\"url(\"+newImage+\")\"; console.log(\"updating with \"+newImage); setTimeout( replaceImage, 11000);}}setTimeout( replaceImage, 11000);function disconnect(){update=false;document.getElementById(\"inner\").innerHTML=\"DISCO<BR>NECT\";document.getElementById(\"reloader\").style.backgroundImage=\"none\";document.getElementById(\"datext\").style.display=\"table\";}setTimeout( disconnect, 60000);var textVisible=true;var update=true;function blink(){if ( textVisible && update==true ){document.getElementById(\"datext\").style.display=\"none\"; textVisible=false; setTimeout( blink, Math.random() * 10000 + 1000);}else{document.getElementById(\"datext\").style.display=\"table\"; textVisible=true; setTimeout( blink, Math.random() * 500 + 250);}}blink();</script></body></html>";
      //String message = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"; 
     server.send(200, "text/html", message); 
     /*
   } else if ( server.uri().startsWith("/hotspot-detect.html") ) {
    String message = "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"; 
     server.send(200, "text/html", message); 
     */
   } else if ( server.uri().startsWith("/disconnect.html") ) {
      //Serial.println("Disconnecting client");
      //  server.client().disconnect();  
   } else {
     String message = "<!DOCTYPE html><html><head><style>body, html{height: 100%; margin: 0; background-color: #252525; border-color: #454545; color: #fff;}textarea{padding: .4em; line-height: 1.4em; width: 100%; height: 350px; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box; background-color: #2a2a2a; border-color: #1d1d1d; color: #fff; overflow: hidden;}button{background-color: #6B6B6B; border-color: #1f1f1f; color: #fff; text-shadow: 0 1px 0 #111; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box; -webkit-appearance: none; -moz-appearance: none; width: 100%; margin: .5em 0; padding: .7em 1em; font-weight: bold; border-width: 1px; border-style: solid}body, textarea, button{font-size: 1em; line-height: 1.3; font-family: sans-serif}.ui-corner-all{-webkit-border-radius: .3125em; border-radius: .3125em}#l{padding: 10; max-width: 800px; min-height:700px; margin: 0 auto;}#b, #m{display: none}</style></head><body onload=\"show()\"><div id='l'> <h3>FREE WI-FI ACCESS TERMS AND CONDITIONS</h3> <textarea class='ui-corner-all'>This agreement sets out the terms and conditions on which wireless internet access (\"the Service\") is provided free of charge to you, a guest, vendor, board member or employee of the FREE WIFI ANYWHERE association (\"The Association\").Your access to the Service is completely at the discretion of the Association. Access to the Service may be blocked, suspended, or terminated at any time for any reason including, but not limited to, violation of this Agreement, actions that may lead to liability for the Association, disruption of access to other users or networks, and violation of applicable laws or regulations. The Association reserves the right to monitor and collect information while you are connected to the Service and that the collected information can be used at discretion of the Association, including sharing the information with any law enforcement agencies, the Association partners and/or the Association vendors.The Association may revise this Agreement at any time. You must accept this Agreement each time you use the Service and it is your responsibility to review it for any changes each time.We reserve the right at all times to withdraw the Service, change the specifications or manner of use of the Service, to change access codes, usernames, passwords or other security information necessary to access the service.The service and any products or services provided on or in connection with the service are provided on an \"as is\", \"as available\" basis without warranties of any kind. All warranties, conditions, representations, indemnities and guarantees with respect to the content or service and the operation, capacity, speed, functionality, qualifications, or capabilities of the services, goods or personnel resources provided hereunder, whether express or implied, arising by law, custom, prior oral or written statements by the Association, or otherwise (including, but not limited to any warranty of satisfactory quality, merchantability, fitness for particular purpose, title and non-infringement) are hereby overridden, excluded and disclaimed.</textarea> <button id='b' class='ui-btn ui-corner-all'>I Accept</button> </div><script type=\"text/javascript\">var l=document.getElementById(\"l\"); var b=document.getElementById(\"b\"); b.addEventListener(\"click\", function(){window.open( 'www.html', '_self' );}, false); function show(){b.style.display=\"block\";}</script></body></html>";
     server.send(200, "text/html", message); 
   }
  
  // THIS IS THE SINGLE FILE VERSION : String message = "<!DOCTYPE html><html><head><title>Success</title><style>body, html{height: 100%; margin: 0; background-color: #252525; border-color: #454545; color: #fff;}.bgimg-1, .bgimg-2, .bgimg-3{position: relative; opacity: 0.65; background-position: center; background-repeat: no-repeat; background-size: cover;}.bgimg-1{background-image: url(\"pics/1.jpg\"); height: 100%;}textarea{padding: .4em; line-height: 1.4em; width: 100%; height: 350px; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box; background-color: #2a2a2a; border-color: #1d1d1d; color: #fff; overflow: hidden;}button{background-color: #6B6B6B; border-color: #1f1f1f; color: #fff; text-shadow: 0 1px 0 #111; -webkit-box-sizing: border-box; -moz-box-sizing: border-box; box-sizing: border-box; -webkit-appearance: none; -moz-appearance: none; width: 100%; margin: .5em 0; padding: .7em 1em; font-weight: bold; border-width: 1px; border-style: solid}body, textarea, button{font-size: 1em; line-height: 1.3; font-family: sans-serif}.ui-corner-all{-webkit-border-radius: .3125em; border-radius: .3125em}#l{padding: 10; max-width: 800px; min-height:700px; margin: 0 auto;}#b, #m{display: none}</style></head><body onload=\"show()\"><div id='l'> <h3>FREE WI-FI ACCESS TERMS AND CONDITIONS</h3> <textarea class='ui-corner-all'>This agreement sets out the terms and conditions on which wireless internet access (\"the Service\") is provided free of charge to you, a guest, vendor, board member or employee of the FREE WIFI ANYWHERE association (\"The Association\").Your access to the Service is completely at the discretion of the Association. Access to the Service may be blocked, suspended, or terminated at any time for any reason including, but not limited to, violation of this Agreement, actions that may lead to liability for the Association, disruption of access to other users or networks, and violation of applicable laws or regulations. The Association reserves the right to monitor and collect information while you are connected to the Service and that the collected information can be used at discretion of the Association, including sharing the information with any law enforcement agencies, the Association partners and/or the Association vendors.The Association may revise this Agreement at any time. You must accept this Agreement each time you use the Service and it is your responsibility to review it for any changes each time.We reserve the right at all times to withdraw the Service, change the specifications or manner of use of the Service, to change access codes, usernames, passwords or other security information necessary to access the service.The service and any products or services provided on or in connection with the service are provided on an \"as is\", \"as available\" basis without warranties of any kind. All warranties, conditions, representations, indemnities and guarantees with respect to the content or service and the operation, capacity, speed, functionality, qualifications, or capabilities of the services, goods or personnel resources provided hereunder, whether express or implied, arising by law, custom, prior oral or written statements by the Association, or otherwise (including, but not limited to any warranty of satisfactory quality, merchantability, fitness for particular purpose, title and non-infringement) are hereby overridden, excluded and disclaimed.</textarea> <button id='b' class='ui-btn ui-corner-all'>I Accept</button> </div><div id=\"reloader\" class=\"bgimg-1\"></div><script type=\"text/javascript\">var l=document.getElementById(\"l\"); var b=document.getElementById(\"b\"); var m=document.getElementById(\"reloader\"); b.addEventListener(\"click\", function(){m.style.display=\"block\"; l.style.display=\"none\"; replaceImage();}, false); function show(){b.style.display=\"block\"; m.style.display=\"none\";}function replaceImage(){var newImage='pics/1.jpg?'+new Date().getMilliseconds(); document.getElementById(\"reloader\").style.backgroundImage=\"url(\"+newImage+\")\"; console.log(\"updating with \"+newImage); setTimeout( replaceImage, 10000);}</script></body></html>";
 

}

///////////////////////////////////////////////////////////////////////////////////
//    used when form is submitted and at setup to set the camera resolution      //
///////////////////////////////////////////////////////////////////////////////////
void setCamResolution(int reso)
{
  switch (reso)
  {
    case 0:
      myCAM.OV2640_set_JPEG_size(OV2640_160x120);
      resolution = 0;
      break;

    case 1:
      myCAM.OV2640_set_JPEG_size(OV2640_176x144);
      resolution = 1;
      break;

    case 2:
      myCAM.OV2640_set_JPEG_size(OV2640_320x240);
      resolution = 2;
      break;

    case 3:
      myCAM.OV2640_set_JPEG_size(OV2640_352x288);
      resolution = 3;
      break;

    case 4:
      myCAM.OV2640_set_JPEG_size(OV2640_640x480);
      resolution = 4;
      break;

    case 5:
      myCAM.OV2640_set_JPEG_size(OV2640_800x600);
      resolution = 5;
      break;

    case 6:
      myCAM.OV2640_set_JPEG_size(OV2640_1024x768);
      resolution = 6;
      break;

    case 7:
      myCAM.OV2640_set_JPEG_size(OV2640_1280x1024);
      resolution = 7;
      break;

    case 8:
      myCAM.OV2640_set_JPEG_size(OV2640_1600x1200);
      resolution = 8;
      break;

  }
}

///////////////////////////////////////////////////////
//   deletes all files in the /pics directory        //
///////////////////////////////////////////////////////
void clearData()
{
  errMsg = "======  Data Storage Cleared =========";
  Dir dir = SPIFFS.openDir("/pics");
  while (dir.next()) {
    SPIFFS.remove(dir.fileName());
  }

  fileCount = 0;
  fileTotalKB = 0;
  fileUsedKB = 0;

  onRequest();
}


/////////////////////////////////////
//  handles form submission        //
/////////////////////////////////////
void handleSubmit()
{
  errMsg = "";
  Serial.println( "--- Form Submited ---");
  Serial.println( "Server args " + server.args());

  if (server.args() > 0 ) {
    for ( uint8_t i = 0; i < server.args(); i++ ) {

      // can be useful to determine the values from a form post.
      Serial.println( "Server arg " + server.arg(i));
      Serial.println( "Server argName " + server.argName(i) );
      if (server.argName(i) == "imgMode")
      {
        Serial.println(" Image Mode: " + server.arg(i));

        if (server.arg(i) == "stream")
        {
          imgMode = 0;
        }
        else
        {
          imgMode = 1; // capture mode
        }
      }

      if (server.argName(i) == "rez") {

        if (server.arg(i).toInt() != resolution)
        {

          setCamResolution(server.arg(i).toInt());

          updateDataFile();

          //// IMPORTANT: removing or reducing the delay can result in dark images
          delay(1500); // add a delay to allow the white balance to adjust to new resolution
        }
      }
    }
  }

  if (imgMode == 1) // capture
  {
    myCAMSaveToSPIFFS();
  }

  onRequest();

}

////////////////////////////////
//    main setup function     //
////////////////////////////////
void setup() {

  SPIFFS.begin();
  delay(10);


  resolution = 4;
/*
  // SPIFFS.format(); // uncomment to completely clear data including properties file

  // check for properties file
  File f = SPIFFS.open(fName, "r");

  if (!f) {
    // no file exists so lets format and create a properties file
    Serial.println("Please wait 30 secs for SPIFFS to be formatted");

    SPIFFS.format();

    Serial.println("Spiffs formatted");

    f = SPIFFS.open(fName, "w");
    if (!f) {
      Serial.println("properties file open failed");
    }
    else
    {
      // write the defaults to the properties file
      Serial.println("====== Writing to properties file =========");

      f.println(resolution);

      f.close();
    }

  }
  else
  {
    // if the properties file exists on startup,  read it and set the defaults
    Serial.println("Properties file exists. Reading.");

    while (f.available()) {

      // read line by line from the file
      String str = f.readStringUntil('\n');

      Serial.println(str);

      resolution = str.toInt();

    }

    f.close();
  }
*/

  uint8_t vid, pid;
  uint8_t temp;

#if defined(__SAM3X8E__)
  Wire1.begin();
#else
  Wire.begin();
#endif

  Serial.begin(115200);
  Serial.println("ArduCAM Start!");

  // set the CS as an output:
  pinMode(CS, OUTPUT);

  // initialize SPI:
  SPI.begin();
  SPI.setFrequency(4000000); //4MHz

  //Check if the ArduCAM SPI bus is OK
  myCAM.write_reg(ARDUCHIP_TEST1, 0x55);
  temp = myCAM.read_reg(ARDUCHIP_TEST1);
  if (temp != 0x55) {
    Serial.println("SPI1 interface Error!");
  }

  //Check if the camera module type is OV2640
  myCAM.wrSensorReg8_8(0xff, 0x01);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_HIGH, &vid);
  myCAM.rdSensorReg8_8(OV2640_CHIPID_LOW, &pid);
  if ((vid != 0x26 ) && (( pid != 0x41 ) || ( pid != 0x42 )))
    Serial.println("Can't find OV2640 module! pid: " + String(pid));
  else
    Serial.println("OV2640 detected.");


  //Change to JPEG capture mode and initialize the OV2640 module
  myCAM.set_format(JPEG);
  myCAM.InitCAM();

  setCamResolution(resolution);

  myCAM.clear_fifo_flag();

  if (wifiType == 0) {
    if (!strcmp(ssid, "SSID")) {
      Serial.println("Please set your SSID");
    }
    if (!strcmp(password, "PASSWORD")) {
      Serial.println("Please set your PASSWORD");
    }
    // Connect to WiFi network
    Serial.println();
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.mode(WIFI_STA);

    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }

    WiFi.config(ip, gateway, subnet); // remove this line to use DHCP

    Serial.println("WiFi connected");
    Serial.println("");
    Serial.print("ip: ");
    Serial.println(WiFi.localIP());
  } else if (wifiType == 1) {
    Serial.println();
    Serial.println();
    Serial.print("Share AP: ");
    Serial.println(AP_ssid);
    Serial.print("The password is: ");
    Serial.println(AP_password);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_ssid, AP_password);
    Serial.println("");
    Serial.println("AP ip");
    Serial.println(WiFi.softAPIP());
  }

  // setup handlers
  //server.on("/capture", HTTP_GET, serverCapture);
  //server.on("/stream", HTTP_GET, serverStream);
  //server.on("/submit", handleSubmit);
  //server.on("/clear", clearData);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  const byte DNS_PORT = 53;
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());   // if DNSServer is started with "*" for domain name, it will reply with  // provided IP to all DNS request

  
  server.on ( "/generate_204", onRequest );  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
  server.on ( "/fwlink", onRequest );  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  server.onNotFound(onRequest);
  
  server.begin();
  Serial.println("Server started");


  Dir dir = SPIFFS.openDir("/pics");
  while (dir.next()) {
    fileCount++;
  }

  FSInfo fs_info;
  SPIFFS.info(fs_info);

  fileTotalKB = (int)fs_info.totalBytes;
  fileUsedKB = (int)fs_info.usedBytes;

}

void updateNetwork() {
  yield();
  dnsServer.processNextRequest(); 
  server.handleClient();
}

/////////////////////////////
//    Main loop function   //
/////////////////////////////
unsigned long lastTimeCaptured;

void loop() {

  updateNetwork();

  if ( millis() - lastTimeCaptured >= 10000 ) {
    lastTimeCaptured = millis();
    myCAMSaveToSPIFFS();
  }

}

