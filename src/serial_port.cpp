/**
 * @file serial_port.cpp
 * @ingroup serial_port
 * @brief Arduino's Serial as C functions (serial_port.h): each one calls the Serial method of the
 *        same name, so the bytes on the wire are what Serial itself sends.
 */
#include "serial_port.h"
#include <Arduino.h>
#include <stdarg.h>

/* (documented in serial_port.h) */
int serial_available(void)
{
    return Serial.available();
}

/* (documented in serial_port.h) */
int serial_read(void)
{
    return Serial.read();
}

/* (documented in serial_port.h) */
void serial_write(const uint8_t *buf, size_t n)
{
    Serial.write(buf, n);
}

/* (documented in serial_port.h) */
void serial_write_byte(uint8_t c)
{
    Serial.write(c);
}

/* (documented in serial_port.h) */
void serial_print(const char *s)
{
    Serial.print(s);
}

/* (documented in serial_port.h) */
void serial_println(const char *s)
{
    Serial.println(s);
}

/* (documented in serial_port.h) */
void serial_printf(const char *fmt, ...)
{
    /* as HardwareSerial's printf: a buffer on the stack, a bigger one from the heap if needed */
    char loc[128];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(loc, sizeof(loc), fmt, ap);
    va_end(ap);
    if (n < 0) {
        return;
    }
    if ((size_t)n < sizeof(loc)) {
        Serial.write((const uint8_t *)loc, (size_t)n);
        return;
    }
    char *big = (char *)malloc((size_t)n + 1);
    if (!big) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    Serial.write((const uint8_t *)big, (size_t)n);
    free(big);
}

/* (documented in serial_port.h) */
void serial_flush(void)
{
    Serial.flush();
}

/* (documented in serial_port.h) */
void serial_set_baud(uint32_t baud)
{
    Serial.updateBaudRate((unsigned long)baud);
}
