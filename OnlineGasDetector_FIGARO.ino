#include <avr/wdt.h>
#include <SoftwareSerial.h>

#pragma region GsmModule

#define MODEM_RX 10
#define MODEM_TX 11
#define MODEM_POWER_PIN  12
#define MODEM_BAUD 19200

unsigned long SendRequestLastTickTime = 0;
const unsigned long SendRequestWorkInterval = 2000;

volatile bool NeedForSendRequest = false;

#pragma endregion

#pragma region Notificator

#define TestButtonPin 3
#define MuteButtonPin 2
#define BuzzerPin 13
#define AliveLedPin 5
#define AlertLedPin 4

unsigned long AliveIndicatorLastTickTime = 0;
const unsigned long AliveIndicatorWorkInterval = 5000;

unsigned long WatchDogResetLastTickTime = 0;
const unsigned long WatchDogResetWorkInterval = 4000;

int SignalSwitchWorkInterval = 200;

const unsigned long ResetInterval = 3600000UL;

volatile bool Mute = false;      // Нажата кнопка молчания
bool AlertIsActive = false;      // Режим тревоги
bool TestAlertIsActive = false;  // Режим теста

#pragma endregion

#pragma region GasAnalyser

#define FigaroAnalogPin A2 // Аналоговый пин Figaro

// Thresholds
int LedTreshold = 200;
int BuzzerThreshold = 300;
int SendRequestThreshold = 400;
int HysteresisOffset = 50;

bool BuzzerAlertIsActive = false;       // Гистерезис зуммера
bool SendRequestAlertIsActive = false;  // Гистерезис отправки запроса

unsigned long GasAnalyseLastTickTime = 0;
const unsigned long GasAnalyseWorkInterval = 0;

bool PlotterMode = true;              // Выводим значения в гравик //FORTEST
bool Preparing = true;                // Выводим значения в гравик //FORTEST

float COConcentration = 0;  // Концентрация Угарного газа
#pragma endregion

String deviceId = "123";
SoftwareSerial SIM800L(MODEM_RX, MODEM_TX);

//Програмная перезагрузка контроллера
void (*Reboot)(void) = 0;

void setup() {
  Preparing = true;
  //Настройка режимов для пинов
  pinMode(BuzzerPin, OUTPUT);
  pinMode(AlertLedPin, OUTPUT);
  pinMode(AliveLedPin, OUTPUT);
  pinMode(MuteButtonPin, INPUT_PULLUP);
  pinMode(TestButtonPin, INPUT_PULLUP);
  pinMode(MODEM_POWER_PIN, OUTPUT);
  pinMode(FigaroAnalogPin, INPUT);

  digitalWrite(BuzzerPin, LOW);
  
  //Настройка аппаратного прерывания для кнопок
  attachInterrupt(digitalPinToInterrupt(MuteButtonPin), MuteButtonPressEvent, FALLING);
  attachInterrupt(digitalPinToInterrupt(TestButtonPin), TestButtonPressEvent, FALLING);

  //Задаем режим опорного напряжения
  analogReference(EXTERNAL);

  //Подготовка и подключение Serial-порта
  PrepareSerialPorts(); 

  // вывести из-за чего перезагрузилась arduino
  PrintResetCause();

  //Подготовка и калибровка датчика
  PrepareGasAnalyser();

  //Включение прерывания по таймеру
  EnableInterruptTimer();

  //Включение Watchdog на 8 секунд
  wdt_enable(WDTO_8S);
  Preparing = false;

  Serial.println("Start");
}

void loop() {
  //Снимаем показания датчика
  if (tick(GasAnalyseLastTickTime, GasAnalyseWorkInterval)) {
    GasAnalyse();
  }
  //Отправка запроса
  if (tick(SendRequestLastTickTime, SendRequestWorkInterval)) {
    SendRequest(TestAlertIsActive);
  }
  //Маргаем индикатором рабочего режима
  if (tick(AliveIndicatorLastTickTime, AliveIndicatorWorkInterval) && !AlertIsActive) {
      digitalWrite(AliveLedPin, HIGH);
      delay(50);
      digitalWrite(AliveLedPin, LOW);
  }
  //Выключаем режим Mute, если Alert выключен
  if (!AlertIsActive) {
    Mute = false;
  }
  // Сброс сторожевого таймера
  if (tick(WatchDogResetLastTickTime, WatchDogResetWorkInterval)) {
    wdt_reset();
  }
  // Перезагрузка каждый час
  if (millis() > ResetInterval && !AlertIsActive && !TestAlertIsActive) {
    RestartGsmModule();
    Reboot();
  }
}
