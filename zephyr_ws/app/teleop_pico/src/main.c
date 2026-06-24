#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(teleop_pico, CONFIG_LOG_DEFAULT_LEVEL);

#define GYRO_NODE DT_NODELABEL(pmod_gyro)

#define L3G4200D_WHO_AM_I 0x0f
#define L3G4200D_CTRL_REG1 0x20
#define L3G4200D_CTRL_REG4 0x23
#define L3G4200D_OUT_X_L 0x28

#define L3G4200D_READ BIT(7)
#define L3G4200D_AUTO_INCREMENT BIT(6)
#define L3G4200D_WHO_AM_I_VALUE 0xd3

#define SAMPLE_PERIOD_MS 100

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

  LOG_INF("Pmod GYRO sampling started");

  while (true) {
    int16_t raw[3];
    rc = gyro_read_xyz(raw);
    if (rc != 0) {
      LOG_ERR("XYZ read failed (%d)", rc);
      k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
      continue;
    }

    LOG_INF("raw x=%d y=%d z=%d, mdps x=%d y=%d z=%d", raw[0], raw[1], raw[2],
            raw_to_mdps(raw[0]), raw_to_mdps(raw[1]), raw_to_mdps(raw[2]));
    k_sleep(K_MSEC(SAMPLE_PERIOD_MS));
  }

  return 0;
}
