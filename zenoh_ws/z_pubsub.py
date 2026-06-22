import threading
import time
import zenoh


# Note: from the host laptop perspective we *publish* to the board's SUB key-expr
PUB_KEY_EXPR = "demo/example/zenoh-pico-pubsub/sub"
# ...and we *subscribe* to the board's PUB key-expr
SUB_KEY_EXPR = "demo/example/zenoh-pico-pubsub/pub"
VALUE = "Pub from zenoh-python!"
INTERVAL_SEC = 1.0


def listener(sample):
    print(
        f"Received {sample.kind} ('{sample.key_expr}': '{sample.payload.to_string()}')")


def publisher_loop(pub):
    idx = 0
    while True:
        payload = f"[{idx:4d}] {VALUE}"
        pub.put(payload)
        print(f"Sent ('{PUB_KEY_EXPR}': '{payload}')")
        idx += 1
        time.sleep(INTERVAL_SEC)


with zenoh.open(zenoh.Config()) as session:
    sub = session.declare_subscriber(SUB_KEY_EXPR, listener)
    pub = session.declare_publisher(PUB_KEY_EXPR)

    print(f"Subscribed to {SUB_KEY_EXPR}")
    print(f"Publishing on {PUB_KEY_EXPR} every {INTERVAL_SEC}s ...")

    pub_thread = threading.Thread(target=publisher_loop, args=(pub,), daemon=True)
    pub_thread.start()

    while True:
        time.sleep(1)
