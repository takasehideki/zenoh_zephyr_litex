#include "net_utils.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Keep this forward declaration before dhcpv4.h so include sorting does not
 * break type identity. */
struct net_if;

#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

LOG_MODULE_DECLARE(net_utils, CONFIG_LOG_DEFAULT_LEVEL);

int wait_for_ipv4(void) {
  struct net_if* iface = net_if_get_default();
  char addr_buf[NET_IPV4_ADDR_LEN];

  LOG_INF("Waiting for IPv4 address via DHCP...");

  if (iface == NULL) {
    LOG_ERR("no default network interface");
    return -1;
  }

  net_if_up(iface);
  net_dhcpv4_start(iface);

  while (1) {
    struct net_in_addr* addr =
        net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

    if (addr != NULL) {
      LOG_INF("IPv4 address is ready: %s",
              net_addr_ntop(NET_AF_INET, addr, addr_buf, sizeof(addr_buf)));
      return 0;
    }

    k_sleep(K_SECONDS(1));
  }
}
