#include "ipv4_dhcp_client.h"

#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

/* Keep this forward declaration before dhcpv4.h so include sorting does not
 * break type identity. */
struct net_if;

#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>

LOG_MODULE_REGISTER(dhcpv4_client, CONFIG_LOG_DEFAULT_LEVEL);

static struct k_sem ipv4_ready_sem;
static struct net_mgmt_event_callback ipv4_addr_add_cb;
static struct net_if* target_iface;

static void on_ipv4_addr_add(struct net_mgmt_event_callback* cb,
                             uint64_t mgmt_event, struct net_if* iface) {
  ARG_UNUSED(cb);

  if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD || iface != target_iface) {
    return;
  }

  struct net_in_addr* addr =
      net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
  if (addr == NULL) {
    return;
  }

  char addr_buf[NET_IPV4_ADDR_LEN];
  LOG_INF("IPv4 address is ready: %s",
          net_addr_ntop(NET_AF_INET, addr, addr_buf, sizeof(addr_buf)));
  k_sem_give(&ipv4_ready_sem);
}

int dhcpv4_wait_for_ipv4(void) {
  struct net_if* iface = net_if_get_default();
  int wait_s = DHCPV4_WAIT_TIMEOUT_DEFAULT_S;

  LOG_INF("Waiting for IPv4 address via DHCP...");

  if (iface == NULL) {
    LOG_ERR("no default network interface");
    return -ENODEV;
  }

  {
    char addr_buf[NET_IPV4_ADDR_LEN];
    struct net_in_addr* addr =
        net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

    if (addr != NULL) {
      LOG_INF("IPv4 address is already configured: %s",
              net_addr_ntop(NET_AF_INET, addr, addr_buf, sizeof(addr_buf)));
      return 0;
    }
  }

  k_sem_init(&ipv4_ready_sem, 0, 1);
  target_iface = iface;
  net_mgmt_init_event_callback(&ipv4_addr_add_cb, on_ipv4_addr_add,
                               NET_EVENT_IPV4_ADDR_ADD);
  net_mgmt_add_event_callback(&ipv4_addr_add_cb);

  net_if_up(iface);
  net_dhcpv4_start(iface);

  if (k_sem_take(&ipv4_ready_sem, K_SECONDS(wait_s)) == 0) {
    net_mgmt_del_event_callback(&ipv4_addr_add_cb);
    target_iface = NULL;
    return 0;
  }

  net_mgmt_del_event_callback(&ipv4_addr_add_cb);
  target_iface = NULL;
  LOG_ERR("Timed out waiting for IPv4 address via DHCP (%d s)", wait_s);
  return -ETIMEDOUT;
}
