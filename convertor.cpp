#include <convertor.h>
#include <driver/gpio.h>

// У ESP32-C3 только два UART: UART0 (Serial) и UART1 (Serial1).
// Нужны оба, т.к. инверсия задаётся на весь UART целиком (RX и TX вместе).
#define InSerial  Serial1
#define OutSerial Serial

#define DEBOUNCE_MS   30
#define LONG_PRESS_MS 5000

// Кадр CRSF: [sync][len][type][payload...][crc8], len = число байт после len.
#define CRSF_SYNC_OUT   0xC8   // с таким sync кадры уходят на выход
#define CRSF_SYNC_RADIO 0xEE   // с таким sync кадры шлёт пульт
#define CRSF_MAX_FRAME  64     // максимум вместе с sync и len
#define CRSF_CRC_POLY   0xD5

// Бинд = нажатие кнопки Bind в меню Walksnail. Это тот же кадр, что шлёт с пульта
// WSCommandHelper.lua (WSCKeyPress("Bind")), только сразу с выходным sync:
// [sync][len][0x54][dest][origin][action][key][0][0][crc8 кадра 0xD5]
#define CRSF_FRAMETYPE_WS   0x54   // crsfFrameType в скрипте
#define WS_DEST             0xC8   // fcAddress в скрипте
#define WS_ORIGIN           0xEA   // txAddress в скрипте
#define WS_ACTION_PRESS_KEY 0x00   // WS_ACTION.PRESS_KEY
#define WS_KEY_BIND         0x05   // WS_KEYS: Bind

// Кнопка замыкает пин на землю, подтяжка к питанию: отпущена = HIGH, нажата = LOW
static bool buttonStable = HIGH;
static bool buttonLast = HIGH;
static uint32_t buttonChangedAt = 0;
static uint32_t buttonPressedAt = 0;
static bool longPressFired = false;
static bool bindPending = false;

// Сюда набирается кадр со входа. Наружу уходит только целый кадр с верным CRC.
static uint8_t rxBuf[CRSF_MAX_FRAME];
static uint8_t rxPos = 0;

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
    CRSF_SYNC_OUT,
    8,                                  // длина: от типа до crc включительно
    CRSF_FRAMETYPE_WS,
    WS_DEST,
    WS_ORIGIN,
    WS_ACTION_PRESS_KEY,
    WS_KEY_BIND,
    0, 0,                               
    0,                                  // crc8 кадра
  };
  frame[9] = crc8(&frame[2], 7, CRSF_CRC_POLY);  // тип..параметры
  OutSerial.write(frame, sizeof(frame));
}

static void onButtonLongPress() {
  // сам пакет шлём из loop(), между целыми кадрами
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

static inline bool isSync(uint8_t b) {
  return b == CRSF_SYNC_RADIO || b == CRSF_SYNC_OUT;
}

// Выбрасывает n байт из начала буфера
static inline void rxDrop(uint8_t n) {
  rxPos -= n;
  memmove(rxBuf, rxBuf + n, rxPos);
}

// Ищет в буфере целые кадры и отправляет их на выход.
// Границы кадра определяются по содержимому (sync, len, crc), а не по времени:
// байты из UART приходят в loop() пачками, и время чтения ничего не говорит
// о том, когда байт был на линии.
static void parseInput() {
  for (;;) {
    uint8_t skip = 0;                   // мусор до sync выбрасываем
    while (skip < rxPos && !isSync(rxBuf[skip])) skip++;
    if (skip) rxDrop(skip);
    if (rxPos < 2) return;              // ждём байт длины

    uint8_t len = rxBuf[1];
    if (len < 2 || len > CRSF_MAX_FRAME - 2) {  // это был не sync, ищем дальше
      rxDrop(1);
      continue;
    }
    uint8_t total = len + 2;
    if (rxPos < total) return;          // кадр ещё не набрался

    if (crc8(&rxBuf[2], len - 1, CRSF_CRC_POLY) == rxBuf[total - 1]) {
      rxBuf[0] = CRSF_SYNC_OUT;         // sync в CRC не входит, менять можно
      OutSerial.write(rxBuf, total);
      rxDrop(total);
    } else {
      rxDrop(1);                        // ложный sync, ищем дальше
    }
  }
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  InSerial.setRxBufferSize(1024);       // запас, если loop() иногда задерживается
  //              скорость   формат      RX         TX          инверсия
  InSerial.begin(IN_BAUD,   SERIAL_8N1, PIN_IN_RX, -1,         IN_INV);   // вход от пульта
  OutSerial.begin(OUT_BAUD, SERIAL_8N1, -1,        PIN_OUT_TX, OUT_INV);  // выход
  // begin() включает на RX подтяжку вверх. Для инвертированного входа покой = LOW,
  // поэтому между кадрами, когда пульт отпускает линию, её надо тянуть вниз.
 // if (IN_INV) gpio_set_pull_mode((gpio_num_t)PIN_IN_RX, GPIO_PULLDOWN_ONLY);
 gpio_set_pull_mode((gpio_num_t)PIN_IN_RX, GPIO_PULLDOWN_ONLY);  // подтяжка вниз, чтобы на инвертированном входе покой был LOW
}

void loop() {
  while (InSerial.available()) {
    rxBuf[rxPos++] = InSerial.read();
    if (rxPos == CRSF_MAX_FRAME || !InSerial.available()) parseInput();
  }
  if (bindPending) {                    // кадры уходят целиком, так что бинд не влезет в середину
    bindPending = false;
    sendBindPacket();
  }
  handleButton();
}