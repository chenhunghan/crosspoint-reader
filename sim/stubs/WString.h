#pragma once
// CrossPoint simulator: Arduino String over std::string (just the API the
// compiled sources and ArduinoJson's String adapter use).

#include <cstdlib>
#include <cstring>
#include <string>

#include "Print.h"

class String {
 public:
  String() = default;
  String(const char* s) : s_(s ? s : "") {}
  String(const char* s, size_t n) : s_(s ? std::string(s, n) : std::string()) {}
  explicit String(const std::string& s) : s_(s) {}
  String(char c) : s_(1, c) {}
  String(int v) : s_(std::to_string(v)) {}
  String(unsigned v) : s_(std::to_string(v)) {}
  String(long v) : s_(std::to_string(v)) {}
  String(unsigned long v) : s_(std::to_string(v)) {}
  String(long long v) : s_(std::to_string(v)) {}
  String(unsigned long long v) : s_(std::to_string(v)) {}
  String(double v, unsigned int decimals = 2) {
    char b[64];
    snprintf(b, sizeof(b), "%.*f", decimals, v);
    s_ = b;
  }

  String& operator=(const char* s) {
    s_ = s ? s : "";
    return *this;
  }

  const char* c_str() const { return s_.c_str(); }
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  bool reserve(size_t n) {
    s_.reserve(n);
    return true;
  }
  bool concat(const char* s) {
    if (s) s_ += s;
    return true;
  }
  bool concat(const char* s, size_t n) {
    if (s) s_.append(s, n);
    return true;
  }
  bool concat(char c) {
    s_ += c;
    return true;
  }
  bool concat(const String& o) {
    s_ += o.s_;
    return true;
  }
  String& operator+=(const String& o) {
    s_ += o.s_;
    return *this;
  }
  String& operator+=(const char* o) {
    if (o) s_ += o;
    return *this;
  }
  String& operator+=(char c) {
    s_ += c;
    return *this;
  }
  friend String operator+(const String& a, const String& b) { return String(a.s_ + b.s_); }
  friend String operator+(const String& a, const char* b) { return String(a.s_ + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String(std::string(a ? a : "") + b.s_); }
  bool operator==(const String& o) const { return s_ == o.s_; }
  bool operator==(const char* o) const { return s_ == (o ? o : ""); }
  bool operator!=(const String& o) const { return s_ != o.s_; }
  bool operator!=(const char* o) const { return !(*this == o); }
  bool operator<(const String& o) const { return s_ < o.s_; }
  char operator[](size_t i) const { return i < s_.size() ? s_[i] : 0; }
  char& operator[](size_t i) { return s_[i]; }
  char charAt(size_t i) const { return (*this)[i]; }
  bool startsWith(const String& p) const { return s_.rfind(p.s_, 0) == 0; }
  bool endsWith(const String& p) const {
    return s_.size() >= p.s_.size() && s_.compare(s_.size() - p.s_.size(), p.s_.size(), p.s_) == 0;
  }
  int indexOf(char c, size_t from = 0) const {
    auto p = s_.find(c, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int indexOf(const String& t, size_t from = 0) const {
    auto p = s_.find(t.s_, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int lastIndexOf(char c) const {
    auto p = s_.rfind(c);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  String substring(size_t from) const { return from >= s_.size() ? String() : String(s_.substr(from)); }
  String substring(size_t from, size_t to) const {
    if (from > to) std::swap(from, to);
    if (from >= s_.size()) return String();
    return String(s_.substr(from, to - from));
  }
  void trim() {
    const auto b = s_.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
      s_.clear();
      return;
    }
    s_ = s_.substr(b, s_.find_last_not_of(" \t\r\n") - b + 1);
  }
  void toLowerCase() {
    for (auto& c : s_) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  }
  void toUpperCase() {
    for (auto& c : s_) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
  }
  long toInt() const { return strtol(s_.c_str(), nullptr, 10); }
  float toFloat() const { return strtof(s_.c_str(), nullptr); }
  void remove(size_t index, size_t count = std::string::npos) {
    if (index < s_.size()) s_.erase(index, count);
  }
  void replace(const String& from, const String& to) {
    if (from.s_.empty()) return;
    size_t pos = 0;
    while ((pos = s_.find(from.s_, pos)) != std::string::npos) {
      s_.replace(pos, from.s_.size(), to.s_);
      pos += to.s_.size();
    }
  }
  const std::string& str() const { return s_; }


 private:
  std::string s_;
};

inline size_t Print::print(const String& s) { return write(s.c_str()); }
