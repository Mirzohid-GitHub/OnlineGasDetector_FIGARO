void PrepareGasAnalyser() {
}

void GasAnalyse() {
  int figaroAnalogValue = analogRead(FigaroAnalogPin);   
  Serial.print("Аналоговое значение figaro:");    
  Serial.println(figaroAnalogValue);    

  float figaroAnalogVoltage = figaroAnalogValue * (5.0 / 1023.0);
  Serial.print("Аналоговое значение figaro:");    
  Serial.println(figaroAnalogValue);    

  float perPPMValue = figaroAnalogVoltage / 1000;
  Serial.print("Значение 1ppm:");    
  Serial.println(perPPMValue);    

  // делим на 1000 потому что figaro считает до 1000ppm
  COConcentration = int(figaroAnalogVoltage / perPPMValue);
  COHasHighConcentration = COConcentration >= CoThreshold;

  if (PlotterMode) {
    Serial.print("CO:");              //FORTEST
    Serial.println(COConcentration);  //FORTEST
  }

  if (!TestAlertIsActive) {
    AlertIsActive = COHasHighConcentration;
    if(!NeedForSendRequest)
      NeedForSendRequest = COHasHighConcentration;
  }
}

