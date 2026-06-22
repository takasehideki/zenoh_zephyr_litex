#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ipv4_dhcp_client.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define MODE "client"
#ifndef ZENOH_LOCATOR
#define ZENOH_LOCATOR ""
#endif
#define LOCATOR ZENOH_LOCATOR

#define KEYEXPR "demo/example/**"

static void data_handler(z_loaned_sample_t* sample, void* arg) {
  ARG_UNUSED(arg);

  z_view_string_t keystr;
  z_keyexpr_as_view_string(z_sample_keyexpr(sample), &keystr);
  z_owned_string_t value;
  z_bytes_to_string(z_sample_payload(sample), &value);

  LOG_INF("Received ('%.*s': '%.*s')", (int)z_string_len(z_loan(keystr)),
          z_string_data(z_loan(keystr)), (int)z_string_len(z_loan(value)),
          z_string_data(z_loan(value)));
  z_drop(z_move(value));
}

int main(void) {
  int rc = dhcpv4_wait_for_ipv4();
  if (rc != 0) {
    LOG_ERR("DHCPv4 failed (%d)", rc);
    return rc;
  }

  z_owned_config_t config;
  z_config_default(&config);
  zp_config_insert(z_loan_mut(config), Z_CONFIG_MODE_KEY, MODE);
  if (strcmp(LOCATOR, "") != 0) {
    LOG_INF("Using Zenoh locator from compile-time env: %s", LOCATOR);
    zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, LOCATOR);
  }

  LOG_INF("Opening Zenoh Session...");
  z_owned_session_t s;
  if (z_open(&s, z_move(config), NULL) < 0) {
    LOG_ERR("Unable to open session!");
    return -1;
  }
  LOG_INF("OK");

  zp_start_read_task(z_loan_mut(s), NULL);
  zp_start_lease_task(z_loan_mut(s), NULL);

  LOG_INF("Declaring subscriber on '%s'...", KEYEXPR);
  z_owned_closure_sample_t callback;
  z_closure(&callback, data_handler, NULL, NULL);
  z_view_keyexpr_t ke;
  z_view_keyexpr_from_str_unchecked(&ke, KEYEXPR);
  z_owned_subscriber_t sub;
  if (z_declare_subscriber(z_loan(s), &sub, z_loan(ke), z_move(callback),
                           NULL) < 0) {
    LOG_ERR("Unable to declare subscriber.");
    return -1;
  }
  LOG_INF("OK");

  while (1) {
    k_sleep(K_SECONDS(1));
  }

  LOG_INF("Closing Zenoh Session...");
  z_drop(z_move(sub));
  z_drop(z_move(s));
  LOG_INF("OK!");

  return 0;
}
