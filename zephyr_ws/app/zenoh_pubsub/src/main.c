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

#define PUB_KEYEXPR "demo/example/zenoh-pico-pubsub/pub"
#define SUB_KEYEXPR "demo/example/zenoh-pico-pubsub/sub"
#define PUB_VALUE "Pub from Zenoh-Pico!"

#define PUB_PERIOD K_SECONDS(1)

static z_owned_publisher_t g_pub;
static struct k_work_delayable g_pub_work;
static int g_pub_idx;

static void subscriber_handler(z_loaned_sample_t* sample, void* arg) {
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

static void publisher_work_handler(struct k_work* work) {
  ARG_UNUSED(work);

  char buf[256];
  snprintk(buf, sizeof(buf), "[%4d] %s", g_pub_idx, PUB_VALUE);
  LOG_INF("Publishing ('%s': '%s')", PUB_KEYEXPR, buf);

  z_owned_bytes_t payload;
  z_bytes_copy_from_str(&payload, buf);
  z_publisher_put(z_loan(g_pub), z_move(payload), NULL);

  g_pub_idx++;
  (void)k_work_reschedule(&g_pub_work, PUB_PERIOD);
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
    if (strcmp(MODE, "client") == 0) {
      LOG_INF("Using Zenoh locator from compile-time env: %s", LOCATOR);
      zp_config_insert(z_loan_mut(config), Z_CONFIG_CONNECT_KEY, LOCATOR);
    } else {
      zp_config_insert(z_loan_mut(config), Z_CONFIG_LISTEN_KEY, LOCATOR);
    }
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

  LOG_INF("Declaring subscriber on '%s'...", SUB_KEYEXPR);
  z_owned_closure_sample_t callback;
  z_closure(&callback, subscriber_handler, NULL, NULL);
  z_view_keyexpr_t sub_ke;
  z_view_keyexpr_from_str_unchecked(&sub_ke, SUB_KEYEXPR);
  z_owned_subscriber_t sub;
  if (z_declare_subscriber(z_loan(s), &sub, z_loan(sub_ke), z_move(callback),
                           NULL) < 0) {
    LOG_ERR("Unable to declare subscriber.");
    return -1;
  }
  LOG_INF("OK");

  LOG_INF("Declaring publisher for '%s'...", PUB_KEYEXPR);
  z_view_keyexpr_t pub_ke;
  z_view_keyexpr_from_str_unchecked(&pub_ke, PUB_KEYEXPR);
  if (z_declare_publisher(z_loan(s), &g_pub, z_loan(pub_ke), NULL) < 0) {
    LOG_ERR("Unable to declare publisher.");
    return -1;
  }
  LOG_INF("OK");

  g_pub_idx = 0;
  k_work_init_delayable(&g_pub_work, publisher_work_handler);
  (void)k_work_schedule(&g_pub_work, PUB_PERIOD);
  LOG_INF("Publisher periodic task started");

  while (1) {
    k_sleep(K_SECONDS(5));
  }

  return 0;
}
