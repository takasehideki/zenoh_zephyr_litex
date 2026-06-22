#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "net_utils.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define MODE "client"
#ifndef ZENOH_LOCATOR
#define ZENOH_LOCATOR ""
#endif
#define LOCATOR ZENOH_LOCATOR

#define KEYEXPR "demo/example/zenoh-pico-pub"
#define VALUE "Pub from Zenoh-Pico!"

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
      LOG_INF("Using Zenoh locator from compile-time env: %s", LOCATOR);
      zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, LOCATOR);
    } else {
      zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, LOCATOR);
    }
  }

  // Open Zenoh session
  LOG_INF("Opening Zenoh Session...");
  z_owned_session_t s;
  if (z_open(&s, z_move(config), NULL) < 0) {
    LOG_ERR("Unable to open session!");
    return -1;
  }
  LOG_INF("OK");

  // Start the receive and the session lease loop for zenoh-pico
  zp_start_read_task(z_loan_mut(s), NULL);
  zp_start_lease_task(z_loan_mut(s), NULL);

  LOG_INF("Declaring publisher for '%s'...", KEYEXPR);
  z_view_keyexpr_t ke;
  z_view_keyexpr_from_str_unchecked(&ke, KEYEXPR);
  z_owned_publisher_t pub;
  if (z_declare_publisher(z_loan(s), &pub, z_loan(ke), NULL) < 0) {
    LOG_ERR("Unable to declare publisher for key expression!");
    return -1;
  }
  LOG_INF("OK");

  char buf[256];
  for (int idx = 0; 1; ++idx) {
    k_sleep(K_SECONDS(1));
    sprintf(buf, "[%4d] %s", idx, VALUE);
    LOG_INF("Putting Data ('%s': '%s')...", KEYEXPR, buf);

    // Create payload
    z_owned_bytes_t payload;
    z_bytes_copy_from_str(&payload, buf);

    z_publisher_put(z_loan(pub), z_move(payload), NULL);
  }

  LOG_INF("Closing Zenoh Session...");
  z_drop(z_move(pub));

  z_drop(z_move(s));
  LOG_INF("OK!");

  return 0;
}
