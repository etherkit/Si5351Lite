#include "Wire.h"

#ifndef SI5351LITE_AVR
TwoWire Wire;
#endif

TwoWire::TwoWire() {
  reset();
}

void TwoWire::reset() {
  for (uint16_t i = 0; i < 256; i++) {
    regs[i] = 0xA5;
  }
  regs[0] = 0;
  regs[3] = 0x00;
  regs[15] = 0x0C;
  regs[149] = 0x95;
  regs[162] = 0xFF;
  regs[163] = 0xFF;
  regs[164] = 0xFF;
  for (uint8_t i = 26; i < 42; i++) {
    regs[i] = 0x11;
  }
  log_n = 0;
  log_overflow = 0;
  begin_calls = 0;
  tx_calls = 0;
  end_calls = 0;
  read_calls = 0;
  fail_on_end = 0;
  partial_data = 0;
  fail_read_n = 0;
  arm_bulk_fail = 0;
  bulk_keep = 0;
  sys_init_ones = 0;
  addr_ = 0;
  buflen_ = 0;
  ptr_ = 0;
  rx_n_ = 0;
  rx_i_ = 0;
  in_tx_ = 0;
}

void TwoWire::clear_log() {
  log_n = 0;
  log_overflow = 0;
}

void TwoWire::begin() {
  begin_calls++;
}

void TwoWire::beginTransmission(uint8_t addr) {
  addr_ = addr;
  buflen_ = 0;
  in_tx_ = 1;
  tx_calls++;
}

size_t TwoWire::write(uint8_t data) {
  if (!in_tx_ || buflen_ >= 12) {
    return 0;
  }
  buf_[buflen_++] = data;
  return 1;
}

uint8_t TwoWire::endTransmission() {
  end_calls++;
  uint8_t fail = 0;
  uint8_t keep = 0;
  if (buflen_ > 1) {
    keep = (uint8_t)(buflen_ - 1);
  }
  if (fail_on_end != 0 && end_calls == fail_on_end) {
    fail = 1;
    keep = partial_data;
    if (buflen_ <= 1) {
      keep = 0;
    } else if (keep > (uint8_t)(buflen_ - 1)) {
      keep = (uint8_t)(buflen_ - 1);
    }
  } else if (arm_bulk_fail && buflen_ > 2) {
    fail = 1;
    arm_bulk_fail = 0;
    keep = bulk_keep;
    if (keep > (uint8_t)(buflen_ - 1)) {
      keep = (uint8_t)(buflen_ - 1);
    }
  }

  uint8_t committed = 0;
  if (!fail && buflen_ == 1) {
    ptr_ = buf_[0];
  } else if (buflen_ > 1) {
    uint8_t reg = buf_[0];
    for (uint8_t i = 0; i < keep; i++) {
      regs[(uint8_t)(reg + i)] = buf_[1 + i];
    }
    committed = keep;
    if (!fail) {
      ptr_ = reg;
    }
  }

  if (log_n < kLogMax) {
    Log &slot = log[log_n++];
    slot.addr = addr_;
    slot.n = buflen_;
    for (uint8_t i = 0; i < 12; i++) {
      slot.buf[i] = (i < buflen_) ? buf_[i] : 0;
    }
    slot.result = fail ? 2 : 0;
    slot.committed = committed;
  } else {
    log_overflow = 1;
  }

  buflen_ = 0;
  in_tx_ = 0;
  return fail ? 2 : 0;
}

uint8_t TwoWire::requestFrom(uint8_t addr, uint8_t qty) {
  (void)addr;
  read_calls++;
  rx_n_ = 0;
  rx_i_ = 0;
  if (fail_read_n != 0 && read_calls == fail_read_n) {
    return 0;
  }
  if (qty == 0) {
    return 0;
  }
  if (qty > 8) {
    qty = 8;
  }
  for (uint8_t i = 0; i < qty; i++) {
    uint8_t v = regs[(uint8_t)(ptr_ + i)];
    if (i == 0 && ptr_ == 0 && sys_init_ones > 0) {
      v = 0x80;
      sys_init_ones--;
    }
    rx_[i] = v;
  }
  rx_n_ = qty;
  return qty;
}

int TwoWire::available() {
  if (rx_i_ >= rx_n_) {
    return 0;
  }
  return (int)(rx_n_ - rx_i_);
}

int TwoWire::read() {
  if (rx_i_ >= rx_n_) {
    return -1;
  }
  return (int)rx_[rx_i_++];
}
