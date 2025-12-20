// Network settings
const char apn[] = "internet";
const char user[] = "";
const char pass[] = "";
const String server = "45.138.159.216";
const int port = 8080;
bool GsmModuleIsPreparedForSendRequest = false;

// SMS settings
// Формат номера: "+998901234567" (международный формат с + и кодом страны)
const String smsPhoneNumber = "+998990074787"; // Замените на ваш номер

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

    // Сначала отправляем SMS
    bool smsSent = SendSMS(isTestMode);
    if (smsSent) {
      Serial.println("SMS sent successfully");
    } else {
      Serial.println("Failed to send SMS");
    }
    
    delay(2000); // Небольшая задержка между SMS и HTTP

    // Затем отправляем HTTP запрос
    String ServerResponse = SendRequestToServer(isTestMode);
    Serial.println(ServerResponse);
    if (ServerResponse == "") {
      Serial.println("Failed to send htpp request");
      RestartGsmModule();
    } else {
      String responseNumber = ExtractResponseNumber(ServerResponse);
      if (responseNumber == "1") {
        Serial.println("SUCCESS");
        NeedForSendRequest = false;
        if (TestAlertIsActive) {
          TestAlertIsActive = false;
        }
      }
      // DeactivateGsmModulePower();
    }
  }
}

bool SendSMS(bool isTestMode) {
  // Устанавливаем текстовый режим SMS
  String response = sendAtCommand(F("AT+CMGF=1"), true);
  if (response.indexOf(F("OK")) == -1) {
    Serial.println("Failed to set SMS text mode");
    return false;
  }
  
  delay(500);
  
  // Устанавливаем кодировку для кириллицы (опционально)
  sendAtCommand(F("AT+CSCS=\"GSM\""), true);
  delay(500);
  
  // Указываем номер получателя
  String atCommand = "AT+CMGS=\"" + smsPhoneNumber + "\"";
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
  
  // Отправляем текст SMS
  SIM800L.print(smsText);
  delay(500);
  
  // Отправляем Ctrl+Z (символ завершения SMS)
  SIM800L.write(0x1A);
  Serial.println("SMS text sent, waiting for confirmation...");
  
  // Ждем подтверждения отправки
  delay(5000);
  response = waitAtAnswer();
  
  if (response.indexOf(F("+CMGS:")) >= 0 || response.indexOf(F("OK")) >= 0) {
    return true;
  }
  
  return false;
}

void ActivateGsmModulePower() {
  // подаем высокий сигнал пину MODEM_POWER_PIN, чтобы питание GSM-модуля включилось
  digitalWrite(MODEM_POWER_PIN, HIGH);
}

void DeactivateGsmModulePower() {
  // подаем высокий сигнал пину MODEM_POWER_PIN, чтобы питание GSM-модуля выключилось
  digitalWrite(MODEM_POWER_PIN, LOW);
}

void RestartGsmModule() {
  digitalWrite(MODEM_POWER_PIN, LOW);
  delay(1000);
  digitalWrite(MODEM_POWER_PIN, HIGH);
}

String ExtractResponseNumber(String line) {
  line.trim();
  if (line.startsWith(F("+HTTPREAD:"))) {
    int startIndex = line.indexOf("\n");
    String num = line.substring(startIndex + 1, startIndex + 2);
    return num;
  }
  return "0";
}

String GetRequestParams(bool isTestMode) {

  String result = "/msg/add?";
  char messageType = '2';
  if (isTestMode)
    messageType = '3';
  return result + "device_id=" + deviceId + "&message_type=" + messageType + "&ppm=" + String((int)COConcentration);
}

int GetSignalLevel() {
  //Получение информации об уровне сигнала
  sendAtCommand(F("at+csq"), false);
  delay(3000);
  String signalLevelString = waitAtAnswer();
  int signalLevel = signalLevelString.substring(signalLevelString.indexOf(' ') + 1).toInt();
  return signalLevel;
}

String SendRequestToServer(bool isTestMode) {
  bool prepareResult = false;
  prepareResult = prepareSIM800LForSendRequest(isTestMode);
  if (prepareResult) {
    String httpInitresult = sendAtCommand(F("AT+HTTPACTION=0"), true);
    if (httpInitresult.indexOf(F("OK")) >= 0 && httpInitresult.indexOf(F("DEACT")) == -1) {
      if (waitAtAnswer().indexOf("200,") >= 0) {
        // isConnected(true);
        String serverResponse = sendAtCommand(F("AT+HTTPREAD"), true);
        sendAtCommand(F("AT+HTTPTERM"), true);
        sendAtCommand(F("AT+SAPBR=0,1"), true);
        return serverResponse;
      }
    }
  } else
    RestartGsmModule();
}

bool prepareSIM800LForSendRequest(bool isTestMode) {
  String response = "";
  if (!GsmModuleIsPreparedForSendRequest) {
    //Включение режима GPRS
    response += sendAtCommand(F("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\" "), true);
    // //Настройка APN
    // response += sendAtCommand(F("AT+SAPBR=3,1,\"APN\",\"internet\""), true);
  }
  //Включение активного потребления для подключение к интернету
  response += sendAtCommand(F("AT+SAPBR=1,1"), true);
  //Инициализация HTTP канала
  response += sendAtCommand(F("AT+HTTPINIT"), true);
  // //Включение режима поддержки SSL сертификата
  //response += sendAtCommand(F("AT+HTTPSSL=1"), true);
  //установка идентификатора HTTP канала
  response += sendAtCommand(F("AT+HTTPPARA=\"CID\",1"), true);
  response += sendAtCommand("AT+HTTPPARA=\"URL\",\"" + server + ":" + String(port) + GetRequestParams(isTestMode) + "\"", true);
  if (response.indexOf(F("ERROR")) == -1) {
    return true;
  } else {
    return false;
  }
}

String waitAtAnswer() {
  int waitCount = 0;
  while (!SIM800L.available()) {
    if (waitCount > 400) {
      Serial.println(F("Timeout"));
      RestartGsmModule();
      //Reboot();
    }
    delay(250);
    waitCount++;
  }
  //delay(100);
  String response = SIM800L.readString();
  Serial.println(response);
  if (response.indexOf(F("OK")) == -1 && IsGsmGarbage(response))
    response = waitAtAnswer();
  while (response.length() > 0 && (response.substring(0, 1) == F("\r") || response.substring(0, 1) == F("\n")))
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