#include <ESP8266WiFi.h>
#include <stdint.h>
#include <MCP3008.h>
 
//define pin connections
#define CS_PIN 15 // D8 // Why is D8 mapped to pin 0????
#define CLOCK_PIN D5 // 14
#define MOSI_PIN D7 // 13
#define MISO_PIN D6 // 12
 
MCP3008 adc(CLOCK_PIN, MOSI_PIN, MISO_PIN, CS_PIN);

const char* ssid = WIFI_SSID;
const char* password = WIFI_PASSWORD;

int ledPin = 2; // GPIO2

#define PUMP_OFF_PIN 16 // D0 
//#define PUMP_OFF_PIN 5 // D1

volatile int prev_time = 0;
volatile int flow_frequency; // Measures flow sensor pulses
//unsigned int l_hour; // Calculated litres/hour

unsigned char flowsensor = 4; // Sensor Input GPIO5 == D1, GPIO4 == D2
unsigned long currentTime;
unsigned long cloopTime;

#define DEBUG_ADC false
#define DEBUG_FLOW false
#define DEBUG_PUMP_STATE true

#define MIN_FLOW 100.0f
// Probably can be lower when it's not floating. 11.77V ~= 368
#define PUMP_ON_THRESHOLD 150

#define TESTING 1

#ifdef TESTING
  #define FLOW_BUFFER_SIZE 8
  #define FLOW_MIN_SAMPLES 8 // must be <= buffer size

  uint16_t flow_retry_period = 10;
  uint8_t flow_retry_max_attempts = 2;
#else
  #define FLOW_BUFFER_SIZE 256
  #define FLOW_MIN_SAMPLES 180 // must be <= buffer size

  uint16_t flow_retry_period = 10*60; // 10 minutes
  uint8_t flow_retry_max_attempts = 2;
#endif

// Can't currently control the pump directly, unless we add another relay...
//uint16_t prime_maintenance_period = 60*60; // one hour

struct PumpState {
  // ADC readings via optocoupler
  // currently readings are only sufficient to tell if on/off, as optocoupler is the wrong sensitivity
  // battery voltage
  uint16 battery_reading;
  // solar voltage
  uint16 solar_reading;
  // if water level base pump controller wants to turn pump on
  uint16 water_level_pump_on;
  
  // this controls relay that is normally closed,
  // if there is no water flow, or some other failure condition, we can disable the pump
  bool pump_override_off = false;
  long unsigned int last_flow_retry = 0;
  uint8_t flow_retry_attempts = 0;
  
  // Ring Buffer
  uint16 ring_buffer_len = 0;
  uint16 ring_buffer_start_idx = 0;
  
  float flow_ring_buffer[FLOW_BUFFER_SIZE] = { 0.0f };
  
};

PumpState pump_state;

WiFiServer server(80);

float average_flow_from_buffer(PumpState &ps) {
  float sum = 0.0f;
  for (int i = 0; i < pump_state.ring_buffer_len; i++) {
    int idx = (pump_state.ring_buffer_start_idx + i) % FLOW_BUFFER_SIZE;
    sum += pump_state.flow_ring_buffer[idx];
  }
  return sum / pump_state.ring_buffer_len;
}

void add_to_ring_buffer(PumpState &ps, float flow) {
  uint16 next_idx = (pump_state.ring_buffer_start_idx + pump_state.ring_buffer_len) % FLOW_BUFFER_SIZE;
  
  if (next_idx == pump_state.ring_buffer_start_idx) {
    if (pump_state.ring_buffer_len == 0) {
      pump_state.ring_buffer_len += 1;
    } else {
      // wrapping condition
      pump_state.ring_buffer_start_idx = (pump_state.ring_buffer_start_idx + 1) % FLOW_BUFFER_SIZE;
    }
  } else {
    pump_state.ring_buffer_len += 1;
  }
  pump_state.flow_ring_buffer[next_idx] = flow;
  
}

String pump_state_to_string(PumpState &ps) {
  String result;
  result += String(currentTime);
  result += String(" -> PS: battery ");
  result += String(pump_state.battery_reading);
  result += String(" solar ");
  result += String(pump_state.solar_reading);
  result += String(" wlpump ");
  result += String(pump_state.water_level_pump_on);
  result += String(" override_off ");
  result += String(pump_state.pump_override_off);
  result += String(" flowbuffer [");
  for (int i = 0; i < pump_state.ring_buffer_len; i++) {
    int idx = (pump_state.ring_buffer_start_idx + i) % FLOW_BUFFER_SIZE;
    result += String(pump_state.flow_ring_buffer[idx]) + ", ";
  }
  result += String("] lastflowretry ");
  result += String(pump_state.last_flow_retry);
  return result;
}

void ICACHE_RAM_ATTR rising() {
  //attachInterrupt(0, falling, FALLING);
  prev_time = micros();
  flow_frequency++;
}

void setup() {
  // Setup serial-output
  Serial.begin(115200);
  delay(10);

  WiFi.mode(WIFI_STA);

  // Pin 2 has an integrated LED - configure it, and turn it off
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);

  pinMode(PUMP_OFF_PIN, OUTPUT);
  digitalWrite(PUMP_OFF_PIN, LOW);

  // flowsensor setup
  pinMode(flowsensor, INPUT);
  attachInterrupt(digitalPinToInterrupt(flowsensor), rising, RISING);
  currentTime = millis();
  cloopTime = currentTime;
  //sei(); // Enable interrupts
  
  
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  // Set the hostname
  WiFi.hostname("web-blink");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Start the server
  server.begin();
  Serial.println("Server started");

  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  pinMode(0, INPUT);
  int result = digitalRead(0);
  Serial.print("Result is == ");
  Serial.println(result);

  Serial.println("pins ");
  Serial.print("clock ");
  Serial.print(CLOCK_PIN);
  Serial.print("\t");
  Serial.print("mosi ");
  Serial.print(MOSI_PIN);
  Serial.print("\t");
  Serial.print("miso ");
  Serial.print(MISO_PIN);
  Serial.print("\t");
  Serial.print("cs ");
  Serial.print(CS_PIN);
  Serial.println("");

}

void handleWifiClient(WiFiClient& client, PumpState &ps) {
  // Read the first line of the request
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();

  // Match the request
  int value = LOW;
  if (request.indexOf("/LED=ON") != -1) {
    digitalWrite(ledPin, LOW);
    value = HIGH;

    // reset the pump monitor
    pump_state.pump_override_off = false;
    pump_state.flow_retry_attempts = 0;
    // reset ring buffer
    pump_state.ring_buffer_start_idx = 0;
    pump_state.ring_buffer_len = 0;
  }
  if (request.indexOf("/LED=OFF") != -1) {
    digitalWrite(ledPin, HIGH);
    value = LOW;
  }

  // Return the response
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println(""); // do not forget this one
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");

  client.print("Led pin is now: ");

  if(value == HIGH) {
    client.print("On");
  } else {
    client.print("Off");
  }
  client.println("<br><br>");
  client.println("Click <a href=\"/LED=ON\">here</a> turn the LED on pin 2 ON<br>");
  client.println("Click <a href=\"/LED=OFF\">here</a> turn the LED on pin 2 OFF<br>");
  
  client.println("<br><br>");
  
  client.println(pump_state_to_string(pump_state));

  client.println("</html>");
}



void readADCState(PumpState &ps) {
  //int val = adc.readADC(0); // read Channel 0 from MCP3008 ADC (pin 1)
  //Serial.println("ADC == " + String(val));

  // 11.77V = 366
  ps.battery_reading = adc.readADC(0);
  ps.solar_reading = adc.readADC(1);
  ps.water_level_pump_on = adc.readADC(2);

  if (DEBUG_ADC) {
    for (int i=0; i<8; i++) {
      int val = adc.readADC(i);
      Serial.print(val);
      Serial.print("\t");
    }
    Serial.println("");
  }
  
}

float calculateFlow() {
  // Pulse frequency (Hz) = 4.8Q, Q is flow rate in L/min.
  float l_hour = (flow_frequency * 60 / 4.8); // (Pulse frequency x 60 min) / 4.8 = flowrate in L/hour
  
  if (DEBUG_FLOW) {
    Serial.print("flow freq = ");
    Serial.print(flow_frequency);
    Serial.print(", flow (l/hour) = ");
    Serial.println(l_hour);
  }

  flow_frequency = 0; // Reset Counter

  return l_hour;
}

WiFiClient client;

void loop() {
  
  if (client) {
    if(client.connected() && client.available())
    {
      // Wait until the client sends some data
      Serial.println("new client");
      handleWifiClient(client, pump_state);
      //delay(1);
      while (!client.flush(10)) {}
      client.stop();
    }
  } else {
    // Check if a client has connected
    WiFiClient c = server.available();
    if (c) {
      client = c;
    }
  }
  
  currentTime = millis();
  // Every second, read ADC values & calculate litres/hour
  if(currentTime >= (cloopTime + 1000))
  {
    cloopTime = currentTime; // Updates cloopTime

    readADCState(pump_state);

    float flow_l_per_hour = calculateFlow();
    
    add_to_ring_buffer(pump_state, flow_l_per_hour);

    if (pump_state.water_level_pump_on < PUMP_ON_THRESHOLD) {
      // If the pump isn't actually going, don't do anything...
      pump_state.pump_override_off = false;
      digitalWrite(PUMP_OFF_PIN, LOW);
      pump_state.flow_retry_attempts = 0;
      // reset ring buffer
      pump_state.ring_buffer_start_idx = 0;
      pump_state.ring_buffer_len = 0;
      Serial.println("pump is off");
      return;
    }

    float avg_flow = average_flow_from_buffer(pump_state);
    Serial.println("avg flow " + String(avg_flow));

    if (DEBUG_PUMP_STATE) {
      Serial.println("Before");
      Serial.println(pump_state_to_string(pump_state));
    }

    // required samples for measuring avg flow before turn on/off pump
    if (pump_state.ring_buffer_len >= FLOW_MIN_SAMPLES) {
      // change to basing decision off of average flow (but need to only include readings since turned on...)
      if (avg_flow > MIN_FLOW) { // && !pump_state.pump_override_off) {
        pump_state.pump_override_off = false;
        // also reset the number of retries
        pump_state.flow_retry_attempts = 0;

      } else if (avg_flow <= MIN_FLOW
        && !pump_state.pump_override_off
        ) {
        pump_state.pump_override_off = true;
        pump_state.last_flow_retry = currentTime;
        // reset ring buffer
        pump_state.ring_buffer_start_idx = 0;
        pump_state.ring_buffer_len = 0;
      }
    }

    if (pump_state.pump_override_off
      && currentTime >= pump_state.last_flow_retry + (flow_retry_period * 1000)
      && pump_state.flow_retry_attempts < flow_retry_max_attempts
      ) {
      // try enabling the pump again
      pump_state.pump_override_off = false;
      pump_state.flow_retry_attempts += 1;
      // reset ring buffer
      pump_state.ring_buffer_start_idx = 0;
      pump_state.ring_buffer_len = 0;
    }

    if (pump_state.pump_override_off) {
      digitalWrite(PUMP_OFF_PIN, HIGH);
    } else {
      digitalWrite(PUMP_OFF_PIN, LOW);
    }

    if (DEBUG_PUMP_STATE) {
      Serial.println("After");
      Serial.println(pump_state_to_string(pump_state));
    }
  }


}