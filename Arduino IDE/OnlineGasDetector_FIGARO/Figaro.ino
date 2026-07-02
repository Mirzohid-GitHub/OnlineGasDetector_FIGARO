const float alpha = 0.5f;
float filteredValue = 0.0f;

void PrepareGasAnalyser() {
}

void GasAnalyse() {
  float figaroAnalogValue = 0.0;  
  // Первый этап сглаживания
  for (int i = 0; i < 50; i++) {
    figaroAnalogValue += analogRead(FigaroAnalogPin);
    delay(10);
  }
  figaroAnalogValue = figaroAnalogValue / 50.0;
  figaroAnalogValue = (figaroAnalogValue / 1024.0) * 1000.0;
  //figaroAnalogValue = (figaroAnalogValue / 750.0) * 1000.0; 

  //Второй этап сглаживания  
  filteredValue = (alpha * figaroAnalogValue) + ((1.0f - alpha) * filteredValue);
  
  COConcentration = filteredValue;

  if (PlotterMode) {
    // Serial.print("CO:");
    Serial.println(filteredValue);
  }

  if (!TestAlertIsActive) {
    // Гистерезис LED: ON >= 200 (5 замеров подряд), OFF < 150
    if (!AlertIsActive) {
      if (filteredValue >= LedTreshold) {
        LedDebounceCount++;
        if (LedDebounceCount >= AlertDebounceThreshold)
          AlertIsActive = true;
      } else {
        LedDebounceCount = 0;
      }
    } else {
      if (filteredValue < LedTreshold - HysteresisOffset)
        AlertIsActive = false;
    }

    // Гистерезис зуммера: ON >= 300 (5 замеров подряд), OFF < 250
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
