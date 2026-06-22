#include <stdint.h>
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

#define NODE_NAME "zp_turtle"
#define NODE_ID 0
#define PUB_ID 20

#define LINEAR_X 1.0
#define LINEAR_Y 0.0
#define LINEAR_Z 0.0
#define ANGULAR_X 0.0
#define ANGULAR_Y 0.0
#define ANGULAR_Z 0.6
#define PUBLISH_PERIOD_MS 100

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
  char pub_lv[512];
  if (rmw_zenoh_build_node_liveliness(node_lv, sizeof(node_lv), session_zid,
                                      NODE_ID, NODE_NAME) < 0 ||
      rmw_zenoh_build_pub_liveliness(
          pub_lv, sizeof(pub_lv), session_zid, NODE_ID, PUB_ID, NODE_NAME,
          RMW_ZENOH_TWIST_TOPIC, RMW_ZENOH_TWIST_TYPE_NAME,
          RMW_ZENOH_TWIST_TYPE_HASH) < 0) {
    LOG_ERR("Unable to build rmw_zenoh liveliness keyexprs");
    return -1;
  }

  z_owned_liveliness_token_t node_token;
  z_owned_liveliness_token_t pub_token;
  if (rmw_zenoh_declare_liveliness_token(z_loan(session), node_lv,
                                         &node_token) < 0 ||
      rmw_zenoh_declare_liveliness_token(z_loan(session), pub_lv, &pub_token) <
          0) {
    LOG_ERR("Unable to declare rmw_zenoh liveliness tokens");
    return -1;
  }
  LOG_INF("Declared liveliness node token: %s", node_lv);
  LOG_INF("Declared liveliness pub token : %s", pub_lv);

  LOG_INF("Declaring publisher for '%s'...", RMW_ZENOH_TWIST_TOPIC_KEYEXPR);
  z_view_keyexpr_t pub_ke;
  z_view_keyexpr_from_str_unchecked(&pub_ke, RMW_ZENOH_TWIST_TOPIC_KEYEXPR);
  z_owned_publisher_t pub;
  if (z_declare_publisher(z_loan(session), &pub, z_loan(pub_ke), NULL) < 0) {
    LOG_ERR("Unable to declare publisher");
    return -1;
  }
  LOG_INF("OK");

  uint8_t gid[16];
  rmw_zenoh_fill_gid(gid, z_loan(session), PUB_ID);

  for (int64_t seq = 0; 1; ++seq) {
    z_owned_bytes_t payload;
    z_owned_bytes_t attachment;
    z_owned_encoding_t encoding;
    if (rmw_zenoh_serialize_twist_payload(&payload, LINEAR_X, LINEAR_Y,
                                          LINEAR_Z, ANGULAR_X, ANGULAR_Y,
                                          ANGULAR_Z) < 0 ||
        rmw_zenoh_build_attachment(&attachment, seq, rmw_zenoh_now_ns(), gid) <
            0 ||
        rmw_zenoh_make_cdr_encoding(&encoding) < 0) {
      LOG_ERR("Unable to build rmw_zenoh Twist sample");
      return -1;
    }

    z_publisher_put_options_t opts;
    z_publisher_put_options_default(&opts);
    opts.encoding = z_move(encoding);
    opts.attachment = z_move(attachment);

    LOG_INF("Publishing Twist seq=%lld", (long long)seq);
    if (z_publisher_put(z_loan(pub), z_move(payload), &opts) < 0) {
      LOG_ERR("Unable to publish");
      return -1;
    }

    k_sleep(K_MSEC(PUBLISH_PERIOD_MS));
  }

  return 0;
}
