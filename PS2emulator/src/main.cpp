#include <ps2dev.h>    //Emulate a PS/2 device
PS2dev keyboard(10,11);  //clock, data

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

void sendmsg(String msg){

	//activate keyboard intake
	keyboard.keyboard_press(PS2dev::LEFT_SHIFT);
	delay(50);
    keyboard.keyboard_press(PS2dev::F10);
    // small gap between press and release to ensure host detects both
    delay(50);
    keyboard.keyboard_release(PS2dev::F10);
	delay(50);
    keyboard.keyboard_release(PS2dev::LEFT_SHIFT);
	// wait
	delay(1000);
	//delete old sign content
	for (int i=0; i<50; i++){
		keyboard.keyboard_press(PS2dev::DELETE);
		delay(50);
		keyboard.keyboard_release(PS2dev::DELETE);
		delay(50);
	}
	for (int i=0; i<50; i++){
		keyboard.keyboard_press(PS2dev::BACKSPACE);
		delay(50);
		keyboard.keyboard_release(PS2dev::BACKSPACE);
		delay(50);
	}
	//send mode setting
	keyboard.keyboard_release(PS2dev::LEFT_SHIFT);
}

void activateKBD(){
	// Send Shift+F10 every 1000 ms (non-blocking)
  unsigned long now = millis();
  if (now - lastSend >= 1000) {
    lastSend = now;

    // Press left shift, press F10, then release F10 and shift
    // Use PS2dev scan codes defined in ps2dev.h
    keyboard.keyboard_press(PS2dev::LEFT_SHIFT);
	delay(50);
    keyboard.keyboard_press(PS2dev::F10);
    // small gap between press and release to ensure host detects both
    delay(50);
    keyboard.keyboard_release(PS2dev::F10);
	delay(50);
    keyboard.keyboard_release(PS2dev::LEFT_SHIFT);
  }
}




void setup()
{
  keyboard.keyboard_init();
  Serial.begin(9600);
  pinMode(LED_BUILTIN, OUTPUT);
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

  
}