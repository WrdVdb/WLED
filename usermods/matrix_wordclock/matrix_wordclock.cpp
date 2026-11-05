// WLED usermod: shows the time in words, based on the clock JSON file from the word clock matrix app.
#include "wled.h"
#include "wordclock_core.h"

class MatrixWordClock : public Usermod {
 private:
  // --- settings (Config -> Usermods) ---
  enum Mode : uint8_t { WORDS_ANIMATED = 0, WORDS_FIXED = 1 };
  bool    enabled    = true;
  String  file       = "/clock.json";  // file on the ESP32 LittleFS
  int     ledOffset  = 0;              // first LED of the matrix (if the matrix does not start at LED 0)
  uint8_t mode       = WORDS_ANIMATED; // 0: words follow the animation, rest black
                                       // 1: whole matrix follows the animation, words have a fixed colour
  String  wordColorS = "FFFFFF";       // fixed word colour (RRGGBB), mode 1
  uint8_t unlit      = 0;              // mode 0: brightness of the non-word LEDs (0 = black, 255 = unchanged)
  uint8_t bgBright   = 255;            // mode 1: brightness of the animated background (255 = unchanged)
  uint32_t wordColor = RGBW32(255, 255, 255, 0);  // wordColorS as a WLED colour

  // --- state ---
  wordclock::Data data;
  bool ready = false;
  int  lastKey = -1;                   // hour*60+minute of the last computed frame
  std::vector<uint8_t> lit;

  static const char _name[];

  static uint32_t parseColor(String t) {
    t.trim();
    if (t.startsWith("#")) t = t.substring(1);
    uint32_t v = 0xFFFFFF;
    if (t.length() == 6) { char* end; unsigned long x = strtoul(t.c_str(), &end, 16); if (*end == 0) v = (uint32_t)x; }
    return RGBW32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, 0);
  }

  bool loadFile() {
    ready = false; lastKey = -1;
    File f = WLED_FS.open(file, "r");
    if (!f) { DEBUG_PRINTF("[MWC] %s not found\n", file.c_str()); return false; }
#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument doc;
#else
    DynamicJsonDocument doc(8192);
#endif
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) { DEBUG_PRINTF("[MWC] JSON error: %s\n", err.c_str()); return false; }

    JsonArray w = doc["w"];
    const char* t = doc["t"];
    if (w.isNull() || !t || w.size() < 2 || (w.size() & 1)) return false;

    wordclock::Data d;
    size_t W = w.size() / 2;
    d.bpm = (uint8_t)((W + 7) / 8);
    for (size_t i = 0; i < W; i++) {
      d.wStart.push_back(w[2*i].as<uint16_t>());
      d.wLen.push_back(w[2*i + 1].as<uint8_t>());
    }
    d.masks.assign(288u * d.bpm, 0);
    if (wordclock::b64decode(t, d.masks.data(), d.masks.size()) < d.masks.size()) return false;

    JsonArray e = doc["e"];                     // optional: minute marks + 1 2 3 4
    if (!e.isNull() && e.size() == 5) {
      for (int i = 0; i < 5; i++) d.marks[i] = e[i].as<uint16_t>();
      d.hasMarks = true;
    }
    data = std::move(d);
    ready = true;
    DEBUG_PRINTF("[MWC] loaded: %u words\n", (unsigned)W);
    return true;
  }

 public:
  void setup() override {}
  void loop() override {}

  // Called every frame, after the effects have been drawn.
  //  mode 0: words keep the animation, all other LEDs go dark (or dim)
  //  mode 1: every LED keeps the animation, the words are overwritten with the fixed colour
  void handleOverlayDraw() override {
    if (!enabled || !ready) return;
    if (toki.getTimeSource() == TOKI_TS_NONE) return;   // no valid time yet: show the normal effects
    updateLocalTime();
    int h = hour(localTime), m = minute(localTime);
    const size_t len = strip.getLengthTotal();
    if (h * 60 + m != lastKey || lit.size() != len) {
      lit.assign(len, 0);
      wordclock::buildLit(data, h, m, ledOffset, lit);
      lastKey = h * 60 + m;
    }
    for (size_t i = 0; i < len; i++) {
      if (mode == WORDS_FIXED) {
        if (lit[i])                strip.setPixelColor(i, wordColor);
        else if (bgBright < 255)   strip.setPixelColor(i, color_fade(strip.getPixelColor(i), bgBright));
      } else if (!lit[i]) {
        strip.setPixelColor(i, unlit ? color_fade(strip.getPixelColor(i), unlit) : 0);
      }
    }
  }

  // Reload the file without a reboot: send {"mwc":{"reload":true}} to /json/state
  void readFromJsonState(JsonObject& root) override {
    JsonObject o = root[F("mwc")];
    if (!o.isNull() && (o[F("reload")] | false)) loadFile();
  }

  void addToConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)].to<JsonObject>();
    top[F("enabled")]   = enabled;
    top[F("file")]      = file;
    top[F("ledOffset")] = ledOffset;
    top[F("mode")]      = mode;
    top[F("wordColor")] = wordColorS;
    top[F("unlit")]     = unlit;
    top[F("bgBrightness")] = bgBright;
  }

  // Turns the "mode" field into a dropdown on the usermod settings page
  void appendConfigData() override {
    oappend(SET_F("dd=addDropdown('MatrixWordClock','mode');"));
    oappend(SET_F("addOption(dd,'Words follow animation, rest black',0);"));
    oappend(SET_F("addOption(dd,'Whole matrix follows animation, words fixed colour',1);"));
    oappend(SET_F("addInfo('MatrixWordClock:wordColor',1,'RRGGBB, mode 1 only');"));
    oappend(SET_F("addInfo('MatrixWordClock:unlit',1,'mode 0 only: 0 = black');"));
    oappend(SET_F("addInfo('MatrixWordClock:bgBrightness',1,'mode 1 only: 255 = full animation');"));
  }

  bool readFromConfig(JsonObject& root) override {
    JsonObject top = root[FPSTR(_name)];
    bool ok = !top.isNull();
    ok &= getJsonValue(top[F("enabled")],   enabled,   true);
    ok &= getJsonValue(top[F("file")],      file,      String("/clock.json"));
    ok &= getJsonValue(top[F("ledOffset")], ledOffset, 0);
    ok &= getJsonValue(top[F("mode")],      mode,      (uint8_t)WORDS_ANIMATED);
    ok &= getJsonValue(top[F("wordColor")], wordColorS, String("FFFFFF"));
    ok &= getJsonValue(top[F("unlit")],     unlit,     (uint8_t)0);
    ok &= getJsonValue(top[F("bgBrightness")], bgBright, (uint8_t)255);
    if (mode > WORDS_FIXED) mode = WORDS_ANIMATED;
    wordColor = parseColor(wordColorS);
    if (!file.startsWith("/")) file = "/" + file;
    loadFile();                                   // reload after every configuration change
    return ok;
  }

  uint16_t getId() override { return USERMOD_ID_UNSPECIFIED; }
};

const char MatrixWordClock::_name[] PROGMEM = "MatrixWordClock";
static MatrixWordClock matrixWordClock;
REGISTER_USERMOD(matrixWordClock);
