#include <stdio.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ipv4_dhcp_client.h"
#include "rmw_zenoh_compat.h"

LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define MODE "client"
#ifndef ZENOH_LOCATOR
#define ZENOH_LOCATOR ""
#endif
#define LOCATOR ZENOH_LOCATOR

#define NODE_NAME "zp_sub"
#define NODE_ID 0
#define SUB_ID 1

static void data_handler(z_loaned_sample_t* sample, void* arg) {
  ARG_UNUSED(arg);

  char text[256];
  if (rmw_zenoh_deserialize_string_payload(z_sample_payload(sample), text,
                                           sizeof(text)) < 0) {
    LOG_ERR("Unable to decode std_msgs/String payload");
    return;
  }

  z_view_string_t keystr;
  z_keyexpr_as_view_string(z_sample_keyexpr(sample), &keystr);
  LOG_INF("Received ('%.*s': '%s')", (int)z_string_len(z_loan(keystr)),
          z_string_data(z_loan(keystr)), text);
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
  z_owned_session_t session;
  if (z_open(&session, z_move(config), NULL) < 0) {
    LOG_ERR("Unable to open session!");
    return -1;
  }
  LOG_INF("OK");

  zp_start_read_task(z_loan_mut(session), NULL);
  zp_start_lease_task(z_loan_mut(session), NULL);

  char session_zid[64];
  if (rmw_zenoh_zid_to_str(z_loan(session), session_zid, sizeof(session_zid)) <
      0) {
    LOG_ERR("Unable to stringify session zid");
    return -1;
  }
  LOG_INF("session_zid: %s", session_zid);

  char node_lv[256];
  char sub_lv[512];
  if (rmw_zenoh_build_node_liveliness(node_lv, sizeof(node_lv), session_zid,
                                      NODE_ID, NODE_NAME) < 0 ||
      rmw_zenoh_build_sub_liveliness(
          sub_lv, sizeof(sub_lv), session_zid, NODE_ID, SUB_ID, NODE_NAME,
          RMW_ZENOH_STRING_TOPIC, RMW_ZENOH_STRING_TYPE_NAME,
          RMW_ZENOH_STRING_TYPE_HASH) < 0) {
    LOG_ERR("Unable to build rmw_zenoh liveliness keyexprs");
    return -1;
  }

  z_owned_liveliness_token_t node_token;
  z_owned_liveliness_token_t sub_token;
  if (rmw_zenoh_declare_liveliness_token(z_loan(session), node_lv,
                                         &node_token) < 0 ||
      rmw_zenoh_declare_liveliness_token(z_loan(session), sub_lv, &sub_token) <
          0) {
    LOG_ERR("Unable to declare rmw_zenoh liveliness tokens");
    return -1;
  }
  LOG_INF("Declared liveliness node token: %s", node_lv);
  LOG_INF("Declared liveliness sub token : %s", sub_lv);

  LOG_INF("Declaring subscriber on '%s'...", RMW_ZENOH_STRING_TOPIC_KEYEXPR);
  z_owned_closure_sample_t callback;
  z_closure(&callback, data_handler, NULL, NULL);
  z_view_keyexpr_t sub_ke;
  z_view_keyexpr_from_str_unchecked(&sub_ke, RMW_ZENOH_STRING_TOPIC_KEYEXPR);
  z_owned_subscriber_t sub;
  if (z_declare_subscriber(z_loan(session), &sub, z_loan(sub_ke),
                           z_move(callback), NULL) < 0) {
    LOG_ERR("Unable to declare subscriber");
    return -1;
  }
  LOG_INF("OK");

  while (1) {
    k_sleep(K_SECONDS(1));
  }

  return 0;
}
