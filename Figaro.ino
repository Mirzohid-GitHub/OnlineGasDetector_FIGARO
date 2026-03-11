void PrepareGasAnalyser() {
}

void GasAnalyse() {
  float figaroAnalogValue = 0.0;
  for (int i = 0; i < 100; i++) {
    figaroAnalogValue += analogRead(FigaroAnalogPin);
    delay(10);
  }
  figaroAnalogValue = figaroAnalogValue / 100.0;
  figaroAnalogValue = (figaroAnalogValue / 1024) * 1000;

  if (figaroAnalogValue < 20.0) {
    figaroAnalogValue = 0.0;
  }

  COConcentration = figaroAnalogValue;

  if (PlotterMode) {
    // Serial.print("CO:");
    Serial.println(figaroAnalogValue);
  }

  if (!TestAlertIsActive) {
    // Гистерезис LED: ON >= 200 (5 замеров подряд), OFF < 150
    if (!AlertIsActive) {
      if (figaroAnalogValue >= LedTreshold) {
        LedDebounceCount++;
        if (LedDebounceCount >= AlertDebounceThreshold)
          AlertIsActive = true;
      } else {
        LedDebounceCount = 0;
      }
    } else {
      if (figaroAnalogValue < LedTreshold - HysteresisOffset)
        AlertIsActive = false;
    }

    // Гистерезис зуммера: ON >= 300 (5 замеров подряд), OFF < 250
    if (!BuzzerAlertIsActive) {
      if (figaroAnalogValue >= BuzzerThreshold) {
        BuzzerDebounceCount++;
        if (BuzzerDebounceCount >= AlertDebounceThreshold)
          BuzzerAlertIsActive = true;
      } else {
        BuzzerDebounceCount = 0;
      }
    } else {
      if (figaroAnalogValue < BuzzerThreshold - HysteresisOffset)
        BuzzerAlertIsActive = false;
    }

    // Гистерезис отправки: ON >= 400 (5 замеров подряд), OFF < 350
    // if (!SendRequestAlertIsActive) {
    //   if (figaroAnalogValue >= SendRequestThreshold) {
    //     SendRequestDebounceCount++;
    //     if (SendRequestDebounceCount >= AlertDebounceThreshold) {
    //       SendRequestAlertIsActive = true;
    //       NeedForSendRequest = true;
    //     }
    //   } else {
    //     SendRequestDebounceCount = 0;
    //   }
    // } else {
    //   if (figaroAnalogValue < SendRequestThreshold - HysteresisOffset)
    //     SendRequestAlertIsActive = false;
    // }
  }
}
