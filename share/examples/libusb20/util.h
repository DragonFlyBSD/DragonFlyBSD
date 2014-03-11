/* ----------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42) (by Poul-Henning Kamp):
 * <joerg@FreeBSD.ORG> wrote this file.  As long as you retain this notice you
 * can do whatever you want with this stuff. If we meet some day, and you think
 * this stuff is worth it, you can buy me a beer in return.        Joerg Wunsch
 * ----------------------------------------------------------------------------
 *
 * $FreeBSD: head/share/examples/libusb20/util.h 257779 2013-11-07 07:22:51Z hselasky $
 */

#include <stdint.h>
#include <libusb20.h>

void print_formatted(uint8_t *buf, uint32_t len);
