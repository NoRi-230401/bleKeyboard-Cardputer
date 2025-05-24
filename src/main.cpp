#include <Arduino.h>
#include <SD.h>
#include <BleKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <algorithm> // For std::find
#include <map>

void checkSleepTime();
void notifyConnection();
void keySend(m5::Keyboard_Class::KeysState key);
void keyInput();
void dispKey(String msg);
void dispLx(uint8_t Lx, String msg);
void dispBleInfo(bool connected);
void dispPowerOff();
void disp_init();
void m5stack_begin();
void SDU_lobby();
bool SD_begin();
void POWER_OFF();

String PROG_NAME = "Tiny bleKeyboard";
bool SD_ENABLE;
SPIClass SPI2;
bool DISP_ON = true;
String const arrow_key[] = {"UpArrow", "DownArrow", "LeftArrow", "RightArrow", "Escape"}; // Display names
int useFnKeyIndex = -1;

BleKeyboard bleKey;
unsigned long lastKeyInput = 0;
const unsigned long keyInputTimeout = 30 * 60 * 1000L; //  ms: wait for SLEEP
const unsigned long WARN_TM = 30 * 1000L;              // ms: warning for SLEEP
static String activeBleModsDisplay = "";               // Display active BLE modifiers on line 3
static bool toggleFG = true;

void checkSleepTime()
{
  if (millis() > keyInputTimeout + lastKeyInput)
  {
    dispPowerOff();
    POWER_OFF();
  }
  else if (millis() > keyInputTimeout + lastKeyInput - WARN_TM)
  {
    if (toggleFG)
    {
      M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
      dispLx(3, "  SLEEP TIME");
      M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      delay(500);
      toggleFG = !toggleFG;
      dispLx(3, "");
    }
    else
    {
      delay(500);
      toggleFG = !toggleFG;
    }
  }
}

void dispPowerOff()
{
  dispLx(1, "");
  dispLx(2, "");
  M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
  dispLx(3, "  POWER OFF");
  dispLx(4, "");
  dispLx(5, "");
  delay(5000);
}

void notifyConnection()
{
  static bool connected = false;
  if (bleKey.isConnected() && !connected)
  {
    connected = true;
    dispBleInfo(true);
  }
  else if (!bleKey.isConnected() && connected)
  {
    connected = false;
    dispBleInfo(false);
  }
}

static bool capsLock = false;
// m5::Keyboard_Class CP_key;
static bool cursorMode = false;

void keySend(m5::Keyboard_Class::KeysState key)
{

  // --- [fn] Key Processing (High Priority) ---
  if (key.fn)
  {
    // Caps Lock Toggle (Fn + '1')
    if (std::find(key.word.begin(), key.word.end(), '1') != key.word.end())
    {
      capsLock = !capsLock;
      bleKey.write(KEY_CAPS_LOCK); // Sends CAPS_LOCK press & release
      dispLx(2, capsLock ? "capsLock ON" : "capsLock OFF");
      bleKey.releaseAll(); // Release any other held modifiers
      useFnKeyIndex = -1;  // Reset display state
      dispLx(3, "");       // Clear modifier display
      return;
    }
    // Cursor Mode Toggle (Fn + '2')
    if (std::find(key.word.begin(), key.word.end(), '2') != key.word.end())
    {
      cursorMode = !cursorMode;
      dispLx(2, cursorMode ? "cursorMode ON" : "cursorMode OFF");
      bleKey.releaseAll(); // Release any other held modifiers
      useFnKeyIndex = -1;
      dispLx(3, "");
      return;
    }

    // Fn + Arrow Keys (';', '.', ',', '/', '`')
    uint8_t fnKeyAction = 0;
    int tempFnKeyIndex = -1;
    if (std::find(key.word.begin(), key.word.end(), ';') != key.word.end())
    {
      fnKeyAction = KEY_UP_ARROW;
      tempFnKeyIndex = 0;
    }
    else if (std::find(key.word.begin(), key.word.end(), '.') != key.word.end())
    {
      fnKeyAction = KEY_DOWN_ARROW;
      tempFnKeyIndex = 1;
    }
    else if (std::find(key.word.begin(), key.word.end(), ',') != key.word.end())
    {
      fnKeyAction = KEY_LEFT_ARROW;
      tempFnKeyIndex = 2;
    }
    else if (std::find(key.word.begin(), key.word.end(), '/') != key.word.end())
    {
      fnKeyAction = KEY_RIGHT_ARROW;
      tempFnKeyIndex = 3;
    }
    else if (std::find(key.word.begin(), key.word.end(), '`') != key.word.end())
    {
      fnKeyAction = KEY_ESC;
      tempFnKeyIndex = 4;
    }

    if (fnKeyAction != 0)
    {
      bleKey.write(fnKeyAction);
      dispLx(3, "");
      if (tempFnKeyIndex != -1)
        dispKey(arrow_key[tempFnKeyIndex]);
      // else dispKey("Esc"); // Already handled by arrow_key array for index 4

      bleKey.releaseAll();
      useFnKeyIndex = -1;
      return;
    }

    // Fn + Del for KEY_DELETE
    if (key.del)
    {
      bleKey.write(KEY_DELETE);
      dispKey("Delete : 0x" + String(KEY_DELETE, HEX));
      bleKey.releaseAll();
      return;
    }
  }

  // --- Update BLE Modifier State (Ctrl, Shift, Alt, Opt/GUI) based on physical keys ---
  uint8_t physicalMods = key.modifiers;
  // No longer need to get bleHostMods or use _BIT constants
  activeBleModsDisplay = "";

  // keySend 関数の先頭あたりに追加
  if (!key.word.empty())
  {
    Serial.printf("key.word[0]: %c (0x%X), physicalMods: 0x%X\n", key.word[0], key.word[0], physicalMods);
  }
  else
  {
    Serial.printf("key.word is empty, physicalMods: 0x%X\n", physicalMods);
  }

  // CTRL
  if (physicalMods & 0b0001)
  {
    bleKey.press(KEY_LEFT_CTRL);
    activeBleModsDisplay += "Ctrl ";
  }
  else
  {
    bleKey.release(KEY_LEFT_CTRL);
  }
  // SHIFT
  if (physicalMods & 0b0010)
  {
    bleKey.press(KEY_LEFT_SHIFT);
    activeBleModsDisplay += "Shift ";
  }
  else
  {
    bleKey.release(KEY_LEFT_SHIFT);
  }
  // ALT
  if (physicalMods & 0b0100)
  {
    bleKey.press(KEY_LEFT_ALT);
    activeBleModsDisplay += "Alt ";
  }
  else
  {
    bleKey.release(KEY_LEFT_ALT);
  }
  // OPT (GUI Key)
  if (key.opt)
  {
    bleKey.press(KEY_LEFT_GUI);
    activeBleModsDisplay += "Opt ";
  }
  else
  {
    bleKey.release(KEY_LEFT_GUI);
  }

  // If activeBleModsDisplay is not empty, trim it in place.
  // Then, pass the (possibly trimmed or originally empty) string to dispLx.
  if (!activeBleModsDisplay.isEmpty())
  {
    activeBleModsDisplay.trim(); // Modifies activeBleModsDisplay in place
  }
  // After this, activeBleModsDisplay is either its original empty state, or the trimmed version.
  dispLx(3, activeBleModsDisplay);
  Serial.printf("Modifiers updated. Physical: 0x%X, Display: %s\n", physicalMods, activeBleModsDisplay.c_str());

  // BleKeyboard.h で定義されている修飾キーのビットマスク定数 (T-vK/ESP32-BLE-Keyboard の標準的な値)
  // これらの定数が実際に BleKeyboard.h でどのように定義されているか確認してください。
  const uint8_t BLE_KEY_LEFT_CTRL_BIT_MASK = (1 << 0);  // 0x01
  const uint8_t BLE_KEY_LEFT_SHIFT_BIT_MASK = (1 << 1); // 0x02
  const uint8_t BLE_KEY_LEFT_ALT_BIT_MASK = (1 << 2);   // 0x04
  const uint8_t BLE_KEY_LEFT_GUI_BIT_MASK = (1 << 3);   // 0x08
  // const uint8_t BLE_KEY_RIGHT_CTRL_BIT_MASK  = (1 << 4); // 0x10 (今回は左のみ考慮)
  // const uint8_t BLE_KEY_RIGHT_SHIFT_BIT_MASK = (1 << 5); // 0x20 (今回は左のみ考慮)
  // const uint8_t BLE_KEY_RIGHT_ALT_BIT_MASK   = (1 << 6); // 0x40 (今回は左のみ考慮)
  // const uint8_t BLE_KEY_RIGHT_GUI_BIT_MASK   = (1 << 7); // 0x80 (今回は左のみ考慮)

  // --- Cursor Mode: Translate specific chars to arrow keys (if Fn not active) ---
  if (cursorMode)
  {
    bool arrowKeySentInCursorMode = false;
    // key.word はShiftキーの影響を受けるため、key.word[0] と physicalMods を組み合わせて
    // どの物理キーが押されたかを判断する。
    if (!key.word.empty()) { // key.word が空でないことを確認
      char first_char_in_word = key.word[0];
      uint8_t arrowKeyCode = 0;
      int tempDispIndex = -1;

      // Check for the physical key corresponding to ';' / ':'
      if (first_char_in_word == ';' || first_char_in_word == ':')
      {
        arrowKeyCode = KEY_UP_ARROW;
        tempDispIndex = 0;
      }
      // Check for the physical key corresponding to '.' / '>'
      else if (first_char_in_word == '.' || first_char_in_word == '>')
      {
        arrowKeyCode = KEY_DOWN_ARROW;
        tempDispIndex = 1;
      }
      // Check for the physical key corresponding to ',' / '<'
      else if (first_char_in_word == ',' || first_char_in_word == '<')
      {
        arrowKeyCode = KEY_LEFT_ARROW;
        tempDispIndex = 2;
      }
      // Check for the physical key corresponding to '/' / '?'
      else if (first_char_in_word == '/' || first_char_in_word == '?')
      {
        arrowKeyCode = KEY_RIGHT_ARROW;
        tempDispIndex = 3;
      }
      // Check for the physical key corresponding to '`' / '~'
      else if (first_char_in_word == '`' || first_char_in_word == '~')
      {
        arrowKeyCode = KEY_ESC;
        tempDispIndex = 4;
      }
      // Note: This logic assumes that if Shift is pressed, key.word[0] will contain
      // the shifted character (e.g., ':' for ';'). If key.word becomes empty or
      // behaves differently with Shift for these specific keys, this logic might need adjustment.

      if (arrowKeyCode != 0)
      {
        Serial.printf("CursorMode: Sending arrow key 0x%X via sendReport. Physical Modifiers: 0x%X\n", arrowKeyCode, physicalMods);

        // Fnキー処理と同様に bleKey.write() を試す
        // この時点で bleKey.press(KEY_LEFT_SHIFT) などが physicalMods に基づいて
        // 既に呼び出されているため、修飾キーは有効になっているはず。
        bleKey.write(arrowKeyCode);

        // cursorMode の場合、Fnキー処理のように bleKey.releaseAll() は呼び出さない。
        // Shiftキーなどの修飾キーは、物理的に離されるまで維持されるべき。
        // bleKey.write() はキーのプレスとリリースを行うため、
        // 矢印キーは単発で送信される。

        if (tempDispIndex != -1)
          dispKey(arrow_key[tempDispIndex]);
        arrowKeySentInCursorMode = true;
        return;
      }
    }
  }

  // --- Special Keys (Backspace, Enter, Tab) - if Fn not active ---
  if (key.del)
  {
    bleKey.write(KEY_BACKSPACE);
    dispKey("Backspace : 0x" + String(KEY_BACKSPACE, HEX));
    return;
  }
  if (key.enter)
  {
    bleKey.write(KEY_RETURN);
    dispKey("Enter : 0x" + String(KEY_RETURN, HEX));
    return;
  }
  if (key.tab)
  {
    bleKey.write(KEY_TAB);
    dispKey("Tab : 0x" + String(KEY_TAB, HEX));
    return;
  }

  // --- Regular Character Key Processing ---
  if (!key.word.empty())
  {
    bool char_sent_this_event = false;
    for (char k_char : key.word)
    {
      bool handled_by_cursor_mode_already = false;
      if (cursorMode)
      {
        if (k_char == ';' || k_char == '.' || k_char == ',' || k_char == '/' || k_char == '`')
        {
          handled_by_cursor_mode_already = true;
        }
      }

      if (!handled_by_cursor_mode_already)
      {
        bleKey.write(k_char);
        dispKey(String(k_char) + " : 0x" + String(k_char, HEX));
        char_sent_this_event = true;
      }
    }
    if (char_sent_this_event)
    {
      return;
    }
  }
}

void keyInput()
{
  if (M5Cardputer.Keyboard.isChange())
  {
    lastKeyInput = millis();
    m5::Keyboard_Class::KeysState keys_status = M5Cardputer.Keyboard.keysState();

    if (M5Cardputer.Keyboard.isPressed())
    {
      keySend(keys_status);
    }
    else // All keys are physically released
    {
      // When no keys are pressed on the Cardputer, release all modifiers on the BLE host.
      // This ensures a clean state.
      bleKey.releaseAll();

      activeBleModsDisplay = "";
      dispLx(3, "");
      useFnKeyIndex = -1;
    }
  }
}

#define H_CHR 22 // 1 line height
void dispLx(uint8_t Lx, String msg)
{
  //---   Lx is (1 to 5) -----------
  // L0   - app title -
  // L1   BLE connect info [GREEN]
  // L2    ---
  // L3   (ctrl/shift/alt) [WHITE]
  // L4    ---
  // L5   Keys-- char/(tab/enter/del)/(UpArrow/DownArrow/LeftArrow/RightArrow) [YELLOW]
  if (Lx < 1 || Lx > 5)
    return;

  M5Cardputer.Display.fillRect(0, Lx * H_CHR, M5Cardputer.Display.width(), (Lx + 1) * H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(30, Lx * H_CHR);
  M5Cardputer.Display.print(msg);
}

void dispBleInfo(bool connected)
{
  const String BLE_INF[] = {"OFF", "ON"};

  if (connected)
  {
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    dispLx(1, "BLE connected");
  }
  else
  {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    dispLx(1, "BLE disconnected");
  }
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void dispKey(String msg)
{
  M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  dispLx(5, msg);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void setup()
{
  m5stack_begin();
  if (SD_ENABLE)
    SDU_lobby();

  bleKey.begin();
  disp_init();
  dispBleInfo(false);
  lastKeyInput = millis();
  Serial.println("Cardputer Started!");
}

void loop()
{
  M5Cardputer.update();
  notifyConnection();
  keyInput();
  checkSleepTime();
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
  M5Cardputer.Display.println("- " + PROG_NAME + " -");
}

#define WAIT_SERIAL_SETTING_DONE
void m5stack_begin()
{
  auto cfg = M5.config();
  // M5Cardputer.begin(cfg, true); // M5.config()より先にM5Cardputer.begin()を呼ぶ必要がある場合があるため、念のため修正
  cfg.serial_baudrate = 115200; // シリアル通信速度
  cfg.internal_imu = false;     // IMU (加速度・ジャイロセンサー) を使用しない
  cfg.internal_mic = false;     // マイクを使用しない
  cfg.output_power = false;     // Groveポートの電源出力を無効化 (バッテリー節約のため)
  cfg.led_brightness = 0;
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Speaker.setVolume(0);

  // initial display setup
  M5Cardputer.Display.setBrightness(70);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.setCursor(0, 0);

  SPI2.begin(
      M5.getPin(m5::pin_name_t::sd_spi_sclk),
      M5.getPin(m5::pin_name_t::sd_spi_miso),
      M5.getPin(m5::pin_name_t::sd_spi_mosi),
      M5.getPin(m5::pin_name_t::sd_spi_ss));

  SD_ENABLE = SD_begin();

#ifdef WAIT_SERIAL_SETTING_DONE
  delay(5000);
#endif
  Serial.println("\n\n*** m5stack begin ***");
}

// ------------------------------------------
// SDU_lobby : SD_uploader lobby
// ------------------------------------------
// load "/menu.bin" on SD
//    if 'a' pressed at booting
// ------------------------------------------
void SDU_lobby()
{
  M5Cardputer.update();
  if (M5Cardputer.Keyboard.isKeyPressed('a'))
  {
    updateFromFS(SD, "/menu.bin");
    ESP.restart();

    while (true)
      ;
  }
}

bool SD_begin()
{
  int i = 0;
  while (!SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), SPI2) && i < 10)
  {
    delay(500);
    i++;
  }
  if (i >= 10)
  {
    Serial.println("ERR: SD begin erro...");
    return false;
  }
  return true;
}

void POWER_OFF()
{
  Serial.println(" *** POWER OFF ***");

  SD.end();
  delay(2 * 1000L);
  M5.Power.powerOff();

  for (;;)
  { // never
    delay(1000);
  }
}
