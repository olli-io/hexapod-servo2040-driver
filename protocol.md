# Hexapod Protocol for Open Servo Controllers

**Disclaimer: MSB-set byte appended to GET reply**

An additional MSB-set byte has been added at the end of the GET reply (see commit `13dea99 Add MSB-set byte at the end of GET`). This trailing byte serves as a frame delimiter so the receiver can unambiguously detect the end of a GET reply and resynchronize after errors, since command bytes are the only bytes with the MSB set. This is a deviation from the original protocol description below and should be accounted for when parsing GET replies.

This document defines a simple serial protocol to send commands and receive feedback between the Chica Server app and any open servo controller board, such as an Arduino, RPI or the Servo2040 board.

## SET command

Sent by the phone to set the value of a pin or a sequence of pins, which can be a servo pulse width for a servo pin, or a digital or analog output for output pins such as the servo power relay.

Format:

```
[1 byte: S][1 byte: Start index][1 byte: Count][2 bytes: value][2 bytes: value]...[2 bytes: value]
```

Example #1: set pin 4 to 1500

```
[S][4][1][1500]
```

Example #2: set pins 7, 8, .. , 12 to 1007, 1008, … 1012

```
[S][7][6][1007][1008][1009][1010][1011][1012]
```

Note #1: the board and the phone agree on the pin indices based on the config of the app. For example if a particular pin is set to be the servo power relay, the board will interpret the value of 0 or 1 as a digital output on the pin, rather than the actual value.

Note #2: The optimal case is when all the servos are set to consecutive pins, and we can set all of them with a single command. But even if the pins are not consecutive, the phone can optimize the commands and set the servo values in batches of consecutive pins.

## GET command

Sent by the phone to get the value of a pin or a sequence of pins, which can be a servo pulse width for a servo pin, or a sensor input such as voltage, current or the touch sensors.

Phone sends:

```
[1 byte: G][1 byte: Start index][1 byte: Count]
```

Servo controller replies:

```
[1 byte: G][1 byte: Start index][1 byte: Count][2 bytes: value][2 bytes: value]...[2 bytes: value]
```

Example #1: get the value on pin 4

```
[G][4][1]
```

Example #2: get the value on pins 20 to 25

```
[G][20][6]
```

## Command bytes vs. Value bytes

All command bytes (`S` and `G`) have their most significant bit set to 1, and all other bytes have their most significant bit cleared. This is very useful to recover from errors. As a result, only 7 bits are available for commands (total of 128 command types), and 14 bits for the data values. And we can have a total of 128 pins.

Examples:

```
S command byte: 0x53 | 0x80
G command byte: 0x47 | 0x80
```

Writing a 14 bit value to the buffer:

```c
int value;
buffer[0] = value & 0x7F;
buffer[1] = (value >> 7) & 0x7F;
```

Reading a 14 bit value from the buffer:

```c
int value = (buffer[0] & 0x7F) | ((buffer[1] & 0x7F) << 7);
```
