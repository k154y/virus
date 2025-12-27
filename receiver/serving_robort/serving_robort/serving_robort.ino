#include <AccelStepper.h>

// Define stepper driver type
#define DRIVER_TYPE 1

// Define stepper motors and their pins
AccelStepper motor1(DRIVER_TYPE, A0, A1);    
AccelStepper motor2(DRIVER_TYPE, A6, A7);
AccelStepper motor3(DRIVER_TYPE, 42, 44);
AccelStepper motor4(DRIVER_TYPE, 46, 48);

// Enable pins for TB6600 drivers
const int en1 = 4;
const int en2 = 7;
const int en3 = 10;
const int en4 = 13;

// Motor speed
const int speed = 400;

// Current motor command
char currentCommand = 'S';

void setup() {
  Serial.begin(9600);     // USB serial (Used for Python Communication)
  // Serial1.begin(9600); // Bluetooth serial - disabled as it caused confusion
  
  // Wait for serial to initialize
  delay(2000);
  
  Serial.println("🤖 ROBOT READY - A* NAVIGATION");
  Serial.println("Pin Configuration:");
  Serial.println("Motor1: A0, A1");
  Serial.println("Motor2: A6, A7"); 
  Serial.println("Motor3: 42, 44");
  Serial.println("Motor4: 46, 48");
  Serial.println("Enable: 4, 7, 10, 13");

  // Enable TB6600 drivers
  pinMode(en1, OUTPUT); digitalWrite(en1, LOW);
  pinMode(en2, OUTPUT); digitalWrite(en2, LOW);
  pinMode(en3, OUTPUT); digitalWrite(en3, LOW);
  pinMode(en4, OUTPUT); digitalWrite(en4, LOW);

  // Set motor speeds
  motor1.setMaxSpeed(speed);
  motor2.setMaxSpeed(speed);
  motor3.setMaxSpeed(speed);
  motor4.setMaxSpeed(speed);
  
  // Stop all motors
  stopMotors();
  
  // *** FIX: Send READY signal on the primary Serial port (USB) ***
  Serial.println("READY");
  Serial.println("READY - Waiting for commands...");
}

void loop() {
  // Read Command from primary USB Serial (or Bluetooth if connected to that COM port)
  if (Serial.available()) {
    char cmd = Serial.read();
    
    // Only process valid command characters
    if (cmd == 'F' || cmd == 'B' || cmd == 'L' || cmd == 'R' || cmd == 'S') {
      Serial.print("Received: '");
      Serial.print(cmd);
      Serial.println("'");
      
      currentCommand = cmd;
      handleCommand(cmd);
    }
  }

  // Move motors continuously
  switch(currentCommand) {
    case 'F':
    case 'B':
    case 'L':
    case 'R':
      motor1.runSpeed();
      motor2.runSpeed();
      motor3.runSpeed();
      motor4.runSpeed();
      break;
    case 'S':
      // The stopMotors() call is handled inside handleCommand, 
      // but keeping the case here prevents accidental motor running if currentCommand is 'S'
      break;
  }
}

void handleCommand(char c) {
  switch(c){
    case 'F':  // Forward
      motor1.setSpeed(speed);
      motor2.setSpeed(speed);
      motor3.setSpeed(-speed);
      motor4.setSpeed(speed);
      Serial.println("Command: FORWARD");
      Serial.println("ACK:F");  // *** FIX: ACK on primary Serial ***
      break;

    case 'B':  // Backward
      motor1.setSpeed(-speed);
      motor2.setSpeed(-speed);
      motor3.setSpeed(speed);
      motor4.setSpeed(-speed);
      Serial.println("Command: BACKWARD");
      Serial.println("ACK:B");  // *** FIX: ACK on primary Serial ***
      break;

    case 'L':  // Left
      motor1.setSpeed(-speed);
      motor2.setSpeed(speed);
      motor3.setSpeed(speed);
      motor4.setSpeed(speed);
      Serial.println("Command: LEFT");
      Serial.println("ACK:L");  // *** FIX: ACK on primary Serial ***
      break;

    case 'R':  // Right
      motor1.setSpeed(speed);
      motor2.setSpeed(-speed);
      motor3.setSpeed(-speed);
      motor4.setSpeed(-speed);
      Serial.println("Command: RIGHT");
      Serial.println("ACK:R");  // *** FIX: ACK on primary Serial ***
      break;

    case 'S':  // Stop
      stopMotors();
      Serial.println("Command: STOP");
      Serial.println("ACK:S");  // *** FIX: ACK on primary Serial ***
      break;

    default:   // Unknown command
      Serial.print("Unknown Command: '");
      Serial.print(c);
      Serial.println("'");
      Serial.print("ERROR:");
      Serial.println(c);
      stopMotors();
      break;
  }
}

void stopMotors() {
  motor1.setSpeed(0);
  motor2.setSpeed(0);
  motor3.setSpeed(0);
  motor4.setSpeed(0);
}
