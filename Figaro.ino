void PrepareGasAnalyser() {
}

void GasAnalyse() {
  int figaroAnalogValue = 0;
  for (int i = 0; i < 5; i++) {
    figaroAnalogValue += analogRead(FigaroAnalogPin);
    delay(100);
  }
  figaroAnalogValue = figaroAnalogValue / 5.0;
  float COConcentration = (figaroAnalogValue * (3.8 / 1023.0) * 1000) / 3.8;

  if (COConcentration > 50) {
    COConcentration += 300;
  }

  // Проверяем превышение порога
  COHasHighConcentration = COConcentration >= CoThreshold;

  if (PlotterMode) {
    Serial.print("CO:");
    if (COConcentration < 350) {
      Serial.println("<350");
    } 
    else if (COConcentration > 1000) {
      Serial.println(">1000");
    }
    else {
      Serial.println(COConcentration);
    }
  }

  if (!TestAlertIsActive) {
    AlertIsActive = COHasHighConcentration;
    // if (!NeedForSendRequest)
      // NeedForSendRequest = COHasHighConcentration;
  }
}