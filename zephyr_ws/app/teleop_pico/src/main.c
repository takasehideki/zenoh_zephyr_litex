#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "ipv4_dhcp_client.h"
#include "rmw_zenoh_compat.h"

LOG_MODULE_REGISTER(teleop_pico, CONFIG_LOG_DEFAULT_LEVEL);

#define MODE "client"
#ifndef ZENOH_LOCATOR
#define ZENOH_LOCATOR ""
#endif
#define LOCATOR ZENOH_LOCATOR

#define ACCEL_NODE DT_NODELABEL(pmod_acl2)

#define NODE_NAME "zp_turtle"
#define NODE_ID 0
#define PUB_ID 20

#define SAMPLE_PERIOD_MS 100
#define BIAS_SAMPLE_COUNT 50

#define DEADZONE_MS2 1.0
#define LINEAR_GAIN_PER_MS2 0.35
#define ANGULAR_GAIN_PER_MS2 0.90
#define MAX_LINEAR_X 1.2
#define MAX_ANGULAR_Z 2.0

static double apply_deadzone_ms2(double value) {
  if (value > DEADZONE_MS2) {
    return value - DEADZONE_MS2;
  }
  if (value < -DEADZONE_MS2) {
    return value + DEADZONE_MS2;
  }
  return 0.0;
}

static double clamp_double(double value, double min, double max) {
  if (value < min) {
    return min;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static int read_accel_ms2(const struct device* accel, double axis_ms2[3]) {
  struct sensor_value accel_xyz[3];
  int rc = sensor_sample_fetch(accel);
  if (rc != 0) {
    return rc;
  }

  rc = sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, accel_xyz);
  if (rc != 0) {
    return rc;
  }

  axis_ms2[0] = sensor_value_to_double(&accel_xyz[0]);
  axis_ms2[1] = sensor_value_to_double(&accel_xyz[1]);
  axis_ms2[2] = sensor_value_to_double(&accel_xyz[2]);
  return 0;
}

static int estimate_bias_ms2(const struct device* accel, double bias_ms2[3]) {
  double sum_ms2[3] = {0.0, 0.0, 0.0};

  LOG_INF("Estimating accelerometer bias (%d samples)... keep the board still",
          BIAS_SAMPLE_COUNT);

  for (int i = 0; i < BIAS_SAMPLE_COUNT; ++i) {
    double axis_ms2[3];
    int rc = read_accel_ms2(accel, axis_ms2);
    if (rc != 0) {
      return rc;
    }
    sum_ms2[0] += axis_ms2[0];
    sum_ms2[1] += axis_ms2[1];
    sum_ms2[2] += axis_ms2[2];
    k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
  }

  bias_ms2[0] = sum_ms2[0] / BIAS_SAMPLE_COUNT;
  bias_ms2[1] = sum_ms2[1] / BIAS_SAMPLE_COUNT;
  bias_ms2[2] = sum_ms2[2] / BIAS_SAMPLE_COUNT;
  LOG_INF("Bias m/s^2 x=%.3f y=%.3f z=%.3f", bias_ms2[0], bias_ms2[1],
          bias_ms2[2]);

  return 0;
}

int main(void) {
  const struct device* accel = DEVICE_DT_GET(ACCEL_NODE);
  if (!device_is_ready(accel)) {
    LOG_ERR("Pmod ACL2 device is not ready");
    return -ENODEV;
  }

  int rc;
  double bias_ms2[3] = {0.0, 0.0, 0.0};
  rc = estimate_bias_ms2(accel, bias_ms2);
  if (rc != 0) {
    LOG_ERR("Bias estimation failed (%d)", rc);
    return rc;
  }

  rc = dhcpv4_wait_for_ipv4();
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
    LOG_ERR("Unable to open session");
    return -EIO;
  }

  zp_start_read_task(z_loan_mut(session), NULL);
  zp_start_lease_task(z_loan_mut(session), NULL);

  char session_zid[64];
  if (rmw_zenoh_zid_to_str(z_loan(session), session_zid, sizeof(session_zid)) <
      0) {
    LOG_ERR("Unable to stringify session zid");
    return -EIO;
  }

  char node_lv[256];
  char pub_lv[512];
  if (rmw_zenoh_build_node_liveliness(node_lv, sizeof(node_lv), session_zid,
                                      NODE_ID, NODE_NAME) < 0 ||
      rmw_zenoh_build_pub_liveliness(
          pub_lv, sizeof(pub_lv), session_zid, NODE_ID, PUB_ID, NODE_NAME,
          RMW_ZENOH_TWIST_TOPIC, RMW_ZENOH_TWIST_TYPE_NAME,
          RMW_ZENOH_TWIST_TYPE_HASH) < 0) {
    LOG_ERR("Unable to build rmw_zenoh liveliness keyexprs");
    return -ENOMEM;
  }

  z_owned_liveliness_token_t node_token;
  z_owned_liveliness_token_t pub_token;
  if (rmw_zenoh_declare_liveliness_token(z_loan(session), node_lv,
                                         &node_token) < 0 ||
      rmw_zenoh_declare_liveliness_token(z_loan(session), pub_lv, &pub_token) <
          0) {
    LOG_ERR("Unable to declare rmw_zenoh liveliness tokens");
    return -EIO;
  }

  z_view_keyexpr_t pub_ke;
  z_view_keyexpr_from_str_unchecked(&pub_ke, RMW_ZENOH_TWIST_TOPIC_KEYEXPR);
  z_owned_publisher_t pub;
  if (z_declare_publisher(z_loan(session), &pub, z_loan(pub_ke), NULL) < 0) {
    LOG_ERR("Unable to declare publisher");
    return -EIO;
  }

  uint8_t gid[16];
  rmw_zenoh_fill_gid(gid, z_loan(session), PUB_ID);

  LOG_INF("Teleop started: publishing Twist from ACL2");

  for (int64_t seq = 0;; ++seq) {
    double accel_ms2[3];
    rc = read_accel_ms2(accel, accel_ms2);
    if (rc != 0) {
      LOG_ERR("Accelerometer read failed (%d)", rc);
      k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
      continue;
    }

    double delta_x = apply_deadzone_ms2(accel_ms2[0] - bias_ms2[0]);
    double delta_y = apply_deadzone_ms2(accel_ms2[1] - bias_ms2[1]);

    double linear_x = clamp_double(-(delta_y)*LINEAR_GAIN_PER_MS2,
                                   -MAX_LINEAR_X, MAX_LINEAR_X);
    double angular_z = clamp_double((delta_x)*ANGULAR_GAIN_PER_MS2,
                                    -MAX_ANGULAR_Z, MAX_ANGULAR_Z);

    z_owned_bytes_t payload;
    z_owned_bytes_t attachment;
    z_owned_encoding_t encoding;
    if (rmw_zenoh_serialize_twist_payload(&payload, linear_x, 0.0, 0.0, 0.0,
                                          0.0, angular_z) < 0 ||
        rmw_zenoh_build_attachment(&attachment, seq, rmw_zenoh_now_ns(), gid) <
            0 ||
        rmw_zenoh_make_cdr_encoding(&encoding) < 0) {
      LOG_ERR("Unable to build rmw_zenoh Twist sample");
      return -EIO;
    }

    z_publisher_put_options_t opts;
    z_publisher_put_options_default(&opts);
    opts.encoding = z_move(encoding);
    opts.attachment = z_move(attachment);

    if (z_publisher_put(z_loan(pub), z_move(payload), &opts) < 0) {
      LOG_ERR("Unable to publish");
      return -EIO;
    }

    LOG_INF(
        "accel x=%.3f y=%.3f z=%.3f delta x=%.3f y=%.3f -> Twist linear.x=%.3f "
        "angular.z=%.3f",
        accel_ms2[0], accel_ms2[1], accel_ms2[2], delta_x, delta_y, linear_x,
        angular_z);

    k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
  }

  return 0;
}
