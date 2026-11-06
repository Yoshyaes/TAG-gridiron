// Verification-only forced include (cl /FI): routes std::exp/log/sin/cos/atan2 in the Sim sources
// to RefMath.cpp. All std headers are included first so library templates are unaffected.
#pragma once
#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
namespace std { double refm_exp(double); double refm_log(double); double refm_sin(double); double refm_cos(double); double refm_atan2(double, double); }
#define exp(x) refm_exp(x)
#define log(x) refm_log(x)
#define sin(x) refm_sin(x)
#define cos(x) refm_cos(x)
#define atan2(y, x) refm_atan2(y, x)
