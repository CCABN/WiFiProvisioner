#ifndef HTML_TEMPLATE_H
#define HTML_TEMPLATE_H

#include <Arduino.h>

// GCC/Clang builtin to include file as binary data
// This creates a symbol that points to the file contents
extern "C" {
    extern const uint8_t index_html_start[] asm("_binary_data_index_html_start");
    extern const uint8_t index_html_end[] asm("_binary_data_index_html_end");
}

// Runtime conversion function
inline String getHTMLTemplate() {
    size_t html_size = index_html_end - index_html_start;
    return String((const char*)index_html_start, html_size);
}

#endif // HTML_TEMPLATE_H