/************************************************************************************
 * 
 * Name    : mrButton
 * File    : Button.h
 * Author  : Mark Reds <marco@markreds.it>
 * Date    : December 7, 2020
 * Version : 1.0.0
 * Notes   : Arduino library to drive a push button.
 * 
 * Copyright (C) 2020 Marco Rossi (aka Mark Reds).  All right reserved.
 * 
 * This file is part of mrButton.
 * 
 * mrButton is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * mrButton is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with mrButton. If not, see <http://www.gnu.org/licenses/>.
 * 
 ************************************************************************************/

#ifndef _Button_h_
#define _Button_h_

#if defined(ARDUINO) && ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#include "pins_arduino.h"
#endif

class Button {
	private:
		uint8_t m_pin;
		bool m_state;
		bool m_changed;
		uint16_t m_delay;
		uint32_t m_timer;
		bool changed();
	public:
		Button(uint8_t pin);
		virtual ~Button();
		inline void begin() { pinMode(m_pin, INPUT_PULLUP); }
		bool read();
		bool toggled();
		inline bool state() { return m_state; }
		inline bool pressed() { return read() == PRESSED && changed(); }
		inline bool released() { return read() == RELEASED && changed(); }

		const static bool PRESSED = LOW;
		const static bool RELEASED = HIGH;
};

#endif
