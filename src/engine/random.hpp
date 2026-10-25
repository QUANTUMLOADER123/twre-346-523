// random.hpp - CUniformRandomStream (Numerical Recipes ran1, same algorithm family Source uses
// so weapon spread can be driven deterministically from CUserCmd::random_seed)
#pragma once
#include <cmath>
#include <cstdint>

class CUniformRandomStream {
public:
  enum { NTAB = 32, IA = 16807, IM = 2147483647, IQ = 127773, IR = 2836, NDIV = (1 + (IM - 1) / NTAB) };
  CUniformRandomStream() { SetSeed(0); }
  void SetSeed(int seed) { m_idum = (seed < 0) ? seed : -seed; m_iy = 0; }
  float RandomFloat(float lo = 0.f, float hi = 1.f) {
    float fl = AM * (float)GenerateRandomNumber();
    if (fl > RNMX) fl = RNMX;
    return fl * (hi - lo) + lo;
  }
  int RandomInt(int lo, int hi) {
    unsigned int x = (unsigned int)(hi - lo) + 1;
    if (x <= 1) return lo;
    unsigned int maxAcceptable = 0x7FFFFFFFu - ((0x7FFFFFFFu + 1u) % x);
    unsigned int n;
    do { n = (unsigned int)GenerateRandomNumber(); } while (n > maxAcceptable);
    return lo + (int)(n % x);
  }
private:
  static constexpr float AM = 1.0f / IM, EPS = 1.2e-7f, RNMX = 1.0f - EPS;
  int GenerateRandomNumber() {
    int j, k;
    if (m_idum <= 0 || !m_iy) {
      if (-(m_idum) < 1) m_idum = 1; else m_idum = -(m_idum);
      for (j = NTAB + 7; j >= 0; j--) {
        k = (m_idum) / IQ; m_idum = IA * (m_idum - k * IQ) - IR * k;
        if (m_idum < 0) m_idum += IM;
        if (j < NTAB) m_iv[j] = m_idum;
      }
      m_iy = m_iv[0];
    }
    k = (m_idum) / IQ; m_idum = IA * (m_idum - k * IQ) - IR * k;
    if (m_idum < 0) m_idum += IM;
    j = m_iy / NDIV; if (j >= NTAB || j < 0) j = (j % NTAB) & 0x7fffffff;
    m_iy = m_iv[j]; m_iv[j] = m_idum;
    return m_iy;
  }
  int m_idum = 0, m_iy = 0, m_iv[NTAB] = {};
};

// global stream (like vstdlib RandomFloat / RandomInt / RandomSeed)
inline CUniformRandomStream& GlobalRandom() { static CUniformRandomStream s; return s; }
inline void RandomSeed(int s) { GlobalRandom().SetSeed(s); }
inline float RandomFloat(float lo = 0.f, float hi = 1.f) { return GlobalRandom().RandomFloat(lo, hi); }
inline int RandomInt(int lo, int hi) { return GlobalRandom().RandomInt(lo, hi); }

// MD5_PseudoRandom-like hash for command_number -> random_seed
inline int MD5_PseudoRandom(unsigned int seed) {
  uint32_t x = seed * 2654435761u; x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return (int)(x & 0x7fffffff);
}
