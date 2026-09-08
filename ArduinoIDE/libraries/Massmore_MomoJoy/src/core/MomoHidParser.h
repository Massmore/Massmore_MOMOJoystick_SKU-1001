// Massmore_MomoJoy - HID Report Descriptor parser (pure C++, no dynamic allocation)
//
// Parses a BLE HID "Report Map" (characteristic 0x2A4B) into a flat list of
// input fields so that any BLE gamepad can be decoded generically instead of
// hard-coding one vendor's byte layout.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MomoHidUsage.h"

namespace massmore {

// One decoded INPUT field inside a report.
struct HidField {
  uint8_t  reportId   = 0;      // 0 when the descriptor uses no report IDs
  uint16_t usagePage  = 0;
  uint16_t usage      = 0;      // for arrays: usageMin
  uint16_t usageMin   = 0;
  uint16_t usageMax   = 0;
  uint16_t bitOffset  = 0;      // offset inside the report payload (ID byte excluded)
  uint8_t  bitSize    = 0;
  int32_t  logicalMin = 0;
  int32_t  logicalMax = 0;
  uint8_t  flags      = 0;      // HidItemFlag bitmask

  bool isVariable() const { return (flags & kFlagVariable) != 0; }
  bool isConstant() const { return (flags & kFlagConstant) != 0; }
  bool isSigned()   const { return logicalMin < 0; }
};

class MomoHidParser {
 public:
  static const size_t kMaxFields = 96;
  static const size_t kMaxReports = 8;

  // Parse a report map. Returns false only on a malformed descriptor;
  // fields found before the error are still available.
  bool parse(const uint8_t* desc, size_t len);

  void   reset();
  size_t fieldCount() const { return count_; }
  const HidField& field(size_t i) const { return fields_[i]; }

  bool  usesReportIds() const { return usesReportIds_; }
  size_t reportCount() const { return reportCount_; }
  uint8_t reportIdAt(size_t i) const { return reportIds_[i]; }

  // Total payload length in bytes for a given report ID (ID byte excluded).
  uint16_t reportSizeBytes(uint8_t reportId) const;

  // Find the first non-constant field matching page/usage.
  // reportId 0xFF = any report.
  const HidField* find(uint16_t page, uint16_t usage, uint8_t reportId = 0xFF) const;

  // Find the field block that declares buttons (usage page 0x09).
  const HidField* findButtonBlock(uint8_t reportId = 0xFF) const;

  bool overflowed() const { return overflow_; }

 private:
  struct GlobalState {
    uint16_t usagePage;
    int32_t  logicalMin;
    int32_t  logicalMax;
    uint8_t  reportSize;
    uint8_t  reportCount;
    uint8_t  reportId;
  };

  void addInputFields(const GlobalState& g, uint8_t flags,
                      const uint32_t* usages, size_t usageCount,
                      uint32_t usageMin, uint32_t usageMax, bool haveRange);
  uint16_t& bitCursorFor(uint8_t reportId);
  void      noteReportId(uint8_t reportId);

  HidField fields_[kMaxFields];
  size_t   count_ = 0;

  uint8_t  reportIds_[kMaxReports] = {0};
  uint16_t bitCursor_[kMaxReports] = {0};
  size_t   reportCount_ = 0;

  bool usesReportIds_ = false;
  bool overflow_      = false;
};

// ---- Bit extraction --------------------------------------------------------

// Extract `bitSize` bits starting at `bitOffset` from `data` (LSB-first, as HID
// specifies) and sign-extend when the field's logical minimum is negative.
int32_t hidExtract(const uint8_t* data, size_t len, uint16_t bitOffset,
                   uint8_t bitSize, bool isSigned);

inline int32_t hidExtract(const uint8_t* data, size_t len, const HidField& f) {
  return hidExtract(data, len, f.bitOffset, f.bitSize, f.isSigned());
}

}  // namespace massmore
