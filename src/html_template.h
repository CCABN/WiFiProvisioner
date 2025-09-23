#ifndef HTML_TEMPLATE_H
#define HTML_TEMPLATE_H

#include <Arduino.h>

// Stringify macro to convert file content to string
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

// Raw string literal containing the HTML file content
const char HTML_CONTENT[] PROGMEM =
#include "../data/index.html"
;

inline String getHTMLTemplate() {
    return String(FPSTR(HTML_CONTENT));
}

#endif // HTML_TEMPLATE_H