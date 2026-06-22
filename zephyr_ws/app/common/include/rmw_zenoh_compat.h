#ifndef RMW_ZENOH_COMPAT_H_
#define RMW_ZENOH_COMPAT_H_

#include <stddef.h>
#include <stdint.h>
#include <zenoh-pico.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RMW_ZENOH_DOMAIN_ID 0
#define RMW_ZENOH_TOPIC "/chatter"
#define RMW_ZENOH_STRING_TYPE_NAME "std_msgs::msg::dds_::String_"
#define RMW_ZENOH_STRING_TYPE_HASH \
  "RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18"

#define RMW_ZENOH_STRING_TOPIC_KEYEXPR \
  "0/chatter/std_msgs::msg::dds_::String_/" RMW_ZENOH_STRING_TYPE_HASH

#define RMW_ZENOH_PUB_QOS_KEY "::,7:,:,:,,"
#define RMW_ZENOH_SUB_QOS_KEY "::,10:,:,:,,"

int rmw_zenoh_zid_to_str(const z_loaned_session_t* session, char* buf,
                         size_t buf_len);
int rmw_zenoh_build_node_liveliness(char* buf, size_t buf_len,
                                    const char* session_zid, int node_id,
                                    const char* node_name);
int rmw_zenoh_build_pub_liveliness(char* buf, size_t buf_len,
                                   const char* session_zid, int node_id,
                                   int pub_id, const char* node_name);
int rmw_zenoh_build_sub_liveliness(char* buf, size_t buf_len,
                                   const char* session_zid, int node_id,
                                   int sub_id, const char* node_name);
int rmw_zenoh_declare_liveliness_token(const z_loaned_session_t* session,
                                       const char* keyexpr,
                                       z_owned_liveliness_token_t* token);
int rmw_zenoh_serialize_string_payload(z_owned_bytes_t* payload,
                                       const char* text);
int rmw_zenoh_deserialize_string_payload(const z_loaned_bytes_t* payload,
                                         char* buf, size_t buf_len);
int rmw_zenoh_build_attachment(z_owned_bytes_t* attachment, int64_t seq,
                               int64_t timestamp_ns, const uint8_t gid[16]);
void rmw_zenoh_fill_gid(uint8_t gid[16], const z_loaned_session_t* session,
                        uint32_t entity_id);
int64_t rmw_zenoh_now_ns(void);
int rmw_zenoh_make_cdr_encoding(z_owned_encoding_t* encoding);

#ifdef __cplusplus
}
#endif

#endif  // RMW_ZENOH_COMPAT_H_