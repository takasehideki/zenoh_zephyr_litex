#include <stdint.h>
#include <string.h>
#include <zenoh-pico.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
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

#define GYRO_NODE DT_NODELABEL(pmod_gyro)

#define NODE_NAME "zp_turtle"
#define NODE_ID 0
#define PUB_ID 20

#define L3G4200D_WHO_AM_I 0x0f
#define L3G4200D_CTRL_REG1 0x20
#define L3G4200D_CTRL_REG4 0x23
#define L3G4200D_OUT_X_L 0x28

#define L3G4200D_READ BIT(7)
#define L3G4200D_AUTO_INCREMENT BIT(6)
#define L3G4200D_WHO_AM_I_VALUE 0xd3

#define SAMPLE_PERIOD_MS 100
#define BIAS_SAMPLE_COUNT 50

#define DEADZONE_MDPS 1200
#define LINEAR_GAIN_PER_MDPS 0.00003
#define ANGULAR_GAIN_PER_MDPS 0.00005
#define MAX_LINEAR_X 1.2
#define MAX_ANGULAR_Z 2.0

static const struct spi_dt_spec gyro_spi =
    SPI_DT_SPEC_GET(GYRO_NODE, SPI_OP_MODE_MASTER | SPI_WORD_SET(8));

static int gyro_read_reg(uint8_t reg, uint8_t* value) {
  uint8_t tx_buf[2] = {reg | L3G4200D_READ, 0x00};
  uint8_t rx_buf[2] = {0};
  const struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = sizeof(tx_buf)};
  const struct spi_buf rx_spi_buf = {.buf = rx_buf, .len = sizeof(rx_buf)};
  const struct spi_buf_set tx = {.buffers = &tx_spi_buf, .count = 1};
  const struct spi_buf_set rx = {.buffers = &rx_spi_buf, .count = 1};

  int rc = spi_transceive_dt(&gyro_spi, &tx, &rx);
  if (rc != 0) {
    return rc;
  }

  *value = rx_buf[1];
  return 0;
}

static int gyro_write_reg(uint8_t reg, uint8_t value) {
  uint8_t tx_buf[2] = {reg, value};
  const struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = sizeof(tx_buf)};
  const struct spi_buf_set tx = {.buffers = &tx_spi_buf, .count = 1};

  return spi_write_dt(&gyro_spi, &tx);
}

static int gyro_read_xyz(int16_t axis[3]) {
  uint8_t tx_buf[7] = {L3G4200D_OUT_X_L | L3G4200D_READ |
                       L3G4200D_AUTO_INCREMENT};
  uint8_t rx_buf[7] = {0};
  const struct spi_buf tx_spi_buf = {.buf = tx_buf, .len = sizeof(tx_buf)};
  const struct spi_buf rx_spi_buf = {.buf = rx_buf, .len = sizeof(rx_buf)};
  const struct spi_buf_set tx = {.buffers = &tx_spi_buf, .count = 1};
  const struct spi_buf_set rx = {.buffers = &rx_spi_buf, .count = 1};

  int rc = spi_transceive_dt(&gyro_spi, &tx, &rx);
  if (rc != 0) {
    return rc;
  }

  axis[0] = (int16_t)((uint16_t)rx_buf[2] << 8 | rx_buf[1]);
  axis[1] = (int16_t)((uint16_t)rx_buf[4] << 8 | rx_buf[3]);
  axis[2] = (int16_t)((uint16_t)rx_buf[6] << 8 | rx_buf[5]);

  return 0;
}

static int32_t raw_to_mdps(int16_t raw) { return (int32_t)raw * 875 / 100; }

static int32_t apply_deadzone_mdps(int32_t value) {
  if (value > DEADZONE_MDPS) {
    return value - DEADZONE_MDPS;
  }
  if (value < -DEADZONE_MDPS) {
    return value + DEADZONE_MDPS;
  }
  return 0;
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

static int estimate_bias_mdps(int32_t bias_mdps[3]) {
  int64_t sum_raw[3] = {0, 0, 0};

  LOG_INF("Estimating gyro bias (%d samples)... keep the board still",
          BIAS_SAMPLE_COUNT);

  for (int i = 0; i < BIAS_SAMPLE_COUNT; ++i) {
    int16_t raw[3];
    int rc = gyro_read_xyz(raw);
    if (rc != 0) {
      return rc;
    }
    sum_raw[0] += raw[0];
    sum_raw[1] += raw[1];
    sum_raw[2] += raw[2];
    k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
  }

  bias_mdps[0] = raw_to_mdps((int16_t)(sum_raw[0] / BIAS_SAMPLE_COUNT));
  bias_mdps[1] = raw_to_mdps((int16_t)(sum_raw[1] / BIAS_SAMPLE_COUNT));
  bias_mdps[2] = raw_to_mdps((int16_t)(sum_raw[2] / BIAS_SAMPLE_COUNT));
  LOG_INF("Bias mdps x=%d y=%d z=%d", bias_mdps[0], bias_mdps[1], bias_mdps[2]);

  return 0;
}

int main(void) {
  if (!spi_is_ready_dt(&gyro_spi)) {
    LOG_ERR("Pmod GYRO SPI device is not ready");
    return -ENODEV;
  }

  uint8_t who_am_i = 0;
  int rc = gyro_read_reg(L3G4200D_WHO_AM_I, &who_am_i);
  if (rc != 0) {
    LOG_ERR("WHO_AM_I read failed (%d)", rc);
    return rc;
  }

  LOG_INF("WHO_AM_I=0x%02x", who_am_i);
  if (who_am_i != L3G4200D_WHO_AM_I_VALUE) {
    LOG_WRN("Unexpected WHO_AM_I; expected 0x%02x", L3G4200D_WHO_AM_I_VALUE);
  }

  rc = gyro_write_reg(L3G4200D_CTRL_REG1, 0x0f);
  if (rc != 0) {
    LOG_ERR("CTRL_REG1 write failed (%d)", rc);
    return rc;
  }

  rc = gyro_write_reg(L3G4200D_CTRL_REG4, 0x80);
  if (rc != 0) {
    LOG_ERR("CTRL_REG4 write failed (%d)", rc);
    return rc;
  }

  int32_t bias_mdps[3] = {0, 0, 0};
  rc = estimate_bias_mdps(bias_mdps);
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

  LOG_INF("Teleop started: publishing Twist from gyro");

  for (int64_t seq = 0;; ++seq) {
    int16_t raw[3];
    rc = gyro_read_xyz(raw);
    if (rc != 0) {
      LOG_ERR("XYZ read failed (%d)", rc);
      k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
      continue;
    }

    int32_t mdps_x = apply_deadzone_mdps(raw_to_mdps(raw[0]) - bias_mdps[0]);
    int32_t mdps_y = apply_deadzone_mdps(raw_to_mdps(raw[1]) - bias_mdps[1]);
    int32_t mdps_z = apply_deadzone_mdps(raw_to_mdps(raw[2]) - bias_mdps[2]);

    double linear_x = clamp_double(-((double)mdps_y) * LINEAR_GAIN_PER_MDPS,
                                   -MAX_LINEAR_X, MAX_LINEAR_X);
    double angular_z = clamp_double(((double)mdps_z) * ANGULAR_GAIN_PER_MDPS,
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
        "raw x=%d y=%d z=%d mdps x=%d y=%d z=%d -> Twist linear.x=%.3f "
        "angular.z=%.3f",
        raw[0], raw[1], raw[2], mdps_x, mdps_y, mdps_z, linear_x, angular_z);

    k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
  }

  return 0;
}
