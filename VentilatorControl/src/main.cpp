// ------------------------------VENTILATOR CONTROL---------------------------------- //

//TODO: LOW Refactor to have all variables extremely modular and not hard coded (later)

// ----------------------------------LIBRARIES--------------------------------------- //
#include "includes.h"
// ----------------------------------LIBRARIES--------------------------------------- //



// ----------------------------------DEBUGGING--------------------------------------- //
#define SYSTEM__SERIAL_DEBUG__STATEMACHINE 1
// ----------------------------------DEBUGGING--------------------------------------- //



// ----------------------------------CONSTANTS--------------------------------------- //
#define INCHES_2_CM 2.54f

#define DEFAULT_BAUD_RATE 256000

#define BLOWER_DRIVER_MIN_PULSE_MICROSECONDS (double)1000
#define BLOWER_DRIVER_MAX_PULSE_MICROSECONDS (double)2000
#define BLOWER_DRIVER_PULSE_STARTUP_WIGGLE   (double)100
#define DEFAULT_ESC_INIT_TIME 3000

#define PINCH_VALVE_DRIVER_FULL_OPEN_PULSE_MICROSECONDS (double)1450
#define PINCH_VALVE_DRIVER_FULL_CLOSE_PULSE_MICROSECONDS (double)1725

#define MIN_PERCENTAGE (double)0
#define MAX_PERCENTAGE (double)100

#define PINCH_VALVE_PIN 3
// #define SOLENOID_PIN 4
#define BLOWER_PIN 5

#define DEFAULT_PEEP 5.000000f
#define DEFAULT_PIP 20.000000f
#define DEFAULT_BM 10  // breaths per minute
#define DEFAULT_RISE 1000  // 1 second
#define DEFAULT_IE 2 // inhale/exhale ratio
#define DEFAULT_KP 1.000000
#define DEFAULT_KI 1.000000
#define DEFAULT_KD 0.000000

#define DEFAULT_INHALE_RAMP (uint32_t)250
#define DEFAULT_INHALE_DURATION (uint32_t)1500
#define DEFAULT_BREATH_CYCLE_DURATION (uint32_t)6000
#define DEFAULT_CONTROL_LOOP_INIT_STABILIZATION (uint32_t)3000

#define DEFAULT_PID_SAMPLE_TIME 1.5

typedef enum{
    INHALE_RAMP,
    INHALE_HOLD,
    EXHALE_RAMP,
    EXHALE_HOLD,
    IDLE
}BreathCycleStep;
BreathCycleStep CurrCycleStep;

Servo blower;
Servo pinch_valve;

AllSensors_DLHR_L60D_8 gagePressure(&Wire);
// ----------------------------------CONSTANTS--------------------------------------- //



// --------------------------------USER SETTINGS------------------------------------- //
double pressure_reading;
double blower_speed;

double PeepPressureCentimetersH2O = DEFAULT_PEEP;
double PipPressureCentimetersH2O = DEFAULT_PIP;

String string_from_pi;
String property_name;
double value;

double valve_position, valve_state;
// --------------------------------USER SETTINGS------------------------------------- //



// --------------------------------STATE TIMINGS------------------------------------- //
uint32_t CurrTimeInCycleMilliseconds; // Time since the start of the current breath cycle. Resets at the beginning of every breath cycle
uint32_t CycleStartTimeFromSysClockMilliseconds;  // Time that the current breath cycle started ( in terms of system clock millis() )
uint32_t ControlLoopStartTimeMilliseconds; // Time, in terms of millis(), the state machine last switched out of IDLE
uint32_t ControlLoopInitialStabilizationTimeMilliseconds = DEFAULT_CONTROL_LOOP_INIT_STABILIZATION; // Length of time after transitioning out of IDLE that the system waits before transitioning to INHALE_RAMP
uint32_t InhaleRampDurationMilliseconds = DEFAULT_INHALE_RAMP; // Length of the INHALE_RAMP period for a breath cycle. AKA Value of CurrTimeInCycleMilliseconds when the state changes to INHALE_HOLD .User configurable
uint32_t InhaleDurationMilliseconds = DEFAULT_INHALE_DURATION; // Combined length of the INHALE_RAMP and INHALE_HOLD periods. AKA Value of CurrTimeInCycleMilliseconds when the state changes to EXHALE. User configurable.
uint32_t BreathCycleDurationMilliseconds = DEFAULT_BREATH_CYCLE_DURATION; // Total length of breath cycle, AKA when cycle step resets to INHALE_RAMP and CurrTimeInCycleMilliseconds resets to 0

uint32_t TimeOfLastSolenoidToggleMilliseconds = 0; // Time, in terms of millis(), that the solenoid last changed states
// --------------------------------STATE TIMINGS------------------------------------- //



// --------------------------------PID SETTINGS-------------------------------------- //
// TODO: MEDIUM reorganize constants
// Pressure Controlled Blower PID
double pressure_system_input, blower_output_speed_in_percentage, pinch_valve_output_openness_in_percentage, CurrPressureSetpointCentimetersH2O;
//double Blower_Kp = DEFAULT_KP, Blower_Ki = DEFAULT_KI, Blower_Kd = DEFAULT_KD;
double Blower_Kp=10.010000, Blower_Ki=0, Blower_Kd=0.008000;
double PinchValve_Kp = 5, PinchValve_Ki = 0, PinchValve_Kd = 1;
PID Blower_PID(&pressure_system_input,
                &blower_output_speed_in_percentage,
                &CurrPressureSetpointCentimetersH2O,
                Blower_Kp, Blower_Ki, Blower_Kd, DIRECT);

PID PinchValve_PID(&pressure_system_input,
                &pinch_valve_output_openness_in_percentage,
                &CurrPressureSetpointCentimetersH2O,
                PinchValve_Kp, PinchValve_Kd, PinchValve_Kd, REVERSE);

// --------------------------------PID SETTINGS-------------------------------------- //



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
  pinMode(BLOWER_PIN, OUTPUT);
  digitalWrite(BLOWER_PIN, LOW);
  blower.attach(BLOWER_PIN);
  // Hold throttle LOW for ESC to initialize properly

  blower.writeMicroseconds(BLOWER_DRIVER_MIN_PULSE_MICROSECONDS);

  delay(DEFAULT_ESC_INIT_TIME*0.90);

  blower.writeMicroseconds((BLOWER_DRIVER_MIN_PULSE_MICROSECONDS+BLOWER_DRIVER_MAX_PULSE_MICROSECONDS)/2);

  delay(DEFAULT_ESC_INIT_TIME*0.10);

  blower.writeMicroseconds(BLOWER_DRIVER_MIN_PULSE_MICROSECONDS);
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
    pressure_reading = gagePressure.pressure * INCHES_2_CM;
    gagePressure.startMeasurement();
  }
  return pressure_reading;
}

void get_values_from_raspberry_pi (void)
{
    // TODO: HIGH Break out serial protocol into seperate file
  // TODO: HIGH ensure serial operations do not dirupt control loop
  // $<property_name> <value><LF>
  // PEEP	cmH20
  // PIP	cmH20
  // FI02	%
  // F	b/m
  // RISE	sec
  // I/E	ratio (denominator) <1,2,3>
  if (Serial.available())
  {
    string_from_pi = Serial.readStringUntil('*'); 
    if(string_from_pi[0] == '$')
    {
      property_name = string_from_pi.substring(string_from_pi.indexOf('$') + 1, string_from_pi.indexOf(' '));
      value = string_from_pi.substring(string_from_pi.indexOf(' ') + 1, string_from_pi.indexOf('*')).toFloat();
    }

    if(property_name.equalsIgnoreCase("PEEP"))
    {
      // PEEP Value
      PeepPressureCentimetersH2O = value;
    }

    else if(property_name.equalsIgnoreCase("PIP"))
    {
      // PIP Value
      PipPressureCentimetersH2O = value;
    }

    // else if(property_name == "FI02")
    // {
    //   // Flow of O2 in %
    // }

    // else if(property_name.equalsIgnoreCase("F"))
    // {
    //   // Breathes per minute in b/m
    // }

    else if(property_name.equalsIgnoreCase("Ramptime"))
    {
      // Rise time in seconds
      InhaleRampDurationMilliseconds = value;
    }

    else if(property_name.equalsIgnoreCase("InhaleTime"))
    {
      InhaleDurationMilliseconds = value;

      // arbitrarily default to having the ramp time be 25% of the inhale duration
      InhaleRampDurationMilliseconds = InhaleDurationMilliseconds*0.25;
    }

    else if(property_name.equalsIgnoreCase("BreathDuration"))
    {
      BreathCycleDurationMilliseconds = value;

      // if the I:E ratio exceeds 1:1 (e.g ~2:1), cap it at 1:1. The standards we are workign from  state an expected I:E of 1:1-1:3
      if( (InhaleDurationMilliseconds/BreathCycleDurationMilliseconds) >= 0.50 )
      {
        InhaleDurationMilliseconds = BreathCycleDurationMilliseconds*0.50;
        InhaleRampDurationMilliseconds = InhaleDurationMilliseconds*0.25;
      }
    }

    else if( property_name.equalsIgnoreCase("Start") )
    {
      if(CurrCycleStep == IDLE)
      {
        CurrCycleStep = EXHALE_HOLD;
        breath_cycle_timer_reset(true);
        Serial.println("Test Started");
        Serial.print("PEEP: ");              Serial.print(PeepPressureCentimetersH2O);                  Serial.println("cmH20");
        Serial.print("PIP: ");               Serial.print(PipPressureCentimetersH2O);                   Serial.println("cmH20");
        Serial.print("Breathcycle Duration: ");   Serial.print(BreathCycleDurationMilliseconds) ;  Serial.println("ms");
        Serial.print("Inhale Duration: ");   Serial.print(InhaleDurationMilliseconds) ;            Serial.println("ms");
      }
    }

    else if( property_name.equalsIgnoreCase("Stop") )
    {
      Serial.println("Test Stopped");
      CurrCycleStep = IDLE;
    }

    else if(  property_name.equalsIgnoreCase("blowerkp"))
    {
      Serial.print("Blower kp changed to ");
      Serial.println(value);
      Blower_Kp = value;
    }

    else if(  property_name.equalsIgnoreCase("blowerkd"))
    {
      Serial.print("Blower kd changed to ");
      Serial.println(value);
      Blower_Kd = value;
    }

    else if(  property_name.equalsIgnoreCase("blowerki"))
    {
      Serial.print("Blower ki changed to ");
      Serial.println(value);
      Blower_Ki = value;
    }
    
    else if(  property_name.equalsIgnoreCase("PinchValvekp"))
    {
      Serial.print("PinchValve kp changed to ");
      Serial.println(value);
      PinchValve_Kp = value;
    }

    else if(  property_name.equalsIgnoreCase("PinchValveki"))
    {
      Serial.print("PinchValve ki changed to ");
      Serial.println(value);
      PinchValve_Ki = value;
    }

    else if(  property_name.equalsIgnoreCase("PinchValvekd"))
    {
      Serial.print("PinchValve kd changed to ");
      Serial.println(value);
      PinchValve_Kd = value;
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

void cycle_state_setpoint_handler(void)
{
  // TODO: HIGH breakout into separate file and rewrite for readability
  // TODO: MEDIUM put gains in a header, not in the source

  //Recompute Setpoints
  switch(CurrCycleStep)
  {
    case INHALE_RAMP:
      Blower_Kp=mapf(PipPressureCentimetersH2O, 15, 45, 240, 1000);
      Blower_Ki=0;
      Blower_Kd = mapf(PipPressureCentimetersH2O, 15, 45, .3, 24);
      CurrPressureSetpointCentimetersH2O = (((float)CurrTimeInCycleMilliseconds/(float)InhaleRampDurationMilliseconds)*(PipPressureCentimetersH2O-PeepPressureCentimetersH2O))+PeepPressureCentimetersH2O;
    break;
    case INHALE_HOLD:
      Blower_Kp = mapf(PipPressureCentimetersH2O, 25, 45, 2, 48);
      Blower_Ki = mapf(PipPressureCentimetersH2O, 25, 45, 0, 0);
      Blower_Kd= 0.1;
      CurrPressureSetpointCentimetersH2O = PipPressureCentimetersH2O*mapf(PipPressureCentimetersH2O, 15, 45, 1.2, 1);
    break;
    case EXHALE_RAMP:
      Blower_Kp=10, Blower_Ki=0, Blower_Kd=0.3;
    case EXHALE_HOLD:
    case IDLE:
      Blower_Kp=10, Blower_Ki=0, Blower_Kd=0.5;
    default:
      CurrPressureSetpointCentimetersH2O = PeepPressureCentimetersH2O*1.25;
    break;
  }
}

void setup()
{
  // Initializations
  blower_esc_init();
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

  print_pid_setpoint_and_current_value();

  get_values_from_raspberry_pi();

    // TODO: HIGH Implement alarms, with serial protocol to inform the GUI/HMI
}