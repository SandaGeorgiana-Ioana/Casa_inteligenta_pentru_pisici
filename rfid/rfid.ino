#include <SPI.h> 
#include <MFRC522_minimal.h>
#include <Servo.h>
#include <SoftwareSerial.h>

#define RST_PIN 9        
#define SS_PIN 10        
#define BUZZER_PIN 8     
#define SERVO_PIN 6      
#define RAIN_ANALOG A0   
#define DHTPIN 4      
#define LIGHT_PIN A2     
#define WATER_SENSOR A3  

#define BT_RX 2         
#define BT_TX 3        

#define LED_PIN 13
#define STATUS_LED A1 

char Incoming_value = 0;

MFRC522_minimal rfid(SS_PIN, RST_PIN);
Servo myServo;
SoftwareSerial bluetooth(BT_RX, BT_TX);

//carduri rfid autorizate
String authorizedUIDs[] = {
    //"8f ba 79 1f",
    //"14 8a 96 a3",
    "15 4c 2b 1f",
    "b1 81 ee a9"
};

int numberOfAuthorizedUids = sizeof(authorizedUIDs) / sizeof(authorizedUIDs[0]);

//praguri pentru senzori
int noRainThreshold = 500;    
int lightRainThreshold = 200;  

int darkThreshold = 50;      
int dimThreshold = 100;       


//***********codul pentru citerea de pe registri senzor DHT11***********

uint8_t dataDHT[5];

//setari pentru pini
void pinOutput() { DDRD |= (1 << DHTPIN); }
void pinInput()  { DDRD &= ~(1 << DHTPIN); }
void pinHigh()   { PORTD |=  (1 << DHTPIN); }
void pinLow()    { PORTD &= ~(1 << DHTPIN); }

//delay aprox in ms folosind numarar de cicluri CPU
void delay_counter(uint16_t us)
{
    TCCR1A = 0;
    TCCR1B = (1 << CS10);//prescaler = 1 =>16 MHz
    TCNT1  = 0;

    //nr de ticks = us * 16
    uint16_t ticks = us * 16;

    while (TCNT1 < ticks);

    TCCR1B = 0;//oprește timerul
}


//asteapat pana cand primeste o stare
bool waitForState(uint8_t state, uint16_t timeout_us) {
  uint16_t t = 0;
  while (((PIND & (1 << DHTPIN)) ? 1 : 0) != state) {
    delay_counter(10);
    t++;
    if (t >= timeout_us) //eroare citire
      return false;
  }
  return true;
}

//citire bit cu bit
uint8_t readDHT11() {

  //initializare
  pinOutput();
  pinLow();
  delay_counter(20);
  pinHigh();
  delay_counter(40);

  pinInput();

  //raspunsul senzorului
  if (!waitForState(0, 100)) return 1;
  if (!waitForState(1, 100)) return 1;
  if (!waitForState(0, 100)) return 1;

  //citit cei 5 bytes trasnsmisi de senzor
  for (int j = 0; j < 5; j++) {
    uint8_t val = 0;
    for (int i = 0; i < 8; i++) {

      //asteapta inceputul uni bit
      if (!waitForState(1, 100)) return 1;
      delay_counter(40);//delay pentru asteptarea raspunsului
       
      //daca pinul este High => bit = 1
      if (PIND & (1 << DHTPIN)) val |= (1 << (7 - i));
      
      //asteapta revenire pe Low
      if (!waitForState(0, 100)) return 1;
    }
    dataDHT[j] = val;//salvam bitul citit
  }
  //validare => byte-ul 5 = suma celorlanti 4
  if (dataDHT[4] != ((dataDHT[0] + dataDHT[1] + dataDHT[2] + dataDHT[3]) & 0xFF))
    return 1; //eroare

  return 0;//citire reusita
}

//*****************************************************************************

//initializare pini
void setup() {
  Serial.begin(9600);
  bluetooth.begin(9600);
  SPI.begin();
  rfid.PCD_Init();
  delay(100);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(RAIN_ANALOG, INPUT);
  pinMode(LIGHT_PIN, INPUT);
  pinMode(WATER_SENSOR, INPUT);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  myServo.attach(SERVO_PIN);
  myServo.write(0);
  delay(300);
  myServo.detach(); 

  Serial.println("Sistem pornit...");
  bluetooth.println("Sistem pornit...");
}

//*****************************************************************************
void loop() {

  bool systemOK = true;

  //control pentru led testare bluetooth
  if (bluetooth.available() > 0) {
    Incoming_value = bluetooth.read();
    Serial.print("Comanda BT: ");
    Serial.println(Incoming_value);

    if (Incoming_value == '1') {
      digitalWrite(LED_PIN, HIGH);
      bluetooth.println("LED aprins");
    } 
    else if (Incoming_value == '0') {
      digitalWrite(LED_PIN, LOW);
      bluetooth.println("LED stins");
    }
  }

  //citire DHT11
  uint8_t status = readDHT11();

  float temperatura = NAN;
  float umiditate = NAN;

  //compune temperatura din parte intreag si zecimala
  if (status == 0) {
    umiditate = dataDHT[0] + dataDHT[1] / 10.0;
    temperatura = dataDHT[2] + dataDHT[3] / 10.0;
  } else {
    systemOK = false;
  }

  //citire senzori
  int rainRaw = analogRead(RAIN_ANALOG);
  int lightRaw = analogRead(LIGHT_PIN);
  int waterRaw = analogRead(WATER_SENSOR);

  //constante pentru procente senzori
  int rainPercent  = constrain(map(rainRaw, 1023, 0, 0, 100), 0, 100);
  int lightPercent = constrain(map(lightRaw, 0, 1023, 0, 100), 0, 100);
  int waterPercent = constrain(map(waterRaw, 0, 1023, 0, 100), 0, 100);

  //clasificari pe baza valorilor senzorilor
  String rainStatus;
  if (rainRaw > noRainThreshold) rainStatus = "Nu ploua";
  else if (rainRaw > lightRainThreshold) rainStatus = "Ploua putin";
  else rainStatus = "Ploua mult";

  String lightStatus;
  if (lightRaw < darkThreshold) lightStatus = "Noapte / intuneric";
  else if (lightRaw < dimThreshold) lightStatus = "Lumina slaba";
  else lightStatus = "Zi / lumina puternica";

  String waterStatus;
  if (waterRaw < 200) waterStatus = "Nivel scazut de apa";
  else if (waterRaw < 550) waterStatus = "Nivel mediu de apa";
  else waterStatus = "Nivel ridicat de apa";

  //afisare valori
  Serial.println("-----------------------");
  bluetooth.println("-----------------------");

  if (!isnan(temperatura)) {
    Serial.print("Temperatur: "); Serial.println(temperatura);
    Serial.print("Umiditate: "); Serial.println(umiditate);

    bluetooth.print("Temperatura: "); bluetooth.println(temperatura);
    bluetooth.print("Umiditate: "); bluetooth.println(umiditate);
  } else {
    Serial.println("Eroare DHT");
    bluetooth.println("Eroare DHT");
  }

  Serial.print("Ploaie: "); Serial.print(rainPercent); Serial.print("% -> "); Serial.println(rainStatus);
  Serial.print("Lumina: "); Serial.print(lightPercent); Serial.print("% -> "); Serial.println(lightStatus);
  Serial.print("Apa: "); Serial.print(waterPercent); Serial.print("% -> "); Serial.println(waterStatus);

  bluetooth.print("Ploaie: "); bluetooth.print(rainPercent); bluetooth.print("% -> "); bluetooth.println(rainStatus);
  bluetooth.print("Lumina: "); bluetooth.print(lightPercent); bluetooth.print("% -> "); bluetooth.println(lightStatus);
  bluetooth.print("Apa: "); bluetooth.print(waterPercent); bluetooth.print("% -> "); bluetooth.println(waterStatus);


  //********************************rfid******************************
  if (!rfid.PICC_IsNewCardPresent()) {
    digitalWrite(STATUS_LED, systemOK ? HIGH : LOW);//ledul bipaie daca nu avem card valid
    delay(500);
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    Serial.println("Eroare la citirea cardului...");
    bluetooth.println("Eroare la citirea cardului...");
    systemOK = false;
    digitalWrite(STATUS_LED, systemOK ? HIGH : LOW);
    delay(500);
    return;
  }

  //citire si construire UID
  String scannedUID = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    scannedUID += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "") + String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) scannedUID += " ";
  }

  Serial.print("Scanned UID: "); Serial.println(scannedUID);
  bluetooth.print("Scanned UID: "); bluetooth.println(scannedUID);

  //verificare card
  bool isAuthorized = false;
  for (int i = 0; i < numberOfAuthorizedUids; i++) {
    if (scannedUID.equalsIgnoreCase(authorizedUIDs[i])) {
      isAuthorized = true;
      break;
    }
  }

  //autorizare acces
  if (isAuthorized) {
    tone(BUZZER_PIN, 1000, 200);//buzerul bipaie
    myServo.attach(SERVO_PIN);
    myServo.write(90);//usa se deschide
    Serial.println("Status: Present");
    bluetooth.println("Status: Present");
    delay(5000);
    myServo.write(0);//usa se inchide
    delay(300);
    myServo.detach();
  } else {
    tone(BUZZER_PIN, 500, 200);//buzerul  bipaie mai incet
    myServo.attach(SERVO_PIN);
    myServo.write(0);//usa nu se deschide
    delay(300);
    myServo.detach();
    Serial.println("Status: Absent");
    bluetooth.println("Status: Absent");
  }

  delay(3000);
  rfid.PICC_HaltA();

  //******************status final pentru led********************
  digitalWrite(STATUS_LED, systemOK ? HIGH : LOW);
}
 