#include "Trend.h"

void Trend::add(float value) {
  m_sum += value;
  m_samples++;
}

void Trend::commit() {
  if (m_samples == 0) {
    return;
  }
  m_history[m_next] = m_sum / m_samples;
  m_next = (m_next + 1) % SIZE;
  if (m_count < SIZE) {
    m_count++;
  }
  m_sum = 0.0f;
  m_samples = 0;
}

TrendDirection Trend::direction() const {
  if (m_count < SIZE) {
    return TREND_UNKNOWN;
  }
  float newest = m_history[(m_next + SIZE - 1) % SIZE];
  float oldest = m_history[m_next];
  float delta = newest - oldest;
  if (delta >= m_threshold) {
    return TREND_UP;
  }
  if (delta <= -m_threshold) {
    return TREND_DOWN;
  }
  return TREND_STEADY;
}
