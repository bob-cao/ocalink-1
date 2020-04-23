// ------------------------------VENTILATOR CONTROL---------------------------------- //

#include "includes.h"

void breath_cycle_timer_reset(bool hardreset = false)
{
  CurrTimeInCycleMilliseconds = 0;
  CycleStartTimeFromSysClockMilliseconds = millis();
  if(hardreset)
  {
    ControlLoopStartTimeMilliseconds = CycleStartTimeFromSysClockMilliseconds;
  }
}

void blower_esc_init (void)
{
  pinMode(BLOWER_SPEED_PIN, OUTPUT);
  digitalWrite(BLOWER_SPEED_PIN, LOW);
  blower.attach(BLOWER_SPEED_PIN);

  // Hold throttle LOW and toggle for ESC to initialize properly
  blower.writeMicroseconds(BLOWER_DRIVER_MIN_PULSE_MICROSECONDS);

  delay(DEFAULT_ESC_INIT_TIME*0.90);

  blower.writeMicroseconds((BLOWER_DRIVER_MIN_PULSE_MICROSECONDS+BLOWER_DRIVER_MAX_PULSE_MICROSECONDS)/2);

  delay(DEFAULT_ESC_INIT_TIME*0.10);

  blower.writeMicroseconds(BLOWER_DRIVER_MIN_PULSE_MICROSECONDS);
}

void AlarmLEDInit(void)
{
  AlarmLED.begin();
}

static void chase(uint32_t primary, uint32_t secondary, int cycleDelay)
{
  for(uint16_t i = 0; i < AlarmLED.numPixels() + 4; i++)
  {
    AlarmLED.setPixelColor(i  , primary); // Draw new pixel
    AlarmLED.setPixelColor(i - 8, secondary); // Erase pixel a few steps back
    AlarmLED.show();
    delay(cycleDelay);
  }
}

void alarms_init(void)
{
  AlarmLEDInit();

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // Buzzer is a piezo with a built in driver

  pinMode(BATTERY_ALARM_PIN, INPUT);  // Battery UPS backup has a NC relay
}

void pinch_valve_init (void)
{
  pinMode(PINCH_VALVE_PIN, OUTPUT);
  pinch_valve.attach(PINCH_VALVE_PIN);
  pinch_valve.writeMicroseconds(PINCH_VALVE_DRIVER_FULL_OPEN_PULSE_MICROSECONDS);
}

void pressure_sensors_init (void)
{
  Wire.begin();  // Each pressure sensor will be unique I2C addresses based on MPN
  gagePressure.setPressureUnit(AllSensors_DLHR::PressureUnit::IN_H2O);
  gagePressure.startMeasurement(AllSensors_DLHR::AVERAGE4);
}

void pid_init (void)
{
  Blower_PID.SetMode(AUTOMATIC);  // Set PID Mode to Automatic, may change later
  Blower_PID.SetOutputLimits(MIN_PERCENTAGE, MAX_PERCENTAGE);
  Blower_PID.SetSampleTime(DEFAULT_PID_SAMPLE_TIME);

  PinchValve_PID.SetMode(AUTOMATIC);  // Set PID Mode to Automatic, may change later
  PinchValve_PID.SetOutputLimits(MIN_PERCENTAGE, MAX_PERCENTAGE);
  PinchValve_PID.SetSampleTime(DEFAULT_PID_SAMPLE_TIME);

}

double get_pressure_reading (void)
{
  if(!gagePressure.readData(false))
  {
    // Get a pressure reading and convert to units of cmH2O
    if( gagePressure.pressure > 0 )
       pressure_reading = gagePressure.pressure * INCHES_2_CM;
    gagePressure.startMeasurement();
  }
  return pressure_reading;
}

void get_values_from_raspberry_pi (void)
{
  // TODO: HIGH Break out serial protocol into seperate file
  // TODO: HIGH ensure serial operations do not dirupt control loop
  // TODO: Add error checking for data out of range
  // TODO: Ensure that all fields are populated before going into running test

  // $<property_name>,<argument_value><LF>

  if (Serial.available())
  {
    string_from_pi = Serial.readStringUntil('*');
    if(string_from_pi[0] == '$')
    {
      property_name = string_from_pi.substring(string_from_pi.indexOf('$') + 1, string_from_pi.indexOf(','));
      argument = string_from_pi.substring(string_from_pi.indexOf(',') + 1, string_from_pi.indexOf('*'));

      if(isDigit(argument[0]))
      {
        argument_value = argument.toFloat();
      }
      else
      {
        argument_value = NAN;
      }
      if(property_name.equalsIgnoreCase("PIP") && !isnan(argument_value))
      {
        if(argument_value >= PIP_MIN_RECEIVE && argument_value <= PIP_MAX_RECEIVE)
        {
          PipPressureCentimetersH2O = argument_value;  // PIP Value
          Serial.print("PIP: ");
          Serial.println(PipPressureCentimetersH2O);
        }
        else
        {
          Serial.println("PIP INPUT OUT OF BOUNDS!");
        }
      }
      else if(property_name.equalsIgnoreCase("PEEP") && !isnan(argument_value))
      {
        if(argument_value >= PEEP_MIN_RECEIVE && argument_value <= PEEP_MAX_RECEIVE)
        {
          PeepPressureCentimetersH2O = argument_value;  // PEEP Value
          Serial.print("PEEP: ");
          Serial.println(PeepPressureCentimetersH2O);
        }
        else
        {
          Serial.println("PEEP INPUT OUT OF BOUNDS!");
        }
      }
      // else if(property_name.equalsIgnoreCase("FIO2") && !isnan(argument_value))
      // {
      //   double fio2_requested = argument_value/FIO2_MULTIPLIER;  // Multipled by FIO2_MULTIPLIER on Raspberry Pi
      //   if(fio2_requested >= FIO2_MIN_RECEIVE && fio2_requested <= FIO2_MAX_RECEIVE)
      //   {
      //     FlowOfOxygen = fio2_requested;  // Flow of O2 in %
      //     Serial.print("FIO2: ");
      //     Serial.println(FlowOfOxygen);
      //   }
      // }
      else if(property_name.equalsIgnoreCase("TRISE") && !isnan(argument_value))
      {
        double trise_requested = argument_value/TRISE_MULTIPLIER;  // Multipled by TRISE_MULTIPLIER on Raspberry Pi
        if(trise_requested >= TRISE_MIN_RECEIVE && trise_requested <= TRISE_MAX_RECEIVE)
        {
          InhaleRampDurationMilliseconds = trise_requested * SEC_TO_MS;  // Rise time in seconds
          Serial.print("TRISE: ");
          Serial.println(InhaleRampDurationMilliseconds);
        }
        else
        {
          Serial.println("TRISE INPUT OUT OF BOUNDS!");
        }
      }
      else if(property_name.equalsIgnoreCase("RR") && !isnan(argument_value))
      {
        if(argument_value >= RR_MIN_RECEIVE && argument_value <= RR_MAX_RECEIVE)
        {
          RespritoryRate = argument_value;  // Respritory Rate in breathes per minute
          Serial.print("RR: ");
          Serial.println(RespritoryRate);
        }
        else
        {
          Serial.println("RR INPUT OUT OF BOUNDS!");
        }
      }
      else if(property_name.equalsIgnoreCase("IE") && !isnan(argument_value))
      {
        double ie_requested = argument_value/IE_MULTIPLIER;  // Multipled by IE_MULTIPLIER on Raspberry Pi
        if(ie_requested >= IE_MIN_RECEIVE && ie_requested <= IE_MAX_RECEIVE)
        {
          InhalationExhalationRatio = ie_requested;  // Inhalation/Exhalation Ratio
          InhaleDurationMilliseconds = (BREATHS_PER_MINUTE_TO_SEC * SEC_TO_MS) / ((InhalationExhalationRatio + 1.0) * RespritoryRate);
          ExhaleDurationMilliseconds = (BREATHS_PER_MINUTE_TO_SEC * SEC_TO_MS * (1.0 - (1.0 / (InhalationExhalationRatio + 1.0)))) / RespritoryRate;
          Serial.print("IE: ");
          Serial.println(InhalationExhalationRatio);
          Serial.println("InhaleDurationMilliseconds: ");
          Serial.println(InhaleDurationMilliseconds);
          Serial.println("ExhaleDurationMilliseconds: ");
          Serial.println(ExhaleDurationMilliseconds);
        }
        else
        {
          Serial.println("IE INPUT OUT OF BOUNDS!");
        }
      }
      else if( property_name.equalsIgnoreCase("CMD") )
      {
        if(argument.equalsIgnoreCase("START") || argument.equalsIgnoreCase("STOP"))
        {
          if(argument.equalsIgnoreCase("STOP"))
          {
            Serial.println("TEST STOPPED, GOING INTO IDLE");  // Test was requested to stop, go to IDLE
            CurrCycleStep = IDLE;
          }
          else if(argument.equalsIgnoreCase("START"))
          {
            if(CurrCycleStep == IDLE)  // To start test, cycle state needs to be in IDLE
            {
              CurrCycleStep = EXHALE_HOLD;
              breath_cycle_timer_reset(true);
              Serial.println("TEST STARTED");
              Serial.print("PEEP: ");         Serial.print(PeepPressureCentimetersH2O);                     Serial.println("cmH20");
              Serial.print("PIP: ");          Serial.print(PipPressureCentimetersH2O);                      Serial.println("cmH20");
              // Serial.print("FIO2: ");         Serial.print(FlowOfOxygen);                                   Serial.println("cmH20");
              Serial.print("TRISE: ");        Serial.print(InhaleRampDurationMilliseconds);                 Serial.println("ms");
              Serial.print("RR: ");           Serial.print(RespritoryRate);                                 Serial.println("b/m");
              Serial.print("IE: ");           Serial.print((1.00 / InhalationExhalationRatio) * RATIO_TO_PERCENTAGE);    Serial.println("%");
            }
          }
          else
          {
            Serial.println("CMD CODE INPUT OUT OF BOUNDS!");  // Command code is out of bounds
          }
        }
      }
      else if( property_name.equalsIgnoreCase("A_STATE") )
      {
        if(argument_value == 0 || argument_value == 1)
        {
          if(argument_value == 1)
          {
            alarm_state = 1;
            Serial.println("ALARMS ARE ON!");  // Alarms are ON
          }
          else if(argument_value == 0)
          {
            alarm_state = 3;
            Serial.println("ALARMS ARE OFF AND CLEARED!");  // Clear Alarms
          }
        }
        else
        {
          Serial.println("ALARM STATE INPUT OUT OF BOUNDS!");  // Alarm out of bounds error
        }
      }
      else
      {
        Serial.println("UNKNOWN MESSAGE");  // A message starting with '$' was not valid
      }
    }
  }
}

void print_pid_setpoint_and_current_value(void)
{
  if( CurrCycleStep != IDLE )
  {
    if( millis() % 50 == 0 )
    {
      Serial.print(pressure_system_input);
      Serial.print(" ");
      switch(CurrCycleStep)
      {
        case INHALE_RAMP:
          Serial.print(CurrPressureSetpointCentimetersH2O);
        break;
        case INHALE_HOLD:
          Serial.print(PipPressureCentimetersH2O);
        break;
        case EXHALE_RAMP:
        case EXHALE_HOLD:
        case IDLE:
        default:
          Serial.print(PeepPressureCentimetersH2O);
        break;
      }
      Serial.print(" ");
      Serial.print(blower_output_speed_in_percentage);
      Serial.println();
    }
  }
}

void reset_blower_pid_integrator(void)
{
  Blower_PID.SetMode(MANUAL);
  blower_output_speed_in_percentage = 0;
  Blower_PID.SetMode(AUTOMATIC);
}

void write_calculated_pid_blower_speed(void)
{

  switch( CurrCycleStep)
  {
    case INHALE_RAMP:
    case INHALE_HOLD:
    case EXHALE_HOLD:
      // static float BlowerSpeedExhaleWeightedAverage = -1;
      // Set Blower_Kp, Blower_Ki, Blower_Kd and comute Pressure PID
      Blower_PID.SetTunings(Blower_Kp, Blower_Ki, Blower_Kd);
      Blower_PID.Compute();
      
    break;
    case IDLE:
    case EXHALE_RAMP:
      reset_blower_pid_integrator();
      blower_output_speed_in_percentage = 0;
    break;
  }
      // Output PID calcuslated 0-100% to motor
      blower_speed = map(blower_output_speed_in_percentage,
                        MIN_PERCENTAGE,
                        MAX_PERCENTAGE,
                        BLOWER_DRIVER_MIN_PULSE_MICROSECONDS,
                        BLOWER_DRIVER_MAX_PULSE_MICROSECONDS);
      blower.writeMicroseconds(blower_speed);
}

// Write percent openness to the pinch valve
// 0% = closed
// 100% = wide open
void write_pinch_valve_openness(double opennessPercentage)
{
  long pinchValvePulseLengthMicroseconds = 
  map(opennessPercentage,
      MIN_PERCENTAGE,
      MAX_PERCENTAGE,
      PINCH_VALVE_DRIVER_FULL_CLOSE_PULSE_MICROSECONDS,
      PINCH_VALVE_DRIVER_FULL_OPEN_PULSE_MICROSECONDS);
  pinch_valve.writeMicroseconds(pinchValvePulseLengthMicroseconds);
}

void pinch_valve_control(void)
{
  switch (CurrCycleStep)
  {
  case EXHALE_RAMP:
    pinch_valve_output_openness_in_percentage = 100.0;
    break;
  case EXHALE_HOLD:
    PinchValve_PID.SetTunings(PinchValve_Kp,PinchValve_Ki,PinchValve_Kd);
    PinchValve_PID.Compute();
    break;
  case INHALE_HOLD:
  case INHALE_RAMP:
    pinch_valve_output_openness_in_percentage = 0.0;
    break;
  default:
    pinch_valve_output_openness_in_percentage = 100.0;
    break;
  }

  write_pinch_valve_openness(pinch_valve_output_openness_in_percentage);  
}

void cycle_state_handler (void)
{
  // TODO: LOW Make sure clock overflow is handled gracefully
  if( CurrCycleStep != IDLE )
  {
    CurrTimeInCycleMilliseconds = millis()-CycleStartTimeFromSysClockMilliseconds;
    if(millis()-ControlLoopStartTimeMilliseconds > ControlLoopInitialStabilizationTimeMilliseconds)
    {
      if(CurrTimeInCycleMilliseconds <= InhaleRampDurationMilliseconds)
      {
        CurrCycleStep = INHALE_RAMP;
      }
      else if((InhaleRampDurationMilliseconds < CurrTimeInCycleMilliseconds) &&
              (CurrTimeInCycleMilliseconds <= InhaleDurationMilliseconds))
      { 
        CurrCycleStep = INHALE_HOLD;
      }
      else if((InhaleDurationMilliseconds < CurrTimeInCycleMilliseconds) &&
            (CurrTimeInCycleMilliseconds <= BreathCycleDurationMilliseconds))
      {
        if( pressure_system_input > (PeepPressureCentimetersH2O + 5) )
        {
          CurrCycleStep = EXHALE_RAMP;
        }
        else
        {
          CurrCycleStep = EXHALE_HOLD;
        }
      }
      else if(CurrTimeInCycleMilliseconds > BreathCycleDurationMilliseconds)
      {
        CurrCycleStep = INHALE_RAMP;
        breath_cycle_timer_reset();
      }
    }
    else // if idle == true
    {
      CurrCycleStep = EXHALE_HOLD;
    }
  }
}

// Compensation for DC offset in pressure achieved.
// Found by linear regression of the following experimental datapoints:
// Target Val	Setpoint Req'd
// 15       	16
// 20	        21
// 25	        27.9
// 30	        33.5
// 35	        38.5
// 40	        44.5
// 45	        49.25
// 50	        55

double linear_remap_setpoint_compensation(double setpoint)
{
  return (1.116785714*setpoint)-0.5892857143;
}

void cycle_state_setpoint_handler(void)
{
  // TODO: HIGH breakout into separate file and rewrite for readability
  // TODO: MEDIUM put gains in a header, not in the source

  //Recompute Setpoints
  switch(CurrCycleStep)
  {
    case INHALE_RAMP:
      Blower_Kp = mapf(PipPressureCentimetersH2O, 15, 45, 240, 1000);
      Blower_Ki = 0;
      Blower_Kd = mapf(PipPressureCentimetersH2O, 15, 45, 0.3, 24);
      CurrPressureSetpointCentimetersH2O = (((float)CurrTimeInCycleMilliseconds/(float)InhaleRampDurationMilliseconds)*(PipPressureCentimetersH2O-PeepPressureCentimetersH2O))+PeepPressureCentimetersH2O;
    break;
    case INHALE_HOLD:
      Blower_Kp = mapf(PipPressureCentimetersH2O, 15, 45, 2, 48);
      Blower_Ki = mapf(PipPressureCentimetersH2O, 15, 45, 0, 0);
      Blower_Kd = 1.25;
      CurrPressureSetpointCentimetersH2O = PipPressureCentimetersH2O;
    break;
    case EXHALE_RAMP:
      Blower_Kp=10, Blower_Ki=0, Blower_Kd=0.3;
    case EXHALE_HOLD:
    case IDLE:
      Blower_Kp=10, Blower_Ki=0, Blower_Kd=0.5;
    default:
      CurrPressureSetpointCentimetersH2O = PeepPressureCentimetersH2O;
    break;
  }

  CurrPressureSetpointCentimetersH2O = linear_remap_setpoint_compensation(CurrPressureSetpointCentimetersH2O);
}

void buzzer_toggle(void)
{
  static unsigned long lastBuzzerToggle = 0;
  if( millis()-lastBuzzerToggle > 500 )
  {
    digitalWrite(BUZZER_PIN, buzzer_state);
    buzzer_state = !buzzer_state;
    lastBuzzerToggle = millis();
  }
}

// ALARM CODES
// A	Battery Backup Activated Alarm
// B	Disconnect Alarm
// C	High PIP Alarm
// D	Low PIP Alarm
// E	High PEEP Alarm
// F	Low PEEP Alarm
// G	Apnea Alarm
// H	High/Low RR Alarm
// I	I:E Ratio Alarm

void alarms_handler(void)
{
  static unsigned long PrevAlarmTimePipError = 0;
  static unsigned long PrevAlarmTimePeepError = 0;
  static unsigned long PrevAlarmTimeDisconnectError = 0;

  // Ventilator specific alarms (battery backup activated)
  isBatteryActivated = digitalRead(BATTERY_ALARM_PIN);
  if(isBatteryActivated)
  {
    alarm_state = 1;
    Serial.write("$ALARMS,A*");  // BATTERY BACKUP ALARM
  }

  // Disconnect Alarm
  if((millis()-PrevAlarmTimeDisconnectError > DisconnectAlarmTimer)
      && (CurrCycleStep == EXHALE_HOLD && CurrCycleStep != INHALE_RAMP)
      && (pressure_system_input >= -peep_low_alarm
      && pressure_system_input <= peep_low_alarm))
  {
    // make sound and send Raspberry Pi alarm status flag
    alarm_state = 1;
    Serial.write("$ALARMS,B*");  // DISCONNECT ALARM
    PrevAlarmTimeDisconnectError = millis();
  }

  // High and Low PIP Alarms
  if((millis()-PrevAlarmTimePipError > PipAlarmTimer)
      && (CurrCycleStep == INHALE_HOLD && CurrCycleStep != EXHALE_RAMP && CurrCycleStep != INHALE_RAMP)
      && (pressure_system_input <= PipPressureCentimetersH2O - pip_alarm
      || pressure_system_input >= PipPressureCentimetersH2O + pip_alarm))
  {
    // make sound and send Raspberry Pi alarm status flag
    alarm_state = 2;

    if(pressure_system_input >= PipPressureCentimetersH2O + pip_alarm)
    {
      Serial.write("$ALARMS,C*");  // HIGH PIP ALARM
      // exhale immedietly to PEEP pressure and continue breathing cycle, don't reset alarm
      CurrCycleStep = EXHALE_RAMP;
    }
    else if(pressure_system_input <= PipPressureCentimetersH2O - pip_alarm)
    {
      Serial.write("$ALARMS,D*");  // LOW PIP ALARM
    }

    PrevAlarmTimePipError = millis();
  }

  // High and Low PEEP Alarms
  if((millis()-PrevAlarmTimePeepError > PeepAlarmTimer)
      && (CurrCycleStep == INHALE_HOLD && CurrCycleStep != EXHALE_RAMP && CurrCycleStep != INHALE_RAMP)
      && (pressure_system_input <= PeepPressureCentimetersH2O - peep_alarm
      || pressure_system_input >= PeepPressureCentimetersH2O + peep_alarm))
  {
    // make sound and send Raspberry Pi alarm status flag
    alarm_state = 2;

    if(pressure_system_input >= PeepPressureCentimetersH2O + peep_alarm)
    {
      Serial.write("$ALARMS,E*");  // HIGH PEEP ALARM
    }
    else if(pressure_system_input <= PeepPressureCentimetersH2O - peep_alarm)
    {
      Serial.write("$ALARMS,F*");  // LOW PEEP ALARM
    }

    PrevAlarmTimePeepError = millis();
  }
  else
  {
    alarm_state = 3;
  }

  // TODO: Add Apnea Alarm
  // error caused by no spontaneous breath for x amount of time
  // Time is user set, still deciding on venturi flow or intake pressure as trigger
  // if(millis()-PrevAlarmTimeApneaError > ApneaTimer)
  // {
  //   PrevAlarmTimeApneaError = millis();
  //   buzzer_toggle();
  //   Serial.write("$ALARMS,G*");  // APNEA ALARM
  // }

  // TODO: Add High/Low RR Alarm
  // if(millis()-PrevAlarmTimeRRError > RRTimer)
  // {
  //   PrevAlarmTimeRRError = millis();
  //   buzzer_toggle();
  //   Serial.write("$ALARMS,H*");  // High/Low RR ALARM
  // }

  // TODO: Add I:E ratio Alarm
  // if(millis()-PrevAlarmTimeIEError > IETimer)
  // {
  //   PrevAlarmTimeIEError = millis();
  //   buzzer_toggle();
  //   Serial.write("$ALARMS,I*");  // I:E Ratio ALARM
  // }
}

void alarm_audible_and_visual_select(void)
{
  switch (alarm_state)
  {
    case 1:
      chase(red, low_red, LED_ON_TIME); // red
      digitalWrite(BUZZER_PIN, HIGH);
      break;
    case 2:
      chase(amber, low_amber, LED_ON_TIME); // amber
      buzzer_toggle();
      break;
    case 3:
    default:
      chase(green, low_green, LED_ON_TIME); // green
      digitalWrite(BUZZER_PIN, LOW);
      break;
  }
}

void send_values_to_raspberry_pi(void)
{
  // Send values to Raspberry Pi
  // Serial.println();  // Populate with values once that is figured out
}

void setup()
{
  // Initializations
  blower_esc_init();
  alarms_init();
  pinch_valve_init();
  pressure_sensors_init();
  pid_init();

  // Start cycle state in IDLE state
  CurrCycleStep = IDLE;

  // Serial initialization
  #if SYSTEM__SERIAL_DEBUG__STATEMACHINE
  Serial.begin(DEFAULT_BAUD_RATE);
  #endif

  breath_cycle_timer_reset(true);
}

void loop()
{
  pressure_system_input = get_pressure_reading();

  cycle_state_handler();

  cycle_state_setpoint_handler();

  pinch_valve_control();

  write_calculated_pid_blower_speed();

  // print_pid_setpoint_and_current_value();

  alarms_handler();

  alarm_audible_and_visual_select();

  get_values_from_raspberry_pi();

  send_values_to_raspberry_pi();
}