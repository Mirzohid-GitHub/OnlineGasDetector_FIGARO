// Network settings
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const char fixedAlertPhone[] = "+998999070321";
// const String server = "45.138.159.216";
const char server[] = "144.91.100.62";
const int port = 8080;
bool FixedSmsSent = false; // SMS фиксированному номеру уже отправлено в текущем цикле тревоги

// Гарантированно выключает питание при любом выходе из функции, включая return.
class GsmPowerGuard {
public:
  GsmPowerGuard() {
    ActivateGsmModulePower();
    Serial.println(F("GSM: operation started"));
  }

  ~GsmPowerGuard() {
    DeactivateGsmModulePower();
    Serial.println(F("GSM: operation finished, power off"));
  }
};

void SendRequest(bool isTestMode) {
  if (NeedForSendRequest || isTestMode) {
    GsmPowerGuard powerGuard;
    if (!WaitForGsmReady(60000UL)) {
      Serial.println(F("GSM: initialization/registration timeout"));
      return;
    }
    bool isHaveSignal = false;
    for (int i = 0; i < 5; i++) {
      if (GetSignalLevel() > 0) {
        isHaveSignal = true;
        break;
      }
      delay(1000);
    }
    if (!isHaveSignal) {
      return;
    }

    char smsTextBuf[50];
    if (isTestMode) {
      snprintf_P(smsTextBuf, sizeof(smsTextBuf), PSTR("Test alert. CO level: %d ppm"), (int)COConcentration);
    } else {
      snprintf_P(smsTextBuf, sizeof(smsTextBuf), PSTR("ALARM! CO detected: %d ppm"), (int)COConcentration);
    }
    // Шаг 1: SMS фиксированному номеру (с повторными попытками)
    if (!FixedSmsSent) {
      if (!InitSmsMode())
        return;
      for (int attempt = 0; attempt < 3; attempt++) {
        if (SendSMS(smsTextBuf, fixedAlertPhone)) {
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
          if (!WaitForGsmReady(60000UL))
            break;
          while (SIM800L.available()) SIM800L.read();
        }
        if (!InitSmsMode())
          continue;
      }
      if (!FixedSmsSent) {
        // Все попытки исчерпаны — попробуем в следующем цикле
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
      return;
    }

    String body = ExtractResponseBody(ServerResponse);
    if (body.length() > 0 && body.charAt(0) == '1') {
      bool ownersSmsSent = true;

      // Шаг 3: SMS владельцам из ответа сервера
      int commaIdx = body.indexOf(',');
      if (commaIdx != -1) {
        String phones = body.substring(commaIdx + 1);
        // GPRS уже закрыт в SendRequestToServer — просто переключаемся на SMS
        ownersSmsSent = SendSMSToOwners(smsTextBuf, phones);
      }

      if (!ownersSmsSent) {
        Serial.println(F("Owner SMS delivery failed; alert remains active"));
        return;
      }

      NeedForSendRequest = false;
      FixedSmsSent = false; // сброс для следующего цикла тревоги
      if (TestAlertIsActive) {
        TestAlertIsActive = false;
      }
      Serial.println(F("SUCCESS"));
    }

  }
}

// Отправляет ping на сервер — сообщает что устройство живо.
// Пропускается если идёт тревога или модуль занят.
void SendPing() {
  if (NeedForSendRequest || AlertIsActive || TestAlertIsActive) return;

  GsmPowerGuard powerGuard;
  if (!WaitForGsmReady(60000UL)) {
    Serial.println(F("GSM: initialization/registration timeout"));
    return;
  }

  bool isHaveSignal = false;
  for (int i = 0; i < 5; i++) {
    if (GetSignalLevel() > 0) {
      isHaveSignal = true;
      break;
    }
    delay(1000);
  }
  if (!isHaveSignal) {
    return;
  }

  bool ok = true;
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""), true))) ok = false;
  if (ok) {
    char apnCmd[50];
    snprintf_P(apnCmd, sizeof(apnCmd), PSTR("AT+SAPBR=3,1,\"APN\",\"%s\""), apn);
    if (!isAtResponseOk(sendAtCommand(apnCmd, true))) ok = false;
  }
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+SAPBR=1,1"), true))) ok = false;
  if (ok) sendAtCommand(F("AT+HTTPTERM"), true); // закрыть зависшую сессию если есть
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+HTTPINIT"), true))) ok = false;
  if (ok && !isAtResponseOk(sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true))) ok = false;

  if (ok) {
    char urlCmd[80];
    snprintf_P(urlCmd, sizeof(urlCmd),
      PSTR("AT+HTTPPARA=\"URL\",\"%s:%d/dev/ping?device_id=%s\""),
      server, port, deviceId);
    if (isAtResponseOk(sendAtCommand(urlCmd, true))) {
      sendAtCommand(F("AT+HTTPACTION=0"), true);
      waitAtAnswer();
    }
  }

  sendAtCommand(F("AT+HTTPTERM"), true);
  sendAtCommand(F("AT+SAPBR=0,1"), true);
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
  if (!isAtResponseOk(sendAtCommand(F("AT+CSCS=\"GSM\""), true))) {
    Serial.println(F("Failed to select GSM character set"));
    return false;
  }
  delay(500);
  return true;
}

// Отправляет SMS каждому номеру из строки phones (через запятую).
// Каждый номер повторяется до успешной отправки.
bool SendSMSToOwners(const char *smsText, const String &phones) {
  while (SIM800L.available()) SIM800L.read(); // сбросить остатки от GPRS сессии
  if (!InitSmsMode())
    return false;
  bool allSent = true;
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
        sent = SendSMS(smsText, phone.c_str());
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
            if (!WaitForGsmReady(60000UL)) {
              break;
            }
            while (SIM800L.available()) SIM800L.read();
          }
          if (!InitSmsMode())
            continue;
        }
      }
      if (!sent)
        allSent = false;
      delay(2000);
    }
  }
  return allSent;
}

bool SendSMS(const char *smsText, const char *phoneNumber) {
  DrainGsmInput();
  SIM800L.print(F("AT+CMGS=\""));
  SIM800L.print(phoneNumber);
  SIM800L.println('"');
  Serial.print(F("AT+CMGS=\""));
  Serial.print(phoneNumber);
  Serial.println('"');

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
  bool gotCmgs = false;
  bool gotOk = false;
  for (int waitCount = 0; waitCount < 120; waitCount++) {
    wdt_reset();
    while (SIM800L.available()) {
      response += SIM800L.readString();
      Serial.println(response);

      if (response.indexOf(F("ERROR")) >= 0) {
        Serial.println(F("SMS send error"));
        return false;
      }
      gotCmgs = response.indexOf(F("+CMGS:")) >= 0;
      gotOk = response.indexOf(F("\r\nOK\r\n")) >= 0;
      if (gotCmgs && gotOk) {
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

void DrainGsmInput() {
  while (SIM800L.available())
    SIM800L.read();
}

void RestartGsmModule() {
  digitalWrite(MODEM_POWER_PIN, LOW);
  GsmPowerIsOn = false;
  delay(1000);
  digitalWrite(MODEM_POWER_PIN, HIGH);
  GsmPowerIsOn = true;
}


// Извлекает тело ответа из строки вида "+HTTPREAD: N\r\n<body>\r\nOK"
String ExtractResponseBody(const String &data) {
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
  String response = sendAtCommandTimed(F("AT+CSQ"), 5000UL);
  int csqIdx = response.indexOf(F("+CSQ:"));
  if (csqIdx == -1) return 0;
  int signalLevel = response.substring(csqIdx + 6).toInt();
  if (signalLevel == 99) return 0;
  return signalLevel;
}

bool IsGsmRegistered(const String &response) {
  int cregIdx = response.indexOf(F("+CREG:"));
  if (cregIdx < 0)
    return false;

  int statusPos = response.indexOf(',', cregIdx);
  if (statusPos >= 0)
    statusPos++;
  else
    statusPos = cregIdx + 6;

  while (statusPos < (int)response.length() && response.charAt(statusPos) == ' ')
    statusPos++;
  if (statusPos >= (int)response.length())
    return false;

  char status = response.charAt(statusPos);
  return status == '1' || status == '5';
}

bool WaitForGsmReady(unsigned long timeoutMs) {
  const unsigned long startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    String atResponse = sendAtCommandTimed(F("AT"), 2500UL);
    if (isAtResponseOk(atResponse)) {
      String simResponse = sendAtCommandTimed(F("AT+CPIN?"), 3000UL);
      if (simResponse.indexOf(F("+CPIN: READY")) >= 0) {
        String networkResponse = sendAtCommandTimed(F("AT+CREG?"), 3000UL);
        if (IsGsmRegistered(networkResponse)) {
          Serial.println(F("GSM: modem, SIM and network ready"));
          return true;
        }
      }
    }

    Serial.println(F("GSM: waiting for initialization/registration"));
    delay(1000);
  }

  return false;
}

String SendRequestToServer(bool isTestMode) {
  if (!prepareSIM800LForSendRequest(isTestMode)) {
    // prepareSIM800LForSendRequest сам закрывает GPRS/HTTP при ошибке
    return "";
  }
  String httpInitresult = sendAtCommand(F("AT+HTTPACTION=0"), true);
  if (httpInitresult.indexOf(F("OK")) >= 0 && httpInitresult.indexOf(F("DEACT")) == -1) {
    String actionResult;
    if (httpInitresult.indexOf(F("+HTTPACTION:")) >= 0)
      actionResult = httpInitresult;
    else
      actionResult = waitAtAnswer();
    if (actionResult.indexOf(F("200,")) >= 0) {
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
bool isAtResponseOk(const String &response) {
  return response.indexOf(F("OK")) >= 0 &&
         response.indexOf(F("ERROR")) == -1;
}

bool prepareSIM800LForSendRequest(bool isTestMode) {
  // Настройка GPRS bearer
  if (!isAtResponseOk(sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\""), true))) return false;
  // APN — без этого bearer не откроется после перезагрузки модуля
  char apnCmd[50];
  snprintf_P(apnCmd, sizeof(apnCmd), PSTR("AT+SAPBR=3,1,\"APN\",\"%s\""), apn);
  if (!isAtResponseOk(sendAtCommand(apnCmd, true))) return false;

  // Открытие bearer с таймаутом 30 сек (вместо 100 по умолчанию)
  Serial.println(F("--------"));
  Serial.println(F("AT+SAPBR=1,1"));
  DrainGsmInput();
  SIM800L.println(F("AT+SAPBR=1,1"));
  String sapbrResp = "";
  for (int w = 0; w < 120; w++) { // 120 × 250ms = 30 сек
    delay(250);
    if (SIM800L.available()) {
      sapbrResp = waitAtAnswer(30000UL);
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
  snprintf_P(urlCmd, sizeof(urlCmd),
    PSTR("AT+HTTPPARA=\"URL\",\"%s:%d/msg/add?device_id=%s&message_type=%c&ppm=%d\""),
    server, port, deviceId, msgType, (int)COConcentration);
  if (!isAtResponseOk(sendAtCommand(urlCmd, true))) {
    sendAtCommand(F("AT+HTTPTERM"), true);
    sendAtCommand(F("AT+SAPBR=0,1"), true);
    return false;
  }

  return true;
}

String waitAtAnswer(unsigned long timeoutMs) {
  const size_t MaxResponseLength = 320;
  String response;
  response.reserve(160);
  const unsigned long startedAt = millis();

  while (millis() - startedAt < timeoutMs) {
    wdt_reset();
    while (SIM800L.available()) {
      char c = (char)SIM800L.read();
      Serial.write(c);
      if (response.length() < MaxResponseLength)
        response += c;
    }

    if (response.indexOf(F("\r\nOK\r\n")) >= 0 ||
        response.indexOf(F("\r\nERROR\r\n")) >= 0 ||
        response.indexOf(F("+CME ERROR:")) >= 0 ||
        response.indexOf(F("+CMS ERROR:")) >= 0 ||
        response.indexOf(F("+HTTPACTION:")) >= 0) {
      Serial.println();
      return response;
    }
    delay(20);
  }

  Serial.println(F("\nTimeout"));
  return response;
}

String waitAtAnswer() {
  return waitAtAnswer(12000UL);
}

bool IsGsmGarbage(const String &text) {
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

String sendAtCommand(const String &command, bool waitFeedback) {
  if (command == "") {
    waitFeedback = true;
  } else {
    DrainGsmInput();
    Serial.println(F("--------"));
    Serial.println(command);
    SIM800L.println(command);
  }

  if (waitFeedback)
    return waitAtAnswer();
  return "";
}

// Перегрузка для F("..."): команда не копируется из Flash в heap.
String sendAtCommand(const __FlashStringHelper *command, bool waitFeedback) {
  DrainGsmInput();
  Serial.println(F("--------"));
  Serial.println(command);
  SIM800L.println(command);
  return waitFeedback ? waitAtAnswer() : String();
}

String sendAtCommand(const char *command, bool waitFeedback) {
  DrainGsmInput();
  Serial.println(F("--------"));
  Serial.println(command);
  SIM800L.println(command);
  return waitFeedback ? waitAtAnswer() : String();
}

String sendAtCommandTimed(const __FlashStringHelper *command, unsigned long timeoutMs) {
  DrainGsmInput();
  Serial.println(F("--------"));
  Serial.println(command);
  SIM800L.println(command);
  return waitAtAnswer(timeoutMs);
}
