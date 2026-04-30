#ifndef NEXS_KEYMAP_H
#define NEXS_KEYMAP_H

#include <string.h>

static inline const char* translate_key_layout(const char *layout, char c) {
    static char buf[2] = {0};
    buf[0] = c; buf[1] = 0;

    if (!layout || strcmp(layout, "us") == 0) {
        return buf;
    }

    if (strcmp(layout, "ita") == 0) {
        switch (c) {
            case '[':  return "è";
            case ']':  return "+";
            case ';':  return "ò";
            case '\'': return "à";
            case '\\': return "ù";
            case '`':  return "ì";
            case '@':  return "\"";
            case '^':  return "&";
            case '&':  return "/";
            case '*':  return "(";
            case '(':  return ")";
            case ')':  return "=";
            case '_':  return "?";
            default:   return buf;
        }
    }
    
    if (strcmp(layout, "ita-mac") == 0) {
        switch (c) {
            case '[':  return "è";
            case ']':  return "+";
            case ';':  return "ò";
            case '\'': return "à";
            case '\\': return "ù";
            case '`':  return "ì";
            case '@':  return "\"";
            case '#':  return "@";
            case '<':  return "`";
            default:   return buf;
        }
    }

    if (strcmp(layout, "uk") == 0) {
        switch (c) {
            case '@':  return "\"";
            case '"':  return "@";
            case '#':  return "~";
            case '~':  return "#";
            case '\\': return "|";
            default:   return buf;
        }
    }

    if (strcmp(layout, "uk-mac") == 0) {
        switch (c) {
            case '@':  return "\"";
            case '"':  return "@";
            case '\\': return "#";
            case '3':  return "£";
            default:   return buf;
        }
    }

    return buf;
}

#endif /* NEXS_KEYMAP_H */
