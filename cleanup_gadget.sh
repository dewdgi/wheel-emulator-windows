#!/bin/bash
# Cleanup old USB Gadget configuration

echo "Cleaning up old USB Gadget..."

# Unbind from UDC
if [ -f /sys/kernel/config/usb_gadget/g29wheel/UDC ]; then
    echo "" > /sys/kernel/config/usb_gadget/g29wheel/UDC 2>/dev/null || true
fi

# Remove config links
rm -f /sys/kernel/config/usb_gadget/g29wheel/configs/c.1/hid.usb0 2>/dev/null || true

# Remove config strings
rmdir /sys/kernel/config/usb_gadget/g29wheel/configs/c.1/strings/0x409 2>/dev/null || true

# Remove config
rmdir /sys/kernel/config/usb_gadget/g29wheel/configs/c.1 2>/dev/null || true

# Remove function
rmdir /sys/kernel/config/usb_gadget/g29wheel/functions/hid.usb0 2>/dev/null || true

# Remove strings
rmdir /sys/kernel/config/usb_gadget/g29wheel/strings/0x409 2>/dev/null || true

# Remove gadget
cd /sys/kernel/config/usb_gadget 2>/dev/null && rmdir g29wheel 2>/dev/null || true

echo "Cleanup complete. Now run: sudo ./wheel-emulator"
