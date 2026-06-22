import time
import zenoh


KEY_EXPR = "demo/example/zenoh-pico-pub"
VALUE = "Pub from zenoh-python!"
INTERVAL_SEC = 1.0


with zenoh.open(zenoh.Config()) as session:
    pub = session.declare_publisher(KEY_EXPR)
    print(f"Publishing on {KEY_EXPR} every {INTERVAL_SEC}s ...")

    idx = 0
    while True:
        payload = f"[{idx:4d}] {VALUE}"
        pub.put(payload)
        print(f"Sent ('{KEY_EXPR}': '{payload}')")
        idx += 1
        time.sleep(INTERVAL_SEC)
