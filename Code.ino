#include <LiquidCrystal.h>
#include <Servo.h>
#include <EEPROM.h>

// --- Hardware Pin Configurations ---
#define BTN_A_ROTATE   A0     
#define BTN_B_YES      A1     
#define BTN_C_NO       A2     
#define ADMIN_TOGGLE   A3     

#define RELAY_PUMP       6      
#define SERVO_COFFEE_PIN 12 
#define SERVO_TEA_PIN    13    
#define US_TRIG          8         
#define US_ECHO          9          

// --- System Defaults (Factory Settings) ---
const int DEFAULT_FLOW = 12;
const int DEFAULT_DIST = 15;
const byte MAGIC_BYTE  = 0xAA; 

// --- Live Variables (Loaded from EEPROM) ---
int flowRate;      
int targetDist;    

// --- EEPROM Mapping ---
#define EE_ADDR_STATS  0   
#define EE_ADDR_FLOW   2   
#define EE_ADDR_DIST   4   
#define EE_ADDR_INIT   6   

// --- UI Architectures ---
enum UserState : uint8_t { U_HOME, U_MENU, U_SIZE, U_CONFIRM, U_DISPENSE, U_WAIT_REMOVE };
enum AdminState : uint8_t { A_HOME, A_CHANGE_VALS, A_TEST, A_LOGS, A_STATS, A_CV_EDIT, A_US_DIAG };

UserState uState = U_HOME;
AdminState aState = A_HOME;

LiquidCrystal lcd(11, 10, 2, 3, 4, 5);
Servo servoCoffee; 
Servo servoTea;

// --- System State Variables ---
const char* drinks[] = {"Coffee", "Tea", "Water", "Nimbu"};
int drinkIndex = 0;
bool isLargeSize = true; 
bool screenNeedsUpdate = true;
bool isAdminMode = false;
unsigned long waitRemoveStartTime = 0; 

// Admin Menu Navigation Variables
int adminMenuIndex = 0;  
int cvMenuIndex = 0;       
int testMenuIndex = 0;   
int tempEditValue = 0;   

// --- ANTI-EMI AUTOMATIC RESET VARIABLES ---
unsigned long lastActivityTime = 0; 
const unsigned long EMI_RESET_INTERVAL = 5000; 

// --- Premium System Logging Buffers ---
const char* sysLogs[] = {
  "SYS INIT OK     ",
  "PUMP ENGINE RDY ",
  "CUP REMOVED ERR ",
  "RECOVERY ACTIVE ",
  "BREW SUCCESSFUL "
};
int activeLogIndex = 0;

byte customSolidBlock[8] = { B11111, B11111, B11111, B11111, B11111, B11111, B11111, B11111 };

void setup() {
  Serial.begin(9600);
  
  pinMode(BTN_A_ROTATE, INPUT_PULLUP);
  pinMode(BTN_B_YES, INPUT_PULLUP);
  pinMode(BTN_C_NO, INPUT_PULLUP);
  pinMode(ADMIN_TOGGLE, INPUT_PULLUP); 
  
  pinMode(US_TRIG, OUTPUT);
  pinMode(US_ECHO, INPUT);
  
  pinMode(RELAY_PUMP, OUTPUT);
  digitalWrite(RELAY_PUMP, HIGH); 

  servoCoffee.attach(SERVO_COFFEE_PIN);
  servoTea.attach(SERVO_TEA_PIN);
  servoCoffee.write(0); 
  servoTea.write(0);
  
  lcd.begin(16, 2);
  lcd.createChar(0, customSolidBlock); 

  byte initCheck;
  EEPROM.get(EE_ADDR_INIT, initCheck);
  if (initCheck != MAGIC_BYTE) {
    Serial.println(F("MEMORY: First Boot Detected. Formatting EEPROM..."));
    factoryReset(); 
  } else {
    EEPROM.get(EE_ADDR_FLOW, flowRate);
    EEPROM.get(EE_ADDR_DIST, targetDist);
    Serial.println(F("MEMORY: Custom Settings Loaded Successfully."));
  }
  
  Serial.println(F("======================================="));
  Serial.println(F("    PRAGYATMIKA OS V4.4 : PRODUCTION   "));
  Serial.println(F("======================================="));
  
  lcd.setCursor(0, 0); lcd.print(F("PRAGYATMIKA OS"));
  lcd.setCursor(0, 1); lcd.print(F("Premium Booting."));
  delay(1500);
  
  lastActivityTime = millis(); 
}

void loop() {
  // --- Asynchronous Self-Healing Anti-EMI Engine ---
  if (millis() - lastActivityTime >= EMI_RESET_INTERVAL) {
    // Execute reset only on baseline idle states to protect running processes
    if ((!isAdminMode && uState == U_HOME) || (isAdminMode && aState == A_HOME)) {
      recoverLCD();
      screenNeedsUpdate = true; 
      Serial.println(F("HEAL: Clean Home LCD Refresh Executed."));
    }
    lastActivityTime = millis(); 
  }

  // Admin Momentary Mode Toggle Handler
  if (buttonPressed(ADMIN_TOGGLE)) {
    isAdminMode = !isAdminMode; 
    lastActivityTime = millis(); 
    
    if (isAdminMode) {
      aState = A_HOME;
      adminMenuIndex = 0;
      screenNeedsUpdate = true;
      Serial.println(F("LOG: Admin Mode Locked."));
    } 
    else {
      uState = U_HOME;
      screenNeedsUpdate = true;
      Serial.println(F("LOG: User OS Restored."));
    }
  }

  if (isAdminMode) runAdminOS();
  else runUserOS();
}

// ==========================================
//               USER PLATFORM
// ==========================================
void runUserOS() {
  switch (uState) {
    case U_HOME:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("  PRAGYATMIKA   "));
        lcd.setCursor(0, 1); lcd.print(F("[Press B Start]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_B_YES)) { uState = U_MENU; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case U_MENU:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Drink: [A:Cycle]"));
        lcd.setCursor(0, 1); lcd.print(F("> ")); lcd.print(drinks[drinkIndex]);
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { drinkIndex = (drinkIndex + 1) % 4; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_B_YES)) { 
        lcd.setCursor(0, 0); lcd.print(F(">> Selected <<  "));
        delay(800); uState = U_SIZE; screenNeedsUpdate = true; lastActivityTime = millis();
      }
      if (buttonPressed(BTN_C_NO)) { uState = U_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case U_SIZE:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Size: [A:Change]"));
        lcd.setCursor(0, 1); lcd.print(isLargeSize ? F("> Full (150mL) ") : F("> Half (75mL)  "));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { isLargeSize = !isLargeSize; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_B_YES)) { 
        lcd.setCursor(0, 0); lcd.print(F(">> Confirmed << "));
        delay(800); uState = U_CONFIRM; screenNeedsUpdate = true; lastActivityTime = millis();
      }
      if (buttonPressed(BTN_C_NO)) { uState = U_MENU; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case U_CONFIRM:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(drinks[drinkIndex]);
        lcd.print(isLargeSize ? F(" 150mL") : F(" 75mL"));
        lcd.setCursor(0, 1); lcd.print(F("[B:Dispense C:N]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_B_YES)) {
        lastActivityTime = millis();
        if (isCupPresent()) uState = U_DISPENSE;
        else {
          lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("NO CUP DETECTED!"));
          lcd.setCursor(0, 1); lcd.print(F("Insert Vessel..."));
          delay(1500); screenNeedsUpdate = true; 
        }
      }
      if (buttonPressed(BTN_C_NO)) { uState = U_SIZE; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case U_DISPENSE:
      executeDispense();
      recoverLCD(); 
      uState = U_WAIT_REMOVE; 
      screenNeedsUpdate = true;
      lastActivityTime = millis(); 
      break;
      
    case U_WAIT_REMOVE:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(F("Drink Is Ready! "));
        lcd.setCursor(0, 1); lcd.print(F("Please Remove   "));
        screenNeedsUpdate = false;
        waitRemoveStartTime = millis(); 
      }

      static unsigned long lastSensorPrint = 0;
      if (millis() - lastSensorPrint > 1000) {
        Serial.print(F("DEBUG - Sensor sees: "));
        Serial.print(getDistance());
        Serial.println(F(" cm"));
        lastSensorPrint = millis();
      }

      if (!isCupPresent() || (millis() - waitRemoveStartTime > 10000)) { 
        lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("  THANK YOU! "));
        lcd.setCursor(0, 1); lcd.print(F("  Enjoy Drink "));
        delay(1500); uState = U_HOME; screenNeedsUpdate = true;
        lastActivityTime = millis();
      }
      break;
  }
}

// ==========================================
//          PREMIUM DISPENSE SYSTEM
// ==========================================
void executeDispense() {
  int targetVolume = isLargeSize ? 150 : 75;
  int rawTime = targetVolume * flowRate; 
  
  lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Brewing Premium "));
  
  if (drinkIndex == 0) {
    servoCoffee.write(90); delay(600); servoCoffee.write(0); delay(400);
  } else if (drinkIndex == 1) {
    servoTea.write(90); delay(600); servoTea.write(0); delay(400);
  }

  digitalWrite(RELAY_PUMP, LOW); // Inrush Current Spark Event
  
  for (int i = 0; i <= 100; i += 5) {
    if (!isCupPresent()) {
      digitalWrite(RELAY_PUMP, HIGH); // Emergency Cutoff Spike
      
      // --- INTERCEPT 1: Force rewrite registers on safety interrupt ---
      delay(50);
      recoverLCD(); 
      
      activeLogIndex = 2; 
      lcd.setCursor(0, 0); lcd.print(F("SAFETY PAUSE    "));
      lcd.setCursor(0, 1); lcd.print(F("Replace Cup!    "));
      while(!isCupPresent()) { delay(100); }
      activeLogIndex = 3; 
      
      lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Brewing Premium "));
      digitalWrite(RELAY_PUMP, LOW);
    }
    lcd.setCursor(0, 1);
    int dynamicBlocks = map(i, 0, 100, 0, 16);
    for (int cell = 0; cell < 16; cell++) {
      if (cell < dynamicBlocks) lcd.write(byte(0)); else lcd.print(F(" ")); 
    }
    delay(rawTime / 20);
  }
  
  lcd.setCursor(0, 0); lcd.print(F("Finalizing...   "));
  delay(450); 
  
  // -------------------------------------------------------------
  // --- INTERCEPT 2: PRIMARY RELAY COLLAPSE DISCHARGE SHIELD ---
  // -------------------------------------------------------------
  digitalWrite(RELAY_PUMP, HIGH); // Major back-EMF spike flies here
  
  delay(100);       // Wait for the electrical line transient oscillations to subside
  recoverLCD();     // Force target hardware register refresh
  
  activeLogIndex = 4; 
  screenNeedsUpdate = true;
  lastActivityTime = millis(); 
  
  delay(500); 
  int metrics;
  EEPROM.get(EE_ADDR_STATS, metrics);
  EEPROM.put(EE_ADDR_STATS, metrics + 1);
}

// ==========================================
//               ADMIN SYSTEM
// ==========================================
const char* adminTitles[] = {"1.Change Vals", "2.Test H-Ware", "3.System Logs", "4.Total Stats"};

void runAdminOS() {
  switch (aState) {
    case A_HOME:
      if (screenNeedsUpdate) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print(adminTitles[adminMenuIndex]);
        lcd.setCursor(0, 1); lcd.print(F("[A:Nx B:Go C:Ex]")); 
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { adminMenuIndex = (adminMenuIndex + 1) % 4; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_B_YES)) { 
        lastActivityTime = millis();
        if(adminMenuIndex == 0) { aState = A_CHANGE_VALS; cvMenuIndex = 0; }
        else if(adminMenuIndex == 1) { aState = A_TEST; testMenuIndex = 0; }
        else if(adminMenuIndex == 2) aState = A_LOGS;
        else if(adminMenuIndex == 3) aState = A_STATS;
        screenNeedsUpdate = true; 
      }
      if (buttonPressed(BTN_C_NO)) { isAdminMode = false; uState = U_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case A_CHANGE_VALS:
      if (screenNeedsUpdate) {
        lcd.clear(); lcd.setCursor(0, 0);
        if(cvMenuIndex == 0) lcd.print(F("Edit Flow Rate  "));
        else if(cvMenuIndex == 1) lcd.print(F("Edit Cup Depth  "));
        else lcd.print(F("Factory Reset   "));
        lcd.setCursor(0, 1); lcd.print(F("[A:Next B:Sel C:Bk]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { cvMenuIndex = (cvMenuIndex + 1) % 3; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_C_NO)) { aState = A_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_B_YES)) {
        lastActivityTime = millis();
        if (cvMenuIndex == 2) {
          factoryReset();
          lcd.clear(); lcd.print(F("RESET COMPLETE  ")); delay(1500);
          screenNeedsUpdate = true;
        } else {
          tempEditValue = (cvMenuIndex == 0) ? flowRate : targetDist;
          aState = A_CV_EDIT; screenNeedsUpdate = true;
        }
      }
      break;

    case A_CV_EDIT:
      if (screenNeedsUpdate) {
        lcd.clear(); lcd.setCursor(0, 0);
        if (cvMenuIndex == 0) { lcd.print(F("Flow: ")); lcd.print(tempEditValue); lcd.print(F(" ms/mL")); }
        else { lcd.print(F("Dist: ")); lcd.print(tempEditValue); lcd.print(F(" cm")); }
        lcd.setCursor(0, 1); lcd.print(F("[A:+  C:-  B:Ok]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { tempEditValue++; screenNeedsUpdate = true; lastActivityTime = millis(); } 
      if (buttonPressed(BTN_C_NO)) { tempEditValue--; screenNeedsUpdate = true; lastActivityTime = millis(); }     
      if (buttonPressed(BTN_B_YES)) {
        lastActivityTime = millis();
        if (cvMenuIndex == 0) { flowRate = tempEditValue; EEPROM.put(EE_ADDR_FLOW, flowRate); }
        else { targetDist = tempEditValue; EEPROM.put(EE_ADDR_DIST, targetDist); }
        aState = A_CHANGE_VALS; screenNeedsUpdate = true;
      }
      break;

    case A_TEST:
      if (screenNeedsUpdate) {
        lcd.clear(); lcd.setCursor(0, 0);
        if (testMenuIndex == 0)      lcd.print(F("1.Test Pump Rail"));
        else if (testMenuIndex == 1) lcd.print(F("2.Test Servo Cff"));
        else if (testMenuIndex == 2) lcd.print(F("3.Test Servo Tea"));
        else                         lcd.print(F("4.Ultrasonic Diag"));
        
        lcd.setCursor(0, 1); lcd.print(F("[A:Next B:Run C:Bk]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_A_ROTATE)) { testMenuIndex = (testMenuIndex + 1) % 4; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_C_NO)) { aState = A_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      if (buttonPressed(BTN_B_YES)) {
        lastActivityTime = millis();
        
        if (testMenuIndex == 0) {
          lcd.clear(); lcd.print(F("Testing Pump... "));
          digitalWrite(RELAY_PUMP, LOW); 
          delay(2000); 
          digitalWrite(RELAY_PUMP, HIGH); 
          
          // --- INTERCEPT 3: Hardware Test Manual Shield ---
          delay(100); 
          recoverLCD(); 
          screenNeedsUpdate = true;
        } 
        else if (testMenuIndex == 1) {
          lcd.clear(); lcd.print(F("Testing Coffee.."));
          servoCoffee.write(90); delay(600); servoCoffee.write(0);
          screenNeedsUpdate = true;
        } 
        else if (testMenuIndex == 2) {
          lcd.clear(); lcd.print(F("Testing Tea...  "));
          servoTea.write(90); delay(600); servoTea.write(0);
          screenNeedsUpdate = true;
        } 
        else {
          aState = A_US_DIAG;
          screenNeedsUpdate = true;
        }
      }
      break;

    case A_US_DIAG:
      runLiveUltrasonicDiagnostic();
      break;

    case A_LOGS: 
      if (screenNeedsUpdate) {
        lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Latest Fault Log"));
        lcd.setCursor(0, 1); lcd.print(sysLogs[activeLogIndex]); 
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_C_NO)) { aState = A_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;

    case A_STATS:
      if (screenNeedsUpdate) {
        int history; EEPROM.get(EE_ADDR_STATS, history);
        lcd.clear(); lcd.setCursor(0, 0); lcd.print(F("Lifetime Audits:"));
        lcd.setCursor(0, 1); lcd.print(history); lcd.print(F(" Cups [C:Back]"));
        screenNeedsUpdate = false;
      }
      if (buttonPressed(BTN_C_NO)) { aState = A_HOME; screenNeedsUpdate = true; lastActivityTime = millis(); }
      break;
  }
}

// ==========================================
//          DEEP SIGNAL DIAGNOSTICS
// ==========================================
void runLiveUltrasonicDiagnostic() {
  static unsigned long lastUpdate = 0;
  
  if (millis() - lastUpdate > 400 || screenNeedsUpdate) {
    lastUpdate = millis();
    screenNeedsUpdate = false;
    
    lcd.clear();
    
    // Test 1: Hard Power Fail Check (Floating Ground/VCC line)
    if (digitalRead(US_ECHO) == HIGH) {
      lcd.setCursor(0, 0); lcd.print(F("ERR: HARD FAIL  "));
      lcd.setCursor(0, 1); lcd.print(F("NO VCC OR GND!  "));
      return;
    }
    
    // Pulse Generation Sequence
    digitalWrite(US_TRIG, LOW);
    delayMicroseconds(4);
    digitalWrite(US_TRIG, HIGH);
    delayMicroseconds(12); 
    digitalWrite(US_TRIG, LOW);
    
    // Test 2: Trigger Line Pin Fault Check
    if (digitalRead(US_TRIG) == HIGH) {
      lcd.setCursor(0, 0); lcd.print(F("ERR: TRIG PULSE "));
      lcd.setCursor(0, 1); lcd.print(F("PIN SHORTED HIGH"));
      return;
    }
    
    long pulseDuration = pulseIn(US_ECHO, HIGH, 26000); 
    
    // Test 3 & 4: Missed Echo vs Open Infinity Space
    if (pulseDuration == 0) {
      delayMicroseconds(500);
      if (digitalRead(US_ECHO) == LOW) {
        lcd.setCursor(0, 0); lcd.print(F("ERR: MISSED ECHO"));
        lcd.setCursor(0, 1); lcd.print(F("ECHO WIRE CUT!  "));
      } else {
        lcd.setCursor(0, 0); lcd.print(F("WARN: OUT RANGE "));
        lcd.setCursor(0, 1); lcd.print(F("> 400cm / EMPTY "));
      }
    } 
    // Real-Time Successful Readout
    else {
      int liveDistance = pulseDuration * 0.034 / 2;
      lcd.setCursor(0, 0); lcd.print(F("LIVE SIGNAL TRACK"));
      lcd.setCursor(0, 1); lcd.print(F("Distance: "));
      lcd.print(liveDistance);
      lcd.print(F("cm  "));
    }
  }
  
  // Asynchronous escape listener for Diagnostic View
  if (digitalRead(BTN_C_NO) == LOW) {
    delay(50);
    if (digitalRead(BTN_C_NO) == LOW) {
      while(digitalRead(BTN_C_NO) == LOW);
      aState = A_TEST;
      screenNeedsUpdate = true;
      lastActivityTime = millis();
    }
  }
}

// ==========================================
//            SUBSYSTEM UTILITIES
// ==========================================
void factoryReset() {
  EEPROM.put(EE_ADDR_STATS, 0); 
  EEPROM.put(EE_ADDR_FLOW, DEFAULT_FLOW);
  EEPROM.put(EE_ADDR_DIST, DEFAULT_DIST);
  EEPROM.put(EE_ADDR_INIT, MAGIC_BYTE);
  flowRate = DEFAULT_FLOW;
  targetDist = DEFAULT_DIST;
}

void recoverLCD() {
  delay(150); 
  lcd.begin(16, 2); 
  delay(50); 
  lcd.createChar(0, customSolidBlock); 
  lcd.clear(); 
}

bool buttonPressed(uint8_t pin) {
  if (digitalRead(pin) == LOW) {
    delay(50); 
    if (digitalRead(pin) == LOW) {
      while(digitalRead(pin) == LOW); 
      delay(50);
      return true;
    }
  }
  return false;
}

bool isCupPresent() {
  int validSamples = 0;
  for (int i = 0; i < 4; i++) {
    int currentDistance = getDistance();
    if (currentDistance <= targetDist && currentDistance > 0) validSamples++; 
    delay(20); 
  }
  return (validSamples >= 2); 
}

int getDistance() {
  digitalWrite(US_TRIG, LOW);
  delayMicroseconds(5);
  digitalWrite(US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);
  
  long timeframe = pulseIn(US_ECHO, HIGH, 35000); 
  if (timeframe == 0) {
    delay(10);
    return 999; 
  }
  return timeframe * 0.034 / 2;
}
