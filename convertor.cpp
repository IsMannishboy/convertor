#include <convertor.h>

// У ESP32-C3 только два UART: UART0 (Serial) и UART1 (Serial1).
// Нужны оба, т.к. инверсия задаётся на весь UART целиком (RX и TX вместе).
#define InSerial  Serial1
#define OutSerial Serial

#define DEBOUNCE_MS   30
#define LONG_PRESS_MS 5000
#define LINE_IDLE_US  500   // пауза на входе, после которой можно вставить свой пакет
#define FRAME_GAP_US  300   // пауза дольше ~3 байт = начался новый кадр

// CRSF-команда бинда, как её шлёт пульт:
// [sync][len][0x32][dest][origin][0x10][0x01][crc8 команды 0xBA][crc8 кадра 0xD5]
#ifndef CRSF_BIND_SYNC
  #define CRSF_BIND_SYNC   0xC8
#endif
#ifndef CRSF_BIND_DEST
  #define CRSF_BIND_DEST   0xEC   // приёмник
#endif
#ifndef CRSF_BIND_ORIGIN
  #define CRSF_BIND_ORIGIN 0xEA   // пульт
#endif
#define CRSF_FRAMETYPE_COMMAND 0x32
#define CRSF_COMMAND_SUBCMD_RX 0x10
#define CRSF_COMMAND_RX_BIND   0x01

// Кнопка замыкает пин на землю, подтяжка к питанию: отпущена = HIGH, нажата = LOW
static bool buttonStable = HIGH;
static bool buttonLast = HIGH;
static uint32_t buttonChangedAt = 0;
static uint32_t buttonPressedAt = 0;
static bool longPressFired = false;
static bool bindPending = false;
static uint32_t lastRxAt = 0;
uint8_t frame_counter = 0;

static uint8_t crc8(const uint8_t *data, size_t len, uint8_t poly) {
  uint8_t crc = 0;
  while (len--) {
    crc ^= *data++;
    for (int i = 0; i < 8; i++) crc = (crc & 0x80) ? (crc << 1) ^ poly : crc << 1;
  }
  return crc;
}

static void sendBindPacket() {
  uint8_t frame[] = {
    CRSF_BIND_SYNC,
    7,                                  // длина: от типа до crc кадра включительно
    CRSF_FRAMETYPE_COMMAND,
    CRSF_BIND_DEST,
    CRSF_BIND_ORIGIN,
    CRSF_COMMAND_SUBCMD_RX,
    CRSF_COMMAND_RX_BIND,
    0,                                  // crc8 команды
    0,                                  // crc8 кадра
  };
  frame[7] = crc8(&frame[2], 5, 0xBA);  // тип..подкоманда
  frame[8] = crc8(&frame[2], 6, 0xD5);  // тип..crc команды
  OutSerial.write(frame, sizeof(frame));
}

static void onButtonLongPress() {
  // сам пакет шлём из loop(), когда на входе пауза, чтобы не влезть в середину кадра
  bindPending = true;
}

static inline void handleButton() {
  uint32_t now = millis();
  bool reading = digitalRead(PIN_BUTTON);
  if (reading != buttonLast) {          // уровень дёрнулся, перезапускаем таймер
    buttonLast = reading;
    buttonChangedAt = now;
  }
  if (now - buttonChangedAt > DEBOUNCE_MS && reading != buttonStable) {
    buttonStable = reading;             // уровень держится дольше DEBOUNCE_MS
    if (buttonStable == LOW) {
      buttonPressedAt = now;
      longPressFired = false;
    }
  }
  // срабатывает один раз за нажатие, повторно только после отпускания
  if (buttonStable == LOW && !longPressFired && now - buttonPressedAt >= LONG_PRESS_MS) {
    longPressFired = true; 
    onButtonLongPress();
  }
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  InSerial.setRxBufferSize(1024);       // запас, если loop() иногда задерживается
  //              скорость   формат      RX         TX          инверсия
  InSerial.begin(IN_BAUD,   SERIAL_8N1, PIN_IN_RX, -1,         IN_INV);   // вход от пульта
  OutSerial.begin(OUT_BAUD, SERIAL_8N1, -1,        PIN_OUT_TX, OUT_INV);  // выход
}

void loop() {
  while (InSerial.available()) {
    int byte =         InSerial.read();
    if (micros() - lastRxAt > FRAME_GAP_US) frame_counter = 0;   // была пауза = начало нового кадра
    frame_counter++;
    if(frame_counter == 26){
      frame_counter = 0;
    }else if(frame_counter == 1){
          byte = 0xC8;

    }
    OutSerial.write(byte);
    lastRxAt = micros();
    
  }
  if (bindPending && micros() - lastRxAt >= LINE_IDLE_US) {
    bindPending = false;
    sendBindPacket();
  }
  handleButton();
}
