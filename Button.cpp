/************************************************************************************
 * 
 * Name    : mrButton
 * File    : Button.cpp
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

#include "Button.h"

Button::Button(const uint8_t pin) :
	m_pin(pin),
	m_state(HIGH),
	m_changed(false),
	m_delay(500),
	m_timer(0) {
	
}

Button::~Button() {

}

bool Button::read() {
	uint32_t timer = millis();
	if (timer > m_timer && digitalRead(m_pin) != m_state) {
		m_timer = timer + m_delay;
		m_state = !m_state;
		m_changed = true;
	}
	return m_state;
}

bool Button::changed() {
	if (m_changed) {
		m_changed = false;
		return true;
	}
	return false;
}

bool Button::toggled() {
	read();
	return changed();
}
