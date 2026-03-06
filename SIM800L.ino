// Network settings
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const char fixedAlertPhone[] = "+998990074787";
const String server = "45.138.159.216";
const int port = 8080;

void SendRequest(bool isTestMode) {
  if (NeedForSendRequest || isTestMode) {
    ActivateGsmModulePower();
    delay(2000);
    bool isHaveSignal = false;
    for (int i = 0; i <= 2; i++) {
      if (GetSignalLevel() > 0) {
        isHaveSignal = true;
        break;
      }
      delay(1000);
    }
    if (!isHaveSignal) {
      return;
    }

    // 2.1: Отправляем SMS фиксированному номеру
    SendSMS(isTestMode, String(fixedAlertPhone));
    delay(2000);

    // 2.2: Отправляем HTTP запрос на сервер, получаем "1,тел1,тел2,..."
    String ServerResponse = SendRequestToServer(isTestMode);
    Serial.println(ServerResponse);
    if (ServerResponse == "") {
      Serial.println(F("Failed to send http request"));
      RestartGsmModule();
    } else {
      String body = ExtractResponseBody(ServerResponse);
      if (body.length() > 0 && body.charAt(0) == '1') {
        Serial.println(F("SUCCESS"));
        NeedForSendRequest = false;
        if (TestAlertIsActive) {
          TestAlertIsActive = false;
        }
        // 2.3: Отправляем SMS всем номерам из ответа сервера
        int commaIdx = body.indexOf(',');
        if (commaIdx != -1) {
          String phones = body.substring(commaIdx + 1);
          SendSMSToOwners(isTestMode, phones);
        }
        // Всё успешно — выключаем GSM модуль
        DeactivateGsmModulePower();
      }
    }
  }
}


// Отправляет SMS каждому номеру из строки phones (через запятую).
// Каждый номер повторяется до успешной отправки.
void SendSMSToOwners(bool isTestMode, String phones) {
  int start = 0;
  while (start < (int)phones.length()) {
    int commaIdx = phones.indexOf(',', start);
    String phone;
    if (commaIdx == -1) {
      phone = phones.substring(start);
      start = phones.length();
    } else {
      phone = phones.substring(start, commaIdx);
      start = commaIdx + 1;
    }
    phone.trim();
    if (phone.length() > 0) {
      bool sent = false;
      while (!sent) {
        sent = SendSMS(isTestMode, phone);
        if (sent) {
          Serial.print(F("SMS sent to "));
          Serial.println(phone);
        } else {
          Serial.print(F("Retry SMS to "));
          Serial.println(phone);
          delay(2000);
        }
      }
      delay(2000);
    }
  }
}

bool SendSMS(bool isTestMode, String phoneNumber) {
  // Устанавливаем текстовый режим SMS
  String response = sendAtCommand(F("AT+CMGF=1"), true);
  if (response.indexOf(F("OK")) == -1) {
    Serial.println(F("Failed to set SMS text mode"));
    return false;
  }

  delay(500);

  sendAtCommand(F("AT+CSCS=\"GSM\""), true);
  delay(500);

  // Указываем номер получателя
  String atCommand = "AT+CMGS=\"" + phoneNumber + "\"";
  SIM800L.println(atCommand);
  Serial.println(atCommand);
  delay(1000);

  // Формируем текст сообщения
  String smsText = "";
  if (isTestMode) {
    smsText = "Test alert. CO level: " + String((int)COConcentration) + " ppm";
  } else {
    smsText = "ALARM! CO detected: " + String((int)COConcentration) + " ppm";
  }

  SIM800L.print(smsText);
  delay(500);

  // Ctrl+Z — завершение SMS
  SIM800L.write(0x1A);
  Serial.println(F("SMS text sent, waiting for confirmation..."));

  delay(5000);
  response = waitAtAnswer();

  if (response.indexOf(F("+CMGS:")) >= 0 || response.indexOf(F("OK")) >= 0) {
    return true;
  }

  return false;
}

void ActivateGsmModulePower() {
  digitalWrite(MODEM_POWER_PIN, HIGH);
}

void DeactivateGsmModulePower() {
  digitalWrite(MODEM_POWER_PIN, LOW);
}

void RestartGsmModule() {
  digitalWrite(MODEM_POWER_PIN, LOW);
  delay(1000);
  digitalWrite(MODEM_POWER_PIN, HIGH);
}

// Извлекает тело ответа из строки вида "+HTTPREAD: N\r\n<body>\r\nOK"
String ExtractResponseBody(String data) {
  int httpreadIdx = data.indexOf(F("+HTTPREAD:"));
  if (httpreadIdx == -1) return "";
  int bodyStart = data.indexOf('\n', httpreadIdx);
  if (bodyStart == -1) return "";
  bodyStart++; // пропускаем '\n'
  int bodyEnd = data.indexOf(F("\r\nOK"), bodyStart);
  if (bodyEnd == -1) bodyEnd = data.indexOf(F("\nOK"), bodyStart);
  if (bodyEnd == -1) bodyEnd = data.length();
  String body = data.substring(bodyStart, bodyEnd);
  body.trim();
  return body;
}

String GetRequestParams(bool isTestMode) {
  String result = "/msg/add?";
  char messageType = '2';
  if (isTestMode)
    messageType = '3';
  return result + "device_id=" + deviceId + "&message_type=" + messageType + "&ppm=" + String((int)COConcentration);
}

int GetSignalLevel() {
  sendAtCommand(F("at+csq"), false);
  delay(3000);
  String signalLevelString = waitAtAnswer();
  int signalLevel = signalLevelString.substring(signalLevelString.indexOf(' ') + 1).toInt();
  if (signalLevel == 99) return 0;
  return signalLevel;
}

String SendRequestToServer(bool isTestMode) {
  if (!prepareSIM800LForSendRequest(isTestMode)) {
    RestartGsmModule();
    return "";
  }
  String httpInitresult = sendAtCommand(F("AT+HTTPACTION=0"), true);
  if (httpInitresult.indexOf(F("OK")) >= 0 && httpInitresult.indexOf(F("DEACT")) == -1) {
    if (waitAtAnswer().indexOf("200,") >= 0) {
      String serverResponse = sendAtCommand(F("AT+HTTPREAD"), true);
      sendAtCommand(F("AT+HTTPTERM"), true);
      sendAtCommand(F("AT+SAPBR=0,1"), true);
      return serverResponse;
    }
  }
  return "";
}

bool prepareSIM800LForSendRequest(bool isTestMode) {
  String response = "";
  response += sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\" "), true);
  response += sendAtCommand(F("AT+SAPBR=1,1"), true);
  response += sendAtCommand(F("AT+HTTPINIT"), true);
  response += sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true);
  response += sendAtCommand("AT+HTTPPARA=\"URL\",\"" + server + ":" + String(port) + GetRequestParams(isTestMode) + "\"", true);
  if (response.indexOf(F("ERROR")) == -1) {
    return true;
  } else {
    return false;
  }
}

String waitAtAnswer() {
  String response = "";
  do {
    int waitCount = 0;
    while (!SIM800L.available()) {
      if (waitCount > 400) {
        Serial.println(F("Timeout"));
        RestartGsmModule();
        return "";
      }
      delay(250);
      waitCount++;
    }
    response = SIM800L.readString();
    Serial.println(response);
  } while (response.indexOf(F("OK")) == -1 && IsGsmGarbage(response));
  return response;
}

bool IsGsmGarbage(String text) {
  return (text.indexOf(F("Ready" )) >= 0 ||
          text.indexOf(F("Call"  )) >= 0 ||
          text.indexOf(F("RDY"   )) >= 0 ||
          text.indexOf(F("SMS"   )) >= 0 ||
          text.indexOf(F("CMTI"  )) >= 0 ||
          text.indexOf(F("CME"   )) >= 0 ||
          text.indexOf(F("CPIN"  )) >= 0 ||
          text.indexOf(F("CFUN"  )) >= 0 ||
          text.indexOf(F("DST"   )) >= 0 ||
          text.indexOf(F("CTZV"  )) >= 0 ||
          text.indexOf(F("PSUTTZ")) >= 0);
}

String sendAtCommand(String command, bool waitFeedback) {
  if (command == "") {
    waitFeedback = true;
  } else {
    Serial.println(F("--------"));
    Serial.println(command);
    SIM800L.println(command);
  }

  if (waitFeedback)
    return waitAtAnswer();
  return "";
}
