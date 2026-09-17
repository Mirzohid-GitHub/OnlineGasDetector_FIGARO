char serialCommandBuffer[121];
uint8_t serialCommandLength = 0;
unsigned long SerialCommandLastCharTime = 0;

void ProcessSerialCommands() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    SerialCommandLastCharTime = millis();
    if (c == '\r')
      continue;

    if (c == '\n') {
      serialCommandBuffer[serialCommandLength] = '\0';
      HandleSerialCommand(serialCommandBuffer);
      serialCommandLength = 0;
    } else if (serialCommandLength < sizeof(serialCommandBuffer) - 1) {
      serialCommandBuffer[serialCommandLength++] = c;
    } else {
      serialCommandLength = 0;
    }
  }

  if (serialCommandLength > 0 && millis() - SerialCommandLastCharTime > 100) {
    serialCommandBuffer[serialCommandLength] = '\0';
    HandleSerialCommand(serialCommandBuffer);
    serialCommandLength = 0;
  }
}

void HandleSerialCommand(char *command) {
  while (*command == ' ' || *command == '\t')
    command++;

  char *end = command + strlen(command);
  while (end > command && (end[-1] == ' ' || end[-1] == '\t'))
    *--end = '\0';
  if (*command == '\0')
    return;

  if (toupper(command[0]) == 'A' && toupper(command[1]) == 'T') {
    if (!GsmPowerIsOn) {
      Serial.println(F("Сначала включите GSM"));
      return;
    }
    sendAtCommand(command, true);
    return;
  }

  if (strcasecmp_P(command, PSTR("RESTART")) == 0) {
    Serial.println(F("Restart"));
    delay(100);
    RebootController();
  } else if (strcasecmp_P(command, PSTR("MUTE")) == 0) {
    Mute = true;
    Serial.println(F("Mute"));
  } else if (strcasecmp_P(command, PSTR("GSMON")) == 0) {
    ActivateGsmModulePower();
    Serial.println(F("GSM on"));
  } else if (strcasecmp_P(command, PSTR("GSMOFF")) == 0) {
    DeactivateGsmModulePower();
    Serial.println(F("GSM off"));
  } else if (strcasecmp_P(command, PSTR("ALERTLEDSWITCH")) == 0) {
    ManualAlertLedIsOn = !ManualAlertLedIsOn;
    digitalWrite(AlertLedPin, ManualAlertLedIsOn ? HIGH : LOW);
    Serial.println(ManualAlertLedIsOn ? F("Alert LED on") : F("Alert LED off"));
  } else if (strcasecmp_P(command, PSTR("BUZZERSWITCH")) == 0) {
    ManualBuzzerIsOn = !ManualBuzzerIsOn;
    digitalWrite(BuzzerPin, ManualBuzzerIsOn ? HIGH : LOW);
    Serial.println(ManualBuzzerIsOn ? F("Buzzer on") : F("Buzzer off"));
  } else if (strcasecmp_P(command, PSTR("ALIVELEDSWITCH")) == 0) {
    ManualAliveLedIsOn = !ManualAliveLedIsOn;
    digitalWrite(AliveLedPin, ManualAliveLedIsOn ? HIGH : LOW);
    Serial.println(ManualAliveLedIsOn ? F("Alive LED on") : F("Alive LED off"));
  } else {
    Serial.println(F("Unknown command"));
  }
}
