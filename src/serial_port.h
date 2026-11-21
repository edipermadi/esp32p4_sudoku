/**
 * @file serial_port.h
 * @ingroup serial_port
 * @brief The USB-UART port (Arduino's Serial) for C code.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup serial_port Serial port
 * @brief Arduino's Serial (the USB-UART port, a C++ object) as C functions, for devctl.c.
 *
 * The same object the sketch sets up (setup(): 115200 baud, a 2 KB receive buffer) and prints its
 * log to, so its lines and devctl's share the port as before. Bytes go out exactly as Serial would
 * send them: serial_println() ends a line with "\r\n".
 * @{
 */

/**
 * @brief Bytes received and not read yet.
 * @return how many (0: none)
 */
int serial_available(void);
/**
 * @brief Reads a byte.
 * @return the byte (0..255), or -1 if none is waiting
 */
int serial_read(void);
/**
 * @brief Sends bytes.
 * @param buf  the bytes
 * @param n    how many
 */
void serial_write(const uint8_t *buf, size_t n);
/**
 * @brief Sends one byte.
 * @param c  the byte
 */
void serial_write_byte(uint8_t c);
/**
 * @brief Sends a string (as Serial.print()).
 * @param s  the string
 */
void serial_print(const char *s);
/**
 * @brief Sends a string and "\r\n" (as Serial.println()).
 * @param s  the string
 */
void serial_println(const char *s);
/**
 * @brief Sends formatted text (as Serial.printf()).
 * @param fmt  printf's format, then its arguments
 */
void serial_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
/** @brief Waits until everything sent has left the port. */
void serial_flush(void);
/**
 * @brief Switches the port's speed.
 * @param baud  the new speed, e.g. 921600
 */
void serial_set_baud(uint32_t baud);

/** @} */

#ifdef __cplusplus
}
#endif
