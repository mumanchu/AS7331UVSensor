///////////////////////////////////////////////////////////////////////////////
// AS7331 UV Sensor with UVA, UVB and UVC Sensors and I2C Interface
// Copyright (C) 2026.09.05, https://muman.ch and https://github.com/mumanchu
// All rights reversed, released under the terms of the WTF license
/*
This is an example using the AS7331UVSensor library in CMD mode, where a 
single conversion is controlled by an I2C message sent by startMeasurement().

For full details see
https://github.com/mumanchu/AS7331UVSensor
*/

// Comment this out for release mode, to remove the log output
#define DEBUG

#include <Wire.h>

#include "AS7331UVSensor.h"
AS7331UVSensor uvsensor;

// Comment this out if not using the READY output for polling
// see also line 97
#define AS7331_READY_PIN	7	// D7 (PA8 on Nucleo board)


void setup()
{
	// use different pins for RX/TX logging
	// this is only for the STM32 Nucleo-64 boards
	#ifdef ARDUINO_NUCLEO_64
	Serial.setTx(PC_10);
	Serial.setRx(PC_11);
	#endif

	Serial.begin(115200);
	delay(3000);

	// PuTTY clear screen and scrollback
	Serial.print("\033[2J\033[H\033[3J");

	Serial.println("\n\rStarted\n\r");
	Serial.flush();

	// XIAO has no LED
	#ifdef LED_BUILTIN
	pinMode(LED_BUILTIN, OUTPUT);
	#endif
	#if defined(AS7331_READY_PIN) 
	pinMode(AS7331_READY_PIN, INPUT_PULLDOWN);
	#endif

	// 400kHz
	Wire.begin();
	Wire.setClock(100000);

	// reset and set default configuration
	if (!uvsensor.begin(&Wire, 0x74)) {
		Serial.println("uvsensor.begin() failed");
		Serial.flush();
		while (1) yield();
	}

	// show the calculations
	// the first 1.024MHz set should match the tables on p32..p38
	uvsensor.printCalculations();

	// configure the gain and integration time
	// if using automatic gain control, start with the highest gain (0=x2048)
	uvsensor.setConfigCREG1(0, 6);

	// power up and start reading UV
	uvsensor.powerUp();
	uvsensor.setOperatingState(uvsensor.Measurement);
}

void loop()
{
	// scheduler
	ulong t = micros();

	// flash the LED so we know it's running
	static ulong t1 = 0;
	if (t - t1 > 100000) {		// 100ms
		t1 = t;
		#ifdef LED_BUILTIN
		digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
		#endif
	}

	static bool measurementStarted = false;
	static ulong tstart = 0;	// for timing the conversion, in us
	static ulong tpoll = 0;		// for conversion polling delay, in us
	static ulong tconv = 0;		// expected conversion time in microseconds

	if (measurementStarted) {
		char buf[256];

		// p17, "it is recommended not to communicate via the I2C during the conversion"
		// do not start polling until after the reading has been taken
		
		// use a timer or use the READY signal, comment out as required
		//if (digitalRead(AS7331_READY_PIN)) {	// use READY signal
		if (t - tpoll > tconv) {	// use timer

			AS7331UVSensor::AS7331Status status;
			uvsensor.readStatus(&status);

			// reading is ready
			if ((status & uvsensor.NotReady) == 0) {
				ulong tconvActual = (micros() - tstart) / 1000;
				measurementStarted = false;

				// did measurement overflow occur?
				bool overflow = (status & (uvsensor.OutConvOF | uvsensor.MresOF)) != 0;
				if (overflow) {
					strcpy(buf, "overflow=");
					if (status & uvsensor.OutConvOF)
						strcat(buf, "OUTCONVOF ");
					if (status & uvsensor.MresOF)
						strcat(buf, "MRESOF ");
					if (status & uvsensor.AdcOF)
						strcat(buf, "ADCOF ");
					Serial.println(buf);
					Serial.flush();
				}

				// get raw UV values
				uint uva, uvb, uvc;
				uvsensor.readUV(&uva, &uvb, &uvc);

				// optionally do automatic gain control, comment out as required
				bool gainChanged = uvsensor.automaticGainControl(uva, overflow);
				//bool gainChanged = false;

				// if gain NOT changed by AGC then use this reading
				// else take another reading using the new gain setting
				if (!gainChanged) {

					// calculate irradiance values in uW/cm2
					uint uvIndex = uvsensor.calculateUVIndex(uva, uvb, uvc);
					float irA = uvsensor.calculateIrradianceUVA(uva);
					float irB = uvsensor.calculateIrradianceUVB(uvb);
					float irC = uvsensor.calculateIrradianceUVC(uvc);

					// read on-chip temperature sensor, degC
					int temp;
					uvsensor.readTemperature(&temp);

					// display the values
					sprintf(buf, "t=%umS tconvI=%umS gain=x%u nbits=%u  uva=%u uvb=%u uvc=%u temp=%dC uvIndex=%u",
						tconvActual, uvsensor.tconvI, 2048 >> uvsensor.gainI, uvsensor.nbitsI, 
						uva, uvb, uvc, temp, uvIndex);
					char* s = strchr(buf, '\0');
					sprintf(s, "  irA=%f irB=%f irC=%f", irA, irB, irC);
					Serial.println(buf);
					Serial.flush();
				}
			}
			else {
				// reading was not ready
				// - READY signal not connected or pinMode(READY_PIN, ...) not set?
				// - chip's READY pin not configured correctly (push-pull or open-drain)?
				// - incorrect tconvI calculation?
				// - endTransmission() failed? reset the I2C bus
				Serial.println("not ready");
				Serial.flush();
			}
		}
	}

	// start next measurement
	if (!measurementStarted) {
		uvsensor.startMeasurement();
		measurementStarted = true;
		tstart = micros();		// for sample time measurment
		tpoll = tstart;			// for polling delay

		// expected conversion time in microseconds
		// convert ms to us, and add 12% because the actual reading 
		// seems to take longer than tconvI, not sure why
		tconv = uvsensor.tconvI * 1120;
	}

}
