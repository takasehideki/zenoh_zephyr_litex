#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/kernel.h>

/* Keep this forward declaration before dhcpv4.h so include sorting does not
 * break type identity. */
struct net_if;

#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>

#define MODE "client"
#ifndef ZENOH_LOCATOR
#define ZENOH_LOCATOR ""
#endif
#define LOCATOR ZENOH_LOCATOR

#define KEYEXPR "demo/example/zenoh-pico-pub"
#define VALUE "Pub from Zenoh-Pico!"

static int wait_for_ipv4(void) {
  struct net_if* iface = net_if_get_default();
  char addr_buf[NET_IPV4_ADDR_LEN];

  printf("Waiting for IPv4 address via DHCP...\n");

  if (iface == NULL) {
    printf("ERROR: no default network interface\n");
    return -1;
  }

  net_if_up(iface);
  net_dhcpv4_start(iface);

  while (1) {
    struct net_in_addr* addr =
        net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

    if (addr != NULL) {
      printf("IPv4 address is ready: %s\n",
             net_addr_ntop(AF_INET, addr, addr_buf, sizeof(addr_buf)));
      return 0;
    }

    k_sleep(K_SECONDS(1));
  }
}

int main(void) {
  if (wait_for_ipv4() != 0) {
    return -1;
  }

  // Initialize Zenoh Session and other parameters
  z_owned_config_t config;
  z_config_default(&config);
  zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, MODE);
  if (strcmp(LOCATOR, "") != 0) {
    if (strcmp(MODE, "client") == 0) {
      printf("Using Zenoh locator from compile-time env: %s\n", LOCATOR);
      zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, LOCATOR);
    } else {
      zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, LOCATOR);
    }
  }

  // Open Zenoh session
  printf("Opening Zenoh Session...");
  z_owned_session_t s;
  if (z_open(&s, z_move(config), NULL) < 0) {
    printf("Unable to open session!\n");
    return -1;
  }
  printf("OK\n");

  // Start the receive and the session lease loop for zenoh-pico
  zp_start_read_task(z_loan_mut(s), NULL);
  zp_start_lease_task(z_loan_mut(s), NULL);

  printf("Declaring publisher for '%s'...", KEYEXPR);
  z_view_keyexpr_t ke;
  z_view_keyexpr_from_str_unchecked(&ke, KEYEXPR);
  z_owned_publisher_t pub;
  if (z_declare_publisher(z_loan(s), &pub, z_loan(ke), NULL) < 0) {
    printf("Unable to declare publisher for key expression!\n");
    return -1;
  }
  printf("OK\n");

  char buf[256];
  for (int idx = 0; 1; ++idx) {
    k_sleep(K_SECONDS(1));
    sprintf(buf, "[%4d] %s", idx, VALUE);
    printf("Putting Data ('%s': '%s')...\n", KEYEXPR, buf);

    // Create payload
    z_owned_bytes_t payload;
    z_bytes_copy_from_str(&payload, buf);

    z_publisher_put(z_loan(pub), z_move(payload), NULL);
  }

  printf("Closing Zenoh Session...");
  z_drop(z_move(pub));

  z_drop(z_move(s));
  printf("OK!\n");

  return 0;
}