#include <ESP8266WiFi.h> //1.0.7
#include <MD_MAX72xx.h> //3.5.1
#include <SPI.h>

// Use the CH34X USB driver if an error occurs

#define PRINT_CALLBACK 0
#define DEBUG 0
#define LED_HEARTBEAT 1 // Set this to 1 to enable the LED heartbeat

#if DEBUG
#define PRINT(s, v) { Serial.print(F(s)); Serial.print(v); }
#define PRINTS(s)   { Serial.print(F(s)); }
#else
#define PRINT(s, v)
#define PRINTS(s)
#endif

#if LED_HEARTBEAT
#define HB_LED D2
#define HB_LED_TIME 500 // in milliseconds
#endif

// Define the number of devices we have in the chain and the hardware interface
#define MAX_DEVICES 12

#define CLK_PIN D5 // or SCK
#define DATA_PIN D7 // or MOSI
#define CS_PIN D8 // or SS

int ledPin = D1;
// int ledBlue = D2;
int value;

// SPI hardware interface
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW // Edit this as per your LED matrix hardware type
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// WiFi login parameters - network name and password
const char* ssid = "*************"; // Edit your WiFi SSID here
const char* password = "**********"; // Edit your WiFi password here

// WiFi Server object and parameters
WiFiServer server(80);

// Global message buffers shared by Wifi and Scrolling functions
const uint8_t MESG_SIZE = 255;
const uint8_t CHAR_SPACING = 1;
const uint8_t SCROLL_DELAY = 60;

char curMessage[MESG_SIZE];
char newMessage[MESG_SIZE];
bool newMessageAvailable = false;
char WebResponse[] = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";

char WebPage[] =
"<!DOCTYPE html>"
"<html>"
"<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Surin Learning Lab</title>"
"<style>"
"html, body {"
"  font-family: Helvetica;"
"  display: block;"
"  margin: 0px auto;"
"  text-align: center;"
"  background-color: #cad9c5;"
"  background-size: cover;"
"}"
"#container {"
"  width: 100%;"
"  height: 100%;"
"  margin-left: 5px;"
"  margin-top: 20px;"
"  border: solid 2px;"
"  padding: 10px;"
"  background-color: #2dfa53;"
"}"
"</style>"
"<script>"
"strLine = \"\";"
"function SendText() {"
"  nocache = \"/&nocache=\" + Math.random() * 1000000;"
"  var request = new XMLHttpRequest();"
"  strLine = \"&MSG=\" + document.getElementById(\"txt_form\").Message.value;"
"  request.open(\"GET\", strLine + nocache, false);"
"  request.send(null);"
"}"
"</script>"
"</head>"
"<body>"
"<H1><b>Surin Learning Lab</b></H1>"
"<div id=\"container\">"
"<form id=\"txt_form\" name=\"frmText\">"
"<label>Message:<input type=\"text\" name=\"Message\" maxlength=\"255\"></label>%ltbr><br>"
"</form>"
"<br>"
"<input type=\"submit\" value=\"Send Text\" onclick=\"SendText()\">"
"<p><b>Visit link for more: </b>"
"<a href=\"https://www.surinlearninglab.com\">www.surinlearninglab.com</a></p>"
"</div>"
"</body>"
"</html>";

char* err2Str(wl_status_t code)
{
  switch (code)
  {
    case WL_IDLE_STATUS:    return "IDLE";
    case WL_NO_SSID_AVAIL:  return "NO_SSID_AVAIL";
    case WL_CONNECTED:      return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_DISCONNECTED:   return "DISCONNECTED";
    default: return "??";
  }
}

uint8_t htoi(char c)
{
  c = toupper(c);
  if ((c >= '0') && (c <= '9')) return (c - '0');
  if ((c >= 'A') && (c <= 'F')) return (c - 'A' + 0xA);
  return (0);
}

boolean getText(char* szMesg, char* psz, uint8_t len)
{
  boolean isValid = false;
  char* pStart;
  char* pEnd;

  pStart = strstr(szMesg, "/&MSG=");

  if (pStart != NULL)
  {
    pStart += 6;
    pEnd = strstr(pStart, "/&");

    if (pEnd != NULL)
    {
      while (pStart != pEnd)
      {
        if ((*pStart == '%') && isdigit(*(pStart + 1)))
        {
          char c = 0;
          pStart++;
          c += (htoi(*pStart++) << 4);
          c += htoi(*pStart++);
          *psz++ = c;
        }
        else
          *psz++ = *pStart++;
      }

      *psz = '\0';
      isValid = true;
    }
  }

  return isValid;
}

void handleWiFi(void)
{
  static enum { S_IDLE, S_WAIT_CONN, S_READ, S_EXTRACT, S_RESPONSE, S_DISCONN } state = S_IDLE;
  static char szBuf[1024];
  static uint16_t idxBuf = 0;
  static WiFiClient client;
  static uint32_t timeStart;

  switch (state)
  {
    case S_IDLE:
      PRINTS("\nS_IDLE");
      idxBuf = 0;
      state = S_WAIT_CONN;
      break;

    case S_WAIT_CONN:
    {
      client = server.available();
      if (!client) break;
      if (!client.connected()) break;

#if DEBUG
      char szTxt[20];
      sprintf(szTxt, "%03d:%03d:%03d:%03d", client.remoteIP()[0], client.remoteIP()[1], client.remoteIP()[2], client.remoteIP()[3]);
      PRINT("\nNew client @ ", szTxt);
#endif

      timeStart = millis();
      state = S_READ;
    }
    break;

    case S_READ:
      PRINTS("\nS_READ");
      while (client.available())
      {
        char c = client.read();
        if ((c == '\r') || (c == '\n'))
        {
          szBuf[idxBuf] = '\0';
          client.flush();
          PRINT("\nRecv: ", szBuf);
          state = S_EXTRACT;
        }
        else
          szBuf[idxBuf++] = (char)c;
      }
      if (millis() - timeStart > 1000)
      {
        PRINTS("\nWait timeout");
        state = S_DISCONN;
      }
      break;

    case S_EXTRACT:
      PRINTS("\nS_EXTRACT");
      newMessageAvailable = getText(szBuf, newMessage, MESG_SIZE);
      PRINT("\nNew Msg: ", newMessage);
      state = S_RESPONSE;
      break;

    case S_RESPONSE:
      PRINTS("\nS_RESPONSE");
      client.print(WebResponse);
      client.print(WebPage);
      state = S_DISCONN;
      break;

    case S_DISCONN:
      PRINTS("\nS_DISCONN");
      client.flush();
      client.stop();
      state = S_IDLE;
      break;

    default:
      state = S_IDLE;
  }
}

void scrollDataSink(uint8_t dev, MD_MAX72XX::transformType_t t, uint8_t col)
{
#if PRINT_CALLBACK
  Serial.print("\n cb ");
  Serial.print(dev);
  Serial.print(' ');
  Serial.print(t);
  Serial.print(' ');
  Serial.println(col);
#endif
}

uint8_t scrollDataSource(uint8_t dev, MD_MAX72XX::transformType_t t)
{
  static enum { S_IDLE, S_NEXT_CHAR, S_SHOW_CHAR, S_SHOW_SPACE } state = S_IDLE;
  static char* p;
  static uint16_t curLen, showLen;
  static uint8_t cBuf[8];
  uint8_t colData = 0;

  switch (state)
  {
    case S_IDLE:
      PRINTS("\nS_IDLE");
      p = curMessage;
      if (newMessageAvailable)
      {
        strcpy(curMessage, newMessage);
        newMessageAvailable = false;
      }
      state = S_NEXT_CHAR;
      break;

    case S_NEXT_CHAR:
      PRINTS("\nS_NEXT_CHAR");
      if (*p == '\0')
        state = S_IDLE;
      else
      {
        showLen = mx.getChar(*p++, sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
        curLen = 0;
        state = S_SHOW_CHAR;
      }
      break;

    case S_SHOW_CHAR:
      PRINTS("\nS_SHOW_CHAR");
      colData = cBuf[curLen++];
      if (curLen < showLen)
        break;

      showLen = (*p != '\0' ? CHAR_SPACING : (MAX_DEVICES * COL_SIZE) / 2);
      curLen = 0;
      state = S_SHOW_SPACE;
    case S_SHOW_SPACE:
      PRINT("\nS_ICSPACE: ", curLen);
      PRINT("/", showLen);
      curLen++;
      if (curLen == showLen)
        state = S_NEXT_CHAR;
      break;

    default:
      state = S_IDLE;
  }

  return colData;
}

void scrollText(void)
{
  static uint32_t prevTime = 0;

  if (millis() - prevTime >= SCROLL_DELAY)
  {
    mx.transform(MD_MAX72XX::TSL);
    prevTime = millis();
  }
}

void setup()
{
#if DEBUG
  Serial.begin(115200);
  PRINTS("\n[MD_MAX72XX WiFi Message Display]\nType a message for the scrolling display from your internet browser");
#endif

#if LED_HEARTBEAT
  pinMode(HB_LED, OUTPUT);
  digitalWrite(HB_LED, LOW);
#endif

  mx.begin();
  mx.setShiftDataInCallback(scrollDataSource);
  mx.setShiftDataOutCallback(scrollDataSink);

  curMessage[0] = newMessage[0] = '\0';

  PRINT("\nConnecting to ", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    PRINT("\n", err2Str(WiFi.status()));
    delay(500);
  }
  PRINTS("\nWiFi connected");

  server.begin();
  PRINTS("\nServer started");

  sprintf(curMessage, "%03d:%03d:%03d:%03d", WiFi.localIP()[0], WiFi.localIP()[1], WiFi.localIP()[2], WiFi.localIP()[3]);
  PRINT("\nAssigned IP ", curMessage);

  pinMode(ledPin, OUTPUT);
  Serial.begin(9600);
  Serial.print("\nAssigned IP ");
}

void loop()
{
#if LED_HEARTBEAT
  static uint32_t timeLast = 0;

  if (millis() - timeLast >= HB_LED_TIME)
  {
    digitalWrite(HB_LED, digitalRead(HB_LED) == LOW ? HIGH : LOW);
    timeLast = millis();
  }
#endif

  handleWiFi();
  scrollText();

  digitalWrite(ledPin, 10);
}
