/*
  AnalogReadSerial

  Reads an analog input on pin 0, prints the result to the Serial Monitor.
  Graphical representation is available using Serial Plotter (Tools > Serial Plotter menu).
  Attach the center pin of a potentiometer to pin A0, and the outside pins to +5V and ground.

  This example code is in the public domain.

  http://www.arduino.cc/en/Tutorial/AnalogReadSerial
*/

//volatile int pwm_value = 0;
//volatile int prev_time = 0;
volatile int flow_frequency; // Measures flow sensor pulses
unsigned int l_hour; // Calculated litres/hour
unsigned char flowsensor = 2; // Sensor Input
unsigned long currentTime;
unsigned long cloopTime;

//int pump_running_pin = 3;
int pump_state = 0;

 
// the setup routine runs once when you press reset:
void setup() {
  // initialize serial communication at 9600 bits per second:
  Serial.begin(9600);
  pinMode(flowsensor, INPUT);
  //pinMode(pump_running_pin, INPUT);
  // when pin D2 goes high, call the rising function
  // (interrupt is on digital pin 2)
  attachInterrupt(0, rising, RISING);
  sei(); // Enable interrupts
  currentTime = millis();
  cloopTime = currentTime;
}

void rising() {
  //attachInterrupt(0, falling, FALLING);
  //prev_time = micros();
  flow_frequency++;
}
 
/*void falling() {
  attachInterrupt(0, rising, RISING);
  pwm_value = micros()-prev_time;
}*/

// the loop routine runs over and over again forever:
void loop() {
  currentTime = millis();
  // Every second, calculate and print litres/hour
  if(currentTime >= (cloopTime + 1000))
  {
    cloopTime = currentTime; // Updates cloopTime
    // Pulse frequency (Hz) = 4.8Q, Q is flow rate in L/min.
    l_hour = (flow_frequency * 60 / 4.8); // (Pulse frequency x 60 min) / 4.8 = flowrate in L/hour
    flow_frequency = 0; // Reset Counter
    
    // read the battery level input on analog pin 0:
    int sensorValue = analogRead(A0); 

    // read if pump is "on"
    //pump_state = digitalRead(pump_running_pin);
    pump_state = analogRead(A1);

    // print data to serial
    String withScale = "0 1024 ";
    withScale += sensorValue;
    withScale += " ";
    withScale += l_hour;
    withScale += " ";
    withScale += pump_state;
    // print out the value you read:
    Serial.println(withScale);
  }
  delay(100);        // delay in between reads for stability
}
