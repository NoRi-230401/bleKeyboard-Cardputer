#include <Arduino.h>
#include <SD.h>
#include <BleKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <algorithm> // For std::find
#include <map>

void setup();
void loop();
bool checkKeyInput();
void bleKeySend();
void notifyBleConnect();
void checkAutoPowerOff();

void dispLx(uint8_t Lx, String msg);
void dispSendKey(String msg);
void dispPowerOff();
void dispState();
void dispStateInit();

void dispInit();
void m5stack_begin();
void SDU_lobby();
bool SD_begin();
void POWER_OFF();

// -- Cardputer display define -------
#define X_WIDTH 240
#define Y_HEIGHT 135
#define X_MAX 239
#define Y_MAX 134
#define N_COLS 20 // colums : 列
#define N_ROWS 6  // rows   : 行
#define H_CHR 22  // 1 chara height
#define W_CHR 12  // 1 chara width
//-------------------------------------

const String PROG_NAME = "Tiny bleKeyboard";
bool SD_ENABLE;
SPIClass SPI2;
BleKeyboard bleKey;
m5::Keyboard_Class::KeysState keys_status;
const String arrow_key[] = {"UpArrow", "DownArrow", "LeftArrow", "RightArrow", "Escape"}; // Display names
int useFnKeyIndex = -1;
unsigned long lastKeyInput = 0;
const unsigned long autoPowerOffTimeout = 20 * 60 * 1000L; // ms: wait for auto PowerOff
const unsigned long WARN_TM = 30 * 1000L;                  // ms: warning for auto PowerOff
static String activeBleModsDisplay = "";                   // Display active BLE modifiers on line 3
static bool warnDispFlag = true;
static bool bleConnect = false;
static bool capsLock = false;
static bool cursorMode = false;

void setup()
{
  m5stack_begin();
  if (SD_ENABLE)
    SDU_lobby();

  bleKey.begin();
  dispInit();
  lastKeyInput = millis();
  Serial.println("Cardputer Started!");
}


void loop()
{
  M5Cardputer.update();

  if (checkKeyInput())
  {
    bleKeySend();
  }

  notifyBleConnect();
  checkAutoPowerOff();
  delay(20);
}

bool checkKeyInput()
{
  if (M5Cardputer.Keyboard.isChange())
  {
    lastKeyInput = millis();
    keys_status = M5Cardputer.Keyboard.keysState();

    if (M5Cardputer.Keyboard.isPressed())
    {
      return true;
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
  return false;  
}

void bleKeySend()
{
  m5::Keyboard_Class::KeysState key = keys_status;

  // --- [fn] Key Processing (High Priority) ---
  if (key.fn)
  {
    // Caps Lock Toggle (Fn + '1')
    if (std::find(key.word.begin(), key.word.end(), '1') != key.word.end())
    {
      capsLock = !capsLock;
      // bleKey.write(KEY_CAPS_LOCK); // Sends CAPS_LOCK press & release
      // CP_key.setCapsLocked(capsLock);

      bleKey.releaseAll(); // Release any other held modifiers
      useFnKeyIndex = -1;  // Reset display state
      dispState();
      dispLx(3, ""); // Clear modifier display
      return;
    }

    // Cursor Mode Toggle (Fn + '2')
    if (std::find(key.word.begin(), key.word.end(), '2') != key.word.end())
    {
      cursorMode = !cursorMode;
      // dispLx(2, cursorMode ? "cursorMode ON" : "cursorMode OFF");
      dispState();
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
        dispSendKey(arrow_key[tempFnKeyIndex]);

      bleKey.releaseAll();
      useFnKeyIndex = -1;
      return;
    }

    // Fn + Del for KEY_DELETE
    if (key.del)
    {
      bleKey.write(KEY_DELETE);
      dispSendKey("Delete");
      bleKey.releaseAll();
      return;
    }
  }

  // --- Update BLE Modifier State (Ctrl, Shift, Alt, Opt/GUI) based on physical keys ---
  uint8_t physicalMods = key.modifiers;
  activeBleModsDisplay = "";

  if (!key.word.empty())
    Serial.printf("key.word[0]: %c (0x%X), physicalMods: 0x%X\n", key.word[0], key.word[0], physicalMods);
  else
    Serial.printf("key.word is empty, physicalMods: 0x%X\n", physicalMods);

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

  if (!activeBleModsDisplay.isEmpty())
  {
    activeBleModsDisplay.trim();
  }
  dispLx(3, activeBleModsDisplay);
  Serial.printf("Modifiers updated. Physical: 0x%X, Display: %s\n", physicalMods, activeBleModsDisplay.c_str());

  if (cursorMode)
  {
    bool arrowKeySentInCursorMode = false;
    if (!key.word.empty())
    {
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
        bleKey.write(arrowKeyCode);
        if (tempDispIndex != -1)
          dispSendKey(arrow_key[tempDispIndex]);

        arrowKeySentInCursorMode = true;
        return;
      }
    }
  }

  // --- Special Keys (Backspace, Enter, Tab) - if Fn not active ---
  if (key.del)
  {
    bleKey.write(KEY_BACKSPACE);
    dispSendKey("Backspace - 0x" + String(KEY_BACKSPACE, HEX));
    return;
  }
  if (key.enter)
  {
    bleKey.write(KEY_RETURN);
    dispSendKey("Enter - 0x" + String(KEY_RETURN, HEX));
    return;
  }
  if (key.tab)
  {
    bleKey.write(KEY_TAB);
    dispSendKey("Tab - 0x" + String(KEY_TAB, HEX));
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
        String ucharStr = String(k_char);
        if (capsLock)
        {
          ucharStr.toUpperCase();
        }
        bleKey.write(ucharStr[0]);
        dispSendKey(ucharStr + " - 0x" + String(ucharStr[0], HEX));
        char_sent_this_event = true;
      }
    }
    if (char_sent_this_event)
    {
      return;
    }
  }
}

static unsigned long PREV_bleChk_time = 0;
void notifyBleConnect()
{
  const unsigned long next_check_time = 1000L;
  if (millis() < PREV_bleChk_time + next_check_time)
    return;

  if (bleKey.isConnected() && !bleConnect)
  {
    bleConnect = true;
    dispState();
  }
  else if (!bleKey.isConnected() && bleConnect)
  {
    bleConnect = false;
    dispState();
  }

  PREV_bleChk_time = millis();
}

void checkAutoPowerOff()
{
  if (millis() > autoPowerOffTimeout + lastKeyInput)
  {
    dispPowerOff();
    POWER_OFF();
  }
  else if (millis() > autoPowerOffTimeout + lastKeyInput - WARN_TM)
  {
    if (warnDispFlag)
    {
      M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
      dispLx(3, "  SLEEP TIME");
      M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      delay(500);
      warnDispFlag = !warnDispFlag;
      dispLx(3, "");
    }
    else
    {
      delay(500);
      warnDispFlag = !warnDispFlag;
    }
  }
}

void dispLx(uint8_t Lx, String msg)
{
  //---   Lx is (0 to 5) -----------
  // L0   - app title -
  // L1   BLE connect info [GREEN]
  // L2    ---
  // L3   (ctrl/shift/alt) [WHITE] modifiers
  // L4    ---
  // L5   Keys-- char/(tab/enter/del)/(UpArrow/DownArrow/LeftArrow/RightArrow) [YELLOW]
  // -----------------------------
  if (Lx < 0 || Lx > 5)
    return;

  M5Cardputer.Display.fillRect(0, Lx * H_CHR, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(0, Lx * H_CHR);
  M5Cardputer.Display.print(msg);
}

void dispSendKey(String msg)
{
  M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  dispLx(5, " SendKey: " + msg);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
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

void dispState()
{
  const String StCaps[] = {" unlock", "  lock"};
  const String StCursorMode[] = {"   off", "   on"};
  const String StBle[] = {"ng", "ok"};

  int32_t line2 = 2 * H_CHR;
  M5Cardputer.Display.fillRect(0, line2, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);

  // capsLock state
  M5Cardputer.Display.setCursor(0, line2);
  if (capsLock)
  {
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.print(StCaps[1]);
  }
  else
  {
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print(StCaps[0]);
  }

  // cusror mode state
  M5Cardputer.Display.setCursor(W_CHR * 8, line2);
  if (cursorMode)
  {
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.print(StCursorMode[1]);
  }
  else
  {
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print(StCursorMode[0]);
  }

  // Ble connect status
  M5Cardputer.Display.setCursor(W_CHR * 17, line2);
  if (bleConnect)
  {
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.print(StBle[1]);
  }
  else
  {
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
    M5Cardputer.Display.print(StBle[0]);
  }

  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void dispStateInit()
{
  //---------------------"01234567890123456789"------------------;
  const String L1Str = "f1:Caps f2:CurMd BLE";
  //---------------------"01234567890123456789"------------------;

  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  dispLx(1, L1Str);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  dispState();
}

void dispInit()
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

  dispStateInit();
}

#define WAIT_SERIAL_SETTING_DONE
void m5stack_begin()
{
  auto cfg = M5.config();
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
  // vsCode terminal cannot get serial data
  //  of cardputer before 5 sec ...!
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
