#include <Arduino.h>
#include <SD.h>
#include <nvs.h>
#include <BleKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <algorithm>
#include <map>

void setup();
void loop();
bool checkInput();
void bleSend();
void notifyBleConnect();
void powerSave();
void dispLx(uint8_t Lx, String msg);
void dispModsKeys(String msg);
void dispModsCls();
void dispSendKey(String msg);
void dispSendKey2(String msg);
void dispPowerOff();
void dispState();
void dispBleState();
void dispInit();
void m5stack_begin();
void SDU_lobby();
bool SD_begin();
void POWER_OFF();
void apoInit();
bool wrtNVS(const String title, uint8_t data);
bool rdNVS(const String title, uint8_t &data);
void dispBatteryLevel();

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
static bool cursMode = false; // fn + 2  : Cursor moving Mode On/Off
static bool bleConnect = false;

// -- Auto Power Off(APO) --- (fn + 3)  ----
nvs_handle_t nvs;
const char *NVS_SETTING = "setting";
const String APO_TITLE = "apo";
const int apoTm[] = {1, 2, 3, 5, 10, 15, 20, 30, 999}; // auto Power off Time
const String apoTmStr[] = {" 1min", " 2min", " 3min", " 5min", "10min", "15min", "20min", "30min", " off "};
const uint8_t apoTmMax = 8;
const uint8_t apoTmDefault = 6;
uint8_t apoTmIndex = apoTmDefault;
unsigned long apoTmout; // ms: auto powerOff(APO) timeout
unsigned long lastKeyInput = 0;
const uint8_t NORMAL_BRIGHT = 70;
const uint8_t LOW_BRIGHT = 20;

void setup()
{
  m5stack_begin();
  if (SD_ENABLE)
    SDU_lobby();

  bleKey.begin();
  apoInit();  // Auto PowerOff initialize
  dispInit(); // display initialize

  lastKeyInput = millis();
}

void loop()
{
  M5Cardputer.update();

  if (checkInput()) // check Cardputer key input
    bleSend();      // if so, send data via bluetooth

  notifyBleConnect(); // check bluetooth connection
  powerSave();        // battery and power save
  delay(20);
}

bool checkInput()
{ // check Cardputer key input
  if (M5Cardputer.Keyboard.isChange())
  {
    lastKeyInput = millis();
    // Serial.println("Cardputer Keyboad is Changed");

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

  if (M5Cardputer.BtnA.wasPressed())
  {
    lastKeyInput = millis();
    Serial.println("BtnG0 was Pressed");
  }

  return false;
}

void bleSend()
{
  m5::Keyboard_Class::KeysState key = keys_status;
  uint8_t mods = key.modifiers;
  uint8_t keyWord = 0;
  bool existWord = key.word.empty() ? false : true;
  String sendWord = "";

  if (existWord)
  {
    keyWord = key.word[0];
    sendWord = " " + String(key.word[0]) + " :hid";
    Serial.printf("key.word[0]: %c (0x%X), modifiers: 0x%X\n", keyWord, keyWord, mods);
  }
  else
    Serial.printf("key.word is empty, modifiers: 0x%X\n", mods);

  // ****** [fn] special mode Selection (High Priority) **************
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

  // Cursor moving Mode Toggle (Fn + '2')
  if (key.fn && existWord && (keyWord == '2' || keyWord == '@'))
  {
    cursMode = !cursMode;
    dispState();
    delay(50);
    bleKey.releaseAll();
    dispModsCls();
    return;
  }

  // APO(Auto PowerOff) ---- (Fn + '3')
  if (key.fn && existWord && (keyWord == '3' || keyWord == '#'))
  {
    apoTmIndex = apoTmIndex < apoTmMax ? apoTmIndex + 1 : 0;
    apoTmout = apoTm[apoTmIndex] * 60 * 1000L;
    dispState();
    wrtNVS(APO_TITLE, apoTmIndex);
    // Serial.println("apoTmIndex: " + String(apoTmIndex));
    Serial.println("autoPowerOff: " + apoTmStr[apoTmIndex]);

    delay(50);
    bleKey.releaseAll();
    dispModsCls();
    return;
  }

  // ** disp modifies keys(ctrl,shift,alt,) and fn  **
  //  these keys are used with other key
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
  // String sendWord = "";

  // Keys
  int count = 0;
  for (auto hidCode : key.hid_keys)
  {
    if (count < 6)
    {
      bool fixed = false;

      if (key.fn)
      {
        switch (hidCode)
        {
        case 0x35: // '`'
          hidCode = HID_ESC;
          fixed = true;
          break;

        case 0x2A: // 'BACK'
          hidCode = HID_DELETE;
          fixed = true;
          break;

        case 0x22: // '5'
          hidCode = HID_F5;
          fixed = true;
          break;

        case 0x23: // '6'
          hidCode = HID_F6;
          fixed = true;
          break;

        case 0x24: // '7'
          hidCode = HID_F7;
          fixed = true;
          break;

        case 0x25: // '8'
          hidCode = HID_F8;
          fixed = true;
          break;

        case 0x26: // '9'
          hidCode = HID_F9;
          fixed = true;
          break;

        case 0x27: // '0'
          hidCode = HID_F10;
          fixed = true;
          break;
        }
      }

      // *** ARROW KEYS and Cursor moving mode ***
      if (!fixed && (key.fn || cursMode))
      {
        switch (hidCode)
        {
        case 0x33: // ';'
          hidCode = HID_UPARROW;
          fixed = true;
          break;

        case 0x37: // '.'
          hidCode = HID_DOWNARROW;
          fixed = true;
          break;

        case 0x36: // ','
          hidCode = HID_LEFTARROW;
          fixed = true;
          break;

        case 0x38: // '/'
          hidCode = HID_RIGHTARROW;
          fixed = true;
          break;

        case 0x2d: // '-'
          hidCode = HID_HOME;
          fixed = true;
          break;

        case 0x2f: // '['
          hidCode = HID_END;
          fixed = true;
          break;

        case 0x2e: // '='
          hidCode = HID_PAGEUP;
          fixed = true;
          break;

        case 0x30: // ']'
          hidCode = HID_PAGEDOWN;
          fixed = true;
          break;

        case 0x31: // '\'
          hidCode = HID_INS;
          fixed = true;
          break;

        case 0x34: // '''
          hidCode = HID_PRINTSC;
          fixed = true;
          break;
        }
      }

      bleKeyReport.keys[count] = hidCode;
      sendWord += " 0x" + String(hidCode, HEX);
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

  if (!sendWord.isEmpty())
  {
    if (existWord)
      dispSendKey(sendWord);
    else
      dispSendKey("hid" + sendWord);
  }
  else
    dispLx(5, "");

  delay(50);
  return;
}

static unsigned long PREV_BLECHK_TM = 0;
void notifyBleConnect()
{
  const unsigned long next_check_tm = 1009L; // 1000以上で１番小さい素数にした
  if (millis() < PREV_BLECHK_TM + next_check_tm)
    return;

  PREV_BLECHK_TM = millis();

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
  dispLx(5, msg);
  Serial.println(msg);
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
  //-------- 01234567890123456789---
  dispLx(3, "     POWER OFF      ");
  dispLx(4, "");
  dispLx(5, "");
  delay(2000);
}

void dispState()
{ // Line2
  //---------------------- 01234567890123456789---
  // _____________L1______"             bt:100%";
  // const String L2Str = "fn1:Cap 2:CurM 3:Apo" ;
  // _____________L3______" unlock   off  30min";
  //---------------------- 01234567890123456789---
  const String StCaps[] = {"unlock", " lock"};
  const String StEditMode[] = {"off", " on"};
  const int32_t line3 = 3 * H_CHR;

  M5Cardputer.Display.fillRect(0, line3, M5Cardputer.Display.width(), H_CHR, TFT_BLACK);

  // capsLock state
  M5Cardputer.Display.setCursor(W_CHR * 1, line3);
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

  // Cursor moving mode state
  M5Cardputer.Display.setCursor(W_CHR * 10, line3);
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

  // Auto PowerOff time
  M5Cardputer.Display.setCursor(W_CHR * 15, line3);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5Cardputer.Display.print(apoTmStr[apoTmIndex]);

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

void dispBatteryLevel()
{ // Line1
  //---------------------- 01234567890123456789---
  // _____________L1______"            bat.100%";
  // const String L2Str = "fn1:Cap 3:CurM 2:Apo" ;
  // _____________L3______" unlock   off  30min";
  //---------------------- 01234567890123456789---
  const int32_t line1 = 1 * H_CHR;

  M5Cardputer.Display.fillRect(W_CHR * 16, line1, W_CHR * 3, H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(W_CHR * 16, line1);
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);

  String batLvlStr = "";
  uint8_t batLvl = (uint8_t)M5.Power.getBatteryLevel();
  if (batLvl > 99)
    batLvlStr = String(batLvl);
  else if (batLvl > 9)
    batLvlStr = " " + String(batLvl);
  else
    batLvlStr = "  " + String(batLvl);

  M5Cardputer.Display.print(batLvlStr);
}

void dispInit()
{
  M5Cardputer.Display.setBrightness(NORMAL_BRIGHT);
  M5Cardputer.Display.setRotation(1);
  M5Cardputer.Display.fillScreen(TFT_BLACK);
  M5Cardputer.Display.setFont(&fonts::Font0);
  M5Cardputer.Display.setTextSize(2);
  M5Cardputer.Display.setTextDatum(top_left); // 文字の基準位置
  M5Cardputer.Display.setTextWrap(false);
  M5Cardputer.Display.setCursor(0, 0);

  //-------------------"01234567890123456789"------------------;
  const String L1Str = "            bat.   %";
  const String L2Str = "fn1:Cap 2:CurM 3:Apo";
  //-------------------"01234567890123456789"------------------;
  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  dispBleState(); // L0 : BLE connect information

  dispLx(1, L1Str); // L1 : Battery Level
  dispBatteryLevel();
  
  //  L2 : fn0 to fn3 SpecialMode TILTE
  const int32_t line2 = 2 * H_CHR;
  M5Cardputer.Display.setCursor(0, line2);  
  M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5Cardputer.Display.print("fn1:");
  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5Cardputer.Display.print("Cap ");

  M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5Cardputer.Display.print("2:");
  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5Cardputer.Display.print("CurM ");

  M5Cardputer.Display.setTextColor(TFT_ORANGE, TFT_BLACK);
  M5Cardputer.Display.print("3:");
  M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5Cardputer.Display.print("Apo");
  // dispLx(2, L2Str); //  L2 : fn0 to fn3 SpecialMode TILTE

  M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  dispState(); // L3 : fn0 to fn3 SpecialMode STATE
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
  M5Cardputer.Display.setBrightness(NORMAL_BRIGHT);
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

void apoInit()
{
  uint8_t data;
  if (rdNVS(APO_TITLE, data))
    apoTmIndex = data;

  if (apoTmIndex > apoTmMax)
    apoTmIndex = apoTmDefault;

  apoTmout = apoTm[apoTmIndex] * 60 * 1000L;
  wrtNVS(APO_TITLE, apoTmIndex);
  Serial.println("autoPowerOff: " + apoTmStr[apoTmIndex]);
}

bool wrtNVS(const String title, uint8_t data)
{
  if (ESP_OK == nvs_open(NVS_SETTING, NVS_READWRITE, &nvs))
  {
    nvs_set_u8(nvs, title.c_str(), data);
    nvs_close(nvs);
    return true;
  }
  nvs_close(nvs);
  return false;
}

bool rdNVS(const String title, uint8_t &data)
{
  if (ESP_OK == nvs_open(NVS_SETTING, NVS_READONLY, &nvs))
  {
    nvs_get_u8(nvs, title.c_str(), &data);
    nvs_close(nvs);
    return true;
  }
  nvs_close(nvs);
  return false;
}

static unsigned long prev_btlvl_disp_tm = 0L;
static unsigned long prev_warn_disp_tm = 0L;
static bool warnDispFlag = true;
static int psState = 0;

void powerSave()
{
  const unsigned long NEXT_BATLVL_TM = 3001L;     // ms: battery disp (3000以上で一番小さい素数)
  const unsigned long LOW_BRIGHT_TM = 40 * 1000L; // ms: lower brightness
  const unsigned long WARN_TM = 15 * 1000L;       // ms: warning befor APO
  const unsigned long NEXT_WARN_TM = 500L;        // ms : warn disp On/Off time

  if (millis() > prev_btlvl_disp_tm + NEXT_BATLVL_TM)
  {
    prev_btlvl_disp_tm = millis();
    dispBatteryLevel();
  }

  if (millis() < lastKeyInput + LOW_BRIGHT_TM)
  { // normal state : no power save
    if (psState > 0)
    {
      psState = 0;
      dispInit();
    }
    return;
  }
  else if (millis() < lastKeyInput + apoTmout - WARN_TM)
  { // lower brightness
    if (psState == 0)
    {
      psState = 1;
      M5Cardputer.Display.setBrightness(LOW_BRIGHT);
    }
    return;
  }
  else if (millis() < lastKeyInput + apoTmout)
  { // warnig disp
    if (psState == 1)
    {
      prev_warn_disp_tm = millis();
      warnDispFlag = true;
      psState = 2;
      return;
    }
  }
  else
  { // auto powerOff
    psState = 3;
    dispPowerOff();
    POWER_OFF();
    return;
    //--- never return ---
  }

  //  **** warning disp ****
  if (psState == 2)
  {
    if (millis() < prev_warn_disp_tm + NEXT_WARN_TM)
      return;

    prev_warn_disp_tm = millis();
    if (warnDispFlag)
    {
      M5Cardputer.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
      //-------- 01234567890123456789---
      dispLx(3, "     SLEEP TIME     ");
      M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      warnDispFlag = !warnDispFlag;
    }
    else
    {
      dispLx(3, "");
      warnDispFlag = !warnDispFlag;
    }
  }
}
