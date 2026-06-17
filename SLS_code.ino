// 1. =================== LIBRARIES & DEFINITIONS ===================
#include <Keypad.h>
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#define GREEN_LED A0
#define RED_LED A1
#define BUZZER 13
#define SERVO_PIN 10

// 2. =================== GLOBAL VARIABLES ===================
LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo lockServo;

const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {4, 5, 6, 7};
byte colPins[COLS] = {8, 9, 11, 12};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

char password[5] = "1234";
char enteredPassword[5];
int inputIndex = 0;
int attempts = 0;
bool locked = false;
bool inCooldown = false;
bool isUnlocked = false;

volatile bool emergencyTriggered = false;
volatile bool overrideTriggered = false;

// 3. =================== UART COMMUNICATION ===================
void initUART() {
  UBRR0H = 0;
  UBRR0L = 103;           // 9600 baud @ 16MHz
  UCSR0C = 0x86;          // 8-bit, 1 stop bit, no parity
  UCSR0B = 0x08;          // TX enabled
}

void sendByte(char c) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = c;
}

void sendString(const char* s) {
  while (*s) {
    sendByte(*s++);
  }
}

// 4. =================== INTERRUPT SERVICE ROUTINES ===================
ISR(INT0_vect) {
  emergencyTriggered = true;
}

ISR(INT1_vect) {
  overrideTriggered = true;
}

// 5. =================== HARDWARE INITIALIZATION ===================
void initHardware() {
  lcd.init();
  lcd.backlight();
  lcd.print("Enter Password:");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);

  lockServo.attach(SERVO_PIN);
  lockServo.write(0);

  DDRD &= ~(1 << PD2) & ~(1 << PD3); // PD2, PD3 as input
  PORTD |= (1 << PD2) | (1 << PD3);  // Pull-ups

  EICRA |= (1 << ISC01) | (1 << ISC11); // Falling edge for INT0 and INT1
  EIMSK |= (1 << INT0) | (1 << INT1);   // Enable INT0, INT1

  sei(); // Enable global interrupts
  initUART(); // UART initialization
}

// 6. =================== ARDUINO SETUP & LOOP ===================
void setup() {
  initHardware();
}

void loop() {
  if (emergencyTriggered) {
    triggerEmergencyLock();
    emergencyTriggered = false;
  }

  if (overrideTriggered) {
    toggleLock();
    overrideTriggered = false;
  }

  if (locked || inCooldown) return;

  char key = keypad.getKey();
  handleKeyInput(key);
}

// 7. =================== PASSWORD LOGIC ===================
void handleKeyInput(char key) {
  if (!key) return;

  if (key >= '0' && key <= '9') {
    if (inputIndex < 4) {
      lcd.setCursor(inputIndex, 1);
      lcd.print('*');
      enteredPassword[inputIndex++] = key;
    }
  } else if (key == '*' && inputIndex == 4) {
    enteredPassword[4] = '\0';
    if (strcmp(enteredPassword, password) == 0) {
      accessGranted();
    } else {
      accessDenied();
    }
    inputIndex = 0;
    clearInput();
  } else if (key == '#' && inputIndex == 4) {
    enteredPassword[4] = '\0';
    if (strcmp(enteredPassword, password) == 0) {
      changePassword();
    } else {
      accessDenied();
    }
    inputIndex = 0;
    clearInput();
  }
}

void accessGranted() {
  sendString("Access Granted\r\n");
  digitalWrite(RED_LED, LOW);
  digitalWrite(GREEN_LED, HIGH);
  lcd.clear();
  lcd.print("Access Granted");
  lockServo.write(90);
  delay(5000);
  lockServo.write(0);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, HIGH);
  lcd.clear();
  lcd.print("Enter Password:");
  attempts = 0;
}

void accessDenied() {
  sendString("Access Denied\r\n");
  digitalWrite(BUZZER, HIGH);
  lcd.clear();
  lcd.print("Access Denied");
  delay(2000);
  digitalWrite(BUZZER, LOW);
  lcd.clear();
  lcd.print("Enter Password:");
  attempts++;
  if (attempts >= 3) startCooldown();
}

// 8. =================== COOLDOWN FUNCTION ===================
void startCooldown() {
  inCooldown = true;
  sendString("Cooldown started\r\n");
  for (int i = 60; i > 0; i--) {
    lcd.clear();
    lcd.print("Cooldown: ");
    lcd.print(i);
    lcd.print("s");
    delay(1000);
  }
  attempts = 0;
  inCooldown = false;
  lcd.clear();
  lcd.print("Enter Password:");
}

// 9. =================== PASSWORD CHANGE ===================
void changePassword() {
  lcd.clear();
  lcd.print("New Password:");
  char newPassword[5];
  int idx = 0;

  while (idx < 4) {
    char k = keypad.getKey();
    if (k >= '0' && k <= '9') {
      newPassword[idx] = k;
      lcd.setCursor(idx, 1);
      lcd.print('*');
      idx++;
    }
  }

  newPassword[4] = '\0';
  strcpy(password, newPassword);
  sendString("Password changed to: ");
  sendString(password);
  sendString("\r\n");
  lcd.clear();
  lcd.print("Password Updated");
  delay(2000);
  lcd.clear();
  lcd.print("Enter Password:");
}

// 10. =================== LOCK & EMERGENCY HANDLERS ===================
void toggleLock() {
  if (isUnlocked) {
    lockServo.write(0);
    lcd.clear();
    lcd.print("Locked Manually");
    digitalWrite(RED_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
    sendString("Manual Lock\r\n");
  } else {
    lockServo.write(90);
    lcd.clear();
    lcd.print("Unlocked Manual");
    digitalWrite(RED_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
    sendString("Manual Unlock\r\n");
  }
  isUnlocked = !isUnlocked;
  delay(2000);
  lcd.clear();
  lcd.print("Enter Password:");
}

void triggerEmergencyLock() {
  locked = true;
  lockServo.write(0);
  digitalWrite(BUZZER, HIGH);
  digitalWrite(RED_LED, HIGH);
  digitalWrite(GREEN_LED, LOW);
  lcd.clear();
  lcd.print("EMERGENCY LOCK");
  sendString("EMERGENCY LOCK\r\n");
  delay(5000);
  digitalWrite(BUZZER, LOW);
  startCooldown();
  locked = false;
}

// 11. =================== INPUT UTILITIES ===================
void clearInput() {
  for (int i = 0; i < 4; i++) enteredPassword[i] = '\0';
  lcd.setCursor(0, 1);
  lcd.print("                ");
  lcd.setCursor(0, 1);
}
