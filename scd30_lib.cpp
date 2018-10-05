/****************************************************************
 * 
 * Based on original library from Sparkfun : 
 * 
 * This is a library written for the SCD30
 * SparkFun sells these at its website: www.sparkfun.com
 * Do you like this library? Help support SparkFun. Buy a board!
 * https://www.sparkfun.com/products/14751
 *
 * The SCD30 measures CO2 with accuracy of +/- 30ppm.
 *
 * This library handles the initialization of the SCD30 and outputs
 * CO2 levels, relative humidty, and temperature.
 ******************************************************************
 * October 2018 : Changed, enhanced and extended for raspberry Pi
 * by Paul van Haastrecht (paulvha@hotmail.com)
 * 
 * version 1.0 initial Raspberry Pi
 *  
 * Resources / dependencies:
 * BCM2835 library (http://www.airspayce.com/mikem/bcm2835/)
 * twowire library (https://github.com/paulvha/twowire)
 * 
 * *****************************************************************
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as 
 * published by the Free Software Foundation, either version 3 of the 
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 **********************************************************************/

#include "SCD30.h"

/*
 * 0 : no debug message
 * 1 : sending and receiving data
 * 2 : 1 + I2c protocol progress
 */
int SCD_DEBUG = 0;

// Global main info
float co2 = 0;
float temperature = 0;
float humidity = 0;

/* These track the staleness of the current data
 * This allows us to avoid calling readMeasurement() every time 
 * individual datums are requested */
 
bool co2HasBeenReported = true;
bool humidityHasBeenReported = true;
bool temperatureHasBeenReported = true;

/* global constructor for I2C (hardware of software) */ 
TwoWire TWI;

/* used as part of p_printf() */
int NoColor=0;

/* Constructor */
SCD30::SCD30(void)
{
    settings.sda = DEF_SDA;
    settings.scl = DEF_SCL;
    settings.I2C_interface = soft_I2C;
    settings.I2C_Address = SCD30_ADDRESS;
    settings.baudrate = SCD30_SPEED;
    settings.pullup = false;
}

/* Initialize the port and SCD30 
 * ASC = set / not set Auto Self Calibration
 * interval = continuous measurement every X seconds
 */
bool SCD30::begin(bool asc, uint16_t interval) {
    
    /* Enable internal BCM2835 pull-up resistors on the SDA and SCL
     * GPIO. BUT not on GPIO-2 and GPIO-3. The Raspberry has already 
     * external 1k8 pullup resistors on GPIO 2 and 3
     * 
     * The SCD30 does not seem to have them (looking at the scope signals)
     *
     * While this works it is better to have external resistors for 
     * signal quality. Hence pull-up is disabled by default.
     */
     
    if (settings.pullup) TWI.setPullup();
    
    /* initialize the I2C hardware */
    if (TWI.begin(settings.I2C_interface,settings.sda,settings.scl) != TW_SUCCESS){
        if (SCD_DEBUG > 0) p_printf(RED, (char *) "Can't setup i2c !\n");
        return(false);
    }
  
    /* set baudrate */
    TWI.setClock(settings.baudrate);
   
    /* The SCD30 is using clock stretching for especially after a read ACK
     * This is documented in the interface guide.
     * 
     * The MINIMUM needed for the SCD30 is 14ms (or a value of 1400). 
     * Setting it to max 200000 will allow for 200ms seconds as the interface 
     * guide states it could be up to 150ms once a day during calibration 
     */
    if (SCD_DEBUG > 0) p_printf(YELLOW, (char *) "setting clock stretching to 20000 (~200ms)\n");
    TWI.setClockStretchLimit(200000);
    
    /* if continuous measurement is requested */
    if (interval > 0)
    {
        /* Check for device to respond correctly */
        if(beginMeasuring() == true)     //Start continuous measurements
        {
            /* set interval for continuous measurement start it else stop */
            if (setMeasurementInterval(interval) == false) return(false); 
        }

        /* enable or disable Automatic Self Calibration       */
        return (setAutoSelfCalibration(asc));
     }
     else
     {
        return(StopMeasurement());
     }
}

/********************************************************************
 * close Hardware correctly on the Raspberry Pi
 * 
 * There is NO change to the values stored on the SCD30. That could
 * be added here if needed.
 ********************************************************************/
 
void SCD30::close(void) {
    TWI.close();
}

/************************************************************
 * Returns the latest available CO2 level.If the current level has 
 * already been reported, trigger a new read 
 ****************************************************************/
 
uint16_t SCD30::getCO2(void) {
    
  /* trigger new read if needed */  
  if (co2HasBeenReported == true) 
    readMeasurement(); //Pull in new co2, humidity, and temp into global vars

  co2HasBeenReported = true;

  return (uint16_t)co2; //Cut off decimal as co2 is 0 to 10,000
}

/********************************************************************
 * Returns the latest available humidity
 * If the current level has already been reported, trigger a new read 
 *********************************************************************/

float SCD30::getHumidity(void) {
  
  /* trigger new read if needed */  
  if (humidityHasBeenReported == true) 
    readMeasurement(); //Pull in new co2, humidity, and temp into global vars

  humidityHasBeenReported = true;

  return(humidity);
}

/*****************************************************************
 * Returns the latest available temperature in Celsius
 * If the cache has already been reported, perform a new read
 *****************************************************************/

float SCD30::getTemperature(void) {
  
  /* trigger new read if needed */    
  if (temperatureHasBeenReported == true)
    readMeasurement(); //Pull in new co2, humidity, and temp into global vars

  temperatureHasBeenReported = true;

  return(temperature);
}

/**********************************
 *  get temperature in Fahrenheit 
 **********************************/

float SCD30::getTemperatureF(void) {
    
    float output = getTemperature();
    output = (output * 9) / 5 + 32;

    return(output);
}

/****************************************
 * read serial number from SCD30
 *
 * format 9 digits
 *  digit 1
 *  digit 2
 *  crc
 *  digit 3
 *  digit 4
 *  crc
 *  digit 5
 *  digit 6
 *  crc
 *
 * provided val buffer must be defined at least 7 digits
 *  6 serial + 0x0 termination
 *
 * return
 *  true if OK (serial number in val-buffer)
 *  false in case of error
 ***********************************************/
 
bool SCD30::getSerialNumber(char *val) {
    
    uint8_t buff[10],data[2], crc;
    int     x, y;
    
    if (SCD_DEBUG >0)
       p_printf(YELLOW,(char *) "Reading serialnumber from I2C address 0x%x\n",SCD30_ADDRESS);

    if (sendCommand(CMD_READ_SERIALNBR) == false) return(false);

    // start reading
    if (readbytes((char *) buff, 9) == false) return(false);
 
    for (x = 0, y = 0 ; x < 9 ; x++)
    {

      switch (x)
      {
        case 0:             // skip CRC
        case 1:
        case 3:
        case 4:
        case 6:
        case 7:
          *val++ = buff[x];
          data[y++] = buff[x];
          break;

        case 2:             // handle CRC
        case 5:
        case 8:

          crc = computeCRC8(data,(uint8_t) 2);
          if (buff[x] != crc)
          {
            if (SCD_DEBUG > 1) p_printf(RED, (char *) "crc error : expected %x, got %x\n", crc, buff[x]);
            return(false);
          }
          y = 0;
          break;
       }
     }

     if (SCD_DEBUG > 0) printf("\n");
     *val = 0x0; // terminate
     return(true);
}

/*************************************************************
 *  Enables or disables the ASC See 1.3.6
 *
 * ASC status is saved in non-volatile memory. When the sensor is 
 * powered down while ASC is activated SCD30 will continue with 
 * automatic self-calibration after repowering without sending the command.
 *
 * At this moment it is not able to detect whether the self 
 * calibration has been done or finished
 ****************************************************************/

bool SCD30::setAutoSelfCalibration(bool enable) {
    
  if (enable)
    return(sendCommand(COMMAND_AUTOMATIC_SELF_CALIBRATION, 1)); //Activate continuous ASC
  else
    return(sendCommand(COMMAND_AUTOMATIC_SELF_CALIBRATION, 0)); //Deactivate continuous ASC
}

/*********************************************************
 *  Set the temperature offset. See 1.3.8.
 *
 * Temperature offset value is saved in non-volatile memory.
 * The last set value will be used for temperature offset compensation 
 * after repowering.
 *
 * All this does for now is lower the SCD30 temperature reading to with 
 * the offset value over a period of 10 min, while increasing the humidity 
 * readings. NO impact on the CO2 readings.
 *
 * The value can NOT be negative as it will cause uncontrolled 
 * temperature and humidity results.
 *
 *****************************************************************/

bool SCD30::setTemperatureOffset(float tempOffset) {
  
  /* can not be negative number */
  if (tempOffset < 0) return(false);

  int16_t tickOffset = tempOffset * 100;
  
  if (SCD_DEBUG > 0) p_printf(YELLOW,(char *) "set temperature offset %d\n",tickOffset);
  
  return (sendCommand(COMMAND_SET_TEMPERATURE_OFFSET, tickOffset));
}

/************************************************************
 *  Set the altitude compenstation. See 1.3.9.
 *
 * Setting altitude is disregarded when an ambient pressure is 
 * given to the sensor, Altitude value is saved in non-volatile memory. 
 * The last set value will be used for altitude compensation after repowering.
 *
 * Setting the argument to zero will deactivate the ambient pressure compensation
 ***************************************************************/
 
bool SCD30::setAltitudeCompensation(uint16_t altitude) {
  return(sendCommand(COMMAND_SET_ALTITUDE_COMPENSATION, altitude));
}

/***************************************************************
 * Set the pressure compensation. 
 * This is passed during measurement startup. mbar can be 700 to 1200
 *
 * Setting altitude is disregarded when an ambient pressure is 
 * given to the sensor, pressure value is saved in non-volatile memory. 
 * The last set value will be used for altitude compensation after repowering.
 *
 * Setting the argument to zero will deactivate the ambient pressure compensation
 ****************************************************************/

bool SCD30::setAmbientPressure(uint16_t pressure_mbar) {
  return (beginMeasuring(pressure_mbar));
}

/*****************************************************************
 *  Set Forced Recalibration value (FRC) see 1.3.7
 *
 * Setting a reference CO2 concentration by the here described 
 * method will always overwrite the settings from ASC (see chapter 1.3.6) 
 * and vice-versa. The reference CO2 concentration has to be within 
 * the range 400 ppm ≤ c ref (CO 2 ) ≤ 2000 ppm.
 ******************************************************************/

bool SCD30::setForceRecalibration(uint16_t val) {

    if(val < 400 || val > 2000) return(false);   //Error check
    
    if (SCD_DEBUG > 0) p_printf(YELLOW, (char *) "set forced calibration %d ppm\n", val);

    return (sendCommand(COMMAND_SET_FORCED_RECALIBRATION_FACTOR, val));
}

/*****************************************************************
 * Begins continuous measurements see 1.3.1
 *
 * Continuous measurement status is saved in non-volatile memory. 
 * When the sensor is powered down while continuous measurement mode 
 * is active SCD30 will measure continuously after repowering without 
 * sending the measurement command.
 * 
 * Returns true if successful
 *****************************************************************/

bool SCD30::beginMeasuring(uint16_t pressureOffset) {
  
  /* Error check */ 
  if(pressureOffset < 700 || pressureOffset > 1200) pressureOffset = 0; 
  
  if (SCD_DEBUG > 0) p_printf(YELLOW, (char *) "Begin measuring with pressure offset %d\n", pressureOffset);

  return(sendCommand(COMMAND_CONTINUOUS_MEASUREMENT, pressureOffset));
}

//Overload - no pressureOffset
bool SCD30::beginMeasuring(void) {
  return(beginMeasuring(0));
}

/*******************************************************************
 * Stop continuous measurement. see 1.3.2
 * 
 * return:
 *  true = OK
 *  false  = error
 *********************************************************************/

bool SCD30::StopMeasurement(void) {
  return(sendCommand(CMD_STOP_MEAS));
}

/*******************************************************************
 * Sets interval between measurements
 * 2 seconds to 1800 seconds (30 minutes)
 * 
 * return
 *  true = OK
 *  false  = error
 ******************************************************************/

bool SCD30::setMeasurementInterval(uint16_t interval) {
    
  if (interval < 2 || interval > 1800) return(false);
  
  if (SCD_DEBUG > 0) p_printf(YELLOW, (char *) "set measurement interval %d\n", interval);

  return(sendCommand(COMMAND_SET_MEASUREMENT_INTERVAL, interval));
}

/****************************************************************
 * Perform a single measurement
 * 
 * first call stopMeasurement() to stop any continuous reading which
 * starts automatically after power on (if set before last power-off)
 * 
 * return
 *  true = OK
 *  false  = error
 ******************************************************************/

bool SCD30::StartSingleMeasurement(void) {
  return(sendCommand(CMD_START_SINGLE_MEAS, 0x0000));
}

/****************************************************************
 * checks the data ready status register
 * 
 * Returns true when data is available. see 1.3.4
 ****************************************************************/

bool SCD30::dataAvailable() {
    
    uint8_t data[3];
    
    if (sendCommand(COMMAND_GET_DATA_READY) == false) return(false);
    
    /* start receiving */
    if (readbytes((char *) data, 3) == false) return(false);

    /* check CRC */
    if (checkCrc(data, 2, data[2]) == false) return(false);
    
    /* response[0] = MSB, response[1] = LSB */
    if (data[1] == 1) return(true);
    
    return(false);
}

////////////////////////////////////////////////////////////////////
///// low level routines ///////////////////////////////////////////
////////////////////////////////////////////////////////////////////

/*************************************
 * read amount of bytes from the SCD30
 *************************************/

bool SCD30::readbytes(char *buff, uint8_t len) {
    
    Wstatus result;
    int retry = 3;
    
    /* set slave address for SCD30 */
    TWI.setSlave(settings.I2C_Address);
    
    if (SCD_DEBUG > 0)
       p_printf(YELLOW, (char *) "read from I2C address 0x%x, %d bytes\n",settings.I2C_Address, len);
      
    while(1)
    {
        /* read results from I2C */
        result = TWI.i2c_read(buff, len);
        
        /* if failure, then retry as long as retrycount has not been reached */
        if (result != I2C_OK)
        {
            if (SCD_DEBUG > 1) p_printf(YELLOW, (char *) " read retrying %d\n", result);
            if (retry-- > 0) continue;
        }
 
        /* process result */
        switch(result)
        {
            case I2C_OK:
                return(true);
                
            case I2C_SDA_NACK :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "Read NACK error\n");
                return(false);
    
            case I2C_SCL_CLKSTR :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "Read Clock stretch error\n");
                return(false);
                
            case I2C_SDA_DATA :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "not all data has been read\n");
                return(false);
                
            default:
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "unkown return code\n");
                return(false);
        }
    }
}

/* check CRC for correct received information */
bool SCD30::checkCrc(uint8_t *data, uint8_t len, uint8_t crc_rec) {
      
      uint8_t crc = computeCRC8(data, len);
      
      if (crc_rec != crc)
      {
        if (SCD_DEBUG > 1) p_printf(RED, (char *) "crc error: expected %x, got %x\n", crc, crc_rec);
        return(false);
      } 
      
      return(true);
}
/***********************************************
 * Get 18 bytes from SCD30. see 1.3.5
 *
 * Updates global variables with floats
 * 
 * Returns true if success
 ***********************************************/
bool SCD30::readMeasurement(){
    
  uint8_t data[2];
  uint8_t buff[20];
  uint8_t y, x;
  uint32_t tempCO2 = 0;
  uint32_t tempHumidity = 0;
  uint32_t tempTemperature = 0;

    /* Verify we have data from the sensor */
    if (dataAvailable() == false)   return (false);
     
    if (sendCommand(COMMAND_READ_MEASUREMENT) == false) return(false);

    /* start receiving */
    if (readbytes((char *) buff, 18) == false) return(false);

    /* Parse buffer */
    for (x = 0, y =0  ; x < 18 ; x++)
    {

      switch (x)
      {
        case 0:     // MSB CO2
        case 1:
        case 3:     // lsb C02
        case 4:
          data[y++] = buff[x];     // for crc
          tempCO2 <<= 8;
          tempCO2 |= buff[x];
          
          if (SCD_DEBUG > 0)
          {
              if (x == 0)  printf(" CO2 : ");
              if (x == 4)  printf("0x%4x ", tempCO2);
          }
          
          break;
        case 6:
        case 7:
        case 9:
        case 10:
          data[y++] = buff[x];     // for crc
          tempTemperature <<= 8;
          tempTemperature |= buff[x];

          if (SCD_DEBUG > 0)
          {
              if (x == 6)  printf(" temperature : ");
              if (x == 10) printf("%d 0x%x ", tempTemperature & 0xfff, tempTemperature );
          }

          break;
        case 12:
        case 13:
        case 15:
        case 16:
          data[y++] = buff[x];     // for crc
          tempHumidity <<= 8;
          tempHumidity |= buff[x];
          
          if (SCD_DEBUG > 0)
          {
              if (x == 12)  printf(" humidity : ");
              if (x == 16)  printf("0x%4x\n", tempHumidity);
          }

          break;

        default:   // check CRC
          if (checkCrc(data, 2, buff[x]) == false) return(false);
          y = 0;
          break;
      }
    }
  
  /* Now copy the uint32s into their associated floats */
  memcpy(&co2, &tempCO2, sizeof(co2));
  memcpy(&temperature, &tempTemperature, sizeof(temperature));
  memcpy(&humidity, &tempHumidity, sizeof(humidity));

  /* Mark our global variables as fresh */
  co2HasBeenReported = false;
  humidityHasBeenReported = false;
  temperatureHasBeenReported = false;

  return (true); //Success! New data available in globals.
}

/************************************************************
 * Set for debugging the driver
 *
 * 0 = disable debug messages
 * 1 = sent/receive messages
 * 2 = like 1 + protocol errors
 *
 * This can be called BEFORE performing the begin() call.
 ************************************************************/

void SCD30::setDebug(int val) {
 
    SCD_DEBUG = val;
    
    // if level 2 enable I2C driver messages
    if (SCD_DEBUG == 2) TWI.setDebug(true);
}

/*******************************************************
 *  decode the command that is being sent 
 *******************************************************/
void SCD30::debug_cmd(uint16_t command) {
    
    p_printf(YELLOW, (char *) "Command 0x%x : ", command);

    switch(command)
    {
        case 0x0010:
            p_printf(YELLOW, (char *)"COMMAND_CONTINUOUS_MEASUREMENT");
            break;
        case 0x0104:
            p_printf(YELLOW, (char *)"CMD_STOP_MEAS");
            break;
        case 0x4600:
            p_printf(YELLOW, (char *)"COMMAND_SET_MEASUREMENT_INTERVAL");
            break;
        case 0x0202:
            p_printf(YELLOW, (char *)"COMMAND_GET_DATA_READY");
            break;
        case 0x300:
            p_printf(YELLOW, (char *)"COMMAND_READ_MEASUREMENT");
            break;
        case 0x5306:
            p_printf(YELLOW, (char *)"COMMAND_AUTOMATIC_SELF_CALIBRATION");
            break;
        case 0x5204:
            p_printf(YELLOW, (char *)"COMMAND_SET_FORCED_RECALIBRATION_FACTOR");
            break;
        case 0x5403:
            p_printf(YELLOW, (char *)"COMMAND_SET_TEMPERATURE_OFFSET");
            break;
        case 0x5102:
            p_printf(YELLOW, (char *)"COMMAND_SET_ALTITUDE_COMPENSATION");
            break;
        case 0xD033:
            p_printf(YELLOW, (char *)"CMD_READ_SERIALNBR");
            break;
        case 0xD025:
            p_printf(YELLOW, (char *)"CMD_READ_ARTICLECODE");
            break;
        case 0x0006:
            p_printf(YELLOW, (char *)"CMD_START_SINGLE_MEAS");
            break;
        default:
            p_printf(YELLOW, (char *)"COMMAND_UNKNOWN");
            break;
    }
}

/****************************************
 * Display the clock stretch info
 ****************************************/
void SCD30::DispClockStretch() {
    TWI.DispClockStretch();
}

/*******************************************************
 * Sends a command along with arguments and CRC
 * 
 * return
 *  true = OK
 *  false = error
 ********************************************************/
bool SCD30::sendCommand(uint16_t command, uint16_t arguments) {
    return(sendCommand(command, arguments, 5));
}

/**********************************************************
 * Sends just a command, no arguments, no CRC
 * return
 *  true = OK
 *  false = error
 **********************************************************/
bool SCD30::sendCommand(uint16_t command) {
    return(sendCommand(command, 0x0, 2));
}

/*******************************************************
 * Sends a command to SCD30
 * 
 *  if  len = 2 only command
 *  else command + arguments + CRC
 * 
 * return
 *  true = OK
 *  false = error
 ********************************************************/

bool SCD30::sendCommand(uint16_t command, uint16_t arguments, uint8_t len)
{
    uint8_t buff[5];
    int retry = 3;
    Wstatus result;
    
    /* set slave address for SCD30 */
    TWI.setSlave(settings.I2C_Address);

    if (SCD_DEBUG > 0)
    {
       p_printf(YELLOW, (char *) "sending to I2C address 0x%x, ",settings.I2C_Address);
       debug_cmd(command);
       
       if (len > 2)
        p_printf(YELLOW, (char *)", arguments 0x%x\n",arguments);
       else
        printf("\n");
    }

    buff[0] = (command >> 8); //MSB
    buff[1] = (command & 0xFF); //LSB
    
    /* add arguments and CRC */
    if (len > 2)
    {
        buff[2] = (arguments >> 8); //MSB
        buff[3] = (arguments & 0xFF); //LSB
        buff[4] = computeCRC8(&buff[2], 2); // Calc CRC on the arguments only, not the command
    }
    
    while (1)
    {
        // perform a write of data
        result = TWI.i2c_write((char *) buff, len);
    
        // if error, perform retry (if not exceeded)
        if (result != I2C_OK)
        {
            if (SCD_DEBUG > 1) printf(" send retrying %d\n", result);
            if (retry-- > 0) continue;
        }
  
        switch(result)
        {
            case I2C_OK:
                return(true);
            
            case I2C_SDA_NACK :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "write NACK error\n");
                return(false);

            case I2C_SCL_CLKSTR :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "write Clock stretch error\n");
                return(false);

            case I2C_SDA_DATA :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "write not all data has been sent\n");
                return(false);

            default :
                if (SCD_DEBUG > 1) p_printf(RED, (char *) "Unkown error during writing\n");
                return(false);
        }
    }
}

/************************************************************************
 * Given an array and a number of bytes, this calculate CRC8 for those bytes
 * CRC is only calc'd on the data portion (two bytes) of the four bytes being sent
 * From: http://www.sunshine2k.de/articles/coding/crc/understanding_crc.html
 * Tested with: http://www.sunshine2k.de/coding/javascript/crc/crc_js.html
 * x^8+x^5+x^4+1 = 0x31
 ***********************************************************************/
uint8_t SCD30::computeCRC8(uint8_t data[], uint8_t len)
{
  uint8_t crc = 0xFF; //Init with 0xFF

  for (uint8_t x = 0 ; x < len ; x++)
  {
    crc ^= data[x]; // XOR-in the next input byte

    for (uint8_t i = 0 ; i < 8 ; i++)
    {
      if ((crc & 0x80) != 0)
        crc = (uint8_t)((crc << 1) ^ 0x31);
      else
        crc <<= 1;
    }
  }

  return crc; //No output reflection
}

/*********************************************************************
 * Display in color
 * @param format : Message to display and optional arguments
 *                 same as printf
 * @param level :  1 = RED, 2 = GREEN, 3 = YELLOW 4 = BLUE 5 = WHITE
 * 
 *  if NoColor was set, output is always WHITE.
 *********************************************************************/
void p_printf(int level, char *format, ...) {
    
    char    *col;
    int     coll=level;
    va_list arg;
    
    //allocate memory
    col = (char *) malloc(strlen(format) + 20);
    
    if (NoColor) coll = WHITE;
                
    switch(coll)
    {
    case RED:
        sprintf(col,REDSTR, format);
        break;
    case GREEN:
        sprintf(col,GRNSTR, format);
        break;      
    case YELLOW:
        sprintf(col,YLWSTR, format);
        break;      
    case BLUE:
        sprintf(col,BLUSTR, format);
        break;
    default:
        sprintf(col,"%s",format);
    }

    va_start (arg, format);
    vfprintf (stdout, col, arg);
    va_end (arg);

    fflush(stdout);

    // release memory
    free(col);
}