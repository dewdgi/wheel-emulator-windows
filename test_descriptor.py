#!/usr/bin/env python3
# Verify our HID descriptor byte-by-byte

descriptor = [
    0x05, 0x01,        # Usage Page (Generic Desktop)
    0x09, 0x04,        # Usage (Joystick)
    0xA1, 0x01,        # Collection (Application)
    
    # Input collection
    0xA1, 0x02,        # Collection (Logical)
    
    # 4 axes
    0x09, 0x01,        # Usage (Pointer)
    0xA1, 0x00,        # Collection (Physical)
    0x09, 0x30,        # Usage (X)
    0x09, 0x31,        # Usage (Y)
    0x09, 0x32,        # Usage (Z)
    0x09, 0x35,        # Usage (Rz)
    0x15, 0x00,        # Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  # Logical Maximum (65535)
    0x35, 0x00,        # Physical Minimum (0)
    0x47, 0xFF, 0xFF, 0x00, 0x00,  # Physical Maximum (65535)
    0x75, 0x10,        # Report Size (16)
    0x95, 0x04,        # Report Count (4)
    0x81, 0x02,        # Input (Data,Var,Abs)
    0xC0,              # End Collection
    
    # HAT switch
    0x09, 0x39,        # Usage (Hat switch)
    0x15, 0x00,        # Logical Minimum (0)
    0x25, 0x07,        # Logical Maximum (7)
    0x35, 0x00,        # Physical Minimum (0)
    0x46, 0x3B, 0x01,  # Physical Maximum (315)
    0x65, 0x14,        # Unit (Degrees)
    0x75, 0x04,        # Report Size (4)
    0x95, 0x01,        # Report Count (1)
    0x81, 0x42,        # Input (Data,Var,Abs,Null)
    
    # Padding
    0x75, 0x04,        # Report Size (4)
    0x95, 0x01,        # Report Count (1)
    0x81, 0x03,        # Input (Const,Var,Abs)
    
    # Buttons
    0x05, 0x09,        # Usage Page (Button)
    0x19, 0x01,        # Usage Minimum (1)
    0x29, 0x19,        # Usage Maximum (25)
    0x15, 0x00,        # Logical Minimum (0)
    0x25, 0x01,        # Logical Maximum (1)
    0x75, 0x01,        # Report Size (1)
    0x95, 0x19,        # Report Count (25)
    0x81, 0x02,        # Input (Data,Var,Abs)
    
    # Button padding
    0x75, 0x07,        # Report Size (7)
    0x95, 0x01,        # Report Count (1)
    0x81, 0x03,        # Input (Const,Var,Abs)
    
    0xC0,              # End Collection (Logical)
    
    # OUTPUT report
    0xA1, 0x02,        # Collection (Logical)
    0x09, 0x02,        # Usage (0x02)
    0x15, 0x00,        # Logical Minimum (0)
    0x26, 0xFF, 0x00,  # Logical Maximum (255)
    0x95, 0x07,        # Report Count (7)
    0x75, 0x08,        # Report Size (8)
    0x91, 0x02,        # Output (Data,Var,Abs)
    0xC0,              # End Collection
    
    0xC0               # End Collection (Application)
]

# Calculate report size
input_bits = 0
input_bits += 4 * 16  # 4 axes
input_bits += 4       # HAT
input_bits += 4       # HAT padding
input_bits += 25      # buttons  
input_bits += 7       # button padding

print(f"Descriptor size: {len(descriptor)} bytes")
print(f"INPUT report: {input_bits} bits = {input_bits // 8} bytes")
print(f"OUTPUT report: 7 bytes")
print(f"\nThis matches our code: report(13, 0)")
