#include "MomoHidParser.h"

namespace massmore {

namespace {

// Item prefix: bTag(4) | bType(2) | bSize(2)
const uint8_t kTypeMain   = 0;
const uint8_t kTypeGlobal = 1;
const uint8_t kTypeLocal  = 2;

uint32_t readUnsigned(const uint8_t* p, uint8_t n) {
  uint32_t v = 0;
  for (uint8_t i = 0; i < n; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  return v;
}

int32_t readSigned(const uint8_t* p, uint8_t n) {
  if (n == 0) return 0;
  uint32_t v = readUnsigned(p, n);
  const uint32_t signBit = 1u << (8 * n - 1);
  if (v & signBit) {
    // sign extend
    uint32_t mask = (n >= 4) ? 0u : (0xFFFFFFFFu << (8 * n));
    v |= mask;
  }
  return static_cast<int32_t>(v);
}

}  // namespace

void MomoHidParser::reset() {
  count_ = 0;
  reportCount_ = 0;
  usesReportIds_ = false;
  overflow_ = false;
  for (size_t i = 0; i < kMaxReports; ++i) {
    reportIds_[i] = 0;
    bitCursor_[i] = 0;
  }
}

void MomoHidParser::noteReportId(uint8_t reportId) {
  for (size_t i = 0; i < reportCount_; ++i) {
    if (reportIds_[i] == reportId) return;
  }
  if (reportCount_ < kMaxReports) {
    reportIds_[reportCount_] = reportId;
    bitCursor_[reportCount_] = 0;
    reportCount_++;
  } else {
    overflow_ = true;
  }
}

uint16_t& MomoHidParser::bitCursorFor(uint8_t reportId) {
  for (size_t i = 0; i < reportCount_; ++i) {
    if (reportIds_[i] == reportId) return bitCursor_[i];
  }
  noteReportId(reportId);
  for (size_t i = 0; i < reportCount_; ++i) {
    if (reportIds_[i] == reportId) return bitCursor_[i];
  }
  static uint16_t dummy = 0;
  dummy = 0;
  return dummy;
}

void MomoHidParser::addInputFields(const GlobalState& g, uint8_t flags,
                                   const uint32_t* usages, size_t usageCount,
                                   uint32_t usageMin, uint32_t usageMax,
                                   bool haveRange) {
  uint16_t& cursor = bitCursorFor(g.reportId);

  const bool variable = (flags & kFlagVariable) != 0;
  const bool constant = (flags & kFlagConstant) != 0;

  for (uint8_t i = 0; i < g.reportCount; ++i) {
    const uint16_t off = cursor;
    cursor = static_cast<uint16_t>(cursor + g.reportSize);

    if (constant) continue;  // padding: consume bits, store nothing
    if (count_ >= kMaxFields) {
      overflow_ = true;
      continue;
    }

    HidField f;
    f.reportId   = g.reportId;
    f.usagePage  = g.usagePage;
    f.bitOffset  = off;
    f.bitSize    = g.reportSize;
    f.logicalMin = g.logicalMin;
    f.logicalMax = g.logicalMax;
    f.flags      = flags;

    if (variable) {
      if (haveRange) {
        uint32_t u = usageMin + i;
        if (u > usageMax) u = usageMax;
        f.usage = static_cast<uint16_t>(u);
      } else if (usageCount > 0) {
        const size_t idx = (i < usageCount) ? i : usageCount - 1;
        f.usage = static_cast<uint16_t>(usages[idx] & 0xFFFF);
        if ((usages[idx] >> 16) != 0) f.usagePage = static_cast<uint16_t>(usages[idx] >> 16);
      }
      f.usageMin = f.usage;
      f.usageMax = f.usage;
    } else {
      // Array item: each slot reports one usage index out of [min..max].
      f.usage    = static_cast<uint16_t>(haveRange ? usageMin : (usageCount ? usages[0] & 0xFFFF : 0));
      f.usageMin = static_cast<uint16_t>(haveRange ? usageMin : f.usage);
      f.usageMax = static_cast<uint16_t>(haveRange ? usageMax : f.usage);
    }

    fields_[count_++] = f;
  }
}

bool MomoHidParser::parse(const uint8_t* desc, size_t len) {
  reset();
  if (desc == nullptr || len == 0) return false;

  GlobalState g = {0, 0, 0, 0, 0, 0};
  GlobalState stack[8];
  size_t stackDepth = 0;

  uint32_t usages[32];
  size_t   usageCount = 0;
  uint32_t usageMin = 0, usageMax = 0;
  bool     haveRange = false;

  size_t i = 0;
  bool ok = true;

  while (i < len) {
    const uint8_t prefix = desc[i++];

    if (prefix == 0xFE) {  // long item
      if (i + 2 > len) { ok = false; break; }
      const uint8_t dataSize = desc[i];
      i += 2 + dataSize;
      continue;
    }

    uint8_t size = prefix & 0x03;
    if (size == 3) size = 4;
    const uint8_t type = (prefix >> 2) & 0x03;
    const uint8_t tag  = (prefix >> 4) & 0x0F;

    if (i + size > len) { ok = false; break; }
    const uint8_t* data = desc + i;
    i += size;

    if (type == kTypeMain) {
      switch (tag) {
        case 0x8: {  // Input
          const uint8_t flags = static_cast<uint8_t>(readUnsigned(data, size) & 0xFF);
          addInputFields(g, flags, usages, usageCount, usageMin, usageMax, haveRange);
          break;
        }
        case 0x9:  // Output
        case 0xB:  // Feature
        case 0xA:  // Collection
        case 0xC:  // End Collection
        default:
          break;
      }
      // Locals are cleared after every main item.
      usageCount = 0;
      usageMin = usageMax = 0;
      haveRange = false;
      continue;
    }

    if (type == kTypeGlobal) {
      switch (tag) {
        case 0x0: g.usagePage   = static_cast<uint16_t>(readUnsigned(data, size)); break;
        case 0x1: g.logicalMin  = readSigned(data, size); break;
        case 0x2: g.logicalMax  = (g.logicalMin < 0) ? readSigned(data, size)
                                                     : static_cast<int32_t>(readUnsigned(data, size));
                  break;
        case 0x3: break;  // physical min
        case 0x4: break;  // physical max
        case 0x5: break;  // unit exponent
        case 0x6: break;  // unit
        case 0x7: g.reportSize  = static_cast<uint8_t>(readUnsigned(data, size)); break;
        case 0x8:
          g.reportId = static_cast<uint8_t>(readUnsigned(data, size));
          usesReportIds_ = true;
          noteReportId(g.reportId);
          break;
        case 0x9: g.reportCount = static_cast<uint8_t>(readUnsigned(data, size)); break;
        case 0xA:  // Push
          if (stackDepth < 8) stack[stackDepth++] = g;
          break;
        case 0xB:  // Pop
          if (stackDepth > 0) g = stack[--stackDepth];
          break;
        default: break;
      }
      continue;
    }

    if (type == kTypeLocal) {
      switch (tag) {
        case 0x0: {  // Usage
          // A 4-byte usage is "extended": high word = usage page.
          // 1/2-byte usages inherit the current global usage page.
          const uint32_t v = readUnsigned(data, size);
          if (usageCount < 32) {
            usages[usageCount++] = (size == 4) ? v : (v & 0xFFFF);
          }
          break;
        }
        case 0x1:  // Usage Minimum
          usageMin  = readUnsigned(data, size) & 0xFFFF;
          haveRange = true;
          break;
        case 0x2:  // Usage Maximum
          usageMax  = readUnsigned(data, size) & 0xFFFF;
          haveRange = true;
          break;
        default: break;  // designator/string items are irrelevant here
      }
      continue;
    }
  }

  if (reportCount_ == 0) noteReportId(0);
  return ok;
}

uint16_t MomoHidParser::reportSizeBytes(uint8_t reportId) const {
  for (size_t i = 0; i < reportCount_; ++i) {
    if (reportIds_[i] == reportId) {
      return static_cast<uint16_t>((bitCursor_[i] + 7) / 8);
    }
  }
  return 0;
}

const HidField* MomoHidParser::find(uint16_t page, uint16_t usage, uint8_t reportId) const {
  for (size_t i = 0; i < count_; ++i) {
    const HidField& f = fields_[i];
    if (reportId != 0xFF && f.reportId != reportId) continue;
    if (f.usagePage != page) continue;
    if (!f.isVariable()) continue;
    if (f.usage != usage) continue;
    return &f;
  }
  return nullptr;
}

const HidField* MomoHidParser::findButtonBlock(uint8_t reportId) const {
  for (size_t i = 0; i < count_; ++i) {
    const HidField& f = fields_[i];
    if (reportId != 0xFF && f.reportId != reportId) continue;
    if (f.usagePage == kPageButton) return &f;
  }
  return nullptr;
}

// ---------------------------------------------------------------------------

int32_t hidExtract(const uint8_t* data, size_t len, uint16_t bitOffset,
                   uint8_t bitSize, bool isSigned) {
  if (data == nullptr || bitSize == 0 || bitSize > 32) return 0;
  if (static_cast<size_t>(bitOffset + bitSize) > len * 8) return 0;

  uint32_t value = 0;
  for (uint8_t b = 0; b < bitSize; ++b) {
    const uint16_t bit = static_cast<uint16_t>(bitOffset + b);
    const uint8_t byteVal = data[bit >> 3];
    if (byteVal & (1u << (bit & 7))) value |= (1u << b);
  }

  if (isSigned && bitSize < 32) {
    const uint32_t signBit = 1u << (bitSize - 1);
    if (value & signBit) value |= (0xFFFFFFFFu << bitSize);
  }
  return static_cast<int32_t>(value);
}

}  // namespace massmore
