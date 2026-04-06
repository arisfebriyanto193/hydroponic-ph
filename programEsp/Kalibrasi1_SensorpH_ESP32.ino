int pH_Value; 
float Voltage;

void setup() 
{ 
  Serial.begin(9600);
  pinMode(pH_Value, INPUT); 
} 
 
void loop() 
{ 
  pH_Value = analogRead(34); 
  Voltage = pH_Value * (3.3 / 4095.0); 
  Serial.println(Voltage); 
  delay(500); 
}