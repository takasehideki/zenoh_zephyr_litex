import time
import zenoh


KEY_EXPR = "demo/example/**"


def listener(sample):
    print(
        f"Received {sample.kind} ('{sample.key_expr}': '{sample.payload.to_string()}')")


with zenoh.open(zenoh.Config()) as session:
    sub = session.declare_subscriber(KEY_EXPR, listener)
    print(f"Waiting for {KEY_EXPR} ...")
    while True:
        time.sleep(1)
