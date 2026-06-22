#pragma once

/* Wait until a preferred IPv4 address is obtained via DHCP.
 * Returns 0 on success, -1 on error. */
int wait_for_ipv4(void);
