// Network settings
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const char fixedAlertPhone[] = "+998990074787";
const String server = "45.138.159.216";
const int port = 8080;
bool FixedSmsSent = false; // SMS фиксированному номеру уже отправлено в текущем цикле тревоги

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
    if (!isHaveSignal) return;

    // Текст SMS формируется один раз — isTestMode больше не тянется по цепочке
    char smsTextBuf[50];
    if (isTestMode) {
      snprintf(smsTextBuf, sizeof(smsTextBuf), "Test alert. CO level: %d ppm", (int)COConcentration);
    } else {
      snprintf(smsTextBuf, sizeof(smsTextBuf), "ALARM! CO detected: %d ppm", (int)COConcentration);
    }
    String smsText = String(smsTextBuf);

    // 2.1: Отправляем SMS фиксированному номеру (только если ещё не отправлено)
    if (!FixedSmsSent) {
      InitSmsMode();
      if (SendSMS(smsText, String(fixedAlertPhone))) {
        FixedSmsSent = true;
      }
      delay(2000);
    }

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
        FixedSmsSent = false; // сброс для следующего цикла тревоги
        if (TestAlertIsActive) {
          TestAlertIsActive = false;
        }
        // 2.3: Отправляем SMS всем номерам из ответа сервера
        int commaIdx = body.indexOf(',');
        if (commaIdx != -1) {
          String phones = body.substring(commaIdx + 1);
          SendSMSToOwners(smsText, phones);
        }
        // Всё успешно — выключаем GSM модуль
        DeactivateGsmModulePower();
      }
    }
  }
}

// Отправляет ping на сервер — сообщает что устройство живо.
// Пропускается если идёт тревога или модуль занят.
void SendPing() {
  if (NeedForSendRequest || AlertIsActive || TestAlertIsActive) return;

  ActivateGsmModulePower();
  delay(2000);

  bool isHaveSignal = false;
  for (int i = 0; i < 10; i++) {
    if (GetSignalLevel() > 0) {
      isHaveSignal = true;
      break;
    }
    delay(500);
  }
  if (!isHaveSignal) {
    DeactivateGsmModulePower();
    return;
  }

  bool ok = true;
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\" "), true))) ok = false;
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+SAPBR=1,1"), true))) ok = false;
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+HTTPINIT"), true))) ok = false;
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true))) ok = false;

  if (ok) {
    char urlCmd[80];
    snprintf(urlCmd, sizeof(urlCmd),
      "AT+HTTPPARA=\"URL\",\"%s:%d/dev/ping?device_id=%s\"",
      server.c_str(), port, deviceId.c_str());
    if (isAtResponseOk(sendAtCommand(String(urlCmd), true))) {
      sendAtCommand(F("AT+HTTPACTION=0"), true);
      waitAtAnswer();
    }
  }

  sendAtCommand(F("AT+HTTPTERM"), true);
  sendAtCommand(F("AT+SAPBR=0,1"), true);
  DeactivateGsmModulePower();
}

// Устанавливает SMS режим. Вызывается один раз перед группой отправок,
// а также повторно при каждом ретрае.
bool InitSmsMode() {
  String response = sendAtCommand(F("AT+CMGF=1"), true);
  if (response.indexOf(F("OK")) == -1) {
    Serial.println(F("Failed to set SMS text mode"));
    return false;
  }
  delay(500);
  sendAtCommand(F("AT+CSCS=\"GSM\""), true);
  delay(500);
  return true;
}

// Отправляет SMS каждому номеру из строки phones (через запятую).
// Каждый номер повторяется до успешной отправки.
void SendSMSToOwners(String smsText, String phones) {
  InitSmsMode();
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
        sent = SendSMS(smsText, phone);
        if (sent) {
          Serial.print(F("SMS sent to "));
          Serial.println(phone);
        } else {
          Serial.print(F("Retry SMS to "));
          Serial.println(phone);
          RestartGsmModule(); // модуль мог зависнуть — перезагружаем
          delay(2000);
          InitSmsMode();
        }
      }
      delay(2000);
    }
  }
}

bool SendSMS(String smsText, String phoneNumber) {
  // AT+CMGF и AT+CSCS вынесены в InitSmsMode — здесь не дублируем
  String atCommand = "AT+CMGS=\"" + phoneNumber + "\"";
  SIM800L.println(atCommand);
  Serial.println(atCommand);
  delay(1000);

  SIM800L.print(smsText);
  delay(500);

  // Ctrl+Z — завершение SMS
  SIM800L.write(0x1A);
  Serial.println(F("SMS text sent, waiting for confirmation..."));

  delay(5000);
  String response = waitAtAnswer();

  return response.indexOf(F("+CMGS:")) >= 0 || response.indexOf(F("OK")) >= 0;
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
  bodyStart++;
  int bodyEnd = data.indexOf(F("\r\nOK"), bodyStart);
  if (bodyEnd == -1) bodyEnd = data.indexOf(F("\nOK"), bodyStart);
  if (bodyEnd == -1) bodyEnd = data.length();
  String body = data.substring(bodyStart, bodyEnd);
  body.trim();
  return body;
}

int GetSignalLevel() {
  // Убран ручной delay(3000) — waitAtAnswer внутри sendAtCommand сам ждёт ответ
  String response = sendAtCommand(F("AT+CSQ"), true);
  int signalLevel = response.substring(response.indexOf(' ') + 1).toInt();
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
  // Закрываем GPRS при любой ошибке — раньше соединение оставалось висеть
  sendAtCommand(F("AT+HTTPTERM"), true);
  sendAtCommand(F("AT+SAPBR=0,1"), true);
  return "";
}

// Проверяет ответ AT команды: пустой (таймаут) или ERROR = ошибка
bool isAtResponseOk(String response) {
  return response.length() > 0 && response.indexOf(F("ERROR")) == -1;
}

bool prepareSIM800LForSendRequest(bool isTestMode) {
  // Проверяем каждую команду отдельно — пустой ответ (таймаут) тоже считается ошибкой
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\" "), true))) return false;
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=1,1"), true))) return false;
  if (!isAtResponseOk(sendAtCommand(F("AT+HTTPINIT"), true))) return false;
  if (!isAtResponseOk(sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true))) return false;

  // URL собирается через snprintf в char[] — без String конкатенаций на куче
  char urlCmd[128];
  char msgType = isTestMode ? '3' : '2';
  snprintf(urlCmd, sizeof(urlCmd),
    "AT+HTTPPARA=\"URL\",\"%s:%d/msg/add?device_id=%s&message_type=%c&ppm=%d\"",
    server.c_str(), port, deviceId.c_str(), msgType, (int)COConcentration);
  if (!isAtResponseOk(sendAtCommand(String(urlCmd), true))) return false;

  return true;
}

String waitAtAnswer() {
  String response = "";
  do {
    int waitCount = 0;
    while (!SIM800L.available()) {
      if (waitCount > 400) {
        Serial.println(F("Timeout"));
        return ""; // RestartGsmModule убран — решение о перезапуске принимает вызывающий код
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
