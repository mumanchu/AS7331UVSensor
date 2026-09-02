#pragma once

///////////////////////////////////////////////////////////////////////////////
// AS7331 UV Sensor with UVA, UVB and UVC Sensors and I2C Interface
// Copyright (C) 2026.08.28, https://muman.ch and https://github/mumanchu
// All rights reversed, released under the WTF License
/*

***
TODO handle overflow, 0xffff and overflow status flags
TODO handle wfact
***

TODO verify this
The AS7331 has many gain and conversion settings configurable over I2C which means
you can measure up to 349 mW / cm² on UVA channel, 386 mW / cm² on UVB and 169 mW / cm² on
UVC at 1x gain, and with a responsivity as low as 2.38 nW / cm² per LSB.


UVA – The most common UV ray from the sun and most dangerous, UVA can penetrate the skin down to the middle layer.
UVB – A shorter wavelength than UVA that can only penetrate the skin to the top layer. 
The earth's ozone layer stops some UVB rays from reaching the surface. Treated glass also can stop UVB rays.
UVC – The ozone layer stops all UVC rays from the sun. The only exposure humans get to UVC is from artificial sources, lasers or welding torches etc.
the most dangerous type skin burns and eye injury

The highest ground-level UV index ever recorded on Earth was 43.3, measured at the Licancabur volcano in Bolivia in 2003 due to extreme altitude and tropical latitude



Typical Maximum UVA Levels
Natural Sunlight: Surface intensity for UVA around 365 nm is typically less than 0.006 W/cm² (6 mW/cm²).
Industrial Inspection (N大な/Magnaflux): Safe extended limits are commonly 0.005 W/cm² (5,000 µW/cm²), 
with some maximum limits capped at 0.01 W/cm² (10,000 µW/cm²).
Industrial UV Curing Lamps: Specialized high-power meters can read intensive application outputs up to 30 W/cm².


operating states 
configuration and measurement

software reset -> power down and configuration state


READY polling


wfac calibration


24-bit OUTCONV vs. 16-bit MRES


it is recommended not to communicate via the I²C during the conversion
Use pause times
between two conversion cycles for data transfer via the I²C interface


Sampling Modes
CONT	continuous
CMD		sample on demond

UV readings are synchronised with the falling edge of the SYN pin
variable or dynamic exposure control
sync with external event, uv/light source pulse
eliminate software latency

SYNS	sample on falling edge of SYN pin
SYND	sample on falling EDGE count of SYN pin


Divider, p39
The A/D converter is 24 bits, but values are returned as 16-bits.
The divider allows you to read out the otherwise unavailable upper 8 bits
en_div = 1, div = 


Energy Saving Options


Power Down


Standby


Irradiance
mW/cm2 or uW/cm2 ?
summer midday = 30 to 50 mW/cm2

UV Index



I2C ADDRESS
0x011101xx
0x74..0x77 according to jumbers A1 and A0.

DATA SHEET, DS001047 v4-00 • 2023-Mar-24
All page number references (pxx) are for this document
https://look.ams-osram.com/m/1856fd2c69c35605/original/AS7331-Spectral-UVA-B-C-Sensor.pdf

OTHER LIBRARIES, for reference only
In all these libraries, the irradiance calculations only seem to work for the 
1.024MHz internal clock, and the 'divider' is not supported.
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
	byte osr = 0x42;	// operation state register OSR shadow
	uint mmodeI = 1;	// conversion mode, 0=CONT, 1=CMD, 2-SYNS, 3=SYND
	uint gainI = 10;	// gain setting, 0=x2048, 1=x1024, 2=x512, .. 10=x2, 11=x1
	uint timeI = 6;		// number of clocks at frequency 'cclk', 0=2^10 .. 6=2^16 (65536) .. 14=2^24
	uint divI = 0;		// divider value, 0=disabled, 1=2, 2=4, 3=8, .. 8=256 (1=2^1 .. 8=2^8)
	uint cclkI = 0;		// internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz

public:
	// Values from calculateCoefficients()
	// do not write to these, unless you are experimenting or testing
	float fsrA, fsrB, fsrC;		// full scale range, microWatts per square centimeter, uW/cm2
	float lsbA, lsbB, lsbC;		// sigificance of LS bit, nanoWatts per square centimeter, nW/cm2
	uint tconvI;				// conversion time in milliseconds
	uint nbitsI;				// number of significant bits of conversion (in 24-bit OUTCONV reg)

	// Window factors, set these if the sensor has a glass or plastic cover, each 
	// has a different factor because the window may have different absorbtions
	// needs calibration, default=1.0 (no window glass)
	float wfactA = 1.0f;
	float wfactB = 1.0f;
	float wfactC = 1.0f;

	bool begin(TwoWire* twoWire, uint i2cAddress);

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
		OVERFLOW     = OUTCONVOF | MRESOF | ADCOF
	} AS7221_STATUS;
	bool readStatus(AS7221_STATUS* status);

	bool powerDown() { osr |= 0x40;  return writeReg(0x00, osr); }
	bool powerUp()   { osr &= ~0x40; return writeReg(0x00, osr); }
	bool startMeasurement() { return writeReg(0x00, 0x80); }

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
	// 16-bit raw UV sensor readings, 0xffff = max or overflow
	bool readUVA(uint* uva) { return readReg16(0x02, uva); }
	bool readUVB(uint* uvb) { return readReg16(0x03, uvb); }
	bool readUVC(uint* uvc) { return readReg16(0x04, uvc); }
	bool readUV(uint* uva, uint* uvb, uint* uvc);
	bool readTemperature(uint* degC);
	bool readOUTCONV(ulong* outconv);
	//<<<

	// Calculations
	// irradiance is returned in microWatts-per-square-centimeter, uW/cm2
	float calculateIrradianceUVA(uint uva);
	float calculateIrradianceUVB(uint uvb);
	float calculateIrradianceUVC(uint uvc);
	uint calculateUVIndex(uint uva, uint uvb, uint uvc);
	bool calculateCoefficients(uint gain, uint time, uint cclk, uint div);
	#ifdef DEBUG
	void printCalculations();
	#endif

	// I2C communications
	bool writeReg(uint reg, byte b);
	bool readReg(uint reg, byte* b);
	bool readReg16(uint reg, uint* value);
};


// Does a software reset and initilizes the default settings
// returns false if something's wrong
// for most applications, the default settings will be ok
bool AS7331UVSensor::begin(TwoWire* twoWire, uint i2cAddress)
{
	wire = twoWire;
	i2cAdds = i2cAddress;

	// software reset, write Operational State Register OSR
	// leaves it powered down and in 'configuration' operating state
	if (!writeReg(0x00, 0x0a))
		return false;
	delay(10);

	// check device ID and MUT (API Generation register)
	// this fails if not in the 'configuration' operating state
	byte id;
	if (!readReg(0x02, &id) || id != 0x21) {
		AS7331_PRINTLN("bad device id or not in 'configuration' state");
		return false;
	}

	// save shadow OSR register
	if (!readReg(0x00, &osr))
		return false;

	// set default values, same as initialized by software reset
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

	osr = (osr & 0x07) | state;
	return writeReg(0x00, osr);
}

AS7331UVSensor::AS7221_OPERATING_STATE AS7331UVSensor::getOperatingState()
{
	// use the shadow register
	uint st = osr & 0x07;
	if (st != 0x02 && st != 0x03)
		st = 0;
	return (AS7221_OPERATING_STATE)st;
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
// rdyod : 0 = READY pin is push-pull output, 1 = READY pin is open drain output 
// cclk  : internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz
bool AS7331UVSensor::setConfigCREG3(uint mmode, bool rdyod, uint cclk)
{
	AS7331_ASSERT(mmode <= 3 && cclk <= 3, "bad parameter");
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::CONFIGURATION, "requires CONFIGURATION state");

	// also validates the configuration
	if (!calculateCoefficients(gainI, timeI, cclk, divI))
		return false;
	mmodeI = mmode;
	cclkI = cclk;
	byte creg3 = (mmode << 6) + cclk;
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
// Reads the temperature of the chip, not the ambient temperature.
// temperature cannot be -ve, range is 0..138 degC
// there's no need for a float calculation, the sensor is not that accurate
// the temperature is returned as 0 until the first reading has been taken
bool AS7331UVSensor::readTemperature(uint* degC)
{
	AS7331_ASSERT(getOperatingState() == AS7221_OPERATING_STATE::MEASUREMENT, "requires MEASUREMENT state");

	// temperature reading is 12 bits, max. 0x0fff (4095)
	uint t;
	bool ok = readReg16(0x01, &t);
	// p42, degC = (raw * 0.05) - 66.9
	// integer version, rounded up
	*degC = t == 0 ? 0 : ((t * 5) - 6690 + 50) / 100;
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

// Calculations

// returned values are in microWatts-per-square-centimeter, uW/cm2
float AS7331UVSensor::calculateIrradianceUVA(uint uva)
{
	// if reading is too low, we cannot calculate irradiance
	if (uva < 2)
		return 0.0f;
	// overflow
	if (uva >= 65535)
		return NAN;
	// multiply by LS bit value and convert nanoWatts to microWatts
	return (uva * lsbA) / 1000.0f; 
}
float AS7331UVSensor::calculateIrradianceUVB(uint uvb) 
{ 
	if (uvb < 2)
		return 0.0f;
	if (uvb >= 65535)
		return NAN;
	return (uvb * lsbB) / 1000.0f;
}
float AS7331UVSensor::calculateIrradianceUVC(uint uvc) 
{ 
	if (uvc < 2)
		return 0.0f;
	if (uvc >= 65535)
		return NAN;
	return (uvc * lsbC) / 1000.0f;
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
	if (uva == 0xffff || uvb == 0xffff)
		return 9999;		// overflow!

	// WHO states that 95% of UVB is absorbed by the atmosphere, but this 
	// depends on the altitude. The UVB that does get through is 1000% more 
	// powerful than UVA!
	// For UVC, 100% of UVC is absorbed by the atmosphere so it's ignored 
	// (but the sensor still measures some UVC, e.g. 27uW/cm2 in full evening 
	// sunlight through double glazing at 1500m altitude)

	// irradiance in uW/cm2 (microWatts)
	float irAuW = calculateIrradianceUVA(uva);
	float irBuW = calculateIrradianceUVB(uvb);

	// add in 1000x the UVB
	//TODO 1000x UVB seems way too much, maybe use a ratio?
	//TODO measure UVA vs. UVB at different altitudes
	float iruW = irAuW;
	if (irAuW > 0.0f)		// prevent divide-by-zero
		iruW += ((irBuW / irAuW) * 1000.0f);

	// uW/cm2 -> UV Index
	float uvi = iruW * 0.0004f;

	// return UV Index as an integer, rounded up
	return (uint)(uvi + 0.5f);
}

// Compute the Full Scale Range (FSR) and LS bit value for each sensor
// the results for cclk=1.024MHz should match the tables on p32..p38
// also calculates the conversion time (tconvI) and number of significant bits (nbitsI)
// validates the configuration, returning 'false' if it's invalid
// 
// uses values initialized by setConfigCREGx()
// gain : 0=2048, 1=1024, 2=512, .. 11=1
// time : number of clocks at frequency 'cclk', 0=2^10 .. 14=2^24
// cclk : internal clock frequency, 0=1.024MHz, 1=2.048MHz, 2=4.096MHz, 3=8.192MHz
// div  : divider value, 0=disabled, 1=2, 2=4, .. 8=256  (1=2^1..8=2^8)
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
	//TODO < 10 bits is allowed?
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
	// is < 65535
	/*
	The OUTCONV register is 24 bits. If a conversion of over 16 bits 
	is used, only the LS 16 bits can be read via the MRESx registers.
	To read the upper 8 bits, the 'div' divider can be used, see p39
	section 7.5. This divides the OUTCONV reading, shifting the bits
	which are transferred to the MRES registers.

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
	if (div > 0) {
		if (nbits <= 16) {
			AS7331_PRINTLN("div not needed for <= 16 bit readings");
			return false;
		}

		// div : p54, divider value : 0=disabled, 1=2, 2=4, .. 8=256 (1=2^1..8=2^8)
		gainMul *= (1 << div);
	}
	float multiplier = (float)gainMul / (float)bitDiv;

	// full scale range of MRESx value in microWatts-per-square-centimeter, uW/cm2
	// for refernce only, we don't need these for the calculations
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

				//TODO conversion time < 1ms is allowed?
				if (tconv == 0) {
					//AS7331_PRINTLN("conversion time < 1ms");
					continue;
				}
			
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

	// restore the default values
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

