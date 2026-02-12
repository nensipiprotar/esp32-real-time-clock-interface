#include <Wire.h>                  // I2C communication library for RTC
#include <TM1637Display.h>         // Library for 4-digit TM1637 display
#include <stdint.h>                // Fixed width integer types
#include <stdbool.h>               // Boolean type support


/* ================= RTC MODULE ================= */

typedef struct {
  uint8_t sec;     // Seconds (0–59)
  uint8_t min;     // Minutes (0–59)
  uint8_t hour;    // Hours (0–23)
  uint8_t day;     // Day of week (1–7)
  uint8_t date;    // Date (1–31)
  uint8_t month;   // Month (1–12)
  uint16_t year;   // Full year (e.g., 2026)
} RTC_Time_t;      // Structure used to pass time data between modules

#define DS1307_ADDR 0x68   // I2C address of DS1307 RTC chip
#define REG_SEC     0x00   // Register address for seconds (starting point)

static bool is12Hour = false;   // Flag for display formatting (12hr/24hr)


/* ---- BCD Conversion Functions ---- */

static uint8_t decToBcd(uint8_t v) {
  return ((v / 10) << 4) | (v % 10);   // Convert decimal to BCD format
}

static uint8_t bcdToDec(uint8_t v) {
  return ((v >> 4) * 10) + (v & 0x0F); // Convert BCD to decimal
}


/* ---- RTC Initialization ---- */

void RTC_Init(void) {
  Wire.begin();   // Start I2C bus (ESP32 default SDA=21, SCL=22)
}


/* ---- RTC Set Time ---- */

void RTC_SetTime(RTC_Time_t *t) {

  if (!t) return;   // Protect against null pointer

  // Validate input ranges (defensive programming)
  if (t->sec > 59 || t->min > 59 || t->hour > 23 ||
      t->date == 0 || t->date > 31 ||
      t->month == 0 || t->month > 12 ||
      t->day == 0 || t->day > 7)
      return;   // If invalid values, do nothing

  Wire.beginTransmission(DS1307_ADDR);  // Start communication with RTC
  Wire.write(REG_SEC);                  // Point to seconds register

  Wire.write(decToBcd(t->sec) & 0x7F);  // Write seconds & clear CH (clock halt) bit
  Wire.write(decToBcd(t->min));         // Write minutes in BCD
  Wire.write(decToBcd(t->hour));        // Write hours in BCD (24hr mode)
  Wire.write(decToBcd(t->day));         // Write day of week
  Wire.write(decToBcd(t->date));        // Write date
  Wire.write(decToBcd(t->month));       // Write month
  Wire.write(decToBcd(t->year - 2000)); // Store only last two digits of year

  Wire.endTransmission();               // End I2C transmission
}


/* ---- RTC Get Time (Atomic Safe Read) ---- */

void RTC_GetTime(RTC_Time_t *t) {

  if (!t) return;   // Null pointer safety

  uint8_t sec1, sec2;  // Used for atomic read check
  uint8_t rawHour;     // Temporary storage for hour register

  do {

    Wire.beginTransmission(DS1307_ADDR); // Start communication
    Wire.write(REG_SEC);                 // Set register pointer to seconds
    Wire.endTransmission();

    Wire.requestFrom(DS1307_ADDR, 7);    // Request 7 bytes (sec to year)

    sec1 = Wire.read() & 0x7F;           // Read seconds & mask CH bit
    t->min   = bcdToDec(Wire.read());    // Read minutes
    rawHour  = Wire.read();              // Read raw hour register
    t->day   = bcdToDec(Wire.read());    // Read day
    t->date  = bcdToDec(Wire.read());    // Read date
    t->month = bcdToDec(Wire.read());    // Read month
    t->year  = 2000 + bcdToDec(Wire.read()); // Convert year

    // Re-read seconds to ensure stable data
    Wire.beginTransmission(DS1307_ADDR);
    Wire.write(REG_SEC);
    Wire.endTransmission();
    Wire.requestFrom(DS1307_ADDR, 1);
    sec2 = Wire.read() & 0x7F;

  } while (sec1 != sec2);   // Repeat if second changed during read

  t->sec = bcdToDec(sec1);  // Final stable seconds

  rawHour &= 0x3F;          // Mask control bits from hour register
  uint8_t hr = bcdToDec(rawHour);  // Convert hour to decimal

  t->hour = is12Hour ? ((hr > 12) ? hr - 12 : hr) : hr;  // Apply formatting
}


/* ---- 12 Hour Mode Control ---- */

void RTC_Set12Hour(bool enable) {
  is12Hour = enable;   // Only affects display formatting
}


/* ================= DISPLAY MODULE ================= */

#define DISP_CLK 19   // TM1637 clock pin
#define DISP_DIO 18   // TM1637 data pin

TM1637Display disp(DISP_CLK, DISP_DIO);  // Create display object
static const uint8_t SEG_SMALL_D = 0b01011110; // Custom pattern for small 'd'


void DISP_Init(void) {
  disp.setBrightness(0x0f);  // Set maximum brightness (0–15)
  disp.clear();              // Clear display at startup
}

void DISP_Clear(void) {
  disp.clear();              // Clear display
}


/* ---- Show Time ---- */

void DISP_ShowTime(uint8_t hh, uint8_t mm) {

  uint8_t digits[4];  // Buffer for 4 display digits

  digits[0] = disp.encodeDigit(hh / 10);  // Tens of hour
  digits[1] = disp.encodeDigit(hh % 10);  // Units of hour
  digits[2] = disp.encodeDigit(mm / 10);  // Tens of minute
  digits[3] = disp.encodeDigit(mm % 10);  // Units of minute

  digits[1] |= 0x80;  // Enable colon between HH and MM

  disp.setSegments(digits);  // Update display
}


/* ---- Show Day (d1–d7) ---- */

void DISP_ShowDay(uint8_t day) {

  if (day < 1 || day > 7) {  // Validate day range
    disp.clear();            // Clear display if invalid
    return;
  }

  uint8_t data[4] = {
    0x00,                     // Blank
    SEG_SMALL_D,              // 'd'
    disp.encodeDigit(day),    // Day number
    0x00                      // Blank
  };

  disp.setSegments(data);     // Show formatted day
}


/* ================= MAIN ================= */

RTC_Time_t now;   // Global time structure

void setup() {
  RTC_Init();     // Initialize RTC
  DISP_Init();    // Initialize display
}

void loop() {

  static uint8_t mode = 0;            // Mode variable (state machine)
  static unsigned long last = 0;      // Timestamp for switching

  RTC_GetTime(&now);                  // Read current time

  if (millis() - last > 1500) {       // 1.5 second interval
    last = millis();                  // Update timestamp
    mode = (mode + 1) % 3;            // Cycle modes (0→1→2→0)
  }

  switch (mode) {                     // State machine control

    case 0:
      DISP_ShowTime(now.hour, now.min);   // Show HH:MM
      break;

    case 1:
      DISP_ShowTime(now.date, now.month); // Show DD:MM
      break;

    case 2:
      DISP_ShowDay(now.day);              // Show day (d1–d7)
      break;
  }
}
