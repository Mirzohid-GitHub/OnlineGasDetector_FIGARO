volatile int SosSignalStep = 0;
const int SosMinInterval = 100;

void GenerateSosSignal() {
  // Генерация сигнала S.O.S.
  if (Preparing && !TestAlertIsActive) 
    return;
  // NeedForSendRequest удерживает тревогу до полной отправки HTTP и SMS.
  if (AlertIsActive || TestAlertIsActive || NeedForSendRequest) {
    if (SosSignalStep > 16) {
      SignalSwitchWorkInterval = SosMinInterval * 10;
      SetBuzzerState(0);
      SosSignalStep = 0;
    } else if (SosSignalStep == 5 || SosSignalStep == 7 || SosSignalStep == 9 || SosSignalStep == 11) {
      SetBuzzerState(0);
      SignalSwitchWorkInterval = SosMinInterval * 2;
      SosSignalStep++;
    } else if (SosSignalStep == 6 || SosSignalStep == 8 || SosSignalStep == 10) {
      SetBuzzerState(1);
      SignalSwitchWorkInterval = SosMinInterval * 4;
      SosSignalStep++;
    } else if (SosSignalStep % 2 != 0) {
      SetBuzzerState(0);
      SignalSwitchWorkInterval = SosMinInterval;
      SosSignalStep++;
    } else {
      SetBuzzerState(1);
      SignalSwitchWorkInterval = SosMinInterval;
      SosSignalStep++;
    }
  } else {
    SetBuzzerState(0);
    SosSignalStep = 0;
  }
  // Рассчитываем следующий OCR1A на основе SignalSwitchWorkInterval (в мс)
  uint32_t ticks = ((F_CPU / 1024UL / 1000UL) * (uint32_t)SignalSwitchWorkInterval) - 1;
  if (ticks > 65535UL) ticks = 65535UL;  // Ограничение для 16-бит таймера
  next_ocr = (uint16_t)ticks;
}

void SetBuzzerState(bool activate) {
  bool buzzerIsOn = !Mute && (BuzzerAlertIsActive || TestAlertIsActive || NeedForSendRequest) && activate;
  bool alertLedIsOn = (AlertIsActive || TestAlertIsActive || NeedForSendRequest) && activate;
  if (ManualBuzzerIsOn || buzzerIsOn)
    digitalWrite(BuzzerPin, HIGH);
  else
    digitalWrite(BuzzerPin, LOW);
  if (ManualAlertLedIsOn || alertLedIsOn)
    digitalWrite(AlertLedPin, HIGH);
  else
    digitalWrite(AlertLedPin, LOW);
}
