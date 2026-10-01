#pragma once

#include <stdint.h>

// Tendenza: confronta la media dell'ultimo minuto con quella di
// TREND_WINDOW_MIN minuti prima
#define TREND_WINDOW_MIN  10

enum TrendDirection {
  TREND_UNKNOWN,  // storico non ancora sufficiente
  TREND_STEADY,
  TREND_UP,
  TREND_DOWN
};

class Trend {
public:
  // threshold: variazione minima nella finestra per considerare il valore in salita/discesa
  explicit Trend(float threshold) : m_threshold(threshold) { }

  // Accumula una lettura valida nella media del minuto in corso
  void add(float value);

  // Chiude il minuto in corso e ne salva la media nello storico
  void commit();

  TrendDirection direction() const;

private:
  static const uint8_t SIZE = TREND_WINDOW_MIN + 1;
  float m_history[SIZE];
  uint8_t m_count = 0;
  uint8_t m_next = 0;
  float m_sum = 0.0f;
  uint16_t m_samples = 0;
  float m_threshold;
};
