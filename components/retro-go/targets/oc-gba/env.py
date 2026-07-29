import os

IDF_TARGET = "esp32p4"

# The Waveshare module carries an ESP32-C6 for Wi-Fi, but nothing in this handheld needs
# the network yet and leaving it out keeps the apps well inside their partitions.
os.environ["RG_TOOL_NO_NETWORKING"] = "1"
no_networking = True
