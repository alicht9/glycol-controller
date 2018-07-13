#include <DallasTemperature.h>
#include <EEPROM.h>
#include <LiquidCrystal.h>
#include <OneWire.h>

//What to display on boot up
const String welcome_line_1 = " Welcome to 3FREAKS";
const String welcome_line_2 = " Freak Responsibily!";

//How long to show the welcome message
const unsigned int welcome_delay  = 2; //seconds

//Screen headlines
const String stats_header   = "   CURRENT  TEMPS";
const String set_header     = "      SET MENU";

//How often should we take the temperature?
const unsigned int temp_delay = 60; //seconds

//How far off from our set temp do we care about?
const unsigned int temp_difference = 1; //Degrees

const unsigned int menu_button_pin  = 21;
const unsigned int exit_button_pin  = 20;
const unsigned int left_button_pin  = 19;
const unsigned int right_button_pin = 18;

const unsigned int tank_a_sensor_pin = 3;
OneWire one_wire_a(tank_a_sensor_pin);
DallasTemperature sensors_a(&one_wire_a);

const unsigned int tank_b_sensor_pin = 4;
OneWire one_wire_b(tank_b_sensor_pin);
DallasTemperature sensors_b(&one_wire_b);

const unsigned int tank_c_sensor_pin = 5;
OneWire one_wire_c(tank_c_sensor_pin);
DallasTemperature sensors_c(&one_wire_c);

float last_temp_on_screen = 39;

const unsigned int valve_a_enable_pin = 28;
const unsigned int valve_b_enable_pin = 29;
const unsigned int valve_c_enable_pin = 30;

const unsigned int rs = 38;
const unsigned int en = 37;
const unsigned int d4 = 36;
const unsigned int d5 = 35;
const unsigned int d6 = 34;
const unsigned int d7 = 33;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

bool new_state = false;

enum Display_states { 
  STATS,
  MAIN_MENU,
  HIGHLIGHT_TEMP_A,
  HIGHLIGHT_TEMP_B,
  HIGHLIGHT_TEMP_C,
  SET_TEMP_A,
  SET_TEMP_B,
  SET_TEMP_C
};
Display_states display_state = STATS;

const float default_temp = 38.0;
struct config_t {
  float tank_a_set_temp;
  float tank_b_set_temp;
  float tank_c_set_temp;
} program_config;

struct temp_state_t {
  long last_temp_time;
  float tank_a;
  float tank_b;
  float tank_c;
} current_temps;

/*
 *
 * 
 * EEPROM HELPERS
 *
 *
*/
template <class T> int eeprom_generic_write(int ee, const T& value) {
  const byte* p = (const byte*)(const void*)&value;
  int i;
  for (i=0; i < sizeof(value); i++) {
     EEPROM.write(ee++, *p++);
  }
  return i; //How much we've written
}

template <class T> int eeprom_generic_read(int ee, T& value) {
  byte* p = (byte*)(void*)&value;
  int i;
  for (i=0; i< sizeof(value); i++) {
    *p++ = EEPROM.read(ee++);
  }
  return i; //How much we've read
}

/*
 *
 * 
 * HELPERS
 *
 *
*/

// Tricky way to reset through code only
void(*die) (void) = 0;

char uppercase (char i) {
 //Assumes ASCII
 return i & ~(0x20);
}

//We have to debounce our inputs
//Damn physics...
volatile long last_interrupt_time = millis();
bool is_valid_interrupt() {
  long this_interrupt_time = millis();
  if (this_interrupt_time - last_interrupt_time > 200) {
    last_interrupt_time = millis();
    return true;
  }
  return false;
}

/*
 *
 * 
 * INIT CODE
 *
 *
*/
void setup() {
  lcd.begin(20, 4);
  print_welcome_message();

  eeprom_generic_read(0, program_config);

/*
 *
 * Aurdino Mega Interupts:
 *    Int | Pin
 *   -----------
 *     0     2
 *     1     3
 *     2     21
 *     3     20
 *     4     19
 *     5     18
 *
*/
  pinMode(menu_button_pin,  INPUT_PULLUP);
  pinMode(exit_button_pin,  INPUT_PULLUP);
  pinMode(left_button_pin,  INPUT_PULLUP);
  pinMode(right_button_pin, INPUT_PULLUP);
  attachInterrupt(2, handle_menu_button_ISR,  FALLING);
  attachInterrupt(3, handle_exit_button_ISR,  FALLING);
  attachInterrupt(4, handle_left_button_ISR,  FALLING);
  attachInterrupt(5, handle_right_button_ISR, FALLING);

  sensors_a.begin();
  sensors_b.begin();
  sensors_c.begin();

  pinMode(valve_a_enable_pin, OUTPUT);
  digitalWrite(valve_a_enable_pin, HIGH);
  pinMode(valve_b_enable_pin, OUTPUT);
  digitalWrite(valve_b_enable_pin, HIGH);
  pinMode(valve_c_enable_pin, OUTPUT);
  digitalWrite(valve_c_enable_pin, HIGH);

  current_temps.last_temp_time = -9999999;

  delay(welcome_delay * 1000);
  lcd.clear();
}

/*
 *
 * 
 * GLYCOL CONTROL
 *
 *
*/

void open_valve(char tank) {
  switch (tank) {
    case 'a':
      digitalWrite(valve_a_enable_pin, LOW);
      break;
    case 'b':
      digitalWrite(valve_b_enable_pin, LOW);
      break;
    case 'c':
      digitalWrite(valve_c_enable_pin, LOW);
      break;
  }
}

void close_valve(char tank) {
  switch (tank) {
    case 'a':
      digitalWrite(valve_a_enable_pin, HIGH);
      break;
    case 'b':
      digitalWrite(valve_b_enable_pin, HIGH);
      break;
    case 'c':
      digitalWrite(valve_c_enable_pin, HIGH);
      break;
  }
}

void open_valves_if_needed() {
  if (current_temps.tank_a - program_config.tank_a_set_temp > temp_difference) {
    open_valve('a');
  }

  if (current_temps.tank_b - program_config.tank_b_set_temp > temp_difference) {
    open_valve('b');
  }

  if (current_temps.tank_c - program_config.tank_c_set_temp > temp_difference) {
    open_valve('c');
  }
}

void close_valves_if_needed() {
  if (current_temps.tank_a - program_config.tank_a_set_temp < temp_difference) {
    close_valve('a');
  }

  if (current_temps.tank_b - program_config.tank_b_set_temp < temp_difference) {
    close_valve('b');
  }

  if (current_temps.tank_c - program_config.tank_c_set_temp < temp_difference) {
    close_valve('c');
  }
}

/*
 *
 * 
 * TEMPRATURE LOGIC
 *
 *
*/

bool take_temps_if_needed() {
  long current_time = millis();
  if (current_time - current_temps.last_temp_time  > (temp_delay * 1000)) {
    float tempC;
    float tempF;

    sensors_a.requestTemperatures();
    tempC = sensors_a.getTempCByIndex(0);
    tempF = sensors_a.toFahrenheit(tempC);
    current_temps.tank_a = tempF;

    
    sensors_b.requestTemperatures();
    tempC = sensors_b.getTempCByIndex(0);
    tempF = sensors_b.toFahrenheit(tempC);
    current_temps.tank_b = tempF;
 
    sensors_c.requestTemperatures();
    tempC = sensors_c.getTempCByIndex(0);
    tempF = sensors_c.toFahrenheit(tempC);
    current_temps.tank_c = tempF;
    
    return true;
  }
  return false;
}

double get_tank_temp(char tank) {
  float temp = 0;
  
  switch (tank) {
    case 'a':
      temp = current_temps.tank_a;
      break;
    case 'b':
      temp = current_temps.tank_b;
      break;
    case 'c':
      temp = current_temps.tank_c;
      break;
  }

  if (temp < -100)
    return 11.1;
  return temp;
}

void set_new_temp(char tank, float new_temp) {
  switch (tank) {
  case 'a':
    program_config.tank_a_set_temp = new_temp;
    eeprom_generic_write(0, program_config);
    break;
  case 'b':
    program_config.tank_b_set_temp = new_temp;
    eeprom_generic_write(0, program_config);
    break;
  case 'c':
    program_config.tank_c_set_temp = new_temp;
    eeprom_generic_write(0, program_config);
    break;
  default:
    // We should never get here
    die();
  }
  return;
}

double get_set_temp(char tank){
  double temp = 0;

  switch (tank) {
  case 'a':
    temp = program_config.tank_a_set_temp;
    break;
  case 'b':
    temp = program_config.tank_b_set_temp;
    break;
  case 'c':
    temp = program_config.tank_c_set_temp;
    break;
  default:
    // We should never get here
    die();
  }

  return temp;
}

/*
 *
 * 
 * VIEWS
 *
 *
*/

void print_welcome_message() {
  lcd.clear();
  lcd.setCursor(0,1);
  lcd.print(welcome_line_1);
  lcd.setCursor(0,2);
  lcd.print(welcome_line_2);
}

void print_stats() {
  if (new_state){
    lcd.clear();
    new_state = false;
  }
  int start_line = 0;
  lcd.setCursor(0, start_line);
  lcd.print(stats_header);
  start_line++;
  
  lcd.setCursor(0, start_line);
  lcd.print("A       B       C");
  start_line++;
  
  lcd.setCursor(0, start_line);
  lcd.print(get_tank_temp('a'), 1);
  
  lcd.setCursor(8, start_line);
  lcd.print(get_tank_temp('b'), 1);

  lcd.setCursor(16,start_line);
  lcd.print(get_tank_temp('c'), 1);

  lcd.setCursor(16,3);
  lcd.print("Menu");
}

void print_main_menu() {
  if (new_state){
    lcd.clear();
    new_state = false;
  }
  int start_line = 0;
  lcd.setCursor(0,start_line);
  lcd.print(set_header);
  start_line++;

  lcd.setCursor(0, start_line);
  lcd.print("A       B       C");
  start_line++;
  
  lcd.setCursor(0, start_line);
  lcd.print(get_set_temp('a'), 1);
  
  lcd.setCursor(8, start_line);
  lcd.print(get_set_temp('b'), 1);

  lcd.setCursor(16,start_line);
  lcd.print(get_set_temp('c'), 1);

  lcd.setCursor(0,3);
  lcd.print("Exit");
}

void print_highlight_temp(char tank) {
  if (new_state){
    lcd.clear();
    new_state = false;
  }
  lcd.setCursor(0,0);

  double set_temp = get_set_temp(tank);
  last_temp_on_screen = set_temp;

  String output = "Tank ";
  output.concat(uppercase(tank));
  output.concat(" Is Set To: ");
  lcd.print(output);

  lcd.setCursor(3,1);
  lcd.print(set_temp, 1);

  lcd.setCursor(0,3);
  lcd.print("Exit          Change");
  
  return;
}

void print_set_temp(char tank) {
  if (new_state){
    lcd.clear();
    new_state = false;
  }
  lcd.setCursor(0,0);

  String output = "Set Tank ";
  output.concat(uppercase(tank));
  output.concat(" To: ");
  lcd.print(output);


  lcd.setCursor(0,1);
  lcd.print("<<<");

  lcd.setCursor(8,1);
  lcd.print(last_temp_on_screen, 1);

  lcd.setCursor(17,1);
  lcd.print(">>>");

  lcd.setCursor(0,3);
  lcd.print("Exit             SET");
  
  return;
}


/*
 *
 * 
 * INPUT HANDLERS
 *
 *
*/

void handle_menu_button_ISR() {
  if (!is_valid_interrupt())
    return;
  new_state = true;

  switch (display_state) {
    case STATS:
      display_state = MAIN_MENU;
      break;
    case MAIN_MENU:
      break;
    case HIGHLIGHT_TEMP_A:
      last_temp_on_screen = get_set_temp('a');
      display_state = SET_TEMP_A;
      break;
    case HIGHLIGHT_TEMP_B:
      last_temp_on_screen = get_set_temp('b');
      display_state = SET_TEMP_B;
      break;
    case HIGHLIGHT_TEMP_C:
      last_temp_on_screen = get_set_temp('c');
      display_state = SET_TEMP_C;
      break;
    case SET_TEMP_A:
      set_new_temp('a', last_temp_on_screen);
      display_state = HIGHLIGHT_TEMP_A;
      break;
    case SET_TEMP_B:
      set_new_temp('b', last_temp_on_screen);
      display_state = HIGHLIGHT_TEMP_B;
      break;
    case SET_TEMP_C:
      set_new_temp('c', last_temp_on_screen);
      display_state = HIGHLIGHT_TEMP_C;
      break;
    default:
      // Should never get here
      die();
  }
  return;

}

void handle_exit_button_ISR() {
  if (!is_valid_interrupt())
    return;
  new_state = true;

  switch (display_state) {
    case STATS:
      break;
    case MAIN_MENU:
      display_state = STATS;
      break;
    case HIGHLIGHT_TEMP_A:
      display_state = STATS;
      break;
    case HIGHLIGHT_TEMP_B:
      display_state = STATS;
      break;
    case HIGHLIGHT_TEMP_C:
      display_state = STATS;
      break;
    case SET_TEMP_A:
      display_state = HIGHLIGHT_TEMP_A;
      break;
    case SET_TEMP_B:
      display_state = HIGHLIGHT_TEMP_B;
      break;
    case SET_TEMP_C:
      display_state = HIGHLIGHT_TEMP_C;
      break;
    default:
      // Should never get here
      die();
  }
  return;
}

void handle_left_button_ISR() {
  if (!is_valid_interrupt())
    return;
  new_state = true;

  switch (display_state) {
    case STATS:
      break;
    case MAIN_MENU:
      display_state = HIGHLIGHT_TEMP_C;
      break;
    case HIGHLIGHT_TEMP_A:
      display_state = MAIN_MENU;
      break;
    case HIGHLIGHT_TEMP_B:
      display_state = HIGHLIGHT_TEMP_A;
      break;
    case HIGHLIGHT_TEMP_C:
      display_state = HIGHLIGHT_TEMP_B;
      break;
    case SET_TEMP_A:
      last_temp_on_screen-=0.1;
      break;
    case SET_TEMP_B:
      last_temp_on_screen-=0.1;
      break;
    case SET_TEMP_C:
      last_temp_on_screen-=0.1;
      break;
    default:
      // Should never get here
      die();
  }
  return;
}

void handle_right_button_ISR() {
  if (!is_valid_interrupt())
    return;
  new_state = true;

  switch (display_state) {
    case STATS:
      break;
    case MAIN_MENU:
      display_state = HIGHLIGHT_TEMP_A;
      break;
    case HIGHLIGHT_TEMP_A:
      display_state = HIGHLIGHT_TEMP_B;
      break;
    case HIGHLIGHT_TEMP_B:
      display_state = HIGHLIGHT_TEMP_C;
      break;
    case HIGHLIGHT_TEMP_C:
      display_state = MAIN_MENU;
      break;
    case SET_TEMP_A:
      last_temp_on_screen+=0.1;
      break;
    case SET_TEMP_B:
      last_temp_on_screen+=0.1;
      break;
    case SET_TEMP_C:
      last_temp_on_screen+=0.1;
      break;
    default:
      // Should never get here
      die();
  }
  return;
}

/*
 *
 * 
 * MAIN
 *
 *
*/
void loop() {

  if (take_temps_if_needed()) {
   open_valves_if_needed();
   close_valves_if_needed();
  }

  switch (display_state) {
    case STATS:
    print_stats();
    break;
  case MAIN_MENU:
    print_main_menu();
    break;
  case HIGHLIGHT_TEMP_A:
    lcd.setCursor(0,0);
    print_highlight_temp('a');
    break;
  case HIGHLIGHT_TEMP_B:
    lcd.setCursor(0,0);
    print_highlight_temp('b');
    break;
  case HIGHLIGHT_TEMP_C:
    lcd.setCursor(0,0);
    print_highlight_temp('c');
    break;
  case SET_TEMP_A:
    lcd.setCursor(0,0);
    print_set_temp('a');
    break;
  case SET_TEMP_B:
    lcd.setCursor(0,0);
    print_set_temp('b');
    break;
  case SET_TEMP_C:
    lcd.setCursor(0,0);
    print_set_temp('c');
    break;
  default:
    //If we got here, then shit went really sideways somewhere
    //We should just reset the arduino and let it sort itself out
    lcd.clear();
    lcd.print("ERROR: Unhandled State");
    delay(1000);
    die();
  }
}
