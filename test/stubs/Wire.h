#ifndef SI5351LITE_STUB_WIRE_H_
#define SI5351LITE_STUB_WIRE_H_

#include <stddef.h>
#include <stdint.h>

/* Host and AVR stand-in for the Arduino Wire bus. One object, no heap. */
class TwoWire {
 public:
  static const uint8_t kLogMax = 56;

  struct Log {
    uint8_t addr;
    uint8_t n;
    uint8_t buf[12];
    uint8_t result;
    uint8_t committed;
  };

  uint8_t regs[256];
  Log log[kLogMax];
  uint8_t log_n;
  uint8_t log_overflow;
  uint8_t begin_calls;
  uint16_t tx_calls;
  uint16_t end_calls;
  uint16_t read_calls;
  uint16_t fail_on_end;   /* 1-based endTransmission to NACK; 0 off */
  uint8_t partial_data;   /* data bytes kept when that NACK fires */
  uint16_t fail_read_n;   /* 1-based requestFrom to fail; 0 off */
  uint8_t arm_bulk_fail;  /* next multi-byte data write NACKs */
  uint8_t bulk_keep;      /* data bytes kept for that NACK */
  uint8_t sys_init_ones;  /* status reads that still report SYS_INIT */

  TwoWire();
  void reset();
  void clear_log();

  void begin();
  void beginTransmission(uint8_t addr);
  size_t write(uint8_t data);
  uint8_t endTransmission();
  uint8_t requestFrom(uint8_t addr, uint8_t qty);
  int available();
  int read();

 private:
  uint8_t addr_;
  uint8_t buf_[12];
  uint8_t buflen_;
  uint8_t ptr_;
  uint8_t rx_[8];
  uint8_t rx_n_;
  uint8_t rx_i_;
  uint8_t in_tx_;
};

extern TwoWire Wire;

#endif
