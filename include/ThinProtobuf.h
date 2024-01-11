//===- TemplightProtobufWriter.cpp -----------------------*- C++ -*--------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_THIN_PROTOBUF_H
#define LLVM_SUPPORT_THIN_PROTOBUF_H

#include <llvm/ADT/StringRef.h>
#include <llvm/Support/Endian.h>
#include <llvm/Support/raw_ostream.h>

#include <cstdint>
#include <string>

namespace llvm {

namespace protobuf {

namespace {

union float_to_ulong {
  float f;
  std::uint32_t ui32;
};

union double_to_ulong {
  double d;
  std::uint64_t ui64;
  std::uint32_t ui32[2];
};

union int64_to_uint64 {
  std::int64_t i64;
  std::uint64_t ui64;
};

} // namespace

inline std::uint64_t loadVarInt(StringRef &p_buf) {
  std::uint64_t u = 0;
  if (p_buf.empty())
    return u;
  std::uint8_t shifts = 0;
  while (p_buf.front() & 0x80) {
    u |= (p_buf.front() & 0x7F) << shifts;
    p_buf = p_buf.drop_front(1);
    if (p_buf.empty())
      return u;
    shifts += 7;
  };
  u |= (p_buf.front() & 0x7F) << shifts;
  p_buf = p_buf.drop_front(1);
  return u;
}

template <unsigned int tag> struct getVarIntWire {
  static const unsigned int value = (tag << 3);
};

template <unsigned int tag> struct getIntWire {
  static const unsigned int value = (tag << 3);
};

inline std::int64_t loadSInt(StringRef &p_buf) {
  std::uint64_t u = loadVarInt(p_buf);
  return (u >> 1) ^ (-static_cast<std::uint64_t>(u & 1));
}

template <unsigned int tag> struct getSIntWire {
  static const unsigned int value = (tag << 3);
};

inline double loadDouble(StringRef &p_buf) {
  if (p_buf.size() < sizeof(double_to_ulong)) {
    p_buf = p_buf.drop_front(p_buf.size());
    return double(0.0);
  };
  double_to_ulong tmp;
  std::memcpy(reinterpret_cast<char *>(&tmp), p_buf.data(),
              sizeof(double_to_ulong));
  p_buf = p_buf.drop_front(sizeof(double_to_ulong));
  tmp.ui64 =
      llvm::support::endian::byte_swap<std::uint64_t, llvm::endianness::little>(
          tmp.ui64);
  return tmp.d;
}

template <unsigned int tag> struct getDoubleWire {
  static const unsigned int value = (tag << 3) | 1;
};

inline float loadFloat(StringRef &p_buf) {
  if (p_buf.size() < sizeof(float_to_ulong)) {
    p_buf = p_buf.drop_front(p_buf.size());
    return float(0.0);
  };
  float_to_ulong tmp;
  std::memcpy(reinterpret_cast<char *>(&tmp), p_buf.data(),
              sizeof(float_to_ulong));
  p_buf = p_buf.drop_front(sizeof(float_to_ulong));
  tmp.ui32 =
      llvm::support::endian::byte_swap<std::uint32_t, llvm::endianness::little>(
          tmp.ui32);
  return tmp.f;
}

template <unsigned int tag> struct getFloatWire {
  static const unsigned int value = (tag << 3) | 5;
};

inline bool loadBool(StringRef &p_buf) {
  if (p_buf.empty())
    return false;
  char tmp = p_buf.front();
  p_buf = p_buf.drop_front(1);
  return tmp;
}

template <unsigned int tag> struct getBoolWire {
  static const unsigned int value = (tag << 3);
};

inline std::string loadString(StringRef &p_buf) {
  unsigned int u = loadVarInt(p_buf);
  if (p_buf.size() < u) {
    p_buf = p_buf.drop_front(p_buf.size());
    return std::string();
  };
  std::string s(p_buf.data(), u);
  p_buf = p_buf.drop_front(u);
  return s; // NRVO
}

template <unsigned int tag> struct getStringWire {
  static const unsigned int value = (tag << 3) | 2;
};

inline void skipData(StringRef &p_buf, unsigned int wire) {
  switch (wire & 0x7) {
  case 0:
    loadVarInt(p_buf);
    break;
  case 1:
    if (p_buf.size() < sizeof(double_to_ulong))
      p_buf = p_buf.drop_front(p_buf.size());
    else
      p_buf = p_buf.drop_front(sizeof(double_to_ulong));
    break;
  case 2: {
    unsigned int u = loadVarInt(p_buf);
    if (p_buf.size() < u)
      p_buf = p_buf.drop_front(p_buf.size());
    else
      p_buf = p_buf.drop_front(u);
    break;
  };
  case 5:
    if (p_buf.size() < sizeof(float_to_ulong))
      p_buf = p_buf.drop_front(p_buf.size());
    else
      p_buf = p_buf.drop_front(sizeof(float_to_ulong));
    break;
  default:
    break;
  }
}

inline void saveVarInt(llvm::raw_ostream &OS, std::uint64_t u) {
  std::uint8_t buf[] = {
      0, 0, 0, 0, 0,
      0, 0, 0, 0, 0}; // 80-bits, supports at most a 64-bit varint.
  std::uint8_t *pbuf = buf;
  *pbuf = (u & 0x7F);
  u >>= 7;
  while (u) {
    *pbuf |= 0x80; // set first msb because there is more to come.
    pbuf++;
    *pbuf = (u & 0x7F);
    u >>= 7;
  };
  OS.write(reinterpret_cast<char *>(buf), pbuf - buf + 1);
}

inline void saveVarInt(llvm::raw_ostream &OS, unsigned int tag,
                       std::uint64_t u) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  saveVarInt(OS, u);
}

inline void saveInt(llvm::raw_ostream &OS, unsigned int tag, std::int64_t i) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  int64_to_uint64 tmp;
  tmp.i64 = i;
  saveVarInt(OS, tmp.ui64);
}

inline void saveSInt(llvm::raw_ostream &OS, std::int64_t i) {
  // Apply the ZigZag encoding for the sign:
  saveVarInt(OS, (i << 1) ^ (i >> (sizeof(std::int64_t) * 8 - 1)));
}

inline void saveSInt(llvm::raw_ostream &OS, unsigned int tag, std::int64_t i) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: Varint.
  saveSInt(OS, i);
}

inline void saveDouble(llvm::raw_ostream &OS, double d) {
  double_to_ulong tmp = {d};
  tmp.ui64 =
      llvm::support::endian::byte_swap<std::uint64_t, llvm::endianness::little>(
          tmp.ui64);
  OS.write(reinterpret_cast<char *>(&tmp), sizeof(double_to_ulong));
}

inline void saveDouble(llvm::raw_ostream &OS, unsigned int tag, double d) {
  saveVarInt(OS, (tag << 3) | 1); // wire-type 1: 64-bit.
  saveDouble(OS, d);
}

inline void saveFloat(llvm::raw_ostream &OS, float d) {
  float_to_ulong tmp = {d};
  tmp.ui32 =
      llvm::support::endian::byte_swap<std::uint32_t, llvm::endianness::little>(
          tmp.ui32);
  OS.write(reinterpret_cast<char *>(&tmp), sizeof(float_to_ulong));
}

inline void saveFloat(llvm::raw_ostream &OS, unsigned int tag, float d) {
  saveVarInt(OS, (tag << 3) | 5); // wire-type 5: 32-bit.
  saveFloat(OS, d);
}

inline void saveBool(llvm::raw_ostream &OS, bool b) {
  char tmp = 0;
  if (b)
    tmp = 1;
  OS.write(&tmp, 1);
}

inline void saveBool(llvm::raw_ostream &OS, unsigned int tag, bool b) {
  saveVarInt(OS, (tag << 3)); // wire-type 0: varint.
  saveBool(OS, b);
}

inline void saveString(llvm::raw_ostream &OS, StringRef s) {
  unsigned int u = s.size();
  saveVarInt(OS, u);
  OS.write(s.data(), u);
}

inline void saveString(llvm::raw_ostream &OS, unsigned int tag, StringRef s) {
  saveVarInt(OS, (tag << 3) | 2); // wire-type 2: length-delimited.
  saveString(OS, s);
}

} // namespace protobuf

} // namespace llvm

#endif
