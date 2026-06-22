import struct
import time

import zenoh


ENDPOINT = "tcp/127.0.0.1:7447"
MODE = "client"
DOMAIN_ID = 0
TOPIC = "/chatter"
NODE_NAME = "r_sub"
NODE_ID = 0
SUBSCRIPTION_ID = 1
MONITOR_LIVELINESS = True
DECLARE_LIVELINESS = True

STD_MSGS_STRING_TYPE_NAME = "std_msgs::msg::dds_::String_"
# Type hash shown in rmw_zenoh design docs for std_msgs/msg/String.
STD_MSGS_STRING_TYPE_HASH = (
    "RIHS01_df668c740482bbd48fb39d76a70dfd4bd59db1288021743503259e948f6b1a18"
)
DEFAULT_QOS_KEY = "::,10:,:,:,,"  # default-like subscription QoS fingerprint


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


def build_sub_lv_key(
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
        f"MS/{enclave}/{namespace}/{node_name}/{mangled_fqn}/"
        f"{type_name}/{type_hash}/{qos_key}"
    )


def deserialize_std_msgs_string_cdr(payload: bytes) -> str:
    if len(payload) < 8:
        raise ValueError(f"payload too short for std_msgs/String CDR: {len(payload)} bytes")

    encapsulation = payload[:4]
    if encapsulation == b"\x00\x01\x00\x00":
        strlen = struct.unpack_from("<I", payload, 4)[0]
    elif encapsulation == b"\x00\x00\x00\x00":
        strlen = struct.unpack_from(">I", payload, 4)[0]
    else:
        raise ValueError(f"unexpected CDR encapsulation: {encapsulation.hex()}")

    start = 8
    end = start + strlen
    if end > len(payload):
        raise ValueError(f"string length {strlen} exceeds payload size {len(payload)}")

    data = payload[start:end]
    if data.endswith(b"\x00"):
        data = data[:-1]
    return data.decode("utf-8")


def _read_leb128(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    shift = 0
    while True:
        if offset >= len(data):
            raise ValueError("truncated LEB128 value")
        byte = data[offset]
        offset += 1
        value |= (byte & 0x7F) << shift
        if byte & 0x80 == 0:
            return value, offset
        shift += 7


def decode_attachment(attachment) -> str:
    if attachment is None:
        return "no attachment"

    raw = attachment.to_bytes()
    if len(raw) < 17:
        return f"attachment={raw.hex()}"

    seq = struct.unpack_from("<q", raw, 0)[0]
    timestamp_ns = struct.unpack_from("<q", raw, 8)[0]
    try:
        gid_len, offset = _read_leb128(raw, 16)
        gid = raw[offset:offset + gid_len]
        if len(gid) != gid_len:
            raise ValueError("truncated gid")
        return f"seq={seq} source_timestamp={timestamp_ns} gid={gid.hex()}"
    except ValueError:
        return f"seq={seq} source_timestamp={timestamp_ns} attachment={raw.hex()}"


def build_config(connect: str, mode: str) -> zenoh.Config:
    config = zenoh.Config()
    if mode:
        config.insert_json5("mode", f'"{mode}"')
    if connect:
        config.insert_json5("connect/endpoints", f'["{connect}"]')
    return config


def liveliness_monitor(sample) -> None:
    print(f"[LV] {sample.kind} {sample.key_expr}")


def listener(sample) -> None:
    payload = sample.payload.to_bytes()
    try:
        text = deserialize_std_msgs_string_cdr(payload)
    except Exception as exc:
        print(f"[RX-ERR] {sample.key_expr} bytes={payload.hex()} error={exc}")
        return

    attachment_info = decode_attachment(sample.attachment)
    print(f"[RX] {sample.kind} {sample.key_expr} text={text!r} {attachment_info}")


def main() -> None:
    topic_fqn = _normalize_fqn(TOPIC)
    keyexpr = build_topic_keyexpr(
        DOMAIN_ID,
        topic_fqn,
        STD_MSGS_STRING_TYPE_NAME,
        STD_MSGS_STRING_TYPE_HASH,
    )

    print("[INFO] Starting experimental rmw_zenoh subscriber")
    print(f"[INFO] topic_fqn  : {topic_fqn}")
    print(f"[INFO] keyexpr    : {keyexpr}")
    print(f"[INFO] domain_id  : {DOMAIN_ID}")
    print(f"[INFO] node_name  : {NODE_NAME}")
    print(f"[INFO] mode       : {MODE}")
    print(f"[INFO] endpoint   : {ENDPOINT}")
    print("[WARN] This is a best-effort compatibility test, not an officially supported path.")

    config = build_config(ENDPOINT, MODE)

    with zenoh.open(config) as session:
        # zenoh-python versions differ: `session.info` may be a method or a property.
        info_attr = session.info
        info = info_attr() if callable(info_attr) else info_attr
        session_zid = str(info.zid())
        print(f"[INFO] session_zid: {session_zid}")

        lv_node = None
        lv_sub_token = None
        lv_monitor = None
        lv = session.liveliness()
        if MONITOR_LIVELINESS:
            lv_monitor = lv.declare_subscriber(
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
            sub_lv_key = build_sub_lv_key(
                DOMAIN_ID,
                session_zid,
                NODE_ID,
                SUBSCRIPTION_ID,
                NODE_NAME,
                topic_fqn,
                STD_MSGS_STRING_TYPE_NAME,
                STD_MSGS_STRING_TYPE_HASH,
                DEFAULT_QOS_KEY,
            )

            lv_node = lv.declare_token(node_lv_key)
            lv_sub_token = lv.declare_token(sub_lv_key)
            print(f"[INFO] declared liveliness node token: {node_lv_key}")
            print(f"[INFO] declared liveliness sub token : {sub_lv_key}")

        sub = session.declare_subscriber(keyexpr, listener)
        print("[INFO] subscriber declared")

        # Keep declared objects alive for session lifetime.
        _ = (lv_node, lv_sub_token, lv_monitor, sub)

        while True:
            time.sleep(1)


if __name__ == "__main__":
    main()
