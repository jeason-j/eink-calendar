#include <Config.h>
#include <FS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "SPIFFS.h" // ESP32 only
#include <JPEGDecoder.h>
#include <TFT_eSPI.h>      // Hardware-specific library
TFT_eSPI tft = TFT_eSPI(); // Invoke custom library
#include <SPI.h>
#define JPEG_BUFFER 50000

uint64_t USEC = 1000000;

uint8_t lostConnectionCount = 0;

#define minimum(a,b)     (((a) < (b)) ? (a) : (b))

//====================================================================================
//   Decode and render the Jpeg image onto the TFT screen
//====================================================================================
void jpegRender(int xpos, int ypos) {

  // retrieve infomration about the image
  uint16_t  *pImg;
  int16_t mcu_w = JpegDec.MCUWidth;
  int16_t mcu_h = JpegDec.MCUHeight;
  int32_t max_x = JpegDec.width;
  int32_t max_y = JpegDec.height;

  // Jpeg images are draw as a set of image block (tiles) called Minimum Coding Units (MCUs)
  // Typically these MCUs are 16x16 pixel blocks
  // Determine the width and height of the right and bottom edge image blocks
  int32_t min_w = minimum(mcu_w, max_x % mcu_w);
  int32_t min_h = minimum(mcu_h, max_y % mcu_h);

  // save the current image block size
  int32_t win_w = mcu_w;
  int32_t win_h = mcu_h;

  // record the current time so we can measure how long it takes to draw an image
  uint32_t drawTime = millis();

  // save the coordinate of the right and bottom edges to assist image cropping
  // to the screen size
  max_x += xpos;
  max_y += ypos;

  // read each MCU block until there are no more
  while ( JpegDec.readSwappedBytes()) { // Swapped byte order read

    // save a pointer to the image block
    pImg = JpegDec.pImage;

    // calculate where the image block should be drawn on the screen
    int mcu_x = JpegDec.MCUx * mcu_w + xpos;  // Calculate coordinates of top left corner of current MCU
    int mcu_y = JpegDec.MCUy * mcu_h + ypos;

    // check if the image block size needs to be changed for the right edge
    if (mcu_x + mcu_w <= max_x) win_w = mcu_w;
    else win_w = min_w;

    // check if the image block size needs to be changed for the bottom edge
    if (mcu_y + mcu_h <= max_y) win_h = mcu_h;
    else win_h = min_h;

    // copy pixels into a contiguous block
    if (win_w != mcu_w)
    {
      for (int h = 1; h < win_h-1; h++)
      {
        memcpy(pImg + h * win_w, pImg + (h + 1) * mcu_w, win_w << 1);
      }
    }

    // draw image MCU block only if it will fit on the screen
    if ( mcu_x < tft.width() && mcu_y < tft.height())
    {
      // Now push the image block to the screen
      tft.pushImage(mcu_x, mcu_y, win_w, win_h, pImg);
    }

    else if ( ( mcu_y + win_h) >= tft.height()) JpegDec.abort();

  }

  // calculate how long it took to draw the image
  drawTime = millis() - drawTime; // Calculate the time it took

  // print the results to the serial port
  Serial.print  ("Total render time was    : "); Serial.print(drawTime); Serial.println(" ms");
  Serial.println("=====================================");

}

//====================================================================================
//   Print information decoded from the Jpeg image
//====================================================================================
void jpegInfo() {

  Serial.println("===============");
  Serial.println("JPEG image info");
  Serial.println("===============");
  Serial.print  ("Width      :"); Serial.println(JpegDec.width);
  Serial.print  ("Height     :"); Serial.println(JpegDec.height);
  Serial.print  ("Components :"); Serial.println(JpegDec.comps);
  Serial.print  ("MCU / row  :"); Serial.println(JpegDec.MCUSPerRow);
  Serial.print  ("MCU / col  :"); Serial.println(JpegDec.MCUSPerCol);
  Serial.print  ("Scan type  :"); Serial.println(JpegDec.scanType);
  Serial.print  ("MCU width  :"); Serial.println(JpegDec.MCUWidth);
  Serial.print  ("MCU height :"); Serial.println(JpegDec.MCUHeight);
  Serial.println("===============");
  Serial.println("");
}

//====================================================================================
//   Open a Jpeg file and send it to the Serial port in a C array compatible format
//====================================================================================
void createArray(const char *filename) {

  // Open the named file
  fs::File jpgFile = SPIFFS.open( filename, "r");    // File handle reference for SPIFFS
  //  File jpgFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library

  if ( !jpgFile ) {
    Serial.print("ERROR: File \""); Serial.print(filename); Serial.println ("\" not found!");
    return;
  }

  uint8_t data;
  byte line_len = 0;
  Serial.println("");
  Serial.println("// Generated by a JPEGDecoder library example sketch:");
  Serial.println("// https://github.com/Bodmer/JPEGDecoder");
  Serial.println("");
  Serial.println("#if defined(__AVR__)");
  Serial.println("  #include <avr/pgmspace.h>");
  Serial.println("#endif");
  Serial.println("");
  Serial.print  ("const uint8_t ");
  while (*filename != '.') Serial.print(*filename++);
  Serial.println("[] PROGMEM = {"); // PROGMEM added for AVR processors, it is ignored by Due

  while ( jpgFile.available()) {

    data = jpgFile.read();
    Serial.print("0x"); if (abs(data) < 16) Serial.print("0");
    Serial.print(data, HEX); Serial.print(",");// Add value and comma
    line_len++;
    if ( line_len >= 32) {
      line_len = 0;
      Serial.println();
    }

  }

  Serial.println("};\r\n");
  jpgFile.close();
}

//====================================================================================
//   Opens the image file and prime the Jpeg decoder
//====================================================================================
void drawJpeg(const char *filename, int xpos, int ypos) {

  Serial.println("===========================");
  Serial.print("Drawing file: "); Serial.println(filename);
  Serial.println("===========================");

  // Open the named file (the Jpeg decoder library will close it after rendering image)
  fs::File jpegFile = SPIFFS.open( filename, "r");    // File handle reference for SPIFFS
  //  File jpegFile = SD.open( filename, FILE_READ);  // or, file handle reference for SD library

  //ESP32 always seems to return 1 for jpegFile so this null trap does not work
  if ( !jpegFile ) {
    Serial.print("ERROR: File \""); Serial.print(filename); Serial.println ("\" not found!");
    return;
  }

  // Use one of the three following methods to initialise the decoder,
  // the filename can be a String or character array type:

  //boolean decoded = JpegDec.decodeFsFile(jpegFile); // Pass a SPIFFS file handle to the decoder,
  //boolean decoded = JpegDec.decodeSdFile(jpegFile); // or pass the SD file handle to the decoder,
  boolean decoded = JpegDec.decodeFsFile(filename);  // or pass the filename (leading / distinguishes SPIFFS files)

  if (decoded) {
    // print information about the image to the serial port
    jpegInfo();

    // render the image onto the screen at given coordinates
    jpegRender(xpos, ypos);
  }
  else {
    Serial.println("Jpeg file format not supported!");
  }
}

bool parsePathInformation(String screen_uri, char **path, char *host, unsigned *host_len, bool *secure){
  if(screen_uri==""){
    Serial.println("screen_uri is empty at parsePathInformation()");
    return false;
  }
  char *host_start = NULL;
  bool path_resolved = false;
  unsigned hostname_length = 0;
  
  char *url = (char *)screen_uri.c_str();
  for(unsigned i = 0; i < strlen(url); i++){
    switch (url[i])
    {
    case ':':
      if (i < 7) // If we find : past the 7th position, we know it's probably a port number
      { // This is to handle "http" or "https" in front of URL
        if (secure!=NULL&&i == 4)
        {
          *secure = false;
        }
        else if(secure!=NULL)
        {
          *secure = true;
        }
        i = i + 2; // Move our cursor out of the schema into the domain
        host_start = &url[i+1];
        hostname_length = 0;
        continue;
      }
      break;
    case '/': // We know if we skipped the schema, than the first / will be the start of the path
      *path = &url[i];
      path_resolved = true;
      break;
    }
    if(path_resolved){
      break;
    }
    ++hostname_length;
  }
  if(host_start!=NULL&&*path!=NULL&&*host_len>hostname_length){
    memcpy(host, host_start, hostname_length);
    host[hostname_length] = NULL;
    if(path_resolved){
      return true;
    }
  }
  return false;
}

void progressBar(long processed, long total)
{
  int percentage = round(processed * tft.width() / total);
  tft.fillRect(0, 1, percentage, 4, TFT_RED);
}

void downloadJpeg()
{
  WiFiClient client; // Wifi client object BMP request
  int millisIni = millis();
  int millisEnd = 0;
  bool connection_ok = false;
  
  char *path;
  char host[100];
  bool secure = true; // Default to secure
  unsigned hostlen = sizeof(host);

  if(!parsePathInformation(screenUrl, &path, host, &hostlen, &secure)){
    Serial.println("Parsing error!");
    Serial.println(host);
    return;
  }

char request[300];
int rsize = sizeof(request);
strlcpy(request, "POST "        , rsize);
strlcat(request, path           , rsize);
strlcat(request, " HTTP/1.1\r\n", rsize);
strlcat(request, "Host: "       , rsize);
strlcat(request, host           , rsize);
strlcat(request, "\r\n"         , rsize);

if (bearer != "") {
  strlcat(request, "Authorization: Bearer ", rsize);
  strlcat(request, bearer.c_str(), rsize);
  strlcat(request, "\r\n"        , rsize);
}

#ifdef ENABLE_INTERNAL_IP_LOG
  String ip = WiFi.localIP().toString();
  uint8_t ipLenght = ip.length()+3;
  strlcat(request, "Content-Type: application/x-www-form-urlencoded\r\n", rsize);
  strlcat(request, "Content-Length: ", rsize);
  char cLength[4];
  itoa(ipLenght, cLength, 10);
  strlcat(request, cLength    , rsize);
  strlcat(request, "\r\n\r\n" , rsize);
  strlcat(request, "ip="      , rsize);
  strlcat(request, ip.c_str() , rsize);
#endif

  strlcat(request, "\r\n\r\n" , rsize);
  
  #ifdef DEBUG_MODE
    Serial.println("- - - - - - - - - ");
    Serial.println(request);
    Serial.println("- - - - - - - - - ");
  #endif
  Serial.print("connecting to "); Serial.println(host);
  if (!client.connect(host, 80))
  {
    tft.println("Connection timeout. Check your internet connection");
    Serial.println("connection failed");
    return;
  }
  client.print(request); //send the http request to the server
  client.flush();
  uint32_t imgLength = 0;

  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line.startsWith("Content-Length:")) { //16
      String contentLength = line.substring(16,line.length());
      imgLength = contentLength.toInt();
    }

    if (!connection_ok)
    {
      connection_ok = line.startsWith("HTTP/1.1 200 OK");
    }
    if (line == "\r")
    {
      Serial.println("headers received");
      break;
    }
  }
  
  Serial.printf("Image length: %d bytes\n", imgLength);

  uint8_t *jpegBuffer = new uint8_t[JPEG_BUFFER];
  uint32_t c = 1;

  while (c <= imgLength) {
    yield();
    if (client.available()) {
     jpegBuffer[c] = client.read();
     c++;
     // Remove this fancy loading bar if you want to make it faster
     if (c%10 == 0) {
      progressBar(c, imgLength);
     }
     } else {
       delay(1);
     }
  }
  millisEnd = millis();
  Serial.printf("JPG download: %d ms\n", millisEnd-millisIni);  
  
  bool decoded = JpegDec.decodeArray(jpegBuffer,c);
  
  Serial.printf("JPG decoding: %d ms\n", millis()-millisEnd);  

    if (decoded) {
    jpegInfo();
    jpegRender(0, 0);
    Serial.printf("%d bytes read from cale.es\n", c);
  }
  else {
    Serial.println("Jpeg file format not supported!");
  }

}

/** Callback for receiving IP address from AP */
void gotIP(system_event_id_t event) {
  Serial.println("gotIP: Download image & Render the display");
  downloadJpeg();
}

/** Callback for connection loss */
void lostCon(system_event_id_t event) {
    ++lostConnectionCount;
    Serial.printf("WiFi lost connection try %d to connect again\n", lostConnectionCount);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    if (lostConnectionCount==4) {
      tft.println("Cannot connect to the internet. Check your Config.h credentials");
    }
}

void loop(){

}

void setup(void) {
  Serial.begin(115200);

  tft.begin();
  tft.setRotation(DISPLAY_ROTATION);  
  tft.fillScreen(TFT_WHITE);

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialisation failed!");
  }

  // SPIFFS test. Uncomment this first after doing: pio run --target uploadfs
  // To make sure an image from the SPIFFS renders in your display:
  // drawJpeg("/monkey.jpg", 0 , 0);return;

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  	// Setup callback function for successful connection
	WiFi.onEvent(gotIP, SYSTEM_EVENT_STA_GOT_IP);
	// Setup callback function for lost connection
	WiFi.onEvent(lostCon, SYSTEM_EVENT_STA_DISCONNECTED);
}
