.. _bluetooth-periph-mcs-sample:

BLE Media Control Service Sample
#################################

Overview
********

Application to demonstrate the use of the Media Control Service (MCS) BLE profile.
This sample implements a Media Control Service server with the mandatory characteristics
as specified in the MCS ICS proforma (MCS.ICS.p5).

Mandatory Characteristics Implemented
************************************

According to the MCS ICS specification, the following characteristics are mandatory:

- Media Player Name
- Track Changed
- Track Title
- Track Duration
- Track Position
- Media State
- Media Control Point
- Media Control Point Opcodes Supported
- Content Control ID

Supported Media Control Point Opcodes
**************************************

The sample supports the following Media Control Point opcodes:

- Play
- Pause
- Stop
- Fast Forward
- Fast Rewind

At least one opcode must be supported per the MCS ICS specification (Table 3).

Optional Characteristics
**********************

The following optional characteristics are NOT implemented in this sample:

- Media Player Icon Object ID
- Media Player Icon URL
- Playback Speed
- Seeking Speed
- Current Track Object ID
- Next Track Object ID
- Current Group Object ID
- Parent Group Object ID
- Playing Order
- Playing Order Supported
- Search Results Object ID
- Search Control Point

Object Transfer Service (OTS) is not included since object ID characteristics
are optional and not required for the basic MCS test case.

Requirements
************

* Alif Balletto Development Kit

Building and Running
********************

This sample can be found under ``samples/bluetooth/le_periph_mcs`` in the
sdk-alif tree.

.. note::
   This sample uses the Alif/RivieraWaves Bluetooth host stack (``CONFIG_BT_CUSTOM=y``),
   not Zephyr's native Bluetooth stack.

When running, the sample application starts advertising and waits for a central to connect.
The device name is "ALIF_MCS".

Build command::

    west build -b alif_b1_dk_ab1c1f4m51820ph0_rtss_he samples/bluetooth/le_periph_mcs

Testing
=======

This sample is designed to pass the PTS test case IOPT/MCS/SR/GATTDB/BV-01-I,
which verifies that the MCS service is present in the GATT database with all
mandatory characteristics.

ICS Configuration
================

The ICS file used for testing should declare:

- Table 0, row 1: MCS v1.0 (Mandatory)
- Table 1, row 2: Service supported over LE
- Table 2, rows 1-10: Mandatory MCS characteristics
- Table 3, at least one row: At least one Media Control Point opcode supported
