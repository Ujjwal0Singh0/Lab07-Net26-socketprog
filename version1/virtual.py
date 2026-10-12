import time
import os

MESSAGE_FILE = "oracle_to_virtual.txt"
RESPONSE_FILE = "virtual_to_oracle.txt"

print("Virtual: Waiting for Oracle's message...")

while not os.path.exists(MESSAGE_FILE):
    time.sleep(1)

with open(MESSAGE_FILE, "r") as f:
    message = f.readline().strip()

print(f"Virtual: Received configuration: {message}")

response = f"Configuration received: {message}"

with open(RESPONSE_FILE, "w") as f:
    f.write(response + "\n")

print(f"Virtual: Sent response: {response}")
