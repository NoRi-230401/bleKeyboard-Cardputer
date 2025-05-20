#include <BleKeyboard.h>
#include <M5Cardputer.h>

BLECharacteristic *keyboardInput;

#include <map>
#define CARDPUTER

// void bluetoothKeyboard();
void disp_init();
void prt(String message);
bool DISP_ON = true;
String PROG_NAME = "Tiny Keyboard";

BleKeyboard bleKey;

// ３分間キー入力がなければスリープ
const unsigned int keyInputTimeout = 180000;
// 最後のキー入力時間
unsigned long lastKeyInput = 0;

enum class KeyboardMode
{
  normal,
  numeric,
  cursor,
  media,
  voice_over,
  talkback,
  narrator,
  nvda,
  jaws
};

std::map<KeyboardMode, std::array<uint8_t, 2>> srCommandKeyMap = {
    {KeyboardMode::voice_over, {KEY_LEFT_CTRL, KEY_LEFT_ALT}},
    {KeyboardMode::talkback, {KEY_LEFT_ALT, 0}},
    {KeyboardMode::nvda, {KEY_NUM_0, 0}},
    {KeyboardMode::jaws, {KEY_NUM_0, 0}},
};

const std::map<KeyboardMode, float> modeToneMap = {
    {KeyboardMode::normal, 1000}, {KeyboardMode::cursor, 1100}, {KeyboardMode::numeric, 1200}, {KeyboardMode::media, 1300}, {KeyboardMode::voice_over, 1400}, {KeyboardMode::talkback, 1500}, {KeyboardMode::narrator, 1600}, {KeyboardMode::jaws, 1700}, {KeyboardMode::nvda, 1800}};

constexpr float toneCtrlKey = 2000;
constexpr float toneAltKey = 2100;
constexpr float toneShiftKey = 2200;
constexpr float toneOptKey = 2300;
constexpr float toneFnKey = 2400;

void checkBattery()
{
  if (millis() > keyInputTimeout + lastKeyInput)
  {
    M5Cardputer.Speaker.tone(1000, 100);
    delay(1000);
    M5.Power.deepSleep(0, false);
  }
}

void notifyConnection()
{
  static bool connected = false;
  if (bleKey.isConnected() && !connected)
  {
    connected = true;
    M5Cardputer.Speaker.tone(2000, 100);
    delay(100);
    M5Cardputer.Speaker.tone(2300, 100);
  }
  else if (!bleKey.isConnected() && connected)
  {
    connected = false;
    M5Cardputer.Speaker.tone(2300, 100);
    delay(100);
    M5Cardputer.Speaker.tone(2000, 100);
  }
}

void charSend(char k, KeyboardMode keyMode, KeyboardMode srMode)
{
  const std::map<char, char> numKeyMap = {
      {',', '*'}, {'.', '0'}, {'/', '#'}, {'l', '7'}, {';', '8'}, {'\'', '9'}, {'p', '4'}, {'[', '5'}, {']', '6'}, {'0', '1'}, {'-', '2'}, {'=', '3'}};
  const std::map<char, char> cursorKeyMap = {
      {',', KEY_UP_ARROW}, {'.', KEY_DOWN_ARROW}, {'m', KEY_LEFT_ARROW}, {'/', KEY_RIGHT_ARROW}, {'k', KEY_HOME}, {'\'', KEY_END}, {'l', KEY_PAGE_UP}, {';', KEY_PAGE_DOWN}};
  // 必要があればスクリーンリーダーコマンドキーを送信
  if (srMode != KeyboardMode::normal)
  {
    for (auto i : srCommandKeyMap.at(srMode))
    {
      if (i)
        bleKey.press(i);
    }
  }
  if (keyMode == KeyboardMode::normal)
  {
    k = 0x04;   /// 'a'
    bleKey.write(k);
    Serial.println("k=" + String(k,HEX));
  }
  else if (keyMode == KeyboardMode::numeric)
  {
    if (numKeyMap.find(k) != numKeyMap.end())
    {
      bleKey.write(numKeyMap.at(k));
    }
  }
  else if (keyMode == KeyboardMode::cursor)
  {
    if (cursorKeyMap.find(k) != cursorKeyMap.end())
    {
      bleKey.write(cursorKeyMap.at(k));
    }
  }
  else if (keyMode == KeyboardMode::media)
  {
    switch (k)
    {
    case '=':
      bleKey.write(KEY_MEDIA_PLAY_PAUSE);
      break;
    case '0':
      bleKey.write(KEY_MEDIA_PREVIOUS_TRACK);
      break;
    case '-':
      bleKey.write(KEY_MEDIA_NEXT_TRACK);
      break;
    default:
      break;
    }
  }
  if (srMode != KeyboardMode::normal)
  {
    bleKey.releaseAll();
  }
}

void keySend(m5::Keyboard_Class::KeysState key)
{
  // 現在のキーボードモード
  static KeyboardMode keyboardMode = KeyboardMode::normal;
  static KeyboardMode srMode = KeyboardMode::voice_over;
  // 直前に有効になっているモディファイアキー
  static uint8_t currentModifiers = 0;

  // タイムアウト内にキー入力したよ。
  lastKeyInput = millis();
  if (key.fn)
  {
    // キーボードモード切替
    if (currentModifiers)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::normal), 100);
      keyboardMode = KeyboardMode::normal;
      currentModifiers = 0;
      bleKey.releaseAll();
    }
    else if (keyboardMode == KeyboardMode::normal)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::cursor), 100);
      keyboardMode = KeyboardMode::cursor;
    }
    else if (keyboardMode == KeyboardMode::cursor)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::numeric), 100);
      keyboardMode = KeyboardMode::numeric;
    }
    else if (keyboardMode == KeyboardMode::numeric)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::media), 100);
      keyboardMode = KeyboardMode::media;
    }
    else if (keyboardMode == KeyboardMode::media)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::normal), 100);
      keyboardMode = KeyboardMode::normal;
    }
    else
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::normal), 100);
      keyboardMode = KeyboardMode::normal;
    }
  }
  else if (key.opt)
  {
    // スクリーンリーダー制御モード
    if (currentModifiers)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::normal), 100);
      srMode = KeyboardMode::normal;
      currentModifiers = 0;
      bleKey.releaseAll();
    }
    else if (srMode == KeyboardMode::normal)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::voice_over), 100);
      srMode = KeyboardMode::voice_over;
    }
    else if (srMode == KeyboardMode::voice_over)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::talkback), 100);
      srMode = KeyboardMode::talkback;
    }
    else if (srMode == KeyboardMode::talkback)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::jaws), 100);
      srMode = KeyboardMode::jaws;
    }
    else if (srMode == KeyboardMode::jaws)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::nvda), 100);
      srMode = KeyboardMode::nvda;
    }
    else if (srMode == KeyboardMode::nvda)
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::normal), 100);
      srMode = KeyboardMode::normal;
    }
    else
    {
      M5Cardputer.Speaker.tone(modeToneMap.at(KeyboardMode::voice_over), 100);
      srMode = KeyboardMode::normal;
    }
  }
  else if (key.modifiers)
  {
    // 修飾キー
    currentModifiers = key.modifiers;
    if (key.alt)
    {
      M5Cardputer.Speaker.tone(toneAltKey, 100);
    }
    else if (key.ctrl)
    {
      M5Cardputer.Speaker.tone(toneCtrlKey, 100);
    }
    else if (key.shift)
    {
      M5Cardputer.Speaker.tone(toneShiftKey, 100);
    }
  }
  else
  {
    if (currentModifiers)
    {
      if (currentModifiers & 0b0001)
      {
        Serial.println("Ctrl");
        bleKey.press(KEY_LEFT_CTRL);
      }
      if (currentModifiers & 0b0010)
      {
        bleKey.press(KEY_LEFT_SHIFT);
        Serial.println("Shift");
      }
      if (currentModifiers & 0b0100)
      {
        bleKey.press(KEY_LEFT_ALT);
        Serial.println("Alt");
      }
    }
    // 普通の文字
    if (key.del)
    {
      bleKey.write(KEY_BACKSPACE);
      Serial.println("backSpace = " + String(KEY_BACKSPACE, HEX));
      M5Cardputer.Speaker.tone(3000, 5);
    }
    if (key.enter)
    {
      bleKey.write(KEY_RETURN);
      Serial.println("enter = " + String(KEY_RETURN, HEX));
      M5Cardputer.Speaker.tone(4000, 5);
    }
    if (key.tab)
    {
      M5Cardputer.Speaker.tone(3000, 5);
      bleKey.write(KEY_TAB);
      Serial.println("tab = " + String(KEY_TAB, HEX));
    }

    for (auto i : key.word)
    {
      bleKey.write(i);
      // charSend(i, keyboardMode, srMode);
       Serial.println("i = " + String(i, HEX));
      M5Cardputer.Speaker.tone(4000, 5);
    }

    if (currentModifiers)
    {
      bleKey.releaseAll();
      currentModifiers = 0;
    }
  }
}



void keyInput()
{
  if (M5Cardputer.Keyboard.isChange())
  {
    if (M5Cardputer.Keyboard.isPressed())
    {
      m5::Keyboard_Class::KeysState keys_status = M5Cardputer.Keyboard.keysState();
      keySend(keys_status);
    }
  }
}



void setup()
{
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.output_power = false;
  cfg.led_brightness = 0;
  M5Cardputer.begin(cfg, true);
  lastKeyInput = millis();
  M5Cardputer.Speaker.setVolume(0);
  bleKey.begin();
  // delay(300);

  delay(5000);
  disp_init();

  Serial.println("Hello Tiny keyboard.");
  Serial.println(lastKeyInput);
  M5Cardputer.Speaker.setVolume(25);
  M5Cardputer.Speaker.tone(2000, 100);
  
}

void loop()
{
  M5Cardputer.update();
  notifyConnection();

  keyInput();

  // checkBattery();
    delay(20);
}

void disp_init()
{
  M5Cardputer.Display.setBrightness(70);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.setCursor(0, 0);
  prt("- " + PROG_NAME + " -");
}

void prt(String message)
{
  Serial.println(message);

  if (DISP_ON)
  {
#ifdef CARDPUTER
    M5Cardputer.Display.println(message);
#else
    M5.Display.println(message);
#endif
  }
}
