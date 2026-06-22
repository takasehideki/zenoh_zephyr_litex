#include "rmw_zenoh_compat.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

static void put_u32_le(uint8_t* dst, uint32_t value) {
  dst[0] = (uint8_t)(value & 0xffU);
  dst[1] = (uint8_t)((value >> 8) & 0xffU);
  dst[2] = (uint8_t)((value >> 16) & 0xffU);
  dst[3] = (uint8_t)((value >> 24) & 0xffU);
}

static void put_u64_le(uint8_t* dst, uint64_t value) {
  for (size_t i = 0; i < 8; ++i) {
    dst[i] = (uint8_t)((value >> (8 * i)) & 0xffU);
  }
}

static void put_f64_le(uint8_t* dst, double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof(bits));
  put_u64_le(dst, bits);
}

static uint32_t get_u32_le(const uint8_t* src) {
  return ((uint32_t)src[0]) | ((uint32_t)src[1] << 8) |
         ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static void copy_zid_bytes(uint8_t out[16], const z_id_t* id) {
  memcpy(out, id, 16);
}

static int build_liveliness_topic(char* buf, size_t buf_len,
                                  const char* topic) {
  size_t pos = 0;
  const char* cursor = topic;
  if (cursor[0] == '/') {
    ++cursor;
  }

  while (*cursor != '\0') {
    if (pos + 1 >= buf_len) {
      return -ENOMEM;
    }
    buf[pos++] = (*cursor == '/') ? '%' : *cursor;
    ++cursor;
  }

  if (pos + 1 >= buf_len) {
    return -ENOMEM;
  }
  memmove(&buf[1], buf, pos);
  buf[0] = '%';
  buf[pos + 1] = '\0';
  return 0;
}

int64_t rmw_zenoh_now_ns(void) { return (int64_t)k_uptime_get() * 1000000LL; }

int rmw_zenoh_zid_to_str(const z_loaned_session_t* session, char* buf,
                         size_t buf_len) {
  z_id_t zid = z_info_zid(session);
  z_owned_string_t str;
  if (z_id_to_string(&zid, &str) < 0) {
    return -EIO;
  }

  size_t len = z_string_len(z_loan(str));
  if (len + 1 > buf_len) {
    z_drop(z_move(str));
    return -ENOMEM;
  }

  memcpy(buf, z_string_data(z_loan(str)), len);
  buf[len] = '\0';
  z_drop(z_move(str));
  return 0;
}

int rmw_zenoh_build_node_liveliness(char* buf, size_t buf_len,
                                    const char* session_zid, int node_id,
                                    const char* node_name) {
  int ret =
      snprintk(buf, buf_len, "@ros2_lv/%d/%s/%d/%d/NN/%%/%%/%s",
               RMW_ZENOH_DOMAIN_ID, session_zid, node_id, node_id, node_name);
  return (ret < 0 || (size_t)ret >= buf_len) ? -ENOMEM : 0;
}

int rmw_zenoh_build_pub_liveliness(char* buf, size_t buf_len,
                                   const char* session_zid, int node_id,
                                   int pub_id, const char* node_name) {
  return rmw_zenoh_build_pub_liveliness_for_topic(
      buf, buf_len, session_zid, node_id, pub_id, node_name,
      RMW_ZENOH_STRING_TOPIC, RMW_ZENOH_STRING_TYPE_NAME,
      RMW_ZENOH_STRING_TYPE_HASH);
}

int rmw_zenoh_build_pub_liveliness_for_topic(
    char* buf, size_t buf_len, const char* session_zid, int node_id, int pub_id,
    const char* node_name, const char* topic, const char* type_name,
    const char* type_hash) {
  char topic_key[128];
  if (build_liveliness_topic(topic_key, sizeof(topic_key), topic) < 0) {
    return -ENOMEM;
  }

  int ret =
      snprintk(buf, buf_len, "@ros2_lv/%d/%s/%d/%d/MP/%%/%%/%s/%s/%s/%s/%s",
               RMW_ZENOH_DOMAIN_ID, session_zid, node_id, pub_id, node_name,
               topic_key, type_name, type_hash, RMW_ZENOH_PUB_QOS_KEY);
  return (ret < 0 || (size_t)ret >= buf_len) ? -ENOMEM : 0;
}

int rmw_zenoh_build_sub_liveliness(char* buf, size_t buf_len,
                                   const char* session_zid, int node_id,
                                   int sub_id, const char* node_name) {
  return rmw_zenoh_build_sub_liveliness_for_topic(
      buf, buf_len, session_zid, node_id, sub_id, node_name,
      RMW_ZENOH_STRING_TOPIC, RMW_ZENOH_STRING_TYPE_NAME,
      RMW_ZENOH_STRING_TYPE_HASH);
}

int rmw_zenoh_build_sub_liveliness_for_topic(
    char* buf, size_t buf_len, const char* session_zid, int node_id, int sub_id,
    const char* node_name, const char* topic, const char* type_name,
    const char* type_hash) {
  char topic_key[128];
  if (build_liveliness_topic(topic_key, sizeof(topic_key), topic) < 0) {
    return -ENOMEM;
  }

  int ret =
      snprintk(buf, buf_len, "@ros2_lv/%d/%s/%d/%d/MS/%%/%%/%s/%s/%s/%s/%s",
               RMW_ZENOH_DOMAIN_ID, session_zid, node_id, sub_id, node_name,
               topic_key, type_name, type_hash, RMW_ZENOH_SUB_QOS_KEY);
  return (ret < 0 || (size_t)ret >= buf_len) ? -ENOMEM : 0;
}

int rmw_zenoh_declare_liveliness_token(const z_loaned_session_t* session,
                                       const char* keyexpr,
                                       z_owned_liveliness_token_t* token) {
  z_view_keyexpr_t ke;
  z_view_keyexpr_from_str_unchecked(&ke, keyexpr);
  z_liveliness_token_options_t opts;
  z_liveliness_token_options_default(&opts);
  return z_liveliness_declare_token(session, token, z_loan(ke), &opts);
}

int rmw_zenoh_serialize_string_payload(z_owned_bytes_t* payload,
                                       const char* text) {
  size_t text_len = strlen(text);
  size_t ros_len = text_len + 1;
  size_t payload_len = 8 + ros_len;
  uint8_t buf[256];

  if (payload_len > sizeof(buf)) {
    return -ENOMEM;
  }

  buf[0] = 0x00;
  buf[1] = 0x01;
  buf[2] = 0x00;
  buf[3] = 0x00;
  put_u32_le(&buf[4], (uint32_t)ros_len);
  memcpy(&buf[8], text, text_len);
  buf[8 + text_len] = '\0';

  return z_bytes_copy_from_buf(payload, buf, payload_len);
}

int rmw_zenoh_deserialize_string_payload(const z_loaned_bytes_t* payload,
                                         char* buf, size_t buf_len) {
  z_owned_slice_t slice;
  if (z_bytes_to_slice(payload, &slice) < 0) {
    return -EIO;
  }

  const uint8_t* data = z_slice_data(z_loan(slice));
  size_t len = z_slice_len(z_loan(slice));
  if (len < 8 || data[0] != 0x00 || data[1] != 0x01 || data[2] != 0x00 ||
      data[3] != 0x00) {
    z_drop(z_move(slice));
    return -EINVAL;
  }

  uint32_t ros_len = get_u32_le(&data[4]);
  if (8U + ros_len > len || ros_len == 0 || ros_len > buf_len) {
    z_drop(z_move(slice));
    return -EINVAL;
  }

  size_t text_len = ros_len - 1;
  memcpy(buf, &data[8], text_len);
  buf[text_len] = '\0';
  z_drop(z_move(slice));
  return 0;
}

int rmw_zenoh_serialize_twist_payload(z_owned_bytes_t* payload, double linear_x,
                                      double linear_y, double linear_z,
                                      double angular_x, double angular_y,
                                      double angular_z) {
  uint8_t buf[52];
  buf[0] = 0x00;
  buf[1] = 0x01;
  buf[2] = 0x00;
  buf[3] = 0x00;

  put_f64_le(&buf[4], linear_x);
  put_f64_le(&buf[12], linear_y);
  put_f64_le(&buf[20], linear_z);
  put_f64_le(&buf[28], angular_x);
  put_f64_le(&buf[36], angular_y);
  put_f64_le(&buf[44], angular_z);

  return z_bytes_copy_from_buf(payload, buf, sizeof(buf));
}

int rmw_zenoh_build_attachment(z_owned_bytes_t* attachment, int64_t seq,
                               int64_t timestamp_ns, const uint8_t gid[16]) {
  ze_owned_serializer_t serializer;
  if (ze_serializer_empty(&serializer) < 0) {
    return -EIO;
  }

  ze_loaned_serializer_t* loaned = z_loan_mut(serializer);
  if (ze_serializer_serialize_int64(loaned, seq) < 0 ||
      ze_serializer_serialize_int64(loaned, timestamp_ns) < 0 ||
      ze_serializer_serialize_buf(loaned, gid, 16) < 0) {
    z_drop(z_move(serializer));
    return -EIO;
  }

  ze_serializer_finish(z_move(serializer), attachment);
  return 0;
}

void rmw_zenoh_fill_gid(uint8_t gid[16], const z_loaned_session_t* session,
                        uint32_t entity_id) {
  z_id_t zid = z_info_zid(session);
  copy_zid_bytes(gid, &zid);
  gid[12] = (uint8_t)(entity_id & 0xffU);
  gid[13] = (uint8_t)((entity_id >> 8) & 0xffU);
  gid[14] = (uint8_t)((entity_id >> 16) & 0xffU);
  gid[15] = (uint8_t)((entity_id >> 24) & 0xffU);
}

int rmw_zenoh_make_cdr_encoding(z_owned_encoding_t* encoding) {
  return z_encoding_clone(encoding, z_encoding_application_cdr());
}
