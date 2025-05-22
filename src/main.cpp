#include <Arduino.h>
#include <SD.h>
#include <BleKeyboard.h>
#include <M5Cardputer.h>
#include <M5StackUpdater.h>
#include <map>

void checkBattery();
void notifyConnection();
void keySend(m5::Keyboard_Class::KeysState key);
void keyInput();
void dispKey(String msg);
void dispLx(uint8_t Lx, String msg);
void dispBleInfo(bool connected);
void dispSleepTime();
void disp_init();
void m5stack_begin();
void SDU_lobby();
bool SD_begin();

String PROG_NAME = "Tiny bleKeyboard";
bool SD_ENABLE;
SPIClass SPI2;
bool DISP_ON = true;
String const arrow_key[] = {"up_arrow", "down_arrow", "left_arrow", "right_arrow"};
int arrow_key_index = -1;

BleKeyboard bleKey;
const unsigned long keyInputTimeout = 10 * 60 * 1000L; // (ms) wait for sleep
unsigned long lastKeyInput = 0;
static uint8_t currentModifiers = 0;
static String activeBleMods = ""; // Display active BLE modifiers on line 3

void checkBattery()
{
  if (millis() > keyInputTimeout + lastKeyInput)
  {
    dispSleepTime();
    M5.Power.deepSleep(0, false);
  }
}

void dispSleepTime()
{
  dispLx(1, "");
  dispLx(2, "");
  dispLx(3, "SLEEP TIME");
  dispLx(4, "");
  dispLx(5, "");
  delay(1000);

  dispLx(3, "");
  delay(500);

  dispLx(3, "SLEEP TIME");
  delay(1000);
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

void keySend(m5::Keyboard_Class::KeysState key)
{
  lastKeyInput = millis();
  uint8_t physicalModifiersThisEvent = key.modifiers;

  // --- [fn] + arrow_key ----------
  if (key.fn)
  {
    for (char k_char : key.word)
    {
      if (k_char == ';')
      {
        bleKey.write(KEY_UP_ARROW);
        arrow_key_index = 0;
      }
      else if (k_char == '.')
      { // DOWN_ARROW
        bleKey.write(KEY_DOWN_ARROW);
        arrow_key_index = 1;
      }
      else if (k_char == ',')
      { // LEFT_ARROW
        bleKey.write(KEY_LEFT_ARROW);
        arrow_key_index = 2;
      }
      else if (k_char == '/')
      { // RIGHT_ARROW
        bleKey.write(KEY_RIGHT_ARROW);
        arrow_key_index = 3;
      }

      if (arrow_key_index >= 0 && arrow_key_index <= 3)
      {
        dispKey(arrow_key[arrow_key_index]);
        if (currentModifiers)
        {
          bleKey.releaseAll();
          currentModifiers = 0;
        }
        arrow_key_index = -1;
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
  }
  else if (!key.word.empty() || !key.del || !key.enter || !key.tab)
  {
    // No physical Ctrl, Shift, Alt pressed in *this* event,
    // but other keys might be pressed. `currentModifiers` might hold state
    // from a *previous* modifier-only press.
    // If no character/action key is pressed either, `currentModifiers` will be cleared later
    // by the keyInput() function when all keys are released.
  }

  // --- Action and Character Key Processing ---
  bool characterOrActionSent = false;
  // Flag to track if a character/action key was sent

  if (!key.word.empty() || key.del || key.enter || key.tab) // Check if there's a non-modifier key to send
  {
    if (currentModifiers)
    {
      activeBleMods = "";

      if (currentModifiers & 0b0001)
      {
        bleKey.press(KEY_LEFT_CTRL);
        activeBleMods += "Ctrl ";
      }
      if (currentModifiers & 0b0010)
      {
        bleKey.press(KEY_LEFT_SHIFT);
        activeBleMods += "Shift ";
      }
      if (currentModifiers & 0b0100)
      {
        bleKey.press(KEY_LEFT_ALT);
        activeBleMods += "Alt ";
      }
    }

    // -- Special keys (BACKSPACE/ENTER/TAB) -----------
    if (key.del)
    {
      bleKey.write(KEY_BACKSPACE);
      dispKey("BackSpace : 0x" + String(KEY_BACKSPACE, HEX));
      characterOrActionSent = true;
    }
    if (key.enter)
    {
      bleKey.write(KEY_RETURN);
      dispKey("Enter : 0x" + String(KEY_RETURN, HEX));
      characterOrActionSent = true;
    }
    if (key.tab)
    {
      bleKey.write(KEY_TAB);
      dispKey("Tab : 0x" + String(KEY_TAB, HEX));
      characterOrActionSent = true;
    }

    // 普通の文字
    for (auto i : key.word)
    {
      if (!activeBleMods.isEmpty())
      {
        dispLx(3, activeBleMods);
        activeBleMods = "";
      }
      else
      {
        dispLx(3, "");
      }

      bleKey.write(i);
      dispKey(String(i) + " : 0x" + String(i, HEX));
      characterOrActionSent = true;
    }

    // If a character/action key was sent *and* currentModifiers were active for it,
    // release all BLE modifiers and clear currentModifiers.
    if (characterOrActionSent && currentModifiers)
    {
      bleKey.releaseAll();
      currentModifiers = 0;
    }
    else if (!characterOrActionSent && physicalModifiersThisEvent == 0 && currentModifiers != 0)
    {
      // This case is tricky: no char/action sent, no physical mods NOW, but currentModifiers was set.
      // This implies a modifier was pressed alone, then released alone.
      // The release is handled by keyInput() when isPressed() becomes false.
    }
  }
  else if (physicalModifiersThisEvent == 0 && currentModifiers != 0)
  {
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
    }
    else
    {
      // All keys have been released physically on the Cardputer
      if (currentModifiers)
      {                       // If any modifiers were virtually held by BLE
        bleKey.releaseAll();  // Release them from the BLE host
        currentModifiers = 0; // Clear our record of active BLE modifiers
      }
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
  // L5   Keys-- char/(tab/enter/del)/(up/down/left/right) [YELLOW]
  if (Lx < 1 || Lx > 5)
    return;

  M5Cardputer.Display.fillRect(0, Lx * H_CHR, M5Cardputer.Display.width(), (Lx + 1) * H_CHR, TFT_BLACK);
  M5Cardputer.Display.setCursor(30, Lx * H_CHR);
  M5Cardputer.Display.print(msg);
}

void dispBleInfo(bool connected)
{
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

  disp_init();
  bleKey.begin();
  lastKeyInput = millis();
}

void loop()
{
  M5Cardputer.update();
  notifyConnection();
  keyInput();
  checkBattery();
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

void m5stack_begin()
{
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  cfg.internal_mic = false;
  cfg.output_power = false;
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
