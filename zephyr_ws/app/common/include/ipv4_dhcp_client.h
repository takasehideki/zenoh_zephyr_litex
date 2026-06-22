#pragma once

#define DHCPV4_WAIT_TIMEOUT_DEFAULT_S 60

/* Start DHCPv4 on the default interface and wait until a preferred IPv4
 * address is obtained. Uses DHCPV4_WAIT_TIMEOUT_DEFAULT_S.
 * Returns 0 on success, negative errno on error. */
int dhcpv4_wait_for_ipv4(void);
