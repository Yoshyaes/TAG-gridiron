// The booth's spoken form: captions keep their digits ("3rd and 7 at the Austin 34", "21-14", "4:32"); the voice
// reads words ("third and seven at the Austin thirty-four", "twenty-one to fourteen", "four thirty-two"), so a speech
// synthesizer never has to guess at a number. Also the estimate of how long a line takes to say.
#include "Booth/Booth.h"

#include <cctype>
#include <cmath>

namespace tag {
namespace booth {

namespace {

const char* const kOnes[] = {"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                             "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"};
const char* const kTens[] = {"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"};

std::string bWords(int n) {
  if (n < 0) return "minus " + bWords(-n);
  if (n < 20) return kOnes[n];
  if (n < 100) return std::string(kTens[n / 10]) + (n % 10 ? std::string("-") + kOnes[n % 10] : std::string());
  if (n < 1000) return std::string(kOnes[n / 100]) + " hundred" + (n % 100 ? " " + bWords(n % 100) : std::string());
  if (n < 10000) return bWords(n / 1000) + " thousand" + (n % 1000 ? " " + bWords(n % 1000) : std::string());
  return std::to_string(n);
}

std::string bOrdinal(int n) {
  static const char* const kOrd[] = {"zeroth", "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth",
                                     "tenth", "eleventh", "twelfth"};
  if (n >= 0 && n <= 12) return kOrd[n];
  std::string w = bWords(n);
  if (!w.empty() && w.back() == 'y') return w.substr(0, w.size() - 1) + "ieth";
  return w + "th";
}

bool bDigit(char c) { return c >= '0' && c <= '9'; }
bool bAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }

}  // namespace

const char* kindName(Kind k) {
  switch (k) {
    case Kind::Open: return "open";
    case Kind::Setup: return "setup";
    case Kind::Read: return "read";
    case Kind::Live: return "live";
    case Kind::Result: return "result";
    case Kind::Analysis: return "analysis";
    case Kind::Story: return "story";
    case Kind::Score: return "score";
    case Kind::Break: return "break";
    case Kind::Close: return "close";
  }
  return "?";
}

std::string Booth::spoken(const std::string& text) {
  std::string out;
  const size_t n = text.size();
  size_t i = 0;
  while (i < n) {
    const char c = text[i];
    // "#72" -> "number seventy-two"
    if (c == '#' && i + 1 < n && bDigit(text[i + 1])) {
      size_t j = i + 1;
      int v = 0;
      while (j < n && bDigit(text[j])) v = v * 10 + (text[j++] - '0');
      out += "number " + bWords(v);
      i = j;
      continue;
    }
    if (bDigit(c) && (i == 0 || !bAlpha(text[i - 1]))) {
      size_t j = i;
      int v = 0;
      while (j < n && bDigit(text[j])) v = v * 10 + (text[j++] - '0');
      // a clock: m:ss
      if (j + 2 < n && text[j] == ':' && bDigit(text[j + 1]) && bDigit(text[j + 2])) {
        const int ss = (text[j + 1] - '0') * 10 + (text[j + 2] - '0');
        if (v == 0) out += bWords(ss) + (ss == 1 ? " second" : " seconds");
        else if (ss == 0) out += bWords(v) + (v == 1 ? " minute" : " minutes");
        else if (ss < 10) out += bWords(v) + " oh " + bWords(ss);
        else out += bWords(v) + " " + bWords(ss);
        i = j + 3;
        continue;
      }
      // a score: 21-14
      if (j + 1 < n && text[j] == '-' && bDigit(text[j + 1])) {
        size_t k = j + 1;
        int w = 0;
        while (k < n && bDigit(text[k])) w = w * 10 + (text[k++] - '0');
        out += bWords(v) + " to " + bWords(w);
        i = k;
        continue;
      }
      // an ordinal: 3rd, 21st
      if (j + 1 < n && bAlpha(text[j]) && bAlpha(text[j + 1])) {
        const std::string suf = text.substr(j, 2);
        if (suf == "st" || suf == "nd" || suf == "rd" || suf == "th") {
          out += bOrdinal(v);
          i = j + 2;
          continue;
        }
      }
      // "52-yard" -> "fifty-two yard"
      out += bWords(v);
      if (j < n && text[j] == '-' && j + 1 < n && bAlpha(text[j + 1])) {
        out += ' ';
        ++j;
      }
      i = j;
      continue;
    }
    // ALL-CAPS words (captions shout "TOUCHDOWN") read as words, not letters
    if (bAlpha(c) && std::isupper(static_cast<unsigned char>(c)) && (i == 0 || !bAlpha(text[i - 1]))) {
      size_t j = i;
      bool allCaps = true;
      while (j < n && bAlpha(text[j])) {
        if (!std::isupper(static_cast<unsigned char>(text[j]))) allCaps = false;
        ++j;
      }
      if (allCaps && j - i > 1) {
        out += c;
        for (size_t k = i + 1; k < j; ++k) out += static_cast<char>(std::tolower(static_cast<unsigned char>(text[k])));
        i = j;
        continue;
      }
      out.append(text, i, j - i);
      i = j;
      continue;
    }
    out += c;
    ++i;
  }
  return out;
}

double Booth::estimate(const std::string& say, Who who, int energy) {
  int words = 0, pauses = 0;
  bool in = false;
  for (size_t i = 0; i < say.size(); ++i) {
    const char c = say[i];
    const bool w = bAlpha(c) || c == '\'';
    if (w && !in) ++words;
    in = w;
    if (c == '.' || c == '!' || c == '?' || c == ';') ++pauses;
    if (c == ',') pauses += 0;
  }
  // a broadcaster's pace: ~2.7 words a second in a setup, faster when the play is on, the analyst a touch slower
  double wps = who == Who::PBP ? 2.75 : 2.6;
  wps += 0.25 * energy;
  return 0.2 + words / wps + 0.18 * pauses;
}

}  // namespace booth
}  // namespace tag
