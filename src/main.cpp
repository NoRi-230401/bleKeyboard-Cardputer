#include <Arduino.h>
#include <SD.h>
#include <BleKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <algorithm>
#include <map>

void setup();
void loop();
bool checkKeyInput();
void bleKeySend();
void notifyBleConnect();
void checkAutoPowerOff();
void dispLx(uint8_t Lx, String msg);
void dispModsKeys(String msg);
void dispModsCls();
void dispSendKey(String msg);
void dispSendKey2(String msg);
void dispPowerOff();
void dispState();
void dispBleState();
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
#define N_ROWS 6  // rows   : 行/
#define H_CHR 22  // 1 chara height
#define W_CHR 12  // 1 chara width

// --- hid key-code define ---- 
const uint8_t HID_UPARROW = 0x52;
const uint8_t HID_DOWNARROW = 0x51;
const uint8_t HID_LEFTARROW = 0x50;
const uint8_t HID_RIGHTARROW = 0x4F;
const uint8_t HID_ESC = 0x29;
const uint8_t HID_DELETE = 0x4C;
const uint8_t HID_HOME = 0x4A;
const uint8_t HID_END = 0x4D;
const uint8_t HID_PAGEUP = 0x4B;
const uint8_t HID_PAGEDOWN = 0x4E;
const uint8_t HID_INS = 0x49;
const uint8_t HID_PRINTSC = 0x46;
const uint8_t HID_F5 = 0x3E;
const uint8_t HID_F6 = 0x3F;
const uint8_t HID_F7 = 0x40;
const uint8_t HID_F8 = 0x41;
const uint8_t HID_F9 = 0x42;
const uint8_t HID_F10 = 0x43;

//-------------------------------------
BleKeyboard bleKey;
KeyReport bleKeyReport;

m5::Keyboard_Class::KeysState keys_status;
SPIClass SPI2;

static bool SD_ENABLE;
static bool capsLock = false; // fn + 1  : Cpas Lock On/Off
static bool cursMode = false; // fn + 0  : Cursor moving Mode On/Off
static bool bleConnect = false;
// const String arrow_key[] = {"UpArrow", "DownArrow", "LeftArrow", "RightArrow"};

// -- Auto Power Off  (fn + 9)  ----
unsigned long lastKeyInput = 0;
const int apoTm[] = {3, 5, 10, 15, 20, 30, 999}; // auto Power off Time
const String apoTmStr[] = {" 3m", " 5m", "10m", "15m", "20m", "30m", "off"};
int apoTmIndex = 4;                                                 // 0 to 6
unsigned long autoPowerOffTimeout = apoTm[apoTmIndex] * 60 * 1000L; // ms: wait for PowerOff
const unsigned long WARN_TM = 30 * 1000L;                           // ms: warning befor PowerOff
static bool warnDispFlag = true;

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
    bleKeySend();

  notifyBleConnect();

  if (M5Cardputer.BtnA.wasPressed())
  {
    Serial.println("BtnG0 was Pressed");
    lastKeyInput = millis();
  }
  checkAutoPowerOff();
  delay(20);
}

bool checkKeyInput()
{
  if (M5Cardputer.Keyboard.isChange())
  {
    lastKeyInput = millis();

    if (M5Cardputer.Keyboard.isPressed())
    {
      keys_status = M5Cardputer.Keyboard.keysState();
      return true;
    }
    else // All keys are physically released
    {
      // When no keys are pressed on the Cardputer, release all modifiers on the BLE host.
      // This ensures a clean state.
      bleKey.releaseAll();
      dispModsCls();
    }
  }
  return false;
}

void bleKeySend()
{
  m5::Keyboard_Class::KeysState key = keys_status;
  uint8_t mods = key.modifiers;
  uint8_t keyWord = 0;
  bool existWord = key.word.empty() ? false : true;

  if (existWord)
  {
    keyWord = key.word[0];
    Serial.printf("key.word[0]: %c (0x%X), modifiers: 0x%X\n", keyWord, keyWord, mods);
  }
  else
    Serial.printf("key.word is empty, modifiers: 0x%X\n", mods);

  // ****** [fn] Mode Selection (High Priority) **************
  // Caps Lock Toggle (Fn + '1')
  if (key.fn && existWord && (keyWord == '1' || keyWord == '!'))
  {
    capsLock = !capsLock;
    bleKey.write(KEY_CAPS_LOCK);
    delay(50);
    bleKey.releaseAll();
    dispState();
    dispModsCls();
    return;
  }

  // Auto PowerOff (Fn + '2')
  if (key.fn && existWord && (keyWord == '2' || keyWord == '@'))
  {
    apoTmIndex = apoTmIndex < 6 ? apoTmIndex + 1 : 0;
    autoPowerOffTimeout = apoTm[apoTmIndex] * 60 * 1000L;
    dispState();
    delay(50);
    bleKey.releaseAll();
    dispModsCls();
    return;
  }

  // Cursor moving Mode Toggle (Fn + '3')
  if (key.fn && existWord && (keyWord == '3' || keyWord == '#'))
  {
    cursMode = !cursMode;
    dispState();
    delay(50);
    bleKey.releaseAll();
    dispModsCls();
    return;
  }

  // ** modifies keys(ctrl,shift,alt,) and fn  **
  // use with other key
  String modsDispStr = "";
  if (key.ctrl)
    modsDispStr += "Ctrl ";
  if (key.shift)
    modsDispStr += "Shift ";
  if (key.alt)
    modsDispStr += "Alt ";
  if (key.opt)
    modsDispStr += "Opt ";
  if (key.fn)
    modsDispStr += "Fn ";
  dispModsKeys(modsDispStr);

  // *****  Regular Character Keys *****
  bleKeyReport = {0};
  String hidCode = "";

  // Keys
  int count = 0;
  for (auto hid_key : key.hid_keys)
  {
    if (count < 6)
    {
      bool fixed = false;

      if (key.fn)
      {
        switch (hid_key)
        {
        case 0x35: // '`'
          hid_key = HID_ESC;
          fixed = true;
          break;

        case 0x2A: // 'BACK'
          hid_key = HID_DELETE;
          fixed = true;
          break;

        case 0x22: // '5'
          hid_key = HID_F5;
          fixed = true;
          break;

        case 0x23: // '6'
          hid_key = HID_F6;
          fixed = true;
          break;

        case 0x24: // '7'
          hid_key = HID_F7;
          fixed = true;
          break;

        case 0x25: // '8'
          hid_key = HID_F8;
          fixed = true;
          break;

        case 0x26: // '9'
          hid_key = HID_F9;
          fixed = true;
          break;

        case 0x27: // '0'
          hid_key = HID_F10;
          fixed = true;
          break;
        }
      }

      // *** ARROW KEYS and Cursor moving mode ***
      if (!fixed && (key.fn || cursMode))
      {
        switch (hid_key)
        {
        case 0x33: // ';'
          hid_key = HID_UPARROW;
          fixed = true;
          break;

        case 0x37: // '.'
          hid_key = HID_DOWNARROW;
          fixed = true;
          break;

        case 0x36: // ','
          hid_key = HID_LEFTARROW;
          fixed = true;
          break;

        case 0x38: // '/'
          hid_key = HID_RIGHTARROW;
          fixed = true;
          break;

        case 0x2d: // '-'
          hid_key = HID_HOME;
          fixed = true;
          break;

        case 0x2f: // '['
          hid_key = HID_END;
          fixed = true;
          break;

        case 0x2e: // '='
          hid_key = HID_PAGEUP;
          fixed = true;
          break;

        case 0x30: // ']'
          hid_key = HID_PAGEDOWN;
          fixed = true;
          break;

        case 0x31: // '\'
          hid_key = HID_INS;
          fixed = true;
          break;

        case 0x34: // '''
          hid_key = HID_PRINTSC;
          fixed = true;
          break;
        }
      }

      bleKeyReport.keys[count] = hid_key;
      hidCode += "0x" + String(hid_key, HEX) + String(" ");
      count++;
    }
  }

  // Set Modifiers
  uint8_t modifier = 0;
  if (key.ctrl)
    modifier |= 0x01;
  if (key.shift)
    modifier |= 0x02;
  if (key.alt)
    modifier |= 0x04;
  if (key.opt)
    modifier |= 0x08;
  bleKeyReport.modifiers = modifier;

  // Send
  bleKey.sendReport(&bleKeyReport);

  if (count > 0)
  {
    dispSendKey(hidCode);
    Serial.println("HID: " + hidCode);
  }
  else
    dispLx(5,"");

  delay(50);
  return;
}

static unsigned long PREV_bleChk_time = 0;
void notifyBleConnect()
{
  const unsigned long next_check_time = 1009L; // 1000以上で１番小さい素数にした
  if (millis() < PREV_bleChk_time + next_check_time)
    return;

  if (bleKey.isConnected() && !bleConnect)
  {
    bleConnect = true;
    dispBleState();
  }
  else if (!bleKey.isConnected() && bleConnect)
  {
    bleConnect = false;
    dispBleState();
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
  //----------  Lx is (0 to 5) ------------------
  // L0  "- tiney bleKeyborad -" : BLE info
  // L1    f1:Caps   f2:AutoPower   f3:EditMode
  // L2   [un/lock] [AutoPoffTm]   [on/off]
  // L3    -----
  // L4   modsKeys (Shift/Ctrl/Alt/Opt/Fn)
  // L5   sendKye info(hid code)
  // --------------------------------------------
  if (Lx < 0 || Lx > 5)
    return;

  M5Cardputer.Display.fillRect(0, Lx * H_CHR, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(0, Lx * H_CHR);
  M5Cardputer.Display.print(msg);
}

void dispModsKeys(String msg)
{ // line4 : modifiers keys(Shift/Ctrl/Alt/Opt/Fn)
  dispLx(4, msg);
}

void dispModsCls()
{ // line4 : modifiers keys disp clear
  const int line4 = 4 * H_CHR;
  M5Cardputer.Display.fillRect(0, line4, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);
}

void dispSendKey(String msg)
{ // line5 : normal character send info
  dispLx(5, " HID Code: " + msg);
}

void dispSendKey2(String msg)
{ // line5 : special character send info
  M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  dispLx(5, " " + msg);
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
{ // Line2 : status
  // const String L1Str = "fn1:Cap 2:Apo 3:CurM";
  //---------------------- 01234567890123456789---
  const String StCaps[] = {"unlock", " lock"};
  const String StEditMode[] = {"off", " on"};

  int32_t line2 = 2 * H_CHR;
  M5Cardputer.Display.fillRect(0, line2, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);

  // capsLock state
  M5Cardputer.Display.setCursor(W_CHR * 1, line2);
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

  // Auto PowerOff time
  M5Cardputer.Display.setCursor(W_CHR * 9, line2);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.print(apoTmStr[apoTmIndex]);

  // Edit mode state
  M5Cardputer.Display.setCursor(W_CHR * 16, line2);
  if (cursMode)
  {
    M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    M5Cardputer.Display.print(StEditMode[1]);
  }
  else
  {
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print(StEditMode[0]);
  }

  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
}

void dispBleState()
{ // line0: BLE connect information
  //----------------------"01234567890123456789"------------------;
  // const String PRStr = "- tiny bleKeyboard -";
  //----------------------"01234567890123456789"------------------;

  M5Cardputer.Display.fillRect(0, 0, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(0, 0);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.print("- tiny ");

  if (bleConnect)
    M5Cardputer.Display.setTextColor(TFT_BLUE, TFT_BLACK);
  else
    M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5Cardputer.Display.print("ble");

  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.print("Keyboard -");
}

void dispStateInit()
{
  //-------------------"01234567890123456789"------------------;
  const String L1Str = "fn1:Cap 2:Apo 3:CurM";
  //-------------------"01234567890123456789"------------------;
  dispBleState();

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

  // M5Cardputer.Display.println("- " + PROG_NAME + " -");
  dispStateInit();
}

// #define WAIT_SERIAL_SETTING_DONE
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
