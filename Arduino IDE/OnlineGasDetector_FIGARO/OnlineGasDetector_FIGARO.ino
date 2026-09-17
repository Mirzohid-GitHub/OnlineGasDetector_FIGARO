#include <avr/wdt.h>
#include <SoftwareSerial.h>

#pragma region GsmModule

#define MODEM_RX 11
#define MODEM_TX 10
#define MODEM_POWER_PIN  12
#define MODEM_BAUD 19200

unsigned long SendRequestLastTickTime = 0;
const unsigned long SendRequestWorkInterval = 2000;

unsigned long PingLastTickTime = 300000;
const unsigned long PingWorkInterval = 300000UL; // 5 минут

volatile bool NeedForSendRequest = false;
bool GsmPowerIsOn = false;

#pragma endregion

#pragma region Notificator

#define TestButtonPin 3
#define MuteButtonPin 2
#define BuzzerPin 13
#define AliveLedPin 7
#define AlertLedPin 5

unsigned long AliveIndicatorLastTickTime = 0;
const unsigned long AliveIndicatorWorkInterval = 5000;

unsigned long WatchDogResetLastTickTime = 0;
const unsigned long WatchDogResetWorkInterval = 4000;

unsigned long RebootLastTickTime = 0;

int SignalSwitchWorkInterval = 200;

const unsigned long ResetInterval = 3600000UL;

volatile bool Mute = false;      // Нажата кнопка молчания
volatile bool AlertIsActive = false;      // Используется также из ISR таймера
volatile bool TestAlertIsActive = false;  // Используется также из ISR таймера
bool ManualBuzzerIsOn = false;
bool ManualAlertLedIsOn = false;
bool ManualAliveLedIsOn = false;

#pragma endregion

#pragma region GasAnalyser

#define FigaroAnalogPin A6 // Аналоговый пин Figaro

// Thresholds
int LedTreshold = 300;
int BuzzerThreshold = 300;
int SendRequestThreshold = 400;
int HysteresisOffset = 50;

bool BuzzerAlertIsActive = false;       // Гистерезис зуммера
bool SendRequestAlertIsActive = false;  // Гистерезис отправки запроса

const int AlertDebounceThreshold = 5;
int LedDebounceCount         = 0;
int BuzzerDebounceCount      = 0;
int SendRequestDebounceCount = 0;

unsigned long GasAnalyseLastTickTime = 0;
const unsigned long GasAnalyseWorkInterval = 0;

bool PlotterMode = true;              // Выводим значения в гравик //FORTEST
volatile bool Preparing = true;       // Используется также из ISR таймера

float COConcentration = 0;  // Концентрация Угарного газа
#pragma endregion

const char deviceId[] = "123";
SoftwareSerial SIM800L(MODEM_RX, MODEM_TX);

//Програмная перезагрузка контроллера
void RebootController() {
  Serial.flush();
  wdt_enable(WDTO_15MS);
  while (true) { }
}

void setup() {
  Preparing = true;
  //Настройка режимов для пинов
  pinMode(BuzzerPin, OUTPUT);
  pinMode(AlertLedPin, OUTPUT);
  pinMode(AliveLedPin, OUTPUT);
  pinMode(MuteButtonPin, INPUT_PULLUP);
  pinMode(TestButtonPin, INPUT_PULLUP);
  // Сначала фиксируем LOW в выходном регистре, затем включаем режим OUTPUT.
  // Так при старте не возникает случайного импульса включения модема.
  digitalWrite(MODEM_POWER_PIN, LOW);
  pinMode(MODEM_POWER_PIN, OUTPUT);
  GsmPowerIsOn = false;
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

  Serial.println(F("Start"));
}

void loop() {
  ProcessSerialCommands();

  //Снимаем показания датчика
  if (tick(GasAnalyseLastTickTime, GasAnalyseWorkInterval)) {
    GasAnalyse();
  }
  //Отправка запроса
  if (tick(SendRequestLastTickTime, SendRequestWorkInterval)) {
    SendRequest(TestAlertIsActive);
  }
  // Пинг сервера каждую минуту (только вне режима тревоги)
  if (tick(PingLastTickTime, PingWorkInterval)) {
    //SendPing();
  }
  //Маргаем индикатором рабочего режима
  if (tick(AliveIndicatorLastTickTime, AliveIndicatorWorkInterval) && !AlertIsActive && !ManualAliveLedIsOn) {
      digitalWrite(AliveLedPin, HIGH);
      delay(50);
      digitalWrite(AliveLedPin, LOW);
  }
  //Выключаем режим Mute, если Alert выключен
  if (!AlertIsActive && !TestAlertIsActive && !NeedForSendRequest) {
    Mute = false;
  }
  // Сброс сторожевого таймера
  if (tick(WatchDogResetLastTickTime, WatchDogResetWorkInterval)) {
    wdt_reset();
  }

  // // Перезагрузка каждый час (tick корректно обрабатывает переполнение millis)
  // if (tick(RebootLastTickTime, ResetInterval) && !AlertIsActive && !TestAlertIsActive) {
  //   Serial.println(F("REBOOT: hourly timer"));
  //   RestartGsmModule();
  //   RebootController();
  // }
}
