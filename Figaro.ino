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

  COConcentration = figaroAnalogValue;

  if (PlotterMode) {
    Serial.print("CO:");
    Serial.println(figaroAnalogValue);
  }

  if (!TestAlertIsActive) {
    // Гистерезис LED: ON >= 200, OFF < 150
    if (!AlertIsActive) {
      if (figaroAnalogValue >= LedTreshold)
        AlertIsActive = true;
    } else {
      if (figaroAnalogValue < LedTreshold - HysteresisOffset)
        AlertIsActive = false;
    }

    // Гистерезис зуммера: ON >= 300, OFF < 250
    if (!BuzzerAlertIsActive) {
      if (figaroAnalogValue >= BuzzerThreshold)
        BuzzerAlertIsActive = true;
    } else {
      if (figaroAnalogValue < BuzzerThreshold - HysteresisOffset)
        BuzzerAlertIsActive = false;
    }

    // Гистерезис отправки: ON >= 400, OFF < 350
    // if (!SendRequestAlertIsActive) {
    //   if (figaroAnalogValue >= SendRequestThreshold) {
    //     SendRequestAlertIsActive = true;
    //     NeedForSendRequest = true;
    //   }
    // } else {
    //   if (figaroAnalogValue < SendRequestThreshold - HysteresisOffset)
    //     SendRequestAlertIsActive = false;
    // }
  }
}
