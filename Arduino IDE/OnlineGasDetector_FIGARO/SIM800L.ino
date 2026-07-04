// Network settings
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const char fixedAlertPhone[] = "+998999070321";
// const String server = "45.138.159.216";
const String server = "144.91.100.62";
const int port = 8080;
bool FixedSmsSent = false; // SMS фиксированному номеру уже отправлено в текущем цикле тревоги

void SendRequest(bool isTestMode) {
  if (NeedForSendRequest || isTestMode) {
    ActivateGsmModulePower();
    delay(3000);
    bool isHaveSignal = false;
    for (int i = 0; i < 5; i++) {
      if (GetSignalLevel() > 0) {
        isHaveSignal = true;
        break;
      }
      delay(1000);
    }
    if (!isHaveSignal) {
      DeactivateGsmModulePower();
      return;
    }

    char smsTextBuf[50];
    if (isTestMode) {
      snprintf(smsTextBuf, sizeof(smsTextBuf), "Test alert. CO level: %d ppm", (int)COConcentration);
    } else {
      snprintf(smsTextBuf, sizeof(smsTextBuf), "ALARM! CO detected: %d ppm", (int)COConcentration);
    }
    String smsText = String(smsTextBuf);

    // Шаг 1: SMS фиксированному номеру (с повторными попытками)
    if (!FixedSmsSent) {
      InitSmsMode();
      for (int attempt = 0; attempt < 3; attempt++) {
        if (SendSMS(smsText, String(fixedAlertPhone))) {
          FixedSmsSent = true;
          break;
        }
        Serial.println(F("Retry fixed SMS..."));
        if (attempt == 0) {
          // Первая попытка: просто сбрасываем буфер и переинициализируем
          delay(2000);
          while (SIM800L.available()) SIM800L.read();
        } else {
          // Вторая попытка: полный рестарт модуля
          RestartGsmModule();
          delay(5000);
          while (SIM800L.available()) SIM800L.read();
        }
        InitSmsMode();
      }
      if (!FixedSmsSent) {
        // Все попытки исчерпаны — попробуем в следующем цикле
        DeactivateGsmModulePower();
        return;
      }
      delay(2000);
    }

    // Шаг 2: HTTP запрос на сервер (внутри открывается и закрывается GPRS)
    String ServerResponse = SendRequestToServer(isTestMode);
    Serial.println(ServerResponse);
    if (ServerResponse == "") {
      Serial.println(F("Failed to send http request"));
      // FixedSmsSent остаётся true — в следующем цикле начнём сразу с HTTP
      DeactivateGsmModulePower();
      return;
    }

    String body = ExtractResponseBody(ServerResponse);
    if (body.length() > 0 && body.charAt(0) == '1') {
      Serial.println(F("SUCCESS"));

      // Шаг 3: SMS владельцам из ответа сервера
      int commaIdx = body.indexOf(',');
      if (commaIdx != -1) {
        String phones = body.substring(commaIdx + 1);
        // GPRS уже закрыт в SendRequestToServer — просто переключаемся на SMS
        SendSMSToOwners(smsText, phones);
      }

      NeedForSendRequest = false;
      FixedSmsSent = false; // сброс для следующего цикла тревоги
      if (TestAlertIsActive) {
        TestAlertIsActive = false;
      }
    }

    DeactivateGsmModulePower();
  }
}

// Отправляет ping на сервер — сообщает что устройство живо.
// Пропускается если идёт тревога или модуль занят.
void SendPing() {
  if (NeedForSendRequest || AlertIsActive || TestAlertIsActive) return;

  ActivateGsmModulePower();
  delay(3000);

  bool isHaveSignal = false;
  for (int i = 0; i < 5; i++) {
    if (GetSignalLevel() > 0) {
      isHaveSignal = true;
      break;
    }
    delay(1000);
  }
  if (!isHaveSignal) {
    DeactivateGsmModulePower();
    return;
  }

  bool ok = true;
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""), true))) ok = false;
  if (ok) {
    char apnCmd[50];
    snprintf(apnCmd, sizeof(apnCmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
    if (!isAtResponseOk(sendAtCommand(String(apnCmd), true))) ok = false;
  }
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+SAPBR=1,1"), true))) ok = false;
  if (ok) sendAtCommand(F("AT+HTTPTERM"), true); // закрыть зависшую сессию если есть
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
  while (SIM800L.available()) SIM800L.read(); // сбросить остатки от GPRS сессии
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
      for (int attempt = 0; attempt < 3 && !sent; attempt++) {
        sent = SendSMS(smsText, phone);
        if (sent) {
          Serial.print(F("SMS sent to "));
          Serial.println(phone);
        } else {
          Serial.print(F("Retry SMS to "));
          Serial.println(phone);
          if (attempt == 0) {
            delay(2000);
            while (SIM800L.available()) SIM800L.read();
          } else {
            RestartGsmModule();
            delay(5000);
            while (SIM800L.available()) SIM800L.read();
          }
          InitSmsMode();
        }
      }
      delay(2000);
    }
  }
}

bool SendSMS(String smsText, String phoneNumber) {
  String atCommand = "AT+CMGS=\"" + phoneNumber + "\"";
  SIM800L.println(atCommand);
  Serial.println(atCommand);

  // Ждём приглашение ">" от модуля (до 5 секунд)
  bool gotPrompt = false;
  for (int i = 0; i < 20; i++) { // 20 × 250ms = 5 сек
    wdt_reset();
    if (SIM800L.available()) {
      String data = SIM800L.readString();
      if (data.indexOf('>') >= 0) {
        gotPrompt = true;
        break;
      }
      if (data.indexOf(F("ERROR")) >= 0) {
        Serial.println(F("SMS command error"));
        return false;
      }
    }
    delay(250);
  }
  if (!gotPrompt) {
    Serial.println(F("No SMS prompt"));
    return false;
  }

  SIM800L.print(smsText);
  delay(500);
  SIM800L.write(0x1A);
  Serial.println(F("SMS text sent, waiting for confirmation..."));

  // Ждём подтверждения +CMGS/OK (до 30 секунд). Ответ может прийти частями.
  String response = "";
  response.reserve(120);
  for (int waitCount = 0; waitCount < 120; waitCount++) {
    wdt_reset();
    while (SIM800L.available()) {
      response += SIM800L.readString();
      Serial.println(response);

      if (response.indexOf(F("ERROR")) >= 0) {
        Serial.println(F("SMS send error"));
        return false;
      }
      if (response.indexOf(F("+CMGS:")) >= 0 || response.indexOf(F("OK")) >= 0) {
        return true;
      }
    }
    delay(250);
  }

  Serial.println(F("SMS timeout"));
  Serial.println(response);
  return false;
}

void ActivateGsmModulePower() {
  digitalWrite(MODEM_POWER_PIN, HIGH);
  GsmPowerIsOn = true;
}

void DeactivateGsmModulePower() {
  digitalWrite(MODEM_POWER_PIN, LOW);
  GsmPowerIsOn = false;
}

void RestartGsmModule() {
  digitalWrite(MODEM_POWER_PIN, LOW);
  GsmPowerIsOn = false;
  delay(1000);
  digitalWrite(MODEM_POWER_PIN, HIGH);
  GsmPowerIsOn = true;
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
  // Сбрасываем буфер — убираем загрузочные URC (RDY, CFUN, SMS Ready и т.д.)
  while (SIM800L.available()) SIM800L.read();
  Serial.println(F("--------"));
  Serial.println(F("AT+CSQ"));
  SIM800L.println(F("AT+CSQ"));
  delay(3000);
  String response = "";
  while (SIM800L.available()) {
    response += SIM800L.readString();
  }
  Serial.println(response);
  int csqIdx = response.indexOf(F("+CSQ:"));
  if (csqIdx == -1) return 0;
  int signalLevel = response.substring(csqIdx + 6).toInt();
  if (signalLevel == 99) return 0;
  return signalLevel;
}

String SendRequestToServer(bool isTestMode) {
  if (!prepareSIM800LForSendRequest(isTestMode)) {
    // prepareSIM800LForSendRequest сам закрывает GPRS/HTTP при ошибке
    return "";
  }
  String httpInitresult = sendAtCommand(F("AT+HTTPACTION=0"), true);
  if (httpInitresult.indexOf(F("OK")) >= 0 && httpInitresult.indexOf(F("DEACT")) == -1) {
    if (waitAtAnswer().indexOf("200,") >= 0) {
      delay(500); // дать модулю время подготовить данные для чтения
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
  // Настройка GPRS bearer
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""), true))) return false;
  // APN — без этого bearer не откроется после перезагрузки модуля
  char apnCmd[50];
  snprintf(apnCmd, sizeof(apnCmd), "AT+SAPBR=3,1,\"APN\",\"%s\"", apn);
  if (!isAtResponseOk(sendAtCommand(String(apnCmd), true))) return false;

  // Открытие bearer с таймаутом 30 сек (вместо 100 по умолчанию)
  Serial.println(F("--------"));
  Serial.println(F("AT+SAPBR=1,1"));
  SIM800L.println(F("AT+SAPBR=1,1"));
  String sapbrResp = "";
  for (int w = 0; w < 120; w++) { // 120 × 250ms = 30 сек
    delay(250);
    if (SIM800L.available()) {
      sapbrResp = SIM800L.readString();
      Serial.println(sapbrResp);
      break;
    }
  }
  if (!isAtResponseOk(sapbrResp)) return false;

  // Закрываем предыдущую HTTP сессию если зависла
  sendAtCommand(F("AT+HTTPTERM"), true);
  if (!isAtResponseOk(sendAtCommand(F("AT+HTTPINIT"), true))) {
    sendAtCommand(F("AT+SAPBR=0,1"), true); // закрываем bearer
    return false;
  }
  if (!isAtResponseOk(sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true))) {
    sendAtCommand(F("AT+HTTPTERM"), true);
    sendAtCommand(F("AT+SAPBR=0,1"), true);
    return false;
  }

  char urlCmd[128];
  char msgType = isTestMode ? '3' : '2';
  snprintf(urlCmd, sizeof(urlCmd),
    "AT+HTTPPARA=\"URL\",\"%s:%d/msg/add?device_id=%s&message_type=%c&ppm=%d\"",
    server.c_str(), port, deviceId.c_str(), msgType, (int)COConcentration);
  if (!isAtResponseOk(sendAtCommand(String(urlCmd), true))) {
    sendAtCommand(F("AT+HTTPTERM"), true);
    sendAtCommand(F("AT+SAPBR=0,1"), true);
    return false;
  }

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
