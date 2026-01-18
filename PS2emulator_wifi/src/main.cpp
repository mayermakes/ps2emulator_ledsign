#include <ps2dev.h>    //Emulate a PS/2 device
#include <WiFiS3.h>

#include "secrets.h" 


PS2dev keyboard(10,11);  //clock, data

WiFiServer server(80);

unsigned long timecount = 0;
unsigned long lastSend = 0;

// --- PS/2 helper functions ---
// Send a key (press + release). If 'special' is true use the special scancode path.
static void sendKeyCode(unsigned char code, bool special = false, bool holdShift = false) {
  unsigned char leds;
  if (holdShift) {
    keyboard.keyboard_press(PS2dev::LEFT_SHIFT);
    delay(50);
  }

  if (special) {
    keyboard.keyboard_press_special(code);
    delay(60);
    keyboard.keyboard_release_special(code);
  } else {
    keyboard.keyboard_press(code);
    delay(60);
    keyboard.keyboard_release(code);
  }

  if (holdShift) {
    delay(40);
    keyboard.keyboard_release(PS2dev::LEFT_SHIFT);
  }

  // keep PS/2 handling responsive
  keyboard.keyboard_handle(&leds);
  delay(30);
}

// Map common printable ASCII characters to PS2 scan codes.
// Returns true if mapping exists; sets 'code', 'special' (if special code) and 'needShift'.
static bool charToPS2(char ch, unsigned char &code, bool &special, bool &needShift) {
  special = false;
  needShift = false;

  // Uppercase letters: mark shift and fall through to lowercase mapping
  if (ch >= 'A' && ch <= 'Z') {
    needShift = true;
    ch = ch - 'A' + 'a';
  }

  if (ch >= 'a' && ch <= 'z') {
    switch (ch) {
      case 'a': code = PS2dev::A; break;
      case 'b': code = PS2dev::B; break;
      case 'c': code = PS2dev::C; break;
      case 'd': code = PS2dev::D; break;
      case 'e': code = PS2dev::E; break;
      case 'f': code = PS2dev::F; break;
      case 'g': code = PS2dev::G; break;
      case 'h': code = PS2dev::H; break;
      case 'i': code = PS2dev::I; break;
      case 'j': code = PS2dev::J; break;
      case 'k': code = PS2dev::K; break;
      case 'l': code = PS2dev::L; break;
      case 'm': code = PS2dev::M; break;
      case 'n': code = PS2dev::N; break;
      case 'o': code = PS2dev::O; break;
      case 'p': code = PS2dev::P; break;
      case 'q': code = PS2dev::Q; break;
      case 'r': code = PS2dev::R; break;
      case 's': code = PS2dev::S; break;
      case 't': code = PS2dev::T; break;
      case 'u': code = PS2dev::U; break;
      case 'v': code = PS2dev::V; break;
      case 'w': code = PS2dev::W; break;
      case 'x': code = PS2dev::X; break;
      case 'y': code = PS2dev::Y; break;
      case 'z': code = PS2dev::Z; break;
    }
    return true;
  }

  if (ch >= '0' && ch <= '9') {
    switch (ch) {
      case '0': code = PS2dev::ZERO; break;
      case '1': code = PS2dev::ONE; break;
      case '2': code = PS2dev::TWO; break;
      case '3': code = PS2dev::THREE; break;
      case '4': code = PS2dev::FOUR; break;
      case '5': code = PS2dev::FIVE; break;
      case '6': code = PS2dev::SIX; break;
      case '7': code = PS2dev::SEVEN; break;
      case '8': code = PS2dev::EIGHT; break;
      case '9': code = PS2dev::NINE; break;
    }
    return true;
  }

  switch (ch) {
    case ' ': code = PS2dev::SPACE; return true;
    case ',': code = PS2dev::COMMA; return true;
    case '.': code = PS2dev::PERIOD; return true;
    case '/': code = PS2dev::SLASH; return true;
    case '-': code = PS2dev::MINUS; return true;
    case '=': code = PS2dev::EQUAL; return true;
    case ';': code = PS2dev::SEMI_COLON; return true;
    case '\\': code = PS2dev::BACKSLASH; return true;
    case '[': code = PS2dev::OPEN_BRACKET; return true;
    case ']': code = PS2dev::CLOSE_BRACKET; return true;
    case '\'': code = PS2dev::TICK_MARK; return true;
    case '<': code = PS2dev::COMMA; needShift = true; return true; // '<' is shift + ','
    case '>': code = PS2dev::PERIOD; needShift = true; return true; // '>' is shift + '.'
    default: return false;
  }
}

// Send a single printable character (mapped via charToPS2)
static void sendChar(char ch) {
  unsigned char code;
  bool special = false;
  bool needShift = false;
  if (!charToPS2(ch, code, special, needShift)) return; // ignore unsupported
  sendKeyCode(code, special, needShift);
}

// Send a C-string
static void sendString(const char *s) {
  while (*s) {
    sendChar(*s++);
  }
}

// Press and release Shift+F10
static void sendShiftF10Once() {
  unsigned char leds;
  keyboard.keyboard_press(PS2dev::LEFT_SHIFT);
  delay(50);
  keyboard.keyboard_press(PS2dev::F10);
  delay(50);
  keyboard.keyboard_release(PS2dev::F10);
  delay(50);
  keyboard.keyboard_release(PS2dev::LEFT_SHIFT);
  keyboard.keyboard_handle(&leds);
}

// The exact requested sequence (literal markers):
// Shift+F10, delay, Delete x50, Backspace x50, "<OPR><F2>", message, "<CLR><END>"
void sendMessage(const char *message) {
  unsigned char leds;

  // 1) Shift+F10
  sendShiftF10Once();
  delay(60);

  // 2) Delete 50 times (special key)
  for (int i = 0; i < 50; ++i) {
    keyboard.keyboard_press_special(PS2dev::DELETE);
    delay(50);
    keyboard.keyboard_release_special(PS2dev::DELETE);
    keyboard.keyboard_handle(&leds);
    delay(40);
  }

  // 3) Backspace 50 times
  for (int i = 0; i < 50; ++i) {
    keyboard.keyboard_press(PS2dev::BACKSPACE);
    delay(60);
    keyboard.keyboard_release(PS2dev::BACKSPACE);
    keyboard.keyboard_handle(&leds);
    delay(40);
  }

  // 4) Literal markers and message
  sendString("<OPR><F1>");
  sendString(message);
  sendString("<CLR><END>");
}

void readSerialAndSend() {
  static String inputBuffer = "";

  while (Serial.available() > 0) {
    char c = Serial.read();

    // End of message (newline)
    if (c == '\n') {
      inputBuffer.trim();  // remove whitespace/newlines (also removes \r)

      if (inputBuffer.length() > 0) {
        // sendMessage expects a C string
        sendMessage(inputBuffer.c_str());
      }

      inputBuffer = "";  // clear buffer
    } else {
      inputBuffer += c;
    }
  }
}





void setup()
{
  keyboard.keyboard_init();
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
  
  // Start Access Point using credentials from secrets.h
  Serial.print("Starting AP: ");
  Serial.println(WIFI_SSID);
  // Start access point using WiFiS3 beginAP
  WiFi.beginAP(WIFI_SSID, WIFI_PASS);
  delay(200);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  server.begin();
}
void loop()
{
  //Handle PS2 communication and react to keyboard led change
  //This should be done at least once each 10ms
  unsigned char leds;
  if(keyboard.keyboard_handle(&leds)) {
    //Serial.print('LEDS');
    //Serial.print(leds, HEX);
    digitalWrite(LED_BUILTIN, leds);
  }
  readSerialAndSend();

  // Handle HTTP clients
  WiFiClient client = server.available();
  if (client) {
    // Read request line (non-blocking with small timeout) while keeping PS/2 handling alive
    String reqLine = "";
    unsigned long timeout = millis() + 1000; // 1s to receive request
    bool lineRead = false;
    while (millis() < timeout && client.connected()) {
      while (client.available()) {
        char c = client.read();
        if (c == '\r') continue;
        if (c == '\n') { lineRead = true; break; }
        reqLine += c;
      }
      // keep PS/2 responsive while waiting
      keyboard.keyboard_handle(&leds);
      if (lineRead) break;
      delay(1);
    }

    // Very simple parsing of: GET /ledsign/<message> HTTP/1.1
      // Very simple parsing of: GET /ledsign/<message> HTTP/1.1
      String responseBase = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\n";
      String response = responseBase; // filled for non-root responses
      bool responded = false;
      if (reqLine.startsWith("GET ")) {
      int sp1 = reqLine.indexOf(' ');
      int sp2 = reqLine.indexOf(' ', sp1 + 1);
      if (sp1 >= 0 && sp2 > sp1) {
        String path = reqLine.substring(sp1 + 1, sp2);
        // Serve a small HTML form at /
        if (path == "/" || path == "") {
          String html = "Content-Type: text/html\r\n\r\n";
          html += "<!doctype html><html><head><meta charset=\"utf-8\"><title>LED Sign</title></head><body>";
          html += "<h1>LED Sign</h1>";
          html += "<form onsubmit=\"event.preventDefault();var m=encodeURIComponent(document.getElementById('msg').value);if(m.length>0)location.href='/ledsign/'+m;\">";
          html += "<input id=\"msg\" name=\"msg\" placeholder=\"Message\" style=\"width:300px\">";
          html += "<button type=\"submit\">Send</button>";
          html += "</form>";
          html += "<p>Or use GET /ledsign/&lt;url-encoded-message&gt;</p>";
          html += "</body></html>";
            client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n" + html);
            client.stop();
            responded = true;
        }

        // Expect path like /ledsign/<message>
        const String prefix = "/ledsign/";
        if (path.startsWith(prefix)) {
          String encoded = path.substring(prefix.length());
          // URL-decode
          String decoded = "";
          for (int i = 0; i < encoded.length(); ++i) {
            char ch = encoded.charAt(i);
            if (ch == '%') {
              if (i + 2 < encoded.length()) {
                char h1 = encoded.charAt(i+1);
                char h2 = encoded.charAt(i+2);
                int val = -1;
                auto hexVal = [](char x)->int{
                  if (x >= '0' && x <= '9') return x - '0';
                  if (x >= 'A' && x <= 'F') return x - 'A' + 10;
                  if (x >= 'a' && x <= 'f') return x - 'a' + 10;
                  return -1;
                };
                int v1 = hexVal(h1);
                int v2 = hexVal(h2);
                if (v1 >= 0 && v2 >= 0) {
                  val = v1 * 16 + v2;
                  decoded += (char)val;
                  i += 2;
                  continue;
                }
              }
            } else if (ch == '+') {
              decoded += ' ';
            } else {
              decoded += ch;
            }
          }

          // Call sendMessage with decoded content
          decoded.trim();
          if (decoded.length() > 0) {
            sendMessage(decoded.c_str());
            response += "Message queued\n";
          } else {
            response += "Empty message\n";
          }
        } else {
          response += "Unknown endpoint\n";
        }
      } else {
        response += "Bad request\n";
      }
    } else {
      response += "Unsupported Method\n";
    }

      if (!responded) {
        client.print(response);
        delay(1);
        client.stop();
      }
  }

  
}