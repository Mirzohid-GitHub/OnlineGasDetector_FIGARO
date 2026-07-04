const float alpha = 0.5f;
float filteredValue = 0.0f;

void PrepareGasAnalyser() {
}

void GasAnalyse() {
  float figaroAnalogValue = 0.0;

  for (int i = 0; i < 50; i++) {
    figaroAnalogValue += analogRead(FigaroAnalogPin);
    delay(10);
  }
  figaroAnalogValue = figaroAnalogValue / 50.0;
  figaroAnalogValue = (figaroAnalogValue / 1024.0) * 1000.0;
  //figaroAnalogValue = (figaroAnalogValue / 750.0) * 1000.0;

  filteredValue = (alpha * figaroAnalogValue) + ((1.0f - alpha) * filteredValue);

  COConcentration = filteredValue;

  if (PlotterMode && (!AlertIsActive || !TestAlertIsActive)) {
    // Serial.print("CO:");
    Serial.println(filteredValue);
  }

  if (!TestAlertIsActive) {
    if (!BuzzerAlertIsActive) {
      if (filteredValue >= BuzzerThreshold) {
        BuzzerDebounceCount++;
        if (BuzzerDebounceCount >= AlertDebounceThreshold)
          BuzzerAlertIsActive = true;
      } else {
        BuzzerDebounceCount = 0;
      }
    } else {
      if (filteredValue < BuzzerThreshold - HysteresisOffset)
        BuzzerAlertIsActive = false;
    }
    AlertIsActive = BuzzerAlertIsActive;

    // Request alert hysteresis: ON >= 400, OFF < 350
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
