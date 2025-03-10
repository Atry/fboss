// Copyright 2021-present Facebook. All Rights Reserved.
#include "ModbusCmds.h"
#include <algorithm>

namespace rackmon {

void Response::checkValue(
    const std::string& what,
    uint32_t value,
    uint32_t expectedValue) {
  if (value != expectedValue) {
    throw BadResponseError(what, expectedValue, value);
  }
}

void Response::decode() {
  validate();
  // Error response is structured as:
  // addr(1), errorFunc(1), errorCode(1)
  // Where errorFunc is the response function with
  // the last bit set to 1 (Hence mask: 0x80).
  bool isError = len == 3 && (respFunction & 0x80) != 0;
  if (isError) {
    throw ModbusError(raw[2]);
  }
}

ReadHoldingRegistersReq::ReadHoldingRegistersReq(
    uint8_t deviceAddr,
    uint16_t registerOffset,
    uint16_t registerCount)
    : deviceAddr_(deviceAddr),
      registerOffset_(registerOffset),
      registerCount_(registerCount) {
  addr = deviceAddr;
}

void ReadHoldingRegistersReq::encode() {
  *this << deviceAddr_ << kExpectedFunction << registerOffset_
        << registerCount_;
  finalize();
}

ReadHoldingRegistersResp::ReadHoldingRegistersResp(
    uint8_t deviceAddr,
    std::vector<uint16_t>& regs)
    : expectedDeviceAddr_(deviceAddr), regs_(regs) {
  if (!regs.size()) {
    throw std::underflow_error("Response too small");
  }
  // addr(1), func(1), bytecount(1), <2 * count regs>, crc(2)
  len = 5 + (2 * regs.size());
}

void ReadHoldingRegistersResp::decode() {
  // addr(1), func(1), count(1), <2 * count regs>, crc(2)
  Response::decode();
  uint8_t byteCount, function, deviceAddr;
  *this >> regs_ >> byteCount >> function >> deviceAddr;
  checkValue("deviceAddr", deviceAddr, expectedDeviceAddr_);
  checkValue("function", function, kExpectedFunction);
  checkValue("byteCount", byteCount, regs_.size() * 2);
}

WriteSingleRegisterReq::WriteSingleRegisterReq(
    uint8_t deviceAddr,
    uint16_t registerOffset,
    uint16_t value)
    : deviceAddr_(deviceAddr), registerOffset_(registerOffset), value_(value) {
  addr = deviceAddr;
}

void WriteSingleRegisterReq::encode() {
  *this << deviceAddr_ << kExpectedFunction << registerOffset_ << value_;
  finalize();
}

WriteSingleRegisterResp::WriteSingleRegisterResp(
    uint8_t deviceAddr,
    uint16_t registerOffset)
    : expectedDeviceAddr_(deviceAddr), expectedRegisterOffset_(registerOffset) {
  // addr(1), func(1), reg(2), value(2), crc(2)
  len = 8;
}

WriteSingleRegisterResp::WriteSingleRegisterResp(
    uint8_t deviceAddr,
    uint16_t registerOffset,
    uint16_t expectedValue)
    : expectedDeviceAddr_(deviceAddr),
      expectedRegisterOffset_(registerOffset),
      expectedValue_(expectedValue) {
  // addr(1), func(1), reg(2), value(2), crc(2)
  len = 8;
}

void WriteSingleRegisterResp::decode() {
  Response::decode();
  uint16_t registerOffset;
  uint8_t function, deviceAddr;
  *this >> value_ >> registerOffset >> function >> deviceAddr;
  checkValue("function", function, kExpectedFunction);
  checkValue("deviceAddr", deviceAddr, expectedDeviceAddr_);
  checkValue("registerOffset", registerOffset, expectedRegisterOffset_);
  if (expectedValue_) {
    checkValue("value", value_, expectedValue_.value());
  }
}

WriteMultipleRegistersReq::WriteMultipleRegistersReq(
    uint8_t deviceAddr,
    uint16_t registerOffset)
    : deviceAddr_(deviceAddr), registerOffset_(registerOffset) {
  // addr(1), function(1), reg_start(2), reg_count(2), bytes(1)
  addr = deviceAddr;
  // Start pointing to after the header to allow for
  // the user to start streaming arbitrary data to it.
  len = 7;
}

void WriteMultipleRegistersReq::encode() {
  if (len <= 7) {
    throw std::underflow_error("No registers to write");
  }
  // Compute exactly how much data/registers the user
  // had streamed/encoded into the message.
  uint8_t dataLen = len - 7;
  // Pad if the result does not fit in whole 16bit regs.
  // XXX We probably need to throw here instead of padding.
  if ((dataLen % 2) != 0) {
    *this << uint8_t(0);
    dataLen += 1;
  }
  // Count the total registers.
  uint16_t registerCount = dataLen / 2;
  len = 0; // Clear so we can get to the header
  *this << deviceAddr_ << kExpectedFunction << registerOffset_ << registerCount
        << dataLen;
  len += dataLen;
  finalize();
}

WriteMultipleRegistersResp::WriteMultipleRegistersResp(
    uint8_t deviceAddr,
    uint16_t registerOffset,
    uint16_t registerCount)
    : expectedDeviceAddr_(deviceAddr),
      expectedRegisterOffset_(registerOffset),
      expectedRegisterCount_(registerCount) {
  // addr(1), func(1), reg_off(2), reg_count(2), crc(2)
  len = 8;
}

void WriteMultipleRegistersResp::decode() {
  // addr(1), func(1), off(2), count(2), crc(2)
  Response::decode();
  uint16_t registerCount, registerOffset;
  uint8_t function, deviceAddr;
  // Pop fields from behind
  *this >> registerCount >> registerOffset >> function >> deviceAddr;
  checkValue("deviceAddr_", deviceAddr, expectedDeviceAddr_);
  checkValue("function", function, kExpectedFunction);
  checkValue("registerOffset", registerOffset, expectedRegisterOffset_);
  checkValue("registerCount", registerCount, expectedRegisterCount_);
}

ReadFileRecordReq::ReadFileRecordReq(
    uint8_t deviceAddr,
    const std::vector<FileRecord>& records)
    : deviceAddr_(deviceAddr), records_(records) {
  addr = deviceAddr;
}

void ReadFileRecordReq::encode() {
  *this << deviceAddr_ << kExpectedFunction << uint8_t(0);
  uint8_t& bytes = raw[2];
  for (auto& rec : records_) {
    *this << kReferenceType << rec.fileNum << rec.recordNum
          << uint16_t(rec.data.size());
  }
  bytes = len - 3;
  finalize();
}

ReadFileRecordResp::ReadFileRecordResp(
    uint8_t deviceAddr,
    std::vector<FileRecord>& records)
    : deviceAddr_(deviceAddr), records_(records) {
  // addr(1), func(1), bytes(1) ... CRC(2)
  len = 5;
  for (auto& r : records) {
    // len(1), type(1), data(N * 2)
    len += 2 + (2 * r.data.size());
  }
}

void ReadFileRecordResp::decode() {
  Response::decode();
  // len includes addr,func,data_len, so get the expected
  // data_len by subtracting 3 from the length after chopping
  // off the CRC.
  uint8_t bytesExp = len - 3;
  for (auto it = records_.rbegin(); it != records_.rend(); it++) {
    uint8_t referenceType, fieldSize;
    FileRecord& rec = *it;
    *this >> rec.data >> referenceType >> fieldSize;
    checkValue("referenceType", referenceType, kReferenceType);
    checkValue("fieldSize", fieldSize, 1 + (rec.data.size() * 2));
  }
  uint8_t dataLen, deviceAddr, function;
  *this >> dataLen >> function >> deviceAddr;
  checkValue("dataLen", dataLen, bytesExp);
  checkValue("function", function, kExpectedFunction);
  checkValue("deviceAddr", deviceAddr, deviceAddr_);
  checkValue("length", len, 0);
}

} // namespace rackmon
