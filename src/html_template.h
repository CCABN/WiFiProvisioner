#ifndef HTML_TEMPLATE_H
#define HTML_TEMPLATE_H

#include <Arduino.h>

// Macro to include file content as raw string literal
#define INCLUDE_HTML_FILE(filepath) \
    const char HTML_CONTENT[] PROGMEM = R"HTMLDELIM(" \
    #include filepath \
    ")HTMLDELIM";

// Include the HTML file from data directory
INCLUDE_HTML_FILE("../data/index.html")

inline String getHTMLTemplate() {
    return String(FPSTR(HTML_CONTENT));
}

#endif // HTML_TEMPLATE_H