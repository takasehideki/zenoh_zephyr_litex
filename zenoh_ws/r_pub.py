import struct
import time
import uuid

import zenoh
import zenoh.ext


ENDPOINT = "tcp/127.0.0.1:7447"
MODE = "client"
DOMAIN_ID = 0
TOPIC = "/chatter"
NODE_NAME = "r_pub"
NODE_ID = 0
PUBLISHER_ID = 10
VALUE = "Hello from zenoh-python (for rmw_zenoh)"
INTERVAL_SEC = 1.0
MONITOR_LIVELINESS = True
DECLARE_LIVELINESS = True
ATTACH_RMW_ZENOH_METADATA = True

STD_MSGS_STRING_TYPE_NAME = "std_msgs::msg::dds_::String_"
# Type hash shown in rmw_zenoh design docs for std_msgs/msg/String.
STD_MSGS_STRING_TYPE_HASH = (
    "RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18"
)
DEFAULT_QOS_KEY = "::,7:,:,:,,"  # default-like publisher QoS fingerprint


def _normalize_fqn(topic: str) -> str:
    if not topic:
        raise ValueError("topic must not be empty")
    if not topic.startswith("/"):
        topic = "/" + topic
    if "//" in topic:
        raise ValueError(f"invalid topic: {topic}")
    if topic == "/":
        raise ValueError("topic '/' is invalid")
    return topic


def _mangle_name(name: str) -> str:
    return name.replace("/", "%")


def build_topic_keyexpr(domain_id: int, topic_fqn: str, type_name: str, type_hash: str) -> str:
    # rmw_zenoh keyexpr uses fully qualified name without leading '/'.
    fqn_no_lead = topic_fqn.lstrip("/")
    return f"{domain_id}/{fqn_no_lead}/{type_name}/{type_hash}"


def build_node_lv_key(
    domain_id: int,
    session_zid: str,
    node_id: int,
    node_name: str,
    enclave: str = "%",
    namespace: str = "%",
) -> str:
    return (
        f"@ros2_lv/{domain_id}/{session_zid}/{node_id}/{node_id}/"
        f"NN/{enclave}/{namespace}/{node_name}"
    )


def build_pub_lv_key(
    domain_id: int,
    session_zid: str,
    node_id: int,
    entity_id: int,
    node_name: str,
    topic_fqn: str,
    type_name: str,
    type_hash: str,
    qos_key: str,
    enclave: str = "%",
    namespace: str = "%",
) -> str:
    mangled_fqn = _mangle_name(topic_fqn)
    return (
        f"@ros2_lv/{domain_id}/{session_zid}/{node_id}/{entity_id}/"
        f"MP/{enclave}/{namespace}/{node_name}/{mangled_fqn}/"
        f"{type_name}/{type_hash}/{qos_key}"
    )


def serialize_std_msgs_string_cdr(text: str) -> bytes:
    # CDR LE encapsulation header + ROS string field (length includes nul terminator).
    utf8 = text.encode("utf-8")
    ros_string = utf8 + b"\x00"

    out = bytearray()
    out += b"\x00\x01\x00\x00"  # CDR little-endian encapsulation
    out += struct.pack("<I", len(ros_string))
    out += ros_string
    return bytes(out)


def build_attachment(seq: int, publisher_gid: bytes):
    if len(publisher_gid) != 16:
        raise ValueError("publisher_gid must be exactly 16 bytes")
    ts_ns = time.time_ns()
    # rmw_zenoh uses zenoh::ext::Serializer, whose tuple format is field concatenation.
    return zenoh.ext.z_serialize(
        (zenoh.ext.Int64(seq), zenoh.ext.Int64(ts_ns), bytearray(publisher_gid))
    )


def build_config(connect: str, mode: str) -> zenoh.Config:
    config = zenoh.Config()
    if mode:
        config.insert_json5("mode", f'"{mode}"')
    if connect:
        config.insert_json5("connect/endpoints", f'["{connect}"]')
    return config


def liveliness_monitor(sample) -> None:
    print(f"[LV] {sample.kind} {sample.key_expr}")


def main() -> None:
    topic_fqn = _normalize_fqn(TOPIC)
    keyexpr = build_topic_keyexpr(
        DOMAIN_ID,
        topic_fqn,
        STD_MSGS_STRING_TYPE_NAME,
        STD_MSGS_STRING_TYPE_HASH,
    )

    print("[INFO] Starting experimental rmw_zenoh publisher")
    print(f"[INFO] topic_fqn  : {topic_fqn}")
    print(f"[INFO] keyexpr    : {keyexpr}")
    print(f"[INFO] domain_id  : {DOMAIN_ID}")
    print(f"[INFO] node_name  : {NODE_NAME}")
    print(f"[INFO] interval_s : {INTERVAL_SEC}")
    print(f"[INFO] mode       : {MODE}")
    print(f"[INFO] endpoint   : {ENDPOINT}")
    print("[WARN] This is a best-effort compatibility test, not an officially supported path.")

    publisher_gid = uuid.uuid4().bytes
    config = build_config(ENDPOINT, MODE)

    with zenoh.open(config) as session:
        # zenoh-python versions differ: `session.info` may be a method or a property.
        info_attr = session.info
        info = info_attr() if callable(info_attr) else info_attr
        session_zid = str(info.zid())
        print(f"[INFO] session_zid: {session_zid}")

        lv_node = None
        lv_pub = None
        lv_sub = None
        lv = session.liveliness()
        if MONITOR_LIVELINESS:
            lv_sub = lv.declare_subscriber(
                f"@ros2_lv/{DOMAIN_ID}/**", liveliness_monitor, history=True
            )
            print(f"[INFO] monitoring liveliness: @ros2_lv/{DOMAIN_ID}/**")

        if DECLARE_LIVELINESS:
            node_lv_key = build_node_lv_key(
                DOMAIN_ID,
                session_zid,
                NODE_ID,
                NODE_NAME,
            )
            pub_lv_key = build_pub_lv_key(
                DOMAIN_ID,
                session_zid,
                NODE_ID,
                PUBLISHER_ID,
                NODE_NAME,
                topic_fqn,
                STD_MSGS_STRING_TYPE_NAME,
                STD_MSGS_STRING_TYPE_HASH,
                DEFAULT_QOS_KEY,
            )

            lv_node = lv.declare_token(node_lv_key)
            lv_pub = lv.declare_token(pub_lv_key)
            print(f"[INFO] declared liveliness node token: {node_lv_key}")
            print(f"[INFO] declared liveliness pub token : {pub_lv_key}")

        pub = session.declare_publisher(keyexpr)
        print("[INFO] publisher declared")

        seq = 0
        # Keep declared tokens alive for session lifetime.
        _ = (lv_node, lv_pub, lv_sub)

        while True:
            text = f"[{seq:4d}] {VALUE}"
            payload = serialize_std_msgs_string_cdr(text)

            put_kwargs = {"encoding": zenoh.Encoding.APPLICATION_CDR}
            if ATTACH_RMW_ZENOH_METADATA:
                put_kwargs["attachment"] = build_attachment(seq, publisher_gid)

            pub.put(payload, **put_kwargs)
            print(f"[TX] seq={seq} text={text!r} bytes={len(payload)}")
            seq += 1
            time.sleep(INTERVAL_SEC)


if __name__ == "__main__":
    main()
