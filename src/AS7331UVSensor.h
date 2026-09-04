#pragma once

///////////////////////////////////////////////////////////////////////////////
// AS7331 UV Sensor with UVA, UVB and UVC Sensors and I2C Interface
// 
// If you re-use this code, please include this copyright notice:
// Copyright (C) 2026.09.04, https://muman.ch and https://github/mumanchu
// All rights reversed, released under the terms of the WTF License
// For details see
// https://github.com/mumanchu/AS7331UVSensor
/*
For testing, the 'Sparkfun Mini Spectral UV Sensor' (SEN-23518) was used
https://docs.sparkfun.com/SparkFun_Spectral_UV_Sensor_AS7331/introduction/

The AS7331 contains three separate photodiode sensors, for UVA, UVB and UVC 
ranges. Sensors are read using 24-bit delta-sigma A/D converters, and raw 
16-bit values are returned for each sensor. It runs on 3.3V and has a 
standard I2C serial interface.

CONFIGURATION p49

The gain, internal clock frequency and the conversion time (the number of 
clocks per sample at the internal clock frequency) can be programmed, to 
provide a wide range sensitivities and resolutions. It has one 8-bit 
operating state register (OSR), and three 8-bit configuration registers 
CREG1..CREG3.

OPERATING STATES p15

The AS7331 has two operating states. In the CONFIGURATION state the 
configuration registers are accessed. In the MEASUREMENT state the 
measurement registers are accessed. Both sets of registers have the same 
numbering, so the wrong data will be read if it is in the wrong operating
state. After a reset(), it remains Powered Down in CCONFIGURATION mode.

ENERGY SAVING OPTIONS p23

It has two power-saving modes, Power Down and Standby.

MEASUREMENTS p17

Measurements can be started by an I2C command (CMD), continuous sampling
(CONT), or by the falling edge of an external SYN signal (SYNS=start signal, 
SYND=start and end signals) so readings can be synchronized with an external 
event.

POLLING p17

It is recommended not to send I2C messages while a conversion is in progress.
A timer or the chip's READY output can be used to determine when a new reading 
is available. Both these methods are illustrated in the example application.

DIVIDER p39

Although the ADC samples are 24-bits, the raw UV values read from the MRESx 
registers are only 16-bits. This is fine if the resolution is 10..16 bits, but 
for 11..24 bit resolutions only the LEAST SIGNIFICANT 16 bits are returned. 
This means that an overflow error could occur with certain configurations and 
the readings will be invalid. Because of this, a divider value 'div' can be 
configured to shift the 24-bit register so the otherwise unavailable upper 
8 bits are returned.


IRRADIANCE CALCULATIONS p30 'Transfer Function'

Equation 3 in the data sheet is used for the irradiance calculations, 
with full scale ranges (FSR) and LS bit significance calculated by 
calculateCoefficients(), which is called automatically whenever the
configuration is changed. Irradiance values are returned in microWatts-
per-square-centimeter, uW/cm2. 

The library also calculates the stndard UV Index based on the UVA and UVB 
values.

Internally calculated values (fsrX, lsbX, tconvI and nbitsI) are public 
and can be read by the application.

ADVANTAGES OF THIS LIBRARY

This library provides some unique features.

Calculations of the Full Scale Range (FSR) and LS bit significance for the 
full range of settings. The `calculateCoefficients()` method also detects 
invalid configurations. Calculations can be verified by comparing them with 
the tables in the data sheet p32..38 (these are for 1.024MHz only).

Handles overflow and low values below the configured sensitivity. It will 
not silently produce invalid results under these conditions.

Automatic Gain Control (AGC). If overflow or underflow occurs, the gain can 
be automatically adjusted to provide the full range of readings.

Window compensation factors (`wfacX`). These are multipliers that compensate 
for reductions caused by a transparent glass or plastic sensor cover, which 
is usually fitted to prevent dirt accumulating directly on the UV sensor. 
The `wfacX` values are easily calibrated by taking a reading without the 
cover, then a reading with the cover, and dividing the two. 
e.g. wfac 1.0=no cover, 1.25=with 25% loss. UVA, UVB and UVC may have 
different wfac values, depending on the cover material. These could also be 
used the reduce the values - the UVC readings from my sensor seem tobe way 
too high (maybe it's an out-of-spec sensor).

Documentation. Full details of each method can be read from the associated 
comments in the source code. There is no need to document them in more 
than one place, which risks the separate descriptions getting out-of-sync. 
Therefore, documentation generator tags (@class, @code etc.), which make 
the comments difficult to read, are not used. Note that most modern code 
editors will display this comment as a pop-up tooltip while you are editing 
the code.

I2C ADDRESSES

0x011101xx = 0x74..0x77 according to jumpers A1 and A0 (xx)

DATA SHEET, DS001047 v4-00 2023-Mar-24

All page numbers (pxx) reference *this version* of the document
https://look.ams-osram.com/m/1856fd2c69c35605/original/AS7331-Spectral-UVA-B-C-Sensor.pdf

THE OTHER AS7331 LIBRARIES, for reference

https://github.com/sparkfun/SparkFun_AS7331_Arduino_Library
https://github.com/adafruit/Adafruit_AS7331
https://github.com/RobTillaart/AS7331
*/

typedef unsigned int uint;		// uint is 16 or 32 bits
typedef unsigned long ulong;	// ulong is always 32 bits

// For debug checks and error reporting, #define DEBUG before #include "AS7331UVSensor.h"
#ifdef DEBUG
#define AS7331_PRINTLN(s) { Serial.println(s); Serial.flush(); }
#define AS7331_ASSERT(b, s) if (!(b)) { AS7331_PRINTLN(s); return false; }
#else
#define AS7331_PRINTLN(s)
#define AS7331_ASSERT(b, s)
#endif


class AS7331UVSensor
{
	TwoWire* wire;
	byte i2cAdds;

	// Shadow values
	byte osr = 0x42;	// operation state register OSR shadow, p49
	byte creg1 = 0xa6;	// config register 1 CREG1 shadow, p51/52
	byte creg2 = 0x40;	// config register 2 CREG2 shadow, p53/54
	byte creg3 = 0x40;	// config register 3 CREG3 shadow, p55

public:
	// Values initialised by setConfigCREGx()
	// do not write to these unless you are experimenting or testing the algoritms
	uint mmodeI = 1;	// conversion mode, 0=CONT, 1=CMD, 2-SYNS, 3=SYND
	uint gainI = 10;	// gain setting, 0=x2048, 1=x1024, 2=x512, .. 10=x2, 11=x1
	uint timeI = 6;		// number of clocks at frequency 'cclk', 0=2^10 .. 6=2^16 (65536) .. 14=2^24
	uint divI = 0;		// divider value, 0=disabled, 1=2, 2=4, 3=8, .. 8=256 (1=2^1 .. 8=2^8)
	uint cclkI = 0;		// internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz

	// Values initialized by calculateCoefficients()
	// do not write to these unless you are experimenting or testing the algoritms
	float fsrA, fsrB, fsrC;		// full scale range, microWatts-per-square centimeter, uW/cm2
	float lsbA, lsbB, lsbC;		// sigificance of LS bit, nanoWatts-per-square centimeter, nW/cm2
	uint tconvI;				// conversion time in milliseconds
	uint nbitsI;				// number of significant bits of conversion (in 24-bit OUTCONV reg)
	uint agcMaxI, agcMinI;

	// Window factors, set these if the sensor has a transparent cover, each 
	// has a different factor because the window may have different absorbtions
	// needs calibration, default=1.0 (no window glass)
	float wfactA = 1.0f;
	float wfactB = 1.0f;
	float wfactC = 1.0f;

	bool begin(TwoWire* twoWire, uint i2cAddress);
	bool reset();

	//>>> for both CONFIGURATION and MEASUREMENT operating states
	// status bits for testing 'status' value returned by readStatus(), p59
	typedef enum : byte {
		OUTCONVOF    = 0x80,	// overflow of internal bit time counter, OUTCONV
		MRESOF       = 0x40,	// overflow of one or more MRESx 16-bit registers
		ADCOF        = 0x20,	// overflow of one or more ADC channels
		LDATA        = 0x10,	// output buffer overwritten with new value before previous value was read
		NDATA        = 0x08,	// new readings transferred to MRESx registers
		NOTREADY     = 0x04,	// 1=busy, 0=reading available
		STANDBYSTATE = 0x02,	// 1=in standby
		POWERSTATE   = 0x01,	// 1=powered down
		OVERFLOW     = OUTCONVOF | MRESOF
	} AS7221_STATUS;
	bool readStatus(AS7221_STATUS* status);

	bool powerDown() { osr |= 0x40; return writeReg(0x00, osr); }
	bool powerUp() { osr &= ~0x40; return writeReg(0x00, osr); }
	bool stanbyOn() { creg3 &= ~0x10; return writeReg(0x08, creg3); }
	bool standbyOff() { creg3 |= 0x10; return writeReg(0x08, creg3); }

	typedef enum : byte { 
		INVALID = 0x00, CONFIGURATION = 0x02, MEASUREMENT = 0x03 
	} AS7221_OPERATING_STATE;
	bool setOperatingState(AS7221_OPERATING_STATE state);
	AS7221_OPERATING_STATE getOperatingState();
	//<<<

	//>>> for CONFIGURATION operating state only
	bool setConfigCREG1(uint gain, uint time);
	bool setConfigCREG2(bool en_tm, bool en_div, uint div);
	bool setConfigCREG3(uint mmode, bool rdyod, uint cclk);
	bool setBREAK(uint tbreak);
	bool setEDGES(uint edges);
	//<<<

	//>>> for MEASUREMENT operating state only
	bool startMeasurement() { return writeReg(0x00, 0x80); }
	// 16-bit raw UV sensor readings, *uvX = 0xffff = overflow
	bool readUVA(uint* uva) { return readReg16(0x02, uva); }
	bool readUVB(uint* uvb) { return readReg16(0x03, uvb); }
	bool readUVC(uint* uvc) { return readReg16(0x04, uvc); }
	bool readUV(uint* uva, uint* uvb, uint* uvc);
	bool readTemperature(int* degC);
	bool readOUTCONV(ulong* outconv);
	bool automaticGainControl(uint uva, bool overflow = false);
	//<<<

	// Calculations
	float calculateIrradianceUVA(uint uva);
	float calculateIrradianceUVB(uint uvb);
	float calculateIrradianceUVC(uint uvc);
	uint calculateUVIndex(uint uva, uint uvb, uint uvc);
	bool calculateCoefficients(uint gain, uint time, uint cclk, uint div);
	#ifdef DEBUG
	void printCalculations();
	#endif

protected:
	// I2C communications
	bool writeReg(uint reg, byte b);
	bool readReg(uint reg, byte* b);
	bool readReg16(uint reg, uint* value);
};


// Does a software reset and initilizes the default settings
// returns false if something's wrong
// the default settings are ok for for most applications
bool AS7331UVSensor::begin(TwoWire* twoWire, uint i2cAddress)
{
	wire = twoWire;
	i2cAdds = i2cAddress;

	// software reset
	// leaves it powered down and in the 'configuration' operating state
	return reset();
}

// p49
// Software reset, write Operational State Register OSR
// leaves it powered down and in 'configuration' operating state
// configuration registers are set to their default values
bool AS7331UVSensor::reset()
{
	if (!writeReg(0x00, 0x0a))
		return false;
	delay(10);

	// p
	// check device ID and MUT (API Generation register)
	// this fails if not in the 'configuration' operating state
	byte id;
	if (!readReg(0x02, &id) || id != 0x21) {
		AS7331_PRINTLN("bad device id or not in CONFIGURATION state");
		return false;
	}

	// save shadow OSR register
	if (!readReg(0x00, &osr))
		return false;

	// set default values, same as initialized by software reset
	// this also saves the shadow values and calculates the coefficients
	if (!setConfigCREG3(1, 0, 0))	// mmode=CMD, READY=push-pull, cclk=1.024MHz
		return false;
	if (!setConfigCREG1(10, 6))		// gain=2 time=64ms (65536 clocks)
		return false;
	if (!setConfigCREG2(0, 0, 0))	// en_tm=1, en_div=0, div=2^1
		return false;

	return true;
}

// p59
// Returns the 8-bit STATUS register value
// test the returned 'status' value using AS7221_STATUS bits
// IMPORTANT
// * p17, "It is recommended not to communicate via the I2C during the conversion"
//   Poll the READY pin to determine when the reading is available.
//   OR use the 'tconvI' milliseconds value for a scheduler delay, start the 
//   delay when the reading starts, and call readStatus() about 2ms after
// * Reading the status clears the status bits, always check all the status bits
//   for overflow etc.
bool AS7331UVSensor::readStatus(AS7221_STATUS* status)
{
	// 1st byte is OSR, 2nd byte is STATUS
	uint reg16;
	if (!readReg16(0x00, &reg16)) {
		*status = NOTREADY;
		return false;
	}
	*status = (AS7221_STATUS)(reg16 >> 8);	// return STATUS register only
	return true;
}

// Operating state : CONFIGURATION or MEASURMENT
bool AS7331UVSensor::setOperatingState(AS7221_OPERATING_STATE state)
{
	AS7331_ASSERT(state == AS7221_OPERATING_STATE::CONFIGURATION ||
		state == AS7221_OPERATING_STATE::MEASUREMENT, "invalid state");

	osr = (osr & ~0x07) | state;
	return writeReg(0x00, osr);
}

AS7331UVSensor::AS7221_OPERATING_STATE AS7331UVSensor::getOperatingState()
{
	// use the shadow register
	AS7221_OPERATING_STATE state = (AS7221_OPERATING_STATE)(osr & 0x07);
	if (state == AS7221_OPERATING_STATE::CONFIGURATION || state == AS7221_OPERATING_STATE::MEASUREMENT)
		return state;
	return AS7221_OPERATING_STATE::INVALID;
}

// Configuration

// p51
// gain : 0=2048, 1=1024, 2=512, .. 11=1
// time : number of clocks at frequency 'cclk', 0=2^10 .. 14=2^24
bool AS7331UVSensor::setConfigCREG1(uint gain, uint time)
{
	AS7331_ASSERT(gain <= 11 && time <= 14, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	// slao validates the configuration
	if (!calculateCoefficients(gain, time, cclkI, divI))
		return false;
	gainI = gain;
	timeI = time;
	byte creg1 = (gain << 4) + time;
	return writeReg(0x06, creg1);
}

// p53, p54
// en_tm  : enables conversion time measurement, SYND mode only, see readOUTCONV()
// en_div : enables digital divider 'div'
// div    : internal prescaler divider, 0=2^1, 1=2^2, 2=2^3, .. 7=2^8
//          div is only useful if the number-of-bits > 16
bool AS7331UVSensor::setConfigCREG2(bool en_tm, bool en_div, uint div)
{
	AS7331_ASSERT(div <= 7, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	// divider p54
	uint divi = en_div ? div + 1 : 0;
	// also validates the configuration
	if (!calculateCoefficients(gainI, timeI, cclkI, divi))
		return false;
	divI = divi;
	byte creg2 = div;
	if (en_tm)
		creg2 |= 0x40;
	if (en_div)
		creg2 |= 0x08;
	return writeReg(0x07, creg2);
}

// p55
// mmode : 0 = CONT mode, continuous measurement
//         1 = CMD mode, measurement on command (default)
//         2 = SYNS mode, externally synchronized start of measurement
//         3 = SYND mode, externally synchronized start and end of measurement
// rdyod : 0 = READY pin is push-pull output, 1 = READY pin is open-drain output 
//         if push-pull, use pinMode(READY_PIN, INPUT);
//         if open-drain, use pinMode(READY_PIN, INPUT_PULLUP); 
//            or add external 10K..50Kohm pull-up resistor to 3.3V
// cclk  : internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz
// note: the SB bit is controlled by standbyOn() and standbyOff()
bool AS7331UVSensor::setConfigCREG3(uint mmode, bool rdyod, uint cclk)
{
	AS7331_ASSERT(mmode <= 3 && cclk <= 3, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	// also validates the configuration
	if (!calculateCoefficients(gainI, timeI, cclk, divI))
		return false;
	mmodeI = mmode;
	cclkI = cclk;
	// CREG3 shadow register
	creg3 = (mmode << 6) + cclk;
	if (rdyod)
		creg3 |= 0x08;
	return writeReg(0x08, creg3);
}

// p56
// Break time between two measurements (except CMD mode): from 0 to 2040us, step size 8us
// tbreak : 0..255 in 8us steps, e.g. 255 * 8us = 2040uS
//          0 = minimum time of 3 clocks of fCLK
bool AS7331UVSensor::setBREAK(uint tbreak)
{
	AS7331_ASSERT(tbreak <= 255, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	return writeReg(0x09, (byte)tbreak);
}

// p56
// edges : 1..255, the number of SYN falling edges, SYND mode only
// After a measurement was started in SYND mode, this defines the number of 
// additional falling edges of input SYN until the conversion is completed.
bool AS7331UVSensor::setEDGES(uint edges)
{
	AS7331_ASSERT(edges > 0 && edges <= 255, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	return writeReg(0x0a, (byte)edges);
}

// Measurements

// p41
// Reads the compensation temperature of the chip (not the ambient temperature)
// range is -66 .. +137 C (0..4095 raw), that's 0.05degC LS bit resolution
// there's no need for a float calculation, the sensor is not that accurate
bool AS7331UVSensor::readTemperature(int* degC)
{
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::MEASUREMENT, "requires MEASUREMENT state");

	// temperature reading is 12 bits, max. 0x0fff (4095)
	// p42, degC = (raw * 0.05) - 66.9
	// 0 = -66.9C, 1338 = 0C, 4095 = 137.85C
	uint t;
	bool ok = readReg16(0x01, &t);
	// an integer is probably all we need
	*degC = (((int)t * 5) - 6690) / 100;
	return ok;
}

// Read all 3 UV sensor's raw values
bool AS7331UVSensor::readUV(uint* uva, uint* uvb, uint* uvc)
{
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::MEASUREMENT, "requires MEASUREMENT state");

	*uva = 0; *uvb = 0; *uvc = 0;
	return readUVA(uva) && readUVB(uvb) && readUVC(uvc);
}

// Read internal clock count for a measurement, SYND mode only
// time = outconv / cclk
bool AS7331UVSensor::readOUTCONV(ulong* outconv)
{
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::MEASUREMENT, "requires MEASUREMENT state");
	AS7331_ASSERT(mmodeI == 3, "SYND mode only");

	uint lo, hi;
	if (!readReg16(0x05, &lo) || !readReg16(0x06, &hi)) {
		*outconv = 0;
		return false;
	}
	*outconv = ((ulong)hi << 16) + lo;
	return true;
}

// If the UVA reading is above or below a certain value, or an overflow
// flag (xxxOF) was set, decrease or increase the gain and take another 
// reading. The invalid reading should be discarded.
// Returns true if the gain was changed and a new reading should be taken,
// false if the gain was not changed.
// The disadvantage is that readings may be discarded, which can slow it 
// down a lot if sampling times are long and the UV changes are very big.
bool AS7331UVSensor::automaticGainControl(uint uva, bool overflow /*= false*/)
{
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::MEASUREMENT, "requires MEASUREMENT state");

	// gainI : 0=x2048, 1=x1024, 2=x512, .. 10=x2, 11=x1

	// overflow, reduce the gain
	if ((uva >= agcMaxI || overflow) && gainI != 11) {
		// increasing gainI decreases the gain
		++gainI;
		AS7331_PRINTLN("gain decreased");
	}

	// underflow, increase the gain
	else if (uva < agcMinI && gainI != 0) {
		// decreasing gainI increases the gain
		--gainI;
		AS7331_PRINTLN("gain increased");
	}
	else 
		return false;

	// update the gain
	if (!setOperatingState(AS7221_OPERATING_STATE::CONFIGURATION))
		return false;
	if (!setConfigCREG1(gainI, timeI))
		return false;
	return setOperatingState(AS7221_OPERATING_STATE::MEASUREMENT);
}

// Calculations

// irradiance is returned in microWatts-per-square-centimeter, uW/cm2
// NAN is returned on overflow (16-bit reading = 65535), 
// but also check the STATUS register xxxOF bits
float AS7331UVSensor::calculateIrradianceUVA(uint uva)
{
	// if reading is too low, we cannot calculate a sensible irradiance
	if (uva < 2)
		return 0.0f;
	// overflow
	if (uva >= 0xffff)
		return NAN;
	// multiply by LS bit value and convert nanoWatts to microWatts
	return ((float)uva * lsbA) / 1000.0f; 
}
float AS7331UVSensor::calculateIrradianceUVB(uint uvb) 
{ 
	if (uvb < 2)
		return 0.0f;
	if (uvb >= 0xffff)
		return NAN;
	return ((float)uvb * lsbB) / 1000.0f;
}
float AS7331UVSensor::calculateIrradianceUVC(uint uvc) 
{ 
	if (uvc < 2)
		return 0.0f;
	if (uvc >= 0xffff)
		return NAN;
	return ((float)uvc * lsbC) / 1000.0f;
}

// UV Index Calculation
// 0        No UV, safe for vampires
// 1..2     Low
// 3..5     Moderate
// 6..7     High
// 8..10    Very high
// 11+      Extreme, put your clothes back on!
// 9999     Overflow, reduce the gain
uint AS7331UVSensor::calculateUVIndex(uint uva, uint uvb, uint uvc)
{
	if (uva >= 0xffff || uvb >= 0xffff)
		return 9999;		// overflow!

	// The WHO states that 95% of UVB is absorbed by the atmosphere, but this 
	// depends on the altitude. The UVB that does get through is 1000x more 
	// powerful than UVA!
	// For UVC, 100% of UVC should be absorbed by the atmosphere, so it's ignored 
	// (but my sensor measures VERY HIGH levels of UVC, up to 10uW/cm2 in evening 
	// sunlight through double glazing!?)

	// irradiance in uW/cm2 (microWatts)
	float irAuW = calculateIrradianceUVA(uva);
	float irBuW = calculateIrradianceUVB(uvb);

	// add in the UVB reading
	//TODO 1000x UVB reading seems way too high! UVI goes way too high
	//TODO out-of-spec chip?
	//TODO measure UVA vs. UVB at different altitudes
	float iruW = irAuW;
	// for now, just use a ratio...
	if (irAuW > 0.0f)		// prevent divide-by-zero
		iruW += ((irBuW / irAuW) * 1000.0f);

	// uW/cm2 -> UV Index
	float uvi = iruW * 0.0004f;

	// return UV Index as an integer, rounded up
	return (uint)(uvi + 0.5f);
}

// Compute the Full Scale Range (FSR) and LS bit value for each sensor
// the results for cclk=1.024MHz should match the tables on p32..p38
// calculates the conversion time (tconvI) and number of significant bits (nbitsI)
// validates the configuration, returning 'false' if it's invalid
// 
// uses values initialized by setConfigCREGx()
// gain : 0=2048, 1=1024, 2=512, .. 11=1
// time : number of clocks at frequency 'cclk', 0=2^10 .. 14=2^24
// cclk : internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz
// div  : divider value + 1, 0=disabled, 1=2, 2=4, .. 8=256  (1=2^1..8=2^8)
bool AS7331UVSensor::calculateCoefficients(uint gain, uint time, uint cclk, uint div)
{
	if (gain > 11 || time > 14 || cclk > 3 || div > 8) {
		AS7331_PRINTLN("bad parameter");
		return false;
	}

	// some gains are invalid for clocks above 1024MHz, see table on p38
	if (cclk == 1 && gain < 1) {	// 2048MHz, max. gain is 1024x
		AS7331_PRINTLN("invalid gain for cclk");
		return false;
	}
	if (cclk == 2 && gain < 2) {	// 4098MHz, max. gain is 512x
		AS7331_PRINTLN("invalid gain for cclk");
		return false;
	}
	if (cclk == 3) {				// 8192MHz, max. gain is 256x
		if (gain < 3 || gain & 1) {	// and gains 128x, 32x, 8x and 2x are invalid
			AS7331_PRINTLN("invalid gain for cclk");
			return false;
		}
	}

	// internal clock frequency in KHz (MHz / 1000)
	ulong fclk = 1024L << cclk;

	// no. of clocks at frequency fclk
	ulong nclk = 1L << (time + 10);

	// conversion time in milliseconds
	// >= 64ms conversion time is required for a 16-bit result
	//TODO conversion time < 1ms is allowed?
	ulong tconv = nclk / fclk;
	if (tconv == 0) {
		AS7331_PRINTLN("conversion time < 1ms");
		return false;
	}

	// number of significant bits in conversion, according to tconv
	// 1ms=10 bits, .. 16384ms=24bits
	uint nbits = 9;
	for (uint t = tconv; t; t >>= 1)	// tconv is power-of-2
		++nbits;
	if (nbits < 10 || nbits > 24) {		// this should never happen
		AS7331_PRINTLN("invalid nbits");
		return false;
	}

	// base FSR in uW/cm2, at gain = 2048, conversion time = 1ms
	static const float baseFSRA = 170.0f;
	static const float baseFSRB = 189.0f;
	static const float baseFSRC = 83.0f;

	ulong bitDiv = nbits <= 16 ? 1 : (1 << (nbits - 16));
	ulong gainMul = 1 << gain;

	// if 'div' is active, increase the LS bit value and the full scale range
	// 'div' is not needed for readings <= 16 bits or if the Full Scale Range
	// is < 0xffff (65535)
	/*
	The OUTCONV register is 24 bits. If a conversion of over 16 bits is used, 
	only the LS 16 bits can be read via the MRESx registers. To read the upper 
	8 bits, the 'div' divider can be used, see p39 section 7.5. This divides 
	the OUTCONV reading, shifting the bits which are transferred to the MRES 
	registers.

	Without divider, the LS 16 bits are returned
					  222211111111100000000000
	OUTCONV 24 bits   321098765432109876543210
	MRES    16 bits           5432109876543210

	With divider 2 (en_div=1, div=0)
	OUTCONV 24 bits   321098765432109876543210
	MRES    16 bits         5432109876543210..

	With divider 256 (wn_div=1, div=7)
	OUTCONV 24 bits   321098765432109876543210
	MRES    16 bits   5432109876543210

	If the 24-bit OUTCONV register / div contains a value bigger than the 16-bit
	MRES register, the STATUS register's MRESOF bit is set.
	*/

	// div  : divider value + 1, 0=disabled, 1=2, 2=4, .. 8=256 (1=2^1..8=2^8)
	if (div > 0) {
		if (nbits <= 16) {
			AS7331_PRINTLN("div not needed for <= 16 bit readings");
			return false;
		}
		gainMul *= (1 << div);
	}
	float multiplier = (float)gainMul / (float)bitDiv;

	// full scale range of MRESx value in microWatts-per-square-centimeter, uW/cm2
	// for reference only, we don't need these for the calculations
	fsrA = baseFSRA * multiplier;
	fsrB = baseFSRB * multiplier;
	fsrC = baseFSRC * multiplier;
	// special case for UVC channel, not sure why, see p36
	if (time >= 4 && time <= 6)
		fsrC /= 2.0f;

	// LS bit significance of upper 16 bits in nanoWatts-per-square-centimeter, nw/cm2
	float xlsb = (gainMul * 1000.0f) / (1024L << time);

	// include the window factors in the calculation (default = 1.0)
	lsbA = baseFSRA * xlsb * wfactA;
	lsbB = baseFSRB * xlsb * wfactB;
	lsbC = baseFSRC * xlsb * wfactC;

	// automatic gain control switching values depend on the number of bits
	agcMaxI = (1 << nbits) - 1;
	agcMinI = agcMaxI >> 6;		// divide-by-64

	tconvI = tconv;
	nbitsI = nbits;

	return true;
}

#ifdef DEBUG

// Print the calculations, for testing only
// the results for cclk=1.024MHz should match the tables on p32..p38
void AS7331UVSensor::printCalculations()
{
	//                           123456 12345 123456 123456  12345678901234  12345678901234  "
	static const char* header = "gain   time  tconv  nbits      fsrA            fsrB         "
	//   12345678901234  12345678901234  12345678901234  12345678901234
		"   fsrC            lsbA            lsbB            lsbC\n\r";

	char buf[256];

	for (uint cclk = 0; cclk < 4; ++cclk) {
		for (uint time = 0; time < 15; ++time) {

			// no. of clocks at frequency fclk
			ulong nclk = 1L << (time + 10);
			ulong fclk = 1024L << cclk;
			bool headerPrinted = false;

			for (uint gain = 0; gain < 12; ++gain) {

				// some gains are invalid for clocks above 1024MHz, see table on p38
				if (cclk == 1 && gain < 1)		// 2048MHz, max. gain is 1024x
					continue;
				if (cclk == 2 && gain < 2)		// 4098MHz, max. gain is 512x
					continue;
				if (cclk == 3) {				// 8192MHz, max. gain is 256x
					if (gain < 3 || gain & 1)	// and gains 128x, 32x, 8x and 2x are invalid
						continue;
				}
				
				// conversion time in milliseconds
				// >= 64ms conversion time is required for a 16-bit result
				ulong tconv = nclk / fclk;

				// skip conversion time < 1ms
				if (tconv == 0)
					continue;
			
				//TODO implement 'div', see p39
				uint div = 0;

				if (calculateCoefficients(gain, time, cclk, div)) {

					if (!headerPrinted) {
						sprintf(buf, "\n\rcclk=%u  fclk=%u  nclk=%lu\n\r", cclk, fclk, nclk);
						AS7331_PRINTLN(buf);
						AS7331_PRINTLN(header);
						headerPrinted = true;
					}
					uint actualGain = 1 << (11 - gain);
					sprintf(buf, "%-6u %-5u %-6u %-6u %15f %15f %15f %15f %15f %15f",
						actualGain, time, tconv, nbitsI, fsrA, fsrB, fsrC, lsbA, lsbB, lsbC);
					AS7331_PRINTLN(buf);
				}
			}
		}
		AS7331_PRINTLN("");
	}

	// restore the default coefficients
	calculateCoefficients(gainI, timeI, cclkI, divI);
}
#endif

// I2C Communications

bool AS7331UVSensor::writeReg(uint reg, byte b)
{
	wire->beginTransmission(i2cAdds);
	wire->write(reg);
	wire->write(b);
	if (wire->endTransmission() != 0) {
		AS7331_PRINTLN("endTransmission() failed");
		return false;
	}
	return true;
}

bool AS7331UVSensor::readReg(uint reg, byte* b)
{
	*b = 0;
	wire->beginTransmission(i2cAdds);
	wire->write(reg);
	if (wire->endTransmission(false) != 0) {
		AS7331_PRINTLN("endTransmission() failed");
		return false;
	}
	if (wire->requestFrom(i2cAdds, 1) != 1) {
		AS7331_PRINTLN("requestFrom() failed");
		return false;
	}
	*b = wire->read();
	return true;
}

bool AS7331UVSensor::readReg16(uint reg, uint* value)
{
	*value = 0;
	wire->beginTransmission(i2cAdds);
	wire->write(reg);
	if (wire->endTransmission(false) != 0) {
		AS7331_PRINTLN("endTransmission() failed");
		return false;
	}
	if (wire->requestFrom(i2cAdds, 2) != 2) {
		AS7331_PRINTLN("requestFrom() failed");
		return false;
	}
	byte buf[2];
	if (wire->readBytes(buf, 2) != 2) {
		AS7331_PRINTLN("readBytes() failed");
		return false;
	}
	*value = *(uint16_t*)buf;
	return true;
}
