#include <BleKeyboard.h>
#include <M5Cardputer.h>
BLECharacteristic *keyboardInput;
#include <map>

#define CARDPUTER
void disp_init();
void prt(String message);
void dispLx(uint8_t Lx, String msg);

bool DISP_ON = true;
String PROG_NAME = "Tiny bleKeyboard";
BleKeyboard bleKey;

// ３分間キー入力がなければスリープ
const unsigned int keyInputTimeout = 180000;

// 最後のキー入力時間
unsigned long lastKeyInput = 0;
constexpr float toneCtrlKey = 2000;
constexpr float toneAltKey = 2100;
constexpr float toneShiftKey = 2200;
constexpr float toneOptKey = 2300;
constexpr float toneFnKey = 2400;

// 直前に有効になっているモディファイアキー
static uint8_t currentModifiers = 0;

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

void keySend(m5::Keyboard_Class::KeysState key)
{
  // タイムアウト内にキー入力したよ。
  lastKeyInput = millis();

  // Store the state of physically pressed modifiers for this event
  uint8_t physicalModifiersThisEvent = key.modifiers;

  // Fnキーが押されている場合の特殊処理
  if (key.fn)
  {
    for (char k_char : key.word) // key.word は std::vector<char> なので range-based for が使えます
    {
      if (k_char == ';')
      {
        // M5Cardputer.Speaker.tone(toneFnKey, 50); // Fnキーの音（短く）
        // M5Cardputer.Speaker.tone(1500, 70);      // UP_ARROWに対応する音（例：少し高めの音）
        bleKey.write(KEY_UP_ARROW); // KEY_UP_ARROW (BleKeyboard.hで定義) を送信
        Serial.println("Fn + ; -> UP_ARROW sent");
        dispLx(3, "Fn+; -> UP"); // 画面の3行目に情報を表示

        if (currentModifiers)
        {
          bleKey.releaseAll();
          currentModifiers = 0;
        }
        return; // このキーイベントの処理を終了
      }
      else if (k_char == '.')
      {                               // DOWN_ARROW
        bleKey.write(KEY_DOWN_ARROW);
        Serial.println("Fn + . -> DOWN_ARROW sent");
        dispLx(3, "Fn+; -> DOWN"); // 画面の3行目に情報を表示

        if (currentModifiers)
        {
          bleKey.releaseAll();
          currentModifiers = 0;
        }
        return; // このキーイベントの処理を終了
      }
      else if (k_char == ',')
      {                               // LEFT_ARROW
        bleKey.write(KEY_LEFT_ARROW);
        Serial.println("Fn + , -> LEFT_ARROW sent");
        dispLx(3, "Fn+; -> LEFT"); // 画面の3行目に情報を表示
        if (currentModifiers)
        {
          bleKey.releaseAll();
          currentModifiers = 0;
        }
        return; // このキーイベントの処理を終了
      }
      else if (k_char == '/')
      {                                // RIGHT_ARROW
        bleKey.write(KEY_RIGHT_ARROW);
        Serial.println("Fn + / -> RIGHT_ARROW sent");
        dispLx(3, "Fn+; -> RIGHT"); // 画面の3行目に情報を表示
        if (currentModifiers)
        {
          bleKey.releaseAll();
          currentModifiers = 0;
        }
        return;
      }
    }
  }

  // --- Modifier Key Processing ---
  // This section handles the state of Ctrl, Shift, Alt based on physical key presses.
  // It updates `currentModifiers` which reflects modifiers intended to be active for BLE transmission.
  if (physicalModifiersThisEvent) // If Ctrl, Shift, or Alt are physically pressed in this event
  {
    currentModifiers = physicalModifiersThisEvent; // Set them as active for BLE for this key event
    Serial.println("Physical Modifiers active this event: " + String(currentModifiers, HEX));
    dispLx(1, "Mods: " + String(currentModifiers, HEX)); // Display physical modifiers
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
  } else if (!key.word.empty() || !key.del || !key.enter || !key.tab) {
    // No physical Ctrl, Shift, Alt pressed in *this* event,
    // but other keys might be pressed. `currentModifiers` might hold state
    // from a *previous* modifier-only press.
    // If no character/action key is pressed either, `currentModifiers` will be cleared later
    // by the keyInput() function when all keys are released.
    dispLx(1, ""); // Clear physical modifier display line if no physical mods in this event
  }

  // --- Action and Character Key Processing ---
  bool characterOrActionSent = false; // Flag to track if a character/action key was sent
  if (!key.word.empty() || key.del || key.enter || key.tab) // Check if there's a non-modifier key to send
  {
    if (currentModifiers)
    {
      if (currentModifiers & 0b0001)
      {
        bleKey.press(KEY_LEFT_CTRL);
        Serial.println("Ctrl");
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
      // Display active BLE modifiers on line 2
      String activeBleMods = "";
      if(currentModifiers & 0b0001) activeBleMods += "Ctrl ";
      if(currentModifiers & 0b0010) activeBleMods += "Shift ";
      if(currentModifiers & 0b0100) activeBleMods += "Alt ";
      
      if (!activeBleMods.isEmpty()) {
        dispLx(2, activeBleMods);
      }
    }

    if (key.del)
    {
      bleKey.write(KEY_BACKSPACE);
      Serial.println("backSpace = " + String(KEY_BACKSPACE, HEX));
      dispLx(3, "backSpace");
      M5Cardputer.Speaker.tone(3000, 5);
      characterOrActionSent = true;
    }

    if (key.enter)
    {
      bleKey.write(KEY_RETURN);
      Serial.println("enter = " + String(KEY_RETURN, HEX));
      dispLx(3, "Return");
      M5Cardputer.Speaker.tone(4000, 5);
      characterOrActionSent = true;
    }

    if (key.tab)
    {
      bleKey.write(KEY_TAB);
      Serial.println("tab = " + String(KEY_TAB, HEX));
      dispLx(3, "Tab");
      M5Cardputer.Speaker.tone(3000, 5);
      characterOrActionSent = true;
    }

    // 普通の文字
    for (auto i : key.word)
    {
      bleKey.write(i);
      // charSend(i, keyboardMode, srMode);
      Serial.println("i = " + String(i, HEX));
      dispLx(4, String(i));
      M5Cardputer.Speaker.tone(4000, 5);
      characterOrActionSent = true;
    }

    // If a character/action key was sent *and* currentModifiers were active for it,
    // release all BLE modifiers and clear currentModifiers.
    if (characterOrActionSent && currentModifiers)
    {
      bleKey.releaseAll();
      currentModifiers = 0;
      dispLx(2, ""); // Clear active BLE modifier display
    } else if (!characterOrActionSent && physicalModifiersThisEvent == 0 && currentModifiers != 0) {
      // This case is tricky: no char/action sent, no physical mods NOW, but currentModifiers was set.
      // This implies a modifier was pressed alone, then released alone.
      // The release is handled by keyInput() when isPressed() becomes false.
    }
  } else if (physicalModifiersThisEvent == 0 && currentModifiers != 0) {
      // Only modifier keys were involved, and now they are not physically pressed.
      // The actual release of these modifiers from BLE will be handled by keyInput()
      // when M5Cardputer.Keyboard.isPressed() becomes false.
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
    } else {
      // All keys have been released physically on the Cardputer
      if (currentModifiers) { // If any modifiers were virtually held by BLE
        bleKey.releaseAll();    // Release them from the BLE host
        currentModifiers = 0;   // Clear our record of active BLE modifiers
        Serial.println("All keys released, clearing BLE modifiers.");
        dispLx(1, ""); // Clear physical modifier display area
        dispLx(2, ""); // Clear BLE active modifier display area
      }
    }
  }
}

#define H_CHR 20 // character height
void dispLx(uint8_t Lx, String msg)
{
  M5Cardputer.Display.fillRect(0, Lx * H_CHR, M5Cardputer.Display.width(), (Lx + 1) * H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(20, Lx * H_CHR);
  M5Cardputer.Display.print(msg);
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
  delay(5000);
  disp_init();

  Serial.println("Hello Tiny keyboard.");
  Serial.println(lastKeyInput);
  // M5Cardputer.Speaker.setVolume(25);
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
  M5Cardputer.Display.setTextDatum(top_left); // 文字の基準位置
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
