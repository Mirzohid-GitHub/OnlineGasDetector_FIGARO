String serialCommandBuffer = "";
unsigned long SerialCommandLastCharTime = 0;

void ProcessSerialCommands() {
  while (Serial.available()) {
    char c = Serial.read();
    SerialCommandLastCharTime = millis();
    if (c == '\r')
      continue;

    if (c == '\n') {
      HandleSerialCommand(serialCommandBuffer);
      serialCommandBuffer = "";
    } else {
      serialCommandBuffer += c;
      if (serialCommandBuffer.length() > 120)
        serialCommandBuffer = "";
    }
  }

  if (serialCommandBuffer.length() > 0 && millis() - SerialCommandLastCharTime > 100) {
    HandleSerialCommand(serialCommandBuffer);
    serialCommandBuffer = "";
  }
}

void HandleSerialCommand(String command) {
  command.trim();
  if (command.length() == 0)
    return;

  String upperCommand = command;
  upperCommand.toUpperCase();

  if (upperCommand.startsWith("AT")) {
    if (!GsmPowerIsOn) {
      Serial.println(F("Сначала включи GSM"));
      return;
    }
    sendAtCommand(command, true);
    return;
  }

  if (upperCommand == "RESTART") {
    Serial.println(F("Restart"));
    delay(100);
    Reboot();
  } else if (upperCommand == "MUTE") {
    Mute = true;
    Serial.println(F("Mute"));
  } else if (upperCommand == "GSMON") {
    ActivateGsmModulePower();
    Serial.println(F("GSM on"));
  } else if (upperCommand == "GSMOFF") {
    DeactivateGsmModulePower();
    Serial.println(F("GSM off"));
  } else if (upperCommand == "ALERTLEDSWITCH") {
    ManualAlertLedIsOn = !ManualAlertLedIsOn;
    digitalWrite(AlertLedPin, ManualAlertLedIsOn ? HIGH : LOW);
    Serial.println(ManualAlertLedIsOn ? F("Alert LED on") : F("Alert LED off"));
  } else if (upperCommand == "BUZZERSWITCH") {
    ManualBuzzerIsOn = !ManualBuzzerIsOn;
    digitalWrite(BuzzerPin, ManualBuzzerIsOn ? HIGH : LOW);
    Serial.println(ManualBuzzerIsOn ? F("Buzzer on") : F("Buzzer off"));
  } else if (upperCommand == "ALIVELEDSWITCH") {
    ManualAliveLedIsOn = !ManualAliveLedIsOn;
    digitalWrite(AliveLedPin, ManualAliveLedIsOn ? HIGH : LOW);
    Serial.println(ManualAliveLedIsOn ? F("Alive LED on") : F("Alive LED off"));
  } else {
    Serial.println(F("Unknown command"));
  }
}
