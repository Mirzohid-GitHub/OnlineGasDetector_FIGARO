volatile uint16_t next_ocr = 1561;  // Начальный для 100 мс: ((16e6 / 1024 / 1000) * 100) - 1 ≈ 1561

bool tick(unsigned long &lastTickTime, unsigned long millisInterval) {
  if (millis() < lastTickTime) {
    if (millis() + (4294967295UL - lastTickTime) > millisInterval) { /* 4294967295 - Максимальное значение millis(), после которого оно сбрасывается к нулю*/
      lastTickTime = millis();
      return true;
    }
  }
  if (millis() - lastTickTime > millisInterval) {
    lastTickTime = millis();
    return true;
  }
  return false;
}

void yield() {
  wdt_reset();
}

void PrepareSerialPorts() {
  Serial.begin(9600);         //FORTEST
  SIM800L.begin(MODEM_BAUD);  //FORTEST
}

ISR(TIMER1_COMPA_vect) { 
  GenerateSosSignal();
  OCR1A = next_ocr;  // Обновляем интервал для следующего прерывания
}

void EnableInterruptTimer() {
  cli();  // Отключаем глобальные прерывания
  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;
  OCR1A = 1561;                         // Начальный для 100 мс
  TCCR1B |= (1 << WGM12);               // Режим CTC
  TCCR1B |= (1 << CS12) | (1 << CS10);  // Предделитель 1024 (CS12=1, CS11=0, CS10=1)
  TIMSK1 |= (1 << OCIE1A);              // Разрешаем прерывание по совпадению OCR1A
  sei();                                // Включаем глобальные прерывания
}

void MuteButtonPressEvent() {
  if (AlertIsActive) {
    Mute = true;
  }
}

void TestButtonPressEvent() {
  TestAlertIsActive = true;
  AlertIsActive = true;
  NeedForSendRequest = true;
  LedDebounceCount = 0;
  BuzzerDebounceCount = 0;
  SendRequestDebounceCount = 0;
}

uint8_t ResetCause __attribute__((section(".noinit")));

// Выполняется до setup(): watchdog после reset нужно выключить как можно раньше.
void CaptureResetCause() __attribute__((naked, used, externally_visible, section(".init3")));
void CaptureResetCause() {
  ResetCause = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

void PrintResetCause() {
  uint8_t mcusr = ResetCause;

  while (!Serial); // если нужно подождать

  Serial.print(F("Причина перезагрузки (MCUSR = 0b"));
  Serial.print(mcusr, BIN);
  Serial.println(F("):"));

  if (mcusr & (1 << PORF))  Serial.println(F("  - Power-on reset (включение питания)"));
  if (mcusr & (1 << EXTRF)) Serial.println(F("  - External reset (кнопка Reset или пин RESET)"));
  if (mcusr & (1 << BORF))  Serial.println(F("  - Brown-out reset (падение напряжения)"));
  if (mcusr & (1 << WDRF))  Serial.println(F("  - Watchdog reset (сработал сторожевой таймер)"));

  if (mcusr == 0) Serial.println(F("  - Причина не сохранена загрузчиком"));
  Serial.println();
}
