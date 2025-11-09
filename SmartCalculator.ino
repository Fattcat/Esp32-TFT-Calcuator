/*
 * 🧮 ESP32 Smart Calculator s ILI9341 2.8" a XPT2046 dotykom
 * ✅ Blikajúci kurzor | ✅ Symbolické zlomky | ✅ 3-stránkové menu | ✅ História
 * ✅ TinyExpr + vlastný preprocesor zlomkov
 * Autor: podľa tvojich požiadaviek
 */

#include <TFT_eSPI.h>
#include <SPI.h>
#include <vector>
#include "tinyexpr.h"

TFT_eSPI tft = TFT_eSPI();

// ============== NASTAVENIA ==============
String inputStr = "";
std::vector<String> history;
int cursorPos = 0; // pozícia kurzoru v reťazci (znaková)
bool cursorVisible = true;
unsigned long lastCursorToggle = 0;
const long cursorInterval = 500; // ms

enum MenuPage { PAGE_BASIC, PAGE_ADVANCED, PAGE_EXTRA };
MenuPage currentPage = PAGE_BASIC;
bool menuOpen = false;

// Farby
#define COL_BG        TFT_BLACK
#define COL_INPUT_BG  TFT_DARKGREY
#define COL_INPUT_TXT TFT_WHITE
#define COL_BTN_NUM   TFT_DARKGREY
#define COL_BTN_OP    TFT_ORANGE
#define COL_BTN_FUNC  TFT_BLUE
#define COL_BTN_EQ    TFT_GREEN
#define COL_BTN_CLR   TFT_RED
#define COL_HIST      TFT_YELLOW

// ============== ŠTRUKTÚRA TLAČIDLA ==============
struct Button {
  int16_t x, y, w, h;
  String label;
  uint16_t color;
  std::function<void()> onClick;
  bool visible = true;
};

std::vector<Button> buttons;

// ============== POMOCNÉ FUNKCIE ==============

// Konvertuje reťazec s "a/b" → výsledok ako desatinné, ale ak je celé číslo/zlomok, zobrazí ako "3/4"
String fractionToDisplay(double val) {
  if (isnan(val) || isinf(val)) return "ERR";
  
  // Pokus o symbolický zlomok (presnosť ±0.001)
  const double eps = 0.001;
  const int maxDenom = 12;
  for (int d = 1; d <= maxDenom; d++) {
    for (int n = 0; n <= d * 10; n++) { // rozumne obmedzené
      double f = (double)n / d;
      if (fabs(val - f) < eps) {
        if (n % d == 0) return String(n / d);  // celé číslo
        return String(n) + "/" + String(d);
      }
    }
  }
  // Inak desatinné (max 6 miest, odstráni koncové nuly)
  String s = String(val, 6);
  while (s.endsWith("0") && s.indexOf('.') != -1) s.remove(s.length() - 1);
  if (s.endsWith(".")) s.remove(s.length() - 1);
  return s;
}

// Preprocesor: "1/2" → "(1.0/2.0)", "π" → "(3.141592653589793)", "e" → "(2.718281828459045)"
String preprocessExpr(String expr) {
  expr.replace("π", "(3.141592653589793)");
  expr.replace("e", "(2.718281828459045)");

  // Nahraď "a/b" → "(a.0/b.0)" len ak nie sú už v zátvorkách alebo za číslom
  // Jednoduchá verzia: nájdi všetky "X/Y" (kde X,Y sú číslice/π/e) a nahraď
  for (int i = 0; i < expr.length() - 2; i++) {
    if (expr[i] == '/' && i > 0 && i < expr.length() - 1) {
      // nájdi začiatok čitateľa (doľava)
      int startNum = i - 1;
      while (startNum > 0 && (isDigit(expr[startNum]) || expr[startNum] == '.')) startNum--;
      if (!isDigit(expr[startNum]) && expr[startNum] != '.') startNum++;
      // nájdi koniec menovateľa (doprava)
      int endNum = i + 1;
      while (endNum < expr.length() && (isDigit(expr[endNum]) || expr[endNum] == '.')) endNum++;
      if (endNum == i + 1) continue; // žiadny menovateľ

      String numA = expr.substring(startNum, i);
      String numB = expr.substring(i + 1, endNum);
      // Nahraď len ak sú to čísla (alebo π/e už nahradené)
      if (numA.length() > 0 && numB.length() > 0) {
        String repl = "(" + numA + ".0/" + numB + ".0)";
        expr.replace(expr.substring(startNum, endNum), repl);
        i += repl.length(); // posuň index
      }
    }
  }
  return expr;
}

void evaluate() {
  if (inputStr.length() == 0) return;

  String expr = preprocessExpr(inputStr);
  double result = te_interp(expr.c_str(), 0);
  
  if (te_errno) {
    inputStr = "ERR";
  } else {
    // Ulož do histórie
    history.push_back(inputStr + " = " + fractionToDisplay(result));
    if (history.size() > 5) history.erase(history.begin());
    inputStr = fractionToDisplay(result);
  }
  cursorPos = inputStr.length();
}

void clearInput() {
  inputStr = "";
  cursorPos = 0;
}

void backspace() {
  if (cursorPos > 0 && inputStr.length() > 0) {
    inputStr.remove(cursorPos - 1, 1);
    cursorPos--;
  }
}

void insertChar(String ch) {
  inputStr = inputStr.substring(0, cursorPos) + ch + inputStr.substring(cursorPos);
  cursorPos += ch.length();
}

// ============== KRESLENIE ==============
void drawInputField() {
  tft.fillRect(5, 5, 230, 40, COL_INPUT_BG);
  tft.setTextFont(2); // Font 2 = FreeSans12pt (štandardný)
  tft.setTextSize(2);
  tft.setTextColor(COL_INPUT_TXT);
  tft.setCursor(10, 15);
  tft.print(inputStr);

  // Kurzor (v pixeloch)
  int cx = 10 + tft.textWidth(inputStr.substring(0, cursorPos));
  if (cursorVisible) {
    tft.drawLine(cx, 15, cx, 15 + 24, COL_INPUT_TXT);
  }
}

void drawHistory() {
  tft.setTextFont(1); // menší font
  tft.setTextSize(1);
  tft.setTextColor(COL_HIST);
  for (int i = 0; i < history.size(); i++) {
    tft.setCursor(10, 300 - (4 - i) * 12);
    tft.print(history[i]);
  }
}

void drawButton(Button &btn) {
  if (!btn.visible) return;
  tft.setFreeFont(&FreeSans12pt7b); // alebo setTextFont(2)
  tft.fillRoundRect(btn.x, btn.y, btn.w, btn.h, 5, btn.color);
  tft.drawRoundRect(btn.x, btn.y, btn.w, btn.h, 5, TFT_WHITE);
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(btn.label, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(btn.x + (btn.w - w) / 2, btn.y + (btn.h + h) / 2 - 5);
  tft.print(btn.label);
}

void drawMenuIcon(int x, y) {
  tft.fillRoundRect(x, y, 30, 4, 2, TFT_WHITE);
  tft.fillRoundRect(x, y + 8, 30, 4, 2, TFT_WHITE);
  tft.fillRoundRect(x, y + 16, 30, 4, 2, TFT_WHITE);
}

void drawMenu() {
  if (!menuOpen) return;
  tft.fillRect(0, 0, 80, 320, TFT_NAVY);
  tft.drawRect(79, 0, 1, 320, TFT_WHITE);
  
  // Texty
  tft.setTextFont(2); tft.setTextSize(1);
  String pages[] = {"➕➖", "🔺", "🎲"};
  for (int i = 0; i < 3; i++) {
    tft.setTextColor(currentPage == i ? TFT_YELLOW : TFT_WHITE);
    tft.setCursor(15, 60 + i * 40);
    tft.print(pages[i]);
  }
}

void refreshScreen() {
  tft.fillScreen(COL_BG);
  drawInputField();
  drawHistory();
  drawMenuIcon(200, 10);
  drawMenu();
  for (auto &btn : buttons) drawButton(btn);
}

// ============== VYTVORENIE TLAČIDIEL ==============
void createButtons() {
  buttons.clear();

  // Základné číslice a operátory (PAGE_BASIC)
  int btnW = 50, btnH = 40, gap = 10;
  int startX = 10, startY = 70;
  String labelsBasic[] = {"7","8","9","/","C",
                          "4","5","6","*","⌫",
                          "1","2","3","-","=",
                          "0",".","π","+","(",
                          "e",")"};
  uint16_t colorsBasic[] = {
    COL_BTN_NUM,COL_BTN_NUM,COL_BTN_NUM,COL_BTN_OP,COL_BTN_CLR,
    COL_BTN_NUM,COL_BTN_NUM,COL_BTN_NUM,COL_BTN_OP,COL_BTN_CLR,
    COL_BTN_NUM,COL_BTN_NUM,COL_BTN_NUM,COL_BTN_OP,COL_BTN_EQ,
    COL_BTN_NUM,COL_BTN_NUM,COL_BTN_FUNC,COL_BTN_OP,COL_BTN_FUNC,
    COL_BTN_FUNC,COL_BTN_FUNC
  };

  for (int i = 0; i < 22; i++) {
    int row = i / 5;
    int col = i % 5;
    int x = startX + col * (btnW + gap);
    int y = startY + row * (btnH + gap);
    String lbl = labelsBasic[i];
    buttons.push_back({
      x, y, btnW, btnH, lbl, colorsBasic[i],
      [lbl]() {
        if (lbl == "=") evaluate();
        else if (lbl == "C") clearInput();
        else if (lbl == "⌫") backspace();
        else insertChar(lbl);
      },
      (currentPage == PAGE_BASIC)
    });
  }

  // Pokročilé funkcie (PAGE_ADVANCED)
  String labelsAdv[] = {"sin","cos","tan","cot","√",
                        "x²","xʸ","log","ln","!",
                        "abs","floor","ceil","rand","mod"};
  for (int i = 0; i < 15; i++) {
    int row = i / 5;
    int col = i % 5;
    int x = startX + col * (btnW + gap);
    int y = startY + row * (btnH + gap);
    String lbl = labelsAdv[i];
    buttons.push_back({
      x, y, btnW, btnH, lbl, COL_BTN_FUNC,
      [lbl]() {
        if (lbl == "√") insertChar("sqrt(");
        else if (lbl == "x²") insertChar("^2");
        else if (lbl == "xʸ") insertChar("^(");
        else if (lbl == "log") insertChar("log10(");
        else if (lbl == "ln") insertChar("log(");
        else if (lbl == "!") insertChar("!");
        else if (lbl == "abs") insertChar("abs(");
        else if (lbl == "floor") insertChar("floor(");
        else if (lbl == "ceil") insertChar("ceil(");
        else if (lbl == "rand") insertChar("rand()");
        else if (lbl == "mod") insertChar("%");
        else insertChar(lbl + "(");
      },
      (currentPage == PAGE_ADVANCED)
    });
  }

  // Extra operácie (PAGE_EXTRA)
  String labelsExtra[] = {"GCD","LCM","→frac","→dec","%",
                          "deg","rad","hyp","sec","csc",
                          "perm","comb","∑","∏","∫"};
  for (int i = 0; i < 15; i++) {
    int row = i / 5;
    int col = i % 5;
    int x = startX + col * (btnW + gap);
    int y = startY + row * (btnH + gap);
    String lbl = labelsExtra[i];
    buttons.push_back({
      x, y, btnW, btnH, lbl, TFT_PURPLE,
      [lbl]() {
        if (lbl == "→frac") { /* už implementované pri výstupe */ }
        else if (lbl == "→dec") { /* desatinné už default */ }
        else if (lbl == "%") insertChar("/100");
        else if (lbl == "deg") insertChar("*180/π");
        else if (lbl == "rad") insertChar("*π/180");
        else insertChar(lbl + "(");
      },
      (currentPage == PAGE_EXTRA)
    });
  }
}

// ============== HLAVNÉ FUNKCIE ==============
void setup() {
  Serial.begin(115200);
  tft.init();
  tft.setRotation(1); // 0=portrét, 1=landsc., 2=portrét obr., 3=landsc. obr.
  tft.fillScreen(COL_BG);
  
  createButtons();
  refreshScreen();
}

void loop() {
  // Blikanie kurzoru
  if (millis() - lastCursorToggle > cursorInterval) {
    cursorVisible = !cursorVisible;
    lastCursorToggle = millis();
    drawInputField();
  }

  // Dotyk
  uint16_t tx, ty;
  if (tft.getTouch(&tx, &ty)) {
    // Detekcia menu ikony
    if (tx >= 200 && tx <= 230 && ty >= 10 && ty <= 30) {
      menuOpen = !menuOpen;
      refreshScreen();
      delay(200);
      return;
    }

    // Ak je menu otvorené, detekcia výberu stránky
    if (menuOpen && tx < 80) {
      if (ty >= 60 && ty <= 100) { currentPage = PAGE_BASIC; menuOpen = false; }
      else if (ty >= 100 && ty <= 140) { currentPage = PAGE_ADVANCED; menuOpen = false; }
      else if (ty >= 140 && ty <= 180) { currentPage = PAGE_EXTRA; menuOpen = false; }
      createButtons();
      refreshScreen();
      delay(200);
      return;
    }

    // Detekcia tlačidiel
    for (auto &btn : buttons) {
      if (!btn.visible) continue;
      if (tx >= btn.x && tx <= btn.x + btn.w &&
          ty >= btn.y && ty <= btn.y + btn.h) {
        btn.onClick();
        btn.color = TFT_WHITE;
        drawButton(btn);
        delay(100);
        btn.color = (btn.label == "=" ? COL_BTN_EQ : 
                    (btn.label == "C" || btn.label == "⌫") ? COL_BTN_CLR :
                    (btn.label.length() == 1 && isDigit(btn.label[0])) ? COL_BTN_NUM :
                    (btn.label == "+" || btn.label == "-" || btn.label == "*" || btn.label == "/" || btn.label == "%" || btn.label == "^") ? COL_BTN_OP : 
                    COL_BTN_FUNC);
        refreshScreen();
        break;
      }
    }
  }
  }
