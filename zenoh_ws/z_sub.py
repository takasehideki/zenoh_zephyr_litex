import time
import zenoh


def listener(sample):
    print(
        f"Received {sample.kind} ('{sample.key_expr}': '{sample.payload.to_string()}')")


with zenoh.open(zenoh.Config()) as session:
    KEY_EXPR = "demo/example/**"
    sub = session.declare_subscriber(KEY_EXPR, listener)
    print(f"Waiting for {KEY_EXPR} ...")
    while True:
        time.sleep(1)
